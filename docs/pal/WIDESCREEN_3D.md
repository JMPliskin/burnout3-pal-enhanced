# Experimental PAL camera widescreen

This camera-only adaptation reuses the USA v0.4.0 `SetViewWindow` contract from
`games/burnout3/port/widescreen.cpp`. It does not adapt the HUD, frontend sprites,
2D batches or regional anchor identities, and does not change the shared engine.

The PAL target is **0x001E2100**, not the USA address 0x001E2110. Its 100 MIPS
bytes differ from the USA setter only in the frame-notification call target
(PAL 0x001E3FC0). Both take a0=RwCamera and a1=view-window vec2, store x/y at
camera+0x68/+0x6C and their inverses at +0x70/+0x74, and return the camera.
PAL FOV setter 0x00211CB0 supplies that window at wrapper+0x48. These are static
contract checks, not a new general claim of dynamic MIPS equivalence.

`RegisterGameOverrides` registers the hook before gameplay. The hook calls the
preserved `F_001E2100(c)` symbol directly, avoiding recursion through dispatch.
CMake includes the new PAL source, so future builds retain the adaptation.
Generated EE sources and the generated dispatch table remain unedited.

The aspect setting already used by F9 follows this shared chain:
`StepPcSetting` -> `SetWindowAspect` -> `aspect` in pc_settings.ini ->
`PictureAspect`. The hook reads PictureAspect on each call. For an original
positive 4:3 camera window, it changes only x by `PictureAspect / (4/3)`, calls
the original setter, then restores the caller's x bits. It leaves y unchanged.
In 16:9 this is Hor+: more at the sides, with the same vertical view. It does
not force a constant FOV or modify the game's speed-dependent camera logic.

As USA, 4:3 and windows whose y/x is not approximately 0.75 pass through.
Additional finite-value guards preserve malformed windows instead of widening
NaNs/infinities; an overflowed result is also passed through. A scope guard
restores x if the original call throws. Neither change affects the normal USA
widening formula. No external cheat addresses or hex patches are used.

## Specific regression evidence

`port/tests/widescreen_camera_tests.cpp` links the real, preserved PAL EE
objects, host, dispatcher, overrides and camera hook. It does not start the
game, read an ISO or reconstruct a substitute camera function. Only the frame
notification callback is a synthetic spy; its full RenderWare work is not
tested. The standalone test replaces the Windows player main when linking.
The optional PAL_WIDESCREEN_TESTS CMake targets build these checks with the
same new-build EE/VU objects and host libraries; see WIDESCREEN_HUD.md for
commands. Private build reports record exact inputs and hashes.

Fourteen cases passed:

- 4:3 compares all Context bytes and selected camera/stack memory with a direct
  call to the preserved original, plus input-vector preservation.
- Three positive 4:3 windows widen horizontally in 16:9; the actual original
  installs x/y and both inverses, preserves its ABI, and notifies once.
- Repeated calls with the same input do not accumulate the horizontal factor.
- Already panoramic, square, negative/zero and nonfinite windows pass through
  like the original; changing back to 4:3 restores the original projection.
- An original-callback exception still restores the caller's input vector.
- Reloaded aspect settings reach PictureAspect; unrelated preferences survive.

The first test attempt incorrectly demanded the literal float `4/3`. USA uses
two float operations `(16/9)/(4/3)`, whose result differs by one ULP. The test
expectation was corrected and the failure log retained; the hook was unchanged.
This does not establish visual correctness in all PAL cameras or scenes.

## Manual validation

Use the private camera-test launcher and EXE, not the public Release. Initial
configuration: **4:3, 2x, AA Off, GPU Thread Off, HD Off, Frame Pacing On,
V-Sync Off and original blur**. Its empty texture directory prevents accidental
pack discovery. Data and a copy of a previous private test card are isolated;
personal cards and previous executables are untouched. The launcher records
thread mode and initial/final INI per run, without automatic inputs or captures.

At the same stationary car/camera/location, compare 4:3 -> 16:9 -> 4:3 through
F9. Check additional horizontal coverage, unchanged vertical coverage and
object proportions; repeat to test accumulation. Then check other cameras,
car selection and non-gameplay scenes. Keep resolution/AA/HD/thread fixed.

In the original camera-only phase HUD, text and 2D sprites remained outside
scope. Their subsequent adaptation is documented in WIDESCREEN_HUD.md. No claim is made to repair HD
filtering/mipmaps/alpha, GPU Thread performance or postprocess synchronization.
No automatic game run or manual visual acceptance is claimed by these tests.

## Maintainer visual acceptance and next phase

The maintainer subsequently confirmed correct 4:3 side bars, increased horizontal
coverage in 16:9, approximately preserved car proportions and equivalent Fill
framing in the tested window. The camera hook is unchanged in the subsequent
2D experiment; see WIDESCREEN_HUD.md. This does not validate all special scenes.

The subsequent clean-engine integration also passed separate maintainer visual
acceptance in 4:3, 16:9 and Fill, with the adopted HUD. Its 14 camera and 27 HUD
regression cases passed again without rebuilding. GPU Thread and HD were Off;
no shared-engine patch is required by the widescreen hooks themselves.
