"""Append one mechanical PAL translation; never replace existing C++ files.

All additions are experimental NOT VERIFIED, even if an old outcome says passed.
Uses the original lifter and chunk writer only in an exclusive staging directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import uuid

GAME=Path(__file__).resolve().parents[1]
ROOT=GAME.parents[1]
sys.path[:0]=[str(ROOT/'engine/tools'),str(ROOT/'engine')]
import lift as driver
import port
from ps2decomp import lift as translator
from ps2decomp.r5900 import DISASM
from pal_auto import lock


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inventory(directory):
    files={p.name:digest(p) for p in sorted(directory.glob('*.cpp'))}
    owners={}
    for p in sorted(directory.glob('*.cpp')):
        for match in port._FUNCTION.finditer(p.read_text(encoding='utf-8')):
            address=int(match.group(1),16)
            if address in owners:raise ValueError('Duplicate function 0x%08X'%address)
            owners[address]=p.name
    return files,owners


def emit_one(game,address):
    build=Path(game.path(game.build));build.mkdir(parents=True,exist_ok=True)
    output=Path(game.path(str(Path(game.generated).parent/'lifted')))
    if not output.is_dir():raise ValueError('Existing PAL emission directory required')
    with lock(build/'incremental.lock'):
        before,owners=inventory(output)
        if address in owners:raise ValueError('Function already emitted: 0x%08X'%address)
        target=output/('incremental_%08X.cpp'%address)
        if target.exists():raise ValueError('Output already exists; refusing replacement')
        db=game.load_database()
        matches=[f for f in db['functions'] if f['addr']==address]
        if len(matches)!=1:raise ValueError('Address must identify exactly one PAL database function')
        function=matches[0]
        directory=build/'incremental'/('run-'+uuid.uuid4().hex[:12]);directory.mkdir(parents=True,exist_ok=False)
        inputs=[Path(game.elf_path),Path(game.database_path),Path(driver.__file__),Path(translator.__file__)]
        input_hashes={str(p):digest(p) for p in inputs}
        reason='Dynamic equivalence not established by this incremental emitter'
        outcomes=build/'lift_outcomes.json'
        if outcomes.is_file():
            previous=json.loads(outcomes.read_bytes()).get('0x%08X'%address)
            if previous:
                reason+='; previous reported outcome (not revalidated here): '+str(previous)
                input_hashes[str(outcomes)]=digest(outcomes)
        elf=game.load_elf();section=elf.exec_sections[0];image=elf.section_data(section)
        translator.use_sections(driver._other_sections(elf,section))
        translator.use_function_starts(f['addr'] for f in db['functions'])
        text=translator.lift_function(image,section.addr,address,function['end'],function.get('chunks',()))
        if [int(m.group(1),16) for m in port._FUNCTION.finditer(text)]!=[address]:
            raise ValueError('Unexpected generated function identity')
        # write_chunks may delete matching files, but only inside this new directory.
        staged=Path(driver.write_chunks({address:text},str(directory),'one',unverified={address:reason})[0])
        controls=[]
        for lo,hi in [(address,function['end']),*function.get('chunks',())]:
            for pc in range(lo,hi,4):
                ins=DISASM.decode(struct.unpack_from('<I',image,pc-section.addr)[0],pc)
                if ins.has_delay_slot:
                    controls.append({'address':'0x%08X'%pc,'mnemonic':ins.mnemonic,
                                     'target':'0x%08X'%ins.target if ins.mnemonic in ('jal','j','b') else None,
                                     'delay_slot':'0x%08X'%(pc+4)})
        report={'address':'0x%08X'%address,'status':'mechanically_lifted_not_verified','reason':reason,
                'input_sha256':input_hashes,'existing_cpp_sha256':before,'existing_functions':len(owners),
                'controls':controls,'output':str(target),'output_sha256':digest(staged)}
        (directory/'evidence.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        if inventory(output)!=(before,owners):raise ValueError('Emission changed during generation; refusing publication')
        if any(digest(p)!=sha for p,sha in input_hashes.items()):raise ValueError('Input changed during generation')
        # Atomic no-replace publication on the same filesystem. Staging is retained.
        os.link(staged,target)
        after,new_owners=inventory(output)
        if any(after.get(n)!=sha for n,sha in before.items()) or set(new_owners)!=set(owners)|{address}:
            raise ValueError('Post-publication integrity failure; inspect retained evidence')
        return target,directory/'evidence.json'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('address',type=lambda s:int(s,16))
    args=parser.parse_args()
    try:
        path,report=emit_one(port.game_config.load(str(GAME)),args.address)
        print(path);print(report);return 0
    except (ValueError,OSError,translator.Refused) as exc:
        print('Incremental emission rejected:',str(exc),file=sys.stderr);return 1


if __name__=='__main__':raise SystemExit(main())
