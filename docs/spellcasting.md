# Spellcasting comparison

Legend's Spellcasting games, in `../101/`, `../201/`, and `../301/`, are earlier
titles on the Gateway engine line. **Spellcasting 301 is the same generation as
Gateway**, Spellcasting 201 is an intermediate step, and Spellcasting 101 is the
oldest and differs the most. Only the formats below were compared; no game code
was compared except where stated.

| Area | Spellcasting 101 | Spellcasting 201 | Spellcasting 301 |
| --- | --- | --- | --- |
| PIC directory | Same | Same | Same |
| PIC encoding | Own flags; LZW, Huffman, raw | Own flags; LZW, Huffman, raw | Gateway; PKWARE DCL |
| Strings | Three-word headers; tree not decoded | Three-word headers; Gateway Huffman | Gateway layout |
| `VOCAB.DAT` | Plain text | Huffman, as Gateway | Huffman, as Gateway |
| `OBJECT.DAT` | None | As Gateway | As Gateway |
| FNT | None | None | All 19 match the Gateway layout |
| RGN | 6-byte layout | 6-byte layout | 6-byte layout |
| MUS | 20-byte records; SMF and ROL | Gateway layout | Gateway layout |
| RS | `STEVE`; length field unexplained | Mostly Gateway | Gateway |
| Graphics modes | Not checked | CGA, EGA, VGA | EGA, VGA, VESA |

`S301.RTL` contains PKWARE strings; the other two executables do not.

## Strings

`S201STR.DAT` starts with a `u16` segment count, then `u16` plain size, compressed
size, and string count per segment. There are no per-string size tables. The tree
(422 words), dictionary (128 entries), and walk are those of `GATESTR.DAT`, but
each segment is one continuous stream. All segments decode to their stated string
counts. The decoded text totals 292546 bytes against 292504 in the headers,
because trailing bits can produce a stray byte or two per segment.

`S101STR.DAT` has the same headers, but its tree is 218 pairs with different
values (`1, 81, 2, 6, 3, 5`, and so on). The Gateway walk produces garbage.

## Music

Spellcasting 101 MUS files start with 15 records of `u32, u16 size, i16 tag, u16`,
so the first offset is 301 rather than 1441. Each file has equal numbers of `MThd`
headers and `\roll\default` signatures: the first are Standard MIDI, and the second
are AdLib Visual Composer ROL songs. `S101.BNK` carries an `ADLIB-` signature, so
it is presumably their AdLib instrument bank.

## Pictures

Every entry of every file decodes with `gateway-extract`:

| Game | Files | Entries | Images | Dialect |
| --- | ---: | ---: | ---: | --- |
| Gateway | 40 | 1519 | 1504 | `gateway` |
| Spellcasting 101 | 12 | 1036 | 724 | `s101` |
| Spellcasting 201 | 16 | 1110 | 1107 | `s201` |
| Spellcasting 301 | 17 | 649 | 649 | `gateway` |

Entries without images are empty, or are palette-only in Spellcasting 201.

The directory and the optional position fields (flags `0x0001`, `0x0004`,
`0x0002`) match [Gateway](data-files.md#pic). Directories are not always sorted,
so the lowest non-zero offset marks their end, not the first entry. The rest
differs by game.

**Spellcasting 101.** Let `high = flags >> 8`. Bit 0 of `high` means there is no
palette, and `high >> 1` selects the encoding:

| Value | Encoding | Palette |
| ---: | --- | --- |
| 0 | LZW, 4-bit | 16 bytes |
| 1 | Huffman, 1-bit | Never |
| 4 | Raw, 1-bit | Never |
| 5 | Raw, 4 bit planes per row, plane 0 first | 16 bytes |

**Spellcasting 201.** These are independent bits. Flag `0x0100` adds a 16-byte
palette, and `0x4100` with zero size is a palette alone. The encoding is `0x0200`
LZW, `0x0400` Huffman, or `0x2000` raw 1-bit.

The flag `0x0200` means different things in the two games, so `gateway-extract`
tries each dialect on a whole file and uses the one that decodes every entry.
Palettes are EGA register values with 6-bit `rgbRGB` colors. One-bit rows are
padded to bytes, with the most significant bit on the left.

### LZW

Traced from `S101.EXE`: main loop `0x9714`, reset `0x9c2f`, and code reader
`0x9c55`, as offsets into the image after the `0xe00`-byte header.

Codes are read most significant bit first, starting at 5 bits, and the table starts
at 18. Values 0–15 are pixels. Code 16 widens the code by one bit and is followed
by another code. Code 17 resets the width and table, and the next code is a literal.
A code equal to the current all-ones mask ends the stream. Otherwise this is plain
LZW over pixels, including the case of a code equal to the next free entry.

`S101_E3` #114 has 21 pixels and then no complete end code: the next entry starts
after three more bits. The decoder therefore stops at the final pixel.

### Huffman

Traced from `S101.EXE`: table reader `0x9574` and tree walk `0x8fd5`.

The first byte plus one is the record count. Each record is `u8 length`, `u8 symbol`,
and a left-aligned code of one byte, or two if `length > 8`. Code bits are then read
most significant bit first until `((w + 7) / 8) * h` bytes are produced.

A record of length 0 is a single symbol with no bits at all, which fills the image.
`S101_C2` #20 and #21 use this for solid white.

## Limitations

- Colors have not been compared with the running games under DOSBox.
- Spellcasting 101 and 201 mono entries are shown as black and white.
- Some Spellcasting 101 1-bit entries, such as `S101_C2` #19 and #40, look like
  noise and may be deltas. **Hypothesis**; no flag marks them.
- Raw entries that end a file are followed by unexplained bytes, for example
  `S101_C2` #41 has 658 bytes available and needs 364.
- RGN contents have not been matched to pictures in these games.
