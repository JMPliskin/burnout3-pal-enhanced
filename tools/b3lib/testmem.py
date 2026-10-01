"""Memory contents for differential tests.

The tests run original functions against a scratch region of fake memory, and what
that region holds decides whether a function can be tested at all.

Uniform random bytes were the original choice and turned out to be a poor one. Any
pointer a function loads from them is a wild 32-bit value, so pointer-chasing code
faults on the first dereference, and any loop count it loads is in the billions, so
counted loops exhaust the instruction budget. Measured on the functions whose only
obstacle is a loop, 117 of 330 faulted and 32 ran out of budget - not because the
interpreter lacked anything (it hit no unsupported instruction at all) but because
the inputs made no sense.

No single alternative fixes everything, because a word one function reads as a
pointer another reads as a count. So there are several shapes, and a harness tries
them in turn, keeping the first under which the original executes:

  noise     uniform random words (the old behaviour, still occasionally the only fit)
  mixed     pointers into the region in even words, small integers in odd ones
  small     small integers only, for counts, indices and flags
  pointers  pointers into the region only, for linked structures

Trying all four took the loop-only set from 181 executable functions to 247.

The choice of profile changes only what the test can *reach*, never what it
concludes: the original and the derivation always read the same memory.
"""

from __future__ import annotations

import random
import struct
import zlib

SCRATCH = 0x01800000
SCRATCH_SIZE = 0x20000

# Order matters: the first profile under which a function executes is the one it is
# tested with, so the most generally useful shapes come first.
PROFILES = ("mixed", "noise", "small", "pointers")

# Small integers stay below this so a count read from memory gives a short loop.
SMALL_LIMIT = 16


# Each test case needs memory that *differs* from the previous case's, not memory
# that is unique across the whole run. Building 32K random words in Python for every
# case made the derivation run 25x slower, so buffers come from a small cached pool
# per profile instead; consecutive seeds still select different buffers.
POOL_SIZE = 16
_POOL = {}


def build(kind: str, seed: int) -> bytes:
    """One profile's contents for the scratch region, deterministic in `seed`."""
    key = (kind, seed % POOL_SIZE)
    buffer = _POOL.get(key)
    if buffer is None:
        buffer = _build(kind, key[1])
        _POOL[key] = buffer
    return buffer


def _build(kind: str, seed: int) -> bytes:
    """Fill the scratch region with one profile's contents, deterministically."""
    # zlib.crc32 rather than hash(): Python randomises string hashes per process,
    # which would give different "deterministic" memory on every run and make a
    # failing test impossible to reproduce.
    rng = random.Random((zlib.crc32(kind.encode("ascii")) << 16) ^ seed)
    count = SCRATCH_SIZE // 4

    def pointer() -> int:
        # 16-aligned and clear of the end, so a structure read through it stays in
        # range for a reasonable span of fields.
        return SCRATCH + rng.randrange(0, SCRATCH_SIZE - 0x100, 16)

    if kind == "noise":
        words = [rng.getrandbits(32) for _ in range(count)]
    elif kind == "small":
        words = [rng.randrange(SMALL_LIMIT) for _ in range(count)]
    elif kind == "pointers":
        words = [pointer() for _ in range(count)]
    elif kind == "mixed":
        words = [pointer() if i % 2 == 0 else rng.randrange(SMALL_LIMIT)
                 for i in range(count)]
    else:
        raise ValueError("unknown memory profile %r" % kind)
    return struct.pack("<%dI" % count, *words)


def arguments(rng: random.Random) -> tuple:
    """A full register argument vector: eight integers, then eight floats.

    The integers are 16-aligned pointers into the first half of scratch. The first
    four are drawn exactly as they were when only four arguments existed, so the
    cases every earlier function was verified on are unchanged. The floats are bit
    patterns of ordinary values - a mix of small exact ones and random magnitudes -
    rather than random bits, which would be mostly NaNs and denormals.
    """
    integers = [SCRATCH + rng.randrange(0, SCRATCH_SIZE // 2, 16) for _ in range(8)]
    return tuple(integers) + tuple(float_bits(rng) for _ in range(8))


FLOAT_CASES = (0.0, 1.0, -1.0, 0.5, 2.0, -0.25, 100.0, 3.5)


def float_bits(rng: random.Random) -> int:
    """The bit pattern of a plausible float argument."""
    if rng.random() < 0.5:
        value = rng.choice(FLOAT_CASES)
    else:
        value = rng.uniform(-1000.0, 1000.0)
    return struct.unpack("<I", struct.pack("<f", value))[0]
