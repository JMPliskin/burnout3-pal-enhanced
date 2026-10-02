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

## Open questions

* Which compiler and flags produced this binary? Unresolved, and it blocks all
  byte-matching work.
* Where exactly does code end and read-only data begin? The 81% figure is a
  lower bound - the traversal only marks bytes it can prove are reachable.
* What is the `VDB.XML` container format?
* How is the VU1 microcode stored and uploaded? 703 functions touch VU0 macro
  mode, but the VU1 microprograms are a separate problem and are not MIPS code.
