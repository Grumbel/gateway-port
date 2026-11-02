#!/usr/bin/env python3
"""Prepare Gateway RTLink sections and analyze them in a fresh Ghidra project."""

import argparse
import json
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
        headless=None, project_name='Gateway', prepare_only=False, callbacks_json=None,
        decompile_objects=(), decompile_offsets=()):
    if not project_name or not project_name.replace('_', '').isalnum():
        raise ValueError('project name must contain only letters, digits and underscores')
    output = output.resolve()
    project_dir = output / 'project'
    if not prepare_only and (project_dir / (project_name + '.gpr')).exists():
        raise ValueError('Ghidra project already exists: %s; choose --project-name '
                         'or inspect the existing project' % project_name)
    reports = (output / 'ghidra-import-report.json',
               output / 'ghidra-decompile-report.json', output / 'pseudocode.txt')
    if not prepare_only and any(item.exists() for item in reports):
        raise ValueError('Ghidra output reports already exist in %s; choose a fresh '
                         'output directory so stale results cannot be mistaken for success' % output)
    plan_path, plan = prepare(exe, ovl, output, overlay_tool, objects_json,
                              callbacks_json=callbacks_json,
                              decompile_objects=decompile_objects,
                              decompile_offsets=decompile_offsets)
    print('Verified %d isolated RTLink sections, %d fixups.' % (
        len(plan['sections']), sum(s['fixup_count'] for s in plan['sections'])), flush=True)
    print('Plan: %s' % plan_path, flush=True)
    if prepare_only:
        return plan_path
    binary = find_headless(headless)
    project_dir.mkdir(parents=True, exist_ok=True)
    command = headless_command(binary, output, project_name, plan_path)
    subprocess.run(command, check=True)
    if not all(item.is_file() for item in reports):
        raise ValueError('Ghidra did not produce all import/decompile reports and pseudocode; '
                         'inspect %s and %s' % (
                             output / 'ghidra-script.log', output / 'ghidra.log'))
    imported = json.loads(reports[0].read_text())
    decompiled = json.loads(reports[1].read_text())
    expected_labels = sum(len(section['labels']) for section in plan['sections'])
    if (imported.get('imported_sections') != len(plan['sections']) or
        imported.get('labeled_overlay_entries') != expected_labels):
        raise ValueError('Ghidra import counts differ from the validated RTLink plan')
    if plan.get('fingerprinted_gateway') and imported.get('study_event_table_words') != 23:
        raise ValueError('Ghidra did not annotate all 23 native Study event branches')
    if plan.get('fingerprinted_gateway') and imported.get('study_event_flow_edges') != 7:
        raise ValueError('Ghidra did not link all 7 unique Study computed-jump targets')
    if plan.get('fingerprinted_gateway') and imported.get('study_matcher_path_labels') != 9:
        raise ValueError('Ghidra did not annotate all 9 native Study matcher instructions')
    if plan.get('fingerprinted_gateway') and imported.get('study_response_path_labels') != 18:
        raise ValueError('Ghidra did not annotate all 18 native Study response instructions')
    if plan.get('fingerprinted_gateway') and imported.get('vr_return_path_labels') != 24:
        raise ValueError('Ghidra did not annotate all 24 native VR-return instructions')
    expected = [label['name'] for section in plan['sections']
                for label in section['labels'] if label['role'] in
                ('function', 'callback-entry', 'object-entry-candidate',
                 'overlay-entry-candidate')]
    expected.extend(label['name'] for label in plan['resident_labels']
                    if imported.get('resident_matches', {}).get(label['name']) is not None)
    observed = [item['name'] for item in decompiled.get('targets', [])]
    if (decompiled.get('schema_version') != 1 or observed != expected or
        decompiled.get('requested') != len(expected) or
        not isinstance(decompiled.get('completed'), int) or
        not isinstance(decompiled.get('failed'), int) or
        decompiled.get('completed') + decompiled.get('failed') != len(expected) or
        not reports[2].stat().st_size):
        raise ValueError('Ghidra decompile report is incomplete or mismatches the plan')
    print('Ghidra import report: %s' % reports[0])
    print('Decompiled %d/%d targets (including internal branch contexts).' % (
        decompiled['completed'], decompiled['requested']))
    print('Ghidra pseudocode: %s' % reports[2])
    return plan_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path, help='original GATEMAIN.EXE')
    parser.add_argument('ovl', type=Path, help='original GATEMAIN.OVL')
    parser.add_argument('--output', type=Path, default=Path('build/ghidra'))
    parser.add_argument('--overlay-tool', type=Path, default=Path('build/gateway-overlay'))
    parser.add_argument('--objects-json', type=Path,
                        help='optional native inspect_object_state.py export (adds object labels)')
    parser.add_argument('--decompile-object', action='append', type=lambda s: int(s, 0),
                        help='selected native object ID to attempt as a provisional function; '
                             'requires --objects-json (repeatable)')
    parser.add_argument('--decompile-offset', action='append', type=lambda s: int(s, 0),
                        help='original OVL file offset of a provisional native entry '
                             '(repeatable)')
    parser.add_argument('--callbacks-json', type=Path,
                        help='optional native inspect_room_callbacks.py export (decompile candidates)')
    parser.add_argument('--headless', type=Path, help='path to Ghidra analyzeHeadless')
    parser.add_argument('--project-name', default='Gateway',
                        help='new project name; existing projects are never overwritten')
    parser.add_argument('--prepare-only', action='store_true',
                        help='validate/extract without launching Ghidra')
    args = parser.parse_args()
    try:
        run(args.exe, args.ovl, args.output, args.overlay_tool, args.objects_json,
            args.headless, args.project_name, args.prepare_only, args.callbacks_json,
            args.decompile_object or (), args.decompile_offset or ())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'run_ghidra: %s\n' % error)


if __name__ == '__main__':
    main()
