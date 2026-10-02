# Decompilation progress

_Generated 2026-10-02 02:23 from `SLUS_210.50`._

## Headline number

**11.6340%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

**52.4670%** is machine-checked: verified, or `lifted` - translated instruction by instruction and differential-tested, so correct and complete enough to run, but not yet readable source.

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
| `undecompiled` | 4,063 | 40.50% | 1,527,148 | 46.46% |
| `analyzed` | 3 | 0.03% | 976 | 0.03% |
| `reconstructed` | 655 | 6.53% | 34,404 | 1.05% |
| `lifted` | 2,371 | 23.63% | 1,342,280 | 40.83% |
| `verified` | 2,940 | 29.31% | 382,440 | 11.63% |

Tier meanings are defined in [METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).
