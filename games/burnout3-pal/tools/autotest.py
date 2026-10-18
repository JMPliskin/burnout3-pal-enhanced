"""Bounded local PAL checks. QUICK never compiles; FULL reuses immutable objects.
No lifting, automated fixes, regression promotion or graphics baselines.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid
import run_batch as runner
from pal_auto import atomic_json

GAME=Path(__file__).resolve().parents[1]
ROOT=GAME.parents[1]
BUILD=GAME/'build'
TOOLS=Path(__file__).resolve().parent

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def local(path):
 path=Path(path).resolve()
 if not path.is_relative_to(BUILD.resolve()):raise ValueError('Artifact outside PAL build')
 return path

def clean(row):
 return row.get('cleanup_succeeded') is True and row.get('active_processes_after_cleanup')==0 and row.get('timeout_detected') is False

def confirmed(path):
 path=local(path);report=json.loads(path.read_bytes());work=json.loads((path.parent/'work.json').read_bytes())
 containment=json.loads((path.parent/'containment.json').read_bytes())
 if report.get('status')!='built_and_host_tests_passed' or not clean(containment) or containment.get('exit_code')!=0:raise ValueError('Build not confirmed')
 exe=local(report['executable'])
 if exe.parent!=path.parent or sha(exe)!=report['executable_sha256']:raise ValueError('Executable hash mismatch')
 commands=report.get('commands',[])
 if len(commands)!=len(work['commands'])+1 or any(c.get('exit_code')!=0 for c in commands):raise ValueError('Incomplete build evidence')
 if [x['command'] for x in commands]!=work['commands']+[[work['host_tests']]]:raise ValueError('Build command identity mismatch')
 if 'host tests: all passed' not in (path.parent/('%02d-stdout.log'%len(work['commands']))).read_text():raise ValueError('Missing host success evidence')
 compatible=True
 if not work.get('pins'):raise ValueError('Missing build input hashes')
 for name,digest in work['pins'].items():
  p=Path(name).resolve()
  # Mutable PAL tools/override changes require FULL rebuilding, but do not
  # invalidate a historical executable's successful build record.
  mutable=p.is_relative_to((GAME/'tools').resolve()) or p==GAME/'port/overrides.cpp'
  if not p.is_file() or sha(p)!=digest:
   if mutable:compatible=False
   else:raise ValueError('Pinned build artifact changed: '+str(p))
 return {'manifest':path,'report':report,'work':work,'executable':exe,'compatible':compatible}

def select(path=None):
 if path:return confirmed(path)
 errors=[]
 candidates=sorted((BUILD/'native-relocated').glob('run-*/build.json'),key=lambda p:(p.stat().st_mtime_ns,str(p)),reverse=True)
 for p in candidates:
  try:return confirmed(p)
  except (ValueError,KeyError,OSError,TypeError) as exc:errors.append(str(p)+': '+str(exc))
 raise ValueError('No confirmed relocated PAL build. '+ '; '.join(errors[:3]))

def classify(text,code,containment):
 reason=re.search(r'^stopped:\s*(.*)$',text,re.M)
 first=reason.group(1) if reason else None
 category='unknown';evidence=first
 if containment.get('timeout_detected') or (first and any(x in first.lower() for x in ('timeout','time limit'))):category='timeout'
 elif not clean(containment):category='infrastructure_error';evidence='Process cleanup not confirmed'
 elif code is not None and (int(code)&0xffffffff)>=0xc0000000:category='windows_crash';evidence='Windows exit 0x%08X'%(int(code)&0xffffffff)
 elif re.search(r'error LNK\d+',text):category='link_error'
 elif re.search(r'(?:fatal )?error C\d+',text):category='cpp_compile_error'
 elif 'unsupported instruction' in text:category='unsupported_instruction'
 elif first and ('outside guest memory' in first or 'memory fault' in first):category='invalid_memory_access'
 elif first and ('not translated' in first or 'no function covers' in first):
  category='missing_dispatch'
  copies=re.findall(r'discovered: code at (0x[0-9A-Fa-f]+) is a copy of (0x[0-9A-Fa-f]+)',text)
  if any(int(a,16)!=int(b,16) for a,b in copies):category='relocated_code_missing'
 elif first and re.search(r'(not implemented|unimplemented).*(kernel|IOP|RPC)|(kernel|IOP|RPC).*(not implemented|unimplemented)',first,re.I):category='unimplemented_service'
 elif re.search(r'FAILED \(|host tests:.*fail',text,re.I):category='test_error'
 elif code==0 and first:category='host_stop'
 m=re.search(r'functions executed: (\d+) distinct',text)
 gs=re.search(r'GS: (\d+) primitives, (\d+) pixels',text)
 vu=re.search(r'VU: (\d+) instruction pairs',text)
 iop=re.search(r'IOP ([\d.]+)%',text)
 return {'category':category,'confirmed_evidence':evidence,'hypotheses':[],
         'first_blocker':first,'distinct_functions':int(m[1]) if m else None,
         'activity':{'kernel':'kernel services used:' in text,'iop':float(iop[1])>0 if iop else None,
                     'gs':int(gs[1])>0 or int(gs[2])>0 if gs else None,'vu':int(vu[1])>0 if vu else None}}

def worker(config):
 config=local(config);job=json.loads(config.read_bytes());directory=config.parent
 env=dict(os.environ,TEMP=str(directory/'temp'),TMP=str(directory/'temp'),PAL_RELOCATION_AUDIT_DIR=str(directory))
 if 'PS2RT_EE_ROUND_NEAREST' in env:raise ValueError('Verification-only floating point environment present')
 with (directory/'stdout.log').open('x',encoding='utf-8') as out,(directory/'stderr.log').open('x',encoding='utf-8') as err:
  return subprocess.run(job['command'],cwd=ROOT,env=env,stdout=out,stderr=err).returncode

def launch(directory,command,timeout):
 directory.mkdir(parents=True,exist_ok=False);(directory/'temp').mkdir()
 config=directory/'launch.json';atomic_json(config,{'command':command})
 row={};start=time.monotonic()
 runner.run_windows_job([sys.executable,'-B',str(Path(__file__).resolve()),'--_worker',str(config)],timeout,row)
 row['elapsed_seconds']=time.monotonic()-start;atomic_json(directory/'containment.json',row)
 return row

def compare(current,baseline):
 if not baseline:return {'available':False,'possible_regression':False}
 old=baseline.get('diagnosis',{});n=current.get('distinct_functions');before=old.get('distinct_functions')
 return {'available':True,'previous_functions':before,'current_functions':n,
         'possible_regression':n is not None and before is not None and n<before,
         'note':'Function count is a diagnostic signal, not a gameplay equivalence test.'}

def baseline_for():
 for path in sorted((BUILD/'autotest').glob('run-*/summary.json'),key=lambda p:p.stat().st_mtime_ns,reverse=True):
  try:
   old=json.loads(path.read_bytes());manifest=json.loads((path.parent/'manifest.json').read_bytes())
   # Only compare runs using the same ELF and bounded launch configuration.
   if old.get('status')=='diagnosed' and old.get('containment_clean') and manifest.get('configuration')=={'timeout':3,'budget':1000000}:
    game=runner.load(str(GAME))
    confirmed(manifest['build_manifest'])
    if sha(manifest['build_manifest'])!=manifest['build_manifest_sha256']:continue
    if not manifest.get('evidence'):continue
    if any(sha(local(path.parent/name))!=digest for name,digest in manifest['evidence'].items()):continue
    row=json.loads((path.parent/'probe/containment.json').read_bytes())
    if not clean(row) or row.get('exit_code')!=old.get('exit_code'):continue
    if manifest.get('elf_sha256')==sha(game.elf_path):return old,str(path)
  except (OSError,ValueError,KeyError):continue
 return None,None

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--mode',choices=['QUICK','FULL'],default='QUICK');parser.add_argument('--timeout',type=int,default=3)
 parser.add_argument('--build-manifest',type=Path);parser.add_argument('--_worker',type=Path,help=argparse.SUPPRESS)
 parser.add_argument('--dump-ram',action='store_true',help='Preserve final EE RAM in this run for resource diagnostics')
 args=parser.parse_args()
 if args._worker:return worker(args._worker)
 if not 1<=args.timeout<=60:parser.error('Timeout must be 1..60 seconds')
 directory=BUILD/'autotest'/('run-'+uuid.uuid4().hex[:12]);directory.mkdir(parents=True)
 started=time.monotonic();summary={'schema':1,'mode':args.mode,'status':'infrastructure_error'}
 manifest={'schema':1,'created_utc':datetime.now(timezone.utc).isoformat(),'configuration':{'timeout':args.timeout,'budget':1000000},'compilation_jobs':1,'maximum_compilation_jobs':2}
 try:
  selected=select(args.build_manifest)
  manifest['initial_build_manifest']=str(selected['manifest'])
  if args.mode=='FULL':
   for test in ['test_autotest.py','test_relocate_native.py','test_kernel_store.py','test_logitech.py','test_memcard.py','test_switch_completion.py']:
    row=launch(directory/test.removesuffix('.py'),[sys.executable,'-B',str(TOOLS/test),'-v'],120)
    if not clean(row) or row.get('exit_code')!=0:summary.update(status='test_error',failed_test=test);raise ValueError('Specific tests failed')
   if not selected['compatible']:
    work=selected['work'];command=[sys.executable,'-B',str(TOOLS/'relocate_native.py'),'--base-build',work['base']]
    paths=[Path(p) for p in work['pins'] if Path(p).name=='discovered.txt']
    if not paths:raise ValueError('No pinned runtime discoveries for incremental build')
    for p in paths:command+=['--discoveries',str(p)]
    for start,end in work.get('completions',[]):command+=['--complete-function',f'{start:08X}:{end:08X}']
    if work.get('vu'):command+=['--vu-manifest',work['vu']['manifest']]
    row=launch(directory/'build',command,660)
    if not clean(row) or row.get('exit_code')!=0:
     text='\n'.join(p.read_text(errors='replace') for p in (directory/'build').glob('*std*.log'))
     summary.update(status='build_error',diagnosis=classify(text,row.get('exit_code'),row));raise ValueError('Incremental build failed')
    selected=select()
    if not selected['compatible']:raise ValueError('New build inputs incompatible')
    manifest['build_operation']='immutable CMake/Ninja object reuse; changed units compiled sequentially'
   else:manifest['build_operation']='no-op: all pinned inputs unchanged'
  game=runner.load(str(GAME));exe=selected['executable'];elf=Path(game.elf_path).resolve()
  manifest.update(build_manifest=str(selected['manifest']),build_manifest_sha256=sha(selected['manifest']),executable=str(exe),executable_sha256=sha(exe),elf=str(elf),elf_sha256=sha(elf),historical_inputs_current=selected['compatible'],git_revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),tool_sha256=sha(__file__))
  command=[str(exe),str(elf),'--timeout',str(args.timeout),'--budget','1000000','--virtual-time','--gs-inline','--vif1-sync','--trace','--discover',str(directory/'probe/discovered.txt')]
  if args.dump_ram:command+=['--dump-ram',str(directory/'probe/ee-ram.bin')]
  if game.iso and Path(game.path(game.iso)).is_file():command+=['--disc',str(Path(game.path(game.iso)).resolve())]
  baseline,baseline_path=baseline_for() if args.timeout==3 else (None,None)
  confirmed(selected['manifest'])
  if sha(exe)!=manifest['executable_sha256'] or sha(elf)!=manifest['elf_sha256']:raise ValueError('Launch input changed')
  manifest['command']=command
  row=launch(directory/'probe',command,args.timeout+12)
  if sha(exe)!=manifest['executable_sha256'] or sha(elf)!=manifest['elf_sha256']:raise ValueError('Launch input changed during execution')
  for name in ['stdout.log','stderr.log']:
   (directory/name).write_bytes((directory/'probe'/name).read_bytes() if (directory/'probe'/name).exists() else b'')
  diagnosis=classify((directory/'stdout.log').read_text(errors='replace')+'\n'+(directory/'stderr.log').read_text(errors='replace'),row.get('exit_code'),row)
  summary.update(status='diagnosed' if clean(row) else 'infrastructure_error',diagnosis=diagnosis,exit_code=row.get('exit_code'),containment_clean=clean(row),comparison=compare(diagnosis,baseline),baseline=baseline_path)
  manifest['evidence']={str(p.relative_to(directory)):sha(p) for p in (directory/'probe').iterdir() if p.is_file()}
 except Exception as exc:summary['error']=str(exc)
 finally:
  manifest['duration_seconds']=time.monotonic()-started
  atomic_json(directory/'manifest.json',manifest);atomic_json(directory/'summary.json',summary)
  message='# PAL AutoTest\n\n'+json.dumps(summary,indent=2)+'\n'
  (directory/'summary.md').write_text(message,encoding='utf-8')
  (directory/'codex-next-task.md').write_text('Inspect the confirmed blocker in summary.json. No automatic fix was attempted.\n\nReport: '+str(directory/'summary.json')+'\nFull logs: stdout.log and stderr.log.\n'+json.dumps(summary.get('diagnosis',{'error':summary.get('error')}),indent=2),encoding='utf-8')
  for name in ['stdout.log','stderr.log']:
   if not (directory/name).exists():(directory/name).write_text('')
 print(directory);print(json.dumps(summary,indent=2))
 return 0 if summary['status']=='diagnosed' and summary.get('exit_code')==0 and not summary['comparison']['possible_regression'] else 1

if __name__=='__main__':raise SystemExit(main())
