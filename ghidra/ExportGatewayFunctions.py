# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#@category Gateway
# Ghidra post-analysis script (PyGhidra on Ghidra 12; Jython 2.7 on older
# releases). Arguments: absolute ghidra-plan.json, absolute output TSV.
#
# Writes one row per Ghidra function, identified by its ORIGINAL file offset:
# resident code by its GATEMAIN.EXE offset (checked byte-for-byte against the
# file, which has no MZ relocations), overlay code by RTLink section and
# GATEMAIN.OVL offset. Runtime segment addresses are not stable identities.
# The output holds addresses, sizes and names only, never code bytes.

import json
import re

GW_BLOCK = re.compile(r'^GW_RT_(\d{3})$')


def require(condition, message):
    if not condition:
        raise ValueError(message)


args = getScriptArgs()
require(len(args) == 2,
        'usage: ExportGatewayFunctions.py /absolute/ghidra-plan.json /absolute/functions.tsv')
plan = json.load(open(args[0], 'r'))
sections = dict((section['id'], section) for section in plan['sections'])
exe = bytearray(open(plan['exe_file'], 'rb').read())
require(exe[:2] == bytearray(b'MZ'), 'not an MZ executable')
header_bytes = (exe[8] | (exe[9] << 8)) * 16

memory = currentProgram.getMemory()
resident_blocks = [block for block in memory.getBlocks()
                   if block.isInitialized() and not block.isOverlay() and
                   not block.getName().startswith('GW_RT_')]
require(resident_blocks, 'no resident MZ blocks')
image_base = min(block.getStart().getOffset() for block in resident_blocks)


def resident_offset(address):
    offset = address.getOffset() - image_base + header_bytes
    require(header_bytes <= offset < len(exe), 'resident address outside file: %s' % address)
    return offset


def check_resident_bytes(address, offset, count=8):
    count = min(count, len(exe) - offset)
    for index in range(count):
        value = memory.getByte(address.add(index)) & 0xff
        if value != exe[offset + index]:
            return False
    return True


rows = []
mismatches = 0
skipped = 0
for function in currentProgram.getFunctionManager().getFunctions(True):
    entry = function.getEntryPoint()
    block = memory.getBlock(entry)
    if block is None or function.isExternal():
        skipped += 1
        continue
    body = function.getBody()
    size = sum(rng.getLength() for rng in body.getAddressRanges()
               if memory.getBlock(rng.getMinAddress()) == block)
    match = GW_BLOCK.match(block.getName())
    if match:
        sid = int(match.group(1))
        section = sections[sid]
        image_offset = entry.subtract(block.getStart())
        require(0 <= image_offset < section['file_bytes'],
                'overlay entry outside file-backed image: %s' % entry)
        rows.append(('ovl', section['original_file_offset'] + image_offset, str(sid),
                     str(entry), size, function.getName(),
                     str(function.getSymbol().getSource())))
    elif block in resident_blocks:
        offset = resident_offset(entry)
        if not check_resident_bytes(entry, offset):
            mismatches += 1
        rows.append(('exe', offset, '', str(entry), size, function.getName(),
                     str(function.getSymbol().getSource())))
    else:
        skipped += 1

require(mismatches == 0, '%d resident functions do not match GATEMAIN.EXE bytes' % mismatches)
rows.sort(key=lambda row: (row[0], row[1]))
with open(args[1], 'w') as output:
    output.write('file\toffset\tsection\tghidra_address\tbody_bytes\tname\tsource\n')
    for row in rows:
        output.write('%s\t0x%06x\t%s\t%s\t%d\t%s\t%s\n' % row)
print('Exported %d functions (%d resident, %d overlay, %d skipped) to %s' % (
    len(rows), sum(row[0] == 'exe' for row in rows), sum(row[0] == 'ovl' for row in rows),
    skipped, args[1]))
