# Legend engine F10 debugger: evidence, interpretation, limits

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

The host rasterizer merges adjacent/overlapping regions with the same ID,
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

## Other Legend games: format verification, runtime still unconfirmed

The PIC-read tracking and F10 overlay now support the four additional titles
with original game data in `games.zip`:

| Game | Room PIC → region file | Region encoding |
| --- | --- | --- |
| Spellcasting 101 | `S101_E1.PIC`/`S101_C1.PIC` → `S101_R1.RGN`, etc. | Packed rectangles, two-pixel X units |
| Spellcasting 201 | `S201_E1.PIC`/`S201_C1.PIC` → `S201_R1.RGN`, etc. | Packed rectangles, two-pixel X units |
| Spellcasting 301 | `S301_102.PIC` → `S301_002.RGN`, etc. | Gateway-compatible packed rectangles |
| Gateway II: Homeworld | `HOME_102.PIC` → `HOME_002.RGN`; `HOME_304.PIC` → `HOME_304.RGN` | Polygons (see below) |

Older Spellcasting RGNs have fixed-size header tables that need not match the
corresponding PIC's directory: S101 uses 30–80 slots depending on the file,
and S201 uses a 128-slot table even when the PIC has fewer entries. A bounded
header scan prevents interpreting the rectangle data as extra headers; a few
S201 inverted/sentinel rectangles are discarded while keeping valid shapes in
the same frame. The PIC-to-RGN frame indexes are otherwise preserved.

**Gateway II differs materially.** Its six-byte *frame headers* have the same
`offset, default ID, count` layout, but the count denotes polygon records:

| Field | Type | Meaning |
| --- | --- | --- |
| +0 | u16 | Object ID, or 0 to use frame default |
| +2 | u16 | Vertex count |
| +4 onward | repeated `(u16 x, u16 y)` | Ordered scene-local vertices, in single-pixel units |

Polygons with two vertices represent rectangular opposite corners. Other
polygons are filled using the even-odd rule, retaining concavities; the final
ownership buffer still merges regions of the same ID into one contour. All
nine supplied Gateway II region files parse completely, with no unexplained
trailing polygon bytes. A debug `.regions.tsv` dump includes ordered polygon
vertices in a semicolon-delimited last column; rectangle bounds remain in the
existing columns.

The focused test suite checked **41 real PIC/RGN pairs**, covering both color
variants of the two early Spellcasting games, the S301 room series, and all
the available Gateway II region files. Reads selected 375 room-sized PIC
frames and exposed 678 polygons. This establishes binary layout and frame
association, **not** that the guest has selected the same frame now visible on
screen. Live F10 checks are still needed for scene origin, visibility/clipping,
actual mouse-selection priority and object labels in the other titles.
The Gateway 1 DS offsets and handler-layout assumptions are intentionally not
applied to them.

### Experimental native object-name lookup

Gateway 1's RGN IDs differ from sequential `OBJECT.DAT` string indexes (the
drawer counterexample above). The other games can have the same ambiguity.
Spellcasting 201, Spellcasting 301, and Gateway II include `OBJECT.DAT`, but
their live object-directory addresses have not yet been recovered. Their F10
debugger now searches conventional guest memory for a six-byte native object
directory (`type, reserved, record offset, record segment`) independent of DS.
It requires the first object's primary name token to point at the first string
and verifies at least 256 consecutive typed records, far pointers and primary/
secondary tokens against exact `OBJECT.DAT` NUL-delimited string boundaries.
At least three-quarters of these records must have valid names; the table must
also cover all IDs in the selected hitbox frame. A successful candidate is
revalidated on each refresh, with its address retained as a search hint.

This is a **hypothesis about shared native record layouts**, supported by
synthetic tests, not a verified discovery in each game's runtime memory. It
may fail to identify a different layout; in that case F10 continues showing
object IDs and allows a D snapshot for analysis. A successful lookup exports
the ID/name mapping in `.names.tsv` and its directory's linear address in
`.debug.txt`. The mapping must be visually checked against the game before
being treated as confirmed. Spellcasting 101 lacks `OBJECT.DAT` entirely, so
the new lookup does not apply to it; its embedded object-name storage remains
to be located.

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
