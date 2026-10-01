# Roadmap

The end goal is a readable source reconstruction and then a native PC port. The
path there is long; this lays out the phases honestly, including which ones are
blocked and why.

## Phase 0 - Extraction and analysis toolchain **[complete]**

Everything needed to get from a disc image to a structured map of the
executable, with no third-party dependencies.

* ISO9660 reader (`engine/ps2decomp/iso9660.py`) - verified against the retail disc
* ELF reader (`engine/ps2decomp/elf.py`)
* R5900 disassembler (`engine/ps2decomp/r5900.py`) - MIPS III plus the Emotion Engine
  extensions (MMI SIMD, 128-bit LQ/SQ, COP2/VU0 macro mode), 40 unit tests
* Function discovery (`engine/ps2decomp/analysis.py`)
* Symbol and cross-reference recovery (`engine/ps2decomp/symbols.py`)
* Progress accounting (`engine/tools/progress.py`)

## Phase 1 - Static map of the executable **[complete]**

* 9,163 functions discovered; 86.2% of the image proven to be code
* 5,556 strings, 8,159 address cross-references, 355 pointer tables
* 108 KB of VU microcode located, plus a byte-level accounting of the whole image
* Annotated assembly generated for every function

## Phase 2 - Verification without the original compiler **[complete]**

The original compiler is identified but unobtainable, so the project has
deliberately switched from byte-matching to **functional reconstruction**, with
differential testing against an interpreter as the verification mechanism. See
[METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).

* [x] Identify the original compiler: `MW MIPS C Compiler (2.4.1.01)`, from
      `.comment`.
* [x] Decide the strategy deliberately. Matching is unreachable without
      `mwccmips`; chasing it with GCC would stall short and never close.
* [x] Build an R5900 interpreter (`engine/ps2decomp/emu.py`). Covers the integer core,
      common loads/stores, `lq`/`sq`, COP1 single precision and the needed MMI
      subset - enough to execute 80.9% of discovered functions.
* [x] Build the differential-test harness (`engine/ps2decomp/difftest.py`) and wire it
      into the test suite.
* [x] First verified functions: `valuedb::StringLength` and `valuedb::HashName`.
* [x] Automate the mechanical cases: symbolic derivation with differential
      verification (`symexec.py`, `paths.py`, `derive.py`). 327 functions derived
      and verified without being written by hand.
* [ ] Extend interpreter coverage to VU0 macro mode, the bulk of the remaining 15%.
* [ ] Loop summarisation, or accept that looping functions need hand reconstruction.
* [ ] Pass float arguments in the harness. Functions taking a float in `$f12` are
      currently refused rather than derived against a zero, which would pass
      vacuously.

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
* Rendering pipeline, including the VU0/VU1 microcode paths (796 functions use
  VU0 macro mode; `engine/tools/vudis.py` reads the microcode itself)
* Audio, input, save/memory-card, network

## Phase 5 - Game logic

Vehicle physics and damage, AI and traffic, crash/takedown scoring, race rules
and progression, front-end and UI.

## Phase 6 - PC port

Only meaningful once a substantial fraction is verified. Requires replacing the
PS2 hardware layer - GS rasteriser, VU pipelines, DMA, SPU2, controller - with
portable equivalents, which is a large project in its own right, comparable in
size to the reconstruction itself.

Functional reconstruction suits this endpoint better than byte-matching would
have: the goal is source that *runs*, not source that reassembles. The
differential tests carry over directly, since they check behaviour rather than
encoding.

## What is realistic

Phases 0, 1 and 2 are done: the executable is mapped, and there is now an
objective way to verify a reconstruction without the original compiler. Phases 3-5
are years of incremental function-by-function work. Phase 6 follows from them.

Progress is measured by verified bytes, reported by `engine/tools/progress.py`, and by
nothing else. 285 functions are verified so far, 0.5539% of code bytes, and that
figure is deliberately not flattered by counting untested source.

Most of that came from tooling rather than from reconstructing functions one by one,
which is the only way the remaining 99% becomes tractable. The limits are now
concrete: loops, calls into underived functions, and VU0 macro mode.
