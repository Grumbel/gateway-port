# Ghidra: original Gateway native code and RTLink overlays

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

If Ghidra is already installed elsewhere, `nix develop` plus
`--headless /path/to/ghidra/support/analyzeHeadless` works too. The runner
checks for `analyzeHeadless` on `PATH` by default. It uses a new project; it
**does not** overwrite an existing Ghidra project. Choose another
`--project-name` for another import, or open the existing one in Ghidra.
For Nix's `ghidra-bin` package, the runner also searches the installed
`lib/ghidra/support/analyzeHeadless` relative to the `ghidra` wrapper.

Expected output (generated, ignored by Git, contains original game code):

| Path | Contents |
| --- | --- |
| `build/ghidra/ghidra-plan.json` | Verified addresses, section/parent/overlap metadata and labels |
| `build/ghidra/sections/section-NNN.relocated.bin` | RTLink images with 13,947 validated segment fixups across 58 sections |
| `build/ghidra/project/Gateway.gpr` | Ghidra project, with DOS MZ program and independent overlay spaces |
| `build/ghidra/ghidra-import-report.json` | Exact label counts and original-MZ signature matches |
| `build/ghidra/pseudocode.txt` | Decompiler output for confirmed native function-entry candidates, or explicit errors |

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
| EXE | `0x60cf` | `gw_room_exit_dispatch` | Type-1 exit scanning and branch on kind |
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
known entry points that Ghidra accepts as functions. The scripts use Ghidra's
standard Jython/Java APIs, rather than an unsupported RTLink-aware MZ loader.

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

Useful next trace target: a kind-4 exit callback (236 of the 518 indexed native
exit entries have this kind). The resident dispatcher at EXE `0x6269` receives
its return value, which can be correlated with `--trace-navigation` output and
the corresponding overlay callback pseudocode. This is a promising path toward
the modern engine's room-navigation layer.

The headless Ghidra runtime could **not** be executed in the agent's workspace
because Ghidra/Nix are not installed there. The preparation was exercised
against fingerprint-matching original binaries (58 sections, 13,947 fixups)
and its asset-independent tests; the user's first `nix develop .#ghidra` run
will validate the installed Ghidra version's import/decompile APIs.
