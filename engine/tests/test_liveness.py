"""Folding single-use temporaries: what may move, and what must not."""

import os
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path[:0] = [ROOT, os.path.join(ROOT, "tools")]

from ps2decomp import liveness

SYNCED = {"v0", "v1", "a0", "a1", "t0", "t1"}
RETURNED = {"v0", "v1"}


def fold(code, condition=None, live=()):
    return liveness.fold(code, condition, set(live), SYNCED, RETURNED)


class TestFold(unittest.TestCase):
    def test_a_temporary_folds_into_the_condition_that_uses_it(self):
        code, condition = fold(["v0 = ReadS32((u32)a0 + 0x20u);", "v0 = (u64)((s64)a1 < (s64)v0);"],
                               "v0 != 0ull")
        self.assertEqual(code, [])
        self.assertIn("ReadS32((u32)a0 + 0x20u)", condition)

    def test_a_live_result_is_kept(self):
        code, _ = fold(["v0 = ReadS32((u32)a0);", "v0 = v0 + 1;"], live={"v0"})
        self.assertEqual(code, ["v0 = (ReadS32((u32)a0)) + 1;"])

    def test_a_load_is_not_moved_past_a_store(self):
        code, _ = fold(["t0 = ReadS32((u32)a0);", "Write32((u32)a0, 5u);", "v0 = t0;"])
        self.assertEqual(code[0], "t0 = ReadS32((u32)a0);")

    def test_nothing_moves_into_or_past_a_call(self):
        code, _ = fold(["t0 = a0 & 1;", "out(); Call(c, 0x100u); in();", "v0 = v0 + t0;"])
        self.assertEqual(code[0], "t0 = a0 & 1;")

    def test_an_operand_redefined_in_between_blocks_the_fold(self):
        code, _ = fold(["t0 = a0 + 1;", "a0 = 7;", "v0 = t0;"], live={"a0"})
        self.assertEqual(code[0], "t0 = a0 + 1;")

    def test_two_uses_keep_the_definition(self):
        code, _ = fold(["t0 = a0 + 1;", "v0 = t0 + t0;"])
        self.assertEqual(code[0], "t0 = a0 + 1;")

    def test_a_compound_statement_never_takes_a_fold(self):
        code, _ = fold(["t0 = lo + 1;", "{ u64 lo = Read64(q); t1 = t0; }"])
        self.assertEqual(code[0], "t0 = lo + 1;")


if __name__ == "__main__":
    unittest.main()
