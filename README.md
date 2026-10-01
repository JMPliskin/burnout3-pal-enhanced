# Burnout 3: Takedown - decompilation project

Reverse-engineering the PlayStation 2 release of *Burnout 3: Takedown*
(`SLUS_21050`, Criterion Games / Electronic Arts, 2004), working toward a
readable source reconstruction and eventually a native PC port.

**Status: tooling and static analysis complete; source reconstruction barely
begun.** See [Honest scope](#honest-scope) before reading anything else.

---

## What exists today

A working, tested analysis toolchain, and a complete static map of the
executable:

| | |
| --- | ---: |
| Executable analysed | `SLUS_210.50`, 4,073,088 bytes |
| Functions discovered | 7,423 |
| Image proven to be code | 3,300,392 bytes (81.0%) |
| Strings recovered | 5,556 |
| Address cross-references | 8,171 |
| Pointer tables (vtables / jump tables) | 95 |
| Annotated assembly listings generated | 7,423 files |
| Original compiler | Metrowerks CodeWarrior MIPS 2.4.1.01 |
| **Verified-complete source** | **0.0000%** |

That last row is the one that counts, and it is not a typo. See
[docs/PROGRESS.md](docs/PROGRESS.md) for the live numbers.

## Honest scope

The stated goal is a 100% decompilation. That is a legitimate goal, but it is
worth being precise about what it costs, because the gap between "analysis
done" and "decompilation done" is enormous.

A *matching* decompilation means source code that recompiles to byte-identical
machine code. For each of the 7,423 functions that requires recovering the
signature and every struct layout it touches, writing candidate C++, compiling
it with the **original** 2004 toolchain, diffing the output, and iterating until
the bytes agree.

For scale, comparable finished projects:

| Project | Scale | Effort |
| --- | --- | --- |
| Super Mario 64 | ~1 MB, N64 | ~2 years |
| Ocarina of Time | ~1 MB, N64 | ~3 years, 100+ contributors |
| Metroid Prime | GameCube, comparable size | ~5 years |

This binary is **harder than all three**. It is larger, it is completely
stripped (no symbol table, no section boundaries - code and data share one flat
blob), and nobody has yet established which compiler and flags reproduce
Criterion's code generation. Identifying that toolchain is itself a
prerequisite and typically weeks of work before the first function can match.

So: this is a multi-year, multi-person effort, and no amount of unattended
machine time changes that. What *can* be automated - extraction, disassembly,
function discovery, symbol recovery, progress accounting - is done and is in
this repository. The per-function work that follows is iterative and needs a
human in the loop.

The project deliberately refuses to report a flattering headline percentage.
Auto-generating plausible-looking C for thousands of functions is easy and
completely worthless: it compiles to nothing, matches nothing, and has to be
thrown away. Only `matched` counts.

## Requirements

* Python 3.10+ (standard library only - no third-party packages)
* Your own legally-obtained disc image

## Usage

```sh
# Inspect and extract from the disc
python tools/isotool.py info "Burnout 3 - Takedown.iso"
python tools/isotool.py list "Burnout 3 - Takedown.iso" --by-size
python tools/isotool.py extract "Burnout 3 - Takedown.iso" SLUS_210.50 -o extracted/

# Static analysis -> function/string/xref database
python tools/analyze.py extracted/SLUS_210.50 -o build/slus_210_50.json

# Annotated disassembly
python tools/disasm.py build/slus_210_50.json --addr 0x00100008
python tools/disasm.py build/slus_210_50.json --all -o asm/

# Progress accounting
python tools/progress.py build/slus_210_50.json
python tools/progress.py build/slus_210_50.json --markdown > docs/PROGRESS.md

# Tests
python -m unittest discover -s tests -v
```

## Repository layout

```
tools/           analysis toolchain (Python)
  b3lib/         library: iso9660, elf, r5900 disassembler, analysis, symbols
  isotool.py     disc inspection and extraction
  analyze.py     full static-analysis pipeline
  disasm.py      annotated assembly emitter
  progress.py    progress accounting
tests/           unit tests for the disassembler
include/         headers for the reconstructed sources
src/             reconstructed C++ sources
asm/             generated per-function assembly (not committed)
config/          function status table
docs/            plan, methodology, architecture, progress
extracted/       files pulled off the disc (not committed)
build/           analysis database (not committed)
```

## Documentation

* [docs/PLAN.md](docs/PLAN.md) - phased roadmap and what each phase requires
* [docs/METHODOLOGY.md](docs/METHODOLOGY.md) - progress tiers and how a function advances
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - how the toolchain works
* [docs/FINDINGS.md](docs/FINDINGS.md) - what the binary has revealed so far
* [docs/PROGRESS.md](docs/PROGRESS.md) - generated progress report

## Legal

This repository contains **no game code, assets, or binaries**. It contains
original analysis tooling and reverse-engineered documentation. Running any of
it requires a disc image you already own, which you supply yourself and which is
excluded from version control.

Reverse engineering for interoperability and preservation is protected in many
jurisdictions, and this follows the conventions established by the wider game
preservation community: no redistribution of copyrighted material, and no
attempt to substitute for the commercial product.
