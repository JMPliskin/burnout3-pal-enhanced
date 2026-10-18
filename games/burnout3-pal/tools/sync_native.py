"""Build an integrated PAL worktree from preserved source artifacts, never old objects.

The reference checkout is read-only. Output defaults to build/; --build-root
selects a different parent for a new, exclusive build directory.
The engine checkout must match the integration index's gitlink exactly.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

GAME=Path(__file__).resolve().parents[1]
ROOT=GAME.parents[1]
sys.path[:0]=[str(ROOT/'engine/tools'),str(ROOT/'engine')]
import run_batch as runner
import vurecomp

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def write(path,value):Path(path).write_text(json.dumps(value,indent=2),encoding='utf-8')
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args],text=True).strip()

def build_directory(build_root=None):
    parent=ROOT/'build' if build_root is None else build_root.resolve()
    directory=parent/('pal-upstream-'+uuid.uuid4().hex[:10])
    # Never reuse a prior build, even if the generated identifier collides.
    directory.mkdir(parents=True,exist_ok=False)
    return directory

def prepare(reference,build_root=None):
    reference=reference.resolve()
    if reference==ROOT or not (reference/'games/burnout3-pal').is_dir():raise ValueError('Select a separate preserved PAL checkout')
    engine=subprocess.check_output(['git','-C',str(ROOT/'engine'),'rev-parse','HEAD'],text=True).strip()
    expected=git('ls-files','--stage','engine').split()[1]
    if engine!=expected:raise ValueError('Engine checkout differs from integration gitlink')
    previous=reference/'games/burnout3-pal/build/native-relocated/run-75fc2363eedf'
    old=json.loads((previous/'work.json').read_bytes());old_build=json.loads((previous/'build.json').read_bytes())
    if old_build['status']!='built_and_host_tests_passed':raise ValueError('Historical build is not confirmed')
    for p,h in old['pins'].items():
        if sha(p)!=h:raise ValueError('Historical input changed: '+p)
    if sha(old_build['executable'])!=old_build['executable_sha256']:raise ValueError('Historical executable changed')
    d=build_directory(build_root)
    generated=d/'generated';generated.mkdir();cmake=d/'cmake'
    vu=reference/'games/burnout3-pal/build/vu-captures/run-1d27d7abf9a5/vu_programs.txt'
    if sha(vu)!='deff4afda9b0d277532c222ff8e56919d5829743a850122c0ef1d10cf98d1a82':raise ValueError('Original VU capture changed')
    shutil.copy2(vu,d/'vu_programs.txt')
    programs=vurecomp.read_manifest(str(d/'vu_programs.txt'))
    if len(programs)!=97:raise ValueError('Unexpected VU capture inventory')
    vurecomp.main(['--manifest',str(d/'vu_programs.txt'),'--out-dir',str(generated)])
    # Dispatch and switch completion come from the already validated PAL build.
    original=reference/'games/burnout3-pal/build/native-relocated/run-180cb57a8a54'
    for name in ['dispatch.cpp','dispatch_overlays.cpp','discovered.cpp','relocation_audit.cpp']:
        shutil.copy2(original/'port/generated'/name,generated/name)
    sources=sorted(Path(old['sources']).glob('*.cpp'))
    completed=original/'bulk_011_000.cpp'
    if not completed.is_file():raise ValueError('Preserved worker completion missing')
    sources=[completed if p.name==completed.name else p for p in sources]
    import port
    addresses=port.translated_functions(str(Path(old['sources'])))
    if len(addresses)!=len(set(addresses)) or len(addresses)!=old['original_functions']:raise ValueError('Unexpected preserved EE inventory')
    (d/'pal_sources.txt').write_text('\n'.join(p.as_posix() for p in sources)+'\n')
    # Reuse only dependency SOURCES, not incompatible binary libraries or objects.
    cache=Path(old['base'])/'port/cmake'
    configure=['cmake','-S',str(GAME/'port'),'-B',str(cmake),'-G','Ninja',
               '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
               '-DPS2_GENERATED_DIR='+str(generated),'-DPS2_PAL_SOURCES_FILE='+str(d/'pal_sources.txt'),
               '-DPython3_EXECUTABLE='+sys.executable,'-DFETCHCONTENT_FULLY_DISCONNECTED=ON']
    for name in ['vulkan_headers','volk','sdl3','glslang']:
        source=cache/'_deps'/(name.replace('_','-')+'-src')
        if name=='vulkan_headers':source=cache/'_deps/vulkan_headers-src'
        if not source.is_dir():raise ValueError('Cached dependency sources missing: '+str(source))
        configure.append('-DFETCHCONTENT_SOURCE_DIR_'+name.upper()+'='+str(source))
    headers=cmake/'ps2host/third_party';headers.mkdir(parents=True)
    for p in (cache/'ps2host/third_party').glob('*.h'):shutil.copy2(p,headers/p.name)
    pins={str(p):sha(p) for p in [*sources,*generated.glob('*.cpp'),vu,Path(old_build['executable']),GAME/'port/overrides.cpp']}
    merge=subprocess.run(['git','-C',str(ROOT),'rev-parse','--verify','-q','MERGE_HEAD'],capture_output=True,text=True)
    upstream=merge.stdout.strip() if merge.returncode==0 else git('merge-base','HEAD','upstream/master')
    config={'schema':1,'directory':str(d),'reference':str(reference),'engine':engine,'upstream':upstream,
            'base':git('rev-parse','HEAD'),'vcvars':old['vcvars'],'configure':configure,'pins':pins,
            'vu_programs':len(programs),'vu_manifest':str(vu),'vu_sha256':sha(vu),'EE_functions':len(addresses),
            'old_objects_reused':False,'executable':str(cmake/'burnout3-pal.exe'),
            'tests':[str(cmake/'ps2host/ps2host_tests.exe'),str(cmake/'ps2gpu/ps2gpu_tests.exe')]}
    write(d/'work.json',config)
    print(d/'work.json',flush=True)
    return d/'work.json'

def worker(path):
    q=json.loads(path.read_bytes());d=Path(q['directory'])
    batch=d/'environment.bat'
    if not batch.exists():batch.write_text('@echo off\ncall "'+q['vcvars']+'" >nul\nif errorlevel 1 exit /b 1\nset\n')
    result=subprocess.run(['cmd','/d','/c',str(batch)],capture_output=True,text=True,check=True)
    env=dict(x.split('=',1) for x in result.stdout.splitlines() if '=' in x and not x.startswith('='))
    (d/'temp').mkdir(exist_ok=True);env.update(TEMP=str(d/'temp'),TMP=str(d/'temp'))
    env.pop('PS2RT_EE_ROUND_NEAREST',None)
    with Path(q['stdout']).open('xb') as out,Path(q['stderr']).open('xb') as err:
        return subprocess.run(q['command'],cwd=d,env=env,stdout=out,stderr=err).returncode

def execute(path):
    cfg=json.loads(path.read_bytes());d=Path(cfg['directory']);report={'status':'running','steps':[]}
    def run(name,command,timeout):
        task=dict(directory=str(d),vcvars=cfg['vcvars'],command=command,stdout=str(d/(name+'.stdout.log')),stderr=str(d/(name+'.stderr.log')))
        p=d/(name+'.json');write(p,task);row={'name':name,'command':command}
        runner.run_windows_job([sys.executable,'-B',str(Path(__file__).resolve()),'--worker',str(p)],timeout,row)
        report['steps'].append(row);write(d/'build.json',report)
        if row.get('exit_code')!=0 or row.get('timeout_detected') or not row.get('cleanup_succeeded'):raise ValueError(name+' failed; see '+str(d))
        print(name+': passed',flush=True)
    try:
        for p,h in cfg['pins'].items():
            if sha(p)!=h:raise ValueError('Input changed: '+p)
        run('configure',cfg['configure'],180)
        run('compile',['cmake','--build',str(d/'cmake'),'--target','burnout3-pal','ps2host_tests','ps2gpu_tests','-j','2'],2400)
        for name,exe in zip(['host-tests','gpu-tests'],cfg['tests']):run(name,[exe],240)
        if 'host tests: all passed' not in (d/'host-tests.stdout.log').read_text():raise ValueError('Incomplete host test report')
        for p,h in cfg['pins'].items():
            if sha(p)!=h:raise ValueError('Input changed: '+p)
        report.update(status='built_and_tested',executable=cfg['executable'],executable_sha256=sha(cfg['executable']),engine=cfg['engine'])
    except Exception as exc:report.update(status='failed',error=str(exc));raise
    finally:write(d/'build.json',report)

def main():
    p=argparse.ArgumentParser(description=__doc__);a=p.add_mutually_exclusive_group(required=True)
    a.add_argument('--prepare',type=Path,metavar='REFERENCE_ROOT');a.add_argument('--execute',type=Path);a.add_argument('--worker',type=Path,help=argparse.SUPPRESS)
    p.add_argument('--build-root',type=Path,metavar='PATH',help='Parent for a new build (only with --prepare; default: repository build/)')
    args=p.parse_args()
    if args.build_root is not None and args.prepare is None:p.error('--build-root requires --prepare; --execute uses work.json paths')
    if args.prepare:
        if args.build_root is None:prepare(args.prepare)
        else:prepare(args.prepare,build_root=args.build_root)
    elif args.execute:execute(args.execute.resolve())
    else:return worker(args.worker.resolve())
    return 0
if __name__=='__main__':raise SystemExit(main())
