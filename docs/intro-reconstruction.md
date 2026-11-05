# Gateway launcher intro: native reconstruction

**Current priority: source-faithful decompilation.** See
[`decomp/launcher/README.md`](../decomp/launcher/README.md) for the original
`GATE.EXE` routines translated to C. The framebuffer-based presentation
below remains useful as a regression oracle, but reconstructing the original
control flow and data structures takes precedence over matching more
individual pixels.

This is a **separate** program from the game's command/verb loop. It belongs
to the small launcher `GATE.EXE`, not `GATEMAIN.EXE` or its story overlays.
The native prototype in `src/story/intro_presentation.*` reads the original
resources and renders a fixed-clock sequence without Unicorn. The SDL3 viewer
is `gateway-intro`; `gateway-intro-dump` renders one frame to PPM without SDL.

## Original compiled control flow (GATE.EXE)

The launcher does **not** interpret a separate intro script. Its introductory
sequence contains exactly **18 direct calls** to a common far function at
original `GATE.EXE:0x117e`, between call sites `0x15c4` and `0x19fe`. Each
call pushes a pointer to one of the 18 consecutive source strings and three
placement arguments: requested left edge, preferred top edge, and right edge.
The first call specifies `(10,120,310)`; the last specifies `(10,10,310)`.
These are input arguments to the layout function, **not** the final drawn
caption rectangles. The first string begins at original file offset `0x428f`.

The following original file offsets have bounded disassembly evidence:

| Original identity | Recovered purpose | Confidence |
| --- | --- | --- |
| `exe:0x0e37` | Poll keyboard; consume a pending key; Escape (`0x1b`) returns nonzero to the caller's common end path (`exe:0x1a09`). Other ordinary keys return zero after consumption once the intro has started. Also checks the mouse. | Disassembly + separate key injections |
| `exe:0x0f18` | Add a 16-bit delay argument to the 32-bit clock returned by the RTL helper, then wait until the deadline or pending keyboard/mouse input. The keyboard is *not* consumed by this wait. | Disassembly |
| `exe:0x103a` | Scan and measure words, wrap at right edge, calculate lower edge and shift overly tall boxes upward to fit the viewport. | Disassembly; drawing details not fully matched |
| `exe:0x117e` | Show a framed caption, draw its words, then calculate and wait for the text's display time. | Disassembly + original output |
| `exe:0x1288–0x1292` | Call the far RTL string printer once for each measured/wrapped word of a caption. | Disassembly + incomplete DOS caption capture |
| `rtl:0x08298` | Far `strlen` implementation, returning the exact NUL-terminated text length. | Disassembly (`repnz scasb`) |
| `rtl:0x0ed7e` | Print a NUL-terminated string one character at a time, calling the glyph rasterizer. | Disassembly of `GATE.RTL` |
| `rtl:0x0edff` | Rasterize each glyph in row-major bitmap-bit order; make a far pixel-write call for every set bit at `rtl:0x0ef79–0x0ef88`. | Disassembly + differential framebuffer |

At `exe:0x12e3`, the caption routine calls that RTL `strlen` helper; at
`exe:0x12f1`, it multiplies the returned 16-bit length by 45, adds 2,000, and
calls the interruptible wait (`exe:0x0f18`). The original timeout is therefore
`uint16_t(2000 + 45 * strlen(text))` milliseconds. For example, page 0 has
387 bytes and a 19,415ms text timeout; page 17 has 413 bytes and a 20,585ms
timeout. Image loading, animation playback, and explicit scene delays add
additional time between caption starts. The native C++ presentation exposes
`page_display_duration_ms()` using this recovered expression; its full frame
timeline still includes some sampled/interpolated boundaries.

The displayed panel positions also follow a consistent transformation of the
original placement arguments. For requested `left`, `preferred_top`, `right`
and the reconstructed number of wrapped lines, the drawn bounds are:

```text
line_height = font.height + 1 = 11
height = 5 + line_height * wrapped_lines
box_left = left + 2
box_width = right - left - 3
box_top = min(preferred_top + 2, 189 - height)
```

Word widths come from `GATE_305.FNT`'s glyph widths and left/right spacing
bytes. The native layout wraps whole words at `box_width - 8`, consumes runs
of spaces as a single word separator, and preserves one leading space after
an explicit newline while dropping it after an automatic wrap. The glyph ink
origin uses the **upper spacing nibble minus one**; the earlier lower-nibble
interpretation drew some characters one pixel too far right. The recovered
layout yields exactly the *observed* number of lines
for every original paragraph: `8,8,8,5,13,9,14,9,7,12,10,14,5,8,13,11,7,11`.
Thus **all 18 rectangle positions and sizes** now come from the original
`GATE.EXE` arguments and font data; no captured rectangle heights are stored
in the implementation. The tests retain all original measured rectangles as
independent regression expectations. The native glyph rasterization is now
pixel-exact at all tested, fully drawn caption states; see the differential
results below.

To inspect all original call sites without embedding any source text:

```sh
python3 tools/inspect_intro_program.py ../gateway-dos/GATE.EXE
python3 tools/inspect_intro_program.py ../gateway-dos/GATE.EXE --json
python3 tests/intro_program_tests.py
```

The same original machine-code scan finds five **picture-sequence** calls to
linked far target `0xf10:0x1466`. That target is a runtime address, not an
original RTL file offset. Their three immediate arguments identify the base
picture, first changed entry, and frame count:

| Original call file offset | Resource | Base | First changed | Count |
| --- | --- | ---: | ---: | ---: |
| `exe:0x1743` | `GATE_303.PIC` | 42 | 43 | 37 |
| `exe:0x1762` | `GATE_302.PIC` | 33 | 61 | 42 |
| `exe:0x191f` | `GATE_302.PIC` | 0 | 1 | 32 |
| `exe:0x1933` | `GATE_302.PIC` | 33 | 34 | 27 |
| `exe:0x1947` | `GATE_303.PIC` | 0 | 1 | 41 |

The program also calls `exe:0x1c11` seven times with indices
`1,6,4,7,2,5,3`. Disassembly of that function shows a 64-step palette
brightness ramp at **DAC index `45 + 3 * argument`**, followed by a 500ms
interruptible delay. It writes only the **red** channel of one DAC entry
each iteration. The strokes are 9×38 pixels, arranged left-to-right as
indices 1–7, but they appear in **compiled palette-call order**, not
left-to-right. Their time-varying color now uses the six-bit VGA DAC
and the original probe's six-to-eight-bit conversion.

Read-only captures at 20ms intervals from 8.45s through 12.55s establish
a nearly uniform call cadence of **570 guest milliseconds** beginning at
approximately 8,578ms; each 64-write ramp takes approximately **77ms**.
These timings are measurements of the deterministic original emulator,
not explicit delay operands in the machine code (the source explicitly
waits 500ms *after* the ramp). A compact native model using the original
seven-call order and those two measured timing constants reproduces
**201/206 original framebuffer captures byte-for-byte**. The five remaining
samples, at 9,190, 9,730, 9,790, 10,330 and 10,350ms, differ by one DAC
level on exactly one 342-pixel bar, reflecting the unthrottled original loop's
instruction-level timing. This is much stronger coverage than the earlier
500ms snapshots but is not a cycle-exact palette-loop reimplementation.
The compiled sequence also establishes `GATE_303` #43–#79; its precise
runtime timing has not yet been established.

In read-only deterministic runs on private Xvfb, a Space, Enter, or letter
pressed at 22,000ms produces page 1 by 27,000ms, whereas no key leaves page 0
visible. Escape at the same point takes the common end path and reaches the
main game's loading screen; the read-only test then fails at the expected
`RESTART.DAT` write. Inputs during the opening logo did not advance beyond
the ordinary first page by 26,000ms. This establishes the high-level input
boundary but not yet a cycle-exact keyboard/event scheduler.

## Observed opening

The following results come from the unmodified, SHA-256-pinned `GATE.EXE`
in `docs/executable.md`, run **read-only** under the headless probe with
`--deterministic-clock`. Times are guest milliseconds, not real-world video
timestamps. File reads are *not* necessarily draw calls.

| Guest time | Observed output | Resource evidence |
| ---: | --- | --- |
| 0–3.27 s | White 640×480 screen | Intro starts in VGA mode 12h |
| 3.27–7.54 s | Black Legend lettering on white | `GATE_201.PIC` #0; frame at 5s is pixel-exact in native renderer |
| 7.54–12.57 s | Black 320×200 screen; seven red bars brighten in order 1,6,4,7,2,5,3 | Mode 13h; `GATE_300.PIC` #0 at 7.539s; 9×38 bars at x=24,64,105,149,194,242,284, y=81 |
| 12.57–13.15 s | Bars remain visible while resources are loaded | `GATE_300.PIC` #1–#26 (XOR deltas) read over ~580 ms |
| 13.2–14.39 s | Red logo reveal animation | PIC delta composition, native display schedule *approximate* |
| 14.39–18.58 s | Final title and author's credit (visible from ~15.45s) | `GATE_300.PIC` #27 already contains 1,382 credit pixels in palette index 23; only that DAC entry changes from black to white |
| 18.58–19.13 s | Black screen after the title clears | Original black frame at 19s |
| ~19.13–20.13 s | First painted background, before caption | `GATE_301.PIC` #5 followed by an explicit 1,000ms delay at `exe:0x159d` |
| 20.13–~323 s | Prose panels and animated scenery | `GATE_305.FNT` proportional 10px font; 18 NUL-terminated strings from file offset 0x428f in `GATE.EXE` |
| ~40–42.6 s | First caption clears; spacecraft flies across the painting | `GATE_301` #6–#31 preload at 40.19–41.17s; 26 painted sprite frames with ~55ms cadence |
| By 45.0 s | Second caption appears after the animation | Same painting and `GATE_305.FNT`; 45.0s caption frame is pixel-exact |
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
matches the DOS frame. The glyph bearing and whitespace corrections eliminate
the previous 2,124-pixel mismatch at 22,000ms. The 19,000ms black frame and
45,000ms second-caption boundary are now matched too. Across **87 independent
DOS captures** from 19 to 322 seconds, the native renderer matches **85
byte-for-byte**. Two non-matching timestamps remain: 265,500ms (six transient
black pixels outside the chosen spacecraft sprite's rectangle), and
303,000ms (the original text has not finished drawing). At `exe:0x1288`
the original caption loop calls the RTL string drawer once **per word** after
measuring and wrapping it; partial text is therefore an in-progress native
render, not a distinct intro text record. Twenty-three separate, pixel-exact
frames, including intermediate logo DAC values, are protected by
source-independent RGB fingerprints in the native tests. Original text and
art are never embedded in the project; the DOS directory is needed at runtime.

### Mid-update VGA palette

At 259,500ms the previously mysterious 5,089-pixel color difference is
explained by the original program updating the VGA DAC **sequentially**.
Comparing palette indices from the original `GATE_302` #33 picture against
the original DOS framebuffer shows all used indices up through 32 at **9/16**
of the source six-bit DAC value, and all used indices from 34 onward still at
**8/16**. No pixel uses index 33 in that picture, so we cannot distinguish
whether the original DAC loop had reached it at this exact instant. The
native renderer now adjusts colors by *palette index*, retaining the two
brightness steps within one frame. Its 259,500ms PPM now matches all 64,000
pixels, independently protected by an RGB fingerprint. Adjacent transition
times remain approximated until the palette update's exact scheduling is
recovered from the code.

### Partially rendered text: original RTL pixel-write order

The text is **not** drawn atomically. The original call at `exe:0x1288`
hands one word to the RTL printer, which loops over characters and then over
each glyph's bitmap rows and columns, issuing an individual pixel-write call
for each set bit. A framebuffer captured in the middle of this operation can
therefore contain an *incomplete glyph*, not merely fewer complete words.

The native reconstruction now offers two independent diagnostic boundaries:
`page_frame_words(page, count)` renders the first `count` complete words;
`page_frame_ink_pixels(page, count)` renders the first `count` visible black
pixel writes in the same original traversal order. Neither uses hard-coded
intro text, images, or memory dumps. The latter matches the independently
captured original **303,000ms framebuffer byte-for-byte** with **2,627** ink
writes into the page-17 caption (72 words, 4,127 distinct final ink pixels).
That frame is covered by a standalone original RGB fingerprint test.

An additional **26 consecutive original captures** at 100ms intervals from
301,500 through 304,000ms establish that the old scene is still visible at
302,900ms, the last caption is partially rendered at 303,000ms, and its text
is complete by 303,100ms. Thus the entire remaining draw completes within
that last 100ms interval. However, those observations alone do not establish
the precise instruction-count-to-guest-clock relationship or the exact
starting instant. The ordinary `frame_at()` preview still paints full text
on its approximate page boundary and therefore **does not yet reproduce**
the 303,000ms transitional frame; no interpolation is claimed to be original
control flow.

## All eighteen recovered narrative pages

These are DOS PIC reads and caption rectangles first observed by comparing gray
panel bounds in full framebuffer dumps and now independently reproduced from
the original executable's caption arguments and the font. Coordinates are 320×200 pixels.
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
#43–#79 is confirmed as a compiled picture-sequence call at `exe:0x1743`,
though its precise runtime timing has not been established.

## Run and compare

With the Nix development shell or normal CMake installation:

```sh
cmake -S . -B build
cmake --build build --target gateway-intro gateway-intro-dump
./build/gateway-intro ../gateway-dos
./build/gateway-intro-dump ../gateway-dos --ms 22000 build/native-intro.ppm
./build/gateway-intro-dump ../gateway-dos --ms 303000 build/intro-mid-glyph.ppm --ink-pixels 2627
```

The viewer plays the observed timeline; ordinary keys manually advance among
all eighteen narrative pages, F9 toggles image filtering, and Escape exits.
Escape **does not yet launch the main game**. Audio/music and some animation
timing are still absent from the native version. Manual advancement in this
preview selects the next static panel; the original's conditional animation
transitions after an early key press have not been reproduced yet.
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

1. Complete the launcher control flow around the now-identified `exe:0x0e37`
   input check, `exe:0x0f18` interruptible timer, and `exe:0x1a09` handoff.
   Derive each scene's explicit delays and conditional animation paths, then
   replace approximate page boundaries with a native sequential scheduler.
2. Recover the exact logo-delta animation display schedule. The seven
   palette-bar ramps now match 201/206 dense 20ms samples and the final
   title's palette-index-23 credit reveal is recovered. The PIC XOR
   delta-frame cadence at 13.2–14.39s remains interpolated.
3. Calibrate the *time* of caption pixel writes, now that their original
   row-major order and the mid-glyph 303,000ms state match exactly. Narrow
   the 302,900–303,100ms transition interval or recover instruction-count
   timing from a resumable guest state, then connect the verified partial
   raster to the sequential scheduler. Font rasterization, whitespace,
   all eighteen rectangles, and completed caption pixels already match.
4. Derive the actual `GATE_302`/`303` timer loops, fill gaps between the
   sampled frames, and verify intermediate DAC palette-update states beyond
   the now-exact 259.5s snapshot. Investigate the six black pixels at 265.5s
   outside the frame-34 sprite bounds. All 18 panel bounds are known; their
   exact timing is only bracketed for the last eight.
5. Route `GATE_004.MUS` through the existing OPL host path and integrate the
   native intro with the original numeric `GATEMAIN.EXE` handoff.

The goal is a **verified** intro implementation, not a timeline that merely
looks plausible. The eighteen-page reconstruction has a complete narrative
outline and many pixel-exact animation checkpoints; it is **not** a complete
decompilation of the launcher yet.
