"""Differential tests: reconstructed functions against the original machine code.

These are the tests that promote a function to the `verified` tier.  Each one runs
the original code in the R5900 interpreter and compares it against the
reconstruction in `src/`, so a regression in either shows up here.

They need an extracted executable, which is deliberately not committed, and skip
when one is absent.
"""

import os
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

ELF_PATH = os.path.join(ROOT, "extracted", "SLUS_210.50")

from b3lib.difftest import (
    IntFunctionTester,
    StringFunctionTester,
    StringTransformTester,
    ascii_corpus,
    bit_corpus,
)
from b3lib.vdbhash import crc32_textbook, hash_name

# Addresses of the original functions under test.
ADDR_STRLEN = 0x001273E8
ADDR_STRCPY = 0x001272D0
ADDR_STRCAT = 0x00126EC8
ADDR_HASH_NAME = 0x0021BFA0
ADDR_FIND_FIRST_SET = 0x00299550


@unittest.skipUnless(os.path.exists(ELF_PATH), "extracted executable not present")
class TestAgainstOriginal(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from b3lib.elf import ElfFile
        from b3lib.emu import Machine

        cls.elf = ElfFile.load(ELF_PATH)
        cls.machine = Machine.from_elf(cls.elf)
        cls.tester = StringFunctionTester(cls.machine)
        cls.transform = StringTransformTester(cls.machine)
        cls.ints = IntFunctionTester(cls.machine)
        cls.corpus = ascii_corpus()
        # The transform tester multiplies each case by prefixes and alignments, so
        # a trimmed corpus keeps the suite fast without losing the edge cases.
        cls.short_corpus = [c for c in cls.corpus if len(c) <= 64][:12]

    def test_strlen_matches_len(self):
        """func_001273E8 is a SIMD strlen.

        It detects a zero byte sixteen at a time with psubb/pnor/pand against
        0x01 and 0x80 vectors, with separate paths for 8- and 16-byte alignment.
        """
        report = self.tester.run(
            "StringLength", ADDR_STRLEN, lambda b: len(b), self.corpus
        )
        self.assertTrue(report.passed, report.summary() + "\n" +
                        "\n".join(str(m) for m in report.mismatches[:5]) +
                        "\n".join(report.errors[:5]))
        self.assertGreaterEqual(report.cases_run, 20)

    def test_strlen_handles_every_alignment(self):
        """The aligned fast path and the byte-at-a-time path must agree.

        The function branches on the low three and four bits of the pointer, so a
        bug in one path would be invisible if every test string were aligned.
        """
        base = 0x01300000
        for adjust in range(16):
            addr = base + 0x1000 + adjust
            self.machine.memory.write(addr, b"Damping\x00")
            self.assertEqual(
                self.machine.call(ADDR_STRLEN, (addr,)), 7,
                "wrong length at pointer alignment +%d" % adjust,
            )


    def test_string_copy_matches_strcpy(self):
        """func_001272D0 is a SIMD strcpy.

        It tests alignment on `dest | source` together, because the quadword path
        needs both pointers co-aligned.
        """
        report = self.transform.run(
            "runtime::StringCopy", ADDR_STRCPY,
            lambda prefix, src: src, self.short_corpus,
        )
        self.assertTrue(report.passed, report.detail())
        self.assertGreaterEqual(report.cases_run, 40)

    def test_string_append_matches_strcat(self):
        """func_00126EC8 is a SIMD strcat.

        Exercised against several existing destination contents, since the
        function first has to locate the terminator already in `dest`.
        """
        report = self.transform.run(
            "runtime::StringAppend", ADDR_STRCAT,
            lambda prefix, src: prefix + src, self.short_corpus,
            prefixes=(b"", b"Score", b"Material Properties"),
        )
        self.assertTrue(report.passed, report.detail())
        self.assertGreaterEqual(report.cases_run, 120)

    def test_string_writers_do_not_overrun(self):
        """Neither writer may touch memory past the terminator it writes.

        The vector paths legitimately *read* past the end, which is safe on PS2
        hardware, but writing past it would corrupt the caller's buffer. The
        transform tester checks the following byte on every case; this makes the
        intent explicit.
        """
        for name, address, reference in (
            ("StringCopy", ADDR_STRCPY, lambda p, s: s),
            ("StringAppend", ADDR_STRCAT, lambda p, s: p + s),
        ):
            report = self.transform.run(
                name, address, reference, [b"", b"a", b"x" * 17, b"y" * 33]
            )
            self.assertTrue(report.passed, report.detail())


    def test_find_first_set(self):
        """func_00299550 returns the index of the lowest set bit.

        Covered densely rather than sampled: every single-bit value, every
        two-bit pair, the usual edge words, and 120 deterministic random ones.
        """
        def reference(value):
            value &= 0xFFFFFFFF
            for index in range(32):
                if value & (1 << index):
                    return index
            return 32

        report = self.ints.run(
            "runtime::FindFirstSet", ADDR_FIND_FIRST_SET, reference, bit_corpus()
        )
        self.assertTrue(report.passed, report.detail())
        self.assertGreaterEqual(report.cases_run, 150)

    def test_find_first_set_returns_32_for_zero(self):
        """The zero case is the one a port is most likely to get wrong.

        A `ctz` intrinsic is undefined for zero, so substituting one would change
        behaviour here. Pinning the sentinel down stops that happening quietly.
        """
        self.assertEqual(self.machine.call(ADDR_FIND_FIRST_SET, (0,)), 32)

    def test_hash_name_matches_reconstruction(self):
        """valuedb::HashName reproduces func_0021BFA0 exactly."""
        report = self.tester.run(
            "valuedb::HashName", ADDR_HASH_NAME, hash_name, self.corpus
        )
        self.assertTrue(report.passed, report.summary() + "\n" +
                        "\n".join(str(m) for m in report.mismatches[:5]) +
                        "\n".join(report.errors[:5]))
        self.assertGreaterEqual(report.cases_run, 20)

    def test_hash_differs_from_textbook_crc32_on_real_input(self):
        """The signed-shift quirk has to matter, or it was not really recovered.

        If a textbook CRC32 agreed with the original on most inputs, the quirks
        would be unverified coincidence rather than a finding.
        """
        report = self.tester.run(
            "textbook-crc32-control", ADDR_HASH_NAME, crc32_textbook, self.corpus
        )
        self.assertFalse(
            report.passed,
            "textbook CRC32 matched the original; the sra/no-final-XOR "
            "reading is then unsupported",
        )
        # It should disagree on nearly everything, not just a few cases.
        self.assertGreater(len(report.mismatches), report.cases_run * 0.9)

    def test_empty_string_hashes_to_the_initial_value(self):
        """With no bytes consumed and no final XOR, the result is still -1."""
        addr = 0x01400000
        self.machine.memory.write(addr, b"\x00")
        self.assertEqual(
            self.machine.call(ADDR_HASH_NAME, (addr,)) & 0xFFFFFFFF, 0xFFFFFFFF
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
