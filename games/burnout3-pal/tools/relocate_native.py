"""PAL discovery integration: original port generator, immutable object reuse.

Only changed generated units and the optional audit wrapper are compiled.
Original CMake/Ninja commands and runtime libraries are reused, never modified.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import uuid

GAME=Path(__file__).resolve().parents[1]
ROOT=GAME.parents[1]
sys.path[:0]=[str(ROOT/'engine/tools'),str(ROOT/'engine')]
# cppcheck, imported by port.py, changes this verification-only environment
# variable. Preserve the caller's native runtime mode across that import.
_native_rounding=os.environ.get('PS2RT_EE_ROUND_NEAREST')
import port
if _native_rounding is None:os.environ.pop('PS2RT_EE_ROUND_NEAREST',None)
else:os.environ['PS2RT_EE_ROUND_NEAREST']=_native_rounding
from ps2decomp import gaps
from ps2decomp.r5900 import DISASM
import run_batch as runner
from pal_auto import atomic_json


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inside(path):
    path=Path(path).resolve()
    if not path.is_relative_to((GAME/'build').resolve()):raise ValueError('Path outside PAL build: '+str(path))
    return path


def argv(command):
    shell=ctypes.WinDLL('shell32',use_last_error=True)
    shell.CommandLineToArgvW.argtypes=[wintypes.LPCWSTR,ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype=ctypes.POINTER(wintypes.LPWSTR)
    kernel=ctypes.WinDLL('kernel32');kernel.LocalFree.argtypes=[ctypes.c_void_p]
    count=ctypes.c_int();ptr=shell.CommandLineToArgvW(command,ctypes.byref(count))
    if not ptr:raise ctypes.WinError(ctypes.get_last_error())
    try:return [ptr[i] for i in range(count.value)]
    finally:kernel.LocalFree(ptr)


def physical(address):
    return address&0x1fffffff if 0x80000000<=address<0xc0000000 else address


def discoveries(paths):
    pairs={};blocks=[];evidence={}
    pattern=re.compile(r'\[kernel kCopy \(0x5A\) a0=0x([0-9A-F]+) a1=0x([0-9A-F]+) a2=0x([0-9A-F]+)')
    for path in paths:
        path=inside(path);evidence[str(path)]=digest(path)
        for line in path.read_text().splitlines():
            if not line.strip():continue
            words=line.split()
            if len(words)!=2 or any(not re.fullmatch(r'(?:0[xX])?[0-9a-fA-F]{1,8}',x) for x in words):raise ValueError('Invalid discovery record')
            run,source=map(lambda x:int(x,16),words)
            if run%4 or source%4:raise ValueError('Unaligned discovery')
            if run in pairs and pairs[run]!=source:raise ValueError('Conflicting discovery identity')
            pairs[run]=source
        # Both the original probe and AutoTest are supported. Pin every stream
        # used as evidence; never copy/rename historical logs into host.log.
        for log in (path.parent/'host.log',path.parent/'stdout.log',path.parent/'stderr.log'):
            if not log.exists():continue
            evidence[str(log)]=digest(log)
            for m in pattern.finditer(log.read_text(errors='strict')):
                destination,source,size=(int(x,16) for x in m.groups())
                if size<=0 or size>0x10000 or size%4:raise ValueError('Invalid recorded copy size')
                row={'run':destination,'source':source,'size':size}
                if row not in blocks:blocks.append(row)
    if not pairs:raise ValueError('No discoveries selected')
    return pairs,blocks,evidence


def expand(game,pairs,blocks):
    elf=game.load_elf();section=elf.exec_sections[0];image=elf.section_data(section)
    result=dict(pairs);metadata={};pending=list(pairs)
    while pending:
        run=pending.pop();source=result[run]
        if run in metadata:continue
        offset=source-section.addr
        if offset<0 or offset+8>len(image):raise ValueError('Source outside executable section')
        block=next((b for b in blocks if 0<=physical(run)-physical(b['run'])<b['size'] and b['source']+physical(run)-physical(b['run'])==source),None)
        cap=block['size']-(source-block['source']) if block else gaps.MAX_FUNCTION
        code=image[offset:offset+cap]
        length=gaps.function_extent(code,run,run)
        full=gaps.function_extent(image[offset:offset+gaps.MAX_FUNCTION],run,run)
        if full!=length:raise ValueError('Original generator extent exceeds recorded copy')
        metadata[run]={'run':run,'source':source,'length':length,'verification':'NOT VERIFIED',
                       'evidence':'runtime discovery' if run in pairs else 'direct call from discovered function','block':block}
        if block:
            for off in range(0,length,4):
                ins=DISASM.decode(struct.unpack_from('<I',code,off)[0],run+off)
                if ins.is_call and ins.target is not None and physical(block['run'])<=physical(ins.target)<physical(block['run'])+block['size']:
                    target=ins.target;origin=block['source']+physical(target)-physical(block['run'])
                    if target in result and result[target]!=origin:raise ValueError('Conflicting static call mapping')
                    if target not in result:result[target]=origin;pending.append(target)
    return result,metadata


def audit_source(metadata):
    lines=['#define WIN32_LEAN_AND_MEAN','#define NOMINMAX','#include <windows.h>',
           '#include <cstdlib>','#include <filesystem>','#include <stdexcept>',
           '#include "ps2host/host.h"','#include "ps2host/memory.h"',
           'static void Dump(unsigned a, unsigned n, const wchar_t* file) {',
           'const char* dir=std::getenv("PAL_RELOCATION_AUDIT_DIR"); if(!dir) throw std::runtime_error("Missing relocation audit directory");',
           'auto p=ps2host::GuestMemory().Translate(a,n); if(!p) throw std::runtime_error("Relocation audit memory unavailable");',
           'auto path=std::filesystem::u8path(dir)/file;',
           'HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);',
           'if(h==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create exclusive relocation dump");',
           'DWORD written=0;BOOL ok=WriteFile(h,p,n,&written,nullptr);CloseHandle(h);',
           'if(!ok || written!=n) throw std::runtime_error("Incomplete relocation dump");','}']
    for run,m in sorted(metadata.items()):
        lines += [f'void F_{run:08X}(lifted::Context&);',f'static void Audit_{run:08X}(lifted::Context& c) {{ static bool done=false; if(!done) {{']
        b=m['block']
        if b and physical(run)==physical(b['run']):lines.append(f'Dump(0x{run:08X}u,{b["size"]}u,L"block_{run:08X}.bin");')
        lines += [f'Dump(0x{run:08X}u,{m["length"]}u,L"relocated_{run:08X}.bin");done=true;}} F_{run:08X}(c);}}']
    lines += ['void RegisterRelocationAudits() {']
    for run in sorted(metadata):lines.append(f'ps2host::Override(0x{run:08X}u,Audit_{run:08X},"audited relocated code");')
    return '\n'.join(lines+['}'])+'\n'


def compile_arguments(entry,source,target,directory):
    args=argv(entry['command']);original=Path(entry['file'])
    if sum(Path(x)==original for x in args)!=1:raise ValueError('Ambiguous compiler source argument')
    args=[str(source) if Path(x)==original else '/Fo'+str(target) if x.lower().startswith('/fo') else '/Fd'+str(directory)+'/' if x.lower().startswith('/fd') else x for x in args]
    args.append('/DPAL_RELOCATION_AUDIT')
    return args


def database(ninja,base,rule):
    result=subprocess.run([ninja,'-C',str(base),'-t','compdb','-x',rule],capture_output=True,text=True,check=True)
    return json.loads(result.stdout)


def prepare(base,paths):
    base=inside(base);old=base/'port/cmake';old_report=json.loads((base/'build.json').read_bytes())
    sources=inside(old_report['sources'])
    for name,sha in old_report['sha256'].items():
        if digest(ROOT/name)!=sha:raise ValueError('Historical build input changed: '+name)
    game=port.game_config.load(str(GAME));pairs,blocks,evidence=discoveries(paths);pairs,metadata=expand(game,pairs,blocks)
    static=set(port.translated_functions(str(sources)))
    if static&pairs.keys():raise ValueError('Discovered address collides with existing translation')
    directory=GAME/'build/native-relocated'/('run-'+uuid.uuid4().hex[:12]);directory.mkdir(parents=True)
    game.build=str(directory.relative_to(GAME));game.generated=str(sources.parent/'generated')
    pairfile=directory/'port/discovered.txt';pairfile.parent.mkdir()
    pairfile.write_text(''.join(f'{run:08X} {source:08X}\n' for run,source in sorted(pairs.items())))
    with (directory/'generation.log').open('x') as log:
        import contextlib
        with contextlib.redirect_stdout(log):generated=Path(port.generate(game))
    text=(generated/'discovered.cpp').read_text()
    emitted={int(x,16) for x in re.findall(r'^void F_([0-9A-F]{8})\(',text,re.M)}
    if emitted!=set(pairs):raise ValueError('Original generator did not emit every discovered function')
    (generated/'relocation_audit.cpp').write_text(audit_source(metadata))
    ninja=shutil.which('ninja.exe')
    if not ninja:raise ValueError('Ninja unavailable')
    graph=(old/'build.ninja').read_text();compile_rule=re.search(r': (CXX_COMPILER__burnout3-pal_\w+)',graph)[1]
    link_rule=re.search(r'^build burnout3-pal.exe: (\S+)',graph,re.M)[1]
    compdb=database(ninja,old,compile_rule);link=database(ninja,old,link_rule)
    if len(link)!=1:raise ValueError('Ambiguous original link command')
    outputs={};commands=[];pins=dict(evidence)
    for entry in compdb:
        source=Path(entry['file']);original_obj=old/entry['output'];replacement=None
        if source.parent==base/'port/generated':
            replacement=generated/source.name
            if not replacement.exists():raise ValueError('Missing generated unit')
        elif source==GAME/'port/overrides.cpp':replacement=source
        changed=replacement is not None and (source.name=='overrides.cpp' or digest(source)!=digest(replacement))
        if changed:
            target=directory/(source.name+'.obj')
            args=compile_arguments(entry,replacement,target,directory)
            commands.append(args);outputs[str(original_obj.resolve())]=str(target)
        else:pins[str(original_obj.resolve())]=digest(original_obj)
    template=next(x for x in compdb if Path(x['file']).name=='discovered.cpp')
    target=directory/'relocation_audit.cpp.obj'
    commands.append(compile_arguments(template,generated/'relocation_audit.cpp',target,directory))
    raw=link[0]['command'];parts=raw.split(' && ')
    if len(parts)!=3:raise ValueError('Unexpected CMake link wrapper')
    args=argv(parts[1]);separator=args.index('--');prefix=args[:separator+2];linkargs=args[separator+2:];rebased=[]
    for x in prefix:
        rebased.append('--intdir='+str(directory/'link-work') if x.startswith('--intdir=') else x)
    (directory/'link-work').mkdir();inputs=[];options=[]
    for x in linkargs:
        if x.lower().startswith(('/out:','/implib:','/pdb:')):
            key=x.split(':',1)[0];suffix={'/out':'.exe','/implib':'.lib','/pdb':'.pdb'}[key.lower()];options.append(key+':'+str(directory/('burnout3-pal'+suffix)))
        elif x.startswith('/'):options.append(x)
        else:
            path=(old/x).resolve()
            if str(path) in outputs:inputs.append(outputs[str(path)])
            elif path.exists():inputs.append(str(path));pins[str(path)]=digest(path)
            elif '\\' in x or '/' in x:raise ValueError('Missing original link input: '+x)
            else:inputs.append(x)
    inputs.append(str(target));rsp=directory/'link-inputs.rsp';rsp.write_text(subprocess.list2cmdline(inputs),encoding='utf-8')
    commands.append(rebased+['@'+str(rsp)]+options)
    for f in [GAME/'game.json',Path(game.elf_path),Path(game.database_path),Path(__file__),GAME/'port/overrides.cpp',Path(port.__file__),old/'build.ninja',*(generated.glob('*.cpp'))]:pins[str(f)]=digest(f)
    config={'schema':1,'directory':str(directory),'base':str(base),'sources':str(sources),'commands':commands,'pins':pins,
            'metadata':{str(a):m for a,m in metadata.items()},'original_functions':len(static),'verification':'NOT VERIFIED',
            'host_tests':str(old/'ps2host/ps2host_tests.exe'),'vcvars':port.cppcheck.find_vcvars()}
    config['pins'][config['host_tests']]=digest(config['host_tests'])
    atomic_json(directory/'work.json',config);return directory/'work.json'


def worker(path):
    config=json.loads(path.read_bytes());directory=inside(config['directory']);report={'status':'started','commands':[]}
    try:
        (directory/'temp').mkdir()
        batch=directory/'environment.bat'
        if any(x in config['vcvars'] for x in ('"','\n','\r','%')):raise ValueError('Unsafe compiler environment path')
        batch.write_text('@echo off\ncall "'+config['vcvars']+'" >nul\nif errorlevel 1 exit /b 1\nset\n')
        result=subprocess.run(['cmd','/d','/c',str(batch)],capture_output=True,text=True)
        if result.returncode:raise ValueError('Compiler environment failed: '+result.stderr[:1000])
        env=dict(line.split('=',1) for line in result.stdout.splitlines() if '=' in line and not line.startswith('='))
        env.update(TEMP=str(directory/'temp'),TMP=str(directory/'temp'))
        if 'PS2RT_EE_ROUND_NEAREST' in env:raise ValueError('Verification-only floating point environment present')
        for name,sha in config['pins'].items():
            if digest(name)!=sha:raise ValueError('Pinned input changed: '+name)
        for n,args in enumerate(config['commands']+[[config['host_tests']]]):
            result=subprocess.run(args,cwd=directory,env=env,capture_output=True,text=True)
            for stream in ('stdout','stderr'):(directory/('%02d-%s.log'%(n,stream))).write_text(getattr(result,stream),encoding='utf-8')
            report['commands'].append({'command':args,'exit_code':result.returncode})
            if result.returncode:raise ValueError(('Host tests' if n==len(config['commands']) else 'Compile/link')+' failed: '+str(result.returncode))
            if n==len(config['commands']) and 'host tests: all passed' not in result.stdout:raise ValueError('Incomplete host test evidence')
        for name,sha in config['pins'].items():
            if digest(name)!=sha:raise ValueError('Input changed during build: '+name)
        report.update(status='built_and_host_tests_passed',executable=str(directory/'burnout3-pal.exe'),executable_sha256=digest(directory/'burnout3-pal.exe'))
        return 0
    except Exception as exc:report.update(status='failed',error=str(exc));return 1
    finally:atomic_json(directory/'build.json',report)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--base-build',type=Path);parser.add_argument('--discoveries',type=Path,action='append');parser.add_argument('--_worker',type=Path)
    args=parser.parse_args()
    if args._worker:return worker(inside(args._worker))
    if not args.base_build or not args.discoveries:parser.error('Select --base-build and one or more --discoveries')
    path=prepare(args.base_build,args.discoveries);print(path.parent,flush=True)
    row={};runner.run_windows_job([sys.executable,'-B',str(Path(__file__).resolve()),'--_worker',str(path)],600,row)
    atomic_json(path.parent/'containment.json',row)
    print(json.dumps({k:row.get(k) for k in ('exit_code','timeout_detected','cleanup_succeeded','active_processes_after_cleanup')}),flush=True)
    return 0 if row.get('exit_code')==0 and row.get('cleanup_succeeded') and not row.get('timeout_detected') and row.get('active_processes_after_cleanup')==0 else 1


if __name__=='__main__':raise SystemExit(main())
