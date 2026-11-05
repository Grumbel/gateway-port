# Workspace setup for Gateway research

This records the container setup exercised on 2026-11-05. The normal
developer environment remains the pinned Nix flake; this document also
explains how to build the *headless* Unicorn probe when a temporary analysis
container has a compiler but no CMake, Nix, SDL3 or working package mirror.
All game data and extracted dependencies stay outside Git.

## Normal NixOS workspace (preferred)

Clone the source tree as `gateway-port/`; put the legally obtained DOS
distribution in the sibling `gateway-dos/`, never inside the Git tree.
Check the executable digests against [executable.md](executable.md).

```sh
cd gateway-port
nix develop
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
python3 tools/decomp_status.py check
```

For genuine SDL3 interaction, native screen tests or simulated clicks, use a
private X server. On a conventional Ubuntu system with working package
repositories, install `xvfb` and `xauth`, then invoke the appropriate
recording or picker-trial command through `xvfb-run -a`. **Never** replay
automated UI input on the user's desktop display. The CMake/Nix workflow also
builds `gateway` (SDL3), `gateway-probe` (headless Unicorn) and the tests.

## Restricted container with the supplied Unicorn ZIP

In the 2026-11-05 workspace, `gcc`/`g++` 13.3, Python 3.12 and the DOS files
were available, but `nix`, `cmake`, `ctest`, `ninja`, `Xvfb` and SDL3
development files were not. The attached `unicorn.zip` is a complete
**Nix Unicorn 2.1.4** output: public C headers, `libunicorn.a` and
`libunicorn.so`. Its shared library requires glibc 2.43 and cannot load
against this container's older glibc; the **static** library links and runs
with the GCC 13 toolchain and `-latomic`.

From `gateway-port/`, with the archive at the path used in this workspace:

```sh
python3 tools/build_probe_from_unicorn_zip.py \
  --unicorn-zip ../upload/unicorn.zip
build/local-probe/gateway-probe --help
build/local-probe/gateway-probe ../gateway-dos/GATE.EXE \
  --headless --milliseconds 1500 --read-only --deterministic-clock
python3 tests/probe_tests.py build/local-probe/gateway-probe
python3 tools/decomp_status.py check
```

The helper obtains the `gateway_core` source list from `CMakeLists.txt`;
it compiles the probe, Nuked OPL3 and all core sources with the system
`gcc`/`g++`, and links the provided static Unicorn library with pthread,
`dl`, `m` and `atomic`. It extracts only headers and `libunicorn.a` to
`build/local-probe/unicorn/`. Everything it generates lives under ignored
`build/`; the original ZIP is not copied into a commit. It does **not**
produce `gateway` or otherwise substitute for SDL3. The helper ensures the
linked probe remains owner-executable even if an existing output had mode
`0644` and the linker preserved that mode.

The standalone debugger raster test does not require SDL3, Xvfb or Unicorn.
With the same host C++20 compiler, compile only the debugger and its scene
model with function-level dead-code elimination (to exclude unused
game-dependent tracker functions):

```sh
g++ -std=c++20 -O0 -ffunction-sections -fdata-sections -Isrc \
  tests/debug_ui_tests.cpp src/debug_ui.cpp src/debug_scene.cpp \
  -Wl,--gc-sections -o build/local-probe/debug-ui-tests
build/local-probe/debug-ui-tests
```

This exercises the H/R raster with synthetic native picker groups, including
cross-group overwrites, disabled rows and large slot-zero fallbacks. It is
**not** a substitute for inspecting real guest input on private Xvfb.

The scene tracker, including its original PIC/RGN fixture comparisons, can
likewise be tested without SDL3:

```sh
g++ -std=c++20 -O0 -ffunction-sections -fdata-sections -Isrc \
  tests/debug_scene_tests.cpp src/debug_scene.cpp src/regions.cpp \
  src/pic.cpp src/crusher.cpp src/debug_state.cpp src/story_model.cpp \
  -Wl,--gc-sections -o build/local-probe/debug-scene-tests
build/local-probe/debug-scene-tests ../gateway-dos
```

The independent, filesystem-only picker audits can run directly with Python:

```sh
python3 tests/report_picker_scene_tests.py
python3 tests/compare_picker_scene_tests.py
python3 tests/compare_picker_captures_tests.py
```

They compare frozen synthetic native picker geometry, full H winner maps and
the legacy room-only candidate measurements without injecting input.

The available package manager could not install Xvfb or SDL3 here:
`apt-get update` first failed to drop supplementary groups, and
`APT::Sandbox::User=root` got HTTP 500/502 from the Ubuntu snapshot mirror.
This is an environment/network limitation, **not** evidence that Xvfb has
been installed. Consequently the container can run headless, non-interactive
original-code probes and synthetic probe tests, but **must not** run live
picker-click/UI trials until Xvfb is available. On a machine with Nix use
`nix develop` and install/start Xvfb as above.

## Research artifacts and hand-off

- Keep original `gateway-dos/`, archived game distributions and save files
  read-only. The startup smoke test uses `--read-only`.
- Recordings, original RAM dumps, extracted images, generated Ghidra plans
  and local probe binaries all belong in ignored `build/`.
- Share source changes using cumulative Git bundles from the agreed
  `3b8333c02c29e4e9d3cb908e6e341d2b2a674d59` base. Never bundle
  original or extracted game bytes.
- The toolchain fallback checks that native code builds and starts, **not**
  that synthetic GUI interaction or a complete playthrough passes.
