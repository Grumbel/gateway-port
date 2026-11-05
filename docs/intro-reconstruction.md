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
| 14.39–18.58 s | Final title and author's credit (visible from ~15.45s) | `GATE_300.PIC` #27 already contains 1,382 credit pixels in palette index 23; only that DAC entry changes from black to white |
| 18.58–20.13 s | First painted background, before caption | `GATE_301.PIC` #5 |
| 20.13–~323 s | Prose panels and animated scenery | `GATE_305.FNT` proportional 10px font; 18 NUL-terminated strings from file offset 0x428f in `GATE.EXE` |
| ~40–42.6 s | First caption clears; spacecraft flies across the painting | `GATE_301` #6–#31 preload at 40.19–41.17s; 26 painted sprite frames with ~55ms cadence |
| ~45.0–45.5 s | Second caption appears after the animation | Same painting and `GATE_305.FNT` |
| ~255.3–259.3 s | Fade to black, then a spacecraft approaches the station | `GATE_302` #0 with sprites #1–#32; captures at 256/256.5/257/257.5/258/258.5s match #2/#7/#12/#17/#22/#27 exactly |
| ~259.5–263.4 s | New station background and second spacecraft animation | `GATE_302` #33 with sprites #34–#102; captures at 260/260.5/261/261.5/262/262.5/263s match #37/#40/#43/#47/#50/#53/#57 exactly |
| ~263.7–268 s | Starfield and third spacecraft animation, then fade to black | `GATE_303` #0 with sprites #1–#41; captures at 264/264.5/265/266s match #7/#14/#24/#41 exactly |
| ~268.5–283.8 s | A fiery star scene with page 15 | `GATE_310` #4, pixel-identical background at 269 and 270s |
| ~284.4–323 s | Starfield with closing two panels | `GATE_303` #42; blank starfield matches the original at 284.5 and 285s |
| ~323–330 s | Launcher transfers to the main executable | The read-only original probe reaches a `RESTART.DAT` open failure on the subsequent loading screen |

The native renderer matches the original PPM **byte-for-byte** at 5,000ms
(Legend lettering), 13,000ms (bars), 14,500ms (red logo), 41,400ms (first
spacecraft), and most sampled frames of the later spacecraft animations.
The credit lettering is already stored in picture #27, initially black;
changing palette entry 23 through the observed ~85ms ramp reveals it without
rendering an additional font. The dialog's outer black/beveled border now
matches the DOS frame. Text glyph and line-break differences remain: at
22,000ms, 2,124/64,000 pixels differ, primarily inside the dialog, while
the underlying painting is pixel-exact. Original text and art are never
embedded in the project; the DOS directory is needed at runtime.

## All eighteen recovered narrative pages

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
| 10 | ~181,000 | `GATE_302` #33 | (142, 12, 167, 115) |
| 11 | ~197,000 | `GATE_301` #3 | (12, 12, 127, 159) |
| 12 | ~212,000 | `GATE_301` #4 | (62, 122, 247, 60) |
| 13 | ~222,000 | `GATE_301` #4 | (142, 96, 167, 93) |
| 14 | ~235,500 | `GATE_302` #0 | (122, 12, 187, 148) |
| 15 | ~271,000 | `GATE_310` #4 | (12, 63, 147, 126) |
| 16 | ~285,500 | `GATE_303` #42 | (12, 12, 297, 82) |
| 17 | ~302,000 | `GATE_303` #42 | (12, 12, 297, 126) |

The page boundaries after page 9 were bracketed at one- or two-second
intervals with independent DOS framebuffer captures. The new values are
interpolated between those bounds, not recovered source-code timer constants.
The early spacecraft (`GATE_301` #6–#31) and later `GATE_302`/`303` sprites
compose by drawing their original positioned pixels over a full background.
Many such composites match complete original frames exactly, including
all 13 sampled anchors between 256 and 263 seconds. Intermediate frames
are interpolated from these observed pairs; this is **output reconstruction**,
not yet a decompilation of the original timer and input loops. `GATE_303`
#43–#79 is present in the archive but its playback trigger is unconfirmed.

## Run and compare

With the Nix development shell or normal CMake installation:

```sh
cmake -S . -B build
cmake --build build --target gateway-intro gateway-intro-dump
./build/gateway-intro ../gateway-dos
./build/gateway-intro-dump ../gateway-dos --ms 22000 build/native-intro.ppm
```

The viewer plays the observed timeline; Space advances manually among all
eighteen narrative pages, N toggles image filtering, and Escape exits.
Escape **does not yet launch the main game**. Audio/music and some animation
timing are still absent from the native version.
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
a private Xvfb use **no synthetic key or mouse events**. The extended runs
beyond the probe's 180-second no-input guard were run on a private Xvfb using
one placeholder keyboard record scheduled **after every relevant capture**;
the images themselves were recorded before any synthetic input.

For example, with `build/intro-analysis/boundaries.keys` containing
`324000:0000`, the following captures the later animation without input before
the snapshots:

```sh
xvfb-run -a build/local-probe/gateway-probe ../gateway-dos/GATE.EXE \
  --headless --read-only --deterministic-clock \
  --milliseconds 325000 --key-script build/intro-analysis/boundaries.keys \
  --dump-at 258000:build/intro-analysis/at-258000 \
  --dump-at 262000:build/intro-analysis/at-262000
./build/gateway-intro-dump ../gateway-dos --ms 262000 build/native-262000.ppm
cmp build/native-262000.ppm build/intro-analysis/at-262000.ppm
```

Here `gateway-intro-dump` denotes the installed/CMake binary; the manual
compiler fallback lives under `build/intro-analysis/` instead.

## Remaining reconstruction tasks

1. Trace the launcher code controlling the timer, skip-to-next-page and
   Escape handoff. Current dates are observation timestamps and can vary with
   the guest clock/CPU model.
2. Recover the exact logo-delta animation display schedule. The completed
   title and its palette-index-23 credit reveal are recovered, including the
   original 6-bit palette ramp at 10ms intervals.
3. Match caption glyph placement and line wrapping at several panel widths.
   The outer black/beveled rectangle now matches the original.
4. Derive the actual `GATE_302`/`303` timer loops, fill gaps between the
   sampled frames, and verify fade states around 259.5s. All 18 panel bounds
   are known; their exact timing is only bracketed for the last eight.
5. Route `GATE_004.MUS` through the existing OPL host path and integrate the
   native intro with the original numeric `GATEMAIN.EXE` handoff.

The goal is a **verified** intro implementation, not a timeline that merely
looks plausible. The eighteen-page reconstruction has a complete narrative
outline and many pixel-exact animation checkpoints; it is **not** a complete
decompilation of the launcher yet.
