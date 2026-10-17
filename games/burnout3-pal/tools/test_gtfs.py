"""Read-only local PAL module compatibility checks; no IOP execution."""
import re
import struct
import unittest
import relocate_native as r
from ps2decomp.iso9660 import Iso9660

class GtfsTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  game=r.runner.load(str(r.GAME))
  with Iso9660.open(game.path(game.iso)) as iso:
   cls.image=iso.read_file(iso.find('IOP/GTFSCDVD.IRX'))
   cls.resource=iso.find('DATA/PRGDATA.BIN')
  cls.u=lambda self,o:struct.unpack_from('<I',self.image,o)[0]
  cls.h=lambda self,o:struct.unpack_from('<H',self.image,o)[0]
 def test_module_and_resource_exist(self):
  self.assertEqual(self.image[:4],b'\x7fELF')
  self.assertEqual(self.h(18),8) # MIPS
  self.assertGreater(self.resource.size,0)
  ph=self.u(28)
  segments=[ph+i*self.h(42) for i in range(self.h(44))]
  load=next(x for x in segments if self.u(x)==1)
  self.assertEqual(self.u(load+8),0)
  self.assertLess(self.u(24),self.u(load+16))
  self.assertLessEqual(self.u(load+4)+self.u(load+16),len(self.image))
 def test_relocations_supported(self):
  types=set()
  for i in range(self.h(48)):
   sh=self.u(32)+i*self.h(46)
   if self.u(sh+4)!=9:continue
   end=self.u(sh+16)+self.u(sh+20)
   self.assertLessEqual(end,len(self.image))
   for off in range(self.u(sh+16),end,8):types.add(self.u(off+4)&255)
  self.assertTrue(types)
  self.assertLessEqual(types,{2,4,5,6})
 def test_imports_have_host_implementations(self):
  source=(r.ROOT/'engine/host/src/iop_libs.cpp').read_text()
  supported={(name,int(index)) for name,index in re.findall(r'\{\{"([^"]+)",\s*(\d+)\}',source)}
  count=0
  for off in range(0,len(self.image)-20,4):
   if self.u(off)!=0x41e00000:continue
   name=self.image[off+12:off+20].split(b'\0')[0].decode('ascii');stub=off+20
   while stub+8<=len(self.image) and self.u(stub)==0x03e00008 and self.u(stub+4)>>16==0x2400:
    self.assertIn((name,self.u(stub+4)&65535),supported)
    count+=1;stub+=8
  self.assertGreater(count,0)

if __name__=='__main__':unittest.main()
