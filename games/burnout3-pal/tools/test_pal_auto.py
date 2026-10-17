"""Independent queue tests with synthetic reports and no PAL subprocesses."""
import copy
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import pal_auto as auto
import test_generate_batches as generator_tests


class QueueTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.game,self.tools,self.config,_,self.worker_template=generator_tests.GeneratorTests().evidence_fixture(self.root)
        self.build=self.game/'build';self.auto=self.build/'auto'
        self.pipeline_script=self.tools/'run_pal_pipeline.py';self.pipeline_script.write_text('# synthetic pipeline')
        self.source=self.tools/'batches/test.json'
        data=json.loads(self.source.read_bytes());data['schema_version']=1
        data['limits']={'batch_timeout_seconds':2};self.source.write_text(json.dumps(data))
        self.patches=[]
        for module,values in [(auto,dict(ROOT=self.root,GAME=self.game,HERE=self.tools,BUILD=self.build,AUTO=self.auto,PIPELINE=self.pipeline_script)),
                              (auto.pipeline,dict(ROOT=self.root,GAME=self.game,MANIFESTS=self.tools/'batches',RUNNER=self.tools/'run_batch.py')),
                              (auto.evidence,dict(ROOT=self.root,GAME=self.game,HERE=self.tools))]:
            p=patch.multiple(module,**values);p.start();self.addCleanup(p.stop)
        for p in [patch.object(auto.runner,'load',return_value=self.config),
                  patch.object(auto.runner,'validate',side_effect=self.validate),
                  patch.object(auto,'input_paths',return_value=[self.config.elf_path,self.config.database_path,
                       self.game/'game.json',self.tools/'run_batch.py',self.pipeline_script]),
                  patch.object(auto,'launch',side_effect=self.fake_launch)]:
            p.start();self.addCleanup(p.stop)
        self.calls=[];self.outcomes=[]

    def validate(self,manifest):
        if manifest.get('schema_version')!=1:raise ValueError('Synthetic schema rejection')
        return self.config,{'status':'validated_only','dynamic_execution':False}

    def plan(self,extra=None):
        return auto.create_plan([self.source]+(extra or []))

    def load(self,path):
        return json.loads(path.read_bytes())

    def running_checkpoint(self,path,containment=None):
        plan=self.load(path);job=plan['jobs'][0]
        directory=path.parent/'j000-a1';directory.mkdir(exist_ok=True)
        job.update(status='running',attempts=[dict(number=1,status='running',started_at=auto.now(),
            directory=str(directory),pipeline_before=[],containment=containment or {},report=None,
            logs={s:str(directory/(s+'.log')) for s in ('stdout','stderr')})])
        plan['status']='running';auto.atomic_json(path,plan)
        return plan

    def fake_launch(self,config_path,seconds,row):
        self.calls.append(config_path)
        row.update(cleanup_succeeded=True,active_processes_after_cleanup=0,timeout_detected=False,exit_code=0)
        outcome=self.outcomes.pop(0) if self.outcomes else 'passed'
        if outcome=='interrupted':raise KeyboardInterrupt()
        if outcome=='missing':return
        config=self.load(config_path);snapshot=Path(config['manifest']);manifest=self.load(snapshot)
        number=len(self.calls);out=self.build/'pipeline'/('synthetic-%d'%number);out.mkdir(parents=True)
        for name in ('stdout.log','stderr.log'):(config_path.parent/name).write_text('synthetic output')
        if outcome!='passed':
            summary={'mode':'execute','selected_batches':[manifest['id']],'status':'failed',
                     'batches':[{'id':manifest['id'],'status':'failed','functions':[
                         {'address':f['address'],'status':outcome} for f in manifest['functions']]}]}
            (out/'summary.json').write_text(json.dumps(summary));row['exit_code']=1;return
        execution=self.build/'batches'/manifest['id']/('run-%d'%number);execution.mkdir(parents=True)
        executed=execution/'manifest.json';executed.write_bytes(snapshot.read_bytes())
        workers=[];entries=[]
        for entry in manifest['functions']:
            address=entry['address'];directory=execution/('function-'+address[2:]);directory.mkdir()
            dll=directory/'native.dll';dll.write_bytes(b'synthetic binary placeholder')
            report=copy.deepcopy(self.worker_template)
            report.update(address=address,dll=str(dll))
            report['admission']['address']=int(address,16)
            report['sha256']={str(p):auto.pipeline.digest(p) for p in (self.config.elf_path,self.config.database_path,
                self.game/'game.json',self.tools/'run_batch.py',executed)}
            wp=directory/'worker.json';wp.write_text(json.dumps(report))
            wr=dict(address=address,status='passed',cases_executed=8,cases_passed=8,worker_report=str(wp),
                    exit_code=0,cleanup_succeeded=True,active_processes_after_cleanup=0,timeout_detected=False)
            workers.append(wr);entries.append({k:wr[k] for k in ('address','status','cases_executed','cases_passed','worker_report')})
        total=8*len(workers)
        supervisor=dict(status='passed',expected_functions=len(workers),expected_cases=total,cases_executed=total,
                        cases_passed=total,manifest_sha256=auto.pipeline.digest(executed),results=workers)
        sp=execution/'supervisor.json';sp.write_text(json.dumps(supervisor))
        vp=execution/'results.json';vp.write_text(json.dumps({'status':'validated_only','dynamic_execution':False,
            'manifest':manifest,'function_count':len(workers),'case_count':total,
            'addresses':[int(f['address'],16) for f in manifest['functions']],
            'sha256':{str(p):auto.pipeline.digest(p) for p in (snapshot,self.tools/'run_batch.py',
                self.game/'game.json',self.config.elf_path,self.config.database_path)}}))
        summary=dict(status='passed',mode='execute',selected_batches=[manifest['id']],cases_executed=total,
                     sha256={auto.pipeline.path_label(p):auto.pipeline.digest(p) for p in
                             (snapshot,self.pipeline_script,self.tools/'run_batch.py',self.game/'game.json')},
                     batches=[dict(id=manifest['id'],status='passed',functions=entries,
                         validation=dict(status='validated_only',exit_code=0,report=str(vp),report_sha256=auto.pipeline.digest(vp)),
                         execution=dict(status='passed',exit_code=0,dynamic_execution=True,report=str(sp),
                                        report_sha256=auto.pipeline.digest(sp),cases_executed=total,cases_passed=total))])
        (out/'summary.json').write_text(json.dumps(summary))

    def test_create_plan_preserves_sources_and_never_promotes(self):
        before=self.source.read_bytes();files=set((self.tools/'batches').iterdir())
        path=self.plan();p=self.load(path)
        self.assertEqual(p['schema_version'],1);self.assertEqual(p['jobs'][0]['status'],'pending')
        self.assertEqual(p['jobs'][0]['attempts'],[]);self.assertEqual(self.calls,[])
        self.assertEqual(before,self.source.read_bytes());self.assertEqual(files,set((self.tools/'batches').iterdir()))
        self.assertTrue(path.is_relative_to(self.build))

    def test_plan_rejects_id_collisions_before_writing(self):
        second=self.root/'other.json';d=self.load(self.source);d['id']='TEST';second.write_text(json.dumps(d))
        with self.assertRaisesRegex(ValueError,'Conflicting Windows'):self.plan([second])
        self.assertFalse(self.auto.exists());self.assertEqual(self.calls,[])

    def test_atomic_replace_failure_keeps_previous_complete_json(self):
        path=self.plan();before=path.read_bytes()
        with patch.object(auto.os,'replace',side_effect=OSError('synthetic interruption')):
            with self.assertRaises(OSError):auto.atomic_json(path,{'broken':'new value'})
        self.assertEqual(path.read_bytes(),before);self.assertEqual(self.load(path)['schema_version'],1)
        self.assertTrue(list(path.parent.glob('*.tmp')))

    def test_hash_incompatibility_prevents_launch(self):
        path=self.plan();self.config.elf_path.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'Incompatible'):auto.execute_plan(path)
        self.assertEqual(self.calls,[])
        self.assertEqual(self.load(path)['events'][-1]['stage'],'compatibility')

    def test_missing_environment_reference_rejected(self):
        path=self.plan();p=self.load(path);p['inputs'].pop();auto.atomic_json(path,p)
        with self.assertRaisesRegex(ValueError,'environment references'):auto.execute_plan(path)
        self.assertEqual(self.calls,[])

    def test_passed_without_attempts_is_controlled_and_preserves_corrupt_plan(self):
        path=self.plan();p=self.load(path);p['jobs'][0]['status']='passed';auto.atomic_json(path,p)
        before=path.read_bytes()
        with self.assertRaisesRegex(auto.InvalidPlan,'passed job has no attempts'):
            auto.execute_plan(path,resume=True)
        stderr=io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(auto.main(['--resume',str(path)]),1)
        self.assertIn('passed job has no attempts',stderr.getvalue())
        self.assertEqual(path.read_bytes(),before);self.assertEqual(self.calls,[])

    def test_semantically_corrupt_plans_rejected_without_repair(self):
        path=self.plan();valid=self.running_checkpoint(path)
        mutations=[('root type',lambda p:[]),
            ('jobs type',lambda p:p.update(jobs='bad')),
            ('input collection type',lambda p:p.update(inputs=None)),
            ('unknown plan state',lambda p:p.update(status='invented')),
            ('unknown job state',lambda p:p['jobs'][0].update(status='invented')),
            ('unknown function state',lambda p:p['jobs'][0]['functions'][0].update(status='invented')),
            ('unknown attempt state',lambda p:p['jobs'][0]['attempts'][0].update(status='invented')),
            ('attempt list type',lambda p:p['jobs'][0].update(attempts=None)),
            ('attempt identity/order',lambda p:p['jobs'][0]['attempts'][0].update(number=2)),
            ('boolean attempt number',lambda p:p['jobs'][0]['attempts'][0].update(number=True)),
            ('missing attempt identity',lambda p:p['jobs'][0]['attempts'][0].pop('number')),
            ('pending with attempts',lambda p:p['jobs'][0].update(status='pending')),
            ('job/attempt mismatch',lambda p:p['jobs'][0].update(status='compile_error')),
            ('missing terminal fields',lambda p:(p['jobs'][0].update(status='compile_error'),
                                              p['jobs'][0]['attempts'][0].update(status='compile_error'))),
            ('running already finished',lambda p:p['jobs'][0]['attempts'][0].update(finished_at=auto.now())),
            ('bad logs',lambda p:p['jobs'][0]['attempts'][0].update(logs=[])),
            ('bad containment',lambda p:p['jobs'][0]['attempts'][0].update(containment=[])),
            ('nonboolean timeout',lambda p:p['jobs'][0]['attempts'][0]['containment'].update(timeout_detected=1)),
            ('wrong timestamp',lambda p:p['jobs'][0]['attempts'][0].update(started_at='bad')),
            ('invalid input hash',lambda p:p['inputs'][0].update(sha256='bad')),
            ('bad diagnostic events',lambda p:p.update(events='bad')),
            ('missing retry description',lambda p:p['retry_policy'].pop('equivalence')),
            ('too many attempts',lambda p:p['jobs'][0]['attempts'].extend([copy.deepcopy(p['jobs'][0]['attempts'][0])]*2)),
            ('planned with attempts',lambda p:p.update(status='planned')),
            ('passed with running jobs',lambda p:p.update(status='passed')),
            ('attempt directory conflict',lambda p:p['jobs'][0]['attempts'][0].update(directory=str(path.parent/'other')))]
        for label,mutate in mutations:
            with self.subTest(label=label):
                p=copy.deepcopy(valid);result=mutate(p)
                if label=='root type':p=result
                auto.atomic_json(path,p);before=path.read_bytes()
                with self.assertRaises(auto.InvalidPlan):auto.execute_plan(path,resume=True)
                self.assertEqual(path.read_bytes(),before);self.assertEqual(self.calls,[])

    def test_passed_attempt_requires_report_and_evidence_map(self):
        path=self.plan();auto.execute_plan(path);valid=self.load(path)
        for label,mutate in [('no report',lambda a:a.update(report=None)),
                             ('no evidence map',lambda a:a.pop('evidence_sha256')),
                             ('empty evidence map',lambda a:a.update(evidence_sha256={})),
                             ('timeout conflicts with passed',lambda a:a['containment'].update(timeout_detected=True))]:
            with self.subTest(label=label):
                p=copy.deepcopy(valid);mutate(p['jobs'][0]['attempts'][0]);auto.atomic_json(path,p);before=path.read_bytes()
                with self.assertRaises(auto.InvalidPlan):auto.execute_plan(path,resume=True)
                self.assertEqual(path.read_bytes(),before);self.assertEqual(len(self.calls),1)

    def test_success_reused_only_after_complete_evidence_validation(self):
        path=self.plan();self.assertEqual(auto.execute_plan(path),0)
        p=self.load(path);self.assertEqual(p['jobs'][0]['status'],'passed')
        self.assertTrue(p['jobs'][0]['attempts'][0]['evidence_sha256'])
        self.assertEqual(auto.execute_plan(path,resume=True),0);self.assertEqual(len(self.calls),1)
        self.assertEqual(self.source.read_bytes(),Path(p['jobs'][0]['snapshot']['path']).read_bytes())
        self.assertEqual(list((self.tools/'batches').glob('*.json')),[self.source])

    def test_summary_alone_never_passes(self):
        path=self.plan()
        original=self.fake_launch
        def incomplete(config,seconds,row):
            original(config,seconds,row)
            summary=next((self.build/'pipeline').glob('*/summary.json'));d=self.load(summary)
            d['batches'][0]['functions']=[];summary.write_text(json.dumps(d))
        with patch.object(auto,'launch',side_effect=incomplete):self.assertEqual(auto.execute_plan(path),1)
        self.assertEqual(self.load(path)['jobs'][0]['status'],'infrastructure_error')

    def test_complete_counts_do_not_hide_divergent_worker_results(self):
        path=self.plan();original=self.fake_launch
        def corrupt(config,seconds,row):
            original(config,seconds,row)
            report=next((self.build/'batches/test').glob('run-*/function-*/worker.json'))
            d=self.load(report);d['cases'][0]['results']['native']=99;report.write_text(json.dumps(d))
        with patch.object(auto,'launch',side_effect=corrupt):self.assertEqual(auto.execute_plan(path),1)
        self.assertEqual(self.load(path)['jobs'][0]['status'],'infrastructure_error')

    def test_changed_success_artifact_not_reused_or_retried(self):
        path=self.plan();auto.execute_plan(path)
        p=self.load(path);attempt=p['jobs'][0]['attempts'][0]
        dll=next(Path(name) for name in attempt['evidence_sha256'] if name.endswith('.dll'));dll.write_bytes(b'changed')
        self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=True),1)
        self.assertTrue(self.load(path)['jobs'][0]['evidence_invalid']);self.assertEqual(len(self.calls),1)

    def test_missing_dll_pin_never_recreated_even_when_dll_changed(self):
        path=self.plan();self.assertEqual(auto.execute_plan(path),0)
        p=self.load(path);attempt=p['jobs'][0]['attempts'][0]
        dll=next(name for name in attempt['evidence_sha256'] if name.endswith('.dll'))
        del attempt['evidence_sha256'][dll];Path(dll).write_bytes(b'changed DLL without saved pin')
        auto.atomic_json(path,p)
        self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=True),1)
        job=self.load(path)['jobs'][0]
        self.assertEqual(job['status'],'infrastructure_error');self.assertTrue(job['evidence_invalid'])
        self.assertNotIn(dll,job['attempts'][0]['evidence_sha256']);self.assertEqual(len(self.calls),1)

    def test_every_required_saved_pin_is_mandatory(self):
        for suffix in ('.dll','worker.json','supervisor.json','results.json','manifest.json','summary.json','stdout.log','stderr.log'):
            with self.subTest(suffix=suffix):
                path=self.plan();self.assertEqual(auto.execute_plan(path),0)
                p=self.load(path);pins=p['jobs'][0]['attempts'][0]['evidence_sha256']
                key=next(name for name in pins if name.endswith(suffix));del pins[key]
                auto.atomic_json(path,p);calls=len(self.calls)
                self.assertEqual(auto.execute_plan(path,resume=True),1)
                self.assertNotEqual(self.load(path)['jobs'][0]['status'],'passed')
                self.assertNotIn(key,self.load(path)['jobs'][0]['attempts'][0]['evidence_sha256'])
                self.assertEqual(len(self.calls),calls)

    def test_missing_success_log_prevents_reuse(self):
        path=self.plan();auto.execute_plan(path);p=self.load(path)
        Path(p['jobs'][0]['attempts'][0]['logs']['stdout']).unlink()
        self.assertEqual(auto.execute_plan(path,resume=True),1)
        self.assertTrue(self.load(path)['jobs'][0]['evidence_invalid'])

    def test_resume_after_clean_interruption_requires_retry_flag(self):
        path=self.plan();self.outcomes=['interrupted']
        self.assertEqual(auto.execute_plan(path),1)
        self.assertEqual(self.load(path)['jobs'][0]['status'],'interrupted')
        self.assertEqual(auto.execute_plan(path,resume=True),1);self.assertEqual(len(self.calls),1)
        self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=True),0)
        self.assertEqual(len(self.load(path)['jobs'][0]['attempts']),2)

    def test_running_without_conclusive_outcome_becomes_interrupted(self):
        path=self.plan();self.running_checkpoint(path)
        self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=True),1)
        job=self.load(path)['jobs'][0]
        self.assertEqual(job['status'],'interrupted');self.assertEqual(len(job['attempts']),1);self.assertEqual(self.calls,[])

    def test_keyboard_interrupt_without_timeout_preserves_interrupted(self):
        path=self.plan();containment=dict(cleanup_succeeded=True,active_processes_after_cleanup=0,
                                         timeout_detected=False,exit_code=1,win32_api=[{'api':'synthetic'}])
        def interrupt(config,seconds,row):
            self.calls.append(config);row.update(copy.deepcopy(containment));raise KeyboardInterrupt()
        with patch.object(auto,'launch',side_effect=interrupt):
            self.assertEqual(auto.execute_plan(path),1)
        plan=self.load(path);auto.plan_structure(plan)
        job=plan['jobs'][0];attempt=job['attempts'][0]
        self.assertEqual(job['status'], 'interrupted');self.assertEqual(attempt['status'],'interrupted')
        self.assertEqual(attempt['containment'],containment)
        self.assertEqual(auto.execute_plan(path,resume=True),1)
        self.assertEqual(self.load(path)['jobs'][0]['attempts'],job['attempts'])
        self.assertEqual(len(self.calls),1)

    def test_keyboard_interrupt_after_timeout_is_consistent_and_never_retried(self):
        second=self.root/'next.json';data=self.load(self.source);data['id']='zz-next'
        second.write_text(json.dumps(data))
        path=self.plan([second]);containment=dict(cleanup_succeeded=True,active_processes_after_cleanup=0,
                                                 timeout_detected=True,exit_code=1,win32_api=[{'api':'synthetic'}])
        def interrupt(config,seconds,row):
            self.calls.append(config);row.update(copy.deepcopy(containment));raise KeyboardInterrupt()
        with patch.object(auto,'launch',side_effect=interrupt):
            self.assertEqual(auto.execute_plan(path),1)
        plan=self.load(path);auto.plan_structure(plan);job=plan['jobs'][0]
        self.assertEqual(job['status'],'timeout');self.assertEqual(job['attempts'][0]['status'],'timeout')
        self.assertEqual(job['attempts'][0]['containment'],containment)
        self.assertEqual(job['failure_conditions'],['timeout']);self.assertFalse(job['stop_queue'])
        self.assertEqual(plan['jobs'][1]['status'],'pending');self.assertEqual(len(self.calls),1)
        history=copy.deepcopy(job['attempts'])
        # The untouched next batch may run on explicit resume, never the timed-out batch.
        self.assertEqual(auto.execute_plan(path,resume=True),1)
        self.assertEqual(len(self.calls),2)
        self.assertEqual(self.load(path)['jobs'][0]['attempts'],history)
        self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=True),1)
        self.assertEqual(len(self.calls),2);auto.plan_structure(self.load(path))

    def test_keyboard_interrupt_timeout_incomplete_cleanup_blocks_queue(self):
        for cleanup in ({},{'cleanup_succeeded':False},
                        {'cleanup_succeeded':True,'active_processes_after_cleanup':1}):
            with self.subTest(cleanup=cleanup):
                second=self.root/'next.json';data=self.load(self.source);data['id']='zz-next'
                second.write_text(json.dumps(data))
                path=self.plan([second]);containment=dict(timeout_detected=True,win32_api=[{'api':'synthetic'}],**cleanup)
                def interrupt(config,seconds,row):
                    self.calls.append(config);row.update(copy.deepcopy(containment));raise KeyboardInterrupt()
                before=len(self.calls)
                with patch.object(auto,'launch',side_effect=interrupt):auto.execute_plan(path)
                plan=self.load(path);auto.plan_structure(plan);job=plan['jobs'][0]
                self.assertEqual(job['status'],'timeout');self.assertEqual(job['attempts'][0]['status'],'timeout')
                self.assertEqual(job['attempts'][0]['containment'],containment)
                self.assertTrue(job['stop_queue']);self.assertTrue(job['attempts'][0]['stop_queue'])
                self.assertEqual(job['failure_conditions'],['timeout','infrastructure_error'])
                history=copy.deepcopy(job['attempts'])
                for retry in (False,True):
                    self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=retry),1)
                    plan=self.load(path);auto.plan_structure(plan)
                    self.assertEqual(plan['jobs'][0]['attempts'],history)
                    self.assertEqual(plan['jobs'][1]['status'],'pending')
                self.assertEqual(len(self.calls),before+1)

    def test_recover_persisted_timeout_incomplete_cleanup_preserves_history(self):
        path=self.plan();containment=dict(timeout_detected=True,cleanup_succeeded=False,
                                         win32_api=[{'api':'synthetic'}])
        original=self.running_checkpoint(path,copy.deepcopy(containment))['jobs'][0]['attempts'][0]
        self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=True),1)
        plan=self.load(path);auto.plan_structure(plan);job=plan['jobs'][0];attempt=job['attempts'][0]
        self.assertEqual(job['status'],'timeout');self.assertEqual(attempt['status'],'timeout')
        self.assertEqual(attempt['containment'],containment);self.assertTrue(job['stop_queue'])
        for key in ('number','started_at','directory','pipeline_before','report','logs'):
            self.assertEqual(attempt[key],original[key])
        history=copy.deepcopy(job['attempts'])
        for retry in (False,True):
            self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=retry),1)
            self.assertEqual(self.load(path)['jobs'][0]['attempts'],history)
        self.assertEqual(self.calls,[])

    def test_recover_persisted_timeout_never_retries(self):
        path=self.plan();self.running_checkpoint(path,dict(cleanup_succeeded=True,active_processes_after_cleanup=0,
                                                         timeout_detected=True,exit_code=1))
        self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=True),1)
        job=self.load(path)['jobs'][0]
        self.assertEqual(job['status'],'timeout');self.assertEqual(job['attempts'][0]['status'],'timeout')
        self.assertTrue(job['attempts'][0]['containment']['timeout_detected'])
        self.assertEqual(len(job['attempts']),1);self.assertEqual(self.calls,[])
        auto.execute_plan(path,resume=True,retry_infrastructure=True);self.assertEqual(self.calls,[])

    def test_persisted_timeout_wins_over_existing_success_summary(self):
        path=self.plan();auto.execute_plan(path);p=self.load(path);job=p['jobs'][0];attempt=job['attempts'][0]
        job['status']=attempt['status']='running';attempt.pop('finished_at');p['status']='running'
        attempt['containment'].update(timeout_detected=True,exit_code=1);old_report=attempt['report']
        auto.atomic_json(path,p)
        self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=True),1)
        job=self.load(path)['jobs'][0];self.assertEqual(job['status'],'timeout')
        self.assertEqual(job['attempts'][0]['report'],old_report);self.assertEqual(len(self.calls),1)

    def test_recover_running_with_complete_clean_evidence(self):
        path=self.plan();self.assertEqual(auto.execute_plan(path),0)
        p=self.load(path);job=p['jobs'][0];job['status']='running';job['attempts'][0]['status']='running'
        job['attempts'][0].pop('evidence_sha256');job['attempts'][0].pop('finished_at');p['status']='running'
        auto.atomic_json(path,p)
        self.assertEqual(auto.execute_plan(path,resume=True),0)
        self.assertEqual(self.load(path)['jobs'][0]['status'],'passed');self.assertEqual(len(self.calls),1)

    def test_running_without_cleanup_blocks_later_pending_jobs(self):
        second=self.root/'other.json';d=self.load(self.source);d['id']='second';second.write_text(json.dumps(d))
        path=self.plan([second]);self.running_checkpoint(path)
        self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=True),1)
        self.assertEqual(self.calls,[]);self.assertEqual(self.load(path)['jobs'][1]['status'],'pending')

    def test_no_automatic_equivalence_retry(self):
        path=self.plan();self.outcomes=['mismatch']
        self.assertEqual(auto.execute_plan(path),1)
        self.assertEqual(self.load(path)['jobs'][0]['status'],'equivalence_failed')
        self.assertEqual(auto.execute_plan(path,resume=True,retry_infrastructure=True),1)
        self.assertEqual(len(self.calls),1)

    def test_classification_preserves_mixed_failures_with_structural_priority(self):
        for statuses,expected in [(['mismatch'],'equivalence_failed'),(['structural_error'],'infrastructure_error'),
                                  (['mismatch','structural_error'],'infrastructure_error')]:
            with self.subTest(statuses=statuses):
                summary=dict(mode='execute',selected_batches=['test'],batches=[dict(id='test',functions=[
                    dict(address='0x%08X'%(0x1000+i*0x1000),status=s) for i,s in enumerate(statuses)])])
                self.assertEqual(auto.classify_summary(summary,{'id':'test'})[0],expected)
                if len(statuses)==2:self.assertEqual(auto.failure_conditions(summary,{'id':'test'}),
                                                    ['infrastructure_error','equivalence_failed'])

    def test_structural_batch_metadata_also_has_priority(self):
        summary=dict(mode='execute',selected_batches=['test'],batches=[dict(id='test',status='structural_error',
            functions=[dict(address='0x00001000',status='mismatch')])])
        self.assertEqual(auto.classify_summary(summary,{'id':'test'})[0],'infrastructure_error')
        self.assertEqual(auto.failure_conditions(summary,{'id':'test'}),['infrastructure_error','equivalence_failed'])

    def test_mixed_failure_stops_queue_and_cannot_retry_equivalence(self):
        manifest=self.load(self.source);function=copy.deepcopy(manifest['functions'][0]);function['address']='0x00002000'
        manifest['functions'].append(function);self.source.write_text(json.dumps(manifest))
        second=self.root/'other.json';other=copy.deepcopy(manifest);other['id']='zz-next';second.write_text(json.dumps(other))
        path=self.plan([second]);self.outcomes=['mismatch'];original=self.fake_launch
        def mixed(config,seconds,row):
            original(config,seconds,row)
            report=next((self.build/'pipeline').glob('*/summary.json'));d=self.load(report)
            d['batches'][0]['functions'][0]['status']='structural_error';report.write_text(json.dumps(d))
        with patch.object(auto,'launch',side_effect=mixed):self.assertEqual(auto.execute_plan(path),1)
        p=self.load(path);job=p['jobs'][0]
        self.assertEqual(job['status'],'infrastructure_error');self.assertTrue(job['stop_queue'])
        self.assertEqual(job['failure_conditions'],['infrastructure_error','equivalence_failed'])
        self.assertEqual([f['status'] for f in job['functions']],['structural_error','equivalence_failed'])
        self.assertEqual(job['attempts'][0]['failure_conditions'],job['failure_conditions'])
        self.assertEqual(p['jobs'][1]['status'],'pending');self.assertEqual(len(self.calls),1)
        auto.execute_plan(path,resume=True,retry_infrastructure=True)
        self.assertEqual(len(self.calls),1);self.assertEqual(self.load(path)['jobs'][1]['status'],'pending')

    def test_structural_failure_blocks_queue_and_has_bounded_explicit_retry(self):
        second=self.root/'other.json';d=self.load(self.source);d['id']='second';second.write_text(json.dumps(d))
        path=self.plan([second]);self.outcomes=['structural_error','structural_error']
        auto.execute_plan(path);self.assertEqual(len(self.calls),1)
        auto.execute_plan(path,resume=True);self.assertEqual(len(self.calls),1)
        auto.execute_plan(path,resume=True,retry_infrastructure=True);self.assertEqual(len(self.calls),2)
        auto.execute_plan(path,resume=True,retry_infrastructure=True);self.assertEqual(len(self.calls),2)
        self.assertEqual(self.load(path)['jobs'][1]['status'],'pending')

    def test_individual_compile_failure_continues_sequentially(self):
        second=self.root/'other.json';d=self.load(self.source);d['id']='second';second.write_text(json.dumps(d))
        path=self.plan([second]);self.outcomes=['compile_error','passed']
        self.assertEqual(auto.execute_plan(path),1);self.assertEqual(len(self.calls),2)
        self.assertEqual([j['status'] for j in self.load(path)['jobs']],['compile_error','passed'])

    def test_concurrent_plan_and_global_queue_locks(self):
        path=self.plan()
        for target in (self.auto/'queue.lock',path.parent/'plan.lock'):
            with self.subTest(target=target),auto.lock(target):
                with self.assertRaisesRegex(ValueError,'locked'):auto.execute_plan(path)
        self.assertEqual(self.calls,[])

    def test_static_rejection_never_launches(self):
        d=self.load(self.source);d['schema_version']=99;self.source.write_text(json.dumps(d))
        path=self.plan();self.assertEqual(self.load(path)['jobs'][0]['status'],'static_rejected')
        self.assertEqual(auto.execute_plan(path),1);self.assertEqual(self.calls,[])

    def test_no_report_or_zero_exit_is_infrastructure_failure(self):
        path=self.plan();self.outcomes=['missing']
        self.assertEqual(auto.execute_plan(path),1)
        self.assertEqual(self.load(path)['jobs'][0]['status'],'infrastructure_error')

    def test_timeout_is_not_retried(self):
        path=self.plan()
        def timeout(config,seconds,row):
            self.calls.append(config)
            row.update(cleanup_succeeded=True,active_processes_after_cleanup=0,timeout_detected=True,exit_code=1)
        with patch.object(auto,'launch',side_effect=timeout):auto.execute_plan(path)
        self.assertEqual(self.load(path)['jobs'][0]['status'],'timeout')
        auto.execute_plan(path,resume=True,retry_infrastructure=True);self.assertEqual(len(self.calls),1)

    def test_failed_cleanup_is_infrastructure_and_never_retried(self):
        path=self.plan()
        def failed(config,seconds,row):
            self.calls.append(config);row.update(cleanup_succeeded=False,timeout_detected=False,exit_code=0)
        with patch.object(auto,'launch',side_effect=failed):auto.execute_plan(path)
        self.assertEqual(self.load(path)['jobs'][0]['status'],'infrastructure_error')
        auto.execute_plan(path,resume=True,retry_infrastructure=True);self.assertEqual(len(self.calls),1)

    def test_writes_outside_build_rejected(self):
        with self.assertRaisesRegex(ValueError,'outside'):auto.atomic_json(self.root/'bad.json',{})
        self.assertFalse((self.root/'bad.json').exists())

    def test_worker_delegates_to_pipeline_and_captures_both_streams(self):
        path=self.plan();snapshot=self.load(path)['jobs'][0]['snapshot']
        directory=path.parent/'synthetic-child';directory.mkdir()
        config=directory/'launch.json';auto.atomic_json(config,dict(manifest=snapshot['path'],manifest_sha256=snapshot['sha256']))
        def pipeline_main(args):
            import sys
            print('synthetic stdout');print('synthetic stderr',file=sys.stderr);return 7
        with patch.object(auto.pipeline,'main',side_effect=pipeline_main) as delegate:
            self.assertEqual(auto.worker(config),7)
        delegate.assert_called_once_with(['--manifest',snapshot['path'],'--execute','--stop-on-error'])
        self.assertIn('synthetic stdout',(directory/'stdout.log').read_text())
        self.assertIn('synthetic stderr',(directory/'stderr.log').read_text())


if __name__=='__main__':
    unittest.main()
