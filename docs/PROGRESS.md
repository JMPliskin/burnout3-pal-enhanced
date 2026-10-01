# Decompilation progress

_Generated 2026-10-01 09:29 from `SLUS_210.50`._

## Headline number

**0.3282%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

## Static analysis

| metric | value |
| --- | ---: |
| image size | 4,073,088 bytes |
| proven code | 3,511,260 bytes (86.2%) |
| non-code (rodata/data/padding) | 561,828 bytes |
| functions discovered | 9,163 |
| - found via direct calls | 5,677 |
| - found via prologue scan | 888 |
| functions using VU0 macro mode | 796 |
| strings recovered | 5,556 |
| address cross-references | 8,159 |
| pointer tables (vtables/jump tables) | 355 |

## Progress by tier

| tier | functions | share | code bytes | share |
| --- | ---: | ---: | ---: | ---: |
| `undecompiled` | 8,791 | 95.94% | 3,238,224 | 99.49% |
| `analyzed` | 72 | 0.79% | 2,584 | 0.08% |
| `reconstructed` | 56 | 0.61% | 3,472 | 0.11% |
| `verified` | 244 | 2.66% | 10,684 | 0.33% |

Tier meanings are defined in [METHODOLOGY.md](METHODOLOGY.md).
