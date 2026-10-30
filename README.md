# Gateway Linux port

Incremental C++ port of Frederik Pohl’s Gateway to Linux, using SDL3, CMake,
and Nix. The first target is execution of the original real-mode code with DOS
and hardware interfaces replaced by host implementations. Recovering the data
formats and replacing the original interpreter is the long-term goal.

## Current status

This is an initial research harness, **not yet a playable port**. It provides:

- A checked DOS MZ loader with segment relocation and initial register setup.
- `gateway-inspect`, which reports loaded image and entry information.
- `gateway-probe`, a bounded Unicorn real-mode startup probe that traces DOS
  services and stops explicitly on unsupported calls.
- `gateway-host`, an SDL3 graphics/audio diagnostic, separate from guest execution.

Original game files remain external in `../gateway-dos/` and are never needed
for package builds or unit tests. The probe opens game files read-only.

## Build and run

From this directory:

```sh
nix develop path:.
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/gateway-inspect ../gateway-dos/GATE.EXE
./build/gateway-probe ../gateway-dos/GATE.EXE
./build/gateway-host
```

`path:.` includes new files in this initially uncommitted repository. Once files
are tracked, normal `nix develop` and `nix build` work as well. Build a package
with `nix build path:.`; check it with `nix flake check path:.`. The flake locks
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

## Research

See [executable findings](docs/executable.md), [data file formats](docs/data-files.md),
and [next steps](TODO.md).
The neighboring EF2000 port informs the external-data, system-boundary, and
reference-testing approach. Its protected-mode native harness is not directly
applicable to Gateway’s real-mode startup.

[Risca’s Death Gate resource manager](https://github.com/Risca/dgate_resource_manager)
is a format-research reference. Gateway shares its string compression and PIC
directory layout but differs elsewhere; see [the comparison](docs/data-files.md#comparison-with-death-gate).
