# Gateway: action directory and RTLink action references

Research: 2026-11-01. Sources: the original `GATEMAIN.EXE` and
`GATEMAIN.OVL`, whose SHA-256 values are documented in
[executable.md](executable.md), plus the native `objects` replay's action
table and command traces. All hexadecimal OVL offsets below refer to **file
offsets**, not loaded CS:IP addresses.

## What the live action table actually points to

The action dispatcher at `1476:0046` selects six-byte records from the table
whose segment is stored at `[DS:8da8]`. The record begins at
`0x28 + 6*action_id`. It contains a far pointer and an unclassified word.
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
sleeping. Neither resembles a general drawer open/close routine. The pointer
table's extra word is not understood and could affect dispatch. An action
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

## Next dynamic check

`tools/trace_game_logic.py --scenario objects --services state` now installs
entry hooks at the observed loaded targets `0x47307` (close reference) and
`0x48021` (open reference), in addition to the already-confirmed drawer
handler hook. The presence or absence of entry records at these addresses in
a native replay helps determine whether the references execute during the
two tested commands. Overlay residency can change; use
`--verify-overlays` and check `overlay-load` records before attributing a
hit to a specific on-disk section. A non-hit is evidence only for that bounded
replay, not proof that the referenced routine is unreachable.

```sh
python3 tools/trace_game_logic.py --scenario objects --services state \
  --verify-overlays --output build/action-reference-check
rg '^entry .*pc=(47307|48021)\b' build/action-reference-check/native.trace
```

To build a replacement action layer, we still need the dispatcher path that
handles an object's **unhandled** result. Opening and closing the drawer both
return zero from the object-specific event-6 handler, and a separate shared
path must account for the observed property-12 mutation. The references above
are candidates to inspect, not yet established generic action implementations.
