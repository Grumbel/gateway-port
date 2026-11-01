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
  a type-2 linked-chain model is independently tested. Its far return address
  `4319:169e` maps to section-4 OVL `0xc879`, confirming the helper's head
  unlink path during TAKE. The verified insertion call at OVL `0xc8f8` and
  all-setter replay establish drawer → player → room → player head updates;
  removing book from the room restores head `0x175` (its next sibling).
  The snapshot exporter and `inspect_child_chains.py` now decode ordered
  child/sibling lists. The native snapshot identifies `0x175` as the picture,
  and reconstructs a mixed-type room → desk/drawer, commset and inventory
  tree. The research model reproduces these final links and DROP/TAKE head
  transitions; its non-head move test remains synthetic. Next run the new
  `inspect_child_chains.py --audit` on all 734 native records, trace actual
  non-head sibling moves and nested-object moves, extend to other record
  types, and establish which callers enable the gate's
  restriction parameter.
  The child accessor (`0157:0825`) is distinct
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
