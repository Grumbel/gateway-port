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
| `ovl:0x051550`–`0x05156a` | When not eligible and a nonnegative handle exists, invokes cleanup services at `1485:1928` and `1485:1bac`, then sets `DS:6a20=0xffff` |

All four captured snapshots have a nonnegative handle (`DS:6a20=0`),
and the pixels alternate while this handle stays unchanged. That is direct
evidence for a persistent native picture/animation path **separate from the
conditional-layer updater**. The exact behavior and stable file identities
of the called graphics services, the animation tick cadence, and the
interaction with table-based XOR redraws still require disassembly and
additional runtime entry/return traces. `1485:*` addresses above are linked
call operands, not yet resolved original-file routine identities.

The native test now checks all four entire room views, and the original
callback bytes, animation-call operands, handle and guest state are checked
separately. No original asset or runtime snapshot is committed.
