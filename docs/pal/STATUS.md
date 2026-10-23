# PAL Enhanced: current status

Updated 23 October 2026. Read [handoff](../SESSION_HANDOFF.md) before building:
the main local checkout is older than the published code described here.

| Capability | Current evidence/state |
| --- | --- |
| Published source/tag | DONE: PAL `2aac9350...`, engine/gitlink `bfad7255...`, tag v0.2.0-alpha; verified 22 October |
| Public binary release | Local package prepared/accepted; distribution-rights review unresolved; Codex did not create a GitHub Release or upload assets; later manual activity unverified |
| PAL boot, Spanish, original language/video selection | Implemented and manually tested within recorded scope |
| ISO/profile persistence, FMV/audio and races | Accepted; not all modes, cards or concurrent writers certified |
| Regional C and original fallback | Accepted; 2x/3x/4x tested, pacing independent, full selector preserved |
| Hor+ 3D, 4:3/16:9/fill and HUD | Implemented with scoped automated/manual acceptance |
| USA-001/002 | Accepted structural/synthetic efficiency; no quantified sustained PAL FPS/CPU gain |
| F9 settings, GPU Thread, FPS/F1 | Accepted; GPU Thread is restart-only and CLI locks remain effective |
| OFF/FXAA/SMAA | Accepted live switching/persistence; warm-up/overlay fixes preserved |
| F9/L3+R3 versus F12 | Accepted separate controls; no accidental controller captures |
| Audio after capture/window movement | Bounded recovery accepted for tested scenarios, not every interruption/device |
| New-profile fullscreen | Accepted; saved window mode and CLI override respected |
| VU Fast | Backend/compiled blocks exist; cross-program flag fidelity remains unvalidated, no general F9 option |
| SSAA / ultrawide / 120 FPS | Future work; do not infer from internal scaling or PAL60 |
| Spanish/60 Hz preference and selector skip | Planned; original screens still used |
| Native main/pause menu PC Settings/Quit entries | Planned separately from F9; no implementation claimed |
| PAL HD workflow | Existing loader/partial trials, unsatisfactory HD333 result; no complete extraction/converter/import pipeline |
| Soundtrack editor / Spotify / vehicle import | Planned/exploratory, no working implementation claimed |

Known independent limitations: minor non-blocking 3x skybox seam, possible 4x GPU
saturation, isolated pacing-OFF long interval, incomplete PAL50/all-language/effects/
hardware coverage. Do not reopen completed investigations without new evidence.

Next proposed task: bounded PAL-070 language initialization and preference
contract work, after reviewing documentation and arranging an authorized clean
baseline. See [roadmap](ROADMAP.md), [evidence](ARCHITECTURE_AND_EVIDENCE.md) and
[validation](VALIDATION.md). No implementation is authorized by this status page.
