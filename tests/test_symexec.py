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

    def test_arithmetic_inside_shift_is_parenthesised_for_clarity(self):
        """`a + 4 << 2` is valid C meaning `(a + 4) << 2`, but reads wrong.

        Precedence does not require the parentheses here - `+` binds tighter than
        `<<` - so these are deliberately redundant. Redundant parentheses cannot
        change meaning, whereas a misreading can, and this form appears constantly
        in derived index arithmetic.
        """
        expr = Binary("shl", Binary("add", Arg(0), Const(4)), Const(2))
        self.assertEqual(expr.render(), "(arg0 + 4) << 2")

    def test_nested_shift_inside_subtraction_inside_shift(self):
        """The case that exposed a bad guard.

        An earlier version skipped wrapping when the rendered left operand already
        started with "(" - but `(a << 3) - b` starts with a parenthesis without
        being parenthesised as a whole, so the outer shift was left ambiguous.
        """
        inner = Binary("sub", Binary("shl", Arg(0), Const(3)), Arg(1))
        expr = Binary("shl", inner, Const(4))
        self.assertEqual(expr.render(), "((arg0 << 3) - arg1) << 4")

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




class TestEmitterSelfCheck(unittest.TestCase):
    """The generated source must not reference undeclared parameters.

    An earlier version of the emitter built each signature from the return
    expression only, so arguments used solely inside a store went undeclared and
    the output could not compile. Nothing caught it until a human read the file,
    which is not a check.
    """

    def setUp(self):
        from derive import check_signatures

        self.check = check_signatures

    def test_rejects_an_undeclared_argument(self):
        bad = "s32 func_00ABCDEF(u32 arg0) {\n    return arg1 + arg0;\n}\n"
        with self.assertRaises(SystemExit):
            self.check(bad)

    def test_accepts_a_complete_signature(self):
        good = "s32 func_00ABCDEF(u32 arg0, u32 arg1) {\n    return arg1 + arg0;\n}\n"
        self.check(good)

    def test_accepts_a_function_with_no_arguments(self):
        self.check("s32 func_00ABCDEF(void) {\n    return 1;\n}\n")

    def test_checks_store_bodies_too(self):
        bad = (
            "s32 func_00ABCDEF(u32 arg0) {\n"
            "    const u32 addr0 = arg0 + 8;\n"
            "    const u64 val0 = arg2;\n"
            "    Write32(addr0, val0);\n"
            "    return 0;\n"
            "}\n"
        )
        with self.assertRaises(SystemExit):
            self.check(bad)

class TestRendererIsNotDuplicated(unittest.TestCase):
    """The emitter must reuse the one renderer, not keep a copy of it.

    derive.py originally reimplemented the precedence logic so it could print
    Read* helper calls instead of pointer casts. The two copies drifted: a clarity
    rule added to symexec never reached the generated source, so the emitted C
    differed from what these tests were checking. The Load form is now the only
    thing the emitter overrides, and this pins that down.
    """

    def setUp(self):
        from derive import render_cpp

        self.render_cpp = render_cpp

    def _sample(self):
        # Deliberately exercises the clarity rule and a nested load.
        inner = Binary("sub", Binary("shl", Arg(0), Const(3)), Arg(1))
        return Load(Binary("shl", inner, Const(2)), 4, True)

    def test_only_the_load_form_differs(self):
        expr = self._sample()
        default = expr.render()
        emitted = self.render_cpp(expr)
        self.assertIn("*(s32*)", default)
        self.assertIn("ReadS32", emitted)
        # Strip the differing load syntax; the remaining structure must be identical.
        self.assertEqual(
            default.replace("*(s32*)(", "<L>").replace(")", "", 1),
            emitted.replace("ReadS32(", "<L>").replace(")", "", 1),
        )

    def test_emitter_keeps_the_clarity_parentheses(self):
        """The rule added in symexec has to show up in the generated source."""
        emitted = self.render_cpp(self._sample())
        self.assertIn("((arg0 << 3) - arg1) << 2", emitted)

    def test_load_renderer_is_restored_afterwards(self):
        """The hook must not leak, or later default rendering changes silently."""
        expr = self._sample()
        self.render_cpp(expr)
        self.assertIn("*(s32*)", expr.render())


if __name__ == "__main__":
    unittest.main(verbosity=2)
