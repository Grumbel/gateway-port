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
RTL palette transforms. **11 is the predicted remainder if both modified
DOS probes pass.** This is not yet a measured Microsoft LINK result.
Do not manufacture no-op definitions merely to obtain an executable.

## The 26 names in the last confirmed LINK report

| Group | Original unresolved entry points | Count | Required work |
| --- | --- | ---: | --- |
| DOS entry point | `main` | 1 | Reconstruct actual DOS startup/relocation and bind live original linked DS, stack, initial descriptors, assets, CRT initialization and transfer to `gate_exe_play_intro`. An empty `main()` would produce a false success. |
| RTL text/video state | `gate_rtl_video_inversion`, `gate_rtl_video_attribute`, `gate_rtl_video_columns`, `gate_rtl_video_rows`, `gate_rtl_video_tab_base`, `gate_rtl_video_block_mode`, `gate_rtl_video_set_block_mode`, `gate_rtl_video_command` | 8 | **Staged, DOS probe pending.** Original RTL disassembly verifies the seven RTL-DS offsets and pointer dereferences; the separate 16-bit DS must be bound during startup, not assumed to equal EXE DS. The 14-byte command adapter handles only original commands 2/3/9 through BIOS INT 10h. The underlying 12a5:2d8a dispatcher has not been decompiled, and unrecognized commands abort. |
| Pixel and drawing primitives | `gate_rtl_plot_pixel`, `gate_rtl_background_rect`, `gate_rtl_resident_line_to` | 3 | **Staged, DOS probe pending.** The native host's bounded pixel modes 1/2/3, raster operations and clipping are implemented over real DOS VGA. Mode 13h uses A000 linear memory and modes 1/2 use BIOS INT 10h pixel reads/writes, with test pixels/lines/opaque rectangles. Unmodeled modes, diagonal lines and patterned fills abort; the DOSBox test and guest trace comparison remain outstanding. |
| Video adapter query | `gate_rtl_mode_query_adapter`, `gate_rtl_mode_svga_variant` | 2 | **Staged, DOS probe pending.** Original `GATE.RTL:17c95..17d79` maps INT 10h/AX=1a00 active/alternate display codes to type/monitor pairs; `17b4f..17c24` tests AH=6fh, ROM signatures and bank/CRTC ports for SVGA variant 0–5. The old-BIOS fallback and exotic adapter-switch helpers remain outside the supported boundary; unsupported display BIOS requests abort. |
| Original exception/stack state | `gate_rtl_file_divide_fault`, `gate_rtl_file_stack_word` | 2 | Trace original divide-fault exit and near-stack word reader; `0` or `abort()` substitute values would hide original behavior. |
| Runtime calendar sample | `gate_linked_read_runtime_time_sample` | 1 | Reconstruct `12a5:34d8` ten-byte sample and its date/time, timezone/DST dependencies, then compare against the already recovered `gate_rtl_elapsed_milliseconds`. The pending DOS clock is an intentionally approximate live substitute. |
| VFX/picture pipeline | `gate_linked_decompress_picture_pixels`, `gate_linked_blit_vfx_picture`, `gate_linked_transform_picture_palette_indexed`, `gate_linked_transform_picture_palette_rgb`, `gate_linked_save_vfx_draw_state`, `gate_linked_restore_vfx_draw_state` | 6 | The two palette transforms are **staged and host-tested**, DOS probe pending: linked `0cdc:0396`/`:0430` map exactly to original RTL `f4df`/`f579`; sixteen packed 6-bit colors expand into 48 RGB bytes scaled by 21, and the inverse uses signed byte division by 16. Four other VFX services still require decompression, destination pixel layout, and saved drawing-state reconstruction. |
| Intro services and diagnostics | `gate_linked_initialize_intro_services`, `gate_linked_handle_control_key`, `gate_linked_print_intro_diagnostic` | 3 | Reconstruct the original music/IRQ8 setup, Ctrl-Break branch and resident diagnostic destination/format from the EXE and RTL. Silent success and dummy output are not valid equivalents. |
| **Reported total** | | **26** | 15 more definitions staged since this report; 11 projected unresolved after both DOS probes. |

### Current DOS probe order

```sh
nix run -L .#msc51-compile-launcher
nix run -L .#msc51-compile-memory
nix run -L .#msc51-compile-dos-files
nix run -L .#msc51-compile-rtl-state
nix run -L .#msc51-compile-hardware
nix run -L .#msc51-compile-runtime-files
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

Once the linker reports zero missing names, the next milestone is *actual
DOS execution* through the original intro sequence with deterministic
input/frame comparisons. A linked `GATEWAY.EXE` is insufficient by itself.
