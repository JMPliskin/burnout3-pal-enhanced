"""Symbolic execution of straight-line functions, to derive their semantics.

A large number of the discovered functions are tiny read-only accessors - load a
pointer from a structure, index it, return a field.  Reading each one by hand works
but does not scale, and the result is mechanical enough that it can be derived
instead.

This walks a branch-free function with *expressions* in the registers rather than
values, and reports whatever ends up in `$v0` as a tree.  That tree does two jobs:
it renders as C for a human to read, and it evaluates against concrete arguments so
the derivation can be differential-tested against the real machine code.  A
derivation that cannot be tested is worth little, so both come from the same tree.

Scope is deliberately narrow.  Anything with a branch, a store, or an instruction
outside the supported set is refused with `Unsupported` rather than approximated -
a plausible but wrong formula presented as a derivation would be worse than no
derivation at all.
"""

from __future__ import annotations

import dataclasses
import struct

from .r5900 import DISASM

MASK32 = 0xFFFFFFFF
MASK64 = (1 << 64) - 1


class Unsupported(Exception):
    """Raised when a function is outside what can be derived symbolically."""


# -- expression tree -----------------------------------------------------

class Expr:
    """Base class for symbolic values."""

    def render(self) -> str:
        raise NotImplementedError

    def evaluate(self, args, read) -> int:
        raise NotImplementedError

    def __str__(self) -> str:
        return self.render()


@dataclasses.dataclass(frozen=True)
class Arg(Expr):
    index: int  # 0..3 for $a0..$a3

    def render(self) -> str:
        return "arg%d" % self.index

    def evaluate(self, args, read) -> int:
        return args[self.index] & MASK64


@dataclasses.dataclass(frozen=True)
class Const(Expr):
    value: int

    def render(self) -> str:
        v = self.value
        if -256 < _signed(v) < 256:
            return str(_signed(v))
        return "0x%X" % (v & MASK64)

    def evaluate(self, args, read) -> int:
        return self.value & MASK64


@dataclasses.dataclass(frozen=True)
class Load(Expr):
    address: Expr
    size: int
    signed: bool

    def render(self) -> str:
        kind = {1: "s8" if self.signed else "u8",
                2: "s16" if self.signed else "u16",
                4: "s32" if self.signed else "u32",
                8: "u64", 16: "u128"}[self.size]
        return "*(%s*)(%s)" % (kind, self.address.render())

    def evaluate(self, args, read) -> int:
        return read(self.address.evaluate(args, read) & MASK32, self.size, self.signed)


# C operator precedence, tighter binding first. Rendering *must* respect this:
# `arg1 << 2 + arg0` is not `(arg1 << 2) + arg0` in C, it is `arg1 << (2 + arg0)`.
# Emitting unparenthesised text would produce source that disagrees with the tree
# it was derived from, which is the one failure mode this module exists to avoid.
PRECEDENCE = {
    "mul": 10,
    "add": 9, "sub": 9,
    "shl": 8, "shr": 8, "sar": 8,
    "ltu": 6, "lt": 6,
    "and": 4,
    "xor": 3,
    "or": 2,
}
ATOM_PRECEDENCE = 12


def _precedence(expr: "Expr") -> int:
    if isinstance(expr, (Binary, Compare)):
        return PRECEDENCE[expr.op]
    return ATOM_PRECEDENCE


def _wrap(child: "Expr", limit: int) -> str:
    """Render `child`, parenthesising it when its binding is looser than `limit`."""
    text = child.render()
    return "(%s)" % text if _precedence(child) < limit else text


@dataclasses.dataclass(frozen=True)
class Binary(Expr):
    op: str
    left: Expr
    right: Expr

    _SYMBOL = {
        "add": "+", "sub": "-", "mul": "*", "shl": "<<",
        "shr": ">>", "sar": ">>", "and": "&", "or": "|", "xor": "^",
    }

    def render(self) -> str:
        level = PRECEDENCE[self.op]
        # The right operand of a left-associative operator needs a parenthesis at
        # equal precedence too, or `a - (b - c)` would print as `a - b - c`.
        return "%s %s %s" % (
            _wrap(self.left, level),
            self._SYMBOL[self.op],
            _wrap(self.right, level + 1),
        )

    def evaluate(self, args, read) -> int:
        a = self.left.evaluate(args, read)
        b = self.right.evaluate(args, read)
        if self.op == "add":
            return (a + b) & MASK64
        if self.op == "sub":
            return (a - b) & MASK64
        if self.op == "mul":
            return (a * b) & MASK64
        if self.op == "shl":
            return (a << (b & 0x1F)) & MASK64
        if self.op == "shr":
            return (a & MASK32) >> (b & 0x1F)
        if self.op == "sar":
            return (_signed(a) >> (b & 0x1F)) & MASK64
        if self.op == "and":
            return a & b
        if self.op == "or":
            return a | b
        if self.op == "xor":
            return a ^ b
        raise Unsupported("cannot evaluate %r" % self.op)


@dataclasses.dataclass(frozen=True)
class Compare(Expr):
    op: str
    left: Expr
    right: Expr

    def render(self) -> str:
        symbol = {"ltu": "<", "lt": "<"}[self.op]
        level = PRECEDENCE[self.op]
        return "(%s %s %s) ? 1 : 0" % (
            _wrap(self.left, level), symbol, _wrap(self.right, level + 1)
        )

    def evaluate(self, args, read) -> int:
        a = self.left.evaluate(args, read)
        b = self.right.evaluate(args, read)
        if self.op == "ltu":
            return 1 if (a & MASK64) < (b & MASK64) else 0
        return 1 if _signed(a) < _signed(b) else 0


def _signed(value: int) -> int:
    value &= MASK64
    return value - (1 << 64) if value & (1 << 63) else value


def _sign32(value: int) -> int:
    value &= MASK32
    return value - (1 << 32) if value & 0x80000000 else value


def simplify(expr: Expr) -> Expr:
    """Fold constants and drop identities, so the output reads like source."""
    if isinstance(expr, Binary):
        left, right = simplify(expr.left), simplify(expr.right)
        if isinstance(left, Const) and isinstance(right, Const):
            folded = Binary(expr.op, left, right).evaluate((0, 0, 0, 0), None)
            return Const(folded)
        # x + 0, x | 0, x << 0 and friends add nothing.
        if isinstance(right, Const) and right.value == 0 and expr.op in (
            "add", "sub", "or", "xor", "shl", "shr", "sar"
        ):
            return left
        if isinstance(left, Const) and left.value == 0 and expr.op in ("add", "or", "xor"):
            return right
        return Binary(expr.op, left, right)
    if isinstance(expr, Load):
        return Load(simplify(expr.address), expr.size, expr.signed)
    if isinstance(expr, Compare):
        return Compare(expr.op, simplify(expr.left), simplify(expr.right))
    return expr


# -- the symbolic machine ------------------------------------------------

_LOAD_SIZES = {
    "lb": (1, True), "lbu": (1, False),
    "lh": (2, True), "lhu": (2, False),
    "lw": (4, True), "lwu": (4, False),
    "ld": (8, False), "lq": (16, False),
}


@dataclasses.dataclass
class Derivation:
    address: int
    expression: Expr
    instructions: int

    def render(self) -> str:
        return "return %s;" % simplify(self.expression).render()

    def evaluate(self, args, read) -> int:
        return _sign32(simplify(self.expression).evaluate(args, read) & MASK32)


def derive(image: bytes, base: int, start: int, end: int) -> Derivation:
    """Derive the return expression of a branch-free, store-free function."""
    regs = {0: Const(0)}
    for i in range(4):
        regs[4 + i] = Arg(i)

    def get(index: int) -> Expr:
        if index == 0:
            return Const(0)
        if index not in regs:
            raise Unsupported("reads uninitialised register $%d" % index)
        return regs[index]

    addr = start
    count = 0
    while addr < end:
        word = struct.unpack_from("<I", image, addr - base)[0]
        ins = DISASM.decode(word, addr)
        count += 1
        m = ins.mnemonic

        if m == "nop" or m == "sync":
            addr += 4
            continue

        if m == "jr":
            if not ins.is_return:
                raise Unsupported("indirect jump")
            # The delay slot still executes, so fold it in before finishing.
            slot_addr = addr + 4
            if slot_addr < end:
                slot_word = struct.unpack_from("<I", image, slot_addr - base)[0]
                slot = DISASM.decode(slot_word, slot_addr)
                if slot.mnemonic not in ("nop", "sync"):
                    _apply(regs, slot, get)
            return Derivation(start, get(2), count + 1)

        if ins.is_branch or ins.is_jump:
            raise Unsupported("control flow: %s" % m)
        if ins.flags & 0x40:  # Flags.STORE
            raise Unsupported("writes memory")

        _apply(regs, ins, get)
        addr += 4

    raise Unsupported("no return reached")


def _apply(regs: dict, ins, get) -> None:
    """Apply one instruction to the symbolic register state."""
    m = ins.mnemonic

    if m in _LOAD_SIZES:
        size, signed = _LOAD_SIZES[m]
        address = simplify(Binary("add", get(ins.rs), Const(ins.imm & MASK64)))
        regs[ins.rt] = Load(address, size, signed)
        return

    if m == "lui":
        regs[ins.rt] = Const((ins.imm & 0xFFFF) << 16)
        return
    if m in ("addiu", "addi", "daddiu", "daddi"):
        regs[ins.rt] = Binary("add", get(ins.rs), Const(ins.imm & MASK64))
        return
    if m == "andi":
        regs[ins.rt] = Binary("and", get(ins.rs), Const(ins.imm & 0xFFFF))
        return
    if m == "ori":
        regs[ins.rt] = Binary("or", get(ins.rs), Const(ins.imm & 0xFFFF))
        return
    if m == "xori":
        regs[ins.rt] = Binary("xor", get(ins.rs), Const(ins.imm & 0xFFFF))
        return
    if m == "sltiu":
        regs[ins.rt] = Compare("ltu", get(ins.rs), Const(ins.imm & MASK64))
        return
    if m == "slti":
        regs[ins.rt] = Compare("lt", get(ins.rs), Const(ins.imm & MASK64))
        return

    if m in ("addu", "add", "daddu", "dadd"):
        regs[ins.rd] = Binary("add", get(ins.rs), get(ins.rt))
        return
    if m in ("subu", "sub", "dsubu", "dsub"):
        regs[ins.rd] = Binary("sub", get(ins.rs), get(ins.rt))
        return
    if m == "and":
        regs[ins.rd] = Binary("and", get(ins.rs), get(ins.rt))
        return
    if m == "or":
        regs[ins.rd] = Binary("or", get(ins.rs), get(ins.rt))
        return
    if m == "xor":
        regs[ins.rd] = Binary("xor", get(ins.rs), get(ins.rt))
        return
    if m == "sltu":
        regs[ins.rd] = Compare("ltu", get(ins.rs), get(ins.rt))
        return
    if m == "slt":
        regs[ins.rd] = Compare("lt", get(ins.rs), get(ins.rt))
        return

    if m in ("sll", "dsll"):
        regs[ins.rd] = Binary("shl", get(ins.rt), Const(ins.imm))
        return
    if m in ("srl", "dsrl"):
        regs[ins.rd] = Binary("shr", get(ins.rt), Const(ins.imm))
        return
    if m in ("sra", "dsra"):
        regs[ins.rd] = Binary("sar", get(ins.rt), Const(ins.imm))
        return

    raise Unsupported("no symbolic rule for %s" % m)
