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
| `exe:0x1302–0x1a13` | `gate_exe_1302()` | Entire enclosing original intro: initial settings, signed mode fork, short startup branch, 18 explicit caption calls, seven palette-bar calls, five animated sequences, repeated input checks, and distinct cleanup/final paths | Linked graphics services and original mode meaning remain unresolved |
| `exe:0x1a14–0x1c10` | `gate_exe_1a14()` | Four mode-dependent title palette stages: single-byte change for signed mode below 2, or 64-step three-component fade otherwise, with three early key exits | RTL 0f10:0c6e service not yet decompiled |
| `exe:0x0f61–0x1039` | `gate_exe_0f61(uint16_t)` | Prepares image in a 140-byte stack work area; uploads a blank 768-byte RGB palette, then 16 palettes scaled by signed `component * factor / 64` for factors `0,4,…,60`, then the complete original palette | Linked image preparation functions remain opaque; stack far-pointer host substitute is not a recovered compiler ABI |
| `exe:0x103a–0x117d` | `gate_exe_103a()` | Original text-measurement pass, with far pointers to caller's left/top/right/bottom words, signed line-wrap decisions, an 80-byte word buffer, and a signed bottom clamp at 185 that shifts the caller's top coordinate | Font width/line-height service bodies not yet recovered; unchecked word buffer is original behavior |
| `exe:0x117e–0x1301` | `gate_exe_117e()` | Select intro font, call `0x103a` with pointers to its own arguments, draw expanded border, emit words/position and advance through whitespace/newlines, then wait `uint16_t(2000+45*strlen(text))` | Frame and text RTL service semantics still unresolved; no guest-entry differential check |

The long introductory sequence at **`exe:0x13fc` is not a separate
function**. Its entry is a `JGE` branch target inside `exe:0x1302`, whose
first instructions occur immediately after the `RETF` of the preceding
routine. Mode 0 follows an initial two-resource splash into a short path;
nonzero modes call `exe:0x1a14` first. Signed modes below 2 then select the
short path; modes at least 2 enter the 18-caption introduction. Escapes from
that path jump to the cleanup call at `exe:0x1a09`, while the short path
jumps directly to finalization at `exe:0x1a0e`. This distinction is visible
in the original 8086 branches and is intentionally explicit in C.

The caption path shows why layout must be studied before trying to reproduce
pixels: `exe:0x103a` modifies **the caller's original arguments** through
five far-pointer parameters (four local word addresses and the source text).
It adds five to the left/top and subtracts five from the right for the
inner text region, measures each word, counts consecutive spaces as one
advance, and moves the top up if the computed bottom exceeds 185. Only then
does `exe:0x117e` draw the original outer frame and perform a second pass
to print the words. Both functions use an unchecked 80-byte stack word
buffer; the lack of bounds checking is preserved and documented as an
original potential bug, not silently repaired.

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
The `uint16_t *` parameters in `gate_exe_103a` express the original
SS-relative far pointers to the *caller's* stack variables at source level;
their exact DOS compiler ABI remains to be verified. Word scan offsets
increment modulo 65536 without carrying into the source far segment.

The associated `launcher-decompile` CTest target compiles this C directly and
checks register-width edge cases, short and long mode branches, Escape
behavior, all 18 caption calls and argument triples, all five animation
triples, the ordered frame/image calls, two-pass caption layout and palette
writes through mock
segmented memory. A separate `launcher-decompile-binary` test checks the
original routine boundaries, linked far and near call targets, signed mode
branches, clock comparisons, picture fade arithmetic and loop bounds. It
also verifies that the transcribed C's literal caption/animation/frame call
arguments match the SHA-256-pinned original executable. This is a *host
semantic/static test*, not yet a differential guest-execution test or a
recompiled DOS binary. A routine is only considered
fully verified once original-entry guest registers and memory effects can be
compared at the same routine boundary.

## Decompiled linked text RTL (GATE.RTL)

The intro's text services have now been mapped to original `GATE.RTL`
**file offsets**, using both the original `0c4d:xxxx` linked far calls and a
captured, relocated guest-memory image. They live in `gate_rtl.c`, separately
from the `GATE.EXE` code. These are the original compiler's distinct
routines, not newly invented layout/rendering functions:

| Original file range | Linked far call | C function | Recovered original operation |
| --- | --- | --- | --- |
| `rtl:0xebf7–0xec09` | `0c4d:0511` | `gate_rtl_ebf7` | Add two **signed** font-header bytes at +9 and +10 to obtain line advance. |
| `rtl:0xec0a–0xeca3` | `0c4d:0524` | `gate_rtl_ec0a` | Return special tab/newline widths; otherwise choose fixed, per-character-width or per-character-width-plus-packed-spacing mode via signed bytes at font +11/+12. |
| `rtl:0xeca4–0xece9` | `0c4d:05be` | `gate_rtl_eca4` | Normalize a far string pointer, sign-extend each character, sum advances modulo 65536; increment only the far pointer's 16-bit offset. |
| `rtl:0xecea–0xed00` | same RTL segment | `gate_rtl_ecea` | Get ink/background colors, unconditionally writing through two far pointers. |
| `rtl:0xed01–0xed11` | same RTL segment | `gate_rtl_ed01` | Set the two color words used by the glyph printer. |
| `rtl:0xed12–0xed38` | same RTL segment | `gate_rtl_ed12` | Return the cursor x/y via optional far pointers; check **both words together** for NULL. |
| `rtl:0xed39–0xed7d` | `0c4d:0653` | `gate_rtl_ed39` | Store the text cursor; if font header +3 is zero, convert signed pixel x/y to text-mode positions using SAR/IDIV and one-based coordinates. |
| `rtl:0xed7e–0xedfe` | `0c4d:0698` | `gate_rtl_ed7e` | Normalize a far string, call the original `rtl:0xedff` glyph routine for each signed character, or dispatch via the alternate text-mode printer and update cursor coordinates. |
| `rtl:0xedff–0xefd8` | same RTL segment | `gate_rtl_edff` | TAB/newline cursor handling, signed-width and packed-bearing calculations, bitmap offset via two truncated IMULs, per-row MSB-first bit traversal, and one far pixel-write call per set bit. |

The intro's active font points at a runtime descriptor with a four-byte
prefix, followed by the font header. Its `+9=1`, `+10=10`, `+11=-1` and
`+12=-1` bytes select the proportional graphics path. Two far pointers at
descriptor offsets +14 and +18 reference the exact original 128-byte
width and packed-spacing tables from `GATE_305.FNT`. A *captured* guest
at 22,000ms resolves the linked font pointer at `DS:54ca` and cursor x/y
at `DS:550a/550c`. Those linked offsets are **not present** in the
unrelocated RTL's zero-placeholder operands and may depend on its link
layout; the source presently uses named externals for these original C
globals, and for other external runtime calls still under investigation.

`launcher-rtl-decompile` runs the reconstructed C with instrumented far
memory and the original font (when available), checking all printable glyph
advances plus fixed/per-character modes, signed-byte arithmetic, offset
wraparound, text-mode control and glyph-print call order. It additionally
checks the glyph rasterizer against the original bitmap data for **every
printable character** of `GATE_305.FNT`, including each ink pixel coordinate
and advance, plus synthetic bitmask, offset-wrap, background and tab cases.
The distinct
`launcher-rtl-decompile-binary` test checks bounded original 8086 functions,
their compiler branches, far-call identities and, when the optional private
`build/intro-analysis/at-22000.bin` is present, the original relocated
DS globals and all original font width/spacing/bitmap arrays. **No runtime
call into the emulator is replaced yet**: the linked pointer-normalization
and lower-level video/pixel services
are explicit unresolved dependencies, not invented native graphics APIs.

## Immediate next functions

1. Decompile the far pointer normalization services called at
   `rtl:0xecb0/0xed8a` and the pixel writer called at `rtl:0xef83`.
   Also recover the text-mode cursor/print dependency calls at
   `rtl:0xed74/0xeda5/0xedb7`.
2. Recover the linked graphics and RTL calls (`0xf10:1680`, `0xf10:112e`,
   `0xf10:13d2`, `0xf10:16c6`, `0xcdc:04d2`) by their actual code bodies,
   then replace provisional service declarations in the C transcription.
3. Recover the other linked RTL calls by their identities in `GATE.RTL`, then
   replace the provisional far-call prototypes in the C transcription.
4. Recompile against an appropriate 16-bit C compiler and compare individual
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
objdump -D -b binary -m i8086 -M intel --start-address=0x1302 \
  --stop-address=0x13fc ../gateway-dos/GATE.EXE
objdump -D -b binary -m i8086 -M intel --start-address=0x1a14 \
  --stop-address=0x1c11 ../gateway-dos/GATE.EXE
objdump -D -b binary -m i8086 -M intel --start-address=0x0f61 \
  --stop-address=0x103a ../gateway-dos/GATE.EXE
objdump -D -b binary -m i8086 -M intel --start-address=0x103a \
  --stop-address=0x117e ../gateway-dos/GATE.EXE
objdump -D -b binary -m i8086 -M intel --start-address=0x117e \
  --stop-address=0x1302 ../gateway-dos/GATE.EXE
```

The disassembler treats the MZ file as a raw byte buffer so its printed
addresses are **file offsets**. Only bounded, independently recognized code
ranges should be translated; raw `objdump -D` across the whole executable
also disassembles embedded data as plausible instructions.
