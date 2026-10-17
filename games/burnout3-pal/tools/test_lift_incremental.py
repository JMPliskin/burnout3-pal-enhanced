"""Synthetic MIPS fixtures only; no PAL execution or compiler required."""
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import lift_incremental as inc


class IncrementalTests(unittest.TestCase):
    def setUp(self):
        build=inc.GAME/'build';build.mkdir(exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=build);self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.out=self.root/'src/lifted';self.out.mkdir(parents=True)
        self.old=self.out/'lifted_000.cpp'
        self.old.write_text('void F_00002000(lifted::Context& c) {\n}\n')
        # A synthetic call with a non-NOP delay slot, then a tail jump and slot.
        self.image=struct.pack('<4I',0x0C000800,0x24040007,0x08000C00,0x24050009)
        self.elf=self.root/'synthetic.elf';self.elf.write_bytes(self.image)
        self.db=self.root/'db.json';self.db.write_text('{}')
        section=SimpleNamespace(addr=0x1000,size=16,is_alloc=True,type=1)
        elf=SimpleNamespace(exec_sections=[section],sections=[section],section_data=lambda s:self.image)
        self.game=SimpleNamespace(build='build',generated='src/generated',elf_path=str(self.elf),database_path=str(self.db),
            path=lambda p:str(self.root/p),load_elf=lambda:elf,
            load_database=lambda:{'functions':[{'addr':0x1000,'end':0x1010,'chunks':[]},{'addr':0x2000,'end':0x2008}]})

    def test_preserves_existing_and_dispatch_sees_one_addition(self):
        before=self.old.read_bytes();path,report=inc.emit_one(self.game,0x1000)
        self.assertEqual(self.old.read_bytes(),before)
        self.assertEqual(inc.port.translated_functions(str(self.out)),[0x1000,0x2000])
        self.assertIn('NOT VERIFIED',path.read_text())
        data=json.loads(report.read_bytes());self.assertEqual(data['existing_functions'],1)
        self.assertEqual(data['status'],'mechanically_lifted_not_verified')
        table=inc.port.dispatch_source(inc.port.translated_functions(str(self.out)),{})
        self.assertEqual(table.count('{0x00001000u, F_00001000'),1)

    def test_duplicate_function_is_rejected_without_writing(self):
        inc.emit_one(self.game,0x1000);before=inc.inventory(self.out)
        with self.assertRaisesRegex(ValueError,'already emitted'):inc.emit_one(self.game,0x1000)
        self.assertEqual(inc.inventory(self.out),before)

    def test_existing_filename_never_replaced(self):
        target=self.out/'incremental_00001000.cpp';target.write_bytes(b'occupied')
        with self.assertRaisesRegex(ValueError,'Output already exists'):inc.emit_one(self.game,0x1000)
        self.assertEqual(target.read_bytes(),b'occupied')

    def test_atomic_publication_collision_has_no_fallback(self):
        with patch.object(inc.os,'link',side_effect=FileExistsError('publication collision')):
            with self.assertRaises(FileExistsError):inc.emit_one(self.game,0x1000)
        self.assertEqual(inc.port.translated_functions(str(self.out)),[0x2000])

    def test_delay_slots_and_tail_transfer_use_original_lifter(self):
        path,report=inc.emit_one(self.game,0x1000);text=path.read_text()
        expected=inc.translator.lift_function(self.image,0x1000,0x1000,0x1010)
        self.assertIn(expected,text)
        self.assertLess(text.index('0x7u'),text.index('lifted::Call(c, 0x00002000u)'))
        self.assertLess(text.index('0x9u'),text.index('lifted::TailCall(c, 0x00003000u'))
        self.assertEqual([x['delay_slot'] for x in json.loads(report.read_bytes())['controls']],['0x00001004','0x0000100C'])

    def test_unknown_address_and_existing_duplicate_inventory_rejected(self):
        with self.assertRaisesRegex(ValueError,'exactly one'):inc.emit_one(self.game,0x4000)
        (self.out/'another.cpp').write_bytes(self.old.read_bytes())
        with self.assertRaisesRegex(ValueError,'Duplicate function'):inc.emit_one(self.game,0x1000)

    def test_previous_outcome_is_provenance_never_verification(self):
        build=self.root/'build';build.mkdir()
        (build/'lift_outcomes.json').write_text(json.dumps({'0x00001000':'verified'}))
        path,report=inc.emit_one(self.game,0x1000)
        self.assertIn('NOT VERIFIED',path.read_text())
        self.assertIn('not revalidated here',path.read_text())
        self.assertEqual(json.loads(report.read_bytes())['status'],'mechanically_lifted_not_verified')


if __name__=='__main__':unittest.main()
