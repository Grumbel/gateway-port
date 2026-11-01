# Mission Critical PIC research

Mission Critical (1995) uses a different generation of Legend's PIC format.
These findings were checked against four external samples: `MC040.PIC`,
`MC041.PIC`, `MC051.PIC`, and `MC101.PIC`. They are not part of the repository.
No `MISSION.EXE` or decompressed reference images were available for testing.

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

Entries with `flags == ff00h` carry a palette update at their payload offset:

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
            + (flags == 0xff00 ? 4 + palette_count : 0)
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

`MC101.PIC` entries 1–20 have flags `0003h` and last word `0002h`, often at
the same position and dimensions. They resemble animation or layered image
records, but their exact rendering semantics remain **unverified**.

## Compression and extracting streams

The native PKWARE DCL decoder used for Gateway cannot decode these pixels.
[QUFB's Mission Critical research](https://qufb.gitlab.io/writeups/2021/01/22/Decrushing-DOS-Doodads)
reports that `MISSION.EXE` identifies the **Crusher! Data Compression Toolkit
Version 3.0** and demonstrates successful decompression. The researcher's
[crusher_cli](https://gitlab.com/qufb/crusher_cli) calls the proprietary
32-bit `CrusherS.dll` through Wine; it is a wrapper, **not** a portable
native decoder. The DLL is neither distributed nor linked by this project.

`gateway-extract` detects this directory automatically or accepts
`--dialect mc`. Without a Crusher implementation it reports unsupported
compression instead of guessing at another codec. To extract the known parts:

```sh
gateway-extract --raw-chunks -o /tmp/mc-pics ../MissionCritical/*.PIC
```

This writes per-file directories containing:

- `NNN.crusher`: exactly the compressed byte range from an image entry;
- `NNN.pal`: a 768-byte snapshot of the current 6-bit VGA RGB palette when
  a palette is known, including updates from earlier entries or input files;
- `index.tsv`: decimal directory fields, palette range and compressed size.

With the four samples, **55 streams** export successfully. These are not
viewable PNGs: decompression, transparency/masks, palette application, and
composition still require validation. Synthetic unit tests cover bounds,
palette ranges, offsets, and truncated files without including copyrighted
game assets. A portable Crusher decoder or disassembly/reference data from
`MISSION.EXE` is the next prerequisite for producing images.
