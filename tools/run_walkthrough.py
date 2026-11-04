#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Compile a reviewed Gateway walkthrough into BIOS keys and replay the original DOS game.

The plain-text walkthrough DSL deliberately separates parser commands from raw
keystrokes and unknown mouse/menu actions. It does not guess puzzle decisions.
"""

import argparse
from dataclasses import dataclass
from html.parser import HTMLParser
import pathlib
import re
import shutil
import subprocess

from story_scenarios import schedule


MAX_DURATION_MS = 3_600_000
MAX_KEYS = 100_000
KEYS = {'space': 0x3920, 'enter': 0x1c0d, 'escape': 0x011b,
        'up': 0x4800, 'down': 0x5000, 'left': 0x4b00, 'right': 0x4d00}
OPERATIONS = {'COMMAND', 'ANSWER', 'TYPE', 'MENU_KEYS', 'KEY', 'WAIT_MS',
              'MOUSE_CLICK', 'LABEL', 'ASSERT_LOCATION', 'MANUAL'}


@dataclass(frozen=True)
class Step:
    operation: str
    argument: str
    line: int


def parse_script(source):
    steps = []
    labels = set()
    for line_number, raw in enumerate(source.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        operation, _, argument = line.partition(' ')
        operation, argument = operation.upper(), argument.strip()
        if operation not in OPERATIONS or not argument:
            raise ValueError(f'line {line_number}: unknown or incomplete directive')
        if operation in ('COMMAND', 'TYPE'):
            if len(argument) > 64 or any(not 32 <= ord(char) <= 126 for char in argument):
                raise ValueError(f'line {line_number}: input must be 1–64 printable ASCII bytes')
        elif operation == 'ANSWER':
            if argument.lower() not in ('yes', 'no', 'y', 'n'):
                raise ValueError(f'line {line_number}: ANSWER requires yes/no/y/n')
        elif operation == 'MENU_KEYS':
            _menu_keys(argument, line_number)
        elif operation == 'KEY':
            if argument.lower() not in KEYS and not re.fullmatch(r'0x[0-9a-fA-F]{1,4}', argument):
                raise ValueError(f'line {line_number}: unknown BIOS key {argument!r}')
        elif operation == 'MOUSE_CLICK':
            values = argument.split()
            if len(values) != 2 or any(not item.isascii() or not item.isdecimal()
                                       for item in values) or not 0 <= int(values[0]) < 640 or \
                    not 0 <= int(values[1]) < 480:
                raise ValueError(f'line {line_number}: MOUSE_CLICK needs DOS X=0–639 Y=0–479')
        elif operation == 'WAIT_MS':
            if not argument.isascii() or not argument.isdecimal() or not 1 <= int(argument) <= 600000:
                raise ValueError(f'line {line_number}: WAIT_MS must be 1–600000')
        elif operation == 'LABEL':
            if not re.fullmatch(r'[A-Za-z0-9_-]{1,48}', argument) or argument in labels:
                raise ValueError(f'line {line_number}: invalid/duplicate label')
            labels.add(argument)
        elif operation == 'ASSERT_LOCATION':
            values = argument.split()
            if len(values) != 2 or any(not re.fullmatch(r'(?:0x)?[0-9a-fA-F]{1,4}', value)
                                           for value in values):
                raise ValueError(f'line {line_number}: expected hex object ID and location')
            if not 1 <= int(values[0], 16) <= 734:
                raise ValueError(f'line {line_number}: object ID must be 1–2de')
        steps.append(Step(operation, argument, line_number))
    if not steps:
        raise ValueError('empty walkthrough script')
    return steps


def convert_text(source):
    """Conservatively extract only explicitly marked commands/menu instructions.

    A user-provided guide is input, not a redistributable bundled asset. Any
    unrecognized UI operation remains MANUAL and halts automatic replay.
    """
    steps = []
    for line in source.splitlines():
        line = line.strip()
        prompt = re.match(r'^[}>]\s*(.+)$', line)
        typed = re.match(r'^\*{3}\s*Type:\s*(.*?)\s*\*{3}$', line, re.IGNORECASE)
        pressed = re.match(r'^\*{3}\s*Press:\s*(.*?)\s*\*{3}$', line, re.IGNORECASE)
        answered = re.match(r'^.*\?\s*}\s*(Y|N|YES|NO)\s*$', line, re.IGNORECASE)
        if prompt:
            value = prompt.group(1).strip()
            steps.append(f'COMMAND {value}' if _printable(value) else
                         'MANUAL Unrecognized parser input')
        elif typed:
            value = typed.group(1).strip()
            # Original Gateway PV commset: a comma-separated guide sequence
            # denotes individual menu keys, with no Enter after each. Only
            # *single-character* items are known to obey this convention.
            if ',' in value:
                try:
                    parts = _menu_keys(value, 0)
                    steps.append('MENU_KEYS ' + ','.join(parts))
                except ValueError:
                    steps.append('MANUAL Menu sequence: ' + value[:120])
            else:
                steps.append(f'TYPE {value}' if _printable(value) else
                             'MANUAL Unrecognized menu text')
        elif pressed:
            # A Legend UI "Press" instruction may mean clicking a screen
            # region, not typing a letter; do not silently treat it as a key.
            steps.append('MANUAL UI press: ' + pressed.group(1).strip()[:120])
        elif answered:
            steps.append('ANSWER ' + answered.group(1).upper())
    if not steps:
        raise ValueError('no parser prompts or menu directives found in supplied text')
    return '# Extracted from a user-provided guide; REVIEW EVERY STEP.\n' + '\n'.join(steps) + '\n'


def extract_scribd_html(source):
    """Extract only document text from the public HTML wrapper, never site chrome.

    The external guide is a private *input* to the converter; the extracted
    guide is not bundled or distributed with this project.
    """
    class DocumentText(HTMLParser):
        def __init__(self):
            super().__init__(convert_charrefs=True)
            self.depth = 0
            self.found = False
            self.words = []

        def handle_starttag(self, tag, attrs):
            if not self.depth and tag == 'div' and ('data-e2e', 'bot-text') in attrs:
                self.depth = 1
                self.found = True
            elif self.depth:
                if tag == 'div':
                    self.depth += 1
                elif tag == 'br':
                    self.words.append('\n')

        def handle_endtag(self, tag):
            if self.depth:
                if tag == 'div':
                    self.depth -= 1
                elif tag in ('h1', 'h2', 'h3', 'p'):
                    self.words.append('\n')

        def handle_data(self, data):
            if self.depth:
                self.words.append(data)

    parser = DocumentText()
    parser.feed(source)
    if not parser.found:
        raise ValueError('Scribd HTML has no bot-text document; supply plaintext instead')
    return ''.join(parser.words)


def _printable(value):
    return 0 < len(value) <= 64 and all(32 <= ord(char) <= 126 for char in value)


def _menu_keys(argument, line_number):
    parts = [part.strip() for part in argument.split(',')]
    if not 2 <= len(parts) <= 64 or any(len(part) != 1 or not part.isascii() or
                                          not part.isalnum() for part in parts):
        raise ValueError(f'line {line_number}: MENU_KEYS requires 2–64 single '
                         'ASCII alphanumeric menu choices')
    return parts


@dataclass
class Replay:
    keys: list
    mouse_clicks: list
    actions: list
    assertions: list
    checkpoints: list
    duration_ms: int


@dataclass(frozen=True)
class Checkpoint:
    label: str
    time_ms: int
    assertions: tuple
    action_index: int = 0
    command_index: int = 0


def compile_steps(steps, max_actions=None, through=None, spacing_ms=3000, idle_ms=1500,
                  golden_every=None):
    """A deterministic replay prefix. Assertions describe its *final* state.

    The --through prefix isolates one LABEL; --check-labels captures and
    verifies all prior label assertions in one uninterrupted native run.
    """
    if not 100 <= spacing_ms <= 120000 or not 1 <= idle_ms <= 120000:
        raise ValueError('invalid spacing or idle duration')
    if max_actions is not None and max_actions < 1:
        raise ValueError('max-actions must be positive')
    if max_actions is not None and through is not None:
        raise ValueError('--max-actions and --through cannot be combined')
    if golden_every is not None and not 1 <= golden_every <= 10000:
        raise ValueError('--golden-every must be between 1 and 10000')
    if through is not None and not any(s.operation == 'LABEL' and s.argument == through for s in steps):
        raise ValueError(f'unknown label {through!r}')
    keys = schedule('look') + [(time, 0x3920) for time in (15000, 16000, 17000)]
    mouse_clicks = []
    # The historical splash/menu bootstrap is exactly the existing 'puzzle'
    # scenario. By 18000ms the parser accepts command text.
    cursor = 18000
    actions = []
    assertions = []
    checkpoints = []
    command_count = 0
    last_action_finished = 17000
    for step in steps:
        if max_actions is not None and len(actions) >= max_actions:
            break
        op, arg = step.operation, step.argument
        if op == 'LABEL':
            # Capture one guest millisecond before the next planned action;
            # scheduled input at that action's start cannot affect the dump.
            checkpoints.append(Checkpoint(arg, cursor - 1, tuple(assertions),
                                          len(actions), command_count))
            if through == arg:
                break
            continue
        if op == 'ASSERT_LOCATION':
            obj, location = arg.split()
            assertions.append((int(obj, 16), int(location, 16), step.line))
            continue
        if op == 'WAIT_MS':
            assertions = []  # only the terminal checkpoint's checks apply
            cursor += int(arg)
            continue
        if op == 'MANUAL':
            raise ValueError(f'line {step.line}: human review required: {arg}')
        assertions = []
        start = cursor
        if op == 'MENU_KEYS':
            parts = _menu_keys(arg, step.line)
            keys.extend((start + index * spacing_ms, ord(char))
                        for index, char in enumerate(parts))
            end = start + (len(parts) - 1) * spacing_ms
            cursor = end + spacing_ms
        elif op == 'MOUSE_CLICK':
            x, y = (int(value) for value in arg.split())
            mouse_clicks.append((start, x, y))
            end = start + 100  # Press and release, for original INT 33h mouse polling.
        elif op in ('COMMAND', 'ANSWER', 'TYPE'):
            keys.extend((start + index * 30, ord(char)) for index, char in enumerate(arg))
            end = start + len(arg) * 30
            if op in ('COMMAND', 'ANSWER'):
                keys.append((end, KEYS['enter']))
            else:
                end -= 30
        else:
            keys.append((start, KEYS.get(arg.lower(), int(arg, 16)
                         if arg.lower().startswith('0x') else 0)))
            end = start
        actions.append(step)
        if op == 'COMMAND':
            command_count += 1
        last_action_finished = end
        cursor = max(cursor, start + spacing_ms, end + 1)
        if golden_every and op == 'COMMAND' and command_count % golden_every == 0:
            checkpoints.append(Checkpoint(f'golden-command-{command_count:06d}',
                                          cursor - 1, (), len(actions), command_count))
    if not actions:
        raise ValueError('script prefix has no executable actions')
    # Only the last assertions apply to the final snapshot. Earlier checks
    # are retained in labeled checkpoints for --check-labels.
    if not all(keys[index][0] < keys[index + 1][0] for index in range(len(keys) - 1)):
        raise ValueError('keyboard timestamps overlap; increase spacing')
    if len(keys) > MAX_KEYS or len(mouse_clicks) > 4096 or \
            max(cursor, last_action_finished + idle_ms) > MAX_DURATION_MS:
        raise ValueError('walkthrough exceeds 100000 keys or one guest hour; split into stages')
    return Replay(keys, mouse_clicks, actions, assertions, checkpoints,
                  max(cursor, last_action_finished + idle_ms))


def check_locations(snapshot, assertions, ds=None):
    from inspect_object_state import inspect_objects

    regs = dict(line.split('=', 1) for line in snapshot.with_suffix('.regs').read_text().splitlines())
    # A timed checkpoint can interrupt a native callback while DS names a
    # temporary code/overlay segment. The object's resident data segment is
    # then often preserved in ES. Accept either only after the exact 734-entry
    # table validates; never silently infer relations from an unknown layout.
    candidates = [ds] if ds is not None else list(dict.fromkeys(
        int(regs[name], 16) for name in ('DS', 'ES') if name in regs))
    memory = snapshot.with_suffix('.bin').read_bytes()
    objects = None
    for current_ds in candidates:
        try:
            found = inspect_objects(memory, current_ds)
        except ValueError:
            continue
        if len(found) == 734:
            objects = found
            break
    if objects is None:
        raise ValueError('unexpected object layout: no verified 734-object table at DS/ES')
    for object_id, expected, line in assertions:
        observed = objects[object_id - 1].get('location')
        if observed != expected:
            raise ValueError(f'line {line}: object {object_id:x} location expected '
                             f'{expected:x}, observed {observed!r}')


def native_save_description(path):
    """Read the original 33-byte fixed-width save-game description header."""
    if not re.fullmatch(r'GATE[0-9]{3}\.SAV', path.name.upper()):
        raise ValueError('original save must be named GATE###.SAV')
    data = path.read_bytes()
    if not 34 <= len(data) <= 1024 * 1024 or any(c < 32 or c > 126 for c in data[:33]):
        raise ValueError(f'{path}: unrecognized Gateway saved-game header')
    return data[:33].decode('ascii').rstrip()


def import_native_save(source, state_dir):
    """Install one original save in an isolated DOS state directory."""
    description = native_save_description(source)
    state_dir.mkdir(parents=True, exist_ok=True)
    destination = state_dir / source.name.upper()
    if source.resolve() == destination.resolve():
        raise ValueError('save source and destination must differ')
    if destination.exists():
        if destination.read_bytes() != source.read_bytes():
            raise ValueError(f'cannot overwrite different DOS save {destination}')
    else:
        shutil.copyfile(source, destination)
    return description


def verify_recorded(args, replay, previous_saves=None):
    """Recheck a native recording without mutating or restarting the guest."""
    key_path = args.output / 'replay.keys'
    expected_keys = ''.join(f'{ms}:{key:04x}\n' for ms, key in replay.keys)
    if key_path.read_text(encoding='ascii') != expected_keys:
        raise ValueError('recorded replay.keys differs from the reviewed script')
    prefix = args.output / 'native'
    trace = prefix.with_suffix('.trace').read_text().splitlines()
    recorded = sum(line.startswith('key ms=') for line in trace)
    if recorded != len(replay.keys):
        raise RuntimeError(f'guest stopped before all keys were delivered: '
                           f'{recorded}/{len(replay.keys)}; see native.dos')
    mouse_presses = sum(line.startswith('mouse ms=') and line.endswith('left=1')
                        for line in trace)
    if mouse_presses != len(replay.mouse_clicks):
        raise RuntimeError(f'guest delivered only {mouse_presses}/{len(replay.mouse_clicks)} '
                           'scheduled mouse clicks')
    if args.expect_save:
        state_dir = args.output / 'state'
        found = [p.name for p in state_dir.glob('GATE[0-9][0-9][0-9].SAV')
                 if (previous_saves is None or
                     p.stat().st_mtime_ns != previous_saves.get(p.name)) and
                 native_save_description(p) == args.expect_save]
        if not found:
            qualified = 'newly written ' if previous_saves is not None else ''
            raise ValueError(f'no {qualified}native save named {args.expect_save!r}')
        print('Native saved-game slot verified: ' + ', '.join(sorted(found)))
    if replay.assertions:
        check_locations(prefix, replay.assertions, args.state_ds)
    checked = 0
    if args.check_labels:
        for checkpoint in replay.checkpoints:
            if checkpoint.assertions:
                check_locations(args.output / ('checkpoint-' + checkpoint.label),
                                checkpoint.assertions, args.state_ds)
                checked += len(checkpoint.assertions)
    if args.capture_labels or args.check_labels or getattr(args, 'golden_every', None):
        for checkpoint in replay.checkpoints:
            for suffix in ('.bin', '.regs', '.ppm'):
                path = (args.output / ('checkpoint-' + checkpoint.label)).with_suffix(suffix)
                if not path.is_file():
                    raise ValueError(f'missing scheduled native snapshot {path}')
    from inventory_native_handlers import parse_calls
    _, census = parse_calls(prefix.with_suffix('.rtlink-calls').read_text().splitlines())
    print(f'Verified {len(replay.actions)} input actions / {recorded} BIOS keys '
          f'/ {mouse_presses} native mouse clicks in {replay.duration_ms} guest ms')
    print(f'RTLink: {census["validated_calls"]} validated calls, '
          f'{census["distinct_targets"]} distinct section-qualified targets')
    print(f'Asserted {len(replay.assertions)} final object locations; output in {args.output}')
    if args.capture_labels or args.check_labels or getattr(args, 'golden_every', None):
        print(f'Captured {len(replay.checkpoints)} native snapshots; '
              f'checked {checked} intermediate object locations')


def run(args, replay):
    if args.verify_existing:
        verify_recorded(args, replay)
        return
    if not args.probe.is_file() or not (args.data / 'GATE.EXE').is_file():
        raise ValueError('original GATE.EXE or compiled gateway-probe missing')
    args.output.mkdir(parents=True, exist_ok=True)
    state_dir = args.output / 'state'
    if args.import_save:
        print(f'Imported original save: {import_native_save(args.import_save, state_dir)}')
    previous_saves = {p.name: p.stat().st_mtime_ns for p in state_dir.glob('GATE[0-9][0-9][0-9].SAV')}
    key_path = args.output / 'replay.keys'
    key_path.write_text(''.join(f'{ms}:{key:04x}\n' for ms, key in replay.keys), encoding='ascii')
    prefix = args.output / 'native'
    command = [str(args.probe.resolve()), str((args.data / 'GATE.EXE').resolve()),
               '--press', 'escape', '--state-dir', str(state_dir),
               '--milliseconds', str(replay.duration_ms), '--key-script', str(key_path),
               '--trace-research', str(prefix.with_suffix('.trace')),
               '--trace-entry', '147a6', '--trace-entry', '28d2b',
               '--trace-rtlink-calls', str(prefix.with_suffix('.rtlink-calls')),
               '--dump-prefix', str(prefix)]
    if getattr(args, 'golden_every', None):
        command.append('--deterministic-clock')
    if args.verify_overlays:
        command.append('--verify-overlays')
    if args.trace_navigation:
        for entry in ('447e0', '14ed6', '15077', '14742', '14759', '1475e', '1507c'):
            command += ['--trace-entry', entry]
    for entry in args.extra_trace_entry or []:
        command += ['--trace-entry', entry]
    for address in args.trace_byte_write or []:
        command += ['--trace-byte-write', address]
    if args.trace_object_writes:
        command += ['--object-state', 'verify', '--trace-object-writes',
                    args.trace_object_writes, '--trace-entry', '11e6c']
    if args.trace_target:
        command += ['--trace-rtlink-events', str(prefix.with_suffix('.rtlink-events'))]
        for target in args.trace_target:
            command += ['--trace-rtlink-target', target]
    if getattr(args, 'trace_rtlink_sequence', False):
        command += ['--trace-rtlink-sequence', str(prefix.with_suffix('.rtlink-sequence'))]
    for ms, x, y in replay.mouse_clicks:
        command += ['--mouse-click-at', f'{ms}:{x}:{y}']
    command += getattr(args, 'probe_arg', [])
    if args.capture_labels or args.check_labels or getattr(args, 'golden_every', None):
        if len(replay.checkpoints) > 32:
            raise ValueError('--capture-labels supports up to 32 selected LABELs per run')
        for checkpoint in replay.checkpoints:
            command += ['--dump-at',
                        f'{checkpoint.time_ms}:{args.output / ("checkpoint-" + checkpoint.label)}']
    with prefix.with_suffix('.out').open('w') as out, prefix.with_suffix('.dos').open('w') as err:
        try:
            subprocess.run(command, stdout=out, stderr=err, timeout=args.timeout, check=True)
        except subprocess.CalledProcessError as error:
            # The probe's stderr is redirected into native.dos. Expose its
            # tail here so a failed research hook does not look like an
            # unexplained generic subprocess error.
            err.flush()
            log_path = prefix.with_suffix('.dos')
            tail = log_path.read_text(encoding='utf-8', errors='replace').splitlines()[-20:]
            detail = '\n'.join(tail) if tail else '(no probe diagnostics)'
            raise RuntimeError(f'Gateway probe exited with status {error.returncode}; '
                               f'last lines of {log_path}:\n{detail}') from error
    verify_recorded(args, replay, previous_saves)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest='mode', required=True)
    convert = subcommands.add_parser('convert', help='extract marked parser/menu lines from a supplied guide')
    convert.add_argument('guide', type=pathlib.Path)
    convert.add_argument('script', type=pathlib.Path)
    convert.add_argument('--scribd-html', action='store_true',
                         help='extract the document from a locally downloaded Scribd HTML page')
    replay = subcommands.add_parser('run', help='compile and replay an edited walkthrough DSL')
    replay.add_argument('script', type=pathlib.Path)
    replay.add_argument('--probe', type=pathlib.Path, default=pathlib.Path('build/gateway-probe'))
    replay.add_argument('--data', type=pathlib.Path, default=pathlib.Path('../gateway-dos'))
    replay.add_argument('--output', type=pathlib.Path, default=pathlib.Path('build/walkthrough'))
    replay.add_argument('--max-actions', type=int)
    replay.add_argument('--through', metavar='LABEL')
    replay.add_argument('--spacing-ms', type=int, default=3000)
    replay.add_argument('--idle-ms', type=int, default=1500)
    replay.add_argument('--state-ds', type=lambda value: int(value, 16), metavar='HEX')
    replay.add_argument('--import-save', type=pathlib.Path, metavar='GATE###.SAV',
                        help='copy a prior original save into this isolated DOS state directory')
    replay.add_argument('--expect-save', metavar='DESCRIPTION',
                        help='require a newly written DOS saved game with this description')
    replay.add_argument('--verify-overlays', action='store_true')
    replay.add_argument('--trace-navigation', action='store_true')
    replay.add_argument('--extra-trace-entry', action='append', metavar='LINEARHEX',
                        help='capture additional native service entry stacks (repeatable)')
    replay.add_argument('--trace-byte-write', action='append', metavar='LINEARHEX',
                        help='observe guest byte mutations (up to 16, within 64 bytes)')
    replay.add_argument('--trace-object-writes', metavar='OBJECTHEX',
                        help='verify state services and watch one native object record')
    replay.add_argument('--trace-target', action='append', metavar='SECTION:CS:IP')
    replay.add_argument('--trace-rtlink-sequence', action='store_true',
                        help='log every validated RTLink target/caller and guest millisecond')
    replay.add_argument('--capture-labels', action='store_true',
                        help='save a native framebuffer, RAM, and CPU snapshot at each LABEL')
    replay.add_argument('--check-labels', action='store_true',
                        help='also validate ASSERT_LOCATION before each LABEL in one run')
    replay.add_argument('--golden-every', type=int, metavar='N',
                        help='capture extra RAM checkpoints after every N parser commands')
    replay.add_argument('--probe-arg', action='append', default=[], metavar='ARG',
                        help='forward one backend argument to gateway-probe (repeatable)')
    replay.add_argument('--timeout', type=int, default=1800, help='wall-clock timeout, seconds')
    replay.add_argument('--plan-only', action='store_true', help='validate and show planned replay without execution')
    replay.add_argument('--verify-existing', action='store_true',
                        help='check an existing recording and saved-game slot without rerunning DOS')
    args = parser.parse_args()
    try:
        if args.mode == 'convert':
            if args.guide.resolve() == args.script.resolve():
                raise ValueError('input and output must differ')
            source = args.guide.read_text(encoding='utf-8')
            result = convert_text(extract_scribd_html(source) if args.scribd_html else source)
            parse_script(result)
            args.script.write_text(result, encoding='utf-8')
            print(f'Extracted {len(parse_script(result))} directives to {args.script}; review before replay')
            return
        steps = parse_script(args.script.read_text(encoding='utf-8'))
        planned = compile_steps(steps, args.max_actions, args.through,
                                args.spacing_ms, args.idle_ms, args.golden_every)
        print(f'Plan: {len(planned.actions)} actions, {len(planned.keys)} keys, '
              f'{planned.duration_ms} guest ms, {len(planned.assertions)} final assertions, '
              f'{len(planned.checkpoints)} labels')
        if not args.plan_only:
            run(args, planned)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
