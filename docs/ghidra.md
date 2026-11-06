# Ghidra: original Gateway native code and RTLink overlays

For the **separate `GATE.EXE`/`GATE.RTL` introductory program**, use the
dedicated [intro Ghidra batch workflow](intro-ghidra.md). The commands below
apply to the main-game binaries only.

For the independently replayed receptionist event-4 handler
(`GATEMAIN.OVL:0x56f9f`, `GIVE BOOK` branch `0x570a9–0x57114`),
native entry/return/object-write corroboration, and a five-object Ghidra
decompilation pass, see [PV commset and receptionist](pv-commset.md).

The first reproducible Ghidra workflow imports **the original, unchanged**
`GATEMAIN.EXE` with Ghidra's DOS/MZ loader, then imports the 58 separately
relocated `GATEMAIN.OVL` sections into **58 distinct overlay address spaces**.
This is important: multiple RTLink sections occupy the same physical DOS bank
at *different times*. A single flattened DOS image confuses their code and
data, and a synthetic multi-megabyte MZ does not preserve those lifetimes.

This workflow is a static-analysis aid, not yet a port of the logic. The
`gateway-overlay` C++ parser already validates section boundaries and fixups;
the Ghidra import makes it easier to inspect the resulting 16-bit functions.

## Run

Use the optional shell to avoid downloading Ghidra for ordinary C++ builds:

```sh
nix develop .#ghidra
gateway-build
python3 tools/run_ghidra.py \
  ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --overlay-tool "${GATEWAY_BUILD_DIR:-build}/gateway-overlay" \
  --output build/ghidra
```

The shell provides the source-built `ghidra` package, a Python with
`pyghidra`, OpenJDK 21 and `GHIDRA_INSTALL_DIR`. Ghidra 12 removed Jython,
so its `analyzeHeadless` runs the `.py` scripts in `ghidra/` only when the
JVM was started by PyGhidra; the runner therefore launches
`python3 -m pyghidra.ghidra_launch --install-dir "$GHIDRA_INSTALL_DIR"
ghidra.app.util.headless.AnalyzeHeadless ...` whenever `GHIDRA_INSTALL_DIR`
(or `--ghidra-install-dir`) is set and `pyghidra` is importable. Nix's
`ghidra-bin` is not used: its prebuilt decompiler process crashes on NixOS.

The runner uses a new project; it **does not** overwrite an existing Ghidra
project. Choose another `--project-name` for another import, or open the
existing one in Ghidra.

### Without Nix

1. Install a C++ toolchain, CMake, SDL3 and Unicorn, then build the overlay
   parser: `cmake -S . -B build && cmake --build build --target gateway-overlay`.
2. Install Ghidra 12.x from <https://github.com/NationalSecurityAgency/ghidra/releases>
   and JDK 21.
3. Install the PyGhidra wheel that ships with Ghidra:
   `pip install --no-index -f "$GHIDRA_INSTALL_DIR/Ghidra/Features/PyGhidra/pypkg/dist" pyghidra`.
4. Run the command above with `GHIDRA_INSTALL_DIR` exported and
   `--overlay-tool build/gateway-overlay`.

With Ghidra 11 or older (Jython), pass `--headless
/path/to/ghidra/support/analyzeHeadless` instead; that bypasses PyGhidra.
Verified on 2026-11-03 with nixpkgs Ghidra 12.1.4 and pyghidra 3.2.0: all
58 sections and 13,947 fixups imported, 32/32 targets decompiled, in about
1.5 minutes.

Expected output (generated, ignored by Git, contains original game code):

| Path | Contents |
| --- | --- |
| `build/ghidra/ghidra-plan.json` | Verified addresses, section/parent/overlap metadata and labels |
| `build/ghidra/sections/section-NNN.relocated.bin` | RTLink images with 13,947 validated segment fixups across 58 sections |
| `build/ghidra/project/Gateway.gpr` | Ghidra project, with DOS MZ program and independent overlay spaces |
| `build/ghidra/ghidra-import-report.json` | Exact label counts and original-MZ signature matches |
| `build/ghidra/ghidra-decompile-report.json` | One status, original address and Ghidra-containing function per target |
| `build/ghidra/ghidra-callgraph.json` | Static call references and unresolved-call sites in the selected containing functions (not an executed RTLink call graph) |
| `build/ghidra/pseudocode.txt` | Decompiler output for confirmed native function-entry candidates, or explicit errors |
| `build/ghidra/ghidra-functions.tsv` | Every Ghidra function by original file offset (`exe`, or `ovl` plus RTLink section), body size and name; resident entries are byte-checked against `GATEMAIN.EXE`. Input for `tools/decomp_status.py seed` |
| `build/ghidra/ghidra-facts.json` | Every recovered function's static callees, validated ten-byte RTLink trampoline destinations, direct DS/ES offsets, possible loops and instruction counts; input for `tools/classify_shapes.py` (triage, not native ABI proof) |

After running Ghidra with the replay's objects, callbacks, actions and
`--tracked-handler-boundaries decomp/functions.tsv` as in
[plan §11](decompilation-plan.md#11-quick-reference-for-a-work-session), run
`python3 tools/classify_shapes.py build/ghidra/ghidra-facts.json --write`.
This annotates 935 handler/action/callback shapes without storing executable
bytes, decompiler text or object names in Git. Plain `classify_shapes.py`
without `--write` prints the current decision and leaves the tracker unchanged.

Preparation can be run independently of Ghidra, useful on machines where the
Java application is unavailable:

```sh
python3 tools/run_ghidra.py \
  ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --overlay-tool "${GATEWAY_BUILD_DIR:-build}/gateway-overlay" \
  --output build/ghidra --prepare-only
```

The preparer checks the SHA-256 fingerprints from [executable.md](executable.md),
each section's file-backed length, memory/BSS/tail extents, relocated segment,
and **every individual fixup** against the extracted bytes. Only fingerprinted
Gateway binaries receive fixed file-offset labels. An explicit
`--allow-unknown-binaries` on `tools/prepare_ghidra.py` is for synthetic parser
fixtures, without any Gateway-specific symbols.

## More than the built-in entry labels

For all **696 known object/event handlers**, first capture or reuse a valid
post-gameplay native snapshot and export it using the existing tools:

```sh
python3 tools/inspect_object_state.py build/parser-child-writes/native \
  --names ../gateway-dos/OBJECT.DAT > build/ghidra-objects.json
python3 tools/run_ghidra.py \
  ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --overlay-tool "${GATEWAY_BUILD_DIR:-build}/gateway-overlay" \
  --objects-json build/ghidra-objects.json \
  --output build/ghidra --project-name GatewayObjects
```

The preparer resolves each object handler's ten-byte trampoline through the
verified RTLink manifest, then labels its **real section image offset**. It does
not mistake a trampoline, a byte inside a function, or two sections sharing an
address for separate C functions. Exported object labels remain aliases until
the routine's boundaries and call sites have been checked.

Use repeatable `--decompile-object 0x2a9` (with `--objects-json`) for a
**selected** object handler to attempt as a provisional Ghidra function.
Other object aliases do not automatically turn into 696 decompiler targets.
Selection rejects duplicate object IDs, missing native handlers, and two
selected objects sharing the same entry. For a discovered non-object entry,
`--decompile-offset 0x6d564` selects an **original OVL file offset** from a
unique file-backed RTLink section; it is marked `overlay-entry-candidate`
and must not be represented as a verified function boundary. See the
[membership-pin analysis](membership-pin.md) for the Beach reward example.

To seed all **44 known kind-4 room-exit callback entries** as decompilation
candidates, export `tools/inspect_room_callbacks.py --format json` from a
native snapshot and include `--callbacks-json build/room-callbacks.json`
on the `run_ghidra.py` command. This is a separate optional input from
the object aliases; the preparer checks each callback's claimed OVL offset
against a fresh RTLink manifest. Full reproducible commands, ID→section
mapping and the first traced example are in
[room-callbacks.md](room-callbacks.md). Use a fresh output directory for
each run.

To seed the **191 RTLink-mapped action entries** as Ghidra function candidates,
pass `--actions-json` with the complete 195-entry JSON from
`inspect_action_dispatch.py --all --overlay-map <manifest> --format json`.
The preparer re-resolves each trampoline against the extracted manifest and
requires 191 distinct overlay entries. Add
`--resident-exe ../gateway-dos/GATEMAIN.EXE` to the action inspector to map
the four resident entries too. This requires a complete native snapshot and
verifies 8-byte unique signatures, a common resident segment/file-offset base,
and a 96-byte neighborhood whose only differences are expected far-call
segment relocations. The preparer then labels those EXE entries by their
unique original file signatures. All action entries are decompiled before
the function-size export, but their behavior still requires native validation.
Regenerate the JSON from the same snapshot as the callbacks.

For triaging the full handler table, `tools/inventory_native_handlers.py`
maps the 696 section-qualified entries, finds byte-identical entry-prefix
groups, and optionally correlates a complete native trampoline-call census
with object handlers and room callbacks. See
[native-handler-coverage.md](native-handler-coverage.md) for reproducible
first-room measurements, limitations, and decompilation priorities.
For joining its inventory to the selected static call references and sampled
native entry/return/object-record deltas, see
[handler-evidence.md](handler-evidence.md).

## Imported structures and first analysis targets

Ghidra's Data Type Manager receives these *confirmed* little-endian structs:

```c
struct GatewayRoomExit {          // 4 bytes; [word DS:8dcc]:417e
    uint8_t direction;            // 0x03 EAST, 0x07 WEST observed
    uint8_t kind;                 // 1 direct, 2 conditional, 4 callback
    uint16_t argument;
};
struct GatewayConditionalExit {   // 4 bytes; [word DS:8dd2]:4996
    uint16_t destination_room;
    uint16_t condition_object;
};
struct GatewayObjectDirectoryEntry { // 6 bytes; type/record pointer
    uint8_t kind;
    uint8_t unknown;
    uint16_t record_offset;
    uint16_t record_segment;
};
```

Type-1 room records use a byte exit count at `+0x16` and a **one-based** exit
index at `+0x18`. These offsets are documented but the rest of the room record
has **not** been given speculative field names or laid out as a single struct.
The exit tables are addressed through runtime segment pointers; they cannot be
blindly applied at fixed MZ file offsets.

First symbols placed by the importer:

| Original file | Offset | Ghidra label | Evidence |
| --- | ---: | --- | --- |
| EXE | `0x5d5d` | `gw_room_transfer` | Native `048e:0284`; one-word far-call ABI, **function start** |
| EXE | `0x5eea` | `gw_room_transfer_commit` | Within transaction; previous/current room update, **not** function start |
| EXE | `0x5f16` | `gw_room_transfer_player_move` | Native actor relocation, **not** function start |
| EXE | `0x5f88` | `gw_room_transfer_text_guard` | Interior `:3942` check; room-text suppression |
| EXE | `0x60cb` | `gw_room_transfer_return` | Shared `RETF` epilogue, **not** function start |
| EXE | `0x60cf` | `gw_room_exit_dispatch` | Type-1 exit scanning and branch on kind |
| EXE | `0x592a` | `gw_room_callback_dispatch` | 44-entry native far-pointer callback gate, **function start** |
| EXE | `0x6175` | `gw_conditional_exit_gate_return` | After property 0x0c query; **not** function start |
| EXE | `0x6269` | `gw_callback_exit_case` | Inside dispatcher; kind-4 callback, **not** function start |
| OVL §5 | `0xe942` | `gw_open_drawer` | Action 0x71, room object 0x124 |
| OVL §5 | `0xd66b` | `gw_close_drawer` | Action 0x1d |
| OVL §5 | `0xfee1` | `gw_take_book` | Action 0x46 |
| OVL §5 | `0xd9a7` | `gw_drop_book` | Action 0x32 |
| OVL §5 | `0xe8c4` | `gw_inventory` | Action 0x53 |
| OVL §38 | `0x514b5` | `gw_your_quarters_handler` | Room object 0x0ee |
| OVL §38 | `0x519b2` | `gw_book_handler` | Object 0x123 |
| OVL §38 | `0x51a78` | `gw_drawer_handler` | Object 0x124 |

Further labels mark the exact section-4 **call instructions** at `0xc879`,
`0xc8c2`, and `0xc8f8`. Those are not invented function boundaries.

Additional confirmed entries are now included: section-4 linked-child
relocation helper `0xc830`, section-5 DROP selector callback `0xd804`, and
section-5 TAKE selector callback `0xfbaf`. They were previously established
through native location/child writes and selector-return traces in
[action-dispatch.md](action-dispatch.md). The kind-2 case at resident EXE
`0x6156` is explicitly labeled as an **internal instruction**, alongside
`0x6175` and the kind-4 case at `0x6269`.

The resident MZ labels are located by **unique 24-byte code signatures within
the original imported memory blocks**, not by adding a guessed runtime/load
base to the file offsets. If Ghidra's MZ importer changes or omits those bytes,
the script leaves them unlabeled and lists the mismatch in the import report.
For the overlay symbols, a label includes its original file offset, resident
section, source evidence and role (function vs callsite).

## What a decompiled listing does—and does not—prove

The `ImportGatewayRtlink.py` Ghidra *pre-analysis* script adds the overlay
blocks and seeds disassembly at established entry points before autoanalysis.
`DecompileGateway.py` runs **after** analysis and exports pseudocode for the
known entry points that Ghidra accepts as functions. It also exports the
*containing* pseudocode for annotated resident branch instructions, but
never creates a new function at those jump targets. Its JSON report preserves
each target's role, actual containing function entry, and success/failure.
The runner checks that both reports and the listing are present, that the
import counts match the prepared plan, and that every planned target has a
result. It refuses stale reports or a previously created project rather than
misreporting an earlier success. The scripts use Ghidra's
standard Python/Java APIs, rather than an unsupported RTLink-aware MZ loader.

- A section's `runtime_segment` models the verified **0x1000 load-segment**
  replay; it is not a universal DOS address. Section identities are stable,
  but banks can evict each other.
- File-backed bytes are initialized; explicit RTLink BSS is modeled as zero;
  section 57's final **13 unknown bytes** remain uninitialized. They are not
  silently converted to BSS.
- Relocated segment words are correct for that replay, but Ghidra cannot
  infer RTLink's bank lifetime or trampoline return semantics automatically.
  A C call graph crossing two overlay spaces must be verified against traces.
- Entry labels identify known *targets*, not proven complete function extents.
  Decompiler output may miss indirect far calls and mistake data for code.
- Pseudocode is **not a verified reimplementation**. Compare state writes,
  selected story text, and game-visible effects against the existing
  `trace_game_logic.py` native/verification/HLE replay before translating a
  routine to C++.

The kind-4 callback gate is now mapped. A later Casino EAST replay verifies
**one** result, and the 44-entry directory has been imported into a subsequent
Ghidra run. See [room-callbacks.md](room-callbacks.md). Further callbacks
and their successful movement paths still require native traces.

## First real Ghidra execution (2026-11-02)

The importer and post-analysis decompiler were executed end-to-end on the
fingerprint-matching originals with **Ghidra 11.4.2** and **JDK 21**, using
the checked `gateway-overlay` binary. Nix was unavailable in this workspace;
the user-facing `nix develop .#ghidra` entry point remains to be checked
against the version supplied by nixpkgs.

- Exactly **58** independent RTLink overlay memory spaces were created and
  **14** known overlay labels placed (11 function entries, 3 call instructions).
  All four resident signatures matched uniquely against the MZ-loaded bytes.
- The first run exported **9/9** function targets. With the three additional
  native entries and three resident internal-branch targets, the subsequent
  run exported **15/15** target contexts, with no failed decompilations.
- Ghidra's original-MZ mapping placed file `0x60cf` at `1000:60af`; the
  runtime segment alias of its inferred function entry was `105e:5acf`.
  Neither address is an assumed original load address. The three branch
  contexts resolved to the automatically inferred `105e:5b56` (kind 2) and
  `105e:5c69` (kind 4) fragments. These are **not original independent
  function entries**. In particular, their decompiled `unaff_BP` variables
  reuse the resident dispatcher's existing stack frame.
- The section-4 child relocator at `0xc830` decompiled as the expected ordered
  *unlink head or middle sibling → set location and slot → prepend to new
  parent*. The exact callsite comments at `0xc879`, `0xc8c2`, and `0xc8f8`
  remained on their instructions. The earlier native TAKE and EAST/WEST
  movement traces independently validate the location and head-setter calls.
- The section-5 TAKE (`0xfbaf`) and DROP (`0xd804`) selector callbacks
  decompiled with the previously traced `AX=1` interception and `AX=0`
  continuation paths. Ghidra also exposed more branches and object IDs;
  **those other paths have not been exercised and must not be treated as
  validated C++ behavior**.

### Resident room-exit dispatch: verified structure, incomplete semantics

The Ghidra scanner output enumerates native kinds **1 through 5** and a
fallback branch. The original 16-bit instructions at file `0x611d`, `0x6139`,
`0x6156`, `0x6207`, and `0x6269` distinguish their paths:

| Kind | Native handling established from original instructions | Remaining gap |
| --- | --- | --- |
| 1 | `0x611d`: use exit argument directly as destination | Additional target validation outside first-room moves |
| 2 | `0x6156`: index conditional record, check guard object's `0x0c`, then `0x10`; if `0x10` is set, emit text and set `0x0c` before movement | Purpose of the extra `1c24:13b7` call and flag meanings |
| 3 | `0x6139`: index a four-byte far text-pointer table through `DS:8dd0`, then take the shared message path | Which exits use this path and exact displayed text |
| 4 | `0x6269`: call generic callback with `(argument, 0x0f)`; reject zero/current-room results and check against object count before moving | Most callbacks' conditions and live results; one Casino no-move branch now traced |
| 5 | `0x6207`: index an eight-byte record through `DS:8dd4`; test a byte through a stored offset, use one word as a destination when nonzero, otherwise select an optional far message pointer | Table field semantics and runtime outcomes |

This table is a static control-flow result, **not** a new native navigation
replay. The existing EAST/WEST and kind-2 flag observations remain the live
validation. No kind-4 callback-return trace was available for *this earlier*
run; one has since been recorded and [documented](room-callbacks.md).
Ghidra cannot supply live outcomes by static decompilation. Preserve the generated
project/listing under ignored build output; they contain the original game's
code and should not be checked into Git or shipped in bundles.

## Callback directory expansion: second Ghidra run

A fresh headless run against the unchanged fingerprinted EXE/OVL using
**Ghidra 11.4.2** and **JDK 21** imported **58/58** RTLink spaces and all
**58 overlay labels** (14 pre-existing entries/callsites plus 44 callback
candidates). All five resident signatures matched uniquely. The post-analysis
report covers **60/60** target contexts, with zero decompiler failures:
44 callback entries, 13 previously mapped native functions, and three
internal resident branch contexts. The verified callback gate entry is
`GATEMAIN.EXE:0x592a`, decompiled at Ghidra's `1000:590a`.

For Casino callback `0x14`, the decompiler produced the `event == 0x0f`
case split, text-token arguments `f000:5cc1`/`f000:5cc2`, a flag-clear
call targeting object `0x1ad`/property `0x0c`, and returns `0` or `0xf6`.
Independent original-instruction disassembly supplies the **two stack
arguments** of the relation check that the inferred decompiler prototype
omits. Unicorn independently verifies the event-`0x0f`, `AX=0` branch
and zero movement in Casino, but has not tested its alternative branch.
The output statuses indicate successful **pseudocode extraction only**.

## Resident service functions and controlled native branch test

A third headless Ghidra 11.4.2/JDK21 run additionally labels native
relation membership (`EXE:0x254a`) and flag test/set/clear services
(`0x28e4`, `0x291e`, `0x2951`) as function entries. **All nine resident
signatures matched**; all **64/64** decompilation contexts completed
(44 room callbacks, 17 other functions, 3 internal branch contexts).
The bit-test, bit-set and bit-clear pseudocode agrees with the source's
independently established type-dependent descriptor/flag layout, and the
relation-slot loop does not recursively search inventory.

The Casino callback's previously untested `AX=0x0f6` branch now has
**controlled native execution evidence**: a logged one-word intervention
on membership pin `0x2a9` caused the original callback to return the lounge
room ID, followed by a verified native move `0x0f5 → 0x0f6`.
Normal puzzle progression and obtaining the pin are **not** thereby
validated. Reproducible trace comparison commands and limits are in
[room-callbacks.md](room-callbacks.md#controlled-positive-branch-explicitly-modified-guest-state).

## Selective object and non-object native decompilation: fourth run

The selected-entry options were tested on a fresh Ghidra 11.4.2/JDK21
project with fingerprint-matching original binaries, 58 RTLink banks,
the 44 room callback targets and all 696 object aliases. The runner
successfully exported **70/70** contexts, adding five *selected* object
handlers (VR Terminal `0x29b`, technician `0x2a3`, uniform `0x2a4`, pin
`0x2a9`, reader `0x2ab`) and one manually selected non-object entry
at `OVL:0x6d564`. No other object alias was promoted to a verified
function. Ghidra still warns about unresolved indirect jumps and cross-bank
reads; 70/70 is an extraction count, **not** proof of 70 correct C functions.

The first attempt also exposed a Ghidra symbol-collision condition: an
imported secondary user label already existed at a function address, and
renaming the function to that label raised `DuplicateNameException` and
aborted the entire script. The exporter now checks existing address symbols
and preserves an already-named alias instead of failing a nonessential
rename. The corrected exporter completed on the first project's existing
analysis and on the fresh full run.

The `0x6d564` decompilation displays the Beach-result flag `0x78fa`, direct
technician/pin relationship check, move of pin `0x2a9` to room `0x29b`, and
clear of property `0x08`. The later Beach native replay traced its normal
trigger path; see below.
See [membership-pin.md](membership-pin.md) for the original-instruction
cross-checks and precise scope of native validation.

## Beach VR object handlers: fifth run and live reward confirmation

A further fingerprinted Ghidra 11.4.2/JDK21 run selected four more section-50
object handlers: Beach room `0x2ae` (`OVL:0x69170`), scanner `0x2b3`
(`0x69cd1`), frosted glass `0x2b4` (`0x69d63`) and bartender `0x2bb`
(`0x69649`). All **74/74** contexts exported with their original file offsets
and provisional roles. The Beach room's event jump table still has unresolved
indirect jumps. The glass handler's plain conditional branches expose the
drink-refresh word `0x78f6` and success byte `0x78fa`.

The original game was then navigated through real typed inputs to Beach,
glass pickup, repeated bartender interactions, and scanner placement. Native
traces verify the counter reaching **5**, success byte changing **0 → 1**
at 109250 ms, section-51 return at 109251 ms, and membership pin relocation
into VR Terminal at 111158 ms. This validates the earlier static reward
path; unlike the controlled Casino gate test, **no guest state was poked**.
The subsequent 39-command original-game replay also removed the VR collar,
stood up, took the awarded pin (at 117678 ms), and walked from the Casino
through its event-`0x0f` callback into Pedroza Lounge (at 132268 ms).
The final native pin relation is player `0x0d3` and player room is `0x0f6`.
The entire sequence, native watch interpretation and ordered-event assertion
are documented in [beach-vr-replay.md](beach-vr-replay.md).

## Tanning Room and VR couch handlers: sixth headless run

A fresh Ghidra 11.4.2 / JDK 21 import of the same fingerprinted binaries
revalidated **58** overlay spaces, **13,947** relocations and **696** object
aliases; **81/81** requested pseudocode contexts exported. Seven additional
selected object handlers cover Casino `0x0f5`, Pedroza Lounge `0x0f6`,
Tanning Room `0x29a`, its couch `0x29c` and couches collection `0x29d`,
VR Terminal couch `0x2a1`, and collar `0x2a8`. The preliminary handlers
contain event branches and native location/relation checks; several room
wrappers still have computed jump tables and unresolved Ghidra edges.

An original-instruction and two-replay cross-check resolves Tanning Room
`UP`: callback `0x2b` (`OVL:0x693fc`) sees event `0x0f` and returns
`AX=0` in both cases. If the player is directly inside couch `0x29c`,
it nevertheless invokes the native setter to move the player to room
`0x29a`; otherwise it leaves the player standing. See the new
[Tanning Room native analysis](tanning-room.md) for evidence, reproducible
commands and the limited C++ translation.

## Callback `0x2b`: Terminal and Beach native cross-check

The sixth Ghidra export's `GATEMAIN.OVL:0x693fc` callback has now been
cross-checked against **two additional native Unicorn replays**. At VR
Terminal `0x29b`, the collar equipment predicate prevents getting up from
couch `0x2a1` while worn; after removing it, `UP` writes player
`0x2a1 → 0x29b`. At Beach `0x2ae`, `UP` from nested `0x2b1` writes player
`0x2b1 → 0x2ae`; direct room `UP` makes no move. Each event `0x0f`
returns `AX=0`, including those which change the player's relation.

The original Beach branch also calls a native clear of glass property `0x0a`
(not independently validated as a changed bit by the player-only trace).
Ghidra's C-like output still needs original-instruction ABI cross-checks.
The [VR callback investigation](vr-up-callback.md) documents both traces,
original offsets, asset-independent tests and bounded C++ model. No seventh
Ghidra import was needed for this step.

## Study VR: selected interior offset context and seventh run

An original-game `SET SWITCH TO STUDY` replay identified the final
`0x2b` current-room branch (room `0x2bc`, leather couch `0x2be`). A fresh
Ghidra 11.4.2 / JDK21 import selected its room, couch, desk, chair and
Sigfreid `0x2ca` object handlers plus original OVL offset `0x6cfda`.
The first attempt exported **25/26** contexts; Ghidra correctly placed
`0x6cfda` *inside* the containing function at `0x6ce85`, so the earlier
export script treated the offset as a missing independent function.

`DecompileGateway.py` now uses the containing function for a manually
selected `overlay-entry-candidate` that lies in analyzed code, records
`context=inside-containing-function`, and avoids renaming or fabricating
an independent entry. It retains strict entry checks for confirmed handler
addresses. The fresh retry exported **26/26** contexts, revalidating **58
overlays, 13,947 fixups**, and the signed `ES:7904` Study counter logic.
Its room handler still has an unresolved computed event jump.

Original Unicorn replay verified a normal `AX=0` refusal at counter `1`
and a **controlled, explicitly logged** `AX=0` get-up at counter `3`;
both use the same callback and the successful native move writes player
`0x2be → 0x2bc`. Details and commands in [Study VR](study-vr.md).

## Study therapy: eighth headless run and recovered native jump table

The earlier controlled Study get-up has been superseded by a complete
**unmodified 19-stage therapy sequence**. An original-game Unicorn replay
advances signed state word `0x7904` naturally from 1 through 20 using the
two original far-pointer prompt/response tables in section 51. The original
Study DOWN callback `0x05` in section 50 was also exercised: it refuses
leaving the desk at progress 10 and moves player `0x2bf → 0x2bc` at 13,
still returning AX=0. The [Study native analysis](study-vr.md) gives every
command, watched word writes, and the bounded C++ translation.

A fresh Ghidra **11.4.2/JDK21** run supplied the complete **44 callback
directory**, three selected Study handlers, and manual code offsets
`0x6ce85` and `0x6cfda`, yielding **69/69 exported contexts**. It again
validated 58 independent overlay sections and 13,947 relocations.
The decompiler confirms that `0x6ce85` is the enclosing Study progress
function and `0x6cfda` an interior context—not another function.

Study room handler `0x6b521` still warns about a computed indirect jump:
Ghidra cannot read `ram:4000:0b82`, the reference it infers for
`CS:[BX+0x0b82]` inside RTLink section 51. The original OVL contains a
**23-word jump table at `0x6b852`**, verified by the section-51 image base
`0x6acd0`. Its exact event→file-offset mapping has now been decoded in
[Study](study-vr.md); these remain internal branches, not invented
function entries. This highlights why the isolated overlay spaces and
original-instruction cross-checks remain essential for Ghidra's preliminary
C-like output.

The importer now also **marks the 23 jump-table entries as typed u16 data**
and creates 23 same-overlay data references to the verified internal targets.
It labels the table `gw_study_event_jump_table` at section-51 offset
`0x0b82`, adding per-entry event→original-file-offset comments. Every word
is compared with the original bytes before Ghidra creates its reference;
the preparer excludes this Gateway-specific annotation for synthetic/unknown
image fixtures. The import report now exposes `study_event_table_words=23`,
and the runner rejects a fingerprinted import missing those annotations.
This makes the table navigable **without claiming the decompiler can
reconstruct the indirect switch**.

## Study event-1 matcher: ninth headless import and native byte watches

Event 1 at original OVL `0x6b692` leads to a native action/object matcher
at `0x6b6c0–0x6b7e8`. It indexes `[DS:9652]:02a8` for 20 action bytes,
`:02aa` for corresponding expected object words, and `:02fc` for five
alternate action-code pairs; original guest RAM and disassembly agree.
The read-only decoder `tools/inspect_study_script.py --format json` exports
these alongside both far-pointer text tables. Its native semantics are
represented by a bounded C++ predicate and tested independently of assets.

The importer now checks nine verified original opcode anchors and creates
named **interior instruction labels** for actor, target, action, primary
comparison/flag write and alternate-record traversal. The new
`study_matcher_path_labels=9` import report field is checked by
`tools/run_ghidra.py` for fingerprinted original binaries. No new function
boundaries are claimed and no addresses are attached to synthetic overlays.
The fresh Ghidra 11.4.2/JDK21 run again validated **58 sections, 13,947
fixups, 23 table entries, 9 matcher labels and 69/69 contexts**. Its
`ram:4000:0b82` warning persists; decompiler output remains preliminary.

An independent, *unmodified* Unicorn replay watched the physical `:7909`
completion byte: the original room matcher at OVL `0x6b740` set it for
`SCREAM` at 75235 ms and `FEEL COUCH` at 78361 ms. The `UP` stage set
it through the separate response handler at OVL `0x6ceec` (81141 ms),
following the exit callback. All three clears executed at original
OVL `0x6d01c` and native progress reached 4. The inspector
`tools/inspect_study_action_match.py` proves these exact execution paths
and refuses a guest-RAM intervention or incorrect trace origin. This
narrows what is *actually* demonstrated by decompilation of event 1; the
19-stage progress replay remains separate evidence for the full therapy.

## Study response and terminal SLEEP: tenth headless run

The original section-51 response function at `0x6ce85–0x6d04d` has been
cross-checked instruction-by-instruction. Its native state machine uses
progress `:7904`, previous-prompt `:7906`, reminder `:7908`, and match flag
`:7909`, plus direct player-parent tests at stages 3 and 13. It selects
instruction `f000:70af`, repeat `f000:70ad`, insistence `f000:70ae`,
successful response `f000:70b1` or final sleep `f000:70b0`.

The Study jump-table import now also adds **seven unique COMPUTED_JUMP
references from the actual CS:[BX+0x0b82] opcode**, in addition to the
23 data-word references. The importer byte-checks and names **18 interior
response-state-machine instructions**, including the two posture checks,
the four state fields' writes, stage advancement, and the exceptional
`0x6d564` auxiliary call. Its fresh Ghidra 11.4.2/JDK21 import report
confirms **58 overlay sections, 13,947 relocations, 23 event words, 7 flow
edges, 9 action matcher labels, 18 response labels, 69/69 contexts**.
The runner rejects incomplete counts for fingerprinted originals.

Despite the correct user-defined computed edges, Ghidra still warns about
`ram:4000:0b82` when decompiling the Study room handler. Thus the references
make the seven destinations navigable but **do not repair the segmented
decompiler switch**; source-offset/ABI checking remains essential. The
response handler itself decompiles with the state branches shown above.

The earlier 19-stage exercise table has one more record at stage **20**:
action byte `0x84`, wildcard object target, prompt string `sleep` (a valid
guest pointer located at prompt index 20 / response index zero). This is
an end action, not a twentieth counter increment. An original native
`SLEEP` replay sets `:7909` at 132205 ms, calls auxiliary OVL `0x6d564`
at 132277 ms, and returns player to VR Terminal in two verified location
writes at 132438 ms, with progress still 20. A contrasting original
repeated-LOOK replay sets reminder `:7908` and takes the insistence path
back to Terminal without advancing past stage 1. A new multi-byte watcher
and `tools/inspect_study_response.py` validate both original paths.
See [Study VR](study-vr.md) for reproducible commands, exact branch offsets
and the bounded C++ translation.

## VR return dispatcher: eleventh headless run and pending frame

A fresh Ghidra 11.4.2/JDK21 import selects the shared section-51 return
routine `OVL:0x6d564` and VR Terminal handler `0x29b` in addition to
the Study code. It validates the same **58 RTLink sections, 13,947 fixups,
23 Study event words, seven computed-flow edges, nine action-matcher labels,
18 response labels**, and the new **24 byte-checked VR-return instruction
labels**. All **71/71 selected decompiler contexts** exported. The remaining
segmented computed-jump warnings are unchanged.

This routine's preliminary C-like pseudocode suggests two successive player
moves, but native execution reveals an important frame boundary: the call
to resident `048e:0284(0x29b)` leaves the section-51 far-call frame pending
after moving the player into Terminal room `0x29b`. Once the resident call
returns, the frame continues at original `0x6d73b`, moves the player into
Terminal couch `0x2a1`, and then executes `retf` at `0x6d747`.
The original Beach branch awards pin `0x2a9`; the
Study return selects its non-award, result-zero path. A later idle-tail
control proved that no subsequent command is required for this VR frame to
complete; see [Resident room transfer](room-transfer.md). Both are compared
against unmodified native replays in [VR return](vr-return.md). The result-7
branches are static-only and their story semantics are unknown.

## Resident room transaction: twelfth headless run and verified native ABI

The resident `048e:0284` callee is now established as **EXE file `0x5d5d`**,
a real function separate from the room-exit scanner at `0x60cf`. Its
successful far-call ABI accepts the destination room in `[BP+6]`, first
dispatches event `0x0f` to the old room (possibly vetoing the transition),
then commits `:8c4c/:35c6`, moves the native actor with `1c24:13d5`, and
dispatches destination events `0x0d` and `0x0e` around the room-entry
presentation path. The original successful return is `AX=1` at `0x60cb`.

The Ghidra 11.4.2/JDK21 importer adds **one function entry and eleven
byte-signature-checked instruction-only resident labels**. The fresh
headless run verified **58/58 RTLink sections, 13,947 relocations** and
exported **78/78 decompiler contexts**. Ghidra auto-analysis invents
separate containing functions for **six interior tail labels** around
`0x5f88–0x5fc8` and `0x60cb`. The export explicitly marks their
`context` as `ghidra-split-tail`: the original x86 instructions jump there
inside the verified transaction, rather than calling a distinct ABI.
Other segmented computed-jump warnings remain.

Native idle controls also corrected the presumed VR continuation contract.
Without a later key, ordinary EAST remains blocked in native room text and
retains the saved far-return frame. The Study VR return **does complete
without another key**: presentation calls `1c24:12db` and `05bd:000a`
account for 1,082 and 1,909 guest milliseconds respectively, then the
resident `RETF` leads directly to the section-51 Terminal-couch move.
See [Resident room transfer](room-transfer.md) for the original PC map,
replay scripts, and read-only native stack and location-write verifier.
