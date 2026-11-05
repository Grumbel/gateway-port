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
launch layout**, not original EXE/overlay file offsets. The picker routine
itself was identified at `GATE.RTL:0x14186`.

The debugger matches *all* room RGN rectangles against a single live native
group, including each rectangle's coordinates (native screen X = RGN X +
261), count, ordering, and result-kind byte `3`. Only when the entire group
matches does it annotate native `0x80` flags. A mismatch leaves every bit
`unknown`, avoiding guessed cross-title or cross-layout interpretation.
The native `0x80` bit takes precedence over heuristic object presence when a
complete room-group match is available: skipped rectangles are hidden, and
unskipped rectangles remain shown even when our provisional object-location
classification says `absent` or `undisclosed`. Otherwise the heuristic
classifications are used. Clearing the `0x80` bit is **not** by itself proof
of visibility, a drawn object, or successful mouse selection.

The mouse picker has **five rectangle groups**, not just the room-picture
RGN group. In all four retained opening-room snapshots their row counts are
15, 31, 1, 5, and 5. Their first rows are **selectable fallback rectangles**,
not merely group bounds: the original routine tests *every* row in order,
including row zero, then retains the **last matching enabled** row. All five
fallback rectangles have large bounds, some with intentionally negative
signed 16-bit starting coordinates. They explain why the command panel,
picture background, text panel, and scrollbar backgrounds all respond to
mouse clicks. The remaining rows are 30 room-picture RGN rectangles and
**22 non-RGN UI controls**, of which 18 are currently enabled.

The normal **H** hitbox view shows **scene objects only**. When a live native
picker group has been fully matched to the room's RGN rectangles, it uses the
actual room slots and their dynamic `0x80` skip bits. Within that group, the
last enabled rectangle covering a pixel wins; *adjacent slots belonging to the
same engine object are merged* into one colored contour. The overlay stays
inside the room picture (X >= 261, Y < 232). GUI buttons, scrollbars and the
five broad slot-zero fallback rectangles are never highlighted by H, even
where a non-scene picker row overlaps the picture. These native-derived room
outlines are still **predictions of potential scene selection**, not proof that
a later game handler accepted the click.
To inspect the **actual result of an original picker call** while running
Gateway 1, start the SDL runtime with `--debug-native-picker`:

```sh
gateway-run ../gateway-dos/GATE.EXE --debug-native-picker
```

Press **F10** (or **H** to leave the hitbox map visible). A separate, left-side
`Native pick` panel records the **last completed native picker invocation**:
the guest's call coordinates and timestamp, its actual AX/group/slot/kind
return values, the model's independent prediction, and `MATCH`, `MISMATCH`, or
`UNMODELED` when the native rectangle table cannot be decoded. A small green,
red or yellow cross marks the original call's position for five seconds; it
does **not** follow the mouse cursor. The result itself stays visible until the
next call. `Capture ERROR` means the hook could not read a valid guest result.
This diagnostic reads guest state only, and does not modify input or picking.
It observes the bounded `GATE.RTL:0x14186..0x14244` routine at the physical
addresses recovered from the known Gateway 1 launch layout; do not enable it
for other Legend titles or different load layouts. It does **not** prove that
the later handler dispatched or accepted an action. Without the opt-in flag,
the H room hitbox map remains a prediction only.
The panel also identifies the **original returned row's source** using the
table captured at picker entry: `room` (with object ID only after full RGN
alignment), `fallback` (slot zero), `control` (other aligned groups), or
`unmapped` when picture tracking cannot identify the room group. `unresolved`
means the returned group/slot did not point to a matching enabled rectangle at
those coordinates; this is important evidence, not a guessed object. The
`overlaps` count tells how many enabled rectangles contained the click,
including fallbacks and rows later overridden by the winning row.

The native room X origin is 261, one pixel to the right of the previous
approximate RGN overlay origin. The **R** raw view shows every RGN candidate
and independent native GUI/fallback rectangle, including skipped controls as
dim outlines. If the native table can be decoded but its room group cannot be
matched to the tracked PIC/RGN frame, H falls back to the heuristic **scene
RGN objects only**; it does not mislabel arbitrary native rows as room objects.
Native groups, slot numbers, and actual picker outcomes remain available in the
`Native pick` observation panel and raw `.picker.tsv` dump, including when PIC
tracking temporarily falls out of sync.

Gateway 1 now samples the validated 734-object guest table and annotates
each RGN region with one of four *host-side* classifications:

| Classification | Meaning | Heuristic fallback outline | Widescreen list |
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

Press **R** in the F10 menu to display **all** raw RGN candidates and native
UI controls, including skipped ones, for comparison. The original guest
mouse input is unchanged.
Dumped `.regions.tsv` files retain every region and append a `presence`
column and a `native_picker` column (`disabled`, `not_disabled`, `unknown`);
`.picker.tsv` now records **every** native picker row in scan order, including
room RGN slots, fallback regions and non-RGN controls, with group/slot, kind,
signed coordinates, skip flags and object IDs only for fully aligned RGN slots.
`.debug.txt` records whether the picker was decoded and which group (if any)
was matched. It identifies object-presence classification as heuristic.
For the other Legend titles, all regions stay `unknown`, because the Gateway 1
object state and native picker layouts have not been verified there.

The next verification should correlate these categories with native render
events and click-selection results in several rooms. In particular, test
objects already disclosed but subsequently hidden by story flags. Only such
traces can turn provisional room candidates into confirmed active hitboxes.

An optional differential test, built when Unicorn is enabled, executes the
**original** picker at `2649:0541` from four retained 1-MiB guest RAM
checkpoints. It tests a grid plus the inclusive edges and immediate neighbors
of each native rectangle, comparing returned group, slot and kind with the
host-side model. It requires private game data and snapshots and is therefore
skipped by the ordinary public CTest run:

```sh
build/native-picker-oracle-tests ../gateway-dos build/rgn-scenes-v026
```

The lighter original-asset test requires the pinned Gateway files but **no
Unicorn or guest RAM snapshot**. It runs all 40 original PICs through the
native decoder, validates the 58-section RTLink relocation manifest, and
re-scans all original conditional region and picture-layer call sites:

```sh
python3 tests/gateway_original_assets_tests.py --data ../gateway-dos \
  --extract build/gateway-extract --overlay build/gateway-overlay
```

This is also registered as `gateway-original-assets` in CTest, with a clean
skip when original files are not installed. The pinned archive yields 1,504
decoded PIC images, 13,947 RTLink fixups, 433 region rules and 244 potential
picture-layer records. These **static** inventories do not reveal which
native rectangles the running game enables, or whether a click is accepted.

An independent `gateway-original-regions` CTest exercises the scene debugger's
own `parse_regions`/`DebugSceneTracker::add_picture` path on original PIC/RGN
pairs (also skipped without private files):

```sh
build/gateway-original-regions-tests ../gateway-dos
```

In the pinned data, 22 matching PIC/RGN pairs comprise 773 picture frames
and 2,521 rectangular regions; 160 are anonymous (object ID zero). Four
anonymous rectangles lie entirely to the right of the picture's 380-pixel
local scene width: `GATE_015.PIC` and `GATE_115.PIC`, frame 0, with corners
`(396,34)-(440,53)` and `(396,59)-(440,78)` in each file. These are real RGN
records **outside the scene**, not evidence that a click selected a scene
object. The normal H highlighting remains confined to the picture and merges
neighboring rectangles with the same object ID. The raw R debugger can still
inspect GUI and fallback picker rows; the audit reports off-picture RGN data
that would be clipped by the graphical overlay.

For **live** comparisons, `gateway-probe --trace-picker FILE` observes the
original picker routine at its entry and return addresses without replacing
its results. It records the actual click coordinates, all three returned
output parameters, and the F10 scene model's independent prediction. A fully
decoded native picker table can be compared even if its RGN association is
unknown; `unmodeled` means the native table could not be decoded at all.
These hooks currently
assume the known Gateway 1 executable mapping, and are not enabled by default.
The `--trace-picker` flag also fills the on-screen last-result panel when used
with the SDL runtime; `--debug-native-picker` enables just that panel and the
same observational hooks without writing a TSV file.
New `.tsv` traces append `native_source`, `native_index`, `native_flags`,
`native_object`, and `enabled_overlaps` to the established twelve columns.
The source is derived from the **actual returned native group/slot/kind** and
a frozen native row table at call entry, not from the current mouse hover or a
later PIC/RGN estimate. Object ID zero means no independently established
association. Older twelve-column traces remain accepted by the checker.

```sh
python3 tools/report_picker_trace.py build/picker-live/picker.tsv
```

This prints counts by native source, repeated overlap hotspots, and the exact
coordinates and results of mismatched or unresolved calls. Unlike the strict
`check_picker_trace.py` verifier, the report is intended for investigating
disagreements, so it does not reject a trace merely for containing them.
Neither tool establishes the later action dispatch or whether the click was
visible to the player.

For example, add a timed mouse click during the first-room walkthrough (the
coordinates are *screen* coordinates, unlike the RGN picture-local X values):

```sh
mkdir -p build/picker-live
xvfb-run -a python3 tools/run_walkthrough.py run \
  tests/fixtures/gateway-opening.walk \
  --probe /tmp/gateway-port-build/gateway-probe \
  --data ../gateway-dos --output build/picker-live \
  --probe-arg=--mouse-click-at --probe-arg=24000:360:200 \
  --probe-arg=--trace-picker --probe-arg=build/picker-live/picker.tsv
python3 tools/check_picker_trace.py build/picker-live/picker.tsv --allow-unmodeled
```

`check_picker_trace.py` requires at least one modeled call and rejects any
disagreement, trace error, or unfinished picker invocation. Use
`--allow-unmodeled` when the launch traverses a state before a valid picker
table has been decoded; omit it for a strictly modeled capture. A *picker selection*
means a particular group/slot won the native bounding-box scan; it does not
by itself prove that the game's later click dispatch consumed the event.

The fixed `2649:0541` runtime entry is verified only for those four opening
snapshots; this is not a universal segment address or a replacement picker.

### Follow the original game after the picker returns

The opt-in `--trace-picker-follow FILE` also observes the original picker's
entry and exit, reads the *far return address from its real guest stack*, and
captures a bounded sequence of executed guest instructions beginning at that
caller continuation. It records the native selection, exact caller CS:IP,
instruction bytes, and registers/flags before each instruction. It also records
CPU memory writes attributed to the instruction that executed them as
`# write call=N step=N ms=N linear=HEX size=N value=HEX`. Values are the
written low-order bytes in hexadecimal (unlike the decimal `size`). A store
from the final sampled instruction is included. The default window is 96
instructions; `--picker-follow-steps N` accepts 1–1024.

Two narrowly hooked resident dispatch sites are also monitored for **up to
2000 guest milliseconds after the original picker return** (override with
`--picker-dispatch-ms N`, 1–20000). The `# dispatch` records include call ID,
elapsed time, action/event and available arguments:

- `context`: resident action-context coordinator entry
  (`GATEMAIN.EXE:0x105e`, observed `1000:01a4`), with checked prologue and
  native far-call stack action/event and two object IDs.
- `action-table`: original indirect far action CALL
  (`GATEMAIN.EXE:0x59ae`, observed `1476:0060`), with verified five-byte
  opcode, action-directory index and live target CS:IP.

These addresses are specific to the pinned Gateway 1 launch layout. Other
mouse consumers may not visit either site. A new picker ends the prior
association; an expired window cannot claim unrelated later actions. The
summary includes `dispatches`, `dispatch_errors` and `dispatch_ms`, which the
comparator checks. They are *time-correlated dispatcher candidates*, **not**
proof that the click caused their execution or successfully handled anything.

The same opt-in diagnostic now remembers the last 64 **guest-visible DOS
mouse reads** (`INT 33h` functions `3`, `5`, `6`). It captures the returned
button mask, position, and—on function `5`/`6`—the press/release count before
the HLE resets that count, with the guest's interrupt-resume CS:IP. For each
completed native picker it selects the **most recent preceding** guest read
within 2000 guest milliseconds (configurable as `--picker-input-ms N`, 1–20000),
and records `# input call=N status=observed ... coords=match|differ`. If no
read qualifies it writes `# input call=N status=none`. A coordinate mismatch
is retained honestly; an older coincidentally matching read is not selected.
The final summary accounts for every picker as `inputs` or `input_missing`.
Guest reads can represent hovering, button polls, or cached state; even
`INT 33h/5` returning a positive press count does not prove the later picker
consumed the same mouse click.

```sh
mkdir -p build/picker-follow
gateway-run ../gateway-dos/GATE.EXE \
  --trace-picker build/picker-follow/picks.tsv \
  --trace-picker-follow build/picker-follow/continuation.tsv \
  --picker-follow-steps 128 --picker-dispatch-ms 3000 --picker-input-ms 3000
```

The trace is written when the runtime exits. Only the known Gateway 1 picker
layout is supported. The global instruction hook has substantial overhead,
so leave this diagnostic disabled during ordinary gameplay. The tracer skips
the `RETF` instruction itself and waits for the exact caller address (up to
4096 other instructions, which can include IRQ code). It records an explicit
`missed-return` if the continuation does not appear. An earlier window may
be marked `interrupted` by another picker call; unfinished windows are reported
in the final summary. Guest execution, registers, memory and control flow are
never changed by this observer.

`# follow-summary` counts observed stores as `writes` and failed write
observations as `write_errors`. The comparator rejects write errors and
inconsistent counts. The write hook observes guest CPU stores, **not** every
change to emulated memory: host-side `uc_mem_write()` calls performed by HLE
are absent, and a write may be a stack push or unrelated bookkeeping. A
missing write is not evidence that no state changed. The hook rejects writes
wider than eight bytes rather than silently truncating their values.

For two runs started from the **same saved guest state** with different clicks:

```sh
python3 tools/compare_picker_follow.py \
  build/picker-follow/click-a.tsv build/picker-follow/click-b.tsv
# If a run contains several picks, select the matching call IDs:
python3 tools/compare_picker_follow.py \
  build/picker-follow/click-a.tsv build/picker-follow/click-b.tsv \
  --left-call 2 --right-call 3
```

The comparison finds the first difference in executed instruction address or
bytes and independently the first difference in guest-store sequence (step,
linear address, size, value), not a guessed acceptance condition. It accepts
older continuation captures without write information and explicitly reports
their limited evidence. Register differences are displayed for inspection but
do not by themselves establish a different code path.
Dispatcher candidates are compared by kind, action/event, object arguments,
and target address; timing-only differences are ignored. A report showing
`No known resident dispatcher candidates` does **not** establish rejection:
the click may use another UI path, take longer, or bypass those sites.
Older traces remain usable with dispatch evidence labeled unavailable.
The comparator also shows the last guest mouse read preceding each pick,
including its age, button count and coordinate agreement. It distinguishes
unavailable legacy evidence, an observed but unmatched read, and no read
within the configured bound; timing and polling frequency are not treated
as semantic differences between runs.

To investigate a **single trace** without comparing two saved-state runs,
use the per-click evidence report:

```sh
python3 tools/report_picker_follow.py build/picker-follow/continuation.tsv \
  --source room --limit 0
```

It lists actual returned native room objects (rather than cursor predictions),
model match/mismatch status, recent guest `INT 33h` reads, bounded instruction
and store counts, and temporally nearby context/action-table dispatches.
For room results with established object IDs, context calls are classified
as `object-referenced` when one of their two original arguments equals the
selected object, `other-context-objects` when neither does, or `no-context`
when no monitored context call occurred. This is useful for locating which
native branch to trace next, **not** an acceptance verdict: even an exact
object reference may be unrelated to the click. `--source control` or
`--source fallback` investigates the regular GUI paths; omit `--source` to
list all groups. The report accepts earlier call markers without the new
object/overlap/verdict annotations and explicitly marks their provenance
unavailable. The default listing limit is 40 calls (`--limit 0` shows all).

### Compare a native result with the scene-only H highlighting

New `--trace-picker-follow` recordings also preserve the entire native picker
rectangle table **at each call's entry**, after the original game has updated
its conditional skip bits. The `# scene` record identifies the correlated
picture/frame; `# picker-row` records retain original group, slot, kind,
signed inclusive bounds, skip flags, and the engine object ID *only* for a
proven-aligned room slot. Ordinary GUI rows and group fallbacks keep separate
roles; ambiguous RGN associations are `unmapped`. A `selected=1` marker refers
to the **original native return**, not the model's predicted cursor hit.
These records are comment lines, so the established continuation TSV and
comparators remain readable.

```sh
python3 tools/report_picker_scene.py build/picker-follow/continuation.tsv
python3 tools/report_picker_scene.py build/picker-follow/continuation.tsv --limit 0
```

The report recreates the normal H overlay's room-only last-match ownership
at each recorded click point (640×480 logical coordinates, scene X=261..639
and Y=0..231), merging neighboring rows through the shared object ID. It
counts native room selections **not marked by H**, native room selections
whose object differs from H, and GUI/fallback selections that override an
underlying highlighted room region. Each result is explicitly compared with
the frozen native table, and older traces without those rows are reported
as geometry-unavailable. This diagnoses scene mapping, clipping, overlap and
conditional-flag discrepancies; it still cannot establish that the scene
image was drawn or that a downstream handler accepted the mouse click.

For two picker calls with the **same** PIC/frame but different conditional
flags (for example, before and after opening the first-room drawer), compare
the frozen scene maps without replaying the old state from scratch:

```sh
python3 tools/compare_picker_scene.py build/picker-follow/continuation.tsv \
  --before 1 --after 2
# To obtain candidate screen pixels for separate controlled click trials:
python3 tools/compare_picker_scene.py build/picker-follow/continuation.tsv \
  --before 1 --after 2 --json
```

The comparison requires an identical aligned room group and rectangle
geometry; it refuses to attribute scene transitions or unmapped tables to
conditional skip flags. It rasterizes the original last-enabled native
winner **and** the merged-by-object H owner over the 379×232 scene viewport.
Its output counts H gains, losses and object changes independently of native
selection changes, and identifies cases where two native rows change winners
but the scene highlight remains one object. Representative coordinates are
actual changed pixels, including overlapping GUI-vs-room candidates. Test
each coordinate in a **separate run from the same restored guest state**;
clicking them sequentially would alter game state and invalidate the
comparison. A flipped native skip bit remains only selection eligibility,
not proof that the scene was rendered or an action was accepted.

### Capture the actual original picker entry state

`--dump-at` snapshots sample the guest at a scheduled millisecond and may
land **before or after** a changing native picker table. For reproducing
precise conditional hitboxes, `--picker-capture-dir DIR` instead saves guest
RAM and registers **at the original picker entry instruction**, before it
executes. `--picker-capture-limit N` bounds the number of 1-MiB snapshots
to 1–128 (default 16). The directory must be new or empty; old captures
are never overwritten. Use `--picker-capture-from-ms N` to ignore title-screen
and other early picker calls until the target replay stage (guest milliseconds).

```sh
mkdir -p build/picker-entry
xvfb-run -a python3 tools/run_walkthrough.py run \
  tests/fixtures/gateway-opening.walk \
  --probe /tmp/gateway-port-build/gateway-probe \
  --data ../gateway-dos --output build/picker-entry/replay \
  --probe-arg=--picker-capture-dir \
  --probe-arg=build/picker-entry/captures \
  --probe-arg=--picker-capture-limit --probe-arg=32 \
  --probe-arg=--picker-capture-from-ms --probe-arg=15000 \
  --probe-arg=--trace-picker-follow \
  --probe-arg=build/picker-entry/continuation.tsv

# Offline against captured private memory:
build/native-picker-oracle-tests ../gateway-dos \
  --captured build/picker-entry/captures

# Find all observed conditional hitbox-state transitions in that batch:
python3 tools/compare_picker_captures.py build/picker-entry/captures \
  --trace-output build/picker-entry/merged-scene.tsv \
  --oracle-points build/picker-entry/oracle-points.tsv --json
build/native-picker-oracle-tests ../gateway-dos \
  --captured build/picker-entry/captures \
  --points build/picker-entry/oracle-points.tsv
```

Each `call-NNNN.bin` contains 1 MiB of original guest memory, while
`call-NNNN.meta` records the entry registers, click coordinates, guest time,
far return address, and PIC/frame when known. Upon an actual return, a
`call-NNNN.result` file records the original AX/group/slot/kind alongside
the **entry-time** picker table, room-only object associations and skip flags.
This avoids relying on a scheduled checkpoint or a later changed scene.
The continuation trace also adds `# capture call=N base=call-NNNN` for
completed invocations. The oracle
checks the pinned original picker entry bytes and executes the **original**
picker with Unicorn over a coordinate grid, rectangle boundaries and the
captured click, comparing native returns with the independently decoded
group/slot/kind. It reports PIC/RGN alignment separately; GUI rows do not
become scene objects merely because the picker could select them.

`compare_picker_captures.py` validates all `.meta`, `.bin` and `.result`
sidecars, including entry/return click coordinates, scene identity, original
selected row and file completeness. It groups captures with the **same
verified PIC frame and exact rectangle geometry**, folds repeated skip masks
into distinct condition states, and compares each observed transition.
Results include the native selection winner separately from the H overlay's
scene-only merged object shapes. In particular, different slots of the same
object do **not** create separate highlights; GUI and fallback picker rows
remain excluded from H. `--max-pairs N` and `--sample-limit N` bound output,
and the optional merged trace supports `compare_picker_scene.py` for manual
before/after pairs. An unrelated picture frame, ambiguous RGN association or
different geometry is deliberately never attributed to conditional flags.
The `--oracle-points` output names the **two specific captured entry states**
and the exact changed screen pixel for each representative comparison. Passing
those points to `native-picker-oracle-tests --captured ... --points ...`
executes the original picker on **both states** at every difference, in
addition to its ordinary grid and rectangle-edge checks. All candidate points
are derived from actual differing pixels, never bounding-box midpoints.
The tool operates on already captured private data; it does not itself
execute mouse clicks or prove later game-action acceptance.

### Test real click side effects against two unchanged controls

For positions selected by the capture comparator, run **each click in a
fresh DOS process and writable state directory**. The controlled-trial runner
reuses the reviewed walkthrough's deterministic BIOS input schedule and
optionally imports the exact same original saved game for every run. It
automatically runs two no-click controls, then a separate run for each chosen
position; snapshots are recorded one millisecond before injection and after
a fixed observation interval. It compares normalized native RAM (with only
the golden-run volatile exclusions), all CPU registers and any unambiguously
decoded 734-object state records. It also retains original picker selections
observed during the click interval.

For example, to try positions associated with one captured conditional state
*after* the first `open drawer` command:

```sh
# Inspect the plan without starting a guest:
python3 tools/picker_click_trials.py tests/fixtures/gateway-opening.walk \
  --max-actions 1 --points build/picker-entry/oracle-points.tsv \
  --capture-call 2 --max-points 4 --plan-only

# The actual input experiments must use a private Xvfb display:
xvfb-run -a python3 tools/picker_click_trials.py \
  tests/fixtures/gateway-opening.walk --max-actions 1 \
  --points build/picker-entry/oracle-points.tsv --capture-call 2 \
  --max-points 4 --probe build/gateway-probe --data ../gateway-dos \
  --output build/picker-click-trials/drawer-open

# Revalidate the exact completed runs and regenerate their comparison
# without starting DOS again (repeat all experiment-selection arguments):
python3 tools/picker_click_trials.py tests/fixtures/gateway-opening.walk \
  --max-actions 1 --points build/picker-entry/oracle-points.tsv \
  --capture-call 2 --max-points 4 --probe build/gateway-probe \
  --data ../gateway-dos --output build/picker-click-trials/drawer-open \
  --compare-existing
```

Choose a stage or original save **corresponding to the capture's condition**;
the tool cannot infer that equivalence from shared coordinates. To specify a
single position manually use `--point 360:200` rather than `--points` and
`--capture-call`. The default click is 1000 guest milliseconds after the
selected walkthrough prefix ends; `--after-ms` and `--settle-ms` adjust the
delay and post-click observation window. All output contains private game
memory under `build/` and must not be committed.

`comparison.json` reports separately whether the two controls have identical
pre/post snapshots, whether a click trial started from the same pre-click
state, native picker selections during the interval, changed normalized RAM
bytes and changed verified object IDs. It also compares the original probe's
section-qualified RTLink call census for every run, listing the largest
per-target count changes alongside manager-hit deltas. If two no-click controls
disagree in RTLink counts, timing, RAM or CPU state, or a click trial's RAM/CPU
pre-state differs, the trial is **inconclusive**, not classified as accepted.
The call census covers the **entire walkthrough replay**, so even a changed
RTLink target is a lead for follow-up tracing, not proof of a click-specific
call. A qualified RAM/object difference is only a candidate click side effect:
proving the game accepted the click requires tracing the relevant native
caller branch, and a valid picker selection by itself is insufficient.

For native picker calls recorded in the injected time interval, the report now
also retains the original result kind, overlap count and model verdict. Where
the detailed continuation trace is available, it attaches the recent guest
INT 33h read (including whether its coordinates match), completed picker
continuation status, bounded observed CPU-store count, and nearby action-table
or action-context events. A room object referenced by a nearby context event
is explicitly distinguished from a context mentioning other objects. The
`picker_calls_at_injected_point` list identifies original picker returns at
the exact trial coordinates; an empty list does not imply the input was
rejected by the game. All such action events are **temporal candidates**, not
verified causal dispatches. Older or malformed continuation data is reported
as unavailable instead of being turned into a false acceptance verdict.

### Check that a click reproduced the captured conditional state

A screen coordinate taken from one original picker-entry capture may not
represent the same condition after a different walkthrough prefix. Once the
isolated click study is complete, compare its *actual frozen picker tables*
against the reference state used to select the candidate coordinates:

```sh
python3 tools/match_picker_trial_states.py \
  build/picker-entry/captures build/picker-click-trials/drawer-open \
  --reference-call 2 > build/picker-click-trials/drawer-open-state-check.json
```

This read-only check requires `trial-plan.json`, `comparison.json` and the
original picker follow traces. It validates the reference capture's private
RAM sidecar and returned row geometry, then checks each trial's original
picker calls at the **exact injected coordinates** within the observation
interval. It distinguishes different pictures/frames, different rectangle
geometry, room skip-bit differences, non-room GUI/fallback skip-bit differences,
and exact skip-mask matches. It never merges GUI rows into scene-object
hitboxes; all room row identities include original object IDs, while
distinct slots of one object remain distinct internally for priority checks.
An identical native skip mask means the same rectangle eligibility in that
captured scene, **not** that the scene was painted or that an action was
accepted. `baseline_qualified` repeats the existing controlled comparison's
status; this command cannot recover evidence from unstable controls. Before
trusting a previously recorded study, run its `--compare-existing` check.

### Rank the original-code leads from several controlled studies

Once several stages/conditions have been replayed independently, compare the
**section-qualified** original RTLink count changes across their completed
`comparison.json` reports:

```sh
python3 tools/rank_picker_trial_targets.py \
  build/picker-click-trials/drawer-open \
  build/picker-click-trials/drawer-closed \
  --plan build/ghidra/ghidra-plan.json --json \
  > build/picker-click-trials/handler-leads.json
```

The optional verified overlay plan resolves runtime segment:offset targets
back to original `GATEMAIN.OVL` offsets; an **exact** matching tracker entry
can supply its known name and analysis state. The report ranks only qualified
click experiments and lists changed-target frequency, net/absolute call
differences, RAM changes, actual room selections, observed room object IDs
and nearby *temporal* object-context references. Two overlay sections sharing
a runtime address remain distinct original code identities. Missing native
call censuses make ranking explicitly incomplete. When a saved report lists
only the first 32 changed targets, the ranker re-reads both full
`native.rtlink-calls` sidecars and verifies they reproduce the saved summary
*exactly* before ranking the omitted targets. Truncation is incomplete only
when raw sidecars are unavailable; a partial pair is rejected. Malformed or
out-of-bounds native targets are rejected. The first twelve occurrence
examples for each original target retain the experiment filename, isolated
trial name, exact screen coordinates and signed call-count delta, so the
investigation can return directly to a reproducible click. This is prioritization for
disassembly and follow-up live tracing, never a conclusion that the target
was actually called *because* of the injected click.

### Require the same captured picker condition before ranking

The basic ranking above intentionally includes every baseline-qualified
experiment, even if its frozen picker rows later proved to represent a
different room or disabled-rectangle state. For a conditional-hitbox
investigation, regenerate each read-only state check from its actual
reference capture (one separate reference per condition/study), then supply
one state report **for each** ranked study:

```sh
python3 tools/match_picker_trial_states.py \
  build/picker-entry/open-captures build/picker-click-trials/drawer-open \
  --reference-call 2 > build/picker-click-trials/drawer-open-state-check.json
python3 tools/match_picker_trial_states.py \
  build/picker-entry/closed-captures build/picker-click-trials/drawer-closed \
  --reference-call 2 > build/picker-click-trials/drawer-closed-state-check.json
python3 tools/rank_picker_trial_targets.py \
  build/picker-click-trials/drawer-open \
  build/picker-click-trials/drawer-closed \
  --state-check build/picker-click-trials/drawer-open=build/picker-click-trials/drawer-open-state-check.json \
  --state-check build/picker-click-trials/drawer-closed=build/picker-click-trials/drawer-closed-state-check.json \
  --plan build/ghidra/ghidra-plan.json --json
```

`--state-check` enables a strict **matching picker state** ranking scope.
Each state report is matched against the exact original `trial-plan.json`
and `comparison.json` byte hashes; stale, mismatched, mislabelled and
partially supplied reports fail closed. A qualifying trial must have at
least one original picker call at the injected coordinate **and every such
call** must have `matching_skip_mask` against the capture, including GUI
skip bits. If even one call reflects a changed room/GUI mask, scene, geometry
or missing picker data, that trial is excluded. The JSON records qualified
baseline trials, state-matched trials and exclusions by reason separately.
The matcher currently emits `gateway-picker-state-check-v1` reports; older
pre-hash state reports must be regenerated from the original recordings.
This verifies the picker's *condition*, not painted visibility, click
acceptance or causal action execution. The unfiltered command remains
available for investigating independent scene conditions.

The runner writes `trial-plan.json` before starting the first guest; it hashes
the walkthrough, probe, all original game resources (not unrelated DOS save
files), optional imported save, input schedule and chosen coordinates.
`--compare-existing` requires the same plan and calls the original walkthrough
recording verifier for **every** control/trial, checking that the recorded
guest trace delivered all expected BIOS keys and mouse clicks. It then reads
the frozen `.bin`/`.regs` snapshots. If `comparison.json` already exists and
the newly computed result differs, it refuses to overwrite it. Incomplete
runs, changed fixtures and inconsistent reports must be investigated, not
silently accepted as a valid native experiment.

Private captures contain proprietary executable and game-state bytes:
**never commit, publish or include them in a Git bundle**. Full-memory
reads inside the CPU hook may slow gameplay. The entry address is only
verified for this pinned Gateway 1 layout. Live capture/replay has not been
exercised on this host, which lacks the Unicorn/SDL3 runtime.

The F10 native-picker panel shows the last observed dispatcher candidate as
its fifth row and the nearest preceding guest `INT 33h` read as a sixth,
when monitoring with `--debug-native-picker` or `--trace-picker-follow`.
It distinguishes "no candidate observed" from "monitoring disabled". For
interactive F10-only debugging, `--debug-native-picker --picker-dispatch-ms 3000 --picker-input-ms 3000`
activates just the two focused hooks without the expensive global
instruction trace.

This is the next step toward locating the downstream click consumer, **not**
proof that it accepted an action. Do not label a click `accepted` without
tracing the relevant caller branch and its actual side effects.

### Headless scene checkpoints

The probe's `--debug-scene` option enables its existing PIC/RGN scene tracker
even in a **headless** walkthrough. Ordinary memory dumps then also include
`*.regions.tsv`, `*.picker.tsv`, `*.objects.tsv`, and `*.debug.txt`, with the live native object
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

There is now also an independent native `gateway-scene` picture compositor.
With the correct **screen x=261** room origin, it composes the original PIC
background and selected XOR overlays into a PNG; its two opening-room examples
match saved original game framebuffers exactly (84,896 pixels each). It does
not infer image visibility from any RGN entry or assume that `DS:adf8` alone
determines every transient overlay. See
[the compositor reproductions](picture-conditions.md#first-native-picture-compositor-and-exact-frame-validation).

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
