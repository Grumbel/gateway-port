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
| `+0x08` onward | 1,063 | Other engine/story state | Not yet mapped |

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
