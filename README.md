# Burnout 3: Takedown — PAL Enhanced

Burnout 3 on Windows, with the original European experience: choose your language,
pick 50 or 60 Hz, and take to the road. PAL Enhanced builds on
[Daniel Gomes Vieira's Burnout 3 PC port](https://github.com/danielgomesvieira2000/burnout3-pc)
and [ps2-port-engine](https://github.com/danielgomesvieira2000/ps2-port-engine).

## How to Play

1. Open the [v0.1.0-alpha Release](https://github.com/JMPliskin/burnout3-pal-enhanced/releases/tag/v0.1.0-alpha).
2. Under **Assets**, download `Burnout3-PAL-Enhanced-0.1.0-alpha-windows-x64.zip`.
3. Extract the ZIP into a folder of your choice.
4. Double-click `Burnout3-PAL-Enhanced.exe`.
5. On first launch, select an ISO from your own compatible European PS2 copy of
   Burnout 3: Takedown, identified as `SLES_525.84`.
6. Choose your language and 50/60 Hz mode, then play normally.

**Use the Windows ZIP from Releases, not Code > Download ZIP**, which contains
only source code. You do not need Python, CMD, PowerShell or a BAT launcher.

Requirements: Windows 10 1903 or newer (x64), an AVX2 CPU and a Vulkan-capable GPU
with current drivers.

The player remembers your ISO's location and reuses it while the file remains
available and compatible. If you move or rename the ISO, or it becomes
unavailable, the file selector opens again. Changing the ISO path does not remove
your saves. The player does not move, copy or search your computer for the ISO.

### Saves and Settings

Persistent data is stored in `%LOCALAPPDATA%\Burnout3-PAL-Enhanced\`, not inside
the downloaded ZIP or normally beside the EXE:

| Item | Contents |
| --- | --- |
| `pc_settings.ini` | Settings and the selected ISO's location |
| `memcard\` | Virtual memory card, profiles and saves |
| `logs\` | Local program logs |

### Moving or Updating the Game

You can move the extracted game folder elsewhere. Settings and saves stay in
LocalAppData and are reused by the same Windows user. If your ISO is still in its
usual location, you will not need to select it again.

For a future update, extract the new package and use its compatible EXE without
deleting your personal data. Back up `memcard` before updating or importing saves,
and check that version's compatibility notes; compatibility across all future
versions or save formats is not guaranteed.

Data belongs to your Windows user profile. Moving the EXE to another computer
does not automatically transfer your saves or settings.

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

Development source now also includes a separately tested Hor+ 3D widescreen
camera and proportion-preserving HUD anchors for 4:3, 16:9 and Fill the window.
These source changes are not part of the unchanged v0.1.0-alpha download.
See the [development status](docs/pal/STATUS.md) for scope and validation.

## Known Limitations

This is an early alpha. Performance varies and not every mode has been tested.
The v0.1.0-alpha executable retains GPU diagnostic instrumentation; an older build's
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
Reproducing the published alpha uses its documented diagnostic patch; the clean
widescreen integration does not require it. Its optional regression targets are
described in the [widescreen guide](docs/pal/WIDESCREEN_HUD.md).
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
