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

### Conditional targets versus raw RGN candidates

The RGN file describes **potential** click regions. It does not state whether
an object has been drawn or whether the original game's current interaction
logic will accept it. Drawing all of these boxes made future/unavailable
objects appear to be present, especially in the experimental widescreen UI.

The original Gateway 1 rectangle picker **also has a dynamic skip bit**:
in the pinned opening replay the routine at runtime `2649:0541` tests
`DS:[0xa920 + rectangle_index] & 0x80` before comparing the mouse position;
if set, that rectangle is skipped. The group counts are at `DS:a8d4`, the
35-entry-per-group rectangle-index lists at `DS:a974`, and the indexed native
x0/x1/y0/y1 arrays at `DS:a6d0`, `DS:a750`, `DS:a7d0`, and `DS:a850`.
Result-kind bytes are at `DS:aa4e`. These are **runtime addresses for this
launch layout**, not original EXE/overlay file offsets. The routine's
original file offset remains to be identified.

The debugger matches *all* room RGN rectangles against a single live native
group, including each rectangle's coordinates (native screen X = RGN X +
261), count, ordering, and result-kind byte `3`. Only when the entire group
matches does it annotate native `0x80` flags. A mismatch leaves every bit
`unknown`, avoiding guessed cross-title or cross-layout interpretation.
The default F10 rectangles and widescreen candidate list now exclude only
**proven skipped rectangles**, in addition to the existing heuristic
classifications. Clearing the `0x80` bit is **not** by itself proof of
visibility, an actual drawn object, or successful mouse selection.

Gateway 1 now samples the validated 734-object guest table and annotates
each RGN region with one of four *host-side* classifications:

| Classification | Meaning | Default F10 outline | Widescreen list |
| --- | --- | --- | --- |
| `room_candidate` | Direct/nested location chain reaches the player's enclosing type-1 room; supported property `0x07` is not known false | Show | List |
| `absent` | Object is detached, carried by the player, in another room, or within the known closed drawer `0x124` | Hide | Hide |
| `undisclosed` | Object is in the room but has supported native property `0x07` cleared | Hide | Hide |
| `unknown` | Missing/unsupported type, object, flag, or enclosing-room data | Show as unverified | Hide |

The player's object ID `0x0d3` can itself be nested inside another object
(for example, the VR couch); the room lookup follows parent locations with
cycle protection. Type-6 passage records can have two room endpoints, so
the second location word is considered before rejecting a direct-location
mismatch. Only the **known first-room drawer** has an additional
closed-container check; the generic child-slot predicate is a parser rule,
not an established renderer rule. Property `0x07` was observed being set
during room descriptions and when revealing the book, but its exact meaning
in all rooms remains unproven. Both `room_candidate` and `unknown` may still
contain objects that are not drawn. These classifications are diagnostic
filters, **not** a reconstructed native visibility or hotspot-picking routine.

Press **R** in the F10 menu to display **all** raw RGN candidates, including
filtered ones, for comparison. The original guest mouse input is unchanged.
Dumped `.regions.tsv` files retain every region and append a `presence`
column and a `native_picker` column (`disabled`, `not_disabled`, `unknown`);
`.debug.txt` identifies the classification as heuristic. For the other
Legend titles, all regions stay `unknown`, because the Gateway 1 object
state and native picker layouts have not been verified there.

The next verification should correlate these categories with native render
events and click-selection results in several rooms. In particular, test
objects already disclosed but subsequently hidden by story flags. Only such
traces can turn provisional room candidates into confirmed active hitboxes.

### Headless scene checkpoints

The probe's `--debug-scene` option enables its existing PIC/RGN scene tracker
even in a **headless** walkthrough. Ordinary memory dumps then also include
`*.regions.tsv`, `*.objects.tsv`, and `*.debug.txt`, with the live native object
names, presence classifications, and restricted parser-path annotations.
It does not alter the guest renderer or decide which hitboxes are clickable.

```sh
python3 tools/run_walkthrough.py run \
  tests/fixtures/gateway-opening.walk \
  --probe /tmp/gateway-port-build/gateway-probe \
  --data ../gateway-dos --output build/rgn-scenes \
  --probe-arg=--debug-scene \
  --probe-arg=--dump-at --probe-arg=16500:build/rgn-scenes/before \
  --probe-arg=--dump-at --probe-arg=20500:build/rgn-scenes/open \
  --probe-arg=--dump-at --probe-arg=26500:build/rgn-scenes/taken \
  --probe-arg=--dump-at --probe-arg=35000:build/rgn-scenes/closed
```

In the completed original opening replay, all four checkpoints follow
`GATE_102.PIC` / `GATE_002.RGN` entry 15. The room image changes as the drawer
opens and closes, and the book moves between drawer, player, and room, but
the 30 RGN rectangles and the current *heuristic* classifications are identical
at all four checkpoints. Code-only hooks at the rectangle decoder likewise
see the same 30 IDs on each redraw. This is concrete evidence that simply
loading RGN records does **not** apply the original conditional visibility
logic. The live native picker table, however, is **conditional**:

| Checkpoint | Native skip index | Disabled extracted rectangle | Drawer state |
| --- | --- | --- | --- |
| `before` | 55 | Object `0x124`, `(90,158)–(136,223)` | Closed |
| `open` | 54 | Object `0x124`, `(90,162)–(110,223)` | Open |
| `taken` | 54 | Object `0x124`, `(90,162)–(110,223)` | Open |
| `closed` | 55 | Object `0x124`, `(90,158)–(136,223)` | Closed |

All 30 extracted rectangles match the native table's bounds in each captured
1 MiB RAM image. Exactly one is marked skipped at each checkpoint. Thus the
two overlapping drawer rectangles **alternate**, rather than both being
active. To validate the correlation against retained original RAM snapshots,
run `GATEWAY_PICKER_RAM_DIR=build/rgn-scenes-v026
build/headless-local/debug_scene_tests ../gateway-dos` (replace the local
test-binary path as appropriate). This is an optional private-fixture test;
ordinary synthetic tests do not require original data.

The **original source of this alternation** is now identified as a
three-record conditional region table at `ovl:0x053daa (section 38)`. Two
far callbacks check the drawer's open property `0x0c` and disable opposite
slots; the third record conditionally disables the debit card when it is no
longer related to the desk. The shared evaluator is at
`ovl:0x002680 (section 0)`, and the picker itself maps to
`GATE.RTL:0x014186`. See
[the Your Quarters conditional-region analysis](room-your-quarters.md#native-conditional-mouse-region-rules)
for the byte layout, stable source offsets, proof, and the reusable
`tools/inspect_region_conditions.py` inspector. This establishes a general
conditional **picker** mechanism, not a general image-rendering predicate.
The inspector can also census the entire original overlay with `--scan-calls`
and a version-verified `--exe`: all 75 direct evaluator calls map to 49
overlay condition tables (416 records) and 17 resident single-record tables.
See [the complete call census](room-your-quarters.md#coverage-of-native-region-condition-callers)
for the exact provenance and command line. The data is suitable for building
a native conditional-hotspot model once the callbacks have executable
equivalents; it does **not** justify hiding rendered assets solely from the
rectangle table.

The separate **scene painting** mechanism has now been recovered as well:
`GATEMAIN.OVL:0x002827` updates a cached set of 14-byte conditional PIC
layers, using `ovl:0x00272c` to check their predicates. Its first-room table
drives the XOR drawer graphic independently of the hotbox table. See
[conditional picture layers](picture-conditions.md) for the 81-call census,
correlation with the original framebuffers, and the implications for a future
native scene model. The debugger's raw RGN overlay remains a diagnostic view,
not a proxy for this source picture-layer state.

Two short headless control replays scheduled a click at guest ms 18000,
one inside the drawer's extracted rectangle and one on the nearby background.
Both delivered the click to the emulated mouse state, but neither reached
the suspected indexed-object routine at `2649:1407` (runtime linear
`0x27897`); the guest's INT 33h position poll occurred before the clicks.
Therefore those tests **do not** establish the native click-selection ABI.
Find a guest execution path that actually consumes the click before using
mouse tests as an oracle for active hitboxes.

### Restricted parser-slot path (separate diagnostic)

The original resident parser at `GATEMAIN.EXE:0x0f11` optionally invokes
`GATEMAIN.EXE:0x53c4` for each child's indexed slot. That routine is
**byte-verified and traced** (see [parser candidate iterator](action-dispatch.md#resident-parser-candidate-iterator)),
but its caller does not always request the restriction. In particular,
this is **not** the native RGN hit-test or picture-rendering predicate.

Gateway 1 snapshots now also sample properties `0x0c`, `0x13`, `0x1f` and
`0x1c`. For an object with a validated room ancestry and a known native
child-slot byte, the debugger calls the existing reconstructed
`StoryModel::can_enumerate_child_slot` on each ancestor. The result is:

- `pass`: every known slot on the path to the room would pass the restricted
  candidate iterator;
- `blocked`: at least one known slot would be skipped if the restriction is
  enabled;
- `unknown`: missing flag/slot information, ambiguous ancestry, or a path
  outside the currently inferred room.

The result appears in an **additional** `restricted_parser_path` column of
`.regions.tsv` and as `[gate:pass]` or `[gate:blocked]` in the F10 hover label.
For example, the first-room book at native ID `0x123` changes from `blocked`
to `pass` when its enclosing drawer `0x124` is opened, if the original slot-0
conditions are met. This remains an independent indication: the default
hitbox overlay **does not** hide `blocked` objects on this evidence alone,
because an unrestricted parser traversal could still enumerate them and
the rendering rules are not yet recovered.

### Trace original RGN DOS transfers without altering guest execution

`--trace-rgn-dos` records each original DOS read from a `*.RGN` file, including
its file offset, destination guest address, size, caller CS:IP, and SS:SP.
The deprecated spelling `--trace-rgn-memory` is accepted for compatibility,
but now performs **only** this safe DOS-read observation. It never installs a
guest memory-read hook.

```sh
python3 tools/run_walkthrough.py run \
  tests/fixtures/gateway-opening.walk \
  --probe /tmp/gateway-port-build/gateway-probe \
  --data ../gateway-dos \
  --output build/rgn-memory-opening \
  --probe-arg=--trace-rgn-dos

python3 tools/summarize_rgn_memory_trace.py \
  build/rgn-memory-opening/native.trace --rgn-dir ../gateway-dos
```

The trace has `rgn-buffer` records for DOS transfers and a
`rgn-summary mode=dos-only` census. With `--rgn-dir`, each sampled file offset
is identified as a Gateway 1 header field or a rectangle with its numeric
object ID and room-local coordinates. This does **not** establish that the
object was drawn or selectable. For focused instruction traces, separately
use `--trace-entry` or `--extra-trace-entry` in a walkthrough; those are
ordinary Unicorn *code* hooks and do not touch the memory-hook subsystem.
All CS:IP values are only valid for the current guest load layout.

If a replay fails, `run_walkthrough.py` reports the last 20 probe stderr lines.
The complete diagnostics remain in `build/rgn-memory-opening/native.dos` and
stdout in `build/rgn-memory-opening/native.out`. The trace may be useful even
after a failed run, but a partial census is not proof of completed gameplay.

**Why memory-read tracing was withdrawn:** On Unicorn 2.1.4, dynamically
installed `UC_HOOK_MEM_READ` hooks reproducibly made the opening walkthrough
execute data as code and fail with an unsupported hardware port read from
`0x0000`; a no-op callback still failed. Even a statically installed narrow
no-op read hook caused invalid guest execution. With no RGN read hooks the
same 39-second replay passes. Furthermore, the old trace's supposed later
RGN consumers at `4319:1941/1942` actually disassemble to `POP BP; LRET`:
they were reading *stack* values at addresses previously used for DOS RGN
transfers, not the debit-card rectangle. The `2649:13ad` site likewise reads
`[BP-6]`. Existing `rgn-guest-read` data from earlier probes is retained for
audit only and must **not** be used as evidence for hitbox activation.

The DOS-transfer trace is safe and directly observed. It tells us where the
file records enter guest memory, not where the game later copies, renders, or
tests them. Finding those later paths requires instruction disassembly and
controlled event traces.

In a completed 39,000-ms opening walkthrough under Unicorn 2.1.4, this
safe trace recorded 279 `GATE_002.RGN` DOS reads at caller `219e:004c`,
all of which returned normally. The same native replay also passed with
the deprecated alias. An additional *code-only* trace at `2649:13ad/13b4`
captured live six-byte rectangle records without altering the walkthrough.
Those two instructions load a record's object ID and packed coordinate byte
from BP-relative locals; they still do not establish a visibility condition.

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

### Native object-name lookup across games

Gateway 1's RGN IDs differ from sequential `OBJECT.DAT` string indexes (the
drawer counterexample above). The other games can have the same ambiguity.
Spellcasting 201, Spellcasting 301, and Gateway II include `OBJECT.DAT`. Their F10
debugger now searches conventional guest memory for a six-byte native object
directory (`type, reserved, record offset, record segment`) independent of DS.
It requires the first object's primary name token to point at the first string
and verifies at least 256 consecutive typed records, far pointers and primary/
secondary tokens against exact `OBJECT.DAT` NUL-delimited string boundaries.
At least three-quarters of these records must have valid names; the table must
also cover all IDs in the selected hitbox frame. A successful candidate is
revalidated on each refresh, with its address retained as a search hint.

**Gateway II is now confirmed against a live 1 MiB guest snapshot** taken at
guest time 49329 ms. The object directory begins at linear `0x38b1d`, an odd
address; our initial even-only scanner silently missed it. The corrected scan
decodes all **521 objects** with valid token boundaries in the original
`gateway2-dos/OBJECT.DAT` resource. Among the checked mappings, native ID 7 is
`ground`, 377 is `windows`, 385 is `stair door`, and 520 is `laser rifle`.
The regression test uses that snapshot only when explicitly supplied as an
external fixture, without adding the proprietary game files to the repository.

Whether Spellcasting 201 and 301 share the exact directory layout is still
a **hypothesis** supported only by synthetic tests; their live memory needs
checking. The corrected bytewise scanner handles both even and odd starts.
If a game uses a different layout, F10 continues showing
object IDs and allows a D snapshot for analysis. A successful lookup exports
the ID/name mapping in `.names.tsv` and its directory's linear address in
`.debug.txt`. Object-name labels should still be visually checked against the
game's current scene. Spellcasting 101 lacks `OBJECT.DAT` entirely, so
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
