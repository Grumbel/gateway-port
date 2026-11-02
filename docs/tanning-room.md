# Native Tanning Room UP callback: posture is not navigation

The original `Gateway` executable has a concrete case where a **kind-4
room-exit callback returns `AX=0` yet changes the player object location**.
It is the Tanning Room `UP` exit, callback `0x2b`, and both conditional
branches were exercised with normal typed input under Unicorn. No research
memory poke, story HLE or reconstructed script was involved.

## Rooms and exits

The native room table and original `OBJECT.DAT` names agree:

| Room | Input / direction byte | Exit type | Destination or callback |
| --- | --- | --- | --- |
| Pedroza Lounge `0x0f6` | `NORTH` / `0x01` | Kind 1 | Tanning Room `0x29a` |
| Tanning Room `0x29a` | `OUT` / `0x0c` | Kind 1 | Pedroza Lounge `0x0f6` |
| Tanning Room `0x29a` | **`UP` / `0x09`** | **Kind 4** | Callback **`0x2b`**, not an ordinary doorway |

The room contains the native couch object `0x29c` and couches collection
`0x29d`. `LIE ON COUCH` moves the player object `0x0d3` **into `0x29c`**.
This nested location is different from the enclosing type-1 room `0x29a`.

## Original handler and bounded native logic

The verified kind-4 callback trampoline `2c24:13f3` enters RTLink section
**50**, original **`GATEMAIN.OVL:0x693fc`** (guest `4990:02ec`, linear
`0x49bec` in this replay). The Ghidra 11.4.2 C-like output is preliminary;
direct original 16-bit instructions establish the critical branch:

| Original file offset | Original native behavior |
| --- | --- |
| `0x693ff–0x69407` | Only room-exit event `0x0f` proceeds; other events return `0` |
| `0x6940a–0x69417` | Select room-state branch when current room is `0x29a` |
| `0x694b8–0x694cc` | Direct-relation test `0157:000c(0x0d3,0x29c)`; if false, return `0` |
| `0x694cf–0x69512` | If reclining, display text `f000:bc0e` (getting up), call native `1c24:13d5` to move player back into current room, return `0` |

The same `0x2b` callback also handles VR Terminal `0x29b`, Beach `0x2ae`,
and another virtual location `0x2bc`, with different tests and text. Those
branches are not equivalent. The subsequent [VR Terminal and Beach `UP`
analysis](vr-up-callback.md) verifies the collar guard and Beach nested
movement. [Study VR](study-vr.md) subsequently covers the fourth
current-room branch (`0x2bc`), including its signed counter window.

A narrow C++ model method, `StoryModel::apply_tanning_up`, reproduces only
the Tanning branch's observed direct-location test and mutation. Its outcome
contains both the native **zero** return value and any location change;
it makes no claim to implement parser selection, text rendering, or other
rooms. This matters because **`AX=0` does not imply the callback was inert**.

## Two unmodified native replays

Both runs reproduce bundle 086's normal Beach reward, membership-pin pickup
and Casino entry before entering the Tanning Room. They diverge only in the
final actions:

| Precondition / final input | Original trace | Native result |
| --- | --- | --- |
| Already standing, `NORTH,UP` | Callback `0x2b`, event `0x0f`, `AX=0` at **138116 ms** | No player move; stays at `0x29a` |
| Recline first, `NORTH,LIE ON COUCH,UP` | LIE writes `0x029a → 0x029c` at **138428 ms**; callback `0x2b`, event `0x0f` at 141116 ms | Original location setter writes **`0x029c → 0x029a`** at **141139 ms**, callback still returns `AX=0` |

The reclining run watched the player object record, including both word
writes at native record offset `+0x0c`. Its final state again puts the player
in Tanning Room; the membership pin remains in player inventory, the Beach
success flag is `1`, and the bartender counter is `5`. The standing run
watched the pin instead, but recorded the original player-location services.
The paired inspector verifies the callback RTLink target, event/AX, both
reclining location transitions, the absent standing transition, final native
state and absence of `research-poke16` operations.

## Reproduce

Start with a Unicorn-enabled `gateway-probe` and the external original DOS
files. The shared 39-command prefix is the independently validated Beach →
Pedroza Lounge sequence:

```sh
replay_prefix='east,northwest,up,lie on couch,@space,@space,@space,@space,@space,@space,@space,lie on couch,wear collar,push button,@space,@space,@space,take glass,give glass to bartender,take glass,give glass to bartender,take glass,give glass to bartender,take glass,give glass to bartender,take glass,give glass to bartender,take glass,put glass on scanner,@space,@space,remove collar,stand up,take membership pin,down,southeast,east,east,east'

python3 tools/trace_game_logic.py \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" --data ../gateway-dos \
  --native-only --scenario puzzle --verify-overlays --trace-navigation \
  --trace-byte-write 62d5a --trace-object-writes 2a9 \
  --extra-trace-entry 49bec --output build/tanning-standing \
  --command-sequence "$replay_prefix,north,up,look"

python3 tools/trace_game_logic.py \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" --data ../gateway-dos \
  --native-only --scenario puzzle --verify-overlays --trace-navigation \
  --trace-byte-write 62d5a --trace-object-writes d3 \
  --extra-trace-entry 49bec --output build/tanning-reclining \
  --command-sequence "$replay_prefix,north,lie on couch,up,look"

python3 tools/inspect_tanning_up.py \
  build/tanning-standing/native build/tanning-reclining/native
```

For readable input/action associations and callback results, additionally use
`tools/summarize_navigation_trace.py --puzzle-commands "$replay_prefix,north,up,look"
--callback-returns --direction-calls build/tanning-standing/native.trace`.
The reporter accepts up to 48 native puzzle commands, and explicitly marks
source rooms as *unwatched* when no player-record write hook was enabled.

## Sixth headless Ghidra run

A new Ghidra 11.4.2 / JDK 21 project with the unchanged original EXE/OVL
validated **58 RTLink sections and 13,947 relocations** and exported
**81/81 pseudocode contexts**. It retains all 44 callback entries, the
previously selected VR/Beach handlers and one non-object return routine,
and adds seven new object candidates: Casino `0x0f5`, Pedroza Lounge
`0x0f6`, Tanning Room `0x29a`, Tanning couch `0x29c`, couches `0x29d`,
VR couch `0x2a1`, and collar `0x2a8`.

Most room handlers begin with computed event jump tables that Ghidra has not
fully resolved. The `81/81` count means each requested *context exported*,
not that indirect control flow and far-call parameters are complete C.
The handler for couch `0x29c` and the collar do expose useful location tests
and original story-text tokens, but dynamic observations above are the
authority for actual state changes.
