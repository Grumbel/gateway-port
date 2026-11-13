#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Checks the engine/story classification on a synthetic call graph."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import report_callgraph as R  # noqa: E402


def fn(fid, **extra):
    unit = fid.split(':')[0]
    return dict(id=fid, unit=unit, section=None if unit.startswith('exe') else 1,
                ident=fid, bytes=10, **extra)


def edge(a, b, kind='far'):
    return dict(caller=a, callee=b, kind=kind, sites=['x'])


class ClassifyTests(unittest.TestCase):
    def setUp(self):
        cg = dict(functions=[
            fn('exe-startup:0'),
            fn('exe:10'),                 # main loop, reached from start-up
            fn('exe:20'),                 # service used by everyone
            fn('exe:30'),                 # pointer-only engine code (no caller)
            fn('exe:40', trampoline_to='ovl-001:0'),   # RTLink stub of a handler
            fn('ovl-001:0'),              # object handler A
            fn('ovl-001:10'),             # helper private to A
            fn('ovl-001:20'),             # object handler B
            fn('ovl-002:0'),              # action handler
        ], edges=[
            edge('exe-startup:0', 'exe:10', 'near'),
            edge('exe:10', 'exe:20'),
            edge('exe:10', 'exe:40'),     # dispatch through the stub
            edge('ovl-001:0', 'ovl-001:10', 'near'),
            edge('ovl-001:10', 'exe:20'),
            edge('ovl-001:20', 'exe:40'), # B calls A through the stub
            edge('ovl-002:0', 'exe:20'),
        ])
        self.g = R.Graph(cg)
        self.root = {'object': {'ovl-001:0', 'ovl-001:20'}, 'callback': set(),
                     'action': {'ovl-002:0'}}
        self.fclass, self.private, self.users, self.units, self.sites = \
            R.classify(self.g, self.root)

    def test_stub_calls_resolve_to_target(self):
        self.assertIn('ovl-001:0', self.g.callees['ovl-001:20'])
        self.assertIn('ovl-001:0', self.g.callees['exe:10'])

    def test_engine_roots_skip_stubs_and_called_functions(self):
        self.assertEqual(self.root['engine'], {'exe-startup:0', 'exe:30'})

    def test_engine_walk_stops_at_table_roots(self):
        self.assertEqual(self.fclass['exe:10'], 'engine')
        self.assertEqual(self.fclass['ovl-001:0'], 'object')
        self.assertEqual(self.fclass['ovl-001:10'], 'object')
        self.assertEqual(self.fclass['exe:20'], 'action+engine+object')

    def test_story_private_and_api(self):
        self.assertEqual(self.private, {'ovl-001:0', 'ovl-001:10', 'ovl-001:20'})
        # A's helper calls exe:20; B's own walk stops at root A.
        self.assertEqual(dict(self.users), {'exe:20': 1})
        self.assertEqual(self.units['exe:20'], {'ovl-001'})
        self.assertEqual(self.sites['exe:20'], 1)


if __name__ == '__main__':
    unittest.main()
