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

**Correction after live tracing:** these `2c24` pointers are executable
**ten-byte RTLink trampoline starts**, not six-byte reference operands. Each
starts with `e8 disp16` (near CALL to manager `2c24:058d`), followed by
`ea` (far JMP), target IP/CS, and a trailing section word. The reference
begins four bytes *after* the action table pointer.

Live OPEN example, taken at `1476:0060` just before its indirect far call:

```text
action 0x71 pointer = 2c24:1c59 (linear 0x2de99)
live bytes: e8 31 e9 ea 72 15 61 46 05 00
   near CALL: 2c24:058d
   far JMP:   4661:1572
   section:   5
   source EXE sequence at file offset 0xbd33:
   e8 31 e9 ea 72 15 61 36 05 00 (before +0x1000 relocation)
```

Similarly, CLOSE's live trampoline bytes are
`e8 91 ed ea 9b 02 61 46 05 00`, matching EXE file `0xb8d3` after
relocation. Both byte windows remained **identical before, during and after**
the calls. The calls executed at the two table pointers and returned `AX=1`.
Neither target was transiently patched.

Earlier analysis incorrectly used EXE file offset `0xa020 + action_pointer_offset`
to read the stubs. The actual source-file trampoline locations for these five
entries agree with `0xa0da + action_pointer_offset`—a difference of `0xba`
bytes. The reason for this placement difference is not established; it is
**not** a general conversion rule for the manager's directory or other code.
The original section directory remains at `0xac3f` as documented in
[rtlink.md](rtlink.md#directory-layout).

With the correct ten-byte trampoline layout and the section-5 RTLink directory
record (initialized image begins at OVL file `0xd3d0`), the action mapping is:

| Typed input / action | Table pointer | RTLink far target (runtime CS:IP) | OVL file offset |
| --- | --- | --- | ---: |
| `close drawer` / `0x1d` | `2c24:17f9` | `4661:029b`, section 5 | `0xd66b` |
| `drop book` / `0x32` | `2c24:1727` | `4661:05d7`, section 5 | `0xd9a7` |
| `take book` / `0x46` | `2c24:165f` | `4661:2b11`, section 5 | `0xfee1` |
| `inventory` / `0x53` | `2c24:15dd` | `4661:14f4`, section 5 | `0xe8c4` |
| `look` / `0x5f` | `148e:0b90` | Resident entry, no RTLink trampoline | — |
| `open drawer` / `0x71` | `2c24:1c59` | `4661:1572`, section 5 | `0xe942` |

OPEN and CLOSE have verified live callsite/target bytes and section-5 entry
hooks. The remaining three section-5 references are consistent with uniquely
matching ten-byte original EXE trampolines; their actual execution addresses
have not yet been hooked.
The original replay loaded section 5 once, consistent with shared actions
remaining resident. The old inferred section-6/7/9/15 mappings were artifacts
of reading unrelated EXE bytes and are **withdrawn**.

Unlike the old speculative destinations, these overlay offsets really contain
shared action logic. Static disassembly shows:

- OPEN at `0xe942` checks object properties `0x0c`, `0x23`, and `0x0e` in
  sequence, and on its normal success path calls the verified property setter
  `0157:03e0` with **`0x0c`** at OVL `0xeb04`, followed by the **same setter
  with `0x02`** at OVL `0xeb19` (among other state updates and text handling).
- CLOSE at `0xd66b` checks `0x0e`, `0x23`, and `0x0c` in sequence; its normal
  success path calls the verified property clearer `0157:0413` with **`0x0c`**
  at OVL `0xd7cc`.
- Both have `mov ax,1; retf` return paths (`0xebfd` and `0xd7fd`), matching
  the live dispatcher return `AX=1` for the two typed commands.

This matches the drawer's known open-state property and explains why the
drawer-specific event-6 handler can return zero: a **separate shared section-5
action** performs the state change. The property-`0x0c` mutation callsites
have now been confirmed at runtime (below). The property's meaning beyond
its observed open/closed use, property `0x02`, and conditional side effects
remain unclassified.

### Partial C-like translation of the normal paths

The following is **only the common branch after early special cases** (mode
checks, another-object handling, and callbacks are omitted). `has_flag`,
`set_flag`, and `clear_flag` refer to the verified object-state service ABI;
error labels stand for the original text and return branches, not modern
implementations. In particular, an action's `AX=1` return marks handling and
must not be interpreted as proof of success.

```cpp
// OPEN: GATEMAIN.OVL 0xea65–0xeb1e, shared handler 0xe942.
if (has_flag(target, 0x0c)) goto already_open;
if (has_flag(target, 0x23)) goto blocked_open;
if (!has_flag(target, 0x0e)) goto cannot_open;
set_flag(target, 0x0c);  // OVL eb04: live drawer mutation verified
set_flag(target, 0x02);  // OVL eb19: static disassembly; new trace hook pending
// Print text; further conditions involving properties 0x13/0x1f follow.

// CLOSE: GATEMAIN.OVL 0xd706–0xd7d4, shared handler 0xd66b.
if (!has_flag(target, 0x0e)) goto cannot_close;
if (has_flag(target, 0x23)) goto blocked_close;
if (!has_flag(target, 0x0c)) goto already_closed;
// Print closing text.
clear_flag(target, 0x0c);  // OVL d7cc: live drawer mutation verified
```

The order and predicates of these **inner branches** are directly visible in
the original x86 instructions; entry paths and the meanings of properties
`0x0e` and `0x23` have not been proven across different objects.

To inspect the two shared functions:

```sh
python3 tools/disassemble_story.py ../gateway-dos/GATEMAIN.OVL \
  --start 0xd66b --size 0x198 --strings ../gateway-dos/GATESTR.DAT
python3 tools/disassemble_story.py ../gateway-dos/GATEMAIN.OVL \
  --start 0xe942 --size 0x2c2 --strings ../gateway-dos/GATESTR.DAT
```

## Reproduce the mapping from a native snapshot

After building `gateway-overlay` and running the `objects` scenario:

```sh
./build/gateway-overlay ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --fixups > build/rtlink-manifest.json
python3 tools/inspect_action_dispatch.py build/story-commands-043/native \
  --overlay-map build/rtlink-manifest.json
```

The corrected inspector validates an executable `e8 disp16; ea` trampoline
at each nonresident table pointer and checks that its near CALL resolves to
manager `2c24:058d`. It decodes the following six-byte reference from **+4**
and maps the target to its original OVL file offset using the manifest. It
leaves resident `look` as a direct pointer. A malformed or nonmatching
trampoline is left unclassified rather than guessed.

## Native replay evidence and next checks

The original three-way `objects` replay verified 187 RTLink loads spanning
nine sections (`0–5`, `37`, `38`, `56`), matching all 734 object records,
service sequences, and framebuffer hashes. Earlier entry hooks placed on
the incorrectly inferred section-7 targets `0x47307` and `0x48021` did not
fire—unsurprisingly, section 7 was not loaded. This was a **mapping error**,
not evidence that the shared OPEN/CLOSE path was absent.

The native replay hooked the resident action dispatcher `1476:0046` (entry),
`1476:0060` (indirect CALL through `ES:[BX+28]`), and `1476:0065`
(return). It observed:

| Typed command | Time at `1476:0060` | Live far-call target | Time at `1476:0065` | Return AX |
| --- | ---: | --- | ---: | ---: |
| `open drawer` | 18,392 ms | `2c24:1c59` | 18,417 ms | `1` |
| `close drawer` | 34,422 ms | `2c24:17f9` | 34,443 ms | `1` |

The callsite bytes `26 ff 9f 28 00` and `ES=3bc5`, `BX=6*action_id`
confirm a real far CALL through the action table. The following three live
memory samples independently established the valid section-5 trampolines:

- `action_target_context=` immediately before the far call;
- `action_callee_context=` on entry at `2c24:1c59` or `2c24:17f9`;
- `action_target_after_context=` after returning to `1476:0065`.

For OPEN, all three fields were
`61460700e831e9ea721561460500e827`; for CLOSE they were
`61460700e891edea9b0261460500e887`. Skipping the four context bytes gives
the ten-byte trampolines decoded above. This is positive proof of execution
at each target and of unchanged code across the call, not a self-modifying
RTLink reference.

The replay script now prefers `$GATEWAY_BUILD_DIR/gateway-probe` under
`nix develop` (falling back to `build/gateway-probe` elsewhere) and checks the
binary for the `action_code=` diagnostic before starting. This fails quickly
with an actionable build instruction instead of silently producing another
trace without the necessary fields.

```sh
gateway-build  # in nix develop; builds $GATEWAY_BUILD_DIR/gateway-probe
python3 tools/inspect_action_dispatch.py build/action-dispatch-check/native \
  --overlay-map build/rtlink-manifest.json
```

This first check uses the **existing native snapshot**; rerunning the game is
unnecessary. Generate the manifest first (command above) if it is missing.
The subsequent native replay confirmed both section-5 entries and the
property-`0x0c` mutation branches with additional hooks:

```sh
python3 tools/trace_game_logic.py --scenario objects --native-only \
  --verify-overlays --output build/action-section5-check
rg '^entry .*pc=(468ab|47b82|46a0c|47d44)\b' \
  build/action-section5-check/native.trace
python3 tools/summarize_action_effects.py \
  build/action-section5-check/native.trace
```

The user-supplied trace records these linked events:

| Typed action | Section-5 handler entry | State service callsite | Stack arguments | Return |
| --- | --- | --- | --- | --- |
| OPEN `0x71` | `4661:1572` at 18,393 ms | `4661:1734` / OVL `0xeb04` | `(object=0x124, flag=0x0c)` | `AX=1` at 18,417 ms |
| CLOSE `0x1d` | `4661:029b` at 34,422 ms | `4661:03fc` / OVL `0xd7cc` | `(object=0x124, flag=0x0c)` | `AX=1` at 34,443 ms |

Here the recorded `stack` begins with the object and property passed to the
far-called flag service. The service itself is the verified native setter or
clearer; the hook is immediately before that call. `summarize_action_effects.py`
correlates these callsites with the live dispatcher and reports only
**observed** mutations. A missing mutation in its output is not evidence that
the action had no other effects.

The trace driver now additionally hooks `0x47d59`, corresponding to OVL
`0xeb19`, to test the *second OPEN setter* for property `0x02` on a new run.
The original `action-section5-check` trace predates that hook; its absence
there is expected, not evidence of a skipped instruction.

`--native-only` avoids two additional HLE comparison replays; omit it for the
full three-way regression check. Guest addresses are specific to the known
Gateway launch layout, and sections loaded in the same bank may require
residency checks when analyzing other games or actions.

For the modern replacement action layer, we can now distinguish the global
OPEN/CLOSE routines from object-specific event-6 handlers and reuse the
verified object-flag service ABI. Next steps are to confirm the second OPEN
setter, recover mode guards and other state semantics, and identify reusable
contracts for the other actions before replacing native execution.
