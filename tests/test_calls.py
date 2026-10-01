"""Tests for derived functions that call functions which were not derived.

The caller is derived; the callee is called, not inlined - in verification it runs
as the original machine code in the interpreter. What has to hold is ordering:
the callee sees every store made before the call, and loads after the call read
memory as the callee left it.
"""

import os
import struct
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

from b3lib.emu import Machine
from b3lib.paths import Execution, derive_paths
from b3lib.symexec import Load

BASE = 0x00100000
A0, A1, V0, V1, S0, SP, RA = 4, 5, 2, 3, 16, 29, 31


def i_type(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def r_type(rs, rt, rd, funct):
    return (rs << 21) | (rt << 16) | (rd << 11) | funct


def lw(rt, offset, rs): return i_type(0x23, rs, rt, offset)
def sw(rt, offset, rs): return i_type(0x2B, rs, rt, offset)
def ld(rt, offset, rs): return i_type(0x37, rs, rt, offset)
def sd(rt, offset, rs): return i_type(0x3F, rs, rt, offset)
def addiu(rt, rs, imm): return i_type(0x09, rs, rt, imm)
def addu(rd, rs, rt): return r_type(rs, rt, rd, 0x21)
def move(rd, rs): return r_type(rs, 0, rd, 0x2D)
def jal(target): return (3 << 26) | ((target >> 2) & 0x03FFFFFF)


JR_RA = r_type(RA, 0, 0, 0x08)
NOP = 0

# callee(p, n): v0 = *p + n; p[1] = v0; return v0
CALLEE = [lw(V0, 0, A0), addu(V0, V0, A1), sw(V0, 4, A0), JR_RA, NOP]


def caller_words(callee):
    """caller(p): *p = 7; r = callee(p, 5); return r + p[1]"""
    return [
        addiu(SP, SP, -32),
        sd(RA, 16, SP),
        sd(S0, 0, SP),
        move(S0, A0),
        addiu(V1, 0, 7),
        sw(V1, 0, A0),
        jal(callee),
        addiu(A1, 0, 5),                 # delay slot: the second argument
        lw(V1, 4, S0),                   # what the callee wrote
        addu(V0, V0, V1),
        ld(RA, 16, SP),
        ld(S0, 0, SP),
        JR_RA,
        addiu(SP, SP, 32),
    ]


class TestOpaqueCalls(unittest.TestCase):
    def setUp(self):
        callee = BASE
        self.caller = BASE + 4 * len(CALLEE)
        words = CALLEE + caller_words(callee)
        self.image = struct.pack("<%dI" % len(words), *words)
        self.callee = callee
        self.parameters = {callee: (0, 1)}

    def derive(self):
        return derive_paths(self.image, BASE, self.caller,
                            self.caller + 4 * len(caller_words(self.callee)),
                            known={}, parameters=self.parameters)

    def test_call_to_an_underived_function_is_kept_as_a_call(self):
        derivation = self.derive()
        self.assertTrue(derivation.has_calls)
        kinds = [statement[0] for statement in derivation.tree()]
        self.assertEqual(kinds, ["store", "call", "return"])

    def test_without_parameters_the_call_is_still_refused(self):
        """Opaque calls are opt-in: the old behaviour must remain available."""
        from b3lib.symexec import Unsupported

        with self.assertRaises(Unsupported):
            derive_paths(self.image, BASE, self.caller,
                         self.caller + 4 * len(caller_words(self.callee)), known={})

    def test_a_load_after_the_call_reads_the_next_epoch(self):
        derivation = self.derive()
        loads = []
        stack = [derivation.paths[0].expression]
        while stack:
            node = stack.pop()
            if isinstance(node, Load):
                loads.append(node)
            from b3lib.symexec import children
            stack.extend(children(node))
        self.assertEqual([load.epoch for load in loads], [1])

    def test_execution_agrees_with_the_original(self):
        """The callee must see the store made before the call, and the load after
        the call must see what the callee wrote: 7 + 5 = 12, and 12 + 12."""
        derivation = self.derive()
        machine = Machine(BASE, self.image, 0x02000000 - BASE)
        pointer = 0x01800000
        machine.memory.write(pointer, b"\x00" * 16)
        expected = machine.call(self.caller, (pointer,))
        self.assertEqual(expected, 24)

        machine.memory.write(pointer, b"\x00" * 16)
        machine.memory.begin_journal()
        result = derivation.run((pointer,) + (0,) * 15, Execution(machine))
        self.assertEqual(result, 24)
        self.assertEqual(machine.memory.read_int(pointer + 4, 4, False), 12)
        machine.memory.rollback()
        machine.memory.end_journal()

    def test_emitted_statements_call_in_order(self):
        import derive

        derive._SIGNATURES.clear()
        derive._EXTERNS.clear()
        lines = derive.render_body(self.derive())
        text = "\n".join(lines)
        self.assertLess(text.index("Write32"), text.index("func_%08X(" % self.callee))
        self.assertIn("func_%08X(arg0, 5)" % self.callee, text)
        self.assertEqual(derive._EXTERNS, {self.callee: (0, 1)})
        # The result is used, so it is named; the load after the call reads live
        # memory there and needs no capture.
        self.assertIn("const u64 r1 = ", text)
        self.assertTrue(lines[-1].startswith("return r1 + "))


    def test_value_loaded_before_a_call_is_captured_before_it(self):
        """x = *p; r = callee(p, 5); return r + x  - the callee may change *p, so x
        must be read into a local before the call, not after."""
        import derive

        words = CALLEE + [
            addiu(SP, SP, -32),
            sd(RA, 16, SP),
            sd(S0, 0, SP),
            lw(S0, 0, A0),
            jal(BASE),
            addiu(A1, 0, 5),
            addu(V0, V0, S0),
            ld(RA, 16, SP),
            ld(S0, 0, SP),
            JR_RA,
            addiu(SP, SP, 32),
        ]
        image = struct.pack("<%dI" % len(words), *words)
        start = BASE + 4 * len(CALLEE)
        derivation = derive_paths(image, BASE, start, BASE + 4 * len(words), known={},
                                  parameters=self.parameters)
        derive._SIGNATURES.clear()
        derive._EXTERNS.clear()
        lines = derive.render_body(derivation)
        call_line = next(i for i, line in enumerate(lines) if "func_" in line)
        self.assertTrue(lines[0].startswith("const u64 t1 = ReadS32(arg0)"), lines)
        self.assertLess(0, call_line)
        self.assertIn("t1", lines[-1])


if __name__ == "__main__":
    unittest.main()
