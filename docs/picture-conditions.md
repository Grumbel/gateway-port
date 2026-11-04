<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception -->

# Conditional picture layers in original Gateway 1

This is a separate path from [conditional mouse hitboxes](room-your-quarters.md#native-conditional-mouse-region-rules).
An RGN rectangle's disabled bit says whether the mouse picker considers that
rectangle. A picture-layer condition instead controls visual updates. Both
paths use room-specific tables and native object predicates, but their record
formats and the meaning of callback returns differ.

## Stable source routines and record format

Original `GATEMAIN.OVL` section 0 includes:

| Original file offset | Responsibility | Native opening replay |
| --- | --- | --- |
| `ovl:0x00272c` | Evaluate one 14-byte conditional picture record | called by update routine |
| `ovl:0x002827` | Update cached picture-layer state, adding and removing images as predicates change | RTLink `2c24:425b` → `4319:2197` |
| `ovl:0x002680` | Separate 10-byte conditional *mouse-region* evaluator | RTLink `2c24:4251` → `4319:1ff0` |

The runtime addresses describe only the pinned opening replay. Original file
offsets are stable for the examined binaries.

Each picture condition is **14 bytes**, seven little-endian words:

| Word offset | Field | Observed use in `ovl:0x272c` |
| ---: | --- | --- |
| `+0` | `object_id` | Primary object ID (zero is allowed for an unconditional layer) |
| `+2` | `relation_id` | If nonzero, require an immediate relation between primary object and this ID |
| `+4` | `picture_id` | Image to show/remove when the condition changes |
| `+6` | `property` | If nonzero and not `0xff`, require this property on the primary object |
| `+8` | `extra_id` | Additional relation operand in the special `property == 0xff` case |
| `+10` | `callback_offset` | Optional far callback; a zero result rejects the layer |
| `+12` | `callback_segment` | Second word of the callback pointer |

The evaluator checks a nonzero callback first, then object-related predicates.
The special `property == 0xff` path additionally tests relationships involving
`extra_id`; its full game-level semantics need more scenario coverage.
The update routine maintains a byte-per-entry cache at **`DS:adf8`**. A
condition changing from false to true adds its picture; true to false removes
it. For the original image class used here, the host-extracted PIC frames are
**positioned XOR overlays**. Reversing the XOR removes the graphic without
requiring the underlying room to be repainted.

The update routine uses guest graphics services to apply each layer; this
analysis does not replace those services or claim every picture format shares
the same compositing method.

## Your Quarters: corroborated table and pixels

The room's event branch at `ovl:0x0514db` passes three records to the layer
update routine. Its table begins at **`ovl:0x053d80` (section 38)**, adjacent
to—but separate from—the conditional mouse-region table at `ovl:0x053daa`.

| Source | Object | Picture | Condition |
| --- | --- | --- | --- |
| `ovl:0x053d80` | `0x124` drawer | `0x0210` | Property `0x0c` (drawer open) |
| `ovl:0x053d8e` | `0x126` debit card | `0x0212` | Immediate relation to desk `0x125` |
| `ovl:0x053d9c` | `0x131` | `0x0211` | Callback at `ovl:0x051480` (section 38) |

The picture number `0x0210` resolves to **`GATE_102.PIC` entry 16**. The
original PIC directory marks it as a positioned XOR overlay (`flags=0x4201`)
at room coordinates **(90, 159)** with dimensions **48×65**. The native room
pixels start at screen **x=261** (the preceding column is a UI divider), so
the layer occupies screen x=351–398 and
y=159–223. Entries 17 and 18 are similarly positioned overlays for the other
two records.

Four previously captured guest RAM checkpoints and native 640×480 framebuffers
independently agree:

| Checkpoint | `DS:adf8[0..2]` | Drawer visual |
| --- | --- | --- |
| `before` | `0, 1, 0` | Closed |
| `open` | `1, 1, 0` | Open |
| `taken` | `1, 1, 0` | Open, after taking book |
| `closed` | `0, 1, 0` | Closed again |

Within the drawer's original-frame region, **2,751 pixels change on opening**.
The `open` and `taken` images are identical there, as are the `before` and
`closed` images. This ties the conditional table, the native cache, the
external PIC asset, and the guest framebuffer change to the same drawer event.
Outside that crop, input and text updates also affect the framebuffer; a
whole-screen diff cannot be read as a paint-condition signal.

## Complete call-site census

All **81 direct calls** in the original overlay to the picture update service
(`1c24:425b`) have a verified static argument form:

- 60 calls reference overlay-resident tables: **49 unique tables, 223 records**.
- 21 calls reference one 14-byte record each in the resident EXE DS image.

That gives **244 distinct source records**. A zero primary object ID is valid
and represents a layer that does not require an object-presence gate.
The 21 resident records use the pinned EXE data mapping
`exe_file_offset = 0x1ba93 + ds_offset`; they were checked against the same
four original RAM checkpoints. Nineteen records match source bytes exactly,
and two differ only at the linked callback segment (`1c24` in the file,
`2c24` after runtime relocation). The inspector enforces the original EXE
SHA-256 before using this fixed mapping.

Run the read-only census (append `--details` for call sites):

~~~sh
gateway-overlay ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL |
  python3 tools/inspect_picture_conditions.py ../gateway-dos/GATEMAIN.OVL \
    --manifest - --scan-calls --exe ../gateway-dos/GATEMAIN.EXE
~~~

Inspect the actual room table and optionally correlate the original cache:

~~~sh
gateway-overlay ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL |
  python3 tools/inspect_picture_conditions.py ../gateway-dos/GATEMAIN.OVL \
    --manifest - --section 38 --image-offset 0x2900 --count 3 \
    --snapshot-prefix build/rgn-scenes-v026/open
~~~

The `.bin`, `.debug.txt`, and `.ppm` checkpoints are private, optional test
inputs. The Python tests synthesize the table and scanning formats when no
original game data is installed.

## Relevance to a native SDL3 GUI

This gives us an actual original picture-state mechanism, rather than
guessing visibility from RGN rectangles. A future native renderer can keep
the room background, conditional picture layers, and current mouse targets
as separate components of a scene snapshot. Both original data tables are
recoverable; the remaining work is to reproduce or delegate their predicates
and compositing semantics without making story logic dependent on UI layout.
For AI-upscaled art, the original XOR layer must be resolved against its
correct background or converted to an aligned replacement graphic; simply
applying the low-resolution XOR bytes to an upscaled image would be wrong.

## First native picture compositor and exact-frame validation

`src/scene_compositor.cpp` is a pure C++20 **host-side pixel compositor** using
the existing PIC decoder, with no SDL3 or OpenGL dependency. It accepts a
background entry and an explicit ordered list of enabled XOR-layer entries,
preserves the base palette, and validates each layer's position and dimensions.
`gateway-scene` is a small CLI that exports the result to PNG. Neither
component evaluates original story predicates; that boundary stays separate.

Reproduce the *Your Quarters* pictures from the original asset file:

~~~sh
gateway-scene --base 15 --layer 18 --layer 17 \
  -o build/quarters-closed.png ../gateway-dos/GATE_102.PIC
gateway-scene --base 15 --layer 18 --layer 16 \
  -o build/quarters-open.png ../gateway-dos/GATE_102.PIC
~~~

The base is `GATE_102.PIC` entry 15. Entry 18 is the debit-card graphic;
entry 16 is the open drawer. Entry 17 affects just 50 pixels in an upper-left
display/indicator area and is associated with a separate blinking visual
state, not adequately explained by the retained `DS:adf8[2]` byte. In the
original guest checkpoints, the bitmap and that cache byte do not always
agree. **The cache is not yet a complete paint-list oracle**; reconstructing
this indicator's update timing is a remaining task.

With the room image aligned to native screen x=261, the first composition
(entries 18+17) is **pixel-identical** to the original saved `before.ppm` room
viewport, and the second (18+16) is **pixel-identical** to `open.ppm`:

| Original guest frame | Compositor layers | Differing RGB pixels (379×224 room) |
| --- | --- | ---: |
| `before.ppm` | 18, 17 | **0 / 84,896** |
| `open.ppm` | 18, 16 | **0 / 84,896** |

This is stronger validation than a visual approximation: the output matches
the original RGB framebuffer exactly across both complete room views.
`scene-compositor-tests` exercises synthetic reversible XOR composition; when
the private original PIC and PPM fixtures are supplied as arguments, it checks
the original native scenes byte-for-byte. None of these assets are committed.

## Read-only visible-layer inference and indicator callback

The diagnostic can now enumerate up to ten candidate overlays, comparing each
composition to the RGB room viewport of a captured P6 framebuffer. It reports
the number of differing pixels and the number of equally good combinations;
an exact, **unique** pixel match identifies the visible layers in that frame.
This is observation of rendered pixels, not reconstruction of the predicate
engine, and the caller must supply the original room image and alignment.

For the four private native checkpoints, run:

~~~sh
for name in before open taken closed; do
  gateway-scene --base 15 --reference-ppm "build/rgn-scenes-v026/$name.ppm" \
    --screen-x 261 --optional-layer 16 --optional-layer 17 \
    --optional-layer 18 ../gateway-dos/GATE_102.PIC
done
~~~

| Frame | Guest time | Unique visible optional layers | Differing pixels |
| --- | ---: | --- | ---: |
| `before` | 16,500 ms | 17 (indicator), 18 (card) | 0 / 84,896 |
| `open` | 20,500 ms | 16 (drawer), 18 (card) | 0 / 84,896 |
| `taken` | 26,500 ms | 16 (drawer), 17 (indicator), 18 (card) | 0 / 84,896 |
| `closed` | 35,000 ms | 17 (indicator), 18 (card) | 0 / 84,896 |

The original third picture record's callback at **`ovl:0x051480`** is just
`return byte(DS:0x69fe) == 1` (compare, Boolean normalization, far return).
The room's event-13 branch at `ovl:0x05156d` updates that byte based on
`DS:69fc` and a nonzero guest state byte addressed through `DS:9470`.
All four snapshots have `DS:69fe=1` but `DS:adf8[2]=0`, despite showing
indicator 17 in three of those frames. This verifies that neither the
callback nor the observed cache byte alone specifies **the currently painted
indicator**. The four timestamps do not establish a blink period.

The missing *independent picture path* is present in the original room
handler, not in the 14-byte picture-condition table:

| Original source | Observed operation |
| --- | --- |
| `ovl:0x0514fa` (room event 23) | Updates the three mouse-region rules; tests `DS:69fe` and whether signed `DS:6a20` is negative |
| `ovl:0x05152c`–`0x05153d` | If eligible and no handle exists, passes four words `(picture=0x0211, 1, 0, 0)` to the far service at `1485:1788` |
| `ovl:0x051540`–`0x051543` | Saves the service's `AX` return into `DS:6a20`, then calls another zero-argument graphics service at `1485:18da` |
| `ovl:0x051550`–`0x05156a` | When not eligible and a nonnegative handle exists, invokes *global* clear and release services at `1485:1928` and `1485:1bac` (neither takes the handle), then sets `DS:6a20=0xffff` |

All four captured snapshots have a nonnegative handle (`DS:6a20=0`),
and the pixels alternate while this handle stays unchanged. That is direct
evidence for a persistent native picture/animation path **separate from the
conditional-layer updater**.

### Original resident animation scheduler identified

The room's far CALL opcodes at `ovl:0x051538`, `0x051543`, `0x05155a`,
and `0x05155f` reference four entries in the same linked resident segment,
`1485`. Their runtime addresses are loaded with a `0x1000` segment shift.
The original executable/RTL bytes and the saved guest memory identify one
consistent file origin: `RTL file offset = linked offset + 0x11b05`.
All four matches have a fully intact 16-byte instruction window supported
by the four entries, even though some other instruction words are relocated.

| Room CALL | Original RTL entry | Observed 8086 behavior |
| --- | --- | --- |
| `1485:1788` | `rtl:0x1328d` | Creates the sequence, allocates frame records, returns a slot index in `AX`; returns `0xffff` on failure |
| `1485:18da` | `rtl:0x133df` | Initializes active sequences' current frame indices and 32-bit next-due times |
| `1485:1928` | `rtl:0x1342d` | Clears displayed sequence records and zeroes their accumulated timers |
| `1485:1bac` | `rtl:0x136b1` | Releases all active animation allocations and sets the shared sequence count to **zero** |

The creation routine accepts up to **five simultaneously active sequences**
(`DS:[0x12]` in the *original unrelocated instruction*), and up to **20
frames per sequence** (`[BP+8] <= 20`).
It allocates `0x0aa0` bytes, exactly 20 frame records of **`0x88` bytes**.
Other original operands include movement/direction bytes at `DS:[0x32+i]`,
frame indices at `DS:[0x40+i]`, and 32-bit deadlines at `DS:[0x46+4*i]`.
The original RTL file is **not a relocated memory image**; these immediate
offsets must not be used to read the native guest DS directly.

The scheduler is a separate near routine at **`rtl:0x134a5`**. It advances
frame indices (with signed direction), handles end-of-sequence looping or
termination based on a mode argument, and computes subsequent 32-bit
deadlines using frame-data bytes at offsets `+6`/`+7` scaled by `0x32`.
The wrapper at **`rtl:0x133b5`** calls the initialization routine, polls
the scheduler, then invokes the clear routine. Global release in the room
handler is not a hypothetical handle-specific API.

### Live data relocation and timing recovered

A fresh complete 39,000-guest-ms headless replay verified all seven opening
actions and 76 BIOS keys while recording the original RTL entries. The
creation entry (`physical 0x25fd8`) was reached at **9,077 ms**, with
`CS=2485`, `DS=5b46`, and stack words `(0x0211, 1, 0, 0)`.
The initializer at `physical 0x2612a` was reached afterward at 9,101 ms;
the scheduler was observed at `physical 0x261f0`. These addresses are
specific to that launch layout, unlike the RTL file offsets above.

The runtime code at these entries has **patched DS displacements**. Matching
the instruction opcodes and reading the relocated operands in guest memory
reveals two different relocation bases, not a single constant adjustment:

| Field, first slot | Original RTL operand | Live `DS=5b46` operand |
| --- | ---: | ---: |
| Active sequence count | `0x0012` | `0x1428` |
| Frame count | `0x0000` | `0x9fb4` |
| Direction | `0x0032` | `0x9fe6` |
| Frame index | `0x0040` | `0x9ff4` |
| Next deadline (32-bit) | `0x0046` | `0x9ffa` |
| Frame-buffer far pointer | `0x001e` | `0x9fd2` |

All four retained RAM snapshots now decode consistently with the *live*
operands:

| Snapshot | Count | Frames | Direction | Index | Deadline |
| --- | ---: | ---: | ---: | ---: | ---: |
| `before` (16,500 ms) | 1 | 1 | 0 | 0 | `0x30c0` |
| `open` (20,500 ms) | 1 | 1 | 0 | 0 | `0x3fe8` |
| `taken` (26,500 ms) | 1 | 1 | 0 | 0 | `0x56fe` |
| `closed` (35,000 ms) | 1 | 1 | 0 | 0 | `0x78aa` |

The only frame buffer is at `81d2:0000` (linear `0x81d20`), an unchanged
136-byte record in all four dumps. Its duration bytes at record offsets
`+6`/`+7` are `(10, 0)`, giving the static scheduler's `10 * 0x32 = 500`
time increment. With narrow byte-write watches on the relocated deadline
at physical `0x6545a`–`0x6545d`, a replay recorded **59 scheduler deadline
writes** from physical `0x2629f`. Of the 58 successive deadline increments,
**52 were exactly 500**; the other six have different timing gaps, possibly
from initialization or other clock behavior. Most 500-step events were
observed around 499–501 guest ms apart.

The blink itself is now independently verified with **eight additional
original RGB framebuffer dumps**, sampled 250 guest ms apart before the
first parser command. The standard replay still sends its bootstrap space
keys at 16,000 and 17,000 ms; no room command is entered in this interval.
Bounded XOR inference with `gateway-scene` found
exactly one matching layer combination for every frame, with **zero of
84,896 scene pixels differing**:

| Guest ms | Optional layers | Indicator 17 |
| ---: | --- | --- |
| 16,000 | 18 | Off |
| 16,250 | 18 | Off |
| 16,500 | 17, 18 | On |
| 16,750 | 17, 18 | On |
| 17,000 | 18 | Off |
| 17,250 | 18 | Off |
| 17,500 | 17, 18 | On |
| 17,750 | 17, 18 | On |

The complete 640×480 RGB framebuffers confirm each pair is *bit-identical*
and exactly the **same 50 pixels** change at all three on/off transitions;
all other 307,150 pixels are unchanged. The images repeat after 1,000 ms
(16,000 equals 17,000; 16,500 equals 17,500). Thus the indicator has a
**500 ms XOR toggle and 1,000 ms full blink cycle in the idle first-room
state**. This is much stronger than inferring a period from the original four
sparsely timed screenshots. It still leaves open how other room actions
interleave the animation and the conditional-layer updater.

The read-only mapping is reproducible from original files and a retained
guest RAM dump (which is not redistributed):

~~~sh
python3 tools/inspect_rtl_animation.py \
  --ovl ../gateway-dos/GATEMAIN.OVL \
  --exe ../gateway-dos/GATE.EXE \
  --rtl ../gateway-dos/GATE.RTL \
  --ram build/rgn-scenes-v026/before.bin \
  --ds 5b46
~~~

With a retained probe research trace whose `--trace-byte-write` watches
cover the six addresses above, add
`--write-trace build/rtl-timing-v033/native.trace` to obtain the deadline
interval histogram. For an independent framebuffer check, create an opening
replay with eight `--dump-at` arguments at 16000, 16250, 16500, 16750,
17000, 17250, 17500 and 17750 guest ms, before the first parser command at
18,000 ms; run `gateway-scene --base 15 --optional-layer 16
--optional-layer 17 --optional-layer 18 --reference-ppm FRAME.ppm
--screen-x 261 ../gateway-dos/GATE_102.PIC` on each frame. The
inspector checks original versus live opcode
prefixes before using any patched displacements. Synthetic tests reject
stale instruction matches and combine byte writes correctly; optional
original tests assert all relocated offsets, four slot states, the
500-step cadence and the eight-frame blink. This is a source-identity and timing milestone, not a
verified host-side animation port.

The native test now checks all four entire room views, and the original
callback bytes, animation-call operands, handle and guest state are checked
separately. No original asset or runtime snapshot is committed.

### Animation persists across picture-condition updates

A further complete native opening replay (seven actions, 76 BIOS keys,
39,000 guest ms) captured **32 additional framebuffer and conventional-RAM
pairs** over the drawer, book, and close commands. Every sample is reproduced
by a **unique** combination of original PIC overlays 16, 17 and 18, with
**zero differing pixels out of 84,896**. The scene-compositor inference and
the live animation fields are independently checked by
`tests/rtl_animation_tests.py` when the private samples exist.

| Sample interval (guest ms) | Drawer overlay 16 | Indicator overlay 17 | Card overlay 18 | Original scene mismatches |
| --- | --- | --- | --- | ---: |
| 19,500–23,000 (13 captures) | Always on | Alternates on/off | Always on | 0 in all |
| 25,000–32,000 (12 captures) | Always on | Alternates on/off | Always on | 0 in all |
| 34,500–36,000 (7 captures) | Always off | Alternates on/off | Always on | 0 in all |

At every sample, the linker-relocated RTL active-sequence count at
`DS=5b46:1428` remains **one**, the Your Quarters animation handle at
`DS:6a20` remains **zero**, and the picture eligibility byte at
`DS:69fe` remains **one**. In contrast, the first cached picture-state byte
at `DS:adf8` changes from **one** while the drawer is open to **zero**
after closing it. The blink persists in both states. This establishes that
the resident animation's active slot and the room's conditional picture cache
are distinct in this playthrough; it does *not* establish their behavior when
the player leaves the room or stops the animation explicitly. All DS offsets
are tied to the captured launch layout.

Reading the original RTL scheduler disassembly reveals a near call at
`rtl:0x1356d` into the frame renderer at **`rtl:0x12ed7`**. The renderer
checks frame-record byte zero, uses the mode byte at `+1`, passes the frame
payload from `+8` and a position derived from `+2`/`+4`, then makes three
linker-relocated far calls. `--draw-calls` in the RTL inspector validates
the near-call instruction and reads the real far targets from retained
native RAM (the original GATE.RTL far CALL operands are zero):

| Original call site | Function in the renderer | Original callee | Observed relocated target |
| --- | --- | --- | --- |
| `rtl:0x12ee6` | Save video state | `rtl:0x11078` | `23bd:04d9` |
| `rtl:0x12f17` | Draw frame payload | `rtl:0x184e8` | `2a1c:000c` |
| `rtl:0x12f1f` | Restore video state | `rtl:0x10f64` | `23bd:03c5` |

The original callees were resolved independently by matching their *live
instruction bytes* against the original EXE and RTL images: all three
matched 16 bytes, and the two video-state callees each had two entrypoints
supporting their common resident segment base. This identifies source
offsets without assuming that linker-patched far-call operands are present
in the original file.

The current 136-byte frame's first eight bytes decode as enabled=1,
mode=3, x=28, y=1, duration=10, auxiliary duration=0. **Mode 3 is XOR.**
The original draw routine `rtl:0x184e8` dispatches through a video-backend
table at `rtl:0x185eb`, with at least two independently verified paths:

- The packed-pixel branch at `rtl:0x18722` checks for mode 3 and XORs the
  destination words at `rtl:0x18736` and `rtl:0x18745`.
- The VGA branch at `rtl:0x187da` writes the mode shifted left by three
  to VGA Graphics Controller register 3 (ports `0x3ce`/`0x3cf`), at
  `rtl:0x1885b`–`0x18868`. With mode 3 this writes **`0x18`**: bits 3–4
  of register 3 select function `11`, the XOR operation, as documented in
  [IBM's VGA/XGA Technical Reference (May 1992, pp. 2-82)](https://bitsavers.trailing-edge.com/pdf/ibm/pc/cards/IBM_VGA_XGA_Technical_Reference_Manual_May92.pdf).

An independent 13,000-guest-ms native replay watched the *actual* live
entries for both backend branches. The original-mode-3 VGA path
(`rtl:0x187da`, runtime physical `0x2a4be`) was entered **17 times**;
the packed path (`rtl:0x18722`, runtime physical `0x2a406`) was not
entered. The entries at 10,200, 10,701, 11,201, 11,700, 12,200 and
12,701 guest ms show recurring calls during the first-room animation.
Together with the exact scene XOR differences, this confirms the **native
VGA XOR rendering path**, not merely an inferred reversible host overlay.
Original-file offsets remain stable; all runtime physical addresses here
are specific to the recorded launch layout.

For a repeatable private capture, run the native `gateway-opening.walk`
replay with `--max-actions 7` and `--probe-arg=--dump-at` plus
`--probe-arg=MS:PREFIX` for the 32 times listed in the optional test.
Then resolve its renderer targets with:

~~~sh
python3 tools/inspect_rtl_animation.py \
  --ovl ../gateway-dos/GATEMAIN.OVL \
  --exe ../gateway-dos/GATE.EXE \
  --rtl ../gateway-dos/GATE.RTL \
  --ram build/rtl-actions-v034/t20500.bin --ds 5b46 \
  --draw-trace build/mode3-v035/native.trace
~~~

The bounded branch trace was collected through the headless probe with
`--trace-entry 2a1cc --trace-entry 2a406 --trace-entry 2a4be`; these
runtime addresses must be recomputed for any different load layout.
Synthetic and optional-original tests reject changed original mode-3
instructions, resolve far-call source identities, and verify the observed
VGA-only branch distribution. A host-side animation replacement and its
equivalence test remain future steps.

Neither the captured game frames nor the original frame-record bytes are
redistributed.

### Opt-in host XOR animation prototype

`src/scene_animation.{hpp,cpp}` implements a host-side animated PIC XOR layer.
It accepts an already-composed room background, a decoded XOR overlay, the
next **guest-millisecond** toggle deadline, a period and an initial visible
state. It advances over dropped display frames without losing the XOR phase.
An even number of elapsed toggles leaves the scene unchanged; an odd number
applies the overlay once. Replacing the room background preserves the phase;
`synchronize()` accepts an observed deadline and visible state after native
story or UI activity changes scheduling.

This is an **independent rendering prototype**, not a port of
`rtl:0x134a5`, not a replacement-registry entry, and not enabled in the SDL3
game runtime. The diagnostic `gateway-scene` CLI opts in explicitly:

~~~sh
gateway-scene --base 15 --layer 18 --animate-layer 17 \
  --animate-next-toggle-ms 16500 --animate-period-ms 500 --at-ms 16500 \
  --reference-ppm build/rtl-blink-v033/t16500.ppm \
  --screen-x 261 ../gateway-dos/GATE_102.PIC
~~~

The `16500` deadline and initial hidden phase are observations for this
**specific idle replay**, not universal animation start values. The CLI also
accepts `--animation-visible` for an initially visible overlay, and `-o` to
write an indexed PNG. Animation options must be supplied together; the
optional-layer inference mode remains separate. The compiled
`scene-compositor-tests` with the PIC, four original room snapshots and the
private idle-capture directory verifies the still scene and all eight
animated frames. All eight room pictures match **all 84,896 original pixels**.
Synthetic checks cover missed frames, reversible XOR, room-background
replacement, explicit resynchronization, invalid geometry and timestamps.

The 32 action-interleaved captures do *not* follow one global 500-ms phase;
the original game sometimes shifts its effective deadlines. Integrating the
host model into an opt-in SDL3 runtime path requires observing scheduler
updates or implementing their equivalent and verifying those action captures.
No such runtime equivalence is claimed here.

### Exact-match SDL room presentation bridge

`src/host_scene_preview.{hpp,cpp}` caches all eight XOR-layer combinations
for `GATE_102.PIC` background 15 and optional entries 16, 17 and 18. It
compares an original ARGB framebuffer against the cached host compositions,
and redraws the room rectangle **only after exactly one complete pixel match**.
Ambiguous matches and unmatched frames retain the original framebuffer, so
the normal game remains the fallback. Neither this check nor the host draw
modifies guest RAM, native VGA storage or original animation scheduling.
The drawing is inserted before SDL debug overlays and mouse cursor rendering.

Enable it explicitly with `gateway-run ../gateway-dos/GATE.EXE
--host-scene-preview` (interactive SDL runtime only). The first-room
resource choice and screen origin `(261,0)` are deliberately specific to the
verified Gateway VGA captures; a generic game-wide native renderer is not
implemented. The `host-scene-preview-tests` executable always runs synthetic
matching and mismatch/fallback checks. Given the original PIC and the
private `rgn-scenes-v026`, `rtl-blink-v033`, and `rtl-actions-v034`
capture directories, it also matches **all 44 original screenshots**:
4 static states, 8 idle animation states and 32 interleaved-action states.
Every matched frame is byte-identical after native-room replacement.

This bridge *follows* the original frame's visible XOR combination; it does
not independently predict action-dependent animation timing. It establishes
the native rendering handoff and conservative runtime fallback, not a
verified native replacement of any RTL routine. The interactive SDL path
cannot be compiled/tested in a host-only environment without SDL3 and
Unicorn; manual runtime verification is still required.

### Resident deadline cadence and independently painted frames

The recovered RTL sequence slot's 32-bit *next-frame deadline* is an
observable phase clock, but **not** a complete rendering state: the one-frame
sequence's frame index stays zero whether the indicator is on or off.
`RtlXorDeadlinePhase` tracks changes in that deadline in 500-ms increments,
after its visible phase has been anchored by an independent observation. Equal
deadlines preserve the phase; a jump of N complete 500-ms periods toggles it
iff N is odd. A backward clock, non-periodic jump or initial unanchored
deadline returns no phase and requires explicit resynchronization. The
tracker does not read or change guest RAM and is **not** a port of the
original scheduler at `rtl:0x134a5`.

The private 1-MiB RAM captures from the same original launch have
`DS=5b46`; the verified relocated slot-zero deadline is at `DS:9ffa`.
This address is *not portable to other runs*; it is used only in the
optional original-capture test and should be resolved from linked RTL code
before a live integration. Comparing that deadline to the uniquely inferred
indicator layer in each original screenshot gives:

| Original captures | Independent deadline predictions | Re-anchors after non-periodic jumps |
| --- | ---: | ---: |
| Eight idle frames (16,000–17,750 guest ms) | 7/7 | 0 |
| 32 action frames (19,500–36,000 guest ms) | 28/28 | 3 |

The three action-timeline gaps change the next-frame deadline by **900,
2,010 and 2,620 ms**, rather than a multiple of 500. Their cause and any
hidden intervening reset/draw events remain unresolved, so they are *not*
accepted as valid predictions; the reference frame supplies a new anchor.
All 35 confident frames have the correct indicator phase. This explains why
a single globally periodic blink clock could not match the action replay.

`HostScenePreview::paint_mask()` now provides a separate, explicitly driven
native render: it paints the selected cached scene without consulting the
original framebuffer and fails without modifying anything if the room is
off-screen or the mask is invalid. The regression clears the original room
rectangle, paints from the host-only PIC layers and checks every resulting
pixel, including all 35 frames driven by the **predicted** deadline phase.
Other layer bits (drawer/card) are still inferred from the original image in
this experiment. A fully independent SDL renderer needs verified story
predicate inputs for those layers, a trustworthy live RTL slot address, and
safe re-anchoring at discontinuities. The interactive
`--host-scene-preview` remains exact-match only.
