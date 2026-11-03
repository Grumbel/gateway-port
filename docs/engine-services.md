<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
-->

# Gateway native engine-service catalogue

This is the §3.5 catalogue under construction. A routine is considered
*analyzed* only after its original ABI, observed global effects and game
meaning are corroborated by disassembly and native traces. A static call
count alone is **not** evidence for a function's semantics or calling convention.

## Native call census (provisional)

`tools/rank_engine_services.py` consumes the Ghidra instruction addresses but
does **not** trust Ghidra's resolved far-call destinations. It decodes each
CALL from fingerprinted original code, validates the ten-byte RTLink stubs,
and resolves resident far-code segments by matching independent original
code windows against a live native RAM snapshot. This matters because the
same linked far segment may contain code from both `GATEMAIN.EXE` and
`GATE.RTL`; the simple `MZ header + segment*16 + offset` formula is wrong.

From the 2026-11-03 objects replay, 10,178 Ghidra CALL sites were examined,
and 857 still lack a verified source mapping. Eighteen destinations are used
by at least 20 **distinct native object-handler functions**:

| Original code identity | Object handlers | Actions | Status / known name |
| --- | ---: | ---: | --- |
| `rtl:0x0169f4` | 600 | 170 | External RTL, analyzed formatter (below) |
| `ovl:0x00b1e0` (section 4) | 186 | 108 | `gw_format_object_phrase`, analyzed |
| `exe:0x00105e` | 185 | 67 | Unclassified |
| `exe:0x00254a` | 157 | 15 | `gw_native_relation_contains`, analyzed |
| `exe:0x0028e4` | 153 | 46 | `gw_test_object_flag`, analyzed |
| `ovl:0x00c830` (section 4) | 147 | 17 | `gw_relocate_child` |
| `exe:0x00291e` | 87 | 3 | `gw_set_object_flag`, analyzed |
| `exe:0x003e9c` | 74 | 13 | Not yet tracked |
| `rtl:0x00e284` | 70 | 0 | External RTL, uncatalogued |
| `exe:0x002603` | 68 | 11 | Unclassified |
| `rtl:0x0169db` | 61 | 7 | External RTL, uncatalogued |
| `exe:0x006557` | 57 | 1 | Unclassified |
| `exe:0x005c30` | 53 | 1 | Unclassified |
| `exe:0x002951` | 51 | 3 | `gw_clear_object_flag`, analyzed |
| `exe:0x00259c` | 31 | 8 | Unclassified |
| `exe:0x0060cf` | 30 | 8 | `gw_room_exit_dispatch` |
| `exe:0x003f85` | 29 | 3 | Unclassified |
| `exe:0x005bd1` | 23 | 0 | Unclassified |

The `rtl:` identities are file offsets into the original `GATE.RTL`, not
runtime addresses. The current `decomp/functions.tsv` permits only `exe` and
`ovl`; external RTL routines are recorded here until their role is established.
Call counts are *static caller coverage*, not execution frequencies. The
generated per-callee evidence, including native match lengths and segment
support, remains in ignored `build/ghidra-shapes/native-service-fanout.json`.

## External text formatter: `rtl:0x0169f4` (600 object handlers)

This is a large, **printf-like output formatter in `GATE.RTL`**, not an object
handler. Its original bytes (`0x169f4`–`0x16e83`) show a far function frame,
conversion handling for `%d`, `%i`, `%u`, `%o`, `%x`, `%X`, `%c`, `%s`, `%p`,
width/precision digits and `F`, `L`, `N`, `h`, `l` modifiers. It advances an
argument cursor starting at `[BP+0x0a]` by two or four bytes as needed. The
format argument is a **far pointer** at `[BP+6]`/`[BP+8]` (offset/segment).
It calls other relocated runtime helpers to interpret the format, including
far string resolution and numeric conversion; these dependencies have not
yet been independently catalogued.

The observed entry returns with `retf` (`0x16e83`). Callers remove their
arguments: for example, section-4 `GATEMAIN.OVL:0xb889` pushes `SS` and a
local format-buffer offset, calls linked `17cd:105b` and uses `add sp,4` at
`0xb88e`. It does not consume a return register there. The formatter reads
the live stream descriptor `[DS:0x0002]`; its output path passes that word
and a `SS:` buffer pointer to the runtime writer at `GATE.RTL:0x1619b`.
Guest-memory side effects are therefore **stream/device output and whatever
state the runtime writer maintains**, not object-record updates. Exact
return-register and writer side effects are still unverified.

Native trace corroboration: `build/handler-coverage-long/native.trace` has
repeated `entry pc=28d2b` at 9,120–9,193 virtual ms, with CS `27cd`, far
format pointers at entry `SS:SP+4` (e.g. `5b46:db84` and `f000:0c02`),
and return addresses in the section-4 object-description caller. The
`0x28d2b` location is **only this replay's relocated RAM layout**; a live
signature at that address matches the original RTL prologue. This is
strong evidence for a text-output service, but does not establish every
possible formatting behavior. Since the tracker currently represents only
original `exe`/`ovl` files, this external RTL analysis lives here, rather
than inventing an EXE identity for it.

## Section-4 object phrase formatter: `ovl:0x00b1e0` (186 handlers)

`gw_format_object_phrase` is a complex overlay service that constructs a
printable phrase for an object and optionally emits it directly. Its native
function is `GATEMAIN.OVL:0xb1e0`–`0xb90f`, **RTLink section 4**. Its first
far-call arguments are `[BP+6]` formatting flags and `[BP+8]` object ID,
both words. The body rejects invalid object IDs against the global maximum
via `[DS:8f86]`; the flags' low three bits select a formatting variant.
Other mode bits are not yet individually characterized. As with the other
observed C-style far routines, it exits through `retf`, and the callers
remove four argument bytes. For example, the near caller at original
`ovl:0xb9a7`–`0xb9b2` pushes the object and flags `0x0083`, uses
`push cs; call near 0xb1e0`, then `add sp,4`.

Both a string-returning path and a direct-output path are visible:

- At `ovl:0xb8b9`–`0xb8f0`, the service places the constructed string in
  a shared DS buffer starting at `0xaf9e`, advancing a global buffer cursor
  `[DS:0x3e10]` and wrapping it when the next phrase would exceed 128 bytes.
  It obtains a far pointer to this buffer in `DX:AX`; caller `ovl:0xbcdf`
  pushes that far result into another formatting operation.
- When formatting flags include `0x80`, `ovl:0xb883`–`0xb88e` passes a
  stack-local string to the external RTL formatter
  `GATE.RTL:0x169f4`, rather than returning that buffer to be used later.
- At `ovl:0xb8f4`–`0xb903` the service calls resident
  `gw_set_object_flag` with property **7**, recording that the object
  has been shown or identified. This changes guest object state even if
  the returned phrase itself is subsequently ignored.

The live `look` replay with `--extra-trace-entry 43190 --extra-trace-entry
438bb` reached the formatter at virtual 9,120 and 9,187 ms. The former
captured entry stack `(flags=0x0091, object=0x00ee)`; the latter captured
`(flags=0x0002, object=0x0125)`. At the latter call the epilogue returned
`DX:AX=5b46:af9e`, a pointer into the DS buffer; in the companion `objects`
state-verification replay, property 7 was also set after the first-room
description. The addresses `43190` and `438bb` are replay-specific and can
be occupied by *other* overlay sections, so only entries during a confirmed
section-4 activation are evidence for this service. This establishes the
major effects and ABI, not every bit of the formatting flags.

## Resident object-relation predicate: `exe:0x00254a` (157 handlers)

`gw_native_relation_contains` is a far Boolean predicate over one object's
**immediate relation slots**; it is not a recursive containment test.
Arguments are `[BP+6]` object ID and `[BP+8]` candidate related ID, both
16-bit words after the far return address. The body (`0x254a`–`0x259b`)
reads the object's type from the six-byte directory through `[DS:8d50]`,
then the type's relation-slot count (first of three descriptor bytes) through
`[DS:8d52]`. For each slot it calls the far-returning relation accessor at
original `exe:0x2aff` using `push cs; call near`, and compares the returned
AX with the candidate. It returns `AX=1` on the first match or `AX=0`
after all slots; the caller cleans four argument bytes after its `retf`.
It writes no game globals or object records, allocates no memory, and
requires a valid object index at entry; the loop itself does **not** bound
the ID before indexing the object table. The accessor handles type-specific
link layout (including repeated fixed-location fields) described in
[object-state.md](object-state.md#recovered-handler-and-name-inventory).
In a fresh native `objects` replay, `--trace-entry 1157c` captured 35 calls
with object/candidate words at `SS:SP+4`/`+6` (e.g. `0x00d3,0x0125` and
`0x0126,0x0125`). The offset `1157c` is the default resident *runtime*
location of original `exe:0x00254a`, not a stable tracker identity.

## Resident object-property services (153 / 87 / 51 handlers)

The original functions at `exe:0x0028e4` (query), `0x00291e` (set) and
`0x002951` (clear) share a far-call ABI:

| Stack relative to function `BP` | Value |
| --- | --- |
| `[BP+2]`, `[BP+4]` | Caller return IP and CS |
| `[BP+6]` | Object ID (word) |
| `[BP+8]` | Property ID (word) |

All three return with `retf`; the caller removes the four argument bytes.
They push these inputs plus a local far scratch pointer into the near
`exe:0x00284b` address resolver, then clean the **eight** bytes passed to
that helper. The resolver reads `[DS:8d54]` (object-count segment),
`[DS:8d50]` (object directory), `[DS:8d56]` (property definitions), and the
type applicability mask. It yields either a far state-byte address and bit
index or a null pointer. A rejected object/property does no mutation.

| Entry | Result and side effects | Disassembly evidence |
| --- | --- | --- |
| `gw_test_object_flag` | AX = 0/1; reads selected state byte, no writes | `0x2906`–`0x291a`: `les`, `shr`, `and 1` |
| `gw_set_object_flag` | Writes selected state byte OR `(1 << bit)`; no contract for AX | `0x2940`–`0x294a`: `shl`, `or es:[bx],al` |
| `gw_clear_object_flag` | Writes selected state byte AND `~(1 << bit)`; no contract for AX | `0x2973`–`0x297f`: `shl`, `not`, `and es:[bx],al` |

These operations leave unrelated flag bits intact. The two setters can be
idempotent, and the native caller does not use an AX return value; the
experimental HLE preserves entry AX rather than recreating native scratch
register effects. The indexed object/property layout and 16-bit offset
wrapping are independently implemented by
[`src/object_flags.cpp`](../src/object_flags.cpp) and exercised by
`object-flags` CTest. The existing `--object-state verify` replay checks
both query AX and the selected byte after *native* set/clear execution;
`--object-state hle` compares service sequences, final object state and
framebuffers, as described in [object-state.md](object-state.md).

Fresh native comparison: `python3 tools/trace_game_logic.py --probe
build/nix/gateway-probe --native-only --scenario objects --trace-state-writes
--extra-trace-entry 1157c --output build/service-evidence-objects` completed
without native/host disagreement. Its trace contains **1,149 flag queries,
62 sets, 3 clears**, plus the 35 relation-predicate entries; the probe
verified each query result and each proposed native write at the routine's
epilogue. These counts are *execution occurrences in this one scripted path*,
not the distinct-caller totals in the table. The full three-way HLE replay
remains the independent bounded check documented in `object-state.md`.

All four original `exe` entries and one `ovl` service are **analyzed**, not
ported or fully verified under the future §3.7 per-routine differential
harness. The remaining high-fan-out routines, particularly the 185-caller
resident dispatcher, require more detailed classification.
