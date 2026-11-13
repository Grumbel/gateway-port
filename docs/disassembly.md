# Reassemblable NASM disassembly

`tools/disassemble_gateway.py` produces a mechanical, byte-identical NASM
listing of the whole program as the base for later mechanical translation.
Its output contains original code and stays under `build/`.

```sh
nix develop
python3 tools/disassemble_gateway.py ../gateway-dos/GATEMAIN.EXE \
    ../gateway-dos/GATEMAIN.OVL --rtl ../gateway-dos/GATE.RTL \
    --manifest build/handler-coverage/rtlink-manifest.json \
    --output build/disasm --verify
```

The manifest comes from `gateway-overlay --fixups` (see [rtlink.md](rtlink.md)).
`--verify` assembles every file with NASM and compares it with the original
bytes; the run fails if any file differs.

## Output

| File | Content |
| --- | --- |
| `gatemain-exe.asm` | whole `GATEMAIN.EXE`: MZ header, RTLink startup, every resident record |
| `gate-rtl.asm` | whole `GATE.RTL`: header and every library record |
| `ovl-NNN.asm` | one RTLink overlay section in its unrelocated on-disk form |
| `prelude.inc` | `rr` macro for MSC's reg-reg direction-bit encodings (`8B`/`03` forms) |
| `report.json` | coverage, rejected seeds, unresolved edges, encoding checks |

Each resident or library record is a NASM section with `start` = file offset
and `vstart` = linked linear address (load segment 0), so labels and near
branches use the address the code actually runs at. EXE and RTL records share
one linked address space; RTL records fill the holes between EXE records.
Labels are named by file offset: `F_`/`L_`/`T_` (function, label, switch
table) followed by the EXE or OVL file offset, `F_rtl_...` for GATE.RTL.
Near references between an EXE record and an RTL record cannot be labels in
two separate files and fall back to `db`.

## Code discovery

Recursive traversal starts at the MZ entry, then every row of
`decomp/functions.tsv` (tracker-confirmed overlay entries first), and
optionally `--ghidra-functions`. Far calls are resolved through the parent
chain of overlay sections and the resident space; far pointers inside
resident and RTL code are read after applying all RTLink fixups, because the
target-based fixups store placeholders in the file. Resident ten-byte
trampolines (`JMP FAR` followed by a section word) lead into their section.
`jmp word [cs:bx+disp16]` switch tables are bounded by a preceding
`cmp r, N` with `ja`/`jbe`; unbounded scans are cut when code is found inside.

Every seed and every far target is a separate transaction: if anything it
reaches fails to decode, leaves the file-backed bytes, crosses a record
boundary, or overlaps existing code, the transaction is rolled back. This
keeps bad tracker rows (exe rows inherited from early Ghidra runs often point
into data) from damaging the result and keeps one bad callee from rejecting
its callers.

Instruction texts are checked by assembling each distinct text alone; a text
whose bytes differ, an x87 or MSC floating-point emulator sequence
(`INT 34h`–`3Dh`), and other encodings NASM cannot reproduce are emitted as
`db` with the instruction in a comment.

## Status (2026-11-13)

All 61 files reassemble byte-identically. 471,862 bytes are decoded as
169,732 instructions in 2,180 functions; 4,314 bytes are switch tables, 229
instructions are `db` fallbacks, 18 edges are unresolved (indirect jumps and
far calls from code that is probably data), and 212 tracker seeds are
rejected, almost all `exe` rows that start inside data.
