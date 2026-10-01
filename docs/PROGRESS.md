# Decompilation progress

_Generated 2026-10-01 18:48 from `SLUS_210.50`._

## Headline number

**5.8331%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

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
| address cross-references | 8,270 |
| pointer tables (vtables/jump tables) | 381 |

## Progress by tier

| tier | functions | share | code bytes | share |
| --- | ---: | ---: | ---: | ---: |
| `undecompiled` | 7,388 | 73.64% | 3,073,428 | 93.50% |
| `analyzed` | 72 | 0.72% | 2,584 | 0.08% |
| `reconstructed` | 571 | 5.69% | 19,488 | 0.59% |
| `verified` | 2,001 | 19.95% | 191,748 | 5.83% |

Tier meanings are defined in [METHODOLOGY.md](METHODOLOGY.md).
