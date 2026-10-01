# Burnout 3: Takedown - decompilation project

Reverse-engineering the PlayStation 2 release of *Burnout 3: Takedown*
(`SLUS_21050`, Criterion Games / Electronic Arts, 2004), working toward a
readable source reconstruction and eventually a native PC port.

**Status: tooling and static analysis complete; source reconstruction barely
begun.** See [Honest scope](#honest-scope) before reading anything else.

---

## What exists today

A working analysis toolchain, a complete static map of the executable, and an
objective way to verify reconstructed functions without the original compiler.

| | |
| --- | ---: |
| Executable analysed | `SLUS_210.50`, 4,073,088 bytes |
| Functions discovered | 9,163 |
| Image proven to be MIPS code | 86.2% |
| VU microcode located | 108,096 bytes (~173 microprograms) |
| Instructions failing to decode | 0 of 814,598 |
| Strings / xrefs / pointer tables | 5,556 / 8,159 / 355 |
| Functions executable in the interpreter | 7,412 (80.9%) |
| **Verified reconstructions** | **2 functions, 0.0130% of code bytes** |

See [docs/PROGRESS.md](docs/PROGRESS.md) for live numbers.

## Approach: functional reconstruction

The original compiler is known - `MW MIPS C Compiler (2.4.1.01)`, Metrowerks
CodeWarrior, named in the executable's own `.comment` section - but it is
abandoned proprietary software and could not be obtained. Byte-matching is
therefore off the table: Metrowerks and GCC differ systematically in register
allocation and scheduling, so chasing a match with GCC would stall short and never
close.

So the project verifies **behaviour** instead of bytes. An R5900 interpreter runs
the original machine code, the reconstruction runs on the same inputs, and the two
are compared. A function is `verified` only when that test passes.

This is a weaker guarantee than byte-matching and the docs say so plainly: a
passing test shows agreement *on the inputs tested*, not equivalence in general.
What it buys is an automated check that fails loudly on a regression, which
untested source does not. It also caught a real interpreter bug - `beql`/`bnel`
nullify their delay slot when not taken - that inspection and synthetic unit tests
both missed.

For scale: comparable finished projects (Ocarina of Time ~3 years and 100+
contributors, Metroid Prime ~5 years) had *more* symbol information than this
fully stripped binary. This remains a multi-year effort, and the project reports
verified bytes rather than a flattering headline.

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
