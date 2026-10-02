"""Verifying one lifted or structured function against the original.

Kept apart from engine/tools/lift.py, which builds and orchestrates: everything
here decides a verdict, so it is part of the environment fingerprint that
remembered verdicts are keyed by (verdicts.py), and the orchestration is not.
"""

from __future__ import annotations

import random

import derive                       # engine/tools, on the path wherever this is used

from . import layout, liftcheck, testmem

STATE_PARTS = ("registers", "upper register halves", "HI", "LO", "HI1", "LO1",
               "FPU registers", "FPU accumulator", "FPU condition",
               "VU0 vector registers", "VU0 control registers", "VU0 accumulator",
               "shift amount")


def _difference(actual, expected) -> str:
    """Which part of the state differs, naming the register for a register file."""
    for name, a, b in zip(STATE_PARTS, actual, expected):
        if a != b:
            if isinstance(a, tuple):
                index = next(i for i, (x, y) in enumerate(zip(a, b)) if x != y)
                show = lambda v: "0x%X" % v if isinstance(v, int) else repr(v)
                return "%s (%d: lifted %s, original %s)" % (name, index, show(a[index]),
                                                           show(b[index]))
            return name
    return "nothing"


CALLEE_SAVED_REGS = (16, 17, 18, 19, 20, 21, 22, 23, 30)


def abi_view(state):
    """What a caller can rely on after a call, under the calling convention: v0
    and v1, $gp, $sp, $ra, the callee-saved registers whole (upper halves too),
    the float results and the callee-saved float registers."""
    regs, upper, fpr = state[0], state[1], state[6]
    return (tuple(regs[i] for i in (2, 3, 28, 29, 31) + CALLEE_SAVED_REGS),
            tuple(upper[i] for i in CALLEE_SAVED_REGS),
            tuple(fpr[i] for i in (0, 1) + tuple(range(20, 32))))


# How hard to look for inputs the original completes on, when the standard plan
# finds none: fresh argument sets, each tried under every memory profile, with a
# small budget so that a function no input finishes on costs little.
SEARCH_ATTEMPTS = 12
SEARCH_BUDGET = 20_000
CASES = 8


def _cases(func, machine):
    """(arguments, profile, memory seed) for each test case, or None.

    The standard plan - eight argument sets under the first profile the original
    runs with on the first of them - comes first. When it has no profile, inputs
    are searched for instead. Any input the original completes on is as valid a
    test as any other; this only decides which inputs are tried.
    """
    plan = derive.test_plan(func, machine)
    if plan is not None:
        arg_sets, profile = plan
        return [(args, profile, func["addr"] + case) for case, args in enumerate(arg_sets)]
    rng = random.Random(func["addr"] ^ 0x5EA4C4)
    memory = machine.memory
    found = []
    for attempt in range(SEARCH_ATTEMPTS):
        args = testmem.arguments(rng)
        seed = func["addr"] + 1000 + attempt
        # Random arguments reach random addresses; the pages a failed attempt
        # mapped are given back, or the search would use up the process's
        # mapping budget (emu.MAX_PAGES) and fail the functions verified after.
        mark = memory.page_mark()
        ran = False
        for profile in testmem.PROFILES:
            memory.end_journal()
            memory.write(layout.CURRENT.scratch, testmem.build(profile, seed))
            memory.begin_journal()
            try:
                machine.call(func["addr"], args, max_instructions=SEARCH_BUDGET)
                ran = True
            except Exception:
                ran = False
            memory.rollback()
            memory.end_journal()
            if ran:
                found.append((args, profile, seed))
                break
        if not ran:
            memory.unmap_to(mark)
        if len(found) >= CASES:
            break
    return found or None


def verify(func, machine, library, abi=False) -> str:
    """'verified', or why not."""
    mark = machine.memory.page_mark()
    outcome = _verify(func, machine, library, abi)
    if outcome.startswith("untestable"):
        # Nothing was learnt; nothing it mapped should change what comes after.
        machine.memory.unmap_to(mark)
    return outcome


def _verify(func, machine, library, abi) -> str:
    cases = _cases(func, machine)
    if cases is None:
        return "untestable: " + derive.failure_reason()
    memory = machine.memory
    tested = 0
    for args, profile, seed in cases:
        memory.end_journal()
        memory.write(layout.CURRENT.scratch, testmem.build(profile, seed))
        memory.begin_journal()
        try:
            machine.call(func["addr"], args, max_instructions=derive.VERIFY_BUDGET)
        except Exception:
            memory.rollback()
            memory.end_journal()
            continue
        expected = liftcheck.machine_state(machine)
        effects = derive._effects_digest(memory)
        memory.rollback()
        context = liftcheck.entry_context(args)
        why = library.run(func["addr"], context)
        if why is not None:
            memory.rollback()
            memory.end_journal()
            return "lifted run failed: " + why
        actual = liftcheck.context_state(context)
        replayed = derive._effects_digest(memory)
        memory.rollback()
        memory.end_journal()
        if abi:
            if abi_view(actual) != abi_view(expected):
                return "disagrees: " + _difference(abi_view(actual), abi_view(expected))
        elif actual != expected:
            return "disagrees: " + _difference(actual, expected)
        if replayed != effects:
            return "disagrees: memory"
        tested += 1
    return "verified" if tested >= 5 else "untestable: only %d cases ran" % tested
