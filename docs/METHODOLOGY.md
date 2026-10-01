# Methodology

## Progress tiers

Every discovered function sits in exactly one tier, recorded in
`config/functions.csv`.

### `undecompiled`

The analysis found the function and knows its address range. Nothing else is
known. Every function starts here.

### `analyzed`

Someone has read the disassembly and written down what the function does - its
argument count, the structure offsets it touches, its role. No source yet. This
captures understanding, which is often the hard part, but it is not code.

### `reconstructed`

C++ source exists and is believed to behave correctly. It has **not** been tested
against the original. Source at this tier is a hypothesis.

### `verified`

The reconstruction has been differential-tested against the original machine code
running in the R5900 interpreter, on a corpus that includes edge cases, and they
agreed. This is the project's bar for done.

**Only `verified` counts.** The headline figure in [PROGRESS.md](PROGRESS.md)
reports verified bytes and nothing else.

## Why functional reconstruction, and what it does and does not prove

The original compiler is known - Metrowerks CodeWarrior MIPS 2.4.1.01, named in
the executable's `.comment` section - but it is abandoned proprietary software and
could not be obtained. Without it, byte-matching is impossible: Metrowerks and GCC
differ systematically in register allocation, instruction scheduling and
delay-slot filling, so a GCC attempt would plateau short of a match and never
close.

So the project targets behaviour instead of bytes, and replaces byte-matching with
differential testing:

1. Run the **original machine code** in `tools/b3lib/emu.py`, an R5900 interpreter.
2. Run the reconstruction on the same inputs.
3. Compare.

Be precise about the strength of this. A passing differential test shows the two
agree **on the inputs tested**. That is evidence, not proof - a function whose
behaviour depends on an input the corpus never supplies could still differ. The
harness therefore records how many cases ran, puts edge cases ahead of random
ones, and always reports the case count rather than just "passed".

It is a weaker guarantee than byte-matching, and the right response is to say so
rather than to blur the two. What it does give is an objective, automated check
that fails loudly on a regression - which untested source does not.

## Verifying a reconstruction

`tools/b3lib/difftest.py` provides the harness for hand-written reconstructions, and
`tests/test_reconstruction.py` holds those comparisons, so
`python -m unittest discover -s tests` re-checks every verified function.

`tools/derive.py` does the same job for derived functions, in bulk.

The interpreter covers the integer core, the common loads and stores, 128-bit
`lq`/`sq`, COP1 single precision, and the MMI operations the string routines need -
enough to execute **84.8%** of the 9,163 discovered functions. `$gp` is seeded with
`0x004E8670`, recovered from the startup code, since many functions reach globals
through it.

Anything outside the subset raises `UnsupportedInstruction` rather than being
approximated: an interpreter that quietly guessed would manufacture exactly the
false confidence this exists to remove. The remaining gap is almost entirely VU0
macro mode.

### What is actually compared

Return values are not sufficient. The harness compares **memory effects** too,
because a derivation with the right return value and the wrong store address would
otherwise pass.

Writes are journalled and rolled back rather than restored from a snapshot of a
chosen region. These functions write to globals far outside any such region, and an
earlier version that snapshotted only a scratch area left those changes in place -
which corrupted later cases and made correct derivations look wrong. It could
equally have produced false passes.

Writes into the callee's own stack frame are excluded. The frame is dead the moment
the function returns, and the original spills registers into it constantly while a
derived expression writes none, so counting them would fail every function with a
prologue.

### Automated derivation

Roughly a third of verified functions were not written by hand at all. Small
branch-free and acyclic functions are derived symbolically, differential-tested, and
only then emitted as C++ - see [ARCHITECTURE.md](ARCHITECTURE.md). A derivation that
fails or cannot be executed is dropped rather than emitted with a caveat.

Two honesty rules apply to that output:

* A function counts as `verified` only if the test varied something. A constant or a
  fixed-global read ignores its arguments, so eight argument sets exercise one input
  eight times; those land a tier lower.
* Generated functions keep placeholder `func_XXXXXXXX` names. The behaviour is
  verified but no struct types are recovered yet, and a descriptive name invented now
  would read as a finding later.

### Bugs this process caught

Worth listing, because each argues for the approach. None were visible to
inspection, and several were in the *verifier* rather than the reconstruction:

* `beql`/`bnel` nullify their delay slot when not taken; executing it unconditionally
  clobbered a mask register in the SIMD `strlen`.
* Word and doubleword shifts share one mask in the expression model, and the 5-bit
  one turns a shift of 32 into a shift of zero.
* `lui` was not sign-extended into the 64-bit register.
* The verification harness restored only a scratch region, so writes to globals
  persisted across cases.
* Journalling then exposed that stack-frame writes must be excluded.
* Float registers held as host doubles canonicalised NaN payloads, so a `lwc1`/`swc1`
  copy altered the value. Here the derivation was right and the interpreter wrong.
* An overflowing `mul.s` crashed the interpreter outright, because the EE's FPU is
  not IEEE-754: it has no infinities and saturates to the largest representable
  magnitude. A port must reproduce that rather than "fix" it.

## The original toolchain, for the record

The executable's `.comment` section names its compiler outright:

```
MW MIPS C Compiler (2.4.1.01)   PlayStation2
```

Metrowerks CodeWarrior MIPS 2.4.1.01 for PlayStation 2. Normally identifying this
is the opening problem of a decompilation; here the build stamped its own identity
into the binary.

It is nonetheless abandoned proprietary software - Metrowerks went to Freescale and
then NXP, and the PS2 product has not been sold in about two decades - which is
why this project verifies behaviour instead of bytes.

If `mwccmips` 2.4.1.01 is ever obtained, byte-matching becomes available as a
*stronger* check on top of the existing one, and `tools/matchdiff.py` is kept for
exactly that: it compares a compiled object against the original image and scores
it both exactly and semantically.

```sh
python tools/matchdiff.py build/slus_210_50.json 0x0010BE40 --object build/syscalls.o --symbol SignalSema
```

The kernel stubs in `src/kernel/syscalls.cpp` are the natural calibration targets,
being short enough that their source is unambiguous.

## Conventions for reconstructed source

* C++17, since the original is C++ (with RenderWare 3.6 underneath in C).
* Fixed-width types from `include/b3/types.h`.
* Every reconstructed function carries a comment naming the address it came
  from and its current tier.
* Names that are recovered from evidence (a string, an RTTI entry, a RenderWare
  API signature) are used as-is. Names that are inferred get a comment saying so.
  Never silently invent a name that looks authoritative.
* Structure layouts live in headers, with offsets documented, because they are
  the reusable product of the work.
