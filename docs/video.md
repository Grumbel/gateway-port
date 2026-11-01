# VGA host model

Gateway's intro executable detects VGA with BIOS `INT 10h AX=1a00`, queries the
current mode with `AH=0fh`, then selects mode `12h`: 640×480, sixteen colors.
The pure C++ `Vga` model stores four 64-KiB planes independently from Unicorn
memory and converts the displayed pixels to opaque ARGB8888 for the SDL host.

The graphics subset implements sequencer plane masks, graphics-controller
set/reset and raster operations, read-map selection, color comparison,
write modes 0–3, DAC palette streams, attribute palette selection, and CRTC
start/pitch registers. Chain-four addressing is available for mode 13h tests;
that mode's full BIOS palette and timing are not yet modeled.

The status port uses synthetic alternating retrace states. It prevents polling
loops from waiting forever but is not a measured hardware timing model.
Authentic VGA ROM fonts, arbitrary CRTC timings, odd/even memory addressing, and
hardware video interrupts remain outside this subset.

Unit tests exercise independent plane/color output, pitch, latch copies,
set/reset masks, read comparison, write mode 3, DAC access, display offsets, and
chain-four addressing. These tests verify the model's contracts; comparisons
with original DOSBox captures are still required for game-output accuracy.

The `gateway` SDL runtime updates an ARGB8888 texture from these pixels and queues
stereo PCM to an SDL audio stream. The intro's initial Legend title text has been
observed in a bounded capture. A synthetic guest integration test exercises plane
writes, SDL rendering/audio submission, and the final framebuffer snapshot.

## Text-mode return from the intro

Mode `03h` has eight 4-KiB text pages with 80×25 character/attribute cells.
BIOS `AH=09h` stores repeated characters and attributes without advancing the
cursor; `AH=08h` reads a cell, and `AH=05h` selects the displayed page. Switching
to text mode resets cursor/page state and the associated BIOS data-area fields.
Direct guest writes to `B800:0000` and BIOS writes share the same guest memory;
the SDL/dump path snapshots those cells before rendering.

The renderer uses 8×16 cells (640×400), with the existing external game-font
fallback scaled from 8×8. Foreground/background attributes are supported; blink
is shown steadily, not interpreted as graphics XOR. This is not a pixel-exact
9-dot VGA ROM font, and a visible hardware cursor is not yet rendered.

Regression fixtures cover the reported mode-12h → mode-03h → cursor query →
character write sequence, page selection, repeated writes across a row boundary,
and direct text-memory writes.

## BIOS font fallback without fixed game fonts

Gateway II supplies proportional `HOME_201.FNT` and other fonts, but no fixed
`HOME_000/100/200.FNT`. A failed `HOME_200.FNT` open precedes BIOS character
output. The runtime now uses the vendored public-domain font8x8 basic Latin
font when no fixed game font is available. Rows are reversed to VGA bit order
and scaled to the selected cell height; available fixed game fonts keep their
existing priority. Extended CP437 characters and exact native VGA ROM shapes
remain unsupported. Attribution is in `third_party/font8x8/`.

On Linux (2026-10-31), this 22-second headless check passed the reported font
failure, loaded `HOMEMAIN.EXE`, and captured the Living Room with readable menus
and opening description:

```sh
./build/gateway ../gateway2-dos/HOME.EXE --headless --press escape \
  --milliseconds 22000 --state-dir build/home-font-state \
  --dump-prefix build/home-font
```

All 17 CTest targets pass, including synthetic BIOS glyph checks without game
fonts in modes 10h and 12h. Interactive input and audible sound remain unverified.

## VGA split-screen display addressing

The CRTC line-compare register (`18h`, extended by `07h` bit 4 and `09h`
bit 6) resets the display memory address after the matching scanline. This
permits a fixed lower display area while the top region uses a nonzero page
start. The renderer now applies that address reset in planar and mode-13h
graphics rather than applying the display start to every row. The initial
line compare is set above the visible frame to leave splitting disabled.

`--trace-video` includes writes to these split-screen registers alongside the
existing CRTC start, pitch and addressing-mode traces. Real-world validation
against the Spellcasting 101/201 intermittent offset remains necessary.

## BIOS graphics page allocation

The 201 `--trace-video` log shows that the picture changes only on BIOS
`AH=05h` page changes (0 to 1 and back), without guest CRTC register writes.
Mode `10h` uses a 32 KiB page allocation even though the visible 640x350
16-color image occupies only 28,000 bytes per plane. The earlier implementation
used the visible size as the page stride, causing a 4,768-byte displacement
whenever page 1 was selected. BIOS page sizes are now mode-specific:

| Mode | Page allocation |
| --- | ---: |
| 03h | 4 KiB |
| 0Dh | 8 KiB |
| 0Eh | 16 KiB |
| 10h | 32 KiB |
| 12h, 13h | 64 KiB |

The BIOS data area's page-size and page-start values are also updated on mode
and page changes. Unit tests verify that mode 10h page 1 starts at 32 KiB.

## BIOS graphics text and page flipping

`INT 10h AH=09h` uses `BH` to select the page receiving the glyph; that page
may be different from the one currently displayed by the CRTC. Previously,
the software glyph renderer used the current display start instead and could
draw character pixels onto the wrong buffer. The destination now uses the
mode's BIOS page allocation and `BH`, independently of the scanout address.
Tests draw on hidden and visible mode-10h pages and verify isolation. This
could explain intermittently missing interface text when a game flips pages;
live verification is still required.
