# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Asset-independent import-plan validation and code-address disambiguation."""

import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_ghidra import (KNOWN_OVERLAY, KNOWN_RESIDENT, add_callback_labels,
                            name_for_object, plan_sections)
from run_ghidra import find_headless, headless_command, pyghidra_launcher, run


class GhidraPlanTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        # Keep the runner on the mocked analyzeHeadless path even inside
        # nix develop .#ghidra, which exports GHIDRA_INSTALL_DIR.
        environment = patch.dict('run_ghidra.os.environ')
        environment.start()
        self.addCleanup(environment.stop)
        os.environ.pop('GHIDRA_INSTALL_DIR', None)
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

    def test_selected_object_entries_are_provisional_and_unselected_are_aliases(self):
        objects = [dict(object=0x2a3, handler=644, name='technician',
                        native_code=dict(section=0, file_offset=0x104)),
                   dict(object=0x2a9, handler=650, name='membership pin',
                        native_code=dict(section=0, file_offset=0x108)),
                   dict(object=0x2ab, handler=652, name='reader',
                        native_code=dict(section=0, file_offset=0x10c))]
        sections, _ = plan_sections(self.manifest, self.root, objects,
                                    decompile_objects=[0x2a3, 0x2a9])
        self.assertEqual([label['role'] for label in sections[0]['labels']],
                         ['object-entry-candidate', 'object-entry-candidate', 'object-entry'])
        self.assertEqual(sections[0]['labels'][1]['name'],
                         'gw_object_2a9_membership_pin')
        self.assertIn('provisional', sections[0]['labels'][1]['explanation'])

    def test_rejects_unresolved_or_duplicated_selected_object_entries(self):
        item = dict(object=0x2a3, handler=644,
                    native_code=dict(section=0, file_offset=0x104))
        with self.assertRaisesRegex(ValueError, 'requires an objects snapshot'):
            plan_sections(self.manifest, self.root, decompile_objects=[0x2a3])
        with self.assertRaisesRegex(ValueError, 'absent'):
            plan_sections(self.manifest, self.root, [item], [0x2a9])
        with self.assertRaisesRegex(ValueError, 'duplicate or invalid'):
            plan_sections(self.manifest, self.root, [item], [0x2a3, 0x2a3])
        with self.assertRaisesRegex(ValueError, 'no native handler'):
            plan_sections(self.manifest, self.root, [dict(object=0x2a3)], [0x2a3])
        with self.assertRaisesRegex(ValueError, 'share an entry'):
            plan_sections(self.manifest, self.root,
                          [item, dict(item, object=0x2a9)], [0x2a3, 0x2a9])

    def test_explicit_overlay_candidates_are_file_backed_and_provisional(self):
        sections, _ = plan_sections(self.manifest, self.root,
                                    decompile_offsets=[0x10f, 0x100])
        self.assertEqual([(label['name'], label['offset'], label['role'])
                          for label in sections[0]['labels']],
                         [('gw_ovl_10f_candidate', 15, 'overlay-entry-candidate'),
                          ('gw_ovl_100_candidate', 0, 'overlay-entry-candidate')])
        self.assertTrue(all('not necessarily a function boundary' in label['explanation']
                            for label in sections[0]['labels']))
        for offsets, error in [([0x110], 'no unique file-backed'),
                               ([0xff], 'no unique file-backed'),
                               ([-1], 'duplicate or invalid'),
                               ([0x100, 0x100], 'duplicate or invalid')]:
            with self.subTest(offsets=offsets), self.assertRaisesRegex(ValueError, error):
                plan_sections(self.manifest, self.root, decompile_offsets=offsets)

    def test_explicit_offset_cannot_duplicate_selected_object_handler(self):
        objects = [dict(object=0x2a9, handler=650, name='membership pin',
                        native_code=dict(section=0, file_offset=0x104))]
        with self.assertRaisesRegex(ValueError, 'duplicates a selected object handler'):
            plan_sections(self.manifest, self.root, objects,
                          decompile_objects=[0x2a9], decompile_offsets=[0x104])

    def test_normalizes_cp437_and_unlabeled_objects(self):
        self.assertEqual(name_for_object({'object': 1, 'name': 'Drawe®'}),
                         'gw_object_001_drawe')
        self.assertEqual(name_for_object({'object': 2}), 'gw_object_002_unnamed')

    def test_navigation_branches_are_not_labeled_as_function_entries(self):
        overlay = {item[1]: item[2] for item in KNOWN_OVERLAY}
        resident = {item[1]: item[2] for item in KNOWN_RESIDENT}
        self.assertEqual(overlay['gw_relocate_child'], 'function')
        self.assertEqual(overlay['gw_take_precheck'], 'function')
        self.assertEqual(overlay['gw_drop_precheck'], 'function')
        self.assertEqual(overlay['gw_unlink_child_head_call'], 'callsite')
        self.assertEqual(resident['gw_conditional_exit_case'], 'instruction')
        self.assertEqual(resident['gw_callback_exit_case'], 'instruction')
        self.assertEqual(resident['gw_room_callback_dispatch'], 'function')
        self.assertEqual(resident['gw_room_transfer'], 'function')
        for name in ('pending', 'exit_event', 'commit', 'player_move',
                     'enter_event', 'text_guard', 'alternate_entry',
                     'presentation_guard', 'presentation_call',
                     'post_event', 'return'):
            self.assertEqual(resident['gw_room_transfer_' + name], 'instruction')
        self.assertEqual(resident['gw_native_relation_contains'], 'function')
        self.assertEqual(resident['gw_test_object_flag'], 'function')
        self.assertEqual(resident['gw_set_object_flag'], 'function')
        self.assertEqual(resident['gw_clear_object_flag'], 'function')

    def test_callback_entries_are_distinct_provisional_function_targets(self):
        sections, label_at = plan_sections(self.manifest, self.root)
        callbacks = dict(schema_version=1, table_count=44, exit_count=2, callbacks=[
            dict(callback_id=0x14, status='rtlink',
                 uses=[dict(room=0xf5), dict(room=0xf5)],
                 rtlink_reference=dict(section=0, target_segment=0x3000,
                                       target_offset=4),
                 native_code=dict(filename='GATEMAIN.OVL', section=0,
                                  image_offset=4, file_offset=0x104))])
        self.section['filename'] = 'GATEMAIN.OVL'
        add_callback_labels(callbacks, self.manifest, label_at)
        self.assertEqual(sections[0]['labels'][0]['role'], 'callback-entry')
        self.assertEqual(sections[0]['labels'][0]['name'], 'gw_room_callback_14')
        callbacks['callbacks'][0]['native_code']['file_offset'] = 0x105
        with self.assertRaisesRegex(ValueError, 'mismatches manifest'):
            add_callback_labels(callbacks, self.manifest, label_at)

    def test_callback_rejects_duplicate_ids_and_use_count_mismatch(self):
        _, label_at = plan_sections(self.manifest, self.root)
        self.section['filename'] = 'GATEMAIN.OVL'
        item = dict(callback_id=2, status='rtlink', uses=[dict(room=1)],
                    rtlink_reference=dict(section=0, target_segment=0x3000,
                                          target_offset=4),
                    native_code=dict(filename='GATEMAIN.OVL', section=0,
                                     image_offset=4, file_offset=0x104))
        callbacks = dict(schema_version=1, table_count=44, exit_count=2,
                         callbacks=[item, dict(item)])
        with self.assertRaisesRegex(ValueError, 'duplicate or invalid'):
            add_callback_labels(callbacks, self.manifest, label_at)
        callbacks['callbacks'] = [item]
        with self.assertRaisesRegex(ValueError, 'use count mismatch'):
            add_callback_labels(callbacks, self.manifest, label_at)

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
        example = dict(fingerprinted_gateway=True, sections=[dict(fixup_count=17, labels=[
            dict(name='test_routine', role='function')])], resident_labels=[])
        with patch('run_ghidra.prepare', return_value=(plan_path, example)), \
                patch('run_ghidra.find_headless', return_value=Path('/bin/headless')), \
                patch('run_ghidra.subprocess.run') as invoke:
            (self.root / 'ghidra-import-report.json').write_text(json.dumps(dict(
                imported_sections=1, labeled_overlay_entries=1,
                study_event_table_words=23, study_event_flow_edges=7,
                study_matcher_path_labels=9, study_response_path_labels=18,
                vr_return_path_labels=24)))
            (self.root / 'ghidra-decompile-report.json').write_text(json.dumps(dict(
                schema_version=1, targets=[dict(name='test_routine')],
                completed=1, failed=0, requested=1)))
            (self.root / 'pseudocode.txt').write_text('verified test output')
            (self.root / 'ghidra-callgraph.json').write_text(json.dumps(dict(
                schema_version=1, targets=[dict(name='test_routine', edges=[],
                                                unresolved_calls=[])],
                unique_containing_functions=1, static_call_references=0)))
            # Simulate fresh reports appearing *during* the headless invocation.
            old_exists = Path.exists
            def exists_after_invocation(path):
                if path in (self.root / 'ghidra-import-report.json',
                            self.root / 'ghidra-decompile-report.json',
                            self.root / 'ghidra-callgraph.json',
                            self.root / 'pseudocode.txt') and not invoke.called:
                    return False
                return old_exists(path)
            with patch('run_ghidra.Path.exists', exists_after_invocation):
                run(Path('exe'), Path('ovl'), self.root, Path('tool'))
        command = invoke.call_args[0][0]
        self.assertEqual(command[0], '/bin/headless')
        self.assertTrue(invoke.call_args[1]['check'])

    def test_runner_rejects_missing_fingerprinted_study_jump_table(self):
        plan_path = self.root / 'ghidra-plan.json'
        plan_path.write_text(json.dumps(dict(exe_file='/input/GATEMAIN.EXE')))
        example = dict(fingerprinted_gateway=True, sections=[dict(
            fixup_count=17, labels=[dict(name='test_routine', role='function')])],
            resident_labels=[])
        with patch('run_ghidra.prepare', return_value=(plan_path, example)), \
                patch('run_ghidra.find_headless', return_value=Path('/bin/headless')), \
                patch('run_ghidra.subprocess.run') as invoke:
            (self.root / 'ghidra-import-report.json').write_text(json.dumps(dict(
                imported_sections=1, labeled_overlay_entries=1,
                study_event_table_words=22)))
            (self.root / 'ghidra-decompile-report.json').write_text(json.dumps(dict(
                schema_version=1, targets=[dict(name='test_routine')],
                completed=1, failed=0, requested=1)))
            (self.root / 'pseudocode.txt').write_text('mock decompiler output')
            (self.root / 'ghidra-callgraph.json').write_text('{}')
            old_exists = Path.exists
            def exists_after_invocation(path):
                if path in (self.root / 'ghidra-import-report.json',
                            self.root / 'ghidra-decompile-report.json',
                            self.root / 'ghidra-callgraph.json',
                            self.root / 'pseudocode.txt') and not invoke.called:
                    return False
                return old_exists(path)
            with patch('run_ghidra.Path.exists', exists_after_invocation):
                with self.assertRaisesRegex(ValueError, '23 native Study event branches'):
                    run(Path('exe'), Path('ovl'), self.root, Path('tool'))

    def test_runner_rejects_incomplete_static_callgraph(self):
        plan_path = self.root / 'ghidra-plan.json'
        plan_path.write_text(json.dumps(dict(exe_file='/input/GATEMAIN.EXE')))
        example = dict(fingerprinted_gateway=False, sections=[dict(
            fixup_count=0, labels=[dict(name='test_routine', role='function')])],
            resident_labels=[])
        with patch('run_ghidra.prepare', return_value=(plan_path, example)), \
                patch('run_ghidra.find_headless', return_value=Path('/bin/headless')), \
                patch('run_ghidra.subprocess.run') as invoke:
            (self.root / 'ghidra-import-report.json').write_text(json.dumps(dict(
                imported_sections=1, labeled_overlay_entries=1)))
            (self.root / 'ghidra-decompile-report.json').write_text(json.dumps(dict(
                schema_version=1, targets=[dict(name='test_routine')],
                completed=1, failed=0, requested=1)))
            (self.root / 'pseudocode.txt').write_text('mock pseudocode')
            (self.root / 'ghidra-callgraph.json').write_text(json.dumps(dict(
                schema_version=1, targets=[], unique_containing_functions=0,
                static_call_references=0)))
            old_exists = Path.exists

            def exists_after_invocation(path):
                if path in (self.root / 'ghidra-import-report.json',
                            self.root / 'ghidra-decompile-report.json',
                            self.root / 'ghidra-callgraph.json',
                            self.root / 'pseudocode.txt') and not invoke.called:
                    return False
                return old_exists(path)

            with patch('run_ghidra.Path.exists', exists_after_invocation):
                with self.assertRaisesRegex(ValueError, 'static callgraph'):
                    run(Path('exe'), Path('ovl'), self.root, Path('tool'))

    def test_runner_rejects_incomplete_fingerprinted_study_matcher(self):
        plan_path = self.root / 'ghidra-plan.json'
        plan_path.write_text(json.dumps(dict(exe_file='/input/GATEMAIN.EXE')))
        example = dict(fingerprinted_gateway=True, sections=[dict(
            fixup_count=17, labels=[dict(name='test_routine', role='function')])],
            resident_labels=[])
        with patch('run_ghidra.prepare', return_value=(plan_path, example)), \
                patch('run_ghidra.find_headless', return_value=Path('/bin/headless')), \
                patch('run_ghidra.subprocess.run') as invoke:
            (self.root / 'ghidra-import-report.json').write_text(json.dumps(dict(
                imported_sections=1, labeled_overlay_entries=1,
                study_event_table_words=23, study_event_flow_edges=7,
                study_matcher_path_labels=8, study_response_path_labels=18,
                vr_return_path_labels=24)))
            (self.root / 'ghidra-decompile-report.json').write_text(json.dumps(dict(
                schema_version=1, targets=[dict(name='test_routine')],
                completed=1, failed=0, requested=1)))
            (self.root / 'pseudocode.txt').write_text('mock decompiler output')
            (self.root / 'ghidra-callgraph.json').write_text('{}')
            old_exists = Path.exists

            def exists_after_invocation(path):
                if path in (self.root / 'ghidra-import-report.json',
                            self.root / 'ghidra-decompile-report.json',
                            self.root / 'ghidra-callgraph.json',
                            self.root / 'pseudocode.txt') and not invoke.called:
                    return False
                return old_exists(path)

            with patch('run_ghidra.Path.exists', exists_after_invocation):
                with self.assertRaisesRegex(ValueError, '9 native Study matcher'):
                    run(Path('exe'), Path('ovl'), self.root, Path('tool'))

    def test_runner_rejects_missing_study_flow_and_response_annotations(self):
        plan_path = self.root / 'ghidra-plan.json'
        plan_path.write_text(json.dumps(dict(exe_file='/input/GATEMAIN.EXE')))
        example = dict(fingerprinted_gateway=True, sections=[dict(
            fixup_count=17, labels=[dict(name='test_routine', role='function')])],
            resident_labels=[])
        for bad_key, error in (('study_event_flow_edges', '7 unique Study'),
                               ('study_response_path_labels', '18 native Study'),
                               ('vr_return_path_labels', '24 native VR-return')):
            with self.subTest(bad_key=bad_key), \
                    patch('run_ghidra.prepare', return_value=(plan_path, example)), \
                    patch('run_ghidra.find_headless', return_value=Path('/bin/headless')), \
                    patch('run_ghidra.subprocess.run') as invoke:
                import_report = dict(imported_sections=1, labeled_overlay_entries=1,
                                     study_event_table_words=23, study_event_flow_edges=7,
                                     study_matcher_path_labels=9,
                                     study_response_path_labels=18,
                                     vr_return_path_labels=24)
                import_report[bad_key] -= 1
                (self.root / 'ghidra-import-report.json').write_text(
                    json.dumps(import_report))
                (self.root / 'ghidra-decompile-report.json').write_text(json.dumps(dict(
                    schema_version=1, targets=[dict(name='test_routine')],
                    completed=1, failed=0, requested=1)))
                (self.root / 'pseudocode.txt').write_text('mock decompiler output')
                (self.root / 'ghidra-callgraph.json').write_text('{}')
                old_exists = Path.exists

                def exists_after_invocation(path):
                    if path in (self.root / 'ghidra-import-report.json',
                                self.root / 'ghidra-decompile-report.json',
                                self.root / 'ghidra-callgraph.json',
                                self.root / 'pseudocode.txt') and not invoke.called:
                        return False
                    return old_exists(path)

                with patch('run_ghidra.Path.exists', exists_after_invocation):
                    with self.assertRaisesRegex(ValueError, error):
                        run(Path('exe'), Path('ovl'), self.root, Path('tool'))

    def test_runner_detects_failed_ghidra_scripts(self):
        plan_path = self.root / 'ghidra-plan.json'
        plan_path.write_text(json.dumps(dict(exe_file='/input/GATEMAIN.EXE')))
        with patch('run_ghidra.prepare', return_value=(plan_path, {'sections': []})), \
                patch('run_ghidra.find_headless', return_value=Path('/bin/headless')), \
                patch('run_ghidra.subprocess.run'):
            with self.assertRaisesRegex(ValueError, 'did not produce'):
                run(Path('exe'), Path('ovl'), self.root, Path('tool'))

    def test_runner_rejects_stale_headless_reports(self):
        (self.root / 'pseudocode.txt').write_text('old pseudocode')
        with self.assertRaisesRegex(ValueError, 'stale results'):
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

    def test_nix_ghidra_bin_symlinked_wrapper(self):
        # ghidra-bin 12 links bin/ghidra to lib/ghidra/ghidraRun.
        prefix = self.root / 'nix-store-package'
        run_script = prefix / 'lib/ghidra/ghidraRun'
        launcher = prefix / 'lib/ghidra/support/analyzeHeadless'
        launcher.parent.mkdir(parents=True)
        launcher.write_text('headless executable fixture')
        run_script.write_text('ghidra fixture')
        wrapper = prefix / 'bin/ghidra'
        wrapper.parent.mkdir(parents=True)
        wrapper.symlink_to(run_script)
        with patch('run_ghidra.shutil.which',
                   side_effect=lambda name: str(wrapper) if name == 'ghidra' else None):
            self.assertEqual(find_headless(), launcher)


    def test_pyghidra_launcher_requires_install_dir(self):
        self.assertIsNone(pyghidra_launcher())

    def test_pyghidra_launcher_command(self):
        install = self.root / 'ghidra'
        (install / 'Ghidra').mkdir(parents=True)
        with patch('run_ghidra.importlib.util.find_spec', return_value=object()):
            launcher = pyghidra_launcher(install)
        self.assertEqual(launcher[1:], ['-m', 'pyghidra.ghidra_launch', '--install-dir',
                                        str(install),
                                        'ghidra.app.util.headless.AnalyzeHeadless'])
        plan_path = self.root / 'ghidra-plan.json'
        plan_path.write_text(json.dumps(dict(exe_file='/input/GATEMAIN.EXE')))
        command = headless_command(launcher, self.root, 'Gateway', plan_path)
        self.assertEqual(command[:len(launcher)], launcher)
        self.assertEqual(command[len(launcher) + 1:len(launcher) + 4],
                         ['Gateway', '-import', '/input/GATEMAIN.EXE'])

    def test_pyghidra_launcher_without_module(self):
        install = self.root / 'ghidra'
        (install / 'Ghidra').mkdir(parents=True)
        with patch('run_ghidra.importlib.util.find_spec', return_value=None):
            self.assertIsNone(pyghidra_launcher(install))

    def test_pyghidra_launcher_rejects_non_installation(self):
        with patch('run_ghidra.importlib.util.find_spec', return_value=object()):
            with self.assertRaisesRegex(ValueError, 'not a Ghidra installation'):
                pyghidra_launcher(self.root)


if __name__ == '__main__':
    unittest.main()
