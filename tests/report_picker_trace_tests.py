#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Provenance report regressions with synthetic picker results."""

import importlib.util
from pathlib import Path
import sys
import tempfile


TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import check_picker_trace  # noqa: E402

SPEC = importlib.util.spec_from_file_location('report_picker_trace',
                                               TOOLS / 'report_picker_trace.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


with tempfile.TemporaryDirectory() as dirname:
    trace = Path(dirname) / 'picker.tsv'
    header = '\t'.join(check_picker_trace.FIELDS)
    match = '120\t40\t30\t1\t0\t1\t3\t1\t0\t1\t3\tmatch\troom\t12\t0\t292\t3'
    mismatch = '121\t40\t30\t1\t0\t1\t3\t1\t2\t0\t1\tmismatch\tfallback\t17\t1\t0\t2'
    unresolved = '122\t25\t25\t1\t0\t0\t1\t1\t2\t1\t5\tmismatch' + \
                 '\tunresolved\t31\t128\t0\t1'
    trace.write_text(header + '\n' + '\n'.join((match, mismatch, unresolved)) + '\n')
    output = MODULE.report(trace)
    assert 'Calls: 3, match=1, mismatch=2' in output
    assert 'room         1' in output and 'fallback     1' in output
    assert 'competing enabled rectangles: 2' in output
    assert 'g=0 s=1 index=12 room: 1 calls' in output
    assert 'Mismatches and unresolved native rows: 2' in output
    assert '122ms at (25,25)' in output
    assert output.count('later action dispatch') == 1
    assert MODULE.report(trace, limit=1).count('further calls') == 1
    trace.write_text('\t'.join(check_picker_trace.LEGACY_FIELDS) + '\n' +
                     '\t'.join(match.split('\t')[:12]) + '\n')
    assert 'legacy trace' in MODULE.report(trace)

print('Gateway picker provenance report tests passed')
