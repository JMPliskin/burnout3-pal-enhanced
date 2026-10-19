# PAL Enhanced: release and development status

Updated 19 October 2026. Gameplay acceptance is scoped evidence, not complete
binary equivalence or certification of every game mode.

## Public release

[v0.1.0-alpha](https://github.com/JMPliskin/burnout3-pal-enhanced/releases/tag/v0.1.0-alpha)
is the unchanged Windows download. The maintainer confirmed PAL ISO selection,
ISO-path persistence and renamed-image recovery, a clean isolated installation,
new-profile creation/save/reload, menus/FMV/audio/controls and complete races
with GPU Thread On. Spanish and original PAL 50/60 Hz selection are retained.
Its compiled GPU instrumentation remains present even when trace logging is off;
the older build's frozen image is not explained or declared fixed.

## Development source: clean widescreen integration

The selective integration is based on 3ad44e482c201097333dc79a452bfb6b8b4bc648
with unchanged, clean engine d7e6e023f261d9f95ad097fcd063ec95e40ca465.
A new isolated Release build, no old objects/libraries and no EE/VU regeneration,
passed 14 camera and 27 HUD tests. Both suites passed again before commit.
The maintainer separately confirmed 4:3, 16:9, Fill and the main HUD visually
with GPU Thread Off, HD Off, 2x, AA Off, pacing On and vsync Off.

| Capability | Status in this source integration |
| --- | --- |
| Original 4:3 camera | Preserved and accepted visually |
| 16:9 Hor+ camera | Horizontal expansion; vertical view retained |
| Fill the window | Uses effective picture aspect; accepted in tested window |
| HUD anchors / sprites | Speed, boost, takedowns and main positioning accepted |
| Every language, replay, special scene or mode | Not exhaustively validated |
| GPU Thread On in this clean build | Not covered by this acceptance |
| F9 AA selector from the private experiment | Not integrated |
| Executable-relative HD discovery/catalog patches | Not integrated |
| SSAA and extended PC SETTINGS/mods/LAN | Not newly adapted or validated |

These source changes are not a new binary Release. The public alpha used the
instrumented-engine recipe; the new camera/HUD do not require that patch.
Neither the old frozen frame nor HD quality/performance questions are closed.

## Preserved inputs and evidence limits

9,383 EE translations and the original 97-program VU manifest remain private
reconstruction inputs. Recorded compiled VU1 coverage above 99.99% is specific
to the measured sessions. VU Fast remains off in the comparisons. Synthetic
regressions and gameplay do not promote historical NOT VERIFIED functions.

GPU counters confirm active CLUT GPU loads/texture decoding and substantial
read-category downloads. A presentation fallback is a supported hypothesis,
not a proven redundant synchronization or approved optimization. Whole-session
Off/On statistics are not equivalent race windows or unique-image FPS.

## Technical references

- [Development continuity](../../PAL_DEVELOPMENT.md)
- [Camera](WIDESCREEN_3D.md) and [HUD](WIDESCREEN_HUD.md)
- [USA/PAL address mapping](USA_PAL_ADDRESS_MAPPING.md)
- [Graphics/performance evidence and next measurements](GRAPHICS_PERFORMANCE.md)
- [HD trial](HD_TEXTURE_PACK.md) and [texture comparison](TEXTURE_COMPARISON.md)
- [Adaptation chronology](../../games/burnout3-pal/port/ADAPTATIONS.md)
- [Recovery inputs](../../games/burnout3-pal/port/RECOVERY.md)
- [Upstream integration](UPSTREAM_INTEGRATION.md) and [historical boot findings](BOOT_FLOW.md)

Next technical priority is attributing presentation fallbacks/read ranges in an
isolated diagnostic session before changing synchronization. Future AA/settings
and graphics work must remain separate from this accepted widescreen change.
