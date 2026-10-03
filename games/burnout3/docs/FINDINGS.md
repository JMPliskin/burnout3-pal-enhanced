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

`engine/ps2decomp/vu.py` decodes microcode bundles; `engine/tools/vudis.py` drives it. Current
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

Every byte classified, each assigned to exactly one category (`engine/tools/imagemap.py`).
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
reconstructed in `games/burnout3/src/kernel/syscalls.cpp`:

* `0x0010BE40` - syscall `0x42`, `SignalSema`
* `0x0010FEA0` - loads a global semaphore id from `0x00484204` and tail-calls the
  above
* `0x00114EB8` - samples the COP0 `Status` EIE bit, executes `ei`, and returns
  the *previous* interrupt-enable state

These are deliberately the first targets: they are short enough that the mapping
from machine code to source is unambiguous, which makes them the right test
cases for identifying the original compiler.

## How consistent struct field offsets are

`engine/tools/fields.py` measures this before any struct recovery is attempted. Each
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

The recovery (`engine/ps2decomp/structs.py`, reported by `engine/tools/structtypes.py`) finds
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

### Loops, re-rolled

The derivation unrolls every loop it can, so the emitted code used to show a loop
that clears 24 records as 120 statements. Where a block of element writes repeats
with every index advanced by the same step and nothing else different, it is now
folded back into a `for` loop; each statement keeps its own starting index, so a
body writing `a[i]` and `a[i + 20]` re-rolls too. 28 loops are recovered this way,
and the generated file shrinks from 12,255 to 11,237 lines.

What does not re-roll yet: two-dimensional layouts - records 0x90 bytes apart that
each contain a small array of their own. The run detection finds the inner arrays,
so successive records' writes are labelled against different arrays and the outer
loop is invisible. Fixing that needs nested arrays (records containing arrays) in
the type model.

### In the emitted source

480 of the 647 derived functions take at least one typed argument. Plain field
accessors account for 2,609 reads and 2,302 writes, array elements of struct types
for 252 reads and 2,054 writes, scalar globals for 510 reads and 431 writes, and
global arrays for 211 reads and 459 writes. 43 reads and 82 writes are still done by
address.

## Calling convention

The game follows the EE's register calling convention: eight integer arguments in
`$a0..$a3` and then `$t0..$t3`, and eight float arguments in `$f12..$f19`. The
deriver originally modelled only `$a0..$a3`. Asked for its own refusal reasons
(`engine/tools/refusals.py`), it named "reads uninitialised register" as the first
obstacle in 958 functions: 582 reading `$t0..$t3` (more than four arguments) and
294 reading `$f12..$f14` (float arguments). With the full argument vector that
falls to 86.

## Calls into code that is not derived yet

Requiring every callee to be derived first held back 4,409 functions - more than
any other obstacle. Letting derived code call the original instead (see
ARCHITECTURE.md) took derived-and-verified functions from 694 to 1,138, and
verified code from 1.15% to 2.33% of code bytes. All emitted functions agree with
the original when compiled, their underived callees running as original code.

What the change exposed about the analysis: 1,377 functions call an address that
the function discovery never recorded as a function start. Every `jal` target is
one by definition, so discovery was missing functions; such calls are refused
rather than made with guessed arguments. (Making them anyway, with no arguments,
let seven tail-calling thunks through - and they disagreed.)

### Function discovery, corrected

The cause was the "does this look like code" check. It stopped at the first
block-ending instruction without counting its delay slot, so a function whose
whole body is `jr $ra; lw $v0, 8($a0)` - a getter - had one instruction of
"work" and was rejected as data. 424 call targets were lost that way, one called
from 866 sites. Fixing it, accepting `jal` targets that are an empty function or
a thunk (`jr $ra; nop`, `j x; nop`), and letting the prologue scan find functions
whose bytes another function's traversal merely passed through, takes discovery
from 9,163 to 10,032 functions. Every earlier function is kept, no verified
function's extent changed, and exactly one `jal` target in the binary is now not
a function start.

Derived and verified rose from 1,138 to 1,612 functions; verified code from 2.33%
to 2.52% of code bytes. The calls still refused for "not a known function start"
are now tail jumps into the middle of other functions - shared code - which is
also what "control flow leaves the function" is.

### Shared tails

Metrowerks shares code between functions: a short function often ends by jumping
into the middle of another, to a tail they both finish with. The deriver now
follows such a jump and carries on as though the tail were its own - nothing
returns to the jump, so it is not a call - while a jump to a known function start
stays a tail call. That took derived-and-verified functions from 1,612 to 1,835
(verified code 2.52% to 2.60%), with all 1,794 emitted ones agreeing when
compiled. Many of the rest now get as far as a shared *epilogue*, which restores
registers from a stack frame the jumping function did not lay out itself; "reads
an unwritten stack slot" rose from 398 to 1,281 accordingly.

## Loads that may or may not overlap a store

The largest obstacle after shared tails was a load that might read what an
earlier store wrote, where the addresses alone cannot decide it - `*p = 7; return
*q;` with nothing relating p and q (1,726 functions). The deriver now puts a
*barrier* there: the stores so far are applied, and the load reads memory as it
then is. That is exactly what the original does, so it is not a guess that
testing has to back up - the barrier is an opaque call with no callee, and the
epoch machinery built for calls handles it unchanged. The same applies when an
inlined callee might read what its caller wrote: a barrier goes in first.

Derived and verified rose from 1,835 to 2,020 functions; verified code from 2.60%
to 3.32% of code bytes. All 1,967 emitted functions agree when compiled, and the
category no longer appears among the refusals.

## The stack frame: widths, out-parameters, fragments

Frame slots were looked up by exact address and width, so a value spilled 8 bytes
wide and reloaded 4 bytes wide counted as never written, and a 4-byte reload
returned the whole 64-bit register. Frame reads are now forwarded from the newest
covering write and narrowed as the load narrows.

Passing the address of a local to a callee - an out-parameter - used to be
refused (594 functions). Now the frame *escapes*: its slots are flushed to memory
and later accesses go to memory, so the callee's writes are seen. Spills of the
entry values the model invents for saved registers are not flushed; they would
write made-up values, the return sentinel among them, into the guest stack. In
the emitted C++, such a function allocates guest stack (`StackEnter`/`StackLeave`)
and addresses its frame relative to `sp`; verification runs its callees clear of
that frame. A first version missed frame addresses that were still unfolded sums
and emitted the model's stack address as a constant; the compiled check caught it.

The same escape has to happen before an *inlined* callee is handed a local's
address: without it the callee read stale stack memory, and a function taking no
arguments passed its near-vacuous test anyway. Any frame address reaching memory
in a derivation that did not escape is now refused outright.

Most of the remaining "reads an unwritten stack slot" refusals are not stack
arguments but *fragments*: pieces of larger functions that discovery accepted as
entries (through jump tables), which restore registers from the enclosing
function's frame. They are not worth deriving as functions.

Derived and verified: 2,020 -> 2,157 functions; verified code 3.32% -> 3.86% of code bytes. All 2,102 emitted functions agree when
compiled. Two functions that the pipeline drops (one disagreeing in the model, one
compiled) agree when run alone; their callees most likely read a register the
parameter analysis did not attribute to them, which then holds whatever the
interpreter last left there. Verification catches such cases and drops them.

## Calls through function pointers

A call through a register - a function pointer or a virtual method - is now an
opaque call whose target is an expression: verification evaluates it and runs
whatever original code it points at, and the emitted C++ calls
`CallPointer(address, args...)`, which a port maps from guest addresses to its
own functions and the compiled check links back to the interpreter. Which
function is reached, and so what it reads, is unknown, so the call passes the
integer arguments up to the highest one the function set (always `$a0`, the
object of a method call) and the float arguments it set; passing every register
that still held something would have given every such function sixteen
parameters. Derived and verified rose from 2,157 to 2,223 functions, verified
code from 3.86% to 4.24%.

"Could not be tested" rose by about 300 at the same time. Test memory is random,
so a function pointer loaded from it points nowhere and the call faults. Test
memory that holds real function addresses where code expects them is the obvious
next improvement to the harness.

## A harness the code can run in

Of the functions that derived but could not be tested, measuring why showed three
causes: a null pointer read from the null page (712), an instruction the
interpreter lacked in some *callee* (545 - `syscall`, `mfc0`, the trap
instructions and the FPU accumulator ops dominate), and wild pointers (237). Only
113 ran and then failed in the comparison itself.

* Game state is reached through global pointers - a manager object, the current
  race - that are null in the executable and set up at run time. The recovered
  types say which globals are dereferenced as pointers; for testing, the 804 still
  holding zero are pointed at scratch memory. Like the memory profiles this decides
  only what a test reaches: original and derivation read the same memory.
* The interpreter gained stubs for `syscall` (does nothing, returns 0) and `mfc0`
  (reads 0), the trap instructions, and the FPU accumulator. A derived function's
  own code never contains these; they let the original callees it calls run.

Two modelling bugs surfaced on the way. A 16-byte struct copy (`lq` then `sq`) to
the stack was treated as a register spill, so a callee given its address read
memory that was never written; such copies - also to ordinary memory, previously
refused as 128-bit stores - are now two 8-byte copies, and only the model's
invented entry values of saved registers (now including the float ones) stay
symbolic. And the parameter analysis scanned functions in address order, so a
register written on one branch and read after the paths rejoin looked unused;
callers then passed nothing in it. It is now live-in analysis over each
function's control flow, solved to a fixed point over the call graph. That is
conservative - a call through a register might read anything - so 61 of 527
underived callees take all sixteen argument registers; a register the caller no
longer holds a value in is passed as 0, and if the callee really reads it the test
disagrees and the function is dropped, instead of refusing every such call.

Derived and verified rose from 2,223 to 2,475 functions; verified code from 4.24%
to 5.17% of code bytes. All 2,415 emitted functions with a verdict agree when
compiled. Three model disagreements and one compiled one remain among the dropped
functions; they agree when run alone and depend on interpreter state left by
earlier tests.

## Loops whose trip count depends on the input

Loops were either unrolled - when their trip count folds to a constant - or
refused; together with "too many paths", mostly loops forking every iteration,
that was the largest modelling gap, close to a megabyte of code. A loop is now
derived once over variables (`paths.Loop`): every register the body writes is a
`Var`, initialised from its value before the loop; the body is walked once as a
tree whose leaves continue (giving every variable its next value), break to an
exit, or return. Each iteration begins a new memory epoch, so a load inside the
loop reads memory as it is on that iteration, and one from the last iteration
used after the loop reads that iteration's memory. Which exit was taken is an
ordinary condition afterwards, so the rest of the machinery is unchanged. Loop
mode is only tried when unrolling fails, and only for functions with a backward
branch. Variables nothing reads are pruned - otherwise a temporary like `$t0`
starts from argument 4 and the function appears to take a fifth argument. The
emitted C++ is a real `for (;;)` - a `strlen` comes out as exactly that.

Derived and verified rose from 2,475 to 2,680 functions, verified code from 5.17%
to 5.83% of code bytes, with 394 loops in the emitted source. 22 functions
disagree only in compiled form and are dropped; they are the next thing to look
at. Loops made a full run much slower - derivation alone went from about two
minutes to twelve - so refused functions are now re-attempted only when a callee
has been derived since, struct recovery runs once per run, and the compiled
check reuses the original's results recorded during verification; a full run
is back under half an hour.

## Deterministic tests

Every interpreter call now starts from the same register state. Registers used to
keep whatever the previous run left, so a callee reading one the analysis did not
attribute to it saw different garbage depending on what had run before: the same
function could pass alone and fail in the pipeline, and the compiled check -
which reuses the original's results from verification - disagreed on 22
functions for that reason alone. With clean registers it disagrees on 2.

That also exposed functions that had been passing by luck, and one real pattern
behind them: a function that sets `$v0`, calls a function that never touches
`$v0`, and returns - handing back its own earlier value. The model took every
call's `$v0` to be the callee's result. Calls to functions that provably leave
`$v0` alone (940 of them, a fixed point over the call graph) now keep the caller's
value, which is exact. Callees that leave it alone on only some paths remain; a
"result" that is a leftover register is meaningless to a port, and those
functions stay dropped. Model disagreements went from 23 to 11.

Derived and verified: 2,670 functions; 2,624 emitted functions agree when
compiled. Verified code: 6.01% of code bytes.

## Pipeline speed

A full run - derive, recover types, emit, compile, run the compiled code - had
grown to 29 minutes. Measuring each stage rather than guessing showed that
compiling takes 5 seconds and the compiled check 21; emission took 688 seconds,
and ran two or three times per pipeline. Nearly all of it was the struct
catalog: every typed memory access scanned every embedded type of its base and
recomputed each one's size from all its fields, and the global address space
embeds thousands of objects. Caching each type's extent and embedded map, and
finding the covering objects by binary search, took emission to 8 seconds and
type recovery from 95 seconds to under one. A full run now takes five minutes,
four and a half of them derivation.

## Merging branches where they meet

Path enumeration doubles at every branch, so a function with a run of independent
`if`s is refused for having too many paths (659 functions, 509 KB). As a last
escalation, a branch whose two routes meet again - at its immediate
post-dominator in the function's control-flow graph - and which on both routes
only computes register values is merged: each differing register becomes
`cond ? a : b`, and the walk carries on once. Sides that store, call or touch the
frame still fork. That recovered 21 functions (verified code 7.87% -> 8.06%), at
a real cost: derivation went from 392 to 676 seconds, since every function that
explodes now has its branch regions walked again. Merging sides that store - as
guarded stores - is the natural extension, and where most of the remaining
"too many paths" functions are.

## Names from evidence, and code shared with other executables

The executable has no symbols, so names come only from evidence, and each name in
`config/names.csv` records its source:

| source | functions |
| --- | ---: |
| kernel stub (`addiu $v1, $zero, N; syscall`), service from the ABI table | 66 |
| self-identifying diagnostic string (`"sceDbcInitSocket: rpc error"`) | 44 |
| hand-assigned in the status table | 15 |

The diagnostic strings identify three middleware families besides Sony's own
libraries: Logitech's wheel and headset libraries (`lgDev*`, `lgAud*`, `lgKbM*`)
and EA's online lobby code (`_Lobby*`), part of the DirtySock network library.
The rule is strict, because module tags use the same `Name: message` form
(`ProtoMangle: ...`, `DirtyElf: ...`, `libpad2: ...`). A name must look like a
function name, belong to exactly one function, and be the only name that
function prints. `lgDevInit` is lost to that rule, because a helper prints version
banners under the same prefix.

Most of those strings were invisible at first. Address pairing dropped its
pending `lui` at every branch or call *before* executing the delay slot, and the
usual way to pass a string is `lui $a0` before a `jal`, then `addiu $a0` in its
delay slot. Executing the delay slot first and keeping callee-saved registers
across calls raised the recovered cross-references from 8,270 to 9,239. Pairing
also never dropped a register that something else overwrote. That has been fixed
too.

Signatures that mask relocations (call targets, `lui`/low-half pairs,
`$gp` offsets) show how much code is shared with the two other executables on the
disc:

| other executable | functions of 4+ instructions also present | bytes |
| --- | ---: | ---: |
| `NTGUI_UC.ELF` (network GUI) | 441 of 9,039 (4.9%) | 64,700 (2.0%) |
| `NFSUNDER.ELF` (NFS Underground 2 demo, another EA studio) | 376 (4.2%) | 52,328 (1.6%) |

That is the Sony SDK and runtime the three builds share. The named functions
seed the cross-game library (`engine/signatures/library.json`), so another game
linked against the same libraries gets these names for free.

## Why derived functions could not be tested

A derivation counts only once it has been run against the original, and 2,106
derived functions could not be, because the *original* failed under the test
harness. `derive.py` now records why each one failed and prints the breakdown.
The first breakdown pointed at the harness rather than at the functions:

| cause | functions | fix |
| --- | ---: | --- |
| `ei`, `dsllv`, `mult1` not in the interpreter | 174 | implemented, with `di`, `dsrlv`, `dsrav`, `madd`, and the pipeline-1 `multu1`/`div1`/`mfhi1`/... |
| a trap: execution had jumped into the test scratch region | 337 | calls outside the executable are stubbed |
| null pointer read from a global *object*'s field | about 1,200 | found by running, and seeded |
| access to the EE hardware registers or scratchpad | about 320 | both mapped as plain memory |

The traps were all at addresses in scratch. A function calling through a pointer
read from random test memory executed random bytes until one decoded as a trap.
A call or pointer jump to anywhere outside the executable now returns 0 without
running anything. The original, its derivation and the compiled C++ all reach
indirect callees through the same interpreter, so all three meet the same stub.

The null pointers mostly came from one place. Static seeding handled global
pointer *variables*, but a global *object*, such as the singleton at `0x0051BA44`
passed as `this` to 978 call sites, holds pointer fields that stay null until the
game runs, and its methods dereference them. Recovering those fields statically
drowned in merged types (30,000 candidate addresses, two thirds of them inside
the executable). Running each function once is precise: a fault just above zero,
right after a word was read as zero, names the word. Seeding it and retrying finds
548 such words in 50 seconds.

Letting functions run further exposed a cost. The original's memory effects were
kept per byte for the compiled check to reuse, and functions that now complete a
large `memset` made the pipeline exhaust 60 GB of commit. Effects are now kept as
a digest, and a test that writes more than 64 KB is abandoned (90 functions).

Derived and verified: 2,811 -> 3,357 functions. Agreeing when compiled: 2,754 ->
3,260. Verified code: 8.06% -> 10.20%. Remaining causes, by size: loads from
unmapped memory (693), the instruction budget (491), stores to unmapped memory
(124), the 64 KB write cap (90), and VU0 macro instructions (`lqc2`, `vadd`).

## Lifting: a third of the code, machine-checked

Derivation verifies 10% of the code bytes, and measuring where they sit shows why
it stalls:

| function size (instructions) | share of code | verified by derivation |
| --- | ---: | ---: |
| up to 32 | 8.1% | 38.8% |
| 33-96 | 18.9% | 24.1% |
| 97-256 | 30.7% | 7.2% |
| 257-512 | 18.9% | 1.5% |
| over 512 | 23.4% | not attempted |

Path enumeration cannot finish large functions. Lifting, one C++ statement per
instruction over an explicit register context, has no such limit. Each lifted
function is compiled and checked against the original on the same test cases,
comparing the whole register file and memory: **4,865 functions, 32.9% of the
code**, from a 10-minute run. With the derived functions that makes **32.0% of
code bytes machine-checked** (functions verified both ways count once, under
`verified`).

Comparing whole register states found three bugs that had been verified wrong,
because the interpreter and the symbolic model made the same mistake and so
agreed:

* `sqrt.s fd, ft` takes its operand from the ft field (fs is always 0). Both read
  fs, so 78 emitted lines took the square root of `$f0`.
* `pcpyld $v1, $v1, $v0` wrote the low half before reading the high half's source,
  which is the same register. The lifted code read both first and disagreed.
* `lwu` sign-extended instead of zero-extending (interpreter only).

The interpreter also gained `break`, `mtc0`, the unaligned loads and stores
(`lwl`/`lwr`/`ldl`/`ldr`/`swl`/`swr`/`sdl`/`sdr`) and six more MMI lane operations.
What still blocks lifting is VU0 macro mode (`lqc2`, `qmtc2`, `vadd`...: about 650
functions). What blocks *testing* lifted functions is the same as for derivation:
memory faults on test data (2,480) and the instruction budget (1,076).

## Pages mapped on demand

Test data reaches addresses no real program state does: a pointer field of a
seeded object, an index scaled past the scratch region. Every fault there made a
function untestable, though it said nothing about the function. A journalled test
that faults on unmapped memory now undoes its writes, maps the 4 KB page with
deterministic "mixed" data (pointers into scratch, small integers), and runs
again. Pages stay mapped, so the derivation, the compiled code and the lifted code
all read what the original read. Derived and verified: 3,386 -> 3,669 functions
(10.29% -> 11.48%). Lifted and verified: 4,865 -> 5,401 (32.9% -> 38.3%).
Machine-checked: 32.0% -> 37.7%.

The cost is time, since functions now run further: a full lift run went from 10
to 25 minutes. The largest remaining obstacle moved from memory faults to the
instruction budget (2,023 lifted functions), as loops now reach data that keeps
them going.

## VU0 macro mode: over half the code machine-checked

The game does its vector maths on VU0 through COP2: about 62,000 instructions in
some 700 functions. 8,079 of them did not even decode. The special2 group is
indexed by bits 10..6 and 1..0 together, which the decoder now does, so every
COP2 instruction in the executable decodes.

`engine/ps2decomp/vu0.py` defines one model of VU0 arithmetic, and
`runtime/src/vu0.cpp` mirrors it line for line for lifted code:

* Operands with a zero exponent are flushed to zero; ones with the maximum
  exponent (an infinity or NaN elsewhere) are the largest finite magnitude.
* Results are computed in double, rounded to single, saturated and flushed.
* Multiply-add rounds the product first.

That is PCSX2's interpreter, not the hardware, which truncates in places. The
module says so, and a port wanting bit-exact physics has to revisit it. The flags
are not modelled, so reading them (`cfc2` of MAC, status or clip) is refused
rather than answered with a flag never set, and so is `vcallms`, which runs VU0
microcode.

Lifted and verified: 5,401 -> 5,888 functions, **38.3% -> 53.1% of the code**.
VU0 functions are large, so bytes rose far more than the count. Machine-checked:
37.7% -> **52.5%**. Derivation gains too (3,669 -> 3,710), because callees that
use VU0 can now run. There were no disagreements: across thousands of real
functions, the C++ mirror and the Python model agree on the whole VU0 state.

Execution reaching an address outside the program now stops the test. Before,
fault mapping happily mapped a page for an instruction fetch, and a bad return
address ran test data as code until the budget ran out (546 functions).

## Fragments, tail transfers, and a parallel check: 64% machine-checked

Two causes accounted for most lifted functions whose tests failed.

**Fragments.** Function discovery also finds shared epilogue tails: code entered
with a frame it never built, which restores `$ra` from its caller's frame and
returns through it. Test memory there was zero, so every one of them "returned"
to address 0. The stack above the test stack top is now filled with the return
sentinel, so a fragment's return ends its test as a function's does.

**Tail transfers.** A lifted branch out of a function hands the run to the code it
reaches - often such an epilogue - and must wait for *this function's* return,
but it waited for whatever `$ra` held. After an earlier `jal` that points inside
the function, while the epilogue restores the real return address from the
stack. A lifted function now records `$ra` and `$sp` at entry and passes them to
the host for tail transfers. The host also accepts a return at a higher stack
pointer than at entry, as an epilogue leaves it.

Lift verification now runs in parallel: each process gets one fixed share of the
functions, replays the parent's seeded pointers, and exits, so results do not
depend on scheduling. 8.5 minutes on 14 processes, down from 21.

Lifted and verified: 5,888 -> 7,720 functions, 53.1% -> 64.8% of the code.
Machine-checked: 52.5% -> **63.7%**. Derived and verified: 3,710 -> 3,797
(11.74%). The instruction budget is now the main obstacle (1,718 functions).

## Teaching the symbolic model 128-bit registers, the accumulator and VU0

Readable derivation stopped at instructions only the interpreter knew. The
symbolic model (symexec.py, paths.py) now has:

* **The FPU accumulator**, for `adda`/`suba`/`mula`/`madda`/`msuba`/`madd`/`msub`,
  rendered as `FloatMulAdd(acc, a, b)`.
* **Upper register halves**, as pseudo-registers. `lq` loads both halves; `sq`
  stores both, if the function built both; `pcpyld`, `pcpyud`, `por`, `pand`,
  `pxor` and `pnor` work on the pairs. An upper half the function never wrote is
  the caller's, so reading it is refused, not taken as zero.
* **VU0**: vector lanes as pseudo-registers, operations as `VuAdd`/`VuMul`/...
  nodes evaluated by the same functions the interpreter uses (vu0.py), and
  emitted as calls to the shared C++ implementation the lifted code uses.

Derived and verified: 3,829 -> 3,979 functions; readable verified code 12.01% ->
13.01%. Emitting a nested loop's variables at function scope cut functions
dropped for not compiling from 28 to 2.

## Structured lifted code

Lifted code is correct but reads like assembly with gotos. structure.py renders
the same statements structured: natural loops become `for (;;)` with
`continue`/`break`, two-way branches that rejoin at their immediate
post-dominator become `if`/`else`, registers become named locals (`a0`, `v0`,
`f12`), and two lambdas copy them to and from the context around calls and
returns. Whatever does not fit stays a `goto`, so the output is always faithful.
The structured form keeps lifted code's interface, so it is verified the same
way, with the whole register file compared. `lift.py --structure` emits it for
every function whose structured form verifies as well.

Of the 7,743 lifted and verified functions, 7,526 are emitted structured; every structured form tried passed verification. The rest (jump tables, linking branches) stay in the goto form. 2,296 of the structured functions still contain a `goto`, mostly compound conditions: two tests leading to one shared block.

## A complete translation

Every function that lifts is now emitted, not just the verified ones: 9,940 of
10,032. The 2,197 that could not be checked are marked `NOT VERIFIED`, with the
reason, and sit in the `reconstructed` tier. That tier means "source exists and
is believed correct but is untested", which is exactly what they are, and they
never count as verified or lifted. Calls to functions named in `names.csv` say
the name.

| tier | functions | code bytes |
| --- | ---: | ---: |
| verified (readable, derived) | 3,117 | 13.0% |
| lifted (verified translation) | 3,909 | 51.3% |
| reconstructed (mostly translated, not checkable on test data) | 2,915 | 34.0% |
| undecompiled | 91 | 1.7% |

The unverified functions are mostly ones whose original never finishes on random
test memory: walks that need a sorted range, a terminated list, a matching key.
They need realistic data, such as a memory snapshot of the running game, to be
checked.

## Division is defined; saved registers are not noise

Division by zero had been modelled as "HI/LO unchanged", so a derivation of an
unguarded `div` depended on stale registers and was refused (198 functions
first). Metrowerks rarely guards a division. The EE defines the result: a zero
divisor gives LO = 1 for a negative dividend, otherwise -1, with the dividend in
HI (`divu`: LO = -1); INT_MIN / -1 gives INT_MIN and 0. All three models now
follow that (`emu.divide`), the symbolic model emits `DivQuot`/`DivRem` from a
small runtime file, and `multu` derives through zero-extended operands. Derived
and verified: 3,979 -> 4,033 functions; readable verified code 13.01% -> 13.83%.
VU0 inside loops is now derivable too (lanes carried as loop variables), though
it moved nothing: the functions it would reach are refused for other reasons
first.

Structured lifted code drops the saves and restores of callee-saved registers.
They become plain locals, never written back, and `$ra` is restored on return.
That removes the stack traffic at both ends of nearly every function, which a
caller cannot observe. A function that breaks the convention, or reaches a
shared epilogue that restores from the stack, fails that form's verification
and is structured with the saves kept: 7,412 and 114 respectively.

## Folding temporaries in structured code

Structured code still moved every value through a register: `v0 = ReadS32(a0 +
0x20); v0 = (a1 < v0); if (v0 != 0)`. liveness.py adds a backward liveness pass
over the CFG, and folds a definition into its one use when nothing in between can
change its meaning: no operand redefined, and for a load no store, call or
compound statement. A call's `out()` reads every register without naming it, so
such a use blocks the fold, and nothing folds into a compound statement whose
own `lo`/`hi` would capture names.

Dropping writes to dead temporaries changes registers no caller may read, so this
form is verified at the calling-convention level, as derived functions are. Of
7,526 structured functions, 6,731 pass in folded form; 683 more pass with saves
elided and 112 with saves kept. Constants built in two steps (`lui`, `addiu`)
are joined, and a constant that addresses a recovered string carries the string.

## Finding inputs, and memory that ends: 68% machine-checked

Of the lifted functions that failed verification, most were untestable: the
original never finished under the standard inputs. Three ways to change that were
measured.

* A larger instruction budget does not help. Of 120 functions that ran out of
  budget, 5 finished within 1M instructions; 85 faulted once allowed to run on,
  and 30 never finished even at 5M.
* Searching for inputs does, a little. When the standard plan finds no memory
  profile the original completes under, lift.py now tries up to twelve fresh
  argument sets under every profile, with a small budget, and tests with those
  it completes on. Any input the original completes on is as valid a test as any
  other; this only decides which are tried.
* Memory whose pointers only point forward helps most. Random pointers make
  cycles, and a list walk follows one forever. Two new profiles, `forward` and
  `forward-mixed`, hold pointers that are null or point further on, so every
  chain ends. In a sample of 150 untestable functions, 18 ran under them. They
  come last, so no function that ran before is tested differently.

The search first made things worse: 7,743 verified functions became 7,581. Pages
mapped on demand (emu.Memory.map_page) are capped per process, and random
arguments reach random addresses: the search used up the budget, and the
functions verified after it in the same process faulted. Pages mapped by a failed
attempt, or by a verification that ends untestable, are now unmapped
(Memory.unmap_to). With that, 7,897 functions verify (69.1% of code bytes), and
MACHINE-CHECKED goes from 64.2% to 67.7%. Verification takes twice as long, 40
minutes on 14 processes.

Structured code reads a little more like source: a loop whose last statement is
its test is a `do { } while (c);` (1,006 of them), a comparison's widened 0 or 1
tested against zero is the comparison, small sign-extended literals are plain,
and a sign extension nested inside a sum the outer one already truncates is
dropped. 7,678 of the 7,897 verified functions pass in structured form.

## A page budget that decided results, and remembered verdicts

Test memory maps pages on demand, up to a fixed number per process. Two things
spent that budget on nothing: memory profiles that failed - in derivation as in
lifting - kept the pages they had mapped, and the pages of every function tested
stayed mapped for the rest of the run. Derivation tests some 10,000 functions in
one process; it reached the cap of 4,096 pages part-way through, and from then on
functions faulted for want of a page. Whether a function verified depended on
how far through the run it came. Adding the two forward-pointer profiles spent
the budget sooner and lost eleven verified functions; that is how it showed.

Pages a failed profile or attempt mapped are now unmapped (Memory.unmap_to), the
cap is 16,384 pages, and reaching it prints a warning. Derivation now verifies
4,235 functions, up from 4,058: VERIFIED goes from 14.04% to 14.86%. No run
reaches the cap.

A full lift run took 40 minutes and was repeated for every change, even one that
only changes how structured code is written. A verdict depends only on the
function's C++ text and the environment - callees run in the interpreter, from
the original code - so verdicts are now remembered, keyed by the text, the form
of comparison and a fingerprint of the environment (verdicts.py). A change to
structuring re-verifies only the functions whose text changed. On a 600-function
sample a repeated run verifies nothing again and agrees exactly; switching one
rewrite off re-verifies the 359 functions it touched. `--no-cache` recomputes
everything, the check on what the key cannot see.

Structured code: a test straight after a delay slot that cannot affect it is a
plain `if`; an empty then-arm is inverted; a loop that ends `if (c) { ...
continue; } else break;` tests `if (!c) break;` and, where that test comes
first, is a `while (c)`; and the link register a `jal` sets is passed to the
call. `for (;;)` loops fall from 5,129 to 3,611, with 2,136 do-while and 388
while loops, and 36,600 `ra = ...` lines go. 7,679 of 7,898 verified functions
pass in structured form, one more than before.

## Parallel derivation, and effects keyed by address

Derivation ran in one process, over half an hour. It now runs on 14, bottom-up
through the call graph: each layer of functions after every function it calls,
so a function can inline any callee that will ever be derived - serially, only
those earlier in address order. Inlining is not always a gain: a callee's paths
multiply the caller's, and some callers that derived when they called the callee
cannot be derived with it inlined. Those are retried with their callees called.
4,300 functions derive, 65 more than serially, in 9 minutes instead of 34.

The first parallel run failed 280 functions in the compiled check, which replays
in the parent process the cases a worker recorded. Recorded memory effects were
keyed by position in the interpreter's buffer, and a page mapped on demand lands
wherever the buffer ends at the time: the same address sat at different
positions in different processes. Serially, recording and checking happened in
one process and never showed it. Effects are now keyed by address
(Memory.address_at). The compiled check passes 4,166 functions, with the same
17 disagreements as before (16 serially). VERIFIED goes from 14.86% to 15.25%.

The status table had one precedence wrong. A function whose derivation ignores
its arguments is derived but only `reconstructed` - its test exercises one input
eight times - and that row displaced a `lifted` row for the same function, which
was tested on its whole state. 739 functions verified by lifting were reported
as merely reconstructed. A weak derivation now gives way to a verified lifted
copy. MACHINE-CHECKED goes from 67.6% to 69.1%; nothing was newly verified, it
was under-reported.

## Float results, and 26 functions that were verified for the wrong reason

The largest single obstacle left was a float returned by a call: 214 functions
read `$f0` after calling a function not yet derived, and the model kept `$v0` but
dropped `$f0`. A call now has a float result too - offered only where the code
reads `$f0` before writing it, so loops, which carry every register a call
clobbers, do not drag a phantom value through their variables. The emitted call
declares the callee to return the float's bits; a callee whose result is used
both ways, or a float result of a derived function, a pointer call or a call in
a loop, is still refused.

Letting those functions derive exposed something worse. A function whose own
code computes `$f0`, while `$v0` holds only what a callee left there, returns a
float - and was derived as returning that leftover `$v0`. Its test compares
`$v0`, so it passed. 26 functions (7 KB) were counted as verified on that basis.

Float returns are now modelled. What a function computes into `$f0` proves
nothing - `$f0` is every float computation's scratch register - so the evidence
is taken at the call sites (calls.float_returning): a function returns a float
if a caller reads `$f0` after calling it before writing it, and the evidence
passes down tail calls. 116 functions qualify. Their derivations return `$f0`,
are tested on `$f0`'s 32 bits, return `u32` in the emitted source, and inline
and tail-call as floats. A function that computes `$f0` and returns a leftover
`$v0` with no such evidence is still refused ("returns a float in $f0") rather
than tested on the wrong value.

Emission drops a function it cannot render, and that can drop its callers - a
caller that uses the result of a callee whose signature says void. It never
readmitted them once the callee itself was left out and called as the original.
It now retries every left-out function against the final signatures. Why each
function is not among the derived ones - refused, not emitted, not compiling, or
disagreeing once compiled - is written to `build/derive_refusals.json`.

VERIFIED: 15.31%, from 15.25% - after removing the 26 functions that did not
belong. The compiled check disagrees on 20 functions (17 before), which are left
out as always. Remembered lift verdicts are now keyed by the import closure
of the verification code alone, so changes to the symbolic deriver no longer
cost a cold lift run.

## Stack slots assembled byte by byte

The frame model answered a stack read from the single newest write covering it,
and refused a read spanning several writes ("partly overwritten", 4.6% of the
code) or the upper half of a 16-byte spill ("beyond the modelled 64 bits",
2.8%). A read is now assembled byte by byte from the newest writes covering each
byte, and a 16-byte spill keeps both halves - its reload still returns exactly
what was saved, so callee-saved round trips cancel as before. Both refusals are
gone; 4,450 functions derive (4,315 before).

The new expressions shift float results into the upper word of a 64-bit value.
The float helpers return `u32` in C++, so `FloatDiv(...) << 32` shifted at 32
bits and lost the value: 32 functions agreed as expressions and disagreed once
compiled. A 64-bit shift of an operand that may be 32 bits wide in C++ is now
cast to `u64` first. The compiled check is back to 18 disagreements.

VERIFIED: 16.18%, from 15.31%.

## Running natively: the boot so far

`engine/host` runs the translated code on a PC (engine/docs/PORTING.md);
`python engine/tools/port.py` builds the port and runs it on the user's own
executable. What Burnout 3's start-up taught the host:

* `crt0` writes the FPU control register (`ctc1 $zero, $31`), which neither the
  interpreter nor the lifter knew. Of FCR31 only the condition bit matters on
  the EE; both now implement `cfc1`/`ctc1` that way.
* The SDK's start-up installs syscall handlers with SetSyscall and then searches
  kernel memory, 0x80000000-0x80080000, for the handler addresses it installed,
  to learn where the kernel's syscall table is. The host keeps the table in
  guest kernel RAM, so the search finds it, as on the console.
* It copies three kernel extensions out of the executable's data into kernel
  RAM and runs them there - 0x740 bytes from 0x485060 to 0x80076000 (the alarm
  services), and two more. The host finds where such code came from by its
  bytes; port.py translates it at its run address and builds again.
* Timer registers are written through a kernel-mode helper that ends in `eret`
  (0x1156C0); the port overrides it with the store it performs.
* The SDK reads IOP RAM directly at 0x1C000000, and addresses the timers
  through kseg1 (0xB0001000): the host maps both.
* Five functions it called - 0x116E18, 0x130BF0 and three at 0x4D5xxx - were in
  no function the analysis knew. That led to recovering functions from the gaps
  (below).

It now boots through `main` (0x12EB30) into two threads, 257 functions, and
stops where both wait for the IOP: the next stage is the IOP's side of SIF RPC.

## Functions recovered from the gaps

The analysis follows calls from the entry point and recognised pointer tables;
a function reached any other way - a virtual method, a callback stored in a
variable, a tail-call target - was never found. `ps2decomp/gaps.py` recovers
them from the gaps between known functions where there is evidence one starts
(a call, an address built in code, a pointer in the data, or the first
instruction after a known function) and the code there reaches a return or a
tail jump before the next known function, and lifts. It recovers 1,215
functions, 221 KB; every function found before is unchanged.

Known code grows from 3,287,248 to 3,508,108 bytes, so percentages fall for the
same work. In bytes: 9,025 functions lift and verify (7,915 before), 5,241
derive (4,450), VERIFIED is 551,268 bytes (531,816), 15.71%; MACHINE-CHECKED
69.14%. Undecompiled code falls to 1.50%.

## Two structuring bugs that verification missed

Running natively, the game's software double multiplication (`__muldf3`,
0x12E370) looped forever on 4915200.0 x 2^-32, though every form of it had been
verified - the random test inputs never reached the path. Run on those inputs,
the plain lifted form was right and both structured forms with saves elided
looped. Two bugs:

* Eliding callee-saved saves dropped every load of a saved register from the
  stack - but `ld $s3, 0x10($sp)` there loads data, a fraction a callee stored.
  Only the prologue's own save slots are elided now, and a function that
  writes a saved register it never saves elides nothing.
* Folding single-use temporaries read a conditional move (`if (v1 != 0) s1 =
  v0;`) as a definite assignment, so `s1 = s1 << 1` looked dead after its one
  use and moved into the conditional branch. A conditional assignment now reads
  its target and does not kill it.

641 functions' structured forms changed and were verified again. Random inputs
proved too weak to catch either; the next safeguard is to compare each
structured form with its plain lifted form - verified against the interpreter,
and mechanical - natively, over thousands of inputs.

## The IOP over SIF

The host answers the SDK's SIF commands as the IOP would (engine/host/src/
iop.cpp): the EE's receive buffer from command 0x80000000, RPC bind and call
answered with RPC-end packets in that buffer and the SIF0 DMA interrupt, run by
the scheduler. Burnout 3 binds the CD/DVD file system's servers (0x80000592,
0x80000593, 0x8000059C) and file I/O (0x80000001); none has a host
implementation yet, so their calls are answered with zeros and noted.
`sceGsSyncPath`, which polls VU1's status, is replaced by name (sdk.cpp).
Boot now reaches 665 functions, six threads and its first GIF and VIF1
transfers.

Boot then stopped at 0x1F1778, which polls VPU-STAT (`cfc2 vi29`) to wait for
VU1. No VU microprogram runs in either model, so both units are always idle
and VPU-STAT reads 0 (vu0.py, runtime vu0.cpp). The cold lift that followed
verified the two functions that read it: 9,045 lifted and verified, 69.63%.

The SDK's two RPC entry points are now named from their call sites, with no
signature needed (`naming.rpc_names`). `sceSifCallRpc` (0x10F1E8) is the target
of 121 call sites that set all eight argument registers; the next most is 40.
`sceSifBindRpc` (0x10F018) is passed 13 distinct SDK server IDs in `$a1`. The
same rule applies to any game linked against the SDK. Naming also stopped
counting the lifter's `F_xxxxxxxx` placeholders as names. They made up 7,245 of
the "status table" names and hid every later source of evidence. The honest
count is 128 named functions.

## Boot: the DMA scheduler, interrupts and the IOP modules

Each wait the boot ran into came from hardware or kernel behaviour the host
didn't model yet. They are listed in the order boot reached them.

* **PATH3 handover.** 0x1F1778 is EA's DMA scheduler, run directly and as the
  DMAC handler for VIF1 and GIF. It masks PATH3 by storing MSKPATH3 into the
  VIF1 FIFO, then waits for GIF_STAT.M3P. It starts the GIF chain and waits for
  GIF_STAT.FQC to show the GIF holding data before letting VIF1 go. The host now
  decodes VIFcodes from FIFO stores and VIF1 DMA (vif.cpp). It walks source
  chains (dma.cpp), and holds GIF data in the FIFO while PATH3 is masked.
* **DMA-completion interrupts.** The scheduler counts transfers in flight
  (`$gp-0x5E40`). It increments the count *after* starting them, so the
  interrupt can't run inside the store to CHCR. Interrupts are posted and then
  delivered at the running thread's next loop head (a runtime hook,
  `lifted::loop_head_hook`) or between threads. Handlers run with the program's
  `$gp`. D_STAT now clears and toggles when written, as the hardware's does.
* **Real-time vblank.** Some queue entries pause the DMA queue until the next
  frame (`$gp-0x5EA1`), and the vblank handler (0x1F1CE8) resumes it while
  thread 1 busy-waits. Vblank is now raised 60 times a second, as the display
  does. Runs with `--budget` stay deterministic.
* **iWakeupThread is service -0x34, not -0x33.** The vblank handler wakes the
  sleeping threads with GetThreadId (-47) then iWakeupThread (-52).
* **loadfile.** `sceSifLoadModule` asks for the module's version (procedure
  0xFF) and refuses to load anything unless it is "2800" (SDK 2.8). Loading now
  succeeds without running anything; the modules' services are the host's. The
  game loads these modules: SIO2MAN, SIO2D, DBCMAN, DS2O, MC2_S1, LIBSD, RWA,
  B3ROUTE, GTFSCDVD, USBD, LGAUD and LGDEVW, then the network stack (INET,
  NETCNF, INETCTL, MSIFRPC, LIBNET, NETCNFIF, IFCONFIG, EZNETCNF, EZNETCTL, DEV9,
  SMAP, DRTYSCKF).
* **DECI2.** The SDK's TTY output goes over DECI2 and polls until the debugger
  answers. A retail console has no debugger, so `Deci2Call` requests now fail
  and the writer gives up. `kputs` text is logged as `[guest]` lines. The first
  one: `libdbc: Module version mismatch [libdbc.a = 3.0, dbcman.irx = 0.0]`.
* **dbcman** (0x80001300) reports version 3.0 through procedure 0x80001363.
* **lgDev** (0x046D046D, Logitech's USB vendor ID) is the wheel driver from the
  game's disc, so its server lives in the port (port/iop_servers.cpp). It
  reports version 0x010B1B00 through procedure 12, and enumeration (procedure 1)
  fails at every index because no wheel is attached.

Boot now runs 1,050 distinct functions and reaches the iopheap server.

## RenderWare Audio runs on an IOP interpreter

The game's sound driver, RWA.IRX (RenderWare Audio), talks to its EE library
over raw SIF commands rather than RPC:

* **EE → IOP.** User command 0 carries 0x30-byte records: +0x10 type, then
  arguments, plus a chunked data transfer. The EE side is 0x2AEFC0.
* **IOP → EE.** User command 1, handled at 0x2AF340, which feeds the receiver
  thread 0x2AF2A0. Types 9–16 are the library's own (9 is the handshake,
  version 0x115). The rest go to the game's dispatcher 0x2B3B50: type 0x1A
  answers command 0x11, and type 0x1B (two values) answers command 0x13.

It has about 80 command types and drives the SPU2 through libsd, so
reimplementing it would mean rewriting a sound engine. Instead the host now
*runs* it (engine/host/src/iop_vm.cpp). An R3000 interpreter over IOP RAM loads
the IRX from the disc, relocates it, and binds its imports to the host's IOP
libraries (iop_libs.cpp). Those libraries are threads, semaphores, event flags,
alarms, sysmem, sysclib, sifcmd/sifman and libsd. The export numbers come from
ps2sdk's export tables (engine/data/iop_exports.csv) and the error codes from
its kerr.h. The port opts modules in by name
(`ps2host::iop::RunModule("RWA.IRX")`).

What it took to get the conversation going:

* **IOP heap.** Command 0x49 hands RWA a block the EE allocated through the
  iopheap server (0x80000003). That server now allocates from the
  interpreter's heap (procedure 4 = alloc {size, mode, address}, 2 = free).
  With zeros, RWA cleared 8 KB at IOP address 0.
* **Module start routines run as IOP threads**, as modload runs them; RWA's
  asks for its own thread id.
* **`sceSifSetCmdBuffer` sets a module's handler table**, not a packet buffer.
  The host keeps handlers itself and gives packets a buffer of its own.
* **libsd IO-mode transfers** (`SD_TRANS_MODE_IO`, 8) raise no transfer
  interrupt. RWA's mixer threads woke on a spurious one and called a null
  callback.
* **Gap recovery had split the EE dispatcher 0x2B3B50**: its case bodies from
  0x2B3CE0 on became a "function" of their own. Run-time discovery translates
  them as tail calls, which works natively; the analysis should still learn
  that branches past a function's end extend it.

The EE now sends RWA commands 1, 2, 0x11, 0x13, 0x15, 0x30, 0x49 and 0x4E, and
RWA's replies reach the game. Sound is not played yet: libsd keeps the SPU2's
state and SPU RAM, and fires transfer interrupts at the hardware's rate, but
nothing is mixed. Boot runs 1,385 distinct functions.

## From the frame loop to the loading screen

* **Scratchpad DMA.** Each frame the game builds its GIF/VIF packets in
  scratchpad and copies them to main memory with SPR_FROM (to 0x7FD110). VIF1's
  chain reads them from there. Until SPR_FROM copied data, every DIRECT packet
  was zeros (8.6 million empty GIF tags).
* **A software GS** (engine/host/src/gs*.cpp) draws what the GIF delivers.
  During the frame loop, VIF1 carries only MSKPATH3 and DIRECT: no VU1
  microprograms are involved yet. With `--frames DIR` the port writes the
  displayed picture as BMPs. **The first one shows Burnout 3's LOADING
  screen.**
* **GTFS file loads** are EA's own file system: RPC 0x00475453, procedure 3 =
  open by name, 5/4 = read into an EE address. The data comes back through
  `sceSifSetDmaIntr`, a DMA plus a completion callback, which needed
  implementing. The first files are `Data\Global.txd`, `Data\GlobalUs.bin`,
  `Data\HeadUs.bin`, `pveh\vlist.bin`, `Tracks\tlist.bin` and
  `Data\PrgData.bin`; the copy in memory matches the disc, with the game's
  pointer fix-ups applied.
* **RenderWare Audio streams by the SPU2's clock.** After the files, loading
  waits on RWA's sound-bank stream. Its transfer queue (0x4EB8, consumer
  0x4A94) runs off the SPU2 interrupt: RWA sets core 0's IRQA to 0x800 and
  enables the interrupt. 0x800 lies in the SPU2's memory streams, rings of 256
  samples in SPU RAM 0x0000-0x27FF that the hardware writes and reads one sample
  per 48 kHz tick, so the interrupt is a 5.3 ms clock. (B3ROUTE's
  "Meminterrupt" addresses are the same idea.) The host now models the SPU2's
  voices, loop flags, ENDX and that interrupt (spu2.cpp). Sound is not mixed
  yet.

## The loading screen's wait, and a third structuring bug

Loading stopped at stage 10 of the frontend sequence (0x131480): it waits for
the EA Trax music player (0x3FB0F0), which polls the stream's state (0x3865A0
reads `obj+0x24C` through 0x2A2C80) for 5. The state stayed 4. Following it
back with `--watch` and `--watch-write`:

* 0x2A6470 caches the state at +0x148 of the sound object (0x1EEC8C4) from
  0x2A5BA0, which combines the object's own state (+0x8), a property-3 getter
  on its stream (`[[0x1EEF840+0x20]+0x28]`, 2 -> 3 -> 4 -> 5) and 0x2A5D40.
* 0x2A5D40, in mode 3 (+0x38), walks the object's child list and asks each
  child's property 0 (0x2AB1D0 -> 0x2AD600) for 5. Every one of them reached 5,
  and RWA's IOP side did its part, yet 0x2A5D40 returned 0.

The analysis cut 0x2A5D40 short at 0x2A5DA0: a `daddu $s3, $zero, $zero` that
nothing reaches (dead code after a `b`) ends the function's contiguous run, so
its branches to 0x2A5DB8 and on become tail transfers to code found at run
time. The plain lifted form handles that. The structured form did not: the
function sets `$s3 = 5` and then `beq $v1, $s3` out of the function, to code
that returns `$s3`. Folding saw the comparison as the 5's only use, since its
liveness modelled a fall-through out of the function but not a *taken branch*
out of it, and folded the constant into the condition, losing the store.
liveness.py now counts every synchronised register as live after a branch that
leaves the function; engine/tests/test_liveness.py has the case.

Verification missed it for a reason worth fixing too. The equivalence harness
filled memory with hashes of addresses, so `ReadS32(a0 + 8) == 5` was never
true and the branch that needed the store never ran. Half its cases now read
memory, registers or both as small values (aligned words 0-8).

With the music stream at 5, loading ran on to stage 23 and stopped on a
store to 0x37943844. 0x00228400 returned with its caller's `$s0` replaced by
0x37800000 - a float, 2^-16. Its callee 0x002284B0, a VU0 routine the
interpreter cannot test (it loops past the step limit), was emitted in a
structured form with its saves elided, which keeps `$s0` in a local and
trusts the stack restore to be bookkeeping. But `qmfc2 $s0, $vf1` writes `$s0`
through the context (Cop2Move), past the local, so the value reached the
caller. Two fixes:

* A function whose saved registers are reached through the context - a COP2
  move of one, or a 128-bit write (`lq`, MMI) setting its upper half - keeps
  its saves (structure.py `_save_slots`).
* The structured forms of the 2,171 functions the interpreter cannot check
  were emitted unchecked. Each now has to behave as its plain lifted form in
  the native equivalence check, or the plain form is emitted
  (lift.py `structure_unverified`).

### Functions in pieces

Counting the lift's tail transfers that land somewhere other than a function
start: 2,377 functions do it, to 2,544 addresses. Most are not dead code. The
compiler places cold blocks out of line, after the hot code, and the
out-of-line blocks of neighbouring functions are interleaved (0x003FCB58-
0x003FCBC4 holds pieces branching back into 0x003FC930, 0x003FCABC and on).
The analysis models a function as one contiguous range, so those pieces
become functions of their own ("recovered: adjacent") reached by tail
transfer. Running and verifying are unaffected - a transfer runs the piece
with the same registers and stack - but structuring and reading see
fragments. Giving a function every block its traversal reaches, wherever it
lies, is the fix the analysis needs.

A prototype of that (following branches but not `j` from each start, and
stopping at called functions) gives 2,388 functions 710 KB of chunks, and
shows the bigger cause: many analysis "functions" are pieces of one. Switch
statements jump through tables (`lui`/`addiu` the table, `sll`, `addu`, `lw`,
`jr`), and the pointer harvest takes every table entry for a function - the
case at 0x0019A9A8 of the switch in 0x0019A950 is `func_0019A9A8`. So
0x00100B90-0x00100EA0 is four "functions" branching into one another.
Recognising jump tables, and treating their targets as labels of the
function that jumps through them, comes before chunks.

The analysis now does (analysis.py `jump_table`): 426 switch tables, 1,785
case "functions" dropped, 219 functions grown to hold their cases - 9,360
functions where there were 11,247. 0x0019A950 runs to 0x0019B37C with its
25 cases inside. The out-of-line blocks remain to be joined to their
functions.

## From the loading screen to the frontend

With the structuring fixes, loading finishes - stage 0x17 of 0x00131480 is
its last, "done" - and the frontend's frame loop runs. Each step past it was
one missing piece of the machine:

* **Alarms.** The frame loop paces itself by sleeping on an alarm each frame
  (0x00131B90 -> 0x00237050: SetAlarm, then SleepThread; the callback wakes
  the thread). The SDK patches the kernel's alarm calls with code that runs
  on timer 3 through the kernel's own interrupt table (SetSyscall slot 0x12C),
  so the host keeps SetAlarm and its relatives as its own, timed in horizontal
  blanks on the host clock. The game's own alarm library runs on timer 2's
  compare and overflow interrupts, so the EE timers are modelled too
  (timers.cpp). Idle, the host now waits for the next event instead of
  running frames at once.
* **The first menu**: "Burnout 3 supports 480p (Progressive Scan) video...".
* **Input.** libpad2's DualShock 2 report is the button bits, then the sticks
  (right X, Y, left X, Y), then twelve pressures; the sticks were reported at
  0, pushed fully up-left, and the menu never saw a press. `--press
  BUTTON@FRAME` answers menus in a scripted run.
* **The memory card.** libmc2 opens its slot (port 2) through dbcman, like a
  pad; dbcman now opens a socket on any port and reports what is plugged in -
  nothing but the pad. The game says no memory card is inserted and offers to
  start without saving.
* **Movies.** Then it plays a movie through libmpeg, which waits on the IPU
  (IPU_CTRL read 274 million times). Until the IPU is modelled,
  sceMpegGetPicture marks a movie ended at once.

GetOsdConfigParam and SetOsdConfigParam (0x4B and 0x4A) were swapped in the
host's kernel; the console now reports English, 4:3.

With functions whole (their out-of-line pieces joined), a run needs no new
translation at all on the way to the main menu. In order, scripted with
`--press up@1200 --press cross@1350 --press start@1900 --press down@2300
--press cross@2450 --press cross@3000 --press cross@3400 --press up@3800
--press cross@3950`: no memory card -> start anyway; the title screen ("A
Criterion Games Production...") -> Start; "Burnout 3 requires a Profile" ->
New Profile; the name entry (BURNOUT) -> Done; save the profile? -> No;
autosave disabled? -> Yes; the main menu, "SELECT OPTION", with its ticker
("RACE AND CRASH AROUND THE WORLD"). VU1 has still run nothing.

Open there: the main menu's options and the name-entry keyboard's letters are
drawn - textured triangle strips from the frontend font at 0x3B60 - with
vertex colour and alpha 0, so they do not show. The ticker, the same font and
primitive type, has colour 0x16242D80. Most likely a fade that does not
advance; what its value waits on is the next question.

## Memory cards: libmc2

The game links libmc2 ("PsIIlibmc2  2811") and ships its IOP side,
MC2_S1.IRX, with MCMAN, MCSERV and SIO2MAN. MC2_S1 drives the card itself -
it imports sio2man, sio2d, secrman (MagicGate authentication) and dbcman -
so running it would mean emulating SIO2 and the card's protocol. The host
will answer libmc2's RPC instead, over a directory standing for the card; the
protocol is read from the game's own libmc2 when the frontend first uses it:
it lives at 0x00241000-0x0024B000, binds server 0x80001101 at 0x00249A0C, and
calls it from 0x00249B48, 0x00249BD8 and 0x0024AAE8.

## Open questions

* Which compiler and flags produced this binary? Unresolved, and it blocks all
  byte-matching work.
* Where exactly does code end and read-only data begin? The 81% figure is a
  lower bound - the traversal only marks bytes it can prove are reachable.
* What is the `VDB.XML` container format?
* How is the VU1 microcode stored and uploaded? 703 functions touch VU0 macro
  mode, but the VU1 microprograms are a separate problem and are not MIPS code.
