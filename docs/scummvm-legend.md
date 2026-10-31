# ScummVM Legend engine comparison

[dreammaster's `legend` branch](https://github.com/dreammaster/scummvm/tree/legend/engines/legend)
holds an unfinished ScummVM engine for Gateway and Companions of Xanth. This note
covers commit `d4f425c` (2018-01-22), about 10600 lines. It draws the Gateway
interface and decodes resources, but has no interpreter, RGN loading, overlay
drawing, or sound. Its name for this generation of games is "early".

Its detection MD5 for `GATE_000.RGN`, `33c250bf306b7cbbd3dd71b6029b8784`, matches
our file, so it was written against the same Gateway data. The code is GPL. Use it
as a research lead and do not copy it into this port.

## Checked against our data

| Topic | ScummVM | Result |
| --- | --- | --- |
| PIC low flag byte | Frame count; `count * 4` position bytes | **Confirmed.** Only 0, 1, 2, and 5 occur, and output is unchanged |
| PIC high flag byte | Low nibble is depth, `0x10` is palette | **Confirmed** for palette |
| Palette sizes | 0, 16, 48, 768, 48 for classes 0–4 | Adopted; no class 0 or 4 palette occurs |
| 4-bit pixels | Low nibble first | **Confirmed**, matches our finding |
| `VOCAB.DAT` tables | `u16` count, `(offset, flags)` pairs, link groups | **Confirmed** for Gateway: parses to the exact end of file |
| FNT tables | 128 widths, 128 left/right spacing nibbles | **Confirmed** on `GATE_001`: values are mostly `0x10` or `0x00` |
| Picture numbers | See below | **Confirmed** on two uses |
| DCL decoder | Same algorithm as table-driven code | Consistent; both decode all data |
| Huffman text walk | Same as ours | Consistent |

## Picture numbers

`PicFile::open` splits a picture number in early games:

- Bits 0–7 are the entry index, and bits 8–11 are the file number.
- Bit 15 selects series 2. Otherwise bits 12–15 give the series, with 0 meaning 1.
- The file is `GATE_<series * 100 + file>.PIC`.

Two uses check out. List buttons load `17 | 0x8000` onwards, and `GATE_200` #17–21
are scroll buttons. Part titles load `260 + part`, which is `GATE_101` #4–6: the
Part 1, 2, and 3 titles.

**Hypothesis:** this partly answers which series a VGA launch uses: rooms from 1xx
and interface art from 2xx. ScummVM's screen is 640×480, and its 380×232 scene area
fits the 379×224 rooms. Its comment calls bit 15 "EGA pictures", which does not fit
that layout. The rule covers ScummVM's chosen mode only, not other display modes.

## Interface layout

`Interface` gives the 640×480 screen layout. It is not confirmed by us:

| Area | Rectangle (left, top, right, bottom) |
| --- | --- |
| Command buttons | 0, 0, 120, 136 |
| Compass | 120, 0, 260, 136 |
| Scene | 260, 0, 640, 232 |
| Commands list | 0, 136, 117, 480 |
| Items list | 117, 136, 260, 480 |
| Text | 260, 232, 640, 480 |

Text uses 30 rows of 80 columns. `Commset` and `CommandButtons` hold more button
rectangles. Cursors and command lists come from a separate `legend.dat`, which is
not in that directory. Their source in the original executable is unknown.

## Points to treat with care

- **Strings:** `TextIndexEntry` reads `u16` count and `u32` size, six bytes per
  segment. `GATESTR.DAT` has four-byte headers: it starts `56, 61, 1063, 142, 3120`.
  That loader does not fit our file, and may have been tested only with Xanth.
- **Message numbers:** `getMessage` accepts `0xFsssnnnn`, with section `sss` and
  string `nnnn`. **Hypothesis** for Gateway: nothing in the branch calls it with a
  real number, and it has an infinite loop for non-zero string numbers.
  [Native Gateway references](game-logic.md#text-tokens-correction-to-the-earlier-hypothesis)
  instead support `0xf0000000 | (segment << 10) | string_index` for six checked uses.
- **`PIC_40`:** this is decimal 40, or `0x28`, but is tested against the flags byte
  to set a mode of 3. **Hypothesis:** `0x40` was meant, which is our XOR overlay bit,
  and 3 is the VGA graphics controller's XOR function. The value is never used.
- **Directory end:** `IndexEntries::load` stops at the first entry's offset, and
  asserts that it is non-zero. Spellcasting 101 needs the lowest non-zero offset, but
  this engine does not target that game.
- **Frames:** frame 0 means origin (0, 0), and frame `n` uses position `n`. Compass
  pieces load frame 1. What frames 2–5 select is unknown.
- **Two last bytes:** `_fieldA` and `_fieldB` are stored but not used. Our notes treat
  them as one unknown `u16`. Most values are small.
