# PAL versus USA: boundaries for future adaptations

Updated 23 October 2026. SOURCE facts refer to the published baseline in
[PAL_DEVELOPMENT.md](../../PAL_DEVELOPMENT.md).

- PAL uses `SLES_525.84` (1.01); USA uses `SLUS_210.50`. The original PAL ELF is
  authoritative for PAL logic, state and addresses. No global address delta is valid.
- Shared host/engine features can be reused by contract. Backend implementation,
  PAL activation and PAL validation are three separate checks.
- Original PAL language and 50/60 Hz screens are meaningful regional behavior.
  Spanish acceptance does not certify every language; PAL60 success does not
  certify PAL50, 120 FPS or arbitrary simulation clocks.
- PAL native worker/CardOr targets and copied-code discoveries were individually
  established. The recovered `0x00242AC8` case is not a USA locale hook.
- PAL camera `0x001E2100` is not USA `0x001E2110`; PAL Flush2D/Sprites2D and HUD
  anchor identities likewise need instruction/call/structure evidence.
- PAL-010's 640x448 visible region in a 640x1024 PSMCT16S target and the 640x512
  selector are explicitly guarded. Do not enable the regional exception globally.
- USA-001/002 were adapted in isolation to the older compatible engine, not
  imported with all commits from their later USA parents.
- Texture key equality, original RGBA equality and regional artwork suitability
  are different questions. PAL language/livery content must survive replacement.
- Native PC Settings/Quit integration is proposed PAL work. Do not copy SLUS
  addresses or assume an available USA menu implementation works in PAL.

**PAL correctness does not depend on pixel-identical USA rendering.** Use valid
PAL references, contracts and reproducible scenes; distinguish legitimate regional
differences from defects. Software/GPU comparisons and exact synthetic memory
comparisons have their own narrower purpose.

See [architecture/evidence](ARCHITECTURE_AND_EVIDENCE.md), [boot](BOOT_FLOW.md) and
the [retained mapping study](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/docs/pal/USA_PAL_ADDRESS_MAPPING.md).
