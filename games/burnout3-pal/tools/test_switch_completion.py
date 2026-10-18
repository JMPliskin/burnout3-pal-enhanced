"""Synthetic extent guards and read-only PAL switch emission checks; no game run."""
import re
import struct
import tempfile
from pathlib import Path
from types import SimpleNamespace
import unittest
import relocate_native as r

class CompletionTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(dir=r.GAME/'build');self.addCleanup(self.temp.cleanup)
  self.root=Path(self.temp.name);self.sources=self.root/'sources';self.sources.mkdir();self.out=self.root/'out';self.out.mkdir()
  self.old=self.sources/'unit.cpp';self.old.write_text('void F_00001000(lifted::Context& c) { c.r[2]=7; }\nvoid F_00002000(lifted::Context& c) { c.r[2]=9; }\n')
  self.before=self.old.read_bytes()
  image=struct.pack('<6I',0x24020007,0x1000fffe,0,0,0x03e00008,0)
  section=SimpleNamespace(addr=0x1000,size=len(image),is_alloc=True,type=1)
  elf=SimpleNamespace(exec_sections=[section],sections=[section],section_data=lambda s:image)
  self.rows=[dict(addr=0x1000,end=0x100c,chunks=[[0x1010,0x1018]])]
  self.game=SimpleNamespace(load_database=lambda:{'functions':self.rows},load_elf=lambda:elf)
 def test_staged_completion_preserves_source_and_other_symbol(self):
  old,new,e=r.complete_function(self.game,self.sources,self.out,0x1000,0x1018)
  self.assertEqual(self.old.read_bytes(),self.before)
  self.assertIn('void F_00002000(lifted::Context& c) { c.r[2]=9; }',new.read_text())
  self.assertEqual(new.read_text().count('void F_00001000('),1)
  self.assertIn('NOT VERIFIED',new.read_text());self.assertEqual(e['original_sha256'],r.digest(old))
 def test_overlapping_function_is_rejected(self):
  self.rows.append(dict(addr=0x1014,end=0x1018,chunks=[]))
  with self.assertRaisesRegex(ValueError,'overlaps'):r.complete_function(self.game,self.sources,self.out,0x1000,0x1018)
 def test_wrong_boundary_is_rejected(self):
  with self.assertRaisesRegex(ValueError,'terminal chunk'):r.complete_function(self.game,self.sources,self.out,0x1000,0x1014)
 def test_existing_output_never_overwritten(self):
  (self.out/'unit.cpp').write_text('occupied')
  with self.assertRaises(FileExistsError):r.complete_function(self.game,self.sources,self.out,0x1000,0x1018)
  self.assertEqual((self.out/'unit.cpp').read_text(),'occupied')
 def test_real_pal_switch_labels_and_delay_slot(self):
  import autotest
  game=r.runner.load(str(r.GAME));sources=Path(autotest.select()['work']['sources'])
  old,new,e=r.complete_function(game,sources,self.out,0x242a50,0x242d50)
  original=old.read_text();text=new.read_text()
  self.assertEqual(re.findall(r'^void F_\w+',original,re.M),re.findall(r'^void F_\w+',text,re.M))
  start=text.index('void F_00242A50(');end=text.index('\nvoid F_',start+10);body=text[start:end]
  elf=game.load_elf();sec=elf.exec_sections[0];image=elf.section_data(sec)
  targets=struct.unpack_from('<99I',image,0x4bb880-sec.addr)
  self.assertTrue(all(0x242a50<=t<0x242d50 for t in targets))
  for t in set(targets):self.assertIn(f'L_{t:08X}:',body)
  self.assertNotIn('TailCall(c, 0x00242AC8u',body)
  self.assertIn('goto L_00242A90',body)
  slot=body.index('c.r[5] = generated::ReadS32((u32)c.r[16] + 0xCu)')
  self.assertLess(slot,body.index('lifted::Call(c, 0x00244208u)',slot))
  self.assertIn('if (c.r[3] == 0ull)',body) # beql predicate retained

if __name__=='__main__':unittest.main()
