"""PAL binding/ABI checks. Callback spies test routing, not filesystem semantics."""
import json,struct,sys,unittest,uuid
from pathlib import Path
import test_kernel_store as kernel
r=kernel.r
HARNESS=kernel.HARNESS.split('int main() {')[0]+r'''
int main() {
 RegisterGameOverrides();
 const u32 addresses[]={0x244208,0x2445c0,0x246578,0x246778,0x245fb0,0x2470c8,0x245388};
 assert(ps2host::bindings.size()==8 && ps2host::bindings.back().first==0x00131410);
 for(size_t i=0;i<7;++i) {
  assert(ps2host::bindings[i].first==addresses[i]);
  for(bool host:{false,true}) {
   ps2host::mc2::SetCardDirectory(host?"synthetic-card":"");
   ps2host::mc2::host_hit=ps2host::mc2::game_hit=0;
   lifted::Context c{};for(int n=4;n<=9;++n)c.r[n]=0x1234000000000000ull+n;
   auto before=c;ps2host::bindings[i].second(c);
   assert(c.r[2]==i+1+(host?0:100));
   assert(ps2host::mc2::host_hit==(host?i+1:0));
   assert(ps2host::mc2::game_hit==(host?0:i+1));
   for(int n=4;n<=9;++n)assert(c.r[n]==before.r[n]);
  }
 }
 std::puts("14 routing cases passed");
}
'''
class MemcardTests(unittest.TestCase):
 def test_actual_cardor_bindings_and_fallback(self):
  d=r.GAME/'build/memcard-tests'/('run-'+uuid.uuid4().hex[:12]);d.mkdir(parents=True)
  row={};r.runner.run_windows_job([sys.executable,'-B',__file__,'--worker',str(d)],90,row);r.atomic_json(d/'containment.json',row)
  self.assertEqual(row.get('exit_code'),0,str(d));self.assertTrue(row.get('cleanup_succeeded'));self.assertFalse(row.get('timeout_detected'))
  self.assertIn('14 routing cases passed',(d/'stdout.log').read_text())
 def test_pal_worker_handlers_and_argument_delay_slots(self):
  g=r.runner.load(str(r.GAME));e=g.load_elf();sec=e.exec_sections[0];b=e.section_data(sec)
  expected={2:(0x244208,0x242ad0,0x8e05000c),3:(0x2445c0,0x242b34,0x8e040000),5:(0x246578,0x242af4,0x8e080014),6:(0x246778,0x242b1c,0x8e080014),7:(0x245fb0,0x242b54,0x8e06000c),10:(0x2470c8,0x242bd8,0x8e090018),11:(0x245fb0,0x242b8c,0x8e06000c),14:(0x245388,0x242c54,0x0000402d)}
  for cmd,(handler,call,slot) in expected.items():
   target=struct.unpack_from('<I',b,call-sec.addr)[0];self.assertEqual(((call+4)&0xf0000000)|((target&0x3ffffff)<<2),handler)
   self.assertEqual(struct.unpack_from('<I',b,call+4-sec.addr)[0],slot)
   entry=struct.unpack_from('<I',b,0x4bb880-sec.addr+4*(cmd-2))[0];self.assertLessEqual(entry,call)
 def test_current_build_retains_worker_completion(self):
  import autotest
  self.assertIn([0x242a50,0x242d50],autotest.select()['work']['completions'])

if __name__=='__main__':
 if len(sys.argv)==3 and sys.argv[1]=='--worker':raise SystemExit(kernel.worker(Path(sys.argv[2]),HARNESS))
 unittest.main()
