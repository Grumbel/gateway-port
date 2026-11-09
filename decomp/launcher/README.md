# GATE.EXE: source-shaped C decompilation

Bulk Ghidra extraction for the intro's relocated `GATE.RTL` is now documented
in [intro Ghidra workflow](../../docs/intro-ghidra.md). The generated
pseudocode stays ignored under `build/` and is not automatically incorporated
into these manually reviewed C files.

This directory is the **decompilation**, not the native SDL intro player.
Its goal is to reconstruct the original 16-bit C program function by function,
with its actual calls, loops, data accesses, argument order, arithmetic and
bugs. Names are descriptive and provisional where the original symbol table
is absent. The original file offsets are retained in routine comments and
in [`symbol-names.tsv`](symbol-names.tsv), which maps the 148 former
address-based identifiers to their new names. The `gate_exe_` and `gate_rtl_`
entries in its first column identify original **file offsets**, while the
`gate_far_` entries identify linked `segment:offset` addresses observed in
one DOS memory layout, not permanent file offsets. The linked wrappers keep
their own `gate_linked_` names to distinguish a call-site alias from its
reconstructed RTL implementation. No renaming changes the original calling
conventions, control flow, or instruction-offset evidence.

The historical progress log in `docs/decompilation-plan.md` retains the
old names as recorded at the time; use the index to translate them.

## Named original memory layouts

Original hexadecimal memory locations remain in the definitions, not at
individual call sites. [`gate_memory_layout.h`](gate_memory_layout.h) records
the **absolute linked DS byte offsets** (for example
`GATE_DS_FILL_PATTERN = 0x12de`), the separate video-descriptor relative
offsets, and the resident descriptor table at `DS:27ee` with its `0x3e`-byte
stride. [`gate_rtl_image.h`](gate_rtl_image.h) records the distinct
**relative** layouts of the 12-byte resource header, the resource-loader
scalars at `DS:1416`, the 136-byte picture record and its embedded VFX image
at record `+0x08`. For example, the picture's pixel FAR pointer is at record
`+0x08 + 0x18` (offset WORD) and `+0x08 + 0x1a` (segment WORD). Animation
delays are picture-record bytes `+0x06` and `+0x07`.

The eighteen sequential `GATE_DS_INTRO_CAPTION_01` through `_18` names
also pin their original `DS:363b` through `DS:4820` string pointers in the
same header. Title/palette stages use separately named channel offsets.

[`gate_intro_schedule.h`](gate_intro_schedule.h) names every picture-resource
ID and animation frame-count argument in the original `GATE.EXE:1302..1a13`
intro and `EXE:1a14..1c10` title interlude. The exact 16-bit resource IDs
remain in the enum declarations (`0x3105` = `GATE_301.PIC` entry #5;
`0x332a` = `GATE_303.PIC` entry #42); names such as
`GATE_INTRO_PICTURE_CAPTIONS_01_02` describe *where the asset is used*,
not an unverified interpretation of its artwork. There are two 26-frame
preloaded sequences and five three-argument delta sequences. The latter's
source call offsets are `EXE:1743`, `1762`, `191f`, `1933`, and `1947`,
with original `(base, first delta, count)` values retained in the header.
The C mock test retains raw IDs as independent expected values; the
original-executable regression resolves named arguments back to these
literal constants before comparing against disassembled call operands.
The same header names the original 1,000/2,000/3,000ms scene waits, title
interlude waits, 500ms red-bar hold, and the 2,000 + 45ms-per-character
caption timeout. Caption font selection, 80-byte word buffer, five-pixel
text inset, 185-pixel bottom clamp, DAC channel strides and 64-level palette
ramps are documented there as source-level immediates. These are *not*
newly introduced presentation timings: the original loops and calls retain
their order, signed arithmetic and interruptibility.

The font cache at `DS:54ce` is a two-entry array of 30-byte records, with
named byte and FAR-pointer displacements (`GATE_FONT_SLOT_*`) shared by the
font loader and glyph renderer. The 14-byte resident video-command packet
has a separate `GATE_VIDEO_PACKET_*` layout in `gate_rtl_video.h`, and
`gate_rtl_palette.h` distinguishes the original palette-table offsets
`0x035c`/`0x0366` from the x86 VGA I/O port numbers. Hardware aperture
segments (`A000` and `B800`) and the resident clear callback offsets remain
explicitly documented in `gate_memory_layout.h`.

These names are labels for **observed memory usage**, not proven original
compiler symbols. Ambiguous startup words and reserved descriptor fields
have deliberately neutral labels. The `*_SEGMENT` DS slots hold relocated
segment values: callers still read those WORDs dynamically and then access
their named *relative* descriptor offsets. A named constant must never be
treated as a flat native pointer, nor should the mode descriptor's `+0x3e`
be confused with the unrelated DS absolute offsets or resource record fields.
Static and guest-memory tests retain raw addresses where they serve as
independent oracles for this mapping.

Startup and RTL numeric operands are also grouped by their actual calling
context. At `GATE.EXE:1309/1314/131f/132a`, the four original startup writes
set `DS:0660=0x0401`, `DS:065c=0x007f`, `DS:065e=1`, and `DS:064e=0` via
relocated segment selectors; `gate_memory_layout.h` names those defaults.
`GATE.RTL:c3c5` separately tests music flags `DS:0662 & 0x06` and a nonzero
`DS:0660` value. The second startup parameter's meaning is **unverified**, so
its name describes position, not an invented sound or renderer option.

At `GATE.RTL:11ecb..120f1`, `gate_rtl_image.h` names the 16-bit picture
resource-ID encoding: high bit `0x8000` selects the current graphics-mode
group; otherwise the group is the signed ID shifted right by 12. File number
is `100*group + ((ID>>8)&0x0f)`, with directory entry `(ID&0xff)` and
12-byte headers.
The cached-file empty sentinel remains `0xffff`, and the nonpositive
picture-sequence count remains **256** (including the original 32-record
array overrun hazard). Both animation paths retain the original `50`-ms
base/spread multiplier from picture record `+06/+07`.

The `0`/`1` second argument of intro picture loads is a **picture-position
selector**, not a boolean flag or VGA mode: at `RTL:11ecb..120f1`, zero
uses `(x=0,y=0)`, while one requests the first 4-byte `(x,y)` pair at the
picture-data offset stored in the header, subject to its signed count clamp.
`GATE_PICTURE_POSITION_ORIGIN` and `GATE_PICTURE_POSITION_FIRST_PAIR` name
these values in the `EXE:1302..1a13` intro, `RTL:12f6b..130aa` delta
animations and `RTL:130ab..13114` preloaded sequences. Independent tests
continue to check numeric arguments directly.

The same header now names the low-level image post-load conversions at
`RTL:12131..123d3`: the signed format tags `-1` (`0xffff`, compact rows)
and `+1` (expand rows), their respective **16**- and **48**-byte palette
copies, the unusual WORD-sign/SAR masks used in the three-quarters
coordinate calculation, and the reverse four-thirds calculation. Every
fourth source row is dropped by signed remainder `row % 4 == 3`; the
other format's end-row adjustment uses `row % 3 == 2`. The original
VFX record's row-stride and plane-row multipliers remain **4**, and
`RTL:12c33` retains the signed `(width + 7) / 8` emulation, including
the 8086 `0x8000` intermediate-negation corner case. These names describe
observed operations, **not** independently identified picture-file codecs.

The resident VFX construction and clear paths (`245d:00f7`,
`2479:0002`, `245d:016c`) now use explicit definitions for table mode
indices **2** (640×480, four 1-bit planes) and **3** (320×200, one 8-bit
plane), eight-bit/byte packing, even-byte row alignment, FAR 64KiB pixel
wrapping, the original `0xffe6` allocation failure, `0xca00` signature,
`0x65` kind byte and **128**-byte record length. The identity of kind
`0x65` remains unknown. Rectangle/frame flags are separately named in
`gate_rtl_draw.h` for `RTL:fc37` and `RTL:ff99`, including the fixed
background setup code **10**, whose broader meaning is not established.
The all-ones pixel fill byte `0xff` and the distinct `0xff` byte written
throughout a released 128-byte image record are separately named. The
rectangle signed-top clamp retains its original **-1** (`0xffff`) bound.

The next naming pass covers text-mode and resource-file operands.
`gate_rtl_video.h` identifies TAB `9`, newline `10`, the eight-pixel text
cell, two four-bit glyph-spacing fields, glyph MSB mask `0x80`, and the
signed WORD masks used by `RTL:ed39` and `RTL:fa17` text-position/tab
rounding. `gate_memory_layout.h` keeps the original font pointer-half
offset, font resource file type `2`, group multiplier `100`, invalid-cache
byte `0xff`, and fallback character and width values used by
`RTL:e771..ebcf`. These are distinct from the relocated font memory
offsets and 30-byte slot layout already recorded there.

The original resource-file wrapper at `RTL:e511..e5c4` uses an 80-byte
stack filename, a four-byte path-table entry, path probe argument `4`,
and file type `5` to select create bit `0x80`; the same bit is tested by
the resident DOS `1c29:0000` open routine. A failed one-byte read
returns `0xffff` whether EOF or I/O failure, while failed resident DOS
seek returns its separate `0xffff` sentinel. The source retains distinct
names for those equal values. All independent mock expectations remain
numeric to continue checking the original operands.

Palette and display immediates are defined near their original services:
`gate_rtl_graphics.h` names the `64`-denominator/`4`-step signed fades and
the mode-1 three-pass, 16-byte transform with 100ms waits
(`RTL:12773..12995`). `gate_rtl_mode.h` names the text, VGA-256, SVGA,
invalid-mode, adapter and monitor selector values; `gate_rtl_palette.h`
names VGA adapter type `5` and the 16 attribute-palette entries
(`RTL:f49b..f4de`, `RTL:f61b..f826`). These are original C/RTL operands,
not new hardware emulation settings. Original mode-to-BIOS mapping and
unidentified adapter/monitor classifications are still provisional.
The eight-byte resident adapter-query output at `242a:000a` now uses
named offsets for its verified first two fields (video adapter type and
monitor type). At `RTL:ff99`, graphics-mode index `1` selects the original
special caption-edge branches; its hardware mode identity is not inferred.

`gate_rtl_palette.h` also names the original source's low-level color
conversion arithmetic at `RTL:f61b..f826`: packed two-bit RGB channel
locations, the six lookup bit weights (`2,2,2,1,1,1`), the DAC step
of `21` (producing component intensities `0,21,42,63`), signed
sample divisor `31`, RGB triple width `3`, and mode-indexed color-count
table arguments. Mode `1` is the packed branch; mode `3` skips the
attribute-controller overscan tail. These are source-verified operands,
not inferred descriptions of a new palette format.

`gate_c_runtime_constants.h` documents original Microsoft C 5.1
`rand.c` parameters at `RTL:91d8/91ea`, linked `12a5:3afa/3b0c`:
LCG multiplier `0x343fd`, increment `0x269ec3`, 32-bit wrap, and
15-bit result mask `0x7fff`. The linked clock/timezone code separately
names the three-byte zone abbreviations, `_DIGIT` classification bit
`4`, `3600`-second timezone conversion, and `1000`-millisecond clock
conversion at `RTL:8cc8..8d9a` / `RTL:8fa2..8ff8`. Ten-byte time
sample field positions are *relative stack offsets*, not DS addresses.
As before, the incomplete original time sampler is not treated as a
verified replacement for the native intro's host clock.

The remaining launcher input and title literals are collected in
`gate_intro_schedule.h`: linked `0df2:00f8..0127` buffers any key other
than ASCII space `0x20` or carriage return `0x0d`; original
`GATE.EXE:0e37..0f17` tests key `3` (a special control-key handler),
Escape `27`, and mouse flag mask `0x0002`. The name of that mouse mask
describes the observed skip branch, not an unverified physical button.
The text background argument `0xffff` is signed -1, which suppresses
glyph background painting at `RTL:edff`. The short-path pen `15`,
shared full-screen rectangle style `2`, paint-mode index `1`, RGB-mode threshold `2`, and
green/blue palette component displacements `1/2` all retain their original
values in the header; the four signed mode tests remain separate loads.
At `GATE.RTL:8298` (linked `12a5:2bba`),
`gate_c_runtime_constants.h` additionally names the Microsoft CRT
`strlen` 65535-byte scan limit and 65534 result for unterminated input.
These values are constants of the original linked behavior, not native
buffer sizes or a general Unicode/keyboard abstraction.

The executable is compiled native 8086 code, **not** a small interpreted
script. Its original C source, compiler version, and library headers have not
been recovered. These `.c` files are a manual decompilation of the disassembly;
they are **not** a claim to possess byte-for-byte original source text.

## Recovered source-shaped functions

| Original file range | C function | Recovered behavior | Uncertainty |
| --- | --- | --- | --- |
| `exe:0x0e37–0x0f17` | `gate_exe_handle_intro_input()` | Poll key, dispatch key 3/Escape/mouse bit 1, set `DS:35fc`, display first-key message, interruptible 3500ms wait | External text/UI functions retain linked addresses; exact types unknown |
| `exe:0x0f18–0x0f60` | `gate_exe_wait_interruptible(int16_t)` | Signed 16-bit delay added to DX:AX clock, signed-high/unsigned-low deadline, keyboard/mouse early return | Clock RTL name and units inferred; DX:AX ABI certain |
| `exe:0x1c11–0x1c6b` | `gate_exe_reveal_red_bar(int16_t)` | If no key pending, update DAC component `45+3*bar` through 64 values and call palette upload each time; wait 500ms | Meaning of palette-upload far pointer not fully proven |
| `exe:0x1c6c–0x1caa` | `gate_exe_reveal_credit_color()` | Write same 0..63 grayscale value to three adjacent DAC components, uploading palette each step | Exact RTL source declarations unknown |
| `exe:0x1302–0x1a13` | `gate_exe_play_intro()` | Entire enclosing original intro: initial settings, signed mode fork, short startup branch, 18 explicit caption calls, seven palette-bar calls, five animated sequences, repeated input checks, and distinct cleanup/final paths | Linked graphics services and original mode meaning remain unresolved |
| `exe:0x1a14–0x1c10` | `gate_exe_play_title_interlude()` | Four mode-dependent title palette stages: single-byte change for signed mode below 2, or 64-step three-component fade otherwise, with three early key exits | RTL 0f10:0c6e service not yet decompiled |
| `exe:0x0f61–0x1039` | `gate_exe_fade_in_picture(uint16_t)` | Prepares image in a 140-byte stack work area; uploads a blank 768-byte RGB palette, then 16 palettes scaled by signed `component * factor / 64` for factors `0,4,…,60`, then the complete original palette | Linked image preparation functions remain opaque; stack far-pointer host substitute is not a recovered compiler ABI |
| `exe:0x103a–0x117d` | `gate_exe_measure_caption()` | Original text-measurement pass, with far pointers to caller's left/top/right/bottom words, signed line-wrap decisions, an 80-byte word buffer, and a signed bottom clamp at 185 that shifts the caller's top coordinate | Now exercised with the real recovered font; unchecked word buffer is original behavior |
| `exe:0x117e–0x1301` | `gate_exe_show_caption()` | Select intro font, call `0x103a` with pointers to its own arguments, draw expanded border, emit words/position and advance through whitespace/newlines, then wait `uint16_t(2000+45*strlen(text))` | Native integration tests exercise all eighteen captions, but not through guest-entry differential execution or an SDL loop |

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
The `uint16_t *` parameters in `gate_exe_measure_caption` express the original
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

## Linked startup keyboard and video wrappers

The 22-second linked DOS RAM snapshot contains two adjacent keyboard
functions in the `0df2` bank that are directly called by `GATE.EXE`:

| Linked entry | Reconstructed C | Original behavior |
| --- | --- | --- |
| `0df2:00f8..0127` | `gate_linked_read_buffered_key()` | If a key is pending, read it; return that key and store it back to `DS:1340` unless it is Space (`0x20`) or CR (`0x0d`). Return zero without a read when none is pending. |
| `0df2:0128..0136` | `gate_linked_flush_keyboard()` | Repeatedly read and discard keys while the pending-key service returns nonzero. |

`gate_input.c` preserves their original conditional calls, 16-bit key
values and memory effects. A strict C11/UBSan test exercises absent keys,
Space/CR, Escape, Ctrl-C, replay of buffered keys and the flush loop;
original linked-byte checks pin both branch/call targets and the relocated
`DS:1340` operand. The existing `intro_host_input.c` adapters for linked
`0df2:000a/0030` now consult that guest DS slot after the injected host
queue, so a native integration test verifies the full **key read → unget →
pending → reread → flush** path without a second fake buffer. The special
`DS:133e` Ctrl-Break counter and DOS `INT 21h:0c00` reset path remain an
HLE approximation using the host key callback, not source-C guest-entry
equivalence. SDL keyboard delivery itself is still pending.

The previously unresolved `0cdc:07d8` video call from `EXE:1302` is
now resolved to original `GATE.RTL:f921..f935`: invoke linked resident
`2488:000a` with two zero words, then draw the inverse block through
`rtl:f8eb` only when the cursor-block mode is nonzero. Source-C and
linked-snapshot tests verify the order, operands, conditional branch,
relocated `DS:030e` word and resident target. The resident service's
actual video/page side effects are **not yet modeled**; the C boundary
remains explicit instead of guessing a clear-screen or page-switch API.

## Decompiled linked text RTL (GATE.RTL)

The intro's text services have now been mapped to original `GATE.RTL`
**file offsets**, using both the original `0c4d:xxxx` linked far calls and a
captured, relocated guest-memory image. They live in `gate_rtl.c`, separately
from the `GATE.EXE` code. These are the original compiler's distinct
routines, not newly invented layout/rendering functions:

| Original file range | Linked far call | C function | Recovered original operation |
| --- | --- | --- | --- |
| `rtl:0xe771–0xe79a` | same RTL segment | `gate_rtl_clear_font_cache` | Release both 30-byte font-cache slots in increasing order. |
| `rtl:0xe79b–0xe800` | same RTL segment | `gate_rtl_release_font_slot` | Reset one occupied font slot, release width/spacing/bitmap FAR allocations and update the active pointer. |
| `rtl:0xe801–0xe872` | same RTL segment | `gate_rtl_choose_font_slot` | Choose an empty slot or evict the lowest signed-reference-count slot; free its previous allocations. |
| `rtl:0xe873–0xebcf` | `0c4d:018d` | `gate_rtl_select_font` | Select a cached font or open a `.FNT` resource, read its ten-byte header, allocate optional width/spacing tables and bitmap, retry/fallback on heap failure, and update video text metrics. |
| `rtl:0xebf7–0xec09` | `0c4d:0511` | `gate_rtl_font_line_advance` | Add two **signed** font-header bytes at +9 and +10 to obtain line advance. |
| `rtl:0xec0a–0xeca3` | `0c4d:0524` | `gate_rtl_font_character_advance` | Return special tab/newline widths; otherwise choose fixed, per-character-width or per-character-width-plus-packed-spacing mode via signed bytes at font +11/+12. |
| `rtl:0xeca4–0xece9` | `0c4d:05be` | `gate_rtl_measure_text_width` | Normalize a far string pointer, sign-extend each character, sum advances modulo 65536; increment only the far pointer's 16-bit offset. |
| `rtl:0xecea–0xed00` | same RTL segment | `gate_rtl_get_text_colors` | Get ink/background colors, unconditionally writing through two far pointers. |
| `rtl:0xed01–0xed11` | same RTL segment | `gate_rtl_set_text_colors` | Set the two color words used by the glyph printer. |
| `rtl:0xed12–0xed38` | same RTL segment | `gate_rtl_get_text_cursor` | Return the cursor x/y via optional far pointers; check **both words together** for NULL. |
| `rtl:0xed39–0xed7d` | `0c4d:0653` | `gate_rtl_set_text_cursor` | Store the text cursor; if font header +3 is zero, convert signed pixel x/y to text-mode positions using SAR/IDIV and one-based coordinates. |
| `rtl:0xed7e–0xedfe` | `0c4d:0698` | `gate_rtl_draw_text_string` | Normalize a far string, call the original `rtl:0xedff` glyph routine for each signed character, or dispatch via the alternate text-mode printer and update cursor coordinates. |
| `rtl:0xedff–0xefd8` | same RTL segment | `gate_rtl_draw_font_glyph` | TAB/newline cursor handling, signed-width and packed-bearing calculations, bitmap offset via two truncated IMULs, per-row MSB-first bit traversal, and one far pixel-write call per set bit. |

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

The font selector itself is now recovered at `RTL:e771..ebcf` and linked
from original `0c4d:018d`. The pinned live code relocates its two 30-byte
slots to `DS:54ce/54ec`; type, fallback height and font metrics use the
separate DS segment selectors at `4d84..4d8e`. Real FAR calls resolve to
the previously recovered `0c33` file open/byte-read/block-read/close
functions and the native-heap boundary `1e2c:0008/0138`. The original
header is read byte-by-byte, then optional 128-byte width and spacing
tables and a signed-product bitmap length are allocated and read, in that
order. Allocation failure releases both slots, retries the same file once,
and finally constructs the original default text font. Slot eviction,
cache hits, signed text-metric rounding and 16-bit arithmetic remain in
the reconstructed C; no synthetic bitmap is substituted for font data.

`launcher-rtl-font-decompile` and `launcher-rtl-font-binary` cover control
flow, fail/retry cases and original linked addresses. The native font
integration test additionally opens real `GATE_305.FNT` using the recovered
RTL file selector and captured 22-second DS tables, verifies *every byte*
of its three allocated arrays, then checks the original advance functions
and metric globals. It requires a private guest-memory snapshot and game
data and is omitted if these are unavailable. This is a verified isolated
font path, also used by the native caption integration below, **not** an
independently runnable source-C intro.

`launcher-intro-native-caption-tests` connects the original `GATE.EXE:117e`
caption renderer directly to the reconstructed source-C word measurement,
font selection, glyph rasterizer, caption-frame painter, DOS file operations,
native FAR heap, VGA pixels and deterministic input/clock adapters. Its
optional private-data CTest `launcher-intro-native-original-caption` reads
the 22-second DOS RAM snapshot and actual `GATE_305.FNT`, then renders all
**eighteen original captions** using their arguments from `EXE:1302`.
No text or font assets are embedded in the test sources. Every call verifies
font selection, file-handle cleanup, original duration, a nonempty frame,
and the first glyph's ink/background pixels against independently decoded
font bitmap bits. With the available snapshot, all 18 pass (323,066
nonzero pixels in total, 268 verified first-glyph ink pixels). An optional
PGM output argument exports the first caption with a *diagnostic* contrast
map, not the DOS VGA DAC palette. These are independently run original C
**caption calls**, not the enclosing `gate_exe_play_intro` presentation, which
still needs its graphics, event loop and startup dependencies wired together.

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
DS globals and all original font width/spacing/bitmap arrays. These tests
are separate from the native host's bounded VGA pixel and solid-background
services described below. The source-C intro has not been wired into the
emulator or an independent runnable host.

## Decompiled graphics palette transitions (GATE.RTL)

Five further original functions are recovered in `gate_rtl_graphics.c`:

| RTL file offset | Original linked far entry | C function | Operation |
| --- | --- | --- | --- |
| `0x12773–0x12821` | `0f10:0c6e` | `gate_rtl_fade_palette_in()` | For graphics mode 3, upload 17 palettes containing 768 signed bytes scaled by `0,4,...,64`; for other nonzero modes except 1, scale 48 bytes; mode 1 uploads a null FAR pointer once. |
| `0x12823–0x12891` | `0f10:0d1e` | `gate_rtl_clear_palette_buffer()` | Clear a positive, mode-indexed signed number of bytes in the original video buffer, then upload a null palette pointer. |
| `0x12893–0x12995` | `0f10:0d8e` | `gate_rtl_fade_palette_out()` | Mode 1: three rounds of 16 destructive byte bit-transformations, null palette uploads, and 100ms delays. Other nonzero modes: 17 palettes with factors `64,60,...,0`, followed by `rtl:0x12823`. |
| `0x13185–0x13190` | `0f10:1680` | `gate_rtl_initialize_picture_record()` | Clear the first byte of an unconditionally dereferenced far work-area pointer. |
| `0x131cb–0x131f5` | `0f10:16c6` | `gate_rtl_release_picture_record()` | If the pointer is non-null and its first byte is set, call the original resource-release service on the same-segment pointer plus 8, then clear that byte. |

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
| `0xf473–0xf49a` | `gate_rtl_wait_vertical_retrace()` | Read the original display status port until retrace bit 3 clears, then repeatedly until it sets. No timeout. |
| `0xf61b–0xf826` | `gate_rtl_upload_vga_palette(GateFarPtr)` | Wait for retrace; dispatch by original palette mode and display-adapter type. For adapter type 5, program VGA DAC index `0x3c8` and data `0x3c9` through direct RGB triples, 2-bit packed-to-6-bit RGB, or two original lookup tables plus a zero blue component. For other adapters, program 16 attribute-controller entries at `0x3c0`. Re-enable the attribute controller after non-mode-3 output. |

The original source reads the first byte of the FAR palette argument even
when the argument is `0000:0000`, and the low-level palette uploader is
therefore *not* a null-pointer-safe host routine. It uses signed comparison
and `IDIV` for the color count and lookup quotient; the FAR pointer's offset
increments by three without segment carry in the direct/converted paths.
None of these quirks were removed.

The source-level linkage now maps `gate_linked_upload_intro_palette` to `gate_rtl_upload_vga_palette`.
The earlier fade helpers pass a host stack pointer; their isolated linker
adapter converts it into the original FAR shape via `gate_stack_pointer()`.
The palette's relocated DS globals and VGA port writes now have bounded
native host implementations, described below. The stack-pointer bridge,
actual BIOS/video presentation and complete intro call graph remain unconnected.

`launcher-rtl-palette-decompile` verifies all major palette modes through
instrumented port reads/writes and FAR memory, including retrace transitions,
byte-exact ordered VGA writes, packed channel extraction, signed lookup
quotients and wrapping FAR pointers. Original routine boundaries and the
near retrace-call target are checked by `launcher-rtl-graphics-binary`.
This **does not yet prove native 8086 entry-state equivalence**.

## Original display-mode switch (GATE.RTL)

`gate_rtl_mode.c` reconstructs `GATE.RTL:0xf14d..0xf389`, linked into the
intro as `0cdc:0004`. Its original C returns 0 for failure and 1 after
successful setup. It first marks mode `DS:0fce` as `0xffff`, even when an
out-of-range request fails. It queries resident adapter service `242a:000a`,
tests adapter and monitor capability tables, then selects the BIOS mode
through `2409:0008`. Graphics mode 4 optionally remaps to SVGA modes 5, 6
or 7. The separate `2409:003e` service enables the display.

The relocated source tables occupy `DS:12e2/12ee` (capabilities) and
`DS:12f8/1302/130c/1316/1320` (five mode descriptors). The routine writes
dimensions and the original signed-division-derived row count through far
segment selectors `DS:4d92..4d9e`, then initializes the mode-dependent
color and mask words through `DS:4da0..4da8`. The near helper
`rtl:0xf49b..0xf4de` initializes the VGA attribute entries 0..15 for
modes 1, 2 and 4 using the original `rtl:f473` retrace wait.

The native hardware adapter models the observed resident boundary:
adapter query, optional SVGA variant, BIOS mode selection, initial enable,
clip rectangle `25b7:003a` and clip enable `25b7:0008`. The resident
`2409:0008` stores the live pixel-dispatch mode at `DS:15cc`; the host
does likewise, so the **original source-C mode switch** configures the
existing planar and linear framebuffer services. Strict C11/UBSan tests
exercise normal modes, failures, SVGA variants, attribute palettes,
graphics-global writes and source-C mode-2/mode-3 pixel output. Another
test uses the original 22-second DOS RAM to verify the mode-3 table state.
Neither actual BIOS `INT 10h` nor an SDL window transition occurs yet.

## Original image display entry point

`gate_rtl_image.c` also recovers the complete outer image-display routine
`GATE.RTL:0x12b19..0x12b66`, linked from `GATE.EXE` at `0f10:1014`.
It allocates an unchecked 136-byte stack work record, initializes only its
first byte, calls original `rtl:0x12c33` to load a requested resource,
and, only on nonzero return, invokes original `rtl:0x1266f`, calls the
`rtl:0x12ed7` image processor, invokes the recovered `rtl:0x131cb` work
cleanup, and returns 1. On load failure, it returns 0 immediately.

The original callers ignore AX; the source-level far-call adapter retains
their `void` signature but `gate_rtl_show_picture()` itself preserves the original
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
services. These are now reconstructed in `gate_rtl_file.c`. The isolated C test covers cache hits/misses, sticky failures,
resource-ID calculations, descriptor rejection, negative mode handling,
compressed formats, and exact ordered calls.

`gate_rtl_file.c` transcribes thirteen bounded original-file routines in the
linked `0c33` bank: `rtl:0xe4ff`, `0xe511`, `0xe531`, `0xe582`, `0xe59b`,
`0xe5c5`, `0xe5e3`, `0xe5fc`, `0xe61e`, `0xe637`, `0xe650`, `0xe669`, and
`0xe68b`. They set the
resource pathname base, test a formatted-path service, format/open a resource
file, seek, read one byte/word/buffer or element records, write one
byte/buffer or element records, and close.
All five calls from the image selector use one-to-one linked wrappers;
the remaining routines have distinct original entry offsets even where
their instruction bodies are identical. Original stack layout, the optional
open flag `0x80`, the untested return from seek, and the **unchecked read
failure** in the two-byte reader are preserved. An explicit original-stack
slot accessor prevents a fabricated zero/`0xffff` fallback for the two
readers; the actual guest stack bytes are a future runtime adapter input.
The element readers/writers truncate `size * count` to a 16-bit byte count,
then divide the returned byte count by the original size; a zero size calls
an explicit guest DIV exception hook. The pinned RTL/linked RAM test checks
all thirteen boundaries and FAR targets,
and isolated C tests check call order, arguments and return behavior. The
underlying `1c29` runtime and `12a5` formatter are still external—not an
independently working DOS file adapter.

`gate_rtl_image_finish.c` now recovers the next bounded cluster, from the
original `GATE.RTL` offsets `0x120f3`, `0x12131`, `0x121b5`, `0x12205`
and `0x12383`. These are the format-tag selector, optional 16/48-byte
transform table setup, signed coordinate transformer, post-load descriptor
adjuster and in-place four-row image compactor. They retain the original
signed division/remainder, 16-bit `-32768` overflow behavior, forward FAR
copies without segment carry, conditional message call and work-record
field writes. Isolated tests cover both format branches, palette table
sources, coordinate extremes, row skips, far-offset wrap and the no-op
condition. The original `0cdc:0430`, `0cdc:0396` and `1258:172e` FAR
services are still externally declared, not fabricated C implementations.

`gate_rtl_image_prepare.c` adds the bounded original video preparer
`GATE.RTL:0x1266f..0x12771`. For nonzero video modes, it clears the status
word, conditionally uploads the pending palette and copies its 768 bytes,
zeros the eight-byte staging area, copies the original words and two flag
bytes, then clears those source flags and derives a new status. The isolated
C test covers the early return, optional palette path, both flag outcomes
and original ordered memory effects.

**Important relocation correction:** raw `GATE.RTL` disassembly shows
zero-valued displacement placeholders for global references. They are
**not** accesses to offset zero. The captured *linked* guest RAM shows,
for example, the mode at `ES:0fce`, the resource header at `ES:005c`,
file handle at `ES:005a`, image-table data at `DS:1480/1486`, and scalar
selector globals relocated from `DS:0000..0010` to `DS:1416..1426`.
The C resource selector, image loader and new image post-load routines use
those linked offsets; service arguments also preserve the original FAR
destination pointers. The pinned-original binary tests check selected
patched operands and linked FAR call targets against private guest RAM.
Segment words and the remaining hardware/file services still require
independent runtime backing.

`gate_rtl_image_processor.c` recovers five more bounded original image
functions: `rtl:0x12ed7` (active-record draw wrapper), `0x12f2b`
(draw an array of 136-byte records), `0x13115` (draw frames with the original
two timing bytes), `0x13191` (initialize an array's active flags) and
`0x131f7` (release each array member). The draw wrapper sign-extends the
one-byte draw mode, applies the two relocated 16-bit origins, and calls
the original three FAR services at linked `1e48:04d9`, `24a7:000c`,
`1e48:03c5`; the mode-13h path of `24a7:000c` is now implemented in the
native VFX host below, while the other display-mode handlers remain open.
The timed array's delay uses `50*byte6 + clock_low % (50*byte7+1)` without
carrying a FAR pointer into the next segment. Mock tests cover mode sign,
coordinate wrap, inactive records, signed array counts, clock remainder,
pointer rollover and null guards.

`gate_rtl_intro_animation.c` reconstructs the main intro frame controller
`rtl:0x12f6b..0x130aa` (original linked `0f10:1466`), now wired to its
`GATE.EXE` far-call alias. It fades out, resets original display state,
optionally draws the initial splash, and then loads consecutive resources
in mode 1. For each, it waits until the **signed-high/unsigned-low**
32-bit deadline or stops on the original input callback, prepares/draws,
cleans up, and schedules the next frame using the original timing bytes.
Host tests check the complete call ordering, first-image bypass, resource
failure, signed-clock overflow, input interruption and the resource-ID
increment across `0xffff`. The restored C remains an **archive of routines**,
not a standalone intro player.

`gate_rtl_intro_sequence.c` recovers both the 136-byte consecutive-record
loader `rtl:0x12d67..0x12dda` and the 32-frame stack-based animation entry
`rtl:0x130ab..0x13114` (the original `0f10:15a6` FAR call). The 4,352-byte
stack array, 32-record error diagnostic, original `rtl:0x13191` initializer,
mode-1 sequential resource loading, timed playback and cleanup are preserved.
The lower-level loader's unusual `count < 1` fallback to 256 records and
relocated `DS:1424` flag are deliberately retained; a zero/negative count
can exceed the animation entry's stack allocation, just as in the binary.
Original-byte tests verify all five near-call targets and the relocated
diagnostic service; C tests cover success, failure, count overflow and
resource-ID wrap. This closes another original EXE far-call alias, but does
not itself supply the graphics or host services.

`gate_dos_file_runtime.c` reconstructs the five original resident DOS file
thunks at absolute `1c29:0000`, `0028`, `0036`, `0055` and `0074`:
open/create, close, byte read, byte write and seek. The exact INT 21h
register ABI, carry-bit handling and ignored return words are preserved.
The companion `gateway_intro_file_host` archive provides a tested native
file backend for 8.3 assets under a caller-supplied DOS directory, with
FAR-offset wrap, file handles and 32-bit seek. Mock tests verify wrapper
semantics; native tests create/read/seek/delete a scratch DOS asset. The
host file adapter also covers the **two intro-used calls** of the `12a5`
runtime: read-permission checking and the verified `%s_%03d.%s` resource
filename template. This is explicitly a narrow host implementation, not a
claim to have decompiled the entire original `sprintf`/`access` runtime. The
saved RAM also reveals that `24a7:000c` **does contain executable VFX code**
at physical `0x24a7c`; the earlier unlinked RTL copy was a **different**
region, physical `0x34a7c`. The VFX decoder and other services remain open.

**None of these tests is a full guest-entry differential run.** Unhandled
`12a5` runtime variants, other DOS/RTL services, and complete startup
initialization still block a standalone source-C intro. The mode-13h VFX
renderer is now covered below, but the intro is not yet playable.

### Native linked-runtime and real picture playback path (bundles 088–090)

`gateway_intro_memory_host` now implements the original 20-bit guest-RAM
aliasing and relocated DS words for the source-C intro. C stack-local FAR
buffers use reserved synthetic segments `e000..efff`; these are a temporary
host bridge, **not** a claim of complete DOS high-memory emulation. The
image-resource globals at linked DS `4dce..4dea`, scalar selector words at
`DS:1416..1426`, renderer palette pointer, font FAR pointer (`DS:54ca`),
text cursor (`DS:550a/550c`) and text colors (`DS:54c6/54c8`) now read
real bound RAM, without embedding a particular DOS load segment.
The original graphics mode, status, mode-specific allocation sizes and
mutable palette bytes are likewise backed by the recovered RTL globals.
The separate `gateway_intro_input_host` archive lets a future SDL3 runner
inject a monotonic millisecond clock, key queue and mouse state. Its tests
use deterministic callbacks; it does not yet initialize SDL or play video.

The separately recovered `12a5:2bba` FAR `strlen` now implements its actual
`REPNE SCASB` scan bound and 16-bit offset behavior. The recovered
`0c4d:061b` text-color linker alias and `0c33` file-service aliases resolve
to source C; the latter are separated into a dedicated archive object to
support native resource linking without pulling the entire graphics bank.

The integration test links **original reconstructed C functions** for
`RTL:e4ff/e531/e5e3/e68b`, `RTL:11ecb`, `RTL:12c33`, `RTL:12205`,
`RTL:12ed7` and all five linked `0c33` file aliases to these native
adapters. The optional original-snapshot test reads the live relocated
filename table from a 22-second capture, opens `GATE_200.PIC`, selects
entry #1 of `GATE_300.PIC` (270×38 pixels, flags byte `0x43`, frame origin
`(24,81)`), allocates a `CA00` VFX image record, decodes its real DCL
pixel payload, and draws it into the 320×200 eight-bit A000 framebuffer.
It then releases the original image record. No original assets are in Git.

The `12c33` C translation also fixes a critical misplaced-coordinate bug:
the original binary reads relocated near DS words `1420/1422`, **not** the
DS `4dce/4dd0` FAR segment selectors. This had previously put every image
far outside the screen. The corrected operands are pinned to the original
linked RAM, and the picture selector's actual frame origin is checked.

Three distinct parts remain intentionally separate:

- `gate_vfx_image.c` reconstructs original resident `145d:00f7` allocation,
  `1479:0002` fill, and `145d:016c` release for both four-plane mode 2 and
  eight-bit mode 3. Plane-row stride is padded to even bytes, the original
  multi-plane fill pattern follows the bitplane index, and the pixel heap
  supports the original 64KiB segment advance for large images.
- `intro_host_vfx.c` reconstructs mode-2 `24a7:02fe` and mode-3 `24a7:04c4`:
  copy, XOR, AND, OR, clipping, stride, offsets, and (for mode 2) unaligned
  destination bits. The native host retains four independent VGA bitplanes
  at 640×480 rather than representing mode 12h as linear A000 RAM. Other
  original VFX handlers are not yet implemented.
- `intro_host_pic_decode.cpp` is a **host-level DCL codec replacement**
  for `0eec:018a`, using the existing `gateway::explode_dcl`. It consumes
  the already-positioned original DOS stream. Mode 2 converts the original
  low-nibble-first, four-bit pixels to interleaved, MSB-first bitplanes;
  mode 3 retains the existing byte-per-pixel conversion. This is *not* a
  decompilation of the DOS `1af5` codec. Its C++ requirement does not enter
  the pure-C reconstructed archive.

An additional early 1-second original guest-RAM capture verifies VGA mode
12h's descriptor (640×480, one bit per plane, four planes) and the resident
`24a7:02fe` original instruction bytes. A headless native integration test
now loads **real `GATE_201.PIC` #1 (300×30 pixels)** via the source-C RTL
selector, four-plane VFX constructor and DCL host bridge, draws it through
the original C wrapper, then compares **every displayed pixel** against
the independent `gateway::decode_pic` format decoder. The 22-second
`GATE_300.PIC` eight-bit original test still passes.

With CMake and GCC/G++:

```sh
cmake -S . -B build/intro-c-only -DGATEWAY_INTRO_DECOMPILE_ONLY=ON
cmake --build build/intro-c-only --target \
  launcher-intro-native-file-integration-tests launcher-intro-host-vfx-tests
build/intro-c-only/launcher-intro-native-file-integration-tests \
  build/intro-analysis/at-22000.bin ../gateway-dos
build/intro-c-only/launcher-intro-native-file-integration-tests \
  --mode2 build/intro-analysis/at-1000.bin ../gateway-dos
```

Without snapshot arguments the executable runs only its synthetic
scratch-directory file test. With original data, the image integration is
headless and uses the specific recorded original DS `2a7a`. The successful
two-byte read still uses a test-only initial stack WORD because a faithful
uninitialized 8086 stack model is unfinished. **The source-C entry point,
full intro sequence integration, SDL3 event loop, text drawing and several
hardware services are still missing; this is not yet a playable source-C
intro.**

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
There is no source-C executable entry point or complete native host adapter yet.
`nix run .#intro` still starts the independent C++ presentation prototype,
not this decompilation. A successful CMake/Nix build only proves the archive
and tests can compile; it does *not* demonstrate that the C intro plays.

Mode-12h and mode-13h image allocation, DCL decoding and drawing are linked
through the original C loader. Font rasterization, solid text backgrounds,
palette uploads and original display-mode selection now have bounded native
hardware services. The remaining dependencies include actual window/video
presentation, the startup entry and keyboard/timing services. Use guest-entry
differential checks
before calling the complete intro playable or resuming game logic.

The graphics-font pixel path is now connected: the source-C `rtl:0xedff`
printer's FAR pixel call at `rtl:0xef83` resolves in captured RAM to the
**resident `25d7:0004` graphics dispatcher**. A separate native video host
implements its observed 640×480 planar and 320×200 linear pixel outputs,
including the original DS `29de` raster-operation byte and optional signed
clip rectangle. The linked `rtl:ec0a/ed39/edff` tab, line and text-pitch
words are read from the relocated mode-descriptor segment selectors at
`DS:4d82/4d86/4d8a/4d90`. Tests pass seven different characters through
the *actual source-C glyph routine* with the original 22-second font bitmap,
then verify all 64,000 video bytes against independently decoded font bits.
Synthetic tests exercise all four pixel operations and the planar mode.
The original opaque-background calls link to resident `25a3:0002` and
`2698:0006`; a bounded host implements their **style-2, solid-fill branch**,
including the inclusive background cell and restoration of graphics state.
Both synthetic planar and captured-font linear-framebuffer tests cover
opaque backgrounds followed by glyph ink. Pattern fills and alternate
rectangle styles are still intentionally unavailable.

The original `rtl:0xf61b` **palette-upload C** also runs against live-linked
globals instead of test mocks: graphics mode `DS:0fce`, VGA type `DS:5516`,
retrace port `DS:5514`, palette color-count table `DS:0fd2`, conversion
flag through `DS:4daa`, and relocated lookup bytes at `DS:132a/1334`.
A native hardware port boundary records the exact DAC RGB6 values written
to `3c8/3c9` and the VGA attribute-controller entries at `3c0`. It
provides an artificial clear/set retrace transition to complete the
original polling loops; this does **not** reproduce real VGA timing.
Unit tests exercise source-C modes 1/2/3, mode-zero early return, attribute
changes, real snapshot globals and all 256 mode-13h DAC colors. The
mode-switch C also configures the descriptor and pixel dispatcher through
the native BIOS adapter. The SDL presenter, real BIOS video effects,
startup entry and complete timing/input handling remain outstanding;
this is not playable.

The next three bounded **original RTL drawing routines** are now source C:
`GATE.RTL:fbf5..fc22` selects the graphics ink and returns the preceding
ink, clamping only values *signed-greater* than the mode descriptor's
maximum; `fc23..fc36` stores the fill pattern and its `pattern != 0`
transparency flag using the original `CMP/SBB/INC` (unsigned carry);
`fc37..fc80` selects normal versus style-bit-7 hatch setup, clamps the
signed top coordinate to -1, and passes its five arguments to the original
resident rectangle entry. They are linked to the actual EXE calls
`0cdc:0aac` and `0cdc:0aee` rather than new presentation-layer drawing.
Live 22-second RAM confirms the relocated DS fields `5512`, `4da8`,
`12de/12e0`, the ink writer `25a3:011e`, fill state `25a3:0002` and
rectangle `2698:0006`. The native setup adapter accepts and records all
original resident fill-pattern numbers 0..11, but **only solid pattern 0,
style 2, opaque rectangles can be painted**; patterned and bordered
rectangles are still separate unmodeled resident paths. In particular,
calling `fc37` with a high-bit hatch style is source-C translated but
not renderable by the current host.

The original resident `25d7` dispatch table also proves VGA modes 1 and 2
share the same `25d7:00d6` four-plane pixel callback. The native C video
host consequently supports the intro's **mode-1 640x350** clip and its
short-branch solid rectangle alongside mode 2 (640x480) and mode 3
(320x200). Strict C11 UBSan tests cover both original wrappers with
mocked FAR calls, native planar/linear rectangles and an original 22-second
snapshot, plus binary-dispatch provenance. This advances a source-C branch,
**not** complete intro playback; the original entry, mode-1 image blits,
font loader, caption frames and SDL event/presentation loop still need work.

The complete original **caption-frame painter** at `GATE.RTL:ff99..1029f`
is now source C (`gate_rtl_draw_caption_frame`), connected to original FAR call
`0cdc:0e50`. Its full frame-style flow preserves the low-byte bit-1
outline, bit-2 inset/bevel and bit-3 outset/shadow forks, including the
special graphics-mode-0 and graphics-mode-1 branches, exact 16-bit corner
arithmetic and final selected ink. The original 22-second linked bank
resolves 39 distinct FAR calls to `2621:000e` (move-to) and `2621:003e`
(line-to); their resident bodies write the pen at `DS:29de+12/+14` and
send lines to `2621:0079`. The four mode-descriptor color words at offsets
`4c`, `4e`, `50`, `52` are each read via their own relocated FAR segment
selector (`DS:4da0..4da6`), not a hardcoded 16-color palette.

The native host models the original resident rectangle's **style-1
outline** and straight horizontal/vertical line-to commands, preserving
pen position around the direct rectangle call. Both planar VGA mode 2
and linear mode 3 now execute source-C frame decoration on top of the
existing style-2 interior fill. Synthetic tests verify ordered source-C
move/line and color calls for styles 2/4/8 and the mode-0/mode-1 control
branches; native tests draw real-color bevel pixels, including the
captured original mode-13h descriptor's black/white/grey shading.
The resident diagonal-line, pen-width, patterned and text-mode raster
paths remain unimplemented. The font selector now works natively with the
original font, and all captions can draw through the original C chain.
Video startup, picture/animation presentation and SDL3 remain outstanding,
so full source-C intro playback is **not independently playable**.

## Experimental 16-bit DOS compiler probe

The supplied original `GATE.RTL` confirms `rtl:0x8298..0x82ae` is a FAR
`strlen` entry. The newly regenerated 22-second linked RAM capture verifies
that **all 23 bytes** occur at physical `0x1560a`, the address of runtime
`12a5:2bba` (EXE-relative `02a5:2bba`), exactly once in the 1 MiB image.
The source-C linked wrapper now delegates to `gate_rtl_string_length`; 16-bit FAR
offset wrapping and the missing-terminator scan bound remain tested.

With OpenWatcom v2's **16-bit** `wcl` on `PATH`, run
`sh tools/build_intro_dos_probe.sh` from the repository root. The intended
output is `build/intro-dos/INTROCHK.EXE`, a real-mode DOS diagnostic testing
32 steps of the recovered Microsoft C runtime random generator with a small
DS adapter. It does **not** launch Gateway or load any game assets. GCC
passes the equivalent test, but OpenWatcom is unavailable in this workspace,
so its DOS link and runtime are unverified.

The full source-C archive is not yet DOS-linkable: the `gate_far_*` calls
represent relocated original 16:16 services, while the guest-memory,
filesystem, input and video adapters still need real-mode implementations.
Compiling the source in OpenWatcom's small model alone cannot reproduce the
original Microsoft C ABI or segmented data layout.

The user has now compiled **all 28** reconstructed modules with the actual
Microsoft C 5.1 `/AL /Od /c` compiler through
`nix run -L .#msc51-compile-launcher`. The staged DOS 8.3 source names map
back to their original files via `build/msc51-launcher/SOURCE.MAP`, and all
31-character identifier aliases are documented in `SYMBOL.MAP`. The first
compiler run exposed unsupported C99 automatic aggregate initializers and
identifier collisions; the corrected staging passed the next actual run.
Original file offsets in source comments remain intact, not replaced by
shortened identifiers.

`nix run -L .#msc51-link-launcher` is a **separate diagnostic** for the original
Microsoft linker. It reuses the successful `.OBJ` files, writes a response
file and saves `LINK.LOG` for an unresolved-service inventory. The first
working Microsoft LINK 3.65 invocation on the user host listed 28 inputs and
reported **108 unresolved externals** (`L2029`); the original DOS program is
not yet linkable. Among these are the startup `_main`, the `gate_ds_*` and
`gate_far_*` segmented-memory boundary, `gate_dos_file_*` services and
original linked FAR calls. For example, `gate_linked_intro_mouse_flags` is
associated with observed linked `0e48:07fe`, while
`gate_linked_format_resource_filename` uses observed linked `12a5:3366`;
neither address is an original stable source-file offset. RTL state accessors
such as `gate_rtl_video_columns` retain their annotated **RTL DS:0x0004**
location, also distinct from original RTL file offsets.

`nix run .#msc51-link-report -- --details --json build/msc51-link-report.json`
summarizes an existing `LINK.LOG`, restores all shortened identifiers, shows
the referring original C modules and preserves provenance types:
`original_file_offset` (for `rtl:0x...` or `exe:0x...`) versus
`observed_linked_far` (only valid for the recorded DOS layout). This runs
without a compiler or DOSBox. No resident, memory, video, file or input
adapter is assumed to exist merely because object compilation succeeds, and
the current linker errors should not be silenced with unverified no-op
functions. See the top-level toolchain instructions.

The eight guest-memory symbols now have a separate **DOS-specific**
implementation in `dos/msc51_memory.c`, using Microsoft C 5.1 `FP_SEG`/
`FP_OFF` far pointers and an explicitly bound guest DS. The optional
`nix run -L .#msc51-compile-memory` builds `MEMORY.OBJ` and runs an actual
Microsoft C DOS `MEMCHK.EXE` before permitting the next link attempt.
The probe checks DS words, 8086 offset wrapping, and SS:offset access to a
stack variable. A recorded source fingerprint also prevents reusing an older
`MEMORY.OBJ` after an adapter change. The subsequent user's LINK run confirms
that these eight symbols are resolved. No original launcher DS setup, CRT
entry or resident services are claimed to exist yet.

**Confirmed follow-up:** the user's real Microsoft LINK invocation now
includes `MEMORY.OBJ` and reports 100 missing names, exactly eight fewer
than the original report. The next boundary is the five `gate_dos_file_*`
calls. `dos/msc51_dos_file.c` implements `INT 21h` create/open/close/read/
write/seek with Microsoft C 5.1 `intdosx`, retaining the carry flag alongside
the actual AX and DX registers. `nix run -L .#msc51-compile-dos-files` builds
`DOSFILE.OBJ` and runs a DOS `FILECHK.EXE` using a temporary file on the
writable staging mount. A fresh `FILOK.LOG` and source fingerprint are
required to include the adapter in the following `LINK.RSP`; this
historical-DOS validation is awaiting the user's next run.

The next 40 unresolved state accessors already have documented, tested
native-host implementations: 17 image/resource selectors and accessors, 12
text/cursor/mode properties, six palette properties, and five graphics
scratch properties. `msc51-compile-rtl-state` stages their **exact existing
native bodies** from `src/intro_host_rtl_*_globals.c`, adapting only DOS
C89 includes, compound pointer initialization and compiler symbol length.
`RTLLAY.H` derives its offsets from `gate_memory_layout.h`; `RTLIMAC.H`
derives four graphics constants from `gate_rtl_image.h`. Under the 16-bit
Microsoft compiler, the `RTLCHK.EXE` probe exercises actual allocated
segments, including changing live selectors and mode-dependent values.
Only after success does `LINK.RSP` include `RTLIMG.OBJ`, `RTLTEXT.OBJ`,
`RTLPAL.OBJ` and `RTLGR.OBJ`. The predicted unresolved inventory, if both
the pending DOS file adapter and these 40 functions link, is **55**:
25 still-unimplemented RTL hardware functions, 29 original linked FAR
services and the single C startup `_main`. Neither the 55 count nor the
DOS probe is yet verified by the historical toolchain; no dummy startup
or no-op hardware functions are being supplied.

**Verified by the user's Microsoft LINK 3.65 log:** All 28 original game
modules, `MEMORY.OBJ`, `DOSFILE.OBJ` and the four RTL state objects were
accepted and exactly 55 symbols remain. The five file and 40 RTL state
names have disappeared from the error list, as predicted.

The next independent DOS adapter is `dos/msc51_hardware.c`. Its four
remaining functions use historical Microsoft C's actual VGA byte I/O
(`inp`/`outp`) and faithfully write the clip rectangle and enable word at
the linked DS offsets verified for resident 25b7. The probe leaves VGA
output untouched, reads status port 0x03da, and checks the segment memory
effects of clipping and the special case `(enabled == 1)`. This module is
not a substitute for the missing resident video dispatcher, palette
upload, sound, input, overlays, or startup; if the user confirms it with
`nix run -L .#msc51-compile-hardware`, the expected linker remainder is 51.

`msc51-compile-runtime-files` is a further independent DOS adapter.
`dos/msc51_runtime_files.c` implements three original linked Microsoft C
runtime boundaries: FAR `strncpy` (including NUL padding and destination
pointer return), `access(path,4)` for DOS read permission, and the
`%s_%03d.%s` resource filename formatter. The adapters use original
segment:offset addresses through the verified `MEMORY.OBJ`; the pathname
buffer is bounded by the recovered 80-byte caller stack allocation.
`RTFCHK.EXE` verifies signed resource naming, NUL padding, bounded copying,
and both present/missing files on the writable DOS drive. LINK requires a
fresh `RTFOK.LOG` and source fingerprint before using `RTFILE.OBJ`.
After hardware and this stage both pass the user's real compiler and LINK,
**48** unresolved symbols are predicted: 26 original linked services,
21 RTL state/hardware functions and the CRT `_main`. Neither the resulting
symbol count nor a runnable original launcher is yet verified.

`msc51-compile-resident-state` builds an independent `RESSTATE.OBJ`
containing six resident entry points anchored to original 2409:0008/003e,
25a3:0002/011e, and 2621:000e. It retains the live linked-DS coordinates
for pen color, pen position and fill arguments, the descriptor selection
limit of seven, and the original enabled-mode transition using BIOS INT 10h.
The clear service also copies the recovered 2488:000a descriptor guards,
text B800 and linear A000 fills, and sets the sequencer plane mask to fill
individual A000 VGA planes. The probe exercises guard paths without
damaging the current DOSBox picture; the actual VGA clear effects and
restored GC/SEQ register state still require visual/hardware verification.
The DOS probe checks the DS words and selects its already-current BIOS
mode rather than disturbing the console. Passing `RESCHK.EXE`, fresh
`RESOK.LOG`, and a matching fingerprint are required before LINK consumes
the new object. The expected remainder becomes **42** if all previous
pending objects link. There is still no original program initialization.

`msc51-compile-heap` compiles `HEAP.OBJ`, implementing real DOS allocation
and release for original font heap (`1e2c:0008/0138`) and VFX pixel heap
(`253a:0004/0048`). It maps byte counts to DOS memory paragraphs,
including buffers larger than 64KiB, and rejects invalid null releases.
`HEAPCHK.EXE` verifies word roundtrips, access beyond the first segment,
normal release and null rejection; fresh `HEAPOK.LOG` and a source
fingerprint guard the LINK input. The original heap's private free-list
has not been reconstructed; the adapter uses DOS allocation explicitly.
The predicted missing names become **37** if all stages pass on DOSBox.

`msc51-compile-font` separately verifies the original source-C cleanup at
`GATE.RTL:e771..e800`, the fourth control-key teardown call. It links the
actual staged font module `M13.OBJ` against the previously tested real-DOS
heap/memory adapters, with fail-closed probe-only definitions for unrelated
font-selection services. `FONTCHK.EXE` verifies three allocated FAR buffers
are freed, an unused slot is preserved, resource bytes are invalidated on
occupied slots, and the active font pointer changes even for empty slots.
`FNTTEST.LOG` and the staged-source signature guard the result. This
establishes no missing outer Ctrl-Break handler or DOS executable entry.

`msc51-compile-stream` exercises the first Ctrl-Break FAR callee at
`GATE.RTL:15706..15729` directly from the staged `M11.OBJ` with genuine
Microsoft C 5.1 and DOSBox. The descriptor pointer uses relocated
`DS:6150/6152`, separate from the signed state at `DS:156e/1570`.
The nested original CRT stream-close routine at `RTL:6640` has two
no-I/O exits: `(flags & 83h) == 0` or `(flags & 40h) != 0`. Those clear
descriptor byte `+0ah` and invalidate the open-state word; an already
negative word skips everything. The native code aborts on active streams
requiring the unrecovered flush/close sequence. The DOS probe links the
real `M11.OBJ`, DOS file and runtime adapters, with a source signature
and `STRTEST.LOG` PASS witness. It does not prove active file closure.

`msc51-compile-input` adds a real DOS BIOS keyboard/mouse adapter for
linked `0df2:000a/0030` and `0e48:011b/07fe`. It checks original input
state at linked DS:133e/1340, queries INT 16h/AH=01h through Microsoft C's
`_bios_keybrd(_KEYBRD_READY)` without consuming the keyboard queue,
reads through `_bios_keybrd(_KEYBRD_READ)` when needed, and calls INT 33h
for mouse reset/buttons. Its DOS probe checks the original unget slot and
non-destructive BIOS status polling. The special Ctrl-Break flushing path
has not been independently validated and is specifically not claimed as
bit-identical. `INP.OBJ` is LINK-eligible only after the source-fingerprinted
DOS test. The same object now also handles the animation input poll through
the recovered inverse-of-key-pending convention. A **32**-symbol remainder
is predicted, not yet verified.

The user's genuine 5.1 compiler rejected `union REGS.x.flags` with C2038.
That member was present only in the host-side syntax fixture, not in the
historical header. The corrected BIOS library calls implement the same
read/peek services without accessing FLAGS through the register structure.
Re-run `msc51-compile-input`; the earlier failed probe does not qualify
`INP.OBJ` for the final launcher link.

`msc51-compile-clock` stages `CLOCK.OBJ` to resolve the two EXE/RTL
elapsed-millisecond call sites and two delay functions. A genuine DOS
INT 21h/AH=2Ch read supplies hundredths-of-a-second wall time with a shared
first-read baseline; the probe checks both clock users and a real 20-ms
wait. Source-fingerprinted `CLKOK.LOG` is required for LINK eligibility.
This is **not** an original C runtime sampler: the recovered 12a5:38c4
sample/baseline algorithm is more complex, and runtime execution, midnight,
DST and animation-side-effect behavior still require differential testing.
With all pending objects built and accepted, **28** unresolved symbols are
predicted at the end of the clock stage. The historically confirmed count
remains 55.

`msc51-compile-crt` adds `CRTENV.OBJ` for the two linked original C runtime
environment and numeric-conversion functions. Unlike a host reimplementation,
the adapter calls Microsoft C's DOS `getenv` and `atol` through large-model
FAR pointers, copying only the bounded environment *name* from guest memory.
`CRTCHK.EXE` probes DOS PATH, missing-name behavior and signed numeric
conversion before producing a source-fingerprinted success marker.
Microsoft C 5.1 does **not** supply Turbo C's `MK_FP` helper: construct the
numeric input pointer with `FP_SEG(digits) = segment` and
`FP_OFF(digits) = offset` from `<dos.h>`; this resolves the user's observed
`LINK L2029: _MK_FP` compiler-probe failure. The corrected source still
requires a genuine DOSBox rerun.
The DOS heap adapter now additionally implements the startup allocator's
1000h-paragraph request with a real DOS segment, tested in `HEAPCHK.EXE`.
The user's Microsoft LINK 3.65 run confirmed **26** missing names with all
thirteen prior DOS objects in place (1 CRT entry, 10 original services, 15
RTL/hardware services). The extended `RTLHW.C` binds the seven video state
accessors and three-command BIOS text packet dispatcher; the extended
`RESSTATE.C` implements the three bounded raster primitives using DOS mode
13h A000 memory and INT 10h planar pixels. `RTLHW.C` now also supplies the
two resident display adapter probes using original INT 10h display-combination
codes, SVGA BIOS/ROM signatures, and restored video port tests. This reduces
the **predicted** remainder to **13**. Additionally, the original linked
`0cdc:0396` and `0cdc:0430` palette transformations map precisely to RTL
`f4df`/`f579`. Their 16-color indexed/RGB conversions are now implemented
in `RTLHW.C` with original relocated DS output offsets `0fd8` / `1008`,
signed byte quotient semantics and host round-trip tests, reducing the
**projected** remainder further to **11**. Both 5.1 compile/probes and the new link result are
still needed. See
[MSC51 link closure](../../docs/msc51-link-closure.md) for the inventory.

The same `RESSTATE.OBJ` now stages the bounded 24a7:000c VFX image draw
service. Like the separately tested native VFX adapter it supports original
CA00 4-plane mode-12h images and linear mode-13h 8-bit images, with
original clipping, row strides, 8086 source-offset wrap and COPY/AND/OR/XOR
operators. Real DOS planar output uses BIOS INT 10h pixel read/write in
place of resident register-optimized drawing. Host and DOS probe fixtures
check both modes; these are **not** proof of instruction-equivalent VFX
behavior. One further projected symbol is removed, leaving **10**.

The independent `msc51-compile-picture-dcl` stage now provides a bounded
streaming version of the image decompression service from the tested native
PKWARE DCL codec. `PICTDCL.OBJ` reads the original DOS resource handle,
restores its position after buffered read-ahead, and writes mode-12h/13h
picture data to the caller's extended FAR image buffer. A host C89 test
passes for a reference compressed stream in both modes; the DOS probe is
staged but must still run under Microsoft C 5.1. Its success would reduce
the **projected** unresolved total to **9**. This decoder does not claim to
reproduce the original DOS resident decompression routine instruction by
instruction.

The original RTL resource byte/word readers (`RTL:e59b/e5c5`) do not
initialize their two-byte BP-2 stack slot. The element-count wrappers
(`RTL:e5fc/e669`) execute unsigned `DIV` with their element-size parameter
and raise the CPU exception when that divisor is zero. The generated DOS
`M11.C` now enables `GATE_MSC51_ORIGINAL_FILE_ABI` to preserve those original
operations without external stack-word/exception callbacks, while the
source C host tests retain explicit seeded-stack and fault injection.
GCC C89 at `-O0` eliminates both references from the generated object; the
actual MSC 5.1 code and guest stack history still need checking. Predicted
remaining unresolved names fall **9 to 7**, pending real Microsoft LINK.

## Immediate next functions

1. Connect the existing verified native video, font, picture and caption
   services to the source-C `gate_exe_play_intro` startup and SDL3 event/render
   loop, preserving the original sequence and its interruptibility.
2. Recover the remaining patterned/background and text-mode video
   dependencies (`rtl:0xed74/0xeda5/0xedb7`), keyboard, mouse and timing
   services on the intro's actual call graph. The far call at `rtl:0xef83`
   now has a bounded native VGA host but its resident source is not yet C.
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
