#!/usr/bin/env python3
"""Derive small accessor functions symbolically, verify them, and emit C++.

Many of the discovered functions are tiny branch-free accessors: load a pointer
from a structure, index it, return a field.  Reading each by hand works but does
not scale, and the result is mechanical enough to derive instead.

The pipeline here is:

1. symbolically execute the function to get its return expression;
2. differential-test that expression against the original machine code running in
   the interpreter, over several argument sets;
3. emit C++ only for the ones that passed.

Step 2 is the point.  A derivation nobody checked is a guess with good
presentation, so anything that fails or cannot be executed is dropped rather than
emitted with a caveat.

    python tools/derive.py build/slus_210_50.json
    python tools/derive.py build/slus_210_50.json --emit src/generated
"""

from __future__ import annotations

import argparse
import json
import os
import random
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from b3lib.elf import ElfFile
from b3lib.emu import EmulationError, Machine
from b3lib.paths import PathDerivation, derive_paths
from b3lib.symexec import (
    Arg,
    Binary,
    Compare,
    Const,
    Derivation,
    Load,
    Predicate,
    Unsupported,
    arguments_used,
    load_renderer,
    simplify,
)

from b3lib.emu import DEFAULT_STACK_SIZE, DEFAULT_STACK_TOP

MAX_INSTRUCTIONS = 96
# The callee's own frame. Writes here are not observable effects, so they are
# excluded when comparing what the original and the derivation did to memory.
STACK_LOW = DEFAULT_STACK_TOP - DEFAULT_STACK_SIZE
STACK_HIGH = DEFAULT_STACK_TOP + 0x10
SCRATCH = 0x01800000
SCRATCH_SIZE = 0x20000
ARG_SETS = 8

# Loads become calls on these helpers rather than raw pointer casts, so the
# emitted source stays readable and a port can define them over its own memory
# model. Recovering real struct types is later work.
READERS = {
    (1, True): "ReadS8", (1, False): "ReadU8",
    (2, True): "ReadS16", (2, False): "ReadU16",
    (4, True): "ReadS32", (4, False): "ReadU32",
    (8, False): "ReadU64",
}

WRITERS = {1: "Write8", 2: "Write16", 4: "Write32", 8: "Write64"}


# Delegated to symexec rather than reimplemented here. The local version missed
# the Not/Select/SignExtend32 node types when they were added, so arguments nested
# inside them went undeclared and the signature self-check rejected the output.
used_args = arguments_used


def derivation_args(derivation) -> set:
    """Every argument a derivation mentions, across conditions and stores.

    Missing any of these produces a signature that does not declare a parameter the
    body uses, so this has to consider path conditions too - not just the returned
    expression.
    """
    args = set()
    for path in derivation.paths:
        if path.expression is not None:
            args |= used_args(path.expression)
        for predicate in path.conditions:
            args |= used_args(predicate)
        for store in path.stores:
            args |= used_args(store.address)
            args |= used_args(store.value)
    return args


def _render_load_as_helper(node, render_child) -> str:
    """Render a Load as a Read* helper call rather than a pointer cast."""
    reader = READERS.get((node.size, node.signed))
    if reader is None:
        raise Unsupported("no reader for %d-byte load" % node.size)
    return "%s(%s)" % (reader, render_child(node.address))


def render_cpp(expr) -> str:
    """Render an expression using the Read* helpers instead of pointer casts.

    This deliberately reuses the one renderer in symexec rather than reimplementing
    it. An earlier version had its own copy of the precedence logic, and the two
    drifted: a clarity rule added to symexec never reached the generated source, so
    the emitted C differed from what the tests were checking.
    """
    with load_renderer(_render_load_as_helper):
        return simplify(expr).render()


def render_path_body(path, prefix: str) -> list:
    """Render one path's statements: stores, then the return.

    Every expression on a path was derived against memory as it stood on entry, so
    the returned value and each store's operands are captured into locals *before*
    any write lands. Emitting the writes first inverts a read-modify-write, which
    is precisely what an increment is.
    """
    if path.expression is None:
        # Void: the stores *are* the function, so there is nothing to capture into a
        # temporary first and nothing to return.
        if not path.stores:
            return ["/* no observable effect */"]
        for store in path.stores:
            if store.size not in WRITERS:
                raise Unsupported("no writer for %d-byte store" % store.size)
        return [
            "%s(%s, %s);"
            % (WRITERS[store.size], render_cpp(store.address), render_cpp(store.value))
            for store in path.stores
        ]

    value = render_cpp(path.expression)
    if not path.stores:
        return ["return %s;" % value]

    for store in path.stores:
        if store.size not in WRITERS:
            raise Unsupported("no writer for %d-byte store" % store.size)

    lines = [
        "/* Captured before any store: every expression below refers to",
        "   memory as it stood on entry. */",
        "const s32 %sresult = %s;" % (prefix, value),
    ]
    for index, store in enumerate(path.stores):
        lines.append("const u32 %saddr%d = %s;" % (prefix, index, render_cpp(store.address)))
        lines.append("const u64 %sval%d = %s;" % (prefix, index, render_cpp(store.value)))
    for index, store in enumerate(path.stores):
        lines.append("%s(%saddr%d, %sval%d);" % (WRITERS[store.size], prefix, index, prefix, index))
    lines.append("return %sresult;" % prefix)
    return lines


def render_body(derivation) -> list:
    """Render a whole derivation as C++ statement lines, without indentation."""
    if derivation.is_straight_line:
        return render_path_body(derivation.paths[0], "")

    lines = []
    for index, path in enumerate(derivation.paths[:-1]):
        condition = " && ".join(
            "(%s)" % render_cpp(predicate) for predicate in path.conditions
        )
        lines.append("if (%s) {" % condition)
        lines.extend("    " + line for line in render_path_body(path, "p%d_" % index))
        lines.append("}")
    # The final path is the fall-through: its conditions are the negation of every
    # branch above, so restating them would be redundant and could drift.
    lines.extend(render_path_body(derivation.paths[-1], "end_"))
    return lines


def collect(database: dict, image: bytes, base: int, machine: Machine,
            max_passes: int = 6, verbose: bool = True) -> tuple:
    """Derive and verify, repeating until no further functions become derivable.

    Derivation runs to a fixed point because a call can only be inlined once its
    callee has been derived. Each pass therefore unlocks the callers of whatever the
    previous pass proved, and the process stops when a pass adds nothing.

    Only *verified* derivations are offered for inlining. Building on an unverified
    one would let a single bad derivation propagate into everything above it, and
    the whole point of the pipeline is that nothing unchecked gets used.
    """
    rng = random.Random(0xB3)
    machine.memory.write(SCRATCH, bytes(rng.randrange(256) for _ in range(SCRATCH_SIZE)))

    def read(addr, size, signed):
        return machine.memory.read_int(addr, size, signed)

    verified, rejected = [], {"unsupported": 0, "untestable": 0, "mismatch": 0}
    known = {}
    done = set()
    candidates = [
        f for f in database["functions"]
        if f["size"] // 4 <= MAX_INSTRUCTIONS and f["has_return"]
    ]

    for pass_number in range(1, max_passes + 1):
        added = 0
        pass_rejected = {"unsupported": 0, "untestable": 0, "mismatch": 0}
        for func in candidates:
            if func["addr"] in done:
                continue
            outcome = _attempt(func, image, base, machine, read, known, pass_rejected)
            if outcome is None:
                continue
            done.add(func["addr"])
            verified.append(outcome)
            known[func["addr"]] = outcome[1]
            added += 1
        if verbose:
            print("  pass %d: +%d derived and verified (total %d)"
                  % (pass_number, added, len(verified)))
        if added == 0:
            # The last pass's refusal counts are the ones that describe what is
            # actually left, so keep those rather than the first pass's.
            rejected = pass_rejected
            break
        rejected = pass_rejected
    return verified, rejected


def _journal_effects(memory) -> dict:
    """Collapse a write journal into a net address -> byte map.

    Comparing journals directly would be wrong: the original and the derivation may
    write the same bytes in a different order, or write one location twice. What has
    to match is the resulting memory, so the journal is reduced to final values.

    Writes into the callee's own stack frame are excluded. They are not observable
    effects - the frame is dead the moment the function returns - and the original
    makes plenty of them spilling registers while a derived expression makes none,
    so counting them would fail every function that has a prologue.
    """
    frame_low = STACK_LOW - memory.base
    frame_high = STACK_HIGH - memory.base
    effects = {}
    for offset, previous in getattr(memory, "_journal", None) or ():
        for index in range(len(previous)):
            position = offset + index
            if frame_low <= position <= frame_high:
                continue
            effects[position] = memory.bytes[position]
    return effects


def _attempt(func, image, base, machine, read, known, rejected):
    """Try to derive and verify one function; returns the record or None."""
    if True:
        try:
            derivation = derive_paths(
                image, base, func["addr"], func["end"], known=known
            )
        except Unsupported:
            rejected["unsupported"] += 1
            return None
        except Exception:
            rejected["unsupported"] += 1
            return None

        # Seeded from the address so argument sets are stable across passes: a
        # function retried in a later pass must be tested on the same inputs, or a
        # pass-dependent result would be impossible to reproduce.
        local_rng = random.Random(func["addr"])
        arg_sets = [
            tuple(SCRATCH + local_rng.randrange(0, SCRATCH_SIZE // 2, 16) for _ in range(4))
            for _ in range(ARG_SETS - 1)
        ]
        arg_sets.append((SCRATCH, SCRATCH + 16, SCRATCH + 32, SCRATCH + 48))

        tested = 0
        failed = False
        for args in arg_sets:
            # A function with stores is only verified if its *memory effects* match
            # too, so snapshot the scratch region, let the original run, and then
            # check that applying the derived stores to the same starting state
            # reproduces exactly what the original produced.
            # Journalling rather than snapshotting a region: these functions write
            # to globals well outside the scratch area, and restoring only scratch
            # left those changes in place - which both corrupted later cases and
            # made correct derivations look wrong.
            machine.memory.begin_journal()
            try:
                expected = machine.call(func["addr"], args)
            except (EmulationError, Exception):
                machine.memory.rollback()
                failed = True
                break
            effects = _journal_effects(machine.memory)
            machine.memory.rollback()

            try:
                actual = derivation.evaluate(args, read)
                writes = derivation.evaluate_stores(args, read)
            except Exception:
                machine.memory.rollback()
                failed = True
                break
            machine.memory.begin_journal()
            for address, value, size in writes:
                try:
                    machine.memory.write_int(address, value, size)
                except Exception:
                    failed = True
                    break
            replayed = _journal_effects(machine.memory)
            machine.memory.rollback()
            machine.memory.end_journal()
            if failed:
                break

            tested += 1
            # A void function leaves whatever the caller had in $v0, so there is no
            # return value to compare - only its memory effects carry meaning.
            # Comparing $v0 anyway registered 175 correct derivations as failures.
            if actual is not None and expected != actual:
                rejected["mismatch"] += 1
                failed = True
                break
            if replayed != effects:
                rejected["mismatch"] += 1
                failed = True
                break
        if failed or tested < 5:
            rejected["untestable"] += 1
            return None

        # Only count a function as verified when the test actually varied
        # something. If the expression ignores its arguments - a constant, or a
        # read from a fixed global - then running it on eight argument sets
        # exercises one input eight times, and calling that "verified over 8 sets"
        # would inflate the figure. Those are still derived and almost certainly
        # right, but the evidence is weaker, so they land a tier lower.
        # A branching function is argument-dependent if any of its path conditions
        # mentions an argument, even when every returned value is constant: the
        # test then genuinely exercised different routes.
        argument_dependent = bool(derivation_args(derivation))
        return (func, derivation, tested, argument_dependent)


def emit(verified, out_dir: str, source_name: str) -> None:
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "accessors.cpp")
    verified = sorted(verified, key=lambda item: item[0]["addr"])

    lines = []
    lines.append("/*")
    lines.append(" * Derived accessor functions - GENERATED, do not edit by hand.")
    lines.append(" *")
    lines.append(" * Produced by tools/derive.py from %s." % source_name)
    lines.append(" * Each function was symbolically derived from the original machine code and")
    lines.append(" * then differential-tested against it in the R5900 interpreter; only")
    lines.append(" * derivations that agreed on every argument set appear here.")
    lines.append(" *")
    lines.append(" * The names are placeholders. These are genuine, verified behaviour but no")
    lines.append(" * struct types have been recovered yet, so a descriptive name would be an")
    lines.append(" * invention dressed as a finding. Renaming belongs with the type work.")
    lines.append(" *")
    lines.append(" * Regenerate with:")
    lines.append(" *     python tools/derive.py build/slus_210_50.json --emit src/generated")
    lines.append(" */")
    lines.append("")
    lines.append('#include "b3/generated.h"')
    lines.append("")
    lines.append("namespace generated {")
    lines.append("")

    for func, derivation, tested, strong in verified:
        # Arguments can appear only inside a store, so the signature has to be
        # built from the stores as well as the return expression - otherwise the
        # emitted function references an undeclared parameter and will not compile.
        args = sorted(derivation_args(derivation))
        params = ", ".join("u32 arg%d" % i for i in args) or "void"
        try:
            body_lines = render_body(derivation)
        except Unsupported:
            continue
        lines.append(
            "/* 0x%08X  %d caller(s)  %s */"
            % (
                func["addr"],
                len(func["callers"]),
                ("differential-tested over %d argument sets" % tested) if strong
                else "argument-independent: derived, test near-vacuous",
            )
        )
        return_type = "void" if derivation.is_void else "s32"
        lines.append("%s func_%08X(%s) {" % (return_type, func["addr"], params))
        lines.extend("    " + line for line in body_lines)
        lines.append("}")
        lines.append("")

    lines.append("}  // namespace generated")
    text = "\n".join(lines) + "\n"
    check_signatures(text)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    print("wrote %s (%d functions, signature self-check passed)"
          % (path, len(verified)))


def check_signatures(text: str) -> None:
    """Refuse to write source that references an undeclared argument.

    An earlier version built each parameter list from the return expression alone
    and so emitted functions using arguments that appear only inside a store. That
    source could not compile, and nothing caught it until the output was read by
    eye - which is not a check. This makes it one.
    """
    problems = []
    # Accepts both return types: a function that writes memory and returns nothing
    # is emitted as `void`, and the check must still inspect its body.
    pattern = re.compile(
        r"^(?:s32|void) (func_[0-9A-F]+)\(([^)]*)\) \{\n(.*?)^\}$", re.S | re.M
    )
    for match in pattern.finditer(text):
        name, signature, body = match.groups()
        declared = set(re.findall(r"arg(\d)", signature))
        used = set(re.findall(r"arg(\d)", body))
        missing = sorted(used - declared)
        if missing:
            problems.append(
                "%s uses undeclared %s"
                % (name, ", ".join("arg" + index for index in missing))
            )
    if problems:
        raise SystemExit(
            "refusing to write generated source:\n  " + "\n  ".join(problems)
        )


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("database")
    parser.add_argument("--elf", default=None)
    parser.add_argument("--emit", help="directory to write generated C++ into")
    parser.add_argument("--csv", help="append verified rows to this status table")
    parser.add_argument("--limit", type=int, default=20)
    args = parser.parse_args(argv)

    with open(args.database, encoding="utf-8") as handle:
        database = json.load(handle)
    elf_path = args.elf or os.path.join("extracted", database["meta"]["source"])
    elf = ElfFile.load(elf_path)
    section = elf.exec_sections[0]
    image = elf.section_data(section)
    machine = Machine.from_elf(elf)

    started = time.time()
    verified, rejected = collect(database, image, section.addr, machine)
    print("symbolically derived and verified : %d functions" % len(verified))
    print("  rejected, not derivable         : %d" % rejected["unsupported"])
    print("  rejected, could not be tested   : %d" % rejected["untestable"])
    print("  rejected, derivation disagreed  : %d" % rejected["mismatch"])
    print("  elapsed                         : %.1fs" % (time.time() - started))

    strong = [item for item in verified if item[3]]
    weak = [item for item in verified if not item[3]]
    print("  of which argument-dependent      : %d  -> tier `verified`" % len(strong))
    print("  argument-independent             : %d  -> tier `reconstructed`" % len(weak))
    print("    (a constant or fixed-global read; eight argument sets exercise one")
    print("     input eight times, so the test is near-vacuous for these)")
    total_callers = sum(len(f["callers"]) for f, _d, _t, _s in verified)
    total_bytes = sum(f["size"] for f, _d, _t, _s in verified)
    print("  covering %s bytes and %s call sites"
          % (format(total_bytes, ","), format(total_callers, ",")))

    print("\ntop derivations by caller count:")
    for func, derivation, tested, _strong in sorted(
        verified, key=lambda item: -len(item[0]["callers"])
    )[: args.limit]:
        try:
            body_lines = render_body(derivation)
        except Unsupported:
            continue
        # A branching function prints as several statements; collapse it to one
        # line here so the summary stays scannable, and note the path count.
        summary = " ".join(line.strip() for line in body_lines
                           if not line.strip().startswith(("/*", "memory as")))
        shape = "" if derivation.is_straight_line else " [%d paths]" % len(derivation.paths)
        print("  0x%08X (%3d callers)%s  %s"
              % (func["addr"], len(func["callers"]), shape, summary[:96]))

    if args.emit:
        emit(verified, args.emit, database["meta"]["source"])
    if args.csv:
        import csv

        existing = {}
        if os.path.exists(args.csv):
            with open(args.csv, newline="", encoding="utf-8") as handle:
                for row in csv.DictReader(handle):
                    existing[int(row["address"], 16)] = row
        for func, _derivation, tested, strong in verified:
            if func["addr"] in existing:
                continue
            existing[func["addr"]] = {
                "address": "0x%08X" % func["addr"],
                "name": "generated::func_%08X" % func["addr"],
                "status": "verified" if strong else "reconstructed",
                "source": "src/generated/accessors.cpp",
                "notes": (
                    "symbolically derived; differential-tested over %d argument sets"
                    % tested
                ) if strong else (
                    "symbolically derived; result ignores its arguments, so the "
                    "differential test is near-vacuous - derived, not verified"
                ),
            }
        rows = [existing[key] for key in sorted(existing)]
        with open(args.csv, "w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(
                handle, fieldnames=["address", "name", "status", "source", "notes"]
            )
            writer.writeheader()
            writer.writerows(rows)
        print("updated %s (%d rows)" % (args.csv, len(rows)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
