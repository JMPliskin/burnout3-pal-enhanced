# Decompilation progress

_Generated 2026-10-01 20:59 from `SLUS_210.50`._

## Headline number

**8.0579%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

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
| `undecompiled` | 7,201 | 71.78% | 2,995,316 | 91.12% |
| `analyzed` | 72 | 0.72% | 2,584 | 0.08% |
| `reconstructed` | 581 | 5.79% | 24,464 | 0.74% |
| `verified` | 2,178 | 21.71% | 264,884 | 8.06% |

Tier meanings are defined in [METHODOLOGY.md](METHODOLOGY.md).
