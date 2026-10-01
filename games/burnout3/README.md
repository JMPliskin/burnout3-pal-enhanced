# Burnout 3: Takedown

Reverse-engineering the PlayStation 2 release of *Burnout 3: Takedown*
(`SLUS_21050`, Criterion Games / Electronic Arts, 2004), working toward a
readable source reconstruction and eventually a native PC port. This is the
first game the [engine](../../engine) was built on.

## Where it stands

| | |
| --- | ---: |
| Executable analysed | `SLUS_210.50`, 4,073,088 bytes |
| Functions discovered | 10,032 |
| Image proven to be MIPS code | 86.4% |
| VU microcode located | 108,096 bytes |
| Functions derived and verified automatically | 2,811 |
| **Verified code** | **8.06% of code bytes** |

[docs/PROGRESS.md](docs/PROGRESS.md) is generated and has the live numbers.

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
