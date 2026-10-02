# Decompilation progress

_Generated 2026-10-02 03:05 from `SLUS_210.50`._

## Headline number

**11.7430%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

**63.7121%** is machine-checked: verified, or `lifted` - translated instruction by instruction and differential-tested, so correct and complete enough to run, but not yet readable source.

## Static analysis

| metric | value |
| --- | ---: |
| image size | 4,073,088 bytes |
| proven code | 3,519,772 bytes (86.4%) |
| non-code (rodata/data/padding) | 553,316 bytes |
| functions discovered | 10,032 |
| - found via direct calls | 6,100 |
| - found via prologue scan | 945 |
| functions using VU0 macro mode | 810 |
| strings recovered | 5,556 |
| address cross-references | 9,239 |
| pointer tables (vtables/jump tables) | 381 |

## Progress by tier

| tier | functions | share | code bytes | share |
| --- | ---: | ---: | ---: | ---: |
| `undecompiled` | 2,305 | 22.98% | 1,153,432 | 35.09% |
| `analyzed` | 1 | 0.01% | 428 | 0.01% |
| `reconstructed` | 686 | 6.84% | 39,012 | 1.19% |
| `lifted` | 4,046 | 40.33% | 1,708,356 | 51.97% |
| `verified` | 2,994 | 29.84% | 386,020 | 11.74% |

Tier meanings are defined in [METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).
