# Architecture and evidence register

Consolidated 23 October 2026. Evidence labels are defined in
[PAL_DEVELOPMENT.md](../../PAL_DEVELOPMENT.md). SOURCE claims below refer to
PAL `2aac9350af0a139771d8a5677d0863b216343efc` and engine
`bfad7255f17f7789a8bbb0a59a6e3a003535c548`, **not** the older main checkout.
AUTO/MANUAL results are retained records, not tests rerun for this document.
Private dossier paths are repository-root-relative workspace descriptions in the
[handoff](../SESSION_HANDOFF.md); never publish their game inputs or generated data.

## 1. Native, emulated and PAL-specific responsibilities

| Layer | Established architecture | Evidence / limits |
| --- | --- | --- |
| EE game code | Original PAL ELF -> JSON analysis/lifting -> native C++ objects and generated dispatch; host overrides only where a PAL contract is established | SOURCE + AUTO; native execution is not blanket semantic equivalence; unknown dispatches are not permission to add stubs |
| Shared host | USA-derived ps2-port-engine handles kernel services, memory/devices, DMA/SIF, disc, scheduling, window/input, settings and rendering | SOURCE; backend presence, game activation and PAL acceptance are separate facts |
| IOP/audio | Original PAL modules run through the IOP VM and shared service paths; SPU2 production feeds the host audio consumer/WASAPI | SOURCE + OBSERVED + MANUAL; absent imports retain explicit traps, not fabricated success |
| VU | Captured PAL microprograms have compiled blocks; missing/changed programs retain interpreter fallback | SOURCE + AUTO; session coverage is not complete float/flag equivalence or VU Fast acceptance |
| GS | Shared GS registers/local memory and GPU bridge track texture cache, CPU/GPU ownership, dirty/stale pages and render targets; Vulkan renders/presents | SOURCE; legitimate CPU reconciliation/readback and software picture fallback remain |
| PAL adaptations | Relocation discovery, worker completion, CardOr/SkipMovie hooks, camera/HUD identities, regional-presentation opt-in and player defaults | SOURCE + bounded AUTO/MANUAL; no global USA-to-PAL offset assumption |
| PC options | Shared host F9 overlay and settings; PAL Windows entry supplies own ISO/profile paths and defaults | SOURCE + AUTO + MANUAL; original native main/pause menus have no new PC Settings/Quit entries |

Use the original PAL executable as the authority for game logic. A USA routine
with matching semantics can guide a PAL adaptation; matching symbols, addresses,
name hashes or visible pixels alone are insufficient. PAL50/60, language, assets
and legitimate rendering differences prevent treating USA pixel identity as the
definition of PAL correctness.

## 2. Recompilation, maps and memory limits

**SOURCE:** `game.json` identifies `SLES_525.84`, ELF entry `0x00100008`, cleaned
analysis database `build/sles_525_84_clean.json`. Local original database had
9,388 entries; cleaning excludes `0x0046A280`, `0x0046A2A0`, `0x0046A480` and
`0x00481818`, false function candidates in VU data, leaving 9,384 analyzed entries.
The cleaned map is not a license to rewrite extracted inputs.

**AUTO, reviewed preserved bulk summary:** 37 batches completed in
`build/bulk/run-6785cb70ebf8/summary.json`; states are 477 preserved_verified +
5,983 verified, 24 preserved_unverified + 2,899 unverified, and one rejected.
Thus 6,460 lifter-classified verified / 2,923 unverified, not 9,383 independently
proved C++/MIPS functions. There are 9,383 preserved EE translations. Errors include
instruction limits, insufficient cases, memory reads/writes, timeouts and unsupported
instructions; a usable game does not erase those validation gaps.

| Layout item | Established value | Consequence |
| --- | --- | --- |
| EE image limit | `0x02000000` | Honor real mappings/contracts |
| Synthetic scratch | `0x01ED0000`, size `0x20000` | `[0x01ED0000,0x01EF0000)` is only an isolated test reservation |
| Synthetic stack | top `0x01F00000`, size `0x10000` | Not a general allocator for arbitrary runtime hooks |
| VU DMA/VIF source range | `[0x0046A280,0x00484320)`; 106,656 bytes | Historical 28 DMA packets, 67 MPG commands, 13,226 decoded pairs; not ordinary EE functions |

**OBSERVED:** full startup offers the scratch range to the game heap. Never use
it as spare full-game memory. Final interpreter checks disable automatic fault
mapping. Compare MIPS, symbolic expression and native DLL separately; arguments
are u64 images while the historical return harness is s32. Full registers/HI/LO,
64-bit returns and arbitrary pointer provenance are not thereby verified.

### Existing automation, not a new framework

- `generate_batches.py`: statically admits functions/cases from current maps and
  hashed evidence, fixed seed/limit, exclusive generated manifests. No embedded
  game implementation or expected-return oracle. Listing/generation is not execution.
- `run_pal_pipeline.py`: explicit batches or repeated manifest paths; deterministic
  order, Windows-safe identities, `--validate-only` or explicit `--execute`, atomic
  checkpoints and individual/aggregate status. Never implicitly runs all batches.
- `pal_auto.py`: persistent sequential plan, immutable snapshots, run/resume and
  semantic integrity checks. Only infrastructure/interrupted attempts can be retried
  once with `--resume --retry-infrastructure` and confirmed cleanup; equivalence,
  coverage, rejection, compile errors and timeouts are not silently retried.
- `run_batch.py`: independent per-function workers and private Windows Jobs;
  completion/timeout/cleanup are separate. An independent compiler deadline is
  not proved merely by a containing worker deadline.
- `autotest.py`: manifest/hash-based QUICK/FULL adapters, not arbitrary newest-EXE
  selection or auto-repair. An old adapter may reject the newer release manifest;
  do not bypass that rejection or claim compatibility from the tool name.

The old batch README's claim that execution/jobs were unavailable was stale;
the current code and retained tests implement them. See [validation](VALIDATION.md).

## 3. Boot and dispatch contracts that must survive

Detailed boot chronology is summarized in [BOOT_FLOW.md](BOOT_FLOW.md). Important
SOURCE/AUTO/OBSERVED findings from the accepted adaptation record:

- Copied code `0x004854E0 -> 0x80076000`; second block
  `0x004846A8..0x004849D8 -> 0x80075000..0x80075330`, 816 bytes. Its resolver
  entry ends at `0x80075038`; selector 3 returns data, not callable privileged code.
  Keep original discovery mechanisms; do not invent TLB instruction emulation.
- PAL memory-card worker `0x00242A50` had a map ending at `0x00242AC8` plus a later
  chunk `0x00242D40..0x00242D50`. `jr` at `0x00242AC0` with delay-slot NOP at
  `0x00242AC4` legitimately reached `0x00242AC8`. Complete the **same function**
  through `0x00242D50`; this recovered dispatch, not a new locale function.
- `relocate_native.py --complete-function 00242A50:00242D50` preserves published
  sources and rejects overlap/incorrect final boundary. The original worker,
  queue/polling and PAL command table `0x004BB880` remain.
- CardOr uses PAL GetInfo `0x00244208`, Format `0x002445C0`, Read `0x00246578`,
  Write `0x00246778`, Create `0x00245FB0`, GetDir `0x002470C8`, Lookup `0x00245388`.
  With a card it delegates to host mc2; without one it invokes the original PAL
  routine. Actual profile save/reload was subsequently accepted manually.
- PAL SkipMovie `0x00131410` requests stop at object `+0x4A` on Start edge, retains
  original update, and does not fake completion at `+0x49`.
- `0x1000F000` is INTC_STAT, not D_STAT. PAL `0x00102E08` was not proved to be
  sceGsSyncPath; forcing bits or naming it from a USA address was rejected.
- Relocation-audit registration includes required compatibility behavior. Keep
  `PAL_RELOCATION_AUDIT` and its generated registrations when their manifest
  requires them; stripping all such code as logging would remove working hooks.

Historical debug language state: effective Spanish 5 at `0x004E2E98`; list cursor
`0x01E89EB0` selected index 2 in that particular language screen. These observations
are **not** a stable preference API or safe values to poke into every boot phase.
The original 60 Hz test screen requires explicit Yes; its default No returns to
50 Hz. Automatic Spanish/60 Hz selection and skipping remain PLAN.

### PAL-070: established language contracts from the last read-only review

**SOURCE, reused rather than re-audited:** the PAL initializer is `0x00437740`
and selection processing is `0x004374C0`. Existing translations are in
`bulk_033_000.cpp` under the preserved lifted source directory. Original ELF
resource pointers select `Data/GlobalEn.bin`/`HeadEn.bin`, Du, Sp and Sw variants:
English, Dutch, Spanish and Swedish. This establishes the reviewed edition's
resource branches, not manual certification of all four languages.

The original entry `0x0012EB30` recognizes a guest argument beginning `language=`.
Guest `language=3` (initializer argument value **3**) selects Spanish resources
and internal language ID **5**. These are different number spaces; `language=5` must not be described
as Spanish. The argument's host transport and complete startup effects have not
been dynamically validated. It is not a confirmed PC EXE CLI option.

Keep the original asynchronous resource loading and selector/confirmation
contracts. Direct initialization through `language=` can differ from showing
the selector; do not force it when the user requests the original sequence
without establishing that behavior. Debug cursor/RAM values are not a replacement.

**PLAN:** milestone 1 is a persistent preferred language through shared PC
settings/CLI precedence, Spanish preferred, with original selection behavior
available and no unrelated-save dependency. Resolve host-to-guest routing,
selector initialization/lifetime and requested/effective state first. Video
preferences and optional startup skipping are later milestones. No preference
backend or new F9 controls are implemented by these findings.

## 4. GS/Vulkan and PAL-010

**SOURCE:** bridge `PresentGpuPicture` first identifies the GS framebuffer and
compatible target, validates the visible region/coherency and obtains the actual
Vulkan resource. If unsafe it retains `gs::Picture` CPU-picture presentation.
Global ownership/dirty/stale contracts are unchanged. Pacing only chooses due-time
presentation; it is not a prerequisite for regional validity.

Certified regional helper in `host/src/pal_regional_presentation.h`:

| Guard | Exact current source scope |
| --- | --- |
| Key | FBP 0, FBW 10, PSM `0x0A` (PSMCT16S) |
| Geometry | Logical target 640x1024, shown 448, DBX/DBY 0, scale 2/3/4 |
| GS video state | PMODE `0x66`, SMODE2 1, circuit 1 fields zero, DISPFB2 `0x51400`, DISPLAY2 `0x1bf9ff0183227c` |
| Visible footprint | Exactly pages 0..69, 70 x 8 KiB = 560 KiB, blocks `[0,2240)` |
| Physical resource | Extent exactly `640*scale` by `1024*scale`, initialized draw/reload state |
| Conflicts | No visible stale page, CPU-newer block, pending patch, conflicting producer, depth or unknown ownership reason |
| Startup selector | Exact selector GS state uses DISPLAY2 `0x001ff9ff01848290`; preserve full 640x512 picture fallback |

Do not generalize these guards to other layouts, PAL50, formats or games without
new evidence. Other stale pages can remain outside the valid visible region;
indiscriminately accepting a stale target is forbidden. Scale/resource changes
must still fail safely. No artificial resolution-dependent selector offsets.

**AUTO + MANUAL:** synthetic predicate/scale/fallback checks and a short replay
passed; manual live acceptance covered 2x pacing ON/OFF, 3x/4x ON. GS Replay does
not execute the live race's PresentGpuPicture path. Historical replay automation
uses existing capture inputs, isolated processes, frame/memory comparison and
framediff/gsmem/timeline, not USA CRCs as automatic PAL oracles.

**SOURCE + OBSERVED:** CSM1 CLUT GPU loads and texture decode are active. CSM2
still has a CPU path; TEXCLUT/CBP/CBW/COU/COV/CPSM/CSA/TEXA coverage and actual PAL
frequency are not established. Read-category counters, fence waits, downloads and
fallback counts are different metrics. The earlier 560 KiB presentation fallback
attribution does not prove every download is redundant or caused by CSM2.

## 5. USA-001 / USA-002 and performance interpretation

**USA-001 SOURCE:** cache entries already had page indexes, but invalid entries
remained there. Invalidated textures now leave every indexed page and are
reinserted after real revalidation/decode. CPU writes, GPU overlap, replacement
generation/loss and eviction use central transitions. Stable cached pointers and
swap-removal avoid duplicate entries, shared temporary storage and compensating
global scans. Cache access retains existing GS stream serialization.

**USA-002 SOURCE:** only opted-in PSMCT16 groups of 16 with certified table/column
geometry use masked/interleaved SIMD stores. Prefixes/tails, insufficient groups,
page/wrap boundaries and rejected layouts stay scalar. PSMCT16S/PSMZ16/PSMZ16S
do not become SIMD just because they are 16-bit. Input retains the existing u32
alignment contract; SIMD-alignment offsets were tested. ISA compilation guards
retain the scalar path where not advertised; the prepared full player remains
AVX2, and no complete non-AVX2 release was validated.

**AUTO:** USA-001 2,000 differential operations and 4,096-invalid-alias history;
USA-002 1,504 cases per SIMD/scalar executable, including 512 fixed-seed random
cases and protected input/GS boundaries, comparing the complete 4 MiB GS memory.
Replays preserved rendered BMPs and final GS memory. These are focused correctness
results, not frame-presentation or whole-game speedup measurements.

The final read-only three-version report retained five distinct sessions. In
comparable 4,800–10,800 VBLANK windows, presentation rate was about 59.9/s in all
three versions. CPU/GS workload varied by scene; normalized costs were suggestive
but did not isolate either optimization. USA-001 versus USA-002 had 8/90 versus
2/90 **windows whose maximum exceeded 20 ms** over VBLANK 3,600–14,400. That is
not a count of individual slow frames or causal proof. The same PAL-010 EXE varied
from 12/50 such windows to 0/50 between sessions. Percentiles were of window means,
not per-frame samples. Eligible GS16 synthetic medians were 6.591 ms scalar versus
1.856 ms selected; actual live SIMD frequency was not logged. Both changes were
accepted for exactness/structural efficiency, with global PAL gain unquantified.

## 6. Widescreen, settings and the F9 failure investigation

**SOURCE/AUTO:** PAL camera `0x001E2100` adapts the USA setter contract, calls its
preserved original directly and restores the caller's input. Only x is widened by
effective aspect/(4/3); y stays unchanged. No accumulation or constant forced FOV.
Camera x/y and inverse fields are `+0x68/+0x6C/+0x70/+0x74` for this established
contract. PAL Flush2D `0x0021A4E0`, Sprites2D `0x00364B00`, sixteen packet builders
and regional anchor identities support the HUD. Not all anchors have whole-body
dynamic equivalence. Fourteen camera and 27 HUD cases plus manual acceptance apply.

**SOURCE:** settings precedence is defaults < INI < legacy GPU-thread environment
< explicit `--setting`. Runtime overrides are separate from saved preferences.
F9 shows locks and GPU Thread's effective/requested startup state; it never hot
toggles renderer threading. Same-directory temporary/replacement writes roll back
on failure, retain unrelated keys and avoid rewriting just by opening the panel.
Concurrent writers to one profile are not covered.

**SOURCE/AUTO/MANUAL:** AA is a real live OFF/FXAA/SMAA selection via the window
API and deferred display state, with prepared immutable pipelines and safe
resource reuse. Postprocessing includes the game HUD. SSAA is not exposed. The
overlay mutex is not held across expensive driver work; necessary GPU lifetime
waits remain. Preserve both this handoff and warm-up.

**OBSERVED:** early logs had ~1.98–2.61 s intervals without interaction markers;
attributing each to AA was unsupported. First-use pipeline probes measured only
~40/51 ms. User reproduction then isolated controller-only stalls: L3+R3 reached
both the menu and full GS capture handlers. Recorded capture gaps were 7,560.300
and 6,714.970 ms; keyboard F9 was ~0.18–0.74 ms with normal queue levels. F12
capture remains deliberately expensive. The restart-only AA alternative was not
adopted; do not merge it back from its preserved experimental worktree.

### Native menu findings and PAL-090–095 requirements

**SOURCE, reused from the prior inspection:** USA
`games/burnout3/port/overrides.cpp` replaces the main menu's ONLINE label/action
with PC Settings and adds Quit as entry six. PAL's original constructor
`0x003183E0` establishes five entries; original input is `0x00318240`.
The PAL count of five was also checked against the original ELF. These are
partial static contracts, not validated PAL hooks or pause-menu mappings.

**PLAN:** preserve all five original entries, explicitly ONLINE and Multiplayer,
then append PC Settings and Quit Game for **seven**. Entries use 8-byte records;
the selected-index/count fields are at menu `+0x14/+0x16`. The original five
inline entries start at `+0x18`, followed by help pointers at `+0x40`. Do not
grow the inline array into those fields. New external tables need at least
**56 entry bytes + 28 help-pointer bytes**, plus alignment and localized text.
Preserve 8-byte/4-byte table alignment and validate all seven indices/callbacks.

USA's implementation keeps six visible rows and scrolls larger settings pages;
seven PAL main-menu rows are not visually validated. Preserve appearance,
navigation, transitions, sounds and original callbacks. Intercept the added
actions safely instead of feeding unsupported indices into the original handler.

**Do not reuse USA ONLINE donor storage.** Its GuestPool borrows original online
strings for guest-visible arrays/text. PAL must retain those strings and behavior.
Establish safe guest-owned capacity and lifetime across menu rebuilds and language
resource unload/reload. The synthetic scratch region is not a general allocation.
USA's GuestText widens individual bytes into UTF-16 units; that is not a correct
UTF-8 conversion for multilingual new text. Every added label/help/settings text
and confirmation must use the selected original PAL language, correct UTF-16,
adequate capacity and supported glyphs. Existing original strings remain intact.

Keep native PC Settings separate from the accepted F9 overlay while sharing the
actual settings/CLI/restart contracts. Decide later whether F9 also opens the
native screen; no replacement or redesign is implemented now. Retain a separate
pause-menu **Quit to Desktop**, with localized confirmation, cancel behavior and
save/IO-safe shutdown. USA's main-menu GuestExit example does not prove that
quitting during a PAL save or paused race is safe. Native PAL additions remain
unimplemented; callback, allocation/lifetime and live layout tests are pending.

## 7. Audio recovery contract

**SOURCE/OBSERVED:** capture readback/file work and modal window dragging can
block host/EE while WASAPI continues consuming. SPU2 catches up by clock (bounded
to one second); the old ring favored older samples and filled to ~650–680 ms.
The old +/-0.5% regulator could not quickly remove that latency. This was not
evidence that WASAPI itself caused the interruption.

Accepted recovery: production gap >=250 ms enables a two-second window. Only
the consumer, at callback entry, discards obsolete excess above target plus the
larger of 20 ms/two callbacks. It retains the latest target span (default 60 ms),
resets interpolation phase and crossfades for 96 frames/2 ms at 48 kHz. It does
not flush WASAPI, alter producer indices/mixer/game clocks or trim normal high
queues without a qualifying gap. Keep expiry, tolerance and normal-output guards.

**AUTO:** 27 synthetic gap/target/wrap combinations; 3,000 normal operations were
byte-identical to the prior output. Real WASAPI with a synthetic producer after
an 800 ms pause recovered to ~30.021 ms at 30 ms and ~42.104 ms later. The device
probe was muted, so it was not audible game acceptance. Subsequent **MANUAL**
capture/window tests on EXE `12772164...` accepted recovery. Do not reopen that
closed scope or claim every possible pause/device is solved.

## 8. Texture modding and unproved future assets

**SOURCE:** the engine has PNG/DDS replacement loading and conditional F9/Tab
controls, not a complete original-PAL extraction/catalog/import pipeline. Native
keys use XXH3-64 of GS texture data and optional CLUT/region/format/TEXA metadata.
The loader normalizes TCC `0x4000`; common syntax is texture[-clut][-rWxH]-bits.
Equal prefixes are candidates, not verified replacement identity.

**OBSERVED/MANUAL:** earlier 21-file trial had 40,304 misses and zero applications.
HD333 had 333 indexed/167 loaded DDS and real substitutions, but the maintainer
reported worse PAL in-race quality/performance. Its sampled BC3/DXT5 images had
one mip level; this alone does not explain a thread-dependent slowdown. Separate
executable-relative discovery/catalog patches were not generally integrated.
The audited full-key/image study found 333 direct matches plus 168 candidates;
501 keys of interest do not equal 501 usable HD assets. Preserve uncertainty and
regional language/livery content.

**UNCONFIRMED/PLAN:** original soundtrack containers/metadata/indexing/size limits
are not established here. Spotify requires future official API/terms checks. BGV
vehicle compatibility, including the reported Legends PSP version 0x17, has not
been validated against binary inputs. No vehicle importer or native menu extension
is claimed. See the roadmap's independent staged investigations.

## 9. Durable evidence entry points

- [Boot contracts](BOOT_FLOW.md), [validation/build commands](VALIDATION.md),
  [regional differences](PAL_DIFFERENCES.md), [status](STATUS.md).
- [Accepted adaptation chronology](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/games/burnout3-pal/port/ADAPTATIONS.md).
- [Camera evidence](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/docs/pal/WIDESCREEN_3D.md)
  and [HUD contracts](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/docs/pal/WIDESCREEN_HUD.md).
- [HD findings](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/docs/pal/HD_TEXTURE_PACK.md)
  and [texture identity study](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/docs/pal/TEXTURE_COMPARISON.md).

The old host crash `0xC0000409` and its dump remain historical evidence with no
proved cause; a later clean-TEMP success did not diagnose it. Conversation details
lost to compaction and not recoverable from these records are **unavailable**, not
facts to reconstruct from intuition. No new empirical claims are made by this
documentation-only consolidation.
