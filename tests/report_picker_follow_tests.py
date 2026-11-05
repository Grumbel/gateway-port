#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Synthetic provenance and temporal-action report regressions."""

from pathlib import Path
import subprocess
import sys
import tempfile


TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from report_picker_follow import parse_picker_calls, report  # noqa: E402


FIELDS = ('call', 'step', 'ms', 'cs', 'ip', 'linear', 'bytes', 'ax', 'bx',
          'cx', 'dx', 'si', 'di', 'bp', 'ss', 'sp', 'flags')


def instruction(call):
    return '\t'.join(map(str, (call, 0, 1200, '4100', '123', '41123',
                               '85c0', '1', '0', '0', '0', '0', '0', '0',
                               '5200', '100', '202')))


with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / 'picker-follow.tsv'
    calls = [
        '# call=1 ms=1000 x=280 y=50 native_ax=1 group=0 slot=1 kind=3 '
        'source=room object=123 overlaps=2 verdict=match return=4100:0123',
        '# call=2 ms=2000 x=35 y=12 native_ax=1 group=1 slot=1 kind=5 '
        'source=control object=0 overlaps=2 verdict=match return=4100:0123',
        '# call=3 ms=3000 x=285 y=60 native_ax=1 group=0 slot=2 kind=3 '
        'source=room object=124 overlaps=1 verdict=mismatch return=4100:0123',
    ]
    events = [
        '# dispatch call=1 ms=1012 age=12 kind=context action=5f '
        'first=7b second=0 target=0:0',
        '# dispatch call=3 ms=3015 age=15 kind=context action=5f '
        'first=1 second=2 target=0:0',
        '# input call=1 status=observed seq=1 age=3 func=5 button=0 '
        'buttons=1 count=1 x=280 y=50 resume=1000:04d5 coords=match',
        '# input call=2 status=none',
        '# input call=3 status=observed seq=3 age=9 func=3 button=-1 '
        'buttons=0 count=0 x=280 y=40 resume=1000:04d5 coords=differ',
    ]
    content = ('\t'.join(FIELDS) + '\n' + '\n'.join(calls) + '\n' +
               '\n'.join(instruction(call) for call in range(1, 4)) + '\n' +
               '\n'.join(events) + '\n' +
               ''.join(f'# end call={call} status=complete steps=1\n'
                       for call in range(1, 4)) +
               '# follow-summary calls=3 completed=3 interrupted=0 '
               'missed=0 errors=0 pending=0 steps=1 writes=0 write_errors=0 '
               'dispatches=2 dispatch_errors=0 dispatch_ms=2000 '
               'inputs=2 input_missing=1 input_ms=2000\n')
    path.write_text(content)
    result = report(path)
    assert 'Original native picker calls: 3' in result
    assert 'Sources: room=2, control=1' in result
    assert 'Room-object context evidence: object-referenced=1, other-context-objects=1' in result
    assert ('Call 1 at 1000ms (280,50): room object=0x7b '
            'AX=1 g=0 slot=1 kind=3 verdict=match overlaps=2') in result
    assert 'Guest mouse: INT 33h/5' in result
    assert 'Context object link: object-referenced' in result
    assert 'Context object link: other-context-objects' in result
    assert 'Call 2 at 2000ms (35,12): control' in result
    assert 'Candidates: none at the two monitored resident sites' in result
    assert 'coords=differ' in result
    assert 'not proof of click acceptance' in result
    assert 'Call 2 at' not in report(path, source='room')
    assert 'Call 1 at' in report(path, source='room', limit=1)
    assert '... 1 more matching calls' in report(path, source='room', limit=1)
    assert 'Call 3 at' in report(path, source='room', limit=0)
    assert len(parse_picker_calls(path)) == 3
    command = subprocess.run([sys.executable, str(TOOLS / 'report_picker_follow.py'),
                              str(path), '--source', 'control'],
                             capture_output=True, text=True, check=True)
    assert 'Call 2 at' in command.stdout and 'Call 1 at' not in command.stdout
    for broken in (calls[0].replace('object=123', 'object=-1'),
                   calls[1].replace('object=0', 'object=7'),
                   calls[0].replace('slot=1', 'slot=-1'),
                   calls[0].replace('verdict=match', 'verdict=accepted'),
                   calls[0].replace('overlaps=2 ', ''),
                   calls[0] + ' object=123'):
        original = calls[0] if broken.startswith('# call=1 ') else calls[1]
        bad = content.replace(original, broken)
        path.write_text(bad)
        try:
            report(path)
        except ValueError as error:
            assert 'native picker call' in str(error)
        else:
            raise AssertionError('invalid native picker provenance should fail')
    legacy = '\n'.join(line.split(' object=')[0] +
                       (' return=' + line.split(' return=')[1]
                        if ' return=' in line else '')
                       if line.startswith('# call=') else line
                       for line in content.splitlines()) + '\n'
    path.write_text(legacy)
    result = report(path)
    assert 'Sources: room=2, control=1' in result
    assert 'object identity unavailable for 2 older calls' in result
    assert 'Call 1 at 1000ms (280,50): room' in result

print('Gateway native picker evidence report checks passed')
