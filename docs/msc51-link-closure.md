<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception -->

# Microsoft C 5.1 launcher link closure

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
The next step is to review this report before implementing more runtime
initialization, especially any values outside `DS:3592..4cea`.

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
