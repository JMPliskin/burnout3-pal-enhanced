# Burnout 3 PAL Enhanced — Development Roadmap

Document schema **0.1.19**, 23 October 2026; game version **0.2.0-alpha**.
This is the existing PAL roadmap consolidated in English, not a second roadmap.
This root-repository copy consolidates the existing planning worktree's 0.1.17
roadmap. Updating that worktree was denied by Windows file permissions; its old
copy is preserved and a synchronization patch is prepared in the ignored handoff
build. This is one evolving plan, not independent roadmaps. Preserve existing IDs;
new entries do not authorize implementation, builds or Git actions.

Read [development continuity](../../PAL_DEVELOPMENT.md),
[handoff](../SESSION_HANDOFF.md), [architecture/evidence](ARCHITECTURE_AND_EVIDENCE.md)
and [changelog](PLANNING_CHANGELOG.md). Historical details remain in the
[0.1.17 roadmap](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/docs/pal/ROADMAP.md),
[upstream tracker](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/docs/pal/UPSTREAM_TRACKER.md)
and [USA/PAL matrix](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/docs/pal/UPSTREAM_PAL_MATRIX.md).
Their old next-action statements are superseded here; original evidence is retained.

Current consolidation base: README-only PAL `a2e524e` on release source `2aac9350`;
engine `bfad7255`. Task IDs/statuses are unchanged. Historical workspace paths
and publication statements are scoped by the handoff consolidation update.

## Principles and status vocabulary

Accuracy and PAL behavior precede performance. Use SLES-52584 **1.01**, its original
ELF, shared engine mechanisms and evidence-based regional mappings. PAL correctness
does not require pixel-identical USA output: language, projection, timing and assets
can differ legitimately. Do not apply global address offsets or loosen GS coherency.
Keep game logic, presentation, audio and optional PC features separate.

| State | Meaning |
| --- | --- |
| DONE | Implemented and verified/accepted within the stated PAL scope; not exhaustive certification |
| TESTING | Implementation or tooling exists, but specified acceptance remains incomplete |
| IN_PROGRESS | Actively authorized implementation; **none at this session close** |
| READY | Bounded proposal prepared; execution still needs authorization |
| RESEARCH | Open technical question with partial findings or a required investigation; not permission to run a broad audit |
| BACKLOG | Planned work, including long-term ideas; no implementation claimed |
| BLOCKED | Named external dependency, missing evidence or reverse engineering prevents adoption/publication |
| REJECTED | Approach not adopted; preserve the reason/evidence |

Claim labels **SOURCE / AUTO / MANUAL / OBSERVED / UNCONFIRMED / PLAN** are defined
in PAL_DEVELOPMENT.md. AUTO means existing test records, not tests rerun today.
Keep previously validated/unaffected, current validation, pending manual and
not-covered evidence separate. Only reopen closed work for the triggers in AGENTS.md.

## Current baseline and completed milestones

| Reference | Identity and scope |
| --- | --- |
| Published PAL main and tag | `2aac9350af0a139771d8a5677d0863b216343efc`; tag `v0.2.0-alpha` |
| Published engine / gitlink | `bfad7255f17f7789a8bbb0a59a6e3a003535c548`, JMPliskin fork |
| Main local checkout | `13f4295`, intentionally not moved through a dirty tree; documentation is newer than its code |
| Prior public release | v0.1.0-alpha and its assets/tag unchanged |
| Historical widescreen integration | `integration/pal-widescreen-20261019`, `434a86c9...`; engine `d7e6e023...` |
| Historical USA reference | game `444b541b9919055af3ee97787e84de7c2ceed98f` (v0.6.0-alpha), engine `721c73cd00e21aa87349ad5ae092277dcd3b69fa`; not adopted wholesale |

Source publication and remote recursive cloning were verified on **22 October**.
No GitHub Release/assets were created by Codex. Later maintainer publication has
not been checked on 23 October. Binary distribution rights remain unresolved in
the available evidence; source/tag publication is already complete.

- **SOURCE + MANUAL:** Windows startup, own PAL ISO selection/path persistence,
  Spanish, original language and 50/60 Hz selection, profile save/reload, FMV,
  Start skip, menus, controls, races and audio. Not every language/mode is certified.
- **SOURCE + AUTO + MANUAL:** Hor+ camera, original 4:3, 16:9/fill and HUD;
  14 camera and 27 HUD cases. Ultrawide is a separate future expansion.
- **PAL-010 DONE:** global stale rejection caused unnecessary presentation fallback;
  C validates visible pages, target identity, geometry/PSM, physical scale,
  initialization and pending producers. Only certified PAL layout is enabled.
  The 640x512 selector keeps its original fallback. Pacing remains independent.
  Manual scope: 2x ON/OFF, 3x/4x ON; accepted EXE `612719b3...`.
- **Engine reconciliation DONE:** `28057a1a` is an ancestor of `d7e6e023`:
  35 reachable commits including one merge, 20 first-parent commits. This is
  compatible ancestry, not a linear chain of 35. PAL-010 already used that base.
- **USA-001/002 DONE:** commits `2fcc03d...` / `e966221...`; invalid texture removal/
  reinsertion and eligible PSMCT16 SIMD accepted. Structural/synthetic improvements
  are proven; sustained whole-game PAL gains and energy savings are not quantified.
- **F9-A/B DONE:** requested/effective GPU Thread with restart notice; FPS F1/F9;
  persistent settings and runtime locks; live OFF/FXAA/SMAA; pipeline warm-up and
  safe overlay handoff; keyboard F9 and edge-triggered L3+R3; capture only on F12.
- **PAL-011-AUDIO DONE within tested scope:** consumer-side bounded recovery after
  capture/window interruptions, without changing simulation or WASAPI sizing.
  New PAL profiles start fullscreen; saved window preference and CLI prevail.
- **Release package AUTO + MANUAL:** EXE `a7122e87...`, ZIP `bf8cdc0c...`;
  clean extraction/startup/PAL selector/F9/AA accepted, SMAA persisted and short
  restart preserved INI. Full identities and private evidence map are in the handoff.

## Independent known limitations

- 3x skybox seam: **MINOR / NON-BLOCKING**, no investigation requested now.
- Pacing OFF: isolated ~375.6 ms interval, no established cause or sustained graphics regression.
- 4x: demanding scenes can be GPU limited; ~94% busy observed, not a regional regression.
- Historical drag/capture stalls produced ~650–680 ms audio queues. Recovery is
  accepted for tested interruptions, not all devices, callbacks or pause scenarios.
- PAL50, all effects/languages/game modes and hardware combinations are not exhaustively validated.
- VU Fast remains unexposed; compiled VU coverage is not flag/float equivalence.

## F0 — Infrastructure, recompilation and evidence

| ID | Priority | State | Work / dependencies | Acceptance and evidence |
| --- | --- | --- | --- | --- |
| PAL-001 | P0 | TESTING | Persistent roadmap, tracker, matrix and handoff | Documentation/link checks; maintainer review pending, not a functional blocker |
| PAL-001B | P0 | TESTING | Preserve external engine research and provenance | Reviews/revisions traceable; no functional import; retained historical catalog |
| PAL-002 | P0 | TESTING | Existing finite GS replay library/automation; remaining software/GPU comparison coverage | Same starting GS state, isolated processes and quantitative differences; replay cannot validate live PresentGpuPicture |
| PAL-003 | P2 | RESEARCH | Reversible bounded multi-frame F12 capture | Finite memory/storage and replay integrity; not a full save state |
| PAL-004 | P1 | RESEARCH | Reuse targeted GS/game/HUD/audio/VU/performance results | Per-revision provenance; separate process, simulation, presentation and GPU metrics; no repeat-all default |
| PAL-005 | P1 | TESTING | PAL native-recompilation pipeline and recovery | Original ELF authority; JSON maps, generated batches, MIPS/expression/DLL checks, explicit queue/resume; full semantic equivalence incomplete |

PAL-005 already has 9,384 analyzed entries and 9,383 translated EE functions in
preserved inputs. The historical 37-batch report lists 6,460 lifter-classified
verified entries, 2,923 unverified and one rejected entry; do not promote these
classifications to universal MIPS/native equivalence. Preserve ELF, manifests,
generated sources/objects and the synthetic scratch-versus-game-heap distinction.

## F1 — GS, Vulkan, coherency and compatibility

| ID | Priority | State | Work / dependencies | Acceptance and evidence |
| --- | --- | --- | --- | --- |
| PAL-010 | P0 | DONE | Accepted regional C, published in the engine baseline | Safe visible region, scale and selector fallback; manual scope above; no new fallback investigation |
| PAL-010A | P0 | TESTING | Existing symbol proof study: 40 entries, 13 prior pairs revisited, no new identities | Names are not binary identity; causal-symbol utility still unvalidated, not a prerequisite for continuing development |
| PAL-011 | P0 | BACKLOG | Any additional ownership/Use/Containing optimization beyond accepted C | The original fallback cause is closed; reopen only for new evidence, never accept overlap as validity |
| PAL-012 | P0 | RESEARCH | Presentation timing, SetPresentAtBlankEnd, VBLANK/input latency | PAL50/60 and actual clocks measured separately; existing pacing is accepted and must remain intact |
| PAL-013 | P0 | DONE | Original 560 KiB fallback-attribution investigation | Attributed in the measured PAL60 trace; unrelated byte/producer questions require a new concrete case, not another general audit |
| PAL-014 | P1 | RESEARCH | CSM2/TEXCLUT and CBP/CBW/COU/COV/CPSM/CSA/TEXA | CSM1 GPU and decode already work; CSM2 CPU exists, frequency/cost unquantified; palette/region tests before any new GPU path |
| PAL-015 | P1 | RESEARCH | Remaining sky/cloud/bloom/fog/alpha/color-depth alias compatibility | Keep ORIGINAL behavior; compare valid PAL references, not mandatory USA pixel identity; PAL50 explicit |
| PAL-016 | P1 | RESEARCH | Selective additional transfer/render-target opportunities | Inventory inherited work first; USA-001/002 are already integrated, no wholesale upstream update |
| PAL-016A | P1 | DONE | USA-001 valid texture page indexing, `2fcc03d...` | 2,000 differential operations, alias case, identical replay image/memory and manual acceptance; no quantified sustained PAL gain |
| PAL-016B | P1 | DONE | USA-002 eligible PSMCT16 SIMD, `e966221...` | 1,504 differential cases per SIMD/scalar build, protected boundaries, replay identity and manual acceptance; other 16-bit formats stay scalar |
| PAL-017 | P3 | BACKLOG | Possible alternative renderer/backend | Only after a demonstrated structural limitation, measured gain and explicit approval; no migration started |

## F2 — EE/VU, threading, CPU and compilation

| ID | Priority | State | Work / dependencies | Acceptance and evidence |
| --- | --- | --- | --- | --- |
| PAL-020 | P1 | RESEARCH | Comparable scene CPU/GPU/GS-thread baselines | Existing three-version analysis is sufficient for experimental adoption; no causal whole-game speedup claim |
| PAL-021 | P1 | RESEARCH | VU Exact/Fast and selected later USA work | Exact stays default; validate flags/float results and cross-program consumers without regeneration |
| PAL-022 | P1 | RESEARCH | clang-cl/LTO/PGO versus MSVC | Compatible ABI, explicit PAL profiles and targeted equivalence; not part of current accepted build |
| PAL-023 | P2 | RESEARCH | GPU Thread queue/buffer/hint opportunities | Match scenes and inputs; separate first-use pipelines, CPU busy and GPU time; current thread mode is startup-latched |
| PAL-024 | P3 | BACKLOG | Broader CPU/iGPU compatibility, including non-AVX2 | Actual build/runtime ISA coverage; shipped local x64 package currently requires AVX2 |

## F3 — Display, widescreen, effects and controls

| ID | Priority | State | Work / dependencies | Acceptance and evidence |
| --- | --- | --- | --- | --- |
| PAL-030 | P2 | DONE | Live OFF/FXAA/SMAA integration | Consolidated with PAL-070B; not another pending AA integration task |
| PAL-030-SSAA | P2 | RESEARCH | Genuine SSAA, separate from internal scale | Resolve/downsample behavior, cost and PAL correctness; not an available F9 method |
| PAL-031 | P2 | DONE | Accepted Hor+ camera and HUD integration | 4:3 preserved; camera 0x001E2100 and HUD contracts; scoped automated/manual evidence |
| PAL-031-U | P3 | BACKLOG | Ultrawide/32:9 and additional camera/HUD scenes | Do not infer from tested fill-window framing; validate anchors, cutscenes, clipping and proportions |
| PAL-032 | P2 | BACKLOG | Optional bloom/blur/shadow/LOD/fog/particle controls | Preserve ORIGINAL, one effect and dependency set per task |
| PAL-034 | P2 | BACKLOG | Rebinding, classic/modern controls, prompts, mouse, additional controller features, wheel/FFB | PAL device/menu/gameplay/persistence evidence; no assumed SLUS mappings |
| PAL-035 | P3 | BACKLOG | Units/horn/traffic/lights/mirror/Crash Breaker/developer options | Each PAL address/contract must be established independently |

## F4 — High frame rates and latency

| ID | Priority | State | Work / dependencies | Acceptance and evidence |
| --- | --- | --- | --- | --- |
| PAL-040 | P2 | RESEARCH | Separate simulation, presentation/interpolation, input sampling and audio timing | Do not accelerate physics/clocks. PAL50/60 support does not imply arbitrary-rate safety |
| PAL-041 | P2 | BACKLOG | Independent experimental 120 FPS interpolation, then display-rate/uncapped feasibility | Race/crash/Impact Time/menu/FMV/splitscreen timing, latency and fallback; 144/165/240 are later hypotheses, not current features |

Keep the accepted pacing, VSync, VBLANK and audio clocks unchanged until a bounded
timing task is authorized. Do not sum synthetic optimization percentages into FPS.

## F5 — Audio and standalone soundtrack modding

| ID | Priority | State | Work / dependencies | Acceptance and evidence |
| --- | --- | --- | --- | --- |
| PAL-050 | P2 | RESEARCH | Future local EA TRAX replacement/playlist behavior | Establish PAL original audio containers/codec, indexing, metadata, size/alignment/streaming limits before choosing an implementation |
| PAL-050-TOOL | P3 | BACKLOG | Standalone soundtrack editor | Select original song, import audio, edit title/artist, replace or redirect entries, export/apply, back up and restore; no destructive ISO writes by default |
| PAL-051 | P4 | BACKLOG | Exploratory Spotify application/supported-API integration | Review current official authentication/API/account/playback restrictions and licenses; playlist/next-track mapping and external sync must be proven; no implementation/credentials assumed |
| PAL-052 | P2 | RESEARCH | Other car-engine/SPU2/crackle issues, only if reproducible | Voices/music/FMV and original game behavior preserved; no broad mixer refactor |
| PAL-011-AUDIO | P1 | DONE | Bounded recovery after producer gaps of at least 250 ms | Existing synthetic/WASAPI tests and manual capture/window recovery accepted; not every imaginable interruption |

The standalone tool and Spotify are independent proposals. Inspect legitimate
local source data first; the soundtrack's actual format and writable metadata
contracts have not been established in the evidence reviewed for this handoff.

## F6 — Multiplayer

| ID | Priority | State | Work / dependencies | Acceptance and evidence |
| --- | --- | --- | --- | --- |
| PAL-060 | P2 | RESEARCH | PAL splitscreen and inherited USA support | Both viewports, HUD/GS/audio/players and PAL timing; no new validation claimed |
| PAL-061 | P3 | RESEARCH | PAL IOP/inet/lobby and LAN before VPN | Establish regional protocol and local operation; do not assume old EA servers exist |

## F7 — PAL boot, localization, profiles and distribution

| ID | Priority | State | Work / dependencies | Acceptance and evidence |
| --- | --- | --- | --- | --- |
| PAL-070 | P1 | BACKLOG | First milestone: persistent preferred language with original selection behavior available; Spanish preference, all original SLES-52584 languages and existing PC settings integration | Reuse init 0x00437740 / selection 0x004374C0; guest language=3 selects Spanish resources/internal ID 5. Prove host-to-guest/selector contracts, defaults/INI/CLI and clean startup first; video preferences and screen skipping are later milestones |
| PAL-070-BOOT | P1 | DONE | Historical missing dispatch around 0x00242AC8 | Recovered libmc2 switch case using completion 0x00242A50:0x00242D50; not an unresolved language dispatch or license to patch RAM |
| PAL-070A | P2 | DONE | F9-A GPU Thread, CLI/environment locks and FPS F1/F9 | AUTO + MANUAL, accepted EXE `304821ee...`; no hot thread toggle |
| PAL-070B | P2 | DONE | Live AA, warm-up/overlay and L3+R3/F12 separation | AUTO + MANUAL on `12772164...`; integrated/published, not pending again |
| PAL-070B-FS | P2 | DONE | Fullscreen default for new PAL profiles | Saved window mode/CLI and diagnostic window launcher remain supported |
| PAL-070B-VU | P2 | RESEARCH | VU Fast exposure decision | Existing comparison excludes MAC/STATUS by design; cross-program flag readers remain unchecked. No general F9 option |
| PAL-071 | P1 | RESEARCH | All original PAL languages, accents and new PC labels | Spanish already works; per-language coverage and fallback remain necessary |
| PAL-072 | P1 | TESTING | Existing ISO/profile/LocalAppData recovery plus broader migration coverage | Creation/save/reload and isolated package preference reuse accepted; do not claim all external card formats or concurrent writers |
| PAL-073 | P1 | BACKLOG | Additional game-mode acceptance when relevant | Target missing race/Road Rage/Crash/World Tour/garage/FMV cases; no blanket cartesian regression after small changes |
| PAL-074 | P2 | BLOCKED | Public binary release/distribution only | Local ZIP is ready/accepted; documented rights review is missing. Any later manual release is unverified here |
| PAL-074-GIT | P1 | DONE | Publish engine, PAL main and exact v0.2.0-alpha tag | `bfad7255...` / `2aac9350...`, verified HTTPS recursive clone; no GitHub Release/assets created |
| PAL-075 | P2 | READY | Contributor and issue-reporting guidance | Existing bug template; provide version/hardware/repro/logs without proprietary/personal data; build reproducibility limitations explicit |

Game identity remains SLES-52584 1.01. Original PAL language and video-test screens
work today; **automatic Spanish/60 Hz preference and screen skipping do not**.
The prior read-only review identified En/Du/Sp/Sw resource paths (English, Dutch,
Spanish, Swedish) and the original guest `language=` path. That is static evidence,
not confirmation that `language=3` is accepted by the PC EXE CLI or that its direct
initialization preserves a visible selector. Do not confuse guest argument 3 with
internal Spanish ID 5. Preserve original resource loading and selection callbacks.
Milestone 1 is language persistence with the original sequence available; later
milestones cover preferred 60 Hz, video confirmation/fallback and optional skipping.
Do not hardcode debug cursor addresses as the final implementation or require an
unrelated save. See [boot evidence](BOOT_FLOW.md) for the confirmed historical flow.

## F8 — PAL texture extraction, identification and HD replacement

| ID | Priority | State | Work / dependencies | Acceptance and evidence |
| --- | --- | --- | --- | --- |
| PAL-033 | P2 | TESTING | Existing shared PNG/DDS loader and partial HD333 trials | Real replacements observed, but PAL quality/performance unsatisfactory; no approved HD pack or complete extraction pipeline |
| PAL-033-MAP | P2 | RESEARCH | Original/replacement full-key correspondence | Separate exact identifiers, exact RGBA partners and perceptual candidates; preserve ambiguous/region-specific art |
| PAL-033-EXTRACT | P3 | BACKLOG | Original PAL container parsing, extraction and catalog | Determine format, asset boundaries/indexing and original IDs from legitimate input; automate hashes/metadata/previews with provenance |
| PAL-033-TOOLS | P3 | BACKLOG | External upscaling/replacement/injection workflow | Match runtime requests, alpha/palette, UV/clamp, mipmaps/filtering and upload path; reversible mappings, no original assets in Git |

The backend hashes GS texture data with XXH3-64 and includes palette/format/region
metadata. A PNG/DDS filename prefix is not enough. The historical 333 direct-key
trial and additional candidate maps do not mean a validated converter exists.
Evaluate extraction, cataloging, mappings, replacement loading and visible correct
rendering as distinct stages. Do not extend a failed pack trial by assumption.

## F9 — Vehicle Modding / Custom Car Import (long-term)

All work here is **PLAN / UNCONFIRMED**, not a current importer. Investigate in
order: **Burnout Legends (PSP)**, **Burnout Revenge (PS2)**, then
**Burnout Dominator (PSP)**. Suspected Legends BGV compatibility and reported
version **0x17** are user-reported hypotheses, not binary-validated facts.

| ID | Priority | State | Stage / dependency | Acceptance |
| --- | --- | --- | --- | --- |
| PAL-080 | P4 | BACKLOG | Inspect legitimate source/destination asset headers and BGV versions | Record exact bytes, endian/platform/layout differences; stop if compatibility premise fails |
| PAL-081 | P4 | BACKLOG | Parse one Legends vehicle; validate geometry/material interpretation | Vertex buffers, hierarchy, UVs, texture references, materials, LODs and attachment/animation structures explained; then isolated static rendering |
| PAL-082 | P4 | BACKLOG | Collision, deformation/damage meshes and platform differences | Establish physics/mesh contracts; geometric similarity alone is insufficient |
| PAL-083 | P4 | BACKLOG | Isolated in-game import, then reusable import/export tool feasibility | Backups, bounded validation and correct rendering/collision/damage; no claim that import currently works |

Do not distribute imported commercial assets. Only investigate conversion after
parsing/semantic checks; no guessed shared structs or promised cross-game support.

## F10 — Native Menu Integration and PC Settings

The working F9 overlay is **SOURCE + AUTO + MANUAL** in the shared host. It is not
an integration into Burnout 3's original menu structures. No native PC Settings or
new Quit entry is claimed in the accepted PAL sources.

| ID | Priority | State | Proposed work / dependencies | Targeted acceptance |
| --- | --- | --- | --- | --- |
| PAL-090 | P2 | RESEARCH | Partial static PAL map: constructor 0x003183E0, input 0x00318240, five original entries; pause-menu contracts still missing | Preserve selection/count/entry/help structure; validate seven-row layout and callbacks without assuming USA addresses or live correctness |
| PAL-091 | P2 | BACKLOG | Append PC Settings and Quit Game: preserve all five original entries, including ONLINE and Multiplayer; target seven | Preserve original appearance/transitions/sounds/navigation; separately allocated entry/help arrays, bounds and string lifetime; no USA ONLINE donor storage |
| PAL-092 | P2 | BACKLOG | Native PC Settings screen for implemented display/aspect/render/performance options | Reuse shared settings/CLI/restart semantics; decide whether F9 opens the same settings as an alternative shortcut |
| PAL-093 | P2 | BACKLOG | Native pause-menu Quit to Desktop with confirmation | Preserve all pause choices; localized cancel/confirm and controller behavior; establish pending-save/IO completion and clean shutdown before accepting exit |
| PAL-094 | P2 | RESEARCH | Localize every new native entry, help text and confirmation in the selected original PAL language | Real UTF-16 conversion, capacity/glyph/layout coverage and lifetime across language-resource reload; English literals/byte widening are insufficient |
| PAL-095 | P2 | BACKLOG | Native access to staged language/video/startup preferences | Depends on PAL-070/071; persistent language first, video/skipping later; preserve original selectors, shared precedence and F9 compatibility |

**Reused SOURCE evidence:** USA replaces ONLINE and adds Quit as entry six; PAL's
original constructor creates five entries. The PAL requirement is seven, not a
copy of the USA replacement. Seven external 8-byte entry records require **56
bytes**, plus seven 4-byte help pointers (**28 bytes**), at least 84 bytes of new
table storage before alignment and localized strings. Preserve 8-byte entry and
4-byte help alignment; do not extend inline rows into the original help fields.
USA uses a six-visible-row layout; seven PAL entries need a validated layout or
original-style scrolling. Additional callbacks must handle the two new indices
before delegating unchanged original indices to the PAL handler.

USA's pool borrows ONLINE strings and initializes English text once. That is not
safe for PAL's retained ONLINE or language reloads. Establish owned guest-visible
storage and lifetime; the original synthetic scratch range is not free game memory.
New labels, settings text and Yes/No questions must follow the selected PAL
language with correct UTF-16, glyph coverage and nontruncating capacities.
All seven-entry, localization and pause-Quit behavior remains **unimplemented**.

Prefer the original UI system. Extend a single isolated entry first, then
navigation/callback tests, then persistence/CLI/restart behavior and save-safe exit.
Avoid replacing the native menus with external overlays unless an evidenced
technical limitation requires it. This phase does not redesign or replace F9 now.

## Next authorized-work proposal and stopping rule

1. Review these uncommitted documentation changes and the main-checkout/baseline
   discrepancy. Prepare a clean isolated baseline only with authorization.
2. Recommend **PAL-070 milestone 1**: reuse the established language initializer,
   selection callback and guest argument mapping; resolve only the remaining
   host-to-guest and original-selector contracts. No repeated general audit.
3. Implement persistent language preference only after those contracts and targeted
   tests are agreed. Preserve original selection/loading. Video preferences and
   skipping come later; native menus remain separate and F9 stays accepted.
4. Revisit other opportunities only when selected by the maintainer. Completed
   PAL-010, USA-001/002 and F9 work are reusable foundations, not pending tasks.

At completion record exact revisions, actual tests/evidence, PAL50/60 limits and
remaining risks. Reuse engine framediff/gsmem/timeline/replay/profiling and existing
tests; do not build new instrumentation by default. A roadmap is not Git or build
permission. Once the scoped acceptance criteria are met, stop.
