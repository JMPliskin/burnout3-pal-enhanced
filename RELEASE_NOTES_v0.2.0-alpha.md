# Burnout 3 PAL Enhanced v0.2.0-alpha

## Highlights

A new experimental PAL graphics base and a more complete PC Settings panel,
building on the original alpha's PAL startup, profiles, Vulkan renderer, audio
and language/video selection. This is an **alpha pre-release**, not exhaustive
certification of every configuration or game mode.

## New Features

- Regional GPU presentation reuses valid visible regions, with safe fallback
  and the original 640×512 selector preserved. Tested at 2×, 3× and 4×.
- Improved 16:9 camera and HUD support from the accepted PAL integration.
- F9 exposes GPU Thread, FPS Counter and persistent options with clear CLI locks
  and restart notices. GPU Thread changes apply on the next start.
- OFF / FXAA / SMAA can be changed live and their preferences persist.
- New profiles start fullscreen. Existing windowed preferences remain respected.

## Performance Improvements

- USA-001 removes invalid textures from repeated GS page-index invalidation work
  and reinserts them when validated.
- USA-002 uses SIMD for eligible groups of sixteen PSMCT16 transfer pixels,
  retaining the scalar path for unsupported cases.
- AA pipelines are prepared before play and compatible resources are reused.
- Frame Pacing remains optional and independent of regional presentation.

Focused correctness tests and synthetic benefits are established. A sustained
whole-game PAL CPU/FPS percentage gain has not been quantified.

## Bug Fixes

- L3+R3 opens/closes PC Settings without triggering a full GS capture.
  Held combinations do not repeatedly toggle the panel. F12 still captures.
- Overlay updates avoid holding the host handoff mutex during driver work.
- Bounded audio-latency recovery was accepted after tested GS captures and
  window movement. This does not cover every possible interruption/device.
- Saved settings remain distinct from runtime overrides; GPU Thread shows
  requested versus effective state when restart is pending.

## Installation / Upgrading

After publication has been cleared, use
`Burnout3-PAL-Enhanced-0.2.0-alpha-windows-x64.zip`, extract it and run
`Burnout3-PAL-Enhanced.exe`. Supply your own compatible European **SLES_525.84** ISO.
Windows x64, AVX2 and a Vulkan 1.3-capable GPU/driver are required.

Keep `%LOCALAPPDATA%\Burnout3-PAL-Enhanced\`: it contains preferences, ISO location
and virtual cards. Back up `memcard` first; extract the new version separately
without overwriting personal data. Existing profile workflows are retained;
arbitrary external save formats are not certified. No development tools are
needed for an approved player ZIP.

F9 / L3+R3: PC Settings. F1: FPS Counter. F12: diagnostic capture.
Original PAL language and 50/60 Hz screens remain; Spanish has been tested.

## Known Issues

- Minor, non-blocking skybox seam at 3×; heavy 4× scenes may be GPU limited.
- An isolated pacing-OFF long interval has no established cause.
- VU Fast, SSAA in F9, 120 FPS interpolation, custom music, additional USA control
  features and multiplayer/LAN are not validated PAL features of this release.
- Advanced PAL language/startup-selector preferences are not implemented.
- Source reconstruction still depends on local generated artifacts and caches.
- Public binary distribution remains pending rights review: MIT licensing of the
  port does not authorize redistribution of reconstructed original game code.
  This package is prepared locally; no public release/upload is authorized yet.

## Credits

Original PC port and ps2-port-engine: Daniel Gomes Vieira and contributors.
PAL Enhanced: JMPliskin, with assistance from OpenAI ChatGPT and Codex, directed
and reviewed by the maintainer. Third-party/SMAA notices are retained.
Unofficial; no affiliation with Electronic Arts or Criterion Games.
