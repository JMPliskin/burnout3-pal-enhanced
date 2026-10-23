# Validation, automation and build guide

Updated 23 October 2026. Commands are references for **future authorized tasks**;
none of the builds/gameplay commands below was run during documentation handoff.
Use [AGENTS.md](../../AGENTS.md) and its incremental policy. Select tests from the
actual changed contracts, not from the length of this inventory.

## Baseline and prerequisites

The published source is PAL `2aac9350...` with engine `bfad7255...`. The root
checkout remains `13f4295`; newer CMake targets/tests may exist only in the
published checkout. Do not copy code into the old tree just to run a test.

Development uses Windows x64, Python 3.12 for PAL queue/pipeline tools, MSVC x64,
Windows SDK, CMake and Ninja, plus compatible cached SDL3/Vulkan/other dependencies.
`pal_auto.py` explicitly requires Python 3.12. The release's documented local
incremental build used the available VS Python 3.9 helper for that particular
recipe; this does not waive the queue's 3.12 requirement. Do not install tools
or change environments as a side effect of a documentation task.

The release was MSVC `/O2 /MT`, AVX2, with Windows SDK resources and no external
PDB. Vulkan 1.3 driver support is required. A recursive clone gets the engine,
**not** proprietary game inputs, generated EE/VU, private manifests or the cached
objects required for the current reconstruction workflow.

## Read-only entry checks

```powershell
git branch --show-current
git rev-parse HEAD
git status --short
git ls-tree HEAD engine
git config --file .gitmodules --get submodule.engine.url
```

These do not authorize checkout, fetch, submodule update or reset. Inspect the
handoff before using same-named local/remote branches or the development repo's
unrelated USA `v0.2.0-alpha` tag.

## Function pipeline and targeted Python tests

From a compatible checkout, with legitimate local inputs where the tool needs them:

```powershell
py -3.12 -B games/burnout3-pal/tools/generate_batches.py --list
py -3.12 -B games/burnout3-pal/tools/run_pal_pipeline.py --list
py -3.12 -B games/burnout3-pal/tools/run_pal_pipeline.py --batches batch-001 --validate-only
py -3.12 -B games/burnout3-pal/tools/pal_auto.py --plan --manifest games/burnout3-pal/tools/batches/batch-001.json
```

Listing and validate-only do not prove execution/equivalence. Planning writes a new
ignored plan, not a completed run. Execution is opt-in: pipeline `--execute`, queue
`--run <plan.json>` or `--resume <plan.json>`. Only an audited infrastructure retry
uses `--resume <plan.json> --retry-infrastructure`; no automatic semantic retries.
Manifests are immutable inputs and output directories are exclusive.

Select the affected module, for example:

```powershell
py -3.12 -B -m unittest discover -s games/burnout3-pal/tools -p test_run_pal_pipeline.py
```

Other existing modules include test_generate_batches, test_pal_auto,
test_windows_job, test_relocate_native, test_autotest, test_switch_completion,
test_memcard, test_skip_movie, test_vu_integration, test_sync_native and
test_player_startup. Inspect each fixture's scope before running it: do not assume
every test is pure Python or needs the game. See [batch guide](../../games/burnout3-pal/tools/batches/README.md).

## Existing C++ tests in the accepted source

| Target / source | Scope |
| --- | --- |
| `ps2host_texture_page_index_tests` | USA-001 membership/invalidation/reinsertion; no game |
| `ps2host_gs16_tests` | USA-002 scalar/SIMD memory differential and boundaries |
| `ps2host_pc_settings_tests` | Real settings/menu with synthetic window/input endpoints |
| `ps2host_audio_recovery_tests` | Bounded ring recovery, wraps/thresholds and normal-output identity |
| `pal_player_startup_tests` | Player path/profile/default startup behavior |
| `pal_widescreen_camera_tests`, `pal_widescreen_hud_tests` | Existing translated inputs, real originals and scoped synthetic callbacks; optional `PAL_WIDESCREEN_TESTS=ON` |
| PAL `port/tests/f9_*` and `pal_regional_presentation_tests.cpp` | Native window/AA/menu, effective GPU setting and regional predicate fixtures; consult retained compile manifests |

Build only an affected target in an **already validated compatible** tree:

```powershell
# Set $validatedBuild to the explicitly reviewed build directory first.
cmake --build $validatedBuild --target ps2host_texture_page_index_tests --parallel 2
```

Do not configure a fresh whole tree just to run a small test. Generic CMake/host
targets may generate VU corpus artifacts; `sync_native.py --prepare` emits VU
and is not harmless configuration. No EE/VU regeneration without explicit need
and authorization. Preserve source lists, relocation completion and compatibility
registrations. Old v0.9.0 `relocate_native --reuse-build` recipes are not generic
ABI-compatible rebuilds for the accepted newer engine.

## Manifest-based runtime checks and incremental builds

AutoTest reuses the existing supervisor and validates manifests/hashes:

```powershell
py -3.12 -B games/burnout3-pal/tools/autotest.py --mode QUICK --timeout 3 --build-manifest $reviewedManifest
```

Use this only for a change it can validate and a supported manifest. FULL may
compile changed units; do not run it reflexively. Rejected manifest/input hashes
must be diagnosed, not bypassed. A function counter, clean start or time-limit
exit is not whole-game equivalence. A frame-180 stop is expected only when the
specific launcher requested it and recorded its marker, clean Job and no timeout;
arbitrary exit code 2 is not automatically success.

Release dossier `build_release.py`, `build_identity.json`, response files and
`build/reused_inputs.json` retain the actual incremental recipe: rebuild affected
host/GPU/player units, verify immutable EE/VU/dependency objects, link version
resources, then package. Treat the script as a recorded recipe, **not** something
to rerun into its existing output. A future build needs a new directory and a
reviewed manifest/source/ABI/cache mapping. No turnkey fresh-source-build claim.

## Existing renderer/diagnostic tools

Reuse engine `tools/framediff.py`, `gsmem.py`, `timeline.py`, GS Replay, profiling
flags and finite GS captures. Use their `--help` for the pinned revision rather
than inventing arguments. Existing local replay wrappers record capture hashes,
starting state and image/memory comparisons; prefer those when inputs apply.
Do not replay after every menu/document edit. Replay does not validate live
PresentGpuPicture or interactive F9 transitions. F12 capture can intentionally
block host execution; L3+R3 must not trigger it.

Launchers in the latest accepted controls dossier include
`F9-B Controls Fullscreen 2x ON.bat` and
`F9-B Audio Diagnostic Window 2x ON.bat`. The release EXE works without a BAT.
Earlier `Jugar Burnout 3 PAL*.bat`/AutoTest launchers select historical builds or
manifests; inspect them before using them as a new baseline. Use copied cards and
isolated LocalAppData for tests, never personal saves or simultaneous writers.

## Packaging, diagnostics and contributions

The accepted packaging tool takes `--exe`, `--dependency-cache`, `--output-root`
and optional `--forbid-path`. It checks PE x64/imports/private paths/licenses,
then includes versioned notes and checksums. Example with explicitly reviewed paths:

```powershell
py -3.12 -B games/burnout3-pal/tools/package_player.py --exe $approvedExe --dependency-cache $validatedCache --output-root $freshOutput
```

It does not upload. No ISO/BIOS/extracted assets/cards/INI/logs/caches/generated
sources/PDB belong in the ZIP or Git. The EXE still contains reconstructed game
program code: an ISO requirement and MIT engine license do not settle those rights.

A release-specific smoke test covers clean extraction/startup/ISO selection,
representative F9/AA/settings and compatible copied profiles. The existing v0.2
package already has that acceptance; do not repeat it for this documentation.

Logs are under the player's LocalAppData data directory; retain relevant
player/host logs, exact revision/EXE hash, CLI, settings, Job outcome and existing
crash dumps before proposing a fix. No new universal crash uploader is claimed.
Use the published GitHub bug template/Issues, search duplicates, and include OS,
CPU/GPU/driver, version, repro steps and expected/actual outcome. For feature
requests identify a roadmap ID, proposed behavior and dependencies. Redact ISO
paths/personal information; never attach game files, cards or giant captures.

Contributors should propose small isolated changes, establish PAL contracts,
follow English commit-title/body conventions, cite tests/evidence and retain
credits/licenses. The USA project remains upstream; do not bundle an upstream
update with an unrelated PAL feature. A proposal/report does not authorize a push.
