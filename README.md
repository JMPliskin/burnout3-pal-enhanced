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
