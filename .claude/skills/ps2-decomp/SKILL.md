---
name: ps2-decomp
description: Decompile a PlayStation 2 game into verified, readable C++ with this repository's engine - setting up a new game, running the derive/verify pipeline, triaging why functions are refused, extending the engine, and reporting progress honestly. Use when working on any game under games/, adding a game, or changing engine/.
---

# PS2 decompilation by functional reconstruction

This repository turns a PS2 executable into C++ whose behaviour has been checked
against the original machine code. There is no original compiler, so nothing is
byte-matched. Instead every function goes through the same check:

1. The original code runs in an R5900 interpreter (`engine/ps2decomp/emu.py`).
2. A symbolic derivation of the same function runs on the same inputs.
3. The C++ emitted from that derivation is compiled and run on them too.

A function counts as **verified** only when all three agree on the return value
and on every byte of memory written.

There is a second, mechanical tier. `engine/tools/lift.py` translates every
function instruction by instruction into C++ over a register context and verifies
it the same way, comparing the whole register file. Lifted code is complete and
correct but not readable, so it is the `lifted` tier, reported apart. Derivation
then turns lifted functions into readable ones over time. Read `engine/docs/METHODOLOGY.md` before
changing what "verified" means, and `engine/docs/ARCHITECTURE.md` before changing
the engine.

## Non-negotiable rules

- **Only verified code counts.** Never report a derived-but-untested or
  reconstructed-but-untested function as progress. The headline number is
  verified code bytes from `engine/tools/progress.py`, never a function count.
- **Refuse rather than approximate.** When the interpreter or the deriver meets
  something it does not model, it raises (`UnsupportedInstruction`,
  `Unsupported`). Keep it that way. A quiet approximation hands back confident
  wrong answers, and the tests cannot catch what they never exercise.
- **Every engine change is a regression risk for every function.** After
  changing `engine/`, run both test suites and the full pipeline. Compare the
  three numbers (derived, compiled agree, verified %) with the last commit. A
  drop is a bug even if the new feature works.
- **Never commit disc material.** The ISO, `extracted/`, `build/` and `asm/` are
  gitignored. Check `git diff --cached --name-only` before every commit.
- **The engine stays game-independent.** Facts about one game go in its
  `game.json` or its own directory, never as constants in `engine/`.

## Repository map

```
engine/ps2decomp/   the library (interpreter, derivation, struct recovery, ...)
engine/tools/       CLI drivers; every one takes --game (default: $PS2_GAME, or
                    the only directory under games/)
engine/runtime/     ps2rt headers and eefpu.cpp the generated C++ compiles against
engine/tests/       unit tests: python -m unittest discover -s engine/tests
games/<name>/       game.json, src/, include/, config/functions.csv, docs/,
                    tests/, and (gitignored) extracted/ build/ asm/
```

## Adding a new game

1. Ask the user for the disc image. Never look for one online.
2. Find the executable's name with
   `python engine/tools/isotool.py list "<iso>"` (`SLUS_xxx.xx`, `SLES_...`,
   `SCUS_...`; `SYSTEM.CNF` names it on the `BOOT2` line).
3. Create `games/<name>/game.json` with at least `name`, `iso` (relative to the
   game directory) and `executable`. Leave `layout` out at first. The image bounds
   come from the ELF, and `$gp` is detected from the entry code. If
   `engine/ps2decomp/game.py`'s `detect_gp` returns None, find `$gp` by hand in
   the entry code and set `"layout": {"gp": "0x..."}`.
4. Extract and analyse:
   ```sh
   python engine/tools/isotool.py extract "<iso>" <EXECUTABLE> -o games/<name>/extracted/
   python engine/tools/analyze.py --game games/<name>
   python engine/tools/imagemap.py --game games/<name>
   ```
   If imagemap shows a large `data` region in the middle of code, look for VU
   microcode (`python engine/tools/vudis.py ... --stats`) and add it to
   `vu_regions` in `game.json`.
5. Check the test memory layout. The defaults put scratch memory at 0x01800000
   and the test stack at 0x01F00000. If the executable or its BSS reaches into
   those, override `scratch`/`stack_top` in `layout` to free addresses below 32 MB.
6. Run the pipeline, then record progress:
   ```sh
   python -u engine/tools/derive.py --game games/<name> --emit --csv > games/<name>/build/derive.log
   python engine/tools/progress.py --game games/<name> --markdown > games/<name>/docs/PROGRESS.md
   ```
   A full run on a 4 MB executable takes about 14 minutes. Run it in the
   background, and never start two runs at once: they write the same outputs.
   Then lift, after derivation, because lifted rows yield to derived ones:
   ```sh
   python -u engine/tools/lift.py --game games/<name> --emit --csv > games/<name>/build/lift.log
   ```
7. Name what the evidence supports, and share it:
   ```sh
   python engine/tools/signatures.py --game games/<name> name     # config/names.csv
   python engine/tools/signatures.py --game games/<name> export   # into the library
   ```
   `name` names kernel stubs from `engine/data/ee_syscalls.csv`, functions that
   print their own name in diagnostics, and functions whose relocation-masked
   signature is already named in `engine/signatures/library.json`. Every name
   records its source. Run `export` after names improve, so the next game
   benefits. `compare --elf <other.elf>` measures how much code two executables
   share, such as the SDK.
8. Start `games/<name>/docs/FINDINGS.md` with what the binary revealed: the
   compiler (`.comment` section), middleware (RCS `$Id:` strings), the SDK
   version, and the VU regions.

## The improvement loop

Progress comes from extending the engine so whole classes of refused functions
start to verify. Work the biggest class first:

1. `python engine/tools/refusals.py --game <dir>` groups every function that was
   not derived by the deriver's own reason. Use `--detail "<category>"` for the
   exact messages.
2. `python engine/tools/blockers.py --game <dir> --single` classifies each
   function by the full set of obstacles it contains. It counts the functions
   blocked by exactly one obstacle, since those are the only ones a single fix
   can reach. Removing the most common *first* obstacle has repeatedly unlocked
   nothing.
3. Pick the largest category with a sound fix. Write a failing unit test in
   `engine/tests/` from hand-assembled words first, then implement the fix.
4. To debug a single function, use
   `python engine/tools/inspect_function.py --game <dir> 0xADDRESS`. It shows the
   emitted C++ and, for each disagreeing case, what the original, the model and
   the compiled code produced.
5. Run both test suites and the full pipeline. Compare the numbers, then commit
   with the before and after numbers in the message, and add a section to the
   game's FINDINGS.md when the change taught something about the binary.

Hand-written reconstructions of functions the deriver cannot do (SIMD string
routines, hashes) go in `games/<name>/src/`. Verify them with
`engine/ps2decomp/difftest.py` in `games/<name>/tests/test_reconstruction.py`.

## R5900 facts that cause silent bugs

Each of these has already produced plausible wrong output once:

- Word operations (`addu`, `addiu`, `sll`, `lw`, ...) sign-extend their 32-bit
  result into the 64-bit register, and `lui` is sign-extended too.
- Word shifts mask the amount to 5 bits; doubleword shifts mask it to 6.
- `beql`/`bnel` (branch-likely) execute their delay slot only when taken.
- The EE FPU is not IEEE-754: there are no infinities or NaNs, and overflow
  saturates to the largest magnitude. Keep float bits as u32, never as host
  doubles.
- `lq`/`sq` move 128 bits. Callee-saved registers are spilled with them, so their
  upper halves must survive.
- EABI passes 8 integer arguments (`$a0`-`$a3`, `$t0`-`$t3`) and 8 float
  arguments (`$f12`-`$f19`). Liveness decides which ones a function reads.
- In emitted C++, operator precedence and store order are where bugs hide. The
  tree can be right while the printed C means something else. This is why the
  compiled check exists; never skip it when MSVC is available.

## Working practices that paid off

- Make tests deterministic: seed every random input from the function address,
  and reset all registers on every interpreter call.
- When derivation and the interpreter share a bug, they agree and "verify"
  wrong code (`sqrt.s` read fs instead of ft in both). Check the encoding of every
  new instruction against the EE manual, not against the disassembler alone. The
  lifter's whole-register comparison is a second line of defence.
- Profile before optimising. Most slowness came from repeated work: uncached
  catalog lookups, re-deriving unchanged functions, re-running the original for
  the compiled check.
- Patch Python files with the Edit tool or a script written to a file. Shell
  heredocs have turned `\b`, `\x00` and `\\n` into control bytes. Some files are
  CRLF: match patterns against the raw bytes.
- Document as you go. FINDINGS.md records what the binary taught; METHODOLOGY.md
  records what verification means; PROGRESS.md is generated, never hand-edited.
