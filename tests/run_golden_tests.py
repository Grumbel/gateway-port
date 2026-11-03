#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Synthetic golden RAM comparison tests; no original game data needed."""

import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from run_golden import (CONVENTIONAL_END, MEMORY_SIZE, capture_campaign, check_pair,
                        excluded_ranges, first_divergence, normalized_memory,
                        planned_checkpoints, read_manifest, save_manifest, snapshot_prefix)
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
                                         'checkpoint=golden-command-000002.*command 2.*0x30000'):
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


if __name__ == '__main__':
    unittest.main()
