# Mission Critical PIC research

Mission Critical (1995) uses a different generation of Legend's PIC format.
These findings were checked against four external samples: `MC040.PIC`,
`MC041.PIC`, `MC051.PIC`, and `MC101.PIC`. They are not part of the repository.
The bundled Crusher! decoder was reconstructed from a separately supplied
`MISSION.EXE`. The game executable and the compressed sample data are not
distributed with this project.

## Directory and payloads

**Confirmed for all 61 occupied entries.** Each file begins with a fixed
`0x1400`-byte directory of 256 entries, each 20 bytes. All integers are
little-endian:

| Offset | Type | Meaning |
| ---: | --- | --- |
| `00h` | `u32` | Absolute payload offset (zero means unused slot) |
| `04h` | `u32` | Compressed image stream length, excluding palette data |
| `08h` | `u16` | Flags; observed `ff00h`, `0000h`, `0003h` |
| `0ah` | `u16` | Screen X coordinate |
| `0ch` | `u16` | Screen Y coordinate |
| `0eh` | `u16` | Width |
| `10h` | `u16` | Height |
| `12h` | `u16` | Unknown; observed `0000h` and `0002h` |

Entries with a nonzero high flags byte (`flags & ff00h`) carry a palette
update at their payload offset. `MISSION.EXE` tests the high byte at `0x855cc`,
not the full value `ff00h`:

| Field | Type | Meaning |
| --- | --- | --- |
| `start` | `u16` | Byte offset within a 768-byte VGA RGB palette |
| `count` | `u16` | Number of RGB bytes following (multiple of 3) |
| `rgb` | `u8[count]` | VGA DAC components, each 0–63 |
| `pixels` | `u8[compressed_size]` | Crusher-compressed image, if size is nonzero |

Without that flag the compressed stream starts at the payload offset. The
decoder must retain the most recent palette across entries (and potentially
across files). The four sample archives satisfy this relation for **all 61**
occupied entries, including palette-only records and EOF:

```text
next_offset = offset + compressed_size
            + ((flags & 0xff00) ? 4 + palette_count : 0)
```

| File | Occupied entries | Palette updates | Image streams | Palette-only |
| --- | ---: | ---: | ---: | ---: |
| `MC040.PIC` | 14 | 9 | 10 | 4 |
| `MC041.PIC` | 14 | 2 | 12 | 2 |
| `MC051.PIC` | 12 | 1 | 12 | 0 |
| `MC101.PIC` | 21 | 1 | 21 | 0 |
| **Total** | **61** | **13** | **55** | **6** |

Most palette updates replace all 768 bytes. `MC040.PIC` entries 3–6 instead
replace 96 bytes (32 RGB colors) starting at palette byte offsets 480, 192,
288, and 384 respectively. Those entries contain no image stream.

The original PIC loader also distinguishes compressed from uncompressed
images by comparing `compressed_size` with `width * height` (at `0x85a52`).
When those are equal the payload is already **raw eight-bit palette indices**;
Crusher must not be invoked. This case did not occur in the initial four
sample archives. Likewise, records do not need to occupy every byte through
EOF: the engine seeks by the per-entry offsets and permits unindexed trailing
data.

`MC101.PIC` entries 1–20 have flags `0003h` and last word `0002h`, often at
the same position and dimensions. They resemble animation or layered image
records, but their exact rendering semantics remain **unverified**.

## Native Crusher! 3.0 decompression

The native PKWARE DCL decoder used for Gateway cannot decode these pixels.
[QUFB's Mission Critical research](https://qufb.gitlab.io/writeups/2021/01/22/Decrushing-DOS-Doodads)
reports that `MISSION.EXE` identifies the **Crusher! Data Compression Toolkit
Version 3.0**. The researcher's [crusher_cli](https://gitlab.com/qufb/crusher_cli)
calls a proprietary `CrusherS.dll` through Wine; this project instead has an
independent native implementation in `src/crusher.cpp` and does not use that DLL.

The relevant routines in the bound DOS/4GW LE image of `MISSION.EXE` were
located through the game's PIC directory loader at virtual address `0x85420`.
That loader opens resource type zero and reads exactly `0x1400` directory bytes.
The decoder call at `0x8cb08` reaches Crusher at `0x9a524`; the bitstream
implementation is around `0xaf640`–`0xaf914`. The format has:

- An MSB-first bitstream with repeated blocks; each block starts with a 16-bit
  token count followed by three Huffman descriptions.
- A meta-code tree (up to 19 code-length symbols), a literal/match tree (510
  symbols), and a distance-class tree (13 symbols). Lengths are canonical
  Huffman codes; a zero-count tree can specify a single fixed symbol.
- Literal symbols `0`–`255`, followed by matches of length `symbol - 253`.
  The distance class supplies a variable-length offset into a rolling
  4096-byte dictionary.
- No embedded uncompressed-size field; the PIC directory width and height
  determine the expected number of output pixels. Records of that exact
  byte length are stored without compression.

For all **55** image streams in the original four sample archives, decoding produced
exactly `width * height` pixels, consumed the compressed data except for 0–7
padding bits, and generated a valid indexed PNG. A second exploratory Python
implementation was compared byte-for-byte with all 55 C++ PNGs. The USS
Lexington communications screen and mission background images were visually
inspected. No game resources are embedded in the tests.

Four additional archives (`MC004.PIC`, `MC005.PIC`, `MC015.PIC`, and
`MC024.PIC`) were later supplied. **All 287 image records decode**, including
the raw records `MC004` #27 and `MC015` #72–74. `MC024` #0 uses `ff04h` flags
and carries a palette update, confirming the high-byte test. The native PNGs
from these four files were generated without failures; notably `MC024` #0
renders a legible control panel. The related `.RGN` resources are a separate
region format, not required for decoding the pixel streams.

## Extracting PNGs and raw streams

`gateway-extract` detects the newer directory automatically or accepts
`--dialect mc`. Normal extraction now creates `NNN.png` files:

```sh
gateway-extract -o /tmp/mc-pics ../MissionCritical/*.PIC
```

The current palette is retained across records and input files in the order
provided. VGA six-bit DAC channels are expanded to eight-bit PNG palette
entries. When the first image is encountered before any palette update, a
greyscale fallback makes the indexed pixels inspectable but does **not**
reconstruct their intended colors. Flags `0003h` may refer to animations or
layers; each entry is exported separately, without unverified composition.

For format inspection, the original chunk export still works:

```sh
gateway-extract --raw-chunks -o /tmp/mc-pics ../MissionCritical/*.PIC
```

This writes per-file directories containing:

- `NNN.crusher`: exactly the compressed byte range from an image entry;
- `NNN.pal`: a 768-byte snapshot of the current 6-bit VGA RGB palette when
  a palette is known, including updates from earlier entries or input files;
- `index.tsv`: decimal directory fields, palette range and compressed size.

Synthetic unit tests cover directory bounds, palettes, fixed-symbol and
canonical Huffman tables, LZ copies, and truncated streams without including
any copyrighted game assets. Full PIC frame composition and rendering semantics
inside *Mission Critical* are still open questions.
