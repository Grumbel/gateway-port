#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Synthetic controlled-click replay tests without original game execution."""

from pathlib import Path
import json
import sys
import tempfile
from types import SimpleNamespace
from unittest import mock


TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from picker_click_trials import (compare_existing, compare_recordings,
                                 picker_observations, read_points, run_study,
                                 study_plan)  # noqa: E402


def snapshot(directory, phase, extra=None):
    directory.mkdir(parents=True, exist_ok=True)
    content = bytearray(0x100000)
    if extra:
        for address, value in extra.items():
            content[address] = value
    (directory / f'{phase}.bin').write_bytes(content)
    (directory / f'{phase}.regs').write_text(
        'AX=0\nBX=0\nCX=0\nDX=0\nCS=2649\nIP=541\n'
        'SS=f900\nSP=100\nDS=0\nES=0\nBP=0\nFLAGS=202\n',
        encoding='ascii')


with tempfile.TemporaryDirectory(prefix='gateway-picker-click-test-') as temporary:
    root = Path(temporary)
    stage = root / 'stage.walk'
    stage.write_text('COMMAND open drawer\nLABEL drawer_open\n')
    replay, click, post = study_plan(stage, points=[(280, 80), (320, 80)])
    assert replay.keys and replay.actions and replay.assertions == []
    assert click >= 22000 and post == click + 3000
    assert replay.duration_ms == post + 1000
    for broken in ([], [(280, 80), (280, 80)], [(640, 80)]):
        try:
            study_plan(stage, points=broken)
        except ValueError:
            pass
        else:
            raise AssertionError('accepted unsafe or duplicate trial coordinates')

    points_path = root / 'points.tsv'
    points_path.write_text('call\tx\ty\n1\t280\t80\n2\t300\t80\n'
                           '1\t320\t80\n1\t320\t80\n')
    assert read_points(points_path, 1) == [(280, 80), (320, 80)]
    try:
        read_points(points_path, 3)
    except ValueError as error:
        assert 'no changed picker pixels' in str(error)
    else:
        raise AssertionError('accepted an absent state ID')

    folder = root / 'fake-recordings'
    for name in ('control-a', 'control-b', 'click-001'):
        snapshot(folder / name, 'before')
        snapshot(folder / name, 'after', {0x2000: 7} if name == 'click-001' else None)
    report = compare_recordings(folder, [('click-001', 280, 80)])
    assert report['control_pre_equal'] and report['control_post_equal']
    assert report['trials'][0]['comparison_qualified']
    assert report['trials'][0]['post_diff']['changed_bytes'] == 1
    assert report['trials'][0]['post_diff']['first_offsets'] == ['0x02000']
    assert not report['trials'][0]['object_changes']['available']
    assert not report['trials'][0]['picker_trace']['available']

    (folder / 'click-001' / 'picker-follow.tsv').write_text(
        '# call=1 ms=1234 x=280 y=80 native_ax=1 group=0 slot=2 kind=3 '
        'source=room object=292 overlaps=1 verdict=match return=4100:0123\n')
    event = picker_observations(folder / 'click-001', 1000, 2000)
    assert event['available'] and len(event['calls']) == 1
    assert event['calls'][0]['object'] == 292
    assert picker_observations(folder / 'click-001', 2000, 3000)['calls'] == []

    snapshot(folder / 'control-b', 'after', {0x2000: 5})
    unstable = compare_recordings(folder, [('click-001', 280, 80)])
    assert not unstable['control_post_equal']
    assert not unstable['trials'][0]['comparison_qualified']
    snapshot(folder / 'control-b', 'after')
    snapshot(folder / 'click-001', 'before', {0x3000: 1})
    unequal = compare_recordings(folder, [('click-001', 280, 80)])
    assert not unequal['trials'][0]['same_pre_state']
    assert not unequal['trials'][0]['comparison_qualified']
    snapshot(folder / 'click-001', 'before')
    control_regs = folder / 'control-b' / 'before.regs'
    original_regs = control_regs.read_text()
    control_regs.write_text(original_regs.replace('IP=541', 'IP=542'))
    cpu_unstable = compare_recordings(folder, [('click-001', 280, 80)])
    assert not cpu_unstable['control_pre_cpu_equal']
    assert not cpu_unstable['trials'][0]['comparison_qualified']
    control_regs.write_text(original_regs)

    fake_data = root / 'gateway-original'
    fake_data.mkdir()
    (fake_data / 'GATE.EXE').write_bytes(b'fixture')
    fake_probe = root / 'gateway-probe'
    fake_probe.write_text('fixture')
    output = root / 'experiment'
    args = SimpleNamespace(script=stage, output=output, after_ms=1000,
                           settle_ms=3000, max_actions=None, through=None,
                           plan_only=False, probe=fake_probe, data=fake_data,
                           import_save=None, timeout=1, verify_overlays=False)
    captured = []

    def fake_run(options, candidate):
        name = options.output.name
        captured.append((name, list(candidate.mouse_clicks), options.probe_arg))
        snapshot(options.output, 'before')
        snapshot(options.output, 'after', {0x3000: 1} if name.startswith('click-') else None)

    with mock.patch('picker_click_trials.walkthrough_run', side_effect=fake_run):
        actual = run_study(args, [(280, 80), (320, 80)])
    assert len(captured) == 4
    assert [item[0] for item in captured] == [
        'control-a', 'control-b', 'click-001', 'click-002']
    assert all(not clicks for _, clicks, _ in captured[:2])
    assert captured[2][1][-1] == (click, 280, 80)
    assert captured[3][1][-1] == (click, 320, 80)
    assert all('--deterministic-clock' in extra and '--dump-at' in extra
               for _, _, extra in captured)
    assert actual['control_post_equal']
    assert all(trial['comparison_qualified'] for trial in actual['trials'])
    assert json.loads((output / 'comparison.json').read_text())['points'] == 2
    assert json.loads((output / 'trial-plan.json').read_text())['points'] == [
        [280, 80], [320, 80]]

    verified = []

    def fake_verify(options, candidate):
        verified.append((options.output.name, list(candidate.mouse_clicks)))

    with mock.patch('picker_click_trials.verify_recorded', side_effect=fake_verify):
        recovered = compare_existing(args, [(280, 80), (320, 80)])
    assert recovered == actual
    assert verified == [
        ('control-a', []), ('control-b', []),
        ('click-001', [(click, 280, 80)]),
        ('click-002', [(click, 320, 80)])]

    manifest_file = output / 'trial-plan.json'
    original_manifest = manifest_file.read_text()
    manifest = json.loads(original_manifest)
    manifest['points'][0][0] = 300
    manifest_file.write_text(json.dumps(manifest))
    try:
        compare_existing(args, [(280, 80), (320, 80)])
    except ValueError as error:
        assert 'differs' in str(error)
    else:
        raise AssertionError('accepted a mismatched trial manifest')
    manifest_file.write_text(original_manifest)

    stage.write_text('COMMAND open drawer\nLABEL drawer_changed\n')
    try:
        compare_existing(args, [(280, 80), (320, 80)])
    except ValueError as error:
        assert 'differs' in str(error)
    else:
        raise AssertionError('accepted modified walkthrough input')
    stage.write_text('COMMAND open drawer\nLABEL drawer_open\n')

    (fake_data / 'GATE_002.RGN').write_bytes(b'original region fixture')
    try:
        compare_existing(args, [(280, 80), (320, 80)])
    except ValueError as error:
        assert 'differs' in str(error)
    else:
        raise AssertionError('accepted changed original game data')
    (fake_data / 'GATE_002.RGN').unlink()

    # User saves placed in the data directory are not used by the isolated
    # runner: those changes must not invalidate the reference game fixture.
    (fake_data / 'GATE001.SAV').write_bytes(b'unrelated save')
    with mock.patch('picker_click_trials.verify_recorded', side_effect=fake_verify):
        assert compare_existing(args, [(280, 80), (320, 80)]) == actual

    result_file = output / 'comparison.json'
    original_result = result_file.read_text()
    altered = json.loads(original_result)
    altered['control_pre_equal'] = False
    result_file.write_text(json.dumps(altered))
    try:
        with mock.patch('picker_click_trials.verify_recorded', side_effect=fake_verify):
            compare_existing(args, [(280, 80), (320, 80)])
    except ValueError as error:
        assert 'refusing to overwrite' in str(error)
    else:
        raise AssertionError('overwrote divergent recorded trial comparison')
    result_file.write_text(original_result)

    with mock.patch('picker_click_trials.verify_recorded', side_effect=fake_verify):
        (output / 'click-002' / 'after.bin').unlink()
        try:
            compare_existing(args, [(280, 80), (320, 80)])
        except FileNotFoundError:
            pass
        else:
            raise AssertionError('accepted missing recorded guest memory')
    try:
        run_study(args, [(280, 80)])
    except ValueError as error:
        assert 'new or empty' in str(error)
    else:
        raise AssertionError('reused an existing trial output directory')

    empty_output = root / 'not-created'
    args.output = empty_output
    args.plan_only = True
    run_study(args, [(280, 80)])
    assert not empty_output.exists()

print('Gateway controlled native picker click tests passed')
