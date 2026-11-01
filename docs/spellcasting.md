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

The next logs show 101 reaching `INT 10h AX=1130h` (BIOS font pointer query)
with `BH=0`. The runtime now exposes its initialized fallback font through
guest-visible 8x8, 8x14, and 8x16 tables and returns a pointer, character
height, and screen row count. In 201, the next failing write was to `236h`,
the DSP reset register for a card at base `230h`. The absent-card model now
covers the successive 16-port base ranges `210h` through `260h`, instead of
only `220h`. The actual game startup beyond these points is unverified.

In the next trace, 101 reaches `INT 10h AX=1002h` after switching to graphics
mode `10h`. The runtime now loads all 16 VGA attribute palette entries and
the overscan color from the 17-byte `ES:DX` table. 201 reaches `INT 10h
AH=05h AL=01h` in the same graphics mode; BIOS page selection now updates
the planar display start address and the BIOS data area for graphics pages,
not only text pages. Unit checks cover palette mapping and page visibility.

The intro trace for 101 subsequently stops on `unsupported PIT counter mode`.
The PIT now accepts the standard binary counter modes 0–5, including one-shot
and software-strobe counter expiration, instead of accepting only periodic
modes 2/3. Trigger semantics for hardware-triggered modes 1/5 remain a
simplification; audio and intro timing still require live comparison.

A user screenshot of the 201 intro shows a vertically split/misaligned planar
picture after graphics page switching. The renderer previously treated VGA
CRTC display-start registers as *byte* addresses, while planar graphics modes
normally use *word* addresses. The rendered offset and BIOS page-start
programming now use consistent 2-byte units. This is a concrete addressing
bug fix, but visual confirmation in the complete runtime is still pending.

The subsequent 101 loading screen reaches BIOS `INT 10h AH=06h, AL=00h`,
requesting a full 80x25 clear using the specified text attribute. The runtime
now implements text-window clearing and scrolling for `AH=06h`/`07h` against
the active page's guest `B800:` memory; tests cover full clears, directional
scrolling and preserving cells outside the requested rectangle.

After skipping the 101 title with Escape, a later trace reaches the same
`INT 10h AH=06h, AL=00h` service in EGA mode `10h`, after switching to
graphics page 1. The BIOS handler now scrolls and clears planar graphics in
character-cell rectangles on the selected graphics page as well. Focused VGA
tests exercise full-page clearing and both scroll directions, including
preservation of the other graphics page. Gameplay past this call remains
unverified.

### Spellcasting 101 title playback

The title screen installs an IRQ0 handler at `140E:00E5` and waits in
`140E:005F` for its `DX` completion flag. The handler advances speaker data
and eventually restores the normal handler at `140E:0117`, which sets bit 0
of `DX`. Its initialization at `140E:0148` programs PIT channel 0 in mode 2
with divisor 72: approximately 16,572 IRQs per second. Previously the host
advanced emulated time 1 ms at a time and delivered at most one IRQ per step,
so playback ran at a fraction of its intended rate. The scheduler now slices
guest execution at timer edges and preserves millisecond-granularity CLI and
SDL presentation schedules. A PIT test checks 165–166 distinct interrupts
in 10 ms. End-to-end title playback still requires verification.

The subsequent runtime report confirmed that the title finishes, but sound
is slow/distorted and keys do not interrupt playback. The title installs a
raw IRQ1 handler at `140E:0125` and reads scan codes from keyboard port
`60h`; forwarding keys solely through BIOS `INT 16h` cannot reach it. The
runtime now forwards queued keys to a installed IRQ1 handler, supplying the
scan code at port `60h`, while leaving BIOS-only input unchanged when no
IRQ1 handler exists. SDL event polling is limited to once per emulated
millisecond rather than once per high-frequency timer edge. PC-speaker
sampling also keeps its sample phase across short PIT execution slices,
tested by comparing a 16-ms bulk render against 60-us steps. Audio quality,
keypress response, and real-time execution speed need live verification.

The 101 shutdown path later invokes `INT 10h AX=0002h`, switching to BIOS
mode 2 (80x25 black-and-white text). This is the same `B800:` text-memory
layout as color mode 3, not an unsupported graphics mode. The runtime now
supports mode 2 throughout BIOS text operations, video-memory snapshots,
and SDL presentation. Tests cover switching to mode 2 and restoring the
text display on exit. Distorted intro audio remains unresolved.

### PC-speaker playback quality

The 101 title uses PIT channel 2 as a one-shot pulse-width DAC, updating its
width from a roughly 16.6-kHz IRQ0 stream. Sampling the speaker's instantaneous
logic level only once per 48-kHz output frame aliases short pulses into harsh,
incorrect amplitudes. The host now integrates the speaker's high/low output
over individual 1.193-MHz PIT ticks, accumulating an average for each 48-kHz
sample. Fractional phase persists across timer slices; tests compare bulk and
fine-grained renders and confirm that a sub-sample pulse yields an intermediate
amplitude. This models pulse-width averaging, not full physical speaker
response. Live listening remains necessary.

To distinguish slow guest emulation from bad audio synthesis, run 101 with
`--trace-timing`. Once per guest second, the runtime prints `TIMING guest_ms`,
`wall_ms`, and an effective `speed` ratio. A sustained ratio below `1.00x`
means the emulator cannot provide audio as quickly as SDL consumes it.

The next report shows the title/intro is still rough and takes over a minute,
whereas DOSBox plays it in roughly 15 seconds. Measured emulation speed falls
from `0.95x` to `0.76x`, which alone cannot account for the whole duration
difference. The timing trace now also reports cumulative IRQ0 deliveries,
hardware PIT edges, and speaker-channel programming writes, all in decimal.
Compare `irq0` and `pit_edges` during the title sound to determine whether
timer events are being coalesced or the guest sound driver is itself advancing
too slowly. The audio heard at the initial Legend logo is also missing and
requires separate sound-device investigation.

### Sound Blaster startup configuration

The reference DOSBox startup executes `SB-SOUND.COM` before `S101.EXE`.
The game's bundled `READ.ME` recommends this driver for AdLib-compatible
music on a Sound Blaster. The COM binary is a resident implementation of the
AdLib `SOUND.COM` interface: it contains the 24-byte signature
`51 01 "SOUND-DRIVER-AD-LIB" 01 01 00` immediately before its entry point.
The interface is installed on interrupt `65h`, with a function selected by
`SI` and arguments at `ES:BX`. It is distinct from the Sound Blaster DSP;
music is ultimately played through the OPL FM synthesizer. The original 101
manual distinguishes the AdLib/MT-32 music backend from the PC-speaker
RealSound effects and fallback title track.

The single-executable port does not run DOS TSR programs before the game.
Use `--adlib-driver --trace-research /tmp/s101-adlib.log` to advertise a
compatible installation signature and record `adlib` calls with function
number and the first 16 argument bytes. `--probe-adlib` remains an alias for
existing trace scripts. This option is still opt-in: the emulator provides a
high-level music driver instead of executing the original DOS TSR.

The first probe trace shows 101 invoking functions `05h`, `00h`, `17h`,
`0Ah`, `06h`, `12h`, `02h`, `09h`, `0Ch`, `10h`, `03h`, and then repeatedly
`10h`/`04h`. The documented `04h` GetState function returns status in `AX`
(`0` means playback finished), not in the `ES:BX` argument buffer. The
initial discovery stub failed to set `AX`, leaving the game's polling loop
unable to observe completion. That early probe returned idle status to expose
the next part of the game's musical call sequence.

The next trace still repeats `SI=10h` (SetTimbre) and `SI=04h` (GetState)
over nine thousand times. Disassembly of the bundled 7123-byte `SB-SOUND.COM`
at resident offset `0FD0h` shows why: SetTimbre writes a success flag into
the saved `AL` slot before returning (`FFh` after accepting an event,
`00h` before acceptance). A probe that leaves `AL` unchanged is interpreted
as a permanently full event buffer. Returning `AL=FFh` for SetTimbre,
preserving `AH`, exposed the remainder of the score: 1,096 `0Eh` (PlayNoteDel)
calls, 1,045 active-voice selections, and smaller numbers of `08h` volume,
`10h` timbre, and `11h` pitch events. This confirms that 101 supplies music
through the AdLib driver, not a Sound Blaster DSP sample stream.

The current `--adlib-driver` queues these events in guest beat time (the TSR's
controller timestamps are relative to `RelTimeStart`, but delayed note
timestamps are absolute), handles tempo changes, translates 56-byte AdLib INS
timbres to OPL2 registers, and
synthesizes notes through the existing Nuked OPL backend. It includes the
driver's 11 voices with percussion-mode mapping and returns queue acceptance
in `AL`. The final diagnostic reports the number of synthesized notes and
remaining events. A synthetic regression test checks event timing and a
non-silent FM waveform, but **actual 101 intro playback still needs a DOSBox
listening comparison**; pitch, volume and rhythm subtleties are not yet
validated against the real TSR.

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
