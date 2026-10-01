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
from b3lib.symexec import (
    Arg,
    Binary,
    Compare,
    Const,
    Derivation,
    Load,
    Unsupported,
    derive as derive_expression,
    simplify,
)

MAX_INSTRUCTIONS = 12
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


def used_args(expr) -> set:
    if isinstance(expr, Arg):
        return {expr.index}
    if isinstance(expr, Load):
        return used_args(expr.address)
    if isinstance(expr, (Binary, Compare)):
        return used_args(expr.left) | used_args(expr.right)
    return set()


def render_cpp(expr) -> str:
    """Render an expression using the Read* helpers instead of pointer casts."""
    expr = simplify(expr)

    def walk(node, limit=0) -> str:
        if isinstance(node, Load):
            reader = READERS.get((node.size, node.signed))
            if reader is None:
                raise Unsupported("no reader for %d-byte load" % node.size)
            return "%s(%s)" % (reader, walk(node.address))
        if isinstance(node, Arg):
            return "arg%d" % node.index
        if isinstance(node, Const):
            return node.render()
        if isinstance(node, Binary):
            from b3lib.symexec import PRECEDENCE

            level = PRECEDENCE[node.op]
            left = walk(node.left, level)
            right = walk(node.right, level + 1)
            text = "%s %s %s" % (left, Binary._SYMBOL[node.op], right)
            return "(%s)" % text if level < limit else text
        if isinstance(node, Compare):
            left = walk(node.left, 7)
            right = walk(node.right, 7)
            return "(%s < %s) ? 1 : 0" % (left, right)
        raise Unsupported("cannot render %r" % type(node).__name__)

    return walk(expr)


def collect(database: dict, image: bytes, base: int, machine: Machine) -> tuple:
    rng = random.Random(0xB3)
    machine.memory.write(SCRATCH, bytes(rng.randrange(256) for _ in range(SCRATCH_SIZE)))

    def read(addr, size, signed):
        return machine.memory.read_int(addr, size, signed)

    verified, rejected = [], {"unsupported": 0, "untestable": 0, "mismatch": 0}
    for func in database["functions"]:
        if func["size"] // 4 > MAX_INSTRUCTIONS or not func["has_return"]:
            continue
        try:
            derivation = derive_expression(image, base, func["addr"], func["end"])
        except Unsupported:
            rejected["unsupported"] += 1
            continue
        except Exception:
            rejected["unsupported"] += 1
            continue

        arg_sets = [
            tuple(SCRATCH + rng.randrange(0, SCRATCH_SIZE // 2, 16) for _ in range(4))
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
            before = bytes(machine.memory.read(SCRATCH, SCRATCH_SIZE))
            try:
                expected = machine.call(func["addr"], args)
            except (EmulationError, Exception):
                machine.memory.write(SCRATCH, before)
                failed = True
                break
            after = bytes(machine.memory.read(SCRATCH, SCRATCH_SIZE))

            machine.memory.write(SCRATCH, before)
            try:
                actual = derivation.evaluate(args, read)
                writes = derivation.evaluate_stores(args, read)
            except Exception:
                machine.memory.write(SCRATCH, before)
                failed = True
                break
            for address, value, size in writes:
                try:
                    machine.memory.write_int(address, value, size)
                except Exception:
                    failed = True
                    break
            replayed = bytes(machine.memory.read(SCRATCH, SCRATCH_SIZE))
            machine.memory.write(SCRATCH, before)
            if failed:
                break

            tested += 1
            if expected != actual or replayed != after:
                rejected["mismatch"] += 1
                failed = True
                break
        if failed or tested < 5:
            if not failed:
                rejected["untestable"] += 1
            elif rejected["mismatch"] == 0:
                rejected["untestable"] += 1
            continue

        # Only count a function as verified when the test actually varied
        # something. If the expression ignores its arguments - a constant, or a
        # read from a fixed global - then running it on eight argument sets
        # exercises one input eight times, and calling that "verified over 8 sets"
        # would inflate the figure. Those are still derived and almost certainly
        # right, but the evidence is weaker, so they land a tier lower.
        argument_dependent = bool(used_args(derivation.expression))
        for store in derivation.stores:
            if used_args(store.address) or used_args(store.value):
                argument_dependent = True
        verified.append((func, derivation, tested, argument_dependent))
    return verified, rejected


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
        args = set(used_args(derivation.expression))
        for store in derivation.stores:
            args |= used_args(store.address)
            args |= used_args(store.value)
        args = sorted(args)
        params = ", ".join("u32 arg%d" % i for i in args) or "void"
        try:
            body = render_cpp(derivation.expression)
            for store in derivation.stores:
                if store.size not in WRITERS:
                    raise Unsupported("no writer for %d-byte store" % store.size)
                render_cpp(store.address)
                render_cpp(store.value)
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
        lines.append("s32 func_%08X(%s) {" % (func["addr"], params))
        if derivation.stores:
            # Every expression was derived against memory as it stood on entry, so
            # the values have to be captured *before* any write lands. Emitting the
            # stores first would change the meaning whenever a function reads and
            # writes the same location - which is exactly what an increment does.
            lines.append("    /* Captured before any store: the derived expressions")
            lines.append("       all refer to memory as it was on entry. */")
            lines.append("    const s32 result = %s;" % body)
            for index, store in enumerate(derivation.stores):
                lines.append("    const u32 addr%d = %s;" % (index, render_cpp(store.address)))
                lines.append("    const u64 val%d = %s;" % (index, render_cpp(store.value)))
            for index, store in enumerate(derivation.stores):
                lines.append("    %s(addr%d, val%d);" % (WRITERS[store.size], index, index))
            lines.append("    return result;")
        else:
            lines.append("    return %s;" % body)
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
    pattern = re.compile(r"^s32 (func_[0-9A-F]+)\(([^)]*)\) \{\n(.*?)^\}$", re.S | re.M)
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
            body = render_cpp(derivation.expression)
        except Unsupported:
            continue
        print("  0x%08X (%3d callers)  return %s;"
              % (func["addr"], len(func["callers"]), body[:78]))

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
