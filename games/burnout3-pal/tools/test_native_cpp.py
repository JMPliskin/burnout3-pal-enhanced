"""Isolated PAL C++ smoke test; run with Python 3.12 from any directory.

Every invocation creates a new build/cpp-smoke/run-<UUID> directory.
Generated artifacts remain under the ignored build directory.
"""
import sys, json, inspect, hashlib, struct, uuid, traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
ROOT = GAME.parent.parent
SMOKE_ROOT = GAME / "build" / "cpp-smoke"
sys.path.insert(0, str(ROOT / 'engine/tools'))
import derive
from ps2decomp import cppcheck, testplan
from ps2decomp.game import load
from ps2decomp.emu import Machine
from ps2decomp.r5900 import DISASM

MASK = (1 << 64) - 1
MIN, MAX = -(1 << 31), (1 << 31) - 1
CASES = {
    0x13B740: [(v, 0) for v in (0, 1, -1, MIN, MAX, MAX-7, MIN+1, 100)],
    0x19B6B0: [(v, 0) for v in (0, 1, -1, MIN, MAX, MAX-1, MIN+1, 100)],
    0x13B6B0: [(0,0),(0,1),(100,2),(-1,1),(MIN,0),(MIN,1),(MAX,0),(MAX,1)],
    0x3E97F0: [(-1,0),(MIN,1),(0,0),(0,1),(100,0),(100,1),(MAX,0),(MAX,1)],
}

def require(ok, message):
    if not ok:
        raise RuntimeError(message)




def run(report, work):
    game = load(str(ROOT / 'games/burnout3-pal'))
    db = game.load_database()
    require(len(db['functions']) == 9384, 'Unexpected database count')
    selected = [f for f in db['functions'] if f['addr'] in CASES]
    require(len(selected) == 4, 'Missing functions')
    require(all(f['has_return'] and not f['uses_vu'] and not f['calls'] and not f.get('chunks') for f in selected), 'Unexpected function properties')
    elf = game.load_elf(); section = elf.exec_sections[0]; image = elf.section_data(section)
    machine = Machine.from_elf(elf)
    report['inputs'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (Path(game.database_path), Path(game.elf_path), ROOT/'games/burnout3-pal/game.json')}
    report['python'] = sys.version
    report['compiler_environment'] = cppcheck.find_vcvars()
    plan_signature = inspect.signature(derive.test_plan)
    attempt_signature = inspect.signature(derive._attempt)
    report['api_signatures'] = {'test_plan': str(plan_signature), '_attempt': str(attempt_signature)}
    require(set(plan_signature.parameters) == {'func','machine'}, 'Unexpected test_plan signature')
    def plan(*args, **kwargs):
        bound = plan_signature.bind(*args, **kwargs)
        return [((a & MASK), (b & MASK)) + (0,)*14 for a,b in CASES[bound.arguments['func']['addr']]], 'mixed'
    derive.test_plan = plan
    original_attempt = derive._attempt
    def attempt(*args, **kwargs):
        bound = attempt_signature.bind(*args, **kwargs)
        result = original_attempt(*args, **kwargs)
        require(result is not None, 'Derivation rejected 0x%08X: %r' % (bound.arguments['func']['addr'], bound.arguments['rejected']))
        require(result[2] == 8, 'Incomplete derivation cases')
        return result
    derive._attempt = attempt
    derive.CALLERS_KNOWN = derive.game_config.callers_known(game)
    verified, rejected = derive.collect(dict(db, functions=selected), image, section.addr, machine, max_passes=1)
    report['rejections'] = rejected
    require(len(verified) == 4, 'Not all functions derived')
    generated = work / 'generated'
    emitted = derive.emit(verified, str(generated), db['meta']['source'], catalog=None)
    require(len(emitted) == 4, 'Emitter dropped functions')
    source = generated / 'accessors.cpp'
    text = source.read_text()
    signatures = cppcheck.functions_in(text)
    require(len(signatures) == 4 and {f[0] for f in signatures} == set(CASES), 'Unexpected emitted addresses')
    for address, name, returns, indices, params in signatures:
        count = 2 if address in (0x13b6b0,0x3e97f0) else 1
        require(returns == 's32' and indices == list(range(count)) and params == ', '.join('u64 arg%d'%i for i in range(count)), 'Unexpected emitted signature')
    require(not cppcheck.externs_in(text), 'Unexpected external functions')
    report['signatures'] = signatures
    dll, built = cppcheck.build(str(source), cppcheck.RUNTIME_SOURCES, str(Path(cppcheck.RUNTIME)/'include'), str(work/'compile'))
    require(len(built) == 4, 'Unexpected compiled function count')
    compiled = cppcheck.CompiledFunctions(dll, machine)
    report['dll'] = dll
    report['functions'] = []
    for f, derivation, tested, strong in verified:
        address = f['addr']; args, profile = plan(func=f, machine=machine)
        evaluator = compiled.evaluator(address)
        outcome, count = testplan.run_cases(f, machine, args, profile, evaluator)
        require(outcome == 'ok' and count == 8, 'Compiled equivalence failed: 0x%08X %s %d'%(address,outcome,count))
        coverage = {}; slots = set(); rows = []
        original_step = machine.step
        def step():
            pc = machine.pc
            ins = DISASM.decode(machine.memory.read_int(pc,4,False),pc)
            original_step()
            if ins.has_delay_slot: slots.add(pc+4)
            if ins.mnemonic in ('beq','bne','bltz'):
                coverage.setdefault(pc,set()).add(machine.pc == ins.target)
        machine.step = step
        try:
            for vector, (a,b) in zip(args, CASES[address]):
                machine.memory.begin_journal()
                actual = machine.call(address, vector, max_instructions=testplan.VERIFY_BUDGET)
                require(not machine.memory._journal, 'Original wrote memory')
                machine.memory.rollback(); machine.memory.end_journal()
                machine.memory.begin_journal()
                native, writes = evaluator(vector)
                require(not writes and not machine.memory._journal, 'Native wrote memory')
                machine.memory.rollback(); machine.memory.end_journal()
                symbolic = derivation.evaluate(vector, machine.memory.read_int)
                require(actual == native == symbolic,
                        'Differential mismatch at 0x%08X (a0=%d, a1=%d): MIPS=%r, native=%r, symbolic=%r'
                        % (address, a, b, actual, native, symbolic))
                rows.append({'a0':a,'a1':b,'mips':actual,'native':native,'symbolic':symbolic,'memory_writes':0})
        finally:
            machine.step = original_step
        require(all(outcomes == {True,False} for outcomes in coverage.values()), 'Incomplete branch coverage')
        report['functions'].append({'address':'0x%08X'%address,'expression':derive.render_body(derivation),'cases':rows,'run_cases':{'outcome':outcome,'tested':count},'branches':{'0x%08X'%pc:sorted(values) for pc,values in coverage.items()},'delay_slots':['0x%08X'%pc for pc in sorted(slots)]})
        print('PASS 0x%08X: 8 cases; no memory writes; all branches covered'%address, flush=True)
    for relative, digest in report['inputs'].items():
        require(hashlib.sha256((ROOT/relative).read_bytes()).hexdigest()==digest, 'Input changed')
    report['status'] = 'passed'
    report['total_cases'] = 32
    report['discrepancies'] = 0

if __name__ == '__main__':
    require(sys.version_info[:2] == (3,12), 'Python 3.12 required')
    SMOKE_ROOT.mkdir(parents=True, exist_ok=True)
    work = SMOKE_ROOT / ('run-' + uuid.uuid4().hex)
    work.mkdir(exist_ok=False)
    report = {'status':'running','artifacts':str(work)}
    try:
        run(report,work)
    except Exception:
        report['status']='failed'
        report['error']=traceback.format_exc()
        raise
    finally:
        with (work/'results.json').open('x',encoding='utf-8') as handle:
            json.dump(report,handle,indent=2)
        print('Report:',work/'results.json',flush=True)