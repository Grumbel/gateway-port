# Source-C intro: full silent sequence, live playback and frame replay

The original `GATE.EXE:1302` is translated in `decomp/launcher/`, along with
its caption layout and font/drawing RTL callees. This chapter concerns the
**source-C path**, separate from the native `gateway-intro` C++ reconstruction.
The recovered original `GATE.EXE:1302` C entry now runs from start through
its final caption, with source-C image loading, original animation control
flow, source-C font/drawing, palette operations and time delays. An SDL3
live runner displays the in-progress VGA framebuffer and forwards Escape to
the original C input check; a separate deterministic recording/replayer
remains available for frame inspection. **Both are silent and start from a
captured DOS memory state**: music startup and the game handoff are pending.

The first executable C-only milestone is the complete set of eighteen
`GATE.EXE:117e` caption calls with their original arguments, font loading,
word wrapping, bevel frames, pixels, and wait-duration calculation. The
host bridge in `src/intro_source_c_captions.c` retains the original segmented
memory layout and calls the actual recovered C routines. It does not duplicate
their text rasterizer or invent caption text. A pinned private DOS RAM capture
provides relocated globals, source text and the initial data structures.

## Prepare the original snapshot

With the original DOS assets in `../gateway-dos`:

```sh
mkdir -p build/intro-analysis
build/local-probe/gateway-probe ../gateway-dos/GATE.EXE \
  --headless --read-only --deterministic-clock --milliseconds 24000 \
  --dump-at 22000:build/intro-analysis/at-22000
```

If the local probe has not been built, build it through the normal CMake or
Nix workflow first. `at-22000.bin` is a private 1 MiB guest-memory image,
not a file to commit or package. The host currently expects DS `0x2a7a` and
mode 3 from this capture; it will reject incompatible mode settings.

## Run the source-C captions

```sh
nix run .#intro-source-c -- build/intro-analysis/at-22000.bin ../gateway-dos
```

Space or Return advances, Backspace goes back, P pauses automatic advance,
F9 changes filtering and Escape exits. All eighteen original prose-panel
renderers are invoked. Their current standalone presentation has a diagnostic
grayscale index map; unlike the actual VGA output, it does not reproduce the
original palette or background images. On-screen timing uses the original
caption's C wait length sampled with a deterministic host clock.

For inspection or on a headless machine, export all eighteen 320x200 PGM
frames using only the C-source build:

```sh
mkdir -p build/source-c-pages
nix run .#intro-source-c-dump -- \
  build/intro-analysis/at-22000.bin ../gateway-dos build/source-c-pages
```

The source-C test exercises every caption with the real original font and
compares occupied-pixel totals against the established integration oracle:
**323,066 total nonzero pixels** over eighteen screens. It checks the
separate original caption wait durations too. Test screenshots are ignored
under `build/`; no narrative text or original assets enter the Git history.

## Play the original source-C intro live (silent)

```sh
nix run .#intro-source-c-live -- ../gateway-dos
```

This executes `GATE.EXE:1302` on a worker thread while an SDL3 window
displays the actual source-C VGA output at its 320×200 resolution and
the original 4:3 display aspect. The clock and delays follow host time.
Escape is injected as an original DOS key event: the recovered C code
takes its own interruption and cleanup branches. Space, Return, Backspace
and printable keys are also forwarded to the recovered C handler, including
its non-Escape prompt branch. P pauses the host clock and F9 toggles
filtering. Mouse input and extended DOS scan codes are not forwarded yet.

As in the recording mode below, the one-argument Nix app first uses the
Unicorn DOS runner to generate a private 22-second memory snapshot. An
existing snapshot may instead be given as a second argument (or before
`DOS-DIR`) to skip the initial emulation. The temporary snapshot is removed
on exit. The original C intro itself does not execute any x86 instructions.

The headless live-input regression injects Escape at 65 seconds of
simulated time, verifies it is consumed exactly once by the original C
sequence, and checks a clean return at about 69.7 seconds. It also injects
an ordinary key at 50 seconds and later Escape at 100 seconds, verifying
both are consumed by original C. There is no automated SDL window test in
this environment.

## Play the deterministic source-C recording (silent)

```sh
nix run .#intro-source-c-full -- \
  ../gateway-dos
```

This app executes the **actual recovered C `GATE.EXE:1302` function**,
including its original image, fade, caption and animation entry points.
Narrow native bridges supply VFX/DCL, RAM, VGA mode 13h, files, deterministic
clock and silent audio initialization. It first creates a private temporary
framebuffer/DAC recording, then replays those frames in SDL3 at their
simulated timestamps. With only the DOS directory argument, the Nix app
first uses the existing `gateway-probe` with deterministic Unicorn execution
to regenerate the same 22-second RAM snapshot. Only the *preparation* uses
8086 emulation; `EXE:1302` and its scene sequencing execute as source C.
The RAM image and recording are removed on exit. No game assets are packaged
in the Nix derivation. For subsequent quick runs, an existing private snapshot
can be passed as a second argument, or in the earlier `snapshot DOS-DIR`
order, to skip the probe.

Controls: Space/P pauses, Left/Right seeks five seconds, Home/End seeks to
the beginning/end, F9 toggles image filtering and Escape exits. These are
**replayer controls**, not original-game key interrupts; Space does not
execute the original skip branch. This replay is silent and does not launch
the game afterward.

To capture the full sequence without SDL3:

```sh
nix build .#intro-decompile
result/bin/gateway-intro-source-c-composite \
  build/intro-analysis/at-22000.bin ../gateway-dos \
  build/intro-final.ppm --full --record build/intro-full.gvr
```

The private `GVR1` format consists of four signature bytes followed by
records of: 32-bit little-endian virtual timestamp (ms), 64,000 VGA mode-13h
pixel indices, and 768 six-bit DAC components. Its 816 frames span 342,026
simulated milliseconds on the pinned RAM capture; two consecutive headless
runs produced byte-identical recordings. The source-only Nix package does
not require SDL3 or Unicorn. Keep `.gvr` files private like screenshots.

### Check against actual DOS frames

The image comparator searches the private source-C recording for the closest
color-accurate RGB match to a screenshot taken from the original Unicorn/DOS
guest. It reports the nearest **clock** frame separately from the best
**pixel** match, so large timing errors are visible:

```sh
python3 tools/compare_intro_frames.py --window-ms 400000 \
  build/intro-analysis/intro-full.gvr \
  40000:build/intro-analysis/original-40s.ppm \
  120000:build/intro-analysis/original-120s.ppm
```

An original probe snapshot can be obtained with `--headless --read-only
--deterministic-clock --milliseconds 80000 --dump-at
40000:build/intro-analysis/original-40s` (alongside the original `GATE.EXE`
argument). For snapshots past 180 seconds, the probe requires a key script
with its first event strictly **after** all capture times; no key was sent
before these reference frames.

In the local private-asset audit, **all seven** original 320×200 DOS
screenshots at 40, 60, 78, 120, 180, 240 and 300 guest seconds were
matched by a source-C frame at **64,000/64,000 identical RGB pixels**.
The matching C timestamps were 51.188, 77.730, 77.780, 123.248, 175.742,
267.018 and 321.088 seconds. The apparent offsets are not constant,
so these are image-equality oracles, **not** timing proof. At guest second
340 the original has already entered the 640×480 game loading screen and
displays a missing `RESTART.DAT` warning in this read-only test setup;
that is beyond the 320×200 source-C intro and cannot be compared here.

## Original source-C image loader milestone

The source-C image-load/draw boundary can also render the original
`GATE_300.PIC` resource #1 (270×38, positioned at x=24, y=81) without
running 8086 instructions:

```sh
nix run .#intro-source-c-picture-dump -- \
  build/intro-analysis/at-22000.bin ../gateway-dos build/source-c-picture.pgm
```

The command accepts `--resource ID MODE` to select original picture calls
from `EXE:1302`; for example, `--resource 0x3105 0` displays the first
large prose background. With `--rgb` at the end, it writes a true-color PPM
instead of indexed grayscale. The RGB data is produced by the recovered
`GATE.RTL:f61b` palette uploader and an explicit native VGA DAC port model;
the 6-bit VGA components expand to eight bits for the image file.
For resources that do not upload a palette (such as `0x301b`), standalone
RGB output lacks the palette inherited from preceding intro frames; it is
therefore not yet a complete color-accurate sequential playback.

To inspect inherited palette and guest-memory state across successive
picture calls, run the **persistent source-C picture-call sequence**:

```sh
mkdir -p build/source-c-sequence
nix run .#intro-source-c-picture-dump -- \
  build/intro-analysis/at-22000.bin ../gateway-dos \
  build/source-c-sequence --sequence
```

This executes thirteen selected original `EXE:1302`/`1a14` picture calls in
one source-C memory and DAC context and exports each completed framebuffer
as an RGB PPM. Unlike eleven separate `--resource` runs, this retains the
palette and file/resource caches between calls, including the palette
carried into resource `0x301b`. The regression checks the SHA-256 of all
thirteen exported frames. **This is deliberately an image-call subset, not
an attempt to fake the complete intro:** the original interleaves palette
fades, animation batches, source-C caption calls, waits and early key exits
between these calls. Repeated `0x3221` stages are equal here precisely
because those intervening operations are still absent.

Eleven independent original display-ID/mode combinations are currently
exercised against the private 22-second snapshot, each from a fresh copy
of that state. The regression suite pins every complete 320×200 pixel-index
frame by SHA-256, as well as the 0x3105 RGB PPM output. Calls with different
resource IDs can legitimately share pixels (e.g. `0x8100`/`0x3100`).

It runs the full recovered original `RTL:12b19` one-picture caller, including
`RTL:12c33` for resource selection/loading, `RTL:1266f` for pre-display state,
`RTL:12ed7` for drawing and the original C image descriptor and memory
allocation logic. The original snapshot carries an earlier pending DAC
update, so the standalone diagnostic resets that pending bit before loading
the isolated resource; this is a test-state normalization, not an original
source-C initialization sequence. The compressed DCL payload decoder (`0eec:018a`)
and the low-level VGA blitter (`24a7:000c`) are native host bridges, not
claimed as decompiled C. The 22-second reference produces **2,589 nonzero
pixels**, strictly within x=24..293 and y=81..118. A regression test checks
the entire 320×200 output fingerprint with private game data. This is an
image-path diagnostic, **not the original animation schedule**.

## Remaining limitations

- The original C entry uses an already relocated 22-second DOS RAM capture.
  The Nix wrapper generates it automatically, but independent source-C
  startup without a Unicorn bootstrap is not yet reconstructed.
- Original `RTL:c3bd` music setup is a silent host stub; no audio is replayed.
- The live SDL runner forwards Escape to the original source-C input check.
  Printable keys also reach the original C handler, though extended DOS
  scan codes and mouse services still need reconstruction. The offline `GVR1`
  replayer remains read-only and its keys are host controls.
- `GATE.EXE:1302` completes, but the game is not started afterward.
- Seven selected original DOS frames match source-C RGB output exactly,
  but a complete frame-by-frame comparison at equal game time remains open;
  the 16ms retrace model and other host delays are provisional.
