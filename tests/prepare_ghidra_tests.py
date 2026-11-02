"""Asset-independent import-plan validation and code-address disambiguation."""

import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_ghidra import name_for_object, plan_sections
from run_ghidra import find_headless, headless_command, run


class GhidraPlanTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.raw = bytearray(range(16))
        struct.pack_into('<H', self.raw, 4, 0x1234)
        self.relocated = bytearray(self.raw)
        struct.pack_into('<H', self.relocated, 4, 0x2234)
        (self.root / 'section-000.raw.bin').write_bytes(self.raw)
        (self.root / 'section-000.relocated.bin').write_bytes(self.relocated)
        self.section = dict(id=0, linked_segment=0x2000, runtime_segment=0x3000,
                            image_file_offset=0x100, file_bytes=16,
                            initialized_size=16, memory_size=32,
                            missing_paragraph_tail=0, parent=None, overlaps=[],
                            fixups=[dict(offset=4, segment=0x2000, image_offset=4)])
        self.manifest = dict(relocation=0x1000, sections=[self.section])

    def test_valid_relocation_bss_label_and_object_entry(self):
        objects = [dict(object=0x124, name='drawer/Thing', handler=7,
                        native_code=dict(section=0, file_offset=0x104))]
        sections, label_at = plan_sections(self.manifest, self.root, objects)
        self.assertEqual(sections[0]['memory_size'], 32)
        self.assertEqual(sections[0]['fixup_count'], 1)
        self.assertEqual(sections[0]['relocated_sha256'],
                         hashlib.sha256(self.relocated).hexdigest())
        self.assertEqual(sections[0]['labels'][0]['name'], 'gw_object_124_drawer_thing')
        label_at(0x10f, 'last_byte', 'callsite', 'test')
        self.assertEqual(sections[0]['labels'][1]['offset'], 15)

    def test_tail_is_not_bss(self):
        self.section['initialized_size'] = 24
        self.section['missing_paragraph_tail'] = 8
        self.assertEqual(plan_sections(self.manifest, self.root)[0][0]['missing_paragraph_tail'],
                         8)

    def test_rejects_broken_fixup_site(self):
        self.section['fixups'][0]['segment'] += 1
        with self.assertRaisesRegex(ValueError, 'invalid fixup site'):
            plan_sections(self.manifest, self.root)

    def test_rejects_broken_relocated_bytes(self):
        (self.root / 'section-000.relocated.bin').write_bytes(self.raw)
        with self.assertRaisesRegex(ValueError, 'relocation bytes differ'):
            plan_sections(self.manifest, self.root)

    def test_rejects_unresolved_code_address(self):
        _, label_at = plan_sections(self.manifest, self.root)
        with self.assertRaisesRegex(ValueError, 'resolves to 0 sections'):
            label_at(0x200, 'bad', 'function', 'test')

    def test_rejects_duplicate_sections(self):
        self.manifest['sections'].append(dict(self.section))
        with self.assertRaisesRegex(ValueError, 'duplicate or discontinuous'):
            plan_sections(self.manifest, self.root)

    def test_rejects_unspecified_relocations(self):
        del self.section['fixups']
        with self.assertRaisesRegex(ValueError, '--fixups'):
            plan_sections(self.manifest, self.root)

    def test_rejects_handler_wrong_section(self):
        objects = [dict(object=1, handler=1,
                        native_code=dict(section=3, file_offset=0x104))]
        with self.assertRaisesRegex(ValueError, 'wrong section'):
            plan_sections(self.manifest, self.root, objects)

    def test_normalizes_cp437_and_unlabeled_objects(self):
        self.assertEqual(name_for_object({'object': 1, 'name': 'Drawe®'}),
                         'gw_object_001_drawe')
        self.assertEqual(name_for_object({'object': 2}), 'gw_object_002_unnamed')

    def test_runner_uses_scripts_in_pre_analysis_order(self):
        plan_path = self.root / 'ghidra-plan.json'
        plan_path.write_text(json.dumps(dict(exe_file='/input/GATEMAIN.EXE')))
        cmd = headless_command(Path('/bin/analyzeHeadless'), self.root, 'Gateway', plan_path)
        self.assertEqual(cmd[3:5], ['-import', '/input/GATEMAIN.EXE'])
        self.assertLess(cmd.index('-preScript'), cmd.index('-postScript'))
        self.assertEqual(cmd[cmd.index('-preScript')+1], 'ImportGatewayRtlink.py')
        self.assertEqual(cmd[cmd.index('-postScript')+1], 'DecompileGateway.py')

    def test_runner_refuses_existing_project(self):
        (self.root / 'project').mkdir()
        (self.root / 'project/Gateway.gpr').write_bytes(b'existing Ghidra project')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            run(Path('exe'), Path('ovl'), self.root, Path('tool'))

    def test_runner_headless_invocation(self):
        plan_path = self.root / 'ghidra-plan.json'
        plan_path.write_text(json.dumps(dict(exe_file='/input/GATEMAIN.EXE')))
        example = dict(sections=[dict(fixup_count=17)])
        with patch('run_ghidra.prepare', return_value=(plan_path, example)), \
                patch('run_ghidra.find_headless', return_value=Path('/bin/headless')), \
                patch('run_ghidra.subprocess.run') as invoke:
            (self.root / 'ghidra-import-report.json').write_text('{}')
            (self.root / 'pseudocode.txt').write_text('verified test output')
            run(Path('exe'), Path('ovl'), self.root, Path('tool'))
        command = invoke.call_args[0][0]
        self.assertEqual(command[0], '/bin/headless')
        self.assertTrue(invoke.call_args[1]['check'])

    def test_runner_detects_failed_ghidra_scripts(self):
        plan_path = self.root / 'ghidra-plan.json'
        plan_path.write_text(json.dumps(dict(exe_file='/input/GATEMAIN.EXE')))
        with patch('run_ghidra.prepare', return_value=(plan_path, {'sections': []})), \
                patch('run_ghidra.find_headless', return_value=Path('/bin/headless')), \
                patch('run_ghidra.subprocess.run'):
            with self.assertRaisesRegex(ValueError, 'did not produce'):
                run(Path('exe'), Path('ovl'), self.root, Path('tool'))

    def test_nix_ghidra_bin_support_fallback(self):
        prefix = self.root / 'nix-store-package'
        wrapper = prefix / 'bin/ghidra'
        launcher = prefix / 'lib/ghidra/support/analyzeHeadless'
        launcher.parent.mkdir(parents=True)
        launcher.write_text('headless executable fixture')
        wrapper.parent.mkdir(parents=True)
        wrapper.write_text('ghidra fixture')
        with patch('run_ghidra.shutil.which',
                   side_effect=lambda name: str(wrapper) if name == 'ghidra' else None):
            self.assertEqual(find_headless(), launcher)


if __name__ == '__main__':
    unittest.main()
