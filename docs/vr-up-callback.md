# Native VR `UP`: two more branches of callback `0x2b`

Investigation: 2026-11-02, using the unchanged original `GATEMAIN.OVL` and
normal typed Unicorn input. This extends the
[Tanning Room UP analysis](tanning-room.md), not a replacement for the
room exit scanner or engine.

## Result

The original kind-4 room-exit callback `0x2b`, entered through RTLink
trampoline `2c24:13f3`, uses **section 50**, file offset **`0x693fc`**
(live `4990:02ec`, linear `0x49bec`). The exit event is `0x000f`.

| Enclosing room | Condition | State effect | Native return |
| --- | --- | --- | --- |
| Tanning Room `0x29a` | Player directly inside couch `0x29c` | Move player to room `0x29a`; standing player unchanged | `AX=0` |
| VR Terminal `0x29b` | Collar `0x2a8` equipped in player slot 1? Otherwise, player directly in room? | Equipped: refuse, no move. Unequipped and nested: move player to room `0x29b` | `AX=0` |
| Beach `0x2ae` | Player directly in room? | Direct: refuse, no move. Nested: move player to room and call clear of glass `0x2b4` property `0x0a` | `AX=0` |

**`AX=0` is not evidence of an inert callback.** The callback can move the
player without supplying a room ID to the exit scanner. Object events using
the same trampoline with event `0x0001` are not `UP` exits.

## Original 16-bit x86 logic

Disassemble with:

```sh
python3 tools/disassemble_story.py ../gateway-dos/GATEMAIN.OVL \
  --start 0x693fc --size 0x191
```

| Original file offset | Verified instruction effect |
| --- | --- |
| `0x693ff–0x6942f` | Gate event `0x0f` and dispatch current room from `[DS:9632]:35c6` to `0x29a`, `0x29b`, `0x2ae`, or `0x2bc` |
| `0x6954f–0x6956a` | VR Terminal uses `0157:005e(0x2a8,0x0d3,1)` for collar equipment relation; if true, select text token `f000:bc11` and return zero |
| `0x6956c–0x69588` | If no collar, native `0157:000c(player,current room)` distinguishes direct from nested location; only nested reaches common get-up path |
| `0x694cf–0x69512` | Common get-up: display token `f000:bc0e`; call `1c24:13d5(player,current room,0)` and return zero |
| `0x69446–0x6945f` | Beach direct-relation branch selects blocked-direction token `f000:bc0f` and returns zero |
| `0x69462–0x694b5` | Beach nested path displays get-up text, moves player, **then** calls `0157:0413(0x2b4,0x0a)` to clear the glass flag |
| `0x69515–0x6954d` | Separate `0x2bc` branch checks object `0x2be` and word `ES:7904`; still only a static lead |

Text tokens `bc0e`, `bc0f`, and `bc11` correspond respectively to a get-up
response, a blocked-direction response, and a worn-collar refusal. Ghidra
11.4.2 exported the callback among bundle 087's **81/81** selected contexts;
indirect branches and inferred far-call signatures remain preliminary.

## Two original-game replays

Both native runs used `--native-only --verify-overlays --trace-navigation`,
plus `--trace-object-writes d3` to watch the type-7 player location at record
offset `+0x0c`. The verifier correlates the original setter's service event
with the watched native word write; no `research-poke16` was present.

| Input and context | Event `0x0f` callback and original word writes |
| --- | --- |
| VR Terminal, inside couch `0x2a1`, collar worn: `UP` | **60116–60159 ms**, `AX=0`, no player move |
| Terminal, `REMOVE COLLAR, UP` | Collar moved from player to couch at **63497 ms**; **66116–66139 ms** callback writes **player `0x2a1 → 0x29b`**, `AX=0` |
| Beach, player nested in `0x2b1`: `UP` | **69116–69140 ms** callback writes **player `0x2b1 → 0x2ae`**, `AX=0` |
| Beach, player already directly in `0x2ae`: repeat `UP` | **72116–72137 ms**, `AX=0`, no player move |

Terminal couch entry `0x29b → 0x2a1` was watched at 54412 ms; Beach nested
entry `0x2ae → 0x2b1` at 63171 ms. Final native snapshots independently
identify player and enclosing room `0x29b` (Terminal) and `0x2ae` (Beach).
The Beach puzzle has not been completed in these short replays (reward byte
and bartender counter remain zero).

**Evidence boundary:** the original assembly definitely invokes a clear of
glass property `0x0a` after nested Beach `UP`; this run watched the *player*,
not that glass flag. It does not establish that the bit was set or changed.

## Model, tests and reproduction

`StoryModel::apply_vr_terminal_up(player, collar_equipped)` models the guard
as a caller-supplied Boolean, since the meaning of native equipment slot 1
is not inferred from the model's ordinary location field.
`StoryModel::apply_beach_up(player)` models the nested move and glass flag
clear (when that bit exists in supplied state). Both return
`StoryCallbackOutcome{return_ax=0, changes=...}`; they do not translate
parser text, ordinary room movement, or the `0x2bc` branch.

`tools/inspect_vr_up.py` rejects mismatched callback entries, nonzero AX,
research pokes, absent native player writes, reversed collar order, forbidden
movements and unexpected final rooms. Its Python tests are asset-independent.

Use original DOS binaries and a built Unicorn-enabled probe:

```sh
terminal='east,northwest,up,up,lie on couch,@space,@space,@space,@space,@space,@space,@space,lie on couch,wear collar,up,remove collar,up,look'
beach='east,northwest,up,lie on couch,@space,@space,@space,@space,@space,@space,@space,lie on couch,wear collar,push button,@space,@space,@space,up,up,look'

python3 tools/trace_game_logic.py \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" --data ../gateway-dos \
  --native-only --scenario puzzle --verify-overlays --trace-navigation \
  --trace-object-writes d3 --extra-trace-entry 49bec \
  --output build/vr-terminal-up --command-sequence "$terminal"

python3 tools/trace_game_logic.py \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" --data ../gateway-dos \
  --native-only --scenario puzzle --verify-overlays --trace-navigation \
  --trace-object-writes d3 --extra-trace-entry 49bec \
  --output build/vr-beach-up --command-sequence "$beach"

python3 tools/inspect_vr_up.py build/vr-terminal-up/native build/vr-beach-up/native
```

For input/callback association, use `tools/summarize_navigation_trace.py`
with `--puzzle-commands "$beach" --callback-returns --direction-calls` on
`build/vr-beach-up/native.trace`. Input timing alone does not prove a native
action succeeded; the watched records and callback returns provide that proof.
