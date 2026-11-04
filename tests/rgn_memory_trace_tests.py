# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic checks for bounded original-game RGN consumer reports."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from summarize_rgn_memory_trace import describe_gateway_rgn_offset, parse_trace, summarize


TRACE = [
    'read ms=100 file=GATE_002.RGN offset=0 size=1a address=13400',
    'rgn-buffer ms=100 file=GATE_002.RGN offset=0 address=13400 size=1a',
    'rgn-guest-read ms=120 file=GATE_002.RGN offset=6 address=13406 size=2 '
    'cs=1000 ip=1234 ax=0 bx=0 cx=0 dx=0',
    'rgn-consumer file=GATE_002.RGN cs=1000 ip=1234 first_offset=6 hits=7 '
    'first_ms=120 last_ms=330',
    'rgn-consumer file=GATE_002.RGN cs=1000 ip=5678 first_offset=a hits=3 '
    'first_ms=140 last_ms=140',
    'rgn-summary buffers=2 evictions=1 active=1 consumers=2 dropped=0',
]


class RgnMemoryTests(unittest.TestCase):
    def test_counts_and_sorted_consumer_site_report(self):
        summary, consumers, samples, reads = parse_trace(TRACE)
        self.assertEqual(summary['buffers'], 2)
        self.assertEqual(reads, [])
        self.assertEqual(consumers['GATE_002.RGN', 0x1000, 0x1234]['hits'], 7)
        self.assertEqual(samples['GATE_002.RGN', 0x1000, 0x1234][0],
                         (120, 6, 0x13406, 2))
        report = summarize(TRACE)
        self.assertIn('1000:1234', report)
        self.assertLess(report.index('1000:1234'), report.index('1000:5678'))
        self.assertIn('none proves', report)
        self.assertIn('reused stack data', report)

    def test_safe_dos_only_trace_groups_call_sites(self):
        trace = [
            'rgn-buffer ms=8258 file=GATE_002.RGN offset=5a address=68faa size=6 '
            'cs=2649 ip=1350 ss=5b46 sp=dc10',
            'rgn-buffer ms=8259 file=GATE_002.RGN offset=264 address=68fb0 size=6 '
            'cs=2649 ip=13ad ss=5b46 sp=dc10',
            'rgn-buffer ms=8259 file=GATE_002.RGN offset=26a address=68fb0 size=6 '
            'cs=2649 ip=13ad ss=5b46 sp=dc10',
            'rgn-summary mode=dos-only buffers=3 evictions=0 active=0 consumers=0 dropped=0',
        ]
        summary, consumers, samples, reads = parse_trace(trace)
        self.assertEqual(summary['mode'], 'dos-only')
        self.assertEqual(len(reads), 3)
        self.assertEqual(consumers, {})
        self.assertEqual(samples, {})
        report = summarize(trace)
        self.assertLess(report.index('2649:13ad'), report.index('2649:1350'))
        self.assertIn('temporary stack buffer', report)
        with self.assertRaisesRegex(ValueError, 'DOS read count disagrees'):
            parse_trace(trace[:-1] + ['rgn-summary mode=dos-only buffers=2 '
                                      'evictions=0 active=0 consumers=0 dropped=0'])

    def test_filters_and_empty_window(self):
        self.assertNotIn('1000:5678', summarize(TRACE, minimum_hits=4))
        self.assertIn('No tracked RGN buffer reads', summarize(TRACE, focus='OTHER.RGN'))
        self.assertIn('... 1 more consumer', summarize(TRACE, limit=1))
        self.assertIn('ranking is incomplete', summarize(TRACE[:-1] +
                      ['rgn-summary buffers=2 evictions=1 active=1 consumers=2 dropped=4']))

    def test_optional_original_data_annotations_are_static_only(self):
        # One six-byte entry points to a zero-ID rectangle which inherits its
        # header default.  The report deliberately does not infer visibility.
        data = bytes.fromhex('06 00 26 01 01 00 00 00 1b 93 2a 9d')
        self.assertEqual(describe_gateway_rgn_offset(data, 0), 'header[0] word+0')
        self.assertEqual(describe_gateway_rgn_offset(data, 8),
                         'rect[0] object=header-default room-xy=(54,147)-(84,157)')
        self.assertIsNone(describe_gateway_rgn_offset(data, 100))
        annotated = summarize(TRACE, rgn_data={'GATE_002.RGN': data})
        self.assertIn('header[0] word+0', summarize(
            [line.replace('offset=6', 'offset=0') for line in TRACE],
            rgn_data={'GATE_002.RGN': data}))
        self.assertIn('none proves', annotated)

    def test_reject_incomplete_or_corrupt_census(self):
        with self.assertRaisesRegex(ValueError, 'missing RGN census'):
            parse_trace(TRACE[:-1])
        with self.assertRaisesRegex(ValueError, 'consumer count disagrees'):
            parse_trace(TRACE[:-1] + ['rgn-summary buffers=2 evictions=1 active=1 consumers=3 '
                                      'dropped=0'])
        with self.assertRaisesRegex(ValueError, 'sample without corresponding'):
            parse_trace(TRACE + ['rgn-guest-read ms=200 file=GATE_002.RGN offset=0 '
                                 'address=13400 size=2 cs=1000 ip=ffff'])


if __name__ == '__main__':
    unittest.main()
