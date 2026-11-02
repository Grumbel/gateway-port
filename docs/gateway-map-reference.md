# Gateway: published map reference

We can now compare the native room/object snapshot with the game's **original
printed maps** and a recovered **native room-exit table layout**. The maps
are an independent reference, not executable game logic. Direction codes and
conditional traversal still require live native trace verification.

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

A subsequent native-code inspection provides a **specific mechanism**:
kind-4 callback `0x00` checks flag `0x0c` on cabin hatch `0x1b7` and,
when true, returns shared room ID `0x1b1`. Its 14 indexed exits span seven
landing-area rooms. This is verified static x86 control flow, while the
flag-true traversal remains to be replayed. See
[native room callbacks](room-callbacks.md#two-more-statically-decoded-patterns).

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

1. The ordinary **Your Quarters ↔ Heecheetown** connection is native-observed
   without a type-6 pair. The source location-setter callsite and multiple
   child-head writes match the original OVL; see below. The resident exit
   scanner has now been located; export its table entries and correlate its
   direction argument with verified movement (below).
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
  --commands 'east,west,east,west,look,inventory,look' \
  --overlay-map build/rtlink-manifest.json \
  --overlay-file ../gateway-dos/GATEMAIN.OVL \
  --child-heads
```

The report shows candidate player-location changes, the native far-return
address of each location setter, and the code PC of any matching watched
write. Native service `changed=1` calls are verified by the probe; watched
memory-write entries alone occur **before guest execution** and are labelled
*attempts*, not established changes. With both `--overlay-*` inputs, the
original caller is displayed **only** when the five unrelocated bytes before
the native return are a direct far `CALL 0157:064b`. RTLink sections may
overlap in guest memory, so address arithmetic alone does not identify the
original code. Without original assets, omit the two `--overlay-*` arguments;
the report remains useful but its source callsite is unverified. Command names
are assigned by scheduled time windows, not by recovered parser semantics.

### Verified ordinary first-room movement

The user's native `--trace-state-writes --trace-object-writes d3` replay
produced four changed player-location services. The matched watched writes
recorded the old and new words at type-7 object `0x0d3`, record offset
`+0x0c`:

| Guest ms | Timed input | Player location before | After | Native setter return | Writer PC |
| ---: | --- | --- | --- | --- | --- |
| 18,266 | EAST | `0x0ee` Your Quarters | `0x0f1` Heecheetown | `4319:16e7` | `0x11c32` |
| 21,177 | WEST | `0x0f1` Heecheetown | `0x0ee` Your Quarters | `4319:16e7` | `0x11c32` |
| 25,261 | EAST | `0x0ee` Your Quarters | `0x0f1` Heecheetown | `4319:16e7` | `0x11c32` |
| 28,177 | WEST | `0x0f1` Heecheetown | `0x0ee` Your Quarters | `4319:16e7` | `0x11c32` |

These are **verified native service deltas**, not inferred from the published
map. The direction labels remain timed associations; testing both directions
twice and observing the expected inverse writes supports this Gateway Station
edge. It does **not** establish that every game room uses this path.

All four setter calls return to `4319:16e7`. Under the verified section-4
RTLink base (`0x4319` maps to OVL file offset `0xb1e0`), the return maps to
OVL `0xc8c7`. The user's subsequent run **verified the actual original
five-byte far call at `GATEMAIN.OVL:0xc8c2`**, targeting `0157:064b`.
That call falls inside the disassembled shared child-list relocation helper
`GATEMAIN.OVL:0xc830–0xc911`. Cardinal travel therefore **uses the same
location-update machinery as TAKE and DROP**, not a separate movement-only
field setter. The optional mapping above checks both section identity and
original call bytes; it rejects unverified/ambiguous addresses.

The player is a **middle member** of the original room's linked child list,
so a move may use a sibling setter for unlinking rather than the child-head
setter. To inspect the native child-head setter calls captured in this same
replay, including source-verified original head-set callsites, run:

```sh
python3 tools/summarize_object_writes.py \
  build/room-navigation/native.trace \
  --child-only --all-setters --relocations \
  --overlay-map build/rtlink-manifest.json \
  --overlay-file ../gateway-dos/GATEMAIN.OVL
```

The user's report also confirmed two distinct far-call sites inside that
same helper: `GATEMAIN.OVL:0xc879` unlinks a head, and `0xc8f8` inserts a
head. Both calls target `0157:08fc`, the native indexed-child setter.

| Guest ms | Player move | Old room's head replacement | New room's head insertion |
| ---: | --- | --- | --- |
| 18,266 | Quarters → Heecheetown | No head setter observed | Heecheetown (`0x0f1`) → player `0x0d3` |
| 21,177 | Heecheetown → Quarters | Heecheetown (`0x0f1`) → `0x194` | Quarters (`0x0ee`) → player `0x0d3` |
| 25,260–261 | Quarters → Heecheetown | Quarters (`0x0ee`) → `0x175` | Heecheetown (`0x0f1`) → player `0x0d3` |
| 28,177 | Heecheetown → Quarters | Heecheetown (`0x0f1`) → `0x194` | Quarters (`0x0ee`) → player `0x0d3` |

These are **native child-setter entry observations**, not independently
verified post-write head values. The separate `0x0d3` location-setter
service deltas *were* verified. The `--child-heads` option merges both
sources into one report, filters out the unrelated `0x161` setters at
similar timestamps, and requires original-byte-verified section-4 callers.

The first EAST move begins with the player *within* the room's original
sibling chain, so no old-head setter is expected. WEST inserts the player
as Quarters' new head. The next EAST removes that head and restores
`0x175` (**picture**), matching the previously decoded room list. Likewise,
`0x194` is the head restored when leaving Heecheetown. This strongly
supports the **shared unlink/prepend object-move algorithm** for room travel.
It does not directly observe the first departure's *middle-sibling* update
(`0157:0a4b`); a future trace must confirm it. `--relocations` only pairs
adjacent old-head and new-head calls, so it naturally misses the initial
middle-sibling removal and also includes unrelated `0x161` bookkeeping.

### Next: identify the caller choosing the destination

The known setter return `4319:16e7` points **inside** the move helper, not
to the direction parser. The helper's entry at original section-4 file
`0xc830` corresponds to runtime linear `0x447e0` in this default layout.
To capture the entry stack and caller in another bounded replay, run:

```sh
python3 tools/trace_game_logic.py \
  --scenario objects --native-only --verify-overlays \
  --trace-state-writes --trace-object-writes d3 --trace-navigation \
  --command-sequence 'east,west,east,west,look,inventory,look' \
  --output build/room-navigation-caller

rg '^entry .*pc=447e0\b' build/room-navigation-caller/native.trace
```

Each entry carries its guest CS, full initial stack words and frame chain.
Check that `cs=4319` at the event and that the entry coincides with a
verified player location transition; other overlay code may occupy the
same linear address at other times. Identifying its **actual** caller and
the input/destination argument flow is still future work, and this hook
does not itself decode the navigation table.

### First caller-stack result: the direction handler is above RTLink

The user's `--trace-navigation` replay produced **four player-specific
move-helper entries** alongside unrelated startup/`0x161` object moves.
Its far-call arguments, in order after the substituted return IP/CS, are
the player (`0x0d3`), destination room, and destination slot (`0`):

| Guest ms | Object | Destination room | Slot | Immediate far return | Saved BP return chain |
| ---: | --- | --- | ---: | --- | --- |
| 18,266 | `0x0d3` | `0x0f1` | 0 | `2c24:0701` | `148e:065a` ← `148e:0c28` ← `1000:04da` |
| 21,177 | `0x0d3` | `0x0ee` | 0 | `2c24:0701` | Same chain |
| 25,260 | `0x0d3` | `0x0f1` | 0 | `2c24:0701` | Same chain |
| 28,177 | `0x0d3` | `0x0ee` | 0 | `2c24:0701` | Same chain |

This is consistent with the earlier room and child-list transition reports.
In particular, the destination room was already chosen **before** entering
the shared object mover at runtime `4319:1650` (`0x447e0`). As established
in [RTLink's return management](rtlink.md#parent-sections-and-lifetime),
`2c24:0701` is a **manager redirect trampoline**, not an original direction
script or dispatch callsite. Neither the first saved BP return
`148e:065a` nor its ancestors is guaranteed to be the direct call that
chose the destination; they are leads to inspect at higher levels.

The new summary option filters entry-stack observations by the same verified
player-location service deltas, omitting unrelated object moves:

```sh
python3 tools/summarize_navigation_trace.py \
  build/room-navigation-caller/native.trace \
  --world build/parser-child-writes/world.json \
  --commands 'east,west,east,west,look,inventory,look' \
  --move-callers
```

To inspect those persistent resident-code ranges in the **guest-memory
snapshot** (not presumed original EXE file offsets), disassemble the saved
`native.bin` around both BP returns:

```sh
objdump -D -b binary -m i8086 -M intel \
  --start-address=0x14ec0 --stop-address=0x14f80 \
  build/room-navigation-caller/native.bin

objdump -D -b binary -m i8086 -M intel \
  --start-address=0x154a0 --stop-address=0x15560 \
  build/room-navigation-caller/native.bin
```

Here `148e:065a` is linear `0x14f3a` and `148e:0c28` is linear `0x15508`.
`objdump` performs a linear sweep and can misidentify instruction boundaries
or decode inline data, so validate candidate calls with runtime traces.

### Recovered native room-exit table and resident scanner

The saved return `148e:0c28` is immediately after `push cs; call near
148e:05f6` at linear `0x15505`. Comparing the resident code with the
original `GATEMAIN.EXE` establishes that `148e:05f6` is original EXE file
offset **`0x60cf`**, not an RTLink entry point. It is the **type-1 room-exit
scanner**. Its caller pushes the direction argument (a byte zero-extended
to a word) before pushing CS; the scanner therefore sees the entry stack
`0c28,148e,<direction>`. This is a code-verified ABI. The user's repeated
EAST/WEST replay subsequently established **`0x03` = EAST and `0x07` = WEST**
for the two ordinary first-room transitions.

Two original resident getters (`0157:0e83` / `0157:0ed5`, EXE file offsets
`0x33c1` / `0x3413`) read these type-1 room record fields:

| Type-1 record offset | Size | Native meaning |
| --- | ---: | --- |
| `+0x16` | byte | Number of exit entries |
| `+0x18` | word | One-based index of the first exit entry; zero means no indexed exits |

The scanner reads the current-room ID from the word at
`[word DS:8dba]:35c6`. For each exit, it forms `(first - 1 + i) * 4`
and uses the global table at **`[word DS:8dcc]:417e`**, comparing the
first byte with the argument. The original EXE loop at **`0x6322`–`0x637a`**
reveals the four-byte record:

| Exit entry byte | Width | Meaning |
| --- | ---: | --- |
| `+0` | 1 | Direction code |
| `+1` | 1 | Exit kind |
| `+2` | 2 | Argument (meaning depends on kind) |

Kind **1** passes its argument as the destination-room ID to the normal
room-changing function (EXE file `0x5d5d`). Kind **2** uses the argument
as an **index into another four-byte table** at `[word DS:8dd2]:4996`:

| Kind-2 indexed record | Size | Meaning |
| --- | ---: | --- |
| `+0` | word | Candidate destination-room ID |
| `+2` | word | Object ID to test for flags |

The original EXE handler at file `0x6156–0x61fe` calls native object-flag
test `0157:03a6` with flag `0x0c`. When that test succeeds, it passes the
table's destination ID into the same room-change function as kind 1. If
the test fails, it checks flag `0x10`. If `0x10` is set, it calls another
service, sets flag `0x0c` using native `0157:03e0`, and then calls the room
mover. With neither flag, it follows a message path without calling the
mover. The additional service's visible effects remain unclassified.
Thus flag `0x0c` alone does not decide whether an exit is usable; this is
code-derived control flow, not yet a replay of the transitions' actual
condition-object state. Kinds **3–5** branch
into additional text, callback and extended handling and remain incomplete.
It would be wrong to treat every row as an unconditionally traversable route.

#### First native direction-byte replay and supplied graph

The user's second bounded EAST/WEST replay produced these entry/move pairs:

| Input | Direction argument | Exit record | Verified native player move |
| --- | --- | --- | --- |
| EAST at 18,176 ms | `0x03` | Quarters entry 244, kind 2, index `0x00e` | `0x0ee` → `0x0f1` at 18,266 ms |
| WEST at 21,176 ms | `0x07` | Heecheetown entry 255, kind 2, index `0x010` | `0x0f1` → `0x0ee` at 21,177 ms |
| EAST at 25,176 ms | `0x03` | Same kind-2 entry | `0x0ee` → `0x0f1` at 25,261 ms |
| WEST at 28,176 ms | `0x07` | Same kind-2 entry | `0x0f1` → `0x0ee` at 28,177 ms |

Both are actual *verified player location writes*. The match to a particular
exit-table row is an inference from the scanner's comparison and supplied
snapshot, not a separate trace of that record's selection. The snapshot
also lists Quarters direction `0x0c` as kind 2, argument `0x00f`; its
compass meaning is not yet established. Heecheetown's other four routes
are kind-1 direct entries: `0x01` → Corridor D4, `0x03` → Blue Hell Bar,
`0x05` → Corridor D6, and `0x08` → Corporation Office Lobby.

The attached DOT export has **232 kind-1 edges across 87 room nodes**;
it excludes kind-2 edges, so Your Quarters is missing even though the
replay proved that its east exit works. That is a limitation of the original
direct-only exporter, not evidence of a disconnected game world.

A follow-up full snapshot report established **110 type-1 rooms**, **107**
with indexed exits, and **518** records: 232 kind 1, 35 kind 2, 13 kind 3,
**236 kind 4**, and 2 kind 5. All kind-1 and kind-2 records resolve to a
type-1 destination, spanning **100 distinct candidate room nodes**.
Crucially, the three Quarters/Heecheetown kind-2 records all use **object
`0x1ab` (`room door`)** as their condition: index `0x00e` (Quarters EAST),
`0x00f` (Quarters direction `0x0c`), and `0x010` (Heecheetown WEST).
Their common flag state can therefore control these entrances as a unit.
The two directions from Quarters lead to the *same* room and guard, but the
meaning of `0x0c` has not yet been mapped to an input command.

#### Kind-4 callback may choose a dynamic room

The original EXE's kind-4 branch at file offsets **`0x6269–0x62a5`**
passes `(exit.argument, 0x0f)` to a far callback (original unrelocated
operand `0474:0002`). It interprets the returned AX as a **candidate
destination object ID**: zero or the current room performs no movement;
a value beyond the current object count also does not move. Otherwise the
branch feeds AX into the **same room mover** as kind 1. Thus the 236 kind-4
entries are **not unlinked by definition**: they can represent dynamically
resolved destinations that no static room-ID graph can discover. Their
original callback semantics and actual destinations need runtime tracing.

The next replay resolved the entire 44-entry callback directory and
**traced Casino EAST's callback `0x14`** returning `AX=0`, without a
player movement. The full native table, original OVL addresses and the
initial decompilation are in [room-callbacks.md](room-callbacks.md).

`--trace-navigation` now also hooks **linear `0x1507c`** (`148e:079c`,
original EXE file `0x6275`), the instruction immediately after that far
callback returns. At this point AX is still the callback result. On a
replay that genuinely exercises a kind-4 exit, use:

```sh
python3 tools/summarize_navigation_trace.py \
  build/room-direction-check/native.trace \
  --commands 'east,west,east,west,look,inventory,look' \
  --callback-returns
```

The original EAST/WEST scenario uses kind 2, so **no kind-4 return is
expected there**. First choose a reachable room and direction whose
exported record has `kind=4`, then replay that direction with the new hook.
The verified example instead reaches Casino using **three EAST moves**
and attempts a fourth; `--trace-navigation` now also captures the scanner's
callsite, the resident far-pointer gate, the selected RTLink trampoline and
the gate's return. It filters event `0x01` object-handler calls separately
from event `0x0f` room-exit decisions.

Use the new **read-only snapshot exporter** on an existing native replay:

```sh
python3 tools/inspect_room_exits.py \
  build/room-navigation-caller/native \
  --names ../gateway-dos/OBJECT.DAT --room 0x0ee --room 0x0f1

# Decode the final memory snapshot's 0x1ab property flags as well:
python3 tools/inspect_room_exits.py \
  build/room-navigation-caller/native \
  --names ../gateway-dos/OBJECT.DAT --room 0x0ee --room 0x0f1 \
  --condition-flags

# All type-1 room entries and direct type-1 destinations:
python3 tools/inspect_room_exits.py \
  build/room-navigation-caller/native \
  --names ../gateway-dos/OBJECT.DAT --format json \
  > build/room-exits.json

# Candidate *direct* route graph (not a verified world map):
python3 tools/inspect_room_exits.py \
  build/room-navigation-caller/native \
  --names ../gateway-dos/OBJECT.DAT --format dot \
  > build/room-exits.dot
dot -Tsvg build/room-exits.dot -o build/room-exits.svg

# Include kind-2 destination candidates as dashed, flag-gated edges:
python3 tools/inspect_room_exits.py \
  build/room-navigation-caller/native \
  --names ../gateway-dos/OBJECT.DAT --format dot --include-conditional \
  > build/room-exits-with-conditions.dot
dot -Tsvg build/room-exits-with-conditions.dot \
  -o build/room-exits-with-conditions.svg

# Check how many rooms and exit kinds each graph covers:
python3 tools/inspect_room_exits.py \
  build/room-navigation-caller/native --format stats
```

The exporter uses the existing `inspect_object_state.py` descriptor and
object directory logic, does not modify the dump or game files, and validates
record and guest-memory bounds. The default DOT graph contains only solid
kind-1 direct-room candidates. Optional dashed kind-2 edges identify the
table's destination and annotate the condition-object ID, but deliberately
do not claim the current flags permit traversal. Other scripted exit kinds
still do not appear. Room names are optional; the raw IDs remain stable.
`--condition-flags` uses the original `0157:03a6` property descriptor and
type mask to report the guard's flag `0x0c` and `0x10` **at dump time**.
It does not reconstruct the earlier values when the player used the exit;
`--trace-state-writes` plus the native `object-flag[-set|-clear]` service
entries are still needed for that timeline.

To repeat the **confirmed first-room EAST/WEST trace**, use
`--trace-navigation` (hooks both `0x447e0` and `0x14ed6`):

```sh
python3 tools/trace_game_logic.py \
  --scenario objects --native-only --verify-overlays \
  --trace-state-writes --trace-object-writes d3 --trace-navigation \
  --command-sequence 'east,west,east,west,look,inventory,look' \
  --output build/room-direction-check

python3 tools/summarize_navigation_trace.py \
  build/room-direction-check/native.trace \
  --world build/parser-child-writes/world.json \
  --commands 'east,west,east,west,look,inventory,look' \
  --direction-calls

# To inspect the untouched stack/argument words directly:
rg '^entry .*pc=14ed6\b' build/room-direction-check/native.trace
```

The `--direction-calls` filter only accepts the exact `0c28,148e` entry
frame and displays a nearby verified player-location change, if any. This
is a **bounded guest-time correlation**. The two repeat transitions and the
supplied exit table strongly corroborate EAST/WEST's codes. This does not
prove which other numeric codes correspond to compass words or how the
kind-2 flag-`0x10` branch behaves. The scanner is reached through a call
distinct from the section-4 RTLink mover.

If nothing is reported, check whether the commands actually reached normal
gameplay and whether another state variable tracks the current room. A
negative result is not proof that the game's navigation is static or that
the player object's location must change on every compass command.
