# Beach VR: original-game native reward, pin pickup and Casino access

The complete Beach VR reward **and subsequent membership-gated Casino exit**
were exercised in the **original, unmodified Gateway executable under
Unicorn**, without HLE story services or a research memory poke. This extends
the static chain in [membership-pin.md](membership-pin.md) with native evidence
for the previously untested success and inventory pickup paths.

## How to enter Beach without skipping the plot logic

From the normal beginning, after the existing startup input at 10–17 seconds:

1. **EAST** from Your Quarters (`0x0ee`) to Heecheetown (`0x0f1`).
2. **NORTHWEST** to Corporation Office Lobby (`0x0ec`).
3. **UP** to VR Terminal (`0x29b`); `IN` instead goes to the board room.
4. **LIE ON COUCH** starts a multi-page briefing with the technician, not
   yet the actual reclining action. Advance the pages with SPACE.
5. **LIE ON COUCH** *again*, **WEAR COLLAR**, **PUSH BUTTON**.
6. Advance the Beach introduction. The native player's room changes to
   Beach `0x2ae`; the game equips a swim suit and exposes the beach bar,
   bartender, frosted glass, and scanner.

The original game's screen and native object-state snapshot confirm this
route. The earlier scripted attempts failed because commands sent during
the technician's modal briefing were consumed as *more-text* input. The
new `puzzle` scenario supports one-shot `@space`, `@enter`, and `@escape`
steps, up to 40 commands spaced three seconds apart (bounded to 150,000
guest milliseconds). The existing seven-slot `objects` scenario is unchanged.

## Glass and bartender: the native progression

The **Beach glass `0x2b4`** belongs to the beach bar `0x2b2` at the start.
TAKE GLASS invokes its real section-50 handler (`OVL:0x69d63`, loaded at
guest linear `0x4a553`) and moves the glass to player `0xd3`. INVENTORY
explicitly confirms the frosted glass is carried.

Ghidra 11.4.2 decompiled the handlers for Beach room `0x2ae`, scanner
`0x2b3`, glass `0x2b4`, and bartender `0x2bb` as **provisional native
functions**. The entire expanded 74-target headless run completed; several
indirect jumps remain unresolved. The original-instruction and dynamic
results agree on the following conditional path:

| Original OVL offset | Observed or recovered operation | Evidence |
| --- | --- | --- |
| `0x69d63` | Glass event-6 handler dispatches command byte at `ES:e664` | Ghidra; native TAKE call at guest `0x4a553` |
| `0x69f0b–0x69f3c` | A drink/bartender interaction increments word `0x78f6` | Ghidra; first GIVE produces counter 1 in native snapshot |
| `0x6a0fc–0x6a11d` | With glass directed at scanner and counter **at least 5**, play `f000:bc51` and set success byte `[word DS:9644]:78fa` to 1 | Original disassembly; native byte-write at `109250` ms |
| `0x6d579–0x6d5ae` | After the VR collapse, check technician presence, success byte, and direct pin ownership | Original disassembly; native entry at `109251` ms |
| **`0x6d5c0–0x6d5d2`** | **Move pin `0x2a9` to VR Terminal `0x29b`** | Native object write at `111158` ms; final snapshot confirms new location |

The five GIVE/TAKE rounds and subsequent **PUT GLASS ON SCANNER** can be
submitted as typed commands. In the successful replay, the original engine
sets the drink counter to **5**, displays the overflowing-glasses simulation
collapse and returns the player to the terminal. The technician's reward
text uses token `f000:70b9`, followed by the pin relocation. This is now
**native execution evidence**, whereas the earlier artificial Casino pin
poke only validated the destination gate.

## Awarded pin to Pedroza Lounge: the full original-game chain

After the technician gives the pin, the player is **still reclining on the
couch** (`0x2a1`) and **wearing the VR collar**. Attempts to descend directly
produce the original message “You can't move while you're wearing the
collar.” The correct additional inputs are **REMOVE COLLAR**, **STAND UP**,
**TAKE MEMBERSHIP PIN**, **DOWN**, **SOUTHEAST**, then **EAST** three times.
The first two EASTs reach Blue Hell Bar and Casino; the third triggers the
room-exit gate.

The complete 39-step replay ran without any injected memory writes. The
native snapshot shows player `0x0f6`, pin directly related to player
`0x0d3`, success byte `1`, and drink counter `5`. In the full run (without
the optional LOOK/INVENTORY demonstrations used in the shorter replay), the
following ordered evidence was captured:

| Guest time | Native operation |
| ---: | --- |
| `103249` ms | Glass/scanner handler writes Beach-success byte `0 → 1` |
| `105158` ms | Section-51 return places membership pin `0x2a9` in terminal `0x29b` |
| `114320` ms | STAND UP moves player from couch `0x2a1` to room `0x29b` |
| `117678` ms | TAKE MEMBERSHIP PIN moves pin `0x29b → 0x0d3` using original location setter |
| `129609` ms | Native player movement reaches Casino `0x0f5` |
| `132176` ms | EAST invokes callback `0x14`, room-exit event `0x0f` |
| **`132268` ms** | **Callback returns `AX=0x00f6`, then original setter moves player `0x0f5 → 0x0f6`** |

The original Pedroza Lounge screenshot corroborates the native state: a
bouncer notices the gold membership pin and admits the player. This is the
**normal positive branch** of the callback whose first controlled test is
documented in [room-callbacks.md](room-callbacks.md).

## Native trace proof, not a reconstructed C++ model

The replay ending just after the pin award produced these exact native
values (object IDs are hexadecimal):

| Live data | Result |
| --- | --- |
| Player location | `0x29b` — VR Terminal |
| Technician location | `0x2a0` — chair |
| Membership pin location | **`0x29b`** — room floor |
| Beach-success byte `0x78fa` | **1** |
| Drink-refresh count `0x78f6` | **5** |
| Pin held by player `0xd3` | No; TAKE is still needed |

The new `--trace-byte-write 62d5a` is an observer-only Unicorn memory
write hook. In the fingerprinted default-layout replay, original DS is
`0x5b46` and `DS:0x9644` points to DS, giving physical address
`0x62d5a`. It sees the crucial byte change at **109250 ms**:

```text
byte-write ms=109250 address=62d5a pc=4a908 cs=4990 ip=1008 size=1 before=0 value=1
```

At 109251 ms the separate section-51 return routine runs at `0x4c194`.
The object-write hook records pin relation word **`a302 → 9b02`** at
111158 ms, and the final live object directory has the same value.

**Important watch caveat:** The same *physical* address also receives a
write at 3130 ms while DOS startup still has a different memory layout.
That early hit is unrelated to the Beach variable. The inspector reports
all hits but separates those preceding the first scripted gameplay command
(18,000 ms). A raw physical watch is not a symbol-aware watch.

## Reproduce and inspect

Use a Unicorn-enabled `gateway-probe` build and external original DOS data.
The following is the successful 32-step native reward replay; it needs no
Ghidra installation to run:

```sh
python3 tools/trace_game_logic.py \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" \
  --data ../gateway-dos --native-only --scenario puzzle \
  --trace-byte-write 62d5a --trace-object-writes 2a9 \
  --extra-trace-entry 4a553 --extra-trace-entry 4c194 \
  --output build/beach-pin-native \
  --command-sequence 'east,northwest,up,lie on couch,@space,@space,@space,@space,@space,@space,@space,lie on couch,wear collar,push button,@space,@space,@space,look,take glass,inventory,give glass to bartender,take glass,give glass to bartender,take glass,give glass to bartender,take glass,give glass to bartender,take glass,give glass to bartender,take glass,put glass on scanner,@space'

python3 tools/inspect_beach_pin.py build/beach-pin-native/native
```

The inspector follows original DS segment pointers to read the Beach flag
and counter, and uses native object records for player, technician, glass,
and pin locations. When a bounded replay ends during a far RTLink service,
the final CPU DS may not be Gateway DS. In that case it can recover the
unique Gateway data segment from recorded original action-dispatch entries
(`1476:0046`) rather than probing memory for a plausible address.

To verify the **entire** native reward, pickup and Casino traversal, run the
full 39-step sequence. This variant has no research memory interventions;
the inspector's `--expect-beach-casino` option validates both final object
state **and the ordered original write/callback trace**:

```sh
python3 tools/trace_game_logic.py \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" \
  --data ../gateway-dos --native-only --scenario puzzle \
  --verify-overlays --trace-navigation \
  --trace-byte-write 62d5a --trace-object-writes 2a9 \
  --extra-trace-entry 4a553 --extra-trace-entry 4c194 \
  --output build/beach-casino-native \
  --command-sequence 'east,northwest,up,lie on couch,@space,@space,@space,@space,@space,@space,@space,lie on couch,wear collar,push button,@space,@space,@space,take glass,give glass to bartender,take glass,give glass to bartender,take glass,give glass to bartender,take glass,give glass to bartender,take glass,give glass to bartender,take glass,put glass on scanner,@space,@space,remove collar,stand up,take membership pin,down,southeast,east,east,east'

python3 tools/inspect_beach_pin.py \
  build/beach-casino-native/native --expect-beach-casino
```

The verification requires the byte-write watch, original pin's two relation
word transitions, the Casino `event 0x0f → AX=0x0f6` callback, and the native
player move to Pedroza Lounge in that order. A controlled `research-poke16`
event invalidates it. Unresolved Ghidra indirect jumps and other VR scenarios
remain separate open research; the exact Beach-to-Casino route above is proven.
