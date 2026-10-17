"""Synthetic API failures and benign Windows integration; no PAL inputs."""
import ctypes as C
import contextlib
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid
import unittest
from unittest.mock import patch

import run_batch as runner


class Function:
    def __init__(self, fn): self.fn=fn
    def __call__(self, *args): return self.fn(*args)


class FakeKernel:
    def __init__(self, failure=None):
        self.failure=failure;self.calls=[];self.closed=[];self.flags=None
    def __getattr__(self, name):
        def invoke(*args):
            self.calls.append(name)
            if name=='InitializeProcThreadAttributeList' and args[0] is None:
                if self.failure=='size_query':C.set_last_error(5);return 0
                args[3]._obj.value=64;C.set_last_error(122);return 0
            if name==self.failure:
                C.set_last_error(5)
                return 0xffffffff if name=='ResumeThread' else 0
            if name=='CreateJobObjectW': return 100
            if name=='CreateProcessW':
                self.flags=args[5]
                assert args[4] is False
                si=args[8].contents
                assert si.cb==C.sizeof(type(si))+C.sizeof(C.c_void_p)
                if C.sizeof(C.c_void_p)==8:assert si.cb==112
                assert C.c_void_p.from_address(C.addressof(si)+C.sizeof(type(si))).value
                pi=args[9]._obj
                pi.hProcess=101;pi.hThread=102;pi.dwProcessId=103
            if name=='UpdateProcThreadAttribute':
                assert args[2]==0x2000D and args[4]==C.sizeof(W.HANDLE)
                assert args[3][0]==100
            if name=='IsProcessInJob': args[2]._obj.value=int(self.failure!='not_member')
            if name=='QueryInformationJobObject': args[2]._obj.ActiveProcesses=0
            if name=='GetExitCodeProcess': args[1]._obj.value=0
            if name=='CloseHandle': self.closed.append(args[0])
            if name=='WaitForSingleObject': return 0
            return 1
        fn=Function(invoke);setattr(self,name,fn);return fn


@unittest.skipUnless(os.name=='nt','Win32 ctypes required')
class SyntheticTests(unittest.TestCase):
    def run_fake(self, failure=None):
        kernel=FakeKernel(failure);row={}
        with patch.object(C,'WinDLL',return_value=kernel):
            runner.run_windows_job([sys.executable,'-c','pass'],2,row)
        return kernel,row

    def test_flags_layout_order_and_cleanup(self):
        k,row=self.run_fake()
        self.assertEqual(k.flags,0x08080004)
        self.assertTrue(row['job_assigned_at_creation'])
        self.assertTrue(row['cleanup_succeeded'])
        self.assertEqual(k.closed,[102,101,100])
        self.assertLess(k.calls.index('UpdateProcThreadAttribute'),k.calls.index('CreateProcessW'))
        self.assertLess(k.calls.index('IsProcessInJob'),k.calls.index('ResumeThread'))
        self.assertNotIn('AssignProcessToJobObject',k.calls)
        self.assertEqual(k.calls.count('DeleteProcThreadAttributeList'),1)

    def test_failure_paths_no_fallback(self):
        for api in ['size_query','CreateJobObjectW','SetInformationJobObject',
                    'InitializeProcThreadAttributeList','UpdateProcThreadAttribute',
                    'CreateProcessW','IsProcessInJob','not_member','ResumeThread']:
            with self.subTest(api=api):
                k,row=self.run_fake(api)
                self.assertEqual(row['status'],'structural_error')
                self.assertNotIn('AssignProcessToJobObject',k.calls)
                if api!='ResumeThread':self.assertNotIn('ResumeThread',k.calls)
                self.assertLessEqual(k.calls.count('CreateProcessW'),1)
                initialized=api not in ['size_query','CreateJobObjectW','SetInformationJobObject','InitializeProcThreadAttributeList']
                self.assertEqual(k.calls.count('DeleteProcThreadAttributeList'),int(initialized))
                if api!='CreateJobObjectW':self.assertIn(100,k.closed)


def run_logged(command,seconds,row):
    directory=runner.GAME/'build'/'windows-job-tests'/('run-'+uuid.uuid4().hex)
    directory.mkdir(parents=True)
    try:runner.run_windows_job(command,seconds,row)
    finally:
        (directory/'win32.json').write_text(json.dumps(row,indent=2))


class Proxy:
    """Forward real APIs while intercepting a specific lifecycle boundary."""
    def __init__(self, real, hook):self.real=real;self.hook=hook;self.cache={}
    def __getattr__(self,name):
        if name not in self.cache:
            real=getattr(self.real,name)
            def invoke(*args):
                real.argtypes=fn.argtypes;real.restype=fn.restype
                return self.hook(name,real,args)
            fn=Function(invoke);self.cache[name]=fn
        return self.cache[name]


@unittest.skipUnless(os.name=='nt','Windows integration only')
class IntegrationTests(unittest.TestCase):
    def test_normal_and_timeout(self):
        for code,seconds,timed in [('pass',5,False),('import time; time.sleep(60)',0.3,True)]:
            with self.subTest(timed=timed):
                row={};run_logged([sys.executable,'-B','-c',code],seconds,row)
                self.assertTrue(row['job_assigned_at_creation'],row)
                self.assertTrue(row['cleanup_succeeded'],row)
                self.assertEqual(row['timeout_detected'],timed)
                self.assertEqual(row['active_processes_after_cleanup'],0)

    def test_nested_jobs_and_unaffected_sibling(self):
        # Outer helper contains this entire integration subprocess and descendants.
        row={}
        run_logged([sys.executable,'-B',__file__,'--nested'],15,row)
        self.assertEqual(row.get('exit_code'),0,row)
        self.assertTrue(row['cleanup_succeeded'],row)

    def test_close_last_job_handle_before_resume(self):
        real=C.WinDLL('kernel32',use_last_error=True);job=[];process=[]
        def hook(name,fn,args):
            if name=='CreateJobObjectW':
                result=fn(*args);job.append(result);return result
            if name=='CreateProcessW':
                result=fn(*args);process.append(args[9]._obj.hProcess);return result
            if name=='ResumeThread':
                close=real.CloseHandle;close.argtypes=[W.HANDLE];close.restype=W.BOOL
                self.assertTrue(close(job[0]))
                wait=real.WaitForSingleObject;wait.argtypes=[W.HANDLE,W.DWORD];wait.restype=W.DWORD
                self.assertEqual(wait(process[0],5000),0)
                C.set_last_error(5);return 0xffffffff
            if name=='CloseHandle' and args[0]==job[0]:return 1 # already closed deliberately
            return fn(*args)
        row={}
        with patch.object(C,'WinDLL',return_value=Proxy(real,hook)):
            run_logged([sys.executable,'-B','-c','raise SystemExit(99)'],5,row)
        self.assertTrue(row['job_assigned_at_creation'],row)
        self.assertEqual(row['status'],'structural_error')

    def test_abrupt_controller_death(self):
        row={}
        run_logged([sys.executable,'-B',__file__,'--death-observer'],20,row)
        self.assertEqual(row.get('exit_code'),0,row)
        self.assertTrue(row['cleanup_succeeded'],row)

    def test_parent_job_close_kills_nested_descendants(self):
        row={}
        run_logged([sys.executable,'-B',__file__,'--death-observer','--active-tree'],20,row)
        self.assertEqual(row.get('exit_code'),0,row)
        self.assertTrue(row['cleanup_succeeded'],row)


def nested():
    sibling=subprocess.Popen([sys.executable,'-B','-c','import time; time.sleep(60)'])
    try:
        # Worker waits for its own descendant to start via a pipe, then exits.
        code="import subprocess,sys; p=subprocess.Popen([sys.executable,'-B','-c',\"import time; print('ready',flush=True); time.sleep(60)\"],stdout=subprocess.PIPE); assert p.stdout.readline().strip()==b'ready'"
        row={};run_logged([sys.executable,'-B','-c',code],5,row)
        assert row.get('exit_code')==0 and row['cleanup_succeeded'] and row['active_processes_after_cleanup']==0,row
        assert sibling.poll() is None
        # Close (rather than terminate) the last inner Job handle under an outer Job.
        IntegrationTests('test_close_last_job_handle_before_resume').test_close_last_job_handle_before_resume()
        assert sibling.poll() is None
    finally:
        sibling.kill();sibling.wait(timeout=5)


def controller(event_name, directory, active=False):
    real=C.WinDLL('kernel32',use_last_error=True)
    real.OpenEventW.argtypes=[W.DWORD,W.BOOL,W.LPCWSTR];real.OpenEventW.restype=W.HANDLE
    real.SetEvent.argtypes=[W.HANDLE];real.SetEvent.restype=W.BOOL
    event=real.OpenEventW(2,False,event_name);assert event
    def hook(name,fn,args):
        result=fn(*args)
        if name=='CreateProcessW' and result:
            Path(directory,'pid.json').write_text(json.dumps(args[9]._obj.dwProcessId))
            assert real.SetEvent(event)
            # Observer kills only this controller while the child is still suspended.
            real.WaitForSingleObject.argtypes=[W.HANDLE,W.DWORD];real.WaitForSingleObject.restype=W.DWORD
            real.WaitForSingleObject(W.HANDLE(-1),15000)
        return result
    if active:
        row={}
        runner.run_windows_job([sys.executable,'-B',__file__,'--tree-worker',event_name,directory],20,row)
        Path(directory,'controller.json').write_text(json.dumps(row))
        return
    with patch.object(C,'WinDLL',return_value=Proxy(real,hook)):
        runner.run_windows_job([sys.executable,'-B','-c','import time; time.sleep(60)'],20,{})


def tree_worker(event_name,directory):
    p=subprocess.Popen([sys.executable,'-B','-c',"import time; print('ready',flush=True); time.sleep(60)"],stdout=subprocess.PIPE)
    assert p.stdout.readline().strip()==b'ready'
    Path(directory,'pid.json').write_text(json.dumps([os.getpid(),p.pid]))
    k=C.WinDLL('kernel32',use_last_error=True)
    k.OpenEventW.argtypes=[W.DWORD,W.BOOL,W.LPCWSTR];k.OpenEventW.restype=W.HANDLE
    k.SetEvent.argtypes=[W.HANDLE];k.SetEvent.restype=W.BOOL
    event=k.OpenEventW(2,False,event_name);assert event
    assert k.SetEvent(event)
    k.WaitForSingleObject.argtypes=[W.HANDLE,W.DWORD];k.WaitForSingleObject.restype=W.DWORD
    k.WaitForSingleObject(W.HANDLE(-1),15000)


def death_observer(active=False):
    k=C.WinDLL('kernel32',use_last_error=True)
    k.CreateEventW.argtypes=[C.c_void_p,W.BOOL,W.BOOL,W.LPCWSTR];k.CreateEventW.restype=W.HANDLE
    k.WaitForSingleObject.argtypes=[W.HANDLE,W.DWORD];k.WaitForSingleObject.restype=W.DWORD
    k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE
    k.CloseHandle.argtypes=[W.HANDLE];k.CloseHandle.restype=W.BOOL
    name='Local\\PALJobTest-'+uuid.uuid4().hex
    event=k.CreateEventW(None,False,False,name);assert event
    directory=runner.GAME/'build'/'windows-job-tests'/('observer-'+uuid.uuid4().hex)
    directory.mkdir(parents=True)
    with contextlib.nullcontext(str(directory)) as directory:
        p=subprocess.Popen([sys.executable,'-B',__file__,'--controller',name,directory]+(['--active-tree'] if active else []))
        children=[]
        try:
            assert k.WaitForSingleObject(event,8000)==0,Path(directory,'error.txt').read_text() if Path(directory,'error.txt').exists() else 'No ready event'
            pids=json.loads(Path(directory,'pid.json').read_text())
            for pid in pids if isinstance(pids,list) else [pids]:
                child=k.OpenProcess(0x100000,False,pid);assert child
                children.append(child)
            p.kill();p.wait(timeout=5)
            for child in children:
                assert k.WaitForSingleObject(child,5000)==0,'Descendant survived controller death'
        finally:
            if p.poll() is None:p.kill();p.wait(timeout=5)
            for child in children:k.CloseHandle(child)
            k.CloseHandle(event)


if __name__=='__main__':
    if '--nested' in sys.argv:nested()
    elif '--tree-worker' in sys.argv:
        try:tree_worker(sys.argv[2],sys.argv[3])
        except BaseException:
            import traceback
            Path(sys.argv[3],'error.txt').write_text(traceback.format_exc())
            raise
    elif '--controller' in sys.argv:controller(sys.argv[2],sys.argv[3],'--active-tree' in sys.argv)
    elif '--death-observer' in sys.argv:death_observer('--active-tree' in sys.argv)
    else:unittest.main()
