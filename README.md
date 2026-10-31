# Gateway Linux port

Incremental C++ port of Frederik Pohl’s Gateway to Linux, using SDL3, CMake,
and Nix. The first target is execution of the original real-mode code with DOS
and hardware interfaces replaced by host implementations. Recovering the data
formats and replacing the original interpreter is the long-term goal.

## Current status

This is an original-code runtime, **not yet a playable port**. It provides:

- A checked DOS MZ loader with segment relocation and initial register setup.
- `gateway-inspect`, which reports loaded image and entry information.
- `gateway-probe`, a bounded Unicorn real-mode startup probe that traces DOS
  services and stops explicitly on unsupported calls.
- A tested DOS memory arena and planar VGA model with framebuffer dumps.
- `gateway`, an SDL3 runtime presenting guest VGA output and streaming AdLib PCM.
- `gateway-host`, a separate SDL3 graphics/audio diagnostic.
- `gateway-extract`, which converts PIC files to indexed PNGs.

Original game files remain external in `../gateway-dos/` and are never needed
for package builds or unit tests. The probe opens game files read-only unless given `--state-dir`; the SDL runtime
keeps writes in a separate persistent state directory.

## Build and run

From this directory:

```sh
nix develop
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/gateway-inspect ../gateway-dos/GATE.EXE
./build/gateway-probe ../gateway-dos/GATE.EXE
./build/gateway-host
```

Build a package with `nix build`; check it with `nix flake check`. Use `path:.`
while testing new untracked files that Git-backed flakes would otherwise omit. The flake locks
nixpkgs and provides SDL3, Unicorn, the compiler, CMake, and research tools.

For loader-only development without SDL3 or Unicorn:

```sh
cmake -S . -B build-core -DGATEWAY_WITH_SDL=OFF -DGATEWAY_WITH_UNICORN=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

The host diagnostic displays color bars and queues a short 440 Hz test tone.
Escape or closing the window exits. A bounded headless API check is available:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/gateway-host --smoke-test
```

This checks initialization and submission, not audible output or visible pixels.
The probe is limited to ten million instructions and two seconds of CPU execution.
It reports unsupported services rather than silently returning fabricated success.

## Extracting pictures

`gateway-extract` writes `DIR/<file>/<entry>.png` and needs neither SDL3 nor Unicorn.
It reads Gateway and the three Spellcasting games, and detects the format per file:

```sh
./build/gateway-extract -o build/pictures/gateway ../gateway-dos/*.PIC
./build/gateway-extract -o build/pictures/s101 ../101/*.PIC
```

Pass all of a game's files together, because some rely on a palette from an earlier
file. XOR overlays are merged onto their picture; `--raw-overlays` keeps the deltas.
`--dialect s101|s201|gateway` overrides detection. The exit status is non-zero if any
entry fails to decode.

## Research

See [executable findings](docs/executable.md), [data file formats](docs/data-files.md),
the [Spellcasting comparison](docs/spellcasting.md), the
[ScummVM engine comparison](docs/scummvm-legend.md), and [next steps](TODO.md).
The neighboring EF2000 port informs the external-data, system-boundary, and
reference-testing approach. Its protected-mode native harness is not directly
applicable to Gateway’s real-mode startup.

[Risca’s Death Gate resource manager](https://github.com/Risca/dgate_resource_manager)
is a format-research reference. Gateway shares its string compression and PIC
directory layout but differs elsewhere; see [the comparison](docs/data-files.md#comparison-with-death-gate).

To retain a snapshot at the startup boundary:

```sh
./build/gateway-probe ../gateway-dos/GATE.EXE --dump-prefix build/gate
```

This writes `build/gate.bin` (guest memory) and `build/gate.ppm` (VGA pixels),
including on unsupported-service stops. These are diagnostic artifacts, not
proof that the rendered game output is correct.

## Running original code with SDL3

```sh
./build/gateway ../gateway-dos/GATE.EXE
# Or, from the installed Nix package:
nix run . -- ../gateway-dos/GATE.EXE
```

Close the window to stop. Escape, Space, Enter, letters, digits, arrow keys,
and common punctuation are forwarded to the guest BIOS keyboard queue. Mouse
input, normal DOS child EXEC, and broader gameplay remain pending. Escape now
loads the main executable through the original overlay trampoline; main startup
creates `RESTART.DAT` in writable state and currently reaches DOS file enumeration. The intro reaches
its initial Legend title text; this does not establish complete intro playback.

For a bounded capture (1–60000 ms of virtual guest time):

```sh
./build/gateway ../gateway-dos/GATE.EXE --milliseconds 5000 --dump-prefix build/intro
./build/gateway ../gateway-dos/GATE.EXE --headless --milliseconds 5000 --dump-prefix build/intro
```

The scheduler currently advances 1 ms per 5,000 guest instructions (5 MIPS),
using 16 ms presentation/audio blocks. Timing remains provisional. Dummy-driver
integration tests exercise real guest VGA instructions through the SDL runtime.

Writable game state defaults to `$XDG_STATE_HOME/gateway-port`, or
`$HOME/.local/state/gateway-port` when XDG state is unset. Override it with
`--state-dir DIR`; use `--read-only` for a run without guest writes. Original
assets remain unchanged, including when a writable name shadows an asset.
For a probe with local state:

```sh
./build/gateway-probe ../gateway-dos/GATE.EXE --press escape \
  --state-dir build/runtime-state --milliseconds 18000 --dump-prefix build/main
```
