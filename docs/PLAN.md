# Roadmap

The end goal is a readable source reconstruction and then a native PC port. The
path there is long; this lays out the phases honestly, including which ones are
blocked and why.

## Phase 0 - Extraction and analysis toolchain **[complete]**

Everything needed to get from a disc image to a structured map of the
executable, with no third-party dependencies.

* ISO9660 reader (`tools/b3lib/iso9660.py`) - verified against the retail disc
* ELF reader (`tools/b3lib/elf.py`)
* R5900 disassembler (`tools/b3lib/r5900.py`) - MIPS III plus the Emotion Engine
  extensions (MMI SIMD, 128-bit LQ/SQ, COP2/VU0 macro mode), 40 unit tests
* Function discovery (`tools/b3lib/analysis.py`)
* Symbol and cross-reference recovery (`tools/b3lib/symbols.py`)
* Progress accounting (`tools/progress.py`)

## Phase 1 - Static map of the executable **[complete]**

* 7,423 functions discovered; 81.0% of the image proven to be code
* 5,556 strings, 8,171 address cross-references, 95 pointer tables
* Annotated assembly generated for every function

## Phase 2 - Establish a matching toolchain **[compiler identified; sourcing blocked]**

Nothing can reach `matched` until this is finished, so it gates the entire
project. See [METHODOLOGY.md](METHODOLOGY.md#the-toolchain).

* [x] **Identify the original compiler.** The `.comment` section names it
      directly: `MW MIPS C Compiler (2.4.1.01)` / `PlayStation2` - Metrowerks
      CodeWarrior 2.4.1.01 for PS2. No fingerprinting was needed.
* [x] **Build the verification harness.** `tools/matchdiff.py` compares a
      candidate against the original image and scores it both exactly and
      semantically. Tested against known-good, near-miss and wrong-function
      inputs.
* [ ] **Obtain `mwccmips` 2.4.1.01.** A sourcing problem rather than a technical
      one: CodeWarrior for PS2 is abandoned proprietary software, unsold for
      about two decades. This is now the only thing standing between the project
      and verified progress.
* [ ] **Recover the optimisation flags**, calibrating against the kernel stubs in
      `src/kernel/syscalls.cpp`.
* [ ] **Success criterion: one function, byte-identical.**

If the compiler cannot be obtained, matching is not achievable - Metrowerks and
GCC differ systematically in register allocation and scheduling, so a GCC attempt
would stall short and never close. The honest fallback is then a **functionally
equivalent** reconstruction, which is a legitimate goal but a different and
weaker one, and should be chosen deliberately rather than arrived at by drift.

## Phase 3 - Runtime and library layer

The most tractable code, because much of it has a known public API.

* EE kernel wrappers, semaphores, DMA, interrupt handling
* RenderWare 3.6 - the binary names the exact SDK source files it links
  (`rwsdk/src/plcore/babinary.c`, `bamatrix.c`, `bacamera.c`, `batextur.c`,
  `driver/sky2/basky.c`, `pipe/p2/baim3d.c`, and others). The RenderWare API is
  documented, so these functions can be identified from their signatures rather
  than guessed at.
* libc / standard runtime routines, which have recognisable shapes

## Phase 4 - Engine systems

* The ValueDB tuning system - the binary references `../Export/ValueDB/Score.cfg`
  and hundreds of named tuning parameters (`Spring Coeff`, `Damping`,
  `Min/Max Impulse`, `Slip Speed Multipler`). `DATA/VDB.XML` on the disc is the
  authored side of the same system, which makes this unusually well-evidenced.
* File and streaming layer (`.RWS`, `.TXD`, `.BIN`, `.KFS` containers)
* Rendering pipeline, including the VU0/VU1 microcode paths (703 functions use
  VU0 macro mode and need hand review)
* Audio, input, save/memory-card, network

## Phase 5 - Game logic

Vehicle physics and damage, AI and traffic, crash/takedown scoring, race rules
and progression, front-end and UI.

## Phase 6 - PC port

Only meaningful once a substantial fraction is matched. Requires replacing the
PS2 hardware layer - GS rasteriser, VU pipelines, DMA, SPU2, controller - with
portable equivalents, which is a large project in its own right, comparable in
size to the decompilation itself.

## What is realistic

Phases 0 and 1 are done. Phase 2 is a focused, well-defined problem and is the
right next thing to attempt. Phases 3-5 are years of incremental
function-by-function work. Phase 6 follows from them.

Progress should be measured by matched bytes, reported by `tools/progress.py`,
and by nothing else.
