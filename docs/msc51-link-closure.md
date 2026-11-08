<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception -->

# Microsoft C 5.1 launcher link closure

This is the **remaining implementation/verification inventory**, not a list
of functions already recovered. The latest real Microsoft LINK 3.65 report
supplied by the user on 2026-11-08 confirmed **26 unresolved names**, with
`RTLHW`, `RTFILE`, `RESSTATE`, `HEAP`, `INP`, `CLOCK` and `CRTENV` linked.
The extended `RTLHW` adapter supplies eight RTL text/video names, and
`RESSTATE` supplies three bounded raster names. The adapter also now stages
two original BIOS/chipset detection entry points and two exact relocated
RTL palette transforms. The intro's mode-12h/13h VFX blitter is also staged
with bounded host behavior. A new C89 DCL decoder additionally stages the
compressed-picture service, with host-verified buffered DOS input and planar
output. The original resource file wrappers now stage their raw BP-2 stack
slots and unguarded CPU division without two external host-test callbacks.
**6 is the predicted remainder if all updated DOS probes pass.**
This is not yet a measured Microsoft LINK result.
Do not manufacture no-op definitions merely to obtain an executable.

## The 26 names in the last confirmed LINK report

| Group | Original unresolved entry points | Count | Required work |
| --- | --- | ---: | --- |
| DOS entry point | `main` | 1 | Reconstruct actual DOS startup/relocation and bind live original linked DS, stack, initial descriptors, assets, CRT initialization and transfer to `gate_exe_play_intro`. An empty `main()` would produce a false success. |
| RTL text/video state | `gate_rtl_video_inversion`, `gate_rtl_video_attribute`, `gate_rtl_video_columns`, `gate_rtl_video_rows`, `gate_rtl_video_tab_base`, `gate_rtl_video_block_mode`, `gate_rtl_video_set_block_mode`, `gate_rtl_video_command` | 8 | **Staged, DOS probe pending.** Original RTL disassembly verifies the seven RTL-DS offsets and pointer dereferences; the separate 16-bit DS must be bound during startup, not assumed to equal EXE DS. The 14-byte command adapter handles only original commands 2/3/9 through BIOS INT 10h. The underlying 12a5:2d8a dispatcher has not been decompiled, and unrecognized commands abort. |
| Pixel and drawing primitives | `gate_rtl_plot_pixel`, `gate_rtl_background_rect`, `gate_rtl_resident_line_to` | 3 | **Staged, DOS probe pending.** The native host's bounded pixel modes 1/2/3, raster operations and clipping are implemented over real DOS VGA. Mode 13h uses A000 linear memory and modes 1/2 use BIOS INT 10h pixel reads/writes, with test pixels/lines/opaque rectangles. Unmodeled modes, diagonal lines and patterned fills abort; the DOSBox test and guest trace comparison remain outstanding. |
| Video adapter query | `gate_rtl_mode_query_adapter`, `gate_rtl_mode_svga_variant` | 2 | **Staged, DOS probe pending.** Original `GATE.RTL:17c95..17d79` maps INT 10h/AX=1a00 active/alternate display codes to type/monitor pairs; `17b4f..17c24` tests AH=6fh, ROM signatures and bank/CRTC ports for SVGA variant 0–5. The old-BIOS fallback and exotic adapter-switch helpers remain outside the supported boundary; unsupported display BIOS requests abort. |
| Original exception/stack state | `gate_rtl_file_divide_fault`, `gate_rtl_file_stack_word` | 2 | **Staged without external services, DOS compile pending.** Original `RTL:e59b..e5e2` uses an uninitialized WORD at BP-2 for both reads. `e5fc..e61d` and `e669..e68a` use unguarded unsigned `DIV`, which raises INT 0 for a zero divisor. Staged Microsoft `/Od` code preserves raw automatic stack slots and division; host tests retain their seeded stack/fault callbacks. Microsoft code generation and live stack history remain unverified. |
| Runtime calendar sample | `gate_linked_read_runtime_time_sample` | 1 | **Staged, DOS probe pending.** Original `RTL:8bb6..8cb7` reads DOS INT 21h/2Ah and 2Ch and fills the ten-byte epoch/milliseconds/TZ-minutes/DST sample. The bounded adapter follows original calendar and 1987 US DST arithmetic, including different hour/second values at the two DST call sites. Independent host tests cover leap days, timezone signs, boundaries, and FAR offset wrap. The other animation clocks are still approximate. |
| VFX/picture pipeline | `gate_linked_decompress_picture_pixels`, `gate_linked_blit_vfx_picture`, `gate_linked_transform_picture_palette_indexed`, `gate_linked_transform_picture_palette_rgb`, `gate_linked_save_vfx_draw_state`, `gate_linked_restore_vfx_draw_state` | 6 | The two palette transforms are **staged and host-tested** against original RTL `f4df`/`f579`. The VFX blitter is **staged as a bounded DOS adapter**, not an instruction-for-instruction reconstruction of resident `24a7:000c`: modes 2/3 interpret original CA00 image records and render four-plane mode 12h through BIOS pixels or mode 13h through A000 bytes, with original raster operators and clipping. A bounded C89 streaming DCL decoder is staged and host-tested against a reference stream, mode-12h plane packing, 64KiB rollover and file-position preservation. DOS probes are still pending; saved drawing-state lifetime remains unresolved. |
| Intro services and diagnostics | `gate_linked_initialize_intro_services`, `gate_linked_handle_control_key`, `gate_linked_print_intro_diagnostic` | 3 | Reconstruct the original music/IRQ8 setup, Ctrl-Break branch and resident diagnostic destination/format from the EXE and RTL. Silent success and dummy output are not valid equivalents. |
| **Reported total** | | **26** | 18 more definitions and two original-stack/fault call-site replacements staged since this report; 6 projected unresolved after DOS probes. |

### Current DOS probe order

```sh
nix run -L .#msc51-compile-launcher
nix run -L .#msc51-compile-memory
nix run -L .#msc51-compile-dos-files
nix run -L .#msc51-compile-rtl-state
nix run -L .#msc51-compile-hardware
nix run -L .#msc51-compile-runtime-files
nix run -L .#msc51-compile-picture-dcl
nix run -L .#msc51-compile-resident-state
nix run -L .#msc51-compile-heap
nix run -L .#msc51-compile-input
nix run -L .#msc51-compile-clock
nix run -L .#msc51-compile-crt
nix run -L .#msc51-link-launcher
nix run .#msc51-link-report -- --json build/msc51-link-report.json
```

The linker refuses stale compiled objects, missing probe successes and
changed source fingerprints before attempting a link. The probe executables
are not end-to-end intro tests; the resident raster probe now exercises
real VGA pixels, but not full original image reproduction. The original
binary assets and GATE.EXE/GATE.RTL live guest snapshot remain necessary to
recover and validate the listed core services.

`msc51-link-launcher` now checks all 12 compiler/probe prerequisites and
reports every missing or stale source signature with its `nix run` command
in one rejection. The 28 reconstructed modules can therefore compile
successfully while LINK is still blocked by an older `RTLSIG.LOG` or
`PICSIG.LOG`; those are freshness failures, not additional unresolved
symbols. Do not run `msc51-link-report` until LINK has actually produced
`LINK.LOG`.

The file-wrapper changes require rerunning `msc51-compile-launcher` so
`M11.C` compiles with `GATE_MSC51_ORIGINAL_FILE_ABI`. The checked-in source
C continues using explicit host callbacks for deterministic tests. A GCC
C89 `-O0` compile and `nm -u` check confirms that the DOS-staged `M11.C` no
longer references either artificial helper. That check is not a substitute
for disassembling the resulting Microsoft 5.1 `M11.OBJ` or verifying the
original DOS exception handler and stack history.

After updating to the extended `RTLHW.C`, rerun
`nix run -L .#msc51-compile-hardware` before linking. Its probe now uses an
independent 0x40-paragraph RTL data segment, three distinct FAR dimension
segments, block-cursor state, real INT 10h text cursor get/set, and a
non-visible character/attribute rewrite. **A passing probe does not
initialize the RTL segment for `GATEWAY.EXE`**: that will require a real
DOS startup entry and original RTL loader/relocator, currently unavailable.

It additionally probes the VGA BIOS display-combination response and checks
the original adapter-pair encoding when the BIOS reports color VGA. The SVGA
variant probe reads/writes video banking and CRTC test registers, restoring
their prior state after each detection attempt. On a BIOS without AX=1a00,
adapter detection deliberately aborts: the original fallback through other
chipset/monitor queries is not recovered, so returning a fabricated adapter
would lead the source C down the wrong graphics mode branch. The host fixture
tests all five nonzero SVGA outcomes, variant zero, monochrome handling,
active/alternate adapters and register restoration; Microsoft C and an
authentic DOSBox video BIOS still need to verify these paths.

The same `RTLHW.OBJ` probe also exercises the two palette transforms.
Their original return FAR offsets are linked `DS:0fd8` (RGB triplets) and
`DS:1008` (packed indices), derived from the relocated palette work base
`DS:0fce` plus the RTL entry's 0x0a / 0x3a offsets. Both source pointers
wrap their offsets without carrying into the segment. The independent host
fixture checks all 16 round-tripped color indices, 0/21/42/63 DAC channels,
specific channel-to-bit ordering and input offset wrap. The DOS probe checks
the output addresses and all 16 round trips against Microsoft C 5.1 memory.

The extended `RESSTATE.C` also requires
`nix run -L .#msc51-compile-resident-state` before linking. It exercises
copy/XOR in mode 13h, single-width horizontal lines, opaque fill and outline,
state restoration, and one mode-12h planar pixel through real BIOS. The host
fixture independently checks AND/XOR/OR, clipping, both line orientations,
and rectangle interiors with a 20-bit segmented-address model. These checks
do not establish pixel-equivalence with every original 25d7/2621/2698
callback, nor do they implement previously unobserved raster modes.

The same resident stage now includes the VFX image blitter. Its DOS probe
draws and XOR-restores two real mode-13h bytes, then reads back two colors
from a four-plane mode-12h CA00 image. The host fixture independently
checks mode-13h 2x2 source stride and copy/XOR plus distinct mode-12h
plane contributions. The original VFX resident module and other display
modes have not been decompiled; BIOS-per-pixel drawing is intentionally
slower than its optimized native VGA-register loop.

### Original VFX save/restore state: the remaining evidence

The missing `gate_linked_save_vfx_draw_state` and
`gate_linked_restore_vfx_draw_state` **are present in the original RTL**:
`GATE.RTL:0x11078..0x1111f` and `0x10f64..0x11077`. The three far calls
from the frame renderer `rtl:0x12ed7` and their original source identities
are independently captured in `docs/picture-conditions.md`.

These two functions are *not* simple copies of `DS:29de` drawing words.
Their original 8086 branches inspect a linker-relocated flag WORD, test
bit 0 and bit masks `0x30` and `0x0c`, and may call additional resident
services. Some of those calls receive rectangle coordinates, while others
receive a request number `0x33`; their precise effects remain unverified.
The provable short path
of the save function clears flag bit 0 and returns 1 if the state is
nonzero and bit 0 was set; otherwise it returns 0. The restore function
conditionally sets bit 0 if called with a nonzero saved result, and its
other branches may redraw screen regions. Consequently, an unconditional
`return 1` / empty restore would hide missing graphics behavior.

The read-only `--vfx-state` inspector now verifies **all 27 direct DS operand
sites** in these two routines against `GATE.RTL` and the live relocated RAM
image (including one-byte `MOV AX,[imm16]` opcodes), checks consistent shared
flags, width and height offsets, reports cursor offsets, and resolves eight
additional FAR call targets.
With an original scene snapshot and the game's files outside Git:

```sh
python3 tools/inspect_rtl_animation.py \
  --ovl ../gateway-dos/GATEMAIN.OVL \
  --exe ../gateway-dos/GATE.EXE \
  --rtl ../gateway-dos/GATE.RTL \
  --ram build/rgn-scenes-v026/before.bin \
  --vfx-state --json
```

If a VFX entry trace independently establishes the live DS segment, supply
it with `--vfx-ds HEX` to inspect the flag WORD at that **capture time**;
the analyzer does not assume the screenshot's DS is the service's DS, or
that the capture necessarily exercised a particular branch. Its synthetic
tests cover relocated operands, inconsistent linker sites, state-flag
branch selection and callback extraction. An optional SHA-256-pinned original
binary test checks all 27 opcode/displacement pairs, both FAR entry points
and eight unrelocated FAR CALL sites. **No DOS
VFX state definitions are staged until the real relocated offsets and
required downstream callbacks have been validated.**

The new `msc51-compile-picture-dcl` stage builds `PICTDCL.OBJ` from a
streaming, bounded C89 translation of the repository's independently tested
PKWARE DCL decoder (`src/pic.cpp`). It uses a 4096-byte static history
window, reads up to 256 bytes per DOS call and rewinds any unread portion
after the DCL end marker, retaining the resource selector's file position.
The output goes straight to the caller-allocated extended FAR buffer, either
as mode-13h bytes or mode-12h interleaved four-plane rows. The host fixture
tests the reference stream, 64KiB segment rollover, planar colors, read-ahead
rollback and truncated/error paths. The independent DOS probe reads a file
with trailing bytes and checks DOS's actual seek position. This is a
bounded replacement, **not** a reconstructed copy of the original DOS codec.
The `RTLIMAC.H` staging header now includes the original file-handle/header
offset macros, so rerun `msc51-compile-rtl-state` before this new probe.

Once the linker reports zero missing names, the next milestone is *actual
DOS execution* through the original intro sequence with deterministic
input/frame comparisons. A linked `GATEWAY.EXE` is insufficient by itself.
