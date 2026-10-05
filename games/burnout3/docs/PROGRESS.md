# Decompilation progress

_Generated 2026-10-06 01:40 from `SLUS_210.50`._

## Headline number

**15.5525%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

**75.1933%** is machine-checked: verified, or `lifted` - translated instruction by instruction and differential-tested, so correct and complete enough to run, but not yet readable source.

## Static analysis

| metric | value |
| --- | ---: |
| image size | 4,073,088 bytes |
| proven code | 3,547,880 bytes (87.1%) |
| non-code (rodata/data/padding) | 525,208 bytes |
| functions discovered | 9,279 |
| - found via direct calls | 6,100 |
| - found via prologue scan | 942 |
| functions using VU0 macro mode | 729 |
| strings recovered | 5,556 |
| address cross-references | 10,079 |
| pointer tables (vtables/jump tables) | 140 |

## Progress by tier

| tier | functions | share | code bytes | share |
| --- | ---: | ---: | ---: | ---: |
| `undecompiled` | 3 | 0.03% | 876 | 0.02% |
| `analyzed` | 0 | 0.00% | 0 | 0.00% |
| `reconstructed` | 1,395 | 15.03% | 869,968 | 24.78% |
| `lifted` | 4,130 | 44.51% | 2,093,704 | 59.64% |
| `verified` | 3,751 | 40.42% | 545,976 | 15.55% |

Tier meanings are defined in [METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).
