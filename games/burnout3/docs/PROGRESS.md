# Decompilation progress

_Generated 2026-10-04 04:51 from `SLUS_210.50`._

## Headline number

**15.4193%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

**70.7628%** is machine-checked: verified, or `lifted` - translated instruction by instruction and differential-tested, so correct and complete enough to run, but not yet readable source.

## Static analysis

| metric | value |
| --- | ---: |
| image size | 4,073,088 bytes |
| proven code | 3,524,820 bytes (86.5%) |
| non-code (rodata/data/padding) | 548,268 bytes |
| functions discovered | 9,221 |
| - found via direct calls | 6,100 |
| - found via prologue scan | 945 |
| functions using VU0 macro mode | 725 |
| strings recovered | 5,556 |
| address cross-references | 9,439 |
| pointer tables (vtables/jump tables) | 140 |

## Progress by tier

| tier | functions | share | code bytes | share |
| --- | ---: | ---: | ---: | ---: |
| `undecompiled` | 3 | 0.03% | 876 | 0.02% |
| `analyzed` | 0 | 0.00% | 0 | 0.00% |
| `reconstructed` | 1,691 | 18.34% | 1,030,836 | 29.21% |
| `lifted` | 3,802 | 41.23% | 1,952,944 | 55.34% |
| `verified` | 3,725 | 40.40% | 544,112 | 15.42% |

Tier meanings are defined in [METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).
