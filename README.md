# Burnout 3 PAL Enhanced

**v0.2.0-alpha — Windows x64**

The European Burnout 3 experience on PC, with modern graphics settings and the
original PAL language and 50/60 Hz screens. PAL Enhanced is an unofficial
adaptation of [Daniel Gomes Vieira's Burnout 3 PC port](https://github.com/danielgomesvieira2000/burnout3-pc),
built on [ps2-port-engine](https://github.com/danielgomesvieira2000/ps2-port-engine).
Supply your own compatible European PS2 ISO: **SLES_525.84** (SLES-52584).
Spanish has been tested. This early alpha does not certify every configuration.

## Download and installation


1. Download `Burnout3-PAL-Enhanced-0.2.0-alpha-windows-x64.zip`.
2. Extract the complete ZIP into a normal local folder.
3. Double-click **Burnout3-PAL-Enhanced.exe**.
4. Select an ISO from your own compatible European copy when prompted.
5. Follow the original language and PAL 50/60 Hz screens.

Use the Windows ZIP, **not Code → Download ZIP**, which contains source code.
An approved player package needs no Python, compiler, terminal or BAT launcher.
The adjacent `.zip.sha256` file identifies the ZIP; `SHA256SUMS.txt` inside it
identifies the EXE.

### Requirements

- Windows 10 version 1903 or newer, **64-bit**.
- An **AVX2-capable CPU**, with operating-system support for AVX state.
- A Vulkan 1.3-capable GPU with a current driver.
- Your own compatible PAL ISO. No ISO, BIOS, extracted assets or saves are bundled.

The Visual C++ runtime is linked statically in the prepared package. Vulkan's
loader and driver must be available through your graphics driver installation.

### Updating and player data

Extract the new version separately and start its EXE. **Do not delete or replace
your LocalAppData folder.** Data stays in
`%LOCALAPPDATA%\Burnout3-PAL-Enhanced\`:

| Path | Contents |
| --- | --- |
| `pc_settings.ini` | PC preferences and the selected ISO location |
| `memcard\` | Virtual memory cards, profiles and saves |
| `logs\` | Per-run local logs |

Existing preferences override new defaults: an explicit windowed preference stays
windowed. New profiles start fullscreen, at 2×, 16:9, Frame Pacing ON, VSync OFF
and additional AA OFF.

Back up `memcard` before updating or importing saves. Previously validated profile
and preference workflows are retained; arbitrary external card/save formats have
not been certified. Do not run two instances against the same writable card.
Moving the EXE does not move your data. Another Windows user/computer has its own
profile. If the saved ISO path becomes unavailable, the chooser opens again;
changing that path does not erase saves.

## What's new in v0.2.0-alpha

### Regional GPU presentation

The accepted regional path reuses a GPU render target when its **visible region**
is valid, even if unrelated pages elsewhere are stale. It avoids unnecessary
image reconstruction and readback in validated PAL cases. Manual comparisons
found clearer, smoother output at **2×, 3× and 4×**.

The original fallback remains when direct presentation is unsafe, including the
special 640×512 startup selector. Frame Pacing is optional and independent of
regional presentation. This does not establish perfect equivalence of every
effect or a fixed FPS gain across all scenes and hardware.

### Performance improvements

- invalid cached textures leave their GS page indexes and are
  reinserted when validated, avoiding repeated work on invalid entries.
- eligible PSMCT16 transfers process groups of sixteen pixels with
  SIMD. Unsupported formats, alignment and boundary cases retain the scalar path.
  Memory-equivalence tests and focused synthetic benefits are established;
  sustained whole-game PAL gains are **not quantified**.
- **GPU Thread** is selectable from F9. Its effective startup state and saved
  preference are distinguished; a change requires restarting the game.

### Graphics and PC settings

**OFF, FXAA and SMAA change during play without restarting.** Immutable AA
pipelines are prepared before guest execution, compatible resources are reused,
and overlay handoff avoids holding its mutex during driver work.

| Setting | Availability | Application |
| --- | --- | --- |
| Internal resolution | Original, 2×, 3×, 4×; existing screen-size choice retained | During play |
| Aspect ratio | 4:3, 16:9; existing fill-window choice retained | During play |
| Antialiasing | OFF / FXAA / SMAA | During play |
| Fullscreen / windowed | Both supported | During play |
| Frame Pacing | ON / OFF | During play |
| VSync | ON / OFF | During play |
| GPU Thread | ON / OFF | **Restart required** |
| FPS Counter | F9 option and F1 shortcut | During play |

A pending GPU Thread preference is shown alongside its effective state. F9 does
not imply that a restart-only change has applied. Existing texture-pack controls
remain available when a pack is loaded; no pack or new PAL pack certification is
included. SSAA is not an integrated PAL menu option.

**Settings precedence:** built-in defaults → saved `pc_settings.ini` → explicit
runtime overrides. `--setting key=value` takes priority over saved preferences and
the legacy GPU Thread environment selection. F9 labels locked overrides (`[CLI]`
or `[env]`), cannot change them during that run and does not copy them into saved
preferences indirectly. Unlocked F9 changes are saved normally.

### Controls and window/audio fixes

- **L3+R3 toggles only PC Settings.** It no longer starts an expensive GS capture
  simultaneously. Holding the combination does not repeatedly toggle the menu;
  release it before pressing again.
- **F12 remains the independent diagnostic capture shortcut.** A full capture
  can deliberately pause execution and write substantial local diagnostic files.
- Bounded audio-latency recovery was created **GS capture and
  window movement**. This does not claim to solve every possible interruption.
- Fullscreen is the default for **new** profiles. Windowed mode, existing saved
  preferences and command-line precedence are preserved.

### PAL compatibility retained

Original PAL language and 50/60 Hz screens remain, including all button
instructions. Spanish, virtual profiles, profile recovery
and remembering the ISO have prior acceptance evidence. Advanced language
preferences, skipping selectors and forcing a preferred PAL mode will be
added on next releases.

## Controls

| Control | Action |
| --- | --- |
| F9 | Open/close PC Settings |
| L3 + R3 | Open/close PC Settings with a controller |
| F1 | Toggle the FPS Counter |
| F12 | Request a full GS diagnostic capture |
| Arrow keys / Enter / Esc, in F9 | Navigate, change/confirm, close |

The game retains its PS2 button prompts. Keyboard/controller support is retained;
Xbox One USB has prior manual coverage and DualSense was used in F9-B testing.
These observations are not exhaustive controller certification.

## Known limitations

- A **minor, non-blocking skybox seam at 3×** remains.
- Heavy scenes at 4× can be GPU limited and have occasional performance drops.
- **VU Fast** is not exposed as a validated PAL option. VU Exact remains the
  reference. **SSAA** is not a functional F9 selection in this release.
- 120 FPS interpolation, custom music, additional USA control options and
  multiplayer/LAN remain outside this release's validated PAL feature set.
- AA processes the presented game image, including the HUD; there is no new
  HUD-specific AA switch or complete all-effects equivalence claim.
- Audio recovery covers tested interruptions, not every device/driver or pause.
  Hardware, video and gameplay modes are not exhaustively certified.
  No guaranteed frame rate is advertised.

## Building from source

This is separate from using an approved Windows ZIP. Development needs the pinned
engine, Python, MSVC/Windows SDK, CMake, Ninja and your own PAL inputs. The documented
workflow uses Python 3.12 and preserved local EE/VU generation artifacts and
dependency caches. **A fresh clone is not a turnkey source build.** Game-derived
code and these local artifacts are not in the repository.

See the [reconstruction guide](games/burnout3-pal/port/diagnostics/README.md),
[recovery documentation](games/burnout3-pal/port/RECOVERY.md),
[roadmap](docs/pal/ROADMAP.md) and [release notes](RELEASE_NOTES_v0.2.0-alpha.md).
The submodule uses the JMPliskin fork; the original engine remains upstream.
Release preparation reuses verified immutable EE/VU objects and rebuilds affected
player/runtime units without regenerating the game.

## Credits and legal

- Original Burnout 3 PC port: **Daniel Gomes Vieira**.
- Shared engine: **ps2-port-engine and its contributors**.
- PAL Enhanced adaptation and maintenance: **JMPliskin**.
- Programming, reverse-engineering, testing and debugging assistance:
  **OpenAI ChatGPT and Codex**, directed and reviewed by the maintainer.
- Third-party libraries and SMAA retain their licenses and notices in the package.

Original port/runtime work is covered by [LICENSE](LICENSE). The player links
locally reconstructed game code; requiring an ISO at runtime does not itself
establish permission to redistribute that program.
No disc images, extracted assets, translated
source files or personal saves are included in the repository/package.

Unofficial project, not affiliated with Electronic Arts or Criterion Games.
Burnout is a trademark of Electronic Arts. Retain LICENSE and third-party notices.
