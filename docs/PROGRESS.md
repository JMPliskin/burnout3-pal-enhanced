# Decompilation progress

_Generated 2026-10-01 08:24 from `SLUS_210.50`._

## Headline number

**0.0000%** of the executable's code bytes are verified complete - that is, source that recompiles to byte-identical machine code.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `matched` there is no proof the source is right.

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
| `undecompiled` | 9,084 | 99.14% | 3,248,976 | 99.82% |
| `analyzed` | 72 | 0.79% | 3,412 | 0.10% |
| `identified` | 7 | 0.08% | 2,576 | 0.08% |
| `matched` | 0 | 0.00% | 0 | 0.00% |

Tier meanings are defined in [METHODOLOGY.md](METHODOLOGY.md).
