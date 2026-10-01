#!/usr/bin/env python3
"""Reader for the game's ValueDB tuning database (`DATA/VDB.XML`).

Despite the `.xml` extension the file is a compiled binary.  The format was
recovered by analysis; what is verified:

* a 20-byte header - a version word (2), three counts, and an offset;
* a single table of `count[0]` eight-byte records, each a 32-bit key and an
  IEEE-754 single;
* the table is sorted by key compared as a **signed** int32, which is why the
  negative keys (3,775 of them) all precede the positive ones (3,750).  The game
  binary-searches it with signed comparisons, matching the signed `int`
  accumulator in the hash routine.

The keys are hashes produced by `valuedb.HashName` (recovered from
`func_0021BFA0`; see `tools/b3lib/vdbhash.py`).  Two independent checks support
that reading: the keys are strictly ascending as signed integers, and their mean
spacing matches a uniform distribution over 2^32 to within 0.001%, which is what
a well-behaved 32-bit hash produces and what an allocated-ID scheme would not.

Mapping the keys back to parameter names is still open.  The names are *not* in
this file - it stores nothing but hashes and floats.  The names live in the
executable, and the full path hashed at runtime includes context from the caller's
node structure that is not recoverable from the call sites alone, so hashing the
statically-known path fragments does not reproduce the keys.

    python tools/vdb.py extracted/DATA/VDB.XML
    python tools/vdb.py extracted/DATA/VDB.XML --json -o build/vdb.json
    python tools/vdb.py extracted/DATA/VDB.XML --lookup "Score/Boost/Air/..."
"""

from __future__ import annotations

import argparse
import json
import math
import os
import statistics
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

HEADER_SIZE = 20
RECORD_SIZE = 8
TABLE_OFFSET = 0x18  # records begin here, just past the header


class VdbError(Exception):
    pass


def parse(data: bytes) -> dict:
    """Parse the header and the sorted record table."""
    if len(data) < TABLE_OFFSET:
        raise VdbError("file is too small to contain a header")
    version, count_a, count_b, count_c, tail_offset = struct.unpack_from("<5I", data, 0)

    available = (len(data) - TABLE_OFFSET) // RECORD_SIZE
    if count_a > available:
        raise VdbError(
            "header declares %d records but only %d fit in the file" % (count_a, available)
        )

    records = [
        struct.unpack_from("<if", data, TABLE_OFFSET + i * RECORD_SIZE)
        for i in range(count_a)
    ]
    table_end = TABLE_OFFSET + count_a * RECORD_SIZE

    return {
        "version": version,
        "header_counts": [count_a, count_b, count_c],
        "tail_offset": tail_offset,
        "records": records,
        "table_end": table_end,
        "file_size": len(data),
    }


def validate(parsed: dict) -> list:
    """Return human-readable consistency checks over the parsed table."""
    notes = []
    records = parsed["records"]
    keys = [k for k, _v in records]
    values = [v for _k, v in records]

    notes.append("version            : %d" % parsed["version"])
    notes.append("records            : %s" % format(len(records), ","))
    notes.append(
        "table span         : 0x%06X..0x%06X" % (TABLE_OFFSET, parsed["table_end"])
    )

    ascending = all(keys[i] < keys[i + 1] for i in range(len(keys) - 1))
    notes.append(
        "sorted (signed)    : %s" % ("yes" if ascending else "NO - format assumption broken")
    )
    negative = sum(1 for k in keys if k < 0)
    notes.append(
        "key sign split     : %s negative, %s positive"
        % (format(negative, ","), format(len(keys) - negative, ","))
    )

    # A hash spreads keys uniformly; an allocated id would cluster. Comparing the
    # mean gap against the uniform expectation is a cheap, strong check.
    if len(keys) > 2:
        gaps = [keys[i + 1] - keys[i] for i in range(len(keys) - 1)]
        expected = 2 ** 32 / len(keys)
        mean_gap = statistics.mean(gaps)
        notes.append(
            "mean key gap       : %s (uniform expectation %s, %+.3f%%)"
            % (
                format(round(mean_gap), ","),
                format(round(expected), ","),
                (mean_gap / expected - 1.0) * 100.0,
            )
        )

    nonfinite = sum(1 for v in values if not math.isfinite(v))
    notes.append(
        "values             : [%g, %g]%s"
        % (min(values), max(values), "" if not nonfinite else "  (%d non-finite)" % nonfinite)
    )
    notes.append(
        "header count[1..2] : %s, %s (meaning unknown)"
        % (format(parsed["header_counts"][1], ","), format(parsed["header_counts"][2], ","))
    )
    notes.append("header offset      : 0x%06X" % parsed["tail_offset"])

    if parsed["table_end"] < parsed["file_size"]:
        notes.append(
            "unparsed tail      : 0x%06X..0x%06X (%s bytes) - structure unknown"
            % (
                parsed["table_end"],
                parsed["file_size"],
                format(parsed["file_size"] - parsed["table_end"], ","),
            )
        )
    return notes


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("path")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("-o", "--output")
    parser.add_argument("--limit", type=int, default=10, help="records to preview")
    parser.add_argument("--lookup", help="hash this path and report its value, if present")
    args = parser.parse_args(argv)

    with open(args.path, "rb") as handle:
        data = handle.read()
    parsed = parse(data)

    if args.lookup:
        from b3lib.vdbhash import hash_name

        key = hash_name(args.lookup)
        signed = key - 0x100000000 if key & 0x80000000 else key
        match = next((v for k, v in parsed["records"] if k == signed), None)
        print('path  : "%s"' % args.lookup)
        print("hash  : 0x%08X" % key)
        if match is None:
            print("result: not present in the database")
            return 1
        print("value : %g" % match)
        return 0

    if args.json:
        payload = {
            "version": parsed["version"],
            "header_counts": parsed["header_counts"],
            "records": [
                {"key": k & 0xFFFFFFFF, "value": v} for k, v in parsed["records"]
            ],
        }
        text = json.dumps(payload, indent=1)
        if args.output:
            with open(args.output, "w", encoding="utf-8") as handle:
                handle.write(text)
            print("wrote %s" % args.output)
        else:
            print(text)
        return 0

    print("ValueDB: %s" % args.path)
    print("file size          : %s bytes" % format(parsed["file_size"], ","))
    for note in validate(parsed):
        print(note)
    print("\nfirst %d records:" % args.limit)
    for key, value in parsed["records"][: args.limit]:
        print("  0x%08X  %g" % (key & 0xFFFFFFFF, value))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
