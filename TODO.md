# Port milestones

1. Add a writable guest-state overlay for `RESTART.DAT`. Direct main-program
   startup with the recovered numeric arguments now loads vocabulary and reaches
   an `AH=3Dh` write-only open of that file. Copy writable state into an explicit
   user directory; never modify `../gateway-dos/`.
2. Recover RTL/OVL mapping and relocation behavior; document the module entry
   reached after the shared RTLink startup.
3. Implement guest file access beneath the supplied game directory, with writes
   redirected to a separate user directory. Add a proper PSP, environment, DOS
   child EXEC handling and complete PSP semantics. A tracked environment, DOS
   memory arena, command tail, and timer interrupt bridge are already implemented.
4. Complete the intro-to-main handoff and mouse input. Guest VGA, AdLib PCM, and
   BIOS/DOS keyboard input now connect to the `gateway` SDL3 runtime. Identify
   further game-routine HLE boundaries from actual execution.
5. Identify sound-driver calls and replace them with SDL3 audio. Validate timing
   and playback against DOSBox recordings.
6. Compare startup, menu, save/load, and a short gameplay sequence against DOSBox.
7. Extract tested resource parsers and interpreter components as formats become
   understood; replace guest routines incrementally.

Keep original assets unchanged and external. Store generated traces, screenshots,
and memory dumps under ignored `build*/` directories. Record file hashes and
addresses for every binary-specific observation. Current tests cover MZ loading,
synthetic DOS startup calls, unsupported-service stops, execution limits, and an
SDL3 dummy-driver diagnostic, plus the PIC decoder and PNG writer. Interactive
gameplay still needs validation.

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
