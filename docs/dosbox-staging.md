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

## Reproduced with Microsoft C 5.1 and original game files

On 2026-11-09, the uploaded `Microsoft C 5.1 Optimizing Compiler (5.25).7z`
was checked against the SHA-512 pinned in `flake.nix`, unpacked using the
official 7-Zip 26.06 Linux command-line build, and assembled from its DOS
floppy images with locally built GNU mtools 4.0.49. The real-mode DOS
`CL.EXE`/compiler passes and `LINK.EXE` were selected; compiler, original
game data and compiled binaries were kept outside Git.

The first `msc51.py probe` run exposed two missing staged headers, **not**
an original-code mismatch. `RANDOM.C` referred to the original
`gate_c_runtime_constants.h`, while its replacement `GATE.H` lacked the
source-defined `GATE_DS_RANDOM_STATE_LOW/HIGH` offsets. The probe now stages
only the four needed original random constants in `CONST.H` and the two
original memory-layout offsets in `GATE.H`, without changing the recovered
random-generator expressions. Microsoft C 5.1 then compiled and linked
`INTROCHK.EXE`, whose 32 DOS random steps passed.

With an installed compiler and portable DOSBox executable, the reproduction
uses the existing command-line workflow (paths below are examples):

```sh
python3 tools/msc51.py probe \
  --compiler /path/to/msc51-installed \
  --dosbox /path/to/dosbox --headless
python3 tools/msc51.py compile-data \
  --compiler /path/to/msc51-installed \
  --dosbox /path/to/dosbox --headless \
  --original-game-dir ../gateway-dos
python3 tools/msc51.py link-launcher \
  --compiler /path/to/msc51-installed \
  --dosbox /path/to/dosbox --headless
```

All optional `compile-*` stages (launcher, memory, selectors, DOS files,
data, RTL state, hardware, runtime files, picture DCL, resident state, heap,
input, clock, CRT) passed with the historical compiler. The original-game
`DATREAL.LOG` verified the 15,616-byte resident CRC32 `F332BF93` and
19,690-byte prepared-data CRC32 `5A991491`; owner, entry-prologue, audio,
video guard/layout and **live mode-13 VGA** probes also passed. In particular,
`BVIDREAL.LOG` confirmed an A000 pixel write, full mode-13 framebuffer
clear, opaque rectangle with unchanged surrounding pixels, and prior BIOS
video mode restoration. None of the probes executes original resident code
or installs IRQs. A fresh Microsoft LINK 3.65 launcher diagnostic still reports exactly
two missing symbols, `_main` and `_gate_linked_handle_control_key`, confirmed
by a 42/42-input OMF audit; the full game remains unlinked.

## What this verifies (and what it does not)

| Test | Result on 2026-11-09 |
| --- | --- |
| Official portable archive SHA-256, binary version and shared libraries | PASS |
| Headless DOSBox runs `VER`, writes host-visible `PROBE.TXT`, exits | PASS |
| Original `GATE.EXE` from read-only game mount | Enters BIOS VGA mode `12h` (640×480, 16 colors) in a five-second bounded run; terminated by timeout, not a completed game test |
| Source-only Microsoft C staging and FAT-floppy tests with local mtools | PASS; no mtools skip |
| Full Python test discovery, including the real DOSBox smoke | 134/134 passed, no skips |
| Decompilation tracker | `tracker ok: 1798 routines` |
| `BOOTPREP.EXE`, `BOOTVGA.EXE` with actual Microsoft C 5.1/DOSBox | PASS; relocated startup data, original-derived mode-3 layout, live mode-13 pixel, clear, rectangle and BIOS restoration |
| Full launcher LINK using the real compiler and linker | 42/42 objects parse; blocked by exactly `_main` and `_gate_linked_handle_control_key` |

The original `GATE.EXE` mode transition is proof of a running DOS executable,
not proof that its graphics are correct or that the reconstructed launcher
executes. The dummy renderer has no visible window. Visual comparisons and
input injection still require the project's private Xvfb workflow, and the
full C entry/other resident banks remain unreconstructed.

The live `BOOTVGA.EXE` check was reproduced headlessly with the uploaded
compiler and original Gateway files. Its DOS log is a hardware-code-path
witness, **not** a visual screenshot comparison or a playable intro.
