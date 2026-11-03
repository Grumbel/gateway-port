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
| `rtl:0x00e284` | 70 | 0 | External RTL bounded string append, analyzed |
| `exe:0x002603` | 68 | 11 | `gw_relation_reachable`, analyzed |
| `rtl:0x0169db` | 61 | 7 | External RTL character-output wrapper, analyzed |
| `exe:0x006557` | 57 | 1 | `gw_add_score`, analyzed |
| `exe:0x005c30` | 53 | 1 | `gw_present_story_text`, analyzed |
| `exe:0x002951` | 51 | 3 | `gw_clear_object_flag`, analyzed |
| `exe:0x00259c` | 31 | 8 | `gw_relation_contains_in_slot`, analyzed |
| `exe:0x0060cf` | 30 | 8 | `gw_room_exit_dispatch`, analyzed |
| `exe:0x003f85` | 29 | 3 | `gw_timed_event_exists`, analyzed |
| `exe:0x005bd1` | 23 | 0 | `gw_present_child_list`, analyzed |

The `rtl:` identities are file offsets into the original `GATE.RTL`, not
runtime addresses. The current `decomp/functions.tsv` permits only `exe` and
`ovl`; external RTL routines are recorded here until their role is established.
Call counts are *static caller coverage*, not execution frequencies. The
generated per-callee evidence, including native match lengths and segment
support, remains in ignored `build/ghidra-shapes/native-service-fanout.json`.

## RTL interactive retry loop: `rtl:0x015f15` (provisional)

The original `GATE.RTL` contains a far routine with a `push bp; mov bp,sp`
prologue at `0x15f15` and `retf` at `0x1611b`. Its call at `0x16044` is
followed by `or ax,ax; jne 0x16044`, a tight loop until the callee returns
zero. It subsequently makes another far call, stores that result as an input
word, and distinguishes `0x20` (space), `0x0d` (Enter), and `0x141`/`0x142`.
Other parts of the routine use display/scroll position words. Calling it
an interactive input/display routine is an **inference**, not a validated API.

The original RTL offsets were mapped to runtime segment `27cd` using the
**same offset delta `0x15999`** independently found at the byte-verified
character-output wrapper (`27cd:1042` -> `rtl:0x169db`) and formatter
(`27cd:105b` -> `rtl:0x169f4`). The far-call return at `27cd:06b0` therefore
corresponds to RTL offset `0x16049`, immediately following the call at
`0x16044`. A seeded native bar replay reported that address as the caller
of **all 38,212** invocations of RTLink section 0, target `4319:06ee`,
resolved to `GATEMAIN.OVL:0x0d7e`. No observed call from a second caller
was reported. Three full/seeded bar recordings differ by at most 13 extra
invocations, all at that target; all other per-target counts and the final
original game save agree. See [golden replays](golden-replays.md).

The follow-up original-code disassembly of `GATEMAIN.OVL:0x0d7e` verifies
the **return-value computation**. It calls a shared helper at linked
`13bd:06da` with four zero words, stores its result, and conditionally
performs additional calls/state updates when that result is nonzero. It
also calls linked `1367:000a`, ORs that result with the saved helper value,
and executes `cmp cx,1; sbb ax,ax; neg ax` after moving the OR to `CX`.
Consequently, it returns **AX=1 precisely when both underlying results are
zero**, and AX=0 when at least one is nonzero. This is consistent with the
RTL retry loop continuing while there is no work/input available and
breaking on a pending event. The two helpers' exact contracts, condition
for the transition and non-object side effects are still unverified.

In a seeded bar test, detailed tracing began at **53,500 guest ms**. All
**256 sampled entries and returns** matched; every call returned `AX=1`
(one `0000 -> 0001`, then 255 `0001 -> 0001`). A follow-up capture starting
at **143,995 guest ms** observed **59 entries and 59 matched returns**:
58 returned `AX=1`, then **one returned `AX=0` at 144,000 guest ms**. The
retry loop therefore terminates normally when this target changes its
result; the observed extra iterations are not distinct story-handler calls.
Both samples reported zero object-record byte deltas, but the summary does
not establish whether native object-table snapshots were available for
every event. Neither sample proves there were no non-object memory writes.
The two underlying helpers' specific event conditions remain unidentified;
there is no native/HLE replacement yet.

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

## External character-output wrapper: `rtl:0x0169db` (61 handlers)

This is a 25-byte **far character-output wrapper** in `GATE.RTL` immediately
before the formatter above. At original `0x169db`–`0x169f3`, it accepts one
16-bit stack argument at `[BP+6]`, then explicitly **zeros the upper byte
at `[BP+7]` in the caller's argument slot**. It passes three words to a
near same-segment writer at `GATE.RTL:0x1619b`: the output-stream word at
`[DS:0x0002]`, the offset of the character argument on the stack, and `SS`
as its segment. The writer therefore receives a far pointer to the
zero-extended character. This wrapper removes the writer's six argument
bytes with `add sp,6` and returns with `retf`; its own callers clean the
single two-byte character argument. For example, original EXE
`0x5cc4`–`0x5ccd` pushes `0x000a`, calls linked `17cd:1042`, then executes
`add sp,2`. There is no established meaningful
return-register contract. Its effects are stream/device output and the
deliberate write to the **caller stack**, not object-state mutation. The
writer's internals and error handling are still not independently analyzed.

The native `objects` replay at `build/service-evidence-score-text/native.trace`
captured **18 entries** at temporary runtime linear `0x28d12`
(`CS=27cd`) during text rendering. Example argument words at `SS:SP+4`
are `0x000a`, `0x0009`, and `0x002e`, while return addresses point into
active original display/text functions. The adjacent RTL formatter was
independently located at linear `0x28d2b` in the same launch, exactly
25 bytes later. This validates the code identity, single-word ABI and
output usage; it is not a per-routine replacement differential test.

## External bounded string append: `rtl:0x00e284` (70 handlers)

This original `GATE.RTL` far function spans `0xe284`–`0xe2d8` (85
bytes) and returns `AX=0/1`. Its input is a far, NUL-terminated source
string pointer at `[BP+6]` offset / `[BP+8]` segment, with **four argument
bytes removed by callers**. It first checks a shared destination far
pointer for null. Two calls to the *same* external length helper measure
the destination and source; it adds their 16-bit lengths and compares the
sum, **strictly less than**, the shared capacity. If there is room, it
passes the destination and source far pointers to a four-word external
string-combining helper and returns `AX=1`. Otherwise it returns `AX=0`
without calling that helper. The sum is 16-bit and uses an unsigned
comparison, so overflow is a possible original edge case; do not silently
substitute a different capacity check in a native port. In the normal
success path the destination string changes, while the function itself
performs no object-record writes and no direct allocation. The precise
underlying helper implementations are still unclassified; their use as
string length/append operations follows from their argument shapes,
double-length guard and the actual input replay, not matching CRT symbols.

The on-disk RTL image has **zero relocation placeholders** at the pointer
and helper references, so literal `DS:0` / `DS:2` interpretations are
incorrect. The source-pinned live memory at
`build/handler-coverage-long/native.bin` reveals the relocated shared
destination pointer at `[DS:0x0f8a]` (far offset/segment), the bound at
`[DS:0x9dfe]`, and the repeated far length helper at linked `1805:2bb4`;
the append helper is at linked `1805:2b38`. The capacity word in that
snapshot is **`0x0100` (256 bytes)**. These are relocated *runtime* DS
addresses, not fixed file identities or global locations guaranteed for
other Legend versions.

In the original extended office walkthrough at
`build/service-evidence-office-extended/native.trace`, runtime linear
entry `0x21890` (`2183:0060`) was reached **289 times** while individual
BIOS keypresses were being accepted. For example the 14,000–14,300 ms
entries have source far pointer `5b46:dc22` at `SS:SP+4/+6`, and the
input character in BX changes from `0x6c` to `0x6f` to `0x6f` to
`0x6b` along with the scripted keystrokes. This establishes a real
input-buffer append use; the 70 static handler callers may use it in
other string-building contexts too. There is no per-routine differential
test for capacity-boundary and overflow cases yet.

At temporary return `4319:14d5`, the source-pinned native RAM has
`add sp,4` following the linked far call, independently confirming the
caller removes both source-pointer words. The new
`run_walkthrough.py run --extra-trace-entry LINEARHEX` option forwards
additional hooks to the original probe without changing the reviewed
keystroke sequence or distributing the recorded RAM.

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
The companion `gw_remove_timed_event` has a corrected native boundary but
no captured calls in this replay and remains **named**. The membership
helper below was independently captured later in the armory stage.

## Resident scheduled-event membership: `exe:0x003f85` (29 handlers)

`gw_timed_event_exists`, original EXE `0x3f85`–`0x3fc1` (61 bytes), is a
far **Boolean queue membership test**. Its one word at `[BP+6]` is an
event ID (only the low byte is compared); the caller removes two argument
bytes after `retf` and consumes `AX=0` (absent) or `AX=1` (present).
In a confirmed native armory call, the return address `49d8:11c9`
(temporary linear `0x4af49`) begins `add sp,2; or ax,ax; je ...`, which
confirms both caller cleanup and the Boolean result test.

The body initializes its local index to zero, walks exactly
`[DS:0x35c2]` occupied records in the four-byte event array through
`[DS:0x8d5a]`, and compares the candidate event byte at each record's
offset `+0`. There are no direct queue writes or allocations. It also
calls an external service at linked `0805:0c02` after setting `AX=2`,
before scanning; **that helper's side effects remain unclassified**, so
the whole routine must not yet be assumed globally pure.

The reviewed original armory/class walkthrough at
`build/service-evidence-armory/native.trace` entered the routine at
temporary `12e3:0114` (linear `0x12f44`) **18 times**. First
calls at 51,428 virtual ms passed event IDs `0x37` and `0x38`; later
calls at 66,178–97,848 ms repeatedly queried `0x22`. These are the
actual stack words at `SS:SP+4`, not static references. The previous
timer insertion replay separately established the four-byte event
layout. The helper is now `analyzed` but not **ported** or **verified**
under the planned differential harness.

## Resident room-exit dispatcher: `exe:0x0060cf` (30 handlers)

`gw_room_exit_dispatch` is one **691-byte far function** at original EXE
`0x60cf`–`0x6381`. It accepts one direction-code word at `[BP+6]` (only
the low byte is compared), and the caller cleans two argument bytes. The
observed caller at original EXE `0x66f2`–`0x6704` loads the direction
from the shared state byte `[word DS:8dce]:e665`, zero-extends and pushes
it, calls the dispatcher using `push cs; call near`, then uses `add sp,2`.
The native replay's saved far return `148e:0c28` corresponds to original
`0x6701`. The routine returns `AX=1`
on the handled-exit path at `0x6201` and `AX=0` when no indexed or matching
exit is found (`0x6302`); **handled does not necessarily mean that the
player moved**. This particular caller overwrites AX with 1 on return,
so its own caller does not consume the dispatcher's Boolean directly;
the function's other scratch registers and flags are not established as
public results.

It reads the current-room word at `[word DS:8dba]:35c6`, its indexed-exit
count and one-based first-exit index through resident room-record getters,
then loops over the original four-byte exit records at
`[word DS:8dcc]:417e`. A record has `direction:u8`, `kind:u8` and
`argument:u16`. The jump comparisons at `0x6352`–`0x637a` dispatch **five
different kinds**:

| Kind | Original case block | Established effect |
| --- | --- | --- |
| 1 | `0x611d` | Passes the record's room ID to resident room transfer `exe:0x5d5d` |
| 2 | `0x6156` | Looks up a condition object/destination pair through `[DS:8dd2]`; checks object flags `0x0c` and `0x10`, may set `0x0c`, emit text or move |
| 3 | `0x6139` | Reads a far text pointer from the `[DS:8dd0]` indexed table, then enters the shared message path; message encoding still requires analysis |
| 4 | `0x6269` | Calls the room callback with event `0x0f`; a valid different room ID returned in AX feeds room transfer; zero or same-room can be handled without moving |
| 5 | `0x6207` | Selects data through `[DS:8dd4]`, checks a referenced byte and may feed a far pointer into the shared message path; exact game-level condition unresolved |

The dispatcher can change room state, object flags, child chains and
game-visible output through its callees. It allocates no guest memory
directly. See [gateway-map-reference.md](gateway-map-reference.md#recovered-native-room-exit-table-and-resident-scanner)
for the independently decoded record tables and conditional routes, and
[action-dispatch.md](action-dispatch.md) for the earlier native location
transitions and kind-4 callback experiment. These delegated mutations and
the five case paths make this a **complex** routine, not five separate
services.

Ghidra 12 exported only the first 69 bytes of the actual routine at
`0x60cf` and invented nine functions at its internal branch labels
`0x6114`, `0x611d`, `0x6139`, `0x6156`, `0x6207`, `0x6269`, `0x62a8`,
`0x6302` and `0x637d`. The only real far return is at `0x6381`, followed
by the next prologue at `0x6382`. The seeder suppresses these phantoms,
including the previously named conditional/callback *case labels*, and
retains one original-offset tracker identity.

The fresh original office walkthrough
(`build/service-evidence-office/native.trace`, 12 scripted actions) reached
temporary runtime entry `148e:05f6` (linear `0x14ed6`) **three times** at
45,158, 48,206 and 51,206 virtual ms, with direction arguments
`0x0003`, `0x0001`, `0x0001` on their far entry stacks. Earlier native
EAST/WEST replays independently verified child-relocation and actor-location
changes following this scanner. This evidence establishes actual invocation
and the major ABI/control flow, not complete dynamic coverage of all five
exit kinds or a replacement differential test.

## Resident score accumulator: `exe:0x006557` (57 handlers)

`gw_add_score`, original EXE `0x6557`–`0x65ae` (88 bytes), accepts a single
score-award word at `[BP+6]`, uses a far `retf`, and has **caller cleanup of
two bytes**. Independent resident near-call sites `0x6011`–`0x6016` and
`0x660c`–`0x6611` each push AX, `push cs; call near 0x6557`, then
`add sp,2`. Both callers immediately repurpose AX; there is no verified
return-value contract. Zero awards return without touching score or output.
For nonzero awards it performs a native **16-bit wrapping addition** into
`[DS:0x3946]`: there is no overflow test or score clamping. This is a
direct guest-global write, not object allocation or room relocation.

After incrementing the score it checks byte `[word DS:8dd6]:438f`. If
zero, it skips the optional announcement path. Otherwise it emits an
amount-bearing line through the external RTL formatter at linked
`17cd:105b`, optionally emits a one-time extra announcement when
`[DS:0x3948]` is zero (then sets that byte to 1), and finally emits a
second shared DS string via the same formatter. These print side effects
are conditional; the score addition is not. Formatting and output status
have not been captured independently at the inner writer boundary.

A fresh native replay of the **first 12 actions of the office walkthrough**,
reusing its deterministic `replay.keys`, captured two calls at temporary
linear `0x1535e` (`148e:0a7e`) in
`build/service-evidence-office/extra.trace`: at 21,328 virtual ms the
far entry stack carried award `0x0002`, and at 21,421 ms it carried
award `0x0003`. Both calls arose during the original take-items action;
the caller frame included the corresponding native object context.
The original conditional output branch and 16-bit wrap were verified from
disassembly, not exercised across all flag states in this bounded trace.

## Resident story-text presentation: `exe:0x005c30` (53 handlers)

`gw_present_story_text` is a 40-byte far **presentation coordinator** at
original EXE `0x5c30`–`0x5c57`. It receives a far string pointer at
`[BP+6]` offset / `[BP+8]` segment. The native caller removes the four
argument bytes; after a linked call, one observed source return at
`49d8:08e2` begins `add sp,4` (native linear `0x4a662`, source-pinned
RAM). The function itself contains no explicit writes to story globals,
object records, or heap allocation, and no known caller consumes a
meaningful AX result. Its **effects are delegated to the display subsystem**:

1. It first calls linked runtime `17cd:045e` with no arguments; the
   relocated body checks and prepares the active output stream state.
2. It passes the input far pointer to linked RTL `0dbd:003e`, a larger
   GUI/text rendering routine with screen-state checks.
3. It passes a zero word to RTL `0dbd:029a`, then calls `0dbd:0330`
   without arguments. These latter functions manage GUI/output state,
   but their exact flushing, pagination and window effects have not been
   separately characterized.

The native extended office walkthrough captured **two calls** to
temporary linear `0x14a37` (`148e:0157`) at 27,573 and 57,433 virtual
ms. Their input far strings were `5b46:6a3a` and `5b46:6f51`, with
return addresses into independently loaded overlay sections. This
supports a reusable story-text presentation role; it does **not** prove
that every call displays a modal dialog or synchronously waits for input.
Do not substitute a simplified print-only service until the downstream
GUI behaviors and caller-visible ordering are verified.

Ghidra 12 cuts the body into pseudo-functions at `0x5c30` (14 bytes) and
`0x5c42` (22 bytes); the latter starts **within a far-CALL instruction**.
Original `retf` at `0x5c57` and the next prologue at `0x5c58` establish
the real 40-byte boundary. The adjacent child-list presenter at
`exe:0x5bd1`–`0x5c2f` similarly has a ghost boundary at `0x5bfa`, inside
the `cmp` immediate. The seeder merges both originals; the child-list
presenter now has independent native-call evidence below.

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

All thirteen original `exe` entries and two `ovl` services are **analyzed**, not
ported or fully verified under the future §3.7 per-routine differential
harness. All three analyzed external RTL functions live only in this catalogue,
not the `exe`/`ovl` tracker. This completes the §3.5 catalogue for the
eighteen services with at least twenty distinct object-handler callers.

## Resident conditional child-list presentation: `exe:0x005bd1` (23 handlers)

`gw_present_child_list` is a 95-byte far function
(`0x5bd1`–`0x5c2f`) with caller-cleaned word arguments
`[BP+6]=object` and `[BP+8]=child_slot`. It conditionally prints the objects
in that child's linked-list slot **whose property 8 is not set**. The
`child_slot` argument is passed unchanged to both overlay helpers; it is
also used to choose one of two formatting selectors (`0x49` normally,
`0x4f` for slot 1). Ghidra's apparent function at `0x5bfa` begins inside
an instruction and is not an independent entry.

The original caller at `ovl:0x051aea`–`0x051af9` passes object `0x0124`
and slot 0, then executes `add sp,4` after its linked far call. It is
reachable through object `0x0124`'s event-6 handler when the current command
code is `0x3a` and object property `0x0c` passes its guard. An independent
three-command native replay (an OPEN action followed by two examinations)
reached this service at temporary linear PC `0x149d8` twice, at guest
21,507 and 24,506 ms. Both calls had arguments `(0x0124,0)` and both
entered the successful branch: the first linked call returned AX=1.

The two inner RTLink trampoline identities are now resolved by original
EXE records and the live section-qualified call census:

| Linked call | Original trampoline | Original callee | Role |
| --- | --- | --- | --- |
| `1c24:3dbf` | `exe:0x00e4f9` | `ovl:0x00bf3d` (section 4) | Count eligible children |
| `1c24:3db5` | `exe:0x00e4ef` | `ovl:0x00b910` (section 4) | Format and emit eligible child list |

The count helper `ovl:0xbf3d` first calls the native indexed child-head
accessor `0157:0825(object,slot)`, follows the `0157:09e4` sibling chain,
and increments a 16-bit count for each child for which the native
`0157:03a6` property-8 test returns zero. It returns that count in AX.
If zero, the resident presenter exits without printing the child list.
On success it formats the containing object with mode 2 through the linked
object-phrase service, emits a selector-dependent phrase through the RTL
formatter, and calls `ovl:0xb910` with the original `(object,slot)` words.
That routine traverses the same chain, skips property-8 objects, emits
eligible child phrases with formatting flags `0x83` and list separators,
and returns the number of displayed children in AX. Finally the resident
presenter prints a shared suffix. Its final AX is not a documented count:
in this replay the epilogue observed AX=5 after the final RTL output call.

The native RTLink event trace observed each of the inner helpers **three
times**, all with object `0x0124` and slot 0, with AX=1 at every return.
Every call paired with a return and had verified object-state snapshots.
The count helper made **no object-record changes**. The listing helper
changed object `0x0123` state byte 4 from `0x00` to `0x40` during the
initial OPEN action, and neither later examination changed records.
This is the existing `gw_format_object_phrase` property-7 mark-on-presentation
effect described above; it is *not* safe to replace the listing helper with
a pure output loop. The resident routine itself contains no direct
object-record writes, but its child formatter is stateful. Stream output,
separator and first-presentation ordering must be preserved in the port.

The native evidence establishes this exercised branch and its ABI; other
child slots, zero-count inputs, malformed sibling chains and exact text
formatting still need the §3.7 differential and §3.6 golden-replay tests.
