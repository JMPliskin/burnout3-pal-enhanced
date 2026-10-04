# Decompilation progress

_Generated 2026-10-04 07:13 from `SLUS_210.50`._

## Headline number

**15.5233%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

**71.0711%** is machine-checked: verified, or `lifted` - translated instruction by instruction and differential-tested, so correct and complete enough to run, but not yet readable source.

## Static analysis

| metric | value |
| --- | ---: |
| image size | 4,073,088 bytes |
| proven code | 3,524,820 bytes (86.5%) |
| non-code (rodata/data/padding) | 548,268 bytes |
| functions discovered | 9,198 |
| - found via direct calls | 6,100 |
| - found via prologue scan | 945 |
| functions using VU0 macro mode | 725 |
| strings recovered | 5,556 |
| address cross-references | 9,439 |
| pointer tables (vtables/jump tables) | 139 |

## Progress by tier

| tier | functions | share | code bytes | share |
| --- | ---: | ---: | ---: | ---: |
| `undecompiled` | 3 | 0.03% | 876 | 0.02% |
| `analyzed` | 0 | 0.00% | 0 | 0.00% |
| `reconstructed` | 1,678 | 18.24% | 1,013,228 | 28.90% |
| `lifted` | 3,791 | 41.22% | 1,947,228 | 55.55% |
| `verified` | 3,726 | 40.51% | 544,168 | 15.52% |

Tier meanings are defined in [METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).
