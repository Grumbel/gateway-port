# Repository Guidelines

## Project Goals & Architecture

Build a Linux port of Frederik Pohl’s Gateway using C++, SDL3, CMake, and a NixOS-compatible `flake.nix`. The first milestone is a playable game through partial emulation, replacing original graphics and sound functions with high-level emulation (HLE) implemented through SDL3. Keep emulation, HLE boundaries, and host services separate so components can be replaced incrementally.

The long-term goal is to reverse engineer the game’s resource formats and interpreter, then implement an alternative interpreter. Record discoveries, offsets, calling conventions, and supporting evidence; distinguish confirmed behavior from hypotheses.

## Project Structure & Module Organization

Work inside `gateway-port/`, the actual Git repository. The sibling `../gateway-dos/` contains original DOS executables and resources for analysis and runtime input. Preserve these files unchanged; `.RS` files are game assets, not Rust source. The sibling `../FredPohlsGateway/` provides a packaged DOSBox reference. Comparative assets are in `../101/`, `../201/`, and `../301/` (Spellcasting 101, 201, and 301); preserve these originals too.

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

Read `docs/data-files.md`, `docs/spellcasting.md`, and `docs/scummvm-legend.md` before extending parsers or HLE. Spellcasting 301 shares Gateway's format generation; 101/201 use different picture codecs. Reuse only verified layouts and distinguish format compatibility from runtime compatibility.

[Risca/dgate_resource_manager](https://github.com/Risca/dgate_resource_manager) explores Death Gate resources. Treat it as a research lead; verify format similarities against Gateway data before reuse.

Pull requests should describe behavior, evidence, validation, and remaining limitations; include screenshots for visible changes.

## Decompilation work

- Follow `docs/decompilation-plan.md`: read its "Ground rules" and
  "Operating loop" sections at the start of every session, and update its
  status markers and progress log in the commits that complete steps.
- Track routines in `decomp/functions.tsv` with `tools/decomp_status.py`;
  `check` must pass before every commit.
- C++ that reconstructs game logic goes in `src/story/`, as
  `LicenseRef-Gateway-Derived`.

## Commits

- Commit verified work in small, focused increments; do not accumulate hours
  of work before committing. Use an imperative subject and a detailed body
  explaining the change, its purpose, validation, and remaining limitations.
- Every new commit must include a `Co-authored-by:` trailer naming the AI
  that actually made the changes in that session, with its vendor's
  attribution address. Never copy attribution from earlier commits.
- Use a specific model name when the session metadata or the user states
  one; otherwise use a generic name. Do not guess a version and do not stop
  to ask. Examples:
  `Co-authored-by: Claude Opus 5.5 <noreply@anthropic.com>`,
  `Co-authored-by: Claude <noreply@anthropic.com>`,
  `Co-authored-by: Codex <codex@openai.com>`,
  `Co-authored-by: ChatGPT (High) <codex@openai.com>`.
- The commit author is always the repository owner
  (`Ingo Ruhnke <grumbel@gmail.com>`), never the AI.
- Never push, publish, or force-push; the owner does that.

## Licensing

- New files carry SPDX headers with
  `SPDX-FileCopyrightText: <year> Ingo Ruhnke <grumbel@gmail.com>`; never
  name an AI or its vendor as copyright holder. Files without comment syntax
  are covered in `REUSE.toml`. `nix flake check` runs `reuse lint`.
- Port, tool, test and documentation code is
  `GPL-3.0-or-later WITH GPL-3.0-linking-exception`.
- Code that reimplements game logic from tables, constants or control flow
  read out of the original program (such as `src/story_model.*`) is
  `LicenseRef-Gateway-Derived`. File-format parsers and hardware/DOS
  emulation stay GPL.
- Never commit original executables, game data, extracted assets, story
  text, executable bytes, or Ghidra pseudocode.

## Delivery

- Agents with direct repository access (Codex, Claude Code) commit
  directly on `master`.
- Web chat sessions without repository access deliver a Git bundle based on
  the current `master`, created with `git bundle create <name>.bundle
  <base>..master`, and state the base commit.
- Original game data may arrive as a `.zip`. Extract it outside the
  repository (as the sibling directories listed above), verify the
  executable SHA-256 values in `docs/executable.md`, and never commit the
  archive or its contents.
- Generated Ghidra output stays under `build/`; see `docs/ghidra.md` to
  regenerate it with or without Nix.
