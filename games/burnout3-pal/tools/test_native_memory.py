"""PAL memory DLL regression: run with Python 3.12 from any directory.

Each execution creates a fresh build/cpp-memory/run-<UUID> directory.
Requires the locally extracted ELF, clean database and MSVC x64.
Only synthetic test values are embedded; function bodies come from the ELF.
"""
import sys,json,inspect,uuid,traceback,hashlib,struct
from pathlib import Path
SCRIPT_DIR=Path(__file__).resolve().parent
GAME=SCRIPT_DIR.parent
ROOT=GAME.parent.parent
OUTPUT_ROOT=GAME/"build"/"cpp-memory"
sys.path.insert(0,str(ROOT/'engine/tools'))
import derive
from ps2decomp import testmem,testplan,cppcheck
from ps2decomp.game import load
from ps2decomp.emu import Machine
from ps2decomp.r5900 import DISASM
SPEC={0x121568:(12,2,False,[0,0x1f,0x20,0x21,0x40,0x8000,0xffff,0xffdf]),0x134330:(64,4,True,[0,1,2,3,4,0x7fffffff,0x80000000,0xffffffff]),0x1a87c0:(44,1,False,[0,1,3,4,5,8,0x80,0xff])}
def require(ok,msg):
 if not ok: raise RuntimeError(msg)
def run(report):
 require(sys.version_info[:2]==(3,12),'Python 3.12 required')
 g=load(str(ROOT/'games/burnout3-pal'));db=g.load_database();require(len(db['functions'])==9384,'Database count')
 fs=[f for f in db['functions'] if f['addr'] in SPEC];require(len(fs)==3,'Missing functions')
 require(all(f['has_return'] and not f['uses_vu'] and not f['calls'] and not f.get('chunks') and not any(f['addr']<h and f['end']>l for l,h in g.vu_regions) for f in fs),'Function properties')
 e=g.load_elf();s=e.exec_sections[0];image=e.section_data(s);m=Machine.from_elf(e)
 lo=int(g.layout['scratch'],0);hi=lo+int(g.layout['scratch_size'],0);require((lo,hi)==(0x1ed0000,0x1ef0000),'Scratch')
 inputs=[Path(g.database_path),Path(g.elf_path),ROOT/'games/burnout3-pal/game.json'];hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs};report['inputs_sha256']=hashes
 report['api_signatures']={x.__name__:str(inspect.signature(x)) for x in (derive.collect,derive._attempt,testplan.test_plan,testplan.run_cases,testplan.prepare_test_memory,testmem.build)}
 blobs={};vectors={}
 for a,(offset,width,signed,values) in SPEC.items():
  b=bytearray(hi-lo);v=[]
  for i,value in enumerate(values):
   base=lo+0x100+i*0x100;require(base%16==0 and (base+offset)%width==0 and base+offset+width<=hi,'Alignment/bounds')
   b[base+offset-lo:base+offset-lo+width]=value.to_bytes(width,'little');v.append((base,)+(0,)*15)
  blobs[a]=bytes(b);vectors[a]=v
 original_build=testmem.build;profile='pal-memory-explicit'
 def build(kind,seed):
  if kind==profile:
   found=[a for a in SPEC if a<=seed<a+8];require(len(found)==1,'Unknown seed');return blobs[found[0]]
  return original_build(kind,seed)
 testmem.build=build
 ps=inspect.signature(derive.test_plan)
 def plan(*args,**kwargs):
  bound=ps.bind(*args,**kwargs);return vectors[bound.arguments['func']['addr']],profile
 derive.test_plan=plan
 # Preparation may enable fault mapping; disable it immediately after preparation.
 original_prepare=derive.prepare_test_memory
 def prepare(*args,**kwargs):
  result=original_prepare(*args,**kwargs);m.map_faults=False;m.memory.page_source=None;return result
 derive.prepare_test_memory=prepare
 original_attempt=derive._attempt;sig=inspect.signature(original_attempt)
 def attempt(*args,**kwargs):
  bound=sig.bind(*args,**kwargs);r=original_attempt(*args,**kwargs)
  require(r is not None,'Rejected: %r'%bound.arguments['rejected']);require(r[2]==8,'Incomplete cases');return r
 derive._attempt=attempt;derive.CALLERS_KNOWN=derive.game_config.callers_known(g)
 verified,rejected=derive.collect(dict(db,functions=fs),image,s.addr,m,max_passes=1)
 report['rejections']=rejected;require(len(verified)==3,'Incomplete derivation')
 source_dir=HERE/'generated';emitted=derive.emit(verified,str(source_dir),db['meta']['source'],catalog=None);require(len(emitted)==3,'Emitter dropped function')
 source=source_dir/'accessors.cpp';text=source.read_text();signatures=cppcheck.functions_in(text)
 require(len(signatures)==3 and {x[0] for x in signatures}==set(SPEC),'Signatures')
 require(all(x[2]=='s32' and x[3]==[0] and x[4]=='u64 arg0' for x in signatures),'Signature types');require(not cppcheck.externs_in(text),'Externs')
 dll,built=cppcheck.build(str(source),cppcheck.RUNTIME_SOURCES,str(Path(cppcheck.RUNTIME)/'include'),str(HERE/'compile'))
 raw=Path(dll).read_bytes();pe=struct.unpack_from('<I',raw,60)[0];require(struct.unpack_from('<H',raw,pe+4)[0]==0x8664,'Not x64');require('/O2' in (HERE/'compile/build.bat').read_text(),'Missing /O2')
 compiled=cppcheck.CompiledFunctions(dll,m);report['dll']=dll;report['compiler']=cppcheck.find_vcvars();report['optimization']='/O2';report['functions']=[]
 m.map_faults=False;m.memory.page_source=None
 for f,d,tested,strong in verified:
  a=f['addr'];offset,width,signed,values=SPEC[a];evaluate=compiled.evaluator(a)
  outcome,count=testplan.run_cases(f,m,vectors[a],profile,evaluate)
  require(outcome=='ok' and count==8,'DLL run_cases failed: %08X %s %d'%(a,outcome,count))
  rows=[];slots=set();old_read=m.memory.read_int;old_step=m.step;reads=[]
  def read(addr,size,is_signed):
   value=old_read(addr,size,is_signed)
   if lo<=addr<hi:reads.append((addr,size,bool(is_signed),value))
   return value
  def step():
   pc=m.pc;ins=DISASM.decode(old_read(pc,4,False),pc);old_step()
   if ins.has_delay_slot:slots.add(pc+4)
  m.memory.read_int=read;m.step=step
  try:
   for i,vector in enumerate(vectors[a]):
    load_addr=vector[0]+offset;field=values[i].to_bytes(width,'little');expected_read=(load_addr,width,signed,int.from_bytes(field,'little',signed=signed));results={};traces={}
    for mode in ('mips','symbolic','native'):
     m.memory.end_journal();m.memory.write(lo,blobs[a]);require(m.memory.read(load_addr,width)==field,'Wrong bytes');reads.clear();m.memory.begin_journal()
     if mode=='mips':value=m.call(a,vector,max_instructions=testplan.VERIFY_BUDGET)
     elif mode=='symbolic':
      value=d.evaluate(vector,m.memory.read_int);require(not d.evaluate_stores(vector,m.memory.read_int),'Symbolic stores')
     else:
      value,writes=evaluate(vector);require(not writes,'Native stores')
     require(not m.memory._journal,'Function wrote memory');require(reads and all(r==expected_read for r in reads),'Incorrect %s reads: %r expected %r'%(mode,reads,expected_read))
     if mode in ('mips','native'):require(len(reads)==1,'Unexpected read count')
     results[mode]=value;traces[mode]=list(reads);m.memory.rollback();m.memory.end_journal();require(m.memory.read(load_addr,width)==field,'Bytes changed')
    require(results['mips']==results['symbolic']==results['native'],'Differential mismatch: %r'%results)
    rows.append({'case':i+1,'field_hex':field.hex(),'read_address':'0x%08X'%load_addr,'width':width,'signed':signed,'results':results,'reads':traces,'memory_writes':0})
   require(f['end']-4 in slots,'Missing delay slot')
  finally:m.memory.read_int=old_read;m.step=old_step
  report['functions'].append({'address':'0x%08X'%a,'expression':derive.render_body(d),'cases':rows,'run_cases':{'outcome':outcome,'tested':count},'delay_slots':['0x%08X'%pc for pc in sorted(slots)]})
  print('PASS %08X: 8 cases; exact read address/width/sign; no writes'%a,flush=True)
 require(not m.map_faults and m.memory.page_source is None,'Automatic fault mapping enabled')
 for p,digest in hashes.items():require(hashlib.sha256(Path(p).read_bytes()).hexdigest()==digest,'Input changed')
 report.update(status='passed',total_cases=24,discrepancies=0,automatic_fault_mapping=False)
if __name__ == '__main__':
 require(sys.version_info[:2]==(3,12),'Python 3.12 required')
 OUTPUT_ROOT.mkdir(parents=True,exist_ok=True)
 HERE=OUTPUT_ROOT/('run-'+uuid.uuid4().hex)
 HERE.mkdir(exist_ok=False)
 report={'status':'running','artifacts':str(HERE)}
 try:run(report)
 except Exception:
  report['status']='failed';report['error']=traceback.format_exc();raise
 finally:
  with (HERE/'results.json').open('x',encoding='utf-8') as h:json.dump(report,h,indent=2)
  print('Report:',HERE/'results.json',flush=True)
