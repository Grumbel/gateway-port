#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Synthetic golden RAM comparison tests; no original game data needed."""

import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from run_golden import (CONVENTIONAL_END, MEMORY_SIZE, capture_campaign, check_pair,
                        describe_cpu_difference, describe_rtlink_sequence, describe_stage_save,
                        excluded_ranges,
                        explain_difference, first_divergence, normalized_memory,
                        planned_checkpoints, read_manifest, report_checkpoint_drift,
                        main as golden_main, run_campaign, save_manifest, seed_previous_stage,
                        selected_stages, snapshot_prefix, with_stage_save_hashes)
from run_walkthrough import compile_steps, parse_script
from run_walkthrough_campaign import load_campaign


class GoldenReplayTests(unittest.TestCase):
    @staticmethod
    def fake_campaign(root):
        (root / 'first.walk').write_text('COMMAND look\nLABEL seen\n'
                                         'COMMAND open box\nLABEL opened\n'
                                         'COMMAND save\nTYPE FIRST\nKEY enter\n',
                                         encoding='ascii')
        manifest = root / 'campaign.json'
        manifest.write_text(json.dumps(dict(schema_version=1, stages=[
            dict(name='first', script='first.walk', save='FIRST')])), encoding='ascii')
        return manifest

    @staticmethod
    def fake_two_stage_campaign(root):
        (root / 'first.walk').write_text('COMMAND save\nTYPE FIRST\nKEY enter\n',
                                         encoding='ascii')
        (root / 'second.walk').write_text(
            'COMMAND restore\nTYPE FIRST\nKEY enter\n'
            'COMMAND look\nCOMMAND save\nTYPE SECOND\nKEY enter\n', encoding='ascii')
        manifest = root / 'campaign.json'
        manifest.write_text(json.dumps(dict(schema_version=1, stages=[
            dict(name='first', script='first.walk', save='FIRST'),
            dict(name='second', script='second.walk', save='SECOND')])), encoding='ascii')
        return manifest

    @staticmethod
    def add_snapshot(root, stage_name, checkpoint, story_mutation=False, volatile_mutation=False):
        prefix = snapshot_prefix(root, stage_name, checkpoint['label'])
        prefix.parent.mkdir(parents=True, exist_ok=True)
        memory = bytearray(MEMORY_SIZE)
        # Nontrivial retained story byte, plus activity in documented volatile RAM.
        memory[0x30000] = 45 + int(story_mutation)
        memory[0x1ff10] = 23 + int(volatile_mutation)
        memory[0x46c] = 8 + int(volatile_mutation)
        prefix.with_suffix('.bin').write_bytes(memory)
        prefix.with_suffix('.regs').write_text('SS=1000\nSP=ff00\n', encoding='ascii')

    def test_parser_command_interval_preserves_labeled_assertions(self):
        steps = parse_script('COMMAND look\nASSERT_LOCATION 123 d3\nLABEL first\n'
                             'COMMAND open box\nLABEL next\nCOMMAND save\n')
        replay = compile_steps(steps, golden_every=2)
        self.assertEqual([p.label for p in replay.checkpoints],
                         ['first', 'golden-command-000002', 'next'])
        self.assertEqual([(p.action_index, p.command_index) for p in replay.checkpoints],
                         [(1, 1), (2, 2), (2, 2)])
        self.assertEqual(replay.checkpoints[0].assertions[0][:2], (0x123, 0xd3))
        self.assertEqual(replay.checkpoints[1].time_ms, replay.checkpoints[2].time_ms)
        for invalid in (0, -1, 10001):
            with self.assertRaisesRegex(ValueError, '--golden-every'):
                compile_steps(steps, golden_every=invalid)

    def test_mask_only_active_stack_tail_and_bda_timer(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'mem.bin'
            raw = bytearray(MEMORY_SIZE)
            raw[0x1ff10] = 55  # active stack
            raw[0x10010] = 56  # same segment, but below SP: retained
            raw[0x046c] = 57  # documented BDA clock
            raw[0x30000] = 58  # story state
            path.write_bytes(raw)
            memory, ranges = normalized_memory(path, dict(SS=0x1000, SP=0xff00))
            self.assertEqual(memory[0x1ff10], 0)
            self.assertEqual(memory[0x10010], 56)
            self.assertEqual(memory[0x046c], 0)
            self.assertEqual(memory[0x30000], 58)
            self.assertEqual(len(memory), CONVENTIONAL_END)
            self.assertEqual(ranges, excluded_ranges(dict(SS=0x1000, SP=0xff00)))
            self.assertEqual(ranges[1]['end'] - ranges[1]['start'], 0x100)
            path.write_bytes(raw[:-1])
            with self.assertRaisesRegex(ValueError, 'expected 1048576'):
                normalized_memory(path, dict(SS=0x1000, SP=0xff00))

    def test_two_runs_match_except_volatile_then_first_story_change_detected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.fake_campaign(root)
            stage = load_campaign(manifest)[0]
            checkpoints = planned_checkpoints(stage, every=1)
            self.assertEqual([c['label'] for c in checkpoints],
                             ['golden-command-000001', 'seen',
                              'golden-command-000002', 'opened',
                              'golden-command-000003', 'final'])
            for number, run in enumerate(('a', 'b')):
                for checkpoint in checkpoints:
                    self.add_snapshot(root / run, 'first', checkpoint,
                                      volatile_mutation=bool(number))
            a = capture_campaign(manifest, root / 'a', every=1)
            b = capture_campaign(manifest, root / 'b', every=1)
            check_pair(a, b)
            self.assertEqual(len(a['stages'][0]['checkpoints']), 6)
            self.assertEqual(a['stages'][0]['checkpoints'][0]['command_index'], 1)
            self.assertNotIn('data', json.dumps(a))
            self.add_snapshot(root / 'b', 'first', checkpoints[2], story_mutation=True)
            broken = capture_campaign(manifest, root / 'b', every=1)
            self.assertEqual(first_divergence(a, broken)[:2],
                             ('first', 'golden-command-000002'))
            with self.assertRaisesRegex(ValueError,
                                         'checkpoint=golden-command-000002.*command 2.*'
                                         '0x30000; 1 changed byte in 1 page'):
                check_pair(a, broken, root / 'a', root / 'b')
            file = root / 'hashes.json'
            save_manifest(file, a)
            self.assertEqual(read_manifest(file), a)
            self.assertNotIn('mem.bin', file.read_text())

    def test_mismatch_in_script_exclusions_or_missing_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.fake_campaign(root)
            stage = load_campaign(manifest)[0]
            with self.assertRaises(FileNotFoundError):
                capture_campaign(manifest, root / 'missing', every=10)
            for point in planned_checkpoints(stage, every=10):
                self.add_snapshot(root / 'a', 'first', point)
            original = capture_campaign(manifest, root / 'a', every=10)
            changed = copy.deepcopy(original)
            changed['stages'][0]['checkpoints'][0]['stack']['sp'] += 1
            self.assertIn('stack differs', first_divergence(original, changed)[2])
            changed = copy.deepcopy(original)
            changed['stages'][0]['script_sha256'] = '0' * 64
            self.assertIn('script digest differs', first_divergence(original, changed)[2])

    def test_real_campaign_stays_below_scheduled_snapshot_limit(self):
        manifest = Path(__file__).resolve().parents[1] / 'tests/fixtures/gateway-campaign.json'
        for stage in load_campaign(manifest):
            with self.subTest(stage=stage.name):
                points = planned_checkpoints(stage, every=10)
                self.assertLessEqual(len(points) - 1, 32)
                self.assertEqual(points[-1]['label'], 'final')
                self.assertGreater(points[-1]['action_index'], 0)

    def test_partial_campaign_is_explicit_and_forwards_stage_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.fake_campaign(root)
            stage = load_campaign(manifest)[0]
            for point in planned_checkpoints(stage, every=10):
                self.add_snapshot(root / 'run', 'first', point)
            recorded = capture_campaign(manifest, root / 'run', 10, through_stage='first')
            self.assertEqual([s['name'] for s in recorded['stages']], ['first'])
            with self.assertRaisesRegex(ValueError, 'unknown --through-stage'):
                capture_campaign(manifest, root / 'run', 10, through_stage='missing')
            with mock.patch('run_golden.subprocess.run') as invoke:
                run_campaign(manifest, root / 'probe', root / 'game', root / 'output',
                             10, 100, False, [], through_stage='first')
            command = invoke.call_args.args[0]
            self.assertEqual(command[command.index('--through-stage') + 1], 'first')

    def test_diagnostic_uses_union_of_differing_live_stacks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prefix_a = snapshot_prefix(root / 'a', 'first', 'sample')
            prefix_b = snapshot_prefix(root / 'b', 'first', 'sample')
            for prefix, sp in ((prefix_a, 0xff00), (prefix_b, 0xfe00)):
                prefix.parent.mkdir(parents=True)
                memory = bytearray(MEMORY_SIZE)
                memory[0x1fe50] = 9 if sp == 0xff00 else 8
                memory[0x30000] = 1
                prefix.with_suffix('.bin').write_bytes(memory)
                prefix.with_suffix('.regs').write_text(
                    f'SS=1000\nSP={sp:04x}\nCS=2200\nIP=1234\n', encoding='ascii')
            description = explain_difference({}, {}, root / 'a', root / 'b',
                                             'first', 'sample')
            self.assertIn('SP=ff00/fe00', description)
            self.assertIn('retained RAM identical after shared volatile exclusions',
                          description)
            self.assertNotIn('0x1fe50', description)
            memory = bytearray(prefix_b.with_suffix('.bin').read_bytes())
            memory[0x30000] = 2
            prefix_b.with_suffix('.bin').write_bytes(memory)
            description = explain_difference({}, {}, root / 'a', root / 'b',
                                             'first', 'sample')
            self.assertIn('0x30000; 1 changed byte in 1 page', description)

    def test_drift_report_names_later_checkpoints_and_save_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            a, b = root / 'a', root / 'b'
            for stage, digest in ((a, 'a' * 64), (b, 'b' * 64)):
                folder = stage / 'first'
                folder.mkdir(parents=True)
                (folder / 'stage-meta.json').write_text(
                    json.dumps({'save_sha256': digest}), encoding='ascii')
            self.assertEqual(describe_stage_save(a, b, 'first'), 'stage-end saves differ')
            self.assertEqual(describe_cpu_difference({'CS': 10}, {'CS': 11}),
                             'CPU a/b CS=000a/000b')
            first = {'label': 'first', 'command_index': 10, 'time_ms': 30000,
                     'stack': {'ss': 1, 'sp': 5}, 'sha256': 'a' * 64}
            second = dict(first, label='second', command_index=11, time_ms=33000)
            changed = dict(first, stack={'ss': 1, 'sp': 6}, sha256='b' * 64)
            from io import StringIO
            from contextlib import redirect_stdout
            output = StringIO()
            with redirect_stdout(output):
                report_checkpoint_drift({'stages': [{'name': 'first',
                                                     'checkpoints': [first, second]}]},
                                        {'stages': [{'name': 'first',
                                                     'checkpoints': [changed, second]}]})
            self.assertIn('1/2 checkpoints differ', output.getvalue())
            self.assertIn('command=10 ms=30000 (stack/mask, RAM)', output.getvalue())

    def test_mismatched_stage_handoff_fails_even_when_ram_matches(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.fake_campaign(root)
            stage = load_campaign(manifest)[0]
            for run in ('a', 'b'):
                directory_path = root / run
                for checkpoint in planned_checkpoints(stage, 10):
                    self.add_snapshot(directory_path, 'first', checkpoint)
                save = directory_path / 'first/state/GATE001.SAV'
                save.parent.mkdir(parents=True)
                save.write_bytes(b'FIRST'.ljust(33, b' ') + run.encode())
                (directory_path / 'first/stage-meta.json').write_text(json.dumps(dict(
                    name='first', script_sha256=stage.script_sha256,
                    save_description='FIRST',
                    save_sha256=hashlib.sha256(save.read_bytes()).hexdigest())),
                    encoding='ascii')
            a, b = (capture_campaign(manifest, root / run, 10) for run in ('a', 'b'))
            self.assertEqual(a['stages'][0]['checkpoints'], b['stages'][0]['checkpoints'])
            self.assertEqual(first_divergence(a, b)[:2], ('first', 'save'))
            with self.assertRaisesRegex(ValueError,
                                         'stage=first checkpoint=save: native saved-game'):
                check_pair(a, b)
            # Older captures lacked save hashes. Source metadata can supply
            # them without replaying the original game.
            for record in (a, b):
                del record['stages'][0]['save_sha256']
            self.assertIsNone(first_divergence(a, b))
            self.assertIn('save_sha256', with_stage_save_hashes(a, root / 'a')['stages'][0])
            with self.assertRaisesRegex(ValueError, 'checkpoint=save'):
                check_pair(a, b, root / 'a', root / 'b')

    def test_seeded_late_stage_replay_checks_original_save_and_isolates_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.fake_two_stage_campaign(root)
            source = root / 'source'
            save = source / 'first/state/GATE001.SAV'
            save.parent.mkdir(parents=True)
            save.write_bytes(b'FIRST'.ljust(33, b' ') + b'original-save')
            digest = hashlib.sha256(save.read_bytes()).hexdigest()
            stages = load_campaign(manifest)
            (source / 'first/stage-meta.json').write_text(json.dumps(dict(
                name='first', script_sha256=stages[0].script_sha256,
                save_description='FIRST', save_sha256=digest)), encoding='ascii')
            selected = selected_stages(manifest, 'second', 'second')
            self.assertEqual([stage.name for stage in selected], ['second'])
            with self.assertRaisesRegex(ValueError, '--from-stage must not follow'):
                selected_stages(manifest, 'second', 'first')
            with self.assertRaisesRegex(ValueError, 'unknown --from-stage'):
                selected_stages(manifest, 'absent')
            for copy_name in ('a', 'b'):
                target = root / copy_name
                seed_previous_stage(manifest, 'second', source, target)
                self.assertEqual((target / 'first/state/GATE001.SAV').read_bytes(),
                                 save.read_bytes())
                with mock.patch('run_golden.subprocess.run') as invoke:
                    run_campaign(manifest, root / 'probe', root / 'game', target, 10,
                                 100, False, [], 'second', 'second')
                command = invoke.call_args.args[0]
                self.assertEqual(command[command.index('--from-stage') + 1], 'second')
                for checkpoint in planned_checkpoints(stages[1], 10):
                    self.add_snapshot(target, 'second', checkpoint)
                captured = capture_campaign(manifest, target, 10, 'second', 'second')
                self.assertEqual([item['name'] for item in captured['stages']], ['second'])
            save.write_bytes(save.read_bytes() + b'corruption')
            with self.assertRaisesRegex(ValueError, 'modified'):
                seed_previous_stage(manifest, 'second', source, root / 'c')

    def test_rtlink_sequence_finds_first_call_and_timing_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            a = root / 'run-a/first/native.rtlink-sequence'
            b = root / 'run-b/first/native.rtlink-sequence'
            for path in (a, b):
                path.parent.mkdir(parents=True)
            def record(index, ms, target, caller='4661:17b8'):
                return (f'rtlink-seq id={index} ms={ms} section=4 '
                        f'target=4319:{target} caller={caller}\n')
            first = record(1, 1200, 'd5d')
            second = record(2, 1300, '730')
            third = record(3, 1500, '730')
            a.write_text(first + second + third, encoding='ascii')
            b.write_text(first + second + third, encoding='ascii')
            self.assertIn('identical (3 calls)',
                          describe_rtlink_sequence(root / 'run-a', root / 'run-b', 'first'))
            b.write_text(first + record(2, 1301, '730') + third, encoding='ascii')
            self.assertIn('first guest-ms difference at #2: 1300/1301',
                          describe_rtlink_sequence(root / 'run-a', root / 'run-b', 'first'))
            b.write_text(first + record(2, 1301, 'a13') + third, encoding='ascii')
            detail = describe_rtlink_sequence(root / 'run-a', root / 'run-b', 'first')
            self.assertIn('first code-call difference at #2', detail)
            self.assertIn('target=4319:730', detail)
            self.assertIn('target=4319:a13', detail)
            self.assertIn('preceding calls #1', detail)
            b.write_text(first, encoding='ascii')
            self.assertIn('b end-of-trace',
                          describe_rtlink_sequence(root / 'run-a', root / 'run-b', 'first'))
            b.write_text('rtlink-seq id=7 ms=1200 section=4 target=4319:d5d '
                         'caller=4661:17b8\n', encoding='ascii')
            with self.assertRaisesRegex(ValueError, 'malformed RTLink call sequence'):
                describe_rtlink_sequence(root / 'run-a', root / 'run-b', 'first')
            b.unlink()
            with self.assertRaisesRegex(ValueError, 'missing on one side'):
                describe_rtlink_sequence(root / 'run-a', root / 'run-b', 'first')

    def test_sequence_trace_flag_reaches_campaign(self):
        with mock.patch('run_golden.subprocess.run') as invoke:
            run_campaign(Path('campaign.json'), Path('probe'), Path('data'),
                         Path('output'), 5, 100, False, [],
                         trace_rtlink_sequence=True)
        self.assertIn('--trace-rtlink-sequence', invoke.call_args.args[0])

    def test_independent_run_count_is_bounded(self):
        for count in ('1', '9', 'not-a-number'):
            with self.subTest(count=count), \
                 mock.patch.object(sys, 'argv', ['run_golden.py', 'run',
                                                'campaign.json', '--runs', count]), \
                 mock.patch('sys.stderr'):
                with self.assertRaises(SystemExit) as caught:
                    golden_main()
                self.assertEqual(caught.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
