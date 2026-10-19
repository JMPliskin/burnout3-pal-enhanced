# USA/PAL texture correspondence methodology

This document records completed private filename/image studies. Their tools,
original captures, third-party textures and generated reports are not shipped
by this widescreen integration. No runtime compatibility is inferred from a
filename match alone.

## Loader identifiers

At engine d7e6e023, PNG and DDS base names use these forms:

```text
texture-bits
texture-clut-bits
texture-rWIDTHxHEIGHT-bits
texture-clut-rWIDTHxHEIGHT-bits
```

Texture and CLUT hashes are hexadecimal u64 values without required leading
zero padding. `bits` is eight hexadecimal digits and records GS format,
dimensions and relevant TEXA state. The loader clears TCC (0x4000). Extensions
and subfolders do not participate in the full lookup key. Region dimensions,
palette and other metadata must otherwise agree. A shared texture-hash prefix
or first component is only a candidate, never full-key equivalence.

The engine uses XXH3-64 over GS texture data, not encoded PNG/DDS bytes; CLUT
and region/format/alpha state participate. Matching PCSX2 name syntax does not
prove identical hash inputs for every capture version. Legacy packed-region
and mip-suffixed names require separate support, not arbitrary suffix removal.

## Triple capture snapshot

Original USA and PAL images were captured with PCSX2 2.8.2 on the same circuit,
with replacements disabled. Captured coverage can still differ by car, LOD,
traffic, timing and progress.

| Quantity | Result |
| --- | ---: |
| PAL original PNG / unique loader keys | 1,264 |
| USA original PNG / unique loader keys | 1,249 |
| USA HD DDS / unique loader keys | 7,859 |
| PAL/USA matching full loader identifiers | 1,170 |
| PAL originals with some identical dimensioned RGBA USA image | 1,173 |
| Exact RGBA PAL/USA pairs | 1,485 |
| Exact RGBA pairs with different keys | 315 |
| Direct PAL/HD full-key matches, ignoring only TCC representation | 333 |
| PAL dump coverage by those direct keys | 26.34% |
| HD pack represented by those direct keys | 4.24% |
| Additional PAL keys with exact-original HD adaptation candidates | 152 |

Pair counts are many-to-many, not unique assets. Only three PAL originals add
an exact-content partner beyond the 1,170 shared identifiers. Exact RGBA means
equal dimensions and decoded channels including alpha; lossless format expansion
is recorded separately from resizing, cropping, hidden-RGB or perceptual tests.
There were 781 perceptual candidate pairs, not 781 verified replacements.

## Historical capture snapshot

| USA capture | Original keys | Direct HD matches | Capture coverage |
| --- | ---: | ---: | ---: |
| PCSX2 1.7.2404 | 421 | 369 | 87.65% |
| PCSX2 2.8.2 | 1,249 | 333 | 26.66% |

319 HD entries occurred in both USA captures, 50 only in the older capture and
14 only in the newer: 383 distinct entries in their union. Absence from a dump
does not establish incompatibility. There were 362 equal loader keys and 538
exact RGBA pairs across generations. Of those pairs, 176 differed in texture
hash, none in palette hash or normalized GS metadata. Repeated visual content
and ambiguous partners are retained, not selected arbitrarily.

The older capture contributed 16 additional PAL candidate keys, excluding the
333 direct and 152 earlier candidates: **501 distinct keys of interest = 333
direct + 168 candidates**. This does not mean 501 usable HD replacements.
Different capture sizes alone do not prove a universal hash-algorithm change.

## Evidence required for an adaptation map

Keep three separate classes: full loader-key equality; exact original pixels
with different keys; perceptual/partial candidates. Preserve all ambiguous
partners. No category proves that the HD artist's replacement preserves PAL
language, advertising, signs or liveries. No regional category follows from a
hash or a non-UI folder. A small reviewed sample showed livery/palette variations,
not a demonstrated regional cause.

A future conversion needs native requested keys, verified original-image
partners, metadata/alpha/region checks and a visual On/Off trial. Observed lookup,
loaded DDS, associated image and visible correct replacement are distinct stages.
The earlier 21-file trial had 40,304 misses and zero applications; the later
333-file trial did replace textures but was visually unsatisfactory. Neither
result proves total USA/PAL incompatibility. See [HD trial](HD_TEXTURE_PACK.md).
No converter or converted package is approved by these studies.
