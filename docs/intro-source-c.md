# Source-C intro: live AdLib playback and deterministic frame replay

The original `GATE.EXE:1302` is translated in `decomp/launcher/`, along with
its caption layout and font/drawing RTL callees. This chapter concerns the
**source-C path**, separate from the native `gateway-intro` C++ reconstruction.
The recovered original `GATE.EXE:1302` C entry now runs from start through
its final caption, with source-C image loading, original animation control
flow, source-C font/drawing, palette operations and time delays. An SDL3
live runner displays the in-progress VGA framebuffer and forwards Escape to
the original C input check; a separate deterministic recording/replayer
remains available for frame inspection. The **live** SDL3 path now plays
the original `GATE_004.MUS` song zero using a native AdLib MIDIPlay decoder
and the repository's Nuked OPL3 synthesizer. The deterministic frame replay
remains silent. **Both start from a captured DOS memory state**: the original
DOS music IRQ driver and independent startup C are pending. A separate hybrid
launcher can now enter the original game after this source-C intro, but does
not yet carry the reconstructed guest state across that boundary.

The complete intro now executes the recovered original `GATE.RTL:f14d`
display-mode C routine at its original linked FAR call. The native BIOS/VGA
adapter supplies detected VGA type 5, a capable default monitor subtype,
and initial mode `13h` to `f14d`, which establishes the renderer descriptor, selects
mode `13h`, and configures the four clipping edges and clipping enable.
Those clipping writes were removed from the source-C host's initial setup.
The existing 816-frame replay and final guest-RAM checks remain byte-exact;
this still does not reconstruct the earlier DOS startup responsible for
building the linked tables and relocating the C runtime.

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

### Profile the remaining startup-memory dependencies

The source-C memory host can record guest FAR reads that happen **before**
the corresponding RAM byte is written by its emulated FAR-memory interface:

```sh
nix build .#intro-decompile
./result/bin/gateway-intro-source-c-composite \
  build/intro-analysis/at-22000.bin ../gateway-dos \
  build/intro-analysis/full-profile.ppm \
  --full --read-deps build/intro-analysis/initial-reads.txt
```

`initial-reads.txt` records physical 4 KiB page addresses, byte counts, and
**41 exact first-read byte ranges**, never the proprietary RAM values. Ranges
use hexadecimal physical offsets with exclusive ends; the output refuses to overwrite an
existing file. The default 22-second snapshot produces **7,429 first-read
bytes across seven pages** (physical page bases `00000`, `1b000`, `2a000`,
`2b000`, `2d000`, `2e000`, `2f000`). The original full intro's 816-frame
recording and output hashes are unchanged. A regression additionally zeroes
all other 249 pages of the original 1 MiB RAM image in a private temporary
copy, reruns the source-C intro, and obtains the **same complete 816-frame
recording byte-for-byte**. Thus the current observed path only requires
28 KiB of observed *page-granularity* startup state. Further ablation of
those seven pages established that zeroing the `2a000` page as well still
preserves the entire recorded replay, leaving just **six pages (24 KiB)**
necessary to reproduce this pinned visual run. That does not prove the
removed page is irrelevant to every side effect or input path.

One substantial dependency is already accounted for by the original files:
the `2dddb:2f15e` first-read range (4,995 bytes) is part of a contiguous
**5,869-byte exact copy of `GATE.EXE` data**. The captured RAM interval
`2dd9d:2f48a` matches original file offsets `04251:0593e` byte for byte.
The original-asset test verifies this mapping without including those bytes
in the repository. A future native startup can load this region from the
original EXE instead of capturing it through Unicorn; other observed reads
(including DOS vectors, relocated code and runtime globals) still require
reconstruction.

The standalone composite now offers an opt-in native restoration step:

```sh
./result/bin/gateway-intro-source-c-composite \
  build/intro-analysis/at-22000.bin ../gateway-dos \
  build/intro-analysis/from-exe.ppm --full --restore-exe-data
```

The adapter reads the verified 5,869-byte region from the user's original
`GATE.EXE` and restores it in guest RAM after loading the snapshot, before
executing source C. In the integration test, **that entire RAM region and
all other unobserved memory are first zeroed** in a private snapshot. Native
restoration still reproduces every one of the 816 frame/palette/timing
records and the final picture byte-for-byte. Missing, truncated, or
non-MZ executable inputs are rejected. This code only eliminates one
static-data dependency on the capture; it is not a complete initializer and
does not yet change the default SDL live launcher.

The other 768-byte run at RAM `1b77c:1ba7c` maps to original `GATE.RTL`
file bytes `0d7bf:0dabf`: **722 of the 768 bytes match exactly**, and the
remaining 46 bytes are **23 paired 16-bit loader fixups**, each replacing two
zeros in the RTL file. This mapping is also verified in the test. It
identifies the raw code's source but does not yet recover the relocator that
must generate those words during a cold C startup.

The independent `--full --restore-rtl-data` option now copies nonzero bytes
from that original RTL region into the guest image, leaving zero relocation
placeholders (and hence the 23 already-linked words from the private guest
capture) untouched. It rejects a missing or wrong-size original RTL file.
This can be combined with `--restore-exe-data`.

The initial 768-byte RGB palette in RAM `2b77c:2ba7c` is also an exact copy
of the palette attached to **`GATE_301.PIC` directory entry 5** (resource
`0x3105`, palette file offset `025d27`). The independent
`--full --restore-palette` option reads that palette from the user's original
PIC directory, validates the entry class and resource bounds, and restores
the startup buffer. It can be combined with both original-file restorations;
a missing or malformed PIC fails safely. The other startup variants and
palette changes made during playback are not inferred from this one entry.

Most importantly, a byte-level sparse-memory regression starts with **all
1 MiB zero**, preserves only the 7,419 guest bytes first-read before writes
in the pinned default replay (excluding the ablated `2a000` page), and
then removes all three disk-reconstructible regions. Only **206 nonzero bytes
of captured RAM** remain. All three native restorations together reproduce
the complete 816-frame recording and final PPM, byte for byte. This counts
the remaining *nonzero values*, not a self-contained serialized bootstrap:
DOS interrupt-vector state, 23 RTL fixups and some runtime descriptor
pointers still need native initialization. The
captured data is neither committed nor copied into the output bundle.

This is a narrow dependency oracle, **not proof that all memory accesses are
profiled**: the host tracks `gate_far_byte` and `gate_far_set_byte` (including
WORD accessors), but not arbitrary direct native reads of the RAM array,
external file resources, native stack views, or direct VGA planes. Host VGA
initialization is explicitly marked as a write to avoid treating all 64,000
cleared framebuffer bytes as original inputs. The 28 KiB subset is valid for
the pinned full default intro replay; different settings, input sequences,
or untracked direct accesses still need examination before replacing the
Unicorn-generated startup snapshot.

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

## Play the original source-C intro live (with AdLib music)

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

The original AdLib song is read directly from `GATE_004.MUS` in the same DOS
directory, so no extraction step or proprietary files in the Nix store are
needed. The native host parses the MIDIPlay instrument events, note timing,
channel pressure and rhythm-mode controls and sends the resulting PCM audio
to SDL3. The original `GATE.EXE:134c` startup call now notifies the SDL
renderer **only when the original `RTL:c3bd` audio-enable predicates hold**.
The source-C code and its notification are covered by the live-input regression;
music no longer starts merely because the SDL window was created. P pauses
both the source-C clock and the audio device. The source-C intro sequencing
is unchanged; the original `RTL:c3bd` IRQ8 music loader is not reproduced by
this host bridge. Note and patch parsing is tested, but
**audible SDL3 playback and precise synchronization against original DOS
recordings have not been checked in this environment**.

The headless live-input regression injects Escape at 65 seconds of
simulated time, verifies it is consumed exactly once by the original C
sequence, and checks a clean return at about 69.7 seconds. It also injects
an ordinary key at 50 seconds and later Escape at 100 seconds, verifying
both are consumed by original C. There is no automated SDL window test in
this environment.

## Continue from the source-C intro into the original game

```sh
nix run .#intro-source-c-play -- ../gateway-dos
```

This optional mode uses the same live, music-enabled source-C intro. When
`EXE:1302` returns, it closes the SDL intro window and runs the original
`GATEMAIN.EXE` through the existing Unicorn/SDL3 game runtime. Escape skips
the remaining intro using the original source-C input logic and continues to
the game; closing the window cancels both programs. The intro-only
`intro-source-c-live` app above retains its inspection window after completion.

The game command-line parameters `0 258 9 2 816 1 7 544` were checked against
the original DOS launcher's `4b03` overlay transition and preserved child PSP
command tail for the default VGA/AdLib setup. They allow the original main
program to initialize its files and load the opening room without replaying
the whole DOS intro in Unicorn. The main runtime creates game state such as
`RESTART.DAT` under its configured writable state directory, not in the
original DOS asset directory.

**This is a hybrid transition, not a memory-identical overlay replacement.**
The source-C host does not yet transfer its 1 MiB post-intro guest memory,
the live CPU register/segment state, or DOS open handles to the main game.
`GATEMAIN.EXE` instead starts in a fresh emulated process with the captured
launcher arguments. Non-default original hardware/configuration choices
have not been mapped to corresponding argument values. The SDL3 wrapper
and its complete user-visible handoff cannot be exercised in this headless
development environment; only the standalone game startup was tested.

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
  build/intro-final.ppm --full --record build/intro-full.gvr \
  --dump-memory build/intro-post-ram.bin
```

The private `GVR1` format consists of four signature bytes followed by
records of: 32-bit little-endian virtual timestamp (ms), 64,000 VGA mode-13h
pixel indices, and 768 six-bit DAC components. Its 816 frames span 342,026
simulated milliseconds on the pinned RAM capture; two consecutive headless
runs produced byte-identical recordings. The source-only Nix package does
not require SDL3 or Unicorn. `--dump-memory` saves the complete **1 MiB RAM
at the return of original C `EXE:1302`**, *before* the host shuts down its
file/video services. The output must not already exist; this prevents an
accidental overwrite of the input snapshot or DOS assets. Its SHA-256 for the
pinned guest state is
`91703bb67fd1f07cf54aa9797e8d41700f68aa7aa4cebbb100e4c0424ee93c79`;
only that hash is tracked. The new RAM output is a **data snapshot**, not a
resumable CPU savestate: it lacks the original live 8086 registers/stack and
the launcher-to-game overlay loader's continuation. Keep `.gvr` and `.bin`
files private like screenshots.

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
- Original `RTL:c3bd` IRQ8 music setup remains a native host boundary, but
  live SDL3 playback now uses the original MIDIPlay/MDI stream and Nuked OPL3.
  The separate deterministic `GVR1` replayer is still silent, and output/timing
  accuracy has not been checked against real DOS audio.
- The live SDL runner forwards Escape to the original source-C input check.
  Printable keys also reach the original C handler, though extended DOS
  scan codes and mouse services still need reconstruction. The offline `GVR1`
  replayer remains read-only and its keys are host controls.
- `intro-source-c-play` enters the original game via a fresh Unicorn process;
  it does not preserve the DOS overlay's process memory or CPU state.
- Seven selected original DOS frames match source-C RGB output exactly,
  but a complete frame-by-frame comparison at equal game time remains open;
  the 16ms retrace model and other host delays are provisional.
