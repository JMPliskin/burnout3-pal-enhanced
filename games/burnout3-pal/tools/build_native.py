"""Build the existing PAL emission with port.py in an exclusive build directory.

No lifting, discovery execution, regional hooks or game input changes.
Accept standalone CMake/Ninja when Visual Studio's bundled tools are absent.
"""
import contextlib
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
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


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources',type=Path,help='Complete lifted directory under PAL build; original emission is untouched')
    parser.add_argument('--jobs',type=int,default=2)
    args=parser.parse_args()
    if not 1<=args.jobs<=4:parser.error('Use one to four build jobs')
    sources=args.sources.resolve() if args.sources else GAME/'src/lifted'
    if args.sources and (not sources.is_relative_to((GAME/'build').resolve()) or sources.name!='lifted'):
        parser.error('Staged sources must be a lifted directory under PAL build')
    if not sources.is_dir():parser.error('Missing source directory')
    addresses=port.translated_functions(str(sources))
    # Check duplicates without importing the verifier (which changes FPU env).
    symbols=[m.group(1) for p in sources.glob('*.cpp')
             for m in port._FUNCTION.finditer(p.read_text(encoding='utf-8'))]
    if len(symbols)!=len(set(symbols)):parser.error('Duplicate translated symbols')
    directory=GAME/'build/native-bootstrap'/('run-'+uuid.uuid4().hex[:12])
    directory.mkdir(parents=True,exist_ok=False)
    game=port.game_config.load(str(GAME))
    game.build=str(directory.relative_to(GAME))
    game.generated=str(sources.parent/'generated')
    original_tool=port._visual_studio_tool
    original_run=port.subprocess.run
    tools={};commands=[]
    def tool(*parts):
        bundled=original_tool(*parts)
        chosen=bundled if Path(bundled).is_file() else shutil.which(parts[-1])
        if not chosen:raise RuntimeError('Missing build tool: '+parts[-1])
        tools[parts[-1]]=chosen
        return chosen
    def run(*args,**kwargs):
        kwargs['env']=dict(os.environ,CMAKE_BUILD_PARALLEL_LEVEL=str(main_jobs))
        result=original_run(*args,**kwargs)
        number=len(commands)
        commands.append({'command':args[0],'exit_code':result.returncode})
        for stream in ('stdout','stderr'):
            (directory/('%02d-%s.log'%(number,stream))).write_text(getattr(result,stream) or '',encoding='utf-8')
        return result
    main_jobs=args.jobs
    report={'status':'started','tools':tools,'commands':commands,'build_jobs':args.jobs,'sources':str(sources)}
    for path in [GAME/'game.json',Path(game.elf_path),Path(game.database_path),
                 *sorted(sources.glob('*.cpp'))]:
        report.setdefault('sha256',{})[str(path.relative_to(ROOT))]=hashlib.sha256(path.read_bytes()).hexdigest()
    print(directory,flush=True)
    try:
        port._visual_studio_tool=tool;port.subprocess.run=run
        with (directory/'driver.log').open('x',encoding='utf-8') as log:
            with contextlib.redirect_stdout(log),contextlib.redirect_stderr(log):
                generated=port.generate(game)
                (Path(generated)/'pal_sources.txt').write_text(str(sources).replace('\\','/'),encoding='utf-8')
                report['translated_addresses']=['0x%08X'%a for a in addresses]
                report['executable']=port.build(game,generated)
        report['status']='built_and_host_tests_passed'
        return 0
    except (Exception,SystemExit) as exc:
        report.update(status='failed',error=str(exc));print(str(exc),file=sys.stderr)
        return 1
    finally:
        port._visual_studio_tool=original_tool;port.subprocess.run=original_run
        (directory/'build.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':raise SystemExit(main())
