# PS2 decompilation by functional reconstruction

**Burnout 3: Takedown, native on Windows.** The first game this toolchain
was built on now runs as a native PC port: 60 frames a second at up to 4x the
PS2's resolution, widescreen with an editable HUD, a mouse in the menus,
controller rumble, PC settings in the game's own menu.

> **Unofficial and fan-made.** Not affiliated with, endorsed or sponsored by
> Electronic Arts or Criterion Games. You need your own copy of the game: a
> disc image made from your own Burnout 3: Takedown disc (NTSC-U, SLUS-21050).
> No game content is in this repository or in the release.

## Playing the PC port

Download `Burnout3-PC-<version>-windows-x64.zip` from
[Releases](../../releases), unzip it somewhere you can write to, run
`burnout3.exe` and choose your disc image. It needs Windows 10 or 11 (64-bit),
a processor with AVX2 and a Vulkan 1.3 graphics driver. The README in the zip
has the controls, the PC settings and the known issues
([games/burnout3/port/release/README.txt](games/burnout3/port/release/README.txt)).

This is an **alpha**: please report problems with the `burnout3.log` written
beside the executable.

To build it yourself, see [engine/docs/PORTING.md](engine/docs/PORTING.md):
the pipeline below regenerates the game's code from your disc image, then
`python engine/tools/port.py --game games/burnout3 build`, and
`python games/burnout3/port/tools/package.py` makes the release zip.

## The toolchain

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
   python engine/tools/port.py --game games/<name>      # build and run natively
   ```

The last step builds the translated code into a native executable on the host in
`engine/host/` and runs it on the game's own executable, reporting how far it
gets. See [engine/docs/PORTING.md](engine/docs/PORTING.md).

Every tool takes `--game`. Without it, a tool uses `$PS2_GAME`, or the only game
under `games/`.

## Tests

```sh
python -m unittest discover -s engine/tests
python -m unittest discover -s games/burnout3/tests   # skips without extracted files
```

## Legal

This repository contains **no game assets, no disc images or BIOS files, and
none of the game's code.** The game's code in C++ - lifted, derived or
reconstructed - is generated on your own machine from your own disc image by
the tools here, and is never committed (`.gitignore`). What the repository
holds is original: the toolchain, the port's engine, the PC features and their
art (the icon and the mouse pointer are drawn by scripts here), documentation
of how the game works (addresses, function names, findings), and checksums of
test pictures.

The Windows release contains the game's program translated to C++ and
compiled, with no game assets: it reads everything it plays from the player's
own disc image, and refuses an image that is not the game's disc.

Reverse engineering for interoperability and preservation is protected in many
jurisdictions. This project follows the conventions of the wider game
preservation community: it redistributes no game assets and does not try to
substitute for the commercial product. "Burnout" is a trademark of Electronic
Arts Inc.

The project's own code is under the MIT licence ([LICENSE](LICENSE)). The port
is built with SDL 3, volk, glslang and the Vulkan headers, under their own
licences (the release's THIRD-PARTY-NOTICES.txt).
