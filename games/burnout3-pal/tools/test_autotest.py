"""Synthetic AutoTest tests; no game execution or native build."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import autotest as a

class AutoTestTests(unittest.TestCase):
 def row(self):return dict(cleanup_succeeded=True,active_processes_after_cleanup=0,timeout_detected=False)
 def test_missing_dispatch(self):
  d=a.classify('stopped: a call to 0x001156C0, code no function covers, not translated yet\nfunctions executed: 21 distinct',2,self.row())
  self.assertEqual(d['category'],'missing_dispatch');self.assertEqual(d['distinct_functions'],21)
 def test_copy_distinct_from_original(self):
  d=a.classify('stopped: not translated yet\ndiscovered: code at 0x80076000 is a copy of 0x004854E0',2,self.row())
  self.assertEqual(d['category'],'relocated_code_missing')
 def test_same_address_is_not_relocation(self):
  self.assertEqual(a.classify('stopped: not translated yet\ndiscovered: code at 0x001156C0 is a copy of 0x001156C0',2,self.row())['category'],'missing_dispatch')
 def test_windows_crash(self):
  for code in [0xc0000409,-1073740791]:self.assertEqual(a.classify('',code,self.row())['category'],'windows_crash')
 def test_timeout_dominates(self):
  row=self.row();row['timeout_detected']=True
  self.assertEqual(a.classify('stopped: not translated',0,row)['category'],'timeout')
 def test_host_time_limit_with_clean_process_exit(self):
  d=a.classify('stopped: the time limit, in the loop at 0x00443F30',2,self.row())
  self.assertEqual(d['category'],'timeout')
 def test_cleanup_required(self):
  row=self.row();row['active_processes_after_cleanup']=1
  self.assertEqual(a.classify('',0,row)['category'],'infrastructure_error')
 def test_classifier_categories(self):
  for text,category in [('error LNK2019','link_error'),('fatal error C1083','cpp_compile_error'),('unsupported instruction eret','unsupported_instruction'),('stopped: write outside guest memory','invalid_memory_access'),('stopped: IOP service unimplemented','unimplemented_service'),('host tests: failed','test_error'),('unclassified', 'unknown')]:
   self.assertEqual(a.classify(text,2,self.row())['category'],category)
 def test_activity_zero_not_success(self):
  d=a.classify('GS: 0 primitives, 0 pixels written; VU: 0 instruction pairs; IOP 0.0%',0,self.row())
  self.assertFalse(d['activity']['gs']);self.assertFalse(d['activity']['vu']);self.assertEqual(d['category'],'unknown')
 def test_regression_comparison(self):
  self.assertTrue(a.compare({'distinct_functions':20},{'diagnosis':{'distinct_functions':21}})['possible_regression'])
  self.assertFalse(a.compare({'distinct_functions':22},{'diagnosis':{'distinct_functions':21}})['possible_regression'])
 def fixture(self,root):
  exe=root/'game.exe';exe.write_bytes(b'synthetic executable')
  pin=root/'unit.obj';pin.write_bytes(b'synthetic object')
  work={'commands':[['synthetic compiler']],'host_tests':'synthetic test','pins':{str(pin):a.sha(pin)}}
  report={'status':'built_and_host_tests_passed','executable':str(exe),'executable_sha256':a.sha(exe),'commands':[{'command':work['commands'][0],'exit_code':0},{'command':[work['host_tests']],'exit_code':0}]}
  for name,data in [('work.json',work),('build.json',report),('containment.json',dict(self.row(),exit_code=0))]:(root/name).write_text(json.dumps(data))
  (root/'01-stdout.log').write_text('host tests: all passed');return root/'build.json',exe,pin
 def test_confirmed_manifest(self):
  with tempfile.TemporaryDirectory(dir=a.BUILD) as temp:
   p,exe,pin=self.fixture(Path(temp));self.assertEqual(a.confirmed(p)['executable'],exe)
 def test_tampered_binary_rejected(self):
  with tempfile.TemporaryDirectory(dir=a.BUILD) as temp:
   p,exe,pin=self.fixture(Path(temp));exe.write_bytes(b'changed')
   with self.assertRaisesRegex(ValueError,'hash mismatch'):a.confirmed(p)
 def test_incomplete_manifest_rejected(self):
  with tempfile.TemporaryDirectory(dir=a.BUILD) as temp:
   p,exe,pin=self.fixture(Path(temp));data=json.loads(p.read_text());data['commands'].pop();p.write_text(json.dumps(data))
   with self.assertRaisesRegex(ValueError,'Incomplete'):a.confirmed(p)
 def test_changed_object_rejected(self):
  with tempfile.TemporaryDirectory(dir=a.BUILD) as temp:
   p,exe,pin=self.fixture(Path(temp));pin.write_bytes(b'changed')
   with self.assertRaisesRegex(ValueError,'Pinned'):a.confirmed(p)
 def test_outside_build_rejected(self):
  with self.assertRaises(ValueError):a.local(a.ROOT/'README.md')
 def test_no_workers_when_selection_fails(self):
  with patch('sys.argv',['autotest','--mode','FULL']),patch.object(a,'select',side_effect=ValueError('synthetic rejection')),patch.object(a,'launch') as launch:
   self.assertEqual(a.main(),1);launch.assert_not_called()
 def test_baseline_requires_unchanged_logs(self):
  with tempfile.TemporaryDirectory(dir=a.BUILD) as temp:
   root=Path(temp);build=root/'native';build.mkdir();p,exe,pin=self.fixture(build)
   elf=root/'elf';elf.write_bytes(b'synthetic elf')
   run=root/'autotest/run-synthetic';(run/'probe').mkdir(parents=True)
   log=run/'probe/stdout.log';log.write_text('synthetic log')
   row=dict(self.row(),exit_code=2);(run/'probe/containment.json').write_text(json.dumps(row))
   (run/'summary.json').write_text(json.dumps({'status':'diagnosed','containment_clean':True,'exit_code':2,'diagnosis':{'distinct_functions':21}}))
   manifest={'configuration':{'timeout':3,'budget':1000000},'build_manifest':str(p),'build_manifest_sha256':a.sha(p),'elf_sha256':a.sha(elf),'evidence':{str(x.relative_to(run)):a.sha(x) for x in (log,run/'probe/containment.json')}}
   (run/'manifest.json').write_text(json.dumps(manifest))
   with patch.object(a,'BUILD',root),patch.object(a.runner,'load',return_value=SimpleNamespace(elf_path=elf)):
    self.assertIsNotNone(a.baseline_for()[0]);log.write_text('changed');self.assertEqual(a.baseline_for(),(None,None))
 def test_quick_launch_never_builds(self,dump_ram=False):
  with tempfile.TemporaryDirectory(dir=a.BUILD) as temp:
   root=Path(temp);build=root/'native';build.mkdir();p,exe,pin=self.fixture(build)
   elf=root/'elf';elf.write_bytes(b'synthetic elf')
   selected={'manifest':p,'executable':exe,'compatible':True}
   def simulated(directory,command,timeout):
    self.assertEqual(Path(command[0]),exe);self.assertIn('--virtual-time',command)
    self.assertEqual('--dump-ram' in command,dump_ram)
    if dump_ram:self.assertEqual(Path(command[command.index('--dump-ram')+1]),directory/'ee-ram.bin')
    directory.mkdir();(directory/'stdout.log').write_text('stopped: not translated yet\nfunctions executed: 43 distinct')
    (directory/'stderr.log').write_text('');return dict(self.row(),exit_code=2)
   with patch.object(a,'BUILD',root),patch.object(a,'select',return_value=selected),patch.object(a,'confirmed',return_value=selected),patch.object(a.runner,'load',return_value=SimpleNamespace(elf_path=elf,iso=None)),patch.object(a,'baseline_for',return_value=(None,None)),patch.object(a,'launch',side_effect=simulated) as launch,patch('sys.argv',['autotest','--mode','QUICK']+(['--dump-ram'] if dump_ram else [])):
    self.assertEqual(a.main(),1);self.assertEqual(launch.call_count,1)

 def test_optional_ram_dump_stays_in_run(self):
  self.test_quick_launch_never_builds(True)

if __name__=='__main__':unittest.main()
