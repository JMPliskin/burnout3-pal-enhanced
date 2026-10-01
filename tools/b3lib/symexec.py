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

    def __post_init__(self) -> None:
        # Record the node's size *as a tree* - the size its rendering would have -
        # at construction, from the children's already-recorded sizes. That makes the
        # size free to query, which matters because unrolled loops build expressions
        # that share subtrees: a value used twice per iteration doubles the tree each
        # time round while the underlying graph grows only linearly. Anything that
        # walks such a value as a tree is exponential, and the deriver has to be able
        # to notice that before it walks it. Defined here so every dataclass subclass
        # inherits it; dataclasses call an inherited __post_init__.
        #
        # Named `tree_size`, not `size`: Load already has a field called `size` (the
        # access width), and a base-class `size` attribute becomes that field's
        # default value, which breaks every dataclass declared after it.
        object.__setattr__(
            self,
            "_tree_size",
            min(1 + sum(child.tree_size for child in children(self)), 1 << 30),
        )

    @property
    def tree_size(self) -> int:
        """Node count as a tree, i.e. the size of this expression when printed."""
        return getattr(self, "_tree_size", 1)

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
    "dshl": 8, "dshr": 8, "dsar": 8,
    "ltu": 6, "lt": 6, "geu": 6,
    # Relational and equality operators, for branch conditions.
    "eq": 5, "ne": 5, "le": 6, "gt": 6, "ge": 6,
    "and": 4,
    "xor": 3,
    "or": 2,
}
ATOM_PRECEDENCE = 12


def _precedence(expr: "Expr") -> int:
    # Arithmetic shifts render as a parenthesised cast expression, which is an atom.
    if isinstance(expr, Binary) and expr.op in ("sar", "dsar"):
        return ATOM_PRECEDENCE
    if isinstance(expr, (Binary, Compare, Predicate)):
        return PRECEDENCE[expr.op]
    if isinstance(expr, Select):
        return 1
    return ATOM_PRECEDENCE


# -- C++ value semantics -------------------------------------------------
#
# The rendered text is C++, and it has to mean what the tree means, not merely
# look like it. The tree computes on 64-bit register values, so the rendering
# makes every value a `u64`: arguments are declared `u64`, the Read* helpers
# return the register image of the loaded value (sign- or zero-extended), and
# the operators whose meaning depends on width or signedness carry explicit
# casts. Left to C++'s own promotion rules, `ReadU8(p) << 24` is an `int` shift
# and a signed compare of two `u64`s is unsigned - an earlier version emitted
# exactly that, and only compiling and running the output exposed it.

def fits_u32(expr: "Expr") -> bool:
    """True when the value is provably below 2**32."""
    if isinstance(expr, Const):
        return (expr.value & MASK64) <= MASK32
    if isinstance(expr, Load):
        return not expr.signed and expr.size <= 4
    if isinstance(expr, (Compare, Predicate, FloatPredicate, Not1, FloatOp,
                         IntToFloat, FloatToInt)):
        return True
    if isinstance(expr, Binary):
        if expr.op == "shr":
            return True
        if expr.op == "and":
            return fits_u32(expr.left) or fits_u32(expr.right)
        if expr.op in ("or", "xor"):
            return fits_u32(expr.left) and fits_u32(expr.right)
        if expr.op == "dshr" and isinstance(expr.right, Const):
            return (expr.right.value & 0x3F) >= 32
    if isinstance(expr, Select):
        return fits_u32(expr.when_true) and fits_u32(expr.when_false)
    return False


def nonnegative(expr: "Expr") -> bool:
    """True when the value is below 2**63, so signed and unsigned order agree."""
    if fits_u32(expr):
        return True
    if isinstance(expr, Const):
        return _signed(expr.value) >= 0
    if isinstance(expr, Binary):
        if expr.op == "dshr" and isinstance(expr.right, Const):
            return (expr.right.value & 0x3F) >= 1
        if expr.op == "and":
            return nonnegative(expr.left) or nonnegative(expr.right)
        if expr.op in ("or", "xor"):
            return nonnegative(expr.left) and nonnegative(expr.right)
    return False


def _signed_literal(value: int) -> str:
    """A constant spelled so C++ gives it its signed 64-bit value.

    Negative values are written in decimal on purpose: `-0x80000000` is unsigned
    in C++ (the hex literal is an `unsigned int` and negating it wraps), whereas
    the decimal literal 2147483648 is a `long long`.
    """
    s = _signed(value)
    if s == -(1 << 63):
        return "INT64_MIN"
    if s < 0:
        return "-%d" % -s if s <= -256 else str(s)
    return Const(value).render()


def _signed_operand(expr: "Expr", limit: int) -> str:
    """Render an operand of a signed 64-bit comparison."""
    if isinstance(expr, Const):
        return _signed_literal(expr.value)
    if isinstance(expr, SignExtend32):
        # (s32) already yields the right signed value; widening it to u64 and back
        # would only add noise.
        return "(s32)%s" % _wrap(expr.value, ATOM_PRECEDENCE)
    return "(s64)%s" % _wrap(expr, ATOM_PRECEDENCE)


def _render_order(op: str, left: "Expr", right: "Expr", symbol: str, level: int) -> str:
    """Render a relational comparison with the signedness the tree specifies."""
    if op in ("eq", "ne", "ltu", "geu") or (nonnegative(left) and nonnegative(right)):
        return "%s %s %s" % (_wrap(left, level), symbol, _wrap(right, level + 1))
    return "%s %s %s" % (
        _signed_operand(left, level), symbol, _signed_operand(right, level + 1)
    )


def _shift_amount(amount: "Expr", bits: int) -> str:
    """The shift count, masked as the hardware masks it.

    A C++ shift by the operand width or more is undefined, while the R5900 simply
    uses the low 5 (word) or 6 (doubleword) bits, so a variable count is masked
    explicitly and a constant one is reduced before printing.
    """
    mask = bits - 1
    if isinstance(amount, Const):
        return str(amount.value & mask)
    return "(%s & %d)" % (amount.render(), mask)


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
        "dshl": "<<", "dshr": ">>", "dsar": ">>",
    }

    def render(self) -> str:
        level = PRECEDENCE[self.op]
        if self.op in ("sar", "dsar"):
            # An arithmetic shift needs a signed operand of the right width: `sra`
            # shifts the low word as an s32, `dsra` the whole register as an s64.
            # The result is widened back to the u64 every value is.
            cast, bits = ("s32", 32) if self.op == "sar" else ("s64", 64)
            return "(u64)((%s)%s >> %s)" % (
                cast, _wrap(self.left, ATOM_PRECEDENCE), _shift_amount(self.right, bits)
            )
        if self.op in ("shl", "shr", "dshl", "dshr"):
            bits = 32 if self.op in ("shl", "shr") else 64
            left = _wrap(self.left, level)
            if isinstance(self.left, Const):
                # A bare literal is an `int`, and `1 << 31` on an int overflows.
                left = "%sull" % Const(self.left.value & MASK64).render().lstrip("-")
                if _signed(self.left.value) < 0:
                    left = "0x%Xull" % (self.left.value & MASK64)
            if isinstance(self.left, Binary) and self.left.op in ("add", "sub"):
                left = "(%s)" % left
            if self.op == "shr" and not fits_u32(self.left):
                # `srl` shifts the low word only; the upper half must not shift in.
                left = "(%s & 0xFFFFFFFF)" % self.left.render()
            return "%s %s %s" % (left, self._SYMBOL[self.op], _shift_amount(self.right, bits))
        left = _wrap(self.left, level)
        # `a - b << 4` is valid C and means `(a - b) << 4`, but almost nobody reads
        # it that way, so an arithmetic operand of a shift gets parenthesised even
        # though precedence does not require it. Redundant parentheses cannot change
        # meaning; a misread can.
        # `_wrap` leaves an add/sub unparenthesised here because precedence allows
        # it, so wrapping unconditionally cannot double up. Testing the rendered
        # text for a leading "(" would be wrong: `(a << 3) - b` starts with one
        # without being parenthesised as a whole.
        if self.op in ("shl", "shr", "sar", "dshl", "dshr", "dsar") and isinstance(self.left, Binary):
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
        # Word shifts mask the amount to 5 bits, doubleword shifts to 6. Using the
        # 5-bit mask for both silently turns a shift of 32 into a shift of zero,
        # which is how `dsll32`/`dsra32` and the HI half of a multiply came out
        # wrong - caught by the differential tests rather than by reading.
        if self.op == "shl":
            return (a << (b & 0x1F)) & MASK64
        if self.op == "shr":
            return (a & MASK32) >> (b & 0x1F)
        if self.op == "sar":
            return (_sign32(a) >> (b & 0x1F)) & MASK64
        if self.op == "dshl":
            return (a << (b & 0x3F)) & MASK64
        if self.op == "dshr":
            return (a & MASK64) >> (b & 0x3F)
        if self.op == "dsar":
            return (_signed(a) >> (b & 0x3F)) & MASK64
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
        # `slt`/`sltu` produce 0 or 1 as a register value; the cast keeps it a u64
        # so a later `<< 31` cannot become a signed int overflow.
        return "(u64)(%s)" % _render_order(
            self.op, self.left, self.right, "<", PRECEDENCE[self.op]
        )

    def evaluate(self, args, read) -> int:
        a = self.left.evaluate(args, read)
        b = self.right.evaluate(args, read)
        if self.op == "ltu":
            return 1 if (a & MASK64) < (b & MASK64) else 0
        return 1 if _signed(a) < _signed(b) else 0


# -- floating point ------------------------------------------------------
#
# Every expression in this model evaluates to an integer *bit pattern*, floats
# included. That mirrors the hardware - a float register holds 32 bits, and
# `mfc1`/`swc1` move those bits around without interpreting them - and it keeps
# evaluation uniform, with reinterpretation confined to the float operations
# themselves rather than smeared across the whole tree.

def _bits_to_float(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & MASK32))[0]


# The largest finite single-precision magnitude, bit pattern 0x7F7FFFFF.
FLOAT_MAX = struct.unpack("<f", struct.pack("<I", 0x7F7FFFFF))[0]


def _float_to_bits(value: float) -> int:
    """Encode a result the way the EE's FPU would.

    That FPU is deliberately not IEEE-754: no infinities, no NaNs, and an overflow
    saturates to the largest representable magnitude. This must match `_f32` in
    emu.py exactly - the two are compared against each other on every derivation,
    so any divergence registers as a disagreement.
    """
    if value != value:
        return _float_to_bits(0.0)
    if value > FLOAT_MAX:
        value = FLOAT_MAX
    elif value < -FLOAT_MAX:
        value = -FLOAT_MAX
    return struct.unpack("<I", struct.pack("<f", value))[0]


def _float_operand(expr: "Expr") -> str:
    """Render a float operand; a constant becomes a float literal.

    `FloatBits(-1.0f)` says what `0xBF800000` means. The literal is the shortest
    decimal that converts back to exactly the same bits, so nothing is lost; a
    NaN or infinity pattern has no literal and stays hexadecimal.
    """
    if not isinstance(expr, Const):
        return expr.render()
    bits = expr.value & MASK32
    value = _bits_to_float(bits)
    if value != value or value in (float("inf"), float("-inf")):
        return "0x%08X" % bits
    for digits in range(1, 10):
        text = "%.*g" % (digits, value)
        if struct.unpack("<I", struct.pack("<f", float(text)))[0] == bits:
            break
    if not any(c in text for c in ".en"):
        text += ".0"
    return "FloatBits(%sf)" % text


@dataclasses.dataclass(frozen=True)
class FloatOp(Expr):
    """A single-precision arithmetic operation over bit-pattern operands."""

    op: str
    left: Expr
    right: Expr = None   # None for the unary forms

    _NAMES = {
        "add": "FloatAdd", "sub": "FloatSub", "mul": "FloatMul", "div": "FloatDiv",
        "min": "FloatMin", "max": "FloatMax",
        "neg": "FloatNeg", "abs": "FloatAbs", "sqrt": "FloatSqrt", "mov": "FloatMove",
    }

    def render(self) -> str:
        name = self._NAMES[self.op]
        if self.right is None:
            return "%s(%s)" % (name, _float_operand(self.left))
        return "%s(%s, %s)" % (name, _float_operand(self.left), _float_operand(self.right))

    def evaluate(self, args, read) -> int:
        a = _bits_to_float(self.left.evaluate(args, read))
        if self.right is None:
            result = {
                "neg": lambda x: -x,
                "abs": abs,
                "mov": lambda x: x,
                "sqrt": lambda x: x ** 0.5 if x >= 0 else 0.0,
            }[self.op](a)
            return _float_to_bits(result)
        b = _bits_to_float(self.right.evaluate(args, read))
        if self.op == "div":
            # The FPU flags division by zero rather than trapping; the interpreter
            # yields zero, and the two must agree or every such function would
            # register as a disagreement.
            result = 0.0 if b == 0.0 else a / b
        else:
            result = {
                "add": lambda x, y: x + y,
                "sub": lambda x, y: x - y,
                "mul": lambda x, y: x * y,
                "min": min,
                "max": max,
            }[self.op](a, b)
        return _float_to_bits(result)


@dataclasses.dataclass(frozen=True)
class IntToFloat(Expr):
    """`cvt.s.w`: reinterpret the operand's bits as an integer, convert to float."""

    value: Expr

    def render(self) -> str:
        if isinstance(self.value, Const):
            return "FloatFromInt(%d)" % _sign32(self.value.value)
        return "FloatFromInt(%s)" % self.value.render()

    def evaluate(self, args, read) -> int:
        return _float_to_bits(float(_sign32(self.value.evaluate(args, read))))


@dataclasses.dataclass(frozen=True)
class FloatToInt(Expr):
    """`cvt.w.s`: convert a float to an integer, truncating toward zero."""

    value: Expr

    def render(self) -> str:
        return "IntFromFloat(%s)" % _float_operand(self.value)

    def evaluate(self, args, read) -> int:
        return int(_bits_to_float(self.value.evaluate(args, read))) & MASK32


@dataclasses.dataclass(frozen=True)
class FloatPredicate(Expr):
    """A `c.<cond>.s` comparison, as tested later by `bc1t`/`bc1f`."""

    op: str  # eq lt le
    left: Expr
    right: Expr

    _SYMBOL = {"eq": "==", "lt": "<", "le": "<=", "false": "false"}

    def render(self) -> str:
        if self.op == "false":
            return "false"
        return "FloatValue(%s) %s FloatValue(%s)" % (
            _float_operand(self.left), self._SYMBOL[self.op], _float_operand(self.right)
        )

    def evaluate(self, args, read) -> int:
        if self.op == "false":
            return False
        a = _bits_to_float(self.left.evaluate(args, read))
        b = _bits_to_float(self.right.evaluate(args, read))
        return {"eq": a == b, "lt": a < b, "le": a <= b}[self.op]

    def negate(self) -> "Expr":
        return Not1(self)


@dataclasses.dataclass(frozen=True)
class Not1(Expr):
    """Logical negation of a condition, as distinct from a bitwise complement."""

    value: Expr

    def render(self) -> str:
        return "!(%s)" % self.value.render()

    def evaluate(self, args, read) -> int:
        return not self.value.evaluate(args, read)

    def negate(self) -> Expr:
        return self.value


@dataclasses.dataclass(frozen=True)
class SignExtend32(Expr):
    """Narrow to 32 bits and sign-extend, as every 32-bit MIPS result is.

    Needed to model `mult` faithfully: it multiplies the low words of its operands
    as signed values, so the raw 64-bit register contents must not be used.
    """

    value: Expr

    def render(self) -> str:
        return "(u64)(s32)%s" % _wrap(self.value, ATOM_PRECEDENCE)

    def evaluate(self, args, read) -> int:
        return _sign32(self.value.evaluate(args, read)) & MASK64


@dataclasses.dataclass(frozen=True)
class Not(Expr):
    """Bitwise complement, used to express `nor` as `~(a | b)`."""

    value: Expr

    def render(self) -> str:
        return "~%s" % _wrap(self.value, ATOM_PRECEDENCE)

    def evaluate(self, args, read) -> int:
        return ~self.value.evaluate(args, read) & MASK64


@dataclasses.dataclass(frozen=True)
class Select(Expr):
    """A ternary: the shape of a conditional move.

    `movz`/`movn` write their destination only when a condition holds, so the
    result depends on what the destination already contained - which is why the
    previous value is part of the expression rather than discarded.
    """

    condition: Expr
    when_true: Expr
    when_false: Expr

    def render(self) -> str:
        return "%s ? %s : %s" % (
            _wrap(self.condition, 7),
            _wrap(self.when_true, 7),
            _wrap(self.when_false, 7),
        )

    def evaluate(self, args, read) -> int:
        taken = self.condition.evaluate(args, read)
        branch = self.when_true if taken else self.when_false
        return branch.evaluate(args, read) & MASK64


@dataclasses.dataclass(frozen=True)
class Predicate(Expr):
    """A branch condition, evaluated with signed comparison as MIPS does."""

    op: str  # eq ne lt le gt ge, plus ltu geu for an unsigned `sltu` result
    left: Expr
    right: Expr

    _SYMBOL = {"eq": "==", "ne": "!=", "lt": "<", "le": "<=", "gt": ">", "ge": ">=",
               "ltu": "<", "geu": ">="}

    def render(self) -> str:
        return _render_order(self.op, self.left, self.right, self._SYMBOL[self.op], 7)

    def evaluate(self, args, read) -> int:
        a = self.left.evaluate(args, read)
        b = self.right.evaluate(args, read)
        if self.op == "ltu":
            return (a & MASK64) < (b & MASK64)
        if self.op == "geu":
            return (a & MASK64) >= (b & MASK64)
        a, b = _signed(a), _signed(b)
        return {
            "eq": a == b, "ne": a != b, "lt": a < b,
            "le": a <= b, "gt": a > b, "ge": a >= b,
        }[self.op]

    def negate(self) -> "Predicate":
        opposite = {"eq": "ne", "ne": "eq", "lt": "ge", "ge": "lt", "le": "gt", "gt": "le",
                    "ltu": "geu", "geu": "ltu"}
        return Predicate(opposite[self.op], self.left, self.right)


def _signed(value: int) -> int:
    value &= MASK64
    return value - (1 << 64) if value & (1 << 63) else value


def _sign32(value: int) -> int:
    value &= MASK32
    return value - (1 << 32) if value & 0x80000000 else value


def children(expr: Expr) -> tuple:
    """Every subexpression of a node.

    Deliberately fail-closed: an unrecognised node type raises rather than
    returning nothing. Every traversal in the project is built on this, and the
    silent-empty version of it caused a real bug - `Not`, `Select` and
    `SignExtend32` were added without updating the argument scan, so arguments
    nested inside them became invisible and the emitter produced functions
    referencing undeclared parameters. A traversal that cannot see a new node type
    must break loudly, not quietly skip it.
    """
    if isinstance(expr, (Arg, Const)):
        return ()
    if isinstance(expr, Load):
        return (expr.address,)
    if isinstance(expr, (Not, Not1, SignExtend32, IntToFloat, FloatToInt)):
        return (expr.value,)
    if isinstance(expr, Select):
        return (expr.condition, expr.when_true, expr.when_false)
    if isinstance(expr, FloatOp):
        return (expr.left,) if expr.right is None else (expr.left, expr.right)
    if isinstance(expr, (Binary, Compare, Predicate, FloatPredicate)):
        return (expr.left, expr.right)
    raise Unsupported("no traversal rule for %s" % type(expr).__name__)


def rebuild(expr: Expr, new_children) -> Expr:
    """Reconstruct a node with replaced subexpressions, preserving its type."""
    if isinstance(expr, (Arg, Const)):
        return expr
    if isinstance(expr, Load):
        return Load(new_children[0], expr.size, expr.signed)
    if isinstance(expr, Not):
        return Not(new_children[0])
    if isinstance(expr, Not1):
        return Not1(new_children[0])
    if isinstance(expr, SignExtend32):
        return SignExtend32(new_children[0])
    if isinstance(expr, IntToFloat):
        return IntToFloat(new_children[0])
    if isinstance(expr, FloatToInt):
        return FloatToInt(new_children[0])
    if isinstance(expr, Select):
        return Select(*new_children)
    if isinstance(expr, FloatOp):
        if expr.right is None:
            return FloatOp(expr.op, new_children[0])
        return FloatOp(expr.op, *new_children)
    if isinstance(expr, Binary):
        return Binary(expr.op, *new_children)
    if isinstance(expr, Compare):
        return Compare(expr.op, *new_children)
    if isinstance(expr, Predicate):
        return Predicate(expr.op, *new_children)
    if isinstance(expr, FloatPredicate):
        return FloatPredicate(expr.op, *new_children)
    raise Unsupported("no rebuild rule for %s" % type(expr).__name__)


def arguments_used(expr: Expr) -> set:
    """Indices of every `Arg` anywhere in an expression."""
    if isinstance(expr, Arg):
        return {expr.index}
    found = set()
    for child in children(expr):
        found |= arguments_used(child)
    return found


def substitute(expr: Expr, arguments) -> Expr:
    """Rewrite a derivation's `Arg` nodes with the caller's actual expressions.

    This is what makes a derived callee usable at a call site: the callee's result
    is expressed in terms of `arg0..arg3`, and inlining it means replacing those
    with whatever the caller had in `$a0..$a3` at the moment of the call.

    `arguments` maps an argument index to an expression. An index with no entry is
    left as-is, which keeps the result honest: if a callee reads an argument the
    caller never set, that shows up as a stray `argN` rather than silently becoming
    zero.

    Built on `children`/`rebuild` so that a new node type cannot be silently
    skipped - the earlier hand-written version missed three and substituted nothing
    inside them.
    """
    if isinstance(expr, Arg):
        return arguments.get(expr.index, expr)
    kids = children(expr)
    if not kids:
        return expr
    return rebuild(expr, [substitute(child, arguments) for child in kids])


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
    if isinstance(expr, (FloatOp, IntToFloat, FloatToInt, FloatPredicate, Not1,
                         Not, Select, SignExtend32)):
        kids = children(expr)
        return rebuild(expr, [simplify(child) for child in kids]) if kids else expr
    if isinstance(expr, Predicate):
        left, right = simplify(expr.left), simplify(expr.right)
        # `slt` followed by `bne ..., $zero` is how a compiler spells a plain
        # comparison, and printing it as `(a < b) ? 1 : 0 != 0` hides that. The
        # 0/1 result of a Compare is never anything else, so testing it against
        # zero is exactly the comparison itself, or its negation.
        if isinstance(left, Compare) and isinstance(right, Const) and right.value == 0:
            # The Compare's signedness carries over. An earlier version mapped `ltu`
            # to a signed `lt` here, which the tests missed because they only ever
            # passed small positive pointers.
            inner = Predicate(left.op, left.left, left.right)
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

_FLOAT_BINARY = {
    "add.s": "add", "sub.s": "sub", "mul.s": "mul", "div.s": "div",
    "max.s": "max", "min.s": "min",
}
_FLOAT_UNARY = {"mov.s": "mov", "neg.s": "neg", "abs.s": "abs", "sqrt.s": "sqrt"}
_FLOAT_COMPARE = {"c.eq.s": "eq", "c.lt.s": "lt", "c.le.s": "le", "c.f.s": "false"}

# The multiplier result registers. They are kept in the same dict as the general
# registers under indices the hardware cannot name, which avoids threading a second
# piece of state through every caller.
REG_HI = 64
REG_LO = 65

# Float registers and the FPU condition flag share the register dict under indices
# the integer unit cannot name, so no extra state has to be threaded through every
# caller. The condition flag holds a predicate expression, which is what `bc1t` and
# `bc1f` then branch on.
FPR_BASE = 128
FP_COND = 127


def fpr(index: int) -> int:
    return FPR_BASE + index


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
        # The result is a 32-bit value sign-extended into the 64-bit register, which
        # matters as soon as anything manipulates it 64 bits wide: `lui 0xFFFF`
        # followed by `dsrl32 ..., 32` yields 0xFFFFFFFF, not 0. Dropping the
        # sign-extension produced exactly that wrong mask.
        regs[ins.rt] = Const(_sign32((ins.imm & 0xFFFF) << 16) & MASK64)
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

    if m == "sll":
        regs[ins.rd] = Binary("shl", get(ins.rt), Const(ins.imm))
        return
    if m == "srl":
        regs[ins.rd] = Binary("shr", get(ins.rt), Const(ins.imm))
        return
    if m == "sra":
        regs[ins.rd] = Binary("sar", get(ins.rt), Const(ins.imm))
        return
    if m == "dsll":
        regs[ins.rd] = Binary("dshl", get(ins.rt), Const(ins.imm))
        return
    if m == "dsrl":
        regs[ins.rd] = Binary("dshr", get(ins.rt), Const(ins.imm))
        return
    if m == "dsra":
        regs[ins.rd] = Binary("dsar", get(ins.rt), Const(ins.imm))
        return
    # The 32-prefixed forms add 32 to the shift amount.
    if m == "dsll32":
        regs[ins.rd] = Binary("dshl", get(ins.rt), Const(ins.imm + 32))
        return
    if m == "dsrl32":
        regs[ins.rd] = Binary("dshr", get(ins.rt), Const(ins.imm + 32))
        return
    if m == "dsra32":
        regs[ins.rd] = Binary("dsar", get(ins.rt), Const(ins.imm + 32))
        return
    # Variable shifts take their amount from a register.
    if m == "sllv":
        regs[ins.rd] = Binary("shl", get(ins.rt), get(ins.rs))
        return
    if m == "srlv":
        regs[ins.rd] = Binary("shr", get(ins.rt), get(ins.rs))
        return
    if m == "srav":
        regs[ins.rd] = Binary("sar", get(ins.rt), get(ins.rs))
        return
    if m == "dsllv":
        regs[ins.rd] = Binary("dshl", get(ins.rt), get(ins.rs))
        return
    if m == "dsrlv":
        regs[ins.rd] = Binary("dshr", get(ins.rt), get(ins.rs))
        return
    if m == "dsrav":
        regs[ins.rd] = Binary("dsar", get(ins.rt), get(ins.rs))
        return

    if m == "nor":
        regs[ins.rd] = Not(Binary("or", get(ins.rs), get(ins.rt)))
        return

    # Conditional moves leave the destination untouched when the condition fails,
    # so the previous value is part of the result. Without it there is nothing to
    # express, hence the refusal rather than a guess.
    if m in ("movz", "movn"):
        if ins.rd not in regs:
            raise Unsupported("conditional move over an uninitialised destination")
        op = "eq" if m == "movz" else "ne"
        regs[ins.rd] = Select(
            Predicate(op, get(ins.rt), Const(0)), get(ins.rs), regs[ins.rd]
        )
        return

    # Multiply and divide deposit their results in HI/LO, which `mfhi`/`mflo` then
    # read. Modelling the pair keeps the common "multiply then mflo" sequence
    # derivable.
    if m == "mult":
        # A signed 32x32 -> 64 multiply. Multiplying the raw 64-bit register values
        # would be wrong whenever an operand has its high bits set, so both are
        # narrowed to 32 bits and sign-extended first. LO holds the low word
        # sign-extended; HI the high word.
        product = Binary("mul", SignExtend32(get(ins.rs)), SignExtend32(get(ins.rt)))
        regs[REG_LO] = SignExtend32(product)
        regs[REG_HI] = SignExtend32(Binary("dsar", product, Const(32)))
        if ins.rd:
            regs[ins.rd] = regs[REG_LO]
        return
    if m == "multu":
        # The unsigned form needs a zero-extending narrow, which the expression set
        # does not have; refuse rather than reuse the signed one.
        raise Unsupported("unsigned multiply")
    if m in ("div", "divu"):
        # Division by zero leaves HI/LO undefined on the hardware, so a derivation
        # would not be faithful for every input. Refuse rather than describe only
        # the well-defined half.
        raise Unsupported("division: result undefined for a zero divisor")
    # -- coprocessor 1 (single precision) ---------------------------------
    #
    # COP1 R-format packs its registers differently from the integer unit, and the
    # disassembler does not populate rs/rt/rd for those forms, so the fields come
    # straight from the encoding.
    if m in _FLOAT_BINARY:
        fd, fs, ft = (ins.raw >> 6) & 0x1F, (ins.raw >> 11) & 0x1F, (ins.raw >> 16) & 0x1F
        regs[fpr(fd)] = FloatOp(_FLOAT_BINARY[m], get(fpr(fs)), get(fpr(ft)))
        return
    if m in _FLOAT_UNARY:
        fd, fs = (ins.raw >> 6) & 0x1F, (ins.raw >> 11) & 0x1F
        regs[fpr(fd)] = FloatOp(_FLOAT_UNARY[m], get(fpr(fs)))
        return
    if m == "cvt.s.w":
        fd, fs = (ins.raw >> 6) & 0x1F, (ins.raw >> 11) & 0x1F
        regs[fpr(fd)] = IntToFloat(get(fpr(fs)))
        return
    if m == "cvt.w.s":
        fd, fs = (ins.raw >> 6) & 0x1F, (ins.raw >> 11) & 0x1F
        regs[fpr(fd)] = FloatToInt(get(fpr(fs)))
        return
    if m in _FLOAT_COMPARE:
        fs, ft = (ins.raw >> 11) & 0x1F, (ins.raw >> 16) & 0x1F
        regs[FP_COND] = FloatPredicate(_FLOAT_COMPARE[m], get(fpr(fs)), get(fpr(ft)))
        return
    if m == "mtc1":
        regs[fpr(ins.rd)] = get(ins.rt)
        return
    if m == "mfc1":
        regs[ins.rt] = SignExtend32(get(fpr(ins.rd)))
        return

    if m == "mfhi":
        regs[ins.rd] = get(REG_HI)
        return
    if m == "mflo":
        regs[ins.rd] = get(REG_LO)
        return
    if m == "mthi":
        regs[REG_HI] = get(ins.rs)
        return
    if m == "mtlo":
        regs[REG_LO] = get(ins.rs)
        return

    raise Unsupported("no symbolic rule for %s" % m)
