# Burnout 3 PAL Enhanced

European PAL adaptation of the unofficial [Burnout 3 PC port](https://github.com/danielgomesvieira2000/burnout3-pc) by Daniel Gomes Vieira, using [ps2-port-engine](https://github.com/danielgomesvieira2000/ps2-port-engine).

This repository contains development tools, host adaptations, tests and documentation. It does **not** include the game, translated game sources, captured microcode, executable builds or saves. You must supply your own legally obtained European PlayStation 2 copy, executable `SLES_525.84`.

## Status

The stable PAL checkpoint has user-confirmed Spanish selection, original 50/60 Hz screens, persistent profiles, controller input, audio, skippable FMV and completed races. Its 97 locally captured VU programs were compiled; this does not establish equivalence of every game function or compatibility with every mode.

The experimental integration preserves 40 additional USA commits and uses the coherent engine revision `d7e6e023f261d9f95ad097fcd063ec95e40ca465`. Host/Vulkan tests and a limited PAL VU differential passed. **A black frame at frame 2400 remains unexplained; the new engine is not approved as a replacement for the playable stable version.**

Shared renderer, VU, audio and IOP improvements are inherited through the engine. USA-specific PC SETTINGS hooks, configurable controls/prompts, HUD changes, 60 FPS patches, mods and LAN still require regional analysis. USA addresses and patches must not be applied directly to PAL.

## Building from your own copy

Requirements: Windows x64, Python 3.12, MSVC C++ build tools/Windows SDK, CMake, Ninja, and the engine submodule at the revision recorded by the selected branch. GPU operation additionally requires a compatible Vulkan driver. See [engine porting documentation](engine/docs/PORTING.md).

Supply the PAL disc image and extracted executable locally, then generate the analysis database, EE translations and VU capture with the existing tools. Paths are described by [game.json](games/burnout3-pal/game.json); these private inputs and outputs must remain ignored. No download of game data is provided.

The [bulk workflow](games/burnout3-pal/port/BULK.md) describes EE staging/publication. The [recovery guide](games/burnout3-pal/port/RECOVERY.md) describes the preserved local artifacts and incremental reconstruction. **The current integration adapter requires those artifacts and cached dependency sources; a fresh clone alone is not a turnkey PAL build.** VU capture requires running your own game; generated programs and microcode are never distributed here.

With a separate preserved PAL reference checkout available:

```powershell
py -3.12 -B games/burnout3-pal/tools/sync_native.py --prepare PATH_TO_REFERENCE_CHECKOUT --build-root PATH_TO_PRIVATE_BUILDS
py -3.12 -B games/burnout3-pal/tools/sync_native.py --execute PATH_TO_CREATED_WORK_JSON
```

Omitting `--build-root` retains the historical repository `build/` location. Preparation generates VU sources; execution configures and compiles a new Release tree without reusing incompatible historical objects. These commands are not a download or installation of the game.

## Development and upstream

Follow [DEVELOPMENT_POLICY.md](games/burnout3-pal/DEVELOPMENT_POLICY.md). Integrate USA history in a separate worktree, use its coherent engine gitlink, retain PAL adaptations, and validate regional startup before promotion. See [UPSTREAM_INTEGRATION.md](docs/pal/UPSTREAM_INTEGRATION.md), [status](docs/pal/STATUS.md), [adaptations](games/burnout3-pal/port/ADAPTATIONS.md) and the [publication checklist](docs/pal/PUBLICATION.md).

Historical launchers refer to local validated builds; their presence does not mean executables are included. Saves belong in a private memory-card directory. Preserve existing cards when testing experimental builds.

## Credits and license

Original port: [Daniel Gomes Vieira](https://github.com/danielgomesvieira2000). PAL adaptation: [JMPliskin](https://github.com/JMPliskin). Upstream copyright notices, contributor acknowledgements and third-party licenses are retained; see [LICENSE](LICENSE).

Unofficial fan project, not affiliated with Electronic Arts or Criterion Games. Burnout is a trademark of Electronic Arts Inc. No original game assets, disc images, executables, generated game code or captured microcode may be added to this repository.

## Windows player alpha (not released)

The local `Burnout3-PAL-Enhanced-0.1.0-alpha-windows-x64.zip` passed user testing.
No public player download is available yet. GitHub Source Code is the developer
repository, not an executable distribution and contains no game resources.

The player EXE reuses the upstream host entry point, ISO9660/SYSTEM.CNF validation
and settings format. First launch asks for your PAL ISO (SLES_525.84); later
launches reuse its path. Missing/incompatible discs prompt again; cancellation
exits. Unicode paths require Windows 10 1903+ (UTF-8 process manifest).

Requirements: Windows x64, AVX2 CPU and Vulkan driver. No Python/BAT is needed
for playing. Data and settings live in `%LOCALAPPDATA%/Burnout3-PAL-Enhanced/`:
`pc_settings.ini`, `memcard/` and exclusive `logs/run-*/` directories. Replacing
an EXE does not replace this data. Back up cards before any manual import.

The PAL audit directory is created automatically, without a manually configured
environment variable. Language/50-60 Hz selection remains part of the game.
GPU Thread defaults On; compiled diagnostic instrumentation remains. Two races
on the diagnostic predecessor passed with logging enabled/absent, but its
predecessor's freeze is unexplained. User testing confirmed the Windows ISO
selector, persisted disc path, reprompt after renaming the ISO, direct EXE startup
and a complete, fluid race with GPU Thread On. This does not explain or resolve
the historical freeze.

For source reconstruction, retain your own EE/VU source artifacts and dependency
cache; do not rerun bulk generation unnecessarily. The diagnostic engine source
patch and provenance are in `games/burnout3-pal/port/diagnostics/`. With those
sources applied to the pinned engine, configure the PAL CMake target with
`PS2_PAL_SOURCES_FILE` (preserved EE list) and `PS2_GENERATED_DIR` (preserved PAL
 dispatch/VU emission), then build `burnout3-pal` with at most two jobs. Its output
is named `Burnout3-PAL-Enhanced.exe`. See the recovery guide for private inputs.

`tools/test_player_startup.py PATH_TO_NATIVE_STARTUP_TEST_EXE` exercises synthetic
ISO selection/persistence contracts. `tools/package_player.py --exe EXE
--dependency-cache CMAKE_CACHE_TREE --output-root PRIVATE_OUTPUT` reuses USA
license/import audits for a local package. Never add the EXE/ZIP or game-derived
sources to Git; publication requires a separate distribution review.

Historical personal play/capture BATs are retained locally but removed from the
public Git index. `AutoTest-PAL.bat` and Python build/translation/capture/test
infrastructure remain developer tools; advanced host CLI remains available.

Before any public binary Release, validate a clean installation and the exact
memory-card location with no pre-existing profiles. The development player
automatically loaded an older profile; a private test-card copy had been prepared,
but the loaded profile's provenance has not been independently established. Do
not overwrite existing saves to test this. Further work includes explaining GPU
Thread sensitivity to compilation changes, adapting USA graphics improvements,
preparing comparable benchmarks and reviewing distribution conditions.
