#!/usr/bin/env python3
"""Report decompilation progress against the analysis database.

Progress is tracked in four tiers, kept deliberately separate because they mean
very different things:

  undecompiled  the analysis found the function; nothing is known about it
  analyzed      behaviour worked out and written down, but no source yet
  identified    C/C++ source exists and is believed to be behaviourally correct
  matched       the source recompiles to byte-identical machine code

Only `matched` is objective.  The other tiers rest on human judgement, so they
are never folded together into a single headline percentage: a project that
reports "80% done" on the strength of unverified source is lying to itself.

    python tools/progress.py build/slus_210_50.json
    python tools/progress.py build/slus_210_50.json --markdown > docs/PROGRESS.md
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys
import time

TIERS = ["undecompiled", "analyzed", "identified", "matched"]


def load_status(path: str) -> dict:
    """Read the hand-maintained per-function status table."""
    if not os.path.exists(path):
        return {}
    out = {}
    with open(path, newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            address = row.get("address", "").strip()
            if not address:
                continue
            out[int(address, 16)] = {
                "name": row.get("name", "").strip(),
                "status": row.get("status", "undecompiled").strip() or "undecompiled",
                "source": row.get("source", "").strip(),
                "notes": row.get("notes", "").strip(),
            }
    return out


def summarise(database: dict, status: dict) -> dict:
    functions = database["functions"]
    total_functions = len(functions)
    total_code = sum(f["size"] for f in functions)

    by_tier = {tier: {"count": 0, "bytes": 0} for tier in TIERS}
    for func in functions:
        entry = status.get(func["addr"])
        tier = entry["status"] if entry and entry["status"] in TIERS else "undecompiled"
        by_tier[tier]["count"] += 1
        by_tier[tier]["bytes"] += func["size"]

    meta = database["meta"]
    stats = database["stats"]
    image_size = meta["image_size"]
    return {
        "meta": meta,
        "image_size": image_size,
        "code_bytes": stats.get("code_bytes", 0),
        "data_bytes": image_size - stats.get("code_bytes", 0),
        "coverage": stats.get("code_bytes", 0) / image_size * 100.0 if image_size else 0.0,
        "total_functions": total_functions,
        "total_code": total_code,
        "by_tier": by_tier,
        "strings": len(database.get("strings", [])),
        "xrefs": len(database.get("xrefs", [])),
        "pointer_tables": len(database.get("pointer_tables", [])),
        "uses_vu": stats.get("uses_vu", 0),
        "conf_called": stats.get("conf_called", 0),
        "conf_prologue": stats.get("conf_prologue", 0),
    }


def _pct(part: int, whole: int) -> float:
    return (part / whole * 100.0) if whole else 0.0


def render_text(summary: dict) -> str:
    lines = []
    lines.append("Burnout 3 decompilation progress")
    lines.append("=" * 62)
    lines.append("image            : %s" % summary["meta"]["source"])
    lines.append("image size       : %s bytes" % format(summary["image_size"], ","))
    lines.append("proven code      : %s bytes (%.1f%%)"
                 % (format(summary["code_bytes"], ","), summary["coverage"]))
    lines.append("non-code         : %s bytes" % format(summary["data_bytes"], ","))
    lines.append("functions found  : %s" % format(summary["total_functions"], ","))
    lines.append("")
    lines.append("%-14s %9s %7s %14s %8s" % ("tier", "functions", "share", "bytes", "share"))
    lines.append("-" * 62)
    for tier in TIERS:
        row = summary["by_tier"][tier]
        lines.append(
            "%-14s %9s %6.2f%% %14s %7.2f%%"
            % (
                tier,
                format(row["count"], ","),
                _pct(row["count"], summary["total_functions"]),
                format(row["bytes"], ","),
                _pct(row["bytes"], summary["total_code"]),
            )
        )
    lines.append("-" * 62)
    matched = summary["by_tier"]["matched"]
    lines.append("VERIFIED COMPLETE: %.4f%% of code bytes recompile byte-identical"
                 % _pct(matched["bytes"], summary["total_code"]))
    return "\n".join(lines)


def render_markdown(summary: dict) -> str:
    meta = summary["meta"]
    matched = summary["by_tier"]["matched"]
    lines = []
    lines.append("# Decompilation progress")
    lines.append("")
    lines.append("_Generated %s from `%s`._" % (time.strftime("%Y-%m-%d %H:%M"), meta["source"]))
    lines.append("")
    lines.append("## Headline number")
    lines.append("")
    lines.append(
        "**%.4f%%** of the executable's code bytes are verified complete - that is, "
        "source that recompiles to byte-identical machine code." % _pct(matched["bytes"], summary["total_code"])
    )
    lines.append("")
    lines.append(
        "This is the only number that means anything on its own. The tiers below "
        "track work in progress, but until a function reaches `matched` there is no "
        "proof the source is right."
    )
    lines.append("")
    lines.append("## Static analysis")
    lines.append("")
    lines.append("| metric | value |")
    lines.append("| --- | ---: |")
    lines.append("| image size | %s bytes |" % format(summary["image_size"], ","))
    lines.append("| proven code | %s bytes (%.1f%%) |"
                 % (format(summary["code_bytes"], ","), summary["coverage"]))
    lines.append("| non-code (rodata/data/padding) | %s bytes |" % format(summary["data_bytes"], ","))
    lines.append("| functions discovered | %s |" % format(summary["total_functions"], ","))
    lines.append("| - found via direct calls | %s |" % format(summary["conf_called"], ","))
    lines.append("| - found via prologue scan | %s |" % format(summary["conf_prologue"], ","))
    lines.append("| functions using VU0 macro mode | %s |" % format(summary["uses_vu"], ","))
    lines.append("| strings recovered | %s |" % format(summary["strings"], ","))
    lines.append("| address cross-references | %s |" % format(summary["xrefs"], ","))
    lines.append("| pointer tables (vtables/jump tables) | %s |" % format(summary["pointer_tables"], ","))
    lines.append("")
    lines.append("## Progress by tier")
    lines.append("")
    lines.append("| tier | functions | share | code bytes | share |")
    lines.append("| --- | ---: | ---: | ---: | ---: |")
    for tier in TIERS:
        row = summary["by_tier"][tier]
        lines.append(
            "| `%s` | %s | %.2f%% | %s | %.2f%% |"
            % (
                tier,
                format(row["count"], ","),
                _pct(row["count"], summary["total_functions"]),
                format(row["bytes"], ","),
                _pct(row["bytes"], summary["total_code"]),
            )
        )
    lines.append("")
    lines.append("Tier meanings are defined in [METHODOLOGY.md](METHODOLOGY.md).")
    return "\n".join(lines) + "\n"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("database")
    parser.add_argument("--status", default="config/functions.csv")
    parser.add_argument("--markdown", action="store_true")
    args = parser.parse_args(argv)

    with open(args.database, encoding="utf-8") as handle:
        database = json.load(handle)
    summary = summarise(database, load_status(args.status))
    sys.stdout.write(
        render_markdown(summary) if args.markdown else render_text(summary) + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
