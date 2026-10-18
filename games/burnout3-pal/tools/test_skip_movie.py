"""Actual PAL override with synthetic screen/movie state; no game execution."""
import sys,struct,unittest,uuid
from pathlib import Path
import test_kernel_store as kernel
r=kernel.r

HARNESS=kernel.HARNESS.split('int main() {')[0]+r'''
int main() {
 RegisterGameOverrides();auto fn=ps2host::bindings.back();assert(fn.first==0x00131410);
 constexpr u32 screen=0x1000,movie=screen+0x1c8;
 auto init=[&](int state,int active,int finished,int stop,int loop){
  generated::memory.clear();generated::memory[screen+0x2f8]=u8(state);
  generated::memory[movie+0x48]=u8(active);generated::memory[movie+0x49]=u8(finished);
  generated::memory[movie+0x4a]=u8(stop);generated::memory[movie+0x4b]=u8(loop);
 };
 auto step=[&](u16 held,int expected){
  ps2host::held=held;lifted::Context c{};c.r[4]=screen;c.r[5]=0x123456789abcdef0ull;
  auto before=c;int calls=movie_calls,writes=generated::writes;fn.second(c);
  assert(movie_calls==calls+1 && generated::writes==writes+expected);
  before.r[2]=0x55;assert(std::memcmp(&c,&before,sizeof(c))==0);
  if(expected)assert(generated::address==movie+0x4a && generated::value==1 && generated::size==1);
 };
 auto release=[&]{init(0,0,0,0,0);step(0,0);};
 release();init(0x17,1,0,0,0);step(8,1);
 assert(generated::memory[movie+0x48]==1 && generated::memory[movie+0x49]==0);
 // Even if the game clears the request, a held Start must not re-request it.
 generated::memory[movie+0x4a]=0;step(8,0);
 release();init(0x17,1,0,0,1);step(8,0);
 release();init(0x17,1,1,0,0);step(8,0);
 release();init(0x16,1,0,0,0);step(8,0);
 release();init(0x17,0,0,0,0);step(8,0);
 release();init(0x17,1,0,1,0);step(8,0);
 release();init(0x17,1,0,0,0);step(0,0);
 std::puts("8 movie scenarios passed; original called every time; completion untouched");
}
'''

class SkipMovieTests(unittest.TestCase):
    def test_real_override_synthetic_scenarios(self):
        d=r.GAME/'build/movie-tests'/('run-'+uuid.uuid4().hex[:12]);d.mkdir(parents=True)
        row={};r.runner.run_windows_job([sys.executable,'-B',__file__,'--worker',str(d)],90,row)
        r.atomic_json(d/'containment.json',row)
        self.assertEqual(row.get('exit_code'),0,str(d));self.assertTrue(row.get('cleanup_succeeded'))
        self.assertFalse(row.get('timeout_detected'));self.assertEqual(row.get('active_processes_after_cleanup'),0)
        self.assertIn('8 movie scenarios passed',(d/'stdout.log').read_text())

    def test_pal_contract_from_original_instructions(self):
        game=r.runner.load(str(r.GAME));elf=game.load_elf();s=elf.exec_sections[0];data=elf.section_data(s)
        def instruction(pc):return str(r.DISASM.decode(struct.unpack_from('<I',data,pc-s.addr)[0],pc))
        for pc,parts in {0x131434:('lw','$s0','0x2f8'),0x131438:('0x17',),
                         0x131444:('jal','003251b0'),0x131448:('addiu','$a0','$s0','0x1c8'),
                         0x325058:('sb','$v1','0x4a','$a0'),0x3251c0:('lbu','0x48'),
                         0x3251e4:('lbu','0x4a'),0x32525c:('lw','$t9'),
                         0x32528c:('lw','$t9'),0x3252ac:('sb','$zero','0x48'),
                         0x3252b0:('sb','$v1','0x49'),0x3253a8:('lbu','0x4b')}.items():
            with self.subTest(pc=hex(pc)):
                text=instruction(pc).lower()
                for part in parts:self.assertIn(part,text)

if __name__=='__main__':
    if len(sys.argv)==3 and sys.argv[1]=='--worker':raise SystemExit(kernel.worker(Path(sys.argv[2]),HARNESS))
    unittest.main()
