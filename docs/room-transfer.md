# Resident room-transfer transaction and retained 16-bit far-call frame

Investigation: 2026-11-02, original fingerprinted `GATEMAIN.EXE` and
`GATEMAIN.OVL`, unmodified Unicorn/SDL3 native replays, and actual
Ghidra 11.4.2/JDK21 headless import. This is **native machine code**, not
script bytecode. EXE offsets are *original file offsets*, not CS-relative
addresses. Section-51 OVL offsets remain in the separate overlay space.

## Established interface

The room-transition routine begins at **original EXE `0x5d5d`**, immediately
after a different function's `RETF` at `0x5d5c`. Under the verified default
replay layout its live entry is **`048e:0284`** (linear `0x14b64`). It is
the destination service called directly by the section-51 VR-return helper,
and also by ordinary directional navigation through the room-exit dispatcher.

Its caller performs a conventional 16-bit **far CALL with one word
argument**, not a coroutine transfer. The native entry stack contains:

| Address at entry | Meaning | Verified Study-refusal return |
| --- | --- | --- |
| `[SS:SP+0]` | Far return IP | `0x2a60` (section-51 overlay) |
| `[SS:SP+2]` | Far return CS | `0x4990` |
| `[SS:SP+4]` | Destination room ID | `0x029b` (VR Terminal) |

After `PUSH BP; MOV BP,SP; SUB SP,8`, the destination is `[BP+6]`.
Its local frame stays present throughout nested room events, text, scene
work and native object relocation. The common epilogue at EXE `0x60cb`
executes `MOV SP,BP; POP BP; RETF`; the successful traced path returns
`AX=1`. The invalid destination and event-`0x0f` veto paths return `AX=0`.
The precise ABI of every nested service is not yet established.

## Decompiled transaction skeleton

This is a **bounded control-flow transcription** of the original instructions,
not an engine-ready room-event API. In particular, the engine's relation
tests, special guards, UI waits and native callbacks remain unported:

```cpp
// Original GATEMAIN.EXE 0x5d5d–0x60ce; illustrative control flow only.
int16_t transfer_room(uint16_t destination) {
    if (destination == 0 || destination > native_object_count()) return 0;
    auto player_parent = direct_parent(0x00d3);
    if (!directly_in_current_room(0x00d3)) {
        // Special containment/collar and description guards (0x5da8–0x5e18).
    }
    pending_destination_8c4a = destination;
    if (room_event(current_room_35c6, 0x0f) != 0) return 0;
    // Additional conditional relationship/flag effects (0x5e45–0x5ee9).
    previous_room_8c4c = current_room_35c6;
    current_room_35c6 = destination;
    move_object(current_actor_35c8, destination, /*slot=*/0);
    native_pre_enter_service();
    room_event(destination, 0x0d);
    // Select display-text mode from :35ca and destination flag 0x02.
    if (marker_3942 == 0) show_room_text(destination, text_mode);
    else if (marker_3944 != 0) native_alternate_entry_service();
    if (marker_3943 == 0) native_room_presentation(destination);
    room_event(destination, 0x0e);
    // Room property/visibility and optional :3940 relation work.
    marker_3942 = 0;
    marker_3943 = 0;
    return 1;
}
```

| Original EXE | Live linear PC | Verified instruction role |
| ---: | ---: | --- |
| `0x5d5d` | `0x14b64` | True far-call function entry |
| `0x5e1b` | `0x14c22` | Set pending destination `:8c4a` |
| `0x5e26` | `0x14c2d` | Old-room event `0x0f`, may veto |
| `0x5eea` | `0x14cf1` | Previous/current room update begins |
| `0x5f16` | `0x14d1d` | Native actor move `1c24:13d5` |
| `0x5f29` | `0x14d30` | New-room event `0x0d` |
| `0x5f88` | `0x14d8f` | `:3942` guards normal room text |
| `0x5fa4` | `0x14dab` | Taken when normal text is bypassed |
| `0x5fab` | `0x14db2` | Conditional alternate-entry `1c24:12db` call |
| `0x5fb0` | `0x14db7` | Returns from alternate-entry call; tests `:3943` |
| `0x5fc0` | `0x14dc7` | Conditional presentation `05bd:000a` call |
| `0x5fc8` | `0x14dcf` | New-room event `0x0e` |
| `0x60cb` | `0x14ed2` | Shared far-return epilogue |

The relation to the *room-exit scanner* at EXE `0x60cf` is therefore clear:
that scanner selects destination candidates; `0x5d5d` commits an accepted
room transition and performs room-entry work. The scanner and transaction
are **distinct native functions**.

## Native replay: room moves and delayed returns

In a complete *unmodified* replay using
`east,northwest,up,…,look,look,look,look,scream`, the resident-entry stack
and each listed stage were captured with `--extra-trace-entry` hooks:

| Path | Entry / current-room commit | Player location write | `RETF` | Following original caller |
| --- | --- | --- | --- | --- |
| `EAST` from Quarters `0x0ee` to `0x0f1` | 18,176 / 18,266 ms | `0x0ee → 0x0f1` at 18,266 ms | 21,002 ms | Ordinary direction handler |
| VR Study `0x2bc` to Terminal `0x29b` | 81,201 / 81,201 ms | `0x2bc → 0x29b` at 81,201 ms | **84,196 ms** | Section 51 `0x6d73b` at **84,196 ms**, couch move and own `RETF` by 84,197 ms |

The VR move follows the separate section-51 unseating write
`0x2be → 0x2bc` in the *same* millisecond. At the resident `0x5f16`
callsite the live stack contains destination `0x29b`, current actor
`0x0d3`, and slot `0`, matching the original three-word `1c24:13d5`
call. The tracked native type-7 player object's location word changes
from `bc02` to `9b02` through the verified section-4 location setter.
The post-`RETF` section-51 frame is the **same frame** that was present
before the resident call, not a newly scheduled copy.

### Idle controls correct the earlier input-dependent interpretation

The earlier pair of Study runs (ending with SLEEP or with SLEEP followed
by LOOK) established a pending frame but did **not** establish that a new
command resumes it. We added `--extra-idle-ms` to run the unmodified native
guest for up to 60 more seconds after the final scripted key, without
inserting anything in the BIOS keyboard queue.

- **Ordinary EAST, no next command:** Running `east` then **10,000 ms** of
  additional idle time produced no `0x14ed2` epilogue. At the final snapshot
  the original `SS=0x5b46, BP=0xdb9a` resident frame still contains
  `dbb6 065a 148e` (saved BP, return IP, return CS). The earlier ordinary
  replay with a scripted `NORTHWEST` key at 21,000 ms did return at
  21,002 ms. Normal room text can **wait for new input**.
- **Study refusal, no SCREAM/LOOK after the refusal:** Running the same
  `prefix,look,look,look` sequence with **10,000 ms** idle also reached
  resident `RETF` at **84,196 ms** and couch continuation at **84,196–84,197
  ms**, *exactly the times* in the replay that added SCREAM at 84,000 ms.
  Thus the VR-return path **does not require a new command** to complete.
  The `:3942=1` marker bypasses usual room text at `0x5f88`, and native
  room-entry/presentation work completes later. Neither a new script
  scheduler nor a CPU coroutine is needed to explain the far-return stack.

Focused native instruction hooks further divide the input-free 81,203–84,194
ms interval. The `:3944` guard takes the alternate-entry path at EXE
`0x5fab`, and **`1c24:12db` remains active from 81,203 to 82,285 ms**
(1,082 guest ms). Next `:3943 == 0`, so **`05bd:000a` executes from
82,285 to 84,194 ms** (1,909 guest ms) before the event-`0x0e` call.
Both waits occur *inside* the original resident far-call chain without
scheduled later input. The semantics of those two services (including any
host-side timing or presentation behavior) still require decompilation;
the entry/return PCs and elapsed guest time are directly observed.

These two controls distinguish time passage, room text, and subsequent
input. Do not generalize either path's blocking behavior to all rooms.

### Reproduce the native hooks

Requires the original external DOS files and native `gateway-probe`.

```sh
prefix='east,northwest,up,lie on couch,@space,@space,@space,@space,@space,@space,@space,lie on couch,wear collar,set switch to study,push button,@space,@space,@space,look'
resident=(--extra-trace-entry 14b64 --extra-trace-entry 14c22
  --extra-trace-entry 14c2d --extra-trace-entry 14cf1
  --extra-trace-entry 14d1d --extra-trace-entry 14d30
  --extra-trace-entry 14d8f --extra-trace-entry 14dab
  --extra-trace-entry 14ec0 --extra-trace-entry 14ed2
  --extra-trace-entry 4c357 --extra-trace-entry 4c36b
  --extra-trace-entry 4c377)

python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --verify-overlays --trace-navigation --trace-object-writes d3 \
  "${resident[@]}" --command-sequence "$prefix,look,look,look,scream" \
  --output build/room-transfer-refusal
python3 tools/inspect_room_transfer.py build/room-transfer-refusal/native.trace

# Same native game, no follow-up command; do not change the guest RAM.
python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --verify-overlays --trace-navigation --trace-object-writes d3 \
  --extra-idle-ms 10000 "${resident[@]}" \
  --command-sequence "$prefix,look,look,look" \
  --output build/room-transfer-idle
python3 tools/inspect_room_transfer.py build/room-transfer-idle/native.trace

# Contrast an ordinary room entry: let EAST finish rendering without a
# subsequent NORTHWEST key. The resident far-return frame stays in RAM.
python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --verify-overlays --trace-navigation --trace-object-writes d3 \
  --extra-idle-ms 10000 --extra-trace-entry 14b64 \
  --extra-trace-entry 14ed2 --command-sequence 'east' \
  --output build/room-transfer-ordinary-idle

# Additional interior hooks isolate the no-input presentation latency.
presentation=(--extra-trace-entry 14b64 --extra-trace-entry 14dab
  --extra-trace-entry 14db2 --extra-trace-entry 14db7
  --extra-trace-entry 14dc7 --extra-trace-entry 14dcf
  --extra-trace-entry 14ed2 --extra-trace-entry 4c36b)
python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --verify-overlays --extra-idle-ms 5000 "${presentation[@]}" \
  --command-sequence "$prefix,look,look,look" \
  --output build/room-transfer-presentation
python3 tools/inspect_room_transfer.py build/room-transfer-refusal/native.trace \
  --idle-presentation-trace build/room-transfer-presentation/native.trace \
  --ordinary-idle-prefix build/room-transfer-ordinary-idle/native
```

`tools/inspect_room_transfer.py` verifies the native 16-bit caller stack,
ordered original branch PCs, actual player write, retained parent frame,
AX result, and immediate section-51 continuation. Its asset-independent
mutation tests reject wrong destinations, missing/reordered instructions,
wrong guest writes, changed far-return frames and unexpected completions.
The optional idle controls also verify the two presentation wait intervals
and the ordinary still-live far-return frame **directly from native RAM**.
The checker deliberately accepts only the two fingerprinted replay callsites.

## Ghidra caveat and engine boundary

The importer now identifies the resident entry by a **unique 24-byte
original-MZ signature** and adds eleven **instruction-only** labels to the
transaction. The fresh Ghidra 11.4.2/JDK21 analysis imported all **58
isolated RTLink overlays and 13,947 relocations** and exported **78/78
selected contexts**. Ghidra's original MZ address for the entry is
`1000:5d3d`; the runtime `048e:0284` map is established separately by
the native code bytes and entry stack.

Ghidra may split the `0x5f88–0x5fc8` shared entry/presentation path and
`0x60cb` epilogue into **separate inferred functions**.
`DecompileGateway.py` explicitly flags these **six** interior contexts
with `ghidra-split-tail` in the JSON report and a
warning beside the pseudocode. There is no native CALL to either
inferred entry: original short/near branches jump into those blocks.
The label role remains `instruction`; the presence of a Ghidra function
boundary is *not* evidence of a second game-level ABI.

For a native C++ engine, this is now a bounded ordered **transaction
contract**. The room-event callback implementations, pre-entry special
guards, text system and asynchronous-looking UI waits must be reproduced
or independently modeled before replacing this service. The separate
[VR-return branch selector](vr-return.md) should not itself mutate room
state or decide when native presentation is complete.
