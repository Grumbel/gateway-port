#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Run Ghidra's real decompiler on the original, linked Gateway intro.

Unlike the main-game GATEMAIN.OVL pipeline this imports GATE.EXE/GATE.RTL.
Requires a DOS 1 MiB memory snapshot: the RTL's on-disk far operands are not
linked.  Every output containing executable/game bytes stays under build/.
"""

import argparse
import json
from pathlib import Path
import subprocess

from prepare_intro_ghidra import prepare
from run_ghidra import find_headless, pyghidra_launcher


def command(launcher, plan_path, project_name):
    output = plan_path.parent
    scripts = Path(__file__).resolve().parents[1] / 'ghidra'
    if isinstance(launcher, (str, Path)):
        launcher = [str(launcher)]
    return list(launcher) + [str((output / 'project').resolve()), project_name,
            '-import', str(Path(json.loads(plan_path.read_text())['exe_file'])),
            '-scriptPath', str(scripts),
            '-preScript', 'ImportGatewayIntro.py', str(plan_path),
            '-postScript', 'DecompileGatewayIntro.py', str(plan_path),
            '-scriptlog', str((output / 'intro-ghidra-script.log').resolve()),
            '-log', str((output / 'intro-ghidra.log').resolve())]


def run(exe, rtl, snapshot, output, project_name='GatewayIntro',
        prepare_only=False, headless=None, ghidra_install_dir=None,
        load_segment=0x1000):
    if not project_name or not project_name.replace('_', '').isalnum():
        raise ValueError('project name must contain only letters, digits and underscores')
    output = output.resolve()
    reports = (output / 'intro-ghidra-import-report.json',
               output / 'intro-ghidra-report.json',
               output / 'intro-pseudocode.txt',
               output / 'intro-ghidra-callgraph.json',
               output / 'intro-ghidra-functions.tsv',
               output / 'intro-ghidra-unresolved.tsv')
    if not prepare_only and (
            (output / 'project' / (project_name + '.gpr')).exists() or
            any(path.exists() for path in reports)):
        raise ValueError('Ghidra output/project already exists: use a fresh --output '
                         'to avoid mixing reports from different runs')
    path, plan = prepare(exe, rtl, snapshot, output, load_segment)
    print('Prepared %d linked RTL banks, %d known RTL entries and %d EXE entries.' %
          (len(plan['sections']), sum(len(s['entries']) for s in plan['sections']),
           len(plan['exe_labels'])), flush=True)
    print('Intro plan: %s' % path, flush=True)
    if prepare_only:
        return path
    launcher = None if headless else pyghidra_launcher(ghidra_install_dir)
    if launcher is None:
        launcher = find_headless(headless)
    (output / 'project').mkdir(parents=True, exist_ok=True)
    subprocess.run(command(launcher, path, project_name), check=True)
    if any(not report.is_file() or not report.stat().st_size for report in reports):
        raise ValueError('Ghidra did not produce all intro outputs; inspect script/log files')
    imported = json.loads(reports[0].read_text())
    exported = json.loads(reports[1].read_text())
    graph = json.loads(reports[3].read_text())
    expected = sum(len(bank['entries']) for bank in plan['sections'])
    if (imported.get('schema_version') != 1 or
            imported.get('imported_banks') != len(plan['sections']) or
            imported.get('labeled_rtl_entries') != expected or
            set(imported.get('resident_matches', {})) !=
            {item['name'] for item in plan['exe_labels']}):
        raise ValueError('Ghidra did not import the planned intro entries')
    if (exported.get('schema_version') != 1 or
            exported.get('requested', 0) < 1 or
            not 0 <= exported.get('completed', -1) <= exported['requested'] or
            len(exported.get('functions', [])) != exported['requested'] or
            graph.get('schema_version') != 1 or
            len(graph.get('functions', [])) != exported['requested'] or
            len(graph.get('unresolved', [])) != exported.get('unresolved_calls') or
            sum(item['count'] for item in exported.get('unresolved_targets', [])) !=
            exported.get('unresolved_calls')):
        raise ValueError('Ghidra intro decompilation/callgraph is incomplete')
    print('Ghidra decompiled %d/%d intro functions. %d unresolved calls, '
          '%d missing seed entries.' %
          (exported['completed'], exported['requested'],
           exported['unresolved_calls'], len(exported['missing_seeded'])))
    print('Generated pseudocode: %s' % reports[2])
    print('Coverage/status: %s' % reports[1])
    print('Static callgraph: %s' % reports[3])
    print('Ranked unresolved callees: %s' % reports[5])
    for item in exported['unresolved_targets'][:5]:
        print('  %s (%s): %d call sites' %
              (item['target'], item['kind'], item['count']))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path, help='original GATE.EXE (not GATEMAIN.EXE)')
    parser.add_argument('rtl', type=Path, help='original GATE.RTL (not GATEMAIN.OVL)')
    parser.add_argument('--snapshot', type=Path, required=True,
                        help='original emulator RAM image, e.g. at-22000.bin')
    parser.add_argument('--output', type=Path, default=Path('build/ghidra-intro'))
    parser.add_argument('--project-name', default='GatewayIntro')
    parser.add_argument('--load-segment', type=lambda value: int(value, 0), default=0x1000)
    parser.add_argument('--prepare-only', action='store_true',
                        help='verify the relocated image and source identities without Ghidra')
    parser.add_argument('--headless', type=Path, help='explicit Ghidra analyzeHeadless path')
    parser.add_argument('--ghidra-install-dir', type=Path,
                        help='Ghidra installation for PyGhidra (default GHIDRA_INSTALL_DIR)')
    args = parser.parse_args()
    try:
        run(args.exe, args.rtl, args.snapshot, args.output, args.project_name,
            args.prepare_only, args.headless, args.ghidra_install_dir,
            args.load_segment)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'run_intro_ghidra: %s\n' % error)


if __name__ == '__main__':
    main()
