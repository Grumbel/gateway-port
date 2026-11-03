# Section-qualified handler evidence pipeline (bundle 097)

The [walkthrough replay driver](walkthrough-replay.md) can collect these same
RTLink census and native entry traces across longer, checked story paths.

The object inventory contains **696** distinct native object handler entries,
and the room callback table contains **44** more entries. Neither an entry
label nor provisional C pseudocode proves a handler's complete behavior.
Bundle 097 adds a reusable, opt-in toolchain that joins three independent
kinds of evidence:

1. An RTLink trampoline's section and target address, verified against its
   *live* ten-byte native stub (`--trace-rtlink-calls`).
2. Entry registers, the caller's actual far-return address, observed return
   registers, and before/after **734 original object-record** bytes for a
   small selected set of targets (`--trace-rtlink-events`).
3. Ghidra's statically discovered call references, keyed to the selected
   containing function and original OVL file offsets where available.

The instrumented probe executes original x86 code unchanged. Native record
deltas cover raw object bytes and child/link fields, but **not** arbitrary
global data, text output, helper calls with no visible state change, or all
intermediate writes. A function's observed changes include nested callees.
An observed far return is paired by actual original far-return `CS:IP`,
`SS`, and the stack position after `RETF` (allowing small argument cleanup);
the function may still have unobserved alternate exits. The log records
unfinished calls rather than fabricating a return. Snapshot failures are
marked `unavailable`, never interpreted as no mutation.

Only explicitly selected `SECTION:CS:IP` targets are logged in detail,
with a hard cap of **256 calls per replay**. The separate aggregate census
still counts every validated trampoline; a sampled count must never be
mistaken for total call frequency. All section numbers are decimal, and
segment/offset values are hexadecimal. Numeric event inputs and `AX` returns
are raw values, not automatically action outcomes.
For extremely frequent targets, `gateway-probe --trace-rtlink-after-ms N`
defers the first detailed event until guest millisecond `N`, keeping the
256-call limit and unchanged full aggregate census. It requires the usual
`--trace-rtlink-events` file and selected target(s). Walkthrough runners
forward it with `--probe-arg=--trace-rtlink-after-ms --probe-arg=N`.

## Repeat with the original DOS files

With `gateway-probe` and `gateway-overlay` built and the original
`GATEMAIN.EXE`, `GATEMAIN.OVL` and game assets in `../gateway-dos`:

```sh
python3 tools/trace_game_logic.py \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" \
  --data ../gateway-dos --scenario objects --native-only \
  --verify-overlays --trace-navigation --trace-rtlink-calls \
  --trace-rtlink-target 48:4990:1a19 \
  --command-sequence 'east,east,east,east,look,inventory,look' \
  --output build/handler-events-casino

python3 tools/inspect_rtlink_events.py \
  build/handler-events-casino/native.rtlink-events
```

The `48:4990:1a19` target is the verified native Casino callback `0x14`;
the target `50:4990:02ec` is the native VR callback `0x2b`. The two targets
share a load-bank segment but have **different section identities** and
cannot safely be merged by linear address alone. For the VR Terminal replay
use the exact original command sequence in [vr-up-callback.md](vr-up-callback.md)
and add `--trace-rtlink-calls --trace-rtlink-target 50:4990:02ec`.
`--trace-rtlink-target` requires `--native-only --trace-rtlink-calls` and the
probe rejects duplicate or malformed targets.

For the Ghidra side, follow [ghidra.md](ghidra.md), supplying both a verified
native object JSON snapshot and the 44-entry callback table. Each new run
now writes **`ghidra-callgraph.json`** alongside the pseudocode and import
reports. The callgraph contains `edges[]` with static callsite/destination,
optional original OVL offsets, and `unresolved_calls[]` for instructions
where Ghidra did not assign a call reference. It exports **only selected
function contexts**, not an exhaustive call graph of all 58 sections.
`getFunctionContaining()` and static references are Ghidra hypotheses;
indirect far calls and RTLink bank lifetimes still need native verification.

Create the native inventory and merge the sources:

```sh
python3 tools/inventory_native_handlers.py \
  build/ghidra-objects.json build/ghidra/ghidra-plan.json \
  --callbacks-json build/room-callbacks.json --format json \
  > build/handler-inventory.json

python3 tools/build_handler_evidence.py \
  build/handler-inventory.json build/ghidra/ghidra-callgraph.json \
  --replay casino build/handler-events-casino/native.rtlink-calls \
    build/handler-events-casino/native.rtlink-events
```

`--replay NAME CENSUS EVENTS` is repeatable. Use `--format json` for the
entire 696-handler/44-callback indexed dataset, including section-qualified
native addresses, observed input shapes, sampled `AX` results, changed raw
bytes, selected static call references, and conservative unknown-target
entries. The text view ranks interesting entries using a deliberately simple
*triage heuristic*, not a behavioral complexity score. Calls not present in
the supplied replays remain **unobserved**, not disproven or understood.
Neither this index nor any Ghidra source output belongs in the Git bundle;
the scripts and reproduction instructions do.

## Two original native checks

The same fingerprinted 1992 DOS binaries were executed in Unicorn with
original overlays verified, without story HLE or research memory pokes.

| Native scenario | Calls across all RTLink targets | Selected callback invocations and matched returns | Observed record changes |
| --- | ---: | --- | --- |
| Casino EAST denied without pin | 22,432 / 36 distinct targets | `0x14`: **13/13** | **0** changed bytes; the door flag was already clear |
| VR Terminal collar/UP then remove/UP | 88,124 / 42 distinct targets | `0x2b`: **11/11** | **7** changed bytes in **3** objects in the successful `UP` call |

The Casino log confirms 12 event-1 `AX=0x0f6` returns and the event-`0x0f`
refusal `AX=0`. The VR log confirms nine event-1 `AX=0` returns and two
event-`0x0f` `AX=0` returns. In the second exit call, the generic snapshot
diff reports:

- Player `0x0d3`: the location low byte and three child-link bytes.
- VR Terminal room `0x29b`: child-list bytes.
- Couch `0x2a1`: child-list byte.

The exact native write verifier still supplies the independent evidence for
the player relocation `0x2a1 → 0x29b` in [vr-up-callback.md](vr-up-callback.md).
The generic tracer captures the broader *net effect* without knowing that
callback's implementation in advance. A fresh Ghidra 11.4.2/JDK 21 run
completed **83/83** requested contexts (including all 44 callbacks and seven
selected non-callback candidates). It exported **400 static call references
from 71 distinct containing functions**; 51 selected contexts could be
joined to the known handler/callback entries by their original OVL offsets.
These counts are about analysis coverage and exported references, **not
percentage of the game's story logic understood**.

Next investigations can reuse this report to pick repeatedly invoked
handlers and callbacks with observed state effects, then perform controlled
branch variations and write bounded native-vs-C++ tests.
