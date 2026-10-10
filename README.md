# Burnout 3: Takedown - PC port

**Burnout 3: Takedown, native on Windows.** The PS2 game's program translated
to C++ and compiled for the PC - not emulated: 60 frames a second at up to 4x
the PS2's resolution, widescreen with the HUD kept in place, PC settings in the
game's own menu, a mouse in the menus, controllers with rumble.

> **Unofficial and fan-made.** Not affiliated with, endorsed or sponsored by
> Electronic Arts or Criterion Games. You need your own copy of the game: a
> disc image made from your own Burnout 3: Takedown disc (NTSC-U, SLUS-21050).
> No game content is in this repository or in the release.

## Playing it

Download `Burnout3-PC-<version>-windows-x64.zip` from
[Releases](../../releases), unzip it somewhere you can write to, run
`burnout3.exe` and choose your disc image. It needs Windows 10 or 11 (64-bit),
a processor with AVX2 and a Vulkan 1.3 graphics driver - nothing to install.
The README in the zip has the controls, the PC settings and the known issues
([games/burnout3/port/release/README.txt](games/burnout3/port/release/README.txt)).

This is an **alpha**: please report problems in Issues, with the
`burnout3.log` written beside the executable.

## Building it

The port is made with [ps2-port-engine](https://github.com/danielgomesvieira2000/ps2-port-engine),
the game-independent toolchain and runtime, which is this repository's
`engine/` submodule (pinned to a tag):

```sh
git clone --recursive https://github.com/danielgomesvieira2000/burnout3-pc
cd burnout3-pc
# your disc image: "Burnout 3 - Takedown.iso" beside games/ (game.json's "iso")
python engine/tools/isotool.py extract "Burnout 3 - Takedown.iso" SLUS_210.50 -o games/burnout3/extracted/
python engine/tools/analyze.py --game games/burnout3
python engine/tools/derive.py  --game games/burnout3 --emit --csv
python engine/tools/lift.py    --game games/burnout3 --structure --emit --csv
python engine/tools/port.py    --game games/burnout3 build
python games/burnout3/port/tools/package.py        # the release zip
```

For full speed, record the game's VU microprograms once and build again - they
are compiled ahead of time from that list (without it they run interpreted,
VU1 about 1.5x slower): run `port/runs/car_select.args` and
`port/runs/race_training.args` with `--vu-interpret --vu-record
games/burnout3/build/vu_programs.txt` (the engine's docs/PORTING.md, "VU
microprograms compiled ahead of time").

The game's code in C++ is generated on your machine from your disc image and
never committed. Building needs Visual Studio 2022 with C++ (its MSVC, CMake
and Ninja) and Python 3.10+ (Pillow for the art scripts). See the engine's
[docs/PORTING.md](https://github.com/danielgomesvieira2000/ps2-port-engine/blob/master/docs/PORTING.md)
for every option of the port, and its skills (`engine/.claude/skills`) for the
workflow Claude Code follows here.

## What is where

```
engine/                     ps2-port-engine (submodule)
games/burnout3/
  game.json                 the game: its disc, executable, title, version
  port/                     the port: overrides.cpp (the game's own functions,
                            the host's way: PC SETTINGS, quit, movies, loading),
                            widescreen.cpp, mouse.cpp, the icon and pointer
                            (original art, drawn by tools/), release/, runs/
                            (the scripted test runs and their pictures' CRCs)
  config/                   the function table, names, syscalls
  docs/                     FINDINGS (what the game taught), PROGRESS, plans
  tests/, tools/            the game's own tests and tools
```

## Legal

This repository contains **no game assets, no disc images or BIOS files, and
none of the game's code.** The game's code in C++ - lifted, derived or
reconstructed - is generated on your own machine from your own disc image and
never committed (`.gitignore`). What the repository holds is original: the
port's code, the PC features and their art, documentation of how the game
works (addresses, function names, findings) and checksums of test pictures.

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
