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
| `exe:0x00105e` | 185 | 67 | `gw_dispatch_action_context`, analyzed |
| `exe:0x00254a` | 157 | 15 | `gw_native_relation_contains`, analyzed |
| `exe:0x0028e4` | 153 | 46 | `gw_test_object_flag`, analyzed |
| `ovl:0x00c830` (section 4) | 147 | 17 | `gw_relocate_child`, analyzed |
| `exe:0x00291e` | 87 | 3 | `gw_set_object_flag`, analyzed |
| `exe:0x003e9c` | 74 | 13 | `gw_schedule_timed_event`, analyzed |
| `rtl:0x00e284` | 70 | 0 | External RTL, uncatalogued |
| `exe:0x002603` | 68 | 11 | `gw_relation_reachable`, analyzed |
| `rtl:0x0169db` | 61 | 7 | External RTL, uncatalogued |
| `exe:0x006557` | 57 | 1 | Unclassified |
| `exe:0x005c30` | 53 | 1 | Unclassified |
| `exe:0x002951` | 51 | 3 | `gw_clear_object_flag`, analyzed |
| `exe:0x00259c` | 31 | 8 | `gw_relation_contains_in_slot`, analyzed |
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

## Resident action-context coordinator: `exe:0x00105e` (185 handlers)

`gw_dispatch_action_context` is a resident **action/event dispatch coordinator**,
not the lower-level action table dispatcher itself. The original far routine
spans `GATEMAIN.EXE:0x105e`–`0x13e5`. It receives three 16-bit stack words:
`[BP+6]` action/event ID (only the low byte is saved for dispatch),
`[BP+8]` first object ID and `[BP+0x0a]` second object ID. It returns AX
from the selected stage, with caller cleanup of six argument bytes. The
caller at `exe:0x2301`–`0x2315` pushes precisely these three arguments,
executes `push cs; call near 0x105e`, uses `add sp,6`, and tests whether
AX equals **2**.

The native stages are:

1. Increment nesting counter `[DS:0x359c]`, preserve the previous action
   byte, object IDs and two 40-byte context blocks, and install this call's
   values in the global parser context. The active objects' type bytes are
   derived through the six-byte object directory.
2. Call the section-4 per-object setup helper via validated RTLink stub
   `1c24:12d1` (original OVL `0xc5e7`) for both object IDs. The nested
   helper may update object-related global state; this is not a pure call.
3. Call the resident object-handler dispatcher (original EXE `0x5a46`,
   linked `0476:00f8`) for staged event codes 1–6 against context objects
   and relevant locations, stopping when a stage returns nonzero.
4. If none intercepts, call the action selector (original EXE `0x5956`,
   linked `0476:0008`), then the final action-table dispatcher (original EXE
   `0x5994`, linked `0476:0046`). Both are distinct from this routine.
5. Restore the saved global context and copied blocks, decrement the depth,
   return the selected result in AX via `retf`.

One notable original edge path is at `exe:0x1066`–`0x10b9`: when a nested
invocation repeats the same action and object IDs too many times, the code
increments `[DS:0xadb2]`, sends a diagnostic through the RTL formatter,
and returns `AX=2`. This early jump bypasses the ordinary context restore
and nesting-counter decrement; do not silently normalize that behavior in
a future C++ translation. Exact semantics of individual event codes and the
reason for the duplicate-invocation threshold remain open.

The `objects` native trace (`build/service-evidence-dispatch/native.trace`)
captured eight ordinary calls to default-layout entry `1000:01a4` and eight
common epilogue visits. The entry words include actions `0x005f`,
`0x0071`, `0x0046`, `0x0053`, `0x0032`, and `0x001d`, with the matching
first-object IDs at `SS:SP+6`. The epilogue hook at `1000:0523` observes
the context-restore path immediately **before** `mov ax,[bp-0x5a]`;
therefore it does not itself verify final return AX. This is the default
resident launch mapping, not a reusable service identity. The known
action-table/selector behavior is independently documented in
[action-dispatch.md](action-dispatch.md).

## Section-4 child-list relocation: `ovl:0x00c830` (147 handlers)

`gw_relocate_child` is a far, **state-mutating** routine at section-4
`GATEMAIN.OVL:0xc830`–`0xc911`. Its three stack words are `[BP+6]` object,
`[BP+8]` new parent/location and `[BP+0x0a]` child-slot index. The caller
cleans six argument bytes: the room-transfer caller at original EXE
`0x5f05`–`0x5f1b` pushes slot 0, destination room and actor, calls the
validated RTLink stub `1c24:13d5`, then uses `add sp,6`; the section-4
near caller at OVL `0xc936`–`0xc943` follows the same convention.
The service exits with `retf` and has no demonstrated meaningful AX result.

Its original body first reads the object's existing location, child slot,
child-head pointer and next sibling. It unlinks the object from the old
parent's head or sibling chain, writes the new location and slot, and if
the destination is nonzero **prepends** it to the new parent's child-head
chain, preserving the previous head in the moved object's `next_sibling`.
These are writes to the original guest object records through resident
type-sensitive accessors, not an allocation or a flat child array. An
invalid or cyclic preexisting chain has not been exhaustively characterized.

Source corroboration: OVL `0xc879`, `0xc8c2` and `0xc8f8` are respectively
the original unlink-head, set-location and insert-head service call sites.
The earlier `--trace-navigation` native replay observed four verified
location transitions through `0xc8c2` and matched the entry triple
`(object=0x00d3, parent=0x00f1/0x00ee, slot=0)`; see
[action-dispatch.md](action-dispatch.md#the-shared-child-list-relocation-routine).
The additional native `objects` trace in `build/service-evidence-timers`
captured section-4 entries during startup movement, e.g. a call at 5,408
virtual ms with `(object=0x016d, parent=0, slot=0)`.

## Resident scheduled-event insertion: `exe:0x003e9c` (74 handlers)

`gw_schedule_timed_event` adds or updates a **turn-countdown event** in the
resident queue. The real far function begins with `push bp` at original EXE
`0x3e9c` and ends at `0x3f12`. The Ghidra 12 auto-function begins **one
byte late** at `0x3e9d`, then splits two adjacent queue helpers at internal
instructions. `tools/decomp_status.py seed` now canonicalizes these verified
boundaries: `0x3e9c` (119 bytes), `0x3f13` (114 bytes, remove/compact), and
`0x3f85` (61 bytes, membership predicate). Their corresponding phantom
Ghidra rows are not independent native routines. An unchanged tracker
reseed consequently reports zero changes.

The scheduler receives `[BP+6]` event ID (its **low byte** is used) and
`[BP+8]` remaining turns (a 16-bit word). It calls another resident helper
at linked `0805:0c02`, then scans `[DS:0x35c2]` occupied entries of a
four-byte array via the segment pointer `[DS:0x8d5a]`. An existing event
ID is updated in place; a new event takes the next slot. A full 16-entry
queue emits a diagnostic through the RTL text formatter and skips the
append. The normal path writes the event byte at entry `+0`, the delay
word at `+2`, and increments `[DS:0x35c2]` for a new slot. It does not
initialize the unused byte `+1`; the save format serializes that field as
part of a `u16` event ID, so retaining the original memory bytes matters.
Callers remove four argument bytes after `retf`. No caller-visible AX return
semantics have been verified.

In a fresh native `objects` replay, entry `12e3:002b` (linear `0x12e5b`,
specific to this capture) was reached **five times** during startup at
5,407 virtual ms. The entry stack carried
`(event=0x21, turns=0x00b3)`, `(0x17,0x01e0)`, `(0x18,0x0258)`,
`(0x25,0x02d0)` and `(0x2a,0x030c)`. This independently matches the
four-byte event records and countdown behavior recovered from original
saves in [save-games.md](save-games.md#global-state-and-timers).
The companion `gw_remove_timed_event` and `gw_timed_event_exists` routines
have native disassembly-based boundaries but no captured calls in this
replay; they remain **named**, not analyzed or verified.

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

## Resident relation-slot predicate: `exe:0x00259c` (31 handlers)

`gw_relation_contains_in_slot` accepts three far-call argument words:
`[BP+6]` source object, `[BP+8]` wanted relation target, and `[BP+0x0a]`
required slot. Callers clean six argument bytes and use `AX` as a Boolean.
If the signed required slot is nonnegative, the native body first calls
`exe:0x2c91` to read the object's slot byte, returning `AX=0` if the slot
differs. A negative required slot skips that check. It then reads the
type-dependent relation count from the directory/descriptor tables through
`[DS:8d50]` and `[DS:8d52]` and compares each immediate relation returned
by `exe:0x2aff` against the wanted ID. It returns `AX=1` on the first
match and zero otherwise; unlike the recursive helper below, it does not
follow the relation transitively. The original function is `0x259c`–`0x2602`
(103 bytes), has no observed game-state writes or allocations, and does
not validate the source object's index before the first directory lookup.

The native `objects` replay at `build/service-evidence-relations/native.trace`
entered this service at temporary resident linear address `0x115ce` at
25,332 virtual ms. Its far entry stack contained source `0x0123`, wanted
object `0x00d3`, and required slot `1`. That is an actual original call,
not just a static cross-reference; branch-level outcomes have not yet been
recorded by a differential harness.

## Resident recursive relation reachability: `exe:0x002603` (68 handlers)

`gw_relation_reachable` accepts `[BP+6]` source and `[BP+8]` wanted object,
both words; callers clean four bytes. It returns an `AX=0/1` predicate and
does not intentionally mutate guest state. Its native `0x2603`–`0x266f`
(109 bytes) body iterates the source object's type-dependent relation links
using the accessor `exe:0x2aff`. If a link equals the wanted object it
returns true; otherwise it recursively calls **itself** with that link as
the source. A zero-valued link causes an **immediate false return**, even
when later slots exist, and there is no cycle-detection guard. Both details
must survive a native reimplementation. The graph's interpretation as
pure spatial containment has not been established; its exact link layout
is described in [action-dispatch.md](action-dispatch.md#resident-recursive-object-relations).

Ghidra 12 splits this routine at the interior jump target `0x2631`, falsely
exporting a second function of 63 bytes. The native prologue at `0x2603`,
recursive near call at `0x2624`, `retf` at `0x266f` and next prologue at
`0x2670` prove a single 109-byte body; the tracker seeder now normalizes
the split. The native `objects` replay entered the routine at temporary
resident linear `0x11635` at 28,393 virtual ms with arguments
`(source=0x0260,wanted=0x00d3)`. This verifies a real invocation and the
original stack layout, not every recursive branch or pathological cycle.

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

All eight original `exe` entries and two `ovl` services are **analyzed**, not
ported or fully verified under the future §3.7 per-routine differential
harness. The remaining high-fan-out routines include two external RTL
targets and several resident services; all still require native semantic
evidence.
