#!/usr/bin/env python3
"""Prepare reproducible Ghidra RTLink imports from the original Gateway binaries.

Only Gateway's fingerprinted GATEMAIN.EXE/OVL pair is supported by default.
The C++ gateway-overlay extractor remains the authority on section boundaries.
This tool never writes to the original DOS game directory.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from rtlink_trampoline import map_reference, sections_by_id


EXPECTED_HASHES = {
    'GATEMAIN.EXE': '1785bf60f196836c0542dafa4a00085f6b1f3837992f2cc14da12503bc02abcb',
    'GATEMAIN.OVL': '05e14a53904c356cba5f9fbbc9de98cf8fb1a5bdb3b6bcbeff2ae97482982d71',
}

# These are *original file offsets*, established by native disassembly/traces.
# A callsite is not a function boundary. The import script labels those only.
KNOWN_OVERLAY = (
    (0xc830, 'gw_relocate_child', 'function',
     'section-4 linked-child unlink/move/prepend helper; native TAKE and room moves'),
    (0xc879, 'gw_unlink_child_head_call', 'callsite', 'section-4 head setter call'),
    (0xc8c2, 'gw_location_set_call', 'callsite', 'section-4 location setter call'),
    (0xc8f8, 'gw_insert_child_head_call', 'callsite', 'section-4 head setter call'),
    (0xd66b, 'gw_close_drawer', 'function', 'action 0x1d; section 5'),
    (0xd804, 'gw_drop_precheck', 'function',
     'action 0x32 selector-4 callback; intercepts book when not carried'),
    (0xd9a7, 'gw_drop_book', 'function', 'action 0x32; section 5'),
    (0xe8c4, 'gw_inventory', 'function', 'action 0x53; section 5'),
    (0xe942, 'gw_open_drawer', 'function', 'action 0x71; section 5'),
    (0xfbaf, 'gw_take_precheck', 'function',
     'action 0x46 selector-8 callback; intercepts book when already carried'),
    (0xfee1, 'gw_take_book', 'function', 'action 0x46; section 5'),
    (0x514b5, 'gw_your_quarters_handler', 'function', 'room 0x0ee; section 38'),
    (0x519b2, 'gw_book_handler', 'function', 'object 0x123; section 38'),
    (0x51a78, 'gw_drawer_handler', 'function', 'object 0x124; section 38'),
)

KNOWN_RESIDENT = (
    (0x254a, 'gw_native_relation_contains', 'function',
     '0157:000c direct native relation-slot membership test; not recursive'),
    (0x28e4, 'gw_test_object_flag', 'function',
     '0157:03a6 check type-dependent object property bit; returns 0/1'),
    (0x291e, 'gw_set_object_flag', 'function',
     '0157:03e0 type-dependent object property bit setter'),
    (0x2951, 'gw_clear_object_flag', 'function',
     '0157:0413 type-dependent object property bit clearer'),
    (0x592a, 'gw_room_callback_dispatch', 'function',
     '44-entry room callback table; argument ID 0..43 and event word 0x0f'),
    (0x60cf, 'gw_room_exit_dispatch', 'function',
     'type-1 room exit scan; original EXE file 0x60cf'),
    (0x6156, 'gw_conditional_exit_case', 'instruction',
     'kind-2 guard begins here; internal jump target, NOT a function entry'),
    (0x6175, 'gw_conditional_exit_gate_return', 'instruction',
     'instruction immediately after flag 0x0c query in the room dispatcher'),
    (0x6269, 'gw_callback_exit_case', 'instruction',
     'kind-4 callback path, inside the room dispatcher'),
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_files(exe, ovl, allow_unknown=False):
    if exe.resolve() == ovl.resolve():
        raise ValueError('EXE and OVL must be distinct files')
    result = {'exe': digest(exe), 'ovl': digest(ovl)}
    if not allow_unknown and (
        result['exe'] != EXPECTED_HASHES['GATEMAIN.EXE'] or
        result['ovl'] != EXPECTED_HASHES['GATEMAIN.OVL']
    ):
        raise ValueError('GATEMAIN fingerprints differ from docs/executable.md; '
                         'refusing Gateway-specific labels')
    if exe.read_bytes()[:2] != b'MZ':
        raise ValueError('GATEMAIN.EXE has no MZ header')
    return result


def name_for_object(entry):
    name = re.sub('[^A-Za-z0-9]+', '_', entry.get('name', '')).strip('_')[:40]
    return 'gw_object_%03x_%s' % (entry['object'], name.lower() or 'unnamed')


def plan_sections(manifest, images_dir, objects=None):
    """Check every fixup and image extent; return address-space-specific labels."""
    relocation = manifest['relocation']
    if not 0 <= relocation <= 0xffff:
        raise ValueError('relocation factor outside 16-bit range')
    sections = manifest['sections']
    by_id = {item['id']: item for item in sections}
    if len(by_id) != len(sections) or set(by_id) != set(range(len(sections))):
        raise ValueError('duplicate or discontinuous section IDs')
    result = []
    for section in sections:
        sid = section['id']
        raw_file = (images_dir / ('section-%03d.raw.bin' % sid)).resolve()
        relocated_file = (images_dir / ('section-%03d.relocated.bin' % sid)).resolve()
        raw, relocated = raw_file.read_bytes(), relocated_file.read_bytes()
        if not (len(raw) == len(relocated) == section['file_bytes']):
            raise ValueError('section %d: file-backed image length mismatch' % sid)
        initialized = section['initialized_size']
        allocated = section['memory_size']
        if not (0 <= len(raw) <= initialized <= allocated):
            raise ValueError('section %d: invalid initialized/BSS/tail lengths' % sid)
        if section['missing_paragraph_tail'] != initialized - len(raw):
            raise ValueError('section %d: missing paragraph tail mismatch' % sid)
        if section['runtime_segment'] != section['linked_segment'] + relocation:
            raise ValueError('section %d: runtime segment mismatch' % sid)
        if section['runtime_segment'] * 16 + allocated > 0xa0000:
            raise ValueError('section %d: beyond conventional memory' % sid)
        if 'fixups' not in section:
            raise ValueError('section %d: gateway-overlay must run with --fixups' % sid)
        expected = bytearray(raw)
        for fixup in section['fixups']:
            offset = fixup['image_offset']
            derived = ((fixup['segment'] - section['linked_segment']) * 16 +
                       fixup['offset'])
            if derived != offset or not 0 <= offset <= len(raw) - 2:
                raise ValueError('section %d: invalid fixup site' % sid)
            word = struct.unpack_from('<H', expected, offset)[0]
            struct.pack_into('<H', expected, offset, (word + relocation) & 0xffff)
        if bytes(expected) != relocated:
            raise ValueError('section %d: extracted relocation bytes differ' % sid)
        result.append(dict(
            id=sid, linked_segment=section['linked_segment'],
            runtime_segment=section['runtime_segment'],
            original_file_offset=section['image_file_offset'],
            initialized_size=initialized, memory_size=allocated,
            file_bytes=len(raw), missing_paragraph_tail=initialized-len(raw),
            raw_file=str(raw_file), relocated_file=str(relocated_file),
            relocated_sha256=hashlib.sha256(relocated).hexdigest(),
            fixup_count=len(section['fixups']), parent=section['parent'],
            overlaps=section['overlaps'], labels=[]))

    def label_at(file_offset, label, role, explanation, expected_section=None):
        matches = [s for s in result if s['original_file_offset'] <= file_offset <
                   s['original_file_offset'] + s['file_bytes']]
        if len(matches) != 1:
            raise ValueError('original OVL offset 0x%x resolves to %d sections' %
                             (file_offset, len(matches)))
        section = matches[0]
        if expected_section is not None and section['id'] != expected_section:
            raise ValueError('object handler maps to wrong section')
        section['labels'].append(dict(
            offset=file_offset-section['original_file_offset'], name=label,
            role=role, explanation=explanation, file_offset=file_offset))

    # Known offsets apply only to the fingerprinted Gateway pair, not synthetic
    # RTLink fixture images or other games.
    if objects is not None:
        for obj in objects:
            native = obj.get('native_code')
            if not native:
                continue
            label_at(native['file_offset'], name_for_object(obj), 'object-entry',
                     'object 0x%03x handler %d' % (obj['object'], obj['handler']),
                     native['section'])
    return result, label_at


def add_callback_labels(callbacks, manifest, label_at):
    """Seed candidates from a native snapshot, checking every mapped offset."""
    if callbacks.get('schema_version') != 1 or not isinstance(callbacks.get('callbacks'), list):
        raise ValueError('unsupported room callback report')
    if callbacks.get('table_count') != 44:
        raise ValueError('room callback report has unexpected dispatch-table size')
    sections = sections_by_id(manifest)
    seen = set()
    for item in callbacks['callbacks']:
        index = item.get('callback_id')
        if not isinstance(index, int) or not 0 <= index < 44 or index in seen:
            raise ValueError('duplicate or invalid room callback ID')
        seen.add(index)
        if item.get('status') != 'rtlink' or not item.get('uses'):
            raise ValueError('room callback has no validated RTLink uses')
        reference = item['rtlink_reference']
        mapped = map_reference(reference, sections)
        if mapped != item.get('native_code') or mapped['filename'] != 'GATEMAIN.OVL':
            raise ValueError('room callback original-code mapping mismatches manifest')
        label_at(mapped['file_offset'], 'gw_room_callback_%02x' % index,
                 'callback-entry',
                 'kind-4 exit callback ID 0x%02x; %d room exit records; '
                 'entry is mapped statically, behavior unverified' %
                 (index, len(item['uses'])), mapped['section'])
    if callbacks.get('exit_count') != sum(len(item['uses']) for item in callbacks['callbacks']):
        raise ValueError('room callback exit use count mismatch')


def prepare(exe, ovl, output, extractor, objects_json=None, allow_unknown=False,
            callbacks_json=None):
    hashes = checked_files(exe, ovl, allow_unknown)
    output = output.resolve()
    images = output / 'sections'
    images.mkdir(parents=True, exist_ok=True)
    command = [str(extractor.resolve()), str(exe.resolve()), str(ovl.resolve()),
               '--output', str(images), '--fixups']
    extracted = subprocess.run(command, capture_output=True, text=True)
    if extracted.returncode:
        raise ValueError('gateway-overlay failed: ' + extracted.stderr.strip())
    manifest = json.loads(extracted.stdout)
    objects = None
    if objects_json:
        doc = json.loads(objects_json.read_text())
        if not isinstance(doc.get('objects'), list):
            raise ValueError('objects JSON lacks an objects array')
        from inspect_object_state import map_handler_entries
        objects = map_handler_entries(doc['objects'], manifest)
    sections, label_at = plan_sections(manifest, images, objects)
    if callbacks_json:
        if allow_unknown:
            raise ValueError('native room callback labels require fingerprinted Gateway binaries')
        add_callback_labels(json.loads(callbacks_json.read_text()), manifest, label_at)
    if not allow_unknown:
        if len(sections) != 58 or sum(s['fixup_count'] for s in sections) != 13947:
            raise ValueError('Gateway section/fixup counts unexpectedly changed')
        for offset, name, role, explanation in KNOWN_OVERLAY:
            label_at(offset, name, role, explanation)

    resident = []
    data = exe.read_bytes()
    if not allow_unknown:
        for offset, name, role, explanation in KNOWN_RESIDENT:
            if offset + 24 > len(data):
                raise ValueError('resident file signature outside EXE')
            resident.append(dict(file_offset=offset, name=name, role=role,
                                 explanation=explanation, signature=data[offset:offset+24].hex()))
    plan = dict(schema_version=1, game='Gateway 1 (1992)',
                exe_file=str(exe.resolve()), exe_sha256=hashes['exe'],
                ovl_file=str(ovl.resolve()), ovl_sha256=hashes['ovl'],
                relocation=manifest['relocation'], sections=sections,
                resident_labels=resident,
                note='Sections may overlap physically; import each as a distinct Ghidra overlay. '
                     'A relocated segment word is not proof of a cross-overlay reference.')
    target = output / 'ghidra-plan.json'
    target.write_text(json.dumps(plan, indent=2) + '\n')
    return target, plan


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path, help='original GATEMAIN.EXE')
    parser.add_argument('ovl', type=Path, help='original GATEMAIN.OVL')
    parser.add_argument('--output', type=Path, default=Path('build/ghidra'))
    parser.add_argument('--overlay-tool', type=Path, default=Path('build/gateway-overlay'))
    parser.add_argument('--objects-json', type=Path,
                        help='optional inspect_object_state.py JSON with handler pointers')
    parser.add_argument('--callbacks-json', type=Path,
                        help='optional native inspect_room_callbacks.py JSON with kind-4 entries')
    parser.add_argument('--allow-unknown-binaries', action='store_true',
                        help='for synthetic RTLink fixtures; omit Gateway-only labels')
    args = parser.parse_args()
    try:
        target, plan = prepare(args.exe, args.ovl, args.output, args.overlay_tool,
                               args.objects_json, args.allow_unknown_binaries,
                               args.callbacks_json)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, 'prepare_ghidra: %s\n' % error)
    print('Prepared %d isolated RTLink sections, %d fixups; plan: %s' % (
        len(plan['sections']), sum(s['fixup_count'] for s in plan['sections']), target))
    print('No original game data were changed. Run Ghidra using docs/ghidra.md.')


if __name__ == '__main__':
    main()
