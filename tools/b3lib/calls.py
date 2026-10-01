"""Which arguments each function reads, for calling it without deriving it.

A derived function that calls one that has not been derived passes it the values
in the argument registers. Only the ones the callee actually reads are part of
the call: a register that merely still holds something is not an argument, and
passing it would make every call site's signature different.

Integer arguments come from structs.argument_use. Float arguments ($f12..$f19)
get the same treatment here: a function reads one if an instruction reads the
register before anything writes it, or passes it on to a callee that reads it.
"""

from __future__ import annotations

import collections
import re
import struct

from .r5900 import DISASM
from .structs import argument_use
from .symexec import FLOAT_ARG_BASE, FLOAT_ARG_REGS

_FLOAT_REGISTER = re.compile(r"\$f(\d+)")


def _float_operands(ins):
    """(read, written) float registers of one instruction."""
    m = ins.mnemonic
    names = [int(n) for op in ins.operands for n in _FLOAT_REGISTER.findall(str(op))]
    if not names:
        return set(), set()
    if m in ("swc1", "mfc1") or m.startswith("c."):
        return set(names), set()
    if m in ("lwc1", "mtc1", "ctc1"):
        return set(), set(names)
    if m.startswith(("adda.", "suba.", "mula.", "madda.", "msuba.")):
        return set(names), set()          # these write the accumulator, not an FPR
    # Arithmetic, moves and conversions: the first operand is the destination.
    return set(names[1:]), {names[0]}


def float_argument_use(database, image, base) -> set:
    """(function, vector index) for every float argument a function reads."""
    functions = {f["addr"]: f for f in database["functions"] if f["size"]}
    used = set()
    passes = collections.defaultdict(set)
    for func in functions.values():
        start, end = func["addr"], func["end"]
        original = {reg: FLOAT_ARG_BASE + k for k, reg in enumerate(FLOAT_ARG_REGS)}
        address = start
        while address < end:
            ins = DISASM.decode(struct.unpack_from("<I", image, address - base)[0], address)
            sequence = [ins]
            if ins.has_delay_slot and address + 4 < end:
                sequence.insert(0, DISASM.decode(
                    struct.unpack_from("<I", image, address + 4 - base)[0], address + 4))
            for step in sequence:
                if step.is_call or (step.is_jump and step.mnemonic == "j"):
                    target = step.target
                    if target in functions and not (start <= target < end):
                        for reg, index in original.items():
                            passes[(target, index)].add((start, index))
                    if step.is_call:
                        original = {}          # $f0..$f19 are caller-saved
                    continue
                read, written = _float_operands(step)
                for reg in read:
                    if reg in original:
                        used.add((start, original[reg]))
                for reg in written:
                    original.pop(reg, None)
            address += 8 if len(sequence) == 2 else 4
    work = list(used)
    while work:
        slot = work.pop()
        for caller in passes.get(slot, ()):
            if caller not in used:
                used.add(caller)
                work.append(caller)
    return used


def parameters(database, image, base) -> dict:
    """{function: sorted argument-vector indices it reads}, for every known function.

    A function that reads no argument maps to (), so that "takes nothing" and "not
    a function anyone found" stay distinguishable - a jump into the middle of
    some other code is not a call that can be made with no arguments.
    """
    found = {f["addr"]: set() for f in database["functions"] if f["size"]}
    for function, index in argument_use(database, image, base):
        found.setdefault(function, set()).add(index)
    for function, index in float_argument_use(database, image, base):
        found.setdefault(function, set()).add(index)
    return {function: tuple(sorted(indices)) for function, indices in found.items()}
