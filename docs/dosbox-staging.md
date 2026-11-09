<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception -->

# DOSBox Staging: reproducible headless DOS tests

The optional Microsoft C 5.1 probes run under DOSBox Staging. They are **DOS
programs**, even when their source is assembled on a Linux host. The compiler,
original Gateway files, compiled DOS objects and generated probes must stay
outside Git. The `msc51` Nix apps supply DOSBox Staging and the optional,
hash-pinned compiler when running in the user's Nix environment.

## Portable Linux build checked in an external test environment

On 2026-11-09, the official [Linux x86-64 DOSBox Staging 0.83.0
release](https://www.dosbox-staging.org/download/linux/) (commit `7b40053b7`)
was downloaded from
`dosbox-staging/dosbox-staging`'s GitHub release and unpacked outside the
repository. The 29,377,664-byte archive was checked against the published
SHA-256 before extraction:

```text
dosbox-staging-linux-x86_64-v0.83.0.tar.xz
SHA-256 d3a94f7f1c3e68a47ec88d61145506c7904452adb0c9c5928cb8cfe2331d6c5c
```

The binary's `--version` succeeded; `ldd` reported no missing libraries.
No system-wide installation, root privileges, proprietary assets, or network
access are needed to run the unpacked binary.

## Headless setup

In containers without an X11/Wayland display or audio device, use SDL's
dummy video/audio backends and disable mouse capture. On the tested 0.83.0
build, `SDL_VIDEODRIVER=dummy` **without** `mouse_capture=nomouse` aborted
during `SDL_SetRelativeMouseMode` with `Error putting the mouse into relative
mode`. With that setting, DOSBox fell back from OpenGL to its software SDL
renderer, ran DOS commands, and shut down cleanly.

The following standalone smoke test creates only an ignored `build/` file:

```sh
dosbox=/path/to/dosbox-staging-linux-x86_64-0.83.0-7b400/dosbox
mkdir -p build/dosbox-smoke
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$dosbox" --noprimaryconf --nolocalconf \
  --set mouse_capture=nomouse --set mididevice=none \
  --set core=dynamic --set cpu_cycles=max \
  -c "mount c \"$PWD/build/dosbox-smoke\"" \
  -c 'c:' -c 'ver > PROBE.TXT' --exit
sed -n '1,3p' build/dosbox-smoke/PROBE.TXT
```

The tested result was a clean exit and an actual file written from DOS:

```text
DOSBox Staging version 0.83.0 (7b400)
DOS version 5.00
```

`tools/msc51.py` now accepts `--headless` for its noninteractive DOS probe
commands. It sets both SDL dummy backends and passes `--noprimaryconf`,
`--nolocalconf`, `mouse_capture=nomouse` and `mididevice=none`. Its existing
`--dosbox` argument also accepts an absolute path to the portable executable.
For example, when a complete Microsoft C 5.1 installation is available:

```sh
python3 tools/msc51.py probe \
  --compiler /path/to/installed/msc51 \
  --dosbox "$dosbox" --headless
```

For the data-image/VGA probes, use `compile-data` only after the documented
memory/selector/file probe prerequisites have passed. It can mount the pinned
original files on a separate read-only drive using
`--original-game-dir ../gateway-dos`.

## What this verifies (and what it does not)

| Test | Result on 2026-11-09 |
| --- | --- |
| Official portable archive SHA-256, binary version and shared libraries | PASS |
| Headless DOSBox runs `VER`, writes host-visible `PROBE.TXT`, exits | PASS |
| Original `GATE.EXE` from read-only game mount | Enters BIOS VGA mode `12h` (640×480, 16 colors) in a five-second bounded run; terminated by timeout, not a completed game test |
| Source-only Microsoft C staging tests after adding headless support | 99 run, 98 passed, 1 skipped (`mtools` unavailable) |
| Full Python test discovery, including the optional real DOSBox smoke | 134 run, 133 passed, 1 skipped (`mtools` unavailable) |
| Decompilation tracker | `tracker ok: 1798 routines` |
| `BOOTVGA.EXE` and `BOOTPREP.EXE` with the actual Microsoft C 5.1 runtime | **Not rerun here:** compiler and previously compiled DOS executables are not available in this environment; user-side logs remain the evidence |

The original `GATE.EXE` mode transition is proof of a running DOS executable,
not proof that its graphics are correct or that the reconstructed launcher
executes. The dummy renderer has no visible window. Visual comparisons and
input injection still require the project's private Xvfb workflow, and the
full C entry/other resident banks remain unreconstructed.

The previously reported user-side `BOOTVGA.EXE` log confirms an additional
real-mode check: the reconstructed resident mode-13 switch, a pixel write,
and BIOS mode restoration succeeded without executing original resident
code or installing IRQs. This report is distinct from the local headless
smoke tests, which do **not** claim to have reproduced it.
