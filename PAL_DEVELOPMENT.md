# Burnout 3 PAL Enhanced: development continuity

Updated 22 October 2026 for v0.2.0-alpha source publication. This page describes development source and retained
evidence; it is not a statement that every PAL function or mode is verified.
The older detailed regression chronology remains in Git history and
[ADAPTATIONS.md](games/burnout3-pal/port/ADAPTATIONS.md).

## Release, source and experiments

| Reference | What is accepted | Limits |
| --- | --- | --- |
| Historical public v0.1.0-alpha | Native Windows startup, PAL ISO selection/persistence, clean install, new profile save/reload, full races with GPU Thread On | Instrumentation remains in that binary; its historical freeze was unexplained |
| v0.2.0-alpha accepted source | PAL-010 regional presentation, USA-001/002, F9-A/B, live OFF/FXAA/SMAA, separate L3+R3/F12 controls, bounded audio recovery and fullscreen default | Package startup/PAL selector and F9/AA accepted; no exhaustive hardware/mode certification or quantified whole-game speedup |
| Integrated widescreen source | Hor+ camera, 4:3 preservation, 16:9/Fill and HUD visually accepted by the maintainer | Original GPU Thread/HD Off acceptance; not every replay, language or special mode |
| Separate private HD experiments | HD substitutions observed | Not integrated as a new validated PAL feature; HD333 quality/performance was unsatisfactory |

Publishing v0.2.0-alpha source and its tag does not replace the historical
v0.1.0-alpha ZIP or tag. No GitHub Release or binary upload is performed by this
Git publication task; distribution rights for reconstructed game code remain
unresolved. See [README](README.md) and [release notes](RELEASE_NOTES_v0.2.0-alpha.md)
for current features, limitations and the separate binary-publication gate.
The fork is JMPliskin/burnout3-pal-enhanced; Daniel Gomes Vieira's USA project
is upstream. Do not synchronize the preserved old stable checkout implicitly.

## Regional work already established

- The supported European executable is SLES_525.84; USA uses SLUS_210.50.
  Users supply their own disc. Spanish and original PAL 50/60 Hz selection work.
- PAL IOP modules and regional startup are retained. Memory-card profiles,
  audio, menus, FMV and Start movie skipping have gameplay evidence, not full
  equivalence certification of every underlying service.
- The PAL relocation handler remains required and native startup initializes
  its private audit directory automatically; no manual environment setup is
  needed for ordinary player startup.
- The libmc2 worker's computed dispatch and command table required explicit
  regional review. Successful profile save/reload is functional evidence,
  not proof of all original worker states or opcodes.
- There are 9,384 analyzed entries and 9,383 translated EE functions in the
  preserved build inputs. Historical lifter classifications (6,460 reported
  verified; 2,923 NOT VERIFIED) are not upgraded by compilation or gameplay.
- The preserved manifest contains 97 VU programs. Recorded sessions exceed
  99.99% compiled VU1 coverage; some recorded sessions reached 100%. Coverage
  is session-specific, does not imply VU Fast, and is not semantic equivalence.

## Widescreen implementation and tests

PAL RwCameraSetViewWindow 001E2100 calls its preserved translation directly,
widens only the horizontal original 4:3 window and restores the caller's input.
The vertical field remains unchanged. PAL Flush2D 0021A4E0, sprite draw 00364B00,
16 builders and regional context/object anchors implement the independent 2D
adaptation. Those original widescreen changes did not modify Vulkan viewport,
shaders, AA, HD or GPU Thread; later accepted v0.2.0-alpha work is listed above.

Historical 19 October evidence: a fresh isolated Release build with clean engine d7e6e023 succeeded without old
objects or regenerated EE/VU sources. Fourteen camera and 27 HUD cases passed
and passed again before committing. The maintainer explicitly accepted the
clean build visually after the earlier instrumented experiment. These cases use
synthetic memory/packets and spies, not complete game MIPS equivalence.

- [Camera contract and validation](docs/pal/WIDESCREEN_3D.md)
- [HUD mappings, transforms and optional CMake tests](docs/pal/WIDESCREEN_HUD.md)
- [USA/PAL address methodology and unresolved identities](docs/pal/USA_PAL_ADDRESS_MAPPING.md)

## Rebuilding and preserving evidence

The v0.2.0-alpha gitlink is bfad7255f17f7789a8bbb0a59a6e3a003535c548,
descending from PAL-010, USA-001/002 and the accepted PC settings/audio commits.
Engine d7e6e023f261d9f95ad097fcd063ec95e40ca465 belongs to the historical clean
widescreen build, and the old alpha diagnostic patch is retained for its own
historical build. Do not silently mix these recipes or their binary libraries.
The v0.2.0-alpha package rebuild reused hash-verified immutable EE/VU/dependency
objects and rebuilt affected player/runtime units without regenerating EE/VU.

A clone does not contain the proprietary original inputs, generated EE/VU,
private manifests or dependency source cache needed by the current workflow.
Use [RECOVERY.md](games/burnout3-pal/port/RECOVERY.md) and the
[reconstruction notes](games/burnout3-pal/port/diagnostics/README.md) with their
recorded scope. sync_native --prepare emits VU code; it is not a harmless
configuration-only command. Existing generated sources can be supplied directly
to CMake for the tested clean-engine ABI without rerunning translation.

Use separate build/data directories and private Windows Jobs for automated
workers; preserve hashes, inputs, arguments, timeout and cleanup results.
Ninja may use at most three compile jobs, reduced to two on memory pressure.
Synthetic test directories must be new. Never publish original game files,
generated game code, saves, logs, captures, compiled experiments or private paths.
No automatic code repair, guessed regional deltas or silent failure promotion.

## Research and next decisions

- [Graphics, AA, readbacks and GPU Thread](docs/pal/GRAPHICS_PERFORMANCE.md)
  retains historical investigation. PAL-010 subsequently identified the stale
  target fallback cause and adopted safe visible-region validation. That work is
  closed; do not restart it without a new reproducible regression.
- [HD333 trial](docs/pal/HD_TEXTURE_PACK.md) and
  [identifier/image comparison](docs/pal/TEXTURE_COMPARISON.md): direct lookup
  keys, identical originals and visually correct native replacements differ.
- Preserve PAL-010 coherency guards and accepted F9/AA behavior. Prior focused
  tests and manual acceptance remain reusable for unchanged dependencies;
  GPU Thread or whole-game performance percentages are not established.
- Live FXAA/SMAA and the accepted F9 options are integrated. VU Fast, SSAA,
  advanced PAL startup preferences, additional controls/prompts, mods and LAN
  still require their own regional validation. Bounded audio recovery covers
  the tested capture/window interruptions, not every possible pause or device.
- Do not expand HD packs, implement a hash converter or update the engine as a
  side effect of publishing the accepted v0.2.0-alpha source.

See [STATUS.md](docs/pal/STATUS.md) for the concise feature matrix and
[upstream integration procedure](docs/pal/UPSTREAM_INTEGRATION.md) before any
future authorized synchronization. The initial bootstrap/frame-2400 findings
are historical evidence, not the current playable-alpha status.
