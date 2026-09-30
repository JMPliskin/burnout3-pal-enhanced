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
| Functions discovered | 7,423 |
| - proven by a direct `jal` | 5,676 |
| - found by prologue scan (indirect/vtable targets) | 1,746 |
| Direct call sites | 40,430 |
| Image proven to be code | 3,300,392 bytes (81.0%) |
| Remaining (rodata/data/padding) | 772,696 bytes |
| Functions using VU0 macro mode | 703 |
| Strings | 5,556 |
| Address cross-references | 8,171 |
| Pointer tables (vtables / jump tables) | 95 |

Median function is 200 bytes; the largest is 77,428 bytes, which is almost
certainly several functions the analysis has not yet separated rather than one
genuine monster.

The largest non-code spans - `0x004856C8..0x004D3E00` (321 KB) and
`0x00469D90..0x004843D0` (108 KB) - are the main read-only data blocks.

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

## Open questions

* Which compiler and flags produced this binary? Unresolved, and it blocks all
  byte-matching work.
* Where exactly does code end and read-only data begin? The 81% figure is a
  lower bound - the traversal only marks bytes it can prove are reachable.
* What is the `VDB.XML` container format?
* How is the VU1 microcode stored and uploaded? 703 functions touch VU0 macro
  mode, but the VU1 microprograms are a separate problem and are not MIPS code.
