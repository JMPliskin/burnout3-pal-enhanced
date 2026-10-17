"""Synthetic discovery, relocation and command preservation tests; no PAL execution."""
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
import relocate_native as r

class RelocationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=r.GAME/'build');self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
    def record(self,name,text):
        p=self.root/name;p.mkdir();(p/'discovered.txt').write_text(text);return p/'discovered.txt'
    def test_recorded_pair_and_copy_log(self):
        p=self.record('a','0x80002000 0x00001000\n');(p.parent/'host.log').write_text('[kernel kCopy (0x5A) a0=0x80002000 a1=0x00001000 a2=0x00000018]')
        pairs,blocks,pins=r.discoveries([p]);self.assertEqual(pairs,{0x80002000:0x1000});self.assertEqual(blocks[0]['size'],24);self.assertEqual(len(pins),2)
    def test_duplicate_is_deduplicated_and_conflict_rejected(self):
        p=self.record('a','80002000 00001000\n');q=self.record('b','80002000 00001000\n');self.assertEqual(len(r.discoveries([p,q])[0]),1)
        q.write_text('80002000 00001100\n')
        with self.assertRaisesRegex(ValueError,'Conflicting'):r.discoveries([p,q])
    def test_invalid_and_unaligned_records(self):
        p=self.record('a','invented 1000\n')
        with self.assertRaises(ValueError):r.discoveries([p])
        p.write_text('80002001 1000\n')
        with self.assertRaisesRegex(ValueError,'Unaligned'):r.discoveries([p])
    def game(self,image):
        section=SimpleNamespace(addr=0x1000,size=len(image),is_alloc=True,type=1)
        elf=SimpleNamespace(exec_sections=[section],sections=[section],section_data=lambda s:image)
        return SimpleNamespace(load_elf=lambda:elf)
    def test_call_closure_uses_original_decoder_and_separate_extents(self):
        image=struct.pack('<6I',0x0c000804,0,0x03e00008,0,0x03e00008,0)
        game=self.game(image);pairs,meta=r.expand(game,{0x80002000:0x1000},[dict(run=0x80002000,source=0x1000,size=24)])
        self.assertEqual(pairs,{0x80002000:0x1000,0x80002010:0x1010});self.assertEqual(meta[0x80002000]['length'],16);self.assertEqual(meta[0x80002010]['length'],8)
        emitted=r.port.discovered_functions(game,'',set(pairs.items()))
        self.assertEqual(set(emitted),set(pairs));self.assertIn('F_80002000',emitted[0x80002000][1])
    def test_truncated_copy_and_invalid_source_are_rejected(self):
        image=struct.pack('<4I',0,0,0x03e00008,0);game=self.game(image)
        with self.assertRaises(ValueError):r.expand(game,{0x80002000:0x1000},[dict(run=0x80002000,source=0x1000,size=8)])
        with self.assertRaises(ValueError):r.expand(game,{0x80002000:0x5000},[])
    def test_windows_mirrors(self):
        self.assertEqual(r.physical(0x80002000),0x2000);self.assertEqual(r.physical(0xa0002000),0x2000);self.assertEqual(r.physical(0x70000000),0x70000000)
    def test_audit_calls_real_translation_and_never_overwrites(self):
        text=r.audit_source({0x80002000:dict(run=0x80002000,source=0x1000,length=8,block=None)})
        self.assertIn('F_80002000(c)',text);self.assertIn('CREATE_NEW',text);self.assertIn('Translate(a,n)',text)
    def test_command_roundtrip_preserves_quotes_and_paths(self):
        args=['C:\\Compiler Tools\\cl.exe','-DNAME="Synthetic Game"','/FoC:\\build folder\\test.obj','/FdC:\\build folder\\']
        self.assertEqual(r.argv(subprocess.list2cmdline(args)),args)
    def test_compiler_source_replacement_normalizes_windows_separators(self):
        entry={'file':'C:/build/original.cpp','command':subprocess.list2cmdline(['cl.exe',r'C:\build\original.cpp','/Foold.obj','/Fdold/'])}
        args=r.compile_arguments(entry,Path('C:/new/audit.cpp'),Path('C:/new/audit.obj'),Path('C:/new'))
        self.assertIn(str(Path('C:/new/audit.cpp')),args)
        self.assertNotIn(r'C:\build\original.cpp',args)
        self.assertIn('/Fo'+str(Path('C:/new/audit.obj')),args)

    def test_native_import_preserves_original_rounding_environment(self):
        import os,sys
        code="import os,sys; before=os.environ.get('PS2RT_EE_ROUND_NEAREST'); sys.path.insert(0,'games/burnout3-pal/tools'); import relocate_native; assert os.environ.get('PS2RT_EE_ROUND_NEAREST')==before"
        for value in (None,'caller-defined'):
            env=dict(os.environ)
            if value is None:env.pop('PS2RT_EE_ROUND_NEAREST',None)
            else:env['PS2RT_EE_ROUND_NEAREST']=value
            self.assertEqual(subprocess.run([sys.executable,'-B','-c',code],cwd=r.ROOT,env=env,capture_output=True).returncode,0)

    def test_outside_build_is_rejected(self):
        with self.assertRaises(ValueError):r.inside(r.ROOT/'engine')

    def test_autotest_separate_streams_are_pinned(self):
        p=self.record('autotest','80002000 00001000\n')
        (p.parent/'stdout.log').write_text('diagnostic report')
        (p.parent/'stderr.log').write_text('[kernel kCopy (0x5A) a0=0x80002000 a1=0x00001000 a2=0x00000018]')
        pairs,blocks,pins=r.discoveries([p])
        self.assertEqual(pairs,{0x80002000:0x1000});self.assertEqual(blocks,[dict(run=0x80002000,source=0x1000,size=24)])
        self.assertEqual(set(pins),{str(p),str(p.parent/'stdout.log'),str(p.parent/'stderr.log')})

    def test_pointer_data_does_not_promote_routines(self):
        # Synthetic ordinary entry returns a value loaded in its delay slot.
        # The trailing data deliberately looks like jal; it must stay data.
        image=struct.pack('<6I',0x3c028000,0x03e00008,0x8c422000,0x0c000804,0x03e00008,0)
        pairs,meta=r.expand(self.game(image),{0x80002000:0x1000},[dict(run=0x80002000,source=0x1000,size=24)])
        self.assertEqual(pairs,{0x80002000:0x1000});self.assertEqual(meta[0x80002000]['length'],12)

if __name__=='__main__':unittest.main()
