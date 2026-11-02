#!/usr/bin/env python3
"""Prepare Gateway RTLink sections and analyze them in a fresh Ghidra project."""

import argparse
from pathlib import Path
import shutil
import subprocess

from prepare_ghidra import prepare


def headless_command(executable, output, project_name, plan_path):
    scripts = Path(__file__).resolve().parents[1] / 'ghidra'
    project_directory = (output / 'project').resolve()
    return [str(executable), str(project_directory), project_name,
            '-import', str(executable_path(plan_path)),
            '-scriptPath', str(scripts),
            '-preScript', 'ImportGatewayRtlink.py', str(plan_path),
            '-postScript', 'DecompileGateway.py', str(plan_path),
            str((output / 'pseudocode.txt').resolve()),
            '-scriptlog', str((output / 'ghidra-script.log').resolve()),
            '-log', str((output / 'ghidra.log').resolve())]


def executable_path(plan_path):
    import json
    return Path(json.loads(plan_path.read_text())['exe_file'])


def find_headless(explicit=None):
    if explicit:
        command = shutil.which(str(explicit))
        if command:
            return Path(command)
        if explicit.is_file():
            return explicit.resolve()
        raise ValueError('Ghidra analyzeHeadless is not at %s' % explicit)
    result = shutil.which('analyzeHeadless')
    if result:
        return Path(result)
    # NixOS ghidra-bin exposes the GUI wrapper on PATH, but may install the
    # headless launcher only under the package's lib/ghidra/support directory.
    ghidra = shutil.which('ghidra')
    if ghidra:
        prefix = Path(ghidra).resolve().parent.parent
        for support in (prefix / 'lib/ghidra/support/analyzeHeadless',
                        prefix / 'share/ghidra/support/analyzeHeadless',
                        prefix / 'lib/ghidra/Ghidra/RuntimeScripts/support/analyzeHeadless'):
            if support.is_file():
                return support
    raise ValueError('analyzeHeadless not found; enter nix develop .#ghidra '
                     'or pass --headless /path/to/support/analyzeHeadless')


def run(exe, ovl, output, overlay_tool, objects_json=None,
        headless=None, project_name='Gateway', prepare_only=False):
    if not project_name or not project_name.replace('_', '').isalnum():
        raise ValueError('project name must contain only letters, digits and underscores')
    output = output.resolve()
    project_dir = output / 'project'
    if not prepare_only and (project_dir / (project_name + '.gpr')).exists():
        raise ValueError('Ghidra project already exists: %s; choose --project-name '
                         'or inspect the existing project' % project_name)
    plan_path, plan = prepare(exe, ovl, output, overlay_tool, objects_json)
    print('Verified %d isolated RTLink sections, %d fixups.' % (
        len(plan['sections']), sum(s['fixup_count'] for s in plan['sections'])), flush=True)
    print('Plan: %s' % plan_path, flush=True)
    if prepare_only:
        return plan_path
    binary = find_headless(headless)
    project_dir.mkdir(parents=True, exist_ok=True)
    command = headless_command(binary, output, project_name, plan_path)
    subprocess.run(command, check=True)
    import_report = output / 'ghidra-import-report.json'
    pseudocode = output / 'pseudocode.txt'
    if not import_report.is_file() or not pseudocode.is_file():
        raise ValueError('Ghidra did not produce the import report and pseudocode; '
                         'inspect %s and %s' % (
                             output / 'ghidra-script.log', output / 'ghidra.log'))
    print('Import report: %s' % import_report)
    print('Ghidra pseudocode: %s' % pseudocode)
    return plan_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path, help='original GATEMAIN.EXE')
    parser.add_argument('ovl', type=Path, help='original GATEMAIN.OVL')
    parser.add_argument('--output', type=Path, default=Path('build/ghidra'))
    parser.add_argument('--overlay-tool', type=Path, default=Path('build/gateway-overlay'))
    parser.add_argument('--objects-json', type=Path,
                        help='optional native inspect_object_state.py export (adds object labels)')
    parser.add_argument('--headless', type=Path, help='path to Ghidra analyzeHeadless')
    parser.add_argument('--project-name', default='Gateway',
                        help='new project name; existing projects are never overwritten')
    parser.add_argument('--prepare-only', action='store_true',
                        help='validate/extract without launching Ghidra')
    args = parser.parse_args()
    try:
        run(args.exe, args.ovl, args.output, args.overlay_tool, args.objects_json,
            args.headless, args.project_name, args.prepare_only)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'run_ghidra: %s\n' % error)


if __name__ == '__main__':
    main()
