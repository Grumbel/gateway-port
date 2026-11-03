# Native handler inventory and RTLink call coverage (bundle 095)

The native snapshot contains **734 object records**, of which **709** carry a
nonzero handler index. These refer to **696 different handler IDs** and, after
validating their ten-byte RTLink stubs, **696 different section-qualified
native entry addresses**. Nine handler IDs occur on multiple objects (13
additional object assignments); address deduplication does **not** collapse
the 696 handlers into a small set of wrappers. The **44** native kind-4
room-exit callbacks have no entry address in common with the object handlers.

This inventory identifies *candidate entry points*, not 696 decompiled,
understood functions. A handler ID is not an action code or an independent
story script. Native resident parser and action-dispatch code, non-object
overlay helpers and resource systems account for additional logic.

## Section-aware runtime evidence

`gateway-probe --trace-rtlink-calls FILE` installs one opt-in Unicorn hook at
the RTLink manager's **`2c24:058d`**, physical `0x2c7cd`. When the native
ten-byte `CALL manager; JMP far target; WORD section` trampoline reaches the
manager, the near-return IP on the guest stack points back to its stub. The
hook checks the active CS, `E8`/`EA` opcodes, call displacement, section ID,
target range and stack-return bounds before recording a call. Its output is
aggregated by `(section ID, target CS, target IP)`, with a count and first/last
virtual millisecond. A separate counter flags manager hits that cannot be
attributed to a verified trampoline. There is no per-instruction trace volume.

Crucially, sections reuse the same physical DOS address at different times.
The section word in the *original trampoline* disambiguates the calls; a
linear-PC-only coverage bitmap would conflate unrelated overlays. This trace
measures **trampoline invocations**, not successful handler returns,
per-instruction execution, unique behavioral branches, or percentage of story
logic implemented in C++. The manager may also dispatch internal utilities,
which are reported separately from object handlers and room callbacks.

The older `--trace-blocks FILE` option still dumps only the **last 256**
executed basic blocks, and must not be used as whole-replay code coverage.

## Reproduce against the original DOS binaries

Run after preparing the fingerprinted Ghidra plan with
[`docs/ghidra.md`](ghidra.md). Everything below writes to generated `build/`
paths and a separate replay state directory; no DOS assets are included in Git.

```sh
python3 tools/trace_game_logic.py \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" \
  --data ../gateway-dos --output build/handler-coverage \
  --scenario objects --native-only --verify-overlays --trace-rtlink-calls

python3 tools/inspect_object_state.py build/handler-coverage/native \
  --names ../gateway-dos/OBJECT.DAT > build/handler-coverage/objects.json

"${GATEWAY_BUILD_DIR:-build}/gateway-overlay" \
  ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --fixups > build/handler-coverage/rtlink-manifest.json
python3 tools/inspect_room_callbacks.py build/handler-coverage/native \
  --overlay-map build/handler-coverage/rtlink-manifest.json \
  --format json > build/handler-coverage/callbacks.json

python3 tools/inventory_native_handlers.py \
  build/handler-coverage/objects.json build/ghidra/ghidra-plan.json \
  --callbacks-json build/handler-coverage/callbacks.json \
  --rtlink-calls build/handler-coverage/native.rtlink-calls
```

Use `--format json` for an auditable machine-readable inventory, including
original OVL file offsets, object IDs/names, native sections, callback IDs,
per-entry counts, unclassified RTLink targets and exact 32-byte prefix hash
groups. Omitting `--rtlink-calls` leaves runtime counts **unknown**, not zero.
The tool rechecks mapped file-backed boundaries and Ghidra relocated image
SHA-256 values instead of trusting addresses copied into an older snapshot.

The commands assume a prepared `build/ghidra/ghidra-plan.json` referring to
the section images in that preparation. Re-running the preparer in another
directory is fine; use that plan path instead. The original files remain
external to the source repository.

## Measured first-room replay

The standard seven-command `objects` script (OPEN DRAWER, LOOK, TAKE BOOK,
INVENTORY, DROP BOOK, CLOSE DRAWER, LOOK) against the original GATE.EXE
launcher, with native overlay execution and overlay relocation verification,
produced the following observations on the supported 1992 binaries:

| Scope | Invocations / entries observed |
| --- | ---: |
| RTLink manager calls, validated | 13,263 / 13,263 (0 unrecognized) |
| Distinct invoked RTLink targets, including utilities | 42 |
| Known object handlers entered through trampolines | **5 / 696** (80 calls) |
| Known room-exit callbacks entered | **0 / 44** |
| RTLink calls outside the two tables | 13,183 |
| Native overlay loads verified | 187 (9 distinct sections) |

Observed object handlers (from section-qualified native pointers):

| Handler ID | Object ID | Object | OVL section | Calls |
| ---: | ---: | --- | ---: | ---: |
| 210 | `0x0d3` | you | 56 | 16 |
| 233 | `0x0ee` | Your Quarters | 38 | 57 |
| 282 | `0x123` | book | 38 | 2 |
| 283 | `0x124` | drawer | 38 | 3 |
| 284 | `0x125` | desk | 38 | 2 |

The largest unclassified RTLink target is **section 0, original OVL
`0x0d7e`, with 12,977 calls**. It accounts for most trampoline traffic in
this scenario. A later 2026-11-03 bar campaign isolated its **single caller**
at `27cd:06b0`, mapped to original `GATE.RTL:0x16049` just after a call
at `0x16044`: the RTL loop repeats while its return `AX` is nonzero. The
enclosing RTL routine subsequently checks several keyboard input codes.
That establishes a repeated check, **not yet the overlay helper's full
meaning, return contract or all side effects**. See [golden replays](golden-replays.md).
High call frequency is not a
measure of game-logic complexity. Zero observed calls to an unvisited room's
callback says nothing about that callback's behavior, reachability or code
coverage in other scenarios.

## Decompilation triage

Comparing the 32 relocated bytes from each known entry finds **21 clusters
covering 50 handler entries** with byte-identical prefixes. These include
`Ahriman` / `Azazel` / `Abaddon` / `Aeshma` (handler IDs 52–55, section 34),
the `lilypads` / `cattails` / `ferns` handlers (199, 200, 202, section 14),
and `shirt` / `shorts` / `sandals` (664–666, section 50). This is a search
aid, **not 29 verified shared functions**: the bodies or data beyond byte 32
could diverge, and Ghidra's function boundaries still require confirmation.

A fresh Ghidra 11.4.2/JDK21 run then selected five new candidates while
retaining the established native function/branch contexts. Its export reports
**37/37 target contexts decompiled** (including resident contexts with
known caveats). The *preliminary, unvalidated C-like output* shows:

| Original OVL offset | Candidate | What is visible in Ghidra |
| --- | --- | --- |
| `0x0d7e`, section 0 | Highest-volume non-object target | Tests several state words and calls further helpers; the apparent boolean return and inferred state locations need tracing |
| `0x4992f`, `0x49a47`, section 34 | Ahriman and Azazel handlers | Parallel tests on incoming values `4` and `6`, but object-specific constants differ (including IDs `0x34` and `0x35`); the same prefix does **not** make them interchangeable |
| `0x51b81`, section 38 | Desk handler (`0x125`) | Distinct branches for incoming values `4` and `6`; the latter further tests byte-sized selectors and invokes other native helpers |
| `0x6a3f8`, section 50 | Shirt handler (`0x2b8`) | One apparent value-`6` branch with two byte-selector cases; no runtime invocation yet witnessed |

To repeat these targeted decompilations, add the following to the normal
`tools/run_ghidra.py` command (with `--objects-json` pointing to the native
snapshot exported above):

```sh
  --decompile-offset 0xd7e \
  --decompile-object 0x34 --decompile-object 0x35 \
  --decompile-object 0x125 --decompile-object 0x2b8 \
  --project-name GatewayHandlers095 --output build/ghidra-handlers
```

This produces `pseudocode.txt` and `ghidra-decompile-report.json`; those
outputs contain original game code, are generated locally and are **not**
committed or distributed. Success means Ghidra produced text, not that it
resolved segment registers, indirect jumps, state names, or branch semantics.

A separate **unmodified native replay** hooked the desk entry at linear
`0x4a481` (`CS=0x49d8`). It recorded precisely two calls, at virtual
`18,392 ms` and `34,422 ms`, with the first guest argument word equal to
`0x0005` both times (after the far return pointer). The RTLink census again
counted exactly two desk invocations and the framebuffer matched the baseline.
Since the provisional Ghidra listing only has explicit handling for values
`4` and `6`, these two events appear to take its default-return path; **they
do not validate either substantive desk branch**. This illustrates why a
handler counted as invoked is not equivalent to its logic being understood.

Suggested next native/Ghidra experiments:

1. Validate the high-frequency section-0 candidate at original file `0x0d7e`
   against frame/UI events in native traces, and confirm its state accesses.
2. Compare complete function boundaries and call sites for the identical
   prefix groups. If a common helper is proven, name it once and document
   each call site's object-specific arguments/state.
3. Run several room-navigation and conditional-exit scenarios with the same
   census. Verify room callback return values with the resident scanner hooks;
   a callback *call* does not prove a successful transition.
4. Track *branch* coverage separately if needed, preserving section residency
   and original file offsets; a last-256-block snapshot is insufficient.

This is a measured sample of behavior, not a numerical estimate that 5/696
of the game's story has been understood. Previously confirmed room transfer,
action selection and object mutation logic remains documented in
[`room-transfer.md`](room-transfer.md), [`game-logic.md`](game-logic.md),
and [`action-dispatch.md`](action-dispatch.md).

For event-aware callback coverage across ordinary Casino, Beach reward, and
VR `UP` replays, see [callback-event-coverage.md](callback-event-coverage.md).
Bundle 097's [handler evidence pipeline](handler-evidence.md) adds selected
native function entry/return snapshots, byte-level object-record deltas and
Ghidra static call-reference cross-checks without conflating them with total
story-logic coverage.
