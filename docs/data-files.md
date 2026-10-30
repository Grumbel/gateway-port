# Data file formats

Survey of the resources in `../gateway-dos/`. All integers are little-endian
unless noted. **Confirmed** means checked against every file of that type;
**Hypothesis** means supported by evidence but not yet tied to game code. None
of this has been traced through the original executable or compared with DOSBox.

`gateway-extract` implements the PIC format. Checks of the other formats used
throwaway Python scripts, which are not in this repository. See also the
[Spellcasting comparison](spellcasting.md) and the
[ScummVM engine comparison](scummvm-legend.md), which resolved several unknowns.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `GATESTR.DAT` | 349805 | `940c12fc7c615e6643562457942f97879457d338aa4ec14dc3a2e1827a5f5d89` |
| `VOCAB.DAT` | 22081 | `33468f585c4d6fdf9dd1dd4e1231400647954c433aba27ae7e7cc4f83b5be309` |
| `OBJECT.DAT` | 8031 | `736581a6bc32dac41aa4504380a83837e6ef2da46f9c42c2f9417d976933e9ca` |

## Overview

| Files | Count | Content | Status |
| --- | ---: | --- | --- |
| `GATESTR.DAT` | 1 | Game text, Huffman coded | Fully decoded |
| `VOCAB.DAT` | 1 | Parser vocabulary and index | Words decoded; index partly known |
| `OBJECT.DAT` | 1 | Object and room names | Fully decoded |
| `GATE_nnn.PIC` | 40 | Pictures, sprites, PKWARE DCL compressed | All entries decode |
| `GATE_nnn.RGN` | 17 | Clickable object rectangles for pictures | Layout decoded |
| `GATE_nnn.FNT` | 21 | Bitmap fonts | Layout decoded |
| `GATE_nnn.MUS` | 4 | Paired MT-32 and AdLib Standard MIDI files | Container decoded |
| `*.RS` | 31 | Unsigned 8-bit sound effects | Samples readable; rate unknown |

`GATE.RTL` contains the extension list and name pattern `%s_%03d.%s`. It also
contains `PKWARE` copyright text and the option names `real`, `noreal`, `adlib`,
`mt32`, `blaster`, `xmouse`, `mono`, `svga`, and `nocycle`.

## Comparison with Death Gate

[dgate_resource_manager](https://github.com/Risca/dgate_resource_manager) is a
useful lead. Gateway is an earlier relative of the same engine, not a match.

| Area | Death Gate tool | Gateway |
| --- | --- | --- |
| String file | Three-word segment header | Two-word header; same Huffman scheme |
| PIC directory | Offset, flags, width, height, padding | Same layout; last word is not always zero |
| PIC flags | Bits 0, 12, 14 | Same meaning, plus more bits |
| PIC pixels | Raw 8-bit | DCL compressed; 1, 4, or 8 bits per pixel |
| Palette | Always 768 bytes | 16, 48, or 768 bytes |
| Music | XMIDI, offset and size table | Standard MIDI, paired 24-byte records |
| Digital sound | `DGATE001.VOC` archive | Individual `.RS` files |
| Video | `.Q` and `.FLI` | None present |

The tool does not handle FNT, RGN, `VOCAB.DAT`, or `OBJECT.DAT`. Its string
parser credits Vasyl Tsvirkunov's `legend-str`, which lists Gateway as string
format version 2. The tool itself implements a later version.

## GATESTR.DAT

**Confirmed.** Layout:

1. `u16` segment count: 56.
2. For each segment, `u16` string count and `u16` compressed byte count.
3. For each segment, one `u16` compressed size per string.
4. `u16` tree word count (436), then the Huffman tree.
5. `u16` count (128), then `u16` offsets into the dictionary.
6. `u16` dictionary size (806), then NUL-terminated dictionary strings.
7. Compressed strings from offset 14192 to the end of the file.

Each string is decoded on its own. Start at `code = tree_words - 2` and read bits
least-significant first from each byte. Set `code = tree[code | bit]`. If bit 15
is clear, keep walking. Otherwise `leaf = code ^ 0xffff`: a value below `0x80` is
a character, and any other value selects dictionary entry `leaf - 0x80`. Restart
from the root after each leaf.

The file holds 6013 strings. Size tables match the segment totals, and the
335613 stream bytes end exactly at end of file. Stop at the first NUL, because
leftover padding bits can decode to stray characters. Text uses `printf`-style
`%s` and `%d` markers, `\n`, and `\t`. For example, string 0/1 is
`Slot:    %d (%s)\n`. **Hypothesis:** the game addresses strings as `0xFsssnnnn`,
with segment `sss` and string `nnnn`, as ScummVM's loader expects.

## VOCAB.DAT

**Confirmed:**

1. `u16` tree word count (84), then a tree using the `GATESTR.DAT` scheme.
2. `u16` compressed size (10125) at offset 170, then the compressed word list.
3. `u16` word count (2644) at offset 10297, then one `u16 word_offset, u16 flags`
   pair per word. `word_offset` is a byte offset into the decompressed list.
4. `u16` link group count (91). Each group is `u16 word, u16 count`, then `count`
   pairs of `u16, u16` (210 in total). The file ends exactly after these.

Unlike `GATESTR.DAT`, the word list is one continuous stream of NUL-terminated
words, starting `_integer`, `_string`, `_time`, `a`, `again`. It holds 2644 words
and decompresses to about 18840 bytes.

The pair table is sorted alphabetically by word and starts `&`, `'widow`, `_integer`.
The layout comes from ScummVM and checks out against the file. The meaning of the
link groups is not known; ScummVM keeps only the last pair of each.

**Hypothesis:** `flags` is a word-class bit mask. Its most common values are 2, 4,
6, 1, and 66.

## OBJECT.DAT

**Confirmed.** A `u16` byte count (8029) is followed by 785 NUL-terminated names:
`Blast Zone`, `Gray Plain`, `Plateau`, and so on. RGN files use zero-based
indexes into this list.

## PIC

**Confirmed for all 40 files.** A PIC file starts with a directory of 12-byte
entries:

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | `u32` | File offset of the entry |
| 4 | `u8` | Frame count: 0, 1, 2, or 5 |
| 5 | `u8` | Flags |
| 6 | `u16` | Width in pixels |
| 8 | `u16` | Height in pixels |
| 10 | 2 × `u8` | Unknown; often 0–4, and `04 04` in 28 entries |

The lowest non-zero offset equals the directory size, which gives the entry count.
In Gateway that is always the first offset. Of the
1519 entries, 15 have zero width or height and use at most four bytes.

Each entry holds these fields in order, followed by the pixel stream:

| Field | Present when | Size |
| --- | --- | ---: |
| Frame positions `u16 x, u16 y` | Frame count above 0 | 4 per frame |
| Palette | Flag `0x10` | 16, 48, or 768 |

Below, flags are written as the 16-bit value of offsets 4–5, with the frame count
in the low byte, so `0x10` above appears as `0x1000`. Each frame places the same
bitmap at another position, and `gateway-extract` uses the first.

Bits `0x1000` and `0x4000` match Death Gate. Bits 8–11 identify a display class,
described below. Top-level flag values are `0x2xxx` for plain images, `0x3xxx` for
images with a palette, `0x4xxx` for XOR overlays, and `0x5xxx` for XOR overlays
with a palette. Bit `0x2000` has no known meaning.

The palette size follows the display class. Class 1 has 16 bytes, which look like
EGA palette registers. Class 2 has 48 bytes (16 RGB entries) and class 3 has 768
bytes (256 RGB entries). RGB components use the VGA 0–63 range. ScummVM also gives
class 4 48 bytes, though no Gateway entry has one.

Pixels are a PKWARE DCL "implode" stream, beginning with `00 06`: binary literals
and a 4-KiB dictionary. All 1504 non-empty entries decompress, and every stream
ends exactly where the next entry begins. zlib's `contrib/blast` implements the
format.

Decompressed rows are padded to whole bytes, with no plane separation:

| Depth | Row bytes | Pixel order |
| ---: | ---: | --- |
| 8 | `w` | One byte per pixel |
| 4 | `(w + 1) / 2` | **Low nibble is the left pixel** |
| 1 | `(w + 7) / 8` | Most significant bit is the left pixel |

The nibble order is decided by odd-width images: the final high nibble is zero in
all 43718 such rows, whereas the final low nibble is zero in only 17630. This
order is unusual, so do not assume the conventional one.

Flag `0x4000` overlays are XORed onto the current image at their stored
position. Applying `GATE_102` entries 1, 2, and 4–7 to entry 0 this way gives far
fewer edge mismatches than a plain copy (60 versus 114 for entry 1), and the
result looks seamless. Palette-index XOR therefore acts as a frame delta.

Overlays do not name their picture, and a smaller plain image can sit between a
picture and its overlays. Entry 3 of `GATE_102` does: entries 4–7 lie outside it
and belong to entry 0. `gateway-extract` therefore uses the latest plain image that
covers the overlay's rectangle. Of 686 Gateway overlays, 680 fit the immediately
preceding plain image. Each overlay applies to the plain picture alone, not on top
of the previous overlay. **Hypothesis:** this is what the game does; it matches the
visible results but has not been traced. In Spellcasting 301, 16 of 467 overlays
have no covering image and stay as raw deltas.

Files without a palette of their own depend on another file loaded earlier. Give
`gateway-extract` a whole game's files at once so that it can carry the latest
256-color palette forward.

### Display classes

Graphics modes are listed in `GATE.RTL` at the offsets below. **Hypothesis:** the
hundreds digit of a PIC or FNT number selects the same class as flag bits 8–11.
Sizes of the same artwork grow with the mode's height. For example, one frame
measures 130×47, 130×77, 132×104, and 142×127 across series 0, 1, 2, and 4.

| Series | Class | Mode string | `GATE.RTL` offset | Depth |
| ---: | ---: | --- | ---: | ---: |
| 0xx | 0 | CGA 640x200x2 | `0x2818` | 1 |
| 1xx | 1 | EGA 640x350x16 | `0x2856` | 4 |
| 2xx | 2 | VGA 640x480x16 | `0x2894` | 4 |
| 3xx | 3 | VGA 320x200x256 | `0x28d2` | 8 |
| 4xx | 4 | Tseng, Paradise, Video7, ATI 800x600x16 | `0x2910`–`0x29ca` | 4 |

The match is imperfect:

- Room art in `GATE_102`–`GATE_114` is 379×224 with class 2 flags but series 1.
- Pixel depth follows the series even where flags disagree. `GATE_011` has nine
  1-bit entries with class 2 flags, and `GATE_101` #1 has 4-bit data with class 0.
- 2xx and 4xx hold only a few interface files, so scaled reuse of other series is
  likely. `GATE.RTL` contains `scale_pic : EGA -> VGA disabled`.

Which series a `VGA` launch actually loads should be checked under DOSBox. ScummVM's
[picture numbers](scummvm-legend.md#picture-numbers) suggest 1xx rooms and 2xx interface art.

## RGN

**Confirmed for the layout.** Files are arrays of 6-byte records with two parts.
Header records come first, one per entry of the matching PIC directory:

| Word | Meaning |
| ---: | --- |
| 0 | Byte offset of this entry's rectangles |
| 1 | Default object index for the entry |
| 2 | Rectangle count |

Rectangle records follow, and they cover the rest of the file contiguously:

| Word | Meaning |
| ---: | --- |
| 0 | Object index, or 0 in `GATE_015` and `GATE_315` |
| 1 | Top-left: low byte `x`, high byte `y` |
| 2 | Bottom-right: low byte `x`, high byte `y` |

`x` is in two-pixel units. Across the room files it peaks at 189 (379 / 2) and
`y` peaks at 223. Overlaid on `GATE_102` entry 15, rectangles named `desk chair`
and `scorch mark` fall on the expected parts of the scene.

Header counts match a PIC directory for all 15 non-empty files:

| RGN | Paired PIC | Entries |
| --- | --- | ---: |
| `GATE_002`–`GATE_014` | `GATE_102`–`GATE_114` | 63, 46, 25, 54, 99, 26, 23, 34, 32, 33, 32, 50, 23 |
| `GATE_015` | `GATE_015`, `GATE_115` | 47 |
| `GATE_315` | `GATE_315` | 59 |

`GATE_000.RGN` and `GATE_001.RGN` are 128 zero records. **Hypothesis:**
rectangles drive mouse selection of named objects.

## FNT

**Confirmed for all 21 files** by exact file sizes.

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | 2 × `u8` | Always `01 01` |
| 2 | `u8` | Bytes per glyph row |
| 3 | `u8` | First character (`0x21`) |
| 4 | `u8` | Last character (`0x7f`) |
| 5 | `u8` | Unknown, 0–4; often larger for taller fonts; ScummVM calls it a line offset |
| 6 | `u8` | Glyph height |
| 7 | `i8` | Fixed width, or −1 for a width table |
| 8 | `i8` | Fixed spacing, or −1 for a spacing table |
| 9 | `u8` | Always 0 |

Proportional fonts then have 128 width bytes followed by 128 spacing bytes, each
indexed by character code. A spacing byte holds left spacing in its low nibble and
right spacing in its high nibble. In `GATE_001`, `i` has width 2 and spacing `0x11`,
`W` has width 14, and space has width 4. Glyphs follow for each character in the
range, top row first, with most significant bit at left.

File size is therefore `10 + (proportional ? 256 : 0) + count * row_bytes * height`.

## MUS

**Confirmed for all 4 files.** A directory of 60 records, one zero byte, then
data from offset 1441. Each 24-byte record holds two descriptors of the form
`u32 offset, u16 size, i16 tag, u16 a, u16 b`. Empty slots have offset 0 and tag
`-1`. There are 37 songs, and each descriptor pair points to a Standard MIDI file:

| Part | Tag | SMF type | Division | Content |
| --- | ---: | ---: | ---: | --- |
| First | 1 | 1 | 120 | Separate tracks with instrument names |
| Second | 0 | 0 | 420 | One track with sequencer-specific events |

One zero byte separates the two files of every song. `a` and `b` are small values (1–7)
in the first descriptor and always 0 in the second; their meaning is unknown. The
second descriptor's size was 4 bytes longer than the MIDI data in the two songs
parsed event by event.

**Hypothesis:** the first file is for the Roland MT-32 and the second for AdLib.
First-file track names such as `ELEC ORG 4`, `TAIKO RIM`, and `STR SECT 1` are
MT-32 patches, and program 11 accompanies `ELEC ORG 4`. Second files contain
meta `0x7f` events with manufacturer bytes `00 00 3f`, believed to be Ad Lib's:

| Subtype | Meta length | Count | Likely meaning |
| ---: | ---: | ---: | --- |
| `00 01` | 34 | 836 | Instrument definition |
| `00 02` | 6 | 37 | Melodic or percussive mode |
| `00 03` | 6 | 37 | Pitch-bend range |

If this holds, music can be played by an OPL3 emulator such as the vendored
Nuked OPL3 without a separate instrument bank. How `BLASTER` chooses between the
two has not been established.

## RS

**Confirmed for all 31 files:**

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 5 | Signature `STEVE` |
| 5 | 1 | 2, or 8 in `FORK.RS`; unknown |
| 6 | 1 | `0x48` in every file; unknown |
| 7 | 4 | `u32` sample byte count; file size minus 32 |
| 11 | 21 | Identical bytes in every file; unknown |
| 32 | N | Samples |

Samples are unsigned bytes from 0 to 68. Lengths are multiples of 16, and every
stream ends in 0. `EXPLODE.RS` has a lag-1 autocorrelation of 0.99, and `ZAP.RS`
starts near 34 with smooth changes, so this is raw waveform data rather than a
compressed stream. **Hypothesis:** 34 is the midpoint.

**Hypothesis:** the range suits pulse-width output on the PC speaker, which would
fit the `real` and `noreal` options. If `0x48` (72) is a timer divisor, the rate
would be 1193182 / 72 ≈ 16572 Hz. Neither point is verified, and playback rate
must be recovered from the driver or a DOSBox recording.

## Unresolved references

- The executables name `engine.rs`, `sludge.rs`, `switch.rs`, and `tree.rs`, which
  are not in this distribution. All 31 present files are referenced.
- `GATEMAIN.EXE` names `RESTART.DAT`, also absent. **Hypothesis:** the game
  creates it at runtime. Run the game on a copy to check, since the original
  directory must stay unchanged.

## Next steps

Trace how the interpreter in `GATEMAIN.OVL` selects strings, objects, and series.
It holds no obvious script file; game logic appears to be compiled into the
executables and overlay, so a replacement interpreter needs code analysis as well
as these formats. Decoders should be added as tested `src/` parsers using
synthetic fixtures, with original data kept external.
