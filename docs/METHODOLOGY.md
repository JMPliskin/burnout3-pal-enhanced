# Methodology

## Progress tiers

Every discovered function sits in exactly one tier, recorded in
`config/functions.csv`. The tiers exist because "decompiled" is not a single
thing, and collapsing them into one percentage is how decompilation projects end
up lying to themselves.

### `undecompiled`

The analysis pass found the function and knows its address range. Nothing else
is known. Every function starts here.

### `analyzed`

Someone has read the disassembly and written down what the function does - its
argument count, the structure offsets it touches, its role. No source exists
yet. This tier captures understanding, which is genuinely valuable and is often
the hard part, but it is not code.

### `identified`

C++ source exists and is believed to be behaviourally correct: it should do what
the original does. It has **not** been proven to compile to the same machine
code. Source at this tier is a hypothesis.

### `matched`

The source recompiles to byte-identical machine code against the original
image. This is objective and machine-checkable - no judgement involved.

**Only `matched` is real progress.** The headline figure in
[PROGRESS.md](PROGRESS.md) reports matched bytes and nothing else.

## Why "matched" is the bar

It is easy to produce C that looks like a plausible decompilation of a function.
It is easy to produce thousands of such functions automatically. None of it is
worth anything, for a concrete reason: without byte-verification there is no way
to distinguish source that is correct from source that is subtly wrong, and
subtly-wrong source in a physics or rendering path produces a game that
misbehaves in ways nobody can trace back.

A matching decompilation is self-proving. If the bytes agree, the source is
right, and you can then refactor it into readable code while re-checking that
the bytes still agree.

## The per-function workflow

1. **Read the disassembly.** `python tools/disasm.py build/<db>.json --addr 0x...`
   The listing resolves branch targets to local labels, names called functions,
   and annotates materialised addresses with the strings or tables they point
   at.
2. **Recover the signature.** Which of `$a0..$a3` are read before being written?
   Is `$v0` set on return? Are `$f12..` used, indicating float arguments?
3. **Recover the types.** Structure layout has to be inferred from the load and
   store offsets. Record the layout in a header as you go - the same structure
   will show up in dozens of other functions, and this is where most of the
   lasting value accumulates.
4. **Write candidate source.**
5. **Compile and diff.** Requires the matching toolchain (see below). Adjust the
   source until the assembly agrees.
6. **Promote the tier** in `config/functions.csv` and commit.

## The toolchain

The compiler is known. The executable's `.comment` section names it outright:

```
MW MIPS C Compiler (2.4.1.01)   PlayStation2
```

That is **Metrowerks CodeWarrior**, MIPS C compiler version 2.4.1.01, targeting
PlayStation 2. This removes the guesswork that normally consumes the opening
weeks of a decompilation project - there is no need to fingerprint code
generation against candidate compilers, because the build stamped its own
identity into the binary.

Two things still have to be established:

1. **Obtaining `mwccmips` 2.4.1.01.** CodeWarrior for PlayStation 2 is abandoned
   proprietary software - Metrowerks was absorbed into Freescale and then NXP,
   and the PS2 product has not been sold for roughly two decades. This is a
   practical sourcing problem, not a technical one, and it is now the single
   thing standing between this project and verified progress.
2. **Recovering the optimisation flags.** Once the compiler is available, the
   kernel stubs in `src/kernel/syscalls.cpp` are the calibration targets: they
   are short enough that their source is unambiguous, so any flag combination
   that fails to reproduce them exactly is wrong. Work up from there.

Note that matching Metrowerks output with GCC is not realistic. The two
differ systematically in register allocation, instruction scheduling and
delay-slot filling, so a GCC-based attempt would stall in the high-90s and never
close. If `mwccmips` cannot be obtained, the honest response is to switch
deliberately to a **functionally equivalent** reconstruction and stop reporting
a match percentage at all, rather than chase a target that cannot be reached.

Until the compiler is in place, **no function can reach `matched`**, and the
verified-complete figure stays at zero no matter how much source is written.

## Verifying a candidate

`tools/matchdiff.py` performs the comparison, scoring two ways:

* **exact** - bytes identical. This is the bar for `matched`.
* **semantic** - identical opcodes in the same order, ignoring which registers
  were chosen. A high semantic score with a lower exact score means the logic is
  right and only register allocation differs, which is the normal state of a
  function that is nearly finished.

```sh
python tools/matchdiff.py build/slus_210_50.json 0x0010BE40 --asm candidate.txt
python tools/matchdiff.py build/slus_210_50.json 0x0010BE40 --object build/syscalls.o --symbol SignalSema
```

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
