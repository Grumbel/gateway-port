#!/usr/bin/env python3
"""Run a batch of bounded native Gateway action replays and summarize writes.

Each run is an independent DOS replay with a fresh state directory. The
resulting report describes *observed* changes; an AX=1 action return means
handled, not necessarily accepted. These experiments do not validate the
standalone host StoryModel or cover complete gameplay.
"""
import argparse
from pathlib import Path
import shlex
import subprocess
import sys

from summarize_action_effects import collect_effects
from summarize_admission_trace import summarize as summarize_admission


SLOTS = (18, 21, 25, 28, 31, 34, 37)
SEQUENCES = {
    'baseline': ('open drawer', 'look', 'take book', 'inventory',
                 'drop book', 'close drawer', 'look'),
    'repeat': ('open drawer', 'close drawer', 'open drawer', 'take book',
               'drop book', 'take book', 'inventory'),
    'open-twice': ('open drawer', 'open drawer', 'close drawer', 'close drawer',
                   'open drawer', 'close drawer', 'inventory'),
    'close-first': ('close drawer', 'open drawer', 'close drawer', 'open drawer',
                    'open drawer', 'close drawer', 'inventory'),
    'take-twice': ('open drawer', 'take book', 'take book', 'drop book',
                   'drop book', 'take book', 'inventory'),
    'take-before-open': ('take book', 'open drawer', 'take book', 'drop book',
                         'take book', 'drop book', 'inventory'),
    # New discriminating probes after the original six-run matrix: is a
    # previously seen book still inaccessible behind a closed drawer, and
    # does room-level visibility survive closing the now-empty drawer?
    'closed-after-reveal': ('open drawer', 'close drawer', 'take book',
                            'open drawer', 'take book', 'drop book', 'look'),
    'room-after-close': ('open drawer', 'take book', 'close drawer',
                         'drop book', 'take book', 'inventory', 'look'),
}
DRIVER = Path(__file__).with_name('trace_game_logic.py')


def summarize_trace(text, commands):
    """Render a seven-slot table; don't infer successful actions from AX=1."""
    if ' changed=' not in text:
        raise ValueError('trace lacks state-write annotations (--trace-state-writes)')
    records = collect_effects(text.splitlines())
    if not records:
        raise ValueError('no native action dispatches in trace')
    rows = ['| Guest slot | Typed command | Dispatcher return | Changed object state |',
            '| ---: | --- | --- | --- |']
    by_slot = {slot: [] for slot in SLOTS}
    for record in records:
        if record['ms'] < SLOTS[0] * 1000 - 500:
            continue  # Startup's initial LOOK is not a scripted slot.
        slot = min(SLOTS, key=lambda second: abs(record['ms'] - second * 1000))
        by_slot[slot].append(record)
    for slot, typed in zip(SLOTS, commands):
        observed = by_slot[slot]
        result = ', '.join(f"0x{r['returned']:x}" if r['returned'] is not None
                           else '?' for r in observed) or '(no dispatch observed)'
        effects = []
        for record in observed:
            for (_, operation, obj, arg), changed in zip(record['effects'],
                                                         record['effect_changes']):
                if changed is True:
                    effects.append(f'{operation}(0x{obj:03x},0x{arg:02x})')
        rows.append(f"| {slot}s | {typed} | {result} | {', '.join(effects) or '—'} |")
    return '\n'.join(rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('build/action-matrix'),
                        help='new output directory (existing directories are preserved)')
    parser.add_argument('--probe', type=Path, help='override gateway-probe binary')
    parser.add_argument('--data', type=Path, help='override directory containing GATE.EXE')
    parser.add_argument('--only', help='comma-separated replay names; defaults to all')
    parser.add_argument('--trace-admission', action='store_true',
                        help='also inspect resident selector callbacks before action dispatch')
    parser.add_argument('--list', action='store_true', help='list replays and exit')
    parser.add_argument('--dry-run', action='store_true',
                        help='show commands without launching or writing anything')
    args = parser.parse_args(argv)
    if args.list:
        for name, commands in SEQUENCES.items():
            print(f'{name}: {", ".join(commands)}')
        return 0
    names = args.only.split(',') if args.only else list(SEQUENCES)
    if not names or any(name not in SEQUENCES for name in names) or len(names) != len(set(names)):
        parser.error(f'--only requires unique names from: {", ".join(SEQUENCES)}')
    if args.output.exists() and not args.dry_run:
        parser.error(f'{args.output} already exists; choose a fresh --output to preserve old traces')
    commands = []
    for name in names:
        command = [sys.executable, str(DRIVER), '--scenario', 'objects',
                   '--native-only', '--verify-overlays', '--trace-state-writes',
                   '--command-sequence', ','.join(SEQUENCES[name]),
                   '--output', str(args.output / name)]
        if args.probe:
            command += ['--probe', str(args.probe)]
        if args.data:
            command += ['--data', str(args.data)]
        if args.trace_admission:
            command += ['--trace-admission']
        commands.append((name, command))
    if args.dry_run:
        for name, command in commands:
            print(f'{name}: {shlex.join(command)}')
        return 0
    args.output.mkdir(parents=True)
    report = ['# Gateway native action replay matrix', '',
              'Each scenario is a fresh 42-second guest replay with the same initial LOOK.',
              'The table lists **verified changed bytes only** from native DOS services;',
              'the dispatcher return indicates handling, **not** action success.',
              'No object-specific event effects or uninstrumented writes are claimed.', '']
    for name, command in commands:
        print(f'Running {name} ({len(report)} report lines so far)...', flush=True)
        run_dir = args.output / name
        run_dir.mkdir()
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        (run_dir / 'driver.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            report.extend([f'## {name}: replay failed', '',
                           f'See `{name}/driver.log`; process exited with {result.returncode}.', ''])
            (args.output / 'report.md').write_text('\n'.join(report) + '\n')
            print(result.stdout + result.stderr, file=sys.stderr)
            return result.returncode
        try:
            summary = summarize_trace((run_dir / 'native.trace').read_text(),
                                      SEQUENCES[name])
        except (OSError, ValueError) as error:
            report.extend([f'## {name}: trace cannot be summarized', '', str(error), ''])
            (args.output / 'report.md').write_text('\n'.join(report) + '\n')
            print(f'{name}: {error}', file=sys.stderr)
            return 1
        report.extend([f'## {name}', '', summary, '',
                       f'Full trace: `{name}/native.trace`', ''])
        if args.trace_admission:
            try:
                admission = summarize_admission((run_dir / 'native.trace').read_text().splitlines(),
                                                SEQUENCES[name])
                report.extend(['### Earlier selector stage (raw observations)', '', admission, '',
                               'AX is an unclassified callback result, not a proven permission bit.',
                               'Input/trace association is by guest time slot only.', ''])
            except ValueError as error:
                report.extend(['### Selector-stage trace unavailable', '', str(error), ''])
        (args.output / 'report.md').write_text('\n'.join(report) + '\n')
        print(f'{name}: complete', flush=True)
    print(f'Batch report: {args.output / "report.md"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
