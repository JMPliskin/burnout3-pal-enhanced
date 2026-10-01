"""Tests for function discovery's "does this look like code" judgement.

The check has to reject data that merely decodes - runs of zeroes are valid
`nop`s - while accepting real functions, including the very short ones: a getter
is `jr $ra; lw $v0, 8($a0)`, and an empty function is just `jr $ra; nop`. Getting
the short ones wrong lost 424 call targets, one called from 866 sites.
"""

import os
import struct
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

from b3lib.analysis import ImageAnalyzer

BASE = 0x00100000
JR_RA = 0x03E00008
NOP = 0
LW_V0_8_A0 = (0x23 << 26) | (4 << 21) | (2 << 16) | 8
J_ELSEWHERE = (2 << 26) | ((BASE + 0x100) >> 2)
JAL = (3 << 26) | ((BASE + 0x100) >> 2)
B_FORWARD = 0x10000004         # beq $zero, $zero, +4


def analyzer(words):
    words = list(words) + [NOP] * 64
    return ImageAnalyzer(struct.pack("<%dI" % len(words), *words), BASE, BASE)


class TestLooksLikeCode(unittest.TestCase):
    def test_getter_with_its_body_in_the_delay_slot(self):
        self.assertTrue(analyzer([JR_RA, LW_V0_8_A0])._looks_like_code(BASE))

    def test_empty_function_is_accepted_only_as_a_call_target(self):
        a = analyzer([JR_RA, NOP])
        self.assertTrue(a._looks_like_code(BASE, called=True))
        self.assertFalse(a._looks_like_code(BASE))

    def test_thunk_is_accepted_as_a_call_target(self):
        self.assertTrue(analyzer([J_ELSEWHERE, NOP])._looks_like_code(BASE, called=True))

    def test_zeroes_are_not_code(self):
        self.assertFalse(analyzer([NOP] * 8)._looks_like_code(BASE, called=True))

    def test_empty_delay_slot_does_not_count_as_padding(self):
        """`jal; nop; b; ...` is real code; counting its nop delay slot as padding
        tipped 37 pointer-table entries below the threshold."""
        self.assertTrue(analyzer([JAL, NOP, B_FORWARD, NOP])._looks_like_code(BASE))


if __name__ == "__main__":
    unittest.main()
