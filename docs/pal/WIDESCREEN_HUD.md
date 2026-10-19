# PAL widescreen HUD and 2D experiment

The maintainer visually accepted the separate PAL camera hook at 001E2100:
4:3 has side bars, 16:9 gains lateral scene coverage with preserved car shape,
and Fill the window has equivalent framing in the tested window. That hook is
unchanged in this phase. This is not validation of every camera or cinematic.

## Reused USA implementation

`widescreen_hud.cpp` adapts Daniel Gomes Vieira's MIT-licensed v0.4.0
`widescreen.cpp`: sixteen packet builders, Flush2D, Sprites2D, per-element
anchors and screen-cell identities. The HUD editor, race pause hook, mouse
integration and F9 changes are not imported. The existing camera stays separate.
No renderer, GPU thread, texture, shader, AA or viewport modification is made.

Flush2D 0021A4E0 and Sprites2D 00364B00 match their USA bodies in full except
one mapped direct call each. All builder instructions retain registers, branches,
delay slots and stack layout; differences are constant-pool addresses and calls.
The two 464-byte builders have indistinguishable signatures but identical public
contracts; both entries are wrapped. GP-relative batch start/end offsets remain
-6D20/-5D98. Sprites use the original 32-byte vertex stride and float x/y fields.

Anchor IDs name callers, not new hooks. Most have a full relocated instruction
match. Four regional routines (USA 1A6290, 352AB0, 3B2EE0, 3D6C30) have unique
entry signatures, aligned shared code and the same ordered mapped 2D calls,
but differing scheduling/register allocation. Their identity is supported
structurally; no claim of whole-function dynamic equivalence is made. Private
paired disassemblies and the confidence table preserve that distinction.

| USA | PAL | Role |
|---|---|---|
| `001A0790` | `001A0780` | HUD anchor identity / context |
| `001A5740` | `001A5730` | HUD anchor identity / context |
| `001A6290` | `001A6280` | HUD anchor identity / context |
| `001D5FB0` | `001D5FA0` | HUD anchor identity / context |
| `00352AB0` | `00352C80` | HUD anchor identity / context |
| `00381030` | `00381380` | HUD anchor identity / context |
| `00381AB0` | `00381E00` | HUD anchor identity / context |
| `003CD920` | `003CDC80` | HUD anchor identity / context |
| `003D6C30` | `003D6F90` | HUD anchor identity / context |
| `003D7B40` | `003D7EA0` | HUD anchor identity / context |
| `001D75C0` | `001D75B0` | HUD anchor identity / context |
| `001A7B00` | `001A7AF0` | HUD anchor identity / context |
| `001A8180` | `001A8170` | HUD anchor identity / context |
| `0030D6E0` | `0030D740` | HUD anchor identity / context |
| `0031B130` | `0031B1D0` | HUD anchor identity / context |
| `003B2EE0` | `003B3240` | HUD anchor identity / context |
| `0021A4F0` | `0021A4E0` | 2D flush |
| `00364920` | `00364B00` | Frontend / world-label sprites |
| `002178E0` | `002178D0` | 2D packet builder |
| `00217A20` | `00217A10` | 2D packet builder |
| `00217C40` | `00217C30` | 2D packet builder |
| `00217D40` | `00217D30` | 2D packet builder |
| `00217E70` | `00217E60` | 2D packet builder |
| `00217FC0` | `00217FB0` | 2D packet builder |
| `00218400` | `002183F0` | 2D packet builder |
| `002187B0` | `002187A0` | 2D packet builder |
| `00218D00` | `00218CF0` | 2D packet builder |
| `00219150` | `00219140` | 2D packet builder |
| `00219530` | `00219520` | 2D packet builder |
| `00219830` | `00219820` | 2D packet builder |
| `00219A20` | `00219A10` | 2D packet builder |
| `00219CA0` | `00219C90` | 2D packet builder |
| `00219D50` | `00219D40` | 2D packet builder |
| `00219F20` | `00219F10` | 2D packet builder |

## Coordinates and transformations

The native 2D space stays 640 by 448. GS XYZ coordinates are fixed-point 1/16
pixels; FRAME and XYOFFSET determine whether vertices target the picture and
its centre. Window pixels are not written by these hooks. The horizontal factor
is min(1, (4/3)/PictureAspect), preserving y, z, UV, colour and font atlas data.
Left/right/centre anchors place individual elements; stretch exceptions preserve
intentional full-width panels. Full-screen background/fade edges stay at the
edges, offscreen render targets stay unchanged, and narrower scissor rectangles
follow the content. This does not apply a uniform correction to the entire image.

Speed/units are right-anchored; the boost bar and left panels left-anchored;
position/lap/counters and common panel overlays use the USA cell-specific rules.
Other elements retain USA's centre default. No minimap-specific rule is invented.
The semantic identity of all takedown popups, replays, special modes and language
layouts must still be checked visually. They are not individually validated by
the static drawing-path map or the synthetic tests.

Sprite x coordinates are temporary and restored after the original draw; batches
are adjusted once before their original flush and tracking is cleared afterward.
Builders interrupted by a flush restart tracking at the new buffer cursor.
4:3 has factor 1 and does not alter submitted coordinates. Fill the window uses
the existing shared PictureAspect/actual window-aspect path; no new viewport
logic or second camera correction is introduced.

## Regression tests and limits

`widescreen_hud_tests.cpp` executes the adopted implementation with synthetic GS
packets and draw/flush spies. Twenty-seven cases passed: all four anchor modes,
PAL speed/boost/panel identities, 4:3/16:9 changes, arbitrary wide aspect,
PACKED/REGLIST/A+D XYZ paths, y/z preservation, full-screen quads, non-picture
targets, scissors, exact 4:3 packet preservation, repeated sprite restoration,
empty sprites, builder tracking and mid-builder flushes. The no-window auto
aspect fallback is tested; the actual Fill window path requires manual testing.
These tests do not execute the real game draw/flush sinks or establish MIPS
 equivalence of those functions. All fourteen existing real-camera cases passed
again using the new registration and the unchanged camera object.

## Manual screenshot protocol

Use the independent private experimental launcher/card, HD Off, GPU Thread Off,
2x, AA Off, Frame Pacing On, V-Sync Off and original blur. The public executable
and personal cards remain separate. No automatic game run/captures are enabled.

1. Park the same car at the same point, with the same camera. Open F9 to select
   4:3, close it, then capture. Repeat for 16:9 and Fill the window, closing F9
   each time. Keep the window size and all other settings constant.
2. Compare speed digits/units, boost bar, position/lap/time, takedown counter,
   subtitle/popup text and panel edges: shapes, anchors, clipping and overlap.
   Verify increased 3D lateral coverage and unchanged vertical framing.
3. Return to 4:3 and repeat twice. Compare with the previous camera-only EXE in
   4:3 at the same point. Avoid judging placement with the host menu visible.
4. Check car selection/frontend text, replay/tutorial overlays and other cameras.
   Obtain a separate takedown/popup frame when available; differing gameplay
   frames cannot establish exact visual equality.

F12 is supported by the existing capture system, but is optional and can incur
readbacks. For visual captures use identical conditions and exclude their cost
from performance conclusions. Stop on malformed packets, displaced/cropped HUD,
wrong anchoring, stretched fonts or graphics errors. No performance/HD/GPU-thread
conclusion belongs to this experiment.

## Maintainer validation and clean integration

The maintainer confirmed the 3D Hor+ camera and 2D HUD visually in 4:3,
16:9 and Fill the window: speedometer, boost and takedown elements retain
appropriate sizes and anchors. This does not certify every language, replay,
special sequence or HUD element.

The selective integration uses the unchanged engine gitlink and no local AA,
HD or GPU trace patches. The maintainer separately tested and visually accepted
this clean-engine executable in 4:3, 16:9 and Fill, including the HUD. Earlier
acceptance of the instrumented experiment is not used as a substitute. The
clean integration was tested with GPU Thread Off and HD Off; it does not certify
GPU Thread On or HD rendering.

Configure the normal PAL build with `-DPAL_WIDESCREEN_TESTS=ON`, then build
`burnout3-pal`, `pal_widescreen_camera_tests` and `pal_widescreen_hud_tests`.
Run each test executable with one unique, nonexistent output-directory argument
(a path containing spaces is supported). Expected results: 14 camera and 27 HUD
cases. These are synthetic regression checks, not gameplay equivalence proofs.
All generated translations and source manifests remain private build inputs.
