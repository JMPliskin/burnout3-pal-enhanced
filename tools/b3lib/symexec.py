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
        if _LOAD_RENDERER is not None:
            return _LOAD_RENDERER(self, lambda node: node.render())
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
    # Relational and equality operators, for branch conditions.
    "eq": 5, "ne": 5, "le": 6, "gt": 6, "ge": 6,
    "and": 4,
    "xor": 3,
    "or": 2,
}
ATOM_PRECEDENCE = 12


def _precedence(expr: "Expr") -> int:
    if isinstance(expr, (Binary, Compare, Predicate)):
        return PRECEDENCE[expr.op]
    return ATOM_PRECEDENCE


# How a Load is rendered varies by consumer: the default prints a pointer cast,
# while the C++ emitter prints a Read* helper call. Everything *else* about
# rendering - precedence, parenthesisation - must not vary, so it lives here once
# and the Load form is the only hook. An earlier version duplicated the whole
# renderer in the emitter, and the two drifted: a clarity rule added here never
# reached the generated source.
_LOAD_RENDERER = None


def set_load_renderer(renderer):
    """Install a custom Load renderer; returns the previous one.

    `renderer(node, render_child)` returns the text for a Load node, where
    `render_child(expr)` renders a subexpression at lowest precedence.
    """
    global _LOAD_RENDERER
    previous = _LOAD_RENDERER
    _LOAD_RENDERER = renderer
    return previous


class load_renderer:
    """Context manager form of `set_load_renderer`."""

    def __init__(self, renderer):
        self._renderer = renderer
        self._previous = None

    def __enter__(self):
        self._previous = set_load_renderer(self._renderer)
        return self

    def __exit__(self, *exc):
        set_load_renderer(self._previous)
        return False


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
        left = _wrap(self.left, level)
        # `a - b << 4` is valid C and means `(a - b) << 4`, but almost nobody reads
        # it that way, so an arithmetic operand of a shift gets parenthesised even
        # though precedence does not require it. Redundant parentheses cannot change
        # meaning; a misread can.
        # `_wrap` leaves an add/sub unparenthesised here because precedence allows
        # it, so wrapping unconditionally cannot double up. Testing the rendered
        # text for a leading "(" would be wrong: `(a << 3) - b` starts with one
        # without being parenthesised as a whole.
        if self.op in ("shl", "shr", "sar") and isinstance(self.left, Binary):
            if self.left.op in ("add", "sub"):
                left = "(%s)" % left
        # The right operand of a left-associative operator needs a parenthesis at
        # equal precedence too, or `a - (b - c)` would print as `a - b - c`.
        return "%s %s %s" % (left, self._SYMBOL[self.op], _wrap(self.right, level + 1))

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


@dataclasses.dataclass(frozen=True)
class Predicate(Expr):
    """A branch condition, evaluated with signed comparison as MIPS does."""

    op: str  # eq ne lt le gt ge
    left: Expr
    right: Expr

    _SYMBOL = {"eq": "==", "ne": "!=", "lt": "<", "le": "<=", "gt": ">", "ge": ">="}

    def render(self) -> str:
        return "%s %s %s" % (
            _wrap(self.left, 7), self._SYMBOL[self.op], _wrap(self.right, 7)
        )

    def evaluate(self, args, read) -> int:
        a = _signed(self.left.evaluate(args, read))
        b = _signed(self.right.evaluate(args, read))
        return {
            "eq": a == b, "ne": a != b, "lt": a < b,
            "le": a <= b, "gt": a > b, "ge": a >= b,
        }[self.op]

    def negate(self) -> "Predicate":
        opposite = {"eq": "ne", "ne": "eq", "lt": "ge", "ge": "lt", "le": "gt", "gt": "le"}
        return Predicate(opposite[self.op], self.left, self.right)


def _signed(value: int) -> int:
    value &= MASK64
    return value - (1 << 64) if value & (1 << 63) else value


def _sign32(value: int) -> int:
    value &= MASK32
    return value - (1 << 32) if value & 0x80000000 else value


def substitute(expr: Expr, arguments) -> Expr:
    """Rewrite a derivation's `Arg` nodes with the caller's actual expressions.

    This is what makes a derived callee usable at a call site: the callee's result
    is expressed in terms of `arg0..arg3`, and inlining it means replacing those
    with whatever the caller had in `$a0..$a3` at the moment of the call.

    `arguments` maps an argument index to an expression. An index with no entry is
    left as-is, which keeps the result honest: if a callee reads an argument the
    caller never set, that shows up as a stray `argN` rather than silently becoming
    zero.
    """
    if isinstance(expr, Arg):
        return arguments.get(expr.index, expr)
    if isinstance(expr, Load):
        return Load(substitute(expr.address, arguments), expr.size, expr.signed)
    if isinstance(expr, Binary):
        return Binary(
            expr.op,
            substitute(expr.left, arguments),
            substitute(expr.right, arguments),
        )
    if isinstance(expr, Compare):
        return Compare(
            expr.op,
            substitute(expr.left, arguments),
            substitute(expr.right, arguments),
        )
    if isinstance(expr, Predicate):
        return Predicate(
            expr.op,
            substitute(expr.left, arguments),
            substitute(expr.right, arguments),
        )
    return expr


def simplify(expr: Expr) -> Expr:
    """Fold constants and drop identities, so the output reads like source.

    Every rule here preserves meaning exactly.  The derived code is
    differential-tested against the original, so an over-eager rule shows up as a
    disagreement rather than as quietly wrong source - which is why it is safe to
    simplify for readability at all.
    """
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
        # Collapse a chain of constant offsets: (x + c1) + c2 becomes x + (c1 + c2).
        # These arise constantly from `lui`/`addiu` pairs feeding a further offset,
        # and leaving them unfolded prints addresses as unreadable sums.
        if (
            expr.op == "add"
            and isinstance(right, Const)
            and isinstance(left, Binary)
            and left.op == "add"
            and isinstance(left.right, Const)
        ):
            return simplify(
                Binary("add", left.left, Const((left.right.value + right.value) & MASK64))
            )
        return Binary(expr.op, left, right)
    if isinstance(expr, Load):
        return Load(simplify(expr.address), expr.size, expr.signed)
    if isinstance(expr, Compare):
        return Compare(expr.op, simplify(expr.left), simplify(expr.right))
    if isinstance(expr, Predicate):
        left, right = simplify(expr.left), simplify(expr.right)
        # `slt` followed by `bne ..., $zero` is how a compiler spells a plain
        # comparison, and printing it as `(a < b) ? 1 : 0 != 0` hides that. The
        # 0/1 result of a Compare is never anything else, so testing it against
        # zero is exactly the comparison itself, or its negation.
        if isinstance(left, Compare) and isinstance(right, Const) and right.value == 0:
            inner = Predicate(
                "lt" if left.op in ("lt", "ltu") else left.op, left.left, left.right
            )
            if expr.op == "ne":
                return inner
            if expr.op == "eq":
                return inner.negate()
        return Predicate(expr.op, left, right)
    return expr


# -- the symbolic machine ------------------------------------------------

_LOAD_SIZES = {
    "lb": (1, True), "lbu": (1, False),
    "lh": (2, True), "lhu": (2, False),
    "lw": (4, True), "lwu": (4, False),
    "ld": (8, False), "lq": (16, False),
}


_STORE_SIZES = {"sb": 1, "sh": 2, "sw": 4, "sd": 8, "sq": 16}


@dataclasses.dataclass(frozen=True)
class Store:
    """One memory write a function performs, as a pair of expressions."""

    address: Expr
    value: Expr
    size: int

    def render(self) -> str:
        kind = {1: "u8", 2: "u16", 4: "u32", 8: "u64", 16: "u128"}[self.size]
        return "*(%s*)(%s) = %s;" % (
            kind, simplify(self.address).render(), simplify(self.value).render()
        )


@dataclasses.dataclass
class Derivation:
    address: int
    expression: Expr
    instructions: int
    stores: tuple = ()

    def render(self) -> str:
        lines = [store.render() for store in self.stores]
        lines.append("return %s;" % simplify(self.expression).render())
        return "\n".join(lines)

    def evaluate(self, args, read) -> int:
        return _sign32(simplify(self.expression).evaluate(args, read) & MASK32)

    def evaluate_stores(self, args, read) -> list:
        """Resolve each store to a concrete (address, value, size) triple.

        Addresses and values are evaluated against the state *before* any store is
        applied, which is correct only because a derivation is refused if it ever
        loads from an address it has already written - see `derive`.
        """
        out = []
        for store in self.stores:
            address = simplify(store.address).evaluate(args, read) & MASK32
            value = simplify(store.value).evaluate(args, read)
            out.append((address, value & ((1 << (store.size * 8)) - 1), store.size))
        return out


def derive(image: bytes, base: int, start: int, end: int,
           gp_value: int = None) -> Derivation:
    """Derive the return expression and memory effects of a branch-free function.

    `$gp` is seeded with its startup value, since globals are commonly reached as
    an offset from it and refusing those would exclude a large set of otherwise
    simple functions.
    """
    if gp_value is None:
        from .emu import GP_VALUE

        gp_value = GP_VALUE
    regs = {0: Const(0), 28: Const(gp_value)}
    for i in range(4):
        regs[4 + i] = Arg(i)
    stores = []

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
                if slot.mnemonic in _STORE_SIZES:
                    size = _STORE_SIZES[slot.mnemonic]
                    if size == 16:
                        raise Unsupported("128-bit store")
                    target = simplify(
                        Binary("add", get(slot.rs), Const(slot.imm & MASK64))
                    )
                    stores.append(Store(target, get(slot.rt), size))
                elif slot.mnemonic not in ("nop", "sync"):
                    _apply(regs, slot, get)
            return Derivation(start, get(2), count + 1, tuple(stores))

        if ins.is_branch or ins.is_jump:
            raise Unsupported("control flow: %s" % m)

        if m in _STORE_SIZES:
            size = _STORE_SIZES[m]
            if size == 16:
                # A quadword store needs the upper half of the register, which the
                # expression model does not carry.
                raise Unsupported("128-bit store")
            target = simplify(Binary("add", get(ins.rs), Const(ins.imm & MASK64)))
            stores.append(Store(target, get(ins.rt), size))
            addr += 4
            continue

        if ins.mnemonic in _LOAD_SIZES and stores:
            # Loading after storing would make the result depend on the order the
            # stores were applied, which `evaluate_stores` does not model. Rare
            # enough to refuse rather than complicate.
            raise Unsupported("loads after a store")

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
