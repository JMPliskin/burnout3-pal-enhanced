"""Tests for the ValueDB container reader.

Most tests build a synthetic file so they run without game data.  The ones that
read the real database skip when it has not been extracted.
"""

import os
import struct
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ENGINE = os.path.join(ROOT, "..", "..", "engine")
sys.path[:0] = [os.path.join(ROOT, "tools"), ENGINE, os.path.join(ENGINE, "tools")]

import vdb

REAL_VDB = os.path.join(ROOT, "extracted", "DATA", "VDB.XML")


def build_file(records, meta=(), middle=b"", index=()):
    """Assemble a synthetic ValueDB image matching the recovered layout."""
    table = b"".join(struct.pack("<if", k, v) for k, v in records)
    meta_bytes = b"".join(
        struct.pack("<fffI", a, b, c, vdb.META_SENTINEL) for a, b, c in meta
    )
    index_bytes = b"".join(struct.pack("<Ii", tag, key) for tag, key in index)
    tail_offset = vdb.TABLE_OFFSET + len(table) + len(meta_bytes) + len(middle)
    header = struct.pack(
        "<5I", 2, len(records), len(meta), len(index), tail_offset
    )
    # The header is 20 bytes but the table starts at 0x18, so pad the gap.
    header += b"\x00" * (vdb.TABLE_OFFSET - len(header))
    return header + table + meta_bytes + middle + index_bytes


class TestParse(unittest.TestCase):
    def test_round_trips_a_synthetic_file(self):
        records = [(-100, 1.5), (-5, 2.5), (7, 0.25), (900, -3.0)]
        meta = [(0.0, 1.0, 2.0), (0.5, 0.25, 9.0)]
        index = [(1, -42), (1, 77)]
        data = build_file(records, meta, b"\x01\x02\x03\x04", index)

        parsed = vdb.parse(data)
        self.assertEqual(parsed["version"], 2)
        self.assertEqual(parsed["header_counts"], [4, 2, 2])
        self.assertEqual([k for k, _ in parsed["records"]], [-100, -5, 7, 900])
        self.assertAlmostEqual(parsed["records"][0][1], 1.5)
        self.assertEqual(len(parsed["meta"]), 2)
        self.assertTrue(all(r[3] == vdb.META_SENTINEL for r in parsed["meta"]))
        self.assertEqual(parsed["middle"], b"\x01\x02\x03\x04")
        self.assertEqual([k for _t, k in parsed["index"]], [-42, 77])
        self.assertEqual(parsed["index_end"], len(data))

    def test_rejects_a_truncated_header(self):
        with self.assertRaises(vdb.VdbError):
            vdb.parse(b"\x00" * 8)

    def test_rejects_an_impossible_record_count(self):
        data = bytearray(build_file([(1, 1.0)]))
        struct.pack_into("<I", data, 4, 1_000_000)  # declare far too many records
        with self.assertRaises(vdb.VdbError):
            vdb.parse(bytes(data))

    def test_empty_sections_are_allowed(self):
        parsed = vdb.parse(build_file([(1, 1.0)]))
        self.assertEqual(parsed["meta"], [])
        self.assertEqual(parsed["index"], [])
        self.assertEqual(parsed["middle"], b"")


class TestValidate(unittest.TestCase):
    def test_reports_unsorted_keys(self):
        # Descending keys must be called out rather than silently accepted.
        parsed = vdb.parse(build_file([(5, 1.0), (-5, 2.0)]))
        text = "\n".join(vdb.validate(parsed))
        self.assertIn("NO - format assumption broken", text)

    def test_reports_sorted_keys(self):
        parsed = vdb.parse(build_file([(-5, 1.0), (5, 2.0)]))
        text = "\n".join(vdb.validate(parsed))
        self.assertIn("sorted (signed)    : yes", text)


@unittest.skipUnless(os.path.exists(REAL_VDB), "extracted VDB.XML not present")
class TestRealDatabase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(REAL_VDB, "rb") as handle:
            cls.parsed = vdb.parse(handle.read())

    def test_header_counts(self):
        self.assertEqual(self.parsed["version"], 2)
        self.assertEqual(self.parsed["header_counts"], [7525, 452, 348])

    def test_main_table_is_sorted_as_signed(self):
        keys = [k for k, _v in self.parsed["records"]]
        self.assertEqual(len(keys), 7525)
        self.assertTrue(all(keys[i] < keys[i + 1] for i in range(len(keys) - 1)))

    def test_keys_look_like_a_uniform_hash(self):
        """Mean key spacing should match a uniform spread over 2^32.

        This is the main evidence that the keys are hashes rather than allocated
        identifiers, so it is worth pinning down.
        """
        keys = [k for k, _v in self.parsed["records"]]
        gaps = [keys[i + 1] - keys[i] for i in range(len(keys) - 1)]
        mean_gap = sum(gaps) / len(gaps)
        expected = 2 ** 32 / len(keys)
        self.assertLess(abs(mean_gap / expected - 1.0), 0.001)

    def test_every_metadata_sentinel_is_intact(self):
        self.assertEqual(len(self.parsed["meta"]), 452)
        self.assertTrue(all(r[3] == vdb.META_SENTINEL for r in self.parsed["meta"]))

    def test_index_is_a_separate_key_namespace(self):
        main = {k for k, _v in self.parsed["records"]}
        index_keys = [k for _t, k in self.parsed["index"]]
        self.assertEqual(len(index_keys), 348)
        self.assertEqual(sum(1 for k in index_keys if k in main), 0)

    def test_sections_cover_the_whole_file(self):
        self.assertEqual(self.parsed["index_end"], self.parsed["file_size"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
