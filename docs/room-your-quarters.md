# Gateway: native story logic for Your Quarters

Investigated 2026-11-01 with the original Gateway `GATEMAIN.OVL` and
`GATESTR.DAT` (identities in [executable.md](executable.md)). This is a
**partial decompilation**, not an executable room implementation. All offsets
below are **on-disk file offsets** in `GATEMAIN.OVL`, unless marked as an
offset within the relocated guest data segment.

## Reproduce

```sh
python3 tools/disassemble_story.py ../gateway-dos/GATEMAIN.OVL \
  --start 0x515de --size 0x12e \
  --strings ../gateway-dos/GATESTR.DAT \
  --section-base 0x51480 --event-table 0x51822 --events 24
```

The [RTLink manifest](rtlink.md#tools-and-exact-handler-entries) and live
object inventory locate object `0xee` (Your Quarters), handler 233, section
`0x26`/38, image-relative entry `0x35` and file offset **`0x514b5`**. The
section's code bytes begin at file offset `0x51480`. At `0x514b8`, the room
handler reads a word argument from `[bp+6]`, subtracts 1, checks it against
23, and dispatches via `jmp word ptr cs:[bx+0x3a2]`. The near-address table
is therefore at file offset `0x51480+0x3a2=0x51822` and contains 24 words.
Each word is relative to the section base; `objdump` erroneously interprets
the table as instructions if the disassembly range runs across it.

## Confirmed event dispatch

| Event ID | Handler file offset | Proven behavior of this entry |
| ---: | ---: | --- |
| 1 | `0x51740` | Tests a command byte and argument IDs; calls additional services conditionally |
| 2–5 | `0x51709` | Returns 0 |
| 6 | `0x5170e` | If two state tests pass, displays text 3/4 and returns 1; otherwise returns 0 |
| 7 | `0x5180d` | Calls a shared service and conditionally displays text 3/5 |
| 8–9 | `0x515de` | Room description and drawer/flag processing |
| 10 | `0x516e6` | Tests property 12 of object `0x162`; conditionally dispatches object `0x161` |
| 11 | `0x515de` | Same entry as 8–9 |
| 12 | `0x51709` | Returns 0 |
| 13 | `0x5156d` | Changes `DS:69fe` and conditionally displays text 3/0 |
| 14 | `0x51709` | Returns 0 |
| 15 | `0x515ba` | Calls a shared service, displays text 3/1, then follows further state processing |
| 16–21 | `0x51709` | Returns 0 |
| 22 | `0x514ce` | Branches on an unidentified `ES:3a7a` value; calls a shared routine |
| 23 | `0x514fa` | Similar state condition; calls shared routines and updates `DS:6a20` |
| 24 | `0x5155a` | Conditional cleanup calls, resetting `DS:6a20` to `ffff` |

**Event IDs are not yet semantic names.** Event 8 is *observed* as the room
description request in the prior `look` replay; 9 and 11 share its entry but
their callers have not been traced. A jump-table target inside another branch
does not necessarily represent the same high-level operation as that branch.

## Room description: approximate C-like translation

The branch at file offset `0x515de` reaches the same text formatter as the
previously traced `look` path. Its relevant behavior can be expressed as:

```cpp
// PSEUDOCODE only. These field addresses are not a stable host ABI.
void YourQuarters::description_event() {
    auto plant = (state_word(0x9476, 0x393e) == 10 &&
                  object_flag(0x175, 9))
                     ? far_pointer(DS, 0x6a0a) : far_pointer(DS, 0x6a04);
    auto picture = (state_word(0x9476, 0x393e) == 10 &&
                    object_flag(0x16f, 9))
                       ? far_pointer(DS, 0x6a17) : far_pointer(DS, 0x6a10);

    format_story_text(3, 2, plant, picture);
    if (!object_flag(0x124, 12)) {  // drawer closed
        format_story_text(3, 3);
    }

    for (auto object : {0x125, 0x124, 0x14e, 0x150, 0x14f,
                        0x16f, 0x175, 0x131}) {
        set_object_flag(object, 7);
    }
    if (object_flag(0x162, 12)) {
        dispatch_object(0x161, 0); // exact call semantics still unknown
    }
}
```

Evidence by file offset:

- `0x515de–0x51631`: two tests of `ES:393e == 10`, two queries of property 9
  on objects `0x175` and `0x16f`, and four candidate far text pointers in
  `DS:6a04/6a0a/6a10/6a17`. The *physical segment* for each state read is
  obtained through a word in DS at the stated first offset; these are not
  flattened linear addresses.
- `0x51632–0x5163f`: pushes text token `f000:0c02` and calls the verified
  formatter; the other two far pointers supply its two `%s` substitutions.
- `0x51642–0x51665`: checks `(object=0x124, property=0x0c)` and prints
  `f000:0c03` only if the flag is clear. The earlier object-state replay
  confirms `0x124` is the drawer and this flag tracks its open condition.
- `0x51666–0x516e5`: calls the known native flag setter `0157:03e0`
  eight times with property 7 and the listed object IDs. The meaning of
  property 7 is **not** yet established.
- `0x516e6–0x5170b`: tests property `0x0c` on object `0x162` and conditionally
  calls `048e:00f8` with two words; the label `dispatch_object` is provisional.

The text decoder verifies these strings:

| Text token | Role |
| --- | --- |
| `f000:0c00` | Narrates leaving Heecheetown for the barracks |
| `f000:0c01` | Narrates leaving the barracks for Heecheetown |
| `f000:0c02` | Describes the room and includes two `%s` substitutions for plant and picture |
| `f000:0c03` | Says the desk drawer is closed |
| `f000:0c04` | Describes the barracks west of the player's current position |
| `f000:0c05` | Says the PV commset's message light is blinking |

The text arguments are **text pointers**, not confirmed object-name IDs. The
two objects queried for property 9 appear to influence the plant/picture
substitutions, but the precise meaning of their conditions remains unresolved.

## Drawer: second native handler, same world-state flag

The separate drawer (`0x124`) handler begins at **`0x51a78`** (section 38,
image-relative `0x5f8`). It has a different ABI shape from the room handler:
it accepts the event word at `[bp+6]` but only handles **event 6**, then
dispatches on the byte read through `[DS:9478] : e664`. Those byte values
have *not* yet been mapped to vocabulary verbs. They are recorded as numeric
command codes to avoid guessing their meaning.

```sh
python3 tools/disassemble_story.py ../gateway-dos/GATEMAIN.OVL \
  --start 0x51a78 --size 0x109 --strings ../gateway-dos/GATESTR.DAT
```

Direct observations:

| Code / entry | Behavior |
| --- | --- |
| `0x3a` / `0x51aa8` | Tests drawer property 12 and displays text 3/13 with an open/closed substitution; if open, additionally calls another object service |
| `0x46` and `0x85` / `0x51b20` | Displays text 3/14 about unsuccessfully yanking the drawer out; calls the property-12 setter on drawer `0x124` |
| `0x61` and `0x65` / `0x51b3f` | Displays text 3/15 about looking behind/under the drawer, then calls another shared service |
| `0x7b` / `0x51aff` | Forwards arguments including the drawer ID and literal `0x71` to a far service |
| `0x7c` / `0x51b14` | Forwards arguments including the drawer ID and literal `0x1d` to the same service |
| Other codes | Returns 0 without a recognized branch |

Three more fully decoded story references:

- 3/13 describes the desk drawer as `%s` (open or closed).
- 3/14 describes trying to pull the drawer out of its track.
- 3/15 describes looking underneath and behind the drawer, finding nothing
  but dust.

This establishes a concrete **cross-handler link**: the drawer's native
handler reads and sets property 12; the room-description handler reads that
same property to decide whether to append the closed-drawer sentence. The
shared `object_flag` and `set_object_flag` native service targets were already
validated against the C++ HLE. This is enough to sketch a future modern
`Drawer` object and reactive room description, but not yet to replace the
unknown verbs, object-service side effects, or input parser faithfully.

## Next decompilation boundary

1. Trace all calls into handler 233, with event IDs and register/stack state,
   to give the cases semantic names and validate the event-8/9/11 equivalence.
2. Determine the meaning of property 7 and the state word read via
   `[DS:9476] : 393e` by comparing snapshots before/after room entry.
3. Map the drawer command-byte codes to vocabulary actions, resolve the
   forwarded service calls, then implement the combined room/drawer behavior
   using host state and compare it against the `look`, `open drawer`, `look`
   replay.
4. Preserve the native interpreter/overlay path as the correctness oracle until
   each replacement is tested against the same commands and state transitions.

No original game bytes or generated text dumps are committed.
