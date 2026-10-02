#!/usr/bin/env python3
"""Lift every function to C++, verify each against the original, keep what agrees.

    python engine/tools/lift.py                  # lift, build, verify, report
    python engine/tools/lift.py --emit           # also write the verified ones out
    python engine/tools/lift.py --limit 500      # the first 500 functions only

Lifting (engine/ps2decomp/lift.py) is the mechanical tier under derivation:
every instruction becomes a statement over an explicit register context. Each
lifted function is compiled and run on the same test cases a derivation would
get, against the original machine code, and counts only if the *whole* register
file and every byte written agree. The verified ones are written to the game's
`src/lifted/`. Lifted code is complete rather than readable, so it is reported
apart from the derived functions and never counted as them.
"""

from __future__ import annotations

import argparse
import collections
import os
import random
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.dirname(HERE)]

import derive
from ps2decomp import cppcheck, layout, lift, liftcheck, testmem
from ps2decomp import game as game_config
from ps2decomp.emu import Machine

CHUNK = 300
STATE_PARTS = ("registers", "upper register halves", "HI", "LO", "HI1", "LO1",
               "FPU registers", "FPU accumulator", "FPU condition",
               "VU0 vector registers", "VU0 control registers", "VU0 accumulator",
               "shift amount")


def lift_all(functions, image, base):
    """{address: C++}, plus a Counter of why the rest were refused."""
    lifted, refused = {}, collections.Counter()
    for func in functions:
        try:
            lifted[func["addr"]] = lift.lift_function(image, base, func["addr"], func["end"])
        except lift.Refused as exc:
            refused[re.sub(r" at 0x[0-9A-F]+", "", str(exc))] += 1
    return lifted, refused


_CALL = re.compile(r"\b(Call|TailCall)\(c, 0x([0-9A-F]{8})u")


def _annotated(text: str, names: dict) -> str:
    """Calls to a named function say its name."""
    def name(match):
        known = names.get(int(match.group(2), 16))
        return match.group(0) + (" /* %s */" % known[0] if known else "")
    return _CALL.sub(name, text)


def write_chunks(lifted: dict, directory: str, prefix: str, unverified=None,
                 names=None) -> list:
    """Write functions in files of CHUNK. A function in `unverified` is preceded by
    why it could not be checked; a named one by its name."""
    unverified, names = unverified or {}, names or {}
    os.makedirs(directory, exist_ok=True)
    for stale in os.listdir(directory):
        if stale.startswith(prefix) and stale.endswith(".cpp"):
            os.remove(os.path.join(directory, stale))
    addresses = sorted(lifted)
    paths = []
    for index in range(0, len(addresses), CHUNK):
        path = os.path.join(directory, "%s_%03d.cpp" % (prefix, index // CHUNK))
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("/* Lifted from the original machine code by engine/tools/lift.py:\n"
                         " * one statement per instruction. Every function was checked\n"
                         " * against the original - registers and memory - unless marked\n"
                         " * NOT VERIFIED. Generated - do not edit. */\n\n")
            handle.write('#include "ps2rt/lifted.h"\n\n')
            handle.write("using namespace generated;\nusing namespace lifted;\n\n")
            for address in addresses[index:index + CHUNK]:
                if address in names:
                    handle.write("/* %s (%s) */\n" % names[address])
                if address in unverified:
                    handle.write("/* NOT VERIFIED: %s */\n" % unverified[address])
                handle.write(_annotated(lifted[address], names))
                handle.write("\n")
        paths.append(path)
    return paths


def build(lifted: dict, work: str):
    """Compile, dropping any function the compiler rejects; returns (dll, lifted)."""
    for _attempt in range(8):
        paths = write_chunks(lifted, work, "lift")
        try:
            return liftcheck.build(paths, work), lifted
        except cppcheck.CompileError as exc:
            broken = set()
            for line in str(exc).splitlines():
                match = re.match(r"(.+?)\((\d+)\)", line)
                # Only errors in the lifted files name a function; others - the
                # compiler's own headers, its internals - are not the code's fault.
                if match and os.path.isfile(match.group(1)) and \
                        os.path.basename(match.group(1)).startswith("lift"):
                    broken |= _functions_at(match.group(1), int(match.group(2)))
            if not broken:
                raise
            print("  dropping %d function(s) that do not compile" % len(broken))
            lifted = {a: t for a, t in lifted.items() if a not in broken}
    raise RuntimeError("could not build the lifted library")


def _functions_at(path: str, line_number: int) -> set:
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().splitlines()[:line_number]
    for line in reversed(lines):
        match = re.match(r"void F_([0-9A-F]{8})\(", line)
        if match:
            return {int(match.group(1), 16)}
    return set()


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


def structure_verified(chosen, functions, image, base, game, seeds, jobs, strings=None) -> dict:
    """Swap in the structured form of every function whose structured form verifies too.

    Structuring (structure.py) is a second translation of the same code, so it is
    checked like the first: built into its own library and run on the same cases
    against the original. Where it fails or cannot be structured, the plain lifted
    form stays.
    """
    from ps2decomp import structure

    stage = time.time()
    result = dict(chosen)
    remaining = list(chosen)
    # Most readable first - temporaries folded, checked at the calling-convention
    # level - then structured with the callee-saved saves elided, then with them
    # kept; what no form passes keeps the lifted form.
    forms = (("folded", dict(elide=True, readable=True), True),
             ("with saves elided", dict(elide=True), False),
             ("with saves kept", dict(elide=False), False))
    for label, options, abi in forms:
        structured = {}
        for address in remaining:
            func = functions[address]
            try:
                structured[address] = structure.structure_function(
                    image, base, func["addr"], func["end"], strings=strings, **options)
            except lift.Refused:
                pass
        if not structured:
            break
        work = game.path(os.path.join(game.build, "structure_" + label.split()[-1]))
        dll, structured = build(structured, work)
        outcomes = verify_parallel(sorted(structured), game, seeds, dll, jobs, abi)
        passed = [a for a, outcome in outcomes.items() if outcome == "verified"]
        for address in passed:
            result[address] = structured[address]
        remaining = [a for a in structured if a not in passed]
        print("structured %s: %d passed of %d tried (%.0fs)"
              % (label, len(passed), len(structured), time.time() - stage))
    done = sum(1 for a in chosen if result[a] is not chosen[a])
    print("structured: %d of %d verified functions" % (done, len(chosen)))
    return result


def update_status(path: str, verified, outcomes, names=None, unverified=None) -> None:
    """Rebuild the `lifted` rows; any other row - derived, hand-written - stays.

    A function verified both ways keeps its derived row: readable source that is
    verified is the better tier, and the lifted copy is only the fallback. A
    lifted row takes its name from the game's names.csv, where every name keeps
    its evidence, so rebuilding the rows never loses one.
    """
    import csv

    names = names or {}
    rows = {}
    if os.path.exists(path):
        with open(path, newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                if row["source"] != "src/lifted/":
                    rows[int(row["address"], 16)] = row
    added = 0
    for address in verified:
        existing = rows.get(address)
        if existing and existing["status"] in ("verified", "reconstructed"):
            continue
        rows[address] = {
            "address": "0x%08X" % address,
            "name": (names[address][0] if address in names else
                     existing["name"] if existing else "lifted::F_%08X" % address),
            "status": "lifted",
            "source": "src/lifted/",
            "notes": "lifted instruction by instruction; registers and memory "
                     "differential-tested against the original",
        }
        added += 1
    # Lifted but not checked: source exists and is believed right, which is what
    # `reconstructed` means - never `lifted`, which says it was verified.
    for address, reason in sorted((unverified or {}).items()):
        existing = rows.get(address)
        if existing and existing["status"] in ("verified", "reconstructed"):
            continue
        rows[address] = {
            "address": "0x%08X" % address,
            "name": (names[address][0] if address in names else
                     existing["name"] if existing else "lifted::F_%08X" % address),
            "status": "reconstructed",
            "source": "src/lifted/",
            "notes": "lifted; not verified: %s" % reason,
        }
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["address", "name", "status", "source", "notes"])
        writer.writeheader()
        writer.writerows(rows[key] for key in sorted(rows))
    print("updated %s (%d lifted rows)" % (path, added))


# Each verifying process: its own interpreter, test memory and copy of the library.
_WORKER = None


def _start_worker(game_directory, seeds, dll):
    global _WORKER
    game = game_config.load(game_directory)
    database = game.load_database()
    elf = game.load_elf()
    section = elf.exec_sections[0]
    machine = Machine.from_elf(elf)
    derive.prepare_test_memory(machine, database, elf.section_data(section), section.addr,
                               [], seeds=seeds)
    library = liftcheck.LiftedFunctions(dll, machine, derive.VERIFY_BUDGET)
    _WORKER = (machine, library, {f["addr"]: f for f in database["functions"]})


def _verify_share(task):
    """One process verifies one share and exits: its mapped pages never leak into
    another share, so the result does not depend on scheduling."""
    game_directory, seeds, dll, addresses, abi = task
    _start_worker(game_directory, seeds, dll)
    machine, library, functions = _WORKER
    return [(address, verify(functions[address], machine, library, abi))
            for address in addresses]


def verify_parallel(addresses, game, seeds, dll, jobs, abi=False) -> dict:
    from concurrent.futures import ProcessPoolExecutor

    shares = [addresses[k::jobs] for k in range(jobs)]
    tasks = [(game.directory, seeds, dll, share, abi) for share in shares if share]
    outcomes = {}
    with ProcessPoolExecutor(max_workers=len(tasks), max_tasks_per_child=1) as pool:
        for results in pool.map(_verify_share, tasks):
            outcomes.update(results)
    return outcomes


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    game_config.add_argument(parser)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--only", type=lambda text: int(text, 16), action="append",
                        help="lift just this function (hex address); repeatable")
    parser.add_argument("--emit", action="store_true",
                        help="write the verified functions to the game's src/lifted/")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2),
                        help="processes verifying in parallel (default: all but two cores)")
    parser.add_argument("--structure", action="store_true",
                        help="also emit verified functions in structured form "
                             "(loops, ifs, named locals) where that verifies too")
    parser.add_argument("--csv", action="store_true",
                        help="mark the verified functions `lifted` in the status table")
    args = parser.parse_args(argv)

    game = game_config.from_args(args)
    database = game.load_database()
    elf = game.load_elf()
    section = elf.exec_sections[0]
    image = elf.section_data(section)
    machine = Machine.from_elf(elf)
    functions = [f for f in database["functions"] if f["has_return"] and f["end"] > f["addr"]]
    if args.only:
        functions = [f for f in functions if f["addr"] in set(args.only)]
    if args.limit:
        functions = functions[:args.limit]
    started = time.time()
    seeds = derive.prepare_test_memory(machine, database, image, section.addr, functions,
                                       verbose=True)

    lifted, refused = lift_all(functions, image, section.addr)
    print("lifted %d of %d functions; refused %d" % (len(lifted), len(functions),
                                                     sum(refused.values())))
    for reason, count in refused.most_common(10):
        print("  %5d  %s" % (count, reason))

    stage = time.time()
    work = game.path(os.path.join(game.build, "lift"))
    dll, lifted = build(lifted, work)
    print("  compiling took %.0fs" % (time.time() - stage))

    stage = time.time()
    by_address = {f["addr"]: f for f in functions}
    if args.jobs > 1 and len(lifted) > 50:
        outcomes = verify_parallel(sorted(lifted), game, seeds, dll, args.jobs)
    else:
        library = liftcheck.LiftedFunctions(dll, machine, derive.VERIFY_BUDGET)
        outcomes = {address: verify(by_address[address], machine, library)
                    for address in sorted(lifted)}
    print("  verifying took %.0fs (%d processes)" % (time.time() - stage, max(1, args.jobs)))
    import json
    with open(game.path(os.path.join(game.build, "lift_outcomes.json")), "w",
              encoding="utf-8") as handle:
        json.dump({"0x%08X" % a: outcome for a, outcome in sorted(outcomes.items())}, handle,
                  indent=0)

    verified = sorted(a for a, outcome in outcomes.items() if outcome == "verified")
    tally = collections.Counter(re.sub(r"0x[0-9A-Fa-f]+|\b\d+\b", "N", outcome)
                                for outcome in outcomes.values() if outcome != "verified")
    code_bytes = sum(f["size"] for f in database["functions"])
    verified_bytes = sum(by_address[a]["size"] for a in verified)
    print("lifted and verified: %d functions, %s bytes (%.2f%% of code)"
          % (len(verified), format(verified_bytes, ","), 100.0 * verified_bytes / code_bytes))
    for reason, count in tally.most_common(12):
        print("  %5d  %s" % (count, reason))
    disagreeing = [a for a, outcome in outcomes.items() if outcome.startswith("disagrees")]
    if disagreeing:
        print("  disagreeing: %s" % ", ".join("0x%08X" % a for a in disagreeing[:20]))
    chosen = {a: lifted[a] for a in verified}
    if args.structure:
        strings = {entry["addr"]: entry["text"] for entry in database["strings"]}
        chosen = structure_verified(chosen, by_address, image, section.addr, game, seeds,
                                    args.jobs, strings)
    # Everything else that lifted is emitted too, marked: a complete translation,
    # with each function saying whether it was checked.
    unverified = {a: outcomes[a] for a in lifted if a not in chosen}
    for address in unverified:
        chosen[address] = lifted[address]
        if args.structure:
            from ps2decomp import structure
            try:
                chosen[address] = structure.structure_function(
                    image, section.addr, address, by_address[address]["end"], strings=strings)
            except lift.Refused:
                pass
    from ps2decomp import naming
    names = naming.load(game.path(os.path.join(os.path.dirname(game.status), "names.csv")))
    if args.emit:
        out = game.path(os.path.join(os.path.dirname(game.generated), "lifted"))
        paths = write_chunks(chosen, out, "lifted", unverified, names)
        print("wrote %d files to %s (%d functions, %d not verified)"
              % (len(paths), out, len(chosen), len(unverified)))
    if args.csv:
        update_status(game.path(game.status), verified, outcomes, names, unverified)
    print("  elapsed %.0fs" % (time.time() - started))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
