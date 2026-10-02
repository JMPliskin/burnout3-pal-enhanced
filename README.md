# PS2 decompilation by functional reconstruction

A toolchain for decompiling PlayStation 2 games into readable C++ when the
original compiler is unavailable. Byte-matching is not possible without that
compiler, so this checks **behaviour** instead. An R5900 interpreter runs the
original machine code, and every C++ function is compiled and run against it on
the same inputs. A function counts only when they agree.

There are two tiers. **Derived** functions are symbolic expressions turned into
readable C++. **Lifted** functions are translated instruction by instruction,
then structured (loops, ifs, named locals, temporaries folded). Lifting covers
nearly every function, so together they give a complete translation. Each
function says whether it was verified, and the progress report gives the
readable and the machine-checked share separately.

The engine was built on *Burnout 3: Takedown*, the first game here. Its code is
game-independent: a game is a directory with a `game.json`.

```
engine/            the game-independent toolchain
  ps2decomp/       library: ELF/ISO readers, R5900 + VU disassemblers, analysis,
                   interpreter, symbolic derivation, struct recovery, C++ check
  tools/           command-line drivers (analyze, derive, progress, ...)
  runtime/         headers and sources the generated C++ compiles against
  tests/           unit tests
  docs/            METHODOLOGY (what "verified" means), ARCHITECTURE
games/
  burnout3/        one game: game.json, its sources, docs, game-specific tools
```

## Using it with Claude Code

`.claude/skills/ps2-decomp/SKILL.md` is a Claude Code skill. It covers the
workflow: adding a game, the improvement loop, the rules verification depends
on, and the R5900 details that have caused silent bugs. Claude Code loads it when
working in this repository.

## Requirements

* Python 3.10+, standard library only
* MSVC (found through `vswhere`) for the compiled check; without it, derived
  functions are verified as expression trees only, and the run says so
* A disc image you own, for each game

## Adding a game

1. Make `games/<name>/game.json`:

   ```json
   {
     "name": "Some Game (NTSC-U)",
     "iso": "../../Some Game.iso",
     "executable": "SLUS_123.45"
   }
   ```

   Paths are relative to the game's directory. The defaults are `extracted/`,
   `build/functions.json`, `src/generated/` and `config/functions.csv`. The
   global pointer is detected from the entry code, and the image bounds come from
   the ELF. Optional `layout` overrides set the test memory layout (`scratch`,
   `stack_top`, ...; see `engine/ps2decomp/layout.py`), and `vu_regions` lists VU
   microcode ranges as `[start, end]` pairs.

2. Extract the executable, analyse it, and derive:

   ```sh
   python engine/tools/isotool.py extract "Some Game.iso" SLUS_123.45 -o games/<name>/extracted/
   python engine/tools/analyze.py --game games/<name>
   python engine/tools/signatures.py --game games/<name> name
   python engine/tools/derive.py --game games/<name> --emit --csv
   python engine/tools/lift.py --game games/<name> --structure --emit --csv
   python engine/tools/progress.py --game games/<name>
   ```

Every tool takes `--game`. Without it, a tool uses `$PS2_GAME`, or the only game
under `games/`.

## Tests

```sh
python -m unittest discover -s engine/tests
python -m unittest discover -s games/burnout3/tests   # skips without extracted files
```

## Legal

This repository contains **no game code, assets or binaries**. It contains
original tooling and reverse-engineered documentation. Running it requires a disc
image you already own. That image, and anything extracted or derived from it, is
excluded from version control.

Reverse engineering for interoperability and preservation is protected in many
jurisdictions. This project follows the conventions of the wider game
preservation community: it redistributes no copyrighted material and does not
try to substitute for the commercial product.
