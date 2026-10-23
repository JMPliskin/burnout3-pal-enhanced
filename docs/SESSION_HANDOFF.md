# Burnout 3 PAL Enhanced — Session Handoff

**Snapshot: 23 October 2026. Documentation-only consolidation.**
Read root [AGENTS.md](../AGENTS.md), [PAL_DEVELOPMENT.md](../PAL_DEVELOPMENT.md),
the [roadmap](pal/ROADMAP.md), PAL AGENTS and DEVELOPMENT_POLICY before changes.
Repository convention is lowercase `docs/`; this is the requested session handoff.
Technical documentation/instructions/comments/commits are English; user reports
are concise Spanish. This file replaces reliance on the compacted conversation.

## 2026-10-23 repository consolidation update

This independent development clone starts at PAL `a2e524eaf7baa6b586482798526a773b5f8f9943`,
the README-only child of release source `2aac9350af0a139771d8a5677d0863b216343efc`.
The engine remains `bfad7255f17f7789a8bbb0a59a6e3a003535c548`.
The preceding snapshot and its no-commit statements describe the earlier handoff
session, not this authorized consolidation. The release tag is not moved.
Published README installation/licensing wording is preserved. Historical rights
statements retain their dated evidence scope, not a new legal determination.
No release upload or new distribution-rights review is established by this task.

The old checkout and clean release reference remain preserved. Workspace dossiers
and private-input paths below are relative to the **old checkout root**, not this
new clone. No private inputs, generated EE/VU or dependency caches were transferred;
a source clone is not yet a validated build environment.
The obsolete branch tips are reachable through the release tag; links now use
permanent SHAs. Historical branch names remain provenance, not dependencies.
Keep `master`, `recovery/pal-stable-13f4295` and all release tags. Future features
use local branches/worktrees; temporary remote branches are for collaboration/PRs.

PAL-070 implementation remains unstarted. Resolve only host-to-guest argument
routing, visible-selector entry and resource/state lifetime before implementing
language defaults/INI/CLI precedence. Reuse accepted static findings; video
preferences, screen skipping and native menu additions remain separate.

## 1. Project identity and objectives

Burnout 3 PAL Enhanced is JMPliskin's unofficial European adaptation of Daniel
Gomes Vieira's burnout3-pc and ps2-port-engine. Target **SLES-52584 1.01**, executable
`SLES_525.84`, with legitimate user-supplied PAL inputs. Preserve original game
logic, Spanish, all original PAL language/video choices, saves and working PC
features. Improve selectively; a newer USA feature is not automatically a PAL one.

Use the original PAL ELF as the authority. Match instructions, contracts and
structures, not guessed regional offsets. PAL correctness does not require
pixel-identical output with the USA version. No proprietary game files/generated
game code or personal data belong in Git.

## 2. Current accepted baseline and exact identities

| Item | Identity |
| --- | --- |
| Published PAL source / main | `2aac9350af0a139771d8a5677d0863b216343efc`, `pal-enhanced` |
| Published engine / PAL gitlink | `bfad7255f17f7789a8bbb0a59a6e3a003535c548`, `integration/pc-settings-accepted-engine` |
| PAL tag | `v0.2.0-alpha`; tag object `830b9cb314ce75be06bbad2b8c56df3e92ef39d1`; peeled PAL commit above |
| Prepared candidate | `fac179a5c6e737b6e3b96425859259d7f52b11ea`; final `2aac9350` adds only two documentation corrections |
| Accepted feature EXE | `12772164da2b71557d61e3097ec2fc18c946020cc505a7993886181f7870b2e4` |
| Versioned release EXE | `a7122e8718fb2459a0bd231784d8fce8930485eb43985cbe2814c0d60a2dc341` |
| Local release ZIP | `bf8cdc0c2c8836f63bb4e6642bf1883a415b1d541d168d2ea90b69c6558b9a71`; 12,305,892 bytes |

SOURCE/Git publication was verified on **22 October 2026** using remote refs and
a fresh HTTPS recursive clone. The engine was pushed first, then PAL main by
normal fast-forward from `e400b619...`, then the exact annotated PAL tag. Both
master branches, other refs and the old release were unchanged. No force-push.
Codex did **not** create a GitHub Release or upload EXE/ZIP. Later actions by the
maintainer are not known; do not infer that the binary release now exists.

The game release number 0.2.0-alpha and roadmap schema 0.1.19 are independent.

## 3. Repository/branch structure and local traps

Forks: [PAL](https://github.com/JMPliskin/burnout3-pal-enhanced),
[engine](https://github.com/JMPliskin/ps2-port-engine).
Daniel's original repositories remain upstream. `.gitmodules` in the published
PAL source points to JMPliskin's engine fork and retains the original upstream URL.

**Main local checkout is intentionally old:** branch `pal-enhanced`, HEAD
`13f42950282d6167707d6ae1bbf9943f02098248`, engine gitlink `28057a1a...`.
Its local `origin/pal-enhanced` is stale at `e400b619...`. Do not mistake the branch
name or today's updated documents for newer functional code. No branch/reference
was moved in this handoff task. Root source lacks some newer tests/CMake targets.

Local source/evidence locations, **relative to the main repository root**:

| Location | Role |
| --- | --- |
| `../PAL-Workspace/diagnostics/pal-release-0.2.0-alpha-20261022/publication-repo` | Independent clean repository with final `2aac9350` source, engine and correct PAL tag |
| Same dossier, `git-publication-20261022/fresh-remote-checkout` | Recommended clean existing baseline for a future authorized isolated development branch: PAL 2aac9350 / engine bfad7255; recursive clone and later read-only cleanliness check confirmed |
| `games/burnout3-pal/build/release-0.2.0-alpha-src/run-20261022` | Linked release-candidate worktree, branch release/v0.2.0-alpha-candidate at `fac179a5` |
| `games/burnout3-pal/build/f9-consolidation-src/run-20261022` | integration/pc-settings-accepted, `09560b8f...`; functional commit `0d083940...` |
| `../Experiments/pal-planning-docs` | Existing planning worktree; OS denied roadmap/changelog writes, so the old copy and other dirty work remain untouched; root docs are the consolidated current version |
| `../Experiments/pal-antialiasing` | Historical AA/HD worktree, read-only/preserved; do not merge accumulated patches |
| `../Experiments/pal-gpu-causal-trace` | Preserved original PAL-010 diagnostic worktree |

The main repo has an unrelated **USA** local `v0.2.0-alpha` at
`06a5a27bf1315963679a5b9e3b0abef5f999dba1`. Never move/delete/push that tag as PAL.
The PAL tag lives in `publication-repo`; its previous unpushed candidate tag is
archived locally under `refs/release-preparation/v0.2.0-alpha-fac179a` there.

At task start root AGENTS already had the uncommitted incremental policy and
there was an unrelated untracked filename resembling an initialization command.
Both must be preserved. This task adds documentation-only edits, stages nothing
and creates no commit. Some old worktrees retain historical AGENTS/next-action
text; root permanent policy/current roadmap and user scope take precedence over
obsolete instructions to investigate already closed PAL-010 again.

Windows denied normal writes and patch writes to the planning worktree's two
documents. No ACL, ownership, sandbox permission or global Git trust setting was
changed to force access. A `planning-sync.patch` is prepared under
`games/burnout3-pal/build/session-handoff/run-20261023/`; an authorized user can
review it once file access is available. It was prepared for roadmap 0.1.18 and
does not include the final 0.1.19 refinements; do not apply it as current without
review. Do not mistake the unchanged planning copy's 0.1.17 priorities for the
current root roadmap 0.1.19.
When applying it to that older worktree, also review links to the new companion
documents; the patch is not an authorization to overwrite its other user edits.

## 4. Confirmed working features

Labels: **SOURCE** inspected code, **AUTO** reviewed existing automated report,
**MANUAL** maintainer acceptance, **OBSERVED** debugging record,
**UNCONFIRMED** hypothesis/missing evidence, **PLAN** future work.

| Feature | Scope of confirmation |
| --- | --- |
| PAL startup / Spanish / original language and 50/60 screens | SOURCE + MANUAL; no automatic language/video preference yet |
| ISO chooser/path persistence/recovery | MANUAL, including isolated player installation |
| Virtual profiles save/reload, FMV, Start skip, menus and races/audio | MANUAL; not every game mode or external save format |
| Hor+ camera / 4:3 / 16:9/fill / HUD | SOURCE + AUTO 14 camera and 27 HUD cases + MANUAL |
| Regional C | SOURCE + AUTO + MANUAL 2x ON/OFF, 3x/4x pacing ON; preserve 640x512 selector fallback |
| USA-001 / USA-002 | AUTO memory/index checks + MANUAL; structural/synthetic gains, not quantified sustained PAL speedup |
| GPU Thread F9 / FPS F1 and F9 / persistence and CLI locks | SOURCE + AUTO + MANUAL; GPU Thread needs restart, requested/effective state distinguished |
| OFF/FXAA/SMAA | SOURCE + AUTO + MANUAL live switching and persistence; no restart required |
| F9 keyboard / L3+R3 / independent F12 capture | SOURCE + AUTO + MANUAL; held chord toggles once, release guard retained |
| Audio latency recovery | SOURCE + AUTO + MANUAL after tested capture/window interruptions only |
| Fullscreen for new profiles | SOURCE + AUTO + MANUAL; saved window mode and CLI respected |

VU Exact remains the reference. F9 is a shared-host overlay, **not** new native
main/pause-menu entries. SSAA, VU Fast exposure and native PC Settings/Quit are
not current accepted features.

## 5. Incomplete work and explicit unknowns

- **PLAN:** first PAL-070 milestone is persistent preferred language, Spanish
  preferred, all original SLES-52584 language choices and existing PC settings
  integration, with original selection behavior available. Preferred 60 Hz and
  optional startup skipping are later milestones; no unrelated save dependency.
- **RESEARCH:** VU Fast exists at startup (`--vu-fast`), but prior comparison
  intentionally excluded MAC/STATUS/pending flags and started programs separately.
  Missing proof: later PAL programs reading prior Fast flags. No regeneration needed
  just to select existing blocks; this is a fidelity gap, not a missing backend.
- **TESTING/RESEARCH:** broad PAL50/effect/mode/device coverage; GS replay reference
  differences; CSM2/TEXCLUT frequency and cost. These are not active broad audits.
- **PLAN:** retain all five original main-menu entries, including ONLINE and
  Multiplayer; append PC Settings and Quit Game for seven. Native pause-menu Quit
  to Desktop requires confirmation and save-safe shutdown. All new entries/help/
  dialogues follow the selected original PAL language. Separate this from F9.
- **UNCONFIRMED:** original soundtrack container/metadata/size contracts; Spotify
  feasibility under current official APIs/terms; cross-game vehicle formats/BGV.
- Binary distribution rights remain unresolved in retained evidence. Code/tag
  authorization did not authorize the agent to create a release or upload assets.

## 6. Critical discoveries and rejected approaches

See [architecture/evidence](pal/ARCHITECTURE_AND_EVIDENCE.md) for exact contracts.

1. `0x00242AC8` was a missing case in the libmc2 worker, not a separate boot/locale
   function. Preserve completion `0x00242A50:0x00242D50`, command table and CardOr.
2. Keep copied-code discoveries, original PAL IOP modules and relocation
   compatibility registrations. Do not strip `PAL_RELOCATION_AUDIT` blindly.
3. Synthetic scratch `[0x01ED0000,0x01EF0000)` is offered to the game heap on full
   startup. It is not free runtime memory. s32-return/u64-argument and register/
   memory coverage limits remain; game execution does not prove every function.
4. PAL-010's global stale flag rejected targets with valid visible pages. The
   accepted exception checks a **certified PAL60** region/PSM/video/scale/resource
   plus CPU writes/pending producers; unsafe cases retain fallback. No blanket
   stale acceptance or global dirty/ownership change.
5. Replay does not reproduce live PresentGpuPicture or an interactive F9 change.
   Read-category/fence/copy counters and CPU-busy/GPU-busy are different quantities.
6. The controller F9 chord also triggered full captures. Actual multi-second
   capture gaps (~7.56/6.71 s) caused host stalls/audio catch-up; keyboard F9 did
   not reproduce that defect. Earlier unmarked 2-second gaps were not individually
   attributable to AA. Keep live AA, warm-up and overlay fixes; reject the preserved
   restart-only AA branch as the default behavior.
7. Capture/drag share host/EE blocking while WASAPI continues. Old catch-up left
   ~650–680 ms queued; +/-0.5% regulation was too slow. Recovery is gated by
   >=250 ms production gap, two-second window, excess tolerance, consumer ownership
   and 2 ms crossfade. No indiscriminate flush, game-clock adjustment or new mixer.
8. HD trials were partial: 21-file trial had zero applications; HD333 did replace
   images but worsened PAL quality/performance. Full-key equality, image equality
   and acceptable regional replacement are distinct. No general converter exists.
9. Driver initialization measured ~40/51 ms, not the several-second chord freeze.
   Removing required Vulkan lifetime waits was never justified. Keep safe reuse.
10. The main repo's same-named USA tag cannot be reused for PAL. Engine objects
    must be publicly reachable before dependent PAL gitlinks are published.

### Final reused findings: PAL-070 and native menus

**SOURCE, from the preceding read-only review; not re-audited for this update:**
PAL language init is `0x00437740`, selection `0x004374C0`. Original guest
`language=3` selects GlobalSp/HeadSp and internal Spanish ID 5. The reviewed
resource branches are En/Du/Sp/Sw (English/Dutch/Spanish/Swedish). Guest argument
3 is not internal ID 5, and this is not a confirmed host CLI option. Host-to-guest
transport, complete direct-initialization effects and visible-selector contracts
remain to be validated. Preserve original loading/callbacks; do not poke debug RAM
or blindly use a direct path when the original selection sequence is requested.

**SOURCE:** USA replaces ONLINE and adds Quit as entry six. PAL's original
constructor `0x003183E0` creates five entries; input is `0x00318240`. The PAL
requirement is seven without losing any original entry, not a USA replacement.
New guest-visible arrays need at least **56 entry bytes + 28 help-pointer bytes**,
with 8-byte/4-byte alignment, plus localized strings and appropriate lifetime.
Do not expand inline entries into the existing help fields or borrow USA ONLINE
string storage. Language resource reloads must not leave dangling menu text.

Seven-row layout/navigation and extra-index callbacks are unvalidated; USA uses
six visible rows. New labels, settings text and confirmations require correct
UTF-16 conversion, capacity and glyph coverage in the selected PAL language;
USA's byte-wise string widening and English literals do not establish that.
Main-menu and pause-menu Quit need separate callback/save-safety acceptance,
including localized confirmation/cancel behavior. No native additions are implemented.
See [architecture/evidence](pal/ARCHITECTURE_AND_EVIDENCE.md) and PAL-090–095.

## 7. Known bugs, limitations and workarounds

- Minor/non-blocking 3x skybox seam: leave it separate; no investigation now.
- Heavy 4x GPU load/drop observations, around 94% busy in one sample; not a proven
  regional regression or reason to import unrelated optimizations.
- Isolated pacing-OFF ~375.6 ms interval, cause unknown; keep pacing optional.
- Audio recovery acceptance is bounded to tested capture/window interruptions.
  F12 still deliberately performs expensive capture/readback/file work.
- Original selector must retain full-height fallback to preserve button text.
- VU compiled coverage near/at 100% is session-specific, not Fast or complete
  floating-point/flag equivalence. Unmatched programs may use interpreter fallback.
- Historical host `0xC0000409` remains undiagnosed; later success with private TEMP
  is not a root-cause proof. Retain any original logs/dump.
- All languages, hardware, PAL50, special cameras/modes and concurrent profile
  writers are not certified. Advanced save formats and turnkey clean source builds
  are not claimed. No evidence says current startup requires another person's save.

## 8. Recent tests and evidence locations

All dossiers below are under repository-root sibling `../PAL-Workspace/diagnostics/`.
They are private local evidence, not tracked/shipped artifacts. Original REPORTs
may say pending because they precede later maintainer acceptance; use the current
changelog/explicit acceptance and exact EXE identity to resolve that chronology.

| Dossier | Evidence to reuse |
| --- | --- |
| `pal-gpu-causal-trace/abc-20261020`, `abc-scale-20261021`, `phase6-20261021` | A/B/C cause, scales and independent pacing; initial selector exception; old EXEs untouched |
| `pal-regional-integration-20261021` | Clean C build, predicate tests, short replay and accepted live manual sessions; EXE `612719b3...` |
| `pal-usa001-texture-page-index-20261022` | 2,000 differential operations, alias case, four replay runs with identical image/GS memory, acceptance; EXE `8e469c8f...` |
| `pal-usa002-gs16-20261022` | 1,504 cases per scalar/SIMD build, protected buffers, short replay, synthetic benchmark, accepted EXE `f0534a16...`; final USA_PERFORMANCE_COMPARISON.md |
| `pal-f9-a-20261022` | 35 menu/settings checks, reopen, real Vulkan GPU OFF/ON probes and acceptance; EXE `304821ee...` |
| `pal-f9-b-20261022` | AA integration and bounded existing VU Fast test; original EXE `7d4a0115...`, not latest accepted build |
| `pal-f9-b-live-aa-20261022` | Warm-up/overlay and interaction tracing; EXE `267450fc...`; source of capture-vs-keyboard diagnosis |
| `pal-window-drag-20261021` | Existing host-gap/queue diagnosis with pacing ON/OFF; not a new investigation target |
| `pal-f9-b-controls-audio-fullscreen-20261022` | Accepted `12772164...`; 27 audio combinations/3,000 normal differential operations, native WASAPI synthetic recovery, 29 menu and 134 native checks, later manual capture/drag/chord/fullscreen acceptance |
| `pal-release-0.2.0-alpha-20261022` | build_identity, immutable input hashes, package guard/identity/acceptance; clean extracted EXE startup/selector/F9/AA accepted; saved SMAA, short restart with unchanged INI |
| Same release dossier, `git-publication-20261022` | Engine/main/tag push records, before/after refs, fresh remote clone and final verification; final docs-only PAL commit `2aac9350` |

The final performance analysis found about 59.9 presentations/s across three
versions, no quantified causal CPU/whole-game improvement. USA-002 had fewer
windows with >20 ms maxima versus one USA-001 session (8/90 vs 2/90); scene load
and PAL-010 between-run variation prevent a universal smoothness claim. Percentiles
were of window means. Synthetic GS16 gains must not be summed into game FPS.

## 9. Changes that must not be reverted accidentally

- PAL language/video selectors, Spanish setup, memory-card ABI and worker completion.
- Regional key/video/visible-page/physical-scale/initialization/conflict guards;
  safe fallback and 640x512 selector treatment, independent pacing.
- USA-001 validity/index lifetime transitions and USA-002 scalar fallback/format/ISA guards.
- Hor+ caller restoration/nonaccumulation and scoped HUD identities.
- Distinct runtime versus saved preferences, CLI/environment locks, save rollback,
  GPU Thread restart/effective-state reporting and FPS F1/F9 compatibility.
- Live OFF/FXAA/SMAA, immutable pipeline warm-up, safe resource lifetime and overlay mutex handoff.
- L3+R3 menu-only edge detection/release protection and keyboard F12 capture latch.
- Qualified consumer-side audio recovery, normal-output guards and crossfade.
- Fullscreen only as a new-profile default; no overwrite of explicit window choices.
- Exact VU reference, local artifact hashes, private Jobs, all old EXEs/sessions,
  published history/tags/remotes and personal saves.

## 10. Immediate priorities

1. Review the uncommitted documentation, branch mismatch and any user changes.
   No standing permission exists to stage/commit/push this handoff.
2. Obtain the next task's authorization and select a clean isolated checkout based
   on final `2aac9350` / `bfad7255`; do not merge it into the dirty old root automatically.
3. Proposed next functional work: **PAL-070 milestone 1**, persistent preferred
   language with original selection available. Reuse the identified initializer/
   callback; resolve remaining host/selector contracts, not a new general audit.
   Video preferences and optional skipping follow in later milestones.
4. Keep original-menu PC Settings/Quit as a distinct incremental phase. Do not
   reopen PAL-010, USA performance adoption, working F9/AA/audio or VU regeneration.

## 11. Medium/long-term phases

The single [roadmap](pal/ROADMAP.md) preserves prior IDs and separates:

- Recompilation/tooling completeness and bounded compatibility gaps.
- Native seven-entry main menu retaining ONLINE/Multiplayer, added PC Settings/
  Quit Game, and separate pause-menu Quit to Desktop; localized UTF-16, confirmation
  and save-safe shutdown, reusing the original UI where feasible.
- Actual SSAA/ultrawide, selected renderer/CPU/compiler improvements and latency.
- Simulation/render/audio separation before independent 120 FPS/uncapped experiments.
- Standalone soundtrack replacement tool: format/container/metadata/index/size
  research, select original song, import/edit title/artist, apply/export and restore.
- Exploratory Spotify playback/playlist/next-track integration, conditional on
  current authentication/API/terms and external synchronization constraints.
- Original PAL texture extraction/catalog, exact runtime identifiers, reviewed
  replacement mappings, alpha/mips/filtering/upscaling and reversible runtime loading.
- Long-term vehicle investigation: Legends PSP first, Revenge PS2 second,
  Dominator PSP third. Reported BGV 0x17 compatibility is **unconfirmed**. Stage
  headers -> one parser -> geometry/material/static display -> collision/damage/
  deformation -> isolated import -> tool feasibility. No importer works today.
- PAL multiplayer/LAN and other mods only as separately authorized work.

## 12. Recommended next Codex task

**PAL-070 milestone 1: persistent language preference with original selection
behavior available, implemented only after authorization.** Reuse init
`0x00437740`, selection `0x004374C0` and the guest `language=3`/internal ID 5
mapping. First resolve host argument transport, the safe visible-selector entry
and state/resource lifetime. Then define shared defaults/INI/CLI/F9 semantics,
requested/effective state and focused tests. Spanish is the preferred default;
retain all original language choices and original selection as an option.
Do not claim a saved preference is applied before its backend works. Video
preferences and startup skipping are later milestones. No old cursor-address
writes or dependence on unrelated saves; stop at a concrete missing contract.

## 13. Build/testing commands, private inputs and package

Use [VALIDATION.md](pal/VALIDATION.md) for verified argument syntax, current targets,
Job isolation and cautions. Begin with read-only branch/status/gitlink checks.
Examples are documentation, not instructions to execute without scope:

```powershell
git branch --show-current
git rev-parse HEAD
git status --short
git ls-tree HEAD engine
py -3.12 -B games/burnout3-pal/tools/run_pal_pipeline.py --list
py -3.12 -B games/burnout3-pal/tools/run_pal_pipeline.py --batches batch-001 --validate-only
```

Keep private inputs (paths below relative to repository root):

- ELF `games/burnout3-pal/extracted/SLES_525.84`, cleaned map under PAL build and
  own ISO resolved by the correct checkout's game.json; never version them.
- EE sources `games/burnout3-pal/build/bulk/run-6785cb70ebf8/published/lifted/`.
- VU capture `games/burnout3-pal/build/vu-captures/run-1d27d7abf9a5/vu_programs.txt`,
  SHA-256 `deff4afda9b0d277532c222ff8e56919d5829743a850122c0ef1d10cf98d1a82`;
  97 programs, 24 VU0/73 VU1, 17,008 pairs. Do not move or regenerate by default.
- Historical VU exact build `native-relocated/run-180cb57a8a54` and 582 synthetic
  comparisons; original dependency cache/tree `native-bootstrap/run-15c1a6f3206b`.
  These directory names are under `games/burnout3-pal/build/`.
- Versioned release build used source at `182db779...` plus later non-gameplay
  packaging/documentation commits. Its source/object hashes identify compatibility;
  do not blindly reuse old v0.9.0 ABI objects or rebuild 9,383 functions.

Runtime data remains `%LOCALAPPDATA%\Burnout3-PAL-Enhanced\` (INI/ISO path,
memcard, logs). Test with isolated LocalAppData and a copied card, not personal data.
Latest accepted diagnostic BATs are `F9-B Controls Fullscreen 2x ON.bat` and
`F9-B Audio Diagnostic Window 2x ON.bat` in their dossier. Old launchers can point
to older builds. Release EXE starts without a BAT; do not force diagnostic flags
or window mode into the normal package.

Release assets under the release dossier: `build/Burnout3-PAL-Enhanced.exe`,
`windows/Burnout3-PAL-Enhanced-0.2.0-alpha-windows-x64.zip`, adjacent `.zip.sha256`,
and `publication-repo/RELEASE_NOTES_v0.2.0-alpha.md`. ZIP has exactly EXE, LICENSE,
player README, notes, third-party notices and EXE checksum. No ISO, BIOS, loose
game assets, cards/INI, logs or caches. Reconstructed game code in the EXE still
requires a separate distribution-rights decision; MIT alone is insufficient.

## 14. Documentation entry points and limits

- [AGENTS](../AGENTS.md): durable instructions, incremental policy and permissions.
- [PAL_DEVELOPMENT](../PAL_DEVELOPMENT.md): current state, provenance labels and local-code mismatch.
- [Roadmap](pal/ROADMAP.md) / [changelog](pal/PLANNING_CHANGELOG.md): IDs, phases and state corrections.
- [Architecture/evidence](pal/ARCHITECTURE_AND_EVIDENCE.md): contracts, memory, source paths, failed approaches and quantitative limits.
- [Boot](pal/BOOT_FLOW.md), [differences](pal/PAL_DIFFERENCES.md), [status](pal/STATUS.md), [validation](pal/VALIDATION.md).
- [README](../README.md) / [release notes](../RELEASE_NOTES_v0.2.0-alpha.md): user-facing accepted features and constraints.
- [PAL policy](../games/burnout3-pal/DEVELOPMENT_POLICY.md) and
  [batch guide](../games/burnout3-pal/tools/batches/README.md).
- [Published adaptation history](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/games/burnout3-pal/port/ADAPTATIONS.md),
  [recovery history](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/games/burnout3-pal/port/RECOVERY.md)
  and the local release publication report: immutable chronology, not a reason to
  repeat already accepted work or reuse an old ABI recipe indiscriminately.

Some original discussion details were lost through compaction. Only recoverable
source/reports and explicit acceptance are recorded. Remaining host/selector and
native pause-menu/allocation/lifetime contracts, missing ELF hashes, soundtrack
structures, Spotify permissions and BGV validation have not been invented.
This final documentation refinement reused the prior read-only findings. It ran
no Git commands, source audits, builds or gameplay tests and changed no worktree
other than the named root documents. No new feature is implemented.
