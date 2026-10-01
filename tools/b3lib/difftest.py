"""Differential testing of reconstructed functions against the original code.

This is the project's substitute for byte-matching.  Byte-matching needs the
original Metrowerks compiler, which is unavailable, so a reconstruction cannot be
proven *identical*.  It can, however, be proven to *behave* identically: run the
original machine code in the interpreter, run the reconstruction, and compare.

That distinction matters and is worth being precise about.  A passing differential
test shows the two agree on the inputs tested.  It is evidence, not proof - a
function with input-dependent behaviour the test never exercises can still differ.
So the harness records how many cases ran and insists on edge cases alongside
random ones, and `Report.summary()` always states the case count rather than just
"passed".
"""

from __future__ import annotations

import dataclasses
import random

from .emu import EmulationError, Machine


@dataclasses.dataclass
class Mismatch:
    """One input where the reconstruction and the original disagreed."""

    case: object
    expected: object
    actual: object

    def __str__(self) -> str:
        return "case %r: original gave %r, reconstruction gave %r" % (
            self.case, self.expected, self.actual,
        )


@dataclasses.dataclass
class Report:
    name: str
    address: int
    cases_run: int = 0
    mismatches: list = dataclasses.field(default_factory=list)
    errors: list = dataclasses.field(default_factory=list)

    @property
    def passed(self) -> bool:
        return not self.mismatches and not self.errors and self.cases_run > 0

    def summary(self) -> str:
        state = "VERIFIED" if self.passed else "FAILED"
        detail = "%s  %-34s 0x%08X  %d cases" % (
            state, self.name, self.address, self.cases_run
        )
        if self.mismatches:
            detail += "  %d mismatch(es)" % len(self.mismatches)
        if self.errors:
            detail += "  %d error(s)" % len(self.errors)
        return detail


class StringFunctionTester:
    """Differential-tests a function whose only argument is a C string.

    The string is written into scratch memory and its address passed in $a0.  Each
    case gets a fresh slot so one case cannot contaminate the next through
    leftover bytes - a real hazard, since a missing terminator would otherwise
    read into the previous case's data.
    """

    SLOT_SIZE = 1024

    def __init__(self, machine: Machine, scratch_base: int = 0x01200000) -> None:
        self.machine = machine
        self.scratch_base = scratch_base

    def run(self, name: str, address: int, reference, cases) -> Report:
        report = Report(name=name, address=address)
        for index, case in enumerate(cases):
            payload = case if isinstance(case, bytes) else case.encode("ascii")
            slot = self.scratch_base + index * self.SLOT_SIZE
            if len(payload) + 1 > self.SLOT_SIZE:
                report.errors.append("case %d does not fit a scratch slot" % index)
                continue
            # Clear the slot first so a bug cannot read stale bytes and appear to
            # pass.
            self.machine.memory.write(slot, b"\x00" * self.SLOT_SIZE)
            self.machine.memory.write(slot, payload + b"\x00")
            try:
                actual = self.machine.call(address, (slot,)) & 0xFFFFFFFF
            except EmulationError as exc:
                report.errors.append("case %r: %s" % (payload[:32], exc))
                continue
            expected = reference(payload) & 0xFFFFFFFF
            report.cases_run += 1
            if actual != expected:
                report.mismatches.append(Mismatch(payload, expected, actual))
        return report


def ascii_corpus(count: int = 40, seed: int = 20240501, max_len: int = 48) -> list:
    """Edge cases first, then deterministic random strings.

    The edge cases are the ones that actually catch bugs - empty input, single
    bytes, and lengths either side of the 8- and 16-byte boundaries that the
    SIMD string routines special-case.  The random tail broadens coverage without
    replacing them, and a fixed seed keeps failures reproducible.
    """
    cases = [b"", b"a", b"ab", b"abc"]
    for length in (7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65):
        cases.append(b"x" * length)
    cases += [
        b"Damping",
        b"Spring Coeff",
        b"Score/Boost/Air",
        b"../Export/ValueDB/Score.cfg",
        b"Slip Speed Multipler",
        b"Material Properties/Reflectivity",
        b"~!@#$%^&*()_+-=[]{}|;:,.<>?",
        b"MiXeD CaSe NaMe",
    ]
    rng = random.Random(seed)
    while len(cases) < count:
        length = rng.randint(1, max_len)
        cases.append(bytes(rng.randint(0x20, 0x7E) for _ in range(length)))
    return cases[:count] if count >= len(cases) else cases
