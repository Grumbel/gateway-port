#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic parser and mismatch-gating tests; no DOS game assets required."""

import importlib.util
from pathlib import Path
import tempfile


SCRIPT = Path(__file__).resolve().parents[1] / 'tools/check_picker_trace.py'
SPEC = importlib.util.spec_from_file_location('check_picker_trace', SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def expect_fail(path, phrase, allow=False):
    try:
        MODULE.check_trace(path, allow)
    except ValueError as error:
        assert phrase in str(error), error
    else:
        raise AssertionError(f'expected trace failure: {phrase}')


def trace(rows, calls, matched, mismatched=0, unmodeled=0, errors=0, unfinished=0,
          fields=MODULE.FIELDS):
    return ('\t'.join(fields) + '\n' + '\n'.join(rows) + '\n' +
            f'# picker-summary calls={calls} matched={matched} '
            f'mismatched={mismatched} unmodeled={unmodeled} errors={errors} '
            f'unfinished={unfinished}\n')


with tempfile.TemporaryDirectory() as dirname:
    path = Path(dirname) / 'picker.tsv'
    old_matching = '123\t360\t200\t1\t1\t28\t3\t1\t1\t28\t3\tmatch'
    matching = old_matching + '\troom\t12\t0\t292\t3'
    mismatch = '124\t360\t200\t1\t1\t28\t3\t1\t1\t29\t3\tmismatch' + \
               '\tunresolved\t-1\t-1\t0\t3'
    unmodeled = '2\t0\t0\t-1\t65535\t65535\t0\t1\t0\t0\t1\tunmodeled' + \
                '\tunmodeled\t-1\t-1\t0\t0'
    path.write_text(trace([matching], 1, 1))
    assert MODULE.check_trace(path) == {'match': 1, 'mismatch': 0, 'unmodeled': 0}
    path.write_text(trace([old_matching], 1, 1, fields=MODULE.LEGACY_FIELDS))
    assert MODULE.check_trace(path)['match'] == 1  # Previous bundles remain readable.
    path.write_text(trace([matching, mismatch], 2, 1, mismatched=1))
    expect_fail(path, 'picker disagreements')
    path.write_text(trace([matching, unmodeled], 2, 1, unmodeled=1))
    expect_fail(path, 'unmodeled native picker calls')
    assert MODULE.check_trace(path, True)['unmodeled'] == 1
    path.write_text(trace([matching], 1, 1, unfinished=1))
    expect_fail(path, 'unfinished calls')
    path.write_text(trace([matching], 1, 1, errors=1))
    expect_fail(path, 'picker disagreements')
    path.write_text(trace([matching], 2, 1))
    expect_fail(path, 'summary does not match')
    path.write_text(trace([], 0, 0))
    expect_fail(path, 'no model-verified')
    path.write_text(trace([matching.replace('room\t12', 'room\t-1')], 1, 1))
    expect_fail(path, 'invalid picker provenance values')
    path.write_text(trace([matching.replace('room\t12\t0', 'room\t12\t128')], 1, 1))
    expect_fail(path, 'inconsistent picker provenance')
    path.write_text(trace([matching.replace('room\t12\t0\t292', 'control\t12\t0\t292')], 1, 1))
    expect_fail(path, 'inconsistent picker provenance')

print('Gateway picker trace verification tests passed')
