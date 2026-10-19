# Burnout 3 PAL Enhanced: development continuity

Updated 19 October 2026. This page describes development source and retained
evidence; it is not a statement that every PAL function or mode is verified.
The older detailed regression chronology remains in Git history and
[ADAPTATIONS.md](games/burnout3-pal/port/ADAPTATIONS.md).

## Release, source and experiments

| Reference | What is accepted | Limits |
| --- | --- | --- |
| Public v0.1.0-alpha | Native Windows startup, PAL ISO selection/persistence, clean install, new profile save/reload, full races with GPU Thread On | Instrumentation remains compiled; historical freeze unexplained |
| Widescreen development source | Clean pinned engine, Hor+ camera, 4:3 preservation, 16:9/Fill and HUD visually accepted by the maintainer | GPU Thread/HD Off acceptance; not every replay, language or special mode |
| Separate private AA/HD experiments | F9 Off/FXAA/SMAA switching/persistence tested; HD substitutions observed | Their patches are not integrated; HD333 quality/performance was unsatisfactory |

The current source changes do not replace the published alpha ZIP or tag.
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
adaptation. No Vulkan viewport, shader, AA, HD or GPU Thread change is bundled.

A fresh isolated Release build with clean engine d7e6e023 succeeded without old
objects or regenerated EE/VU sources. Fourteen camera and 27 HUD cases passed
and passed again before committing. The maintainer explicitly accepted the
clean build visually after the earlier instrumented experiment. These cases use
synthetic memory/packets and spies, not complete game MIPS equivalence.

- [Camera contract and validation](docs/pal/WIDESCREEN_3D.md)
- [HUD mappings, transforms and optional CMake tests](docs/pal/WIDESCREEN_HUD.md)
- [USA/PAL address methodology and unresolved identities](docs/pal/USA_PAL_ADDRESS_MAPPING.md)

## Rebuilding and preserving evidence

Keep the pinned engine at d7e6e023f261d9f95ad097fcd063ec95e40ca465.
The public alpha diagnostic patch is retained for that historical build;
the accepted clean widescreen build uses no local engine patch. Do not
silently mix these two recipes or their binary libraries.

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

- [Graphics, AA, readbacks and GPU Thread](docs/pal/GRAPHICS_PERFORMANCE.md):
  read-category transfers and presentation fallback need measured attribution;
  drains and fence-reason counts are not individual copies or proven stalls.
- [HD333 trial](docs/pal/HD_TEXTURE_PACK.md) and
  [identifier/image comparison](docs/pal/TEXTURE_COMPARISON.md): direct lookup
  keys, identical originals and visually correct native replacements differ.
- Evaluate the existing bounded presentation trace before changing coherency
  guards. Use timeline only for measured duration/overlap and a controlled race
  window before claiming a GPU Thread performance gain.
- Review AA integration separately; SSAA, extended PC SETTINGS, configurable
  controls/prompts, mods and LAN still require their own regional validation.
- Do not expand HD packs, implement a hash converter or update the engine as a
  side effect of publishing the accepted widescreen source.

See [STATUS.md](docs/pal/STATUS.md) for the concise feature matrix and
[upstream integration procedure](docs/pal/UPSTREAM_INTEGRATION.md) before any
future authorized synchronization. The initial bootstrap/frame-2400 findings
are historical evidence, not the current playable-alpha status.
