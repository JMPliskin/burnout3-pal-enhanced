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
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from b3lib.elf import ElfFile
from b3lib.r5900 import DISASM
from b3lib.emu import EmulationError, Machine
from b3lib.paths import (STACK_TOP, Execution, PathDerivation, _is_frame_address,
                         tree_expressions,
                         _relation, derive_paths)
from b3lib import calls, structs, testmem, typed
from b3lib.symexec import (
    MASK64,
    Arg,
    Binary,
    CallResult,
    Compare,
    Const,
    Derivation,
    Load,
    Predicate,
    Temp,
    Var,
    rebuild,
    register_name,
    Unsupported,
    FLOAT_ARG_BASE,
    arg_name,
    arg_renderer,
    arguments_used,
    children,
    load_renderer,
    low32,
    simplify,
)

from b3lib.emu import DEFAULT_STACK_SIZE, DEFAULT_STACK_TOP

# Longer functions are not attempted. The limit was 96 while a full run took half
# an hour; with the pipeline fast, 256 added 114 functions and 1.6% of the code,
# and 512 six more large ones (0.2%). Beyond that the limit is not what stops
# functions - path explosion is.
MAX_INSTRUCTIONS = 512
# Refusals that come from unrolling or enumerating a loop; such a function is
# retried with loops derived as loops.
LOOP_SHAPED = {"loop", "too many paths", "exploration budget exhausted",
               "path too long", "expression too large"}
# Instructions one verification case may execute, callees included. Interpreting
# is slow, and a function that calls into a large original routine would otherwise
# spend minutes per case; one that needs more than this is reported untestable.
VERIFY_BUDGET = 100_000
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
        for _count, kind, obj in path.marks:
            if kind == "loop":
                for expr in tree_expressions([("loop", obj)]):
                    args |= used_args(expr)
        if path.expression is not None:
            args |= used_args(path.expression)
        for predicate in path.conditions:
            args |= used_args(predicate)
        for store in path.stores:
            args |= used_args(store.address)
            args |= used_args(store.value)
        for call in path.calls:
            for _index, expr in call.arguments:
                args |= used_args(expr)
            if call.pointer is not None:
                args |= used_args(call.pointer)
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
    if store.size <= 4:
        # Only the bytes written matter, so a sign extension is noise here.
        value = simplify(low32(value))
    if isinstance(value, Const) and store.size < 8:
        return Const(value.value & ((1 << (8 * store.size)) - 1)).render()
    return render_cpp(value)


def _result(expression) -> str:
    """A returned value; a constant is shown as the s32 the caller receives."""
    # The return type is s32, so only the low 32 bits are returned.
    value = simplify(low32(simplify(expression)))
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


class StatementEmitter:
    """Render a derivation that calls underived functions, as ordered statements.

    Its expressions refer to memory by epoch - as it was entered, or as it stood
    after some call - while the emitted C++ simply runs in order against live
    memory. Two things keep them in step. Before a call, every value from the
    current epoch that a later statement uses is captured into a local, since the
    callee may change it. Before a store, every later-used value the store might
    overwrite is captured, since it was read before the store happened. Everything
    else is read where it is used, which is when the original read it.
    """

    def __init__(self, derivation) -> None:
        self.tree = derivation.tree()
        self.void = derivation.is_void
        self.replaced = {}        # id(original node) -> Temp
        self.counter = 0
        self.frame_size = _frame_size([simplify(e) for e in self._expressions(self.tree)])
        self.loops = {}
        self.active = []          # loops whose body is being emitted, innermost last
        self.hoisted = set()      # call results declared before a loop, assigned in it

    def render(self) -> list:
        """The statements, with a guest stack frame around them if one is needed.

        A function whose frame address escapes to a callee keeps its frame in guest
        memory, so it allocates one: `sp` is its stack pointer, and every frame
        address is rendered relative to it.
        """
        lines = self._block(self.tree)
        if not self.frame_size:
            return lines
        return ["const u64 sp = StackEnter(0x%X);" % self.frame_size] + lines

    # -- capture -------------------------------------------------------------

    def _apply(self, expr):
        memo = {}

        def walk(node):
            key = id(node)
            if key in self.replaced:
                return self.replaced[key]
            if key in memo:
                return memo[key]
            kids = children(node)
            new = rebuild(node, [walk(child) for child in kids]) if kids else node
            memo[key] = new
            return new
        result = walk(expr)
        if not self.frame_size:
            return result
        # Frame addresses are often unfolded sums (`sp + 0x10` from an `addiu`), so
        # they are found after folding; a constant one is relative to `sp` here.
        return _relative_to_sp(simplify(result), self.frame_size)

    @staticmethod
    def _expressions(statements) -> list:
        return list(tree_expressions(statements))

    def _capture(self, later, affected, future) -> list:
        """Capture maximal subexpressions of `later` that contain an affected load
        and nothing that only exists in the future; returns declaration lines."""
        lines = []
        state = {}

        def contains(node):
            key = id(node)
            if key in self.replaced:
                return (False, False)
            if key in state:
                return state[key]
            hit = isinstance(node, Load) and affected(node)
            ahead = future(node)
            for child in children(node):
                child_hit, child_ahead = contains(child)
                hit, ahead = hit or child_hit, ahead or child_ahead
            state[key] = (hit, ahead)
            return state[key]

        def visit(node):
            key = id(node)
            if key in self.replaced:
                return
            hit, ahead = contains(node)
            if not hit:
                return
            if not ahead:
                self.counter += 1
                temp = Temp("t%d" % self.counter)
                lines.append("const u64 %s = %s;" % (temp.name, render_cpp(self._apply(node))))
                self.replaced[key] = temp
                return
            for child in children(node):
                visit(child)

        for expr in later:
            visit(expr)
        return lines

    # -- statements ----------------------------------------------------------

    def _block(self, statements, epoch: int = 0) -> list:
        lines = []
        for position, statement in enumerate(statements):
            later = self._expressions(statements[position + 1:])
            kind = statement[0]
            if kind == "store":
                store = statement[1]
                # Capture first: a value the store may overwrite has to be read
                # before the write lands, not after.
                # Any epoch up to this one: between barriers only stores change
                # memory, so a load from before a barrier can still be overwritten.
                lines += self._capture(
                    later,
                    lambda load: load.epoch <= epoch and _relation(
                        store.address, store.size, load.address, load.size) != "disjoint",
                    lambda node: self._beyond(node, epoch),
                )
                lines += self._store(store)
            elif kind == "call":
                call = statement[1]
                lines += self._capture(
                    later,
                    lambda load: load.epoch < call.index,
                    lambda node: self._beyond(node, call.index - 1),
                )
                lines.append(self._call(call, later))
                epoch = call.index
            elif kind == "barrier":
                # Nothing to emit: the statements already run in order against live
                # memory, which is what a barrier means.
                epoch = statement[1].index
            elif kind == "loop":
                lines += self._loop(statement[1], later, epoch)
                epoch = statement[1].after_epoch
            elif kind == "continue":
                lines += self._continue(statement[1], statement[2])
            elif kind == "break":
                if self.loops[statement[1]].exits > 1:
                    lines.append("l%d_exit = %d;" % (statement[1], statement[2]))
                lines.append("break;")
            elif kind == "unreachable":
                continue
            elif kind == "if":
                lines.append("if (%s) {" % render_cpp(self._apply(statement[1])))
                # A local captured inside one branch does not exist in the other, so
                # each branch starts from the captures made before the `if`.
                outer = dict(self.replaced)
                lines += ["    " + line for line in self._block(statement[2], epoch)]
                self.replaced = dict(outer)
                lines.append("} else {")
                lines += ["    " + line for line in self._block(statement[3], epoch)]
                self.replaced = outer
                lines.append("}")
            else:
                if self.frame_size:
                    # The frame's memory is left as it is, so the returned value can
                    # still read it after the allocation is released.
                    lines.append("StackLeave(0x%X);" % self.frame_size)
                if self.void or statement[1] is None:
                    lines.append("return;")
                else:
                    lines.append("return %s;" % _result(self._apply(statement[1])))
        return lines

    def _loop(self, loop, later, epoch) -> list:
        """A loop whose trip count depends on its input, as `for (;;)`.

        The loop may write memory or call anything, so every later-used value read
        before it is captured first, as for a call. Its variables are mutable
        locals; each `continue` gives them their next values together, through
        temporaries, since the new values are all computed from the old ones.
        """
        self.loops[loop.id] = loop
        lines = self._capture(later, lambda load: load.epoch <= epoch,
                              lambda node: self._beyond(node, epoch))
        # A call result inside the loop may be used after it, where a local
        # declared in the body no longer exists, so it is declared here.
        for index in sorted(_calls_in(loop.body)):
            if index not in self.hoisted:
                self.hoisted.add(index)
                lines.append("u64 r%d = 0;" % index)
        initial = dict(loop.init)
        for register in loop.carried:
            value = render_cpp(self._apply(initial[register])) if register in initial else "0"
            lines.append("u64 %s = %s;" % (Var(loop.id, register).render(), value))
        if loop.exits > 1:
            lines.append("u64 l%d_exit = 0;" % loop.id)
        lines.append("for (;;) {")
        outer = dict(self.replaced)
        self.active.append(loop.id)
        lines += ["    " + line for line in self._block(loop.body, loop.head_epoch)]
        self.active.pop()
        self.replaced = outer
        lines.append("}")
        return lines

    def _beyond(self, node, epoch) -> bool:
        """Whether a node cannot be evaluated at this point of the emitted code."""
        if isinstance(node, Var):
            # A loop variable exists only inside its own loop.
            return node.loop not in self.active
        return _beyond(node, epoch)

    def _continue(self, loop, assignments) -> list:
        changed = [(register, expr) for register, expr in assignments
                   if expr != Var(loop, register)]
        lines = []
        for register, expr in changed:
            lines.append("const u64 l%d_next_%s = %s;"
                         % (loop, register_name(register), render_cpp(self._apply(expr))))
        for register, _expr in changed:
            lines.append("%s = l%d_next_%s;"
                         % (Var(loop, register).render(), loop, register_name(register)))
        if not lines:
            return ["continue;"]
        return ["{"] + ["    " + line for line in lines] + ["    continue;", "}"]

    def _pointer_call(self, call, passed, later) -> str:
        """A call through a function pointer or virtual method table."""
        text = _pointer_call_text(self, call, passed)
        if call.index in self.hoisted:
            return "r%d = %s;" % (call.index, text)
        if not any(_mentions_call(expr, call.index) for expr in later):
            return "%s;" % text
        return "const u64 r%d = %s;" % (call.index, text)

    def _store(self, store) -> list:
        address = self._apply(store.address)
        moved = type(store)(address, self._apply(store.value), store.size)
        value = _store_value(moved)
        target = _TYPING.store_target(moved) if _TYPING is not None else None
        if target is not None:
            base, embeds, _owner, offset, index = target
            return [_TYPING.write(None, base, embeds, offset, value, index)]
        if store.size not in WRITERS:
            raise Unsupported("no writer for %d-byte store" % store.size)
        return ["%s(%s, %s);" % (WRITERS[store.size], render_cpp(address), value)]

    def _call(self, call, later) -> str:
        passed = dict(call.arguments)
        if call.pointer is not None:
            return self._pointer_call(call, passed, later)
        signature = _SIGNATURES.get(call.target)
        if signature is None:
            indices = [index for index, _expr in call.arguments]
            _EXTERNS[call.target] = tuple(indices)
            returns_value = True
        else:
            return_type, indices = signature
            missing = [index for index in indices if index not in passed]
            if missing:
                raise Unsupported("callee declares an argument the call does not pass")
            returns_value = return_type != "void"
        arguments = ", ".join(render_cpp(self._apply(passed[index])) for index in indices)
        text = "func_%08X(%s)" % (call.target, arguments)
        used = any(_mentions_call(expr, call.index) for expr in later)
        if used and not returns_value:
            raise Unsupported("uses the result of a call to a void function")
        if not used and not (call.index in self.hoisted and returns_value):
            return "%s;" % text
        if call.index in self.hoisted:
            return "r%d = %s;" % (call.index, text)
        temp = Temp("r%d" % call.index)
        return "const u64 %s = %s;" % (temp.name, text)


def _relative_to_sp(expr, frame_size: int):
    """`expr` with every constant frame address rewritten as `sp + offset`."""
    memo = {}

    def walk(node):
        key = id(node)
        if key in memo:
            return memo[key]
        kids = children(node)
        if not kids and isinstance(node, Const) and _is_frame_address(node):
            new = Binary("add", Temp("sp"),
                         Const((node.value & MASK64) - (STACK_TOP - frame_size)))
        else:
            new = rebuild(node, [walk(child) for child in kids]) if kids else node
        memo[key] = new
        return new
    return walk(expr)


def _frame_size(expressions) -> int:
    """Bytes of guest stack a derivation's frame addresses span, or 0 for none."""
    lowest = None
    seen = set()
    stack = list(expressions)
    while stack:
        node = stack.pop()
        if id(node) in seen:
            continue
        seen.add(id(node))
        if isinstance(node, Const) and _is_frame_address(node):
            address = node.value & MASK64
            lowest = address if lowest is None else min(lowest, address)
        stack.extend(children(node))
    if lowest is None or lowest >= STACK_TOP:
        return 0
    return (STACK_TOP - lowest + 15) & ~15


def _pointer_call_text(emitter, call, passed) -> str:
    last = max(passed) if passed else -1
    arguments = [render_cpp(emitter._apply(call.pointer))]
    for index in range(last + 1):
        arguments.append(render_cpp(emitter._apply(passed[index])) if index in passed else "0")
    return "CallPointer(%s)" % ", ".join(arguments)


def _beyond(node, epoch: int) -> bool:
    """True for a node that only exists after epoch `epoch`: a later load or call."""
    if isinstance(node, Load):
        return node.epoch > epoch
    if isinstance(node, CallResult):
        return node.index > epoch
    return False


def _calls_in(statements) -> set:
    """Indices of every call in a statement tree, loop bodies included."""
    found = set()
    for statement in statements:
        if statement[0] == "call":
            found.add(statement[1].index)
        elif statement[0] == "if":
            found |= _calls_in(statement[2]) | _calls_in(statement[3])
        elif statement[0] == "loop":
            found |= _calls_in(statement[1].body)
    return found


def _mentions_call(expr, index: int) -> bool:
    seen = set()
    stack = [expr]
    while stack:
        node = stack.pop()
        if id(node) in seen:
            continue
        seen.add(id(node))
        if isinstance(node, CallResult) and node.index == index:
            return True
        stack.extend(children(node))
    return False


def _reads_memory(expr) -> bool:
    """Whether evaluating an expression loads from memory."""
    if isinstance(expr, Load):
        return True
    return any(_reads_memory(child) for child in children(expr))


# Emitted functions' signatures, {address: (return type, parameter indices)}, and the
# underived functions the emitted code calls, {address: parameter indices}. Set by
# `emit` around rendering, like _TYPING, because the statements need them.
_SIGNATURES = {}
_EXTERNS = {}


def render_body(derivation) -> list:
    """Render a whole derivation as C++ statement lines, without indentation."""
    if derivation.sequential:
        return StatementEmitter(derivation).render()
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


# The original's results for every verified function's test cases, recorded during
# verification so the compiled check need not run the original again.
EXPECTATIONS = {}


def has_loop(image, base, func) -> bool:
    """Whether a function has a backward branch, i.e. a loop to derive as one."""
    for address in range(func["addr"], func["end"], 4):
        ins = DISASM.decode(struct.unpack_from("<I", image, address - base)[0], address)
        if (ins.is_branch or (ins.is_jump and not ins.is_call)) \
                and func["addr"] <= ins.target <= address:
            return True
    return False


def direct_callees(image, base, func) -> set:
    """Targets of a function's direct calls and jumps out of it."""
    found = set()
    for address in range(func["addr"], func["end"], 4):
        ins = DISASM.decode(struct.unpack_from("<I", image, address - base)[0], address)
        if ins.target >= 0 and (ins.is_call or ins.is_jump) \
                and not (func["addr"] <= ins.target < func["end"]):
            found.add(ins.target)
    return found


_GRAPHS = {}


def recovered_graph(database, image, base):
    """The struct-type graph for this binary, recovered once per run.

    It is needed to seed global pointers before testing and again to type the
    emitted source; recovering it twice was a fifth of a whole run.
    """
    key = (id(database), base)
    if key not in _GRAPHS:
        _GRAPHS[key] = structs.recover(database, image, base)
    return _GRAPHS[key]


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
    # Which arguments each function reads, so a call to one that is not derived can
    # be made rather than refused.
    parameters = calls.parameters(database, image, base)
    # Global pointers that are null until the game runs would make every function
    # using them untestable; point them at scratch (see testmem).
    seeded = testmem.seed_global_pointers(machine.memory,
                                          global_pointers(database, image, base))
    if verbose:
        print("  seeded %d null global pointers for testing" % seeded)
    known = {}
    done = set()
    candidates = [
        f for f in database["functions"]
        if f["size"] // 4 <= MAX_INSTRUCTIONS and f["has_return"]
    ]

    # A refused function can only come out differently in a later pass if one of
    # its callees has since been derived and so can be inlined; anything else about
    # it is unchanged. Re-attempting every refused function each pass was a third
    # of the derivation time.
    callees = {f["addr"]: direct_callees(image, base, f) for f in candidates}
    last = {}                    # address -> the refusal category it last got
    mismatched = []
    newly_known = None
    for pass_number in range(1, max_passes + 1):
        added = 0
        fresh = set()
        for func in candidates:
            if func["addr"] in done:
                continue
            if newly_known is not None and not (callees[func["addr"]] & newly_known):
                continue
            outcome_counts = {"unsupported": 0, "untestable": 0, "mismatch": 0}
            outcome = _attempt(func, image, base, machine, read, known, outcome_counts,
                               parameters)
            if outcome is None:
                for category, count in outcome_counts.items():
                    if count and category != "mismatched":
                        last[func["addr"]] = category
                if outcome_counts.get("mismatched"):
                    mismatched.append(func["addr"])
                continue
            last.pop(func["addr"], None)
            done.add(func["addr"])
            verified.append(outcome)
            # Only call-free derivations are inlined into callers. One that calls an
            # underived function is a sequence of statements, not an expression,
            # and its callers call it instead.
            if not outcome[1].sequential:
                known[func["addr"]] = outcome[1]
                fresh.add(func["addr"])
            added += 1
        if verbose:
            print("  pass %d: +%d derived and verified (total %d)"
                  % (pass_number, added, len(verified)))
        newly_known = fresh
        if added == 0 or not fresh:
            break
    rejected = {"unsupported": 0, "untestable": 0, "mismatch": 0}
    for category in last.values():
        rejected[category] += 1
    rejected["mismatched"] = sorted(a for a in set(mismatched) if last.get(a) == "mismatch")
    return verified, rejected


def global_pointers(database, image, base) -> set:
    """Addresses of global variables that some code dereferences as a pointer."""
    graph = recovered_graph(database, image, base)
    if structs.GLOBALS not in graph.ids:
        return set()
    fields = graph.fields[graph.find(graph.ids[structs.GLOBALS])]
    return {address & 0xFFFFFFFF for address, field in fields.items()
            if field.target is not None and 4 in field.widths()}


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
            machine.call(func["addr"], args, max_instructions=VERIFY_BUDGET)
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
    arg_sets.append(tuple(SCRATCH + 16 * i for i in range(8))
                    + tuple(testmem.float_bits(local_rng) for _ in range(8)))
    profile = _choose_profile(machine, func, arg_sets[0])
    if profile is None:
        return None
    return arg_sets, profile


def run_cases(func, machine, arg_sets, profile, evaluate, record=None, recorded=None):
    """Run the original and a candidate on every case; return (outcome, tested).

    `evaluate(args)` returns the candidate's (result, writes) for one case, with
    memory as the original saw it on entry; result is None for a void function and
    writes are (address, value, size) triples. The outcome is "ok", "mismatch" or
    "failed" (the case could not be run at all).

    `record`, a list, receives the original's (result, effects) for each case;
    `recorded` supplies them instead of running the original again. The compiled
    check uses the results recorded while the derivation was verified: they are
    the same runs on the same memory, and repeating them doubled its cost.
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
        if recorded is not None:
            expected, effects = recorded[case]
        else:
            machine.memory.begin_journal()
            try:
                expected = machine.call(func["addr"], args, max_instructions=VERIFY_BUDGET)
            except Exception:
                machine.memory.rollback()
                machine.memory.end_journal()
                return "failed", tested
            effects = _journal_effects(machine.memory)
            machine.memory.rollback()
            if record is not None:
                record.append((expected, effects))

        # The candidate runs inside the replay journal: one that calls an underived
        # function writes memory as it goes - the callee must see the stores made
        # before it - rather than handing back a list of writes at the end.
        machine.memory.begin_journal()
        try:
            actual, writes = evaluate(args)
        except Exception:
            machine.memory.rollback()
            machine.memory.end_journal()
            return "failed", tested
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


def _attempt(func, image, base, machine, read, known, rejected, parameters=None):
    """Try to derive and verify one function; returns the record or None."""
    if True:
        try:
            try:
                derivation = derive_paths(
                    image, base, func["addr"], func["end"], known=known,
                    parameters=parameters,
                )
            except Unsupported as exc:
                # A loop whose trip count depends on the input can neither be
                # unrolled nor enumerated as paths; derive it as a real loop.
                if str(exc) not in LOOP_SHAPED or not has_loop(image, base, func):
                    raise
                derivation = derive_paths(
                    image, base, func["addr"], func["end"], known=known,
                    parameters=parameters, loops=True,
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
            if derivation.sequential:
                return derivation.run(args, Execution(machine, VERIFY_BUDGET)), []
            return derivation.evaluate(args, read), derivation.evaluate_stores(args, read)

        record = []
        outcome, tested = run_cases(func, machine, arg_sets, profile, evaluate, record)
        # A disagreement and an untestable function are different findings and are
        # counted once each. An earlier version fell through from one to the other
        # and so reported every disagreement twice.
        if outcome == "mismatch":
            rejected["mismatch"] += 1
            rejected.setdefault("mismatched", []).append(func["addr"])
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
        EXPECTATIONS[func["addr"]] = (arg_sets, profile, record)
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
    # Signatures first: a function that calls another emitted function calls it by
    # its real signature, so every one must be known before any body is rendered.
    # A body that cannot be rendered drops its function, which can change how its
    # callers render, so this repeats until nothing more is dropped.
    emitted = list(verified)
    while True:
        _SIGNATURES.clear()
        _EXTERNS.clear()
        headers = {}
        for func, derivation, _tested, _strong in emitted:
            headers[func["addr"]] = _signature(func, derivation, catalog)
            _SIGNATURES[func["addr"]] = (headers[func["addr"]][0], headers[func["addr"]][2])
        used_types = set()
        used_globals, used_objects, used_global_arrays = set(), set(), set()
        chunks, dropped = [], []
        for func, derivation, tested, strong in emitted:
            return_type, params, _indices, roots = headers[func["addr"]]
            _TYPING = typed.Typing(catalog, func["addr"]) if catalog is not None else None
            if _TYPING is not None:
                _TYPING.used |= roots
            try:
                body_lines = render_body(derivation)
            except Unsupported:
                dropped.append(func["addr"])
                _TYPING = None
                continue
            if _TYPING is not None:
                used_types |= _TYPING.used
                used_globals |= _TYPING.used_globals
                used_objects |= _TYPING.used_objects
                used_global_arrays |= _TYPING.used_global_arrays
            _TYPING = None
            chunk = [
                "/* 0x%08X  %d caller(s)  %s */"
                % (
                    func["addr"],
                    len(func["callers"]),
                    ("differential-tested over %d argument sets" % tested) if strong
                    else "argument-independent: derived, test near-vacuous",
                ),
                "%s func_%08X(%s) {" % (return_type, func["addr"], params),
            ]
            chunk += ["    " + line for line in body_lines]
            chunk += ["}", ""]
            chunks.append(chunk)
        if not dropped:
            break
        emitted = [item for item in emitted if item[0]["addr"] not in dropped]

    lines.append("/* Every emitted function, so they can call one another in any order. */")
    for func, _derivation, _tested, _strong in emitted:
        return_type, params, _indices, _roots = headers[func["addr"]]
        lines.append("%s func_%08X(%s);" % (return_type, func["addr"], params))
    lines.append("")
    if _EXTERNS:
        lines.append("/* Functions not derived yet. The code above calls them as the original")
        lines.append("   did; a port links them to their eventual reconstructions, and the")
        lines.append("   compiled check links them to the original machine code. */")
        for address, indices in sorted(_EXTERNS.items()):
            params = ", ".join("u64 %s" % arg_name(index) for index in indices) or "void"
            lines.append("u64 func_%08X(%s);  /* not yet derived */" % (address, params))
        lines.append("")
    for chunk in chunks:
        lines.extend(chunk)
    verified = emitted

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
    return verified


def _signature(func, derivation, catalog):
    """(return type, parameter text, parameter indices, types used) for a function."""
    # Arguments can appear only inside a store, so the signature has to be built
    # from the stores as well as the return expression - otherwise the emitted
    # function references an undeclared parameter and will not compile.
    indices = sorted(derivation_args(derivation))
    params, roots = [], set()
    for index in indices:
        root = catalog.slot(func["addr"], index) if catalog is not None else None
        if index >= FLOAT_ARG_BASE or root is None:
            # A float argument holds a 32-bit pattern, but is declared u64 like
            # every other value: as a u32, `farg0 & 0x7FFFFFFF` + -127 wrapped at
            # 32 bits instead of going negative, and the compiled check caught two
            # functions disagreeing.
            params.append("u64 %s" % arg_name(index))
        else:
            roots.add(root)
            params.append("%s %s" % (catalog.names[root], arg_name(index)))
    return_type = "void" if derivation.is_void else "s32"
    return return_type, ", ".join(params) or "void", tuple(indices), roots


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
        declared = set(re.findall(r"\bf?arg\d+\b", signature))
        used = set(re.findall(r"\bf?arg\d+\b", body))
        missing = sorted(used - declared)
        if missing:
            problems.append("%s uses undeclared %s" % (name, ", ".join(missing)))
    if problems:
        raise SystemExit(
            "refusing to write generated source:\n  " + "\n  ".join(problems)
        )


def _functions_at_errors(source: str, errors: str) -> set:
    """Addresses of the emitted functions that contain the given compiler errors."""
    lines = [int(n) for n in re.findall(r"accessors\.cpp\((\d+)\)", errors)]
    if not lines:
        return set()
    with open(source, encoding="utf-8") as handle:
        text = handle.read().splitlines()
    starts = [(number + 1, int(match.group(1), 16)) for number, line in enumerate(text)
              for match in [re.match(r"^(?:s32|void) func_([0-9A-F]{8})\(.*\) \{$", line)]
              if match]
    found = set()
    for line in lines:
        owner = None
        for number, address in starts:
            if number <= line:
                owner = address
            else:
                break
        if owner is not None:
            found.add(owner)
    return found


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
    # A function whose source does not compile is dropped and the file re-emitted,
    # rather than aborting the whole check: one rendering gap must not cost every
    # other function its verdict.
    source = os.path.join(out_dir, "accessors.cpp")
    for _attempt_number in range(8):
        try:
            stage = time.time()
            dll, _functions = cppcheck.build(source, runtime,
                                             os.path.join(root, "include"), work)
            print("  compiling took %.0fs" % (time.time() - stage))
            break
        except cppcheck.CompilerUnavailable as exc:
            print("compiled check SKIPPED: %s" % exc)
            print("  the emitted source has NOT been run; only its trees were verified")
            return verified
        except cppcheck.CompileError as exc:
            broken = _functions_at_errors(source, str(exc))
            if not broken:
                raise
            print("  dropping %d function(s) that do not compile: %s"
                  % (len(broken), ", ".join("0x%08X" % a for a in sorted(broken)[:8])))
            verified = [item for item in verified if item[0]["addr"] not in broken]
            verified = emit(verified, out_dir, database["meta"]["source"], catalog)
    else:
        raise SystemExit("emitted source still does not compile after dropping functions")
    compiled = cppcheck.CompiledFunctions(dll, machine)

    kept, disagreed, untestable = [], [], []
    for item in verified:
        func = item[0]
        if func["addr"] in EXPECTATIONS:
            arg_sets, profile, recorded = EXPECTATIONS[func["addr"]]
            outcome, tested = run_cases(func, machine, arg_sets, profile,
                                        compiled.evaluator(func["addr"]), recorded=recorded)
        else:
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
    print("  rejected, derivation disagreed  : %d%s" % (
        rejected["mismatch"],
        "  (%s)" % ", ".join("0x%08X" % a for a in rejected.get("mismatched", [])[:8])
        if rejected.get("mismatched") else ""))
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
            graph = recovered_graph(database, image, section.addr)
            evidence = typed.add_derived_evidence(graph, verified)
            catalog = structs.Catalog(graph)
            print("recovered %d struct types in %.1fs (%d fields added from derivations)"
                  % (len(catalog.roots), time.time() - started, evidence))
        stage = time.time()
        verified = emit(verified, args.emit, database["meta"]["source"], catalog)
        print("  emission took %.0fs" % (time.time() - stage))
        if not args.no_compile:
            stage = time.time()
            verified = check_compiled(verified, machine, args.emit, database, catalog)
            print("  compiled check took %.0fs" % (time.time() - stage))
    if args.csv:
        import csv

        existing = {}
        if os.path.exists(args.csv):
            with open(args.csv, newline="", encoding="utf-8") as handle:
                for row in csv.DictReader(handle):
                    # Generated rows are rebuilt from this run, so a function that
                    # is no longer derived - or no longer emitted - does not keep a
                    # status it can no longer show evidence for.
                    if row["source"] == "src/generated/accessors.cpp":
                        continue
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
