# GATE.EXE: source-shaped C decompilation

This directory is the **decompilation**, not the native SDL intro player.
Its goal is to reconstruct the original 16-bit C program function by function,
with its actual calls, loops, data accesses, argument order, arithmetic and
bugs. Names are provisional where the original symbol table is absent. A
function name such as `gate_exe_0f18` gives its immutable **file offset** in
the original `GATE.EXE`, not a DOS runtime `CS:IP`.

The executable is compiled native 8086 code, **not** a small interpreted
script. Its original C source, compiler version, and library headers have not
been recovered. These `.c` files are a manual decompilation of the disassembly;
they are **not** a claim to possess byte-for-byte original source text.

## Recovered source-shaped functions

| Original file range | C function | Recovered behavior | Uncertainty |
| --- | --- | --- | --- |
| `exe:0x0e37–0x0f17` | `gate_exe_0e37()` | Poll key, dispatch key 3/Escape/mouse bit 1, set `DS:35fc`, display first-key message, interruptible 3500ms wait | External text/UI functions retain linked addresses; exact types unknown |
| `exe:0x0f18–0x0f60` | `gate_exe_0f18(int16_t)` | Signed 16-bit delay added to DX:AX clock, signed-high/unsigned-low deadline, keyboard/mouse early return | Clock RTL name and units inferred; DX:AX ABI certain |
| `exe:0x1c11–0x1c6b` | `gate_exe_1c11(int16_t)` | If no key pending, update DAC component `45+3*bar` through 64 values and call palette upload each time; wait 500ms | Meaning of palette-upload far pointer not fully proven |
| `exe:0x1c6c–0x1caa` | `gate_exe_1c6c()` | Write same 0..63 grayscale value to three adjacent DAC components, uploading palette each step | Exact RTL source declarations unknown |

Each function is translated from its complete bounded disassembly, not from
a collection of framebuffer screenshots. Compiler-generated stack-check calls
to `02a5:0c08` and BP prologue/epilogue are not original C statements and are
therefore omitted. All other external operations retain their original linked
far-call identities in `gate_exe.h` until the called routines are decompiled.

The source currently uses `gate_ds_word()`, `gate_far_word()` and
`gate_far_set_byte()` to express the original memory dereferences without
assuming an unverified DOS data structure. These are **temporary source-level
memory intrinsics**. Once the corresponding original data declarations are
recovered, replace them with C structs and far pointers while preserving
their offsets. They are not a proposed game engine or renderer API.

The associated `launcher-decompile` CTest target compiles this C directly and
checks register-width edge cases, keyboard bypasses, exact loop counts, DAC
offsets, fixed far-pointer arguments and text-display side effects through a
mock segmented memory. A separate `launcher-decompile-binary` test checks all
four routine boundaries, their linked far-call targets, the clock comparison
branch instructions, and the loop constants directly against the SHA-256-pinned
original executable. This is a *host semantic/static test*, not yet a differential
guest-execution test or a recompiled DOS binary. A routine is only considered
fully verified once original-entry guest registers and memory effects can be
compared at the same routine boundary.

## Immediate next functions

1. `exe:0x13fc–0x1a13`: decompile the **actual inline introductory call
   sequence**, with 18 explicit caption calls, seven palette bar calls, five
   sequence-animation calls, original key checks and shared end path. Do not
   turn it into a guessed page/animation table.
2. `exe:0x117e`: recover the common framed-caption routine in C, including
   its original local variables, word drawing, and `2000+45*strlen` wait.
3. `exe:0x103a`: recover the word measurement and wrapping routine, using the
   `GATE_305.FNT` external data format, not captured panel geometry.
4. Recover the linked RTL calls by their identities in `GATE.RTL`, then
   replace the provisional far-call prototypes in the C transcription.
5. Recompile against an appropriate 16-bit C compiler and compare individual
   entry/return register and memory states under the Unicorn probe. The
   native host port comes **after** these source-level routines are recovered.

Reference disassembly (read original files only; no binary bytes committed):

```sh
objdump -D -b binary -m i8086 -M intel --start-address=0xe37 \
  --stop-address=0xf18 ../gateway-dos/GATE.EXE
objdump -D -b binary -m i8086 -M intel --start-address=0xf18 \
  --stop-address=0xf61 ../gateway-dos/GATE.EXE
objdump -D -b binary -m i8086 -M intel --start-address=0x1c11 \
  --stop-address=0x1cab ../gateway-dos/GATE.EXE
```

The disassembler treats the MZ file as a raw byte buffer so its printed
addresses are **file offsets**. Only bounded, independently recognized code
ranges should be translated; raw `objdump -D` across the whole executable
also disassembles embedded data as plausible instructions.
