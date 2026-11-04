# Gateway (1992) native saved-game format

`GATE001.SAV`, `GATE002.SAV`, etc. are the **original DOS game's** save
files. This analysis compares eleven authentic, independently restored saves
spanning the office, DataMan, VR, armory, Nubar, tuning fork, Terri, ship,
prospector flight, planetary landing, and metal-box puzzle. The original
files and any executable/game resources remain external to this repository.

The new [`inspect_gateway_save.py`](../tools/inspect_gateway_save.py) reads
these files directly, without starting Gateway or assuming a native memory
dump. To prove the object data against live execution, pass an optional
Unicorn snapshot:

```sh
python3 tools/inspect_gateway_save.py build/campaign/office/state/GATE001.SAV
python3 tools/inspect_gateway_save.py build/campaign/office/state/GATE001.SAV \
  --objects --compare-snapshot build/campaign/office/native
```

To classify two original saves that have different SHA-256 values, without
replaying DOS or copying game data into the repository:

```sh
python3 tools/inspect_gateway_save.py \
  build/golden/full-fixed-clock/run-a/terri/state/GATE007.SAV \
  --compare build/golden/full-fixed-clock/run-b/terri/state/GATE007.SAV
```

The `difference` output lists modified native object IDs, changed byte
offsets inside the 1,071-byte global block (relative and absolute offsets),
known room/turn fields, and scheduled-event record differences. A
`turn_shift_consistent` result means the only changed global bytes belong to
the *provisional* turn counter and all matching timer IDs have countdowns
shifted by exactly that amount; it is not proof of equal story state.
`run_golden.py compare --objects-only` also prints this breakdown
automatically when a stage's original saves differ.

The parser checks lengths, the description terminator, every native object
handler index, room IDs, and the event-table count. `--objects` includes all
734 object descriptors and their exact offsets; the optional snapshot check
compares **every raw object record**, using a verified `DS`/`ES` object
directory. It does not alter the file or guest RAM.

## Confirmed layout

All integers below are little-endian unless noted otherwise. There is **no
file signature** or explicit version field, so recognition also depends on
the structural checks.

| File offset | Bytes | Meaning | Evidence |
| --- | ---: | --- | --- |
| `0x0000` | 33 | Printable, space-padded save description | Original SAVE/RESTORE UI and filenames |
| `0x0021` | 1 | NUL terminator | All eleven observed saves |
| `0x0022` | 20,650 | Packed original records for object IDs 1–734 | Exact byte match with all 734 live records in eleven snapshots |
| `0x50CC` | 1,071 | Fixed global-state block, partly decoded below | Same offset/size in eleven saves |
| `0x54FB` | 2 | Count `N` of scheduled events | Predicts every measured save length |
| `0x54FD` | `4 × N` | Timer records: `u16 event_id`, `u16 remaining_turns` | Differential checks on original consecutive saves |

Hence the exact observed length is **`21,757 + 4 × N` bytes**. The office
save has five events (21,777 bytes); the first-flight save has two (21,765
bytes); the planetary landing and box saves have one (21,761 bytes). In
eleven samples, `N` ranges from one to five. A zero-event file is
structurally possible but has not been observed.

The object block is a plain concatenation, **without per-object type tags,
record lengths or separators**. Its record sizes follow the same eight
native type descriptors recovered in [object-state.md](object-state.md):

| Kind | Records | Bytes each | Total bytes |
| --- | ---: | ---: | ---: |
| 1 | 110 | 26 | 2,860 |
| 2 | 241 | 36 | 8,676 |
| 3 | 24 | 12 | 288 |
| 4 | 8 | 20 | 160 |
| 5 | 63 | 22 | 1,386 |
| 6 | 15 | 24 | 360 |
| 7 | 46 | 32 | 1,472 |
| 8 | 227 | 24 | 5,448 |
| **Total** | **734** | | **20,650** |

The record-kind sequence and type descriptors are derived structural metadata
embedded in the inspector; no proprietary game bytes are embedded. Object
locations, handlers, relation links, child links, and next-sibling slots use
the **same type-specific offsets and accessor rules as live RAM**, so the
full recovered object tree can be inspected directly from a `.SAV` without
starting the emulator.

## Global state and timers

The following fields are identified within the fixed block starting at
`0x50CC`:

| Relative offset | Bytes | Interpretation | Confidence |
| --- | ---: | --- | --- |
| `+0x00` | 2 | Current room object ID | Confirmed by snapshots and restored screenshots |
| `+0x02` | 2 | Previous room ID | Consistent with observed transitions; provisional |
| `+0x04` | 2 | Monotonic turn/progress count, interpreted big-endian | Inferred from consecutive saves and matching timer decreases |
| `+0x06` | 2 | Second time/progress field, encoding not decoded | Unknown |
| `+0x282` | 7 | Terri's generated telephone number, ASCII `NN-NNNN` | Confirmed by guest write and RNG traces (2026-11-04 handoff) |
| Other bytes from `+0x08` onward | | Other engine/story state | Not yet mapped |

In two independently verified full campaigns with a deterministic DOS date
and time, all 734 object records and all scheduled-event records agreed
through the Terri, morning and mission-one saves. Exactly six bytes of their
global blocks differed, starting with the Terri save: the seven-character
ASCII value at `+0x282` contained **different telephone-number-shaped
values** (`NN-NNNN`; the hyphen byte at `+0x284` agreed). The first differing
save was produced by the stage in which Terri hands over a number on a
napkin. Subsequent guest write and CPU traces reported in the 2026-11-04
handoff confirmed the generator and its shared RNG. The field is persistent
story state and must not be added to golden RAM exclusions.

### Telephone generator and shared animation RNG

The traced RNG updates an unsigned 32-bit state modulo 2^32:

```text
state = state * 214013 + 2531011
result = (state >> 16) & 0x7fff
first = random()
second = random()
phone = sprintf("%02d-%04d", second % 99 + 1, first % 9999 + 1)
```

The native traces agreed with both the final state and formatted number.
The investigated runtime stores the state in two words at `DS:358e` and
`DS:3590`, with `DS=5b46`. Telephone formatting was traced from physical
`0x4af89`; RNG calls at `0x1bb56`. These are **capture-specific runtime
addresses**, not original EXE/OVL identities; mapping them to stable file
offsets remains necessary before registering a native replacement.

The animation scheduler shares this RNG, including callbacks whose random
delay range is zero. Its deadline table was at `DS:9ffa`, four bytes per
slot. Recovered minimum/random-range delays in milliseconds were:

| Slot | Minimum | Random range |
| --- | ---: | ---: |
| 0 | 300 | 0 |
| 1 | 150 | 0 |
| 2 | 750 | 750 |
| 3 | 200 | 200 |
| 4 | 4000 | 10000 |

The scheduler reads the guest clock repeatedly while processing slots.
Historical traces first showed relevant ordering divergence near 46,591
guest milliseconds: slot 1 preceded slot 3 in run A, and slot 3 preceded
slot 1 in run B. This changes assignment of random values to callbacks and
can shift the later telephone generation. It establishes propagation of
the discrepancy, **not its original cause**. Slot 3's deadline was at
physical `0x65466`; the clock-return trace point was `0x263b5` in that layout.

The handoff reports thirteen independent 48-second current-build runs all
producing the B deadline (43845), including three without extra entry
tracing. Two independently restored complete Terri-stage runs then matched
strictly at all five RAM checkpoints and produced identical 21,769-byte
saves, SHA-256
`bcf98701dabef5c15ad085944f6e1f55ee78b7e43329f5f919035fd3aefb2831`.
Their artifacts are under `build/golden/terri-recheck-v019/` on the testing
host. These are reported host results, not a new local validation. They do
not prove the historical timing discrepancy is permanently fixed.

To locate the field's byte copies in already-recorded conventional RAM and
find the earliest matching checkpoint in both runs, without another native
replay, run:

```sh
python3 tools/locate_save_ascii.py \
  build/golden/full-fixed-clock/run-a.json \
  build/golden/full-fixed-clock/run-b.json \
  --left-campaign build/golden/full-fixed-clock/run-a \
  --right-campaign build/golden/full-fixed-clock/run-b \
  --stage terri \
  --global-offset 0x282 \
  --length 7
```

The tool verifies each native RAM snapshot against its recorded golden hash.
It verifies each original save against `stage-meta.json` and, when the golden
manifest includes a `save_sha256` entry, against that hash too. Some older
captured manifests omit the optional save hash; in this case the tool prints
an explicit weaker-provenance notice but still checks the stage metadata and
file contents. It then reports both string occurrences and addresses where
the two differing strings occupy the *same* guest linear offset. These
addresses are **candidates** for a `run_walkthrough.py --trace-byte-write`
watchpoint, not proof that a particular RAM copy is the owning global field.
A new native replay and instruction write trace are needed to identify the
writer.

For example, the office save's first words are room `0x00ec`, previous room
`0x00f1`, and the byte pair `01 13` at `+4`, tentatively interpreted as turn
`0x0113` (275). The next DataMan save changes just **two bytes** in the
entire fixed global-state block: `01 13 → 01 14` and `00 f2 → 00 f3`.
Its five timer records retain the same IDs while their remaining counts all
decrease by one. The VR save's count similarly increases by 19 and all five
timers decrease by 19. This strongly supports the **turn countdown**
interpretation, though we have not observed counter overflow and cannot yet
rule out two independently stored byte counters instead of a big-endian word.

The final timer queue is serialized in original order (no implied sorting).
For the office save, its five `(event_id, remaining_turns)` pairs are:

```text
(0x0021, 0x0091)  (0x0017, 0x01be)  (0x0018, 0x0236)
(0x0025, 0x02ae)  (0x002a, 0x02ea)
```

During story progression some IDs disappear and others appear, explaining
the four-byte length changes. The meaning of individual event IDs and the
layout of most of the 1,071-byte global block require decompiling the
native SAVE/RESTORE and timer-dispatch routines. The current decoder is
**read-only** and must not be mistaken for a safe save editor or a completed
replacement of the native save loader.

## Validation

- **11/11** authentic saves match the complete 20,650-byte live object block
  from their corresponding post-save RAM dumps, with zero differing records.
- In all eleven, the 16-bit event count predicts the **exact** file length.
- Consecutive office and DataMan saves confirm the timer decrement rule.
- Synthetic asset-independent unit tests validate record offsets, all eight
  object kind counts, exact lengths, malformed headers and counts, invalid
  handler indices, rooms, and timer decoding.

The original saved games are **not committed**. Generate your own with the
checked [walkthrough campaign](walkthrough-replay.md) and use the inspector
on its isolated output.
