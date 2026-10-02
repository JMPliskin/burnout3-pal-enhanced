# Decompilation progress

_Generated 2026-10-02 11:18 from `SLUS_210.50`._

## Headline number

**14.0354%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

**67.6800%** is machine-checked: verified, or `lifted` - translated instruction by instruction and differential-tested, so correct and complete enough to run, but not yet readable source.

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
| `undecompiled` | 91 | 0.91% | 56,964 | 1.73% |
| `analyzed` | 0 | 0.00% | 0 | 0.00% |
| `reconstructed` | 2,768 | 27.59% | 1,005,476 | 30.59% |
| `lifted` | 3,953 | 39.40% | 1,763,428 | 53.64% |
| `verified` | 3,220 | 32.10% | 461,380 | 14.04% |

Tier meanings are defined in [METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).
