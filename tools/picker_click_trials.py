#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Run isolated Gateway mouse-click trials against two no-click controls.

Every run starts from the same reviewed walkthrough and (optional) original
save, with deterministic DOS time and its own writable state directory. The
guest is snapshotted before and after the injected click. An unstable control
pair or a different pre-click RAM snapshot blocks causal interpretation.
Neither an observed memory difference nor a native picker selection proves
that a downstream game action accepted the click.

Run under private Xvfb with a Unicorn-enabled gateway-probe. Original game
memory, saves, image dumps and traces stay under the untracked output dir.
"""

import argparse
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import re
from types import SimpleNamespace

from run_golden import (extract_native_objects, normalized_memory,
                        object_state_changes, read_registers)
from run_walkthrough import (compile_steps, parse_script, run as walkthrough_run,
                             verify_recorded)
from report_picker_follow import context_reference, parse_picker_calls
from compare_picker_follow import read_trace
from inventory_native_handlers import parse_calls


def read_points(path, capture_call):
    """Consume the verified, per-capture three-column native sample format."""
    if not 1 <= capture_call <= 128:
        raise ValueError('capture call must be 1..128')
    lines = path.read_text(encoding='ascii').splitlines()
    if not lines or lines[0] != 'call\tx\ty':
        raise ValueError('picker point list has an invalid header')
    selected = set()
    for line in lines[1:]:
        if not re.fullmatch(r'[0-9]+\t[0-9]+\t[0-9]+', line):
            raise ValueError('invalid native picker point row')
        call, x, y = (int(value) for value in line.split('\t'))
        if not 1 <= call <= 128 or not 0 <= x < 640 or not 0 <= y < 480:
            raise ValueError('native picker point outside valid call/screen range')
        if call == capture_call:
            selected.add((x, y))
    if not selected:
        raise ValueError(f'no changed picker pixels for captured call {capture_call}')
    return sorted(selected)


def study_plan(script, *, points, after_ms=1000, settle_ms=3000,
               spacing_ms=3000, max_actions=None, through=None):
    if not 1 <= len(points) <= 32 or len(points) != len(set(points)):
        raise ValueError('select 1..32 distinct trial coordinates')
    if any(not 0 <= x < 640 or not 0 <= y < 480 for x, y in points):
        raise ValueError('trial coordinate outside logical VGA screen')
    if not 100 <= after_ms <= 60000 or not 200 <= settle_ms <= 30000:
        raise ValueError('invalid guest-time offset or settle interval')
    replay = compile_steps(parse_script(script.read_text(encoding='utf-8')),
                           max_actions=max_actions, through=through,
                           spacing_ms=spacing_ms)
    click_ms = replay.duration_ms + after_ms
    post_ms = click_ms + settle_ms
    duration_ms = post_ms + 1000
    if duration_ms > 3600000:
        raise ValueError('study would exceed one guest hour')
    # The original script's assertions describe the unmodified ending.
    # A successful injected action can legitimately invalidate them.
    replay = replace(replay, duration_ms=duration_ms, assertions=[], checkpoints=[])
    return replay, click_ms, post_ms


def recording_args(output, click_ms, post_ms, *, probe, data, import_save,
                   timeout, verify_overlays):
    """Build the exact existing walkthrough-runner options, no new DOS path."""
    return SimpleNamespace(
        probe=probe, data=data, output=output, import_save=import_save,
        state_ds=None, expect_save=None, verify_overlays=verify_overlays,
        trace_navigation=False, extra_trace_entry=None, trace_byte_write=None,
        trace_object_writes=None, trace_target=None, trace_rtlink_sequence=False,
        golden_every=None, capture_labels=False, check_labels=False,
        timeout=timeout, verify_existing=False,
        probe_arg=['--deterministic-clock', '--trace-picker-follow',
                   str(output / 'picker-follow.tsv'), '--dump-at',
                   f'{click_ms - 1}:{output / "before"}', '--dump-at',
                   f'{post_ms}:{output / "after"}'])


def snapshot(prefix):
    registers = read_registers(prefix.with_suffix('.regs'))
    memory, excluded = normalized_memory(prefix.with_suffix('.bin'), registers)
    return memory, registers, excluded


def diff_memory(left, right):
    if len(left) != len(right):
        raise ValueError('normalized native RAM lengths differ')
    offsets = [index for index, (a, b) in enumerate(zip(left, right)) if a != b]
    return {'changed_bytes': len(offsets),
            'first_offsets': [f'0x{index:05x}' for index in offsets[:16]],
            'changed_pages': len({index // 4096 for index in offsets})}


def changed_objects(left, right):
    """Report only validated object IDs; never mistake decoding failures for equality."""
    try:
        left_regs = read_registers(left.with_suffix('.regs'))
        right_regs = read_registers(right.with_suffix('.regs'))
        original = extract_native_objects(left.with_suffix('.bin').read_bytes(), left_regs)
        candidate = extract_native_objects(right.with_suffix('.bin').read_bytes(), right_regs)
        return {'available': True, 'ids': object_state_changes(original, candidate)}
    except ValueError as error:
        return {'available': False, 'reason': str(error)}


def picker_observations(directory, start_ms, end_ms):
    """Read original picker selections, not a claim of action acceptance."""
    trace = directory / 'picker-follow.tsv'
    if not trace.is_file():
        return {'available': False, 'reason': 'native picker trace unavailable'}
    calls = parse_picker_calls(trace)
    # Older probe output has only original picker returns. Newer output also
    # has bounded post-return instructions, guest INT 33h reads and temporal
    # dispatch candidates. Keep missing old evidence explicit.
    follow = None
    if calls and any(line.startswith('# follow-summary ')
                     for line in trace.read_text(encoding='utf-8').splitlines()):
        try:
            follow = read_trace(trace)
        except ValueError as error:
            follow = {'invalid': str(error)}
    selected = []
    for call, event in sorted(calls.items()):
        if start_ms <= event['ms'] <= end_ms:
            result = {'call': call, 'ms': event['ms'],
                      'x': event['x'], 'y': event['y'],
                      'source': event['source'],
                      'object': event['object'], 'verdict': event['verdict'],
                      'overlaps': event['overlaps'],
                      'group': int(event['group']) if 'group' in event else None,
                      'slot': int(event['slot']) if 'slot' in event else None,
                      'kind': int(event['kind']) if 'kind' in event else None,
                      'native_ax': int(event['native_ax'])
                      if 'native_ax' in event else None}
            if isinstance(follow, tuple):
                _, endings, writes, dispatches, inputs = follow
                candidates = (dispatches or {}).get(call, [])
                result['continuation'] = {
                    'available': True,
                    'status': endings.get(call, ('unavailable', 0))[0],
                    'cpu_store_count': len(writes.get(call, [])) if writes is not None else None,
                    'object_context': context_reference(event['object'], candidates)
                                      if dispatches is not None else 'unavailable',
                    'guest_input': mouse_read_evidence(inputs, call),
                    'dispatch_candidates': [
                        {'kind': kind, 'action': action,
                         'first_object': first, 'second_object': second,
                         'target': f'{cs:04x}:{ip:04x}' if kind == 'action-table' else None,
                         'age_ms': age}
                        for kind, action, first, second, cs, ip, age in candidates]}
            else:
                result['continuation'] = {
                    'available': False,
                    'reason': follow.get('invalid', 'continuation trace unavailable')
                    if follow is not None else 'continuation trace unavailable'}
            selected.append(result)
    return {'available': True, 'calls': selected}


def mouse_read_evidence(inputs, call):
    if inputs is None:
        return {'available': False, 'reason': 'guest mouse-read trace unavailable'}
    observed = inputs.get(call)
    if observed is None:
        return {'available': True, 'observed': False}
    func, button, buttons, count, x, y, cs, ip, seq, age, coords = observed
    return {'available': True, 'observed': True, 'function': func,
            'button': button, 'buttons': buttons, 'press_count': count,
            'x': x, 'y': y, 'resume': f'{cs:04x}:{ip:04x}',
            'sequence': seq, 'age_ms': age, 'coords': coords}


def rtlink_census(directory):
    """Read the original runtime's complete section-qualified call counts."""
    path = directory / 'native.rtlink-calls'
    if not path.is_file():
        return None
    calls, totals = parse_calls(path.read_text(encoding='ascii').splitlines())
    return calls, totals


def compare_rtlink_counts(baseline, trial):
    """Full-run RTLink delta, never assumed to originate at the click time."""
    if baseline is None or trial is None:
        return {'available': False, 'reason': 'native RTLink census unavailable'}
    original, original_totals = baseline
    changed, changed_totals = trial
    deltas = []
    for key in sorted(set(original) | set(changed)):
        before = original.get(key, {}).get('calls', 0)
        after = changed.get(key, {}).get('calls', 0)
        if before != after:
            sid, cs, ip = key
            deltas.append({'target': f'{sid}:{cs:04x}:{ip:04x}',
                           'before': before, 'after': after, 'delta': after - before})
    deltas.sort(key=lambda row: (-abs(row['delta']), row['target']))
    return {'available': True,
            'manager_hits_delta': changed_totals['manager_hits'] -
                                  original_totals['manager_hits'],
            'validated_calls_delta': changed_totals['validated_calls'] -
                                     original_totals['validated_calls'],
            'unrecognized_hits_delta': changed_totals['unrecognized_manager_hits'] -
                                       original_totals['unrecognized_manager_hits'],
            'changed_targets': len(deltas), 'first_targets': deltas[:32],
            'truncated': len(deltas) > 32}


def compare_recordings(output, trial_names, *, click_ms=0, post_ms=3600000):
    a = output / 'control-a'
    b = output / 'control-b'
    initial_a, initial_regs, _ = snapshot(a / 'before')
    initial_b, initial_b_regs, _ = snapshot(b / 'before')
    final_a, final_regs, _ = snapshot(a / 'after')
    final_b, final_b_regs, _ = snapshot(b / 'after')
    control_a_calls, control_b_calls = rtlink_census(a), rtlink_census(b)
    if control_a_calls is None and control_b_calls is None:
        control_rtlink_equal = None
    else:
        control_rtlink_equal = (control_a_calls is not None and
                                control_b_calls is not None and
                                control_a_calls == control_b_calls)
    control_pre_cpu_equal = initial_regs == initial_b_regs
    control_post_cpu_equal = final_regs == final_b_regs
    initial_consistent = initial_a == initial_b and control_pre_cpu_equal
    controls_post_equal = final_a == final_b and control_post_cpu_equal
    controls_stable = controls_post_equal and control_rtlink_equal is not False
    result = {'control_pre_equal': initial_consistent,
              'control_post_equal': controls_post_equal,
              'controls_stable': controls_stable,
              'control_pre_cpu_equal': control_pre_cpu_equal,
              'control_post_cpu_equal': control_post_cpu_equal,
              'control_rtlink_equal': control_rtlink_equal,
              'control_rtlink_diff': compare_rtlink_counts(control_a_calls,
                                                           control_b_calls),
              'control_pre_diff': diff_memory(initial_a, initial_b),
              'control_post_diff': diff_memory(final_a, final_b),
              'control_picker_traces': {
                  'a': picker_observations(a, click_ms, post_ms),
                  'b': picker_observations(b, click_ms, post_ms)},
              'trials': []}
    for name, x, y in trial_names:
        trial = output / name
        trial_pre, trial_regs, _ = snapshot(trial / 'before')
        trial_post, trial_post_regs, _ = snapshot(trial / 'after')
        same_entry = initial_consistent and trial_pre == initial_a and trial_regs == initial_regs
        qualified = same_entry and controls_stable
        trial_calls = rtlink_census(trial)
        picker = picker_observations(trial, click_ms, post_ms)
        result['trials'].append({
            'name': name, 'x': x, 'y': y,
            'same_pre_state': same_entry,
            'same_pre_cpu': trial_regs == initial_regs,
            'same_post_cpu': trial_post_regs == final_regs,
            'controls_stable': controls_stable,
            'comparison_qualified': qualified,
            'pre_diff': diff_memory(initial_a, trial_pre),
            'post_diff': diff_memory(final_a, trial_post),
            'object_changes': changed_objects(a / 'after', trial / 'after')
                              if qualified else {'available': False,
                                                 'reason': 'unverified common baseline'},
            'rtlink_diff': compare_rtlink_counts(control_a_calls, trial_calls)
                           if qualified else {'available': False,
                                              'reason': 'unverified common baseline'},
            'picker_trace': picker,
            'picker_calls_at_injected_point': [
                item['call'] for item in picker.get('calls', [])
                if item['x'] == x and item['y'] == y],
            'interpretation': ('candidate click-associated state difference only'
                               if qualified else 'inconclusive: baseline/pre-state mismatch'),
        })
    return result


def study_manifest(args, replay, click_ms, post_ms, points):
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest() if path else None

    # The original executable reads many sibling data files, not just its
    # launcher. Include the full, read-only game fixture in the identity;
    # writable DOS save slots live under each trial's private state-dir.
    game_files = {path.name: digest(path)
                  for path in sorted(args.data.iterdir()) if path.is_file() and
                  not re.fullmatch(r'GATE[0-9]{3}\.SAV', path.name, re.IGNORECASE)}
    return {'format': 'gateway-picker-click-trials-v1',
            'script': args.script.name, 'script_sha256': digest(args.script),
            'probe_sha256': digest(args.probe),
            'game_files_sha256': hashlib.sha256(json.dumps(
                game_files, sort_keys=True, separators=(',', ':')).encode('ascii')).hexdigest(),
            'import_save_sha256': digest(args.import_save),
            'scheduled_keys_sha256': hashlib.sha256(json.dumps(
                replay.keys, separators=(',', ':')).encode('ascii')).hexdigest(),
            'script_mouse_clicks': replay.mouse_clicks,
            'actions': len(replay.actions), 'duration_ms': replay.duration_ms,
            'click_ms': click_ms, 'post_ms': post_ms,
            'points': [list(point) for point in points],
            'verify_overlays': args.verify_overlays}


def save_report(output, report):
    result_path = output / 'comparison.json'
    if result_path.exists():
        if json.loads(result_path.read_text(encoding='utf-8')) != report:
            raise ValueError('recorded trial comparison changed; refusing to overwrite')
    else:
        result_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'Verified controlled comparison: {result_path}')


def complete_report(args, points, click_ms, post_ms):
    trials = [(f'click-{number:03d}', x, y)
              for number, (x, y) in enumerate(points, 1)]
    report = compare_recordings(args.output, trials, click_ms=click_ms, post_ms=post_ms)
    report.update({'script': args.script.name, 'click_ms': click_ms,
                   'post_ms': post_ms, 'controls': 2, 'points': len(points)})
    save_report(args.output, report)
    return report


def compare_existing(args, points):
    replay, click_ms, post_ms = study_plan(
        args.script, points=points, after_ms=args.after_ms, settle_ms=args.settle_ms,
        max_actions=args.max_actions, through=args.through)
    recorded = json.loads((args.output / 'trial-plan.json').read_text(encoding='utf-8'))
    expected = study_manifest(args, replay, click_ms, post_ms, points)
    if recorded != expected:
        raise ValueError('recorded picker trial plan differs from current inputs')
    for name, click in [('control-a', None), ('control-b', None)] + [
            (f'click-{index:03d}', point)
            for index, point in enumerate(points, 1)]:
        output = args.output / name
        candidate = replace(replay, mouse_clicks=list(replay.mouse_clicks))
        if click:
            candidate.mouse_clicks.append((click_ms, *click))
        options = recording_args(output, click_ms, post_ms,
                                 probe=args.probe, data=args.data,
                                 import_save=args.import_save, timeout=args.timeout,
                                 verify_overlays=args.verify_overlays)
        verify_recorded(options, candidate)
    return complete_report(args, points, click_ms, post_ms)


def run_study(args, points):
    if args.output.exists() and any(args.output.iterdir()):
        raise ValueError('trial output directory must be new or empty')
    replay, click_ms, post_ms = study_plan(
        args.script, points=points, after_ms=args.after_ms, settle_ms=args.settle_ms,
        max_actions=args.max_actions, through=args.through)
    trials = [(f'click-{number:03d}', x, y)
              for number, (x, y) in enumerate(points, 1)]
    print(f'Plan: {len(replay.actions)} walkthrough actions, {click_ms}ms click, '
          f'{post_ms}ms comparison, {len(trials)} isolated points + 2 controls')
    if args.plan_only:
        for name, x, y in trials:
            print(f'  {name}: ({x},{y})')
        return None
    if not args.probe.is_file() or not (args.data / 'GATE.EXE').is_file():
        raise ValueError('original GATE.EXE or Unicorn gateway-probe missing')
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'trial-plan.json').write_text(json.dumps(
        study_manifest(args, replay, click_ms, post_ms, points), indent=2) + '\n',
        encoding='utf-8')
    for name, click in [('control-a', None), ('control-b', None)] + [
            (label, (x, y)) for label, x, y in trials]:
        output = args.output / name
        candidate = replace(replay, mouse_clicks=list(replay.mouse_clicks))
        if click:
            candidate.mouse_clicks.append((click_ms, *click))
        options = recording_args(output, click_ms, post_ms,
                                 probe=args.probe, data=args.data,
                                 import_save=args.import_save, timeout=args.timeout,
                                 verify_overlays=args.verify_overlays)
        print(f'Running {name}...')
        walkthrough_run(options, candidate)
    return complete_report(args, points, click_ms, post_ms)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('script', type=Path, help='reviewed Gateway walkthrough stage')
    parser.add_argument('--points', type=Path, help='--oracle-points TSV from capture comparison')
    parser.add_argument('--capture-call', type=int, help='entry state ID for --points')
    parser.add_argument('--point', action='append', metavar='X:Y', default=[],
                        help='manual logical VGA position (repeatable)')
    parser.add_argument('--max-points', type=int, default=6)
    parser.add_argument('--after-ms', type=int, default=1000,
                        help='delay after the reviewed walkthrough stage')
    parser.add_argument('--settle-ms', type=int, default=3000,
                        help='post-click observation interval')
    parser.add_argument('--max-actions', type=int)
    parser.add_argument('--through', help='stop walkthrough at a reviewed label')
    parser.add_argument('--probe', type=Path, default=Path('build/gateway-probe'))
    parser.add_argument('--data', type=Path, default=Path('../gateway-dos'))
    parser.add_argument('--import-save', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/picker-click-trials'))
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('--verify-overlays', action='store_true')
    parser.add_argument('--plan-only', action='store_true')
    parser.add_argument('--compare-existing', action='store_true',
                        help='validate completed native recordings and recompute without reruns')
    args = parser.parse_args()
    try:
        if (args.points is None) != (args.capture_call is None) or \
                (args.points and args.point):
            raise ValueError('use --points and --capture-call together, or manual --point')
        if not 1 <= args.max_points <= 32:
            raise ValueError('--max-points must be 1..32')
        if args.points:
            points = read_points(args.points, args.capture_call)
        else:
            points = []
            for value in args.point:
                if not re.fullmatch(r'[0-9]+:[0-9]+', value):
                    raise ValueError('invalid manual X:Y coordinate')
                x, y = (int(part) for part in value.split(':'))
                points.append((x, y))
        if not points:
            raise ValueError('no picker trial coordinates')
        points = sorted(set(points))[:args.max_points]
        if args.plan_only and args.compare_existing:
            raise ValueError('--plan-only and --compare-existing are mutually exclusive')
        if args.compare_existing:
            compare_existing(args, points)
        else:
            run_study(args, points)
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
