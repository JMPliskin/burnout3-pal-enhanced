# Findings

What the static analysis has revealed about the retail executable. Everything
here is derived from analysis of the binary, with the evidence noted so any
claim can be re-checked.

## The disc

| | |
| --- | --- |
| Volume / boot | `SLUS_21050`, `BOOT2 = cdrom0:\SLUS_210.50;1`, NTSC |
| Publisher string | `ELECTRONIC ARTS LTD` |
| Image | 2,867,822,592 bytes, 775 files in 59 directories |
| Mastered | 2004-07-23 to 2004-07-29 |

Three executables ship on the disc:

* `SLUS_210.50` (4,073,816 bytes) - the game
* `NFSUNDER/NFSUNDER.ELF` (3,564,516) - the bundled *Need for Speed Underground 2*
  demo
* `NETGUI/NTGUI_UC.ELF` (4,193,516) - the online/network front-end

## Executable layout

The binary is a single flat `PROGBITS` section, `WAX` (writable, allocated,
executable), loaded at `0x00100000`, 4,073,088 bytes in file with a memory size
of 31,255,040 - so roughly 27 MB of BSS follows the image.

It is **completely stripped**: no symbol table, and no section headers
distinguishing `.text` from `.rodata` or `.data`. Code and data share one blob
with no markers, which is why every boundary in this project had to be
recovered by analysis rather than read off the file.

Entry point is `0x00100008`, and it is a textbook Emotion Engine `crt0`: 31
consecutive `padduw` instructions zeroing the 128-bit general registers, then
`mthi`/`mtlo`/`mthi1`/`mtlo1` clearing both multiplier accumulator pairs.

## Recovered structure

| metric | value |
| --- | ---: |
| Functions discovered | 9,163 |
| - proven by a direct `jal` | 5,677 |
| - found via pointer tables (vtables/callbacks) | 2,597 |
| - found by prologue scan | 888 |
| Pointer candidates rejected (no return) | 37 |
| Direct call sites | 40,430 |
| Image proven to be code | 3,511,260 bytes (86.2%) |
| Code attributed to a function | 3,254,964 bytes |
| Code not yet attributed | 256,296 bytes |
| Functions ending in `jr $ra` | 9,133 (99.67%) |
| Instructions failing to decode | 0 of 813,741 |
| Functions using VU0 macro mode | 796 |
| Strings | 5,556 |
| Address cross-references | 8,159 |
| Pointer tables (vtables / jump tables) | 355 |

Median function is 188 bytes. The largest is 77,428 bytes and is a *genuine*
single function, not a boundary artefact: it contains exactly one stack prologue,
one `jr $ra`, no indirect jumps and no internal `jal` target. Two others of
75,704 and 37,836 bytes share that shape. Roughly 19,000 instructions each, which
points at aggressive Metrowerks inlining.

The largest non-MIPS span is `0x004856C8..0x004D3E00` (321 KB), the main read-only
data block. The next largest, `0x00469D90..0x004843D0` (108 KB), is **not** data at
all but VU microcode - see below.

## Build toolchain

The `.comment` section identifies the compiler directly:

```
MW MIPS C Compiler (2.4.1.01)   PlayStation2
```

Metrowerks CodeWarrior, MIPS C compiler 2.4.1.01, targeting PlayStation 2. This
is unusually lucky - most stripped retail binaries do not retain `.comment`, and
identifying the compiler by fingerprinting code generation is normally weeks of
work and the first hurdle of any matching decompilation.

### A VU disassembler, and how far it gets

`tools/b3lib/vu.py` decodes microcode bundles; `tools/vudis.py` drives it. Current
coverage, with all-zero padding excluded so it cannot flatter the numbers:

| blob | real bundles | upper decoded | lower decoded | microprograms |
| --- | ---: | ---: | ---: | ---: |
| `SLUS_210.50` region | 13,465 | 85.9% | 34.2% | 173 |
| `NFSUNDER.ELF` `.vutext` | 1,955 | 85.4% | 51.8% | 16 |
| `NTGUI_UC.ELF` `.vutext` | 223 | 73.1% | 37.2% | 6 |

Excluding padding matters: a zero upper word decodes as `addx`, so counting zero
bundles as successes would make a block of padding look fully understood.

Finding the bundle layout settled one thing and corrected another. The layout is
lower word first, then upper - confirmed by the padding bundle appearing as
`8000033C 400002FF` in the real section. And the lower opcode is **seven** bits
wide, bits 31..25, not six: masking it to six collapses the special group (`0x40`,
where the integer ALU and elementary-function unit live) onto opcode `0x00`, which
made thousands of real instructions undecodable. Fixing that moved lower coverage
from 26.5% to 34.2%.

The upper (FMAC) pipeline is in good shape at ~86%. The lower slot is not, and the
gap is concentrated in a handful of opcode values. Those are deliberately left as
`.word` rather than filled in by inference: a plausible-looking wrong mnemonic in
a geometry routine is far more damaging than an obvious hole, since it would be
believed. Closing them needs the authoritative VU instruction tables.

Two consequences:

* Matching is a realistic goal, *provided* `mwccmips` 2.4.1.01 can be obtained.
* Attempting to match with GCC is not realistic. Metrowerks differs
  systematically in register allocation, instruction scheduling and delay-slot
  filling, so a GCC-based effort would plateau short of a match.

Neither of the other two executables on the disc retains a `.comment` section,
so their toolchains are unknown.

## The ValueDB name hash

Parameters are addressed by a 32-bit hash of their full slash-separated path, not
by string comparison. The hash is `func_0021BFA0`, a table-driven reflected
CRC32 over the path, with the 256-entry table at `0x0049B110`. That table was
checked against a freshly generated reflected-CRC32 table for polynomial
`0xEDB88320` and all 256 words agree exactly (covered by a test).

It is **not** interchangeable with a standard CRC32, for two reasons visible in
the disassembly:

* the accumulator is a *signed* int and the shift is `sra`, so the vacated high
  bits are filled with the sign bit rather than zeroes - what a compiler emits
  for `int crc`, almost certainly not intentional, but it is what the game
  computes;
* there is no final `^ 0xFFFFFFFF`.

The byte load is `lb`, not `lbu`, so a character above `0x7F` would sign-extend
and index outside the table. Every name in the database is plain ASCII, so this
never fires - worth recording so a port does not "fix" it blindly.

### How a key is built

`func_0021BB60` (613 callers, and the callee at 67 of the tuning-string sites)
assembles the path before hashing it:

1. `StringAssign(buf, part1)`
2. `StringAppend(buf, part2)`
3. `StringLength(buf)`, then compare the last character against `0x2F` (`/`)
4. if it is not already a separator, append the `"/"` at `0x004B9C88`
5. `StringAppend(buf, part3)`
6. `HashName(buf)` - the result drives a search over 32-byte records

`func_0024FDF0` is a thin forwarder that hashes a bare name with no path
assembly, so not every lookup takes the path-building route.

### The compiled database format

`DATA/VDB.XML` is fully mapped - every byte is accounted for, and all three
header counts are explained:

| region | size | content |
| --- | --- | --- |
| `0x000000..0x000018` | 24 B | header: version `2`, counts 7,525 / 452 / 348, offset `0x010918` |
| `0x000018..0x00EB40` | 7,525 x 8 B | `(key: int32, value: f32)`, sorted by **signed** key |
| `0x00EB40..0x010780` | 452 x 16 B | `(f32, f32, f32, 0xCDCDCDCD)` |
| `0x010780..0x010918` | 408 B | 102 words of mixed int and float - undecoded |
| `0x010918..0x0113F8` | 348 x 8 B | `(tag: 1, key: int32)`, sorted signed |

The main table is one run sorted by *signed* comparison, which is why all 3,775
negative keys precede all 3,750 positive ones. An earlier reading mistook that
sign change for a boundary between two separate tables; it is not.

Two checks confirm the keys are hashes rather than allocated identifiers: they
are strictly ascending as signed int32 across all 7,525 entries, and their mean
spacing is 570,757 against a uniform expectation of 570,760 over 2^32 - agreement
to within 0.001%, which allocated ids would not produce.

The 452 sixteen-byte records carry an `0xCDCDCDCD` sentinel in every single
record. That is the fill a Microsoft debug runtime writes over uninitialised
memory, so the authoring tool was a Windows build that serialised a partly
uninitialised structure. The three floats are *not* simply `(something, min,
max)`: only 296 of 452 satisfy `f1 <= f2`, and the first float is zero in 313
records, so the layout is recorded but its meaning is still open.

The 348 entries in the final section use a key namespace that is **disjoint from
the main table** - none of the 348 keys appears among the 7,525 - and split
exactly 174 negative / 174 positive. Their purpose is unknown.

### Naming the keys: still open

The parameter names are **not in the database file**; it holds only hashes and
floats. They live in the executable, so the mapping has to come from there.

Recovering the arguments at all 705 lookup call sites (702 resolved) yields the
real path fragments - leaf parameter name in `$a2`, category in `$a3`, authoring
`.cfg` path in `$t0`, giving pairs such as `Score/Boost/Air` +
`Minimum Air Distance (Metres)`. Hashing those in every ordering and format, over
16 hash configurations and 7,067 candidate strings, matches **none** of the
7,525 keys.

The reason is visible in `func_0021BB60`: the first argument is a node structure
that the function walks (`lw $a2, 0x4($s4)`, `lw $v1, 0xc($s4)`, 32-byte
records), so the full path hashed at runtime includes ancestor context supplied
by the caller. That context is not recoverable from the call sites alone, which
is why static reconstruction stops here. Options from this point are to emulate
the lookup path, or to find the authored `.cfg` files if any survive.

## What the executable is made of

Every byte classified, each assigned to exactly one category (`tools/layout.py`).
The categories summing to exactly 100% is also a check on the other passes - a
double count would show up here.

| category | bytes | share |
| --- | ---: | ---: |
| `code-function` | 3,254,964 | 79.91% |
| `code-orphan` | 259,012 | 6.36% |
| `data` | 199,588 | 4.90% |
| `zero` | 187,851 | 4.61% |
| `vu-microcode` | 108,096 | 2.65% |
| `string` | 52,661 | 1.29% |
| `pointer-table` | 10,916 | 0.27% |
| **total** | **4,073,088** | **100.00%** |

`code-orphan` is MIPS proven reachable by traversal but not attributed to any
discovered function, most of it entered through pointer tables. `data` at 4.90% is
the genuinely unidentified remainder.

Worth noting that MIPS code accounts for 86.3% of the image, and a further 2.65%
is VU microcode, so only about 11% is data of any kind.

## The other two executables

Running the same pipeline over the sibling executables validates the tooling and,
more usefully, reveals structure the main executable has had stripped out.

| executable | functions | code coverage | end in `jr $ra` | VU0 users | strings |
| --- | ---: | ---: | ---: | ---: | ---: |
| `SLUS_210.50` | 9,163 | 86.2% | 99.7% | 796 | 5,556 |
| `NFSUNDER.ELF` | 10,329 | 92.1% | 99.8% | 398 | 2,441 |
| `NTGUI_UC.ELF` | 3,098 | 89.3% | 99.8% | 18 | 770 |

The return rate holding at 99.7-99.8% across three independently built binaries is
reasonable evidence that the discovery heuristics are sound rather than tuned to
one image.

### These two keep their section names

Unlike the game executable, both retain a section header table:

| | `.text` | `.vutext` | `.DVP.overlay.*` |
| --- | ---: | ---: | ---: |
| `NFSUNDER.ELF` | 2,566,272 | 20,624 | 8 sections |
| `NTGUI_UC.ELF` | 741,416 | 1,792 | 2 sections |

`.vutext` holds **VU microcode** - instructions for the Vector Units, which are not
MIPS and need a separate disassembler. `.DVP.overlay.*` sections are DMA/VIF
packet overlays, the microprograms uploaded to VU1. In `NFSUNDER.ELF` `.vutext`
begins at `0x00372880`, immediately after `.text` ends, so this toolchain lays
VU microcode directly behind the MIPS code.

That is a useful lead for the game executable: it almost certainly contains an
equivalent region, and locating it matters for a port, since VU1 does most of the
geometry work. It also explains part of the 13.8% of the image not proven to be
MIPS code - some of it is not MIPS at all.

`NFSUNDER.ELF` is the bundled *Need for Speed Underground 2* demo, a different EA
studio's codebase: it carries no RenderWare `$Id` tags, so it does not share the
game's middleware.

## VU microcode located in the game executable

Roughly 108 KB of `SLUS_210.50` is **VU microcode**, not data, and had been
written off as "non-code" because it is not MIPS.

The region is `0x00469D90..0x004843D0` (108,096 bytes). It was found by taking the
confirmed `.vutext` of `NFSUNDER.ELF` as ground truth rather than guessing at the
instruction set. VU microcode is stored as 8-byte pairs - a lower instruction then
an upper one - and two constants are highly distinctive: `0x8000033C` is the lower
`nop` and `0x000002FF` (with flag bits) the upper `nop`. Every one of the 2,354
lower-`nop` words in the image falls inside a single cluster spanning that region,
with none anywhere else.

Comparing the region against the known-good section:

| | lower `nop` | upper `nop` | `E` bits | dominant upper opcode |
| --- | ---: | ---: | ---: | ---: |
| `SLUS_210.50` candidate | 17.4% | 27.8% | 173 | `0x3F` (4,681) |
| `NFSUNDER.ELF` `.vutext` | 26.3% | 43.6% | 16 | `0x3F` (1,238) |

Same signature - opcode `0x3F` dominant, destination masks concentrated on 0, 15
and 14 - with lower padding density in the game, which is what denser real code
looks like. The `E` (end) bit marks the last instruction of a microprogram, so the
game carries roughly **173 VU microprograms** against the demo's 16. That is
consistent with Burnout 3 pushing most of its geometry work through VU1.

Two consequences:

* The MIPS code-coverage figure is a more complete picture than it looks. A
  meaningful part of what is not MIPS code is not *meant* to be.
* Decoding this needs a **separate VU disassembler** - the VU ISA is unrelated to
  MIPS, with paired upper/lower instruction slots and its own register file. That
  is a prerequisite for a port, since this is where the geometry pipeline lives.

Exact boundaries are approximate: they come from the `nop` cluster combined with
the surrounding non-MIPS gap, not from a section header, because the game
executable has none.

## Middleware: RenderWare 3.6

The binary retains RCS `$Id:` tags naming the exact SDK sources linked in:

```
rwsdk/src/plcore/babinary.c      rwsdk/driver/sky2/basky.c
rwsdk/src/plcore/bamatrix.c      rwsdk/driver/sky2/skyblit.c
rwsdk/src/bacamera.c             rwsdk/driver/sky2/skyconv.c
rwsdk/src/badevice.c             rwsdk/driver/sky2/skyinst.c
rwsdk/src/batextur.c             rwsdk/driver/sky2/texcache.c
rwsdk/src/pipe/p2/baim3d.c       rwsdk/src/pipe/p2/p2heap.c
```

`sky2` is RenderWare's PS2 driver ("Sky" was Sony's codename for the EE), and
`pipe/p2` is the PS2 rendering pipeline. This matters for the project: the
RenderWare API is publicly documented, so these functions can be identified from
their signatures rather than reverse-engineered from scratch. They are the most
tractable large body of code in the image.

## The ValueDB tuning system

The most heavily cross-referenced strings describe a data-driven tuning system.
`../Export/ValueDB/Score.cfg` is referenced from 82 distinct sites, and the
surrounding strings are parameter names:

* Scoring - `Score/Burnout Points`
* Vehicle handling - `Slip Speed Multipler` (the misspelling is in the original),
  `Spring Coeff`, `Damping`
* Collision/audio response - `Min Impulse`, `Max Impulse`, `Min Volume`,
  `Max Volume`

`DATA/VDB.XML` on the disc is the authored side of the same system. Despite the
extension it is **not** text XML but a compiled binary database, so a format
decoder is needed. Because both the consuming code and the authored data are
available, this subsystem is unusually well-evidenced and is a good early target
once the toolchain question is settled.

## Kernel layer

Several EE kernel wrappers are unambiguous and are the first functions
reconstructed in `src/kernel/syscalls.cpp`:

* `0x0010BE40` - syscall `0x42`, `SignalSema`
* `0x0010FEA0` - loads a global semaphore id from `0x00484204` and tail-calls the
  above
* `0x00114EB8` - samples the COP0 `Status` EIE bit, executes `ei`, and returns
  the *previous* interrupt-enable state

These are deliberately the first targets: they are short enough that the mapping
from machine code to source is unambiguous, which makes them the right test
cases for identifying the original compiler.

## How consistent struct field offsets are

`tools/fields.py` measures this before any struct recovery is attempted. Each
function's pointer arguments are scanned for the offsets and widths they are
dereferenced at, and a caller's argument passed unchanged into a callee links the
two slots as the same object. Linked slots form candidate types.

* 6,623 argument slots are dereferenced; 4,094 of them at two or more offsets.
  16,685 pass-through links connect them.
* Where two or more functions touch the same offset of a candidate type, they agree
  on its width 96.6% of the time (98.1% with a strict hub cutoff of 5 callers,
  91.2% with no cutoff). The field evidence is coherent.
* Only about 30% of offsets are seen by more than one function, so most layout
  evidence comes from a single function.
* Group sizes are not stable. Linking through generic routines (slots with many
  callers) fuses unrelated types: the largest group is 2,005 functions with no
  cutoff, 723 at 20 callers, 163 at 10 and 94 at 5. Struct recovery must treat
  widely shared functions as generic rather than as evidence of one type.

## Recovered struct types

The recovery (`tools/b3lib/structs.py`, reported by `tools/structtypes.py`) finds
5,321 types with at least one field across the binary, 779 of them used by two or
more functions. It recovers 3,069 pointer fields that lead to another recovered
type and 1,220 embedded types. The largest type spans 90 functions. The conflict
guard made 10,997 joins and refused 911.

The two checks the guard does not influence: at the 3,148 offsets used by several
functions, 93.9% are used consistently as either float or integer, and 97.2% are
loaded with consistent signedness. With the guard disabled, one type absorbs 1,422
functions and both figures drop (to 91.8% and 95.1% on an earlier run).

Three things the recovery taught about the code:

* About half of all apparent argument links were not arguments at all. A register
  that still holds a value at a call is passed only if the callee reads it, and
  the compiler leaves `$a1..$a3` untouched across many calls to one-argument
  functions.
* Objects embed sub-objects and hand them to functions directly: `func_001219D8`
  initialises three embedded records at +0x1E4, +0x23C and +0x294 (0x58 apart) by
  passing interior pointers to one initialiser.
* Arrays of records are cleared by indexed loops (`func_0013D4D0` zeroes 24 records
  of 0x50 bytes). The scan cannot follow a variable index; the unrolled derivations
  can, and their accesses are fed back as evidence.

### Global variables

Global memory is recovered by the same machinery, as one more type node standing
for the whole address space: a register holding a constant address (`lui`, or the
fixed `$gp` of 0x004E8670) holds "globals plus a constant", so an access through it
is a field of the global space at an absolute address, a pointer loaded from a
global gets a type of its own, and a global's address handed to a function makes
that function's argument type a *global object* at that address.

That finds 6,129 distinct global variables, 443 of which hold a pointer to a
recovered type, and 2,733 global objects of a known type. Adding them did not make
ordinary types worse: excluding globals, offsets shared by several functions are
94.2% consistent as float or integer and 97.5% in signedness, over 3,905 offsets
(against 93.9% and 97.2% over 3,148 before). Global cells themselves are 89.4%
consistent as float or integer, since float globals are often copied as plain
words, and 99.6% in signedness.

### Arrays

Arrays come from two kinds of evidence. A derived access of the form base +
offset + stride * index - found by putting the address in linear form, which is
exact through shifts, multiplies and sums - makes the field at that offset element
0 of an array of that stride. A run of same-shaped fields at a fixed spacing is an
array the code walked with an unrolled loop, and the run gives its length. Runs
must be long (16 contiguous elements, or 6 records with gaps) because a struct of
consecutive ints looks exactly like a short int array; neighbouring globals get no
run detection at all.

Examples: `func_0013D4D0` clears an array of 24 records of 0x50 bytes, and
`arg0.f_362E0((arg1 & 255) >> 5) & 1ull << (arg1 & 31)` is a bitset lookup.

### In the emitted source

480 of the 647 derived functions take at least one typed argument. Plain field
accessors account for 2,609 reads and 2,302 writes, array elements of struct types
for 252 reads and 2,054 writes, scalar globals for 510 reads and 431 writes, and
global arrays for 211 reads and 459 writes. 43 reads and 82 writes are still done by
address.

## Open questions

* Which compiler and flags produced this binary? Unresolved, and it blocks all
  byte-matching work.
* Where exactly does code end and read-only data begin? The 81% figure is a
  lower bound - the traversal only marks bytes it can prove are reachable.
* What is the `VDB.XML` container format?
* How is the VU1 microcode stored and uploaded? 703 functions touch VU0 macro
  mode, but the VU1 microprograms are a separate problem and are not MIPS code.
