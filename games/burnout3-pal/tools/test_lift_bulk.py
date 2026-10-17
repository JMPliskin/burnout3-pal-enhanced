"""Synthetic inventory, emission, interruption and publication tests; no PAL runs."""
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import lift_bulk as bulk


class BulkTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=bulk.inc.GAME/'build');self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);(self.root/'src/lifted').mkdir(parents=True)
        self.original=self.root/'src/lifted/lifted_000.cpp'
        self.original.write_text('void F_00002000(lifted::Context& c) {\n}\n')
        for name in ('game.json','elf','db.json'):(self.root/name).write_text('{}')
        image=struct.pack('<4I',0x03E00008,0,0x03E00008,0)
        section=SimpleNamespace(addr=0x1000,size=16,is_alloc=True,type=1)
        elf=SimpleNamespace(exec_sections=[section],sections=[section],section_data=lambda s:image)
        functions=[dict(addr=0x1000,end=0x1008,size=8,chunks=[]),dict(addr=0x1008,end=0x1010,size=8,chunks=[]),dict(addr=0x2000,end=0x2008,size=8,chunks=[])]
        game=SimpleNamespace(build='build',elf_path=str(self.root/'elf'),database_path=str(self.root/'db.json'),path=lambda p:str(self.root/p))
        for p in [patch.object(bulk.inc,'GAME',self.root),patch.object(bulk,'BASE',self.root/'build/bulk'),
                  patch.object(bulk,'environment',return_value=(game,{'functions':functions},elf,section,image)),
                  patch.object(bulk.testplan,'global_pointers',return_value=[]),
                  patch.object(bulk.verdicts,'fingerprint',return_value='synthetic'),patch.object(bulk.verdicts,'_files',return_value=[])]:
            p.start();self.addCleanup(p.stop)
        self.calls=[]

    def fake(self,command,seconds,row):
        self.calls.append(command);path=Path(command[4]);plan=json.loads(path.read_bytes())
        b=plan['batches'][int(command[5])];phase=command[6];directory=path.parent/command[7]
        result=directory/'result.json'
        if phase=='compile':
            dll=directory/'synthetic.dll';dll.write_bytes(b'synthetic artifact')
            source=directory/'synthetic.cpp';source.write_text('// synthetic input')
            bulk.atomic_json(result,dict(built=b['addresses'],dll=str(dll),dll_sha256=bulk.inc.digest(dll),
                compiled_sources={str(source):bulk.inc.digest(source)},outcomes={}))
        else:
            r=json.loads(result.read_bytes());r['outcomes']={str(a):'verified' if a==0x1000 else 'untestable: synthetic cases' for a in b['addresses']}
            bulk.atomic_json(result,r)
        row.update(exit_code=0,cleanup_succeeded=True,active_processes_after_cleanup=0,timeout_detected=False)

    def test_full_inventory_and_resumable_execution_preserve_sources(self):
        before=self.original.read_bytes();path=bulk.plan_create(size=1)
        with patch.object(bulk.runner,'run_windows_job',side_effect=self.fake):
            bulk.execute(path,1);self.assertEqual(len(self.calls),2)
            bulk.execute(path);self.assertEqual(len(self.calls),4)
            bulk.execute(path);self.assertEqual(len(self.calls),4)
        self.assertEqual(self.original.read_bytes(),before)
        plan=json.loads(path.read_bytes());self.assertEqual(len(plan['records']),3)
        self.assertTrue(all(b['status']=='done' for b in plan['batches']))

    def test_publish_unique_dispatch_and_unverified_labels(self):
        path=bulk.plan_create(size=2)
        with patch.object(bulk.runner,'run_windows_job',side_effect=self.fake):bulk.execute(path)
        out=bulk.publish(path)
        self.assertEqual(bulk.inc.port.translated_functions(str(out)),[0x1000,0x1008,0x2000])
        self.assertEqual((out/self.original.name).read_bytes(),self.original.read_bytes())
        self.assertIn('NOT VERIFIED',next(out.glob('bulk*.cpp')).read_text())
        with self.assertRaises(OSError):bulk.publish(path)

    def test_failed_batch_keeps_prior_results_and_blocks_publication(self):
        path=bulk.plan_create(size=1)
        def failure(command,seconds,row):
            if command[5]=='1':row.update(exit_code=1,cleanup_succeeded=True,active_processes_after_cleanup=0,timeout_detected=False)
            else:self.fake(command,seconds,row)
        with patch.object(bulk.runner,'run_windows_job',side_effect=failure):
            with self.assertRaisesRegex(ValueError,'Compile infrastructure'):bulk.execute(path)
        plan=json.loads(path.read_bytes());self.assertEqual(plan['batches'][0]['status'],'done')
        self.assertEqual(plan['records'][str(0x1000)]['status'],'verified')
        with self.assertRaisesRegex(ValueError,'Incomplete'):bulk.publish(path)

    def test_completed_artifact_tampering_rejected(self):
        path=bulk.plan_create()
        with patch.object(bulk.runner,'run_windows_job',side_effect=self.fake):bulk.execute(path)
        plan=json.loads(path.read_bytes());p=Path(next(iter(plan['batches'][0]['artifacts'])))
        p.write_text('{}')
        with self.assertRaisesRegex(ValueError,'artifact changed'):bulk.execute(path)

    def test_timeout_keeps_completed_verdict_and_labels_remainder_unverified(self):
        path=bulk.plan_create()
        def timeout(command,seconds,row):
            self.fake(command,seconds,row)
            if command[6]=='verify':
                p=path.parent/command[7]/'result.json';r=json.loads(p.read_bytes());r['outcomes']={str(0x1000):'verified'};bulk.atomic_json(p,r)
                row.update(exit_code=1,timeout_detected=True)
        with patch.object(bulk.runner,'run_windows_job',side_effect=timeout):bulk.execute(path)
        plan=json.loads(path.read_bytes())
        self.assertEqual(plan['records'][str(0x1000)]['status'],'verified')
        self.assertEqual(plan['records'][str(0x1008)]['status'],'unverified')
        self.assertEqual(plan['records'][str(0x1008)]['category'],'wall_timeout')

    def test_source_mutation_and_concurrent_execution_are_rejected(self):
        path=bulk.plan_create()
        with bulk.lock(path.parent/'run.lock'):
            with self.assertRaisesRegex(ValueError,'locked'):bulk.execute(path)
        self.original.write_text('changed')
        with self.assertRaisesRegex(ValueError,'Baseline changed'):bulk.execute(path)

    def test_missing_dll_pin_is_rejected_even_with_updated_result_hash(self):
        path=bulk.plan_create()
        with patch.object(bulk.runner,'run_windows_job',side_effect=self.fake):bulk.execute(path)
        plan=json.loads(path.read_bytes());batch=plan['batches'][0]
        p=Path(next(x for x in batch['artifacts'] if x.endswith('result.json')))
        result=json.loads(p.read_bytes());del result['dll_sha256'];bulk.atomic_json(p,result)
        batch['artifacts'][str(p)]=bulk.inc.digest(p);bulk.atomic_json(path,plan)
        with self.assertRaisesRegex(ValueError,'Incomplete compiled evidence'):bulk.execute(path)


    def test_verifier_infrastructure_failure_stops_before_next_batch(self):
        path=bulk.plan_create(size=1)
        def failure(command,seconds,row):
            self.fake(command,seconds,row)
            if command[6]=='verify':row['exit_code']=1
        with patch.object(bulk.runner,'run_windows_job',side_effect=failure):
            with self.assertRaisesRegex(ValueError,'Verification infrastructure'):bulk.execute(path)
        plan=json.loads(path.read_bytes())
        self.assertEqual(len(self.calls),2)
        self.assertEqual(plan['batches'][0]['status'],'verification_blocked')
        self.assertEqual(plan['batches'][1]['status'],'pending')
        self.assertTrue((path.parent/plan['batches'][0]['attempt']/'result.json').exists())

    def test_structural_failure_takes_priority_over_timeout(self):
        path=bulk.plan_create(size=1)
        def failure(command,seconds,row):
            self.fake(command,seconds,row)
            row.update(status='structural_error',timeout_detected=True)
        with patch.object(bulk.runner,'run_windows_job',side_effect=failure):
            with self.assertRaisesRegex(ValueError,'Structural supervisor'):bulk.execute(path)
        plan=json.loads(path.read_bytes());self.assertEqual(len(self.calls),1)
        self.assertEqual(plan['batches'][0]['status'],'infrastructure_error')
        self.assertEqual(plan['batches'][1]['status'],'pending')

    def test_compile_timeout_is_unconfirmed_and_preserves_baseline(self):
        path=bulk.plan_create(size=2)
        def timeout(command,seconds,row):
            self.fake(command,seconds,row)
            row.update(timeout_detected=True,exit_code=1)
        with patch.object(bulk.runner,'run_windows_job',side_effect=timeout):bulk.execute(path)
        plan=json.loads(path.read_bytes())
        self.assertEqual(len(self.calls),1)
        for address in (0x1000,0x1008):
            self.assertEqual(plan['records'][str(address)]['status'],'compile_unconfirmed')
        output=bulk.publish(path)
        self.assertEqual(bulk.inc.port.translated_functions(str(output)),[0x2000])
        self.assertEqual((output/self.original.name).read_bytes(),self.original.read_bytes())

    def test_memory_fault_categories_preserve_read_write_distinction(self):
        self.assertEqual(bulk.category('untestable: MemoryFault: load of N bytes at unmapped N'),'memory_read')
        self.assertEqual(bulk.category('untestable: MemoryFault: store of N bytes at unmapped N'),'memory_write')


class AtomicWriteTests(unittest.TestCase):
    def test_transient_windows_sharing_error_retries_without_deleting(self):
        error=PermissionError('synthetic Windows reader');error.winerror=5
        with patch.object(bulk,'_atomic_json',side_effect=[error,None]) as writer,patch.object(bulk.time,'sleep'):
            bulk.atomic_json('synthetic.json',{'valid':True})
        self.assertEqual(writer.call_count,2)
        self.assertEqual(writer.call_args.args,('synthetic.json',{'valid':True}))

    def test_permanent_persistence_failure_is_bounded_and_propagates(self):
        error=PermissionError('synthetic Windows reader');error.winerror=32
        with patch.object(bulk,'_atomic_json',side_effect=error) as writer,patch.object(bulk.time,'sleep'):
            with self.assertRaises(PermissionError):bulk.atomic_json('synthetic.json',{})
        self.assertEqual(writer.call_count,8)

    def test_other_persistence_error_is_not_retried(self):
        with patch.object(bulk,'_atomic_json',side_effect=ValueError('invalid path')) as writer:
            with self.assertRaises(ValueError):bulk.atomic_json('synthetic.json',{})
        self.assertEqual(writer.call_count,1)


class ResourcesTests(unittest.TestCase):
    def test_memory_reduces_eight_six_four(self):
        sample={'ram_percent':85}
        self.assertEqual(bulk.bulk_resources.limit(8,sample),6)
        self.assertEqual(bulk.bulk_resources.limit(6,sample),4)
        self.assertEqual(bulk.bulk_resources.limit(4,sample),4)

    def test_paging_and_resource_errors_reduce_limit(self):
        for reason in ('paging_pressure','resource_error'):
            self.assertEqual(bulk.bulk_resources.limit(8,{'ram_percent':20,reason:True}),6)

    def test_no_pressure_keeps_limit(self):
        self.assertEqual(bulk.bulk_resources.limit(8,{'ram_percent':84,'paging_pressure':False}),8)

    def test_paging_requires_sustained_pressure(self):
        pressure=bulk.bulk_resources.paging_pressure
        self.assertFalse(pressure(500,69,20))
        self.assertFalse(pressure(12000,69,2))
        self.assertTrue(pressure(12000,69,10))
        self.assertTrue(pressure(1500,81,10))
        self.assertFalse(pressure(None,90,30))

    @unittest.skipUnless(__import__('os').name=='nt','Windows telemetry')
    def test_real_telemetry_without_pal(self):
        monitor=bulk.bulk_resources.Monitor()
        try:
            sample=monitor.sample()
            self.assertGreater(monitor.cpu_count,0)
            self.assertTrue(0<=sample['ram_percent']<=100)
            self.assertGreater(sample['available_bytes'],0)
            self.assertIn('pages_input_per_second',sample)
        finally:monitor.close()


if __name__=='__main__':unittest.main()
