# Decompilation progress

_Generated 2026-09-30 21:51 from `SLUS_210.50`._

## Headline number

**0.0000%** of the executable's code bytes are verified complete - that is, source that recompiles to byte-identical machine code.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `matched` there is no proof the source is right.

## Static analysis

| metric | value |
| --- | ---: |
| image size | 4,073,088 bytes |
| proven code | 3,300,392 bytes (81.0%) |
| non-code (rodata/data/padding) | 772,696 bytes |
| functions discovered | 7,423 |
| - found via direct calls | 5,676 |
| - found via prologue scan | 1,746 |
| functions using VU0 macro mode | 703 |
| strings recovered | 5,556 |
| address cross-references | 8,171 |
| pointer tables (vtables/jump tables) | 95 |

## Progress by tier

| tier | functions | share | code bytes | share |
| --- | ---: | ---: | ---: | ---: |
| `undecompiled` | 7,419 | 99.95% | 3,484,032 | 100.00% |
| `analyzed` | 1 | 0.01% | 20 | 0.00% |
| `identified` | 3 | 0.04% | 52 | 0.00% |
| `matched` | 0 | 0.00% | 0 | 0.00% |

Tier meanings are defined in [METHODOLOGY.md](METHODOLOGY.md).
