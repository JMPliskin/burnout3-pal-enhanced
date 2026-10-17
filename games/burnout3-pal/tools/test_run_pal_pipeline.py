"""Synthetic pipeline tests. Runner subprocesses are simulated; no guest code."""
import contextlib
import io
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import run_pal_pipeline as pipeline


class PipelineTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.game=self.root/'game'
        self.manifests=self.game/'tools/batches';self.manifests.mkdir(parents=True)
        self.runner=self.game/'tools/run_batch.py';self.runner.write_text('# synthetic runner')
        (self.game/'game.json').write_text('{}')
        self.calls=[];self.counter=0
        self.patcher=patch.multiple(pipeline,ROOT=self.root,GAME=self.game,MANIFESTS=self.manifests,RUNNER=self.runner)
        self.patcher.start();self.addCleanup(self.patcher.stop)
        self.subprocess=patch.object(pipeline.subprocess,'run',side_effect=self.fake_runner)
        self.subprocess.start();self.addCleanup(self.subprocess.stop)

    def manifest(self,name,directory=None):
        directory=directory or self.root/'generated';directory.mkdir(parents=True,exist_ok=True)
        path=directory/(name+'.json')
        path.write_text(json.dumps({'id':name,'schema_version':1,'functions':[]}))
        return path

    def fake_runner(self,command,**kwargs):
        """Exercise real invoke/report discovery while replacing only subprocess."""
        self.calls.append(command)
        path=Path(command[command.index('--manifest')+1]);data=json.loads(path.read_bytes())
        validate='--validate-only' in command
        if data.get('schema_version')!=1:
            kwargs['stderr'].write('Synthetic runner rejected schema')
            return SimpleNamespace(returncode=1)
        self.counter+=1
        output=self.game/'build/batches'/data['id']/('synthetic-%d'%self.counter)
        output.mkdir(parents=True)
        report={'status':'validated_only','dynamic_execution':False,'case_count':0} if validate else {
            'status':'passed','results':[],'cases_passed':0,'cases_executed':0}
        (output/('results.json' if validate else 'supervisor.json')).write_text(json.dumps(report))
        return SimpleNamespace(returncode=0)

    def run_main(self,args):
        with contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            return pipeline.main(args)

    def summary(self):
        return json.loads(next((self.game/'build/pipeline').glob('*/summary.json')).read_bytes())

    def test_explicit_valid_path_and_trace(self):
        path=self.manifest('local');before=path.read_bytes()
        self.assertEqual(self.run_main(['--manifest',str(path),'--validate-only']),0)
        self.assertEqual(self.calls[0][self.calls[0].index('--manifest')+1],str(path.resolve()))
        self.assertIn('--validate-only',self.calls[0])
        report=self.summary();self.assertEqual(report['status'],'validated_only')
        self.assertEqual(report['batches'][0]['manifest'],path.relative_to(self.root).as_posix())
        self.assertEqual(report['sha256'][path.relative_to(self.root).as_posix()],pipeline.digest(path))
        self.assertEqual(path.read_bytes(),before);self.assertEqual(list(self.manifests.glob('*.json')),[])

    def test_multiple_manifests_sorted_by_id(self):
        z=self.manifest('z');a=self.manifest('a')
        self.assertEqual(self.run_main(['--manifest',str(z),'--manifest',str(a),'--validate-only']),0)
        self.assertEqual(self.summary()['selected_batches'],['a','z'])
        self.assertEqual(len(self.calls),2)

    def test_nonexistent_path(self):
        self.assertEqual(self.run_main(['--manifest',str(self.root/'missing.json'),'--execute']),1)
        self.assertEqual(self.calls,[])

    def test_invalid_json(self):
        path=self.root/'bad.json';path.write_text('{')
        self.assertEqual(self.run_main(['--manifest',str(path),'--execute']),1)
        self.assertEqual(self.calls,[])

    def test_duplicate_path_alias(self):
        path=self.manifest('local');alias=path.parent/'..'/path.parent.name/path.name
        self.assertEqual(self.run_main(['--manifest',str(path),'--manifest',str(alias),'--execute']),1)
        self.assertEqual(self.calls,[])

    def test_duplicate_ids(self):
        first=self.manifest('local');second=self.manifest('local',self.root/'other')
        self.assertEqual(self.run_main(['--manifest',str(first),'--manifest',str(second),'--execute']),1)
        self.assertEqual(self.calls,[])

    def id_files(self,ids,directory=None):
        directory=directory or self.root/'id-inputs';directory.mkdir(parents=True,exist_ok=True)
        paths=[]
        for index,name in enumerate(ids):
            path=directory/('input-%d.json'%index)
            path.write_text(json.dumps({'id':name,'schema_version':1,'functions':[]}));paths.append(path)
        return paths

    def test_casefold_collision_rejected_before_any_runner_call(self):
        paths=self.id_files(['CASE-TEST','case-test'])
        args=[value for path in paths for value in ('--manifest',str(path))]
        for mode in ('--validate-only','--execute'):
            with self.subTest(mode=mode):
                self.assertEqual(self.run_main(args+[mode]),1)
                self.assertEqual(self.calls,[])
                self.assertFalse((self.game/'build').exists())

    def test_late_collision_rejects_entire_selection(self):
        paths=self.id_files(['first','second','third','FIRST'])
        args=[value for path in paths for value in ('--manifest',str(path))]
        self.assertEqual(self.run_main(args+['--execute']),1)
        self.assertEqual(self.calls,[]);self.assertFalse((self.game/'build').exists())

    def test_distinct_windows_ids_preserve_original_spelling(self):
        paths=self.id_files(['Alpha','Beta'])
        self.assertEqual(list(pipeline.select_manifests(None,paths)),['Alpha','Beta'])
        self.assertEqual(pipeline.windows_id_key('CASE-TEST'),pipeline.windows_id_key('case-test'))
        self.assertNotEqual(pipeline.windows_id_key('Alpha'),pipeline.windows_id_key('Beta'))
        args=[value for path in paths for value in ('--manifest',str(path))]
        self.assertEqual(self.run_main(args+['--validate-only']),0)
        self.assertEqual(len(self.calls),2)

    def test_discovery_also_rejects_casefold_collision(self):
        self.id_files(['CASE-TEST','case-test'],self.manifests)
        self.assertEqual(self.run_main(['--batches','CASE-TEST','case-test','--execute']),1)
        self.assertEqual(self.calls,[]);self.assertFalse((self.game/'build').exists())

    def test_reserved_and_invalid_windows_components(self):
        names=['CON','prn','Aux','nul']+['%s%d'%(prefix,n) for prefix in ('COM','lpt') for n in range(1,10)]
        names+=['CON.txt','name.','name ','a/b','a\\b','a:b','COM\u00b9','x'*256]
        for name in names:
            with self.subTest(name=name):
                path=self.id_files([name])[0]
                self.assertEqual(self.run_main(['--manifest',str(path),'--execute']),1)
                self.assertEqual(self.calls,[]);self.assertFalse((self.game/'build').exists())
        for name in ('CON-test','COM10','LPT0','batch-001','generated-seed-1','x'*255):
            with self.subTest(valid=name):self.assertEqual(pipeline.windows_id_key(name),name.casefold())

    def test_discovery_rejects_reserved_name(self):
        self.id_files(['CON'],self.manifests)
        self.assertEqual(self.run_main(['--batches','CON','--execute']),1)
        self.assertEqual(self.calls,[]);self.assertFalse((self.game/'build').exists())

    def test_conflicting_selection(self):
        path=self.manifest('local')
        self.assertEqual(self.run_main(['--manifest',str(path),'--batches','local','--execute']),1)
        self.assertEqual(self.calls,[])

    def test_historical_batches(self):
        a=self.manifest('batch-001',self.manifests);b=self.manifest('batch-002',self.manifests)
        self.assertEqual(self.run_main(['--batches','batch-002','batch-001','--validate-only']),0)
        self.assertEqual(self.summary()['selected_batches'],['batch-001','batch-002'])
        self.assertEqual([Path(c[c.index('--manifest')+1]) for c in self.calls],[a,b])

    def test_invalid_schema_never_executes(self):
        path=self.manifest('invalid');path.write_text(json.dumps({'id':'invalid','schema_version':99}))
        self.assertEqual(self.run_main(['--manifest',str(path),'--execute']),1)
        self.assertEqual(len(self.calls),1);self.assertIn('--validate-only',self.calls[0])
        self.assertEqual(self.summary()['status'],'failed')
        self.assertTrue(list((self.game/'build/pipeline').glob('*/invalid/failure-batch/diagnostic.json')))

    def test_success_validates_before_synthetic_execution(self):
        path=self.manifest('local')
        self.assertEqual(self.run_main(['--manifest',str(path),'--execute']),0)
        self.assertEqual(len(self.calls),2)
        self.assertIn('--validate-only',self.calls[0]);self.assertNotIn('--validate-only',self.calls[1])

    def test_external_path(self):
        pipeline.ROOT=self.root/'repo'
        path=self.manifest('external')
        self.assertEqual(self.run_main(['--manifest',str(path),'--validate-only']),0)
        self.assertEqual(self.summary()['batches'][0]['manifest'],str(path.resolve()))

    def test_relative_path(self):
        path=self.manifest('relative')
        relative=Path(os.path.relpath(path))
        self.assertEqual(self.run_main(['--manifest',str(relative),'--validate-only']),0)

    def test_changed_manifest_after_validation_never_executes(self):
        path=self.manifest('local');original=self.fake_runner
        def mutate(command,**kwargs):
            result=original(command,**kwargs)
            path.write_text(path.read_text()+' ')
            return result
        with patch.object(pipeline.subprocess,'run',side_effect=mutate):
            self.assertEqual(self.run_main(['--manifest',str(path),'--execute']),1)
        self.assertEqual(len(self.calls),1);self.assertIn('--validate-only',self.calls[0])

    def test_path_and_id_rejections(self):
        path=self.manifest('bad')
        for data in ([],{'id':'../escape'},{'id':''},{'id':12}):
            with self.subTest(data=data):
                path.write_text(json.dumps(data))
                self.assertEqual(self.run_main(['--manifest',str(path),'--execute']),1)
        self.assertEqual(self.run_main(['--manifest',str(path.parent),'--execute']),1)
        self.assertEqual(self.calls,[])

    def test_list_and_legacy_errors(self):
        self.manifest('batch-001',self.manifests)
        self.assertEqual(self.run_main(['--list']),0)
        for args in (['--batches','missing','--execute'],['--batches','batch-001','batch-001','--execute'],
                     ['--execute'],['--manifest',str(self.manifest('local'))],
                     ['--list','--manifest',str(self.manifest('local'))]):
            with self.subTest(args=args):self.assertEqual(self.run_main(args),1)
        self.assertEqual(self.calls,[])


if __name__=='__main__':
    unittest.main()
