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

## Decompiled graphics palette transitions (GATE.RTL)

Five further original functions are recovered in `gate_rtl_graphics.c`:

| RTL file offset | Original linked far entry | C function | Operation |
| --- | --- | --- | --- |
| `0x12773–0x12821` | `0f10:0c6e` | `gate_rtl_12773()` | For graphics mode 3, upload 17 palettes containing 768 signed bytes scaled by `0,4,...,64`; for other nonzero modes except 1, scale 48 bytes; mode 1 uploads a null FAR pointer once. |
| `0x12823–0x12891` | `0f10:0d1e` | `gate_rtl_12823()` | Clear a positive, mode-indexed signed number of bytes in the original video buffer, then upload a null palette pointer. |
| `0x12893–0x12995` | `0f10:0d8e` | `gate_rtl_12893()` | Mode 1: three rounds of 16 destructive byte bit-transformations, null palette uploads, and 100ms delays. Other nonzero modes: 17 palettes with factors `64,60,...,0`, followed by `rtl:0x12823`. |
| `0x13185–0x13190` | `0f10:1680` | `gate_rtl_13185()` | Clear the first byte of an unconditionally dereferenced far work-area pointer. |
| `0x131cb–0x131f5` | `0f10:16c6` | `gate_rtl_131cb()` | If the pointer is non-null and its first byte is set, call the original resource-release service on the same-segment pointer plus 8, then clear that byte. |

Both fades preserve the original low-16-bit IMUL result and signed
truncate-toward-zero division. The buffer reset preserves the mode table's
signed comparison and the original repeated read of its entry. All five
palette upload call sites in these functions resolve in a captured guest
image to original `GATE.RTL:0xf61b`, linked
as `0cdc:04d2`. The mode-1 delay resolves to `123f:00b6`. The
work-area cleanup service resolves to `145d:016c` and remains external.

`gate_rtl_linkage.c` connects these five far-call names and five already
recovered font far-call names directly to their source-shaped C functions;
it is a source-level linker adapter, not a fake graphics renderer or an
additional original C routine. Original RTL graphics globals remain explicit
unresolved dependencies.

The `launcher-rtl-graphics-decompile` test covers palette data and ordering,
signed sample values, both mode branches, the 16-byte destructive transform,
zero-length and negative-count paths, mode-index wrap, and the work area's
null-guarded cleanup and 16-bit far offset wrap. The independent
`launcher-rtl-graphics-binary` test checks original function boundaries and
all 16 mapped intro graphics/video entries; if the private captured RAM is
available, it also proves all 16 original RTL file identities and the
relevant relocated 8086 `CALL FAR` targets. These are host/source-level
checks, **not yet original-entry guest differential tests**.

## Original palette hardware and retrace services

`gate_rtl_palette.c` recovers the actual palette uploader reached at
`0cdc:04d2`, rather than treating it as a generic drawing callback:

| Original `GATE.RTL` file range | C function | Recovered control flow |
| --- | --- | --- |
| `0xf473–0xf49a` | `gate_rtl_f473()` | Read the original display status port until retrace bit 3 clears, then repeatedly until it sets. No timeout. |
| `0xf61b–0xf826` | `gate_rtl_f61b(GateFarPtr)` | Wait for retrace; dispatch by original palette mode and display-adapter type. For adapter type 5, program VGA DAC index `0x3c8` and data `0x3c9` through direct RGB triples, 2-bit packed-to-6-bit RGB, or two original lookup tables plus a zero blue component. For other adapters, program 16 attribute-controller entries at `0x3c0`. Re-enable the attribute controller after non-mode-3 output. |

The original source reads the first byte of the FAR palette argument even
when the argument is `0000:0000`, and the low-level palette uploader is
therefore *not* a null-pointer-safe host routine. It uses signed comparison
and `IDIV` for the color count and lookup quotient; the FAR pointer's offset
increments by three without segment carry in the direct/converted paths.
None of these quirks were removed.

The source-level linkage now maps `gate_far_0cdc_04d2` to `gate_rtl_f61b`.
The earlier fade helpers pass a host stack pointer; their isolated linker
adapter converts it into the original FAR shape via `gate_stack_pointer()`.
That stack-memory service and the original palette globals still need a
real guest-memory/host adapter before the intro can run outside DOS.

`launcher-rtl-palette-decompile` verifies all major palette modes through
instrumented port reads/writes and FAR memory, including retrace transitions,
byte-exact ordered VGA writes, packed channel extraction, signed lookup
quotients and wrapping FAR pointers. Original routine boundaries and the
near retrace-call target are checked by `launcher-rtl-graphics-binary`.
This **does not yet prove native 8086 entry-state equivalence**.

## Original image display entry point

`gate_rtl_image.c` also recovers the complete outer image-display routine
`GATE.RTL:0x12b19..0x12b66`, linked from `GATE.EXE` at `0f10:1014`.
It allocates an unchecked 136-byte stack work record, initializes only its
first byte, calls original `rtl:0x12c33` to load a requested resource,
and, only on nonzero return, invokes original `rtl:0x1266f`, calls the
`rtl:0x12ed7` image processor, invokes the recovered `rtl:0x131cb` work
cleanup, and returns 1. On load failure, it returns 0 immediately.

The original callers ignore AX; the source-level far-call adapter retains
their `void` signature but `gate_rtl_12b19()` itself preserves the original
16-bit result. Independent tests check both paths and complete call order;
the pinned RTL binary test verifies the four **near call** targets.

`gate_rtl_loader.c` now also transcribes the complete bounded image loader
`GATE.RTL:0x12c33..0x12d66`, linked as `0f10:112e`. It rejects an active
work record, clears the two display bytes, asks the original resource service
`rtl:0x11ecb` to load the requested resource, calls original allocation
service `145d:00f7` with the header dimensions, then initializes the work
area using `1479:0002`. It retains the relocated DS segment words at record
offsets 2/4, copies the two header bytes to offsets 6/7, derives the display
flag at offset 1, and computes the mode-dependent stride at offset `0x1c`.
The unusual CWD/XOR/SUB/SAR/XOR/SUB sequence is preserved as wrapping 16-bit
arithmetic, *including* the `-32768` negation overflow. Finally, the original
`0eec:018a` service checks the work record's words at `0x20/0x22`: failure
calls `rtl:0x131cb` to clean up and returns 0; success calls `rtl:0x12205`
and returns 1. Earlier allocation/resource failures do not call cleanup.

The new isolated C test exercises the active, load-failure, allocation-failure,
validation-failure and successful paths, exact call order, FAR offset wrapping,
header fields and signed stride edge cases. The pinned-original binary test
checks the loader's near calls and three linked far service identities using
the optional private guest RAM snapshot.

`gate_rtl_resource.c` also reconstructs the original resource selector
`GATE.RTL:0x11ecb..0x120f2` used by that loader. It preserves resource group
selection (including the special high-bit form), the 100-entry group stride,
the resource file cache and sticky open-error latch, 12-byte descriptor
reads, 32-bit descriptor offsets, signed mode limit, optional two-word mode
data, compressed-format table lookup, and dimension validation. All nine
original resource I/O call sites resolve to five distinct `0c33` FAR
services. The isolated C test covers cache hits/misses, sticky failures,
resource-ID calculations, descriptor rejection, negative mode handling,
compressed formats, and exact ordered calls.

**None of these tests is a full guest-entry differential run.** The
underlying five `0c33` file services and `rtl:0x12205` post-load routine
are still unresolved external dependencies. This remains **neither a
rendered nor a playable source-C intro**.

### Independent C build, including the Nix flake

```sh
nix build .#intro-decompile -L
# Installs lib/libgateway_intro_decompiled.a and include/gateway/intro/*.h.

# Equivalent without Nix/SDL/Unicorn:
cmake -S . -B build/intro-c-only -DGATEWAY_INTRO_DECOMPILE_ONLY=ON
cmake --build build/intro-c-only
ctest --test-dir build/intro-c-only --output-on-failure
```

**This is not yet a playable source-C intro.** The archive contains C object
code with unresolved original DOS/RTL services and segmented-memory globals.
There is no source-C executable entry point or native host adapter yet.
`nix run .#intro` still starts the independent C++ presentation prototype,
not this decompilation. A successful CMake/Nix build only proves the archive
and tests can compile; it does *not* demonstrate that the C intro plays.

With graphics now mapped to immutable file identities, the immediate intro
dependencies to recover next are the original RTL `0x12205` post-load setup,
the `0x12ed7` image processor and
the `0x1266f` preparer, plus the `145d:016c` resource-release service. Recover the remaining
video/keyboard/timing services and use the original guest as an entry-state
oracle *before* claiming a standalone intro or resuming game logic.

## Immediate next functions

1. Decompile the remaining eight mapped graphics/video entry points,
   preserving their original ABI and data globals.
2. Recover the linked font pixel service (`rtl:0xef83`), text-mode video
   dependencies (`rtl:0xed74/0xeda5/0xedb7`), keyboard, mouse and timing
   services on the intro's actual call graph.
3. Replace the source-C shim's unresolved callback globals and services with
   individually verified original source or narrow host hardware adapters.
4. Validate the decompiled C with guest-entry memory/register/side-effect
   differential tests, then demonstrate a real runnable intro backed by
   `gateway_intro_decompiled`. Do not resume game-logic decompilation until
   this milestone passes.

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
