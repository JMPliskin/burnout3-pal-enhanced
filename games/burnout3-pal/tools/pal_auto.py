"""Local sequential PAL queue; no AI, promotion, translation or compilation here.

--plan --manifest FILE [--manifest FILE] creates build/auto/p-*/plan.json.
--run PLAN starts pending jobs; --resume PLAN audits existing attempts first.
Only --resume --retry-infrastructure retries infrastructure/interrupted jobs,
once (two total attempts), and only with confirmed cleanup. Equivalence,
coverage, compilation, static rejection and timeout are never retried here.
All runtime writes stay under build. Source manifests are immutable inputs.
"""
import argparse
from contextlib import contextmanager, redirect_stdout, redirect_stderr
from datetime import datetime, timezone
import json
import os
import re
from pathlib import Path
import sys
import uuid

import run_pal_pipeline as pipeline
import run_batch as runner
import generate_batches as evidence

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
ROOT = GAME.parent.parent
BUILD = GAME / 'build'
AUTO = BUILD / 'auto'
PIPELINE = HERE / 'run_pal_pipeline.py'
SCHEMA = 1
MAX_ATTEMPTS = 2
RETRYABLE = {'infrastructure_error', 'interrupted'}
TERMINAL = {'passed', 'static_rejected', 'derivation_rejected', 'compile_error', 'equivalence_failed', 'timeout'}
JOB_STATES = TERMINAL | RETRYABLE | {'pending', 'running'}
FUNCTION_STATES = JOB_STATES | {'not_run', 'rejected', 'mismatch', 'coverage_failed', 'structural_error'}


class InvalidPlan(ValueError):
    """Semantic corruption: reject without rewriting the plan."""


def invariant(ok, message):
    if not ok:
        raise InvalidPlan('Invalid plan: '+message)


def timestamp(value):
    invariant(isinstance(value,str), 'timestamp must be a string')
    try:
        invariant(datetime.fromisoformat(value).tzinfo is not None, 'timestamp needs a timezone')
    except ValueError as exc:
        raise InvalidPlan('Invalid plan: malformed timestamp') from exc


def hash_map(value):
    invariant(isinstance(value,dict) and value, 'missing evidence hash map')
    invariant(all(isinstance(k,str) and k and isinstance(v,str) and re.fullmatch(r'[0-9a-f]{64}',v)
                  for k,v in value.items()), 'invalid evidence hash entry')


def plan_structure(plan):
    invariant(isinstance(plan,dict), 'root must be an object')
    invariant({'schema_version','id','created_at','updated_at','python','status','inputs','retry_policy','jobs'}<=plan.keys(),
              'missing essential root fields')
    invariant(type(plan['schema_version']) is int and plan['schema_version']==SCHEMA, 'unsupported schema')
    invariant(isinstance(plan['id'],str) and isinstance(plan['python'],str), 'invalid identity/version types')
    invariant(isinstance(plan['status'],str) and plan['status'] in
              {'planned','running','passed','incomplete','infrastructure_error'}, 'unknown plan state')
    timestamp(plan['created_at']);timestamp(plan['updated_at'])
    invariant(isinstance(plan['inputs'],list) and plan['inputs'], 'inputs must be a nonempty list')
    policy=plan['retry_policy']
    invariant(isinstance(policy,dict) and type(policy.get('max_attempts')) is int and
              policy['max_attempts']==MAX_ATTEMPTS, 'invalid retry policy')
    invariant(all(isinstance(policy.get(k),str) for k in ('infrastructure','equivalence')), 'missing retry policy description')
    if 'events' in plan:
        invariant(isinstance(plan['events'],list), 'events must be a list')
        for event in plan['events']:
            invariant(isinstance(event,dict) and isinstance(event.get('reason'),str) and
                      isinstance(event.get('stage'),str), 'invalid diagnostic event')
            timestamp(event.get('at'))
    invariant(isinstance(plan['jobs'],list) and plan['jobs'], 'jobs must be a nonempty list')
    references=list(plan['inputs'])
    for index,job in enumerate(plan['jobs']):
        invariant(isinstance(job,dict) and {'key','id','source','snapshot','functions','status','reason','attempts'}<=job.keys(),
                  'missing job fields')
        invariant(job['key']=='j%03d'%index and isinstance(job['id'],str), 'invalid job identity/order')
        try:pipeline.windows_id_key(job['id'])
        except ValueError as exc:raise InvalidPlan('Invalid plan: unsafe batch ID') from exc
        invariant(isinstance(job['status'],str) and job['status'] in JOB_STATES, 'unknown job state')
        invariant(job['reason'] is None or isinstance(job['reason'],str), 'invalid job reason')
        invariant(isinstance(job['functions'],list), 'functions must be a list')
        addresses=[]
        for function in job['functions']:
            invariant(isinstance(function,dict) and isinstance(function.get('address'),str) and
                      re.fullmatch(r'0x[0-9A-Fa-f]{8}',function['address']) and
                      isinstance(function.get('status'),str) and function['status'] in FUNCTION_STATES,
                      'invalid function identity/state')
            addresses.append(int(function['address'],16))
        invariant(len(addresses)==len(set(addresses)), 'duplicate function identities')
        references.extend([job['source'],job['snapshot']])
        attempts=job['attempts']
        invariant(isinstance(attempts,list) and len(attempts)<=MAX_ATTEMPTS, 'invalid attempt collection/limit')
        for position,attempt in enumerate(attempts,1):
            invariant(isinstance(attempt,dict) and {'number','status','started_at','directory','pipeline_before','containment','report','logs'}<=attempt.keys(),
                      'missing attempt fields')
            invariant(type(attempt['number']) is int and attempt['number']==position, 'invalid attempt identity/order')
            state=attempt['status']
            invariant(isinstance(state,str) and state in JOB_STATES-{'pending','static_rejected'}, 'unknown attempt state')
            timestamp(attempt['started_at'])
            invariant(isinstance(attempt['directory'],str) and attempt['directory'], 'missing attempt directory')
            invariant(isinstance(attempt['pipeline_before'],list) and all(isinstance(p,str) for p in attempt['pipeline_before']),
                      'invalid pre-attempt report references')
            invariant(attempt['report'] is None or isinstance(attempt['report'],str), 'invalid report reference')
            logs=attempt['logs'];invariant(isinstance(logs,dict) and all(isinstance(logs.get(k),str) and logs[k]
                                          for k in ('stdout','stderr')), 'missing log references')
            containment=attempt['containment'];invariant(isinstance(containment,dict), 'invalid containment metadata')
            for key in ('cleanup_succeeded','timeout_detected'):
                invariant(key not in containment or type(containment[key]) is bool, 'invalid containment flag')
            for key in ('exit_code','active_processes_after_cleanup'):
                invariant(key not in containment or type(containment[key]) is int and containment[key]>=0,
                          'invalid containment result')
            if state=='running':
                invariant(position==len(attempts) and 'finished_at' not in attempt, 'nonfinal running attempt')
            else:
                invariant('finished_at' in attempt and 'reason' in attempt and
                          (attempt['reason'] is None or isinstance(attempt['reason'],str)), 'missing terminal attempt outcome')
                timestamp(attempt['finished_at'])
            if state=='passed':
                invariant(isinstance(attempt['report'],str) and attempt['report'], 'passed attempt has no report')
                hash_map(attempt.get('evidence_sha256'))
                invariant(containment.get('cleanup_succeeded') is True and containment.get('active_processes_after_cleanup')==0 and
                          containment.get('timeout_detected') is False and containment.get('exit_code')==0 and
                          'error' not in containment, 'passed attempt has no clean successful exit')
            if containment.get('timeout_detected') is True:
                invariant(state in {'running','timeout','infrastructure_error'}, 'timeout contradicts attempt state')
            if position<len(attempts):
                invariant(state in RETRYABLE and containment.get('cleanup_succeeded') is True and
                          containment.get('active_processes_after_cleanup')==0 and containment.get('timeout_detected') is not True and
                          'equivalence_failed' not in attempt.get('failure_conditions',[]), 'ineligible previous attempt was retried')
        state=job['status']
        if state=='pending':
            invariant(not attempts and all(f['status']=='pending' for f in job['functions']), 'pending job already attempted')
        elif state=='static_rejected':
            invariant(all(f['status']=='static_rejected' for f in job['functions']) and
                      (not attempts or attempts[-1]['status'] in RETRYABLE), 'static rejection conflicts with execution history')
        else:
            invariant(bool(attempts), state+' job has no attempts')
            same=attempts[-1]['status']==state
            invalid_success=state=='infrastructure_error' and job.get('evidence_invalid') is True and attempts[-1]['status']=='passed'
            invariant(same or invalid_success, 'job/last-attempt state conflict')
        if state=='passed':
            invariant(job['functions'] and all(f['status']=='passed' for f in job['functions']), 'passed function state conflict')
        if 'evidence_invalid' in job:invariant(type(job['evidence_invalid']) is bool, 'invalid evidence rejection flag')
        for record in [job,*attempts]:
            if 'failure_conditions' in record:
                invariant(isinstance(record['failure_conditions'],list) and
                          all(isinstance(s,str) and s in TERMINAL|{'infrastructure_error'} for s in record['failure_conditions']),
                          'invalid failure conditions')
            if 'stop_queue' in record:invariant(type(record['stop_queue']) is bool, 'invalid queue-stop flag')
            if 'infrastructure_error' in record.get('failure_conditions',[]):
                invariant(record.get('stop_queue') is True,'structural failure has no queue-stop flag')
        structural=any(f['status']=='structural_error' for f in job['functions'])
        invariant(not structural or state in {'running','infrastructure_error','interrupted','timeout'},
                  'structural failure hidden by job state')
        equivalence=any(f['status'] in {'equivalence_failed','mismatch','coverage_failed'} for f in job['functions'])
        if structural and equivalence:
            invariant({'infrastructure_error','equivalence_failed'}<=set(job.get('failure_conditions',[])) and
                      attempts and {'infrastructure_error','equivalence_failed'}<=set(attempts[-1].get('failure_conditions',[])),
                      'mixed failure conditions missing')
    for reference in references:
        invariant(isinstance(reference,dict) and isinstance(reference.get('path'),str) and reference['path'] and
                  isinstance(reference.get('sha256'),str) and re.fullmatch(r'[0-9a-f]{64}',reference['sha256']), 'invalid input reference')
    if plan['status']=='passed':invariant(all(j['status']=='passed' for j in plan['jobs']), 'passed plan has unfinished jobs')
    if plan['status']=='planned':invariant(all(not j['attempts'] for j in plan['jobs']), 'planned plan already attempted')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def now():
    return datetime.now(timezone.utc).isoformat()


def inside(path, root=None):
    path = Path(path).resolve()
    require(path.is_relative_to((root or BUILD).resolve()), 'Path outside allowed build directory: '+str(path))
    return path


def atomic_json(path, data):
    path = inside(path)
    temporary = path.with_name(path.name+'.'+uuid.uuid4().hex+'.tmp')
    # Leave interrupted temporary files as diagnostics; readers only use path.
    with temporary.open('x', encoding='utf-8') as handle:
        json.dump(data, handle, indent=2)
        handle.write('\n')
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


@contextmanager
def lock(path):
    """Kernel lock, automatically released on process death; never delete locks."""
    path = inside(path)
    with path.open('a+b') as handle:
        handle.seek(0, os.SEEK_END)
        if not handle.tell():
            handle.write(b'0');handle.flush()
        handle.seek(0)
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            raise ValueError('Queue already locked: '+str(path)) from exc
        try:
            yield
        finally:
            handle.seek(0)
            if os.name == 'nt':
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(handle, fcntl.LOCK_UN)


def reference(path):
    path = Path(path).resolve()
    return {'path':str(path), 'sha256':pipeline.digest(path)}


def check_reference(item):
    path = Path(item['path'])
    require(path.is_file() and pipeline.digest(path) == item['sha256'], 'Incompatible/missing input: '+str(path))
    return path


def input_paths(game):
    paths = {Path(__file__), PIPELINE, HERE/'run_batch.py', HERE/'generate_batches.py',
             GAME/'game.json', Path(game.elf_path), Path(game.database_path), Path(sys.executable)}
    # Include semantic dependencies, not only the driver. No generated code read.
    paths.update((ROOT/'engine/ps2decomp').glob('*.py'))
    paths.add(ROOT/'engine/tools/derive.py')
    paths.update(p for p in (ROOT/'engine/runtime').rglob('*') if p.is_file())
    return sorted(paths)


def create_plan(manifests):
    selected = pipeline.select_manifests(None, manifests)
    game = runner.load(str(GAME))
    inputs = [reference(p) for p in input_paths(game)]
    AUTO.mkdir(parents=True, exist_ok=True)
    directory = inside(AUTO/('p-'+uuid.uuid4().hex[:12]))
    directory.mkdir(exist_ok=False)
    (directory/'m').mkdir()
    plan = {'schema_version':SCHEMA, 'id':directory.name, 'created_at':now(),
            'updated_at':now(), 'python':sys.version, 'status':'planned', 'inputs':inputs,
            'retry_policy':{'max_attempts':MAX_ATTEMPTS, 'infrastructure':'explicit resume flag; confirmed cleanup only',
                            'equivalence':'never automatically retry'}, 'jobs':[]}
    for index, (name, original) in enumerate(selected.items()):
        original = original.resolve();raw = original.read_bytes()
        source = reference(original)
        require(pipeline.digest(original)==source['sha256'], 'Manifest changed during planning')
        snapshot = directory/'m'/('j%03d.json'%index)
        with snapshot.open('xb') as handle:
            handle.write(raw);handle.flush();os.fsync(handle.fileno())
        require(pipeline.digest(snapshot)==source['sha256'], 'Manifest changed during snapshot')
        manifest = json.loads(raw)
        require(manifest['id']==name, 'Manifest ID changed during selection')
        # Windows whole-path headroom for the unchanged pipeline and runner.
        output_paths=[BUILD/'pipeline'/('run-'+'0'*32)/name/'execution/stdout.log',
                      BUILD/'batches'/name/('sequential-'+'0'*32)/'function-00000000'/'compile/generated_check_0000000000_00.dll',
                      directory/'j000-a2/stdout.log']
        require(max(len(str(p)) for p in output_paths)<240,
                'Manifest ID makes output paths too long')
        job = {'key':'j%03d'%index, 'id':name, 'source':source, 'snapshot':reference(snapshot),
               'functions':[{'address':f['address'],'status':'pending'} for f in manifest.get('functions',[])
                            if isinstance(f,dict) and isinstance(f.get('address'),str) and
                            re.fullmatch(r'0x[0-9A-Fa-f]{8}',f['address'])],
               'status':'pending', 'reason':None, 'attempts':[]}
        try:
            _, validation = runner.validate(manifest)
            job['functions'] = [{'address':f['address'], 'status':'pending'} for f in manifest['functions']]
            job['validation'] = validation
        except ValueError as exc:
            job.update(status='static_rejected', reason=str(exc))
            for function in job['functions']:function['status']='static_rejected'
        plan['jobs'].append(job)
    atomic_json(directory/'plan.json', plan)
    return directory/'plan.json'


def check_plan(path, plan):
    plan_structure(plan)
    try:
        return check_plan_inputs(path,plan)
    except (KeyError,TypeError,AttributeError,IndexError) as exc:
        raise InvalidPlan('Invalid plan: malformed input/snapshot metadata: '+str(exc)) from exc


def check_plan_inputs(path, plan):
    directory = inside(path, AUTO).parent
    invariant(Path(path).name=='plan.json' and plan['schema_version']==SCHEMA and plan['id']==directory.name,
            'Invalid plan identity/schema')
    require(plan['python']==sys.version, 'Python version changed')
    require(plan['retry_policy']['max_attempts']==MAX_ATTEMPTS, 'Unsupported retry policy')
    expected={p.resolve() for p in input_paths(runner.load(str(GAME)))}
    invariant(len(plan['inputs'])==len(expected) and {Path(r['path']).resolve() for r in plan['inputs']}==expected,
            'Missing/duplicate environment references')
    for item in plan['inputs']:
        check_reference(item)
    require(plan['jobs'], 'Empty plan')
    keys=set();ids=set()
    for index, job in enumerate(plan['jobs']):
        require(job['key']=='j%03d'%index and job['key'] not in keys, 'Invalid job key')
        keys.add(job['key']);key=pipeline.windows_id_key(job['id'])
        invariant(key not in ids, 'Conflicting job IDs');ids.add(key)
        check_reference(job['source'])
        snapshot=inside(check_reference(job['snapshot']),directory/'m')
        invariant(job['snapshot']['sha256']==job['source']['sha256'], 'Snapshot/source conflict')
        manifest=json.loads(snapshot.read_bytes())
        invariant(manifest['id']==job['id'], 'Job/manifest identity conflict')
        if job['status']!='static_rejected':
            invariant({f['address'] for f in job['functions']}=={f['address'] for f in manifest['functions']},
                    'Missing/conflicting planned functions')
        require(len(job['attempts'])<=MAX_ATTEMPTS, 'Attempt limit exceeded')
        for attempt in job['attempts']:
            expected_dir=directory/(job['key']+'-a%d'%attempt['number'])
            invariant(Path(attempt['directory']).resolve()==expected_dir.resolve(), 'attempt directory/identity conflict')
            inside(attempt['directory'],directory)
            for stream in ('stdout','stderr'):
                invariant(Path(attempt['logs'][stream]).resolve()==(expected_dir/(stream+'.log')).resolve(),
                          'log directory/identity conflict')
    return directory


def pin_file(path, pins, reuse):
    path=Path(path).resolve();key=str(path)
    require(path.is_file(),'Missing evidence/artifact: '+key)
    if reuse:require(key in pins,'Missing saved evidence hash: '+key)
    digest=pipeline.digest(path)
    if key in pins:require(pins[key]==digest,'Evidence/artifact changed: '+key)
    pins[key]=digest
    return path


def read_evidence(path, pins, root, reuse=False):
    path=inside(path,root)
    pin_file(path,pins,reuse)
    return json.loads(path.read_bytes())


def verify_success(summary_path, job, pins=None):
    """Read the complete pipeline/supervisor/worker chain; reuse worker validator."""
    reuse=pins is not None
    if reuse:hash_map(pins)
    pins=dict(pins or {})
    for name,digest in pins.items():
        p=inside(name)
        require(p.is_file() and pipeline.digest(p)==digest,'Saved evidence/artifact changed: '+str(p))
    summary=read_evidence(summary_path,pins,BUILD/'pipeline',reuse)
    require(summary['status']=='passed' and summary['mode']=='execute' and
            summary['selected_batches']==[job['id']] and len(summary['batches'])==1, 'Incomplete pipeline success')
    for name,digest in summary['sha256'].items():
        p=Path(name.replace('\\','/'));p=p if p.is_absolute() else ROOT/p
        require(p.is_file() and pipeline.digest(p)==digest, 'Incompatible pipeline input')
    snapshot=check_reference(job['snapshot'])
    manifest=json.loads(snapshot.read_bytes())
    expected={f['address']:f['expected_case_count'] for f in manifest['functions']}
    require(expected and len(expected)==len(manifest['functions']), 'Invalid expected functions')
    total=sum(expected.values())
    require(all(pipeline.path_label(p) in summary['sha256'] for p in
                (PIPELINE,HERE/'run_batch.py',GAME/'game.json',snapshot)), 'Missing pipeline input provenance')
    require(summary['sha256'].get(pipeline.path_label(snapshot))==job['snapshot']['sha256'], 'Unbound pipeline manifest')
    batch=summary['batches'][0]
    require(batch['id']==job['id'] and batch['status']=='passed', 'Batch did not pass')
    validation=batch['validation'];require(validation['status']=='validated_only' and validation['exit_code']==0, 'Missing static validation')
    val=read_evidence(validation['report'],pins,BUILD/'batches',reuse)
    require(pins[str(Path(validation['report']).resolve())]==validation['report_sha256'] and
            val['status']=='validated_only' and val['dynamic_execution'] is False, 'Invalid validation report')
    require(val['manifest']==manifest and val['function_count']==len(expected) and val['case_count']==total and
            set(val['addresses'])=={int(a,16) for a in expected}, 'Incomplete/conflicting static evidence')
    val_hashes=evidence.report_hashes(val)
    game=runner.load(str(GAME))
    require(all(p.resolve() in val_hashes for p in (snapshot,HERE/'run_batch.py',GAME/'game.json',
                Path(game.elf_path),Path(game.database_path))), 'Missing static provenance')
    execution=batch['execution']
    require(execution['status']=='passed' and execution['exit_code']==0 and execution['dynamic_execution'] is True, 'Missing successful execution')
    supervisor=read_evidence(execution['report'],pins,BUILD/'batches',reuse)
    require(pins[str(Path(execution['report']).resolve())]==execution['report_sha256'], 'Supervisor hash conflict')
    # The runner reserializes JSON into its own execution snapshot. Its byte
    # hash need not equal the source snapshot's (whitespace/newlines may differ).
    execution_manifest_path=Path(execution['report']).resolve().parent/'manifest.json'
    execution_manifest=read_evidence(execution_manifest_path,pins,BUILD/'batches',reuse)
    require(supervisor['manifest_sha256']==pins[str(execution_manifest_path)], 'Execution manifest hash conflict')
    require(execution_manifest==manifest, 'Execution manifest differs from planned snapshot')
    require(supervisor['status']=='passed' and supervisor['expected_functions']==len(expected) and
            supervisor['expected_cases']==total and supervisor['cases_executed']==supervisor['cases_passed']==total,
            'Incomplete supervisor success')
    require(summary['cases_executed']==execution['cases_executed']==execution['cases_passed']==total, 'Pipeline count conflict')
    rows=supervisor['results'];entries=batch['functions']
    require(len(rows)==len(entries)==len(expected) and {r['address'] for r in rows}==set(expected) and
            {r['address'] for r in entries}==set(expected), 'Missing/duplicate function reports')
    for row in rows:
        address=row['address'];entry=next(e for e in entries if e['address']==address)
        require(row['status']==entry['status']=='passed' and row['exit_code']==0 and
                row['cleanup_succeeded'] is True and row['active_processes_after_cleanup']==0 and
                row['timeout_detected'] is False, 'Worker failed/unclean/timed out')
        require(row['cases_passed']==row['cases_executed']==entry['cases_passed']==entry['cases_executed']==expected[address], 'Incomplete function counts')
        require(Path(row['worker_report']).resolve()==Path(entry['worker_report']).resolve(), 'Worker reference conflict')
        require(Path(row['worker_report']).resolve().parent.parent==Path(execution['report']).resolve().parent,
                'Worker belongs to another execution')
        worker=read_evidence(row['worker_report'],pins,BUILD/'batches',reuse)
        require(evidence.admit_report(Path(row['worker_report']),worker,game)==[int(address,16)], 'Invalid native evidence')
        # Pin required artifacts as well as reports for future resume.
        for artifact in [worker['dll'],*worker['sha256']]:
            p=Path(artifact).resolve()
            if p.is_relative_to(BUILD.resolve()):
                pin_file(p,pins,reuse)
    return pins


def audit_attempt(attempt, job, *, reuse):
    containment=attempt['containment']
    require(containment.get('cleanup_succeeded') is True and containment.get('active_processes_after_cleanup')==0 and
            containment.get('timeout_detected') is False and containment.get('exit_code')==0 and
            'error' not in containment,'Missing containment cleanup/success')
    if reuse:hash_map(attempt.get('evidence_sha256'))
    else:require(attempt['status']=='running','Initial evidence creation requires a running attempt')
    pins=verify_success(attempt['report'],job,attempt['evidence_sha256'] if reuse else None)
    for name in ('stdout','stderr'):
        path=inside(attempt['logs'][name],inside(attempt['directory'],AUTO))
        pin_file(path,pins,reuse)
    return pins


def failure_conditions(summary, job):
    require(summary.get('mode')=='execute' and summary.get('selected_batches')==[job['id']], 'Foreign pipeline report')
    batches=summary.get('batches',[])
    require(len(batches)==1 and batches[0].get('id')==job['id'], 'Missing batch result')
    batch=batches[0];functions=batch.get('functions',[])
    states={f.get('status') for f in functions}
    conditions=[]
    if 'structural_error' in states or batch.get('execution',{}).get('status')=='structural_error' or \
            batch.get('status')=='structural_error' or summary.get('status')=='structural_error':
        conditions.append('infrastructure_error')
    if states & {'mismatch','coverage_failed'}:conditions.append('equivalence_failed')
    if 'timeout' in states or batch.get('execution',{}).get('status')=='timeout':
        conditions.append('timeout')
    if 'compile_error' in states:conditions.append('compile_error')
    if 'rejected' in states:conditions.append('derivation_rejected')
    return conditions or ['infrastructure_error']


def classify_summary(summary, job):
    conditions=failure_conditions(summary,job)
    return conditions[0], 'Observed failure conditions: '+', '.join(conditions)


def record_failure(summary, job, attempt):
    conditions=failure_conditions(summary,job)
    for record in (job,attempt):
        record['failure_conditions']=conditions
        record['stop_queue']='infrastructure_error' in conditions
    observed={f['address']:f['status'] for f in summary['batches'][0].get('functions',[])}
    for function in job['functions']:
        state=observed.get(function['address'],'not_run')
        function['status']='equivalence_failed' if state in ('mismatch','coverage_failed') else state


def worker(config_path):
    """Contained child: redirect logs, then call the unchanged pipeline entrypoint."""
    config_path=inside(config_path,AUTO)
    config=json.loads(config_path.read_bytes())
    snapshot=inside(config['manifest'],AUTO)
    require(pipeline.digest(snapshot)==config['manifest_sha256'], 'Worker manifest changed')
    with (config_path.parent/'stdout.log').open('x',encoding='utf-8') as out, (config_path.parent/'stderr.log').open('x',encoding='utf-8') as err:
        with redirect_stdout(out),redirect_stderr(err):
            return pipeline.main(['--manifest',str(snapshot),'--execute','--stop-on-error'])


def launch(config_path, seconds, containment):
    require(os.name=='nt', 'Execution requires Windows Jobs; no fallback')
    command=[sys.executable,'-B',str(Path(__file__).resolve()),'--_worker',str(config_path)]
    containment.update(command=command,effective_timeout_seconds=seconds)
    runner.run_windows_job(command,seconds,containment)


def recover_attempt(job):
    attempt=job['attempts'][-1] if job['attempts'] else None
    if not attempt or attempt.get('status')!='running':
        return
    containment=attempt['containment']
    if containment.get('timeout_detected') is True:
        # A persisted timeout is conclusive even without a pipeline summary.
        reason='Recovered persisted pipeline timeout'
        conditions=['timeout']
        unclean=not (containment.get('cleanup_succeeded') is True and containment.get('active_processes_after_cleanup')==0)
        if unclean:conditions.append('infrastructure_error')
        attempt.update(status='timeout',reason=reason,finished_at=now(),failure_conditions=conditions,stop_queue=unclean)
        job.update(status='timeout',reason=reason,failure_conditions=conditions,stop_queue=unclean)
        return
    # Only uniquely new reports can belong to this attempt; ambiguity fails closed.
    before=set(attempt['pipeline_before'])
    candidates=[p for p in (BUILD/'pipeline').glob('*/summary.json') if str(p.resolve()) not in before]
    if len(candidates)==1:
        attempt['report']=str(candidates[0].resolve())
        containment=attempt.get('containment',{})
        if containment.get('cleanup_succeeded') is True and containment.get('active_processes_after_cleanup')==0:
            try:
                summary=json.loads(candidates[0].read_bytes())
                if summary.get('status')=='passed':
                    require(containment.get('exit_code')==0 and containment.get('timeout_detected') is False,
                            'Interrupted attempt exit/timeout conflict')
                    attempt['evidence_sha256']=audit_attempt(attempt,job,reuse='evidence_sha256' in attempt)
                    state,reason='passed',None
                    for function in job['functions']:function['status']='passed'
                else:
                    state,reason=classify_summary(summary,job)
                    record_failure(summary,job,attempt)
                attempt.update(status=state,reason=reason,finished_at=now())
                job.update(status=state,reason=reason)
                return
            except (ValueError,KeyError,OSError,TypeError):
                # Remains interrupted, never reusable based on an incomplete report.
                pass
    attempt.update(status='interrupted',reason='Attempt had no committed conclusive outcome',finished_at=now())
    job.update(status='interrupted',reason=attempt['reason'])


def execute_plan(path, resume=False, retry_infrastructure=False):
    path=inside(path,AUTO)
    AUTO.mkdir(parents=True,exist_ok=True)
    # Global serialization also prevents orchestrator plans sharing a batch ID.
    with lock(AUTO/'queue.lock'),lock(path.parent/'plan.lock'):
        plan=json.loads(path.read_bytes())
        try:
            directory=check_plan(path,plan)
        except InvalidPlan:
            # Preserve corrupt JSON exactly; do not invent or repair state.
            raise
        except (ValueError,KeyError,OSError,TypeError) as exc:
            plan.setdefault('events',[]).append({'at':now(),'stage':'compatibility','reason':str(exc)})
            plan.update(status='infrastructure_error',updated_at=now())
            atomic_json(path,plan)
            raise
        if not resume:
            require(all(not j['attempts'] for j in plan['jobs']), 'Plan already started; use --resume')
        for job in plan['jobs']:
            if resume:
                recover_attempt(job)
                if job['status']=='passed':
                    try:
                        attempt=job['attempts'][-1]
                        require(attempt.get('evidence_sha256'),'Missing saved evidence hashes')
                        audit_attempt(attempt,job,reuse=True)
                    except (ValueError,KeyError,OSError,TypeError) as exc:
                        job.update(status='infrastructure_error',reason='Saved success rejected: '+str(exc))
                        # Never turn incomplete prior success into an automatic retry.
                        job['evidence_invalid']=True
                        plan['status']='incomplete'
            atomic_json(path,plan)
        for job in plan['jobs']:
            check_plan(path,plan)
            state=job['status']
            if job.get('stop_queue') and state not in RETRYABLE:break
            if state=='passed' or state in TERMINAL:
                continue
            if state in RETRYABLE:
                cleanup=job['attempts'] and job['attempts'][-1].get('containment',{}).get('cleanup_succeeded') is True
                timeout_seen=any(a['containment'].get('timeout_detected') is True or a['status']=='timeout' for a in job['attempts'])
                equivalence_seen=any('equivalence_failed' in a.get('failure_conditions',[]) or a['status']=='equivalence_failed'
                                     for a in job['attempts'])
                if not retry_infrastructure or not cleanup or timeout_seen or equivalence_seen or job.get('evidence_invalid') or len(job['attempts'])>=MAX_ATTEMPTS:
                    break
            else:
                require(state=='pending','Unknown job state: '+str(state))
            if any(f['status'] in {'equivalence_failed','mismatch','coverage_failed'} for f in job['functions']):
                break
            snapshot=check_reference(job['snapshot']);manifest=json.loads(snapshot.read_bytes())
            try:
                runner.validate(manifest)
            except ValueError as exc:
                job.update(status='static_rejected',reason=str(exc))
                for function in job['functions']:function['status']='static_rejected'
                atomic_json(path,plan);continue
            attempt_dir=directory/(job['key']+'-a%d'%(len(job['attempts'])+1));attempt_dir.mkdir(exist_ok=False)
            before=[str(p.resolve()) for p in (BUILD/'pipeline').glob('*/summary.json')]
            attempt={'number':len(job['attempts'])+1,'status':'running','started_at':now(),
                     'directory':str(attempt_dir),'pipeline_before':before,'containment':{},'report':None,
                     'logs':{'stdout':str(attempt_dir/'stdout.log'),'stderr':str(attempt_dir/'stderr.log')}}
            job['attempts'].append(attempt);job.update(status='running',reason=None,stop_queue=False,failure_conditions=[])
            plan.update(status='running',updated_at=now());atomic_json(path,plan)
            config=attempt_dir/'launch.json'
            atomic_json(config,{'manifest':str(snapshot),'manifest_sha256':job['snapshot']['sha256']})
            operator_interrupted=False
            try:
                seconds=manifest['limits']['batch_timeout_seconds']+120
                launch(config,seconds,attempt['containment'])
                # Persist cleanup before evidence parsing; resume can recover a
                # fully completed child even if the controller stops afterwards.
                atomic_json(path,plan)
                containment=attempt['containment']
                require(containment.get('cleanup_succeeded') is True and containment.get('active_processes_after_cleanup')==0
                        and 'error' not in containment, 'Pipeline Job cleanup not confirmed')
                if containment.get('timeout_detected'):
                    state,reason='timeout','Pipeline outer Job timeout'
                else:
                    candidates=[p for p in (BUILD/'pipeline').glob('*/summary.json') if str(p.resolve()) not in set(before)]
                    require(len(candidates)==1, 'Missing/ambiguous new pipeline report')
                    report_path=candidates[0].resolve();attempt['report']=str(report_path)
                    summary=json.loads(report_path.read_bytes())
                    if summary.get('status')=='passed':
                        require(containment.get('exit_code')==0, 'Pipeline nonzero exit')
                        attempt['evidence_sha256']=audit_attempt(attempt,job,reuse=False)
                        state,reason='passed',None
                    else:
                        state,reason=classify_summary(summary,job)
                        record_failure(summary,job,attempt)
                check_plan(path,plan)
            except KeyboardInterrupt:
                operator_interrupted=True
                containment=attempt['containment']
                if containment.get('timeout_detected') is True:
                    state,reason='timeout','Operator interruption after detected pipeline timeout'
                    unclean=not (containment.get('cleanup_succeeded') is True and containment.get('active_processes_after_cleanup')==0)
                    conditions=['timeout']+(['infrastructure_error'] if unclean else [])
                    for record in (job,attempt):
                        record.update(failure_conditions=conditions,stop_queue=unclean)
                else:
                    state,reason='interrupted','Operator interruption'
            except (ValueError,KeyError,OSError,TypeError) as exc:
                state,reason='infrastructure_error',str(exc)
            attempt.update(status=state,reason=reason,finished_at=now())
            job.update(status=state,reason=reason)
            if state=='passed':
                for f in job['functions']:f['status']='passed'
            plan['updated_at']=now();atomic_json(path,plan)
            if operator_interrupted or state in ('infrastructure_error','interrupted'):break
        plan.update(status='passed' if all(j['status']=='passed' for j in plan['jobs']) else 'incomplete',updated_at=now())
        atomic_json(path,plan)
        return 0 if plan['status']=='passed' else 1


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    modes=parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--plan',action='store_true')
    modes.add_argument('--run',type=Path)
    modes.add_argument('--resume',type=Path)
    modes.add_argument('--_worker',type=Path,help=argparse.SUPPRESS)
    parser.add_argument('--manifest',action='append',type=Path)
    parser.add_argument('--retry-infrastructure',action='store_true')
    args=parser.parse_args(argv)
    try:
        require(sys.version_info[:2]==(3,12),'Python 3.12 required')
        require(not args.retry_infrastructure or args.resume,'Retry flag requires --resume')
        require(not args.manifest or args.plan,'Manifests only accepted with --plan')
        if args._worker:return worker(args._worker)
        if args.plan:
            require(args.manifest,'--plan requires explicit manifests')
            print(create_plan(args.manifest));return 0
        return execute_plan(args.resume or args.run,bool(args.resume),args.retry_infrastructure)
    except (ValueError,KeyError,OSError,TypeError) as exc:
        print('PAL queue error:',str(exc),file=sys.stderr);return 1


if __name__=='__main__':
    raise SystemExit(main())
