# Gateway: published map reference

We can now compare the native room/object snapshot with the game's **original
printed maps**. This is an external reference for reverse engineering, not
a recovered native navigation table or a source of executable game logic.

Primary source: Legend Entertainment, *Frederik Pohl's Gateway: The Official
Hint Book*, [scanned PDF](https://www.mocagh.org/legend/gateway1-hintbook.pdf),
printed **pages 6–10** (PDF pages 14–18). It contains **11 map panels** and
claims to cover both initially accessible and puzzle-restricted locations.
The [Walkthrough King solution](https://www.walkthroughking.com/text/gateway.aspx)
is a separate, actionable sequence of typed movement commands; use it for
reproducible runs rather than as an authority on internal IDs.

Do not add scans of the copyrighted maps or full walkthroughs to this
repository. Link to them and record only derived facts and observations.

## Map index versus native type-1 room IDs

The room ranges below come from the **734-object native snapshot** in
`docs/object-state.md`; they are not IDs supplied by the hintbook. Ranges
are useful leads for tracing the code that changes the current room, not
verified per-edge or per-direction translations. Several displayed room
names differ from the internal names or appear more than once.

| Published map | Printed page | Candidate native type-1 IDs | Notes |
| --- | ---: | --- | --- |
| Gateway — Level Babe | 6 | `0x0e7`–`0x0eb` | Hidden room and B-series corridors; repeated generic corridor labels on the map. |
| Gateway — Level Dog | 6 | `0x0ec`–`0x0f6`, `0x29a`–`0x29b` | Quarters, lobby, Heecheetown, casino and park; the native `Board Room` and `Central Park` names differ from the printed labels. |
| Gateway — Level Tanya | 7 | `0x0f7`–`0x0fe`, `0x1b1` | Conference/armory/hangar/dock and ship cabin. |
| Dayan 7 | 7 | `0x001`–`0x00a`, `0x1b1` | Multiple *Plateau* records; printed *Below Entrance* versus native *Below Cave*. |
| Aleph 4 | 8 | `0x26d`–`0x277`, `0x1b1` | Dome/village; printed *Behind Rock*, *Muzer Village* and *Big Hut* have slightly different native labels. |
| Aurigae 6 | 8 | `0x230`–`0x234`, `0x1b1` | Landing Pad, City Center, Temple Court, Octagonal Room. |
| Kaduna 3 | 9 | `0x253`–`0x258`, `0x1b1` | Jungle Clearing through Tower. |
| Dorma 5 | 9 | `0x08d`–`0x09c`, `0x1b1` | Pond/shore/forest/cave system. |
| Nemira 3 | 10 | `0x1ba`–`0x1cb`, `0x1b1` | Mountain Plateau, Garden, Tree House, Pinnacles, Becker's House and other locations. |
| Endgame — Phase 1 | 10 | `0x012`–`0x013`, `0x046`–`0x04b`, `0x1b1` | Satellite and Travel Pod with the ballroom and game booths; not a validated complete assignment. |
| Endgame — Phase 2 | 10 | `0x021`–`0x024` | Hydra/Demon/Mirror rooms; printed *Empty Chamber* does not directly match the type-1 name `Demon Chamber`. |

`0x1b1` **ship cabin** is a single native room object, yet *Ship Cabin*
appears on several otherwise separate planetary maps. This suggests a
context-dependent connection or reused room object; the precise mechanism
must be observed in a native transition before it can be modeled.

## Cross-check of all eight populated type-6 relation pairs

The independently derived type-6 endpoint pairs in
[object-state.md](object-state.md#export-an-initial-typed-world-snapshot-for-a-native-renderer)
also occur on the official printed maps:

| Connector ID and name | Endpoint pair | Published panel | Map notation |
| --- | --- | --- | --- |
| `0x020` door | Travel Pod / Satellite | Endgame Phase 1 (p. 10) | Puzzle-gated connection |
| `0x1b7` cabin hatch | Dock / ship cabin | Gateway Level Tanya (p. 7) | Up/down arrow |
| `0x225` archway | Mountain Plateau / Vantage Point | Nemira 3 (p. 10) | Plain line |
| `0x226` rope ladder | Garden / Tree House | Nemira 3 (p. 10) | Up/down arrow |
| `0x227` rope bridge | Pinnacles / Becker's House | Nemira 3 (p. 10) | Puzzle-gated connection |
| `0x24d` archway | City Center / Temple Court | Aurigae 6 (p. 8) | Plain line |
| `0x24e` spiral staircase | Temple Court / Octagonal Room | Aurigae 6 (p. 8) | Plain line |
| `0x259` door | Plaza / Tower | Kaduna 3 (p. 9) | Puzzle-gated connection |

Legend's map legend uses an **arrow to denote UP versus DOWN**, rather
than to establish one-way access. A key-like mark denotes an obstacle/puzzle
to solve before using a connection. Neither marking independently reveals
the DOS game variable or handler responsible for permitting a move.

This alignment is strong evidence that a populated **type-6 two-room record
encodes a special passage's two endpoints**. It does not show that every
ordinary navigation edge has such a record: most of the published map edges
have no corresponding populated type-6 pair. Nor does it establish that the
room names and locations in the final saved snapshot represent the current
state at every point in the story.

## Next native probes

1. Start with an accessible Gateway Station connection such as Heecheetown ↔
   Your Quarters, **which has no type-6 pair**. Trace the current-room word
   and the code that handles a cardinal movement command. The first-room
   snapshot puts player `0x0d3` inside room `0x0ee` (Your Quarters). For native
   type 7, the player's direct-location word is at record offset `+0x0c`;
   watching it tests whether a compass move also changes that field.
2. Separately observe a named type-6 passage (e.g. the Dock's cabin hatch) to
   see where its two relation words enter the room-change decision. Test both
   directions without assuming the hintbook arrow is a one-way constraint.
3. Compare a puzzle-marked passage before/after its condition is satisfied,
   including whether its type-6 words or a separate flag change.
4. Trace scene-picture selection following these transitions. The type-8
   containment list is **not** itself a reliable PIC-resource lookup.

Do not hard-code the published map as the game's control flow. Keep its edges
distinct from **native-observed** transition rules in any future engine.

### Repeatable first-room movement trace

Run in `nix develop` after `gateway-build`, with the original DOS assets
outside the repository. This reuses the deterministic seven-command replay;
the starting prompts are dismissed by its usual startup key schedule.

```sh
python3 tools/trace_game_logic.py \
  --scenario objects --native-only --verify-overlays \
  --trace-state-writes --trace-object-writes d3 \
  --command-sequence 'east,west,east,west,look,inventory,look' \
  --output build/room-navigation

python3 tools/summarize_navigation_trace.py \
  build/room-navigation/native.trace \
  --world build/parser-child-writes/world.json \
  --commands 'east,west,east,west,look,inventory,look'
```

The report shows candidate player-location changes, the native far-return
address of each location setter, and the code PC of any matching watched
write. Native service `changed=1` calls are verified by the probe; watched
memory-write entries alone occur **before guest execution** and are labelled
*attempts*, not established changes. Command names are assigned by scheduled
time windows, not by recovered parser semantics.

If nothing is reported, check whether the commands actually reached normal
gameplay and whether another state variable tracks the current room. A
negative result is not proof that the game's navigation is static or that
the player object's location must change on every compass command.
