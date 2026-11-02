# Port milestones

1. Compare a short gameplay sequence and save/restore against DOSBox.
   The user confirms keyboard input, save/restore, and audio work under SDL.
   Escape reaches Your Quarters; automated SDL keyboard pagination and `look` work.
   Startup creates `RESTART.DAT` in separate writable state. Add further DOS file
   services only as actual game execution requires them.
2. Compare mouse behavior with DOSBox across scenes and dialogs. SDL motion,
   buttons, cursor visibility, and resized-window mapping are implemented;
   Escape enters mouse mode; Inventory and command-word clicks work in the
   original game. Investigate the first click consumed after mode entry.
3. Extend the [first engine/story boundary](docs/engine-boundary.md): `look` now
   traces through action and room handler tables, and an opt-in object-flag HLE
   matches native execution across opening/closing the drawer and taking/dropping
   the book. [Object-state services](docs/object-state.md) now cover flag writes,
   locations, all 734 object records and 696 handler entry points. Recover the
   remaining engine calls. [RTLink parsing](docs/rtlink.md) now maps all 58
   section extents/fixups, verifies 187 native loads, and replaces their data/fixup
   step under `--hle-overlays`. Parent, bank residency and return-stack handling
   remain native. Trace remaining handler-to-engine calls and extend replay to
   movement/save/restore before replacing the manager.
   DOS reads can load executable overlay sections; invalidate Unicorn's translated blocks after
   these writes. Keep regression coverage for replacement code execution.
   [Ghidra import](docs/ghidra.md) now prepares 58 independent overlay banks,
   validates all 13,947 fixups, and seeds the confirmed native handler/room
   entries for static decompilation. Run the headless import on a Nix machine,
   verify its decompiled output against native traces, then recover the
   kind-4 room-exit callback and its section-specific callees. This has not
   yet established complete function boundaries or the RTLink call graph.
4. Validate AdLib playback and scheduler timing against DOSBox recordings.
   SDL audio submission is tested and the user confirms audible output works.
5. Compare startup, menus, scene palettes, and transitions with DOSBox. Full intro
   playback and normal DOS child EXEC are not yet verified/implemented.
6. Extract tested resource parsers and interpreter components as formats become
   understood; replace guest routines incrementally.

## Toward a native Gateway engine

The goal is to eventually render Gateway with modern, configurable fonts and
layout, higher-resolution/upscaled pictures, and native input rather than VGA
emulation. Keep the original executable as a behavioral reference while mapping:

- The current room, visible picture/frame, and active clickable regions. RGN
  values are engine object IDs; `OBJECT.DAT` list indexes are different.
- Which guest variables encode room and action state; which native handlers
  update them; and how object flag/location changes map to player actions.
- The first-room drawer is confirmed type 2; TAKE calls the indexed child
  setter (`0157:08fc`) to clear slot-0 head `0x123` at `+0x14`. The common
  child-list relocation routine at OVL `0xc830` has been disassembled, and
  a mixed-type logical linked-chain model is independently tested. Its far
  return address `4319:169e` maps to section-4 OVL `0xc879`, confirming the helper's head
  unlink path during TAKE. The verified insertion call at OVL `0xc8f8` and
  all-setter replay establish drawer → player → room → player head updates;
  removing book from the room restores head `0x175` (its next sibling).
  The snapshot exporter and `inspect_child_chains.py` now decode ordered
  child/sibling lists. The native snapshot identifies `0x175` as the picture,
  and reconstructs a mixed-type room → desk/drawer, commset and inventory
  tree. The research model reproduces these final links and DROP/TAKE head
  transitions; its non-head move test remains synthetic. The first global
  audit found 182 nonzero child heads, 404 distinct linked objects and **zero**
  inconsistencies. The follow-up classified 110 type-1 room candidates and 94
  populated ownership trees, with nesting depth up to six; all type-3/4/5
  objects are outside indexed child lists, and the unlinked `0x0bb` dike leak
  still has a direct location word. The new `export_story_world.py` writes
  a conservative JSON ownership forest and Markdown room inventory from this
  snapshot. The actual full export shows only 50/110 room candidates with
  direct type-8 children (107 total), so type 8 cannot be treated as a
  universal scene-picture reference. Fifteen unattached type-6 records are
  named like doors, bridges, or archways. A schema-2 snapshot establishes
  **eight pairs of distinct type-1 room IDs** (type-6 slot 0 matches direct
  location; slot 1 names the second room); seven records have zero links.
  These are **candidate passage endpoints, not verified exits**. Graphviz DOT
  export draws just these candidates. The original hintbook's eleven map
  panels now independently confirm all eight pairs and locate most other
  rooms; see `docs/gateway-map-reference.md`. The first ordinary compass
  replay now verifies four player `0x0d3` location changes: EAST moves
  Your Quarters (`0x0ee`) → Heecheetown (`0x0f1`), WEST moves back, and
  both repeat. Every setter returns to `4319:16e7`: the user's original
  overlay-byte verification confirms direct `CALL 0157:064b` at OVL
  `0xc8c2` in the shared move helper. The original child-head setter calls
  also verify the `0xc879` unlink and `0xc8f8` insertion sites. After the
  first EAST (player initially in the *middle* room list), WEST inserts the
  player as Quarters' new head; the next EAST restores head `0x175`
  (picture). The first middle-sibling unlink is not traced yet.
  `summarize_navigation_trace.py --child-heads` now correlates head calls
  with verified player location changes. The `--trace-navigation` replay
  shows the helper receives `(0x0d3, destination, slot=0)` and returns via
  RTLink `2c24:0701`; the same three upstream saved BP returns
  (`148e:065a`, `148e:0c28`, `1000:04da`) appear for all four moves.
  `summarize_navigation_trace.py --move-callers` filters these stack
  observations against verified location changes. The follow-up disassembly
  located the **resident room-exit scanner at `148e:05f6` / original EXE
  `0x60cf`**, and recovered the type-1 exit count/first index at
  `+0x16/+0x18` and four-byte global exit entries at `[word DS:8dcc]:417e`.
  `tools/inspect_room_exits.py` exports raw entries and a conservative
  direct-destination candidate graph. The user's new replay confirms
  **EAST `0x03`** from Quarters and **WEST `0x07`** from Heecheetown, twice
  each. Both use **kind-2 exits**: original EXE `0x6156–0x61fe` indexes
  `[word DS:8dd2]:4996` for a destination room and a flag-object ID,
  tests flags `0x0c` and `0x10`, and uses the normal mover if the first
  test passes. The inspector now exports this table and draws optional
  dashed conditional links, including the previously absent Quarters exits.
  The follow-up snapshot identifies the **same `0x1ab` room door** guard for
  all three first-room transitions (`0x00e`, `0x00f`, `0x010`). A flag
  inspector now decodes property bits `0x0c` and `0x10` at snapshot time;
  these alone do not prove the flags' earlier values during the moves.
  The full snapshot has 518 exits (232 kind 1, 35 kind 2, 13 kind 3,
  **236 kind 4**, 2 kind 5), with 100 room nodes named by static kind-1/2
  edges. Original EXE kind 4 **calls a room-ID-returning callback** and
  moves when its AX result is a valid different object. `--trace-navigation`
  now hooks the callback return at linear `0x1507c`; use
  `summarize_navigation_trace.py --callback-returns` on an actual kind-4
  transition. **Next:** compare snapshot and observed `0x1ab` flag events,
  identify a reachable kind-4 transition and its return AX, correlate the
  larger graph with hintbook maps, and test a named passage (Dock/cabin
  hatch). Decode remaining kinds 3 and 5, verify restrictions,
  bidirectionality, and actual PIC selection independently of map arrows.
  Also trace real non-head sibling and nested moves, extend to other record
  types, and establish which callers enable
  the gate's restriction parameter. The child accessor (`0157:0825`) is distinct
  from DROP's recursive relationship helper (`0157:00c5`); both are decoded,
  but their complete role in parser visibility and spatial containment is
  still unresolved. Compare both graphs against nested-object traces.
- The parser's action/argument representation, handler dispatch, text formatting
  and rendering calls, with reproducible before/after state dumps.
- Save/restore state boundaries before designing a standalone data model or
  translating native story handlers into portable rules.

The initial F10 debugger is intended to expose current knowledge and capture
evidence. Its scene/region inference is provisional, not a recovered game API.

## Other Legend formats

Mission Critical PIC decompression succeeds, and complete `0003h` XOR frames
can be composed with a previous full picture. **Still unresolved:** smaller
`0003h` records mix scene-dependent animation/layer deltas and independent
sprites; the files do not identify the screen state or base picture to which
every delta applies. Trace the original renderer and RGN-to-PIC/animation
selection before automatically composing them. See
[Mission Critical PIC research](docs/mission-critical.md#xor-frame-deltas).

Keep original assets unchanged and external. Store generated traces, screenshots,
and memory dumps under ignored `build*/` directories. Record file hashes and
addresses for every binary-specific observation. Tests cover loaders, DOS memory
and file services, overlay cache invalidation, VGA, timing/audio models, resource
parsing, and SDL integration. First-room interaction does not establish complete
playability.

## Research handoff

State at the end of 2026-10-30. Format findings are in
[data-files](docs/data-files.md), [Spellcasting](docs/spellcasting.md), and
[ScummVM](docs/scummvm-legend.md). `gateway-extract` converts all PIC files of
Gateway and Spellcasting 101, 201, and 301. Neither ScummVM's `legend` branch nor
`dgate_resource_manager` has anything on scripting, bytecode, or opcodes, so game
logic has to be recovered from the binaries.

Next, in order:

1. Extend the traced parser-to-handler dispatch in
   [engine-boundary.md](docs/engine-boundary.md) and map RTLink sections.
   [Game-logic investigation](docs/game-logic.md) now demonstrates native x86
   story routines in `GATEMAIN.OVL` and six text references using
   `0xf0000000 | (segment << 10) | string_index`. The text lookup routine now
   confirms that split.
   The [object-state inventory](docs/object-state.md) now maps the 696 handler
   entries and all name tokens. [RTLink extraction](docs/rtlink.md) now resolves
   all handler file offsets, section images and relocation sites. Trace calls
   leaving handlers to identify the remaining shared services;
   no story-bytecode VM has been identified.
2. Find what uses the 91 `VOCAB.DAT` link groups; they may tie words to actions.
3. Compare extracted colors, overlays, and the series used in each display mode
   with DOSBox. Trace overlay selection and what frames 2–5 mean.
4. Add tested parsers for strings, vocabulary, object state, and fonts. The
   new RGN rectangle and `OBJECT.DAT` name readers are in `src/` with synthetic
   tests; full room/hotspot semantics still need executable traces.

Smaller open items: the `S101STR.DAT` tree, Spellcasting 201 `VOCAB.DAT` tables
(they do not parse with Gateway's layout), S101 `.RS` headers, and RGN contents in
the Spellcasting games. Spellcasting data is in `../101/`, `../201/`, and `../301/`.
Runtime/video/audio work continues separately from these format findings.
