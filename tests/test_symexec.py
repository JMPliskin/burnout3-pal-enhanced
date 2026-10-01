"""Tests for symbolic derivation of straight-line functions.

The precedence tests matter most.  An expression tree evaluates correctly
regardless of how it prints, so a rendering bug produces C that silently disagrees
with the tree it came from - which is exactly the failure this module exists to
avoid.  `arg << 2 + base` is not `(arg << 2) + base` in C.
"""

import os
import struct
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

from b3lib.symexec import (
    Arg,
    Binary,
    Compare,
    Const,
    Load,
    Unsupported,
    derive,
    simplify,
)

BASE = 0x00100000


def assemble(words) -> bytes:
    return struct.pack("<%dI" % len(words), *words)


def enc_i(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def enc_r(op, rs, rt, rd, sa, funct):
    return (op << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct


JR_RA = enc_r(0, 31, 0, 0, 0, 0x08)


class TestRenderingPrecedence(unittest.TestCase):
    def test_shift_inside_addition_is_parenthesised(self):
        """`(a << 2) + b` must not print as `a << 2 + b`.

        In C, `+` binds tighter than `<<`, so the unparenthesised form means
        `a << (2 + b)` - a different expression entirely.
        """
        expr = Binary("add", Binary("shl", Arg(1), Const(2)), Arg(0))
        self.assertEqual(expr.render(), "(arg1 << 2) + arg0")

    def test_addition_inside_shift_needs_no_parentheses(self):
        # `+` already binds tighter than `<<`, so this one is unambiguous.
        expr = Binary("shl", Binary("add", Arg(0), Const(4)), Const(2))
        self.assertEqual(expr.render(), "arg0 + 4 << 2")

    def test_right_operand_of_subtraction_is_parenthesised(self):
        """`a - (b - c)` must keep its parentheses, or it becomes `a - b - c`."""
        expr = Binary("sub", Arg(0), Binary("sub", Arg(1), Arg(2)))
        self.assertEqual(expr.render(), "arg0 - (arg1 - arg2)")

    def test_or_of_ands_needs_no_parentheses(self):
        # `&` binds tighter than `|`.
        expr = Binary(
            "or",
            Binary("and", Arg(0), Const(0xFF)),
            Binary("and", Arg(1), Const(0xFF00)),
        )
        self.assertEqual(expr.render(), "arg0 & 255 | arg1 & 0xFF00")

    def test_and_of_ors_is_parenthesised(self):
        expr = Binary(
            "and", Binary("or", Arg(0), Arg(1)), Binary("or", Arg(2), Arg(3))
        )
        self.assertEqual(expr.render(), "(arg0 | arg1) & (arg2 | arg3)")


class TestEvaluation(unittest.TestCase):
    def _read(self, addr, size, signed):
        # A predictable stand-in: each address reads back as itself.
        value = addr & ((1 << (size * 8)) - 1)
        if signed and value & (1 << (size * 8 - 1)):
            value -= 1 << (size * 8)
        return value

    def test_evaluation_follows_the_tree_not_the_text(self):
        expr = Binary("add", Binary("shl", Arg(1), Const(2)), Arg(0))
        # (3 << 2) + 100 = 112
        self.assertEqual(expr.evaluate((100, 3, 0, 0), self._read), 112)

    def test_load_uses_the_reader(self):
        expr = Load(Binary("add", Arg(0), Const(8)), 4, False)
        self.assertEqual(expr.evaluate((0x1000, 0, 0, 0), self._read), 0x1008)

    def test_compare_is_unsigned_for_ltu(self):
        expr = Compare("ltu", Arg(0), Arg(1))
        self.assertEqual(expr.evaluate((1, 2, 0, 0), self._read), 1)
        self.assertEqual(expr.evaluate((2, 1, 0, 0), self._read), 0)


class TestSimplify(unittest.TestCase):
    def test_folds_constants(self):
        self.assertEqual(simplify(Binary("add", Const(2), Const(3))).render(), "5")

    def test_drops_additive_identity(self):
        self.assertEqual(simplify(Binary("add", Arg(0), Const(0))).render(), "arg0")

    def test_keeps_real_work(self):
        expr = simplify(Binary("add", Arg(0), Const(8)))
        self.assertEqual(expr.render(), "arg0 + 8")


class TestDerive(unittest.TestCase):
    def test_simple_field_load(self):
        # lw $v0, 8($a0) ; jr $ra ; nop
        words = [enc_i(0x23, 4, 2, 8), JR_RA, 0]
        image = assemble(words)
        result = derive(image, BASE, BASE, BASE + len(image))
        self.assertEqual(simplify(result.expression).render(), "*(s32*)(arg0 + 8)")

    def test_indexed_load_keeps_precedence(self):
        # sll $v0, $a1, 2 ; addu $v0, $v0, $a0 ; jr $ra ; lw $v0, 8($v0)
        words = [
            enc_r(0, 0, 5, 2, 2, 0x00),
            enc_r(0, 2, 4, 2, 0, 0x21),
            JR_RA,
            enc_i(0x23, 2, 2, 8),
        ]
        image = assemble(words)
        result = derive(image, BASE, BASE, BASE + len(image))
        rendered = simplify(result.expression).render()
        # The shift must stay grouped, or the C means something else.
        self.assertEqual(rendered, "*(s32*)((arg1 << 2) + arg0 + 8)")

    def test_delay_slot_after_jr_is_included(self):
        """The instruction in the return's delay slot still executes."""
        words = [JR_RA, enc_i(0x09, 4, 2, 5)]  # jr $ra ; addiu $v0, $a0, 5
        image = assemble(words)
        result = derive(image, BASE, BASE, BASE + len(image))
        self.assertEqual(simplify(result.expression).render(), "arg0 + 5")

    def test_refuses_a_branch(self):
        words = [enc_i(0x04, 4, 5, 2), 0, JR_RA, 0]  # beq
        with self.assertRaises(Unsupported):
            derive(assemble(words), BASE, BASE, BASE + 16)

    def test_refuses_a_store(self):
        words = [enc_i(0x2B, 4, 5, 0), JR_RA, 0]  # sw
        with self.assertRaises(Unsupported):
            derive(assemble(words), BASE, BASE, BASE + 12)

    def test_refuses_an_uninitialised_register(self):
        """Reading $s0 without setting it means the frame matters; refuse it."""
        words = [enc_r(0, 16, 0, 2, 0, 0x21), JR_RA, 0]  # addu $v0, $s0, $zero
        with self.assertRaises(Unsupported):
            derive(assemble(words), BASE, BASE, BASE + 12)


if __name__ == "__main__":
    unittest.main(verbosity=2)
