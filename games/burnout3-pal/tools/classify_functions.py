"""Static PAL function inventory. Python 3.12; never executes guest code.

Outputs are exclusively created under build/classification; existing outputs
cause a failure. Pointer provenance is deliberately not inferred from rs alone.
"""
import hashlib
import json
import struct
import sys
from collections import Counter
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
ROOT = GAME.parent.parent
sys.path.insert(0, str(ROOT / 'engine'))
from ps2decomp import game as game_config
from ps2decomp import r5900, emu
from ps2decomp.r5900 import DISASM, Flags

EXCLUSIONS = {0x0046A280, 0x0046A2A0, 0x0046A480, 0x00481818}
RULES = {
    'version': 1,
    'ranges': 'Body and chunks, deduplicated within each function; shared addresses across functions retained.',
    'memory': 'LOAD/STORE flags; cache/pref are separately tagged maintenance operations, not data loads.',
    'provenance': 'Unknown: base register and displacement are evidence only; no dataflow or device/DMA safety proof.',
    'level_0': 'Excluded start, invalid range, confirmed VU intersection or invalid decoding; never a batch candidate.',
    'level_1': '4-30 body instructions, explicit and metadata return, no chunks, memory flags, calls, indirect jumps, VU, COP1, MMI, privileged operations, traps or unsupported interpreter instructions. Forward branches allowed.',
    'level_2': 'Same simple constraints as level 1, but data reads; pointer provenance unproved, not batch eligible.',
    'level_3': 'Same simple constraints, with stores; memory effects unproved, not batch eligible.',
    'level_4': 'Size, chunks, return or control-flow complexity prevents simple classification.',
    'level_5': 'Calls, VU, COP1, MMI, system operations, traps or interpreter support gaps require specialist review.',
    'batch': 'Only level 1; at most 20; sorted by level, body size bytes, numeric address. Static shortlist, not verified equivalence.',
    'support': 'emu.is_supported only; does not certify symbolic derivation, translation or exact hardware semantics.',
}
WIDTH = {'lb':1,'lbu':1,'sb':1,'lh':2,'lhu':2,'sh':2,
         'lw':4,'lwu':4,'sw':4,'lwc0':4,'lwc1':4,'swc1':4,
         'ld':8,'sd':8,'lq':16,'sq':16,'lqc2':16,'sqc2':16}
SIGNED = {'lb','lh','lw'}
PRIVILEGED = {'syscall','break','mfc0','mtc0','ei','di','eret','tlbwi','tlbwr','tlbp','tlbr','cache','sync','pref'}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def classify(f, section, image, vu):
    spans = [(f['addr'], f['end'])] + [tuple(c) for c in f.get('chunks', [])]
    invalid = [list(span) for span in spans if len(span) != 2 or
               not (section.addr <= span[0] < span[1] <= section.end_addr) or
               span[0] % 4 or span[1] % 4]
    vu_hit = any(low < high_vu and high > low_vu
                 for low, high in spans for low_vu, high_vu in vu)
    addresses = sorted({a for low, high in spans if [low, high] not in invalid
                        for a in range(low, high, 4)})
    address_set = set(addresses)
    instructions = [DISASM.decode(struct.unpack_from('<I', image, a-section.addr)[0], a)
                    for a in addresses]
    events = {key: [] for key in ('branches','loads','stores','calls','returns','cop1','vu','mmi',
                                'syscalls','traps','indirect_jumps','maintenance','privileged',
                                'invalid_decode','unsupported_interpreter','delay_slots')}
    mnemonics = Counter()
    for i in instructions:
        mnemonics[i.mnemonic] += 1
        site = {'address':i.addr, 'mnemonic':i.mnemonic}
        op = i.raw >> 26
        if i.is_branch and not i.is_call and i.mnemonic != 'b':
            events['branches'].append(dict(site, target=i.target, likely=i.is_likely,
                                           backward=i.target >= 0 and i.target <= i.addr))
        if i.is_call:
            events['calls'].append(dict(site, target=i.target if i.target >= 0 else None,
                                       indirect=i.mnemonic == 'jalr'))
        if i.is_return: events['returns'].append(site)
        if i.mnemonic == 'jr' and i.rs != 31: events['indirect_jumps'].append(site)
        if op in (0x11,0x31,0x39): events['cop1'].append(site)
        if op in (0x12,0x36,0x3e): events['vu'].append(site)
        if op == 0x1c: events['mmi'].append(site)
        if i.mnemonic == 'syscall': events['syscalls'].append(site)
        if i.flags & Flags.TRAP: events['traps'].append(site)
        if i.mnemonic in PRIVILEGED: events['privileged'].append(site)
        if i.mnemonic in ('cache','pref','sync'): events['maintenance'].append(site)
        if not i.is_valid: events['invalid_decode'].append(site)
        if not emu.is_supported(i): events['unsupported_interpreter'].append(site)
        if i.has_delay_slot:
            events['delay_slots'].append(dict(site, slot=i.addr+4,
                                              slot_in_ranges=i.addr+4 in address_set))
        for key, flag in (('loads',Flags.LOAD),('stores',Flags.STORE)):
            if i.flags & flag and i.mnemonic not in ('cache','pref'):
                events[key].append(dict(site, base_register=i.rs, displacement=i.imm,
                    width_bytes=WIDTH.get(i.mnemonic),
                    sign_extension=(i.mnemonic in SIGNED) if key == 'loads' and i.mnemonic in WIDTH else None,
                    alignment_bytes=WIDTH.get(i.mnemonic), provenance='unknown',
                    initial_argument_register_hint=4 <= i.rs <= 11))
    tags = [key for key, value in events.items() if value]
    if f.get('chunks'): tags.append('chunks')
    if f['has_return']: tags.append('metadata_return')
    if f['uses_vu']: tags.append('metadata_vu')
    if f['calls']: tags.append('metadata_calls')
    if any(b['backward'] for b in events['branches']): tags.append('possible_loop')
    blockers = []
    if invalid: blockers.append('invalid_ranges')
    if vu_hit: blockers.append('confirmed_vu_intersection')
    if f['addr'] in EXCLUSIONS: blockers.append('explicit_exclusion')
    if events['invalid_decode']: blockers.append('invalid_decode')
    specialist = any(events[k] for k in ('calls','vu','cop1','mmi','syscalls','traps','privileged','unsupported_interpreter')) or f['uses_vu'] or bool(f['calls'])
    simple = (4 <= f['size']//4 <= 30 and not f.get('chunks') and f['has_return']
              and bool(events['returns']) and not events['indirect_jumps']
              and 'possible_loop' not in tags
              and all(x['slot_in_ranges'] for x in events['delay_slots']))
    if blockers: level = 0
    elif specialist: level = 5
    elif not simple: level = 4
    elif events['stores']: level = 3
    elif events['loads']: level = 2
    else: level = 1
    return {'address':f['addr'],'address_hex':f"0x{f['addr']:08X}", 'end':f['end'],
            'size_bytes':f['size'],'body_instruction_slots':f['size']//4,
            'database_instructions':f['instructions'],'decoded_unique_slots':len(instructions),
            'label':f['label'],'confidence':f['confidence'],'chunks':f.get('chunks',[]),
            'tags':tags,'difficulty_level':level,'batch_eligible':level == 1,
            'exclusion_reasons':blockers,'invalid_ranges':invalid,'events':events,
            'mnemonics':dict(sorted(mnemonics.items())),
            'uncertainties':['No reachability proof or dynamic equivalence.',
                             'Pointer provenance, device/DMA use and aliasing unproved.',
                             'Interpreter support is not translation support.']}


def main():
    out = GAME/'build/classification'
    paths = [out/name for name in ('functions.json','summary.json','batch_candidates.json')]
    require(not any(p.exists() for p in paths), 'Classification outputs already exist; refusing overwrite')
    game = game_config.load(str(GAME)); db = game.load_database(); elf = game.load_elf()
    functions = db['functions']; require(len(functions) == 9384, 'Expected 9384 functions')
    require(len({f['addr'] for f in functions}) == len(functions), 'Duplicate function starts')
    section = elf.exec_sections[0]; image = elf.section_data(section)
    rows = []
    # Avoid storing whole executable data in outputs. Only static metadata is emitted.
    for f in sorted(functions,key=lambda f:f['addr']):
        rows.append(classify(f,section,image,game.vu_regions))
    inputs = [GAME/'game.json',Path(game.elf_path),Path(game.database_path),Path(__file__),
              Path(r5900.__file__),Path(emu.__file__),Path(game_config.__file__)]
    hashes = {str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    common = {'schema_version':1,'rules':RULES,'sha256':hashes,
              'vu_regions':game.vu_regions,'explicit_exclusions':sorted(EXCLUSIONS)}
    tags = Counter(tag for row in rows for tag in row['tags'])
    levels = Counter(row['difficulty_level'] for row in rows)
    unsupported = Counter(event['mnemonic'] for row in rows for event in row['events']['unsupported_interpreter'])
    candidates = sorted((r for r in rows if r['batch_eligible']),
                        key=lambda r:(r['difficulty_level'],r['size_bytes'],r['address']))[:20]
    payloads = [dict(common,functions=rows),
                dict(common,total_functions=len(rows),levels=dict(sorted(levels.items())),
                     tag_function_counts=dict(sorted(tags.items())),
                     unsupported_instruction_occurrences=dict(sorted(unsupported.items())),
                     batch_candidate_count=len(candidates)),
                dict(common,candidates=candidates)]
    encoded = [(json.dumps(p,indent=2)+'\n').encode('utf-8') for p in payloads]
    for blob,payload in zip(encoded,payloads): require(json.loads(blob)==json.loads(json.dumps(payload)), 'JSON validation failed')
    require(len(candidates)<=20 and all(r['difficulty_level']==1 for r in candidates),'Invalid batch')
    out.mkdir(parents=True,exist_ok=True)
    for path,blob,payload in zip(paths,encoded,payloads):
        with path.open('xb') as handle: handle.write(blob)
        require(json.loads(path.read_bytes())==json.loads(json.dumps(payload)), 'Written JSON validation failed')
    print('Functions:',len(rows));print('Levels:',dict(sorted(levels.items())))
    print('Tags:',dict(sorted(tags.items())));print('Unsupported:',dict(unsupported))
    print('First candidates:',[(r['address_hex'],r['size_bytes']) for r in candidates])
    print('Outputs:',*[str(p) for p in paths],sep='\n')

if __name__ == '__main__':
    main()