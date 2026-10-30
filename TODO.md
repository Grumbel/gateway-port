# Port milestones

1. Implement BIOS VGA detection (`INT 10h AX=1a00`), the current `GATE.EXE`
   stopping point (`242a:0076`). Continue the runtime startup contract. Keep each
   implemented service explicit and test it with a synthetic guest fixture.
2. Recover RTL/OVL mapping and relocation behavior; document the module entry
   reached after the shared RTLink startup.
3. Implement guest file access beneath the supplied game directory, with writes
   redirected to a separate user directory. Add a proper PSP, environment, DOS
   memory-block model, command line (`VGA BLASTER 7 220`), and child EXEC handling.
4. Reach graphics initialization and identify BIOS, VGA, and game-routine HLE
   boundaries. Connect guest display output and keyboard/mouse to SDL3.
5. Identify sound-driver calls and replace them with SDL3 audio. Validate timing
   and playback against DOSBox recordings.
6. Compare startup, menu, save/load, and a short gameplay sequence against DOSBox.
7. Extract tested resource parsers and interpreter components as formats become
   understood; replace guest routines incrementally.

Keep original assets unchanged and external. Store generated traces, screenshots,
and memory dumps under ignored `build*/` directories. Record file hashes and
addresses for every binary-specific observation. Current tests cover MZ loading,
synthetic DOS startup calls, unsupported-service stops, execution limits, and an
SDL3 dummy-driver diagnostic. Interactive gameplay still needs validation.
