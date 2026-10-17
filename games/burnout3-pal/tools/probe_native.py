"""One bounded, headless PAL bootstrap; no translation or automatic retries."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

import run_batch as runner


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    modes=parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--executable',type=Path)
    modes.add_argument('--_worker',type=Path,help=argparse.SUPPRESS)
    parser.add_argument('--discover',action='store_true',help='Record discoveries in this exclusive probe directory')
    args=parser.parse_args()
    build=(runner.GAME/'build').resolve()
    if args._worker:
        config_path=args._worker.resolve()
        if not config_path.is_relative_to(build):parser.error('Worker configuration must be under PAL build')
        config=json.loads(config_path.read_bytes())
        with (config_path.parent/'host.log').open('x',encoding='utf-8') as log:
            return subprocess.run(config['command'],stdout=log,stderr=subprocess.STDOUT,cwd=config_path.parent,env=dict(os.environ,PAL_RELOCATION_AUDIT_DIR=str(config_path.parent))).returncode
    executable=args.executable.resolve()
    if not executable.is_relative_to(build) or not executable.is_file():
        parser.error('Executable must exist under the PAL build directory')
    game=runner.load(str(runner.GAME));elf=Path(game.elf_path).resolve()
    directory=build/'native-probes'/('run-'+uuid.uuid4().hex[:12]);directory.mkdir(parents=True,exist_ok=False)
    command=[str(executable),str(elf),'--timeout','3','--budget','1000000',
             '--virtual-time','--gs-inline','--vif1-sync','--trace']
    if game.iso and Path(game.path(game.iso)).is_file():
        command+=['--disc',str(Path(game.path(game.iso)).resolve())]
    if args.discover:command+=['--discover',str(directory/'discovered.txt')]
    config=directory/'launch.json';config.write_text(json.dumps({'command':command},indent=2),encoding='utf-8')
    row={'command':command,'note':'Diagnostic bootstrap only; nonzero guest exit is not success.'}
    row['inputs']=[{'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
                   for p in (executable,elf,runner.GAME/'game.json')]
    runner.run_windows_job([sys.executable,'-B',str(Path(__file__).resolve()),'--_worker',str(config)],15,row)
    discoveries=directory/'discovered.txt'
    if discoveries.exists():row['discoveries']={'path':str(discoveries),'sha256':hashlib.sha256(discoveries.read_bytes()).hexdigest()}
    (directory/'containment.json').write_text(json.dumps(row,indent=2),encoding='utf-8')
    print(directory)
    print((directory/'host.log').read_text(encoding='utf-8',errors='replace') if (directory/'host.log').exists() else 'No host log')
    return row.get('exit_code',1) if row.get('cleanup_succeeded') and not row.get('timeout_detected') else 1


if __name__=='__main__':raise SystemExit(main())
