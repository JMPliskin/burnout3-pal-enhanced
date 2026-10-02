"""The text rewrites applied to structured code. Each is checked again on every
function by verification; these pin down the shapes they accept and refuse."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from ps2decomp import structure  # noqa: E402


class TestTidy(unittest.TestCase):
    def test_comparison_tested_against_zero_is_the_comparison(self):
        self.assertEqual(structure.tidy("if (((u64)((s64)s1 < (s64)0x6ull)) != 0ull) {"),
                         "if ((s64)s1 < 6) {")
        self.assertEqual(structure.tidy("if (((u64)(a0 < v1)) == 0ull) {"),
                         "if (!(a0 < v1)) {")

    def test_small_sign_extended_literal(self):
        self.assertEqual(structure.tidy("v1 = Sx32(0x9u);"), "v1 = 0x9ull;")
        # 0x80000000 and above change under sign extension.
        self.assertEqual(structure.tidy("v1 = Sx32(0x80000000u);"), "v1 = Sx32(0x80000000u);")

    def test_inner_sign_extension_of_an_addend(self):
        self.assertEqual(structure.tidy("v0 = Sx32(s0 + (Sx32((u32)s1 << 2)));"),
                         "v0 = Sx32(s0 + ((u32)s1 << 2));")
        self.assertEqual(
            structure.tidy("v0 = Sx32(s0 + (Sx32((u32)(Sx32((Sx32((u32)s1 << 3)) - s1)) << 4)));"),
            "v0 = Sx32(s0 + ((u32)(((u32)s1 << 3) - s1) << 4));")

    def test_inner_sign_extension_kept_under_other_operators(self):
        # Under a shift right the high bits reach the low 32; only sums and
        # differences are rewritten.
        line = "v0 = Sx32(s0 >> (Sx32(a0 + a1)));"
        self.assertEqual(structure.tidy(line), line)


class TestDoWhile(unittest.TestCase):
    LOOP = ["    for (;;) {",
            "        Tick(c, 0x00100000u);",
            "        s1 = Sx32(s1 + 0x1u);",
            "        if ((s64)s1 < 6) {",
            "                continue;",
            "        } else {",
            "                break;",
            "        }",
            "    }"]

    def test_loop_ending_in_its_test(self):
        self.assertEqual(structure.loops_to_do_while(self.LOOP),
                         ["    do {",
                          "        Tick(c, 0x00100000u);",
                          "        s1 = Sx32(s1 + 0x1u);",
                          "    } while ((s64)s1 < 6);"])

    def test_reversed_test_is_negated(self):
        loop = list(self.LOOP)
        loop[4], loop[6] = "                break;", "                continue;"
        self.assertEqual(structure.loops_to_do_while(loop)[-1], "    } while (!((s64)s1 < 6));")

    def test_other_continue_keeps_the_for(self):
        loop = self.LOOP[:2] + ["        if (a0 != 0ull) { continue; }"] + self.LOOP[2:]
        self.assertEqual(structure.loops_to_do_while(loop), loop)


if __name__ == "__main__":
    unittest.main()
