"""Derive branching functions as guarded paths.

`symexec.py` handles straight-line functions, which covers accessors but little
else: most small functions have a conditional in them.  Those can still be derived
*exactly*, provided the control flow is acyclic - enumerate every route from the
entry to a return, record the branch conditions holding along each, and the
function becomes an ordered list of guarded results.

Loops are refused outright.  A loop's result depends on a trip count that is not a
function of the arguments alone, so no finite expression describes it; emitting
something anyway would be the plausible-but-wrong output this whole approach exists
to avoid.  Calls are refused for the same reason - the callee would have to be
derived first.

Everything here still ends up differential-tested against the original machine
code, so a mistake in the path logic surfaces as a disagreement rather than as
quietly wrong source.
"""

from __future__ import annotations

import dataclasses
import struct

from .r5900 import DISASM
from .symexec import (
    MASK32,
    MASK64,
    Arg,
    Binary,
    Const,
    Expr,
    Load,
    Predicate,
    Store,
    Unsupported,
    FP_COND,
    Not1,
    children,
    _apply,
    fpr,
    _LOAD_SIZES,
    substitute,
    _sign32,
    _STORE_SIZES,
    simplify,
)

# Caps that keep a pathological function from exploding the enumeration. A small
# accessor or predicate has a handful of paths; anything past these is not the kind
# of function this module is meant for.
MAX_PATHS = 24
MAX_PATH_LENGTH = 96

# `$sp` is seeded with the same value the interpreter uses, so stack addresses fold
# to constants and the frame can be modelled separately from real memory.
#
# This matters more than it looks. Over a thousand functions were refused purely for
# reading `$sp`, because they have an ordinary prologue: reserve a frame, spill the
# callee-saved registers, do the work, reload them, return. Modelling the frame makes
# those spill/reload pairs cancel out, leaving the function's actual behaviour.
from .emu import DEFAULT_STACK_TOP as STACK_TOP

# How far below the stack top a reference is still considered part of the frame. A
# function's own frame is far smaller than this; the bound exists so a wild address
# is treated as real memory rather than silently absorbed into the frame model.
MAX_FRAME_SIZE = 0x4000


def _is_frame_address(expr) -> bool:
    """True when an address is a constant inside the current stack frame."""
    folded = simplify(expr)
    if not isinstance(folded, Const):
        return False
    value = folded.value & MASK64
    return STACK_TOP - MAX_FRAME_SIZE <= value <= STACK_TOP + 0x10


@dataclasses.dataclass
class Path:
    """One route through a function: the conditions holding, and the outcome."""

    conditions: tuple      # Predicates, all true along this path
    expression: Expr       # what $v0 holds on return
    stores: tuple = ()

    def render_condition(self) -> str:
        if not self.conditions:
            return "true"
        return " && ".join("(%s)" % p.render() for p in self.conditions)

    def holds(self, args, read) -> bool:
        return all(p.evaluate(args, read) for p in self.conditions)


@dataclasses.dataclass
class PathDerivation:
    """A function described as an ordered list of guarded results."""

    address: int
    paths: tuple

    @property
    def is_straight_line(self) -> bool:
        return len(self.paths) == 1 and not self.paths[0].conditions

    @property
    def is_void(self) -> bool:
        """True when no path produces a value."""
        return all(path.expression is None for path in self.paths)

    @property
    def stores(self) -> tuple:
        """Every store the function can perform, across all paths."""
        out = []
        for path in self.paths:
            out.extend(path.stores)
        return tuple(out)

    def _render_return(self, path) -> list:
        """The return statement for a path, or nothing when the function is void."""
        if path.expression is None:
            return []
        return ["return %s;" % simplify(path.expression).render()]

    def render(self) -> str:
        if self.is_straight_line:
            path = self.paths[0]
            lines = [store.render() for store in path.stores]
            lines.extend(self._render_return(path))
            return "\n".join(lines) if lines else "/* no observable effect */"

        lines = []
        for path in self.paths[:-1]:
            lines.append("if (%s) {" % path.render_condition())
            for store in path.stores:
                lines.append("    " + store.render())
            returned = self._render_return(path)
            lines.extend("    " + line for line in returned)
            if not returned:
                # A void path still has to leave the function here, or control would
                # fall through into the next guard.
                lines.append("    return;")
            lines.append("}")
        last = self.paths[-1]
        for store in last.stores:
            lines.append(store.render())
        lines.extend(self._render_return(last))
        return "\n".join(lines)

    def select(self, args, read):
        """The first path whose conditions all hold, or None."""
        for path in self.paths:
            if path.holds(args, read):
                return path
        return None

    def evaluate(self, args, read):
        """The returned value, or None for a void function.

        A void function's `$v0` holds whatever the caller left there, so there is
        nothing to compare and the verifier must not try.
        """
        path = self.select(args, read)
        if path is None:
            raise Unsupported("no path matched: the condition set is incomplete")
        if path.expression is None:
            return None
        return _sign32(simplify(path.expression).evaluate(args, read) & MASK32)

    def evaluate_stores(self, args, read) -> list:
        path = self.select(args, read)
        if path is None:
            raise Unsupported("no path matched: the condition set is incomplete")
        out = []
        for store in path.stores:
            address = simplify(store.address).evaluate(args, read) & MASK32
            value = simplify(store.value).evaluate(args, read)
            out.append((address, value & ((1 << (store.size * 8)) - 1), store.size))
        return out


def _getter(regs: dict):
    def get(index: int) -> Expr:
        if index == 0:
            return Const(0)
        if index not in regs:
            raise Unsupported("reads uninitialised register $%d" % index)
        return regs[index]
    return get


def _constant_truth(predicate):
    """Decide a condition that depends on nothing, or return None.

    A condition is only decidable here when every leaf under it is a constant - no
    argument, and no memory load, since a load's value is not known until the test
    supplies memory. `Unsupported` from the evaluation is treated as undecidable
    rather than allowed to escape.
    """
    def all_constant(expr) -> bool:
        kids = children(expr)
        if not kids:
            return isinstance(expr, Const)
        if isinstance(expr, Load):
            return False
        return all(all_constant(child) for child in kids)

    try:
        if not all_constant(predicate):
            return None
        return bool(predicate.evaluate((0, 0, 0, 0), None))
    except Exception:
        return None


def _condition_for(ins, get):
    """The condition that is true exactly when `ins` takes its branch."""
    zero = Const(0)
    # The FPU branches test the flag a preceding `c.<cond>.s` set, so the condition
    # is whatever comparison was recorded rather than anything in this instruction.
    if ins.mnemonic in ("bc1t", "bc1tl"):
        return get(FP_COND)
    if ins.mnemonic in ("bc1f", "bc1fl"):
        return Not1(get(FP_COND))
    table = {
        "beq": ("eq", True), "beql": ("eq", True),
        "bne": ("ne", True), "bnel": ("ne", True),
        "blez": ("le", False), "blezl": ("le", False),
        "bgtz": ("gt", False), "bgtzl": ("gt", False),
        "bltz": ("lt", False), "bltzl": ("lt", False),
        "bgez": ("ge", False), "bgezl": ("ge", False),
    }
    entry = table.get(ins.mnemonic)
    if entry is None:
        raise Unsupported("no condition rule for %s" % ins.mnemonic)
    op, two_register = entry
    right = get(ins.rt) if two_register else zero
    return Predicate(op, get(ins.rs), right)


def _step(regs: dict, stores: list, frame: dict, memory: dict, ins, get) -> None:
    """Apply one non-control instruction.

    Accesses that land in the stack frame are routed to `frame` rather than being
    recorded as real memory effects, so a prologue's register spills and the
    matching epilogue reloads cancel instead of appearing as writes the caller
    could observe.

    `memory` tracks what the function has already written to real memory. Loads
    consult it first, because the derived expressions all refer to memory as it
    stood on entry: a function that stores a value and then reads the same location
    back - a setter returning the field it just set is the common shape - would
    otherwise be derived against stale contents.
    """
    m = ins.mnemonic
    if m in ("nop", "sync"):
        return

    if m == "swc1":
        target = simplify(Binary("add", get(ins.rs), Const(ins.imm & MASK64)))
        value = get(fpr(ins.rt))
        if _is_frame_address(target):
            frame[(simplify(target).value & MASK64, 4)] = value
            return
        stores.append(Store(target, value, 4))
        memory[(target, 4)] = value
        return

    if m == "lwc1":
        target = simplify(Binary("add", get(ins.rs), Const(ins.imm & MASK64)))
        if _is_frame_address(target):
            key = (simplify(target).value & MASK64, 4)
            if key not in frame:
                raise Unsupported("reads an unwritten stack slot")
            regs[fpr(ins.rt)] = frame[key]
            return
        if memory and (target, 4) in memory:
            regs[fpr(ins.rt)] = memory[(target, 4)]
            return
        if memory and not (
            isinstance(target, Const)
            and all(isinstance(a, Const) and a.value != target.value for a, _z in memory)
        ):
            raise Unsupported("load may alias an earlier store")
        regs[fpr(ins.rt)] = Load(target, 4, False)
        return

    if m in _STORE_SIZES:
        size = _STORE_SIZES[m]
        target = simplify(Binary("add", get(ins.rs), Const(ins.imm & MASK64)))
        if _is_frame_address(target):
            # A spill. Record the full value regardless of width: a 128-bit `sq`
            # of a callee-saved register is reloaded by a matching `lq`, and the
            # expression model has no notion of the upper half, so the pair only
            # cancels if the slot holds exactly what was written.
            frame[(simplify(target).value & MASK64, size)] = get(ins.rt)
            return
        if size == 16:
            raise Unsupported("128-bit store to real memory")
        value = get(ins.rt)
        stores.append(Store(target, value, size))
        memory[(target, size)] = value
        return

    if m in _LOAD_SIZES:
        size, _signed_load = _LOAD_SIZES[m]
        target = simplify(Binary("add", get(ins.rs), Const(ins.imm & MASK64)))
        if _is_frame_address(target):
            key = (simplify(target).value & MASK64, size)
            if key not in frame:
                # Reading a frame slot that was never written means the value came
                # from the caller's frame - a stack argument - which this model does
                # not carry.
                raise Unsupported("reads an unwritten stack slot")
            regs[ins.rt] = frame[key]
            return
        if memory:
            if (target, size) in memory:
                # Exactly the location that was written: forward the stored value.
                regs[ins.rt] = memory[(target, size)]
                return
            # Otherwise the load might or might not alias an earlier store. Proving
            # it does not requires both addresses to be constants that differ;
            # anything else is refused rather than assumed independent.
            if not isinstance(target, Const) or not all(
                isinstance(addr, Const) and addr.value != target.value
                for addr, _size in memory
            ):
                raise Unsupported("load may alias an earlier store")

    if ins.is_branch or ins.is_jump or ins.is_call:
        raise Unsupported("branch in a delay slot")
    _apply(regs, ins, get)


# Registers a call may clobber. After inlining a callee these are dropped rather
# than left holding stale values: if the caller then reads one, the derivation is
# refused instead of quietly using a value the real code would not have had.
CALLER_SAVED = tuple(range(2, 4)) + tuple(range(4, 8)) + tuple(range(8, 16)) + (24, 25)


def derive_paths(image: bytes, base: int, start: int, end: int,
                 gp_value: int = None, known: dict = None) -> PathDerivation:
    """Derive a function as guarded paths, handling acyclic control flow.

    `known` maps an address to an already-derived `PathDerivation`. A call to one of
    those is inlined - its result substituted with the caller's arguments - which is
    what lets derivation progress up the call graph instead of stopping at the first
    `jal`. Only single-path callees are inlined, so a conditional callee cannot
    multiply the caller's path count.
    """
    known = known or {}
    if gp_value is None:
        from .emu import GP_VALUE

        gp_value = GP_VALUE

    def word_at(addr: int) -> int:
        if not (base <= addr < base + len(image) - 3):
            raise Unsupported("address outside the image")
        return struct.unpack_from("<I", image, addr - base)[0]

    paths = []

    def walk(addr: int, regs: dict, stores: list, frame: dict, memory: dict,
             conditions: tuple, visited: frozenset):
        if len(paths) > MAX_PATHS:
            raise Unsupported("too many paths")
        if len(visited) > MAX_PATH_LENGTH:
            raise Unsupported("path too long")

        while True:
            if addr in visited:
                raise Unsupported("loop")
            if not (start <= addr < end):
                raise Unsupported("control flow leaves the function")
            visited = visited | {addr}
            get = _getter(regs)
            ins = DISASM.decode(word_at(addr), addr)
            m = ins.mnemonic

            if m in ("nop", "sync"):
                addr += 4
                continue

            if m == "jr":
                if not ins.is_return:
                    raise Unsupported("indirect jump")
                _step(regs, stores, frame, memory,
                      DISASM.decode(word_at(addr + 4), addr + 4), get)
                # A function that never writes $v0 returns nothing. Insisting on a
                # return value refused 514 functions outright - every setter and
                # initialiser in the binary - because they are simply void.
                try:
                    result = _getter(regs)(2)
                except Unsupported:
                    result = None
                paths.append(Path(conditions, result, tuple(stores)))
                return

            if ins.is_branch:
                predicate = _condition_for(ins, get)
                slot = DISASM.decode(word_at(addr + 4), addr + 4)

                # When the condition folds to a constant, only one side can ever
                # run. Exploring both anyway manufactures an infeasible path, and
                # `select` picks the first path whose conditions hold - so a path
                # that can never be taken can still be chosen, which is how a
                # function that stores a flag and immediately branches on it came
                # out wrong. Folding also keeps `if (1 != 0)` out of the output.
                decided = _constant_truth(predicate)
                if decided is True:
                    _step(regs, stores, frame, memory, slot, get)
                    addr = ins.target
                    continue
                if decided is False:
                    if not ins.is_likely:
                        _step(regs, stores, frame, memory, slot, get)
                    addr += 8
                    continue

                # Taken: the delay slot runs, then control moves to the target.
                taken_regs, taken_stores = dict(regs), list(stores)
                taken_frame, taken_memory = dict(frame), dict(memory)
                _step(taken_regs, taken_stores, taken_frame, taken_memory,
                      slot, _getter(taken_regs))
                walk(ins.target, taken_regs, taken_stores, taken_frame, taken_memory,
                     conditions + (predicate,), visited)

                # Not taken: a "likely" branch nullifies its delay slot.
                fall_regs, fall_stores = dict(regs), list(stores)
                fall_frame, fall_memory = dict(frame), dict(memory)
                if not ins.is_likely:
                    _step(fall_regs, fall_stores, fall_frame, fall_memory,
                          slot, _getter(fall_regs))
                walk(addr + 8, fall_regs, fall_stores, fall_frame, fall_memory,
                     conditions + (predicate.negate(),), visited)
                return

            if m in ("b", "j"):
                _step(regs, stores, frame, memory,
                      DISASM.decode(word_at(addr + 4), addr + 4), get)
                if start <= ins.target < end:
                    addr = ins.target
                    continue
                # A jump out of the function is a tail call: the callee's result
                # becomes this function's result, with no return of our own. These
                # are common in thunks and were previously all refused as "control
                # flow leaves the function".
                if ins.target not in known:
                    raise Unsupported("tail-calls an underived function")
                callee = known[ins.target]
                if not callee.is_straight_line:
                    raise Unsupported("tail-calls a conditional function")
                get = _getter(regs)
                arguments = {
                    index: regs[4 + index] for index in range(4) if 4 + index in regs
                }
                inlined = callee.paths[0]
                for store in inlined.stores:
                    stores.append(Store(
                        simplify(substitute(store.address, arguments)),
                        simplify(substitute(store.value, arguments)),
                        store.size,
                    ))
                paths.append(Path(
                    conditions,
                    simplify(substitute(inlined.expression, arguments)),
                    tuple(stores),
                ))
                return

            if ins.is_call:
                if ins.target < 0 or ins.target not in known:
                    raise Unsupported("calls another function")
                callee = known[ins.target]
                if not callee.is_straight_line:
                    raise Unsupported("calls a conditional function")

                # The delay slot runs before the call, and often sets the last
                # argument, so apply it first.
                _step(regs, stores, frame, memory,
                      DISASM.decode(word_at(addr + 4), addr + 4), get)
                get = _getter(regs)

                arguments = {}
                for index in range(4):
                    if 4 + index in regs:
                        arguments[index] = regs[4 + index]
                inlined = callee.paths[0]
                for store in inlined.stores:
                    moved = Store(
                        simplify(substitute(store.address, arguments)),
                        simplify(substitute(store.value, arguments)),
                        store.size,
                    )
                    stores.append(moved)
                    memory[(moved.address, moved.size)] = moved.value
                result = simplify(substitute(inlined.expression, arguments))
                for index in CALLER_SAVED:
                    regs.pop(index, None)
                regs[2] = result
                addr += 8
                continue

            if ins.is_jump:
                raise Unsupported("unhandled jump")

            _step(regs, stores, frame, memory, ins, get)
            addr += 4

    # Seed the registers to exactly the state the interpreter starts a call in, so
    # a derivation and the differential test it is checked against begin from the
    # same place.
    #
    # The callee-saved registers need values for a mundane reason: the prologue
    # *reads* them in order to spill them. Without a value, every function with a
    # frame was refused for "reads uninitialised register $16" or "$31". Seeding
    # them lets the spill and its matching reload cancel out.
    #
    # The limitation to be honest about: a function that genuinely took a value in a
    # callee-saved register would be derived as though that register were zero, and
    # the differential test would still pass, because the interpreter also starts it
    # at zero. The ABI says those registers are not parameters, so this does not
    # arise in compiled code - but it is an assumption, not something proven here.
    from .emu import RETURN_SENTINEL

    regs = {0: Const(0), 28: Const(gp_value), 29: Const(STACK_TOP)}
    for index in list(range(16, 24)) + [30]:   # $s0..$s7 and $fp
        regs[index] = Const(0)
    regs[31] = Const(RETURN_SENTINEL)          # $ra
    # $f20..$f31 are callee-saved, so they are spilled and reloaded for exactly the
    # same reason the integer saved registers are, and were blocking 123 functions
    # for exactly the same reason: the prologue reads them in order to save them.
    # Float *arguments* ($f12..$f14) are deliberately left unseeded - seeding those
    # with zero would make the differential test pass vacuously, since the
    # interpreter starts them at zero too.
    for index in range(20, 32):
        regs[fpr(index)] = Const(0)
    for i in range(4):
        regs[4 + i] = Arg(i)
    walk(start, regs, [], {}, {}, (), frozenset())

    if not paths:
        raise Unsupported("no return reached")
    void_paths = sum(1 for path in paths if path.expression is None)
    if void_paths and void_paths != len(paths):
        # Some paths produce a value and others do not. That is either a function
        # whose result is only meaningful on some routes, or a misread; either way
        # the signature is ambiguous, so refuse rather than choose.
        raise Unsupported("inconsistent void and value-returning paths")
    return PathDerivation(start, tuple(paths))
