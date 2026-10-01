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

from b3lib.difftest import StringFunctionTester, ascii_corpus
from b3lib.vdbhash import crc32_textbook, hash_name

# Addresses of the original functions under test.
ADDR_STRLEN = 0x001273E8
ADDR_HASH_NAME = 0x0021BFA0


@unittest.skipUnless(os.path.exists(ELF_PATH), "extracted executable not present")
class TestAgainstOriginal(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from b3lib.elf import ElfFile
        from b3lib.emu import Machine

        cls.elf = ElfFile.load(ELF_PATH)
        cls.machine = Machine.from_elf(cls.elf)
        cls.tester = StringFunctionTester(cls.machine)
        cls.corpus = ascii_corpus()

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
