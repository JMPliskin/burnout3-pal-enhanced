"""Isolated output-path tests: no real VU generation, build or game execution."""
from contextlib import ExitStack, redirect_stdout, redirect_stderr
import io
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import sync_native as sync
import port


class SyncNativePathsTests(unittest.TestCase):
    def test_historical_manifest_keeps_original_executable_and_targets(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp);exe=directory/'burnout3-pal.exe'
            exe.write_bytes(b'synthetic historical executable')
            work=directory/'work.json'
            sync.write(work,{'directory':str(directory),'vcvars':'synthetic',
                            'pins':{},'configure':['cmake','synthetic'],
                            'tests':['host.exe','gpu.exe'],'engine':'synthetic',
                            'executable':str(exe)})
            commands=[]
            def run_job(command,timeout,row):
                task=json.loads(Path(command[-1]).read_bytes());commands.append(task['command'])
                Path(task['stdout']).write_text('host tests: all passed')
                Path(task['stderr']).write_text('')
                row.update(exit_code=0,timeout_detected=False,cleanup_succeeded=True)
            with patch.object(sync.runner,'run_windows_job',side_effect=run_job),redirect_stdout(io.StringIO()):
                sync.execute(work)
            report=json.loads((directory/'build.json').read_bytes())
            self.assertEqual(report['executable'],str(exe))
            self.assertNotIn('pal_player_startup_tests',commands[1])
            self.assertEqual(len(commands),4)

    def test_default_parent_unchanged(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(sync,'ROOT',Path(temp)):
            directory=sync.build_directory()
            self.assertEqual(directory.parent,Path(temp)/'build')
            self.assertTrue(directory.is_dir())

    def test_relative_external_parent_with_spaces(self):
        with tempfile.TemporaryDirectory() as temp:
            parent=Path(temp)/'workspace with spaces'/'builds'
            directory=sync.build_directory(Path(os.path.relpath(parent)))
            self.assertEqual(directory.parent,parent)
            self.assertTrue(directory.is_absolute())

    def test_collision_preserves_existing_files(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(sync.uuid,'uuid4',return_value=SimpleNamespace(hex='a'*32)):
            directory=sync.build_directory(Path(temp))
            sentinel=directory/'keep.txt';sentinel.write_text('unchanged')
            with self.assertRaises(FileExistsError):sync.build_directory(Path(temp))
            self.assertEqual(sentinel.read_text(),'unchanged')

    def test_file_is_not_a_build_root(self):
        with tempfile.TemporaryDirectory() as temp:
            file=Path(temp)/'file';file.write_text('keep')
            with self.assertRaises(OSError):sync.build_directory(file)
            self.assertEqual(file.read_text(),'keep')

    def test_cli_default_call_unchanged(self):
        with patch.object(sync.sys,'argv',['sync_native.py','--prepare','reference']),patch.object(sync,'prepare') as prepare:
            self.assertEqual(sync.main(),0)
            prepare.assert_called_once_with(Path('reference'))

    def test_cli_external_root(self):
        with patch.object(sync.sys,'argv',['sync_native.py','--prepare','reference','--build-root','workspace with spaces']),patch.object(sync,'prepare') as prepare:
            sync.main()
            prepare.assert_called_once_with(Path('reference'),build_root=Path('workspace with spaces'))

    def test_execute_rejects_root_override_before_work(self):
        with patch.object(sync.sys,'argv',['sync_native.py','--execute','work.json','--build-root','external']),patch.object(sync,'execute') as execute,redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:sync.main()
            self.assertEqual(error.exception.code,2)
            execute.assert_not_called()

    def test_external_execute_rejects_changed_pin_before_launch(self):
        with tempfile.TemporaryDirectory(prefix='external builds ') as temp:
            directory=Path(temp);source=directory/'input.txt';source.write_text('changed')
            work=directory/'work.json'
            sync.write(work,{'directory':str(directory),'pins':{str(source):'0'*64}})
            with patch.object(sync.runner,'run_windows_job') as launch:
                with self.assertRaisesRegex(ValueError,'Input changed'):sync.execute(work)
                launch.assert_not_called()
            self.assertEqual(json.loads((directory/'build.json').read_bytes())['status'],'failed')

    def test_prepare_and_execute_paths_without_generating_or_compiling(self):
        for external in (False,True):
            with self.subTest(external=external),tempfile.TemporaryDirectory(prefix='PAL paths ') as temp,ExitStack() as mocks:
                root=Path(temp)/'integration';game=root/'games/burnout3-pal'
                reference=Path(temp)/'stable';historical=reference/'games/burnout3-pal/build'
                previous=historical/'native-relocated/run-75fc2363eedf'
                original=historical/'native-relocated/run-180cb57a8a54'
                sources=historical/'published/lifted';sources.mkdir(parents=True)
                (sources/'synthetic.cpp').write_text('// synthetic fixture only')
                generated_original=original/'port/generated';generated_original.mkdir(parents=True)
                for name in ['dispatch.cpp','dispatch_overlays.cpp','discovered.cpp','relocation_audit.cpp']:
                    (generated_original/name).write_text('// synthetic fixture only')
                (original/'bulk_011_000.cpp').write_text('// synthetic fixture only')
                cache=historical/'bootstrap/port/cmake'
                for name in ['vulkan_headers','volk','sdl3','glslang']:(cache/'_deps'/(name+'-src')).mkdir(parents=True)
                (cache/'ps2host/third_party').mkdir(parents=True)
                (cache/'ps2host/third_party/header.h').write_text('// synthetic header')
                game.mkdir(parents=True);(game/'port').mkdir();(game/'port/overrides.cpp').write_text('// synthetic')
                vu=historical/'vu-captures/run-1d27d7abf9a5/vu_programs.txt';vu.parent.mkdir(parents=True);vu.write_text('synthetic manifest')
                previous.mkdir(parents=True);exe=previous/'old.exe';exe.write_bytes(b'synthetic executable')
                real_sha=sync.sha
                old={'pins':{str(exe):real_sha(exe)},'sources':str(sources),'base':str(historical/'bootstrap'),'original_functions':1,'vcvars':str(Path(temp)/'tools with spaces/vcvars.bat')}
                sync.write(previous/'work.json',old)
                sync.write(previous/'build.json',{'status':'built_and_host_tests_passed','executable':str(exe),'executable_sha256':real_sha(exe)})
                mocks.enter_context(patch.object(sync,'ROOT',root));mocks.enter_context(patch.object(sync,'GAME',game))
                mocks.enter_context(patch.object(sync,'sha',side_effect=lambda p:'deff4afda9b0d277532c222ff8e56919d5829743a850122c0ef1d10cf98d1a82' if Path(p)==vu else real_sha(p)))
                mocks.enter_context(patch.object(sync,'git',side_effect=lambda *args:'160000 engine-revision 0 engine' if args[0]=='ls-files' else 'git-revision'))
                mocks.enter_context(patch.object(sync.subprocess,'check_output',return_value='engine-revision'))
                mocks.enter_context(patch.object(sync.subprocess,'run',return_value=SimpleNamespace(returncode=1,stdout='')))
                mocks.enter_context(patch.object(sync.vurecomp,'read_manifest',return_value=[object()]*97))
                generator=mocks.enter_context(patch.object(sync.vurecomp,'main'))
                mocks.enter_context(patch.object(port,'translated_functions',return_value=[0x1234]))
                parent=Path(temp)/'external workspace/builds' if external else root/'build'
                sentinel=parent/'existing-build/keep.txt';sentinel.parent.mkdir(parents=True);sentinel.write_text('keep')
                with redirect_stdout(io.StringIO()):work=sync.prepare(reference,parent if external else None)
                cfg=json.loads(work.read_bytes());directory=work.parent
                self.assertEqual(directory.parent,parent)
                command=cfg['configure']
                self.assertEqual(command[command.index('-B')+1],str(directory/'cmake'))
                self.assertIn('-DPS2_GENERATED_DIR='+str(directory/'generated'),command)
                self.assertIn('-DPS2_PAL_SOURCES_FILE='+str(directory/'pal_sources.txt'),command)
                self.assertEqual(cfg['executable'],str(directory/'cmake/Burnout3-PAL-Enhanced.exe'))
                self.assertEqual(cfg['vu_manifest'],str(vu))
                self.assertEqual((directory/'pal_sources.txt').read_text(),(sources/'synthetic.cpp').as_posix()+'\n')
                generator.assert_called_once_with(['--manifest',str(directory/'vu_programs.txt'),'--out-dir',str(directory/'generated')])
                launches=[]
                def run_job(command,timeout,row):
                    task=json.loads(Path(command[-1]).read_bytes());launches.append(task)
                    Path(task['stdout']).write_text('host tests: all passed' if task['stdout'].endswith('host-tests.stdout.log') else '')
                    Path(task['stderr']).write_text('')
                    if task['command'][:2]==['cmake','--build']:
                        new_exe=Path(cfg['executable']);new_exe.parent.mkdir(parents=True,exist_ok=True);new_exe.write_bytes(b'synthetic new executable')
                    row.update(exit_code=0,timeout_detected=False,cleanup_succeeded=True)
                mocks.enter_context(patch.object(sync.runner,'run_windows_job',side_effect=run_job))
                with redirect_stdout(io.StringIO()):sync.execute(work)
                self.assertEqual(launches[0]['command'],command)
                self.assertEqual(launches[1]['command'][2],str(directory/'cmake'))
                self.assertTrue(all(Path(x['directory'])==directory and Path(x['stdout']).parent==directory for x in launches))
                self.assertEqual(json.loads((directory/'build.json').read_bytes())['status'],'built_and_tested')
                self.assertEqual(sentinel.read_text(),'keep')
                self.assertEqual(exe.read_bytes(),b'synthetic executable')


if __name__=='__main__':unittest.main()
