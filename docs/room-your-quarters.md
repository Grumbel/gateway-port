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

## Mapping the parser's numeric command codes

The original `VOCAB.DAT` is now readable with a standalone, read-only tool:

```sh
python3 tools/gateway_vocab.py ../gateway-dos/VOCAB.DAT \
  look open close pull examine drawer --groups
```

The inspected file contains **2,644 indexed words**, **91 link groups** and
**210 link pairs**. Relevant entries (decimal word index, hexadecimal flag):

| Word | Index | Flags |
| --- | ---: | ---: |
| `close` | 381 | `0x0001` |
| `drawer` | 641 | `0x0002` |
| `examine` | 736 | `0x0001` |
| `look` | 1295 | `0x0001` |
| `open` | 1538 | `0x0005` |
| `pull` | 1782 | `0x0001` |

These *word indexes* do not equal the drawer handler's **one-byte command
codes** (`3a`, `46`, `61`, `65`, `7b`, `7c`, `85`). Its link groups should not
be called synonym sets: one group headed by `cattails` also contains `amount`,
`enter`, and `trapdoor`, while a group headed by `pull` includes `sphere` and
`voyage`. They may implement internal parser indexing; their precise role is
still unknown. The last field in a link pair does not generally equal the
word's vocabulary flag (only 37 of 210 pairs match), so the groups have not
been used to infer verbs.

### Get the missing dynamic evidence

The opt-in `--trace-entry` hook now annotates two known original Gateway
handlers, guarded by their expected relocated CS and function prologue:

| Guest entry (linear) | Native handler |
| --- | --- |
| `0x49db5` | room 0xee / Your Quarters / event word at caller stack +4 |
| `0x4a378` | drawer 0x124 / event word at caller stack +4 |

It follows `[DS:9478]` to read the command byte at `ES:e664` and records
`story_object`, `story_event`, and `story_command` in the existing research
trace. Unlike a guessed static mapping, this can connect a *known typed action*
to a branch by observing the live guest. No guest state is changed by tracing.
The addresses are **specific to the established original Gateway launch layout**,
not universal breakpoints for other Legend games or other overlay layouts.

The scripted comparison already drives `look`, `open drawer`, and subsequent
object commands. It now enables these two entry hooks automatically in its
`drawer` and `objects` scenarios:

```sh
python3 tools/trace_game_logic.py --scenario objects --services state \
  --output build/story-commands
python3 tools/summarize_story_trace.py build/story-commands/native.trace
```

The report lists the object, event, command byte, call count, and first virtual
timestamp. The raw records remain in `native.trace` for correlating the
timestamped `key` events. This workflow still requires a built `gateway-probe`
and the original game resources; the probes were **not** run in this analysis
environment, where Unicorn and CMake are unavailable. Command meanings remain
unverified until those replay results are collected.

### Follow-up: real trace and recovered events (2026-11-01)

A native trace supplied from the 42,000 ms `objects` replay confirmed that
**both verified handler entry hooks fired**, but the first version of the
additional `story_command` annotation was absent. The previous summarizer
incorrectly rejected this trace outright. The updated tool also recognizes
ordinary `entry` lines at the two known `CS:IP` locations and reads the event
argument from the third `stack` word (the far-call frame's `[SS:SP+4]`).

These observations come directly from that supplied trace:

| Last completed typed input | Handler entry time | Native object | Native event |
| --- | ---: | ---: | ---: |
| `open drawer` | 18,392 virtual ms | `0x124` (drawer) | 6 |
| `take book` | 25,333 virtual ms | `0x124` (drawer) | 5 |
| `close drawer` | 34,422 virtual ms | `0x124` (drawer) | 6 |

The same captured trace already includes the **top-level action dispatcher**
at linear `0x147a6` / `1476:0046`, with the action ID in AX and at the third
word of its far-call argument frame. Correlating those entries with the
nearest recently submitted input gives:

| Typed input | Native action AX |
| --- | ---: |
| `look` | `0x5f` |
| `open drawer` | `0x71` |
| `take book` | `0x46` |
| `inventory` | `0x53` |
| `drop book` | `0x32` |
| `close drawer` | `0x1d` |

In particular, the drawer handler's `0x7b` branch forwards **action `0x71`**,
and its `0x7c` branch forwards **action `0x1d`** through the same shared call.
This strongly supports associating these two inner command cases with opening
and closing the drawer. It remains an **inference** until we see those exact
branch PCs execute or capture the inner command byte at the new AX hook.

The typed-input association is from the recent Enter key and call timing; it
does not prove that an event ID is itself a verb ID. Event 5 reaches the drawer
handler but its code only processes event 6; it need not modify the drawer.

The next capture uses a stronger observation point for the command itself.
At *file offset* `0x51a8a` the drawer handler loads the command byte into AL;
at `0x51a8e` it clears AH; and at **`0x51a90`** it compares AX with `0x61`.
That last instruction is at guest **`49d8:0610` / linear `0x4a390`**. An
opt-in code hook there can read the command straight from AX, avoiding the
unverified DS-pointer/prologue conditions that suppressed the earlier
annotation. `trace_game_logic.py` now hooks this point as well, and the
summarizer attaches its AX value to the preceding drawer entry. No fixed
translation from the command values to verb names is assumed.

For an already captured bundle-042 trace, the same summarizer now works:

```sh
python3 tools/summarize_story_trace.py build/story-commands/native.trace --details --actions
```

It shows events even when their command byte is unavailable (`?`); a newly
captured trace using the `0x4a390` hook can fill in the exact byte.
`--actions` also reports the native top-level action IDs already available in
the older trace, independently of the inner command-byte probe.

## Next decompilation boundary

1. Trace all calls into handler 233, with event IDs and register/stack state,
   to give the cases semantic names and validate the event-8/9/11 equivalence.
2. Determine the meaning of property 7 and the state word read via
   `[DS:9476] : 393e` by comparing snapshots before/after room entry.
3. Use the command trace above to map the drawer's numeric command codes to
   typed input, resolve the forwarded service calls, then implement the
   combined room/drawer behavior
   using host state and compare it against the `look`, `open drawer`, `look`
   replay.
4. Preserve the native interpreter/overlay path as the correctness oracle until
   each replacement is tested against the same commands and state transitions.

No original game bytes or generated text dumps are committed.
