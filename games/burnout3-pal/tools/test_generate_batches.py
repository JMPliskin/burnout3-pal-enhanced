"""Synthetic generator tests: no guest execution, derivation or compilation."""
import json
import copy
from pathlib import Path
from types import SimpleNamespace as NS
import tempfile
import unittest
from unittest.mock import patch

import generate_batches as gen


def instruction(name='slt',rs=4,rt=5,rd=2,addr=0x1000,control=False):
    return NS(mnemonic=name,rs=rs,rt=rt,rd=rd,addr=addr,has_delay_slot=control)


def admission():
    return dict(address=0x1000,input_arguments=['arg0','arg1'],branches=[],
                conditional_moves=[],profile='acyclic_registers_s32_return')


class GeneratorTests(unittest.TestCase):
    def trace(self,confidence='called',callers=None,comparisons=None):
        f=dict(addr=0x1000,end=0x1010,confidence=confidence,
               callers=[0x2000] if callers is None else callers)
        row=dict(difficulty_level=1,size_bytes=16,address=0x1000)
        return gen.candidate_trace(f,row,admission(),comparisons or []),row

    def test_classification(self):
        self.assertEqual(self.trace()[0]['classification'],'regression_eligible')
        for confidence in ['pointer','recovered: adjacent','unknown']:
            self.assertEqual(self.trace(confidence)[0]['classification'],'exploratory')
        self.assertEqual(self.trace(callers=[])[0]['classification'],'exploratory')
        self.assertFalse(self.trace()[0]['verified'])

    def test_priority_deterministic(self):
        items=[self.trace('recovered: adjacent'),self.trace('called'),self.trace('pointer')]
        key=lambda pair:gen.priority(*pair)
        self.assertEqual(sorted(items,key=key),sorted(reversed(items),key=key))
        self.assertEqual(sorted(items,key=key)[0][0]['classification'],'regression_eligible')

    def test_branch_uncertainty_prevents_eligibility(self):
        a=admission();a['branches']=[0x1004]
        trace=gen.candidate_trace(dict(addr=0x1000,end=0x1010,confidence='called',callers=[0x2000]),
                                  dict(difficulty_level=1),a,[])
        self.assertEqual(trace['classification'],'exploratory')

    def test_comparison_budget_never_silently_claims_coverage(self):
        a=admission();a['input_arguments']=['arg%d'%i for i in range(8)]
        plans=gen.comparison_plan([instruction(rs=4+i,rt=5+i,rd=2,addr=0x1000+i*4) for i in range(7)])
        cases,coverage=gen.generate_cases(a,plans,7)
        self.assertEqual(len(cases),8)
        for c in coverage:
            if set(c['planned_relations'])!={'less','equal','greater'}:
                self.assertIsNotNone(c['limitation'])

    def test_signed_unsigned_relations(self):
        for name in ['slt','sltu']:
            plans=gen.comparison_plan([instruction(name)])
            cases,coverage=gen.generate_cases(admission(),plans,17)
            self.assertEqual(set(coverage[0]['planned_relations']),{'less','equal','greater'})
            self.assertIsNone(coverage[0]['limitation'])
            self.assertTrue(any(c['arguments']['arg0']==c['arguments']['arg1'] for c in cases))
            self.assertEqual(coverage[0]['status'],'pending_execution')
        self.assertEqual(gen.relation(gen.MASK,0,True),'less')
        self.assertEqual(gen.relation(gen.MASK,0,False),'greater')

    def test_transformed_and_control_operands(self):
        for prefix in [instruction('andi',rt=4),instruction('beq',control=True)]:
            plans=gen.comparison_plan([prefix,instruction(addr=0x1004)])
            _,coverage=gen.generate_cases(admission(),plans,1)
            self.assertIsNotNone(coverage[0]['limitation'])
            self.assertEqual(coverage[0]['planned_relations'],[])
            self.assertEqual(self.trace(comparisons=coverage)[0]['classification'],'exploratory')

    def test_zero_and_identical_operands(self):
        for name in ['slt','sltu']:
            for rs,rt in [(0,4),(4,0),(4,4),(0,0)]:
                _,coverage=gen.generate_cases(admission(),gen.comparison_plan([instruction(name,rs,rt)]),0)
                self.assertIn('equal',coverage[0]['planned_relations'])
                if rs==rt or name=='sltu':
                    self.assertIsNotNone(coverage[0]['limitation'])
                else:
                    self.assertIsNone(coverage[0]['limitation'])

    def test_format_and_reproducibility(self):
        plans=gen.comparison_plan([instruction()])
        first=gen.generate_cases(admission(),plans,123)
        self.assertEqual(first,gen.generate_cases(admission(),plans,123))
        cases,_=first
        self.assertEqual(len(cases),8)
        for case in cases:
            gen.runner.validate_memory(case,{'reads':[],'writes':[]},0x20000)
        entry=dict(address='0x00001000',expected_case_count=8,cases=cases,
                   memory_contract={'reads':[],'writes':[]},coverage={'all_return_delay_slots':True})
        self.assertEqual(gen.runner.select_worker({'mode':'explicit','functions':[entry]},entry['address']),entry)
        self.assertEqual(cases,json.loads(json.dumps(cases)))

    def evidence_fixture(self,root):
        game=root/'game';tools=game/'tools';(tools/'batches').mkdir(parents=True)
        elf=game/'elf';base=game/'base';config=game/'game.json';runner=tools/'run_batch.py'
        for path in (elf,base,config,runner):path.write_bytes(b'synthetic input')
        execution=game/'build/batches/test/run';directory=execution/'function-00001000';directory.mkdir(parents=True)
        dll=directory/'native.dll';dll.write_bytes(b'synthetic artifact; never loaded')
        cases=[dict(id='case-%02d'%(i+1),arguments={'arg0':'0x%016X'%i},objects=[]) for i in range(8)]
        entry=dict(address='0x00001000',expected_case_count=8,cases=cases,
                   memory_contract={'reads':[],'writes':[]},coverage={'all_return_delay_slots':True})
        manifest=dict(id='test',mode='explicit',functions=[entry],provenance={})
        snapshot=execution/'manifest.json';snapshot.write_text(json.dumps(manifest))
        (tools/'batches/test.json').write_text(json.dumps(manifest))
        report=dict(status='passed',address=entry['address'],discrepancies=0,case_count=8,
                    automatic_fault_mapping=False,dll=str(dll),
                    rejections=dict(unsupported=0,untestable=0,mismatch=0),
                    sha256={str(p):gen.sha(p) for p in (elf,base,config,runner,snapshot)},
                    admission=dict(address=0x1000,returns=[0x1008],branches=[],conditional_moves=[]),
                    coverage=dict(branches={},conditional_moves={}),
                    cases=[dict(id=c['id'],arguments=c['arguments'],results=dict(mips=i,symbolic=i,native=i),
                                writes=0,delay_slots=[0x100c]) for i,c in enumerate(cases)])
        return game,tools,NS(elf_path=elf,database_path=base),directory/'worker.json',report

    def test_real_worker_structure_positive_and_no_fixed_count(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);game,tools,config,path,report=self.evidence_fixture(root)
            with patch.multiple(gen,GAME=game,HERE=tools,ROOT=root):
                path.write_text(json.dumps(report))
                self.assertEqual(gen.admit_report(path,report,config),[0x1000])
                self.assertEqual(set(gen.verified_functions(config)[0]),{0x1000})

    def test_incomplete_and_incompatible_evidence_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);game,tools,config,path,original=self.evidence_fixture(root)
            def change(label,mutate):
                with self.subTest(label=label):
                    report=copy.deepcopy(original);mutate(report)
                    with self.assertRaises((ValueError,KeyError)):
                        gen.admit_report(path,report,config)
                    path.write_text(json.dumps(report))
                    with self.assertRaisesRegex(ValueError,'Missing current native evidence'):
                        gen.verified_functions(config)
            with patch.multiple(gen,GAME=game,HERE=tools,ROOT=root):
                change('no arguments',lambda r:r['cases'][0].pop('arguments'))
                change('duplicate identity',lambda r:r['cases'][1].update(id=r['cases'][0]['id']))
                change('wrong function',lambda r:r.update(address='0x00002000'))
                change('conflicting admission',lambda r:r['admission'].update(address=0x2000))
                change('incomplete cases',lambda r:r['cases'].pop())
                change('missing result',lambda r:r['cases'][0]['results'].pop('native'))
                change('divergent result',lambda r:r['cases'][0]['results'].update(native=99))
                change('failed execution',lambda r:r.update(status='timeout'))
                change('missing writes',lambda r:r['cases'][0].pop('writes'))
                change('incorrect inputs',lambda r:r['cases'][0]['arguments'].update(arg0='0x0000000000000099'))
                change('stale hash',lambda r:r['sha256'].update({str(config.elf_path):'0'*64}))
                change('missing version',lambda r:r['sha256'].pop(str(tools/'run_batch.py')))
                change('missing snapshot hash',lambda r:r['sha256'].pop(str(path.parent.parent/'manifest.json')))
                change('missing artifact',lambda r:r.update(dll=str(path.parent/'absent.dll')))
                change('failed derivation',lambda r:r['rejections'].update(mismatch=1))
                change('missing delay coverage',lambda r:r['cases'][0].update(delay_slots=[]))

    def test_duplicate_inputs_rejected_even_with_distinct_ids(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);game,tools,config,path,report=self.evidence_fixture(root)
            snapshot=path.parent.parent/'manifest.json';manifest=json.loads(snapshot.read_bytes())
            manifest['functions'][0]['cases'][1]['arguments']=manifest['functions'][0]['cases'][0]['arguments']
            snapshot.write_text(json.dumps(manifest));report['sha256'][str(snapshot)]=gen.sha(snapshot)
            report['cases'][1]['arguments']=report['cases'][0]['arguments']
            with patch.multiple(gen,GAME=game,HERE=tools,ROOT=root):
                with self.assertRaisesRegex(ValueError,'Duplicate input vector'):gen.admit_report(path,report,config)

    def test_historical_smoke_and_memory_structures(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);game,tools,config,_,worker=self.evidence_fixture(root)
            config.layout={'scratch':'0x10000'}
            # Synthetic plans use the same source container shapes as the real scripts.
            (tools/'test_native_cpp.py').write_text('MIN, MAX = -(1 << 31), (1 << 31)-1\nCASES = {4096: [(v, 0) for v in (0,1,2,3,4,5,6,7)]}\n')
            (tools/'test_native_memory.py').write_text('SPEC = {4096: (12,2,False,[0,1,2,3,4,5,6,7])}\n')
            with patch.multiple(gen,GAME=game,HERE=tools,ROOT=root):
                for memory in (False,True):
                    directory=game/'build'/('cpp-memory' if memory else 'cpp-smoke')/'run';directory.mkdir(parents=True)
                    path=directory/'results.json';dll=directory/'native.dll';dll.write_bytes(b'synthetic')
                    cases=[]
                    for i in range(8):
                        if memory:
                            addr=0x10000+0x100+i*0x100+12;read=[addr,2,False,i]
                            case=dict(case=i+1,field_hex=i.to_bytes(2,'little').hex(),read_address='0x%08X'%addr,
                                      width=2,signed=False,results=dict(mips=i,symbolic=i,native=i),
                                      reads={k:[read] for k in ('mips','symbolic','native')},memory_writes=0)
                        else:case=dict(a0=i,a1=0,mips=i,symbolic=i,native=i,memory_writes=0)
                        cases.append(case)
                    report=dict(status='passed',discrepancies=0,dll=str(dll),total_cases=8,
                                rejections=worker['rejections'],automatic_fault_mapping=False,
                                signatures=[[4096,'synthetic','s32',[0],'u64 arg0']],
                                functions=[dict(address='0x00001000',run_cases=dict(outcome='ok',tested=8),cases=cases)])
                    hash_key='inputs_sha256' if memory else 'inputs'
                    report[hash_key]={str(p):gen.sha(p) for p in (config.elf_path,config.database_path,game/'game.json')}
                    self.assertEqual(gen.admit_report(path,report,config),[4096])
                    mutations=[lambda r:r['functions'][0].update(address='0x00002000'),
                               lambda r:r['functions'][0]['cases'].__setitem__(1,copy.deepcopy(r['functions'][0]['cases'][0])),
                               lambda r:r['functions'][0]['cases'][0].pop('field_hex' if memory else 'a0'),
                               lambda r:r['functions'][0]['run_cases'].update(outcome='mismatch'),
                               lambda r:r[hash_key].update({str(game/'game.json'):'0'*64})]
                    for index,mutate in enumerate(mutations):
                        with self.subTest(memory=memory,mutation=index):
                            bad=copy.deepcopy(report);mutate(bad)
                            with self.assertRaises(ValueError):gen.admit_report(path,bad,config)


if __name__=='__main__':
    unittest.main()
