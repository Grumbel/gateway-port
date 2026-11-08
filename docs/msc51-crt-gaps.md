<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception -->

# Unmatched Microsoft C runtime candidate intervals in GATE.RTL

The Microsoft C 5.1 `LLIBCR.LIB` relocation-aware public-symbol comparison
identified 11,761 bytes across 70 CODE-class candidate spans, with 1,056
bytes in 26 intervening gaps (confirmed by the user's 2026-11-08 host run).
These are **gaps between matching fingerprints**, not established function
boundaries, nor proof that their contents are game code. In particular, the
scanner requires a uniquely attributable original OMF `PUBDEF`; CRT code
with an unrecognized or absent public may remain unmatched.

The new report also verifies the class-restricted matching correction: **75
relocation-aware public candidates** (69 CODE, six DATA), compared with 70
exact-prefix candidates. The union is 76 distinct entries: 69 appear in
both scans, six only in the relocation-aware scan, and one only in the exact
scan. In particular, `_file.c:__iob` at `RTL:0x0326d` and
`_file.c:__iob2` at `RTL:0x0335d` are both present again. The single
exact-only entry, `__amsg_exit` at `RTL:0x06145`, fails the relocation-aware
opening threshold (ten fixed bytes out of the first twenty); the public
symbol itself is not disputed by this result.

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
unmatched bytes as Gateway-specific story logic.

## Audit of the other 21 gaps

The uploaded full-gap JSON identifies each interval's neighboring matched
publics. The following observations are from the **pinned original binary**;
disassembly at the bounds was checked against neighboring instruction flows.
An observed entry prologue or `RETF` is *not* an independently verified
OMF public or a proven live call target.

| RTL file offsets (end exclusive) | Bytes | Observed contents |
| --- | ---: | --- |
| `0x06136..06145` | 15 | Tail of startup/error-exit sequence; resets `DS` and writes via `SS` before the adjacent `__amsg_exit` entry |
| `0x06159..0615a` | 1 | Zero separator |
| `0x0621e..06235` | 23 | Separate exit/cleanup prologue, two calls to shared `RTL:0x062a9` helper, then a jump into `__exit`'s body; function name unverified |
| `0x062e0..062e6` | 6 | Loads error code 2 and jumps to `__amsg_exit` |
| `0x06553..06554` | 1 | Zero separator |
| `0x07a30..07a50` | 32 | Complete-looking DOS file-descriptor close wrapper (`INT 21h`, AH=`3eh`), with descriptor bound/state checks and a jump to shared error handling at `RTL:0x065ec` |
| `0x07aca..07ad2` | 8 | Prologue and initial local-variable setup for the open helper that continues at the matched `__copensub` entry |
| `0x07ead..07eae` | 1 | Zero separator |
| `0x08157..08158` | 1 | Zero separator |
| `0x082af..082b0` | 1 | Zero separator |
| `0x08315..0831a` | 5 | Zero byte, a near jump over one byte, then another zero; precedes `__catox` |
| `0x0838c..08396` | 10 | Integer-to-text alternate-entry thunk, which jumps into shared conversion at `RTL:0x0915e`; adjacent `_itoa` has a different parameter setup |
| `0x08467..08468` | 1 | Zero separator |
| `0x0851a..0851e` | 4 | `RETF 8` and one zero byte; appears to terminate the preceding FAR arithmetic helper |
| `0x08cb8..08cc8` | 16 | Checks an initialization flag, calls `RTL:0x08cc8` only once, increments the flag and returns; a probable time-zone initialization guard |
| `0x08ffa..09008` | 14 | FAR-call wrapper passing a zero offset and current `DS`; target is unresolved in the raw file |
| `0x09035..09036` | 1 | Zero separator |
| `0x0915e..0916b` | 13 | Shared 32-bit integer-to-text argument setup; reached directly by the earlier `RTL:0x08393` jump and falls into matched `__cxtoa` |
| `0x091c1..091c2` | 1 | Zero separator |
| `0x09212..09230` | 30 | Complete-looking 32-bit rotate-left helper: masks shift count with `1fh`, rotates `DX:AX` left and reinserts carry into AX, then FAR-returns |
| `0x09251..09262` | 17 | Two zero separators surrounding a DOS delete-file wrapper (`INT 21h`, AH=`41h`) that jumps to shared error handling |

Eight of these are **single-byte zero-only** intervals. Several others
contain recognizable helper entry points or shared tails, but the report
still does not justify claiming an exact count of *unrecovered CRT routines*.
These classifications account for the remaining **201 bytes** as static
observations without changing the original conservative gap/coverage
metrics.

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
the separate exact-prefix baseline remain unchanged. The user's new report
confirms the class-restricted DATA matching correction.

## Next validation

1. For the largest intervals, identify entry starts and callers using
   relocated runtime addresses or OMF module candidates. Treat an unlinked
   raw `RETF` as a boundary hint, not proof.
2. Map the demonstrated close/delete/rotation wrappers to actual OMF publics
   or original runtime call sites, rather than assuming names from behavior.
3. Only assign a definite source symbol after independent OMF or runtime
   evidence; do not expand CRT attribution solely from proximity.
