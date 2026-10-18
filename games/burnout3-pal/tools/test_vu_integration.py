"""Synthetic PAL VU manifest integration; no extracted game code."""
from pathlib import Path
import tempfile
import unittest
import json
from unittest.mock import patch
import relocate_native as r


class VuIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=r.GAME/'build')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.source=self.root/'capture.txt'
        self.target=self.root/'output';self.target.mkdir()

    def stage(self,text):
        self.source.write_text(text,encoding='ascii')
        return r.stage_vu_manifest(self.source,self.target)

    def test_copy_preserves_original_and_counts_units(self):
        raw='vu0 0 0:8000033C,400002FF,8000033C,2FF\nvu1 0 0:8000033C,400002FF,8000033C,2FF\n'
        result=self.stage(raw)
        self.assertEqual((result['programs'],result['vu0'],result['vu1'],result['pairs']),(2,1,1,4))
        self.assertEqual(self.source.read_bytes(),Path(result['manifest']).read_bytes())
        with self.assertRaises(FileExistsError):r.stage_vu_manifest(self.source,self.target)

    def test_invalid_records_are_not_silently_ignored(self):
        for text in ('','vu2 0 0:0,0\n','vu0 0 0:0\n','vu0 8 0:0,0\n','vu0 0 FF8:0,0,0,0\n','vu1 0 0:100000000,0\n','vu0 0 0:0,0;0:0,0\n'):
            with self.subTest(text=text),self.assertRaises(ValueError):self.stage(text)
        self.assertFalse((self.target/'vu_programs.txt').exists())

    def test_duplicate_capture_rejected(self):
        with self.assertRaises(ValueError):self.stage('vu0 0 0:0,0\n'*2)

    def test_same_entry_different_microcode_is_preserved(self):
        self.assertEqual(self.stage('vu0 0 0:0,0\nvu0 0 0:1,0\n')['programs'],2)

    def test_original_generator_produces_registry_and_blocks(self):
        self.stage('vu1 0 0:8000033C,400002FF,8000033C,2FF\n')
        programs=r.port.vurecomp.read_manifest(str(self.source))
        files=r.port.vurecomp.generate_files(programs,'synthetic integration')
        self.assertIn('vu_programs.cpp',files)
        self.assertTrue(any(n.startswith('vu_blocks_') for n in files))
        self.assertIn('RegisterVuProgram',files['vu_programs.cpp'])

    def reuse_fixture(self):
        old=self.root/'old';old.mkdir();exe=old/'game.exe';exe.write_bytes(b'synthetic executable')
        obj=old/'vu_programs.cpp.obj';obj.write_bytes(b'synthetic VU object')
        override=r.GAME/'port/overrides.cpp';oldobj=old/'overrides.cpp.obj';oldobj.write_bytes(b'synthetic override object')
        commands=[['cl.exe',str(override),'/Fo'+str(oldobj)],['cmake','--intdir=old','--','link.exe','@old.rsp','/out:'+str(exe)]]
        (old/'work.json').write_text(json.dumps({'commands':commands,'pins':{str(override):r.digest(override)},'vu':{'programs':1}}))
        (old/'build.json').write_text('{}')
        (old/'link-inputs.rsp').write_text(r.subprocess.list2cmdline([str(oldobj),str(obj)]))
        proof={'status':'passed','executable_sha256':r.digest(exe),'commands':[{'exit_code':0}]*3,'vu_objects_sha256':{str(obj):r.digest(obj)}}
        (old/'differential3.json').write_text(json.dumps(proof))
        return old,exe,obj

    def test_override_relink_never_generates_or_compiles_vu(self):
        old,exe,obj=self.reuse_fixture()
        with patch('autotest.confirmed',return_value={'manifest':old/'build.json','executable':exe}),patch.object(r.port,'generate',side_effect=AssertionError('must not generate')):
            path=r.prepare_override_update(old/'build.json')
        self.addCleanup(r.shutil.rmtree,path.parent)
        work=json.loads(path.read_bytes())
        self.assertEqual(len(work['commands']),2)
        self.assertIn(str(r.GAME/'port/overrides.cpp'),work['commands'][0])
        self.assertIn(str(obj),r.argv((path.parent/'link-inputs.rsp').read_text()))
        self.assertEqual(work['pins'][str(obj)],r.digest(obj))

    def test_changed_vu_object_blocks_reuse(self):
        old,exe,obj=self.reuse_fixture();obj.write_bytes(b'changed')
        with patch('autotest.confirmed',return_value={'manifest':old/'build.json','executable':exe}):
            with self.assertRaisesRegex(ValueError,'VU object changed'):r.prepare_override_update(old/'build.json')


if __name__=='__main__':unittest.main()
