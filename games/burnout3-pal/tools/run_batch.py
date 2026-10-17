"""Validate explicit PAL batches. Dynamic execution intentionally unavailable.

All manifest paths are resolved independently of the shell working directory.
No derive/lift/port modules are imported or executed.
"""
import argparse
import hashlib
import json
import re
import sys
import uuid
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
ROOT = GAME.parent.parent
sys.path.insert(0, str(ROOT / 'engine'))
from ps2decomp.game import load
from ps2decomp.r5900 import DISASM, Flags
import struct

EXCLUDED = {0x46A280, 0x46A2A0, 0x46A480, 0x481818}


def check(ok, message):
    if not ok:
        raise ValueError(message)


def integer(value, name, low=0, high=(1 << 64)-1):
    check(type(value) is int and low <= value <= high, f'Invalid {name}')
    return value


def bits(value):
    check(isinstance(value, str) and re.fullmatch(r'0x[0-9A-Fa-f]{16}', value), 'u64 must have 16 hexadecimal digits')
    return int(value, 16)


def keys(obj, allowed, required=()):
    check(isinstance(obj, dict), 'Expected object')
    check(set(obj) <= set(allowed) and set(required) <= set(obj), 'Unknown or missing fields: ' + str(set(obj)))


def validate_memory(case, contract, scratch_size):
    keys(case, ('id','arguments','objects'), ('id','arguments','objects'))
    check(isinstance(case['id'], str) and case['id'], 'Invalid case id')
    objects = {}
    spans = []
    check(isinstance(case['objects'], list), 'objects must be a list')
    for obj in case['objects']:
        keys(obj, ('id','scratch_offset','size','alignment','fill','fields'), ('id','scratch_offset','size','alignment','fill','fields'))
        name = obj['id']; check(isinstance(name, str) and name and name not in objects, 'Duplicate/invalid object id')
        offset = integer(obj['scratch_offset'], 'object offset', 0, scratch_size)
        size = integer(obj['size'], 'object size', 1, scratch_size)
        align = integer(obj['alignment'], 'alignment', 1, 16)
        check(align & (align-1) == 0 and offset % align == 0 and offset+size <= scratch_size, 'Object alignment/bounds')
        integer(obj['fill'], 'fill', 0, 255)
        check(all(offset >= end or offset+size <= start for start,end in spans), 'Overlapping objects')
        spans.append((offset,offset+size)); objects[name] = obj
        field_spans = []
        check(isinstance(obj['fields'], list), 'fields must be a list')
        for field in obj['fields']:
            keys(field, ('offset','width','value_bits','endian'), ('offset','width','value_bits','endian'))
            width = integer(field['width'], 'field width', 1, 8); check(width in (1,2,4,8), 'Unsupported field width')
            at = integer(field['offset'], 'field offset', 0, size)
            check(at+width <= size and (offset+at) % width == 0, 'Field bounds/alignment')
            check(all(at >= end or at+width <= start for start,end in field_spans), 'Overlapping fields')
            field_spans.append((at,at+width))
            check(field['endian'] == 'little', 'Only little endian supported')
            check(isinstance(field['value_bits'], str) and re.fullmatch(r'0x[0-9a-fA-F]{%d}' % (width*2),field['value_bits']), 'Invalid field bits')
    args = case['arguments']; check(isinstance(args,dict), 'Arguments must be an object')
    for name,value in args.items():
        check(name in {f'arg{i}' for i in range(8)}, 'Only arg0..arg7 supported')
        if isinstance(value,str): bits(value)
        else:
            keys(value, ('object','offset'), ('object','offset'))
            check(value['object'] in objects, 'Unknown argument object')
            obj = objects[value['object']]; at = integer(value['offset'], 'argument offset', 0, obj['size']-1)
            check((obj['scratch_offset']+at) % obj['alignment'] == 0, 'Argument alignment')
    for read in contract['reads']:
        keys(read, ('object','offset','width','signed'), ('object','offset','width','signed'))
        check(read['object'] in objects, 'Unknown read object')
        obj = objects[read['object']]; width = integer(read['width'],'read width',1,8)
        check(width in (1,2,4,8) and type(read['signed']) is bool, 'Read width/sign')
        at = integer(read['offset'],'read offset',0,obj['size'])
        check(at+width <= obj['size'] and (obj['scratch_offset']+at)%width == 0, 'Read bounds/alignment')


def validate(manifest):
    keys(manifest, ('schema_version','id','mode','limits','functions','provenance'), ('schema_version','id','mode','limits','functions'))
    check(type(manifest['schema_version']) is int and manifest['schema_version']==1, 'schema_version must be 1')
    check(isinstance(manifest['id'],str) and re.fullmatch(r'[a-zA-Z0-9_-]+',manifest['id']), 'Invalid batch id')
    check(manifest['mode'] in ('explicit','explore'), 'Unknown mode')
    check(manifest['mode']=='explicit', 'Explore is separate and not implemented in this MVP')
    limits = manifest['limits']
    keys(limits, ('max_passes','instruction_budget','function_timeout_seconds','compile_timeout_seconds','batch_timeout_seconds'), ('max_passes','instruction_budget','function_timeout_seconds','compile_timeout_seconds','batch_timeout_seconds'))
    for key,value in limits.items(): integer(value,key,1,10000000)
    check(limits['max_passes']==1, 'MVP requires one pass')
    check(limits['batch_timeout_seconds'] >= limits['function_timeout_seconds'], 'Batch timeout too short')
    game = load(str(GAME)); db = game.load_database(); elf = game.load_elf()
    check(len(db['functions'])==9384, 'Expected clean database with 9384 functions')
    by_address = {f['addr']:f for f in db['functions']}; check(len(by_address)==9384,'Duplicate database addresses')
    section = elf.exec_sections[0]; image = elf.section_data(section)
    scratch = int(game.layout['scratch'],0); size = int(game.layout['scratch_size'],0)
    check(scratch%16==0 and scratch+size<=int(game.layout['stack_top'],0)-int(game.layout['stack_size'],0),'Scratch/stack conflict')
    check(all(not p.memsz or p.type!=1 or scratch>=p.vaddr+p.memsz or scratch+size<=p.vaddr for p in elf.segments),'Scratch/PT_LOAD conflict')
    check(isinstance(manifest['functions'],list) and manifest['functions'], 'Empty selection')
    addresses=set(); total=0
    for entry in manifest['functions']:
        keys(entry, ('address','expected_case_count','memory_contract','coverage','cases'), ('address','expected_case_count','memory_contract','coverage','cases'))
        check(isinstance(entry['address'],str) and re.fullmatch(r'0x[0-9A-Fa-f]{8}',entry['address']), 'Invalid address')
        address=int(entry['address'],16)
        check(address in by_address and address not in addresses and address not in EXCLUDED, 'Unknown, duplicate or excluded address')
        addresses.add(address); f=by_address[address]
        ranges=[(f['addr'],f['end'])]+[tuple(c) for c in f.get('chunks',[])]
        for lo,hi in ranges:
            check(section.addr<=lo<hi<=section.end_addr and lo%4==hi%4==0,'Invalid function range')
            check(not any(lo<h and hi>l for l,h in game.vu_regions),'VU intersection')
            check(all(DISASM.decode(struct.unpack_from('<I',image,a-section.addr)[0],a).is_valid for a in range(lo,hi,4)), 'Invalid instruction')
        count=integer(entry['expected_case_count'],'case count',8,10000)
        check(isinstance(entry['cases'],list) and len(entry['cases'])==count,'Missing exact cases')
        check(len({c['id'] for c in entry['cases']})==count,'Duplicate case IDs')
        contract=entry['memory_contract']; keys(contract,('reads','writes'),('reads','writes'))
        check(isinstance(contract['reads'],list) and contract['writes']==[], 'MVP supports reads and zero writes only')
        coverage=entry['coverage']; keys(coverage,('all_return_delay_slots','slt_relations','movn_conditions'),('all_return_delay_slots',))
        check(coverage['all_return_delay_slots'] is True,'Delay slots required')
        if 'slt_relations' in coverage: check(coverage['slt_relations']==['less','equal','greater'],'Invalid slt contract')
        if 'movn_conditions' in coverage: check(coverage['movn_conditions']==[False,True],'Invalid movn contract')
        for case in entry['cases']: validate_memory(case,contract,size)
        total+=count
    return game, {'status':'validated_only','dynamic_execution':False,'function_count':len(addresses),'case_count':total,'addresses':sorted(addresses),'limits':limits,'isolation_plan':{'unit':'one subprocess per function','fault_mapping':False,'timeouts':'required supervisor wall-clock limits; not yet implemented'},'uncertainties':['Validation checks declarations and input bounds, not dynamic coverage or equivalence.']}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest',required=True,type=Path)
    parser.add_argument('--validate-only',action='store_true')
    args=parser.parse_args(); run_dir=None
    try:
        path=args.manifest.resolve(); manifest=json.loads(path.read_bytes())
        game,report=validate(manifest)
        if not args.validate_only:
            raise NotImplementedError('Dynamic execution and timeout supervision are not implemented; use --validate-only')
        base=GAME/'build/batches'/manifest['id'];base.mkdir(parents=True,exist_ok=True)
        run_dir=base/('validate-'+uuid.uuid4().hex);run_dir.mkdir(exist_ok=False)
        inputs=[path,Path(__file__).resolve(),GAME/'game.json',Path(game.database_path),Path(game.elf_path)]
        report['sha256']={str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
        report['manifest']=manifest
        output=run_dir/'results.json'
        with output.open('x',encoding='utf-8') as h: json.dump(report,h,indent=2)
        check(json.loads(output.read_bytes())==report,'Report JSON validation failed')
        print(f"VALIDATED ONLY: {report['function_count']} functions, {report['case_count']} explicit cases")
        print('No dynamic execution or compilation; report:',output)
        return 0
    except Exception as exc:
        print(json.dumps({'status':'validation_error' if args.validate_only else 'execution_unavailable','error_type':type(exc).__name__,'error':str(exc)}),file=sys.stderr)
        return 1

if __name__=='__main__':
    raise SystemExit(main())