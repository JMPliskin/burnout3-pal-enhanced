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
    Predicate,
    Store,
    Unsupported,
    _apply,
    _sign32,
    _STORE_SIZES,
    simplify,
)

# Caps that keep a pathological function from exploding the enumeration. A small
# accessor or predicate has a handful of paths; anything past these is not the kind
# of function this module is meant for.
MAX_PATHS = 24
MAX_PATH_LENGTH = 96


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
    def stores(self) -> tuple:
        """Every store the function can perform, across all paths."""
        out = []
        for path in self.paths:
            out.extend(path.stores)
        return tuple(out)

    def render(self) -> str:
        if self.is_straight_line:
            path = self.paths[0]
            lines = [store.render() for store in path.stores]
            lines.append("return %s;" % simplify(path.expression).render())
            return "\n".join(lines)

        lines = []
        for path in self.paths[:-1]:
            lines.append("if (%s) {" % path.render_condition())
            for store in path.stores:
                lines.append("    " + store.render())
            lines.append("    return %s;" % simplify(path.expression).render())
            lines.append("}")
        last = self.paths[-1]
        for store in last.stores:
            lines.append(store.render())
        lines.append("return %s;" % simplify(last.expression).render())
        return "\n".join(lines)

    def select(self, args, read):
        """The first path whose conditions all hold, or None."""
        for path in self.paths:
            if path.holds(args, read):
                return path
        return None

    def evaluate(self, args, read) -> int:
        path = self.select(args, read)
        if path is None:
            raise Unsupported("no path matched: the condition set is incomplete")
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


def _condition_for(ins, get) -> Predicate:
    """The predicate that is true exactly when `ins` takes its branch."""
    zero = Const(0)
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


def _step(regs: dict, stores: list, ins, get) -> None:
    """Apply one non-control instruction, recording a store if it is one."""
    m = ins.mnemonic
    if m in ("nop", "sync"):
        return
    if m in _STORE_SIZES:
        size = _STORE_SIZES[m]
        if size == 16:
            raise Unsupported("128-bit store")
        target = simplify(Binary("add", get(ins.rs), Const(ins.imm & MASK64)))
        stores.append(Store(target, get(ins.rt), size))
        return
    if ins.is_branch or ins.is_jump or ins.is_call:
        raise Unsupported("branch in a delay slot")
    _apply(regs, ins, get)


def derive_paths(image: bytes, base: int, start: int, end: int,
                 gp_value: int = None) -> PathDerivation:
    """Derive a function as guarded paths, handling acyclic control flow."""
    if gp_value is None:
        from .emu import GP_VALUE

        gp_value = GP_VALUE

    def word_at(addr: int) -> int:
        if not (base <= addr < base + len(image) - 3):
            raise Unsupported("address outside the image")
        return struct.unpack_from("<I", image, addr - base)[0]

    paths = []

    def walk(addr: int, regs: dict, stores: list, conditions: tuple, visited: frozenset):
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
                _step(regs, stores, DISASM.decode(word_at(addr + 4), addr + 4), get)
                paths.append(Path(conditions, _getter(regs)(2), tuple(stores)))
                return

            if ins.is_branch:
                predicate = _condition_for(ins, get)
                slot = DISASM.decode(word_at(addr + 4), addr + 4)

                # Taken: the delay slot runs, then control moves to the target.
                taken_regs, taken_stores = dict(regs), list(stores)
                _step(taken_regs, taken_stores, slot, _getter(taken_regs))
                walk(ins.target, taken_regs, taken_stores,
                     conditions + (predicate,), visited)

                # Not taken: a "likely" branch nullifies its delay slot.
                fall_regs, fall_stores = dict(regs), list(stores)
                if not ins.is_likely:
                    _step(fall_regs, fall_stores, slot, _getter(fall_regs))
                walk(addr + 8, fall_regs, fall_stores,
                     conditions + (predicate.negate(),), visited)
                return

            if m in ("b", "j"):
                _step(regs, stores, DISASM.decode(word_at(addr + 4), addr + 4), get)
                addr = ins.target
                continue

            if ins.is_jump or ins.is_call:
                raise Unsupported("calls another function")

            _step(regs, stores, ins, get)
            addr += 4

    regs = {0: Const(0), 28: Const(gp_value)}
    for i in range(4):
        regs[4 + i] = Arg(i)
    walk(start, regs, [], (), frozenset())

    if not paths:
        raise Unsupported("no return reached")
    return PathDerivation(start, tuple(paths))
