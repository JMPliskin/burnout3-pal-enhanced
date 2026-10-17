"""Bounded PAL batch translation; immutable baseline and resumable build staging.

Uses original lift/build/verify routines. No regression promotion or source replacement.
"""
import argparse
from concurrent.futures import ProcessPoolExecutor,wait,FIRST_COMPLETED
import contextlib
import json
import os
from pathlib import Path
import shutil
import sys
import uuid
import time
import bulk_resources

import lift_incremental as inc
from pal_auto import atomic_json as _atomic_json,lock
import run_batch as runner
from ps2decomp import verdicts,testplan
from ps2decomp.emu import Machine

BASE=inc.GAME/'build/bulk'


def atomic_json(path,data):
    # Windows readers/antivirus can transiently deny replacement. Keep the
    # original atomic writer and its diagnostic temporaries; bounded retries
    # never turn a permanent persistence failure into success.
    for attempt in range(8):
        try:return _atomic_json(path,data)
        except PermissionError as exc:
            if getattr(exc,'winerror',None) not in (5,32) or attempt==7:raise
            time.sleep(min(0.05*2**attempt,0.5))


def category(reason):
    s=reason.lower()
    if 'wall' in s or 'timeout' in s:return 'wall_timeout'
    if 'executionlimit' in s or 'instructions' in s and 'exceed' in s:return 'instruction_limit'
    if 'disagrees' in s:return 'equivalence_mismatch'
    if 'does not compile' in s or 'compileerror' in s:return 'compile_error'
    if any(x in s for x in ('read','load')) and ('unmapped' in s or 'memory' in s):return 'memory_read'
    if any(x in s for x in ('write','store')) and ('unmapped' in s or 'memory' in s):return 'memory_write'
    if 'unsupported' in s or 'invalid instruction' in s:return 'instruction_unsupported'
    if any(x in s for x in ('delay slot','branch','jump','control','chunk')):return 'control_flow'
    if 'cases' in s or 'untestable' in s:return 'insufficient_test_cases'
    if 'callee' in s or 'call' in s:return 'callee_failure'
    return 'other'


def environment():
    g=inc.port.game_config.load(str(inc.GAME));db=g.load_database();elf=g.load_elf()
    section=elf.exec_sections[0];image=elf.section_data(section)
    inc.translator.use_sections(inc.driver._other_sections(elf,section))
    inc.translator.use_function_starts(f['addr'] for f in db['functions'])
    return g,db,elf,section,image


def plan_create(size=250,jobs=2,pilot=None):
    if not 1<=size<=500 or not 1<=jobs<=8:raise ValueError('Invalid resource limits')
    g,db,elf,section,image=environment()
    if len({f['addr'] for f in db['functions']})!=len(db['functions']):raise ValueError('Duplicate inventory addresses')
    directory=BASE/('run-'+uuid.uuid4().hex[:12]);directory.mkdir(parents=True,exist_ok=False)
    (directory/'baseline').mkdir()
    files,owners=inc.inventory(inc.GAME/'src/lifted')
    baseline_status={}
    import re
    pattern=re.compile(r'(?:/\* NOT VERIFIED: ([^\n]*) \*/\s*)?^void F_([0-9A-F]{8})\(lifted::Context& c\) \{',re.M)
    for name in files:
        source=inc.GAME/'src/lifted'/name;shutil.copyfile(source,directory/'baseline'/name)
        for m in pattern.finditer(source.read_text()):baseline_status[int(m[2],16)]=m[1]
    if set(baseline_status)!=set(owners):raise ValueError('Cannot classify baseline annotations')
    # Conservative synthetic memory: original static pointer recovery, no dynamic
    # null-pointer discovery and no automatic fault mapping in final verification.
    seeds=sorted(testplan.global_pointers(db,image,section.addr))
    policy='PAL bulk static seeds; map_faults=False; whole state; original verifier'
    fingerprint=verdicts.fingerprint(str(inc.ROOT),(image,Path(g.database_path).read_bytes(),seeds,policy))
    cache=inc.GAME/'build/verdicts.json'
    if cache.exists():shutil.copyfile(cache,directory/'cache.json')
    inputs=[Path(g.elf_path),Path(g.database_path),inc.GAME/'game.json',Path(__file__),
            Path(bulk_resources.__file__),Path(inc.__file__),Path(inc.driver.__file__),*map(Path,verdicts._files(str(inc.ROOT)))]
    plan={'schema':1,'status':'planned','compile_jobs':2,'jobs':jobs,'batch_size':size,'fingerprint':fingerprint,
          'policy':policy,'seeds':seeds,'inputs':{str(p):inc.digest(p) for p in inputs},
          'baseline':files,'baseline_functions':{str(a):r for a,r in baseline_status.items()},'records':{},'batches':[]}
    texts={}
    for f in sorted(db['functions'],key=lambda f:f['addr']):
        a=f['addr'];record={'address':a,'size':f['size'],'status':'pending'}
        if a in owners:
            record.update(status='preserved_verified' if baseline_status[a] is None else 'preserved_unverified',
                          reason=baseline_status[a],source=owners[a],category=category(baseline_status[a]) if baseline_status[a] else None)
        else:
            try:
                try:text=inc.translator.lift_function(image,section.addr,a,f['end'],f.get('chunks',()))
                except inc.translator.Refused as exc:
                    if not f.get('chunks'):raise
                    record['chunk_refusal']=str(exc)
                    text=inc.translator.lift_function(image,section.addr,a,f['end'])
                texts[str(a)]=text;record['status']='translated_pending_compile'
            except (inc.translator.Refused,ValueError) as exc:
                record.update(status='rejected',reason=str(exc),category=category(str(exc)))
        plan['records'][str(a)]=record
    addresses=[int(a) for a in texts]
    groups=[]
    if pilot:
        if not 1<=pilot<=size:raise ValueError('Invalid pilot size')
        groups.append(addresses[:pilot]);addresses=addresses[pilot:]
    groups.extend(addresses[start:start+size] for start in range(0,len(addresses),size))
    for n,group in enumerate(groups):
        plan['batches'].append({'number':n,'addresses':group,'status':'pending'})
    atomic_json(directory/'texts.json',texts)
    plan['texts_sha256']=inc.digest(directory/'texts.json')
    atomic_json(directory/'plan.json',plan)
    print(directory/'plan.json',flush=True)
    return directory/'plan.json'


def check(path,plan):
    if not path.resolve().is_relative_to(BASE.resolve()) or plan['schema']!=1:raise ValueError('Invalid plan path/schema')
    for p,sha in plan['inputs'].items():
        if inc.digest(p)!=sha:raise ValueError('Input changed: '+p)
    for name,sha in plan['baseline'].items():
        if inc.digest(inc.GAME/'src/lifted'/name)!=sha or inc.digest(path.parent/'baseline'/name)!=sha:
            raise ValueError('Baseline changed: '+name)
    if inc.digest(path.parent/'texts.json')!=plan['texts_sha256']:raise ValueError('Staged text changed')


def init_verify(seeds,dll):
    inc.driver._start_worker(str(inc.GAME),seeds,dll)
    inc.driver._WORKER[0].map_faults=False


def verify_one(address):
    machine,library,functions=inc.driver._WORKER
    machine.map_faults=False
    start=time.monotonic();cpu=time.process_time()
    outcome=inc.driver.verify(functions[address],machine,library,False)
    return address,outcome,os.getpid(),time.monotonic()-start,time.process_time()-cpu


def check_result(result,addresses):
    if not isinstance(result,dict) or not {'dll','dll_sha256','compiled_sources','built','outcomes'}<=result.keys():
        raise ValueError('Incomplete compiled evidence')
    built=result['built'];outcomes=result['outcomes']
    if not isinstance(built,list) or len(built)!=len(set(built)) or not set(built)<=set(addresses):
        raise ValueError('Invalid compiled function identities')
    if not isinstance(outcomes,dict) or not set(outcomes)<={str(a) for a in addresses} or not all(isinstance(x,str) for x in outcomes.values()):
        raise ValueError('Invalid verification identities/outcomes')
    if any(outcomes.get(str(a))!=inc.driver.NO_COMPILE for a in set(addresses)-set(built)):
        raise ValueError('Missing compile rejection identity')
    sources=result['compiled_sources']
    if not isinstance(sources,dict) or not sources:raise ValueError('Missing compiled source hashes')
    for p,sha in {result['dll']:result['dll_sha256'],**sources}.items():
        if not Path(p).resolve().is_relative_to(BASE.resolve()) or inc.digest(p)!=sha:
            raise ValueError('Compiled evidence changed/outside staging')


def worker(path,index,phase,attempt):
    plan=json.loads(path.read_bytes());check(path,plan)
    batch=plan['batches'][index];directory=path.parent/attempt;directory.mkdir(exist_ok=True)
    with (directory/(phase+'.log')).open('x',encoding='utf-8') as log,contextlib.redirect_stdout(log),contextlib.redirect_stderr(log):
        texts=json.loads((path.parent/'texts.json').read_bytes())
        selected={a:texts[str(a)] for a in batch['addresses']}
        if phase=='compile':
            original=inc.driver.liftcheck.build
            inc.driver.liftcheck.build=lambda sources,work:original(sources,work,parallel=plan.get('compile_jobs',2))
            dll,built=inc.driver.build(selected,str(directory/'compile'))
            result={'dll':dll,'dll_sha256':inc.digest(dll),'built':list(built),
                    'compiled_sources':{str(p):inc.digest(p) for p in (directory/'compile').glob('*.cpp')},
                    'outcomes':{str(a):inc.driver.NO_COMPILE for a in selected if a not in built}}
            atomic_json(directory/'result.json',result)
        else:
            result=json.loads((directory/'result.json').read_bytes())
            check_result(result,batch['addresses'])
            cache=verdicts.Verdicts(str(path.parent/'cache.json'),plan['fingerprint'])
            missing=[]
            for a in result['built']:
                outcome=cache.get(selected[a],False)
                if outcome is None:missing.append(a)
                else:result['outcomes'][str(a)]=outcome
            result.update(cache_hits=len(result['built'])-len(missing),fresh_completed=0)
            atomic_json(directory/'result.json',result)
            started=time.monotonic(); monitor=bulk_resources.Monitor()
            target=min(plan['jobs'],monitor.cpu_count); remaining=list(missing); resource_retries={}
            telemetry={'requested':plan['jobs'],'cpu_capacity':monitor.cpu_count,'samples':[],
                       'workers':{},'limits':[],'completed':0}
            while remaining:
                sample=monitor.sample(); target=bulk_resources.limit(target,sample)
                telemetry['limits'].append({'workers':target,'elapsed':time.monotonic()-started})
                with ProcessPoolExecutor(max_workers=target,initializer=init_verify,initargs=(plan['seeds'],result['dll'])) as pool:
                    pending={}; draining=False
                    while remaining or pending:
                        while remaining and len(pending)<target and not draining:
                            address=remaining.pop(0); pending[pool.submit(verify_one,address)]=address
                        if not pending:break
                        ready,_=wait(pending,timeout=2,return_when=FIRST_COMPLETED)
                        sample=monitor.sample(); new_target=bulk_resources.limit(target,sample)
                        sample.update(elapsed=time.monotonic()-started,in_flight=len(pending),
                                      pool_processes=len(pool._processes or {}),target=new_target)
                        telemetry['samples'].append(sample)
                        if new_target<target: target=new_target;draining=True
                        for future in ready:
                            address=pending.pop(future)
                            try:a,outcome,pid,wall,cpu=future.result()
                            except (MemoryError,OSError) as exc:
                                if not isinstance(exc,MemoryError) and getattr(exc,'winerror',None) not in (8,14,1450,1455):raise
                                resource_retries[address]=resource_retries.get(address,0)+1
                                telemetry.setdefault('resource_errors',[]).append({'address':address,'error':str(exc)})
                                atomic_json(directory/'resources.json',telemetry)
                                if resource_retries[address]>1:raise RuntimeError('Repeated resource exhaustion; checkpoint retained') from exc
                                target=bulk_resources.limit(target,{'ram_percent':0,'resource_error':True})
                                draining=True;remaining.append(address);continue
                            if a!=address:raise ValueError('Worker identity mismatch')
                            telemetry['workers'].setdefault(str(pid),{'functions':0,'wall_seconds':0,'cpu_seconds':0})
                            info=telemetry['workers'][str(pid)];info['functions']+=1;info['wall_seconds']+=wall;info['cpu_seconds']+=cpu
                            result['outcomes'][str(a)]=outcome
                            cache.put(selected[a],False,outcome);cache.save()
                            result['fresh_completed']+=1;telemetry['completed']+=1
                            atomic_json(directory/'result.json',result)
                        telemetry['elapsed_seconds']=time.monotonic()-started
                        atomic_json(directory/'resources.json',telemetry)
                        if draining and not pending:break
            monitor.close()
    return 0


def summarize(plan):
    from collections import Counter
    states=Counter(r['status'] for r in plan['records'].values())
    errors=Counter(r.get('category','other') for r in plan['records'].values() if r.get('reason'))
    return {'inventory':len(plan['records']),'states':dict(states),'errors':dict(errors),
            'batch_states':dict(Counter(b['status'] for b in plan['batches']))}


def execute(path,only_batches=None):
    path=path.resolve()
    with lock(path.parent/'run.lock'):
        plan=json.loads(path.read_bytes());check(path,plan)
        for batch in plan['batches'][:only_batches]:
            if batch['status']=='done':
                for p,sha in batch['artifacts'].items():
                    if inc.digest(p)!=sha:raise ValueError('Completed batch artifact changed: '+p)
                for p in batch['artifacts']:
                    if p.endswith('result.json'):check_result(json.loads(Path(p).read_bytes()),batch['addresses'])
                continue
            if batch['status'] not in ('pending','running'):raise ValueError('Batch blocked; explicit investigation required')
            check(path,plan)
            attempt='b%03d-%s'%(batch['number'],uuid.uuid4().hex[:8]);directory=path.parent/attempt;directory.mkdir()
            batch.setdefault('attempts',[]).append(attempt)
            batch.update(status='running',attempt=attempt);atomic_json(path,plan)
            unfinished='Verification did not provide a verdict'
            for phase,seconds in (('compile',600),('verify',120)):
                row={};command=[sys.executable,'-B',str(Path(__file__).resolve()),'--_worker',str(path),str(batch['number']),phase,attempt]
                runner.run_windows_job(command,seconds,row);atomic_json(directory/(phase+'-containment.json'),row)
                if not row.get('cleanup_succeeded') or row.get('active_processes_after_cleanup')!=0:
                    batch['status']='infrastructure_error';atomic_json(path,plan);raise ValueError('Unconfirmed cleanup; queue stopped')
                if row.get('status')=='structural_error':
                    batch.update(status='infrastructure_error',reason='Structural supervisor failure; queue stopped')
                    atomic_json(path,plan);raise ValueError(batch['reason'])
                if row.get('exit_code')!=0 or row.get('timeout_detected'):
                    if phase=='compile':
                        if row.get('timeout_detected'):
                            for a in batch['addresses']:
                                plan['records'][str(a)].update(status='compile_unconfirmed',reason='Compile wall timeout; excluded without a compilability verdict',category='compile_wall_timeout')
                            batch.update(status='done',artifacts={str(directory/(phase+'-containment.json')):inc.digest(directory/(phase+'-containment.json'))})
                            atomic_json(path,plan);break
                        batch.update(status='compile_blocked',reason='Compile infrastructure failure')
                        atomic_json(path,plan);raise ValueError(batch['reason'])
                    if not row.get('timeout_detected'):
                        batch.update(status='verification_blocked',reason='Verification infrastructure failure; checkpoint retained')
                        atomic_json(path,plan);raise ValueError(batch['reason'])
                    unfinished='Verification wall timeout; no verdict'
                    break
            if batch['status']=='done':
                atomic_json(path.parent/'summary.json',summarize(plan));continue
            result=json.loads((directory/'result.json').read_bytes())
            check_result(result,batch['addresses'])
            for a in batch['addresses']:
                outcome=result['outcomes'].get(str(a),unfinished)
                status=('verified' if outcome=='verified' else 'non_compilable' if outcome==inc.driver.NO_COMPILE else
                        'mismatch_excluded' if outcome.startswith('disagrees') else 'unverified')
                plan['records'][str(a)].update(status=status,reason=None if outcome=='verified' else outcome,
                                               category=None if outcome=='verified' else category(outcome))
            artifacts={str(directory/'result.json'):inc.digest(directory/'result.json')}
            artifacts[result['dll']]=result['dll_sha256'];artifacts.update(result['compiled_sources'])
            for p in [*directory.glob('*-containment.json'),*directory.glob('resources.json')]:artifacts[str(p)]=inc.digest(p)
            batch.update(status='done',artifacts=artifacts)
            atomic_json(path,plan);atomic_json(path.parent/'summary.json',summarize(plan))
            print('batch',batch['number'],'done',summarize(plan)['states'],flush=True)
        if all(b['status']=='done' for b in plan['batches']):plan['status']='complete';atomic_json(path,plan)
        return plan


def publish(path):
    plan=json.loads(path.read_bytes());check(path,plan)
    if not all(b['status']=='done' for b in plan['batches']):raise ValueError('Incomplete plan; cannot publish')
    for b in plan['batches']:
        for p,sha in b['artifacts'].items():
            if inc.digest(p)!=sha:raise ValueError('Completed evidence changed')
        for p in b['artifacts']:
            if p.endswith('result.json'):check_result(json.loads(Path(p).read_bytes()),b['addresses'])
    stage=path.parent/('publish-'+uuid.uuid4().hex[:8]);stage.mkdir();output=stage/'lifted';output.mkdir()
    for name in plan['baseline']:shutil.copyfile(path.parent/'baseline'/name,output/name)
    texts=json.loads((path.parent/'texts.json').read_bytes())
    for b in plan['batches']:
        chosen={a:texts[str(a)] for a in b['addresses'] if plan['records'][str(a)]['status'] in ('verified','unverified')}
        warnings={a:plan['records'][str(a)]['reason'] for a in chosen if plan['records'][str(a)]['status']!='verified'}
        inc.driver.write_chunks(chosen,str(output),'bulk_%03d'%b['number'],unverified=warnings)
    files,owners=inc.inventory(output)
    expected={int(a) for a,r in plan['records'].items() if r['status'] in ('verified','unverified','preserved_verified','preserved_unverified')}
    if set(owners)!=expected:raise ValueError('Published set identity conflict')
    if any(files[name]!=sha for name,sha in plan['baseline'].items()):raise ValueError('Baseline publication conflict')
    atomic_json(stage/'manifest.json',{'summary':summarize(plan),'cpp_sha256':files,'addresses':sorted(owners)})
    final=path.parent/'published'
    os.rename(stage,final) # fails rather than replacing an existing publication
    print(final/'lifted',flush=True);return final/'lifted'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',action='store_true');parser.add_argument('--run',type=Path)
    parser.add_argument('--publish',type=Path);parser.add_argument('--batch-size',type=int,default=250)
    parser.add_argument('--jobs',type=int,default=2);parser.add_argument('--only-batches',type=int)
    parser.add_argument('--pilot-size',type=int)
    parser.add_argument('--_worker',nargs=4)
    args=parser.parse_args()
    if args._worker:return worker(Path(args._worker[0]),int(args._worker[1]),args._worker[2],args._worker[3])
    if args.plan:plan_create(args.batch_size,args.jobs,args.pilot_size)
    elif args.run:execute(args.run,args.only_batches)
    elif args.publish:publish(args.publish.resolve())
    else:parser.error('Choose --plan, --run or --publish')
    return 0


if __name__=='__main__':raise SystemExit(main())
