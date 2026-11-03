#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Replay validated Gateway stages through authentic DOS SAVE/RESTORE slots.

Each stage is a reviewed standalone .walk. No original game data or third-party
guide text belongs in the manifest; native .SAV files stay in ignored build/.
"""

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from run_walkthrough import compile_steps, native_save_description, parse_script


@dataclass(frozen=True)
class Stage:
    name: str
    script: Path
    description: str
    script_sha256: str
    actions: int
    keys: int
    duration_ms: int


def load_campaign(manifest):
    source = json.loads(manifest.read_text(encoding='utf-8'))
    if not isinstance(source, dict) or set(source) != {'schema_version', 'stages'} or \
            source['schema_version'] != 1 or not isinstance(source['stages'], list) or \
            not 1 <= len(source['stages']) <= 32:
        raise ValueError('campaign requires schema_version 1 and 1–32 stages')
    stages = []
    names = set()
    descriptions = set()
    for index, record in enumerate(source['stages']):
        if not isinstance(record, dict) or set(record) != {'name', 'script', 'save'} or \
                not all(isinstance(value, str) for value in record.values()):
            raise ValueError(f'campaign stage {index}: expected name, script and save')
        name, description = record['name'], record['save']
        if not re.fullmatch(r'[a-z][a-z0-9_-]{0,47}', name) or name in names:
            raise ValueError(f'campaign stage {index}: invalid/duplicate stage name')
        if not 1 <= len(description) <= 33 or any(not 32 <= ord(c) <= 126 for c in description) \
                or description in descriptions:
            raise ValueError(f'campaign stage {index}: invalid/duplicate DOS save title')
        names.add(name)
        descriptions.add(description)
        script = manifest.parent / record['script']
        content = script.read_bytes()
        steps = parse_script(content.decode('utf-8'))
        actions = [step for step in steps if step.operation in ('COMMAND', 'ANSWER',
                                                                'TYPE', 'KEY', 'MENU_KEYS',
                                                                'MOUSE_CLICK')]
        expected_start = [] if index == 0 else [('COMMAND', 'restore'),
                                                  ('TYPE', stages[-1].description),
                                                  ('KEY', 'enter')]
        if expected_start and [(s.operation, s.argument) for s in actions[:3]] != expected_start:
            raise ValueError(f'campaign stage {name}: must begin by restoring previous save')
        if [(s.operation, s.argument) for s in actions[-3:]] != \
                [('COMMAND', 'save'), ('TYPE', description), ('KEY', 'enter')]:
            raise ValueError(f'campaign stage {name}: must end by writing its named DOS save')
        replay = compile_steps(steps)
        stages.append(Stage(name, script, description, hashlib.sha256(content).hexdigest(),
                            len(replay.actions), len(replay.keys), replay.duration_ms))
    return stages


def find_save(state_dir, description):
    candidates = [path for path in sorted(state_dir.glob('GATE[0-9][0-9][0-9].SAV'))
                  if native_save_description(path) == description]
    if len(candidates) != 1:
        raise ValueError(f'expected one native saved game {description!r} in {state_dir}, '
                         f'found {len(candidates)}')
    return candidates[0]


def sha256_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def metadata_path(root, stage):
    return root / stage.name / 'stage-meta.json'


def verified_save(root, stage):
    meta = json.loads(metadata_path(root, stage).read_text(encoding='utf-8'))
    if not isinstance(meta, dict) or meta.get('name') != stage.name or \
            meta.get('script_sha256') != stage.script_sha256 or \
            meta.get('save_description') != stage.description:
        raise ValueError(f'prior campaign stage {stage.name} is stale or was not validated')
    path = find_save(root / stage.name / 'state', stage.description)
    if sha256_file(path) != meta.get('save_sha256'):
        raise ValueError(f'prior campaign save {stage.name} was modified')
    return path


def run_campaign(stages, args):
    names = [stage.name for stage in stages]
    first = names.index(args.from_stage) if args.from_stage else 0
    last = names.index(args.through_stage) + 1 if args.through_stage else len(stages)
    if first >= last:
        raise ValueError('campaign start must precede the selected final stage')
    if args.plan_only:
        for stage in stages[first:last]:
            print(f'{stage.name}: {stage.actions} actions, {stage.keys} BIOS keys, '
                  f'{stage.duration_ms} guest ms -> {stage.description}')
        return
    if not args.probe.is_file() or not (args.data / 'GATE.EXE').is_file():
        raise ValueError('compiled gateway-probe and original GATE.EXE are required')
    if first:
        previous = verified_save(args.output, stages[first - 1])
    else:
        previous = None
    for stage in stages[first:last]:
        output = args.output / stage.name
        cmd = [sys.executable, str(Path(__file__).with_name('run_walkthrough.py')),
               'run', str(stage.script), '--probe', str(args.probe),
               '--data', str(args.data), '--output', str(output), '--check-labels',
               '--expect-save', stage.description, '--timeout', str(args.timeout)]
        if previous is not None:
            cmd += ['--import-save', str(previous)]
        if args.verify_overlays:
            cmd.append('--verify-overlays')
        if getattr(args, 'golden_every', None) is not None:
            cmd += ['--golden-every', str(args.golden_every)]
        if getattr(args, 'trace_rtlink_sequence', False):
            cmd.append('--trace-rtlink-sequence')
        for argument in getattr(args, 'probe_arg', []):
            cmd.append('--probe-arg=' + argument)
        for entry in args.extra_trace_entry or []:
            cmd += ['--extra-trace-entry', entry]
        print(f'Running native campaign stage: {stage.name}', flush=True)
        subprocess.run(cmd, check=True)
        saved = find_save(output / 'state', stage.description)
        metadata = dict(schema_version=1, name=stage.name,
                        script_sha256=stage.script_sha256,
                        save_description=stage.description, save_sha256=sha256_file(saved),
                        original_save=saved.name)
        metadata_path(args.output, stage).write_text(json.dumps(metadata, indent=2) + '\n',
                                                     encoding='utf-8')
        print(f'Verified stage {stage.name}: native {saved.name} ({saved.stat().st_size} bytes)',
              flush=True)
        previous = saved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--probe', type=Path, default=Path('build/gateway-probe'))
    parser.add_argument('--data', type=Path, default=Path('../gateway-dos'))
    parser.add_argument('--output', type=Path, default=Path('build/campaign'))
    parser.add_argument('--from-stage', metavar='NAME')
    parser.add_argument('--through-stage', metavar='NAME')
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('--verify-overlays', action='store_true')
    parser.add_argument('--golden-every', type=int, metavar='N',
                        help='record extra RAM checkpoints after each N parser commands')
    parser.add_argument('--trace-rtlink-sequence', action='store_true',
                        help='record target/caller timeline of validated RTLink calls per stage')
    parser.add_argument('--probe-arg', action='append', default=[], metavar='ARG',
                        help='forward an argument to the native probe (repeatable)')
    parser.add_argument('--extra-trace-entry', action='append', metavar='LINEARHEX',
                        help='capture additional native service entry stacks (repeatable)')
    parser.add_argument('--plan-only', action='store_true')
    args = parser.parse_args()
    try:
        stages = load_campaign(args.manifest)
        names = {stage.name for stage in stages}
        if args.from_stage and args.from_stage not in names:
            raise ValueError('unknown --from-stage')
        if args.through_stage and args.through_stage not in names:
            raise ValueError('unknown --through-stage')
        if args.timeout < 1:
            raise ValueError('--timeout must be positive')
        run_campaign(stages, args)
    except (OSError, ValueError, UnicodeDecodeError, json.JSONDecodeError,
            subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
