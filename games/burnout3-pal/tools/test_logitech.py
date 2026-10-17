"""Native synthetic tests of PAL's registered Logitech callback. No game data."""
import json
from pathlib import Path
import sys
import unittest
import uuid
import test_kernel_store as kernel
r=kernel.r

HARNESS=kernel.HARNESS.split('int main() {')[0]+r'''
#include "ps2host/run.h"
int main() {
 RegisterGameOverrides();
 assert(ps2host::rpc && ps2host::rpc_id==0x046D046Du && ps2host::registrations==1);
 const std::vector<u8> input(0x240,0xa5);const auto saved=input;
 auto response=ps2host::rpc(12,input);u32 version=0;
 std::memcpy(&version,response.data()+4,4);assert(version==0x010B1B00u);
 assert(input==saved && response.size()==input.size());
 for(size_t i=0;i<response.size();++i)if(i<4 || i>=8)assert(response[i]==0);
 for(size_t size: {size_t(0),size_t(1),size_t(7),size_t(8),size_t(576),size_t(1024)}) {
  auto answer=ps2host::rpc(12,std::vector<u8>(size,0xff));
  assert(answer.size()==(size<8?8:size));
  std::memcpy(&version,answer.data()+4,4);assert(version==0x010B1B00u);
 }
 for(u32 index: {0u,1u,42u,0xffffffffu}) {
  std::vector<u8> request(0x240,0);std::memcpy(request.data()+4,&index,4);
  auto answer=ps2host::rpc(1,request);u32 result=0;std::memcpy(&result,answer.data(),4);
  assert(result==0x80000001u && answer.size()==request.size());
  for(size_t i=4;i<answer.size();++i)assert(answer[i]==0);
 }
 bool rejected=false;
 try {ps2host::rpc(99,input);}catch(const ps2host::Unimplemented&){rejected=true;}
 assert(rejected);
 std::puts("{\"registration\":true,\"version\":true,\"response_sizes\":true,\"enumeration\":true,\"unsupported_rejected\":true}");
}
'''

class LogitechTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.directory=r.GAME/'build/logitech-tests'/('run-'+uuid.uuid4().hex[:12]);cls.directory.mkdir(parents=True)
  row={};r.runner.run_windows_job([sys.executable,'-B',str(Path(__file__).resolve()),'--worker',str(cls.directory)],90,row)
  r.atomic_json(cls.directory/'containment.json',row)
  if row.get('exit_code')!=0 or not row.get('cleanup_succeeded') or row.get('timeout_detected') or row.get('active_processes_after_cleanup')!=0:raise AssertionError(str(cls.directory))
  cls.results=json.loads((cls.directory/'stdout.log').read_text().splitlines()[-1])
 def test_registration(self):self.assertTrue(self.results['registration'])
 def test_version_at_offset_four(self):self.assertTrue(self.results['version'])
 def test_response_sizes_and_zero_padding(self):self.assertTrue(self.results['response_sizes'])
 def test_enumeration_without_device(self):self.assertTrue(self.results['enumeration'])
 def test_unsupported_is_rejected(self):self.assertTrue(self.results['unsupported_rejected'])

if __name__=='__main__':
 if len(sys.argv)==3 and sys.argv[1]=='--worker':raise SystemExit(kernel.worker(Path(sys.argv[2]),HARNESS))
 unittest.main()
