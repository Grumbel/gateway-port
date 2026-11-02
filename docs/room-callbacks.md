# Native room-exit callback directory

The kind-4 exit argument is an **index into a 44-entry native far-pointer
table**, not a destination room ID or script opcode. The resident function
starts at original `GATEMAIN.EXE:0x592a`, at `1474:0002` in the observed
Unicorn runtime. It rejects indices >= `0x2c` with `AX=0`; otherwise it
passes the second argument to `[word DS:8da6]:4a22 + 4*index` and returns
that far call's `AX`. This is distinct from the room scanner function at
`EXE:0x60cf` and its kind-4 callsite at `EXE:0x6270`.

The resident room scanner passes **event `0x000f`** to the callback gate.
The same table is also called by object/event logic with **event `0x0001`**:
the observed Casino callback `0x14` returns `AX=0x00f6` in that *other*
context. Do not infer a traversable room exit from those non-room calls.

## Complete static directory from one native snapshot

The native snapshot's 236 kind-4 exit records reference **all 44 IDs**.
All 44 far pointers resolve to valid ten-byte RTLink trampolines and all
targets map inside file-backed bytes of original `GATEMAIN.OVL`, across
**22 overlay sections**. The table below lists original, unrelocated OVL
file offsets, not physical emulator addresses or Ghidra space offsets.

| ID | Exit records | OVL section | File offset |
| ---: | ---: | ---: | ---: |
| `0x00` | 14 | 9 | `0x1a831` |
| `0x01` | 5 | 57 | `0x7956e` |
| `0x02` | 2 | 36 | `0x4f9a6` |
| `0x03` | 2 | 34 | `0x48937` |
| `0x04` | 12 | 32 | `0x43415` |
| `0x05` | 2 | 50 | `0x6958d` |
| `0x06` | 1 | 33 | `0x45988` |
| `0x07` | 2 | 30 | `0x40055` |
| `0x08` | 1 | 31 | `0x4144c` |
| `0x09` | 1 | 31 | `0x414ee` |
| `0x0a` | 30 | 13 | `0x1e318` |
| `0x0b` | 14 | 13 | `0x1e38f` |
| `0x0c` | 14 | 13 | `0x1e335` |
| `0x0d` | 1 | 15 | `0x22357` |
| `0x0e` | 1 | 15 | `0x2233c` |
| `0x0f` | 6 | 13 | `0x1e3ee` |
| `0x10` | 6 | 13 | `0x1e40b` |
| `0x11` | 7 | 17 | `0x28886` |
| `0x12` | 9 | 17 | `0x288a3` |
| `0x13` | 2 | 42 | `0x5b408` |
| `0x14` | 2 | 48 | `0x665f9` |
| `0x15` | 2 | 39 | `0x55ddd` |
| `0x16` | 2 | 39 | `0x55e33` |
| `0x17` | 2 | 9 | `0x1a7a8` |
| `0x18` | 22 | 19 | `0x2bba7` |
| `0x19` | 9 | 22 | `0x321f9` |
| `0x1a` | 2 | 19 | `0x2bbdf` |
| `0x1b` | 5 | 19 | `0x2bc37` |
| `0x1c` | 1 | 19 | `0x2bb8a` |
| `0x1d` | 6 | 19 | `0x2bb52` |
| `0x1e` | 5 | 19 | `0x2bc1a` |
| `0x1f` | 1 | 19 | `0x2bc71` |
| `0x20` | 9 | 19 | `0x2bc54` |
| `0x21` | 1 | 19 | `0x2bbfc` |
| `0x22` | 4 | 25 | `0x37644` |
| `0x23` | 9 | 25 | `0x376b2` |
| `0x24` | 2 | 25 | `0x3767a` |
| `0x25` | 1 | 25 | `0x376cf` |
| `0x26` | 1 | 20 | `0x2e009` |
| `0x27` | 10 | 28 | `0x3eaf2` |
| `0x28` | 1 | 55 | `0x73caa` |
| `0x29` | 1 | 55 | `0x73d32` |
| `0x2a` | 2 | 11 | `0x1d8de` |
| `0x2b` | 4 | 50 | `0x693fc` |

Counts are *records*, not distinct destinations, actual visits or proven
reachable exits. Every entry is a **static code-entry candidate**; the
snapshot cannot establish the conditions and AX results of every handler.

## Casino east: one live callback and native return

The exit records for room `0x0f5` **Casino** have kind 4, argument `0x14`,
directions `0x03` and `0x0b` (indexes 265 and 266). The live table pointer
at `2c24:14d9` is a validated RTLink trampoline into **section 48**, target
`4990:1a19` / original **`GATEMAIN.OVL:0x665f9`**.

Original x86 disassembly (`0x665f9–0x6664e`) and the Ghidra decompilation
show the following simplified candidate, with native call arguments made
explicit where Ghidra's inferred 16-bit far-call signatures omitted them:

```cpp
uint16_t casino_callback_14(uint16_t event) {
    if (event != 0x000f)
        return 0x00f6; // Pedroza Lounge object; not necessarily a room move

    if (native_relation_contains(/* object */ 0x02a9, /* related */ 0x00d3)) {
        display_original_text_token(0xf0005cc2);
        return 0x00f6;
    }
    display_original_text_token(0xf0005cc1);
    clear_object_flag(/* heavy oak door */ 0x01ad, /* property */ 0x0c);
    return 0;
}
```

`0x02a9` is the **membership pin**, `0x00d3` the player, and `0x00f6`
**Pedroza Lounge**. `0157:000c` checks a direct relation slot, not a
recursive inventory search. Its flag and text routines are invoked by the
original binary; the prose/text-token meaning and the positive condition
have not been verified by a live replay.

On a fresh native Unicorn replay with command sequence
`east,east,east,east,look,inventory,look`:

- The first three EAST commands move the player from Your Quarters
  `0x0ee` → Heecheetown `0x0f1` → Blue Hell Bar `0x0f4` → Casino `0x0f5`.
- At guest **28176 ms**, the fourth EAST reaches scanner call `148e:0797`:
  `callback_id=0x14`, event `0x0f`; the live indirect call uses
  `2c24:14d9`, section 48.
- At **28247 ms**, the gate and scanner both return **`AX=0`**. No native
  player-location change follows that exit. The original flag-clear service
  executes for object `0x1ad`, property `0x0c`, with `changed=0` (it was
  already clear), so this is *not* evidence of a changed flag value.
- Subsequent object-event calls to the **same** callback index pass event
  `0x01` and return `0x00f6`; they are not room-exit decisions.

This distinguishes a **verified no-move branch** from a plausible but
untested membership-pin success branch. Do not transplant the latter into
the bounded C++ model as verified puzzle behavior yet.

## Two more statically decoded patterns

**Callback `0x00`: return to the shared ship cabin.** Its 14 exits come in
pairs (direction codes `0x09`/`0x0b`) from seven native rooms: Blast Zone,
Satellite, Southwestern Shore, Mountain Plateau, Landing Pad, Jungle
Clearing and Landing Site. Original `OVL:0x1a831–0x1a87a`, section 9,
tests **property `0x0c` on cabin hatch `0x1b7`** through `0157:03a6`:

```cpp
uint16_t ship_cabin_callback_00(uint16_t event) {
    if (native_has_flag(/* cabin hatch */ 0x01b7, 0x0c))
        return 0x01b1;  // shared ship cabin
    if (event == 0x000f)
        display_original_text_token(0xf0001452);
    return 0;
}
```

This explains a likely *mechanism* for the same native ship-cabin room
`0x1b1` appearing on several otherwise unrelated planetary maps: dynamic
return through a shared cabin-hatch gate. The exact directions, hatch-state
changes and success outcome have **not** been verified in a live replay.

**Callback `0x0a`: non-traversable message branch.** Its 30 indexed exits
are concentrated around shore/forest boundaries. Original
`OVL:0x1e318–0x1e334`, section 13, sends token `f000:cc28` when the
event is `0x000f` and then **unconditionally returns `AX=0`**. This
distinction is confirmed from the original instructions, not solely an
optimistic Ghidra interpretation. It is a static candidate for a blocked
boundary path; the displayed prose has not been replayed and identified.

## Reproduce and inspect

Run a native game replay with `--trace-navigation` and a sequence reaching
the desired room, then create an RTLink manifest with the built extractor:

```sh
"${GATEWAY_BUILD_DIR:-build}/gateway-overlay" \
  ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --fixups > build/rtlink-manifest.json
python3 tools/inspect_room_callbacks.py build/casino-replay/native \
  --names ../gateway-dos/OBJECT.DAT \
  --overlay-map build/rtlink-manifest.json \
  --format json > build/room-callbacks.json
python3 tools/run_ghidra.py \
  ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --overlay-tool "${GATEWAY_BUILD_DIR:-build}/gateway-overlay" \
  --callbacks-json build/room-callbacks.json \
  --output build/ghidra-callbacks
python3 tools/summarize_navigation_trace.py build/casino-replay/native.trace \
  --commands 'east,east,east,east,look,inventory,look' --callback-returns
```

`tools/inspect_room_callbacks.py` can also print readable text and accept
repeated `--room 0x...` filters. It rejects invalid guest-memory pointers,
out-of-section overlay targets and invalid RTLink section IDs; out-of-range
callback arguments are explicitly reported, not indexed as pointers. Its
JSON is mapped *again* against the freshly extracted manifest by the Ghidra
preparer before it can become a label. No original game binaries or data are
written by these tools.

Ghidra **11.4.2** imported all 58 overlays and decompiled **60/60 target
contexts** (44 callback entries, 13 other functions, 3 resident internal
branch contexts). Some calls have missing arguments or unresolved external
segments because Ghidra does not fully infer this game's far-call ABI.
The 44 decompilation statuses are *tool completion*, not 44 behavior proofs.
