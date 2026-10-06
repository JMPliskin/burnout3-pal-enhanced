# Burnout 3: Takedown

Reverse-engineering the PlayStation 2 release of *Burnout 3: Takedown*
(`SLUS_21050`, Criterion Games / Electronic Arts, 2004), working toward a
readable source reconstruction and eventually a native PC port. This is the
first game the [engine](../../engine) was built on.

## Where it stands

| | |
| --- | ---: |
| Executable analysed | `SLUS_210.50`, 4,073,088 bytes |
| Functions discovered | 9,279 |
| Image proven to be MIPS code | 87.1% |
| **Verified code** (readable, derived source) | **17.05% of code bytes** |
| **Machine-checked code** (verified, or lifted and checked) | **80.18% of code bytes** |

[docs/PROGRESS.md](docs/PROGRESS.md) is generated and has the live numbers.

The native port runs the whole game: boot and its movies, the frontend over
its video backgrounds, the garage, races, saves to a memory card kept as a
directory. Played in real time on a recent
laptop under `--gpu`, the game itself runs at full speed (57-60 vertical
blanks a second) and draws its 2D screens at 60 pictures a second, but its
3D scenes - the garage, the race - at 25-30: a picture every other vertical
blank, where the PS2 draws one every blank. VU1's microcode is the limit
(docs/FINDINGS.md, "A race frame, timed"; `--frame-times` reports both
rates). Missing: blend factors over 1 on the GPU (the race HUD's text is
slightly darker).

The original compiler is known (`MW MIPS C Compiler (2.4.1.01)`, Metrowerks
CodeWarrior, named in the executable's `.comment` section) but could not be
obtained, so this project verifies behaviour rather than bytes; see the engine's
[METHODOLOGY.md](../../engine/docs/METHODOLOGY.md).

## Setup

Supply your own disc image. `game.json` expects it at the repository root as
`Burnout 3 - Takedown.iso`.

```sh
python engine/tools/isotool.py extract "Burnout 3 - Takedown.iso" SLUS_210.50 -o games/burnout3/extracted/
python engine/tools/isotool.py extract "Burnout 3 - Takedown.iso" DATA/VDB.XML -o games/burnout3/extracted/
python engine/tools/analyze.py
python engine/tools/derive.py --emit --csv
python engine/tools/progress.py --markdown > games/burnout3/docs/PROGRESS.md
```

## Playing the port

With the disc image in place and the setup above done:

```sh
python engine/tools/lift.py --game games/burnout3 --structure --emit --csv    # the game's code as C++ (about an hour)
python engine/tools/port.py --game games/burnout3 build                        # Visual Studio's CMake and Ninja
python engine/tools/port.py --game games/burnout3 run -- --window --gpu --audio --memcard games/burnout3/build/memcard --timeout 86400
```

`--window` opens the game in a window (SDL3 and Vulkan), `--gpu` draws on
the GPU (without it the software GS draws, much more slowly), `--audio`
plays the sound, `--memcard DIR` gives it a memory card kept as files in
DIR, and `--timeout` is how many seconds it may run (the default suits
tests: 30). A game controller (any SDL3 knows: DualSense, DualShock 4,
Xbox, ...) is the DualShock 2, by button position - the bottom face button
is Cross, the triggers L2 and R2 (analog), both sticks analog. The keyboard
works too: arrows for the D-pad, Enter Start, Backspace Select, X Cross,
C Circle, Z Square, V Triangle, Q and E L1 and R1, 1 and 3 L2 and R2.

For speed, the VU microprograms the game runs are compiled ahead of time
from a recording (engine/docs/PORTING.md): record once, then build again.

```sh
burnout3.exe <extracted SLUS_210.50> --disc "Burnout 3 - Takedown.iso" $(cat games/burnout3/port/runs/race_training.args) \
    --vu-interpret --vu-record games/burnout3/build/vu_programs.txt
python engine/tools/port.py --game games/burnout3 build
```

## What is specific to this game

```
game.json      files, VU microcode region, memory layout for the tests
tools/vdb.py   reader for the ValueDB tuning database (DATA/VDB.XML)
tools/vdbhash.py  the recovered ValueDB name hash
include/, src/ hand-written reconstructions: string routines, bit utilities,
               the ValueDB hash, kernel syscall stubs
src/generated/ derived and verified functions, emitted by the engine
config/        function status table, syscall table
tests/         differential tests of the hand-written reconstructions
docs/          FINDINGS (what the binary revealed), PLAN, PROGRESS
```

`extracted/`, `build/` and `asm/` hold material taken or derived from the disc
and are never committed.
