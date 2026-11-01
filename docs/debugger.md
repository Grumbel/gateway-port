# Gateway 1 F10 debugger: evidence, interpretation, limits

This debugger is deliberately observational. The emulator continues running
the original Gateway executable and its native story handlers. F10 is handled
by SDL and never queued as an INT 16h key or IRQ1 scan code. The drawn menu and
region outlines exist only in a host-side copy of the framebuffer.

## Region pictures and names

`GATE_*.PIC` has a directory of image entries. The related `GATE_*.RGN`
file contains a six-byte record for every PIC entry:

| Offset | Type | Known meaning |
| --- | --- | --- |
| +0 | u16 | Offset of this entry's rectangle list in the RGN file |
| +2 | u16 | Default native engine object ID |
| +4 | u16 | Number of rectangles |

Each six-byte rectangle is a native object ID followed by two packed u16 coordinates.
The low coordinate byte is the X value in two-pixel units and the high byte is
Y in pixels. The debug renderer doubles X and adds 260, since the 640-pixel
Gateway layout's room picture starts at column 260. These boundaries have
been checked against supplied files, but their interaction and hotspot
priority semantics have **not** been checked against executable instructions.

The RGN value is an ID in Gateway's live object table (1–734). Each object's
record holds a one-based name **byte offset** into `OBJECT.DAT`. The file's
separate zero-based NUL-string enumeration is *not* indexed by RGN numbers.
For example, hotspot `292` is the drawer in Gateway's first room, while
`OBJECT.DAT` list index 292 is "slip of paper". Labels appear when the debugger
recognizes the native table; otherwise it displays only the numeric engine ID.

The host rasterizer merges adjacent/overlapping rectangles with the same ID,
draws only the outside contour, and applies a light translucent fill. Different
IDs get distinct stable hues. Hover brightens one object's outline and fill;
anonymous ID-0 rectangles stay separate because their shared zero does not
identify one object. The game framebuffer is never changed. The debug menu
starts in the lower-right text pane and can be dragged by its title bar.

For the Gateway VGA family, `GATE_1xx.PIC` corresponds to the
`GATE_0xx.RGN` room data. The debugger registers a pair when DOS opens a
PIC and subsequently notices reads within a non-XOR room-sized frame. This
is a heuristic, not a recovered engine "current room" property. PIC loads
may be prefetches; scenes with XOR layers, cached images, or mode changes
may not line up with this estimate. A and Left/Right let the tester override
it manually and compare candidate RGN entries to the visible picture.

## Live object state

The observer uses the Gateway-specific DS-relative object structures
recovered in [object-state.md](object-state.md) and
[engine-boundary.md](engine-boundary.md):

- `[DS:8d54]` points to the segment with the object count at `38dc`.
- `[DS:8d50]` points to the object directory (type + record far pointer).
- `[DS:8d52]` points to the type descriptor table (record lengths).
- Native records store name-token byte offsets, object-type-specific handler
  indices, and, for types 2/6/7/8, direct locations.
- The book's verified engine ID is `0x123`; the drawer's is `0x124`.
  The debugger shows the book's direct location and the drawer's `0x0c`
  Boolean flag if a valid 734-object table can be found.

The saved `.objects.tsv` retains the original raw record bytes. The decoded
fields are intended as research leads, not complete semantic state: global
story flags, parser state, timers, event queues, graphics selection, and
overlay-loader state remain largely unidentified. `.regs` and `.bin` allow
offline disassembly and before/after comparisons while guest code is intact.
The clean `.ppm` image can be compared with DOSBox or an eventual native
renderer without host overlays or cursor artifacts.

## Research exercise toward a native engine

1. Start Gateway and take a snapshot of **Your Quarters** with F10 → D.
2. Click a known room object, or type `look`, `open drawer`, `take book`.
3. Take another snapshot; compare `.objects.tsv` and the raw `.bin` changes.
   Use engine IDs from RGN and native records, not OBJECT.DAT string indexes.
4. Manually select the RGN frame corresponding to the picture and check
   hotspot boundaries, then compare with the actual guest click behavior.
5. Correlate a changed object handler with the original RTLink overlay file
   offset using [the handler inventory](object-state.md). Map calls returning
   to shared parser/renderer services before designing a replacement model.

This currently provides instrumentation for the native executable, not a
replacement interpreter or enhanced presentation. A future native host can
use a scalable UI, configurable fonts, and upscaled pictures only after its
story and object-state semantics are known well enough to preserve gameplay.
