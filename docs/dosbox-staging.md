<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception -->

# DOSBox Staging: reproducible headless DOS tests

The optional Microsoft C 5.1 probes run under DOSBox Staging. They are **DOS
programs**, even when their source is assembled on a Linux host. The compiler,
original Gateway files, compiled DOS objects and generated probes must stay
outside Git. The `msc51` Nix apps supply DOSBox Staging and the optional,
hash-pinned compiler when running in the user's Nix environment.

## Source-C DOS executable checkpoint (2026-11-09)

The original-derived launcher now links to `GATEWAY.EXE`, a real-mode
MS-DOS program, using an explicitly reconstructed source-C `main()` and a
source-C FAR-return callback. The prior missing CRT `_main` and Ctrl-Break
symbols are closed without executing incomplete original resident banks.
The original DOS game files stay on a separate read-only drive. The complete
compiler/probe/link/entry sequence now runs in a single DOSBox session:

```sh
gateway-msc51 compile-all --original-game-dir ../gateway-dos --headless
```

For individual investigation after the other required `compile-*` stages
have passed, the final steps remain available separately:

```sh
gateway-msc51 compile-data --original-game-dir ../gateway-dos
gateway-msc51 link-launcher
gateway-msc51 test-dos-entry --original-game-dir ../gateway-dos --headless
```

The last command runs real DOS `GATEWAY.EXE` in four isolated modes:
`/CHECK`, `/PICTURE`, `/SECOND`, and `/TITLE`. It checks each process exit
code and the fresh success text in `ENTRY.LOG`, `PICTURE.LOG`, `SECOND.LOG`,
and `TITLE.LOG`. All four passed under independently installed DOSBox 0.74
with the real Microsoft C 5.1-built EXE. The supported Nix toolchain uses
DOSBox Staging. `/INTRO` is callable from the original-files drive as
`D:\GATEWAY.EXE /INTRO` but **has not completed correctly** in the current
tests. See [link closure](msc51-link-closure.md) for precise scope.

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

For unattended Microsoft C compilation, use a private Xvfb server rather
than SDL's dummy video driver. The user's DOSBox Staging **0.82.2** binary
was observed aborting *before running COMPILE.BAT* with `SDL_VIDEODRIVER=dummy`,
first with its default OpenGL output and then again with explicit
`output=texture` and `texture_renderer=software`. This release still probes
a temporary OpenGL-capable window during startup. Software texture rendering
alone therefore does **not** make SDL's dummy video driver usable.

`--headless` now wraps DOSBox in `xvfb-run -a --` and selects
`SDL_VIDEODRIVER=x11`, `SDL_AUDIODRIVER=dummy` and
`LIBGL_ALWAYS_SOFTWARE=1`. DOSBox uses the temporary private display,
including its GLX capability, without opening a desktop window. It also
disables mouse capture and selects the SDL software texture renderer for the
actual video output. Both `nix run .#gateway-dos` and `nix develop .#msc51`
provide `xvfb-run` automatically. Outside Nix, install `xvfb-run` and its
Xvfb/Xauth dependencies before using `--headless`.

For historical comparison, the tested portable Staging 0.83.0 build could
run with SDL's dummy video driver after disabling mouse capture; without
`mouse_capture=nomouse` it aborted at `SDL_SetRelativeMouseMode`. The current
driver uses private Xvfb for reproducible compiler runs across the versions.

The following standalone smoke test creates only an ignored `build/` file:

```sh
dosbox=/path/to/dosbox-staging-linux-x86_64-0.83.0-7b400/dosbox
mkdir -p build/dosbox-smoke
SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a -- "$dosbox" --noprimaryconf --nolocalconf \
  --set mouse_capture=nomouse --set mididevice=none \
  --set output=texture --set texture_renderer=software \
  --set core=dynamic --set cpu_cycles=max \
  -c "mount c \"$PWD/build/dosbox-smoke\"" \
  -c 'c:' -c 'ver > PROBE.TXT' --exit
sed -n '1,3p' build/dosbox-smoke/PROBE.TXT
```

The previously tested 0.83.0 dummy-video result was a clean exit and an
actual file written from DOS; the Xvfb variant above still needs a direct
Staging runtime check in this environment:

```text
DOSBox Staging version 0.83.0 (7b400)
DOS version 5.00
```

`tools/msc51.py` accepts `--headless` for its noninteractive DOS probe
commands. It starts a private Xvfb/X11 server, selects dummy audio and
software GL, and passes `--noprimaryconf`, `--nolocalconf`,
`mouse_capture=nomouse`, `mididevice=none`, `output=texture`, and
`texture_renderer=software`. Its existing
`--dosbox` argument also accepts an absolute path to the portable executable.

The visible `nix run .#gateway-dos -- ../gateway-dos` gameplay process is
separate from the compiler process. It now explicitly passes
`--set core=dynamic --set cpu_cycles=max` too; earlier it relied on
DOSBox's runtime defaults even though compilation used these settings.
This affects only emulator throughput, not the recovered original timing
constants, source-C routine order or VGA algorithms. In particular,
it does **not** resolve the remaining slow BIOS-per-pixel mode-12h video
adapter, whose original direct-VGA equivalent is documented in
`msc51-link-closure.md`.

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
video guard/layout and **live mode-12 and mode-13 VGA** probes also passed.
In mode 12h, `BVIDREAL.LOG` independently verifies the 16-color pixel, a
full four-plane clear including the final `(639,479)` pixel, and an opaque
rectangle with unchanged surrounding pixels through BIOS 10h pixel reads.
In mode 13h it verifies an A000 pixel write, full framebuffer clear, opaque
rectangle and prior BIOS video mode restoration. The exact mode-12 drawing
log line is mandatory, so an older pixel-only log cannot pass validation.
None of the probes executes original resident code
or installs IRQs. A fresh Microsoft LINK 3.65 launcher diagnostic still reports exactly
two missing symbols, `_main` and `_gate_linked_handle_control_key`, confirmed
by a 42/42-input OMF audit; the full game remains unlinked.

The next 2026-11-09 startup experiment compared the earlier original RAM
captures at 0/100/250ms against 350/450/500/1500ms. At linked `DS=2a7a`,
two initially zero FAR pointers stabilize by 350ms: resource-base
`[54c2]=3592`, `[54c4]=DS`, and active font `[54ca]=54ce`, `[54cc]=DS`.
The `3592h` offset is the exact start of the verified EXE static-data copy;
`54ceh` is the original first font-cache record. The source-level
`gate_exe_bind_resource_pointers` rebinds these four words to the *new*
allocated DOS data segment after image preparation, without assuming the
original snapshot's fixed segment number. `BOOTPREP.EXE` poisons all four
words, invokes the real compiled helper and checks the exact values;
`BOOTVGA.EXE` independently checks the same bindings. Both real DOS probes
passed, and the new `BPREAL.LOG` line is mandatory. This is an observed
startup-state reconstruction, **not** the unidentified original function
that performed those writes or a fully initialized font/resource manager.

The next live probe tested the **original mode 2 / BIOS mode 12h** path,
observed independently in the 350ms original startup capture. It replaces
the old unreachable-retrace test stub with linked original-derived
`RTL:f473..f49a` code (`M24.OBJ`), performs real VGA vertical-retrace and
attribute-controller accesses, checks the original 639×479 inclusive
descriptor bounds, adapter 5, pen/mask `0fh` and status port `03dah`,
then writes a planar 4-bit pixel and reads it back using BIOS INT 10h.
The subsequent mode-13 pixel/clear/rectangle tests and prior BIOS mode
restoration still succeed. Both original images remain mounted read-only.

## What this verifies (and what it does not)

| Test | Result on 2026-11-09 |
| --- | --- |
| Official portable archive SHA-256, binary version and shared libraries | PASS |
| Headless DOSBox runs `VER`, writes host-visible `PROBE.TXT`, exits | PASS |
| Original `GATE.EXE` from read-only game mount | Enters BIOS VGA mode `12h` (640×480, 16 colors) in a five-second bounded run; terminated by timeout, not a completed game test |
| Source-only Microsoft C staging and FAT-floppy tests with local mtools | PASS; no mtools skip |
| Full Python test discovery, including the real DOSBox smoke | 136/136 passed, no skips |
| Decompilation tracker | `tracker ok: 1798 routines` |
| `BOOTPREP.EXE`, `BOOTVGA.EXE` with actual Microsoft C 5.1/DOSBox | PASS; original-derived mode-2 BIOS 12h/retrace and planar pixel, then mode-3 BIOS 13h pixel/clear/rectangle and BIOS restoration |
| Full launcher LINK using the real compiler and linker | 42/42 objects parse; blocked by exactly `_main` and `_gate_linked_handle_control_key` |

The original `GATE.EXE` mode transition is proof of a running DOS executable,
not proof that its graphics are correct or that the reconstructed launcher
executes. The dummy renderer has no visible window. Visual comparisons and
input injection still require the project's private Xvfb workflow, and the
full C entry/other resident banks remain unreconstructed.

The live `BOOTVGA.EXE` check was reproduced headlessly with the uploaded
compiler and original Gateway files. Its DOS log is a hardware-code-path
witness, **not** a visual screenshot comparison or a playable intro.
