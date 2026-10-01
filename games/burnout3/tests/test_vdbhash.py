"""Tests for the recovered ValueDB name hash.

The interesting test is `test_table_matches_the_binary`, which checks the
generated table against the 256 words the game actually ships.  It needs an
extracted executable, which is deliberately not committed, so it skips when one
is not present.
"""

import os
import struct
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ENGINE = os.path.join(ROOT, "..", "..", "engine")
sys.path[:0] = [os.path.join(ROOT, "tools"), ENGINE, os.path.join(ENGINE, "tools")]

from vdbhash import (
    CRC_POLYNOMIAL,
    CRC_TABLE,
    TABLE_ADDRESS,
    crc32_textbook,
    hash_name,
    join_path,
)

ELF_PATH = os.path.join(ROOT, "extracted", "SLUS_210.50")


class TestCrcTable(unittest.TestCase):
    def test_table_size(self):
        self.assertEqual(len(CRC_TABLE), 256)

    def test_known_entries(self):
        # Fixed points of the reflected CRC32 table, independent of our generator.
        self.assertEqual(CRC_TABLE[0], 0x00000000)
        self.assertEqual(CRC_TABLE[1], 0x77073096)
        self.assertEqual(CRC_TABLE[128], CRC_POLYNOMIAL)
        self.assertEqual(CRC_TABLE[255], 0x2D02EF8D)

    @unittest.skipUnless(os.path.exists(ELF_PATH), "extracted executable not present")
    def test_table_matches_the_binary(self):
        """The generated table must equal the one the game ships verbatim."""
        from ps2decomp.elf import ElfFile

        elf = ElfFile.load(ELF_PATH)
        section = elf.exec_sections[0]
        data = elf.section_data(section)
        offset = TABLE_ADDRESS - section.addr
        shipped = struct.unpack_from("<256I", data, offset)
        self.assertEqual(tuple(CRC_TABLE), shipped)


class TestHash(unittest.TestCase):
    def test_empty_string_returns_the_initial_value(self):
        # No bytes consumed, so the accumulator is still -1 and there is no
        # final XOR to change it.
        self.assertEqual(hash_name(""), 0xFFFFFFFF)

    def test_deterministic(self):
        self.assertEqual(hash_name("Damping"), hash_name("Damping"))

    def test_case_sensitive(self):
        self.assertNotEqual(hash_name("Damping"), hash_name("damping"))

    def test_accepts_bytes_and_str_identically(self):
        self.assertEqual(hash_name("Spring Coeff"), hash_name(b"Spring Coeff"))

    def test_result_is_32_bit(self):
        for name in ("", "a", "Spring Coeff", "Score/Burnout Points", "x" * 300):
            self.assertTrue(0 <= hash_name(name) <= 0xFFFFFFFF)

    def test_differs_from_textbook_crc32(self):
        """The signed-shift quirk must actually change the result.

        If these ever agree, the `sra` reading has been lost and the hash will
        no longer reproduce the game's keys.
        """
        self.assertNotEqual(hash_name("Damping"), crc32_textbook("Damping"))
        self.assertNotEqual(hash_name("Spring Coeff"), crc32_textbook("Spring Coeff"))

    def test_textbook_variant_has_no_final_xor(self):
        import zlib

        # zlib applies the final XOR; ours does not, so they are complements.
        self.assertEqual(
            crc32_textbook("Damping"), zlib.crc32(b"Damping") ^ 0xFFFFFFFF
        )


class TestJoinPath(unittest.TestCase):
    def test_inserts_single_separator(self):
        self.assertEqual(join_path("Vehicle", "Spring Coeff"), "Vehicle/Spring Coeff")

    def test_does_not_double_separator(self):
        self.assertEqual(join_path("Vehicle/", "Spring Coeff"), "Vehicle/Spring Coeff")

    def test_skips_empty_parts(self):
        self.assertEqual(join_path("", "Score", "", "Points"), "Score/Points")


if __name__ == "__main__":
    unittest.main(verbosity=2)
