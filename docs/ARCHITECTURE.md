# Toolchain architecture

Python 3.10+, standard library only. No third-party packages, so the toolchain
runs anywhere Python does and cannot rot when a dependency changes.

## Why Python for tools, C++ for sources

**Tools in Python.** The analysis passes are iterative and exploratory - you
rerun them constantly while refining heuristics. Fast iteration matters more
than runtime speed, and the whole pipeline over a 4 MB image finishes in about
five seconds.

**Sources in C++17.** The game is C++, and the RenderWare 3.6 layer beneath it
is C. C++ is the only choice that reproduces the original structure honestly,
and it compiles on a modern host for the eventual port. C would force the class
hierarchies to be faked; anything higher-level could not match.

## Module layout

```
tools/b3lib/
  iso9660.py   ISO9660 reader: directory tree, random access, extraction
  elf.py       32-bit little-endian ELF reader
  r5900.py     Emotion Engine disassembler
  vu.py        Vector Unit microcode disassembler
  analysis.py  function discovery and code/data classification
  symbols.py   strings, address cross-references, pointer tables
  vdbhash.py   the recovered ValueDB name hash
  emu.py       R5900 interpreter, for verification
  symexec.py   symbolic execution of branch-free functions
  difftest.py  differential-test harness

tools/
  isotool.py    disc inspection and extraction
  analyze.py    full static-analysis pipeline
  disasm.py     annotated assembly emitter
  vudis.py      VU microcode disassembler driver
  vdb.py        ValueDB container reader
  layout.py     byte-level accounting of the image
  candidates.py ranks functions by reconstructability
  derive.py     derives, verifies and emits accessor functions
  matchdiff.py  byte-comparison, kept for if the compiler is ever obtained
  progress.py   progress accounting
```

Each layer depends only on the ones above it, and none of them depend on the
CLI drivers.

## `emu.py` - the R5900 interpreter

This is what makes verification possible without the original compiler. It
executes the integer core, common loads and stores, 128-bit `lq`/`sq`, a COP1
single-precision subset, and the MMI operations the string routines need - enough
for 84.8% of discovered functions. `$gp` is seeded with `0x004E8670`, recovered
from the startup code, because a great many functions reach globals through it.

Anything outside the subset raises `UnsupportedInstruction`. That refusal is the
design: an interpreter that quietly approximated an instruction would hand back
confident wrong answers, which is worse than a gap.

Two correctness details that cost real debugging:

* **Branch-likely nullification.** `beql`/`bnel` skip their delay slot when *not*
  taken. Executing it unconditionally corrupted a mask register inside the SIMD
  `strlen` and made it return wrong lengths for every input.
* **Quadword halves.** `lq`/`sq` must preserve the upper 64 bits of a register,
  because Metrowerks spills callee-saved registers with them. Dropping those bits
  corrupts every restore.

## `symexec.py` and `derive.py` - deriving accessors

Roughly 150 discovered functions are tiny branch-free accessors. Rather than read
each by hand, `symexec.py` walks one with expressions in the registers instead of
values and reports what ends up in `$v0`, plus any stores as
(address, value, size) triples.

`derive.py` then differential-tests each derivation against the original machine
code - for stores, by snapshotting memory, running the original, and requiring a
replay of the derived writes to reproduce the same bytes - and emits C++ only for
derivations that agreed.

Three traps here are worth recording, because all three produced plausible-looking
wrong output:

* **Operator precedence.** An expression tree evaluates from its structure, so a
  rendering bug leaves the arithmetic correct while the printed C means something
  else: `arg1 << 2 + arg0` is `arg1 << (2 + arg0)` in C. Rendering is
  precedence-aware and tested.
* **Store ordering.** Every derived expression refers to memory as it stood on
  entry, so emitting stores before the return inverts a read-modify-write. Values
  are captured into temporaries first.
* **Incomplete signatures.** Parameter lists built from the return expression alone
  omit arguments used only inside a store. `check_signatures()` now refuses to
  write source that references an undeclared parameter.

## `r5900.py` - the disassembler

A stock MIPS disassembler mis-decodes a real PS2 binary, because the R5900 is
MIPS III plus Sony/Toshiba extensions. The ones that matter:

* **MMI** (opcode `0x1C`) - 128-bit integer SIMD, with four nested sub-tables
  (MMI0-MMI3) selected by the shift-amount field, plus `PMFHL`/`PMTHL` and the
  parallel shifts. The entry point alone is 31 `padduw` instructions, so getting
  this wrong derails decoding immediately.
* **`LQ`/`SQ`** (opcodes `0x1E`/`0x1F`) - 128-bit load/store.
* **COP2** - on the EE this is the VU0 macro-mode interface, not a conventional
  coprocessor.
* **MIPS III 64-bit arithmetic**, fully implemented on the EE.

`decode(word, addr)` is a pure function, so analysis passes can decode the same
address repeatedly without bookkeeping. Each `Instruction` carries control-flow
flags (branch, call, return, delay slot, likely-branch) that the later passes
run on.

Correctness is covered by 40 unit tests in `tests/test_r5900.py`, hand-assembled
from the instruction-set documentation so the expected values are independent of
the decoder. Writing them caught a real bug: COP1 packs its register fields
differently from the integer unit (`fd` in the shift-amount field, `fs` in `rd`),
which a shared operand formatter had got wrong.

## `analysis.py` - function discovery

The binary has no symbols and no section boundaries, so everything is recovered.
The passes run in order of decreasing confidence, which keeps false positives
down:

1. **Call-target harvesting.** Every `jal` names a function entry directly. The
   compiler only emits `jal` for real call sites, so a `jal` landing inside the
   image is near-proof. 40,430 call sites resolve to 5,677 accepted targets.
2. **Recursive traversal.** From the entry point and every harvested target, walk
   the control-flow graph, following branches and claiming delay slots, stopping
   at returns. This marks bytes *provably* reachable as code, rather than bytes
   that merely happen to decode.
3. **Pointer-table targets.** Virtual calls and callbacks emit no `jal` at all, so
   their targets are invisible to step 1. Every aligned word pointing inside the
   image is considered, then filtered hard: the target must land on a stack
   prologue or just past another function's terminator, decode as a run of real
   instructions, and **end in `jr $ra`**. That last test is what separates code
   from data which merely decodes - a virtual method has to return. 2,597 accepted,
   37 rejected for never returning.
4. **Prologue scanning.** A final `addiu $sp, $sp, -N` sweep catches the remainder:
   888 more.
5. **Extent from contiguous code.** A function's end is the unbroken run of its own
   instructions from its entry, bridging only short all-nop alignment padding.
   Taking the furthest address *reached* instead lets one tail jump swallow
   everything in between - which it did, until it was fixed.

Each function records *why* it was classified, so results stay auditable rather
than being an opaque guess.

Current result: **9,163 functions**, 86.2% of the image proven to be MIPS code,
99.67% of functions ending in a return, and 0 of 814,598 instructions failing to
decode. The coverage figure is a **lower bound** - it counts only bytes proven
reachable, and a further 2.65% of the image is VU microcode rather than MIPS.

Two false-positive traps were worth closing:

* A zero word decodes as a perfectly valid `nop`, so any test that only asks "does
  this decode?" accepts a block of padding as a function. `_looks_like_code` also
  requires real work - at least two non-`nop` instructions and a majority of
  non-padding.
* Seventeen "functions" were sitting inside the read-only data block, decoding
  tables of small integers as instructions on `$zero`, and one pointer array that
  pointed at itself. The return requirement removed all of them.

## `symbols.py` - naming leverage

Three reliable kinds of leaked information:

* **String cross-references.** MIPS materialises a 32-bit address with a
  `lui`/`addiu` pair, or `lui` plus a load offset. Reassembling those recovers
  the constant; when it lands in a string, the call site gains a strong
  human-readable hint. Register tracking resets at any control transfer, so a
  `lui` whose partner sits past a branch is dropped rather than mispaired.
* **RenderWare provenance.** RCS `$Id:` tags name the exact middleware sources
  linked in, identifying which regions are documented-API middleware rather than
  game code.
* **Pointer tables.** Runs of consecutive words pointing at known function
  entries are vtables or jump tables, which reveal C++ class layout. Requiring
  several consecutive valid entries keeps random data from registering.

Everything here generates *hints*. Nothing renames a function without a recorded
reason.

## Data flow

```
disc image
   |  isotool.py
extracted/SLUS_210.50
   |  analyze.py   (elf -> analysis -> symbols)
build/slus_210_50.json      functions, regions, strings, xrefs, tables
   |                     \
   |  disasm.py           \  progress.py  + config/functions.csv
asm/*.s                     docs/PROGRESS.md
```

The database is generated once and everything downstream reads it, so the
expensive passes are not repeated. It is not committed - it is reproducible from
a disc image in about five seconds.
