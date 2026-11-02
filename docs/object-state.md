# Object mutation services and handler inventory

Investigation: 2026-10-31, Linux x86-64 and Unicorn 2.1.4, original Gateway
binaries identified in [executable.md](executable.md). Extends the
[first engine boundary](engine-boundary.md). Original assets remain unchanged.

## Expanded native/host boundary

`--object-state verify|hle` extends the original read-only flag experiment to five
shared services. `--object-flags verify|hle` retains its original query-only scope;
the two options are mutually exclusive. Both are experimental and disabled by
default. The game still runs its native parser, event handlers, higher-level
object movement/list maintenance, text/graphics, and RTLink loader.

| Service | Runtime entry | Native comparison point | Arguments after far return IP/CS |
| --- | --- | --- | --- |
| Flag query | `1157:03a6` / linear `11916` | `1194c` | Object, property; returns AX 0/1 |
| Flag set | `1157:03e0` / `11950` | `1197f` | Object, property; void |
| Flag clear | `1157:0413` / `11983` | `119b4` | Object, property; void |
| Direct location query | `1157:0546` / `11ab6` | `11b2c` | Object; returns AX location word |
| Direct location set | `1157:064b` / `11bbb` | `11c36` | Object, location word; void |

These addresses apply to the observed default Gateway launcher layout. The
extended mode checks a fingerprint of all 1070 bytes at linear `1180d`–`11c3a`
and requires CS `1157`; expected FNV-1a is `d7176aec031d44a3`. Query-only mode
keeps its original 323-byte guard. Intro occupants of the address are ignored
before activation; a subsequent fingerprint mismatch fails the experiment.

For reads, verification mode lets the native function execute and compares AX
at its common epilogue. For writes, it proposes the resulting byte/word without
modifying guest state, executes the native function, and checks the target bytes
at the epilogue. HLE mode applies that write directly to guest-owned state and
returns through the original far-call frame. Void routines leave entry AX intact;
no caller-visible return value is claimed. As in the first experiment, scratch
registers, dead stack contents, and arithmetic flags are not reproduced exactly.

The setters operate on the same validated object/property layout as the query.
Flag writes preserve neighboring bits and honor the property's object-type mask.
The location routines support these fields in the pointed-to object record:

| Type | Location field offset |
| --- | --- |
| 2 | `0x0e` |
| 6 | `0x0a` |
| 7, 8 | `0x0c` |
| Other types | Query returns 0; setter performs no state write |

The original location setter stores any 16-bit location value; it does not check
that the destination is an object. Higher-level native movement code retains
responsibility for containment/list updates. The host performs no independent
world-model bookkeeping.

Address arithmetic adds field offsets modulo 65536, with no carry into the far
segment. A single word load/store then accesses two consecutive linear bytes,
including when its effective offset is `ffff`. Synthetic guest execution checks
that behavior in Unicorn; the host reader/writer and unit tests match it. This
refines the earlier shorthand that “offsets wrap”: operand width matters.

## Recovered handler and name inventory

`tools/inspect_object_state.py` exports object records, names, locations, native
handler pointers, and recognizable RTLink trampoline fields from a probe memory
snapshot. It does not execute the game or change data. For example:

```sh
python3 tools/inspect_object_state.py build/object-state-final/hle \
  --names ../gateway-dos/OBJECT.DAT > build/object-handlers.json
```

The exporter reads DS from the adjacent `.regs` file. Its input layout is Gateway
specific, with bounds/type/record-length/handler/name validation; it is not a
universal RTLink parser. Generated JSON includes game data and belongs in ignored
build output, not source control.

Confirmed inventory in the original first-room replay:

- 734 engine objects; 709 have a nonzero native handler index, 25 do not.
- 696 distinct handler indices, covering 1–696, each with a distinct trampoline
  and a distinct RTLink section/entry-offset pair.
- All 696 trampolines have `call near` followed by `jmp far` and a trailing word.
  Grouping by that word yields 43 section groups. The directory mapping from
  section words to on-disk overlay extents still needs recovery; the exporter
  reports `section_word` rather than claiming a complete loader reconstruction.
- 730 nonzero primary-name tokens plus 55 secondary-name tokens account for all
  785 distinct names in `OBJECT.DAT`; four objects have no primary-name token.
  Each token is a **one-based byte offset into the name payload**, not an index
  into its list of strings. Both fields are words at record offsets 0 and 2;
  zero means no name. All nonzero tokens land on NUL-delimited string boundaries.

The handler-index getter at runtime `1157:0f27` / linear `12497` selects a word
according to object type. Disassembly of its switch table gives:

| Type | Handler-index word offset |
| --- | --- |
| 1 | `0x10` |
| 2 | `0x22` |
| 3 | `0x0a` |
| 4 | `0x12` |
| 5 | `0x14` |
| 6, 8 | `0x16` |
| 7 | `0x1e` |

The dispatcher subtracts one from a nonzero index, divides it into 512-entry
banks, and uses the segments at `[DS:8db4]` and `[DS:8db6]` for four-byte far
pointers. The pointed-to trampolines preserve RTLink loading before the final
jump into a handler. In the captured layout:

| Engine ID/name | Handler | Trampoline | Trampoline far target | Section word |
| --- | ---: | --- | --- | --- |
| `0xee` / Your Quarters | 233 | `2c24:2cc1` | `49d8:0035` | `0x26` |
| `0x123` / book | 282 | `2c24:2ad7` | `49d8:0532` | `0x26` |
| `0x124` / drawer | 283 | `2c24:2acd` | `49d8:05f8` | `0x26` |

Section `0x26` is the room module already traced loading from GATEMAIN.OVL file
offset `0x51480`. Thus the book and drawer handlers start at file offsets
`0x519b2` and `0x51a78`, respectively, in that same loaded section. The trampoline
targets in unloaded sections must not be treated as currently valid code in the
snapshot: another module can occupy the memory until RTLink reloads it.

The object-type descriptor segment is stored at `[DS:8d52]`. The third byte of
each three-byte descriptor supplies these record lengths: 26, 36, 12, 20, 22,
24, 32, 24 for types 1–8. Adjacent record spacing matches those lengths for 721
of 733 pairs when sorted by linear address; the remaining boundaries include
allocation gaps. The 734 records total 20,650 bytes. These records are now an
explicit comparison surface; they are not all of the game's persistent state.

The **first** byte of each descriptor (`type*3-3`) is used by the original
recursive object-relation enumerator as its link-slot count. The exporter now
includes `relation_count` and `relation_links` in object JSON. This is distinct
from the third-byte record length and from the single direct `location` getter:

| Type | Relation accessor used by the native helper |
| ---: | --- |
| 2 | State word at `+0x0e`, returned for every slot |
| 6 | Consecutive words from `+0x0a + 2*slot` |
| 7, 8 | State word at `+0x0c`, returned for every slot |
| 1, 3, 4, 5 | No relation; accessor returns zero |

The exporter checks that a type-6 list stays inside its record, and preserves
zero link values. The recursive consumer and its limitations are documented
in [action-dispatch.md](action-dispatch.md#resident-recursive-object-relations).
`tools/inspect_relation_paths.py` reads this JSON to show one chain between
two requested object IDs. It is intentionally independent of the original
game's visibility, permission checks, and command parser.

### Separate parser child slots

The parser candidate iterator at `GATEMAIN.EXE` file offset `0x0eba`
(`1000:0000` in the original replay) uses **another** object accessor,
`0157:0825` at file `0x2d63`. Its bounds are the **second** byte of the
three-byte descriptor (`type*3-2`). The JSON exporter therefore includes
`child_count` and `child_links`, independently of `relation_count` and
`relation_links`. These are not interchangeable graphs.

| Type | Parser child accessor `0157:0825` |
| ---: | --- |
| 1 | Repeats `state[0x0c]` for every slot |
| 2 | `state[0x14 + 2*slot]` |
| 3 | Returns zero |
| 4 | `state[0x0a + 2*slot]` |
| 5 | `state[0x0c + 2*slot]` |
| 6 | `state[0x0e + 2*slot]` |
| 7 | `state[0x12 + 2*slot]` |
| 8 | Returns zero |

For indexed child lists the exporter rejects records too short for the
declared slots, preserves zero entries, and does not conflate child traversal
with the handler-side relation/path test. The iterator also optionally
filters slots with the separate `0385:0a9a` predicate described in
[action-dispatch.md](action-dispatch.md#resident-parser-candidate-iterator).
The independent C++ `StoryModel::eligible_child_slots()` tests this filtered,
nonzero stage against caller-supplied child words, without assuming that
child pointers can be reconstructed from `location` alone.

The matching **native child-link setter** is `0157:08fc` (original
`GATEMAIN.EXE` file offset `0x2e3a`, replay linear `0x11e6c`). Its far-call
stack words are `(object ID, slot index, new child ID)` at `[BP+6]`, `[BP+8]`,
`[BP+0x0a]`. It checks the same second descriptor byte for the slot bound,
then writes the same type-specific word addresses as the accessor above:
type 2 at `+0x14+2*slot`, type 4 at `+0x0a+2*slot`, type 5 at
`+0x0c+2*slot`, type 6 at `+0x0e+2*slot`, type 7 at `+0x12+2*slot`,
and type 1 at fixed `+0x0c`; type 3/8 are no-ops. The caller removing the
first-room book is now identified as the common relocation helper at original
`GATEMAIN.OVL:0xc879` (section 4), which calls this setter. The independent
location setter at `0157:064b` (file `0x2b89`) updates only its type-dependent
location field; its disassembly does **not** contain a child-list unlink.

The subsequent watched native replay established that drawer `0x124` is
**type 2** with 36 state bytes and 3 child slots. During TAKE, the indexed
setter writes zero over `0x0123` at drawer state offset `+0x14`, at guest
time 28,356 ms. The generic move helper at `GATEMAIN.OVL:0xc830` maintains
these lists as *heads of next-sibling chains*, not flat arrays of all
contained objects. For type 2, offset `+0x10` is a byte slot index and
`+0x12` is the next-sibling word; `+0x14` begins the child-head words.
The child setter returns to live `4319:169e`, identified by verified RTLink
section 4 as original OVL `0xc87e` (immediately after its call at `0xc879`).
See [child-list relocation](action-dispatch.md#the-shared-child-list-relocation-routine)
for the original control flow and bounded C++ model.

### Enumerating siblings instead of just child heads

The same original EXE contains a byte child-slot getter/setter at
`0157:0753/07bc` and a word next-sibling getter/setter at `0157:09e4/0a4b`.
The exporter decodes both fields, independently of `child_links`:

| Native record type | Child's `child_slot` byte | Child's `next_sibling` word |
| --- | ---: | ---: |
| 2 | `+0x10` | `+0x12` |
| 7, 8 | `+0x0e` | `+0x10` |

Types without these native accessors omit the fields rather than inventing
them. Each indexed `child_links` entry is a **head pointer**; subsequent
siblings are reached through each child's `next_sibling` word. This means a
zero child-head write does not by itself establish that the room or inventory
is empty, or that no second object was previously linked behind that head.

The independent inspector reconstructs the lists in a saved native snapshot:

```sh
python3 tools/inspect_object_state.py build/parser-child-writes/native \
  --names ../gateway-dos/OBJECT.DAT > build/parser-child-writes/objects.json
python3 tools/inspect_child_chains.py build/parser-child-writes/objects.json \
  --container 0xee --recursive --max-depth 3
python3 tools/inspect_child_chains.py build/parser-child-writes/objects.json \
  --container 0xd3
```

The inspector labels each slot, retains sibling order, checks the child's
location and slot metadata, and fails on a dangling child or sibling cycle.
Recursive expansion detects a containment cycle without following it forever.
It observes one snapshot, not parser visibility or which actions succeeded.

To check **all** indexed heads and their known sibling chains, rather than
choosing one container at a time, use:

```sh
python3 tools/inspect_child_chains.py build/parser-child-writes/objects.json --audit
```

This prints the number of nonzero child heads, traversed sibling links, and
distinct referenced objects. It reports dangling IDs, metadata disagreements,
cycles, and children referenced from multiple chains, without following bad
links forever. A record type lacking the decoded sibling accessor is counted
as an **undecoded tail**, not silently treated as proof that its list ends.
It cannot establish a complete world graph for types with unknown fields, and
does not infer parser visibility from the absence of link inconsistencies.

#### Whole-snapshot audit: 734 records

The user ran this against the final `parser-child-writes/native` snapshot:

| Native structural measurement | Result |
| --- | ---: |
| All records | **734** |
| Nonzero indexed child heads | **182** |
| Child links traversed | **404** |
| Distinct referenced children | **404** |
| Linked-list tails with no decoded sibling accessor | **0** |
| Dangling, duplicated or inconsistent links detected | **0** |
| Records **not referenced** by these indexed heads | **330** |

This is stronger than validating one room: all encountered children have
consistent native parent/slot metadata and no observed linked-list sharing
or dangling successors. But the other 330 records are *not necessarily*
orphaned game objects: root rooms, unused definitions, other record kinds
and special storage may not appear as an indexed child. The audit has not
proven every object must have such an owner.

The inspector now also prints a type-by-type split of linked/unlinked records,
counts unlinked objects with a nonzero direct location, and identifies root
containers with the most descendants. It checks cycles through **nested
ownership** separately from sibling cycles. To inspect specific unlinked
classes without dumping hundreds of records:

```sh
python3 tools/inspect_child_chains.py build/parser-child-writes/objects.json \
  --audit --list-unlinked --unlinked-type 2 --limit 60
```

The output remains a snapshot graph classification, not a claim that a root
is the current scene or that a missing parent is a bug. In particular, only
observed child links are used to calculate roots and depth—direct-location
words are *checked*, never inserted as guessed graph edges.

#### Export an initial typed world snapshot for a native renderer

`tools/export_story_world.py` makes an immutable, normalized ownership forest
from the same already-decoded `objects.json`. It requires no DOS execution or
additional game assets and strictly checks that each referenced child has one
parent/slot, the sibling chain can be followed, and the resulting containment
graph is rooted and acyclic. Type-1 roots are marked as **room candidates**;
this is based on the native record type and supplied names, not a verified
navigation, scene-selection, or rendering ABI.

```sh
python3 tools/export_story_world.py build/parser-child-writes/objects.json \
  --format markdown > build/parser-child-writes/world.md
python3 tools/export_story_world.py build/parser-child-writes/objects.json \
  --format json > build/parser-child-writes/world.json
python3 tools/export_story_world.py build/parser-child-writes/objects.json \
  --format dot > build/parser-child-writes/room-candidates.dot
# Optional (requires Graphviz):
dot -Tsvg build/parser-child-writes/room-candidates.dot \
  -o build/parser-child-writes/room-candidates.svg
```

The Markdown inventory lists type-1 rooms with direct and nested contents,
**direct type-8 child counts**, other nonempty roots, and unattached records
that nevertheless have nonzero native locations (such as `0x0bb` dike leak at
`0x095`). The JSON forest has `schema_version: 2` and supplies integer IDs,
record types, names, native handler indices and available RTLink file
references, original location/slot/sibling fields, and ordered
`child_slots[].objects` children. It includes all 734 records, including
roots with no active children, without copying opaque `raw_state` bytes into a
purported gameplay schema.

Schema 2 also preserves the *separate* original `relation_count` and
`relation_links` words (`native_relation_count`, `native_relation_links`)
where present. A `connector_candidates` section resolves every **type-6**
direct-location and multi-slot relation word against names and record types
in the same snapshot. A pair of
two distinct references to type-1 objects is labeled a *two-room candidate*,
never an established exit or traversable edge. Missing relation arrays in
older inspector records are explicitly marked unavailable.
The type-6 relation accessor belongs to the recursive relationship helper;
its meaning must not be conflated with parser child lists, scene selection,
or movement directions.

The supplied **full exported snapshot** has 110 type-1 roots among 330
unlinked records; those roots and 404 linked descendants provide a candidate
scene/object hierarchy for a modern client. Only **50 of the 110** type-1
roots have direct type-8 children (107 such children altogether), and **36**
rooms have no descendants. The type-8 names include furniture/scene features
like plants and controls, with only one literally called `picture`; a rule
such as “choose the first type-8 child as the room's background image” would
be unsupported. The original schema-1 export showed 15 unattached type-6
records named like doors, archways, stairs and bridges, but omitted their
multi-slot relation fields. A subsequent **schema-2 Markdown export** of the
same 734-object native state supplied all these words: each of the 15 records
has two relation slots; seven have `0, 0`, and the other eight have **two
distinct references to type-1 rooms**, with the first word equal to the
record's direct-location word.

| Type-6 record | First relation / direct location | Second relation |
| --- | --- | --- |
| `0x020` door | `0x013` Travel Pod | `0x012` Satellite |
| `0x1b7` cabin hatch | `0x0fe` Dock | `0x1b1` ship cabin |
| `0x225` archway | `0x1ba` Mountain Plateau | `0x1bb` Vantage Point |
| `0x226` rope ladder | `0x1c3` Garden | `0x1c4` Tree House |
| `0x227` rope bridge | `0x1c9` Pinnacles | `0x1ca` Becker's House |
| `0x24d` archway | `0x232` City Center | `0x233` Temple Court |
| `0x24e` spiral staircase | `0x233` Temple Court | `0x234` Octagonal Room |
| `0x259` door | `0x257` Plaza | `0x258` Tower |

This pattern is strong evidence for an **endpoint-pair representation** of
special passage objects; the Temple Court appears in two different pairs.
It does *not* establish which direction is allowed, whether the passages are
usable, or how an action actually changes rooms. It also cannot be the full
navigation graph: there are only eight populated pairs for 110 candidate
rooms. The `--format dot` export plots these pairs as **dashed, undirected
candidate edges**, omitting all unreferenced rooms; it is not a map of exits.
Legend's [original hintbook maps](gateway-map-reference.md) independently
place all eight endpoint pairs and provide a separate reference for ordinary
movement edges, with puzzle gates and up/down notation.

Separately, the **resident type-1 room-exit scanner** has now been recovered
from original `GATEMAIN.EXE` file offset `0x60cf` (runtime `148e:05f6`).
For type-1 records specifically, byte `+0x16` stores the exit-entry count,
and word `+0x18` stores its one-based first index into a global four-byte
`{direction, kind, argument}` table at `[word DS:8dcc]:417e`.
`tools/inspect_room_exits.py` reads those fields from the **same native
guest-memory snapshots** as this object inspector and exports raw entries,
JSON or a directed graph of kind-1 destination-room candidates. Its optional
`--include-conditional` mode adds dashed kind-2 candidate links by following
the table at `[word DS:8dd2]:4996`, which gives destination and condition
object IDs. `--format stats` reports room/exit coverage. Unlike the
type-6 passage endpoint pairs above, its direct edges originate in the
native room-exit data. Neither representation establishes that a conditional
exit is currently open or that a parser command will traverse it; see
[room-exit tracing](gateway-map-reference.md#recovered-native-room-exit-table-and-resident-scanner).

Next, correlate a native room transition through one of these named passage
objects with the type-6 record, the changing current-room variable, and the
handler that performs the change. In particular, distinguish the recursive
object-relation test (`0157:00c5`) from parser direction handling. This exporter
still does **not** reconstruct scripts, object visibility, exit connections,
mutable flags, text selection, picture composition, or transitions. Inactive
objects with location zero remain records, not automatically assigned rooms.

#### Decoded Your Quarters hierarchy from the supplied native replay

The user ran the inspector against the repeated-action native snapshot after
the second TAKE. It returned the following *actual ordered links* (not
invented from names or reverse-mapped from screen hitboxes):

| Parent | Slot | Indexed head / following siblings |
| --- | ---: | --- |
| Your Quarters `0x0ee` (type 1) | 0 | `0x175` picture → `0x16f` plant → `0x150` wall locker → `0x14f` bed → `0x14e` desk chair → `0x131` PV commset → `0x125` desk → `0x0d3` you |
| PV commset `0x131` (type 2) | 2 | `0x170` PV slot |
| Desk `0x125` (type 2) | 1 | `0x126` debit card |
| Desk `0x125` | 2 | `0x124` drawer |
| Player `0x0d3` (type 7) | 0 | `0x123` book |
| Player `0x0d3` | 1 | `0x0d7` boots → `0x0d5` white badge → `0x0d4` blue coverall |

The drawer's slot 0 is empty in this snapshot. Thus the `0x175` head restored
by the guest's second TAKE is specifically the *picture*, not an unnamed
special room entry. Type 8 pictures and plants participate in the same
next-sibling chain as type 2 furniture, while the enclosing room is type 1.
The scene-specific hierarchy is regression-tested with an asset-free fixture.
The whole-snapshot audit above validates the decoded link structure, but a
DOSBox comparison has not yet been supplied and the table establishes this
one final snapshot, not every intermediary state.

## State-changing replay

```sh
python3 tools/trace_game_logic.py --scenario objects --services state \
  --output build/object-state-final
```

This runs native, verification, and HLE modes sequentially, with separate writable
state directories and a 180-second host timeout per run. Over 42000 virtual ms it
clears pagination and types:

```text
look
open drawer
look
take book
inventory
drop book
close drawer
look
```

The book starts in the drawer (`123 → 124 → 125 → ee`, book → drawer → desk →
room). Taking it stores location `d3`, the player object. Dropping it stores
location `ee`, Your Quarters. Opening/closing the drawer sets/clears property
`0x0c` on object `0x124`. The last capture shows the closed drawer and the book
on the room floor. Screenshots were visually inspected; native verification and
HLE agree on:

| Service | Calls compared/replaced |
| --- | ---: |
| Flag queries | 1149 |
| Flag sets | 62 |
| Flag clears | 3 |
| Direct location queries | 210 |
| Direct location sets | 10 |
| Total | 1434 |

All native return/write comparisons pass. Verification/HLE service names,
arguments, returned values, write addresses/bytes, and return sites match in
sequence, ignoring virtual timestamps. Final native, verification, and HLE
snapshots have identical **all 734 object records**, descriptor-derived record
sizes, locations, name tokens, handler indices and trampoline mappings.
All final PPMs have SHA-256
`0b7ac6ed8e94ced6c85c8b01496ad39108749870029fe75da802cc9af44566fb`.

All 15 CTest targets pass, including synthetic setter/location layout tests,
word-access behavior, snapshot export and existing SDL checks. These headless
replays did not validate audible output, compare against DOSBox, exercise room
movement or save/restore, or establish complete gameplay. Exact object-record
equality does not imply equality of global state, all guest RAM, or save files.

## Implication for a native game VM

We now have enumerated native object/event entry points and five shared services
implemented against the original guest state. The next extraction step is to
map the 43 RTLink section words to file offsets, relocations, and lifetimes, then
trace calls leaving those handlers to enumerate the remaining service ABI.
Keeping the guest state and original loader remains necessary until that mapping
is complete. Room movement and save/restore should be added to the comparison
suite before enabling a broader host-state replacement.

## Follow-up: section extents and fixups

The [RTLink investigation](rtlink.md) now maps all 696 handler entries to exact
OVL file offsets, extracts all 58 section images, and checks all 13,947 fixups.
The same replay verifies 187 live native loads across nine sections with no
byte differences. Use `--overlay-map build/rtlink-manifest.json` with the object
exporter to include `native_code` file/section/entry fields in its JSON.
