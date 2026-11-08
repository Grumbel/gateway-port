# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#@category Gateway
# Pre-analysis. GATE.EXE uses Ghidra's DOS/MZ loader; GATE.RTL banks are
# imported from an independently verified, already-relocated guest snapshot.
# Do NOT treat source RTL offsets as DOS addresses or undo its live fixups.

import hashlib
import json
import os
from java.io import FileInputStream
from ghidra.program.model.data import (ArrayDataType, CategoryPath,
                                      DataTypeConflictHandler, StructureDataType,
                                      UnsignedCharDataType)
from ghidra.program.model.listing import CodeUnit
from ghidra.program.model.symbol import SourceType
try:
    from jarray import array as _jython_array

    def java_bytes(data):
        return _jython_array([x - 256 if x >= 128 else x for x in data], 'b')
except ImportError:
    import jpype

    def java_bytes(data):
        return jpype.JArray(jpype.JByte)([x - 256 if x >= 128 else x for x in data])


def require(ok, message):
    if not ok:
        raise ValueError(message)


args = getScriptArgs()
require(len(args) == 1, 'usage: ImportGatewayIntro.py /absolute/intro-ghidra-plan.json')
plan = json.load(open(args[0], 'r'))
require(plan['schema_version'] == 1, 'unsupported intro plan')
require('x86:LE:16' in str(currentProgram.getLanguage().getLanguageID()),
        'import original GATE.EXE through the 16-bit DOS/MZ loader')
for key, digest in (('exe_file', 'exe_sha256'), ('rtl_file', 'rtl_sha256')):
    require(hashlib.sha256(open(plan[key], 'rb').read()).hexdigest() == plan[digest],
            'original %s changed since preparation' % key)

memory = currentProgram.getMemory()
original_blocks = [block for block in memory.getBlocks() if not block.isOverlay()]
symbols = currentProgram.getSymbolTable()
listing = currentProgram.getListing()

# A type starting point, intentionally without guessed internal semantics.
record = StructureDataType(CategoryPath('/GatewayIntro'), 'ImageWork136', 0)
record.add(UnsignedCharDataType.dataType, 'active', 'original record byte 0')
record.add(ArrayDataType(UnsignedCharDataType.dataType, 135, 1),
           'unclassified', '136-byte source stack/array record; fields not yet typed')
currentProgram.getDataTypeManager().addDataType(record,
                                                 DataTypeConflictHandler.KEEP_HANDLER)


def annotate(address, name, origin, role):
    symbols.createLabel(address, name, currentProgram.getGlobalNamespace(),
                        SourceType.USER_DEFINED)
    listing.setComment(address, CodeUnit.PRE_COMMENT,
                       '%s: %s; %s (16-bit original file identity; unverified ABI)' %
                       (origin, name, role))
    disassemble(address)


def signature_matches(raw):
    expected = java_bytes(raw)
    results = []
    for block in original_blocks:
        if not block.isInitialized():
            continue
        position = block.getStart()
        while position.compareTo(block.getEnd()) <= 0:
            found = memory.findBytes(position, block.getEnd(), expected, None, True, monitor)
            if found is None:
                break
            results.append(found)
            position = found.add(1)
    return results


resident = {}
for entry in plan['exe_labels']:
    anchor = entry['signature']
    matches = signature_matches(bytearray.fromhex(anchor['hex']))
    if len(matches) != 1:
        resident[entry['name']] = None
        printerr('intro EXE 0x%x: signature has %d MZ-memory matches' %
                 (entry['file_offset'], len(matches)))
        continue
    start = matches[0].subtract(anchor['delta'])
    annotate(start, entry['name'], 'GATE.EXE:0x%x' % entry['file_offset'],
             entry['role'])
    resident[entry['name']] = str(start)

imported = 0
for section in plan['sections']:
    name = section['name']
    require(memory.getBlock(name) is None, 'import already contains %s' % name)
    image = section['relocated_file']
    require(hashlib.sha256(open(image, 'rb').read()).hexdigest() ==
            section['relocated_sha256'], 'linked image %s changed' % name)
    address = toAddr('%04x:0000' % section['segment'])
    require(address is not None, 'cannot represent intro 16-bit segment')
    stream = FileInputStream(image)
    try:
        block = memory.createInitializedBlock(name, address, stream, section['size'],
                                              monitor, True)
    finally:
        stream.close()
    block.setRead(True)
    block.setWrite(False)
    block.setExecute(True)
    spans = section.get('source_spans', [dict(offset=0,
                                            file_base=section['file_base'],
                                            size=section['size'])])
    block.setComment('Captured GATE.RTL segment with %d verified source '
                     'fragments, starting at RTL:0x%x; linker metadata '
                     'gaps are not resident code; NOT entire RTL' %
                     (len(spans), section['file_base']))
    for entry in section['entries']:
        annotate(block.getStart().add(entry['offset']), entry['name'],
                 'GATE.RTL:0x%x linked %04x:%04x' %
                 (entry['file_offset'], section['segment'], entry['offset']),
                 entry['role'])
        imported += 1

result = dict(schema_version=1, imported_banks=len(plan['sections']),
              labeled_rtl_entries=imported, resident_matches=resident)
path = os.path.join(os.path.dirname(args[0]), 'intro-ghidra-import-report.json')
with open(path, 'w') as stream:
    json.dump(result, stream, indent=2)
println('Gateway intro: %d relocated RTL banks, %d known RTL entries, '
        '%d/%d resident EXE entries' %
        (len(plan['sections']), imported, sum(v is not None for v in resident.values()),
         len(resident)))
