# Source-C intro: executable caption milestone

The original `GATE.EXE:1302` is translated in `decomp/launcher/`, along with
its caption layout and font/drawing RTL callees. This chapter concerns the
**source-C path**, separate from the already playable `gateway-intro` native
C++ reconstruction. The source-C path is **not yet a complete playable intro**.

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

## What remains before `GATE.EXE:1302` is playable

- Integrate the complete real startup sequence, including video and audio
  configuration. `RTL:c3bd` has only been partly recovered; silent music HLE
  needs an explicit service boundary.
- Connect the existing source-C resource image loader, palette effects,
  animations and original 18-caption ordering through `gate_exe_1302`, rather
  than rendering caption calls independently.
- Implement wall-clock and SDL input callbacks that preserve the original
  Escape/skip conditions, and correctly handle the full intro/game handoff.
- Validate each sequence boundary and rendered frame against the original
  deterministic DOS snapshots. Only then rename this milestone to a playable
  source-C intro.
