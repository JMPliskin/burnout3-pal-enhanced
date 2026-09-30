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
  analysis.py  function discovery and code/data classification
  symbols.py   strings, address cross-references, pointer tables
```

Each layer depends only on the ones above it, and none of them depend on the
CLI drivers.

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
   image is near-proof. Found 40,430 call sites resolving to 6,101 distinct
   targets.
2. **Recursive traversal.** From the entry point and every harvested target,
   walk the control-flow graph, following branches and claiming delay slots,
   stopping at returns. This marks bytes *provably* reachable as code, rather
   than bytes that merely happen to decode.
3. **Prologue scanning.** In the gaps, look for `addiu $sp, $sp, -N`. This
   catches functions reached only through function pointers - vtables, jump
   tables, callbacks - which a 2004 C++ game uses heavily. Found 1,746 more.
4. **Bounds tightening.** Clamp each function to end no later than the next
   function's start.

Candidate targets are sanity-checked before acceptance (425 were rejected), and
each function records *why* it was classified, so results stay auditable rather
than being an opaque guess.

The resulting 81.0% code coverage is a **lower bound**: it counts only bytes
proven reachable.

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
