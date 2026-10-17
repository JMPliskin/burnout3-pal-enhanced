"""Local PAL pipeline MVP. Default is no execution; select explicit batches.

--validate-only delegates only static validation to run_batch.py. --execute is
required for real batches. All generated diagnostics remain in ignored build/.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
import uuid
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
ROOT = GAME.parent.parent
MANIFESTS = GAME / 'tools/batches'
RUNNER = GAME / 'tools/run_batch.py'
STATES = {'passed','rejected','mismatch','compile_error','timeout','coverage_failed','structural_error'}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checkpoint(path, data):
    temp = path.with_suffix('.json.tmp')
    with temp.open('x', encoding='utf-8') as handle:
        json.dump(data, handle, indent=2)
    temp.replace(path)


def discover():
    found = {}
    for path in sorted(MANIFESTS.glob('*.json')):
        data = json.loads(path.read_bytes())
        name = data.get('id')
        require(isinstance(name,str) and re.fullmatch(r'[A-Za-z0-9_-]+',name), 'Invalid manifest id: '+path.name)
        require(name not in found, 'Duplicate manifest id: '+name)
        found[name] = path
    return found


def normalize(data):
    status = data.get('status','structural_error')
    if status=='structural_error' and 'coverage' in str(data.get('error','')).lower():
        return 'coverage_failed'
    return status if status in STATES else 'structural_error'


def diagnostics(destination, item, worker, row):
    """Local, allowlisted artifacts only; no ELF, ISO, DLL or game assets copied."""
    destination.mkdir(exist_ok=False)
    address = row.get('address')
    package = {'address':address,'status':item['status'],'stage':worker.get('stage','unknown'),
               'error':worker.get('error',row.get('error')),'source_report':item.get('worker_report'),
               'files':[],'instructions':[],
               'privacy':'Local ignored diagnostics; disassembly/generated C++ must never be committed or published.'}
    if address:
        try:
            sys.path.insert(0,str(ROOT/'engine'))
            from ps2decomp.game import load
            from ps2decomp.r5900 import DISASM
            import struct
            game=load(str(GAME));db=game.load_database();elf=game.load_elf();section=elf.exec_sections[0]
            image=elf.section_data(section);a=int(address,16)
            function=next(f for f in db['functions'] if f['addr']==a)
            for low,high in [(function['addr'],function['end'])]+[tuple(c) for c in function.get('chunks',[])]:
                require(section.addr<=low<high<=section.end_addr,'Invalid diagnostic range')
                for pc in range(low,high,4):
                    package['instructions'].append({'address':'0x%08X'%pc,'instruction':str(DISASM.decode(struct.unpack_from('<I',image,pc-section.addr)[0],pc))})
        except Exception as exc:
            package['disassembly_error']=repr(exc)
    report_path=item.get('worker_report')
    if report_path:
        source=Path(report_path).resolve().parent
        require(source.is_relative_to((GAME/'build/batches').resolve()),'Diagnostic source outside batch outputs')
        for relative in ('stdout.log','stderr.log','compile/build.log','generated/accessors.cpp'):
            path=source/relative
            if path.is_file():
                require(path.resolve().is_relative_to(source),'Diagnostic symlink outside own artifacts')
                target=destination/relative;target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(path,target);package['files'].append({'path':relative,'sha256':digest(target)})
    checkpoint(destination/'diagnostic.json',package)
    return str(destination/'diagnostic.json')


def invoke(manifest, mode, directory):
    directory.mkdir(exist_ok=False)
    data=json.loads(manifest.read_bytes());base=GAME/'build/batches'/data['id']
    before=set(base.iterdir()) if base.exists() else set()
    command=[sys.executable,str(RUNNER),'--manifest',str(manifest)]
    if mode=='validate': command.append('--validate-only')
    started=time.monotonic()
    with (directory/'stdout.log').open('x',encoding='utf-8') as stdout, (directory/'stderr.log').open('x',encoding='utf-8') as stderr:
        completed=subprocess.run(command,cwd=str(ROOT),stdout=stdout,stderr=stderr)
    new=(set(base.iterdir()) if base.exists() else set())-before
    expected='results.json' if mode=='validate' else 'supervisor.json'
    reports=[p/expected for p in new if p.is_dir() and (p/expected).is_file()]
    result={'stage':mode,'command':command,'exit_code':completed.returncode,'elapsed_seconds':time.monotonic()-started,
            'status':'structural_error','dynamic_execution':mode=='execute','report':None}
    if len(reports)!=1:
        result['error']='Expected exactly one new runner report; missing or concurrent output ambiguity'
        result['stderr']=(directory/'stderr.log').read_text(encoding='utf-8')
        return result,None
    source=reports[0];report=json.loads(source.read_bytes());result['report']=str(source);result['report_sha256']=digest(source)
    if mode=='validate':
        result['status']='validated_only' if completed.returncode==0 and report.get('status')=='validated_only' and report.get('dynamic_execution') is False else 'structural_error'
        result['expected_cases']=report.get('case_count');result['cases_executed']=0
    else:
        result['status']=report.get('status','structural_error')
        result['cases_executed']=report.get('cases_executed',0);result['cases_passed']=report.get('cases_passed',0)
        if result['status']=='passed':
            require(completed.returncode==0 and len(report.get('results',[]))==len(data['functions']) and report.get('cases_passed')==sum(f['expected_case_count'] for f in data['functions']), 'Inconsistent runner success')
    return result,report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list',action='store_true')
    parser.add_argument('--batches',nargs='+',help='Requested manifest ids; sorted deterministically, never all implicitly')
    modes=parser.add_mutually_exclusive_group()
    modes.add_argument('--validate-only',action='store_true')
    modes.add_argument('--execute',action='store_true',help='Explicitly enable real execution through run_batch.py')
    parser.add_argument('--stop-on-error',action='store_true')
    args=parser.parse_args()
    directory=None;report=None
    try:
        require(sys.version_info[:2]==(3,12),'Python 3.12 required')
        found=discover()
        if args.list:
            require(not args.batches and not args.execute and not args.validate_only,'--list cannot execute or validate batches')
            for name,path in sorted(found.items()):print(name,':',path.relative_to(ROOT))
            return 0
        require(args.batches,'Explicit --batches selection required')
        require(args.validate_only or args.execute,'Choose --validate-only or explicitly enable --execute')
        require(len(args.batches)==len(set(args.batches)),'Duplicate batch selection')
        selected=sorted(args.batches)
        require(all(name in found for name in selected),'Unknown batch id')
        base=GAME/'build/pipeline';base.mkdir(parents=True,exist_ok=True)
        directory=base/('run-'+uuid.uuid4().hex);directory.mkdir(exist_ok=False)
        inputs=[Path(__file__).resolve(),RUNNER,GAME/'game.json']+[found[n] for n in selected]
        report={'status':'running','mode':'execute' if args.execute else 'validate_only','selected_batches':selected,'batches':[],
                'sha256':{str(p.relative_to(ROOT)):digest(p) for p in inputs},
                'limitations':['Current native return ABI is s32; arguments are u64.','Independent compilation timeout not implemented; runner worker/batch deadlines apply.','No additional pipeline-wide timeout; runner owns worker containment.','Failure diagnostics may contain generated game code; local build only.','Report discovery rejects concurrent same-batch output ambiguity.']}
        checkpoint(directory/'summary.json',report)
        for name in selected:
            batch_dir=directory/name;batch_dir.mkdir(exist_ok=False)
            item={'id':name,'manifest':str(found[name].relative_to(ROOT)),'status':'running','functions':[]};report['batches'].append(item)
            try:
                validation,_=invoke(found[name],'validate',batch_dir/'validation');item['validation']=validation
                require(validation['status']=='validated_only','Runner static validation failed')
                if not args.execute:item['status']='validated_only'
                else:
                    execution,global_report=invoke(found[name],'execute',batch_dir/'execution');item['execution']=execution
                    require(global_report is not None,'Missing execution report')
                    for index,row in enumerate(global_report.get('results',[])):
                        worker_path=Path(row['worker_report']);require(worker_path.resolve().is_relative_to((GAME/'build/batches').resolve()),'Unexpected worker report path')
                        actual=worker_path if worker_path.is_file() else Path(row.get('partial_worker_report',str(worker_path)))
                        require(actual.resolve().is_relative_to((GAME/'build/batches').resolve()),'Unexpected partial report path')
                        worker=json.loads(actual.read_bytes()) if actual.is_file() else {}
                        entry={'address':row.get('address'),'status':normalize(row),'worker_report':str(actual),'cases_executed':row.get('cases_executed',0),'cases_passed':row.get('cases_passed',0)}
                        if entry['status']=='structural_error' and worker:entry['status']=normalize(worker) if normalize(worker)=='coverage_failed' else entry['status']
                        item['functions'].append(entry)
                        if entry['status']!='passed':entry['diagnostic']=diagnostics(batch_dir/('failure-%02d'%index),entry,worker,row)
                    item['status']='passed' if execution['status']=='passed' and all(f['status']=='passed' for f in item['functions']) else 'failed'
            except Exception as exc:
                item['status']='structural_error';item['error']=repr(exc)
            if item['status'] not in ('passed','validated_only') and not any('diagnostic' in f for f in item['functions']):
                item['diagnostic']=diagnostics(batch_dir/'failure-batch',{'status':item['status']},{'stage':'validation' if 'execution' not in item else 'execution','error':item.get('error')},{})
                # Preserve runner-facing logs even when no function was started.
                for stage in ('validation','execution'):
                    for log in ('stdout.log','stderr.log'):
                        source=batch_dir/stage/log
                        if source.is_file():shutil.copyfile(source,batch_dir/'failure-batch'/(stage+'-'+log))
            checkpoint(directory/'summary.json',report)
            if item['status']=='structural_error' or args.stop_on_error and item['status'] not in ('passed','validated_only'):break
        for p in inputs:require(digest(p)==report['sha256'][str(p.relative_to(ROOT))],'Input changed during pipeline')
        ok=len(report['batches'])==len(selected) and all(b['status'] in ('passed','validated_only') for b in report['batches'])
        report['status']=('passed' if args.execute else 'validated_only') if ok else 'failed'
        report['cases_executed']=sum(b.get('execution',{}).get('cases_executed',0) for b in report['batches'])
        return 0 if ok else 1
    except Exception as exc:
        if report is not None:report['status']='structural_error';report['error']=repr(exc)
        print('Pipeline error:',repr(exc),file=sys.stderr);return 1
    finally:
        if report is not None and directory is not None:
            checkpoint(directory/'summary.json',report)
            print('Pipeline',report['status'],'report:',directory/'summary.json')

if __name__=='__main__':
    raise SystemExit(main())