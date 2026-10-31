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
3. Recover RTLink module mapping and game-routine boundaries. DOS reads can load
   executable overlay sections; invalidate Unicorn's translated blocks after
   these writes. Keep regression coverage for replacement code execution.
4. Validate AdLib playback and scheduler timing against DOSBox recordings.
   SDL audio submission is tested and the user confirms audible output works.
5. Compare startup, menus, scene palettes, and transitions with DOSBox. Full intro
   playback and normal DOS child EXEC are not yet verified/implemented.
6. Extract tested resource parsers and interpreter components as formats become
   understood; replace guest routines incrementally.

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

1. Find out whether game logic in `GATE.EXE`, `GATE.RTL`, and `GATEMAIN.OVL` is
   native code or embedded bytecode, starting with how the code selects strings.
   Test the `0xFsssnnnn` message number hypothesis there.
2. Find what uses the 91 `VOCAB.DAT` link groups; they may tie words to actions.
3. Compare extracted colors, overlays, and the series used in each display mode
   with DOSBox. Trace overlay selection and what frames 2–5 mean.
4. Add tested parsers for strings, vocabulary, objects, fonts, and regions. No
   decoder other than PIC is in `src/` yet, and the others ran only in throwaway scripts.

Smaller open items: the `S101STR.DAT` tree, Spellcasting 201 `VOCAB.DAT` tables
(they do not parse with Gateway's layout), S101 `.RS` headers, and RGN contents in
the Spellcasting games. Spellcasting data is in `../101/`, `../201/`, and `../301/`.
Runtime/video/audio work continues separately from these format findings.
