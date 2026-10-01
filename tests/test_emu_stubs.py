"""Tests for the interpreter's testing stubs and the FPU accumulator.

The stubs let original *callees* run during a test. Their behaviour is a stated
assumption, not a reconstruction, so these tests pin exactly what was assumed.
"""

import os
import struct
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

from b3lib import testmem
from b3lib.emu import EmulationError, Machine

BASE = 0x00100000
JR_RA = 0x03E00008


def machine(words):
    image = struct.pack("<%dI" % len(words), *words)
    return Machine(BASE, image, 0x02000000 - BASE)


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


class TestStubs(unittest.TestCase):
    def test_syscall_returns_zero(self):
        # addiu $v0, $zero, 7 ; syscall ; jr $ra ; nop
        m = machine([0x24020007, 0x0000000C, JR_RA, 0])
        self.assertEqual(m.call(BASE, ()), 0)

    def test_mfc0_reads_zero(self):
        # addiu $v0, $zero, 7 ; mfc0 $v0, Count ; jr $ra ; nop
        m = machine([0x24020007, 0x40024800, JR_RA, 0])
        self.assertEqual(m.call(BASE, ()), 0)

    def test_trap_fires_only_when_its_condition_holds(self):
        # tge $a1, $a0 ; jr $ra ; nop
        m = machine([0x00A40030, JR_RA, 0])
        m.call(BASE, (5, 4))             # 4 >= 5 is false: no trap
        with self.assertRaises(EmulationError):
            m.call(BASE, (5, 6))         # 6 >= 5: trap


class TestAccumulator(unittest.TestCase):
    def test_madd_adds_the_product_to_the_accumulator(self):
        # adda.s $f0, $f2 ; madd.s $f4, $f0, $f2 ; jr $ra ; mfc1 $v0, $f4
        adda = 0x46020018
        madd = 0x4602011C                 # madd.s $f4, $f0, $f2
        mfc1 = 0x44022000                 # mfc1 $v0, $f4
        m = machine([adda, madd, JR_RA, mfc1])
        m.fpr[0], m.fpr[2] = bits(3.0), bits(2.0)
        # ACC = 3 + 2 = 5; f4 = ACC + 3 * 2 = 11
        self.assertEqual(m.call(BASE, ()) & 0xFFFFFFFF, bits(11.0))


class TestGlobalPointerSeeding(unittest.TestCase):
    def test_only_null_globals_are_seeded_and_into_scratch(self):
        m = machine([0] * 64)
        null_global, set_global = BASE + 0x40, BASE + 0x44
        m.memory.write_int(set_global, 0x12345678, 4)
        seeded = testmem.seed_global_pointers(m.memory, {null_global, set_global})
        self.assertEqual(seeded, 1)
        pointer = m.memory.read_int(null_global, 4, False)
        self.assertTrue(testmem.SCRATCH <= pointer < testmem.SCRATCH + testmem.SCRATCH_SIZE)
        self.assertEqual(m.memory.read_int(set_global, 4, False), 0x12345678)


if __name__ == "__main__":
    unittest.main()
