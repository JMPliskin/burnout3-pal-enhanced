"""Validate PAL manifests and run statically admitted register-only workers.

All manifest paths are resolved independently of the shell working directory.
Only the validated child imports derive; no lift or port execution.
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
    addresses=set(); total=0; admissions=[]
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
        select_worker(manifest,entry['address'])
        admissions.append(admit_function(f,section,image,game.vu_regions))
        total+=count
    return game, {'status':'validated_only','dynamic_execution':False,'function_count':len(addresses),'case_count':total,'addresses':sorted(addresses),'admissions':admissions,'limits':limits,'isolation_plan':{'unit':'one subprocess per function','fault_mapping':False,'timeouts':{'function':'effective whole-worker wall-clock deadline','batch':'effective shared deadline','compile':'configured and measured only; independent compile deadline not yet enforced'},'execution':'sequential admitted register-profile workers; runtime validation still required'},'uncertainties':['Validation checks declarations and input bounds, not dynamic coverage or equivalence.']}



REGISTER_OPS = {
    'nop','jr','b','beq','bne','bltz','bgez','blez','bgtz',
    'lui','addiu','andi','ori','xori','slti','sltiu',
    'sll','srl','sra','sllv','srlv','srav',
    'addu','daddu','subu','and','or','xor','nor','slt','sltu','movn','movz',
}


def admit_function(f, section, image, vu_regions):
    """Conservative register-only acyclic profile; no guest execution.

    Decode body AND chunks, but reject chunks after inspection: their ownership
    and CFG joining are not supported by this profile. Track definite register
    initialization per path including delay slots; never infer safety from level.
    """
    from ps2decomp.emu import is_supported
    prefix='0x%08X: '%f['addr']
    spans=[(f['addr'],f['end'])]+[tuple(c) for c in f.get('chunks',[])]
    code={}
    for low,high in spans:
        check(section.addr<=low<high<=section.end_addr and low%4==high%4==0,prefix+'invalid range')
        check(not any(low<h and high>l for l,h in vu_regions),prefix+'VU overlap')
        for address in range(low,high,4):
            i=DISASM.decode(struct.unpack_from('<I',image,address-section.addr)[0],address)
            check(i.is_valid,prefix+'invalid decoding')
            check(is_supported(i),prefix+'interpreter unsupported '+i.mnemonic)
            check(not i.flags&(Flags.LOAD|Flags.STORE|Flags.TRAP) and not i.is_call,prefix+'memory/call/trap unsupported')
            check(i.raw>>26 not in (0x11,0x12,0x31,0x39,0x36,0x3e),prefix+'FPU/VU unsupported')
            check(i.mnemonic in REGISTER_OPS,prefix+'outside register profile (including HI/LO/system): '+i.mnemonic)
            code[address]=i
    check(not f.get('chunks'),prefix+'chunks inspected but not admitted')
    check(1<=len(code)<=30 and f['size']==f['end']-f['addr'],prefix+'size outside profile')
    check(f['has_return'] and not f['calls'] and not f['uses_vu'],prefix+'metadata return/calls/VU conflict')
    slots={a+4 for a,i in code.items() if i.has_delay_slot}
    for address,i in code.items():
        if i.has_delay_slot:check(address+4 in code and not code[address+4].has_delay_slot,prefix+'invalid delay slot')
        if i.is_return:check(i.rs==31,prefix+'invalid return')
        elif i.is_branch or i.is_jump:
            check(i.target in code and i.target not in slots and i.target>address,prefix+'external/backward/slot transfer')
    inputs=set();return_sites=set();visited=set();work=[(f['addr'],frozenset({0,31}))]
    def access(i,defined):
        n=i.mnemonic
        if n in ('nop','b'):reads=[];dest=None
        elif n=='jr':reads=[i.rs];dest=None
        elif n in ('beq','bne'):reads=[i.rs,i.rt];dest=None
        elif n in ('bltz','bgez','blez','bgtz'):reads=[i.rs];dest=None
        elif n=='lui':reads=[];dest=i.rt
        elif n in ('addiu','andi','ori','xori','slti','sltiu'):reads=[i.rs];dest=i.rt
        elif n in ('sll','srl','sra'):reads=[i.rt];dest=i.rd
        elif n in ('movn','movz'):reads=[i.rs,i.rt,i.rd];dest=i.rd
        else:reads=[i.rs,i.rt];dest=i.rd
        for reg in reads:
            if reg not in defined:
                check(4<=reg<=11,prefix+'uninitialized/nonargument register %d'%reg)
                inputs.add(reg)
        check(dest is None or dest in {0,1,2,3,*range(4,12)},prefix+'writes preserved/stack/return register')
        return frozenset(set(defined)|({dest} if dest is not None else set()))
    while work:
        address,defined=work.pop()
        if (address,defined) in visited:continue
        visited.add((address,defined));check(len(visited)<=10000,prefix+'CFG budget exceeded')
        i=code[address];defined=access(i,defined)
        if i.has_delay_slot:defined=access(code[address+4],defined)
        if i.is_return:
            check(2 in defined,prefix+'return v0 not definitely assigned');return_sites.add(address);continue
        successors=[i.target] if i.mnemonic=='b' else [i.target,address+8] if i.is_branch else [address+4]
        for target in successors:
            check(target in code,prefix+'fallthrough outside function');work.append((target,defined))
    check(return_sites,prefix+'no reachable return')
    return {'address':f['addr'],'profile':'acyclic_registers_s32_return','instruction_count':len(code),
            'input_registers':sorted(inputs),'input_arguments':['arg%d'%(r-4) for r in sorted(inputs)],
            'implicit_inputs':['ra'],'incoming_v0_v1_hi_lo_fpu':False,
            'branches':[a for a,i in code.items() if i.is_branch],
            'returns':sorted(return_sites),'delay_slots':sorted(slots),
            'conditional_moves':[a for a,i in code.items() if i.mnemonic in ('movn','movz')],
            'coverage_status':'pending_dynamic_verification',
            'limits':['Return ABI is s32; full-register and 64-bit-return equivalence not certified.',
                      'Admission proves profile constraints only; derivation/emission may still reject.']}


def select_worker(manifest, only):
    check(isinstance(only,str) and re.fullmatch(r'0x[0-9a-fA-F]{8}',only),'Worker requires one address')
    entries=[f for f in manifest['functions'] if int(f['address'],16)==int(only,16)]
    check(len(entries)==1,'Missing or duplicate worker function')
    entry=entries[0]
    check(manifest['mode']=='explicit','Explore unsupported')
    check(entry['expected_case_count']==8 and len(entry['cases'])==8,'Register worker currently requires exactly eight cases')
    check(not entry['memory_contract']['reads'] and not entry['memory_contract']['writes'] and
          all(not c['objects'] and all(isinstance(v,str) for v in c['arguments'].values()) for c in entry['cases']),
          'Only explicit u64 register cases without memory are supported')
    return entry


def write_report(path, report):
    with path.open('x',encoding='utf-8') as h: json.dump(report,h,indent=2)


def worker(path, only, directory):
    import inspect
    import traceback
    import time
    started=time.monotonic()
    report={'status':'structural_error','address':only,'cases':[],'timings':{},'limits_effective':{'function':True,'batch':True,'compile':False}}
    def partial(stage):
        report['stage']=stage
        temp=directory/'worker.partial.json.tmp'
        with temp.open('x',encoding='utf-8') as h:json.dump(report,h,indent=2)
        temp.replace(directory/'worker.partial.json')
    try:
        # Always validate in the child before importing the derivation tool.
        manifest=json.loads(path.read_bytes());game,validation=validate(manifest)
        entry=select_worker(manifest,only)
        check(directory.is_dir() and directory.parent.parent == GAME/'build/batches'/manifest['id'], 'Invalid worker output directory')
        sys.path.insert(0,str(ROOT/'engine/tools'))
        import derive
        from ps2decomp import cppcheck, testplan
        from ps2decomp.emu import Machine
        check(sys.version_info[:2]==(3,12),'Python 3.12 required')
        report['validation']=validation
        report['sha256']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in
                          (path,Path(__file__),GAME/'game.json',Path(game.database_path),Path(game.elf_path))}
        db=game.load_database();f=next(f for f in db['functions'] if f['addr']==int(entry['address'],16))
        elf=game.load_elf();section=elf.exec_sections[0];image=elf.section_data(section)
        admission=admit_function(f,section,image,game.vu_regions)
        report['admission']=admission
        vectors=[tuple(bits(c['arguments'].get('arg%d'%i,'0x0000000000000000')) for i in range(8))+(0,)*8 for c in entry['cases']]
        m=Machine.from_elf(elf);sig=inspect.signature(derive.test_plan)
        def plan(*args,**kwargs):
            bound=sig.bind(*args,**kwargs);check(bound.arguments['func']['addr']==f['addr'],'Unexpected function');return vectors,'mixed'
        derive.test_plan=plan
        original_prepare=derive.prepare_test_memory
        def prepare(*args,**kwargs):
            result=original_prepare(*args,**kwargs);m.map_faults=False;m.memory.page_source=None;return result
        derive.prepare_test_memory=prepare
        derive.CALLERS_KNOWN=derive.game_config.callers_known(game)
        partial('derive')
        stage=time.monotonic()
        verified,rejected=derive.collect(dict(db,functions=[f]),image,section.addr,m,max_passes=1)
        report['timings']['derive_seconds']=time.monotonic()-stage
        report['rejections']=rejected
        if len(verified)!=1 or verified[0][2]!=8:
            report['status']='rejected';return 1
        _,d,_,_=verified[0];report['derivation']=derive.render_body(d)
        emitted=derive.emit(verified,str(directory/'generated'),db['meta']['source'],catalog=None)
        check(len(emitted)==1,'Emitter dropped function')
        source=directory/'generated/accessors.cpp';signatures=cppcheck.functions_in(source.read_text())
        indices=sorted(derive.derivation_args(d))
        check(all(0<=i<8 for i in indices),'Derived non-GPR/implicit argument unsupported')
        params=', '.join('u64 arg%d'%i for i in indices) or 'void'
        check(len(signatures)==1 and signatures[0][0]==f['addr'] and signatures[0][2:]==('s32',indices,params),'Unexpected signature')
        check(not cppcheck.externs_in(source.read_text()),'Unexpected externs')
        partial('compile')
        stage=time.monotonic()
        try:
            dll,built=cppcheck.build(str(source),cppcheck.RUNTIME_SOURCES,str(Path(cppcheck.RUNTIME)/'include'),str(directory/'compile'))
        except (cppcheck.CompileError,cppcheck.CompilerUnavailable):
            report['timings']['compile_seconds']=time.monotonic()-stage
            report['status']='compile_error';report['error']=traceback.format_exc();return 1
        report['timings']['compile_seconds']=time.monotonic()-stage
        check(len(built)==1,'Unexpected compiled count')
        raw=Path(dll).read_bytes();pe=struct.unpack_from('<I',raw,60)[0]
        check(struct.unpack_from('<H',raw,pe+4)[0]==0x8664 and '/O2' in (directory/'compile/build.bat').read_text(),'DLL architecture/optimization')
        compiled=cppcheck.CompiledFunctions(dll,m);evaluate=compiled.evaluator(f['addr'])
        m.map_faults=False;m.memory.page_source=None
        partial('verify')
        coverage_relations=set();coverage_moves=set();branch_coverage={};move_coverage={};all_slots=set()
        stage=time.monotonic()
        for case,vector in zip(entry['cases'],vectors):
            results={};slots=set();relations=set();moves=set();case_branches={};case_moves={};original_step=m.step;original_execute=m._execute
            def step():
                pc=m.pc;i=DISASM.decode(m.memory.read_int(pc,4,False),pc)
                original_step()
                if i.is_branch:case_branches.setdefault(pc,set()).add(m.pc==i.target)
                if i.has_delay_slot:slots.add(pc+4)
            def execute(i):
                def signed64(v):
                    v &= (1<<64)-1
                    return v-(1<<64) if v>>63 else v
                if i.mnemonic=='slt':
                    x,y=signed64(m.get(i.rs)),signed64(m.get(i.rt))
                    relations.add('less' if x<y else 'equal' if x==y else 'greater')
                if i.mnemonic in ('movn','movz'):
                    condition=bool(m.get(i.rt)) if i.mnemonic=='movn' else not bool(m.get(i.rt))
                    case_moves.setdefault(i.addr,set()).add(condition)
                    if i.mnemonic=='movn':moves.add(condition)
                original_execute(i)
            m.step=step;m._execute=execute
            try:
                for mode in ('mips','symbolic','native'):
                    m.memory.end_journal();m.memory.begin_journal()
                    if mode=='mips':value=m.call(f['addr'],vector,max_instructions=manifest['limits']['instruction_budget'])
                    elif mode=='symbolic':
                        value=d.evaluate(vector,m.memory.read_int);check(not d.evaluate_stores(vector,m.memory.read_int),'Symbolic writes')
                    else:
                        value,writes=evaluate(vector);check(not writes,'Native writes')
                    check(not m.memory._journal,'Unexpected writes');results[mode]=value;m.memory.rollback();m.memory.end_journal()
            finally:m.step=original_step;m._execute=original_execute
            row={'id':case['id'],'arguments':case['arguments'],'results':results,'delay_slots':sorted(slots),'slt_relations':sorted(relations),'movn_conditions':sorted(moves),'branches':{str(k):sorted(v) for k,v in case_branches.items()},'conditional_moves':{str(k):sorted(v) for k,v in case_moves.items()},'writes':0}
            report['cases'].append(row)
            coverage_relations.update(relations);coverage_moves.update(moves);all_slots.update(slots)
            for pc,outcomes in case_branches.items():branch_coverage.setdefault(pc,set()).update(outcomes)
            for pc,outcomes in case_moves.items():move_coverage.setdefault(pc,set()).update(outcomes)
            partial('verify')
            if len(set(results.values()))!=1:
                report['status']='mismatch';return 1
            check(any(pc+4 in slots for pc in admission['returns']),'Missing executed return delay slot')
        report['timings']['verify_seconds']=time.monotonic()-stage
        report['coverage']={'slt_relations':sorted(coverage_relations),'movn_conditions':sorted(coverage_moves),
                            'branches':{str(pc):sorted(branch_coverage.get(pc,set())) for pc in admission['branches']},
                            'conditional_moves':{str(pc):sorted(move_coverage.get(pc,set())) for pc in admission['conditional_moves']}}
        check(all(branch_coverage.get(pc)=={False,True} for pc in admission['branches']),'Incomplete branch coverage')
        check(all(move_coverage.get(pc)=={False,True} for pc in admission['conditional_moves']),'Incomplete conditional move coverage')
        check(all(pc+4 in all_slots for pc in admission['returns']),'Incomplete return delay coverage')
        if 'slt_relations' in entry['coverage']:
            check(coverage_relations=={'less','equal','greater'},'Incomplete slt coverage')
        if 'movn_conditions' in entry['coverage']:
            check(coverage_moves=={False,True},'Incomplete movn coverage')
        check(len(report['cases'])==8 and not m.map_faults,'Incomplete verification')
        for name,digest in report['sha256'].items():check(hashlib.sha256(Path(name).read_bytes()).hexdigest()==digest,'Input changed')
        report.update(status='passed',case_count=8,dll=dll,discrepancies=0,automatic_fault_mapping=False)
        return 0
    except Exception:
        report['status']='structural_error';report['error']=traceback.format_exc();return 1
    finally:
        report['timings']['total_seconds']=time.monotonic()-started
        write_report(directory/'worker.json',report)


def run_windows_job(command, seconds, row):
    """Launch suspended, assign to a private Job, then resume. No Popen internals.

    The Job handle is non-inheritable. No breakaway flags or elevation are used.
    All Win32 definitions and calls are lazy: validation does not touch Win32.
    """
    import ctypes as C
    from ctypes import wintypes as W
    import subprocess
    import time
    k=C.WinDLL('kernel32',use_last_error=True)
    SIZE=C.c_size_t; ULONG_PTR=C.c_size_t
    class IO(C.Structure):
        _fields_=[(n,C.c_ulonglong) for n in ('ReadOperationCount','WriteOperationCount','OtherOperationCount','ReadTransferCount','WriteTransferCount','OtherTransferCount')]
    class BASIC(C.Structure):
        _fields_=[('PerProcessUserTimeLimit',C.c_longlong),('PerJobUserTimeLimit',C.c_longlong),('LimitFlags',W.DWORD),('MinimumWorkingSetSize',SIZE),('MaximumWorkingSetSize',SIZE),('ActiveProcessLimit',W.DWORD),('Affinity',ULONG_PTR),('PriorityClass',W.DWORD),('SchedulingClass',W.DWORD)]
    class EXTENDED(C.Structure):
        _fields_=[('BasicLimitInformation',BASIC),('IoInfo',IO),('ProcessMemoryLimit',SIZE),('JobMemoryLimit',SIZE),('PeakProcessMemoryUsed',SIZE),('PeakJobMemoryUsed',SIZE)]
    class ACCOUNTING(C.Structure):
        _fields_=[('TotalUserTime',C.c_longlong),('TotalKernelTime',C.c_longlong),('ThisPeriodTotalUserTime',C.c_longlong),('ThisPeriodTotalKernelTime',C.c_longlong),('TotalPageFaultCount',W.DWORD),('TotalProcesses',W.DWORD),('ActiveProcesses',W.DWORD),('TotalTerminatedProcesses',W.DWORD)]
    class STARTUP(C.Structure):
        _fields_=[('cb',W.DWORD),('lpReserved',W.LPWSTR),('lpDesktop',W.LPWSTR),('lpTitle',W.LPWSTR),('dwX',W.DWORD),('dwY',W.DWORD),('dwXSize',W.DWORD),('dwYSize',W.DWORD),('dwXCountChars',W.DWORD),('dwYCountChars',W.DWORD),('dwFillAttribute',W.DWORD),('dwFlags',W.DWORD),('wShowWindow',W.WORD),('cbReserved2',W.WORD),('lpReserved2',C.POINTER(W.BYTE)),('hStdInput',W.HANDLE),('hStdOutput',W.HANDLE),('hStdError',W.HANDLE)]
    class PROCESS(C.Structure):
        _fields_=[('hProcess',W.HANDLE),('hThread',W.HANDLE),('dwProcessId',W.DWORD),('dwThreadId',W.DWORD)]
    signatures={
        'CreateJobObjectW':([C.c_void_p,W.LPCWSTR],W.HANDLE),
        'SetInformationJobObject':([W.HANDLE,C.c_int,C.c_void_p,W.DWORD],W.BOOL),
        'CreateProcessW':([W.LPCWSTR,W.LPWSTR,C.c_void_p,C.c_void_p,W.BOOL,W.DWORD,C.c_void_p,W.LPCWSTR,C.POINTER(STARTUP),C.POINTER(PROCESS)],W.BOOL),
        'AssignProcessToJobObject':([W.HANDLE,W.HANDLE],W.BOOL),
        'ResumeThread':([W.HANDLE],W.DWORD),
        'WaitForSingleObject':([W.HANDLE,W.DWORD],W.DWORD),
        'TerminateJobObject':([W.HANDLE,W.UINT],W.BOOL),
        'TerminateProcess':([W.HANDLE,W.UINT],W.BOOL),
        'QueryInformationJobObject':([W.HANDLE,C.c_int,C.c_void_p,W.DWORD,C.c_void_p],W.BOOL),
        'GetExitCodeProcess':([W.HANDLE,C.POINTER(W.DWORD)],W.BOOL),
        'CloseHandle':([W.HANDLE],W.BOOL),
    }
    for name,(args,result) in signatures.items():
        fn=getattr(k,name);fn.argtypes=args;fn.restype=result
    events=row.setdefault('win32_api',[])
    def call(name,*args):
        C.set_last_error(0);result=getattr(k,name)(*args);error=C.get_last_error()
        events.append({'api':name,'result':int(result or 0),'last_error':error,'monotonic':time.monotonic()})
        return result,error
    def required(name,*args):
        result,error=call(name,*args)
        if not result:raise OSError(error,'%s failed (sandbox/job restrictions possible); no fallback or elevation'%name)
        return result
    job=None;pi=PROCESS();assigned=False;started=time.monotonic()
    row.update(timeout_detected=False,cleanup_succeeded=False)
    try:
        job=required('CreateJobObjectW',None,None)
        limits=EXTENDED();limits.BasicLimitInformation.LimitFlags=0x2000 # KILL_ON_JOB_CLOSE
        required('SetInformationJobObject',job,9,C.byref(limits),C.sizeof(limits))
        si=STARTUP();si.cb=C.sizeof(si)
        command_line=C.create_unicode_buffer(subprocess.list2cmdline(command))
        # No inherited handles; primary thread cannot run before assignment.
        required('CreateProcessW',command[0],command_line,None,None,False,0x4 | 0x08000000,None,str(ROOT),C.byref(si),C.byref(pi))
        row['pid']=pi.dwProcessId;row['thread_id']=pi.dwThreadId
        required('AssignProcessToJobObject',job,pi.hProcess);assigned=True
        count,error=call('ResumeThread',pi.hThread)
        if count==0xffffffff:raise OSError(error,'ResumeThread failed')
        remaining=max(0,seconds-(time.monotonic()-started))
        wait,error=call('WaitForSingleObject',pi.hProcess,min(0xfffffffe,int(remaining*1000)))
        if wait==258:
            row['timeout_detected']=True
            row['status']='timeout'
        elif wait!=0:raise OSError(error,'Worker wait failed')
    except Exception as exc:
        row['status']='structural_error';row['error']=repr(exc)
    finally:
        # Terminate only the private Job, or our still-suspended unassigned process.
        cleanup_errors=[]
        try:
            if assigned:
                accounting=ACCOUNTING()
                required('QueryInformationJobObject',job,1,C.byref(accounting),C.sizeof(accounting),None)
                row['active_processes_before_cleanup']=accounting.ActiveProcesses
                if accounting.ActiveProcesses:
                    required('TerminateJobObject',job,1)
                until=time.monotonic()+15
                while True:
                    required('QueryInformationJobObject',job,1,C.byref(accounting),C.sizeof(accounting),None)
                    if accounting.ActiveProcesses==0:break
                    if time.monotonic()>=until:raise TimeoutError('Own Job still contains active processes')
                    time.sleep(0.02)
                row['active_processes_after_cleanup']=0
            elif pi.hProcess:
                required('TerminateProcess',pi.hProcess,1)
            if pi.hProcess:
                wait,error=call('WaitForSingleObject',pi.hProcess,15000)
                if wait!=0:raise OSError(error,'Own worker did not terminate during cleanup')
                code=W.DWORD();required('GetExitCodeProcess',pi.hProcess,C.byref(code));row['exit_code']=code.value
            row['cleanup_succeeded']=True
        except Exception as exc:
            cleanup_errors.append(repr(exc))
        # Closing the last private Job handle is a final kill-on-close safeguard.
        for handle in (pi.hThread,pi.hProcess,job):
            if handle:
                result,error=call('CloseHandle',handle)
                if not result:cleanup_errors.append('CloseHandle failed: %d'%error)
        if cleanup_errors:
            row['cleanup_succeeded']=False;row['status']='structural_error';row['cleanup_errors']=cleanup_errors
        row['elapsed_seconds']=time.monotonic()-started
        if row['timeout_detected'] and row['cleanup_succeeded']:row['status']='timeout'


def supervise(path, manifest, only):
    import subprocess
    import os
    import time
    check(sys.version_info[:2]==(3,12),'Python 3.12 required')
    check(os.name=='nt','Supervisor requires Windows Job Objects')
    addresses=[int(only,16)] if only else sorted(int(f['address'],16) for f in manifest['functions'])
    check(addresses and len(addresses)==len(set(addresses)),'Empty or duplicate selection')
    for address in addresses:select_worker(manifest,'0x%08X'%address)
    base=GAME/'build/batches'/manifest['id'];base.mkdir(parents=True,exist_ok=True)
    directory=base/('sequential-'+uuid.uuid4().hex);directory.mkdir(exist_ok=False)
    snapshot=directory/'manifest.json';write_report(snapshot,manifest)
    limits=manifest['limits'];start=time.monotonic();deadline=start+limits['batch_timeout_seconds']
    report={'status':'running','expected_functions':len(addresses),'expected_cases':8*len(addresses),
            'results':[],'limits':limits,'limits_effective':{'function':True,'batch':True,'compile':False},
            'containment':'private Windows Job; suspended creation then assignment and resume; kill-on-close',
            'compile_limit_note':'Independent compile deadline is not enforced; compilation is bounded by whole-worker and remaining-batch deadlines.',
            'manifest_sha256':hashlib.sha256(snapshot.read_bytes()).hexdigest()}
    def checkpoint():
        report['elapsed_seconds']=time.monotonic()-start
        report['cases_executed']=sum(r.get('cases_executed',0) for r in report['results'])
        report['cases_passed']=sum(r.get('cases_passed',0) for r in report['results'])
        scopes={r['timeout_scope'] for r in report['results'] if r.get('timeout_detected') and r.get('timeout_scope')}
        if report.get('timeout_scope')=='batch':scopes.add('batch')
        report['timeout_scope']='batch' if 'batch' in scopes else 'function' if 'function' in scopes else None
        temp=directory/'supervisor.json.tmp'
        with temp.open('x',encoding='utf-8') as h:json.dump(report,h,indent=2)
        temp.replace(directory/'supervisor.json')
    checkpoint()
    try:
        for address in addresses:
            remaining=deadline-time.monotonic()
            if remaining<=0:
                report['status']='timeout';report['timeout_scope']='batch';break
            child_dir=directory/('function-%08X'%address);child_dir.mkdir(exist_ok=False)
            row={'address':'0x%08X'%address,'status':'structural_error','cases_executed':0,'cases_passed':0,'worker_report':str(child_dir/'worker.json')}
            report['results'].append(row);checkpoint()
            seconds=min(limits['function_timeout_seconds'],remaining)
            row['effective_timeout_seconds']=seconds;row['timeout_scope']='batch' if remaining<limits['function_timeout_seconds'] else 'function'
            command=[sys.executable,str(Path(__file__).resolve()),'--manifest',str(snapshot),'--only',row['address'],'--worker-dir',str(child_dir)]
            run_windows_job(command,seconds,row)
            child_path=child_dir/'worker.json'
            if not child_path.exists() and (child_dir/'worker.partial.json').exists():
                child_path=child_dir/'worker.partial.json'
                row['partial_worker_report']=str(child_path)
            if child_path.exists():
                try:child=json.loads(child_path.read_bytes())
                except Exception as exc:
                    row['status']='structural_error';row['report_error']=repr(exc);checkpoint();raise
                row['cases_executed']=len(child.get('cases',[]))
                row['cases_passed']=sum(set(c.get('results',{}))=={'mips','symbolic','native'} and len(set(c['results'].values()))==1 and c.get('writes')==0 for c in child.get('cases',[]))
                row['timings']=child.get('timings',{});row['coverage']=child.get('coverage',{})
                if not row.get('timeout_detected') and row.get('cleanup_succeeded') and 'error' not in row:
                    status=child.get('status');check(status in ('passed','rejected','mismatch','compile_error','structural_error'),'Invalid worker status')
                    check(status!='passed' or row.get('exit_code')==0 and child.get('case_count')==8 and row['cases_passed']==8,'False worker success')
                    row['status']=status
            checkpoint()
            if row['status']=='structural_error':report['status']='structural_error';break
            if row['status']=='timeout' and row['timeout_scope']=='batch':report['status']='timeout';break
        if report['status']=='running':
            report['status']='passed' if len(report['results'])==len(addresses) and all(r['status']=='passed' for r in report['results']) else 'individual_failures'
        if report['status']=='passed':check(report['cases_passed']==report['expected_cases'],'Incomplete batch')
    except Exception as exc:
        report['status']='structural_error';report['error']=repr(exc)
    finally:
        checkpoint();print(json.dumps(report,indent=2));print('Supervisor report:',directory/'supervisor.json')
    return 0 if report['status']=='passed' else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest',required=True,type=Path)
    parser.add_argument('--validate-only',action='store_true')
    parser.add_argument('--only',help='Optional single admitted manifest address; default runs manifest sequentially')
    parser.add_argument('--worker-dir',type=Path,help=argparse.SUPPRESS)
    args=parser.parse_args()
    try:
        path=args.manifest.resolve();manifest=json.loads(path.read_bytes())
        # Child has its own validation and never dispatches directly into execution.
        if args.worker_dir:
            check(not args.validate_only,'Worker cannot bypass execution using validation flag')
            import contextlib
            directory=args.worker_dir.resolve()
            with (directory/'stdout.log').open('x',encoding='utf-8') as stdout, (directory/'stderr.log').open('x',encoding='utf-8') as stderr:
                with contextlib.redirect_stdout(stdout),contextlib.redirect_stderr(stderr):
                    return worker(path,args.only,directory)
        game,report=validate(manifest)
        if not args.validate_only:return supervise(path,manifest,args.only)
        base=GAME/'build/batches'/manifest['id'];base.mkdir(parents=True,exist_ok=True)
        directory=base/('validate-'+uuid.uuid4().hex);directory.mkdir(exist_ok=False)
        report['sha256']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (path,Path(__file__),GAME/'game.json',Path(game.database_path),Path(game.elf_path))}
        report['manifest']=manifest;write_report(directory/'results.json',report)
        print('VALIDATED ONLY:',report['function_count'],'functions;',report['case_count'],'cases;',directory/'results.json')
        return 0
    except Exception as exc:
        print(json.dumps({'status':'structural_error','error_type':type(exc).__name__,'error':str(exc)}),file=sys.stderr);return 1

if __name__=='__main__':
    raise SystemExit(main())
