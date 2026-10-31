# Repository Guidelines

## Project Goals & Architecture

Build a Linux port of Frederik Pohl’s Gateway using C++, SDL3, CMake, and a NixOS-compatible `flake.nix`. The first milestone is a playable game through partial emulation, replacing original graphics and sound functions with high-level emulation (HLE) implemented through SDL3. Keep emulation, HLE boundaries, and host services separate so components can be replaced incrementally.

The long-term goal is to reverse engineer the game’s resource formats and interpreter, then implement an alternative interpreter. Record discoveries, offsets, calling conventions, and supporting evidence; distinguish confirmed behavior from hypotheses.

## Project Structure & Module Organization

Work inside `gateway-port/`, the actual Git repository. The sibling `../gateway-dos/` contains original DOS executables and resources for analysis and runtime input. Preserve these files unchanged; `.RS` files are game assets, not Rust source. The sibling `../FredPohlsGateway/` provides a packaged DOSBox reference.

Use `src/` for C++, `tests/` for automated checks, and `docs/` for reverse-engineering notes. Keep generated artifacts under `build/` and original game data outside the source tree.

## Build, Test, and Development Commands

The development workflow uses CMake and the pinned Nix flake:

```sh
nix develop
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The flake provides SDL3, Unicorn, and development tools. See `README.md` for executable invocation and game-data paths. The `gateway` runtime connects guest execution to SDL3 video/audio; the separate probe and host diagnostic support research. Full gameplay is not verified yet.

## Coding Style & Testing Guidelines

Format C++ with `clang-format -i src/*.cpp src/*.hpp tests/*.cpp` using `.clang-format`. Isolate binary parsing from SDL3 presentation. Validate offsets, lengths, and guest-memory accesses explicitly.

Add focused tests for parsers, emulated instructions, and HLE contracts using CTest. Compare behavior against the original game under DOSBox. For runtime changes, check graphics, input, and audio, and record reproducible steps. No coverage threshold is established.

## Research & Contribution Guidelines

[Risca/dgate_resource_manager](https://github.com/Risca/dgate_resource_manager) explores Death Gate resources. Treat it as a research lead; verify format similarities against Gateway data before reuse.

Commit frequently in small, focused increments; do not accumulate hours of work before committing. Use an imperative subject and a detailed body explaining the change, its purpose, validation, and remaining limitations. End every commit message with a `Co-authored-by: Name <email>` trailer identifying the collaborating agent. Use the established collaborator identity when available; never invent a human identity.

Pull requests should describe behavior, evidence, validation, and remaining limitations; include screenshots for visible changes.
