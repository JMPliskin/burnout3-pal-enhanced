#!/usr/bin/env python3
"""Remove confirmed PAL VU false positives. Run from any working directory.

Uses only the standard library; refuses to overwrite existing outputs.
"""
import hashlib
import json
from pathlib import Path

EXCLUDED = (0x0046A280, 0x0046A2A0, 0x0046A480, 0x00481818)
PRESERVED = ("regions", "strings", "xrefs", "pointer_tables", "renderware",
             "external_functions", "stats")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    game = Path(__file__).resolve().parents[1]
    source = game / "build/sles_525_84.json"
    target = game / "build/sles_525_84_clean.json"
    report_path = game / "build/sles_525_84_clean_report.json"
    for path in (target, report_path):
        require(not path.exists(), f"Refusing to overwrite {path}")
    source_bytes = source.read_bytes()
    original = json.loads(source_bytes)
    config = json.loads((game / "game.json").read_bytes())
    require(config["executable"] == original["meta"]["source"] == "SLES_525.84",
            "Unexpected executable or database source")
    ranges = [(int(low, 0) if isinstance(low, str) else low,
               int(high, 0) if isinstance(high, str) else high)
              for low, high in config["vu_regions"]]
    require(all(low < high for low, high in ranges), "Invalid VU interval")
    functions = original["functions"]
    require(len(functions) == 9388, "Expected 9388 original functions")
    addresses = [f["addr"] for f in functions]
    require(len(addresses) == len(set(addresses)), "Duplicate original addresses")
    by_address = {f["addr"]: f for f in functions}
    exclusions = []
    for address in EXCLUDED:
        require(address in by_address, f"Missing function 0x{address:08X}")
        f = by_address[address]
        require(any(low <= address < f["end"] <= high for low, high in ranges),
                f"Function 0x{address:08X} is not wholly within vu_regions")
        exclusions.append({
            "address": f"0x{address:08X}",
            "end_exclusive": f"0x{f['end']:08X}",
            "size": f["size"], "confidence": f["confidence"],
            "reason": "Confirmed MIPS false positive in the statically identified DMA/VIF VU container. Exclude function metadata only; retain ELF bytes and references.",
        })
    cleaned = dict(original)
    cleaned["functions"] = [f for f in functions if f["addr"] not in EXCLUDED]
    remaining = [f["addr"] for f in cleaned["functions"]]
    require(len(remaining) == 9384, "Expected 9384 clean functions")
    require(len(remaining) == len(set(remaining)), "Duplicate clean addresses")
    require(set(addresses) - set(remaining) == set(EXCLUDED), "Unexpected exclusions")
    require({k: v for k, v in cleaned.items() if k != "functions"}
            == {k: v for k, v in original.items() if k != "functions"},
            "Changed data outside functions")
    for key in PRESERVED:
        require(cleaned[key] == original[key], f"Changed preserved field: {key}")
    output_bytes = (json.dumps(cleaned, indent=1) + "\n").encode("utf-8")
    require(json.loads(output_bytes) == cleaned, "JSON round-trip failed")
    report = {
        "source": source.name, "output": target.name,
        "source_sha256": hashlib.sha256(source_bytes).hexdigest(),
        "output_sha256": hashlib.sha256(output_bytes).hexdigest(),
        "functions_before": len(functions), "functions_after": len(remaining),
        "functions_excluded": len(exclusions),
        "excluded_function_bytes": sum(e["size"] for e in exclusions),
        "duplicate_addresses_after": len(remaining) - len(set(remaining)),
        "stats_policy": "Unchanged statistics of the first analysis; final counts are reported separately here.",
        "preserved_fields": list(PRESERVED), "vu_regions": config["vu_regions"],
        "exclusions": exclusions,
        "validation": "Exact counts and exclusions, unique addresses, JSON round-trip, preservation of all other fields and unchanged original bytes verified.",
    }
    with target.open("xb") as handle:
        handle.write(output_bytes)
    require(json.loads(target.read_bytes()) == cleaned, "Written JSON validation failed")
    require(source.read_bytes() == source_bytes, "Original database changed")
    with report_path.open("x", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
        handle.write("\n")
    require(json.loads(report_path.read_bytes()) == report, "Report validation failed")
    print(f"Database: {target}")
    print(f"Report: {report_path}")
    print("Functions: 9388 -> 9384; excluded: 4 (96 bytes); duplicates: 0")
    print("Validation passed; source and all non-function fields unchanged.")


if __name__ == "__main__":
    main()
