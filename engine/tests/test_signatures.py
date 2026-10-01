"""Tests for cross-game signatures, evidence-based naming, and address pairing.

Each test hand-assembles the code it needs, so what is being claimed - this field
is relocation, that one is not - is visible in the test itself.
"""

import os
import struct
import sys
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path[:0] = [ROOT, os.path.join(ROOT, "tools")]

from ps2decomp import naming, signatures
from ps2decomp.symbols import SymbolRecovery

BASE = 0x00100000
NOP = 0
JR_RA = 0x03E00008
SYSCALL = 0x0000000C


def lui(rt, imm): return (0x0F << 26) | (rt << 16) | (imm & 0xFFFF)
def addiu(rt, rs, imm): return (0x09 << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)
def lw(rt, rs, imm): return (0x23 << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)
def jal(target): return (3 << 26) | ((target >> 2) & 0x03FFFFFF)
def move(rd, rs): return (rs << 21) | (rd << 11) | 0x25          # or rd, rs, $zero


def image(words):
    return struct.pack("<%dI" % len(words), *words)


def sig(words):
    return signatures.signature(image(words), BASE, BASE, BASE + 4 * len(words))


class TestSignatures(unittest.TestCase):
    def test_relinked_code_has_the_same_signature(self):
        """Call target, both halves of an absolute address and a $gp offset all move
        when the same code is linked into another executable."""
        def body(call, high, low, small):
            return [lui(4, high), jal(call), addiu(4, 4, low), lw(2, 28, small), JR_RA, NOP]
        self.assertEqual(sig(body(0x00123450, 0x0048, 0x1230, -0x7F00)),
                         sig(body(0x00298760, 0x0051, -0x0400, -0x6A10)))

    def test_a_field_offset_is_not_relocation(self):
        self.assertNotEqual(sig([lw(2, 4, 8), JR_RA, NOP]), sig([lw(2, 4, 12), JR_RA, NOP]))

    def test_a_register_overwritten_after_lui_is_no_longer_an_address(self):
        """`lui $a0` then `move $a0, $s0`: the load is a field of *$s0's* object."""
        def body(offset):
            return [lui(4, 0x48), move(4, 16), lw(2, 4, offset), JR_RA, NOP]
        self.assertNotEqual(sig(body(8)), sig(body(12)))

    def test_instruction_count_is_part_of_the_signature(self):
        self.assertTrue(sig([JR_RA, NOP]).startswith("2:"))


class TestLibrary(unittest.TestCase):
    def test_conflicting_names_name_nothing(self):
        library = signatures.Library()
        library.add("8:aa", "sceFoo", "GAME_A")
        self.assertEqual(library.name_for("8:aa"), "sceFoo")
        library.add("8:aa", "sceBar", "GAME_B")
        self.assertIsNone(library.name_for("8:aa"))

    def test_withdrawing_a_game_removes_only_its_names(self):
        library = signatures.Library()
        library.add("8:aa", "sceFoo", "GAME_A")
        library.add("8:aa", "sceBar", "GAME_B")
        library.add("9:bb", "sceOnlyA", "GAME_A")
        library.withdraw("GAME_A")
        self.assertEqual(library.name_for("8:aa"), "sceBar")
        self.assertNotIn("9:bb", library.entries)

    def test_save_and_load_round_trip(self):
        library = signatures.Library()
        library.add("8:aa", "sceFoo", "GAME_A")
        path = os.path.join(tempfile.mkdtemp(), "library.json")
        library.save(path)
        self.assertEqual(signatures.Library.load(path).name_for("8:aa"), "sceFoo")


class TestNaming(unittest.TestCase):
    def test_kernel_stubs_and_their_service_numbers(self):
        words = [addiu(3, 0, 0x42), SYSCALL, JR_RA, NOP,
                 addiu(3, 0, -0x77), SYSCALL, JR_RA, NOP,
                 addiu(3, 0, 0x42), NOP, SYSCALL, JR_RA]          # not a stub
        database = {"functions": [{"addr": BASE + 16 * i, "end": BASE + 16 * (i + 1)}
                                  for i in range(3)]}
        stubs = naming.syscall_stubs(database, image(words), BASE)
        self.assertEqual(stubs, {BASE: 0x42, BASE + 16: -0x77})
        table = naming.syscall_table()
        self.assertEqual(table[0x42], "SignalSema")
        self.assertEqual(table[-0x77], "iSifSetDma")

    def _database(self, references):
        """Functions at 0x1000-spaced addresses, each referencing the given strings."""
        strings, xrefs, functions = [], [], []
        for index, texts in enumerate(references):
            start = BASE + 0x1000 * index
            functions.append({"addr": start, "end": start + 0x100})
            for offset, text in enumerate(texts):
                address = 0x00400000 + len(strings) * 0x40
                strings.append({"addr": address, "text": text})
                xrefs.append({"from": start + 4 * offset, "to": address})
        return {"functions": functions, "strings": strings, "xrefs": xrefs}

    def test_a_self_identifying_message_names_its_function(self):
        database = self._database([["sceDbcInitSocket: rpc error\n"]])
        self.assertEqual(naming.diagnostic_names(database)[BASE][0], "sceDbcInitSocket")

    def test_a_name_printed_by_two_functions_names_neither(self):
        database = self._database([["lgDevInit: halted\n"], ["lgDevInit: version %d\n"]])
        self.assertEqual(naming.diagnostic_names(database), {})

    def test_a_function_printing_two_names_gets_neither(self):
        database = self._database([["lgAudGetMixer: bad\n", "lgAudSetMixer: bad\n"]])
        self.assertEqual(naming.diagnostic_names(database), {})

    def test_module_tags_are_not_function_names(self):
        database = self._database([["ProtoMangle: string too long\n"], ["libpad2: error\n"],
                                   ["Connection: lost\n"]])
        self.assertEqual(naming.diagnostic_names(database), {})


class TestAddressPairing(unittest.TestCase):
    STRING = 0x00100100

    def _xrefs(self, words):
        data = bytearray(0x200)
        data[:4 * len(words)] = image(words)
        recovery = SymbolRecovery(bytes(data), BASE)
        recovery.find_xrefs([(BASE, BASE + 4 * len(words))])
        return [(x.from_addr, x.target) for x in recovery.xrefs]

    def test_low_half_in_a_call_delay_slot(self):
        """The usual way to pass a string: `lui $a0` before the call, `addiu $a0` in
        its delay slot."""
        words = [lui(4, 0x0010), jal(0x00100080), addiu(4, 4, 0x0100), JR_RA, NOP]
        self.assertEqual(self._xrefs(words), [(BASE + 8, self.STRING)])

    def test_overwritten_register_is_not_paired(self):
        words = [lui(4, 0x0010), move(4, 16), addiu(4, 4, 0x0100), JR_RA, NOP]
        self.assertEqual(self._xrefs(words), [])

    def test_callee_saved_register_survives_a_call(self):
        words = [lui(16, 0x0010), jal(0x00100080), NOP, addiu(4, 16, 0x0100), JR_RA, NOP]
        self.assertEqual(self._xrefs(words), [(BASE + 12, self.STRING)])

    def test_caller_saved_register_does_not_survive_a_call(self):
        words = [lui(8, 0x0010), jal(0x00100080), NOP, addiu(4, 8, 0x0100), JR_RA, NOP]
        self.assertEqual(self._xrefs(words), [])


if __name__ == "__main__":
    unittest.main()
