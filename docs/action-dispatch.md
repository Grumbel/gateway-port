# Gateway: action directory and RTLink action references

Research: 2026-11-01. Sources: the original `GATEMAIN.EXE` and
`GATEMAIN.OVL`, whose SHA-256 values are documented in
[executable.md](executable.md), plus the native `objects` replay's action
table and command traces. All hexadecimal OVL offsets below refer to **file
offsets**, not loaded CS:IP addresses.

## What the live action table actually points to

The action dispatcher at `1476:0046` selects entries from the table whose
segment is stored at `[DS:8da8]`. Two adjacent resident functions in the
original EXE establish *different accesses* to the same six-byte stride:

- At original `GATEMAIN.EXE` file offset `0x5956`, a function indexes
  `ES:[0x27 + 6*action_id]` as a **byte**, then treats its nonzero value as
  a selector into another four-byte far-pointer table at the beginning of ES.
- At file offset `0x5994`, a second function indexes
  `ES:[0x28 + 6*action_id]` as a **far pointer**, then issues an indirect far
  `CALL`. The latter function corresponds to the observed `1476:0046`
  dispatcher; the call instruction is expected at runtime `1476:0060`.

The old inspector reported a third `u16` at `0x2c + 6*action_id`, labelled
`other`. **This was a misread:** that word combines the trailing byte of the
current entry with the selector byte of the *next* entry. The revised inspector
displays `selector` and `trailer` as separate, unclassified bytes, and no
longer implies there is a third word in a six-byte action record.
The `look` action `0x5f` points directly to resident code at `148e:0b90`.
The other five actions exercised in the replay instead point into the RTLink
manager segment `2c24`.

These `2c24` pointers do **not** point to runnable instruction starts. They
point to a **six-byte RTLink reference** embedded after the `e8 disp16; ea`
prefix of a ten-byte trampoline. The reference consists of a far destination
offset, far destination segment, and section ID. The manager's EXE image starts
at file offset `0xa020` and has linked segment `1c24`; the observed runtime
load adds `0x1000` to both manager and referenced overlay segments.

One directly checked example from the original EXE:

```text
action 0x71 table pointer = 2c24:1c59
GATEMAIN.EXE file offset 0xa020 + 0x1c59 = 0xbc79
bytes there: 11 1a 61 36 07 00
   target offset = 0x1a11, linked segment = 0x3661, RTLink section = 7
   observed runtime target = 4661:1a11 (linear 0x48021)
```

Reading the on-disk RTLink directory (`0xac3f + 18*section_id`) and applying
the verified image mapping in [rtlink.md](rtlink.md#directory-layout) gives:

| Typed input / action | Table pointer | RTLink reference (linked segment:IP, section) | OVL file offset |
| --- | --- | --- | ---: |
| `close drawer` / `0x1d` | `2c24:17f9` | `3661:0cf7`, section 7 | `0x14e47` |
| `drop book` / `0x32` | `2c24:1727` | `3661:08b4`, section 6 | `0x11454` |
| `take book` / `0x46` | `2c24:165f` | `3990:1da1`, section 9 | `0x1a831` |
| `inventory` / `0x53` | `2c24:15dd` | `399f:2437`, section 15 | `0x22357` |
| `look` / `0x5f` | `148e:0b90` | Resident entry, no RTLink reference | — |
| `open drawer` / `0x71` | `2c24:1c59` | `3661:1a11`, section 7 | `0x15b61` |

These mappings are directly checked against the original EXE records and
section directory. They identify **referenced code**, not necessarily the
shared implementation that actually performs the requested operation. The
contents demonstrate why that distinction matters: the candidate target at
`0x14e47` discusses drinking, while the one at `0x15b61` discusses beds and
sleeping. Neither resembles a general drawer open/close routine. The roles of
the selector and trailing bytes are not fully understood. An action
reference alone does not establish which branch was executed, whether it ran
before/after generic handling, or which service ultimately changed the world
state.

To inspect the decoded code and text at one of these references:

```sh
python3 tools/disassemble_story.py ../gateway-dos/GATEMAIN.OVL \
  --start 0x14e47 --size 0x170 --strings ../gateway-dos/GATESTR.DAT
```

## Reproduce the mapping from a native snapshot

After building `gateway-overlay` and running the `objects` scenario:

```sh
./build/gateway-overlay ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --fixups > build/rtlink-manifest.json
python3 tools/inspect_action_dispatch.py build/story-commands-043/native \
  --overlay-map build/rtlink-manifest.json
```

The inspector now reads the six-byte references from *live* guest memory,
recognizes records in the known `2c24` manager segment, and optionally maps
each to its original overlay file offset using the manifest. It leaves the
resident `look` action as a direct pointer. Do not disassemble at the `2c24`
table pointer: it is metadata, not the first instruction of the action handler.

## Outcome of candidate-target replay, and next dynamic check

The user ran `--scenario objects --services state --verify-overlays` with
the initial two candidate hooks, and **neither target was entered**. All three
replays completed with 187 verified loads across nine sections, identical
734-object state, service-call sequences, and framebuffer hashes. The
verified loaded sections were `0–5`, `37`, `38` and `56`; **section 7 never
loaded**. Its `0x47307` and `0x48021` addresses are not reliable function
entry points in that execution. A successful OPEN/CLOSE therefore need not
pass through the two inferred section-7 bodies. We cannot yet tell whether
the far pointers themselves are replaced at dispatch time, whether a separate
selector path is used, or precisely where the shared OPEN/CLOSE state change
is implemented.

The next native replay instead hooks `1476:0046` (entry), `1476:0060`
(expected `call far es:[bx+28]`), and `1476:0065` (expected post-call). The
research trace records live opcode bytes at these addresses; the callsite
hook also reports the **runtime far pointer actually being called**, if its
instruction signature matches. This checks our proposed file-to-runtime
mapping rather than assuming that table entries are callable code.

A subsequent `--native-only` replay from the user **did** reach the callsite
for both operations: OPEN (`0x71`) entered at 18,392 ms and returned with
`AX=1` at 18,416 ms; CLOSE (`0x1d`) entered at 34,422 ms and returned with
`AX=1` at 34,443 ms. The trace contained no `action_code=` or `action_target=`
and also omitted the newly added `BX`/`CX` fields, so the C++
`gateway-probe` executable was an older build. The entry hook was accidentally
installed twice for `147a6` by the Python driver, explaining its duplicate
lines. Neither result identifies the live far-call destination yet.

The replay script now prefers `$GATEWAY_BUILD_DIR/gateway-probe` under
`nix develop` (falling back to `build/gateway-probe` elsewhere) and checks the
binary for the `action_code=` diagnostic before starting. This fails quickly
with an actionable build instruction instead of silently producing another
trace without the necessary fields.

```sh
gateway-build  # in nix develop; builds $GATEWAY_BUILD_DIR/gateway-probe
python3 tools/trace_game_logic.py --scenario objects --native-only \
  --verify-overlays --output build/action-dispatch-check
rg '^entry .*pc=(147a6|147c0|147c5)\b' build/action-dispatch-check/native.trace \
  | rg 'ms=(18[0-9]{3}|34[0-9]{3})\b'
```

Inspect `action_code=` at entry/call/return and `action_target=` at the
callsite, particularly around `open drawer` at 18,392 ms and `close drawer`
at 34,422 ms. An absent hook or unexpected opcode is a useful result: the
file-to-memory mapping must then be corrected before naming any handler.
`--native-only` avoids rerunning verification/HLE during this diagnostic;
omit it to repeat the full three-way regression check.

To build a replacement action layer, we still need the dispatcher path that
handles an object's **unhandled** result. Opening and closing the drawer both
return zero from the object-specific event-6 handler, and a separate shared
path must account for the observed property-12 mutation. The references above
are candidates to inspect, not yet established generic action implementations.
