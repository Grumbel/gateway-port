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

## Open items

- Only section 38 is lifted; its resident callees still run emulated.
- 16 functions `ovl:0x053019`–`ovl:0x053373` are reached only through data
  (probably region or picture-condition tables); they lift but no replay
  has exercised them.
- No readable C yet: next comes naming of the call-graph services
  ([call-graph.md](call-graph.md)) and cleanup into `src/story/`.
