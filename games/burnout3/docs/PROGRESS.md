# Decompilation progress

_Generated 2026-10-03 09:01 from `SLUS_210.50`._

## Headline number

**15.7141%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

**69.6345%** is machine-checked: verified, or `lifted` - translated instruction by instruction and differential-tested, so correct and complete enough to run, but not yet readable source.

## Static analysis

| metric | value |
| --- | ---: |
| image size | 4,073,088 bytes |
| proven code | 3,519,772 bytes (86.4%) |
| non-code (rodata/data/padding) | 553,316 bytes |
| functions discovered | 11,247 |
| - found via direct calls | 6,100 |
| - found via prologue scan | 945 |
| functions using VU0 macro mode | 812 |
| strings recovered | 5,556 |
| address cross-references | 9,239 |
| pointer tables (vtables/jump tables) | 408 |

## Progress by tier

| tier | functions | share | code bytes | share |
| --- | ---: | ---: | ---: | ---: |
| `undecompiled` | 31 | 0.28% | 23,488 | 0.67% |
| `analyzed` | 0 | 0.00% | 0 | 0.00% |
| `reconstructed` | 2,171 | 19.30% | 1,041,768 | 29.70% |
| `lifted` | 5,177 | 46.03% | 1,891,584 | 53.92% |
| `verified` | 3,868 | 34.39% | 551,268 | 15.71% |

Tier meanings are defined in [METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).
