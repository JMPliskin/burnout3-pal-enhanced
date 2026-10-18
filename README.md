# Burnout 3: Takedown — PAL Enhanced

Burnout 3 on Windows, with the original European experience: choose your language,
pick 50 or 60 Hz, and take to the road. PAL Enhanced builds on
[Daniel Gomes Vieira's Burnout 3 PC port](https://github.com/danielgomesvieira2000/burnout3-pc)
and [ps2-port-engine](https://github.com/danielgomesvieira2000/ps2-port-engine).

## How to Play

**v0.1.0-alpha has passed functional validation. Binary publication is pending distribution review.**
Use the Windows ZIP from [GitHub Releases](https://github.com/JMPliskin/burnout3-pal-enhanced/releases)
once published. GitHub's Source Code download cannot be launched as a game.

For an approved Windows package:

1. Extract it into a folder and open `Burnout3-PAL-Enhanced.exe`.
2. Select an ISO made from your own compatible European PS2 disc (`SLES_525.84`).
3. Choose your language and video mode using a controller or keyboard.

The player remembers your ISO. If it moves, you will be asked to select it again.
No Python or BAT launcher is required. Windows 10 1903 or newer (x64), an AVX2 CPU
and a Vulkan-capable GPU with current drivers are required.

Settings and saves are stored in `%LOCALAPPDATA%/Burnout3-PAL-Enhanced/`.
Back up the `memcard` folder before importing saves or changing builds.

## Features

- Original PAL language and 50/60 Hz selection; Spanish has been tested.
- Persistent profiles through a virtual memory card.
- Keyboard and controller support, including a tested Xbox One USB controller.
- Vulkan rendering, configurable internal resolution and working 16:9 output.
- Compiled PAL VU programs for smoother 3D gameplay.
- Working audio and FMV; press Start to skip non-looping movies.
- Shared F9 settings panel and frame pacing.

The rebuilt alpha has been tested through races with GPU Thread On. A clean
installation, creating and saving a new profile, profile recovery after restart
and remembering the ISO have been confirmed by the maintainer.

## Known Limitations

This is an early alpha. Performance varies and not every mode has been tested.
The current build retains GPU diagnostic instrumentation; an older build's
GPU Thread freeze remains unexplained. The current rebuilt alpha passed manual
gameplay validation; this does not establish compatibility with every system.

Additional USA-specific graphics options, PC SETTINGS extensions, mods and LAN
have not been validated for PAL.

## Building from Source

You need Python 3.12, MSVC/Windows SDK, CMake, Ninja, the pinned engine submodule
and your own PAL game inputs. Game-derived EE/VU code is generated locally and
is not included in this repository.

The current reconstruction workflow also requires preserved local generation
artifacts and cached dependency sources; a fresh clone is not a turnkey build.
Follow the [reconstruction guide](games/burnout3-pal/port/diagnostics/README.md)
and [recovery documentation](games/burnout3-pal/port/RECOVERY.md).
Technical notes and release preparation are kept in [docs/pal](docs/pal).

## AI-Assisted Development

Burnout 3 PAL Enhanced is developed and maintained by JMPliskin with programming,
reverse-engineering, testing and debugging assistance from OpenAI ChatGPT and Codex.

The project builds on Daniel Gomes Vieira's original Burnout 3 PC port and
ps2-port-engine, whose development also uses AI-assisted tools. The PAL adaptation,
development decisions and gameplay validation are directed and reviewed by the
project maintainer.

## Credits & Legal

- Original Burnout 3 PC port: **Daniel Gomes Vieira**.
- Original shared engine: **ps2-port-engine and its contributors**.
- PAL Enhanced project: **JMPliskin**.
- AI development assistance: **OpenAI ChatGPT and Codex**.
- Thanks to all upstream contributors and third-party projects; their notices
  and licenses are retained.

Original port code is covered by the [MIT license](LICENSE). This license does
not grant rights to Burnout 3's original game code or assets. No disc images,
game resources, translated game sources or saves are provided here.

Unofficial fan project, not affiliated with Electronic Arts or Criterion Games.
Burnout is a trademark of Electronic Arts. No authorization from the game's
rights holders is claimed; distributing translated game code carries unresolved
rights considerations distinct from the port's MIT license.
