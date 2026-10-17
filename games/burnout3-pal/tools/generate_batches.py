"""Generate local PAL manifests from current static admission and native evidence.

Only static inspection; optional --validate-only invokes the runner's validator.
No guest execution, return oracle, disassembly or game bytes are embedded.
"""
import argparse
import ast
import hashlib
import json
import random
import struct
import subprocess
import sys
import uuid
from pathlib import Path

HERE=Path(__file__).resolve().parent
GAME=HERE.parent
ROOT=GAME.parent.parent
sys.path.insert(0,str(HERE))
import run_batch as runner


def require(ok,message):
    if not ok:raise ValueError(message)


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def test_definition(path, name):
    """Read synthetic plans without importing regression scripts or derive.

    Only literal arithmetic/containers and comprehensions are permitted; no
    calls, attributes, imports or arbitrary statements can run.
    """
    environment={}
    allowed=(ast.Expression,ast.Constant,ast.Name,ast.Load,ast.Store,ast.Tuple,
             ast.List,ast.Dict,ast.BinOp,ast.UnaryOp,ast.Add,ast.Sub,ast.Mult,
             ast.LShift,ast.USub,ast.ListComp,ast.comprehension)
    for statement in ast.parse(path.read_text(encoding='utf-8')).body:
        targets=statement.targets if isinstance(statement,ast.Assign) else []
        names=[n.id for target in targets for n in ast.walk(target) if isinstance(n,ast.Name)]
        if name not in names and not set(names)&{'MIN','MAX'}:continue
        expression=ast.Expression(statement.value)
        require(all(isinstance(n,allowed) for n in ast.walk(expression)),'Unsafe synthetic plan expression')
        value=eval(compile(expression,str(path),'eval'),{'__builtins__':{},**environment},{})
        if name in names:return value
        require(isinstance(value,tuple) and len(value)==len(names),'Invalid plan constants')
        environment.update(zip(names,value))
    raise ValueError('Missing synthetic plan: '+name)


def report_hashes(report):
    hashes=report.get('sha256',report.get('inputs_sha256',report.get('inputs',{})))
    require(isinstance(hashes,dict) and hashes,'Missing input hashes')
    resolved={}
    for name,digest in hashes.items():
        path=Path(name.replace('\\','/'))
        if not path.is_absolute():path=ROOT/path
        path=path.resolve()
        require(path.is_relative_to(ROOT.resolve()) and path.is_file(),'Unavailable/outside provenance: '+name)
        require(path not in resolved and sha(path)==digest,'Stale/duplicate provenance: '+name)
        resolved[path]=digest
    return resolved


def checked_results(case, nested, writes):
    values=case.get('results') if nested else case
    require(isinstance(values,dict),'Missing results')
    require(all(type(values.get(k)) is int and -(1<<31)<=values[k]<(1<<31)
                for k in ('mips','symbolic','native')),'Missing/non-s32 results')
    require(values['mips']==values['symbolic']==values['native'],'Divergent results')
    require(type(case.get(writes)) is int and case[writes]==0,'Missing/nonzero memory effects')


def admit_report(path, report, game):
    """Validate known historical contracts; return addresses, never inferred proof."""
    require(report.get('status')=='passed' and report.get('discrepancies')==0,'Unsuccessful execution')
    rejections=report.get('rejections',{})
    require(all(rejections.get(k)==0 for k in ('unsupported','untestable','mismatch')),'Rejected derivation cases')
    hashes=report_hashes(report)
    required=[Path(game.elf_path),Path(game.database_path),GAME/'game.json']
    require(all(p.resolve() in hashes for p in required),'Missing ELF/database/config provenance')
    require('dll' in report and Path(report['dll']).resolve().is_relative_to(path.parent.resolve())
            and Path(report['dll']).is_file(),'Missing/foreign DLL artifact')
    if path.name=='worker.json':
        require((HERE/'run_batch.py').resolve() in hashes,'Missing runner version')
        manifest_path=path.parent.parent/'manifest.json'
        require(manifest_path.resolve() in hashes,'Missing hashed execution manifest')
        manifest=json.loads(manifest_path.read_bytes())
        address=report.get('address');entry=runner.select_worker(manifest,address)
        require(report.get('admission',{}).get('address')==int(address,16),'Function identity conflict')
        require(report.get('case_count')==entry['expected_case_count']==8,'Incomplete case count')
        require(report.get('automatic_fault_mapping') is False,'Fault mapping not disabled')
        expected={c['id']:c for c in entry['cases']}
        require(len(expected)==8,'Duplicate expected case identity')
        cases=report.get('cases',[]);require(len(cases)==8,'Incomplete cases')
        seen=set();vectors=set()
        for case in cases:
            identity=case.get('id');require(identity in expected and identity not in seen,'Unknown/duplicate case identity')
            seen.add(identity);arguments=case.get('arguments')
            require(isinstance(arguments,dict) and arguments==expected[identity]['arguments'],'Missing/incorrect arguments')
            vector=tuple(runner.bits(arguments.get('arg%d'%i,'0x0000000000000000')) for i in range(8))
            require(vector not in vectors,'Duplicate input vector');vectors.add(vector)
            checked_results(case,True,'writes')
            require(any(pc+4 in case.get('delay_slots',[]) for pc in report['admission']['returns']),
                    'Missing executed return delay slot')
        admission=report['admission'];coverage=report.get('coverage',{})
        for field in ('branches','conditional_moves'):
            for pc in admission[field]:
                actual=set(v for case in cases for v in case.get(field,{}).get(str(pc),[]))
                require(actual=={False,True} and set(coverage.get(field,{}).get(str(pc),[]))==actual,
                        'Incomplete/conflicting '+field+' coverage')
        require(all(any(pc+4 in c.get('delay_slots',[]) for c in cases) for pc in admission['returns']),
                'Incomplete return coverage')
        for field in ('slt_relations','movn_conditions'):
            if field in entry['coverage']:
                actual=set(v for case in cases for v in case.get(field,[]))
                require(actual==set(entry['coverage'][field]) and set(coverage.get(field,[]))==actual,
                        'Incomplete/conflicting '+field+' coverage')
        return [int(address,16)]
    kind=path.relative_to(GAME/'build').parts[0]
    require(kind in ('cpp-smoke','cpp-memory'),'Unknown execution report format')
    memory=kind=='cpp-memory'
    definition=test_definition(HERE/('test_native_memory.py' if memory else 'test_native_cpp.py'),'SPEC' if memory else 'CASES')
    functions=report.get('functions',[])
    require(len(functions)==len(definition) and report.get('total_cases')==8*len(definition),'Incomplete historical report')
    addresses=set()
    for function in functions:
        address=int(function['address'],16)
        require(address in definition and address not in addresses,'Unknown/duplicate function identity');addresses.add(address)
        require(function.get('run_cases')=={'outcome':'ok','tested':8},'Incomplete native run_cases')
        cases=function.get('cases',[]);require(len(cases)==8,'Incomplete historical cases')
        seen=set()
        if not memory:
            require(sum(s[0]==address for s in report.get('signatures',[]))==1,'Missing compiled identity')
        for case in cases:
            checked_results(case,memory,'memory_writes')
            if not memory:
                require(type(case.get('a0')) is int and type(case.get('a1')) is int,'Missing smoke arguments')
                identity=(case['a0'],case['a1'])
                require(identity in definition[address] and identity not in seen,'Unknown/duplicate smoke inputs')
            else:
                identity=case.get('case');require(type(identity) is int and 1<=identity<=8 and identity not in seen,'Unknown/duplicate memory case')
                offset,width,signed,values=definition[address]
                base=int(game.layout['scratch'],0)+0x100+(identity-1)*0x100
                field=values[identity-1].to_bytes(width,'little')
                require(case.get('field_hex')==field.hex() and case.get('read_address')=='0x%08X'%(base+offset)
                        and case.get('width')==width and case.get('signed') is signed,'Incorrect memory input/contract')
                expected=[base+offset,width,signed,int.from_bytes(field,'little',signed=signed)]
                reads=case.get('reads',{})
                require(all(isinstance(reads.get(k),list) and reads[k] and all(r==expected for r in reads[k])
                            for k in ('mips','symbolic','native')),'Missing/incorrect input read trace')
                require(len(reads['mips'])==len(reads['native'])==1,'Incorrect read count')
            seen.add(identity)
    if memory:require(report.get('automatic_fault_mapping') is False,'Fault mapping not disabled')
    return sorted(addresses)


def verified_functions(game):
    paths=set((GAME/'build').glob('batches/**/worker.json'))
    paths.update((GAME/'build').glob('cpp-*/**/results.json'))
    evidence={};rejected=[]
    for path in sorted(paths):
        report=json.loads(path.read_bytes())
        try:addresses=admit_report(path,report,game)
        except (ValueError,KeyError,TypeError,AttributeError,OverflowError) as exc:
            rejected.append(str(path.relative_to(ROOT))+': '+str(exc));continue
        for address in addresses:
            evidence.setdefault(address,[]).append({'report':str(path.relative_to(ROOT)).replace('\\','/'),'sha256':sha(path)})
    manifests={}
    for path in sorted((HERE/'batches').glob('*.json')):
        data=json.loads(path.read_bytes())
        manifests[data['id']]={'path':str(path.relative_to(ROOT)).replace('\\','/'),'sha256':sha(path),'verified_addresses':[f['address'] for f in data['functions'] if int(f['address'],16) in evidence]}
    missing={int(a,16) for m in manifests.values() for a in
             json.loads((ROOT/m['path']).read_bytes())['provenance'].get('previously_verified_excluded',[])}
    missing.update(int(f['address'],16) for m in manifests.values()
                   for f in json.loads((ROOT/m['path']).read_bytes())['functions'])
    require(not missing-set(evidence),'Missing current native evidence for historical regression addresses: '+
            ', '.join('0x%08X'%a for a in sorted(missing-set(evidence)))+'; rejected reports: '+'; '.join(rejected))
    return evidence,manifests


MASK=(1<<64)-1
ANCHORS=[0,MASK,0x7fffffff,0xffffffff80000000,0xffffffff,
         0x8000000000000000,0x7fffffffffffffff,0xaaaaaaaa55555555]


def comparison_plan(code):
    """Resolve only unchanged entry arguments/zero in the straight entry prefix.

    No expression solver, path assumptions or alias inference. Writes invalidate
    provenance; after any control transfer even lexical operands are unknown.
    """
    sources={0:'zero',**{r:'arg%d'%(r-4) for r in range(4,12)}}
    prefix=True;plans=[]
    for i in code:
        if i.mnemonic in ('slt','sltu'):
            operands=[sources.get(i.rs),sources.get(i.rt)] if prefix else [None,None]
            plans.append({'address':'0x%08X'%i.addr,'operation':i.mnemonic,
                          'signed':i.mnemonic=='slt','width_bits':64,'operands':operands,
                          'limitation':None if all(x is not None for x in operands) else
                          'Operand transformed/unknown or comparison after control transfer; equality not inferred.'})
        if i.has_delay_slot:prefix=False
        n=i.mnemonic
        if n in ('lui','addiu','andi','ori','xori','slti','sltiu'):dest=i.rt
        elif n in ('nop','jr','b','beq','bne','bltz','bgez','blez','bgtz'):dest=None
        else:dest=i.rd
        if dest:sources.pop(dest,None)
    return plans


def relation(a,b,signed):
    def value(x):return x-(1<<64) if signed and x&(1<<63) else x
    a,b=value(a),value(b)
    return 'less' if a<b else 'greater' if a>b else 'equal'


def generate_cases(admission,plans,seed):
    rng=random.Random((seed<<32)^admission['address'])
    names=admission['input_arguments'];vectors=[]
    for plan in plans:
        a,b=plan['operands']
        if plan['limitation']:continue
        pairs=[(MASK,0),(0,0),(0,MASK)] if plan['signed'] else [(0,1),(0,0),(1,0)]
        if a=='zero':pairs=[(0,MASK),(0,0),(0,1)]
        if b=='zero':pairs=[(MASK,0),(0,0),(1,0)]
        for av,bv in pairs:
            if a==b and av!=bv:continue
            if a=='zero' and av or b=='zero' and bv:continue
            vector=dict.fromkeys(names,0)
            if a!='zero':vector[a]=av
            if b!='zero':vector[b]=bv
            if vector not in vectors and len(vectors)<8:vectors.append(vector)
    permutations={name:rng.sample(ANCHORS,8) for name in names}
    for index in range(8):
        if len(vectors)==8:break
        vectors.append({name:values[index] for name,values in permutations.items()})
    cases=[{'id':'case-%02d'%(index+1),'arguments':{n:'0x%016X'%v for n,v in vector.items()},'objects':[]}
           for index,vector in enumerate(vectors)]
    coverage=[]
    for plan in plans:
        row=dict(plan);row['status']='pending_execution'
        row['planned_relations']=[] if plan['limitation'] else sorted({relation(
            0 if plan['operands'][0]=='zero' else v[plan['operands'][0]],
            0 if plan['operands'][1]=='zero' else v[plan['operands'][1]],plan['signed']) for v in vectors})
        if not plan['limitation'] and set(row['planned_relations'])!={'less','equal','greater'}:
            row['limitation']='Three relations not constructible within eight cases (including identical operands/zero).'
        coverage.append(row)
    return cases,coverage


def candidate_trace(function,row,admission,comparisons):
    confidence=function.get('confidence','unknown');callers=sorted(function.get('callers',[]))
    warnings=[]
    if confidence!='called' or not callers:warnings.append('No called-entry identification with recorded callers; boundaries need confirmation.')
    if admission['branches'] or admission['conditional_moves']:
        warnings.append('Branch/conditional-move representative coverage pending; no path solver.')
    if any(c['limitation'] for c in comparisons):warnings.append('Comparison input coverage incomplete or unresolved.')
    classification='exploratory' if warnings else 'regression_eligible'
    return {'address':'0x%08X'%function['addr'],'classification':classification,'verified':False,
            'boundary_source':'game.database_path: functions addr/end/chunks',
            'boundaries':{'start':'0x%08X'%function['addr'],'end':'0x%08X'%function['end'],'chunks':function.get('chunks',[])},
            'identification':{'confidence':confidence,'callers':['0x%08X'%a for a in callers]},
            'criteria':{'difficulty_level':row['difficulty_level'],'admitted_profile':admission['profile'],
                        'acyclic_cfg_and_return_delay_slots':'runner static admission',
                        'representative_inputs':'unchanged entry comparisons plus deterministic u64 anchors'},
            'case_count':8,'case_nature':'synthetic u64 registers; no return oracle; s32 return ABI',
            'comparisons':comparisons,'warnings':warnings,'inventory_uncertainties':row.get('uncertainties',[])}


def priority(trace,row):
    identification=trace['identification'];confidence=identification['confidence']
    return (trace['classification']!='regression_eligible',
            0 if confidence=='called' and identification['callers'] else 1 if confidence=='pointer' else 2,
            len(trace['warnings']),-len(identification['callers']),
            row['difficulty_level'],row['size_bytes'],row['address'])


def prepare(seed,limit):
    game=runner.load(str(GAME));database=game.load_database()
    require(len(database['functions'])==9384,'Unexpected clean base')
    by={f['addr']:f for f in database['functions']};require(len(by)==9384,'Duplicate base starts')
    inventory_path=GAME/'build/classification/functions.json';inventory=json.loads(inventory_path.read_bytes())
    for name,expected in inventory['sha256'].items():
        path=(ROOT/name).resolve();require(path.is_relative_to(ROOT),'Invalid inventory hash path')
        require(path.is_file() and sha(path)==expected,'Stale inventory input: '+name)
    require(len(inventory['functions'])==9384 and {r['address'] for r in inventory['functions']}==set(by),'Inventory/base address mismatch')
    evidence,manifests=verified_functions(game)
    elf=game.load_elf();section=elf.exec_sections[0];image=elf.section_data(section)
    selected=[];discarded=[]
    for row in sorted(inventory['functions'],key=lambda r:(r['difficulty_level'],r['size_bytes'],r['address'])):
        if row['difficulty_level']!=1 or row['address'] in evidence:continue
        try:admission=runner.admit_function(by[row['address']],section,image,game.vu_regions)
        except ValueError as exc:
            discarded.append({'address':'0x%08X'%row['address'],'reason':str(exc)});continue
        code=[runner.DISASM.decode(struct.unpack_from('<I',image,pc-section.addr)[0],pc)
              for pc in range(by[row['address']]['addr'],by[row['address']]['end'],4)]
        cases,comparisons=generate_cases(admission,comparison_plan(code),seed)
        trace=candidate_trace(by[row['address']],row,admission,comparisons)
        selected.append((priority(trace,row),admission,cases,trace))
    selected.sort(key=lambda item:item[0])
    candidate_records=[]
    for index,(rank,admission,cases,trace) in enumerate(selected):
        trace['priority_key']=list(rank)
        trace['selected']=index<limit
        candidate_records.append(trace)
    deferred=[{'address':item[3]['address'],'reason':'Outside requested limit','classification':item[3]['classification']} for item in selected[limit:]]
    selected=selected[:limit]
    require(selected,'No admitted new candidates')
    functions=[];coverage=[]
    for rank,admission,cases,trace in selected:
        address='0x%08X'%admission['address']
        contract={'all_return_delay_slots':True}
        signed=[c for c in trace['comparisons'] if c['signed']]
        if signed and all(not c['limitation'] for c in signed):
            contract['slt_relations']=['less','equal','greater']
        functions.append({'address':address,'expected_case_count':8,'memory_contract':{'reads':[],'writes':[]},'coverage':contract,'cases':cases})
        coverage.append({'address':address,'profile':admission['profile'],'input_arguments':admission['input_arguments'],
                         'branches':[{'address':'0x%08X'%pc,'expected_outcomes':[False,True],'status':'pending_execution'} for pc in admission['branches']],
                         'conditional_moves':[{'address':'0x%08X'%pc,'expected_outcomes':[False,True],'status':'pending_execution'} for pc in admission['conditional_moves']],
                         'return_delay_slots':{'required':True,'status':'pending_execution'},
                         'entry_confidence':by[admission['address']]['confidence'],
                         'note':'Static admission only; generated cases may fail strict coverage. Runner coverage is never relaxed.'})
    manifest={'schema_version':1,'id':'generated-seed-%d'%seed,'mode':'explicit',
              'limits':{'max_passes':1,'instruction_budget':100000,'function_timeout_seconds':60,'compile_timeout_seconds':120,'batch_timeout_seconds':600},
              'provenance':{'generator':'generate_batches.py','generator_sha256':sha(Path(__file__)),
                            'runner_sha256':sha(HERE/'run_batch.py'),'inventory_sha256':sha(inventory_path),
                            'input_sha256':{str(Path(p).relative_to(ROOT)).replace('\\','/'):sha(Path(p))
                                            for p in (game.elf_path,game.database_path,GAME/'game.json')},
                            'seed':seed,'requested_limit':limit,'verified_exclusions':{'0x%08X'%a:reports for a,reports in sorted(evidence.items())},
                            'source_manifests':manifests,'candidates':candidate_records,'deferred':deferred,'discarded':discarded,'expected_coverage':coverage,
                            'case_policy':'Up to eight cases: direct entry comparison relations first, then seeded u64 anchors; omitted arguments zero; no oracle.',
                            'limitations':['Only current runner register profile; return ABI s32.','Historical regression addresses require current native evidence; additional proven addresses are excluded without a fixed count.','Coverage pending execution, not proved by generation/validation.','Independent compile timeout remains unimplemented.']},
              'functions':functions}
    runner.validate(manifest) # Static rules reused; imports no derive and runs no guest.
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--list',action='store_true')
    mode.add_argument('--generate',action='store_true')
    parser.add_argument('--seed',type=int,default=1)
    parser.add_argument('--limit',type=int,default=10)
    parser.add_argument('--validate-only',action='store_true',help='After generation, run only run_batch.py --validate-only')
    args=parser.parse_args()
    try:
        require(sys.version_info[:2]==(3,12),'Python 3.12 required')
        require(1<=args.limit<=10 and 0<=args.seed<(1<<32),'limit must be 1..10 and seed u32')
        require(not args.validate_only or args.generate,'--validate-only requires --generate')
        manifest=prepare(args.seed,args.limit)
        for function in manifest['functions']:print(function['address'],'8 cases; coverage pending')
        if args.list:return 0
        base=GAME/'build/generated-batches';base.mkdir(parents=True,exist_ok=True)
        directory=base/('run-'+uuid.uuid4().hex);directory.mkdir(exist_ok=False)
        path=directory/'manifest.json'
        with path.open('x',encoding='utf-8') as handle:json.dump(manifest,handle,indent=2);handle.write('\n')
        require(json.loads(path.read_bytes())==manifest,'Written JSON mismatch')
        print('Manifest:',path)
        if args.validate_only:
            command=[sys.executable,str(HERE/'run_batch.py'),'--manifest',str(path),'--validate-only']
            with (directory/'validation.stdout.log').open('x',encoding='utf-8') as stdout,(directory/'validation.stderr.log').open('x',encoding='utf-8') as stderr:
                result=subprocess.run(command,cwd=str(ROOT),stdout=stdout,stderr=stderr)
            status={'status':'validated_only' if result.returncode==0 else 'validation_failed','exit_code':result.returncode,'dynamic_execution':False,'functions':len(manifest['functions']),'cases':8*len(manifest['functions']),'manifest_sha256':sha(path)}
            with (directory/'validation.json').open('x',encoding='utf-8') as handle:json.dump(status,handle,indent=2)
            require(result.returncode==0,'Runner rejected generated manifest; see validation logs')
            print('VALIDATED ONLY:',status['functions'],'functions,',status['cases'],'cases; no execution')
        return 0
    except Exception as exc:
        print('Generation error:',repr(exc),file=sys.stderr);return 1

if __name__=='__main__':
    raise SystemExit(main())
