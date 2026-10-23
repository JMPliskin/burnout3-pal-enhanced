# Burnout 3 PAL Enhanced: development continuity

Updated 23 October 2026. Read [AGENTS.md](AGENTS.md), the
[session handoff](docs/SESSION_HANDOFF.md) and [roadmap](docs/pal/ROADMAP.md)
before changing anything. This update is documentation only: no branch switch,
code change, build, game execution, commit or publication was performed.

Current consolidation base: PAL `a2e524eaf7baa6b586482798526a773b5f8f9943` (README-only child of the
release source below), engine `bfad7255f17f7789a8bbb0a59a6e3a003535c548`.
Earlier no-publication statements describe the handoff session. See its
consolidation update for the current workspace and historical path scope.

## Baseline and the local checkout discrepancy

| Reference | Exact identity / scope |
| --- | --- |
| Game | European PS2 Burnout 3, SLES-52584 / `SLES_525.84`, version 1.01 |
| Published PAL source | `pal-enhanced`, `2aac9350af0a139771d8a5677d0863b216343efc` |
| Published PAL tag | `v0.2.0-alpha`, annotated object `830b9cb314ce75be06bbad2b8c56df3e92ef39d1`, peeled commit `2aac9350...` |
| Published engine | `integration/pc-settings-accepted-engine`, `bfad7255f17f7789a8bbb0a59a6e3a003535c548` |
| Main local checkout at handoff | `pal-enhanced`, `13f42950282d6167707d6ae1bbf9943f02098248`; source is older, despite the same branch name |
| Main local remote-tracking ref | `origin/pal-enhanced` at `e400b619...`; stale relative to the verified publication |

**Do not build the old main checkout as if it were v0.2.0-alpha.** Its code,
engine gitlink and tests were intentionally not advanced through a dirty tree.
The final source exists locally in the release dossier's `publication-repo` and
`git-publication-20261022/fresh-remote-checkout`; see the handoff for relative paths.
Do not reset, merge or switch the main checkout without a separate authorized plan.

Git publication was verified on 22 October: engine first, PAL main fast-forward,
then the PAL tag; a fresh HTTPS recursive clone fetched the exact engine from
JMPliskin's fork. Both master branches, other refs and old releases were preserved.
No GitHub Release or binary upload was performed by Codex. Any later maintainer
release activity has **not** been checked in this documentation task.

## Evidence labels

- **SOURCE**: confirmed in inspected code/configuration at the named revision.
- **AUTO**: an existing automated result/report was reviewed; not rerun today.
- **MANUAL**: explicitly accepted by the maintainer on the identified executable.
- **OBSERVED**: recorded debugging behavior; causal scope must be stated.
- **UNCONFIRMED**: hypothesis, missing evidence or unavailable conversation detail.
- **PLAN**: future work, not implemented or authorized by its presence here.

Detailed claims and limits are in [architecture/evidence](docs/pal/ARCHITECTURE_AND_EVIDENCE.md),
[boot findings](docs/pal/BOOT_FLOW.md), [validation](docs/pal/VALIDATION.md) and
[status](docs/pal/STATUS.md). Old numerical claims remain tied to their old reports.

## Accepted functionality — published baseline

| Capability | Evidence / limits |
| --- | --- |
| Native Windows startup, own PAL ISO chooser/path recovery, Spanish, original language and 50/60 Hz screens | SOURCE + MANUAL; all languages/modes are not exhaustively certified |
| Profile creation, save/reload, menus, FMV, Start movie skipping, races and audio | MANUAL; do not infer equivalence of every underlying service |
| Hor+ camera, original 4:3, 16:9/fill and adapted HUD | SOURCE + AUTO (14 camera/27 HUD cases) + MANUAL; wider displays/cinematics remain scoped |
| PAL-010 regional GPU presentation | SOURCE + AUTO + MANUAL at 2x ON/OFF and 3x/4x pacing ON; 640x512 selector keeps the original fallback |
| USA-001 texture page index; USA-002 eligible PSMCT16 grouped writes | AUTO + MANUAL; structural/synthetic benefits established, sustained PAL performance gain not quantified |
| F9-A persistent settings, CLI locks, GPU Thread requested/effective state, FPS via F1/F9 | SOURCE + AUTO + MANUAL; GPU Thread changes require restart |
| F9-B live OFF/FXAA/SMAA, pipeline warm-up, overlay handoff | SOURCE + AUTO + MANUAL; do not restore the rejected restart-only AA alternative |
| L3+R3 menu, keyboard F9 menu, independent F12 GS capture | SOURCE + AUTO + MANUAL; held chord is edge-triggered and retains input-release protection |
| Bounded audio recovery after host interruptions | SOURCE + AUTO + MANUAL for tested capture/window movement; not every possible pause/device |
| Fullscreen default for new PAL profiles | SOURCE + AUTO + MANUAL; saved window mode and CLI overrides prevail |

## Corrections that must survive the handoff

- `0x00242AC8` is a recovered **memory-card worker switch case**, not a currently
  missing language-selection function. Keep completion `0x00242A50:0x00242D50`
  and the PAL CardOr contracts. Original PAL ELF is authoritative.
- Retain relocation/compatibility registrations, copied-code discoveries and
  preserved generated EE/VU inputs. `PAL_RELOCATION_AUDIT` also enables required
  compatibility hooks; it must not be removed as if it were only optional logging.
- Do not loosen global GS ownership/dirty/coherence contracts. C validates visible
  pages only in the certified PAL geometry; invalid candidates still fall back.
- L3+R3 used to trigger full captures as well as F9. Multi-second capture stalls
  explained the reproduced controller-only freezes; keyboard F9 did not reproduce
  them. AA changes were not proven to cause those multi-second pauses.
- Scratch `[0x01ED0000,0x01EF0000)` is isolated synthetic-test memory. Full startup
  offers it to the heap. Never treat it as free game memory.

## What is still pending

Immediate next milestone: persistent preferred language, with Spanish preferred,
original selection behavior available and clean new-profile startup. Init
`0x00437740` / selection `0x004374C0` are established static entry points; guest
`language=3` selects Spanish resources/internal ID 5, but host transport and full
selector behavior remain unvalidated. Video preferences and optional startup
skipping are later milestones. These preferences are **not yet implemented**.

Native main-menu PC Settings/Quit and pause-menu Quit are a **separate planned
phase**, not existing F9 functionality. Preserve all five original PAL entries,
including ONLINE/Multiplayer, and append two for a seven-entry target. Do not
reuse USA ONLINE storage; allocation, layout, localized UTF-16 and save-safe
pause-menu exit still require validation. VU Fast remains unexposed pending flag/
cross-program fidelity checks. SSAA, ultrawide, high-rate interpolation, soundtrack
tools, broader textures/HD, vehicle import and multiplayer remain future work.

Known limitations: minor non-blocking 3x skybox seam; load-dependent 4x GPU drops;
isolated pacing-OFF long interval with no established cause; incomplete PAL50,
hardware, effects and game-mode coverage. Do not reopen completed fallbacks/audio
investigations without a new reproducible regression.

## Build, distribution and history

Use [validation/build commands](docs/pal/VALIDATION.md) and the handoff's artifact
map. A fresh clone resolves the engine but is **not** a turnkey source rebuild:
private ELF/ISO, generated EE/VU, manifests and compatible dependency caches are
required. Preserve them; do not regenerate or clean builds by default.

The local Windows x64 package is versioned 0.2.0-alpha, requires AVX2 and a Vulkan
1.3 driver, and keeps `%LOCALAPPDATA%\Burnout3-PAL-Enhanced\`. Package/startup/F9
acceptance is recorded for the new EXE; old test results are not substituted for it.
Public binary rights remain unresolved in the retained evidence. MIT engine/port
licensing is not clearance for the reconstructed original program.

The previous main-checkout continuity document is preserved in
[Git at 13f4295](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/13f42950282d6167707d6ae1bbf9943f02098248/PAL_DEVELOPMENT.md).
Later detailed chronology is in
[accepted ADAPTATIONS](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/games/burnout3-pal/port/ADAPTATIONS.md).
It contains historical next actions superseded by the current roadmap.
