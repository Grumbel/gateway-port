# RTLink section extraction and native-load verification

Investigation: 2026-10-31, Linux x86-64, Unicorn 2.1.4, original Gateway files
fingerprinted in [executable.md](executable.md). This resolves the section-word
mapping left open in [object-state.md](object-state.md). No original data changed.

## Result

The 43 section groups containing the 696 object/event handlers now map to exact
code extents and entry offsets in `GATEMAIN.OVL`. A tested C++ parser extracts all
58 sections, reads all 13,947 relocation records, and reconstructs loaded images.
An opt-in verifier compares that reconstruction with the bytes produced by the
original RTLink loader after each native load.

This supplies section images and relocation metadata suitable for disassembly
and a future host loader. It does not replace RTLink residency/return-stack
management or produce independent story-only binaries. Engine and story code
remain mixed within the overlay file, and handlers still call resident services
and access the guest data segment.

## Directory layout

The manager's linked segment is `1c24`. In the EXE file, its segment bytes start
at **`0xa020`**; in the observed default replay they run at `2c24:0000`, after
adding load segment `0x1000`. EXE file offsets are not runtime linear addresses.

| Manager-relative offset | EXE file offset | Meaning |
| --- | --- | --- |
| `0x0c19` | `0xac39` | `u16` directory count, 59 including the empty sentinel |
| `0x0c1f` | `0xac3f` | First of 18-byte section records |
| `0x11c7` | `0xb1e7` | Mutable file-open status byte |
| `0x11c8` | `0xb1e8` | NUL-terminated `GATEMAIN.OVL` |

Section IDs are zero-based table indexes. Entries 0–57 describe real sections;
entry 58 is all zero. All integers are little-endian. Each record contains:

| Offset | Type | Meaning |
| --- | --- | --- |
| `0x00` | `u16` | Linked destination segment; runtime adds program load segment |
| `0x02` | `u16` | Manager-relative pointer to file status byte; filename follows it |
| `0x04` | `u16` | Low word of relocation-table file offset in paragraphs |
| `0x06` | `u8` | High byte of that 24-bit paragraph offset |
| `0x07` | `u8` | Flags: `04` in all on-disk records; runtime bit `02` marks loaded |
| `0x08` | `u16` | Total memory paragraphs |
| `0x0a` | `u16` | Relocation count |
| `0x0c` | `u16` | Parent section ID, or `ffff` |
| `0x0e` | `u16` | Unresolved word; preserved in manifests |
| `0x10` | `u16` | Initialized paragraphs read from disk |

The native section lookup at runtime `2c24:0610` computes `0x0c1f + 18*ID`.
The byte loader at `2c24:021c` performs the reads, BSS clearing and relocation;
its disassembly establishes the following formulas:

```text
relocation_file_offset = (low_word | (high_byte << 16)) * 16
relocation_bytes = relocation_count * 4
image_file_offset = relocation_file_offset + ceil(relocation_count / 4) * 16
initialized_size = initialized_paragraphs * 16
memory_size = memory_paragraphs * 16
```

The entire 497,763-byte overlay is accounted for by 55,788 relocation bytes,
308 bytes of relocation-table alignment padding, and 441,667 image bytes.
There are no unexplained gaps between sections.

The checked parser currently accepts the observed subset: one external file,
flags `04`, fixed section addresses, preceding parents whose memory ends at the
child's start, and file-backed fixup sites. It rejects indirect file descriptors,
compressed/callback-backed sections and other RTLink variants rather than
assuming compatibility. `--manager-offset` supports an explicit directory
location but does not make the parser a universal RTLink implementation.

## Relocation and partial paragraphs

Each fixup is `u16 offset, u16 segment`. These are **linked image addresses**,
not a segment displacement from the beginning of the section. The fixup's
location within an extracted image is:

```text
image_offset = (fixup_segment - section_linked_segment) * 16 + fixup_offset
word_at(image_offset) += program_load_segment  # modulo 65536
```

The addition uses the main program's load segment, not the overlay bank segment.
The native relocation loop at `2c24:0431`–`0442` adds `PSP_segment + 0x10` to the
address segment and to the stored relocation word. This explains the zero MZ
relocation count: RTLink keeps its own relocation tables in the overlay.

All 13,947 fixup words are within the actual file-backed image bytes. Some fixup
segments name a paragraph inside the section rather than its starting segment;
flattening the far address, as above, is essential.

Section 8 reserves 64 BSS bytes beyond its initialized image; the original loader
zeroes them. Section 57 declares 4896 initialized bytes but has only 4883 bytes
on disk: 13 bytes of final paragraph tail are absent. The loader advances through
the requested paragraphs after the short DOS read and only zeroes the declared
BSS, so those 13 tail bytes remain from prior memory. They must not be silently
classified as BSS or counted as validated image bytes. This behavior follows
from loader disassembly and synthetic tests; section 57 was not executed in the
bounded first-room replay.

`apply_rtlink()` in [rtlink.cpp](../src/rtlink.cpp) validates before writing,
loads relocated file bytes, preserves the absent tail, and clears only BSS.
It deliberately leaves parent loading, residency bits, return-stack management,
and Unicorn code-cache invalidation to its caller. It is not wired in as a host
replacement for the original manager yet.

## Parent sections and lifetime

The parent relation is functional. After loading a section, the native manager
loads its parent recursively; a child starts immediately above its parent's
memory interval. The provided directory contains these families:

- Parent 12, children 13–17.
- Parent 18, children 19–25.
- Parent 37, children 38–39.

Sections at the same or overlapping memory intervals evict each other. The
manifest lists all memory overlaps so that a future host loader can invalidate
residency correctly; disjoint parent/child intervals can remain loaded together.

For Your Quarters, section 37 holds 1152 bytes at linked segment `3990`, and
section 38 begins at `39d8`, exactly above it. At runtime those are `4990` and
`49d8`. Sections 38 and 39 share the child interval and cannot both be resident.
Other larger sections beginning at `3990` can overwrite both parent and child.

Calls through the known ten-byte trampolines also manage **return lifetimes**.
The native gate at `2c24:058d` reads the trailing section ID, records a six-byte
return frame, redirects the guest return through manager offset `0701`, and
loads the target before the trampoline jumps into it. On return, the manager
restores the original return address and can reload the caller's section.
Copying one handler into a bank and making a plain far call is therefore not a
complete replacement for these gates. The current port retains the native
manager and this return mechanism.

## Tools and exact handler entries

From gateway-port, after building:

```sh
./build/gateway-overlay ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --output build/rtlink-sections --fixups > build/rtlink-manifest.json
python3 tools/inspect_object_state.py build/object-state-final/native \
  --names ../gateway-dos/OBJECT.DAT --overlay-map build/rtlink-manifest.json \
  > build/rtlink-handlers.json
```

The first command extracts `section-NNN.raw.bin` and `section-NNN.relocated.bin`
for all 58 sections. Relocation defaults to `1000` hexadecimal. The JSON manifest
contains code/relocation extents, BSS/absent-tail sizes, parents, overlaps, and,
with `--fixups`, every fixup site. It is printed to stdout for explicit redirection.
Generated binaries and JSON contain original game data and stay under ignored
build output.

The second command maps every nonzero object handler through its trampoline to
`native_code.filename`, `native_code.section`, `native_code.image_offset` and
`native_code.file_offset`. All 696 distinct handlers map successfully to the
43 expected sections. These are **entry offsets**, not established function-end
boundaries; images also contain constants, tables, helper functions and strings.

| Handler | Section | Image offset | OVL file offset |
| --- | --- | --- | --- |
| Your Quarters | `0x26` / 38 | `0x0035` | `0x514b5` |
| book | `0x26` / 38 | `0x0532` | `0x519b2` |
| drawer | `0x26` / 38 | `0x05f8` | `0x51a78` |

For section 38, the relocation table starts at `0x50f80`, has 320 entries, and
the code starts at `0x51480` with length `0x2960`. These values reproduce the
previously traced section rather than relying on a byte-pattern search.

An extracted handler can be disassembled without knowing the RTLink container:

```sh
objdump -D -b binary -m i8086 -M intel --start-address=0x35 --stop-address=0xb0 \
  build/rtlink-sections/section-038.raw.bin
```

To compare reconstructed bytes with a final snapshot, add
`--snapshot build/object-state-final/native` to gateway-overlay. Its output lists
loaded flags and byte differences, and exits nonzero on a loaded-image mismatch.
The four sections marked loaded there (0, 5, 37, 38) match with zero differences.
A final-snapshot difference can also reflect legitimate mutations to overlay data;
the live completion check below avoids comparing after a handler has run.

## Live native-load verification

```sh
python3 tools/trace_game_logic.py --scenario objects --services state \
  --verify-overlays --output build/rtlink-replay
```

`--verify-overlays` is opt-in on both host executables, `gateway` and `gateway-probe`. It hooks the main loader at
linear `0x2c687`, after reads/BSS/fixups and before the section is marked loaded.
It validates CS/DS `2c24` and all 605 loader bytes at `0x2c45c`–`0x2c6b8`
(FNV-1a `ce5135d0c5b49771`) before interpreting the current section record.
Unrelated intro code is ignored before activation; later signature changes fail.
A run that never matches reports zero verified loads, which the replay tool rejects.

This is a read-only comparison; all loading and return management still execute
in native x86. It reconstructs file bytes with the C++ parser/relocator, compares
every file-backed byte and BSS byte, and excludes the unspecified final tail.
Research traces record `overlay-load` events with section, destination, file
offset, byte count and relocation count. Numeric event fields use hexadecimal,
except the decimal virtual timestamp.

Native, service-verification and service-HLE replays each verify **187 loads
across nine sections**, with no mismatches. Their section-load sequences also
match exactly when ignoring virtual timestamps:

| Section ID (decimal) | Native loads |
| --- | ---: |
| 0 | 25 |
| 1 | 29 |
| 2 | 11 |
| 3 | 42 |
| 4 | 32 |
| 5 | 1 |
| 37 | 16 |
| 38 | 16 |
| 56 | 15 |

The same replay still agrees on all 1434 object-service calls, all 734 object
records, handler mappings, and final framebuffers. The final PPM SHA-256 remains
`0b7ac6ed8e94ced6c85c8b01496ad39108749870029fe75da802cc9af44566fb`.
All 17 CTest targets pass, including malformed RTLink data, relocation/zero-fill
semantics, extraction/snapshot CLI behavior, handler-file mapping and prior tests.

This validates live loading for nine sections and static fixup bounds for all
58; it does not establish gameplay correctness for every section. Headless
research did not compare DOSBox behavior, listen to audio, test room movement
or save/restore, or execute independently extracted handlers.

## Next extraction boundary

Trace the calls leaving the mapped handlers and identify the remaining resident
service contracts. With stable `(section ID, entry offset)` identities, traces
can distinguish a handler across bank reloads instead of confusing the same
linear address with whichever module happens to occupy it. A host section loader
can then replace native file reads/fixups while preserving residency, parent and
return-frame semantics; dropping the original manager before that would lose
required call/return behavior.
