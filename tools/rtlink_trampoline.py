# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Validate live Gateway RTLink trampolines and map them to original OVL files.

The ten-byte stub begins with a near CALL to manager 2c24:058d, then a
far JMP and a section word. Far pointers into the manager address the start
of the stub, not the embedded JMP operands. This is Gateway-specific.
"""
import struct


def decode_trampoline(memory, segment, offset):
    """Return the section/target for a validated native trampoline, or None."""
    address = segment * 16 + offset
    if segment != 0x2c24 or not 0 <= offset <= 0xfff6 or \
            address + 10 > min(len(memory), 0xa0000):
        return None
    displacement = struct.unpack_from('<h', memory, address + 1)[0]
    gate = (offset + 3 + displacement) & 0xffff
    if memory[address] != 0xe8 or memory[address + 3] != 0xea or gate != 0x058d:
        return None
    target_ip, target_cs, section_id = struct.unpack_from('<3H', memory, address + 4)
    if section_id >= 58:
        raise ValueError('invalid section ID in RTLink trampoline')
    return dict(section=section_id, target_segment=target_cs, target_offset=target_ip)


def map_reference(reference, sections):
    """Resolve a validated stub through a parsed gateway-overlay manifest."""
    section = sections.get(reference['section'])
    if section is None:
        raise ValueError('RTLink reference has no section in manifest')
    displacement = (reference['target_segment'] - section['runtime_segment']) * 16
    image_offset = displacement + reference['target_offset']
    if not 0 <= image_offset < section['file_bytes']:
        raise ValueError('RTLink target outside section file-backed bytes')
    return dict(filename=section['filename'], section=section['id'],
                image_offset=image_offset,
                file_offset=section['image_file_offset'] + image_offset)


def sections_by_id(manifest):
    sections = {section['id']: section for section in manifest['sections']}
    if len(sections) != len(manifest['sections']):
        raise ValueError('duplicate RTLink section IDs')
    return sections
