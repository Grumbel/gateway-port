<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception -->

# Unmatched Microsoft C runtime candidate intervals in GATE.RTL

The Microsoft C 5.1 `LLIBCR.LIB` relocation-aware public-symbol comparison
identified 11,761 bytes across 70 CODE-class candidate spans, with 1,056
bytes in 26 intervening gaps (first cross-record host run, 2026-11-07).
These are **gaps between matching fingerprints**, not established function
boundaries, nor proof that their contents are game code. In particular, the
scanner requires a uniquely attributable original OMF `PUBDEF`; CRT code
with an unrecognized or absent public may remain unmatched.

## Static disassembly observations

The following evidence comes from the SHA-256-pinned original `GATE.RTL`
and `objdump` in 16-bit x86 mode, **not** from a matching Microsoft library
object or a live entry trace. Relocation operands are unresolved in the raw
file. Names below describe observed behavior, not verified export symbols.

| Gap (original file offsets, end exclusive) | Bytes | Static instruction evidence | Interpretation |
| --- | ---: | --- | --- |
| `0x06330..0x064d8` | 424 | Walks PSP command tail at offset `0x81`, handles whitespace, quotes and backslash runs; counts arguments, allocates stack space, copies strings and constructs FAR pointers; ends in indirect FAR jump | Command-line argument and environment setup; high-confidence **CRT-like** behavior, exact routine entry/name unverified |
| `0x07f1a..0x07ff7` | 221 | FAR-pointer allocation flags, heap block-size alignment, segment allocation and a chain of near helper calls; the trailing code extends across the gap boundary | Heap allocation helpers; high-confidence **CRT-like** behavior, not one complete bounded function |
| `0x08b48..0x08bb6` | 110 | Saves six input register words, invokes DOS `INT 21h`, writes return registers and carry status; followed by `IN AL,DX` and `OUT DX,AL` FAR-returning wrappers | DOS interrupt and port-I/O CRT helpers; high-confidence behavior, unverified original public names |
| `0x06726..0x0676a` | 68 | Iterates a table of 12-byte stream descriptors, tests flags and invokes a FAR close-like helper, accumulating successful calls | Probable stream-close-all helper; requires call-target identification |
| `0x065ec..0x0660c` | 32 | Conditional branches, signed-error return and several FAR return epilogues | Tail of adjacent error-handling/runtime code; **no independent routine identity** established |

The first three gaps total **755 bytes**, and the first four total **823
bytes**. Their contents are inconsistent with interpreting all 1,056
unmatched bytes as Gateway-specific story logic. The remaining 21 gaps
(201 bytes after accounting for all five rows) have not yet been individually
classified. They may include alignment, branch tails, linker-specific code,
different CRT revisions and actual unmatched routines.

To reproduce a bounded disassembly without copying binary contents into the
repository:

```sh
objdump -D -b binary -mi8086 -M intel \
  --start-address=0x6330 --stop-address=0x64d8 \
  ../gateway-dos/GATE.RTL
```

To obtain the complete ordered set of unmatched intervals from the **real
Microsoft C library** on a host with Nix, run:

```sh
nix run -L .#msc51-compare -- \
  --omf-details --scan-relocations --all-crt-gaps \
  --json build/msc51-crt-relocations.json
```

The JSON always includes `public_prefix_candidate_span_summary.all_unmatched_gaps`
in original-file order, with neighboring known public names, as well as the
largest eight intervals by size. Existing `--scan-relocations` behavior and
the separate exact-prefix baseline remain unchanged. The class-restricted
DATA matching correction made after the previous host run still requires
user-side confirmation.

## Next validation

1. Verify the ordered gaps with the actual `LLIBCR.LIB` report and check if
   the class-restricted scan restores distinct `__iob` and `__iob2` entries.
2. For the large remaining intervals, identify entry starts and callers
   using relocated runtime addresses or OMF module candidates. Treat an
   unlinked raw `RETF` as a boundary hint, not proof.
3. Only assign a definite source symbol after independent OMF or runtime
   evidence; do not expand CRT attribution solely from proximity.
