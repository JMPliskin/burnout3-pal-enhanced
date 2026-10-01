# Decompilation progress

_Generated 2026-10-02 01:30 from `SLUS_210.50`._

## Headline number

**11.4772%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

**37.6859%** is machine-checked: verified, or `lifted` - translated instruction by instruction and differential-tested, so correct and complete enough to run, but not yet readable source.

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
| `undecompiled` | 4,548 | 45.33% | 2,013,232 | 61.24% |
| `analyzed` | 3 | 0.03% | 976 | 0.03% |
| `reconstructed` | 653 | 6.51% | 34,212 | 1.04% |
| `lifted` | 1,926 | 19.20% | 861,544 | 26.21% |
| `verified` | 2,902 | 28.93% | 377,284 | 11.48% |

Tier meanings are defined in [METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).
