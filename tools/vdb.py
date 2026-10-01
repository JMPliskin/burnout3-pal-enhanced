#!/usr/bin/env python3
"""Reader for the game's ValueDB tuning database (`DATA/VDB.XML`).

Despite the `.xml` extension the file is a compiled binary, and the format was
recovered by analysis rather than from any documentation.  What is verified:

* a 20-byte header: a version word, three counts, and an offset;
* one or more **sorted** tables of 8-byte `(key, value)` records, where the key
  is a 32-bit hash and the value is an IEEE-754 single;
* tables are distinguished by whether bit 31 of the key is set, and within a
  table keys increase strictly - which is what lets the game binary-search them.

What is **not** yet known is the hash function, so the numeric keys cannot be
turned back into the parameter names (`Spring Coeff`, `Damping`, `Min Impulse`
and so on) that appear in the executable.  Eighteen combinations of the common
string hashes - CRC32 in both bit orders, FNV-1/1a, djb2, sdbm, Jenkins, ELF, and
several multiplicative variants, each with case folding - produce zero matches,
so it is a proprietary hash.

The way to settle it is to read the algorithm out of the code rather than guess:
the executable hashes these strings at runtime to perform the lookup, so the
hash is implemented at the call sites that reference them.  See
`docs/FINDINGS.md` for the addresses to start from.

    python tools/vdb.py extracted/DATA/VDB.XML
    python tools/vdb.py extracted/DATA/VDB.XML --json -o build/vdb.json
"""

from __future__ import annotations

import argparse
import json
import math
import struct

HEADER_SIZE = 20
RECORD_SIZE = 8
KEY_FLAG = 0x80000000
# A real sorted lookup table is large; short runs past the end are coincidence.
MIN_TABLE_RECORDS = 64


class VdbError(Exception):
    pass


def parse(data: bytes) -> dict:
    """Parse the database into its header and sorted tables."""
    if len(data) < HEADER_SIZE:
        raise VdbError("file is too small to contain a header")
    version, count_a, count_b, count_c, tail_offset = struct.unpack_from("<5I", data, 0)

    tables = []
    offset = HEADER_SIZE + 4  # the first sorted table begins at 0x18
    while offset + RECORD_SIZE <= len(data):
        first_key, = struct.unpack_from("<I", data, offset)
        flag = first_key & KEY_FLAG
        records = []
        previous = None
        scan = offset
        # A table runs while keys keep increasing and the flag bit is unchanged;
        # either condition breaking means a new table (or trailing data) begins.
        while scan + RECORD_SIZE <= len(data):
            key, value = struct.unpack_from("<If", data, scan)
            if (key & KEY_FLAG) != flag:
                break
            if previous is not None and key <= previous:
                break
            records.append((key, value))
            previous = key
            scan += RECORD_SIZE
        # Past the real tables the same test starts matching one- and two-record
        # runs of unrelated data. A sorted hash table of any use is far larger
        # than that, so a short run means the sorted section has ended and the
        # remainder is a different structure this parser does not claim to know.
        if len(records) < MIN_TABLE_RECORDS:
            break
        tables.append({"offset": offset, "flagged": bool(flag), "records": records})
        offset = scan

    return {
        "version": version,
        "header_counts": [count_a, count_b, count_c],
        "tail_offset": tail_offset,
        "tables": tables,
        "trailing_offset": offset,
        "file_size": len(data),
    }


def validate(parsed: dict) -> list:
    """Return a list of human-readable consistency observations."""
    notes = []
    total = sum(len(t["records"]) for t in parsed["tables"])
    declared = parsed["header_counts"][0]
    notes.append("tables parsed      : %d" % len(parsed["tables"]))
    notes.append("records parsed     : %s" % format(total, ","))
    notes.append("header count[0]    : %s" % format(declared, ","))
    # count[0] is the total record count across every sorted table; an exact
    # agreement here is the main check that the table boundaries were found
    # correctly, since a mis-split would change the total.
    if total == declared:
        notes.append("  -> matches the parsed record total: table split confirmed")
    else:
        notes.append(
            "  -> MISMATCH: parsed %s records but the header declares %s, so the "
            "table boundaries are wrong" % (format(total, ","), format(declared, ","))
        )
    notes.append(
        "header count[1..2] : %s, %s (meaning unknown)"
        % (format(parsed["header_counts"][1], ","), format(parsed["header_counts"][2], ","))
    )
    for table in parsed["tables"]:
        values = [v for _k, v in table["records"]]
        nonfinite = sum(1 for v in values if not math.isfinite(v))
        notes.append(
            "table @0x%06x  %s records, flag=%d, values [%g, %g]%s"
            % (
                table["offset"],
                format(len(table["records"]), ","),
                int(table["flagged"]),
                min(values),
                max(values),
                "" if nonfinite == 0 else "  (%d non-finite!)" % nonfinite,
            )
        )
    if parsed["trailing_offset"] < parsed["file_size"]:
        notes.append(
            "unparsed tail      : 0x%06x..0x%06x (%s bytes) - structure unknown"
            % (
                parsed["trailing_offset"],
                parsed["file_size"],
                format(parsed["file_size"] - parsed["trailing_offset"], ","),
            )
        )
    return notes


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("path")
    parser.add_argument("--json", action="store_true", help="emit the parsed tables")
    parser.add_argument("-o", "--output")
    parser.add_argument("--limit", type=int, default=12, help="records to preview")
    args = parser.parse_args(argv)

    with open(args.path, "rb") as handle:
        data = handle.read()
    parsed = parse(data)

    if args.json:
        payload = {
            "version": parsed["version"],
            "header_counts": parsed["header_counts"],
            "tables": [
                {
                    "offset": t["offset"],
                    "flagged": t["flagged"],
                    "records": [{"key": k, "value": v} for k, v in t["records"]],
                }
                for t in parsed["tables"]
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
    print("version            : %d" % parsed["version"])
    print("file size          : %s bytes" % format(parsed["file_size"], ","))
    for note in validate(parsed):
        print(note)
    for table in parsed["tables"]:
        print("\nfirst %d records of table @0x%06x:" % (args.limit, table["offset"]))
        for key, value in table["records"][: args.limit]:
            print("  0x%08X  %g" % (key, value))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
