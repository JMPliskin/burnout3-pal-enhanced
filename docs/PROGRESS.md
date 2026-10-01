# Decompilation progress

_Generated 2026-10-01 07:54 from `SLUS_210.50`._

## Headline number

**0.0000%** of the executable's code bytes are verified complete - that is, source that recompiles to byte-identical machine code.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `matched` there is no proof the source is right.

## Static analysis

| metric | value |
| --- | ---: |
| image size | 4,073,088 bytes |
| proven code | 3,300,392 bytes (81.0%) |
| non-code (rodata/data/padding) | 772,696 bytes |
| functions discovered | 7,423 |
| - found via direct calls | 5,676 |
| - found via prologue scan | 1,746 |
| functions using VU0 macro mode | 703 |
| strings recovered | 5,556 |
| address cross-references | 8,171 |
| pointer tables (vtables/jump tables) | 95 |

## Progress by tier

| tier | functions | share | code bytes | share |
| --- | ---: | ---: | ---: | ---: |
| `undecompiled` | 7,347 | 98.98% | 3,480,528 | 99.90% |
| `analyzed` | 72 | 0.97% | 3,412 | 0.10% |
| `identified` | 4 | 0.05% | 164 | 0.00% |
| `matched` | 0 | 0.00% | 0 | 0.00% |

Tier meanings are defined in [METHODOLOGY.md](METHODOLOGY.md).
