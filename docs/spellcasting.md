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

The directory and the frame positions, whose count is the low flag byte, match
[Gateway](data-files.md#pic). Spellcasting 101 uses counts of 0, 1, and 5, and
Spellcasting 201 uses 0, 1, and 2. Directories are not always sorted, so the lowest
non-zero offset marks their end, not the first entry. The rest differs by game.

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

### Startup compatibility probes (2026-11-01)

The Spellcasting 101 launcher calls `INT 10h AX=1B02h` after querying the
current video mode. The documented VGA functionality/state service defines
`AX=1B00h`, not the `AL=02h` variant. The runtime now returns an unsupported
signature (`AL=00h`) specifically for this probe, leaving other unknown
interrupts fatal. Whether the launcher subsequently reaches gameplay remains
to be verified with Unicorn.

Spellcasting 201 gets through its RTLink launcher and then writes to port
`226h`, the reset register of a Sound Blaster at base `220h`. There is no
Sound Blaster PCM/DMA emulation in this port, so the runtime now models an
absent DSP: its reset writes are harmless and the DSP status/data probes
return an undriven `FFh` instead of claiming a reset acknowledgement (`AAh`).
This preserves the existing AdLib emulator and avoids inventing DSP audio
support. Subsequent startup and audio behavior still need runtime validation.

In the next user trace, 101 advanced to `INT 10h AX=1200h, BL=10h`, the
standard EGA/VGA configuration query. The runtime now identifies a color
adapter with 256 KiB of video memory and a nonzero configuration word.
Spellcasting 201 progressed to a write to `22Ah`, which is normally the
Sound Blaster DSP's read-data register. Detection code may write to that
read-only register; this is now treated as harmless in the absent-DSP model.
These are sequential compatibility fixes, not yet proof of a working game.

The following trace reaches `INT 10h AX=EF00h` in 101. No standard VGA
definition was established for this service, so it is handled as an unknown
BIOS extension that leaves registers unchanged; its intended vendor-specific
behavior is still unknown. In 201 the detection sequence advances to a write
at port `228h`. Rather than adding each port separately, the runtime models
the entire unpopulated `220h`–`22Fh` Sound Blaster/Game Blaster I/O window:
reads return `FFh`, and writes have no effect. This does not emulate any
digital or CMS audio hardware.

- Colors have not been compared with the running games under DOSBox.
- Spellcasting 101 and 201 mono entries are shown as black and white.
- Some Spellcasting 101 1-bit entries, such as `S101_C2` #19 and #40, look like
  noise and may be deltas. **Hypothesis**; no flag marks them.
- Raw entries that end a file are followed by unexplained bytes, for example
  `S101_C2` #41 has 658 bytes available and needs 364.
- RGN contents have not been matched to pictures in these games.

## Spellcasting 301 runtime check (2026-10-31)

The same real-mode/SDL runtime now reaches **Beach** through the original
`S301.EXE` launcher and `S301MAIN.EXE` overlay handoff. Previously BIOS `AH=09h`
stopped at `2b9e:5216` because the fallback font lookup required `GATE_000.FNT`.
S301 has no such file. Its fixed `S301_100.FNT` (8×14) and `S301_200.FNT` (8×16)
share the checked fixed-width layout and now supply native-height BIOS glyphs.
The lookup uses the executable's prefix and case-insensitive DOS resolution;
no Gateway files need to be copied into `301/`.

```sh
./build/gateway ../301/S301.EXE --state-dir build/s301-state
./build/gateway-probe ../301/S301.EXE --press escape \
  --state-dir build/s301-state --milliseconds 22000 --dump-prefix build/s301-main
```

Use separate state directories for different games because generated filenames
such as `RESTART.DAT` overlap. The probe capture shows the Beach scene, menus,
and opening description. This establishes startup compatibility; full gameplay,
save/restore, mouse behavior, and audible sound in S301 still need validation.
The packaged `LEGEND.BAT` requests SVGA and Sound Blaster; those command-line
options are not established by this default-launch test.

SHA-256 identities:

- `S301.EXE`: `31f3f72072fe56c2b627a0e176f217f04bcd68d4935da44ee5889c2dff19e66a`
- `S301MAIN.EXE`: `cbbc752eaa659dd0970b1526616ba79ec6021243e36828af67c756f2c155d8b8`
- `S301_100.FNT`: `4022e6ad5fa68117db52601e87757b000a0c77a9de0b93394b27c04bfbf0631a`
- `S301_200.FNT`: `66392db21a0fc7db5d423962c2524edc05521cf927509a4bc4fb9819d3cea93b`
