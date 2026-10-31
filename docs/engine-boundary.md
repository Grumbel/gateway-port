# First native engine/story boundary

Investigation: 2026-10-31, Linux x86-64, Unicorn 2.1.4, original Gateway files
identified in [executable.md](executable.md). This extends [game-logic.md](game-logic.md).
No DOSBox comparison was performed in this investigation.

## What is now demonstrated

The original parser and action dispatcher can run a typed `look` command, enter
a room-specific native handler, and call a C++ replacement for an engine object
flag query. Opening the drawer and looking again works across that same boundary.
This is an executable proof of one shared service boundary, not extraction of an
independent story-only executable or a complete handler ABI.

The separation is functional rather than simply one file per responsibility:
resident code contains action dispatch, object access, text lookup/formatting,
and shared action handlers. Overlays contain both room-specific behavior and
shared services. Story routines directly access shared memory as well as calling
services, so a hybrid runtime still needs the guest state layout and RTLink.

## Traced look path

Addresses in this section are **runtime addresses in the observed default
launcher replay**, unlike the file offsets in game-logic.md. They must not be
assumed for another binary or launch layout.

1. Timed BIOS keys type `look` and Enter after clearing the opening MORE prompt.
   The normal main-program path reaches `1000:04d5`, a far call to `1476:0046`.
2. `1476:0046` receives action `0x5f`. It bounds the action ID to 1–195 and uses
   a six-byte table entry, with the far handler pointer at offset `0x28 + 6*ID`
   in the segment stored at `DS:8da8`.
3. In the captured state, `DS=5b46`, `[DS:8da8]=3bc5`. Action `0x5f` has the
   record `0b90,148e,0008` at `3bc5:0262`: shared look handler `148e:0b90`,
   plus a word whose full semantics remain unmapped.
4. That handler calls the shared room-description path at `148e:017f`. The
   path calls the object/event dispatcher at `1476:00f8` with object `0xee`
   (the current room) and event `8`.
5. The object/event dispatcher obtains a handler index, selects a far function
   pointer, and calls it with the event word. Its handler-pointer arrays are
   reached through `[DS:8db4]` / `[DS:8db6]`; it splits `handler_index-1` into
   a 512-entry bank and entry index. These arrays are a useful extraction lead.
6. RTLink loads `GATEMAIN.OVL` offset `0x51480`, length `0x2960`, at linear
   `0x49d80`. The room handler at file offset **`0x514b5`** therefore runs at
   `49d8:0035`. It switches on event IDs 1–24. Event `8` reaches the description
   code that calls the formatter with `0xf0000c02` (segment 3/string 2).
7. The text formatter at `27cd:105b` calls text lookup at `1254:0616`. That
   lookup explicitly checks for a far-pointer segment of `f000`, shifts the
   offset right by 10, and masks the index with `03ff`. This now confirms the
   token split directly in the lookup routine, beyond matching decoded strings.

At overlay file offset `0x51642`, the room routine tests property `0x0c` on
object `0x124`. The flag returns 0 before `open drawer` and 1 afterward. When
clear, the routine prints token `0xf0000c03`, the drawer-closed sentence. With
it set, the second look omits that sentence and the shared object listing prints
the book inside the open drawer. These IDs are internal engine IDs; they must
not be treated as direct indexes into OBJECT.DAT without recovering that mapping.

The same room module also has small native predicates at file offsets `0x5148a`
and `0x5149b` that query that flag and its inverse.

## First host service: object flag query

The on-disk call in the room routine is `0157:03a6`. RTLink relocation produces
runtime **`1157:03a6`**, linear **`0x11916`**. This is a shared Boolean property
query, not a story-specific predicate. Its recovered implementation is in
[src/object_flags.cpp](../src/object_flags.cpp).

Far-call entry contract:

| Location | Meaning |
| --- | --- |
| `SS:SP + 0` | Return IP |
| `SS:SP + 2` | Return CS |
| `SS:SP + 4` | Object ID, word |
| `SS:SP + 6` | Property ID, word |
| `DS` | Original main data segment |
| Return `AX` | 0 or 1 |
| Cleanup | Callee performs `retf`; caller removes the four argument bytes |

The service reads the live guest structures:

| Guest location | Meaning |
| --- | --- |
| Segment at `[DS:8d54]`, offset `38dc` | Maximum valid object ID (734 in the capture) |
| Segment at `[DS:8d50]`, offset `00ba + 6*object` | Object type byte (supported values 1–8) |
| Same record, +2 and +4 | Far pointer to object state |
| Segment at `[DS:8d56]`, offset `0014 + 4*property` | Bit position byte |
| Same property record, +2 | Object-type applicability mask, word |
| Object state +4 + bit/8 | Flag byte; selected bit is `bit & 7` |

The original helper accepts positive signed object IDs within the object count,
rejects property 0 and unsupported types, and returns false when the property's
type mask does not apply. Far-offset additions wrap at 16 bits without carrying
into the segment. Synthetic tests cover those conditions and live flag changes.

`--object-flags verify` computes the host result at entry, lets the native routine
run, and compares AX at its common epilogue. `--object-flags hle` skips the native
routine and returns the host Boolean through the existing far-call frame.
It preserves entry registers other than AX and the return CS/IP/SP. It relies on
the C calling contract: scratch registers and arithmetic flags are not reproduced
instruction by instruction. Guest memory is read but not changed by the HLE.

Both modes check a 64-bit FNV-1a fingerprint of all 323 bytes at `0x1180d`–`0x1194f`
(the flag-base helper, flag-address helper, and Boolean query), and require
`CS=1157`. The expected fingerprint is `2865a272e07a21e5`. The same bytes occur at
file offset `0x27db` in GATEMAIN.EXE. This is a layout/version guard, not a
cryptographic authenticity check. Unrelated intro code can occupy this address
before main loads and is ignored; a signature change after activation fails.
The feature is opt-in and only validated for this Gateway binary/layout. A run
that never matches reports zero intercepted calls.

## Reproduction and results

From gateway-port after building:

```sh
python3 tools/trace_game_logic.py --scenario look --output build/logic-look-check
python3 tools/trace_game_logic.py --scenario drawer --output build/logic-drawer-check
```

The tool runs native, verification, and HLE modes sequentially, with separate
writable state directories. It clears pagination, types commands through BIOS
input, and writes DOS logs, research traces, memory/register dumps and PPMs.
`drawer` runs `look`, `open drawer`, then `look`; the last capture shows the open
drawer and the book. The automated comparison checks final framebuffers and the
verification/HLE sequences of object IDs, properties, results, and return sites.

| Replay | Duration | Compared/replaced flag calls | Final PPM SHA-256, all three modes |
| --- | ---: | ---: | --- |
| `look` | 18000 virtual ms | 316 | `1ca3a90f031440e19ef42babb450eb1fbe9682409a914ad7fd76a1456f4fb69b` |
| `drawer` | 24000 virtual ms | 620 | `120ac0dd81fb84350e814e4a76006c0e629c1901812bd01d96cb92aa19b62943` |

All native comparisons agreed. Framebuffer equality establishes the observed
presentation at the end of these bounded paths, not equality of all guest state,
complete gameplay, save compatibility, or emulator-equivalent register effects.
All 14 CTest targets pass, including synthetic object layout tests, timed-input
and entry tracing, and existing SDL runtime tests. Audio output was not listened
to during these headless research replays.

Research options available on the probe and SDL runtime:

```sh
--trace-research FILE       # DOS read filename/file offset/length/destination
--trace-entry HEX           # Repeatable linear-address entry snapshots
--key-at MS:BIOSHEX         # Repeatable virtual-time BIOS key injection
--object-flags verify|hle   # Experimental version-guarded service
```

Entry snapshots contain 32 stack words and up to eight guessed BP-chain frames.
Frames are research hints, not a validated unwinder: near calls, overlays, and
nonstandard frames need disassembly confirmation. Raw addresses may refer to
unrelated intro code before main replaces it. DOS read mapping records bytes
before RTLink applies relocations; it does not yet decode the RTLink directory.
Tracing is opt-in and can produce large files. Key times use the existing
5000-instruction scheduling quantum, not DOSBox-calibrated elapsed time.

## Next boundary

Recover the object-to-handler index mapping and the handler-pointer arrays as
explicit data structures, then extend the contract to flag setters and object
location queries. Exercise movement and save/restore across the host services.
Keep the original parser, RTLink loader, and guest memory until their dependencies
are mapped. The successful flag query is evidence for incremental native-game
execution with replaced services; it does not yet justify dropping the original
resident engine.

## Follow-up: writes, locations, and handler inventory

The [object-state investigation](object-state.md) now adds flag set/clear and
direct location read/write HLE, exports all 734 object records and 696 native
handler entry points, and resolves all 785 name tokens. Taking/dropping the book
and closing the drawer pass 1434 service comparisons with identical final object
records and images. Use `--object-state verify|hle` for the extended experiment;
`--object-flags` retains the query-only mode documented above.
