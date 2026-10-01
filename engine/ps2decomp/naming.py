"""Names for functions in a stripped executable, each with the evidence behind it.

Retail PS2 executables carry no symbols, but some functions can be named with
certainty:

* **Kernel stubs.** `addiu $v1, $zero, N; syscall; jr $ra` calls kernel service
  N, and the service numbers are platform ABI (`engine/data/ee_syscalls.csv`).
* **The signature library.** Code already named in another game, and
  recognised here by its signature (`signatures.py`).
* **Diagnostic strings.** SDK and middleware functions identify themselves in
  their error messages - `"sceDbcInitSocket: rpc error"` - so a string that
  opens with a function-shaped name and a colon names the one function that
  prints it (`diagnostic_names`).
* **Hand-assigned names.** The ones in the game's status table that are not
  `generated::` placeholders.

A game's names live in `config/names.csv` as (address, name, source), so every
name can be traced back to its evidence and a wrong one is easy to find. Nothing
is renamed without a source.
"""

from __future__ import annotations

import bisect
import collections
import csv
import os
import re
import struct

DATA = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data")

_SYSCALL = 0x0000000C
_JR_RA = 0x03E00008


def syscall_table(path: str = os.path.join(DATA, "ee_syscalls.csv")) -> dict:
    """{service number: name}; negative numbers are interrupt-context variants."""
    table = {}
    with open(path, encoding="utf-8") as handle:
        rows = csv.DictReader(line for line in handle if not line.startswith("#"))
        for row in rows:
            table[int(row["service"], 0)] = row["name"]
    return table


def syscall_stubs(database: dict, image: bytes, base: int) -> dict:
    """{function address: service number} for every kernel stub.

    A stub is `addiu $v1, $zero, N` then `syscall`, followed by `jr $ra` within the
    next two instructions. Nothing else may come before the syscall.
    """
    found = {}
    for func in database["functions"]:
        start, end = func["addr"], func["end"]
        if end - start < 12 or end - start > 24:
            continue
        words = struct.unpack_from("<%dI" % ((end - start) // 4), image, start - base)
        first = words[0]
        if first >> 16 != 0x2403 or words[1] != _SYSCALL:   # addiu $v1, $zero, imm
            continue
        if _JR_RA not in words[2:4]:
            continue
        service = first & 0xFFFF
        found[start] = service - 0x10000 if service & 0x8000 else service
    return found


# "Name: message". The name must look like a function, not a module tag: either a
# lowercase library prefix then a capital (sceDbcInitSocket, lgAudOpen), or three
# or more capitalised words (_LobbyStatbookCreate). Two-word names are refused
# because module tags use that shape too ("ProtoMangle: ...", "DirtyElf: ...").
_SELF_NAMED = re.compile(r"^(_?(?:[a-z]+[A-Z]|[A-Z][a-z0-9]+[A-Z][a-z0-9]+[A-Z])[A-Za-z0-9_]*)\s?:")


def diagnostic_names(database: dict) -> dict:
    """{function address: (name, the string)} from self-identifying messages.

    A name is used only when exactly one function references strings carrying it,
    and that function carries no other such name.
    """
    strings = {entry["addr"]: entry["text"] for entry in database["strings"]}
    spans = sorted((f["addr"], f["end"]) for f in database["functions"])
    starts = [start for start, _end in spans]
    owners = collections.defaultdict(set)      # name -> functions
    evidence = {}
    for xref in database["xrefs"]:
        text = strings.get(xref["to"])
        match = _SELF_NAMED.match(text) if text else None
        if not match:
            continue
        index = bisect.bisect_right(starts, xref["from"]) - 1
        if index < 0 or xref["from"] >= spans[index][1]:
            continue
        owners[match.group(1)].add(spans[index][0])
        evidence.setdefault(match.group(1), text.strip())
    by_function = collections.defaultdict(set)
    for name, functions in owners.items():
        for address in functions:
            by_function[address].add(name)
    named = {}
    for address, names in by_function.items():
        name = next(iter(names))
        if len(names) == 1 and len(owners[name]) == 1:
            named[address] = (name, evidence[name])
    return named


def status_names(status_path: str) -> dict:
    """Hand-assigned names from a status table: {address: name}."""
    names = {}
    if not os.path.exists(status_path):
        return names
    with open(status_path, encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            name = row.get("name", "")
            if name and not name.startswith("generated::"):
                names[int(row["address"], 16)] = name.split("::")[-1]
    return names


def load(path: str) -> dict:
    """A game's names file: {address: (name, source)}."""
    names = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                names[int(row["address"], 16)] = (row["name"], row["source"])
    return names


def save(path: str, names: dict) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("address,name,source\n")
        for address in sorted(names):
            name, source = names[address]
            handle.write("0x%08X,%s,%s\n" % (address, name, source))
