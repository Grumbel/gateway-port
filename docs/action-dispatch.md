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
action** performs the state change. Both property-`0x0c` and property-`0x02`
setter callsites are now confirmed for the drawer at runtime (below), as is
the property-`0x0c` clearer. The *purpose* of property `0x02` and other
conditional side effects remain unclassified.

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
set_flag(target, 0x02);  // OVL eb19: live drawer callsite verified
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

The independent `StoryModel::evaluate_normal_open_close()` now mirrors these
read-only tests. It reports an ordinary-path guard result without modifying
the object, printing text, or claiming the full DOS command is permitted:

| Result | OPEN predicate (first match wins) | CLOSE predicate (first match wins) |
| --- | --- | --- |
| Already open | `0x0c` set | — |
| Cannot operate | `0x0e` unset (after other checks) | `0x0e` unset |
| Blocked | `0x23` set (after already-open) | `0x23` set (after `0x0e`) |
| Already closed | — | `0x0c` unset (after other checks) |
| Allowed in this **inner branch** | `0x0c` unset, `0x23` unset, `0x0e` set | `0x0e` set, `0x23` unset, `0x0c` set |

The different predicate orders are intentional: an object with both `0x0c`
and `0x23` takes OPEN's *already open* branch, but CLOSE's *blocked* branch
if `0x0e` is present. These contradictory combinations are regression-tested
against the actual instruction ordering at OVL `0xea65–0xeaf7` and
`0xd706–0xd79b`, not observed game text. The naming of the guard results is
provisional, and the model does **not** emulate the earlier special paths.

### Next shared actions to decompile

The other three actions also use the known section-5 image. Static inspection
of the **original OVL file** reveals:

| Action | OVL entry | Candidate operations in the entry's control flow |
| --- | ---: | --- |
| DROP `0x32` | `0xd9a7` | Queries current object location via `0157:0546`; other movement work is delegated to helpers. |
| INVENTORY `0x53` | `0xe8c4` | Calls inventory/list helper `1c24:3dc9`, then emits text; no direct flag mutation in its short entry. |
| TAKE `0x46` | `0xfee1` | On one branch sets flag `0x02` at `0x10066`, clears `0x08` at `0x1007b`, and clears `0x0a` at `0x10090`, using verified `0157` services. |

The subsequent section-5 replay **confirmed execution** of all three shared
entries, including TAKE's predicted property-`0x02` setter and property-`0x08`
and `0x0a` clearers. The book (`0x123`) moves to the player (`0xd3`) on TAKE
and to the room (`0xee`) on DROP. The new service-entry hooks below connect
those changes to their top-level action dispatches, although the intervening
helpers have not yet been decompiled.

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
| OPEN `0x71` | `4661:1572` at 18,393 ms | `4661:1734` / OVL `0xeb04` and `4661:1749` / OVL `0xeb19` | `(0x124, 0x0c)` then `(0x124, 0x02)` | `AX=1` at 18,417 ms |
| CLOSE `0x1d` | `4661:029b` at 34,422 ms | `4661:03fc` / OVL `0xd7cc` | `(object=0x124, flag=0x0c)` | `AX=1` at 34,443 ms |

Here the recorded `stack` begins with the object and property passed to the
far-called flag service. The service itself is the verified native setter or
clearer; the hook is immediately before that call. `summarize_action_effects.py`
correlates these callsites with the live dispatcher and reports only
**observed** mutations. A missing mutation in its output is not evidence that
the action had no other effects.

The later `action-section5-check` replay confirmed the second OPEN setter
at linear `0x47d59` / OVL `0xeb19`, in the same millisecond as the first.
This establishes **which routines are called and with what arguments**;
whether every setter changes bytes must still be checked against the object's
original flag mask and state.

The next trace revision adds the three other shared action entries and hooks
the **native object-state service entries**, which capture calls made from
nested helpers as well as directly within the action. The known far-call ABI
has a return IP and CS followed by `(object, property)` for flag services, or
`(object, location)` for the location setter:

| Service | Linear hook | Expected stack arguments after return pointer |
| --- | ---: | --- |
| Flag set (`0157:03e0`) | `0x11950` | object, flag |
| Flag clear (`0157:0413`) | `0x11983` | object, flag |
| Location set (`0157:064b`) | `0x11bbb` | object, destination |

`summarize_action_effects.py` uses these service entries when present and
falls back to the original OVL callsites for older traces. This avoids
double-counting one flag mutation. New replay results are still needed to
determine which *write attempts* actually change object records.

### All five shared actions observed

The user-supplied native-only replay hooked the complete action path. It
matched the earlier 187 verified RTLink loads and final framebuffer hash.
These calls are now **live observations**, not merely static candidates:

| Command | Observed object-state service calls |
| --- | --- |
| LOOK (`0x5f`) | Repeatedly sets property `0x07` on the room and other objects. |
| OPEN (`0x71`) | Sets properties `0x0c`, `0x02`, `0x07` on drawer `0x124`; also sets `0x07` on book `0x123`. |
| TAKE (`0x46`) | Sets `0x07` on book and drawer; sets book location to `0xd3`; sets book `0x02`, clears `0x08` and `0x0a`. |
| INVENTORY (`0x53`) | Sets `0x07` on player `0xd3` and inventory-related objects. |
| DROP (`0x32`) | Sets book location to `0xee` and its `0x07`. |
| CLOSE (`0x1d`) | Sets drawer `0x07` and clears its `0x0c`. |

Flag `0x07` is especially interesting: LOOK and INVENTORY both set it on many
objects, including repeat visits to the same scene. It could represent
"observed/known" state, but **that interpretation is still a hypothesis**.
The trace records setter calls, which are not necessarily byte changes;
repeated requests to set an existing bit can be idempotent.

To distinguish changes from redundant calls, the replay driver has an opt-in
passive verification run. It executes the original native services, computes
each proposed write and its *pre-call* byte comparison, then verifies native
results at the existing service exit hook:

```sh
gateway-build
python3 tools/trace_game_logic.py --scenario objects --native-only \
  --verify-overlays --trace-state-writes --output build/action-write-check
python3 tools/summarize_action_effects.py \
  build/action-write-check/native.trace --changed-only
```

`[changed]` means the pre-call bytes differ from the predicted write;
`[unchanged]` means the setter would not modify them; `[unsupported]` means
the object's type does not support that property or location operation.
The native verification mode does **not** replace or modify the guest's own
service implementation. `--changed-only` requires this new annotation and
rejects older traces rather than silently dropping their calls.

### Which service calls actually changed state?

The next user-supplied `--trace-state-writes` replay had the **same** final
framebuffer hash and overlay verification count as the original scenario.
Filtering to real byte changes produced:

| Action | Changed object state | Calls that did not change bytes |
| --- | --- | --- |
| First LOOK and later LOOKs | None during the recorded commands | Repeated property-`0x07` setters |
| OPEN drawer | Drawer `0x0c=1`, `0x02=1`; book `0x07=1` | Drawer `0x07` already set |
| TAKE book | Book location `0xd3`, book `0x02=1` | Book/drawer `0x07` already set; clearing book `0x08` and `0x0a` had no effect |
| INVENTORY | None | Repeated property-`0x07` setters |
| DROP book | Book location `0xee` | Book `0x07` already set |
| CLOSE drawer | Drawer `0x0c=0` | Drawer `0x07` already set |

That strongly supports—but does not establish—a persistent *seen/known*
meaning for `0x07`: the book first acquires the bit when the drawer exposes
it, whereas looking again at already-known room contents does not change it.
Flag `0x02` changes on initial OPEN and TAKE, possibly tracking interaction,
but this is still only a hypothesis. The two TAKE clear calls prove their
functions execute but do **not** demonstrate a meaningful transition for
flags `0x08` and `0x0a` in this particular state.

The replay driver now supports replacing the seven later commands while
preserving their original time slots, the initial room LOOK, and pagination
keystrokes. This allows controlled repeated-action experiments without
modifying game data or the DOS executable:

```sh
python3 tools/trace_game_logic.py --scenario objects --native-only \
  --verify-overlays --trace-state-writes \
  --command-sequence 'open drawer,close drawer,open drawer,take book,drop book,take book,inventory' \
  --output build/action-repeat-check
python3 tools/summarize_action_effects.py \
  build/action-repeat-check/native.trace --changed-only
```

The seven slots begin at guest milliseconds `18000`, `21000`, `25000`,
`28000`, `31000`, `34000`, and `37000`. The default command sequence remains
byte-for-byte equivalent to the original scripted replay. A repeated action
is particularly useful for checking whether `0x02` and `0x07` remain set
after their first successful use, rather than being cleared somewhere in a
later handler. Commands that fail parser validation or are blocked by the
game's own state rules are not necessarily comparable; inspect the action
rows and on-screen results before interpreting absent writes.

The subsequent user replay confirmed exactly those persistent effects:

| Time | Action | Changed state |
| ---: | --- | --- |
| 18,392 ms | OPEN | Drawer `0x0c=1`, `0x02=1`; book `0x07=1` |
| 21,422 ms | CLOSE | Drawer `0x0c=0` |
| 25,393 ms | OPEN again | Drawer `0x0c=1` **only** |
| 28,333 ms | TAKE | Book location `0xd3`, `0x02=1` |
| 31,333 ms | DROP | Book location `0xee` |
| 34,333 ms | TAKE again | Book location `0xd3` **only** |
| 37,326 ms | INVENTORY | No changes |

This run verified 189 RTLink section loads across nine sections and ended
with a different framebuffer SHA-256 than the original replay, as expected
from the different actions and final state. In this bounded scenario `0x02`
and `0x07` are persistent across close/reopen and take/drop cycles. That is
strong evidence for the earlier *interacted with* / *seen* interpretations,
but the full property semantics and initial-state rules remain unknown.

### Run several action experiments in one batch

Instead of manually running and summarizing one sequence at a time, use:

```sh
gateway-build
python3 tools/run_action_matrix.py --list
python3 tools/run_action_matrix.py --output build/action-matrix-001
```

This replays six independent first-room scenarios: baseline, the proven
OPEN/CLOSE/TAKE repeat, two order/guard experiments (`open-twice` and
`close-first`), a `take-twice` sequence, and a `take-before-open` sequence.
Every scenario starts in a clean guest state and uses the same initial LOOK,
seven fixed input slots and the native overlay-verification and state-write
hooks. The runner invokes `trace_game_logic.py` for each sequence and writes
`build/action-matrix-001/report.md`, along with the six full `native.trace`
files, register/memory dumps, framebuffer captures and `driver.log` files.
Existing output directories are never silently overwritten.

To try a shorter batch, select one or more of the predefined scenarios:

```sh
python3 tools/run_action_matrix.py --only open-twice,close-first \
  --output build/action-matrix-guards
python3 tools/run_action_matrix.py --only take-twice,take-before-open \
  --dry-run
```

The report groups verified **changed** object-state writes by guest command
slot. It retains `(no dispatch observed)` when a command never reaches the
known dispatcher, and records the dispatch return **without** treating
`AX=1` as success. A command may fail to parse, fail an earlier special guard,
or update additional uninstrumented state: absence of a changed write does
not prove a particular branch. These runs are designed to gather contrasting
evidence for future interpretation; results must be inspected before adding
new semantics to the C++ model. Full native replay and comparison against
multiple scenes remain future work.

### First standalone C++ state model

`src/story_model.hpp` and `src/story_model.cpp` introduce an **experimental
host-owned object state** and four confirmed successful-action transitions:

- OPEN sets properties `0x0c` and `0x02`; CLOSE clears only `0x0c`.
- TAKE moves the target into the player, sets `0x02`, and clears `0x08`/`0x0a`.
- DROP moves the target to the current room.
- A separate `observe(objects)` operation sets property `0x07`, since scene
  disclosure and the command itself are not yet proven to be the same routine.

The model emits mutations **only when logical object state actually changes**,
matching the trace's changed-only view. It uses no DOS guest memory, Unicorn,
SDL, script resources, or hardcoded Gateway object IDs. The `story-model`
CTest exercises the exact first-room repeated-action sequence and checks that
the second OPEN and TAKE preserve the persistent flags. It also checks that
TAKE would clear `0x08` and `0x0a` when they start set, whereas those clear
operations were redundant in the observed native trace.

The read-only common OPEN/CLOSE guard evaluator above is a **separate** step
from `apply_success`: it covers the directly disassembled property predicates
and their order, but it does not authorize calling `apply_success` without
all other DOS runtime checks. The independent test exercises mutually
contradictory flags to catch accidental changes to that sequence.

This is **not yet an interpreter or a general action implementation**:
`apply_success` assumes the original code has already accepted the command.
Reachability, earlier mode/other-object guards, object containment/list maintenance,
text output, room scripts and visual disclosure remain outside the model.
Successful state equivalence in this one replay must not be generalized to
other objects or Legend games without additional tests.

`--native-only` avoids two additional HLE comparison replays; omit it for the
full three-way regression check. Guest addresses are specific to the known
Gateway launch layout, and sections loaded in the same bank may require
residency checks when analyzing other games or actions.

For the modern replacement action layer, we can now distinguish the global
OPEN/CLOSE routines from object-specific event-6 handlers and reuse the
verified object-flag service ABI. Next steps are to distinguish redundant
`0x02` and `0x07` effects across more rooms and objects, recover mode guards
and object-list bookkeeping, then compare the independent C++ model against
multiple scenarios before considering native action replacement.
