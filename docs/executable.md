# Executable observations

These findings refer only to the provided `../gateway-dos/` distribution.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `GATE.EXE` | 22968 | `767ccbbacbd6b304a5bab072894a45ca426a2196ae369b2f11d02ef92bb7134b` |
| `GATEMAIN.EXE` | 155402 | `1785bf60f196836c0542dafa4a00085f6b1f3837992f2cc14da12503bc02abcb` |
| `GATEMAIN.OVL` | 497763 | `05e14a53904c356cba5f9fbbc9de98cf8fb1a5bdb3b6bcbeff2ae97482982d71` |
| `GATE.RTL` | 109193 | `c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566` |

Both executables have MZ signatures, 32-byte headers, zero MZ relocation entries,
and zero overlay numbers. `GATEWAY.BAT` invokes `GATE VGA BLASTER 7 220`.

| Executable | Load image bytes | Relative CS:IP | Relative SS:SP | Minimum extra paragraphs |
| --- | ---: | --- | --- | ---: |
| `GATE.EXE` | 22936 | `002c:0071` | `2229:0400` | 7887 |
| `GATEMAIN.EXE` | 155370 | `005e:0071` | `5911:0400` | 13666 |

Static entry disassembly begins with a near call, followed by a far indirect jump
through `CS:0004`. The called startup requests DOS `INT 21h/AH=35h` for vector 3,
then inspects memory associated with that vector. Both binaries contain the
strings `.RTLink Startup Code`, `rtlinkst.com`, and RTL loading errors. This
supports investigating RTLink's module loader before interpreting game code.
Zero MZ relocations do not establish that later module relocation is absent.

With the probe's initially zero interrupt vector table, both originals request
`AX=3503`, `AX=30f0`, and `AX=1900` before further loader work. These are observed
requests, not a validated DOS implementation. The loader initializes only a
minimal PSP; environment, DOS allocation metadata, and hardware state remain
incomplete. Guest execution is currently separate from the SDL diagnostic.

## Current startup boundary

The probe maps conventional memory through segment `a000`, records that limit
in PSP offset `0002`, and initializes BIOS memory size at `0040:0013` to 640 KiB.
Without the PSP field RTLink reports `est0002` (insufficient free memory).

With these fields, case-insensitive root-directory file access, DOS vector/version
queries, and file read/seek/close services, `GATE.EXE` loads `RTLINKST.COM`, opens
`GATE.RTL`, and reaches `INT 21h AX=4aff` at `12a5:0a10` (post-interrupt IP).
This DOS memory-block resizing request (`AH=4ah`) is now implemented through
[the conventional-memory arena](dos-memory.md). After resizing, startup obtains
DOS version and vector 0, installs its divide-error handler, and reaches device
information (`AX=4400`) at `12a5:0b1e`. Handle attributes and DOS date/time
queries are now implemented. Startup reaches VGA display-combination detection
(`INT 10h AX=1a00`) at `242a:0076`, selects mode `12h`, installs break/error/timer
vectors, and allocates two DOS blocks. BIOS mode state and VGA plane accesses
are implemented. The AdLib register path is now implemented with Nuked OPL3 synthesis. Startup
then queries DOS time and programs the PIT. The timer bridge, virtual calendar,
and BIOS/DOS keyboard input now support bounded intro playback. Default headless
execution is limited to ten million instructions and two seconds of wall time;
`--milliseconds` explicitly selects a longer bounded virtual-time run.

## Intro and main-program startup

The SDL runtime can run bounded intro sequences and forward BIOS/DOS keyboard
input. The intro gets through picture/music loading and displays the initial
Legend text. `--press escape` is a deterministic startup-input probe; `--game-args`
provides a DOS PSP command tail (with the conventional leading space).

Direct `GATEMAIN.EXE` execution with friendly launcher options exits with DOS
status 1. The numeric handoff recovered below allows it to continue into game
data loading. Parent EXEC is still incomplete. Cursor-position BIOS services, console output, and standard-handle
cleanup are implemented so startup diagnostics can proceed.

## Recovered launcher arguments

A breakpoint at linear `1026a` in the supplied intro, immediately before the
far call at `1000:026a`, exposes its spawn arguments on `SS:SP`. With default
settings the argument vector is:

```
GATEMAIN.EXE 0 258 9 2 816 1 7 544
```

These are observed numeric initialization fields, not yet a fully decoded API.
Passing them through `--game-args` allows direct main-program execution to reach
`VOCAB.DAT` loading and DOS file-attribute query `AX=4300`. Friendly strings such
as `VGA ADLIB` are not equivalent to this internal handoff.

To reproduce the launcher snapshot:

```sh
./build/gateway-probe ../gateway-dos/GATE.EXE --press escape \
  --break-at 1026a --milliseconds 12000 --dump-prefix build/spawn
```

Snapshots include `.regs` alongside `.bin` and `.ppm`. The PSP now names a tracked
DOS environment block with `PATH=C:\` and the executable filename. BIOS loading
text uses the original game's fixed 8×8 font scaled to the graphics mode's cell
height; this is a fallback, not an exact replacement for the VGA ROM font.

## Current main-program boundary

The shared case-insensitive file resolver now handles read-only DOS file
attributes as well as opens. With the recovered numeric arguments, main startup
reads vocabulary and other tables, then requests write-only access to
`RESTART.DAT` (`AX=3d01`). Missing files now return DOS error 2, including for
write-only and read/write opens. Main startup handles that failure and calls
file creation (`AX=3ca4`, `CX=0`, at `1805:24f4`). This is the current stop;
creation needs a separate writable state directory. Existing original files
return access denied for writable opens and remain unchanged.

## Intro return to text mode

The intro may restore mode `03h` while reporting a launch failure. Its cursor
queries and `AH=09h` character writes now update text cells and render normally;
this sequence previously raised `unsupported BIOS glyph mode`. Restoring text
mode also resets the cursor and BIOS text geometry instead of reusing graphics
cursor positions.

A replay with `--press escape --milliseconds 18000` now reads the main executable,
prepares DOS memory for the handoff, and stops at `INT 21h AH=52h` (DOS list of
lists, `12a5:42c6`). This separate launcher boundary is still unsupported. It is
independent of the direct main-program `RESTART.DAT` creation boundary above.
