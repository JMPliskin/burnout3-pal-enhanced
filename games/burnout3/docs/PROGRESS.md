# Decompilation progress

_Generated 2026-10-02 00:41 from `SLUS_210.50`._

## Headline number

**10.2885%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

**32.0001%** is machine-checked: verified, or `lifted` - translated instruction by instruction and differential-tested, so correct and complete enough to run, but not yet readable source.

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
| `undecompiled` | 5,157 | 51.41% | 2,201,256 | 66.96% |
| `analyzed` | 5 | 0.05% | 1,504 | 0.05% |
| `reconstructed` | 641 | 6.39% | 32,564 | 0.99% |
| `lifted` | 1,579 | 15.74% | 713,716 | 21.71% |
| `verified` | 2,650 | 26.42% | 338,208 | 10.29% |

Tier meanings are defined in [METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).
