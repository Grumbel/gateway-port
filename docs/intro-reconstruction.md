# Gateway launcher intro: native reconstruction

This is a **separate** program from the game's command/verb loop. It belongs
to the small launcher `GATE.EXE`, not `GATEMAIN.EXE` or its story overlays.
The native prototype in `src/story/intro_presentation.*` reads the original
resources and renders a fixed-clock sequence without Unicorn. The SDL3 viewer
is `gateway-intro`; `gateway-intro-dump` renders one frame to PPM without SDL.

## Observed opening

The following results come from the unmodified, SHA-256-pinned `GATE.EXE`
in `docs/executable.md`, run **read-only** under the headless probe with
`--deterministic-clock`. Times are guest milliseconds, not real-world video
timestamps. File reads are *not* necessarily draw calls.

| Guest time | Observed output | Resource evidence |
| ---: | --- | --- |
| 0–3.27 s | White 640×480 screen | Intro starts in VGA mode 12h |
| 3.27–7.54 s | Black Legend lettering on white | `GATE_201.PIC` #0; frame at 5s is pixel-exact in native renderer |
| 7.54–12.57 s | Black 320×200 screen; seven red bars appear in order | Mode 13h; `GATE_300.PIC` #0 at 7.539s; 9×38 bars at x=24,64,105,149,194,242,284, y=81 |
| 12.57–13.15 s | Bars remain visible while resources are loaded | `GATE_300.PIC` #1–#26 (XOR deltas) read over ~580 ms |
| 13.2–14.39 s | Red logo reveal animation | PIC delta composition, native display schedule *approximate* |
| 14.39–18.58 s | Final title (additional credit lettering appears later) | `GATE_300.PIC` #27, full 320×200 image |
| 18.58–20.13 s | First painted background, before caption | `GATE_301.PIC` #5 |
| 20.13s onward | Prose panels and animated scenery | `GATE_305.FNT` proportional 10px font; text embedded as 18 NUL-terminated strings starting at file offset 0x428f in `GATE.EXE` |
| ~40–42.6 s | First caption clears; spacecraft flies across the painting | `GATE_301` #6–#31 preload at 40.19–41.17s; 26 painted sprite frames with ~55ms cadence |
| ~45.0–45.5 s | Second caption appears after the animation | Same painting and `GATE_305.FNT` |

The native renderer matches the complete original PPM **byte-for-byte** at
5,000ms (Legend lettering), 13,000ms (red bars), 14,500ms (finished red logo),
and 41,400ms (the spacecraft on its painted background). The first narrative
painting also matches the DOS frame **exactly outside the caption/window area**.
The current font reproduces the observed line wrapping closely, but differences
remain in glyph pixels, caption border/shadow and exact timing. No game text or art is embedded in
the source tree; the original data directory is required at runtime.

## First ten recovered narrative pages

These are DOS PIC reads and caption rectangles observed by comparing gray
panel bounds in full framebuffer dumps. Coordinates are 320×200 pixels.
`(x, y, width, height)` covers the panel, not its separate shadow. The page
text is sequentially indexed in the original executable (starting with 0).

| Page | Approx. start (ms) | PIC entry | Caption rectangle |
| ---: | ---: | --- | --- |
| 0 | 20,128 | `GATE_301` #5 | (12, 96, 297, 93) |
| 1 | 39,698 | `GATE_301` #5 | (12, 96, 167, 93) |
| 2 | 55,379 | `GATE_301` #0 | (12, 12, 147, 93) |
| 3 | 67,687 | `GATE_301` #0 | (12, 12, 297, 60) |
| 4 | 82,369 | `GATE_301` #1 | (162, 12, 147, 148) |
| 5 | 101,369 | `GATE_301` #2 | (182, 12, 127, 104) |
| 6 | 114,428 | `GATE_301` #2 | (182, 12, 127, 159) |
| 7 | 132,560 | `GATE_302` #33 | (132, 12, 177, 104) |
| 8 | 152,058 | `GATE_302` #33 | (142, 12, 167, 82) |
| 9 | 163,217 | `GATE_302` #33 | (142, 12, 167, 137) |

The first two pages include an animated spacecraft (`GATE_301` #6–#31),
reconstructed as independent positioned replacement frames against PIC #5.
The frame at 41.4s matches the original exactly. Later scene transitions load
`GATE_302.PIC` and `GATE_303.PIC` frames. Their full scheduler and compositing
remain to be reconstructed.

## Run and compare

With the Nix development shell or normal CMake installation:

```sh
cmake -S . -B build
cmake --build build --target gateway-intro gateway-intro-dump
./build/gateway-intro ../gateway-dos
./build/gateway-intro-dump ../gateway-dos --ms 22000 build/native-intro.ppm
```

The viewer plays the confirmed timeline; Space advances manually between the
ten implemented narrative pages, N toggles image filtering, and Escape exits
the viewer. Escape **does not yet launch the main game**. Audio/music and
the remaining eight narrative pages are still absent from the native version.
The existing Unicorn-based `gateway` executable remains the playable path.

Reference captures can be reproduced without SDL, Xvfb, or input injection
using the local static-Unicorn probe in `docs/workspace-setup.md`:

```sh
build/local-probe/gateway-probe ../gateway-dos/GATE.EXE \
  --headless --read-only --deterministic-clock --milliseconds 24000 \
  --dump-at 5000:build/intro-reference-5000 \
  --dump-at 22000:build/intro-reference-22000
```

All `build/` output and all originals remain outside Git. Research runs without
a private Xvfb use **no synthetic key or mouse events**.

## Remaining reconstruction tasks

1. Trace the launcher code controlling the timer, skip-to-next-page and
   Escape handoff. Current dates are observation timestamps and can vary with
   the guest clock/CPU model.
2. Recover the exact logo animation display schedule and the separately drawn
   title credit; PIC loading is currently known more precisely than display.
3. Match the original caption glyph rasterization, border, and drop shadow by
   pixel comparison; verify text wrapping of short and long paragraphs.
4. Verify all intermediate `GATE_301` spaceship frames and reconstruct the
   `GATE_302`/`303` transitions, then run a complete accelerated intro under a private Xvfb to map pages
   10–17. The current headless no-input probe has a 180s guard.
5. Route `GATE_004.MUS` through the existing OPL host path and integrate the
   native intro with the original numeric `GATEMAIN.EXE` handoff.

The goal is a **verified** intro implementation, not a timeline that merely
looks plausible. The ten-page player is a baseline, not a claim that the
introductory launcher has been fully decompiled.
