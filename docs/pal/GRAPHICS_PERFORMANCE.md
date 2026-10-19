# Graphics and performance: evidence and limits

Research snapshot: 19 October 2026. This page summarizes existing audits; it
does not add engine patches, new settings, profiling tools or optimizations.
Private session logs and captures stay outside Git. Evidence below distinguishes
static code, synthetic tests, maintainer observations and unresolved hypotheses.

## Build identities and scope

- Public v0.1.0-alpha: native player, ISO/profile persistence and manual races
  with GPU Thread On accepted; reproducible GPU instrumentation remains compiled
  into that binary even with trace logging absent. The earlier freeze is unexplained.
- Current widescreen source: clean engine d7e6e023, camera and HUD accepted
  separately with GPU Thread Off and HD Off. No experimental AA/HD/trace patch
  is required by those hooks; this does not validate Thread On in the clean build.
- Private graphics/HD and A/B builds: same pinned engine plus preserved local
  patches. Their tests and measurements do not automatically apply to the clean
  build or change the public alpha ZIP.

## Antialiasing and presentation

| Mode | Pinned engine implementation | PAL evidence and availability |
| --- | --- | --- |
| Off | No AA compute; final linear blit remains | Reference mode, manually tested |
| FXAA | fxaa.comp / RecordPost, one compute pass, source texel size | Backend present; private F9 selector and persistence tested, not integrated here |
| SMAA | SMAA 1x HIGH: luma edges, AreaTex/SearchTex weights, neighborhood blend | Three passes/resources present; private selector tested at 2x/4x, not integrated here |
| SSAA | SupersamplingFactor / WantedScale and downsample.comp resolve | Backend exists; PAL SSAA behavior not validated or newly exposed |
| Internal resolution | GS targets scaled where eligible | 2x/4x used; not all memory/postprocess views scale |

SMAA is not temporal T2x/4x. Synthetic Off/FXAA/SMAA/Off tests recorded 0/1/3/0
AA dispatches and persistence in the private build; they did not compare final
gameplay pixels. F12 GS captures are taken before presentation AA, so equal F12 images cannot
show that FXAA/SMAA is ineffective. Capture the final window with F9 closed;
exclude capture-induced readbacks from timing measurements. If postprocess
resource creation fails, the engine can retain an unfiltered source while the
setting still names a filter; that failure was not observed in the retained tests.
No PAL-specific AA shader defect was demonstrated. At 4x,
already smaller edges, final scaling and game blur/glow may make changes subtle;
that explanation remains a hypothesis, not a quality benchmark.

The inspected path is GS rendering and game posteffects -> direct display target
or CPU Picture fallback -> frame-pacing image -> selected AA/SSAA resolve ->
linear blit into the aspect rectangle -> host overlay -> Vulkan presentation.
AA uses the source image dimensions, not the window's dimensions. The game HUD
is part of the filtered image; the host F9 overlay is later. SSAA is an exclusive
AA mode; internal resolution can coexist with FXAA/SMAA, but they are not stacked
on the SSAA resolve. SSAA's additional scale can substantially increase memory
and fill cost. Do not expose it as validated PAL functionality yet.

## Engine revisions examined, not an update proposal

USA 0.4.0 used 645bbf394abf449414c39d975578b6e090eee4ac; PAL pins
d7e6e023f261d9f95ad097fcd063ec95e40ca465. The historical audit observed upstream
d5e4fd46c04f0513e398bb24ef21b04c0b092aab. This is a dated observation,
not a claim about today's latest master. No revision is changed here.

Commit 676388874920c93c35df886382f94a6669d1d62d changes ScaleOf so views at least
1024 pixels wide also remain at 1x. Its documented glow/splitscreen result is
upstream-specific, not a PAL measurement. It is absent from the pinned engine;
confirm relevant PAL view dimensions before any isolated trial. The audited
post-d7 changes do not provide a BC3 mip-generation, AA shader or HasReplacement
fix. Presentation timing changes span APIs and game opt-ins; copying one file
would not reproduce them safely.

## GPU counters: what the labels actually measure

The completed counter pair used the same instrumented widescreen executable,
HD Off, 2x, 16:9, AA Off, original blur, pacing On and vsync Off. Initial/final
configuration agreed; transient changes were not continuously monitored.
Car/track/camera, selected PAL frequency, warmup and exact race windows were not
certified by the records. GPU_LOG produced about 17/19 MiB of text; these are
attribution sessions, not low-impact performance benchmarks.

| Whole-session counter | Thread Off | Thread On |
| --- | ---: | ---: |
| Host duration, seconds | 85.8 | 93.2 |
| Fences counted under memory-read-back reason | 5,264 | 6,276 |
| Accumulated wait under that reason, seconds | 13.9 | 16.5 |
| GPU-to-CPU transfer, reported KB | 1,474,080 | 1,757,440 |
| GPU CLUT loads | 18,572 | 24,408 |
| GPU texture decodes | 52,810 | 63,400 |

**Confirmed by records:** all downloaded bytes were classified `read`;
`texture` and `write` remained zero in the snapshots. The bridge's KB are KiB
(1024 bytes). GPU CLUT and GPU decode are active; this is not evidence that PAL
lost the CLUT optimization.

**Confirmed by code:** the memory-read-back counter counts Retire/CountWait
fences under a reason, including earlier in-flight work, not MemoryRead calls
or necessarily actual stalls. The aggregate identity KB = 280 * count + 160
holds, while snapshot increments are mainly 560 KiB, with two 640 KiB increments
per session. It does not establish a 280 KiB payload per readback.

**Correlation:** the final reload_draw counts for color view C 0/10/0A
(PSMCT16S, FBW 10) are 2,630 / 3,136, equal to the number of 560 KiB increments.
Those sizes are compatible with 640x448x16-bit data, not proof of the downloaded
range or consumer. Snapshots do not connect a target to each MemoryRead call.

**Main hypothesis:** PresentGpuPicture often bypasses a direct target when
ownership/stale checks fail, then Gs::Picture requests CPU coherence. This can
explain a cost shared by Off/On. The exact target flags, read ranges and callsites
remain unobserved; neither a redundant synchronization nor a safe patch is proven.
TargetToMemory, DepthToMemory and WriteBack can move/swizzle data inside GPU
memory; they must not be counted as downloads to the CPU.

## Worker queues and limits of the A/B result

The On log covers 91 complete windows of 60 kicks. Reported drains: MemoryRead
3,131; DisplayTarget 1,324; SetScale 1; BlockCompression 1. HasReplacement,
HasTarget, UploadReplacement and read-texture/depth/target reasons are zero in
those complete windows; the final partial window is not reported. In particular,
HD Off did not show repeated HasReplacement drains in this pair.

Drain is a call, not a measured stall. Neither its duration nor overlap with
useful work is recorded. Wait percentages from different threads overlap and
must not be added. A separate six-long-session HD-Off study was consistent with
a small presentation/regularity benefit from Thread On, as perceived manually,
but lacked paired race windows. One short On session had zero VU1 pairs and was
excluded. No causal FPS gain, frametime P95/P99 or unique-image rate is established
from 120-frame aggregates. Presentations and VBLANK are not new game images.

## Next steps, conditional and separate

1. Use the existing bounded presentation trace in an isolated diagnostic build
   to record target ownership/stale state, fallback decisions and actual read
   ranges. Keep verbose GPU_LOG off for this attribution pass. The trace patch
   remains available for diagnostics but is not applied to the clean widescreen engine.
2. If the visible region alone is coherent, investigate region-bounded validation;
   if it was invalidated, investigate aliasing/ownership instead. Require evidence
   and focused overlap tests before changing any guard, fence or Drain.
3. Use a short timeline to measure wait duration/overlap, and low-impact frame
   timestamps for controlled 30-second race windows. Separate cache warmup from
   sustained work and record pipeline creation. Repeat Off/On three times with
   one EXE, 2x, AA/HD Off, original blur, pacing On, vsync Off and AC power.
4. Only after the HD-Off reference, isolate HD/UI/non-UI subsets and Thread mode.
   Missing BC3 mip chains alone do not explain a Thread-On-only regression.

VU Fast remains off; compiled VU coverage does not imply fast mode or semantic
identity. Do not bundle compiler changes, pipeline hints, Native Blocks, SSAA,
HUD/FOV changes or a full engine update into this diagnosis. Preserve the now
accepted camera/HUD and the public alpha as separate references.
