<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception -->

# Microsoft C 5.1 launcher link closure

## Verify unresolved names against compiled DOS OMF objects

The Microsoft LINK report listed `_gate_linked_print_intr_b006848b` as
unresolved. However, the current generated `RTFILE.C` defines a function
with exactly this name (the deterministic, 31-character alias of
`gate_linked_print_intro_diagnostic`), and `LINK.RSP` explicitly includes
`RTFILE.OBJ`. This is **not yet proof** that the user's compiled
`RTFILE.OBJ` actually exports that symbol. It is also not proof that the
bounded current diagnostic printer reproduces the original five-call
`GATE.RTL:170c7..1712d` formatter-and-input loop.

Run this source-independent OMF audit on the actual Microsoft C build:

```sh
python3 tools/msc51_object_symbols.py \
  --stage build/msc51-launcher \
  --symbol _main \
  --symbol _gate_linked_handle_control_key \
  --symbol _gate_linked_print_intr_b006848b
```

The tool reads each `*.OBJ` named in `LINK.RSP`, validates its record
boundaries and checksums, and extracts actual `PUBDEF` names. When
`LINK.LOG` exists, it also marks names reported unresolved by LINK but
already exported by one of its input objects. That condition points to a
stale LINK run or another linkage inconsistency; it is **not** evidence
that a new function implementation is needed. Missing objects and invalid
OMF records are reported explicitly. The four synthetic unit tests cover
32-byte decorated symbols, PUBDEF/PUB386 encodings, malformed records,
multi-module inputs and the missing-versus-exported distinction. There
are no real DOS `.OBJ` files in this environment, so the result for the
user's latest LINK run remains to be collected.

The historical Microsoft C 5.1 compiler recognizes 31-character C names
and decorates them with an underscore, giving a **32-character OMF public**;
the [Microsoft KB Q70249](https://jeffpar.github.io/kbarchive/kb/070/Q70249/)
notes that the limitation arises when an old MASM module truncates such a
name, not that two C translation units cannot share it. We therefore
cannot infer a truncation bug merely from this unresolved name.

## DOS image owner for the future real entry point

`decomp/launcher/dos/msc51_startup_bootstrap.c` now owns the first two
independent DOS allocations for the rebuilt source-C entry: `100ah` paragraphs
for the overlapping lower/data windows and `03d0h` paragraphs for the
15,616-byte resident bank. `gate_dos_boot_prepare` opens caller-provided
`GATE.EXE` and `GATE.RTL` paths read-only, composes the two already-verified
images at the *actual* allocated segment addresses, checks their independent
segment-normalized fingerprints `F332BF93` and `5A991491`, then closes both
handles. Allocation, file-open, copy, relocation, checksum and close failures
all discard the partial image and unbind guest DS. A second prepare on an
owned image is rejected. The public release operation frees both allocations.

`tools/msc51.py` stages `BOOT.C`, `BOOT.H` and a standalone `BOOTCHK.EXE` probe
in the regular `msc51-compile-data` batch. When provided with pinned original
files, the strict DOS log validation requires the new original-file owner
success line **in addition** to both existing full-image CRC witnesses. The
host C89 test exercises eleven allocation/file/CRC outcomes and the invalid
callback preflight, including failures at either file close. This verifies
the new ownership contract synthetically; no Microsoft C 5.1 or DOSBox
runtime could be executed in this environment.

On the user's NixOS/DOSBox setup, the relevant complete verification is:

```sh
nix run -L .#msc51-compile-data -- --original-game-dir ../gateway-dos
```

`BOOTREAL.LOG` must contain the exact owner `PASS` line; `DATREAL.LOG` must
also contain both original CRC witnesses. A failed or absent log is not a
successful test. `BOOT.OBJ` is deliberately not included in `LINK.RSP` while
no valid source-C `_main` can consume it.

The standalone DOS probe supplies **explicitly nonexecutable** FAR callback
pointers and external segments solely to test code/data placement and CRC
normalization. These are not wired executable entry points. The owner does
not create the missing other resident banks, establish the recovered code
services or handle original IRQ setup, so `_main` and the two remaining
linker externals are *still unresolved* and `GATEWAY.EXE` is **not playable**.
The original RTLink loader used a PSP-owned block rather than the separate
allocations chosen for the new C startup; no claim of identical DOS MCB
behavior is made.

This is the **remaining implementation/verification inventory**, not a list
of functions already recovered. The latest real Microsoft LINK 3.65 report
supplied by the user on 2026-11-08 confirmed **three unresolved names**:
the CRT `_main`, the original Ctrl-Break handler and the diagnostic. An earlier LINK run
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
**Three is now the measured LINK result**, not a prediction: the original
music-init external is no longer listed. The bounded audio startup guard
in `RTLHW.OBJ` covers the proven no-music branches, but its enabled-music
branch still aborts rather than silently omitting the original IRQ 8
initialization. The separately modified heap DOS probe is still pending.
The two VFX state definitions now cover only the verified state-flag and
mouse-cursor paths; the video-buffer path explicitly aborts. Do not
manufacture no-op definitions merely to obtain an executable.

## Whole-bank DOS diagnostic after the source-exact reconstruction

The first 15,616-byte resident bank matches all four authentic
125/175/250/500ms original RAM captures byte for byte when reconstructed
at their observed layout. `DATAREAL.EXE` now also checks **the entire
relocated bank on actual DOS**, not just representative call/data operands,
by calculating standard CRC-32 over its contents. Exactly 113
caller-selected segment WORDs (226 bytes) are normalized to zero; the
other 15,390 bytes include original-file code/data, fixed relocation
targets, and the ten-byte `GATE.EXE` continuation. The independently
measured original-run CRC-32 is **`F332BF93`** in each capture. The DOS
implementation derives the mask directly from its relocation tables.

The strict `msc51-compile-data --original-game-dir ...` log validator now
requires this full-bank witness and rejects the older sample-only success
line. A GCC C89 host run verifies the same checksum with a different
resident/DS/EXE-callback segment placement and rejects corruption outside
the mask.

The complete `gate_dos_prepare_intro_data` entry point now has the same
kind of independent end-to-end check. The original 125/175/250ms captures
contain an exactly identical **19,690-byte** prepared data image (RTL
prefix plus EXE static data). The reconstructed C89 loader reproduces
every byte in all three captures, including all earlier initialization
fixups. At 500ms the running game has changed 237 of these bytes, so that
late snapshot is not an initialization oracle. For differently allocated
DOS segments, the new `gate_dos_intro_data_crc32` masks 48 variable WORDs
(the 27 DS/CS, 19 external bank segments, and two callback pointer words),
and checks the other **19,594 bytes**. All three early original snapshots
give CRC32 **`5A991491`**. The host tests verify that value at two memory
layouts and catch changed bytes outside those 96 masked bytes.

`DATAREAL.EXE` now checks both *composed* images against their original
fingerprints. The callback needed by the data composition is sourced from
the original RTL code into a DOS stack buffer, **never called**. The strict
original-file log validator rejects prior success strings containing only
the bank CRC or representative checks. This is a stronger staging gate,
**not** yet a verified DOSBox
result in this environment or a completed rebuilt executable: `_main`,
two diagnostic/cleanup services, the other resident banks and the
executable's service wiring are still missing.

## The three names in the latest confirmed LINK report

| Original unresolved name | Source reference | Current work |
| --- | --- | --- |
| `_main` | Microsoft 5.1 `LLIBCR.LIB(dos/crt0.asm)` | No working real-mode original startup yet. It must allocate and bind the original EXE/RTL data segments, initialize and relocate resident driver tables and assets, and transfer to `gate_exe_play_intro`. A placeholder `main()` would make LINK succeed but not create a working Gateway launcher. |
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
| `rtl:157b8` | `221b:021f` | `rtl:15706` | Newly matched 36-byte FAR cleanup, five relocated DS words in two groups and one FAR call |
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
disassembly places it in a data region. Comparing the *actual instruction
skeleton* against the pinned RTL, ignoring only relocatable DS references
and the FAR target, instead identifies **exact original entry `rtl:15706`**.
Its 36-byte body ends in `RETF` just before a `NOP` and embedded data. All
22 invariant bytes match the user's 48-byte live prefix. The original
compares signed `DS:0016` against zero; the runtime references to this
word and its `0018` neighbor become `DS:156e/1570`, a consistent `+1558h`
shift. The separate original arguments at `DS:0004/0002` become
`DS:6152/6150`, with a **different** shift of `+614eh`; treating every
DS address as one uniform relocation would be incorrect. When the flag
is nonnegative, the routine pushes those two words, calls resident
`12a5:0f62`, sets the relocated flag to `ffffh` and neighbor to zero, and
returns FAR. The inspector now checks the complete bounded function and
exposes this nested FAR call on success. The latest user rerun confirms
all **22 invariant bytes**, reporting that target as `12a5:0f62`.
Its source is `RTL:6640` by an independently validated `02a5` resident
bank, with 16 matching entry-prefix bytes. The original function starts
with a FAR descriptor and bit tests on byte offset `0ah`, then calls a
helper using its FAR pointer. To avoid treating the entire long function
as verified on a 16-byte prefix, the inspector now checks **only its first
48 bytes** after masking a single FAR call at `RTL:6663` (44 invariant
bytes). On success it will expose that call's live target; the rest of the
function, nested callee behavior, and overall shutdown ABI remain
unverified.

The latest source-bank projection of that first call is now confirmed by
the user's new 22-second capture: **all 44 invariant bytes of the
`RTL:6640` 48-byte prefix match**, and its call at `RTL:6663` targets
`12a5:17da`. This target independently projects to original `RTL:6eb8`,
with 16 matching entry-prefix bytes. The original bounded function
`RTL:6eb8..6f45` handles a buffered stream flush: it examines the FAR
stream descriptor's flags, conditionally sends pending bytes to a FAR write
service, sets error flag `20h` and returns `ffffh` on a short write, and
resets the stream's current pointer to its base even when no write occurs.
It is 142 bytes long with just one relocated FAR call at `RTL:6f0f`
and two independent immediate WORD relocations at `RTL:6ed8/6eea`.
The user's subsequent 22-second output **passed the complete check of 134
invariant bytes**. The verified FAR call at original `RTL:6f0f` targets
`12a5:2686` in that live run, independently projecting through the validated
`02a5` bank to `RTL:7d64` (decimal 32100). This is no longer just a
16-byte flush entry projection; the whole bounded flush body is confirmed.

Static original-byte disassembly places `RTL:7d64` at the start of a
DOS/CRT-style **file-descriptor write routine**. Its prologue takes the
descriptor at `[BP+6]`, checks a relocated DS descriptor bound, tests the
descriptor's `20h` append and `80h` text-mode flags, optionally seeks to end
with INT 21h/AX=4202h, and processes newline conversion on the text-mode
path before issuing INT 21h/AH=40h writes. The original body reaches
`RTL:7eab` and branches to an epilogue elsewhere; its complete
argument/flag/error semantics remain unverified. This CRT-style descriptor
writer must **not** be conflated with the already reconstructed raw DOS
file-write wrapper `gate_linked_dos_file_write`.

The inspector now offers a **100-byte entry-anchored prefix check** at
`RTL:7d64`. The user's subsequent capture exposed two additional relocated
flag-field WORD operands: original `RTL:7d7c/7d8e` both link to live
`DS:2bf0`, while the bound at `RTL:7d6f` becomes `DS:2bee`.
The initial 94-byte signature rejected the actual runtime entry correctly
because those two operands were still treated as unchanged instructions.
The revised check masks the descriptor-bound WORD, validates the equality
of the two flag-field WORDs, and masks the FAR target at `RTL:7dbd`:
**90 invariant bytes**. Only on success does it expose that helper's
relocated FAR target. This check was then exercised against the authentic
snapshot as described next; the CRT identity alone does not implement
the unresolved Ctrl-Break wrapper.

The user's next 22-second result **matched all 90 invariant bytes** of
that corrected writer prefix, verifying both relocated descriptor-flag
references and exposing its FAR call at `RTL:7dbd` to live `12a5:27d0`.
The independently validated `02a5` bank projects that target to original
`RTL:7eae` (decimal 32430), although the raw prefix matcher agrees on only
three bytes because the next instruction contains a relocated DS address.
Static disassembly identifies the complete `RTL:7eae..7ec1` FAR routine:
it saves the FAR return offset/segment in CX/DX, compares SP with a global
stack bound, returns the positive free-stack distance in AX or zero if
the bound has been reached, restores the return address and executes RETF.
The original 20-byte body has one relocated global-address WORD at
`RTL:7eb1`, leaving **18 invariant bytes**. The inspector now checks that
whole leaf routine without inventing any more nested calls. The user's
next authentic 22-second run **matched all 18 invariant bytes** at original
`RTL:7eae`; its verified `callee_far_calls` is empty. This closes only the
writer's nested stack-space leaf, not the entire Ctrl-Break handler or the
complete CRT descriptor-writer ABI.

## Next startup evidence for the remaining `_main` gap

The original `GATE.EXE` MZ header is 32 bytes and names initial relative
`CS:IP=002c:0071`, corresponding to original file offset `exe:0351`.
The source instructions at `exe:0351` call a loader helper at `exe:0359`
then FAR-jump indirectly through `CS:0004`. That is an **RTLink loader
bootstrap**, not the missing Microsoft C 5.1 `_main` of the rebuilt
source-C launcher. Before implementing that new entry point, obtain
original startup RAM **and registers**, not just the much later
22-second resident bank view:

```sh
mkdir -p build/msc51-startup
nix run -L .#probe -- ../gateway-dos/GATE.EXE \
  --headless --read-only --deterministic-clock --milliseconds 2000 \
  --dump-at 0:build/msc51-startup/at-0 \
  --dump-at 10:build/msc51-startup/at-10 \
  --dump-at 100:build/msc51-startup/at-100 \
  --dump-at 500:build/msc51-startup/at-500 \
  --dump-at 1500:build/msc51-startup/at-1500
```

The probe accepts multiple `--dump-at` values and writes `.regs`, `.bin`,
and `.ppm` for each prefix. The `.regs` files reveal evolving CS:IP,
DS/ES and SS:SP; the `.bin` files permit byte-level comparison with the
original MZ relocation table and resident segment load progress. The
two-second run and all five dumps are independent of the launcher LINK
attempt. Merely matching original loader startup does not supply an
equivalent reconstructed DOS `main()`.

### Original startup capture: observed relocation and segment transition

The user's five original 1 MiB `.bin` and `.regs` captures from the above
command were inspected on 2026-11-08. The original EXE/RTL SHA-256 values
match the pinned inputs in `docs/executable.md`. The new read-only analysis
command makes these checks repeatable; the proprietary captures stay outside
Git:

```sh
python3 tools/inspect_msc51_startup.py \
  --exe ../gateway-dos/GATE.EXE \
  --rtl ../gateway-dos/GATE.RTL \
  --snapshot-dir build/msc51-startup --json
```

The MZ image has **zero entries in its standard relocation directory**.
At 0ms its complete 22,936-byte post-header image appears **identically**
at physical `10000h`, with the initial `CS:IP=102c:0071` pointing to
`exe:0351` and `SS:SP=3229:0400` matching the MZ header. That tells us
what DOS loaded; it does **not** mean RTLink performs no relocations later.

| Capture | CS:IP | DS / SS:SP | EXE image identical bytes | Resident `02a5` bank identical bytes | Entry source |
| --- | --- | --- | ---: | ---: | --- |
| 0ms | `102c:0071` | `0ff0` / `3229:0400` | 22,936/22,936 | 471/15,616 (not present) | `exe:0351` (16 exact bytes) |
| 10ms | `3259:13b2` | `1e60` / `3229:03ba` | 18,636/22,936 | 471/15,616 (not present) | Unmapped; loader image not source-matched |
| 100ms | `3259:103c` | `348a` / `3229:03c4` | 298/22,936 | 14,677/15,616 (**verified**) | Unmapped; loader image not source-matched |
| 500ms | `12a5:3570` | `2a7a` / `2a7a:7a1a` | 268/22,936 | 13,956/15,616 (**verified**) | `rtl:8c4e` (16 exact bytes) |
| 1500ms | `12a5:3bc5` | `2a7a` / `2a7a:7a08` | 268/22,936 | 13,956/15,616 (**verified**) | `rtl:92a3` (16 exact bytes) |

The independently established original resident bank is
`RTL:56de..93dd`, loaded at relative `02a5` (physical `12a50h` for
this run). The three independently selected witnesses `RTL:8298`,
`RTL:91d8` and `RTL:91ea` pass `prepare_intro_ghidra.validate_bank`
at 100, 500 and 1500ms, not at 0 or 10ms. Differences within the bank
include fixups and changed runtime content; the high aggregate byte count
**does not** establish byte-for-byte unrelocated code. The precise transition
to resident execution happens **between the 100ms and 500ms samples**;
the snapshots do not resolve a more exact time.

From the 500/1500ms register agreement (`DS=ES=SS=2a7a`) we can also
sample that **same future runtime data segment at every earlier instant**.
This is different from sampling the active loader `DS` at 10 or 100ms:

| Sample | `[2a7a:065c]` | `[2a7a:065e]` | `[2a7a:0660]` | `[2a7a:0662]` |
| --- | ---: | ---: | ---: | ---: |
| 0 and 10ms | `0000` | `0000` | `0000` | `0000` |
| 100ms | `007f` | `0000` | `0000` | `0000` |
| 500 and 1500ms | `007f` | `0001` | `0401` | `005a` |

This bounds some resident-state initialization to 10–100ms and other
writes to 100–500ms, without claiming their full original semantics.
The original startup switches both data and stack to `2a7a` before the
500ms capture, replacing the MZ's initial `3229` stack. The rebuilt
Microsoft-C launcher still needs its *own* real-mode `_main`: reproduce
the necessary memory/segment bindings and initializations as independently
verified services, then enter the reconstructed intro. Copying the old
bootstrap wholesale, or adding a no-op CRT `_main`, would not close the
functional gap. This capture resolves a **mapping/timing question**, not
any of the three outstanding LINK externals.

### Finer 125–450ms capture: original timer calibration, not an idle loop

Five more independent original `.regs`/`.bin` pairs on 2026-11-08
pinpoint the phase changes further. The same read-only inspector accepts
both capture directories, still requiring the 0ms baseline:

```sh
python3 tools/inspect_msc51_startup.py \
  --exe ../gateway-dos/GATE.EXE --rtl ../gateway-dos/GATE.RTL \
  --snapshot-dir build/msc51-startup \
  --snapshot-dir build/msc51-startup-fine --json
```

| Sample | CS:IP | DS / ES / SS:SP | Observation |
| --- | --- | --- | --- |
| 125ms | `3259:10ae` | `348a` / `3259` / `3229:03c4` | Loader still running; `02a5` bank reaches the stable 13,956/15,616 original-byte match |
| 175ms | `3259:0b93` | `3445` / `1ce0` / `3229:03da` | Original loader, old stack |
| 250ms | `3259:1029` | `3445` / `3259` / `3229:03cc` | Original loader, old stack |
| 350ms | `12a5:013b` | `2a7a` / `26af` / `2a7a:7a4a` | Resident timer calibration; new data and stack segment |
| 450ms | `12a5:0140` | `2a7a` / `26af` / `2a7a:7a4a` | Still in the original calibration loop |
| 500ms | `12a5:3570` | `2a7a` / `2a7a` / `2a7a:7a1a` | Calibration finished; next resident startup stage |

This narrows the original loader-to-resident transition to **250–350ms**,
not a single observed timestamp. The stable resident bank appears by
125ms, well before the actual execution/stack handoff.

Both 350ms and 450ms PCs project to the *interior* of an original
bounded 58-byte routine `RTL:5806..583f`: respectively `rtl:5819`
and `rtl:581e`. The **entire routine** matches the pinned RTL except
three known 16-bit relocated operands (**52 invariant bytes**):
the original two zero DS operands become the *same* `DS:05c8`, and
the count result stores into `DS:05ba`. The routine pushes registers,
repeats an arithmetic counter loop while a byte flag is nonzero,
and saves the calibrated count before returning near. The status byte
is `1` at both 350ms and 450ms; the result is still `ffffh` there.
By 500ms the flag has cleared and the stored result is `35f3h`.

The interrupt-driven flag is independently confirmed. The complete
28-byte **original IRET routine `RTL:58e2..58fd`** matches all
**24 invariant bytes** in the resident bank; the other four encode
the relocated `mov ax,2a7a` (temporary DS) and `dec byte [05c8]`.
The 8086 interrupt vector for **INT 1Ch** changes as follows:

| Sample | Vector at physical `0070h` | Status |
| --- | --- | --- |
| 250ms | `f000:0200` | BIOS/default tick callback |
| 350 and 450ms | `12a5:0204` | Original `RTL:58e2` callback installed |
| 500ms | `1f10:08d0` | Prior resident callback restored |

At 350ms the prior `1f10:08d0` callback is also saved at
`2a7a:05bc/05be`, consistent with the neighboring original
`RTL:5840..5895` setup and `RTL:5896..58e1` cleanup routines. Both
complete *original-code* bodies now pass strict entry-anchored relocation
checks on all the 125ms-and-later captures: **70 invariant bytes** across
the 86-byte timer/INT 1Ch setup, and **62 invariant bytes** across the
76-byte restore. All 15 WORD fixups are restricted to original zero-word
instruction operands; the six repeated saved-vector/date/time operands
agree between setup and restore. The setup's actual patched vector target
is independently checked against the complete `RTL:58e2` IRET callback.
At 100ms the setup and callback checks are still incomplete, strengthening
the observation that relocation is not stable until the 125ms sample.

These are **verified original instruction identities**, not fully verified
decompiled C replacements or proven safe native Microsoft-C IRQ services.
The IRQ-visible calibration and callback
identities are verified by both the original bytes and guest RAM;
their presence does not mean the rebuilt launcher should install
an interrupt handler without a separate DOS-side safety test.

The source-C `_main` still has to bind its own allocations and services;
this evidence now supplies a bounded timing/interrupt initialization
step. It does not by itself initialize the graphics, input, or intro
resources, and the three unresolved Microsoft LINK names remain.

**DOS INT 1Ch ABI probe (bundle 052):** `msc51-compile-clock` now also
stages `TIMER.C` and compiles/runs `TMRCHK.EXE`. This deliberately does
**not** yet reproduce the original PIT-divisor `4daeh` calibration. Instead
it installs a minimal MS C 5.1 `interrupt far` handler via `_dos_getvect`
and `_dos_setvect`, observes two BIOS timer callbacks at the existing PIT
rate, and restores the previous INT 1Ch vector on both success and timeout.
It uses the DOS time-of-day service for a two-second deadline, plus a hard
spin bound in case the DOS clock stops. `TMRTEST.LOG` identifies the result;
`CLKOK.LOG` and the clock source fingerprint are withheld if it fails.
Host tests inject IRQs and suppress them to check both restoration paths.
The handler makes no DOS calls and does not chain the prior vector. It is
only used by the standalone probe; **`TIMER.OBJ` is not added to
`LINK.RSP`,** because neither calling convention nor behavior has yet
been verified under the original Microsoft C compiler in DOSBox. Run:

```sh
nix run -L .#msc51-compile-clock
# Return CLKBUILD.LOG, TMRTEST.LOG, CLKFAIL.LOG or CLKOK.LOG.
```

After the DOS probe succeeds, the next step is an opt-in calibration that
programs and then restores PIT channel 0 while retaining the saved INT 1Ch
vector. The unresolved `_main`, control-key handler and text printer
remain unrelated blockers to a linked executable; the probe does not
silently define them.

**PIT calibration follow-up (bundle 053):** The user's actual Microsoft
LINK 3.65 `CLKBUILD.LOG` compiled and linked both `CLOCK.OBJ` and
`TIMER.OBJ` without diagnostics. Its `TMRTEST.LOG` confirms
`INT 1Ch probe: PASS ticks=2, prior vector restored`, so the interrupt
ABI is now verified under that DOSBox configuration. We retain the
original no-PIT test as the first phase of `TMRCHK.EXE` and add a second
phase that explicitly programs PIT channel 0 (`43h` command `34h`, `40h`
LSB `aeh`, MSB `4dh`), waits for eight IRQs, and returns the PIT to the
original snapshot's observed `ffffh` restore divisor. Both successful
and timed-out paths reset the PIT and restore the previous INT 1Ch vector.
The new phase reports a C busy-loop average for diagnostics; it is
**not** the original `RTL:5806` IMUL-cycle calibration constant and is
not linked into `GATEWAY.EXE`. Host I/O fixtures verify both exact six-
write PIT sequences, callback delivery and restoration on timeout. The
next required guest evidence is a new `TMRTEST.LOG`, plus `CLKBUILD.LOG`
if the Microsoft compiler fails. No original executable bytes are staged.

The original setup/cleanup pair also saves/restores DOS date/time around
PIT reprogramming and protects raw vector/PIT writes with CLI/STI. This
first controlled DOSBox test uses Microsoft C's `_dos_setvect` and
ordinary `outp` calls instead; it neither snapshots an arbitrary caller's
PIT mode/divisor nor sets the DOS clock back. It must therefore **not** be
called from the shipped/native launcher or used on real hardware without
further state-preservation work. The `ffffh` reset divisor is an explicit
assumption taken from the supplied original run, not a verified general
restore of a machine's previous PIT configuration.

**DOS clock restoration follow-up (bundle 054):** The user's actual
`TMRTEST(1).LOG` passed the new calibration with **eight INT 1Ch ticks**
and a C measurement of `3654` iterations per tick, with the prior vector
restored and PIT reset to `ffffh`. The measured C iterations are not
comparable to the original `DS:05ba=35f3h` because the loops differ.
The next isolated probe addition now saves DOS INT 21h/AH=2Ah date
`CX:DX` and AH=2Ch time `CX:DX` immediately before calibration, then
issues the original AH=2Bh set-date and AH=2Dh set-time calls **after**
the vector and PIT reset on both successful and timed-out paths.
Unlike the original, this probe rejects a nonzero AL result from either
DOS setter (`status=-4`) while still restoring the vector and PIT first.
The deterministic host fixture checks the exact saved/restored register
words, the call order and a deliberately failed time setter. An actual
Microsoft C / DOSBox run is still required; setting the guest DOS clock
is an intentional observable side effect and must not be enabled in a
native launcher until the full hardware lifecycle is verified.

**Newest timer result and guest-proof guard (bundle 055):** The user's
`TMRTEST(2).LOG` still prints the *earlier* PIT success line ending in
`PIT reset to FFFFh`, with **no** `DOS date/time restored` clause. The
current `TMRCHK.C` prints that latter clause on success; therefore this
log is not evidence that the new date/time setter calls ran. We cannot
distinguish an older bundle from an older staged DOS binary with this log
alone. `validate_clock_adapter` now checks the actual guest output as well
as the `CLKSIG.LOG` source signature, so an old passing executable cannot
accidentally mark the latest source verified. Re-run
`nix run -L .#msc51-compile-clock` with the latest bundle and return the
new `TMRTEST.LOG` (plus `CLKBUILD.LOG` if the output is still old).

**Guest confirmation (2026-11-08, after bundle 055):** The newly
supplied `TMRTEST(4).LOG` includes `DOS date/time restored` after the
eight-tick PIT probe, and `SELTEST.LOG` reports all 45 aliases passing.
These close the two isolated DOS experiments, not `_main` or the RTL
loader/relocator. The timer's `3654` C loop iterations per tick remain
distinct from the original `35f3h` calibrated counter.

**Startup selector mapping (bundle 055):** Independent of the timer phase,
the original 100ms RAM capture shows a fully initialized table of **45**
selectors at runtime `DS:4d82..4e4e`, already stable while the CPU remains
in its earlier loader segment. Of those observed words, 12 equal the
eventual `DS=2a7a` and 33 equal `DS-000ah=2a70`. Both are overlapping
64KiB real-mode address windows over one region, shifted by 160 bytes.
At 0ms these 45 words are zero; at 10ms they are not yet a valid two-window
table. The snapshot inspector now reports group counts and their first
complete millisecond, rejecting any unclassified entry. The 45-selector
set is only the named range; the later `DS:54c4/54cc` FAR pointer fields
and the rest of the original data initialization are **not** covered.

An independent `SEL.C` DOS adapter sets those 33/12 selectors after the
caller reserves at least `100ah` contiguous DOS paragraphs, with the data
DS pointing ten paragraphs above the allocation base. `SELCHK.EXE` is a
bounded real-mode test: it allocates that memory, checks several selectors
from both sets, performs two writes through the overlapping windows, and
frees it. The C89 host fixture tests all 45 writes and both invalid-input
guards. Run:

```sh
nix run -L .#msc51-compile-memory      # if MEMORY.OBJ is not current
nix run -L .#msc51-compile-selectors
# Return SELBUILD.LOG and SELTEST.LOG if the guest test fails.
```

`SEL.OBJ` is deliberately **not in `LINK.RSP`**. It neither fabricates the
CRT `_main` nor loads the original data, relocates the RTL, initializes
video/music/resources, or makes `GATEWAY.EXE` playable. The next startup
work is reproducing the data-image initialization and relocation into
these observed windows before calling `gate_exe_play_intro`.

**Static EXE data copied by 100ms (bundle 056):** Independently of the
two segment aliases, source-image comparison found a contiguous **5,976-byte
exact copy** from original `GATE.EXE:041e6..0593e` to original runtime
`DS:3592..4ce9` (exclusive end `4cea`). Every byte agrees at 100, 125,
175, 250, 350, 450, 500 and 1500ms; 0 and 10ms do not match. Bytes
immediately on either side of the run do not match the same EXE mapping.
The inspector now reports this exact match and its first complete snapshot.
This is an evidence-backed *source-file copy range*, not evidence that
the rest of the data segment, startup BSS or RTLink fixups are complete.
At 100ms the original loader also holds a temporary unrelocated RTL-file
slice beginning at runtime `DS:a0f8`; that region is overwritten before
500ms, so copying it into the final runtime data image would be wrong.

The new isolated `DATA.C` DOS host adapter takes an already-opened original
EXE handle, validates its caller-owned `100ah`-paragraph dual-window
allocation, and seeks/reads exactly that static run through the existing
INT 21h wrapper. It rejects wrong bindings, unsuccessful seeks, short
reads and DOS read errors; a short read can leave a partial destination,
so callers must discard the allocation. `DATACHK.EXE` uses a self-generated
synthetic file of the requisite size, not redistributed proprietary bytes.
The host C89 fixture covers allocation guards, complete copy, adjacent
sentinels, selector preservation and seek/read failure branches. The
Microsoft C/DOSBox test has **not** been run in this workspace; run:

```sh
nix run -L .#msc51-compile-data
# Return DATTEST.LOG, plus DATBUILD.LOG if the probe fails.
```

`DATA.OBJ` remains outside `LINK.RSP`. The original initialized data
outside this range, the BSS, runtime addresses/relocations, resident bank
loading and the full `_main` control flow are not reconstructed.

**Real original-file variant (next DOS guest test):** `compile-data` now
accepts `--original-game-dir ../gateway-dos`, checks the pinned `GATE.EXE`
SHA-256 on the host, and mounts the supplied directory read-only as DOSBox
drive `E:`. The separately compiled `DATAREAL.EXE` checks the DOS-visible
file size, allocates both overlapping windows, binds the 45 selectors,
executes the existing 5,976-byte loader against the original on-disk file,
then compares the destination against a second DOS read. It checks sentinels
at both boundaries, two representative selector words, and closes/frees
the temporary resources. The optional `DATREAL.LOG` must contain its exact
success marker before the host accepts the result; the default synthetic
`DATACHK.EXE` remains separately mandatory. This is an original-file I/O
and memory-placement validation, not a substitute for the missing full
startup BSS/relocations or three linker definitions.

**Next data-layout measurement (source-byte matches only):** The original
five coarse and five fine `.bin` captures can now be compared against *both*
original files, not just the known 5,976-byte `GATE.EXE` range. The optional
inspection mode examines the **eventual** runtime DS segment at every
capture time, even while the loader's instantaneous `DS` is different. It
reports maximal exact runs of at least 64 nontrivial bytes, with file
offsets, and the largest zero-filled regions stable from 500ms onward:

```sh
python3 tools/inspect_msc51_startup.py \
  --exe ../gateway-dos/GATE.EXE --rtl ../gateway-dos/GATE.RTL \
  --snapshot-dir build/msc51-startup \
  --snapshot-dir build/msc51-startup-fine \
  --runtime-data-layout
# Add --json for the full machine-readable run list.
```

The matcher ignores repeated low-entropy source windows and suppresses
ambiguous equal-length origins, rather than misidentifying zero/filler as
static data. Reported long matches can still include coincidental adjacent
equal bytes, so boundaries must be checked across multiple captures before
adding any new loader copy. **Stable zeros do not prove BSS**: they could
be explicit clearing, unallocated storage, or a presently idle workspace.
**User's authentic 0–1500ms report (2026-11-08):** The largest matches
expose memory aliasing, not merely additional static data to load. The
eventual `DS=2a7a` window contains the loader's earlier `DS=348a` at
offset `a100` (`(348a-2a7a)*16`) and its `DS=3445` at offset `9cb0`.
At 100ms `RTL:00011` appears at `DS:a0f1` for 24,335 bytes; at 175ms
`RTL:1001f` appears at `DS:a100` for 24,320 bytes. `EXE:00020` at
`DS:9cb0` is another observed overlapping loader-space mapping. These
large runs are **not justified as permanent runtime data or BSS**.
Furthermore `DS:64e3` has a 4,703-byte stable-zero region from 500ms,
yet `EXE:043ac` was seen at `DS:64e4` for 2,060 bytes at 100ms;
at least part of that zero region previously held nonzero bytes.

The smaller early RTL runs have a common exact alignment: file offset
equals runtime DS offset **plus `0x29`**, including `RTL:00580 -> DS:0557`,
`RTL:01581 -> DS:1558` and `RTL:02c4f -> DS:2c26`. To measure rather
than guess the intervening fields, `--runtime-data-layout` now also
compares `RTL:00029..035ba` to `DS:0000..3591`, stopping just before
the separately verified 5,976-byte EXE data. Each snapshot reports the
exact-byte count and three kinds of mismatch (source zero changed,
source nonzero cleared, other changes), along with bounded mismatch
spans. It labels source matches physically overlapping the loader's
actual transient DS and reports nonzero-to-zero clearing between the
first prepared capture and 500ms. None of this yet proves the full
`0x3592`-byte RTL region is a direct copy or identifies which differing
words are RTLink relocations versus runtime changes. No unverified
replacement bytes or extra data-copy routine have been added.

**Observed RTL data-preimage comparison (user's genuine 063 report):**

| Capture | Exact bytes / 13,714 | Source zero -> nonzero | Source nonzero -> zero | Other modified bytes |
| --- | ---: | ---: | ---: | ---: |
| 100–250ms | 13,454 | 181 | 64 | 15 |
| 350–450ms | 13,340 | 277 | 16 | 81 |
| 500ms | 13,278 | 323 | 16 | 97 |
| 1500ms | 12,984 | 365 | 86 | 279 |

At 100ms **98.1%** of `RTL:00029..035ba` matches runtime
`DS:0000..3591`; the 260 nonmatching bytes are not random noise.
The largest mismatch spans are `DS:0002+62`, `1538+32`, `0fb6+24`,
and `33dc+20`. By 500ms the mismatch total grows to 436, and by
1500ms to 730, demonstrating subsequent writes. The original guest
also clears at least 3,556 previously nonzero data bytes between the
first prepared and first settled capture; these cannot be inferred as BSS.

The new `gate_dos_load_intro_rtl_data` stages a raw **13,714-byte RTL
preimage only**, from a caller-opened `GATE.RTL` handle to `DS:0000`.
It does not invent the 260 early changed bytes, nor overwrite the exact
EXE-derived `DS:3592..4ce9` range. Both the synthetic DOS `DATACHK.EXE`
and its native C89 host fixture now check the two bounded copies; the
host fixture also covers seek, short-read, and failed-read paths. `DATAREAL.EXE`
additionally opens `E:\GATE.RTL` after verifying/closing `GATE.EXE`,
checks the RTL file size, and independently rereads every preimage byte;
the host pins both original SHA-256 digests. This is a standalone DOS
probe—not yet a startup initializer eligible for `LINK.RSP`.
Its actual Microsoft C 5.1 run must be repeated after this change:

```sh
nix run -L .#msc51-compile-data -- --original-game-dir ../gateway-dos
# Both DATTEST.LOG and DATREAL.LOG must pass; return DATBUILD.LOG on failure.
```

**Follow-up (user confirmation):** Both `DATTEST.LOG` and `DATREAL.LOG`
report PASS on the genuine MS C 5.1/DOSBox run. That verifies these two
bounded source-image copy tests, but does not prove the reconstructed
launcher would initialize DS to match the original.

**Next relocation investigation:** `inspect_msc51_startup.py
--runtime-data-fixups` now isolates the first 100–250ms changed bytes
from the raw `RTL:+0029` preimage, counts which of those subsequently
change after 500ms, and separately counts *all* later writes. It examines
both even- and odd-offset 16-bit windows. Two candidate classes are
reported: an observed word equals the original image-load, resident-CS,
runtime-DS or lower-DS segment, or the wrapping difference from its
source word equals one of these bases. The report includes each source
file offset, before/after words, and early/late stability. Every entry is
**unverified**: matches can be coincidental or runtime initialization;
zero-source pointers are not automatically relocation slots. Complete
candidate lists are available as `--runtime-data-fixups --json` and must
be checked against original startup instructions before being translated
to DOS fixups. No new loader writes have been added on speculation.

**Observed 27 segment-word replacements (user's genuine capture):**
The first 100ms sample differs from the RTL +0029h preimage at 260 bytes,
all unchanged in the 125/175/250ms samples. The subsequent 500/1500ms
captures alter 60 of those early bytes and 531 bytes in this leading data
region overall. Exactly 27 zero-source word sites hold the observed
segment values at 100ms: 22 become `DS=2a7a`, five become resident
`CS=12a5`. Most appear in 4-byte-spaced FAR-pointer records. All 27
source words were independently checked as zero in the pinned original
`GATE.RTL` file. The site groups are:

| Destination word | Eventual-DS offsets |
| --- | --- |
| Data segment | `0fb8,0fbc,0fc0,0fc4,0fc8,0fcc`; `153a,153e,1542,1546,154a,154e,1552,1556`; `26d8,2b78,2c10`; `3246,324c,33ae,3540,3544` |
| Resident code segment | `33de,33e2,33e6,33ea,33ee` |

The 27 words cover **54 of 260** early changed bytes. `DS:2c10` was
subsequently modified in the original runtime, so its 100ms initial
segment value must not be assumed permanent. The remaining **206 changed
bytes** require additional original startup analysis and may include
non-segment fixups, data initialization or code updates.

`gate_dos_fix_intro_rtl_segs(lower, paragraphs, resident_cs)` now applies
only these 27 word replacements to a copied RTL preimage. The caller
supplies the actual loaded resident code segment; this routine does not
assume `12a5`. It checks all 27 original zero words before writing any,
returning `-2` with no modifications for an unexpected preimage, and
rejects invalid allocation/segment parameters with `-1`. The regenerated
synthetic `DATACHK.EXE` and original-file `DATAREAL.EXE` must both PASS
under Microsoft C 5.1/DOSBox before their new results can be trusted.
Neither test attempts to execute relocated code, nor does `DATA.OBJ`
enter `LINK.RSP` yet.

**Follow-up (second user confirmation):** The updated generated-data
DOS probe and original EXE/RTL probe both PASS with the 27-word segment
stage. It is now suitable to investigate the other 206 first-prepared
changed bytes without altering the verified segment positions.

`inspect_msc51_startup.py --runtime-data-remainder` performs a stricter
residual analysis: it rechecks all 27 known source-zero words and their
observed values at *each* early capture before excluding exactly 54
changed bytes. It groups the remaining changed positions into nearby
clusters (bridging at most two matching bytes), records their precise
original RTL offsets, all changed-byte categories and early/late
stability, and lists frequent byte transitions and repeated 16-bit
transformations separately for even/odd alignment. Only short source
and observed hex is printed in text; `--json` contains full spans.
The report does not write a replacement image or presume that every
remaining byte is a relocation. A genuine original-capture run is the
next evidence required before adding further DOS initializer code.

**Follow-up: 19 internal offsets from the genuine remainder report.**
The user-provided report isolates two regular FAR data-pointer tables:
six near words at `DS:0fb6..0fca` (stride four) are rebased from the
original RTL by `+0f92h`, and eight at `DS:1538..1554` (stride four)
by `+14e0h`. The matching segment half of each pointer is among the
previously recovered DS fixups. A third regular five-entry FAR pointer
table has near words at `DS:33dc..33ec` (stride four) changed from
zero to `0c02h`; the corresponding segment words at `DS:33de..33ee`
are the previously recovered resident CS fixups. The exact old words
at all 19 positions are independently confirmed in the pinned original
`GATE.RTL` (`source=file:DS+0029h`). All replacements are stable in the
100/125/175/250ms samples.

`gate_dos_fix_intro_rtl_offs` applies only these 19 near word changes,
checking *every original word* before any write and validating the same
DOS segment allocation and DS binding as the segment fixer. Unlike the
pointer groups targeting `252ch` and `25d7h` in the report, these
internal offsets require no additional segment allocation hypothesis.
Both the generated DOS fixture and the real-file DOS probe now test
all 27 segment and 19 offset writes (46 words = **92 bytes**), leaving
**168 first-prepared mismatch bytes** unclassified. The synthetic C89
host fixture covers failed preflight, invalid bounds and preservation of
existing segment words. The extended Microsoft C 5.1 tests still need
to run on the user's machine, and no initialized image is yet eligible
for startup execution.

**Follow-up: two external-segment pointer tables from the 168-byte
remainder.** The genuine 100/125/175/250ms captures show eight four-byte
FAR pointers at `DS:2814..29c9`, stride `003eh`, and eleven at
`DS:2a93..2b5e`, stride `0014h`. All **76 source bytes** are zero in the
pinned original `GATE.RTL` preimage. Their observed offsets repeat
consistently (`000a,0032,0032,0053,0065` in the first group;
`006b,00d6,0133,0189,01e3,0272,0440` in the second). The corresponding
segments **in this one original capture** are `252ch` and `25d7h`,
respectively. There is not yet evidence tying those addresses to specific
allocation requests or ensuring they remain fixed in another run.

`gate_dos_fix_intro_rtl_external(lower, paras, first_seg, second_seg)`
therefore takes **both actual target segments as caller parameters** and
never embeds the observed addresses. It checks all 38 original source
words are zero before applying any replacements, rejects zero target
segments or incorrect DS allocation bounds, and does not allocate or
initialize either target window. The synthetic MS C 5.1 and real-file
DOSBox probes now exercise this helper with arbitrary test segment values;
the latter also verifies the source against the original game files.
The host C89 and Python unit checks pass, but the new guest DOSBox results
remain pending on the user's machine.

These 19 pointers explain another **65 changed bytes** (some target
offsets contain zero bytes), reducing the unclassified early mismatch
count from 168 to **103**. The Python remainder inspector validates every
pointer and a *single common segment per table* in all early snapshots,
reports each observed segment explicitly, and excludes only bytes it has
verified. The other 103 bytes include dynamic zeroing near `DS:0002`,
small unknown value initializations, and candidate internal offset words
near `DS:3244`, `DS:33ac`, and `DS:353e`. Do not label these BSS or stage
them without separate analysis. `DATA.OBJ` remains probe-only; it is not
a working launcher or a proof of how those two segments are allocated.

**Follow-up: five further internal FAR offsets.** In the user's same
original startup remainder, `DS:3244`, `324a`, and `33ac` become `2c44`,
`2c44`, and `3328` respectively, each rebased from its original RTL word
by `+2c44h` (`0000`, `0000`, `06e4`). Similarly `DS:353e` and `3542`
become `3530` and `3534`, both rebased by `+3530h` from `0000` and
`0004`. All five corresponding FAR segment halves were among the 22
known DS segment replacements, and all five values were unchanged across
100..250ms and through the late samples. Their original source words were
checked directly in the pinned RTL. They extend the same all-or-nothing
`gate_dos_fix_intro_rtl_offs` preflight (now **24 offset words**), and the
synthetic/real DOS probes and the Python remainder inspector test them.
These five fields explain **10 more changed bytes**, bringing coverage
to **167 of 260** early changes and leaving **93**. Microsoft C 5.1/DOSBox
verification of this updated helper remains pending on the user's host.

**2026-11-08 follow-up:** The actual 100/125/175/250ms snapshots also
establish that `DS:0002..0041` is cleared (64 bytes, of which 63 differ
from the original RTL preimage); the two-byte header and `DS:0042` remain
untouched. Added `gate_dos_clear_intro_rtl_prefix` to the DOS staging and
synthetic, original-file and host probes. The remainder inspector now
classifies this range only when *every* early capture confirms the clearing.
The pinned captures then account for **230 of 260** changed bytes, leaving
**30**, all originally zero and subsequently initialized to nonzero values.
This is an observed bounded initialization operation, not proof of the
original loader's algorithm or a complete startup; Microsoft C 5.1/DOSBox
execution of the revised probe remains pending.

**Next six startup words:** The same four early captures also agree on six
zero-source 16-bit offsets at `DS:0525`, `26d6`, `2b74`, `2bc6`, `2c0e` and
`2c24`. They remain unchanged through 250ms; `2c0e` is subsequently
modified. A separate bounded DOS initializer now checks *all* six source
zeros before storing their observed startup offsets. This does not assert
the loader's relocation formula or that every field is a pointer. Synthetic
and original-file DOS probes, the host fixture and the snapshot inspector
cover these sites. With their 12 bytes classified, **242 of 260** early
changes are accounted for; **18** remain in only three clusters
(`DS:049d`, `0518`, `0547`). Real DOS compiler/runtime verification is pending.

**Relocated one-instruction callback at `DS:049d`:** The user's authentic
100–250ms RAM proves this FAR pointer targets physical `17fffh` in the
observed load, a `RETF` byte corresponding to original `GATE.RTL:4bc9`.
The byte immediately before it is `IRET`, and an independent five-byte
unchanged instruction run at original `RTL:4be6` matches the live code
at the corresponding offset after relocation. A new DOS helper takes a
caller-supplied FAR target, checks the `IRET/RETF/MOV` neighborhood and
the two originally zero pointer words, then writes its offset and segment.
It deliberately does not embed `17ec:013f`: the rebuilt executable must
derive the bank's address from its own loader. The snapshot inspector only
classifies these four bytes when *all* early RAM captures match both code
anchors and the original source pointer is zero. This leaves **14 of 260**
initial changes: `DS:0518` (one byte) and `0547` (13 changed bytes within
a 16-byte table). No independent DOS `_main` is implemented yet.

**Checked data-bootstrap entry for the future `_main`:** The source-C DOS
adapter now provides `gate_dos_prepare_intro_data`, which binds the 45
observed data aliases, copies the original EXE and RTL initialized-data
regions from caller-owned DOS file handles, then runs the separately tested
clear, segment, offset, startup-word, external-pointer and relocated-return
fixups. Its resident and external segment locations are **arguments** from
the future loader, never hardcoded original-run addresses. A strict GCC C89
host integration test feeds generated synthetic file images with the same
source-zero contracts, exercises the entire sequence and checks output data
and boundaries. On failure the allocation must be discarded; the routine
does not attempt rollback. Neither the missing 14 bytes nor actual code-bank
loading, IRQ/music setup or entry transfer are supplied. This is a functional
startup-data component, **not a playable DOS executable**.

**First resident code-bank copy:** The 15,616-byte original `GATE.RTL`
region `56de..93dd` is independently established as the source image for
resident bank relative `02a5`. Added a bounded DOS loader that seeks to
`56deh`, reads exactly `3d00h` bytes into a caller-owned allocation of at
least `03d0h` paragraphs, and rejects malformed destinations, failed seek
and short read. Synthetic C89 host tests compare all bytes and the
`DATACHK`/`DATAREAL` DOS probes now cover this raw copy (the real probe
re-reads the original file independently). This **does not execute** the
bank: its original runtime bytes differ at relocation operands, and a
reconstructed linker/relocator and actual `_main` are still required.

**First verified resident relocation class:** In the original 125, 175,
250 and 500ms captures, 98 five-byte `CALL FAR` instructions in the `02a5`
bank have source operands `0000:0000` and linked targets in the same
resident segment. The four samples agree on every target offset. Added a
bounded DOS relocator that checks *all* 98 original `9A 00 00 00 00`
instructions before writing the captured target offsets and the **new
caller-supplied resident segment**. A C89 host test verifies atomic failure
on a corrupt last call and all 98 patched operands; original-source/live
tests independently check every site. `DATAREAL` also probes representative
targets after testing the raw bank copy. This is only one relocation class:
the bank has 1,660 changed bytes relative to the raw file, of which this
cluster accounts for 392 operand bytes. Some targets are beyond the copied
`3d00h` region, and other relocations remain unexplained. The bank must
still **not execute** until the rest of the linker is reconstructed.

**DS-load immediates and EXE call boundary:** Seven source-zero immediate
WORDs in the same bank are each enclosed by the exact instruction sequence
`MOV AX,imm16; MOV DS,AX` and resolve to original DS `2a7a` in all four
early captures. A separate C89 relocator preflights these seven complete
five-byte sequences and inserts the new DOS data segment. The last
zero-operand `CALL FAR` in this bank targets `1000:0030` in the original
EXE load; it is **not** a resident self-call. Its own guarded relocator
takes a caller-supplied equivalent EXE function pointer (the original
address cannot be reused in the recompiled executable). Host and original
RAM tests validate all eight sites. They explain 16 additional differing
bytes (14 DS immediate bytes and two nonzero bytes in the EXE call), for
**408 of 1,660** bank differences now reconstructed. These functions do
not yet resolve other non-FAR-call relocations or establish the equivalent
recompiled EXE callee.

**Seven further direct segment operands:** Distinct opcode patterns identify
three `CMP WORD PTR [BP+0a],imm16` instructions, two `MOV DX,imm16`, one
`MOV DI,imm16`, and one `MOV ES:[0072],imm16` storing resident CS. All seven
original immediates are zero, and all four 125–500ms snapshots agree that
six become DS `2a7a` and the last becomes resident `12a5`. Added a
guarded all-or-nothing C89 relocation routine accepting both *new* segment
values, with original-binary/snapshot assertions and host plus `DATAREAL`
probe checks. This accounts for **14 additional** changed bytes, bringing
the verified first-bank subset to **422/1,660**. Other words coincidentally
equaling these values are deliberately not classified without instruction
evidence. The bank still cannot execute safely.

**Follow-up: determine external allocation ownership from snapshots.**
After the 167 verified initialization byte changes are excluded, the user's
remainder report contains only ten clusters: a 63-byte zeroed prefix at
`DS:0002` (56 later-mutated bytes), a 16-byte initialization table at
`DS:0547` (13 actual changed bytes), a four-byte FAR-looking value at
`DS:049d`, one changed byte at `DS:0518`, and six two-byte sites at
`DS:0525`, `26d6`, `2b74`, `2bc6`, `2c0e`, and `2c24`. Only the `2c0e`
word is later mutated in the two-byte group. It would be premature to
call the cleared prefix BSS or to hardcode `DS:049d` as a FAR pointer
without identifying its runtime segment.

`inspect_msc51_startup.py --runtime-mcb-owners` now walks *actual*
DOS MCB headers at each original RAM snapshot, beginning at the MCB
immediately before the original observed PSP (`0ff0`, hence MCB `0fef`)
unless `--mcb-start 0xSEG` explicitly overrides it. It validates the
`M`/`Z` marker, owner PSP, paragraph count and sequential chain bounds;
malformed chains stop rather than attempting a byte-pattern scan. It
compares all pointer offsets/segment halves at each settled sample and
reports the MCB enclosing their full FAR target ranges, whether the
segment equals the allocated block's data start, and the block owning
the eventual runtime DS. A valid MCB identifies the memory block, *not*
the allocation call which created it. This analysis does not write the
guest or embed original bytes. Synthetic positive, free-block and corrupt
chain cases pass locally. To obtain the missing original-DOS evidence:

```sh
python3 tools/inspect_msc51_startup.py \
  --exe ../gateway-dos/GATE.EXE --rtl ../gateway-dos/GATE.RTL \
  --snapshot-dir build/msc51-startup \
  --snapshot-dir build/msc51-startup-fine \
  --runtime-mcb-owners
```

**Original DOS MCB result (subsequently supplied):** At 100, 125, 175
and 250ms, the complete MCB chain has **one** block (terminal `Z`):
PSP `0ff0`, owner `0ff0`, size `9010h` paragraphs. At 350 and 450ms
the same owner block is `2a8ah` paragraphs long, with one further MCB;
at 500ms there are three MCBs and at 1500ms four. In every settled
sample, both pointer targets and the eventual DS belong to the **same
original process block**. The addresses are interior segments, not starts
of independently allocated blocks:

| Use | Observed segment | Paragraphs after PSP `0ff0` |
| --- | --- | --- |
| First FAR-pointer table (`external_a`) | `252c` | `153c` |
| Second FAR-pointer table (`external_b`) | `25d7` | `15e7` |
| Eventual runtime DS | `2a7a` | `1a8a` |

After the shrink, the PSP block ends at paragraph `3a7a`, exactly
`1000h` paragraphs (64KiB) after the eventual runtime DS base. The
later MCBs do not own these pointers. This **corrects** the earlier
unresolved separate-allocation hypothesis: the segment values describe
positions inside Gateway's original process image. The C89 helper still
takes target segments as caller parameters, because an independently
linked replacement executable must resolve its *own* layout; it should
not use `252c` or `25d7` as constants or allocate two new DOS blocks.

**Next bounded provenance probe:** `--runtime-pointer-sources` separately
reads each observed FAR-pointer target segment at each settled original
RAM capture. It first verifies all original table offsets and a common
nonzero target segment per table, then compares at least `600h` bytes
around the target offsets with nontrivial, unambiguous 64-byte-or-longer
copies from the original EXE/RTL. It also reports nonzero bytes and
changes since that segment's first observed sample. Thus the next user
run can distinguish unchanged original-file copies from modified or
synthetically produced memory, without guessing new DOS allocations.
The absence of a long exact run does not establish that bytes are BSS,
or that the referenced routines were not in the original executable.

```sh
python3 tools/inspect_msc51_startup.py \
  --exe ../gateway-dos/GATE.EXE --rtl ../gateway-dos/GATE.RTL \
  --snapshot-dir build/msc51-startup \
  --snapshot-dir build/msc51-startup-fine \
  --runtime-pointer-sources
```

**Original pointer-source result (subsequently supplied):** Both windows
are completely zero at 100, 125, 175 and 250ms. At 350ms `external_a`
has 1,449 nonzero bytes in a `0600h`-byte window, and `external_b` has
1,515 in `0640h`. They are *byte-identical* at 450, 500 and 1500ms.
Five nontrivial exact RTL source runs are present in each window, with
file offsets in `18e00..193d9` (A) and `19a20..19e40` (B). These are
**loaded code regions** rather than the two imagined DOS allocations:
existing original-memory VFX tests already independently identify
`252c:0032`/`0053` as graphics coordinate helpers and
`25d7:00d6`/`0133` as display pixel-operation handlers.

The first A run (`rtl:18e00 -> 252c:000a`, 218 exact bytes) covers
*all eight* table target offsets, though only four are distinct. Its
local file-offset mapping is `RTL file = 18df6h + target offset`.
All five B source runs agree on `RTL file = 19993h + target offset`;
the earliest `rtl:19a20 -> 25d7:008d` and the later runs remain
consistent despite relocated instructions between them. Combining
those independent copies yields this **candidate entry crosswalk**
(file locations are original-run evidence, not rebased link addresses):

| Target code offset A | Original RTL file | Target code offset B | Original RTL file |
| --- | --- | --- | --- |
| `000a` | `18e00` | `006b` | `199fe` |
| `0032` | `18e28` | `00d6` | `19a69` |
| `0053` | `18e49` | `0133` | `19ac6` |
| `0065` | `18e5b` | `0189` | `19b1c` |
| | | `01e3` | `19b76` |
| | | `0272` | `19c05` |
| | | `0440` | `19dd3` |

`--runtime-pointer-sources` now selects locally supported source-offset
maps and checks the *live bytes at each unique FAR entry* against that
source, reporting exactly how many initial bytes match. This is necessary
because a mapping supported by long runs is not, by itself, proof of
unmodified code at every entry: relocations can land at the first
instruction. The next genuine report will establish those per-entry
prefixes. Do not apply the A base delta to its later code chunks:
some A source runs show different offsets after loader fixups/packing.

**Original entry-prefix verification (subsequently supplied):** At 350ms
all four distinct entries in `252c` match **16/16** leading source bytes
and all seven distinct entries in `25d7` match **11/16**. The same
results persist through the later captures. The eleven candidate RTL
entry offsets in the table above are therefore now independently
supported by *live instruction prefixes*, not just nearby copied runs.
The seven B functions share the Microsoft C-style prologue containing
`MOV AX,imm16; MOV DS,AX`: the first likely patched byte lies at
function-relative `+0bh`, immediately after the `MOV AX` opcode.
The exact runtime immediate values have **not** yet been reported.

The read-only `--runtime-pointer-sources` report now classifies that
specific immediate-word change if the surrounding instructions still
match the original source. It reports original and runtime words per
function, but does not assume they all equal the eventual DS. When a
single source delta is supported by at least 80% of the unambiguous
source-copy run lengths, it also compares the *full* target window
against that linear source candidate, reporting exact byte coverage and
zero/nonzero transitions. This can establish whether the `25d7` bank
can be represented by one loaded image plus bounded relocations; the
multi-delta `252c` window must not be flattened into one copy.
The consecutive-snapshot byte-difference count also tests whether the
regions *actually stay unchanged* after loading, rather than merely
containing the same number of nonzero bytes at different times.

**Original relocation values and 22-second cross-check (subsequently
supplied/verified):** All seven `25d7` function entries replace an original
`MOV AX,0000h` immediate with `2a7ah` before `MOV DS,AX`; this is the
eventual runtime data segment. The 100–250ms windows remain zero, the
350ms snapshot changes 1449 bytes at `252c` and 1515 at `25d7`, and
**all subsequent 450/500/1500ms windows change zero bytes relative to
their preceding samples**. The B region has only **1111/1600 exact bytes**
under a single linear file mapping (`RTL:19993`); 133 byte differences
were zero-to-nonzero and 13 nonzero-to-zero, with additional nonzero
changes. Seven two-byte DS operands explain only 14 of the 489 unequal
bytes, so a plain file copy plus seven relocations is not yet sufficient.

The existing private `build/intro-analysis/at-22000.bin` was available
locally and independently confirmed the same two bank images at 22 seconds:
`252c` preserves **219/228** exact source bytes in its *bounded* first
`00e4h` bytes and all four entry witnesses; `25d7` preserves **1111/1600**
with all five separate source witnesses. Both have therefore been added
to the standard **snapshot-validated** intro Ghidra import. This gives
Ghidra relocated code bytes for further inspection without claiming
the original binary can yet be rebuilt from only those file offsets.

**Later packed-code correction (22-second snapshot):** The raw file's
single-offset comparison was misleading beyond the first function group.
Distinct `GATE.RTL` metadata gaps separate six fragments occupying
`252c:0000..0aaf` and three occupying `25d7:0000..0d7f` in RAM. Every
fragment has 90–96% individually identical bytes against its own source
file range, and distinctive original Microsoft C function prologues
anchor the transitions. The mapping explains the apparent 489-byte
one-delta discrepancy in the first B window, without pretending the
remaining relocation operands are understood. The complete source-span
table and 22 new Ghidra entry candidates are in [intro-ghidra.md](intro-ghidra.md).
The original 350–1500ms trace sampled only the first `0600h`/`0640h`
bytes; the expanded later regions are presently proven at 22 seconds,
not claimed to have been resident at 350ms.

**Later source-origin and word-site audit:** Tightened the nine graphics
fragment ranges using the genuine 22-second image. The first ten runtime
bytes of `252c` and the first four of `25d7` are no longer assigned RTL
source offsets. The two B fragment-boundary bytes still lack file origins,
and B's last seven formerly imported bytes are zero in RAM and outside the
revised `25d7:0000..0d78` import. Across the now source-verified ranges,
`252c` is 2600/2726 exact and `25d7` is 3198/3443 exact; the other
**371 changed bytes** fit **198** originally-zero 16-bit word sites, with
no unexplained changed bytes *within covered source spans*. Instruction
context proves 30 DS and one ES immediate loads and 33 FAR transfers
(30 calls and three jumps, 66 offset/segment word operands). The remaining
101 sites cannot yet be labeled linker fixups just because their original
words were zero. The snapshot-dependent evidence is exported as
`zero_word_changes` by `prepare_intro_ghidra.py --graphics-word-changes`
and regression-tested against the pinned original binaries. Neither the
graphics loading algorithm nor the caller-side allocation and fixup sequence
is implemented by this analysis.

**Further opcode and table confirmation:** Every one of the 198 word sites
now has an explicit local 8086 operand pattern checked against both file
and linked memory. In particular, 23 more FAR calls have a changed segment
word but leave their originally zero offset unchanged; the earlier 30-call
count covered only changed offset/segment pairs. The remaining changes are
register immediates, stack-local immediate stores, six direct-memory
operands and one comparison immediate. The 22 words in eleven repeated
`C7 47 0C`/`C7 47 0E` pairs initialize FAR pointers with a 20-byte stride
from `BX=2a8fh`. All eleven resulting pointers match the original guest
RAM at `DS:2a9b + 14h*index`, all pointing to segment `2621h`. The
`far_pointer_tables` plan evidence includes a separate memory-match field
for every entry; a deliberately corrupted synthetic capture disables the
verified-table classification. This narrows the next reverse-engineering
task to identifying the actual fixup records and DOS loader control flow,
not guessing what these individual 16-bit operands mean.

**Preceding FAR-code bank and physical aliases (22-second RAM):** FAR calls
from the `252c` and `25d7` banks also reach resident `244c`. Six independent
source fragments map `244c:0000..0dff` to original RTL file ranges
`17e6d..18d90` with metadata gaps; 3383 of 3582 source-mapped bytes match
exactly, the other 199 changed bytes lie at 109 originally-zero word sites,
and only post-return runtime bytes `+0115`/`+0b03` lack a source identity.
The actual 16-bit instruction operand contexts are recorded for all 109
sites. Ghidra imports this as the ninth verified RTL bank and seeds six
additional entries. FAR calls to `25b7` resolve physically inside the
`252c` image, while `2621` resolves inside `25d7` at relative `+04a0`;
thus segment equality alone is insufficient for the decompiler. Strict
paragraph-alias and per-fragment file identity resolution now maps 67 of
78 identified graphics FAR transfers. The 11 remaining transfers all
target resident `2409`, not yet mapped. These numbers describe candidate
code bytes in one captured layout, not the original loader's control flow.

**Closing the `2409` graphics FAR-target gap (22-second RAM):** The verified
resident code window `2409:0000..042f` immediately precedes `244c`. Three
separate source fragments map runtime offsets `+0008..0142`, `+0144..0219`
and `+021a..042f` to original RTL offsets `179d9..17b13`, `17b4f..17c24`
and `17c30..17e45`, respectively. Of 1063 source-mapped bytes, 1003 match
the pinned original unchanged. All other 60 differ only at 31 originally
zero words; two of those words have no yet-proven local opcode role. Eight
leading bytes and one post-RETF boundary byte remain unmapped, and cannot
be assigned a file identity by assumption. Three independently matching
Microsoft C function entry prefixes seed Ghidra. With relative bank `1409`
imported, all **86 of 86** identified graphics FAR CALL/JMP targets map
physically to verified original RTL spans, including the earlier 11 missing
`2409` targets and eight additional calls from this bank. This establishes
code provenance in one snapshot, **not** DOS loader/fixup semantics.

**Follow-up opcode identification:** The two remaining `2409` source-zero
word candidates are now identified as `C7 06 disp16 imm16` direct DS-memory
address operands, both changing to `15d0h` in the live image. The exact
`C7 06` prefix matches in the source and linked code; synthetic tests reject
the classification when the source/live opcode bytes diverge. All 31
source-zero sites in this bank now have a conservative local 8086 opcode
role, but their original fixup records and execution order are still unknown.

**Microsoft C 5.1 build correction:** The first user-side `DATA.BAT` run
compiled `DATA.C` but stopped with `DATACHK.C(22): C2073` (an initialized
automatic `char name[]`), so **neither test executable ran**. Both test
filenames now use file-scope static arrays compatible with this compiler;
the original probe's filename needed the same correction. The over-31-character
minimum-paragraph macro in `DATA.C` was shortened to eliminate C4011
truncation warnings. Host C89 compilation is not a replacement for the
Microsoft C run. Rebuild and inspect both result logs:

```sh
nix run -L .#msc51-compile-data -- --original-game-dir ../gateway-dos
# DATBUILD.LOG, DATTEST.LOG and DATREAL.LOG under build/msc51-launcher/
```

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
adapter in `decomp/launcher/dos/msc51_heap.c`. The latest user's 22-second
snapshot exposes its **48-byte live entry**, identifying the original
`RTL:18f23..18f53` by an exact entry-anchored comparison after masking the
sole relocated `mov ax,imm16` DS-segment operand. The 46 unrelocated bytes
in the supplied prefix agree with the source. The latest user rerun now
**confirms the entire 49-byte body, all 47 invariant bytes**, including
the final `RETF 4` immediate; the inspector reports the original source
only on that successful check. The segment's neighboring allocator at
`RTL:18edf..18f22` is 68 bytes earlier, exactly matching the original
linked offsets `253a:0004` and `253a:0048`; this is strong *static* bank
evidence, **not** an independent live allocation-entry comparison.

The recovered release body loads the caller's FAR segment into `ES`,
passes it to DOS `INT 21h/AH=49h`, and ignores the FAR offset in `DI`.
It returns `0000h` on clear carry and **`ffe7h` (-25)** on set carry,
irrespective of the DOS error code in `AX`. The earlier DOS adapter
incorrectly returned `ffe4h` (-28, the *image signature* error) and
rejected a nonzero FAR offset. It now preserves the original offset-ignored
semantics and reports `ffe7h` on a DOS failure; a zero-segment guard remains
as a host safety condition. The DOS heap probe now tests freeing a block
with a nonzero offset. Its revised *authentic Microsoft C 5.1* build/run is
pending a user-side `msc51-compile-heap` rerun.

This does **not** establish that the complete Ctrl-Break cleanup is
implemented: other shutdown calls and original startup state bindings are
missing.

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
bytes as `live_entry_hex` for still-unmatched callees, without treating a
file-wide byte-window match as proof of identity.
Unknown nested targets are similarly reported with their live bytes. This
does not read past a 1-MiB RAM capture, and does not claim that byte strings
are disassembled or executable entry points.

Static disassembly gives leads, **not completed services**: `rtl:c391`
tests a music/device flag and calls a same-bank helper; `rtl:e771` iterates
two records; `rtl:11e5f` reads a device descriptor and dispatches at least
two numbered requests; `rtl:621e` performs multiple near calls; `rtl:f827`
branches on a resident mode selector; `rtl:8aca` initializes a FAR buffer
descriptor and passes it to another formatter. Actual DOS side effects,
the downstream behavior of the now source-matched `221b:021f` branch, and
the remaining diagnostic input-polling behavior have yet to be established. None of these
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
