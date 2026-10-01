"""Lifted C++ against the interpreter, instruction family by instruction family.

Each program is hand-assembled, lifted, compiled once into a library, and run from
the same random states as the interpreter. The whole register file and the memory
written must match. Skipped when no C++ compiler is installed.
"""

import os
import random
import struct
import sys
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path[:0] = [ROOT, os.path.join(ROOT, "tools")]

from ps2decomp import cppcheck, layout, lift, liftcheck
from ps2decomp.emu import Machine

BASE = 0x00100000
JR_RA = 0x03E00008
NOP = 0
ZERO, AT, V0, V1, A0, A1, A2, A3 = 0, 1, 2, 3, 4, 5, 6, 7
T0, T1, T2, T3, T4, T5, T6, T7, S0, T8, SP, RA = 8, 9, 10, 11, 12, 13, 14, 15, 16, 24, 29, 31


def r_type(funct, rs=0, rt=0, rd=0, sa=0, op=0):
    return (op << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct


def i_type(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def branch(op, rs, rt, at, target):
    return i_type(op, rs, rt, (target - at - 4) >> 2)


def cop1(funct, fd, fs, ft):
    return (0x11 << 26) | (0x10 << 21) | (ft << 16) | (fs << 11) | (fd << 6) | funct


def addiu(rt, rs, imm): return i_type(0x09, rs, rt, imm)
def lui(rt, imm): return i_type(0x0F, 0, rt, imm)
def jal(target): return (3 << 26) | ((target >> 2) & 0x03FFFFFF)
def mtc1(rt, fs): return (0x11 << 26) | (4 << 21) | (rt << 16) | (fs << 11)
def mfc1(rt, fs): return (0x11 << 26) | (0 << 21) | (rt << 16) | (fs << 11)


def programs():
    """{name: (offset, words)}; laid out one after another from BASE."""
    out = {}
    at = BASE

    def add(name, words):
        nonlocal at
        out[name] = (at, words)
        at += 4 * len(words)

    add("arithmetic", [
        addiu(V0, A0, 5), r_type(0x21, A0, A1, V1), r_type(0x00, 0, A0, T0, 3),
        r_type(0x03, 0, A1, T1, 2), r_type(0x2A, A0, A1, T2), i_type(0x0B, A0, T3, 100),
        r_type(0x3C, 0, A0, T4, 4), r_type(0x26, A0, A1, T5), lui(T6, 0x8000),
        i_type(0x0D, A2, T7, 0xFFFF), r_type(0x2D, A0, A1, S0), r_type(0x2F, A0, A1, A3),
        r_type(0x04, A2, A0, AT), r_type(0x17, A2, A1, T8),        # sllv, dsrav
        r_type(0x0A, A1, A0, A2), r_type(0x0B, A2, A1, A3),        # movz, movn
        JR_RA, r_type(0x27, A0, A1, T8),                            # nor (delay slot)
    ])
    loop = at
    add("count_bits", [
        r_type(0x25, 0, 0, V0),                                     # or $v0, $zero, $zero
        i_type(0x0C, A0, T0, 1),                                    # andi $t0, $a0, 1
        r_type(0x21, V0, T0, V0),                                   # addu $v0, $v0, $t0
        r_type(0x02, 0, A0, A0, 1),                                 # srl $a0, $a0, 1
        branch(0x05, A0, 0, loop + 16, loop + 4),                   # bne $a0, $zero, loop
        NOP, JR_RA, NOP,
    ])
    add("memory", [
        i_type(0x23, A0, T0, 0), i_type(0x2B, A0, T0, 4), i_type(0x24, A0, T1, 1),
        i_type(0x29, A0, T1, 8), i_type(0x37, A0, T2, 0), i_type(0x3F, A0, T2, 16),
        i_type(0x1E, A0, T3, 0), i_type(0x1F, A0, T3, 32), i_type(0x20, A0, T4, 3),
        JR_RA, i_type(0x27, A0, V0, 0),                             # lwu in the delay slot
    ])
    add("multiply", [
        r_type(0x18, A0, A1), r_type(0x12, rd=V0), r_type(0x10, rd=V1),
        r_type(0x1A, A1, A0), r_type(0x12, rd=T0), r_type(0x10, rd=T1),     # div $a1, $a0
        r_type(0x00, A0, A1, T2, op=0x1C),                          # madd $t2, $a0, $a1
        r_type(0x18, A2, A3, T3, op=0x1C),                          # mult1 $t3, $a2, $a3
        r_type(0x12, rd=T4, op=0x1C),                               # mflo1 $t4
        r_type(0x1B, A1, A2, op=0x1C),                              # divu1 $a1, $a2
        JR_RA, r_type(0x10, rd=T5, op=0x1C),                        # mfhi1 $t5
    ])
    likely = at
    add("likely", [
        branch(0x14, A0, 0, likely, likely + 12),                   # beql $a0, $zero, +12
        addiu(V0, 0, 1),                                            # nullified unless taken
        addiu(V0, V0, 2),
        JR_RA, NOP,
    ])
    callee = at
    add("callee", [JR_RA, r_type(0x21, A0, A0, V0)])               # return a0 + a0
    caller = at
    add("caller", [
        addiu(SP, SP, -16), i_type(0x3F, SP, RA, 0),                # sd $ra, 0($sp)
        jal(callee), addiu(A0, A0, 1),
        i_type(0x37, SP, RA, 0), JR_RA, addiu(SP, SP, 16),
    ])
    fpu = at
    add("float", [
        mtc1(A0, 0), mtc1(A1, 1),
        cop1(0x00, 2, 0, 1), cop1(0x02, 3, 2, 1), cop1(0x03, 4, 3, 0),  # add, mul, div
        cop1(0x34, 0, 0, 1),                                        # c.lt.s $f0, $f1
        cop1(0x04, 5, 0, 12),                                       # sqrt.s $f5, $f12
        cop1(0x24, 6, 3, 0),                                        # cvt.w.s $f6, $f3
        cop1(0x18, 0, 0, 1), cop1(0x1C, 7, 2, 1),                   # adda.s, madd.s
        (0x11 << 26) | (8 << 21) | (1 << 16) | 3,                   # bc1t +3
        NOP, mfc1(V0, 2),
        mfc1(V1, 7), JR_RA, mfc1(T0, 6),
    ])
    mmi = at
    add("mmi", [
        r_type(0x09, A0, A1, T0, 0x0E, op=0x1C),                    # pcpyld $t0, $a0, $a1
        r_type(0x09, T0, A2, T1, 0x12, op=0x1C),                    # pand $t1, $t0, $a2
        r_type(0x29, A2, T0, T2, 0x13, op=0x1C),                    # pnor $t2, $a2, $t0
        r_type(0x29, T0, T1, T3, 0x0E, op=0x1C),                    # pcpyud $t3, $t0, $t1
        r_type(0x08, A0, A1, T4, 0x09, op=0x1C),                    # psubb $t4, $a0, $a1
        JR_RA, r_type(0x29, 0, T0, T5, 0x1B, op=0x1C),              # pcpyh $t5, $t0
    ])
    add("unaligned", [
        i_type(0x22, A0, T0, 3), i_type(0x26, A0, T0, 0),           # lwl/lwr pair
        i_type(0x1A, A0, T1, 7), i_type(0x1B, A0, T1, 0),           # ldl/ldr pair
        i_type(0x22, A0, T2, 5), i_type(0x1B, A0, T3, 3),           # lone halves
        i_type(0x2A, A0, A1, 19), i_type(0x2E, A0, A1, 16),         # swl/swr pair
        i_type(0x2C, A0, A2, 47), i_type(0x2D, A0, A2, 40),         # sdl/sdr pair
        i_type(0x2A, A0, A3, 61), i_type(0x2D, A0, A3, 70),         # lone halves
        JR_RA, NOP,
    ])
    add("mmi_lanes", [
        r_type(0x09, A2, A3, T8, 0x0E, op=0x1C),                    # pcpyld $t8, $a2, $a3
        r_type(0x08, T8, A1, T0, 0x03, op=0x1C),                    # pmaxw $t0, $t8, $a1
        r_type(0x28, T8, A0, T1, 0x03, op=0x1C),                    # pminw $t1, $t8, $a0
        r_type(0x08, T8, A0, T2, 0x01, op=0x1C),                    # psubw $t2, $t8, $a0
        r_type(0x28, T8, A1, T3, 0x10, op=0x1C),                    # padduw $t3, $t8, $a1
        r_type(0x28, A0, T8, T4, 0x18, op=0x1C),                    # paddub $t4, $a0, $t8
        r_type(0x28, 0, T8, T5, 0x01, op=0x1C),                     # pabsw $t5, $t8
        r_type(0x36, 0, T8, T6, 5, op=0x1C),                        # psrlh $t6, $t8, 5
        JR_RA, r_type(0x08, A0, T8, T7, 0x12, op=0x1C),             # pextlw $t7, $a0, $t8
    ])
    add("trap", [r_type(0x0D), JR_RA, NOP])                          # break
    return out


class TestLift(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cppcheck.find_vcvars()
        except cppcheck.CompilerUnavailable as exc:
            raise unittest.SkipTest(str(exc))
        cls.programs = programs()
        words = []
        for _name, (start, body) in sorted(cls.programs.items(), key=lambda kv: kv[1][0]):
            words += body
        cls.image = struct.pack("<%dI" % len(words), *words)
        work = tempfile.mkdtemp()
        source = os.path.join(work, "lifted.cpp")
        with open(source, "w", encoding="utf-8") as handle:
            handle.write('#include "ps2rt/lifted.h"\n\n')
            for _name, (start, body) in cls.programs.items():
                handle.write(lift.lift_function(cls.image, BASE, start, start + 4 * len(body)))
        cls.machine = Machine(BASE, cls.image, 0x02000000 - BASE)
        cls.lifted = liftcheck.LiftedFunctions(liftcheck.build([source], work), cls.machine)

    def check(self, name, cases=40):
        start, _body = self.programs[name]
        rng = random.Random(start)
        scratch = layout.CURRENT.scratch
        for case in range(cases):
            args = [rng.choice([rng.getrandbits(64), rng.getrandbits(8), 0,
                                (1 << 64) - rng.getrandbits(4) - 1])
                    for _ in range(8)]
            if name == "memory":
                args[0] = scratch + 16 * rng.randrange(64)
            if name == "unaligned":
                args[0] = scratch + rng.randrange(1024)
            floats = [struct.unpack("<I", struct.pack("<f", rng.uniform(-100, 100)))[0]
                      for _ in range(8)]
            m = self.machine
            m.memory.end_journal()
            m.memory.write(scratch, bytes(rng.getrandbits(8) for _ in range(2048)))
            m.memory.begin_journal()
            m.call(start, args + floats)
            expected = liftcheck.machine_state(m)
            written = dict(m.memory._journal and {o: m.memory.bytes[o] for o, p in m.memory._journal
                                                  for o in range(o, o + len(p))} or {})
            m.memory.rollback()
            context = liftcheck.entry_context(args + floats)
            self.assertIsNone(self.lifted.run(start, context))
            actual = liftcheck.context_state(context)
            replayed = dict(m.memory._journal and {o: m.memory.bytes[o] for o, p in m.memory._journal
                                                   for o in range(o, o + len(p))} or {})
            m.memory.rollback()
            m.memory.end_journal()
            self.assertEqual(actual, expected, "%s, case %d" % (name, case))
            self.assertEqual(replayed, written, "%s, case %d: memory" % (name, case))

    def test_arithmetic_and_shifts(self):
        self.check("arithmetic")

    def test_loop(self):
        self.check("count_bits")

    def test_loads_and_stores(self):
        self.check("memory")

    def test_multiply_and_divide_on_both_pipelines(self):
        self.check("multiply")

    def test_likely_branch_nullifies_its_delay_slot(self):
        self.check("likely")

    def test_call_with_the_whole_context(self):
        self.check("caller")

    def test_float(self):
        self.check("float")

    def test_128_bit_mmi(self):
        self.check("mmi")

    def test_lane_wise_mmi(self):
        self.check("mmi_lanes")

    def test_unaligned_loads_and_stores(self):
        self.check("unaligned")

    def test_break_abandons_the_run(self):
        start, _body = self.programs["trap"]
        self.assertEqual(self.lifted.run(start, liftcheck.entry_context(())), "trap")


class TestLiftRefusals(unittest.TestCase):
    def test_unsupported_instruction_is_refused(self):
        image = struct.pack("<3I", 0x4B000083, JR_RA, NOP)      # a VU0 macro instruction
        with self.assertRaises(lift.Refused):
            lift.lift_function(image, BASE, BASE, BASE + 12)

    def test_delay_slot_reached_by_a_jump_is_emitted_again(self):
        # 0: beq $a0, $zero, +8 ; 4: addiu $v0, $zero, 1 ; 8: j 4 ; 12: nop
        image = struct.pack("<6I", branch(0x04, A0, 0, BASE, BASE + 16), addiu(V0, 0, 1),
                            (2 << 26) | ((BASE + 4) >> 2), NOP, JR_RA, NOP)
        text = lift.lift_function(image, BASE, BASE, BASE + 24)
        self.assertEqual(text.count("c.r[2] = lifted::Sx32"), 2)


if __name__ == "__main__":
    unittest.main()
