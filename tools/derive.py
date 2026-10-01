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
from b3lib import structs, testmem, typed
from b3lib.symexec import (
    Arg,
    Binary,
    Compare,
    Const,
    Derivation,
    Load,
    Predicate,
    Unsupported,
    arg_renderer,
    arguments_used,
    children,
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
    if _TYPING is not None:
        with load_renderer(_TYPING.render_load), arg_renderer(_TYPING.render_arg):
            return simplify(expr).render()
    with load_renderer(_render_load_as_helper):
        return simplify(expr).render()


# The recovered types for the function being emitted, or None to emit untyped.
# Module state rather than a parameter because it has to reach the renderer's
# hooks, several calls deep.
_TYPING = None


def _store_value(store) -> str:
    """A store's value, with a constant narrowed to the width actually written."""
    value = simplify(store.value)
    if isinstance(value, Const) and store.size < 8:
        return Const(value.value & ((1 << (8 * store.size)) - 1)).render()
    return render_cpp(value)


def _result(expression) -> str:
    """A returned value; a constant is shown as the s32 the caller receives."""
    value = simplify(expression)
    if isinstance(value, Const):
        low = value.value & 0xFFFFFFFF
        return str(low - (1 << 32) if low & 0x80000000 else low)
    return render_cpp(value)


def render_path_body(path, prefix: str, final: bool = True) -> list:
    """Render one path's statements: stores, then the return.

    Every expression on a path was derived against memory as it stood on entry, so
    the returned value and each store's operands are captured into locals *before*
    any write lands. Emitting the writes first inverts a read-modify-write, which
    is precisely what an increment is.

    A path that is not the last must end in a return, void or not: the paths are
    emitted as a sequence of `if` blocks, and one that does not leave falls through
    into the stores of the paths after it. Void paths used to omit it - and the
    capture too - and both were found by compiling and running the output.
    """
    for store in path.stores:
        if store.size not in WRITERS:
            raise Unsupported("no writer for %d-byte store" % store.size)

    if path.expression is None and len(path.stores) <= 1:
        # A single store reads memory before writing anything, so there is nothing
        # it could disturb and no need for temporaries.
        lines = []
        for store in path.stores:
            target = _TYPING.store_target(store) if _TYPING is not None else None
            if target is not None:
                base, embeds, _owner, offset, index = target
                lines.append(_TYPING.write(None, base, embeds, offset, _store_value(store),
                                           index))
            else:
                lines.append("%s(%s, %s);" % (
                    WRITERS[store.size], render_cpp(store.address), _store_value(store)))
        if not final:
            lines.append("return;")
        return lines or ["/* no observable effect */"]

    if path.expression is not None and not path.stores:
        return ["return %s;" % _result(path.expression)]

    # Only what reads memory needs capturing: a constant or an argument cannot be
    # changed by a store, so those are written straight into the statement that
    # uses them, and the source says `arg0.set_f_10(0)` rather than routing a zero
    # through a temporary.
    lines = []
    if path.expression is not None:
        if _reads_memory(path.expression):
            lines.append("const s32 %sresult = %s;" % (prefix, _result(path.expression)))
            returned = "%sresult" % prefix
        else:
            returned = _result(path.expression)
    writes = []
    holders = {}
    for index, store in enumerate(path.stores):
        if _reads_memory(store.value):
            lines.append("const u64 %sval%d = %s;" % (prefix, index, _store_value(store)))
            value = "%sval%d" % (prefix, index)
        else:
            value = _store_value(store)
        target = _TYPING.store_target(store) if _TYPING is not None else None
        if target is not None:
            base, embeds, owner, offset, element = target
            holder = None
            if _TYPING.needs_capture(base):
                # One holder per distinct object, so stores through the same one
                # read alike - which is also what lets an unrolled loop re-roll.
                view = _TYPING.object(base, embeds)
                holder = holders.get(view)
                if holder is None:
                    holder = holders[view] = "%sobj%d" % (prefix, index)
                    lines.append("const %s %s = %s;" % (
                        _TYPING.type_name(owner), holder, view))
            if element is not None and not isinstance(element, int) and _reads_memory(element):
                # An index computed from memory must be read before any store too.
                lines.append("const u64 %sidx%d = %s;" % (prefix, index, render_cpp(element)))
                element = "%sidx%d" % (prefix, index)
            writes.append(_TYPING.write(holder, base, embeds, offset, value, element))
        elif _reads_memory(store.address):
            lines.append("const u32 %saddr%d = %s;" % (prefix, index, render_cpp(store.address)))
            writes.append("%s(%saddr%d, %s);" % (WRITERS[store.size], prefix, index, value))
        else:
            writes.append("%s(%s, %s);"
                          % (WRITERS[store.size], render_cpp(store.address), value))
    if lines:
        lines = [
            "/* Captured before any store: every expression below refers to",
            "   memory as it stood on entry. */",
        ] + lines
    lines.extend(reroll(writes))
    if path.expression is not None:
        lines.append("return %s;" % returned)
    elif not final:
        lines.append("return;")
    return lines


# An element write with a literal index: `arg0.set_f_40(3, 0);`, `set_g_00670BD8(2, v);`
_ELEMENT_WRITE = re.compile(r"^(?P<head>(?:.*\.)?set_[fg]_[0-9A-Fm]+\()(?P<index>\d+), (?P<rest>.*)$")
MIN_ITERATIONS = 3
MAX_LOOP_BODY = 16


def reroll(statements: list) -> list:
    """Fold unrolled loops back into `for` loops.

    The derivation unrolls every loop it can, so a loop that cleared 24 records
    arrives as 120 statements. Wherever a block of statements repeats with every
    element index advanced by the same step and nothing else different, the block
    becomes the body of a loop. Each statement keeps its own starting index, so a
    body that writes `a[i]` and `a[i + 20]` re-rolls too. Iterations run in the
    original order, and the statements write only constants or values captured
    before any store, so the loop does exactly what the sequence did - and the
    compiled check runs it.
    """
    def template(statement):
        match = _ELEMENT_WRITE.match(statement)
        if match is None:
            return None
        return (match.group("head"), match.group("rest")), int(match.group("index"))

    shapes = [template(statement) for statement in statements]
    out = []
    position = 0
    while position < len(statements):
        best = None
        for body in range(1, MAX_LOOP_BODY + 1):
            first = shapes[position:position + body]
            if len(first) < body or any(shape is None for shape in first):
                break
            second = shapes[position + body:position + 2 * body]
            if len(second) < body or any(shape is None for shape in second):
                continue
            step = second[0][1] - first[0][1]
            if step < 1:
                continue
            iterations = 1
            while True:
                block = shapes[position + iterations * body:position + (iterations + 1) * body]
                if len(block) < body or any(
                        shape is None or shape[0] != first[k][0]
                        or shape[1] != first[k][1] + iterations * step
                        for k, shape in enumerate(block)):
                    break
                iterations += 1
            if iterations >= MIN_ITERATIONS and (
                    best is None or iterations * body > best[0] * best[1]):
                best = (iterations, body, step)
        if best is None:
            out.append(statements[position])
            position += 1
            continue
        iterations, body, step = best
        block = shapes[position:position + body]
        if step == 1:
            origin = block[0][1]
            out.append("for (u64 i = %d; i < %d; i++) {" % (origin, origin + iterations))
        else:
            origin = 0
            out.append("for (u64 i = 0; i < %d; i++) {" % iterations)
        for (head, rest), index in block:
            out.append("    %s%s, %s" % (head, _loop_index(index - origin, step), rest))
        out.append("}")
        position += iterations * body
    return out


def _loop_index(offset: int, step: int) -> str:
    """The element index inside a re-rolled loop: step * i + offset."""
    scaled = "i" if step == 1 else "%d * i" % step
    if offset == 0:
        return scaled
    return "%s %s %d" % (scaled, "+" if offset > 0 else "-", abs(offset))


def _reads_memory(expr) -> bool:
    """Whether evaluating an expression loads from memory."""
    if isinstance(expr, Load):
        return True
    return any(_reads_memory(child) for child in children(expr))


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
        lines.extend(
            "    " + line for line in render_path_body(path, "p%d_" % index, final=False)
        )
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


def _choose_profile(machine, func, args):
    """Return the first memory profile under which the original executes, or None.

    The profile only decides whether the test can *reach* the function; the original
    and the derivation always read the same memory, so it never changes a verdict.
    """
    for profile in testmem.PROFILES:
        machine.memory.end_journal()
        machine.memory.write(SCRATCH, testmem.build(profile, func["addr"]))
        machine.memory.begin_journal()
        try:
            machine.call(func["addr"], args)
            return profile
        except Exception:
            continue
        finally:
            machine.memory.rollback()
            machine.memory.end_journal()
    return None


def test_plan(func, machine):
    """The argument sets and memory profile a function is tested with, or None.

    Separate from the test itself so that anything else that must face exactly the
    same cases - the compiled C++ check - can ask for them.
    """
    # Seeded from the address so argument sets are stable across passes: a
    # function retried in a later pass must be tested on the same inputs, or a
    # pass-dependent result would be impossible to reproduce.
    local_rng = random.Random(func["addr"])
    arg_sets = [testmem.arguments(local_rng) for _ in range(ARG_SETS - 1)]
    arg_sets.append((SCRATCH, SCRATCH + 16, SCRATCH + 32, SCRATCH + 48))
    profile = _choose_profile(machine, func, arg_sets[0])
    if profile is None:
        return None
    return arg_sets, profile


def run_cases(func, machine, arg_sets, profile, evaluate):
    """Run the original and a candidate on every case; return (outcome, tested).

    `evaluate(args)` returns the candidate's (result, writes) for one case, with
    memory as the original saw it on entry; result is None for a void function and
    writes are (address, value, size) triples. The outcome is "ok", "mismatch" or
    "failed" (the case could not be run at all).
    """
    tested = 0
    for case, args in enumerate(arg_sets):
        # Fresh memory of the chosen shape for every case, so the test varies
        # memory as well as arguments. Written outside the journal on purpose:
        # the journal does not nest, and this region is rewritten before every
        # case anyway, so nothing leaks between them.
        machine.memory.end_journal()
        machine.memory.write(SCRATCH, testmem.build(profile, func["addr"] + case))
        # A function with stores is only verified if its *memory effects* match
        # too, so let the original run under a journal, and then check that
        # applying the candidate's stores to the same starting state reproduces
        # exactly what the original produced.
        # Journalling rather than snapshotting a region: these functions write
        # to globals well outside the scratch area, and restoring only scratch
        # left those changes in place - which both corrupted later cases and
        # made correct derivations look wrong.
        machine.memory.begin_journal()
        try:
            expected = machine.call(func["addr"], args)
        except Exception:
            machine.memory.rollback()
            machine.memory.end_journal()
            return "failed", tested
        effects = _journal_effects(machine.memory)
        machine.memory.rollback()

        try:
            actual, writes = evaluate(args)
        except Exception:
            machine.memory.rollback()
            machine.memory.end_journal()
            return "failed", tested
        machine.memory.begin_journal()
        try:
            for address, value, size in writes:
                machine.memory.write_int(address, value, size)
        except Exception:
            machine.memory.rollback()
            machine.memory.end_journal()
            return "failed", tested
        replayed = _journal_effects(machine.memory)
        machine.memory.rollback()
        machine.memory.end_journal()

        tested += 1
        # A void function leaves whatever the caller had in $v0, so there is no
        # return value to compare - only its memory effects carry meaning.
        # Comparing $v0 anyway registered 175 correct derivations as failures.
        if actual is not None and expected != actual:
            return "mismatch", tested
        if replayed != effects:
            return "mismatch", tested
    return "ok", tested


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

        plan = test_plan(func, machine)
        if plan is None:
            rejected["untestable"] += 1
            return None
        arg_sets, profile = plan

        def evaluate(args):
            return derivation.evaluate(args, read), derivation.evaluate_stores(args, read)

        outcome, tested = run_cases(func, machine, arg_sets, profile, evaluate)
        # A disagreement and an untestable function are different findings and are
        # counted once each. An earlier version fell through from one to the other
        # and so reported every disagreement twice.
        if outcome == "mismatch":
            rejected["mismatch"] += 1
            return None
        if outcome == "failed" or tested < 5:
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


def emit(verified, out_dir: str, source_name: str, catalog=None) -> None:
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
    lines.append(" * Pointer arguments are typed with the struct types recovered in types.h, so")
    lines.append(" * field reads and writes go through named accessors; anything the recovery")
    lines.append(" * could not place stays a plain Read or Write helper call on the address. The")
    lines.append(" * compiled form of every function is run against the original as well.")
    lines.append(" *")
    lines.append(" * The names are placeholders. The behaviour is verified, but what each")
    lines.append(" * function and field is *for* has not been worked out, and a descriptive")
    lines.append(" * name invented now would read as a finding later.")
    lines.append(" *")
    lines.append(" * Regenerate with:")
    lines.append(" *     python tools/derive.py build/slus_210_50.json --emit src/generated")
    lines.append(" */")
    lines.append("")
    lines.append('#include "b3/generated.h"')
    if catalog is not None:
        lines.append('#include "types.h"')
    lines.append("")
    lines.append("/* Every value here is a u64 register image, so narrowing one to a u32 address")
    lines.append("   or float word is the intended truncation, not an accident; MSVC warns about")
    lines.append("   each of the thousands of them. */")
    lines.append("#ifdef _MSC_VER")
    lines.append("#pragma warning(disable : 4244)")
    lines.append("#endif")
    lines.append("")
    lines.append("namespace generated {")
    lines.append("")

    global _TYPING
    used_types = set()
    used_globals, used_objects, used_global_arrays = set(), set(), set()
    for func, derivation, tested, strong in verified:
        # Arguments can appear only inside a store, so the signature has to be
        # built from the stores as well as the return expression - otherwise the
        # emitted function references an undeclared parameter and will not compile.
        args = sorted(derivation_args(derivation))
        _TYPING = typed.Typing(catalog, func["addr"]) if catalog is not None else None
        params = []
        for index in args:
            root = catalog.slot(func["addr"], index) if catalog is not None else None
            if root is None:
                params.append("u64 arg%d" % index)
            else:
                _TYPING.used.add(root)
                params.append("%s arg%d" % (catalog.names[root], index))
        params = ", ".join(params) or "void"
        try:
            body_lines = render_body(derivation)
        except Unsupported:
            _TYPING = None
            continue
        if _TYPING is not None:
            used_types |= _TYPING.used
            used_globals |= _TYPING.used_globals
            used_objects |= _TYPING.used_objects
            used_global_arrays |= _TYPING.used_global_arrays
        _TYPING = None
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
    types_path = os.path.join(out_dir, "types.h")
    if catalog is not None:
        with open(types_path, "w", encoding="utf-8") as handle:
            handle.write(typed.header(catalog, used_types, source_name,
                                      used_globals, used_objects, used_global_arrays))
        print("wrote %s (%d recovered types)" % (types_path, len(used_types)))
    elif os.path.exists(types_path):
        os.remove(types_path)
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


def check_compiled(verified, machine, out_dir: str, database: dict, catalog=None) -> list:
    """Compile the emitted C++, run it on every function's cases, keep what agrees.

    The derivations were verified as expression trees; this verifies the source
    that is actually shipped. A function whose compiled form disagrees with the
    original is dropped and the file re-emitted without it - a disagreement here
    is a rendering bug, and counting the function anyway would certify source
    nobody tested.
    """
    from b3lib import cppcheck

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    work = os.path.join(root, "build", "cppcheck")
    runtime = [os.path.join(root, "src", "runtime", "eefpu.cpp")]
    try:
        dll, _functions = cppcheck.build(
            os.path.join(out_dir, "accessors.cpp"), runtime,
            os.path.join(root, "include"), work,
        )
    except cppcheck.CompilerUnavailable as exc:
        print("compiled check SKIPPED: %s" % exc)
        print("  the emitted source has NOT been run; only its trees were verified")
        return verified
    compiled = cppcheck.CompiledFunctions(dll, machine.memory)

    kept, disagreed, untestable = [], [], []
    for item in verified:
        func = item[0]
        plan = test_plan(func, machine)
        if plan is None:
            untestable.append(func["addr"])
            continue
        outcome, tested = run_cases(
            func, machine, plan[0], plan[1], compiled.evaluator(func["addr"])
        )
        if outcome == "ok":
            kept.append(item)
        elif outcome == "mismatch":
            disagreed.append(func["addr"])
        else:
            untestable.append(func["addr"])
    print("compiled C++ run against the original: %d agree, %d disagree, %d could not run"
          % (len(kept), len(disagreed), len(untestable)))
    for address in disagreed[:20]:
        print("  disagrees: func_%08X" % address)
    for address in untestable[:20]:
        print("  could not run: func_%08X" % address)
    if len(kept) != len(verified):
        emit(kept, out_dir, database["meta"]["source"], catalog)
    return kept


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("database")
    parser.add_argument("--elf", default=None)
    parser.add_argument("--emit", help="directory to write generated C++ into")
    parser.add_argument("--csv", help="append verified rows to this status table")
    parser.add_argument("--limit", type=int, default=20)
    parser.add_argument("--untyped", action="store_true",
                        help="emit plain addresses instead of recovered struct types")
    parser.add_argument("--no-compile", action="store_true",
                        help="skip compiling the emitted C++ and running it")
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
        catalog = None
        if not args.untyped:
            started = time.time()
            graph = structs.recover(database, image, section.addr)
            evidence = typed.add_derived_evidence(graph, verified)
            catalog = structs.Catalog(graph)
            print("recovered %d struct types in %.1fs (%d fields added from derivations)"
                  % (len(catalog.roots), time.time() - started, evidence))
        emit(verified, args.emit, database["meta"]["source"], catalog)
        if not args.no_compile:
            verified = check_compiled(verified, machine, args.emit, database, catalog)
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
