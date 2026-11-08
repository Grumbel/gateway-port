<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception -->

# Microsoft C 5.1 launcher link closure

This is the **remaining implementation/verification inventory**, not a list
of functions already recovered. The latest real Microsoft LINK 3.65 report
supplied by the user on 2026-11-08 confirmed **four unresolved names**:
the CRT `_main` and three original game services. An earlier LINK run
confirmed **26 names**, with
`RTLHW`, `RTFILE`, `RESSTATE`, `HEAP`, `INP`, `CLOCK` and `CRTENV` linked.
The extended `RTLHW` adapter supplies eight RTL text/video names, and
`RESSTATE` supplies three bounded raster names. The adapter also now stages
two original BIOS/chipset detection entry points and two exact relocated
RTL palette transforms. The intro's mode-12h/13h VFX blitter is also staged
with bounded host behavior. A new C89 DCL decoder additionally stages the
compressed-picture service, with host-verified buffered DOS input and planar
output. The original resource file wrappers now stage their raw BP-2 stack
slots and unguarded CPU division without two external host-test callbacks.
**Four is now the measured LINK result**, not a prediction. A new bounded
audio startup guard in `RTLHW.OBJ` would lower the unresolved count to
**three if its changed DOS probe passes**; its enabled-music branch still
aborts rather than silently omitting the original IRQ 8 initialization.
The two VFX state definitions now cover only the verified state-flag and
mouse-cursor paths; the video-buffer path explicitly aborts. Do not
manufacture no-op definitions merely to obtain an executable.

## The four names in the latest confirmed LINK report

| Original unresolved name | Source reference | Current work |
| --- | --- | --- |
| `_main` | Microsoft 5.1 `LLIBCR.LIB(dos/crt0.asm)` | No working real-mode original startup yet. It must allocate and bind the original EXE/RTL data segments, initialize and relocate resident driver tables and assets, and transfer to `gate_exe_play_intro`. A placeholder `main()` would make LINK succeed but not create a working Gateway launcher. |
| `_gate_linked_initialize_c306ca35` | `GATE.RTL:c3bd..c6fe`, linked `0a46:01ec` | **Staged as a bounded guard in `RTLHW.OBJ`.** Verified original flags/config early exits are preserved. Enabled music requires `GATE_004.MUS` and an IRQ 8 handler, so the substitute aborts on that branch. The changed `msc51-compile-hardware` DOS probe must pass before re-linking. |
| `_gate_linked_handle_control_key` | `GATE.RTL:157b8..157e4`, linked `123f:0004`, called on input key `0x0003` | The full six-call entry is verified. Its video-state branch reaches `RTL:10da1` and the already reconstructed VFX image destructor `RTL:180fe`; several other shutdown services and the original state binding still require verification. Do not conflate Ctrl-C/Break cleanup with ordinary keyboard flushing. |
| `_gate_linked_print_intr_b006848b` | `GATE.RTL:170c7..1712d`, linked `1258:172e`, called by image sequence failure paths | The complete five-call entry is verified; it formats into a 258-byte local buffer, calls resident output, and polls both mouse and keyboard helpers. The full callee ABI, output behavior and failure paths are not yet restored; a no-op would hide loader failures. |

The `tools/inspect_msc51_link_tail.py` command inspects these three live
FAR targets against the original binary images in a captured conventional
RAM snapshot. **The 39-second `checkpoint-book_taken.bin` capture is too
late for these startup identities.** Its music target contains an unrelated
RTL stream reader at `RTL:7ce0` (the alleged entry starts *inside* a JNE
instruction, and the original `RTL:c3bd` music-guard relocations are absent).
Its control-key candidate at `RTL:eeba` likewise begins inside the operand
of the original FAR CALL at `RTL:eeb6`; the first byte at the purported entry
does not match. The diagnostic target has no match. They are *unverified code
windows*, not original routine identities, and the tool now excludes them
from `live_source_match`. A later RAM capture can contain valid code but no
longer contain the code that the startup FAR calls originally reached.

The user's **22-second** capture instead exposes both original resident
entries. At linked `123f:0004`, the first 32 live bytes match 12 of 12
*non-relocated* bytes of `RTL:157b8` (four consecutive FAR calls, followed
by `mov ax,1; push ax; call far; add sp,2`; one more FAR call and `RETF`
follow). At linked `1258:172e`, the first 32 bytes match 26 of 26 stable
bytes at `RTL:170c7`: `push bp; mov bp,sp; sub sp,102h`, a relocated FAR
call, and arguments prepared for a second FAR call. The raw prefix matcher
reported only 1 and 8 bytes respectively because the FAR-call target
operands are linked by the DOS loader. Neither is evidence that the later
39-second occupants are the same functions.

The inspector now separately verifies **entry-anchored** signatures by
skipping *only* known four-byte FAR-call relocation operands. For control
it compares the full 45-byte function (21 invariant bytes, six FAR calls).
For diagnostics it compares the first 45 bytes (37 invariant bytes, two FAR
calls). On a successful check, `entry_status=relocated_original_entry` and
`relocated_far_calls` lists their actual linked call operands. This verifies
code identity, **not** the callees' behavior, arguments or DOS-equivalent
implementation. The user's 22-second rerun succeeded on 2026-11-08:
both relocated entries validated against all 45 bytes.
The original diagnostic actually continues to `rtl:1712d` and has three
more FAR calls at `rtl:17103`, `1710c`, and `17115`. The inspector now
separately checks that full 103-byte body (83 invariant bytes, five FAR
calls), returning `complete_original_entry.matched` and all five target
operands only if the longer check succeeds. If it fails, the established
45-byte/two-call result remains intact. The user's 22-second guest snapshot
**passed the complete 103-byte check**, including all 83 invariant bytes.
The **eleven directly observed relocated FAR operands** are:

| Original call site | Linked target (22-second runtime) | RTL source projection | Evidence/status |
| --- | --- | --- | --- |
| `rtl:157b8` | `221b:021f` | Unknown | Bank not yet identified; raw `rtl:15793` window falls in data, not a callee entry |
| `rtl:157bd` | `1a46:01c0` | `rtl:c391` | Existing bank `0a46` projects here |
| `rtl:157c2` | `1e48:0202` | `rtl:10da1` | Entry-anchored, two FAR and four WORD relocations; matches user's live 48-byte entry |
| `rtl:157c7` | `1c4d:008b` | `rtl:e771` | Existing bank `0c4d` projects here |
| `rtl:157d0` | `1f10:035a` | `rtl:11e5f` | Existing bank `0f10` projects here |
| `rtl:157dc` | `12a5:0b40` | `rtl:621e` | Existing bank `02a5` projects here |
| `rtl:170ce` | `1cdc:06de` | `rtl:f827` | Existing bank `0cdc` projects here |
| `rtl:170e4` | `12a5:33ec` | `rtl:8aca` | Existing bank `02a5` projects here |
| `rtl:17103` | `1e48:07fe` | `rtl:1139d` | New entry-anchored VFX/mouse-state routine, original flag WORD relocated |
| `rtl:1710c` | `1df2:000a` | `rtl:1054d` | New bounded 38-byte FAR routine, DS counter WORDs relocated |
| `rtl:17115` | `1df2:0030` | `rtl:10573` | New 48-byte entry prefix, DS counter WORDs relocated |

The six *older* file offsets are **bank-base projections**, not independently
verified callee entry matches. Existing Ghidra preparation validates each
bank with aggregate matches and independent witnesses on the intro snapshot.
The inspector now performs those checks again before returning an
`original_source_projection` per call, including the exact prefix-match
count at that *callee*. If the bank validation fails or the call points
outside known bank bounds, it reports only a `raw_source_candidate` (when
one exists). It does not promote a mere interior byte-window match into
a verified function identity.

The first call's raw `rtl:15793` byte-window match is misleading: static
disassembly places it in a data region. It must remain unknown until its
original file location and resident bank can be identified independently.
The 48 live bytes do reveal a bounded branch: it compares signed
`DS:156e` to zero and, when nonnegative, pushes `DS:6152` and `DS:6150`,
calls resident `12a5:0f62`, sets `DS:156e=ffff` and `DS:1570=0000`,
then returns FAR. This is an observed runtime code skeleton, **not** a
validated original source identity or a sufficient ABI specification.

The third call is now identified as `rtl:10da1..10dc6`, a bounded FAR
routine (preceded and terminated by `RETF`). It starts with `push cs; call
near rtl:11078`, tests bits `0x0c` of a relocated DS byte, conditionally
passes two FAR pointers (originally `DS:0000` and `DS:0080`) to FAR callees,
then clears the same relocated DS word and returns. Its original 38 bytes
contain **six relocated operands**: two FAR targets, two references to the
same DS word, and two immediate buffer addresses. In the user's live bytes,
these become `DS:13d2` twice and `DS:555c`/`DS:55dc`; the address
difference remains `0080h`. The 26-invariant-byte initial matcher was too
strict because it did not account for those buffer-address relocations.
The revised check compares **22 truly invariant bytes**, preserves the
original DS-word equality and buffer-address difference, and checks both
FAR opcodes. Rechecking the actual 48 live bytes supplied by the user
against the pinned original `GATE.RTL` now succeeds. Both nested FAR calls
target the same observed runtime entry, `245d:016c`. Its original source
has now been located as described below. The video-state lifecycle is not
yet ported to the Microsoft launcher.

`245d:016c` is an **already reconstructed VFX release service**:
`decomp/launcher/gate_vfx_image.c` implements it as
`gate_rtl_graphics_release`. The latest 48 live entry bytes match original
`rtl:180fe` after masking *only* a relocated DS-segment immediate and one
FAR-call target. The original bounded routine ends with `RETF 4` at
`rtl:18151`. The inspector now also checks its complete 86-byte body
(**80 invariant bytes**) and reports the downstream FAR call. Its behavior:

1. Preserve segment/index registers and load the resident graphics DS.
2. Reject a non-`CA00h` image record with status `ffe4h`.
3. Load the pixel-data FAR pointer from record offset `18h` and call
   resident `253a:0048` to release the allocation.
4. On successful release, overwrite the complete `80h`-byte image record
   with `ffh` and return zero. On failure, leave the record intact.

The invoked `253a:0048` is already named
`gate_linked_release_vfx_pixels` and has a bounded real-mode DOS heap
adapter in `decomp/launcher/dos/msc51_heap.c`. The inspector reports that
existing port symbol separately from any original file-source match and
descends another FAR level to expose the live allocator-entry bytes. This
does **not** establish that the complete Ctrl-Break cleanup is implemented:
other shutdown calls and the original startup state bindings are missing.

Three additional diagnostic callees now have strong original entry
boundaries, not merely byte-window guesses:

- `1e48:07fe` maps to `rtl:1139d`, immediately after `RETF` at `rtl:1139c`.
  It tests flags `& 28h`, calls a same-segment helper and submits an
  `AX=0033h` resident request. The live 48-byte prefix matches 42
  invariant bytes after the DS operand and FAR target are excluded.
- `1df2:000a` maps to full FAR routine `rtl:1054d..10572`, which sends a
  request with argument `1` and tests two relocated DS counters. The linked
  counter offsets are `1340h` and `133eh`, preserving their original
  two-byte difference. Its 30 invariant bytes match.
- `1df2:0030` maps to `rtl:10573`, immediately following that `RETF`.
  Its opening branch decrements `DS:133e` and submits `AX=0021h`; the
  48-byte inspected prefix has 40 invariant bytes. The remainder of this
  function is not yet verified.

These entry pairs independently imply consistent resident bank mappings:

| Resident segment (load-relative) | Original file base | Verified entry anchors |
| --- | --- | --- |
| `0e48` | `rtl:10b9f` | `0202 → 10da1`, `07fe → 1139d` |
| `0df2` | `rtl:10543` | `000a → 1054d`, `0030 → 10573` |

The inspector now reports `corroborated_resident_bank` only when two
distinct entry-anchored matches imply the same base within a segment. This
does **not** validate every byte in the intervening bank or prove all
possible callee entries there.

For other unmatched FAR targets, the inspector now includes 48 live entry
bytes as `live_entry_hex`, allowing analysis of the unknown `221b:021f`
callee without treating a file-wide byte-window match as proof of identity.
Unknown nested targets are similarly reported with their live bytes. This
does not read past a 1-MiB RAM capture, and does not claim that byte strings
are disassembled or executable entry points.

Static disassembly gives leads, **not completed services**: `rtl:c391`
tests a music/device flag and calls a same-bank helper; `rtl:e771` iterates
two records; `rtl:11e5f` reads a device descriptor and dispatches at least
two numbered requests; `rtl:621e` performs multiple near calls; `rtl:f827`
branches on a resident mode selector; `rtl:8aca` initializes a FAR buffer
descriptor and passes it to another formatter. Actual DOS side effects,
the original source for `221b:021f`, and the remaining diagnostic
input-polling behavior have yet to be established. None of these
observations justifies replacing
the Ctrl-Break or diagnostic routines with a stub.

Capture an **earlier** original 22-second intro image (the same canonical
snapshot described in [intro-ghidra.md](intro-ghidra.md)). The `probe`
Nix app builds the executable as needed; do not assume the special
`build/local-probe/` manual build exists:

```sh
mkdir -p build/intro-analysis
nix run -L .#probe -- ../gateway-dos/GATE.EXE \
  --headless --read-only --deterministic-clock --milliseconds 24000 \
  --dump-at 22000:build/intro-analysis/at-22000
```

Then run the improved inspector on this earlier snapshot:

```sh
python3 tools/inspect_msc51_link_tail.py \
  --exe ../gateway-dos/GATE.EXE \
  --rtl ../gateway-dos/GATE.RTL \
  --ram build/intro-analysis/at-22000.bin --json
```

`original_music_entry` certifies the original music entry's *two
independently verified relocated DS operands*; `relocated_original_entry`
certifies a known control/diagnostic entry's stable instruction skeleton.
A `source_candidate` is only an internal byte-window match and does **not**
certify function identity. For the third control-key callee, a successful
`relocated_source_match` verifies its exact relocation-masked original
function body and exposes `callee_far_calls`. The control and diagnostic
routines still require callee traces and calling-convention evidence before
adding linker definitions.

## Historical 26-name LINK report (superseded)

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
| **Reported total** | | **26** | 20 more definitions and two original-stack/fault call-site replacements staged since this report; 4 projected unresolved after DOS probes. |

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

The previously missing `gate_linked_save_vfx_draw_state` and
`gate_linked_restore_vfx_draw_state` **are present in the original RTL**:
`GATE.RTL:0x11078..0x1111f` and `0x10f64..0x11077`. The three far calls
from the frame renderer `rtl:0x12ed7` and their original source identities
are independently captured in `docs/picture-conditions.md`.

These two functions are *not* simple copies of `DS:29de` drawing words.
Their original 8086 branches inspect a linker-relocated flag WORD, test
bit 0 and bit masks `0x30` and `0x0c`, and may call additional resident
services. Some of those calls receive rectangle coordinates, while the
software-interrupt wrapper receives interrupt number `0x33`.
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
The old `build/rgn-scenes-v026/before.bin` path was a private research
checkpoint, **not** an artifact created by `nix build` or the Microsoft
compiler probes. It may not exist in a fresh checkout. Generate a new
scene-specific capture from the original game instead (build the normal
`gateway` package first if the probe is missing):

```sh
nix build . -L
python3 tools/run_walkthrough.py run \
  tests/fixtures/gateway-opening.walk \
  --probe result/bin/gateway-probe \
  --data ../gateway-dos \
  --output build/vfx-replay --capture-labels
```

The walkthrough records the unmodified game after it reaches Your Quarters.
It produces `checkpoint-book_taken.bin`, `checkpoint-book_dropped.bin` and
`checkpoint-opening_complete.bin`, each a 1 MiB guest-memory capture.
Inspect any of these snapshots with the game's original files outside Git:

```sh
python3 tools/inspect_rtl_animation.py \
  --ovl ../gateway-dos/GATEMAIN.OVL \
  --exe ../gateway-dos/GATE.EXE \
  --rtl ../gateway-dos/GATE.RTL \
  --ram build/vfx-replay/checkpoint-book_taken.bin \
  --vfx-state --json
```

The inspector explicitly reports a missing `--ram` file and this replay
recipe instead of propagating an uncaught `FileNotFoundError`. A RAM capture
from the early 22-second intro is **not interchangeable**: it may precede
loading the animation and VFX code bank needed to resolve these FAR calls.

The user's original 2026-11-08 `checkpoint-book_taken.bin` report confirms
that **all 27 relocated DS operands agree**, and supplies these load-specific
addresses. These are not unrelocated offsets in the original `GATE.RTL`:

| Original field | Linked memory offset |
| --- | --- |
| VFX flags and mode masks | `DS:13d2` |
| Width/height segment pointers | `DS:8c86`, `DS:8c88` |
| Clipped width/height | `DS:9f9c`, `DS:9f9e` |
| Cursor X/Y | `DS:9f34`, `DS:9f36` |

The same live scene maps eight original VFX service call sites to four unique
FAR entries. Their identities and downstream semantics must remain distinct:

| Call-site group | Captured FAR target | State of evidence |
| --- | --- | --- |
| Save/restore prepare | `2251:032a` | Now matched to `RTL:f473`, VGA vertical-retrace wait |
| Both request-`0x33` paths | `1805:2d84` | Now matched to `RTL:8468`, general interrupt wrapper (INT 33h here) |
| Save/restore draw | `2a1c:000c` | Already matched independently to original `RTL:184e8` VFX drawing service |
| Two restore rectangle operations | `2a71:0004` | Now matched to `RTL:18a95`, clipped video-region transfer dispatcher |

The extended inspector includes `resident_far_sources` for each call, matched
from its actual 16-byte guest entry to an original RTL/EXE offset whenever
unambiguous; a missing/overwritten entry is emitted as `null`, not guessed.
This additional step established the mouse mode-mask `0x30` semantics;
the backing-buffer mode `0x0c` remains unverified. A separately observed
VFX-entry DS can be passed with `--vfx-ds HEX` for flags at capture time; animation entry
traces for neighboring routines are not by themselves proof of that DS.

If a VFX entry trace independently establishes the live DS segment, supply
it with `--vfx-ds HEX` to inspect the flag WORD at that **capture time**;
the analyzer does not assume the screenshot's DS is the service's DS, or
that the capture necessarily exercised a particular branch. Its synthetic
tests cover relocated operands, inconsistent linker sites, state-flag
branch selection and callback extraction. An optional SHA-256-pinned original
binary test checks all 27 opcode/displacement pairs, both FAR entry points
and eight unrelocated FAR CALL sites.

The user's second `checkpoint-book_taken.bin` report uniquely matches all
four callback entry points against their original RTL file locations:

| FAR callback (that one load only) | Original RTL entry | Verified purpose |
| --- | --- | --- |
| `2251:032a` | `rtl:f473` | Poll the VGA input-status port until retrace bit 3 goes low, then high; the port is indirect through the driver's DS word at `+6` |
| `1805:2d84` | `rtl:8468` | Execute the requested software interrupt using an input/output register block; the callers supply interrupt `33h`, AX=2 (hide mouse) on save and AX=1 (show) on restore |
| `2a1c:000c` | `rtl:184e8` | The previously verified VFX image renderer |
| `2a71:0004` | `rtl:18a95` | Validate and clip a video-region transfer, then dispatch through the selected video driver's FAR callback at descriptor `+2e`; called twice during restore |

These are not EXE entry points, and each runtime FAR address is valid for
that captured loading layout only. Each original RTL offset was separately
checked against its 8086 instructions. The inspector now labels callbacks
only if both the file-offset match and instruction prefix agree. An entry
match alone cannot prove that any particular branch executed in the capture.

The original `rtl:11078` save returns zero unless the flag WORD at relocated
`DS:13d2` is nonzero with bit 0 set; it then clears **only bit 0**. The
corresponding `rtl:10f64` restore requires a nonzero flag WORD, a cleared
bit 0, and a nonzero saved argument, then sets **only bit 0**. If `flags &
30h` is nonzero, the original calls INT 33h AX=2 (save) or AX=1
(restore). That branch wins over `flags & 0ch` when both masks are set.

`RESSTATE.OBJ` now stages these verified flag-only and mouse-cursor paths.
Host tests check early returns, preserved high bits, balanced INT 33h mouse
calls, branch priority, and the unusual `flags=1` case where clearing bit 0
leaves no state to restore. The DOS probe checks the flag-only path.
**These are bounded adapters, not the full original behavior.** When `flags
& 0ch` is set without the `30h` mouse branch, they abort explicitly. That
branch still requires the meanings of the two backing-region descriptors
(`DS:0000` and `DS:0080` in the original routine's DS), `rtl:10dc7`
coordinate transformation, and source/destination behavior of `rtl:18a95`.
It must not be silently approximated as a CA00 image blit. Rerun
`msc51-compile-resident-state` before the next Microsoft LINK attempt.

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
