"""Compile and exercise the real PAL override with synthetic memory callbacks.
No ELF is loaded. Callback faults test propagation, not hardware emulation.
"""
import json
from pathlib import Path
import subprocess
import sys
import unittest
import uuid
import relocate_native as r

HARNESS = r'''
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include "ps2host/host.h"
namespace generated {
u8* ram_base=nullptr; bool ram_watched=false; u8* scratchpad_base=nullptr;
static u32 address; static u64 value; static u32 size; static int writes;
void PortWrite(u32 a,u64 v,u32 n) {
 if(a==0xdead0000u) throw std::runtime_error("synthetic memory fault");
 address=a;value=v;size=n;++writes;
}
}
namespace ps2host {
static GuestFunction target=nullptr;
static RpcServer rpc;
static u32 rpc_id=0;
static int registrations=0;
void RegisterRpcServer(u32 id,RpcServer f) {rpc_id=id;rpc=f;++registrations;}
void Override(u32 a,GuestFunction f,const char*) {assert(a==0x001156C0u);target=f;}
}
namespace ps2host { namespace iop {
static int rwa_registrations=0;
static std::vector<std::string> module_names;
void RunModule(const std::string& name) {module_names.push_back(name);++rwa_registrations;}
}}
void RegisterGameOverrides();
int main() {
 RegisterGameOverrides();assert(ps2host::target);
 assert(ps2host::iop::rwa_registrations==2);
 assert((ps2host::iop::module_names==std::vector<std::string>{"RWA.IRX","GTFSCDVD.IRX"}));
 const u32 addresses[]={0x1000,0x80001000,0xB0001000,0xB0001010,0xB0001020};
 for(u32 a:addresses) {
  lifted::Context c;std::memset(&c,0x5a,sizeof(c));c.r[4]=0xffffffff00000000ull|a;c.r[5]=0x12345678abcdef01ull;
  auto before=c;int n=generated::writes;ps2host::target(c);
  assert(generated::address==a && generated::value==0xabcdef01u && generated::size==4);
  assert(generated::writes==n+1 && std::memcmp(&c,&before,sizeof(c))==0);
 }
 lifted::Context c{};c.r[4]=0xdead0000;c.r[5]=42;auto before=c;int n=generated::writes;
 bool fault=false;try{ps2host::target(c);}catch(const std::runtime_error&){fault=true;}
 assert(fault && n==generated::writes && std::memcmp(&c,&before,sizeof(c))==0);
 std::puts("KernelStore: 6 synthetic cases passed");
}
'''

def worker(directory,harness=HARNESS):
 directory=r.inside(directory);h=directory/'harness.cpp';h.write_text(harness)
 vc=r.port.cppcheck.find_vcvars()
 command=['cl.exe','/nologo','/EHsc','/std:c++17','/MT','/Od',
          '/I'+str(r.ROOT/'engine/host/include'),'/I'+str(r.ROOT/'engine/runtime/include'),
          str(h),str(r.GAME/'port/overrides.cpp'),'/Fe:'+str(directory/'test.exe')]
 script=directory/'test.bat'
 script.write_text('@echo off\ncall "'+vc+'" >nul\nif errorlevel 1 exit /b 1\n'+subprocess.list2cmdline(command)+'\nif errorlevel 1 exit /b 1\n"'+str(directory/'test.exe')+'"\n')
 with (directory/'stdout.log').open('x') as out,(directory/'stderr.log').open('x') as err:
  return subprocess.run(['cmd','/d','/c',str(script)],cwd=directory,stdout=out,stderr=err).returncode

class KernelStoreTests(unittest.TestCase):
 def test_actual_override_contract(self):
  directory=r.GAME/'build/kernel-store-tests'/('run-'+uuid.uuid4().hex[:12]);directory.mkdir(parents=True)
  row={};r.runner.run_windows_job([sys.executable,'-B',str(Path(__file__).resolve()),'--worker',str(directory)],90,row)
  r.atomic_json(directory/'containment.json',row)
  self.assertEqual(row.get('exit_code'),0,str(directory))
  self.assertTrue(row.get('cleanup_succeeded'));self.assertFalse(row.get('timeout_detected'))
  self.assertEqual(row.get('active_processes_after_cleanup'),0)
  self.assertIn('6 synthetic cases passed',(directory/'stdout.log').read_text())

if __name__=='__main__':
 if len(sys.argv)==3 and sys.argv[1]=='--worker':raise SystemExit(worker(Path(sys.argv[2])))
 unittest.main()
