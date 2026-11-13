<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
-->

# Instruction-level x86→C lifter

Added 2026-11-13. `tools/lift_x86.py` translates decoded functions of the
NASM base ([disassembly.md](disassembly.md)) instruction by instruction into
C. The output runs inside the Unicorn guest in place of the original code,
with all state kept in guest registers and guest memory. It is mechanical
and not readable game logic. It is the first step towards reconstructed
handlers, and it lets them be checked against the original. Generated C
contains translated program code, so it stays under `build/` and is never
committed.

## Reproduce

`tools/regen_build_sources.sh ../gateway-dos ovl-038` regenerates the
manifest, the disassembly and `build/lift/ovl-038.{c,so}`. Step by step:

```sh
python3 tools/lift_x86.py ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
    --rtl ../gateway-dos/GATE.RTL \
    --manifest build/handler-coverage/rtlink-manifest.json \
    --unit ovl-038 --output build/lift/ovl-038.c
cc -std=c99 -O2 -Wall -Wextra -Wno-unused-label -fPIC -shared -Isrc \
    -o build/lift/ovl-038.so build/lift/ovl-038.c
python3 tools/trace_game_logic.py --native-only --scenario drawer \
    --lift build/lift/ovl-038.so --lift-mode verify
python3 tools/trace_game_logic.py --native-only --scenario drawer \
    --lift build/lift/ovl-038.so --lift-mode run
```

`gateway-probe` takes the same `--lift MODULE.so [--lift-mode verify|run]`.

## Design

- ABI: `src/lift_abi.h` (version 1). A function gets a `gw_cpu` (registers,
  flags, byte read/write callbacks on linear addresses) and returns
  `GW_LIFT_EXIT` (CS:IP is the next guest instruction), `GW_LIFT_EMULATE`
  (CS:IP is an instruction the emulator must run) or `GW_LIFT_BAD_ENTRY`.
- Each function has an entry and resume points: after every call, after
  every instruction left to the emulator, and after `div`/`idiv`. The C code
  is a `switch (c->ip)` over them. Every call, return, far jump or jump out
  of the body stores CS:IP and returns, so the engine, other sections and
  unlifted code keep running in the emulator.
- Left to the emulator: `int`, port I/O, `cli`/`sti`, `hlt`, x87 and
  floating-point emulator calls, BCD, `pusha`/`popa`, `enter`/`leave`,
  `lahf`/`sahf`, `cmc`, `bound`, `iret` and other rare forms. Division
  faults also exit to the emulator, which raises them.
- RTLink fixups: relocated immediates and far pointers are read from guest
  memory at run time, not baked in. A function carries an FNV-1a 64
  fingerprint of its body with fixup bytes zeroed; the runtime calls it only
  when the loaded bytes match.
- Switch tables become C `switch` statements with an exit fallback.
- Flags follow QEMU, which is what Unicorn runs: shift and rotate OF for
  every count, `rcl`/`rcr` with no flag change when the count is a multiple
  of width+1. AF is computed but not compared anywhere.
- Word accesses at offset 0xffff use linear address + 1 without segment
  wrap, as Unicorn does. MSC code does not depend on either.

`src/lift_runtime.cpp` (`gateway::LiftRuntime`) loads a module, hooks the
resume addresses (CS = linked segment + load segment 0x1000), checks CS, IP
and fingerprint, and stops emulation. In the main loop it either:

- **verify**: snapshots 640 KB and the registers, runs the C function on a
  shadow copy, then runs the original until the C exit address and compares
  registers (arithmetic flags without AF, DF) and all memory. A mismatch
  aborts. `uc_ctl_remove_cache` is needed around the `until` address,
  because Unicorn checks it only when translating a block.
- **run**: runs the C function directly on guest memory and continues the
  emulator at its exit. Writes at or above 0xa0000 abort: video memory goes
  through the emulator's memory hooks, which this path bypasses.

## Results

All 47 section-38 functions lift and compile without warnings.

| Run | Result |
| --- | --- |
| `drawer`, verify | 85 lifted steps verified in 3 functions (`ovl:0x0514b5` 83, `ovl:0x051a78`, `ovl:0x051b81`), 0 mismatches, framebuffer identical |
| `drawer`, run | 85 steps, 530 bytes written, framebuffer hash `120ac0dd…2943` = native |
| `look`, run | 44 steps, 312 bytes written, framebuffer hash `1ca3a90f…b69b` = native |

The final memory dumps of run mode differ from native in about 150 bytes.
A verify run, which executes only original code, already differs in 155
bytes from native, and run against verify differs in 24. The differences
come from emulation slicing and virtual time: a lifted step costs no guest
instructions, so timer-driven state advances differently. No logic
difference was found.

## Semantics test

CTest `lift-semantics` (`tests/lift_semantics_tests.py`, `tests/lift_check.cpp`)
assembles about 73 NASM snippets (ALU, inc/dec/neg/not, all shifts and
rotates, the 16 conditional jumps, mul/imul/div/idiv, sign extension,
string instructions with prefixes, segment overrides, 8-bit registers,
loops, a switch table, a near call, a stack frame), lifts them and compares
each with Unicorn on 300 random register and memory states. Data and stack
use separate segments. A trial where a random pointer overwrote the sentinel
return address is skipped. With `--keep DIR` the build is kept, and
`build/lift-check DIR/lifted.so DIR/code.bin DIR/entries.txt 3000` runs a
longer check (about 5 minutes, 0 failing, 2 trials skipped).

## Whole-program lift (trial, 2026-11-13)

Every unit was lifted and compiled once to see how far the lifter reaches
beyond section 38. The output went to a scratch directory, not `build/lift`;
`regen_build_sources.sh` still lifts only `ovl-038` by default. Reproduce
with:

```sh
D=../gateway-dos O=build/lift
for u in exe exe-startup $(seq -f 'ovl-%03g' 0 57); do
  python3 tools/lift_x86.py $D/GATEMAIN.EXE $D/GATEMAIN.OVL --rtl $D/GATE.RTL \
      --manifest build/handler-coverage/rtlink-manifest.json \
      --unit $u --output $O/$u.c &&
  cc -std=c99 -O1 -Wall -Wextra -Wno-unused-label -fPIC -shared -Isrc \
      -o $O/$u.so $O/$u.c
done
```

(or `tools/regen_build_sources.sh ../gateway-dos exe exe-startup ovl-000 …`,
which uses `-O2` and is slower on the 124k-line `exe.c`).

### Results

- **All 60 units lift and compile with no warnings**: `exe`, `exe-startup`,
  `ovl-000`–`ovl-057`, about 488k lines of C in total (`exe.c` 124k).
- **2,153 of 2,180 functions lift (98.8 %).** Every overlay section lifts
  completely. The 27 refused functions are all resident:

  | Reason | Functions |
  | --- | --- |
  | fixup in a memory operand (15) | `exe:0x00bd9c` `0x00bf1c` `0x00d54f` `0x00d559` `0x00d563` `0x00d5a9` `0x00d5bd` `0x00d5d1` `0x00d5ef` `0x00d635` `0x00d63f` `0x00dc2e` `0x01006f` `0x015c42` `0x016ab0` |
  | fixup outside an immediate word (8) | `exe:0x00c0f4` `0x00d649` `0x01003d` `0x010051` `0x01005b` `0x01614a` `0x01647b` `0x01d897` |
  | body spans several CS bases (3) | `rtl:0x004378` `exe:0x0084c3` `exe:0x0097ea` |
  | unknown CS base (1) | `exe:0x00059e` |

- The generated code leaves 543 sites to the emulator (`GW_LIFT_EMULATE`).
- `drawer` replay, `ovl-016` and `ovl-021` in verify mode: the modules load,
  but none of their functions is entered (0 steps). Drawer exercises only a
  few overlays, so these are **untested, not failing**.
- `drawer` replay, `exe` in **verify** mode: **timed out** at the default
  180 s. Verify mode snapshots 640 KB per lifted step, and resident engine
  code is entered constantly.
- `drawer` replay, `exe` in **run** mode: **also timed out** at 180 s; the
  log ended in normal file reads and mouse polls, with no abort message.
  **Hypothesis, not confirmed:** a lifted step costs no guest instructions,
  so virtual time barely advances, and a loop that polls the timer spins
  forever. The per-step `uc_emu_stop`/restart overhead may add to it. Run
  mode would also abort on the first video write at or above `0xa0000`.

## Plan to lift the whole program

Rough estimate: 2–5 working days until everything is lifted and runs.
Verification has no fixed end: it covers only what replays reach.

1. **Make lifted `exe` usable** (1–3 days, do first; the overlays call
   resident code constantly, so they gain little until it runs lifted):
   - Find the actual cause of the run-mode timeout: trace the `lift-run`
     lines, look for a hot loop, and check the virtual-time hypothesis.
   - Charge each lifted step an instruction count (for example the number
     of original instructions it executed) so timer state advances as in
     native.
   - Call lifted functions directly from lifted code instead of exiting
     to the emulator on every call and return.
   - Route lifted writes at or above `0xa0000` through the emulator's
     memory hooks instead of aborting.
   - Make verify mode cheaper, for example by comparing pages dirtied
     during the step instead of all 640 KB, or by sampling one step in N.
2. **Load several modules at once** (about ½ day): `--lift` takes one
   `.so` now (`src/probe.cpp`, `src/lift_runtime.cpp`). Either accept the
   option more than once or link all units into one module.
3. **Lift the 27 refused functions** (½–1 day): handle fixups in memory
   operands and in non-immediate words with the same run-time read as
   immediates, and split bodies that span several CS bases.
4. **Coverage** (open-ended): run every module in verify mode on the
   `look`, `drawer`, `objects` and `puzzle` replays and on the walkthrough
   and golden replays (`tools/run_walkthrough.py`, `tools/run_golden.py`).
   Record per unit which functions were verified. Add replays until each
   section is entered.
5. Only after that: readable C. Naming the call-graph services and
   cleaning up into `src/story/` is a separate, larger job.

## Open items

- `regen_build_sources.sh` lifts only section 38 by default, and no lifted
  module other than section 38 has passed verify mode on any replay. The
  whole-program trial above shows that all units compile.
- 16 functions `ovl:0x053019`–`ovl:0x053373` are reached only through data
  (probably region or picture-condition tables); they lift but no replay
  has exercised them.
- No readable C yet: next comes naming of the call-graph services
  ([call-graph.md](call-graph.md)) and cleanup into `src/story/`.
