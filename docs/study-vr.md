# Study VR: native `UP` callback and a Ghidra context correction

Investigation: 2026-11-02. This completes the **fourth room-specific path**
of room-exit callback `0x2b`, following the [Tanning Room](tanning-room.md)
and [VR Terminal/Beach](vr-up-callback.md) investigations. Input, state
changes, and binary offsets were checked against the original DOS executable
in native Unicorn. No game asset or DOS binary was modified.

## Reaching Study through the original game

From Your Quarters, travel `EAST, NORTHWEST, UP`, which reaches VR Terminal
`0x29b`. The first `LIE ON COUCH` opens the technician's multiple-page
briefing; advance seven pages with SPACE. Then enter:

```text
LIE ON COUCH
WEAR COLLAR
SET SWITCH TO STUDY
PUSH BUTTON
SPACE, SPACE, SPACE
```

The original game moves player `0x0d3` from VR Terminal couch `0x2a1` to
room **Study `0x2bc`** at **60674 ms**, then into its **leather couch
`0x2be`** at **63801 ms**. The Study contains Sigfreid `0x2ca`, a maple
desk `0x2bf`, leather chair `0x2c0`, and other native objects. It has two
kind-4 exits: `UP` calls callback `0x2b`, `DOWN` calls callback `0x05`.
The latter was investigated separately in the subsequent 19-stage replay below.

## Original handler: signed counter and direct parent

The Ghidra 11.4.2 context for callback `0x2b` (`GATEMAIN.OVL:0x693fc`,
RTLink section **50**, live `4990:02ec`, trampoline `2c24:13f3`) and the
original x86 disassembly agree:

| Original file offset | Native instruction effect |
| --- | --- |
| `0x693ff–0x6942c` | Require event `0x0f`, branch on enclosing room `0x2bc` |
| `0x69515–0x69524` | `0157:0546(player 0x0d3)` obtains the direct parent; require leather couch `0x2be` |
| `0x69526–0x69533` | If parent differs, print blocked-direction token `f000:bc0f` and return `AX=0` |
| `0x69536–0x69548` | Load native word `[DS:9634]:7904`; signed `jl`/`jle` accepts **3 through 19 inclusive** |
| `0x6954a–0x6954d` | Out of range on couch: print token `f000:bc10` and return `AX=0` |
| `0x694cf–0x69512` | In range on couch: common get-up text `f000:bc0e`, location setter `1c24:13d5(player, current room, 0)`, then `AX=0` |

This is a **direct-parent** comparison, not a generic recursive containment
query. The two counter comparisons use signed 16-bit conditions, so a raw
counter word `0xffff` is out of range as `-1`. The counter is *read*, not
changed, by this callback. `StoryModel::apply_study_up(player, progress)`
reproduces only the above bounded state effect, using caller-provided
progress and a zero callback return; it does not implement Study dialogue.

## Native evidence: natural refusal and controlled permission

The **unmodified** run entered Study, with the player in couch `0x2be`.
Its first `UP` invoked callback `0x2b` / event `0x0f` at
**75116–75163 ms**. The callback returned **`AX=0` without a player
write**. The final native word was **1**, below the allowed range.

For a second run, an explicit, compare-checked **research-only RAM
intervention** was scheduled at **74000 ms**, after the original engine's
own `0 → 1` word write at **72434 ms** (`4990:21c7`, original
`GATEMAIN.OVL:0x6ce97`) and before `UP`:

```text
research-poke16 ms=74000 address=62d64 before=1 value=3
```

The physical address was derived from the traced original DS `0x5b46`
and the state pointer at `DS:9634`. The hook requires the word to *equal*
the specified old value; otherwise it aborts the replay. The intervention
changes **guest RAM only**, does not edit original files, and is emphatically
**not** an ordinary successful playthrough.

With counter 3, original callback `0x2b` performed a watched native player
write **`0x2be → 0x2bc` at 75140 ms**, while returning **`AX=0`**.
The next `UP` at **78116–78137 ms** returned zero without moving a player
already directly in the room. Following the first successful `UP`, a
*separate original handler* incremented the word **3 → 4** at 75141 ms
(`4990:2321`, `GATEMAIN.OVL:0x6cff1`). The final native snapshot shows
player room `0x2bc` and counter 4.

The two records establish both callback branches by *original execution*,
but with different evidence levels: natural blocked input vs deliberately
prepared eligible state. `tools/inspect_study_up.py` verifies both and
rejects undocumented pokes, wrong native counter watches, absent player
word writes, wrong RTLink entries or returns, and spurious direct-room
movement. Eleven asset-independent negative/positive tests cover these guards.

## Ghidra seventh Study-targeted import and decompiler correction

A fresh Ghidra 11.4.2 / JDK 21 import validated the unchanged **58** RTLink
sections and **13,947** fixups. Five selected original object handlers were
provisionally decompiled:

| Native object | GATEMAIN.OVL entry |
| --- | --- |
| Study `0x2bc` | `0x6b521` (computed event jump still unresolved) |
| Leather couch `0x2be` | `0x6beaf` |
| Maple desk `0x2bf` | `0x6c0c0` |
| Leather chair `0x2c0` | `0x6c06d` |
| Sigfreid `0x2ca` | `0x6bc40` |

An additional manually selected **interior instruction** at `0x6cfda`
initially produced an explicit missing-result report (`25/26`), because
Ghidra had already recognized its containing function at original
`0x6ce85` (`GW_RT_051::04bab5`). `ghidra/DecompileGateway.py` now treats
such a selected `overlay-entry-candidate` as a **containing-function
context**, reports the actual function entry, and does not rename or invent
a boundary at the interior instruction. Existing confirmed entries still
fail explicitly if unexpectedly inside another function.

The fresh retry exported **26/26** target contexts, with
`gw_ovl_6cfda_candidate` marked `inside-containing-function`. The recovered
function shows native state word `0x7904`, its `0 → 1` initialization, its
dialogue-dependent progression, and the `0x6cff1` increment. Ghidra's
C-like output still cannot resolve the Study room handler's indirect jump
table or every far-call signature.

## Reproduce

With a Unicorn-enabled probe and external original DOS game data:

```sh
prefix='east,northwest,up,lie on couch,@space,@space,@space,@space,@space,@space,@space,lie on couch,wear collar,set switch to study,push button,@space,@space,@space,look'

python3 tools/trace_game_logic.py \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" --data ../gateway-dos \
  --native-only --scenario puzzle --verify-overlays --trace-navigation \
  --trace-object-writes d3 --extra-trace-entry 49bec \
  --output build/study-natural --command-sequence "$prefix,up,lie on couch,up"

python3 tools/trace_game_logic.py \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" --data ../gateway-dos \
  --native-only --scenario puzzle --verify-overlays --trace-navigation \
  --trace-object-writes d3 --trace-byte-write 62d64 \
  --research-poke16-at 74000:62d64:1:3 --extra-trace-entry 49bec \
  --output build/study-controlled --command-sequence "$prefix,up,up,look"

python3 tools/inspect_study_up.py \
  build/study-natural/native build/study-controlled/native
```

The puzzle scenario now accepts the same optional, audited, old-word-checked
`--research-poke16-at` as the original seven-slot object scenario, but only
with `--native-only --trace-navigation`. Omitting the option produces an
entirely natural replay. The inspector intentionally refuses to confuse
the two.

For the Ghidra context, export object state with
`tools/inspect_object_state.py`, then pass `--objects-json`, five
`--decompile-object` arguments (`0x2bc`, `0x2be`, `0x2bf`, `0x2c0`, `0x2ca`),
and `--decompile-offset 0x6cfda` to `tools/run_ghidra.py` in a **fresh**
output directory. The imported `pseudocode.txt` contains original game
instructions, so it remains generated/ignored rather than committed.

## Follow-up: all 19 Study therapy stages recovered with original input

The previous controlled `3 → 4` experiment is **no longer needed to reach
the successful branch**. A complete, unmodified native replay now follows
the therapeutic instructions and advances the original signed word
`[DS:9634]:7904` from `0 → 1 → ... → 20`, with every `1 → 2` through
`19 → 20` write executed by **`GATEMAIN.OVL:0x6cff1`** (`4990:2321`,
physical `0x4bc21`). The native initialization is at `0x6ce97`.

The source of the instructions is **not a script bytecode**. Ghidra shows a
single section-51 native function starting at original `0x6ce85` using:

- `[DS:9652]:0x03a2 + 4*progress` for a far-pointer prompt table;
- `[DS:9652]:0x03f2 + 4*progress` for a far-pointer response table;
- text calls `f000:70ad` / `f000:70af` for prompts and `f000:70b1` for
  responses, with `f000:70ae` for a repeat and `f000:70b0` past stage 19;
- the native word `0x7904`, previous-instruction word `0x7906`, and bytes
  `0x7908`, `0x7909` to select a prompt/repeat/response path.

The earlier decoder recognized **19 prompt/response pairs**. A further native
snapshot shows a **20th prompt** at `:03a2 + 4*20 = :03f2` (the response
table's *index zero*); it is the live guest string `sleep`. There are
still only 19 stage-specific response pointers (indices 1–19), because a
successful stage 20 follows the native completion branch instead.
Some entries are `F000` story tokens, others are ordinary DS or section-data
NUL-terminated strings. `tools/inspect_study_script.py` reads both kinds
through a checked native snapshot and the original `GATESTR.DAT`, without
hardcoding the displayed dialogue in the repository. The following commands
were accepted, in order, by the original game's parser; each corresponded
to one contiguous native progress increment:

| Stage | Natural typed input | Native action byte | Expected object | Other accepted bytes | Observed native effect |
| ---: | --- | ---: | ---: | --- | --- |
| 1 | `SCREAM` | `b1` | any | — | Counter 1→2 |
| 2 | `FEEL COUCH` | `a4` | `02be` | — | Counter 2→3 |
| 3 | `UP` | `47` | any | — | **Player couch `0x2be` → Study `0x2bc`**, AX=0, then 3→4 |
| 4 | `HOP` | `52` | any | — | 4→5 |
| 5 | `HIT MYSELF` | `08` | `00d3` | `55`, `56` | 5→6 |
| 6 | `STEP ON HAND` | `18` | `00e4` | — | 6→7 |
| 7 | `SMELL HAND` | `96` | `00e4` | — | 7→8 |
| 8 | `CLIMB WALLS` | `16` | `0165` | — | 8→9 |
| 9 | `JUMP ON DESK` | `52` | `02bf` | `18`, `00` | **Player Study → desk `0x2bf`**, then 9→10 |
| 10 | `SING` | `94` | any | — | 10→11 |
| 11 | `DANCE` | `23` | any | — | 11→12 |
| 12 | `DANCE` | `23` | any | — | 12→13 |
| 13 | `DOWN` | `1c` | any | `1b`, `00` | **Player desk → Study**, AX=0, then 13→14 |
| 14 | `ATTACK COUCH` | `08` | `02be` | `55`, `56` | 14→15 |
| 15 | `CLEAN DESK` | `15` | `02bf` | — | 15→16 |
| 16 | `CRY` | `1f` | any | — | 16→17 |
| 17 | `RUB HAND` | `88` | `00e4` | — | 17→18 |
| 18 | `KISS FLOOR` | `57` | `02de` | — | 18→19 |
| 19 | `SIT ON COUCH` | `92` | `02be` | `19`, `18` | **Player Study → leather couch**, then 19→20 |
| 20 (terminal) | `SLEEP` | `84` | any | — | Native completion: no 20→21 increment; player returns to VR Terminal `0x29b` |

The raw action bytes and expected objects in this table are **additional
original guest RAM data**, not translations inferred from the typed input.
The special `00e4` expected-hand record also accepts parsed object `00e3`
in native x86; `any` means a zero object word. The alternative byte `00`
at stages 9 and 13 is genuinely compared by the original loop and is not a
terminator. An entry matching the original *inner* predicate does not imply
the full parser can resolve and perform an arbitrary command with that byte.

### Decompiled event-1 predicate and independently watched match flag

The room handler's original **event 1** branch at OVL `0x6b692` performs a
special check for native action `0x96` with a zero parsed object, then enters
the therapy matcher at `0x6b6c0`. Its core decision sequence is:

1. Require native actor `[DS:965e]:35c8 == 0x00d3` (player), nonzero
   `[DS:9660]:7904` (therapy stage), and zero completion byte
   `[DS:9662]:7909`.
2. Index the two-byte target at `[DS:9652]:02aa + 4*stage`. If not zero,
   compare with parsed object `[DS:9658]:055e`, treating expected hand
   `0x00e4` as matching either `0x00e4` or `0x00e3`. Object mismatch
   bypasses the action check.
3. Compare native parser action byte `[DS:9654]:e664` with
   `[DS:9652]:02a8 + 4*stage` at OVL `0x6b735`. If equal, store `1`
   at completion byte `:7909` at **OVL `0x6b740`**.
4. Otherwise, at `0x6b786–0x6b7e8`, scan **five** four-byte records
   `{stage:u16, alt1:u8, alt2:u8}` at `[DS:9652]:02fc`, comparing *both*
   alternative bytes only for records with the current stage. Any match
   writes `:7909=1` at `0x6b7b7`. A separate event-1 branch handles
   action `0x84` and leather-couch reclining; it is not modeled.

This is a translation of **one native conditional predicate**, not a full
event dispatcher, parser, or new bytecode. `StoryModel::study_action_rule`
exposes the 19 exercises **plus the stage-20 SLEEP record** and
`StoryModel::evaluate_study_action(progress, action_code, object, actor,
already_matched)` implements only the bounded decision. It reports whether
the match was ineligible, targeted the wrong object, used a wrong action
byte, or matched. It does not set `:7909`, move the player, print text, or
advance the counter. Unlike the original unchecked 16-bit indexing, the
C++ method rejects out-of-range progress.

An additional **unmodified** Unicorn replay watched byte `0x62d69`
(`DS:9662:7909`) and hooked the original Study handler and response entry:

| Typed action / stage | Native set at guest physical PC | Time | Native clear |
| --- | --- | ---: | --- |
| `SCREAM` / 1 | `0x4a370` = OVL `0x6b740`, primary room matcher | 75235 ms | 75356 ms |
| `FEEL COUCH` / 2 | `0x4a370` = OVL `0x6b740`, primary room matcher | 78361 ms | 78501 ms |
| `UP` / 3 | `0x4bb1c` = OVL `0x6ceec`, response handler after exit callback | 81141 ms | 81212 ms |

All three original clears execute at `0x4bc4c` = OVL `0x6d01c` and
the response entry at `0x4bab5` executes for each stage. The native final
snapshot has progress **4**, player standing in Study, and no RAM
intervention. Stage 3 illustrates a crucial boundary: the successful `UP`
**is not** evidence that the room event-1 primary action comparator ran.
The earlier all-19-exercise replay separately proves every progress increment;
these additional watched bytes directly validate only stages 1–3's flag
mechanism. `tools/inspect_study_action_match.py` checks the three exact
native set/clear origins, event/response entries, action time slots, no
extra watched writes, no guest pokes and final state.

```sh
prefix='east,northwest,up,lie on couch,@space,@space,@space,@space,@space,@space,@space,lie on couch,wear collar,set switch to study,push button,@space,@space,@space,look'
python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" --data ../gateway-dos \
  --verify-overlays --trace-navigation --trace-object-writes d3 \
  --trace-byte-write 62d69 --extra-trace-entry 4a151 \
  --extra-trace-entry 4a370 --extra-trace-entry 4bab5 \
  --output build/study-action-match \
  --command-sequence "$prefix,scream,feel couch,up"
python3 tools/inspect_study_action_match.py build/study-action-match/native
python3 tools/inspect_study_script.py build/study-action-match/native \
  --strings ../gateway-dos/GATESTR.DAT --format json
```

The JSON decoder reads the **two** native matcher tables and the two
far-text tables from the snapshot at runtime. No original game dialogue or
generated decompiler output is bundled into source control.

An additional `UP` at counter **20** returns AX=0 without moving. It is
outside callback `0x2b`'s signed 3–19 get-up window. The native snapshot
finishes with player directly in couch `0x2be`, enclosing room Study and
counter `20`. Unlike the earlier 3→4 test, **no RAM intervention was used**.
It was *not yet* the terminal `SLEEP` action resolved below.

## The response state machine and terminal SLEEP, validated in native x86

The original section-51 function at **OVL `0x6ce85–0x6d04d`** (live
`4990:21b5`, physical `0x4bab5`) is a small but genuine native story state
machine. Its inputs are the four guest fields listed above, plus native
`0157:000c` direct-membership checks of player `0x0d3` against the Study
couch `0x2be` and desk `0x2bf`. Its bounded decision order is:

| Condition in original x86 | Control path and text token | Field changes |
| --- | --- | --- |
| Progress 0 (`0x6ce8f`) | Initialize to 1 | `:7904 = 1` |
| Progress 3, player no longer on couch; or progress 13, player no longer on desk (`0x6ceac–0x6ceec`) | Mark action success after UP/DOWN exit callback | `:7909 = 1` |
| Matched, progress 1–19 (`0x6cfda–0x6d031`) | Response pointer from `:03f2 + 4*old_progress`; text `f000:70b1` | Increment `:7904`, then clear `:7909`, `:7906`, `:7908` |
| Matched, progress above 19 (`0x6cfe6`) | `f000:70b0` (sleep), call auxiliary OVL `0x6d564` | No increment and no match-flag clear within this function |
| Not matched, prompted stage differs from current (`0x6cf6f`) | New instruction via `:03a2` and `f000:70af` | `:7906 = :7904` |
| Not matched, already prompted but `:7908` zero (`0x6cf20`) | Repeat instruction with `f000:70ad` | `:7908 = 1` |
| Not matched, prompted and `:7908` nonzero (`0x6cf55–0x6cf69`) | Stronger insistence `f000:70ae`, call auxiliary OVL `0x6d564` | No local counter advance |

`StoryModel::evaluate_study_response()` now translates only those guarded
state transitions. It returns the selected original `F000` token, far-table
stage, and whether the auxiliary native handler would be invoked; **it does
not generate story text or perform that handler's side effects**. The caller
must supply the two direct-parent predicates. It rejects invalid native
counter values that could index outside the verified table. Unlike the
previously bounded 19-action matcher, the native stage-20 action byte
`0x84` is now included in `StoryModel::study_action_rule()`.

Two independent, entirely **unmodified** Unicorn replays validate both
branches that previously remained static:

1. Repeatedly entering `LOOK` instead of following the first instruction
   sets the original `:7908` reminder byte at **78001 ms**
   (`4990:227c`, OVL `0x6cf4c`). The stronger refusal subsequently calls
   OVL `0x6d564` at **81041 ms**. Progress remains **1**, previous prompt
   remains 1, reminder remains 1, and the player is back on the VR Terminal
   couch `0x2a1` (enclosing room `0x29b`). There is **no progress increment**.
2. The complete 19-exercise sequence followed immediately by `SLEEP` sets
   `:7909` in the Study event-1 **primary** matcher at **132205 ms**, with
   stage 20. The response function enters at **132232 ms**, then invokes
   OVL `0x6d564` at **132277 ms**. The native location setter performs
   **two consecutive writes** at **132438 ms**: player from couch `0x2be`
   to Study `0x2bc`, then Study to VR Terminal `0x29b`. Final progress is
   **20**, `:7909` remains **1**, and the native player is standing in
   VR Terminal (rather than remaining on its couch).

The same six-byte watches over the *whole* completion replay establish the
19-stage match origins: **16** primary room matches at OVL `0x6b740`, **one
alternative match** for `JUMP ON DESK` (stage 9, OVL `0x6b7b7`), and **two
posture matches** for `UP` and `DOWN` (stages 3 and 13, OVL `0x6ceec`).
Every one of those 19 matched actions advances via OVL `0x6cff1` and clears
the flag via `0x6d01c`. The terminal SLEEP match is *different*: its
positive `0x7909` write does not lead to a 20→21 counter increment.

`tools/inspect_study_response.py` verifies the **pair of native replays**
against those exact PC origins and field-write sequences, the real
stage-20 action/prompt bytes, the two verified player moves, correct final
states, and absence of guest-RAM pokes. Its own synthetic tests reject
missing counter writes, a mislabeled stage-9 alternative, missing
posture matches or final moves, and spurious state changes. The terminal
prompt is read from guest RAM rather than hardcoded in the decoder.

### Reproduce the native state-machine cross-check

```sh
prefix='east,northwest,up,lie on couch,@space,@space,@space,@space,@space,@space,@space,lie on couch,wear collar,set switch to study,push button,@space,@space,@space,look'
exercises='scream,feel couch,up,hop,hit myself,step on hand,smell hand,climb walls,jump on desk,sing,dance,dance,down,attack couch,clean desk,cry,rub hand,kiss floor,sit on couch'

watch=(--trace-byte-write 62d64 --trace-byte-write 62d65 \
       --trace-byte-write 62d66 --trace-byte-write 62d67 \
       --trace-byte-write 62d68 --trace-byte-write 62d69)

python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" --data ../gateway-dos \
  --verify-overlays --trace-navigation --trace-object-writes d3 \
  "${watch[@]}" --extra-trace-entry 4bab5 --extra-trace-entry 4bb85 \
  --extra-trace-entry 4c194 --output build/study-refusal \
  --command-sequence "$prefix,look,look,look,scream"

python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" --data ../gateway-dos \
  --verify-overlays --trace-navigation --trace-object-writes d3 \
  "${watch[@]}" --extra-trace-entry 4bab5 --extra-trace-entry 4c194 \
  --output build/study-sleep --command-sequence "$prefix,$exercises,sleep"

python3 tools/inspect_study_response.py \
  build/study-refusal/native build/study-sleep/native
```

The repeated `--trace-byte-write` option is a small research-only addition
to the native probe. It accepts at most **16 distinct guest addresses**
within a **64-byte span**, checking word writes that overlap those bytes
without modifying original execution. Wide arbitrary-memory tracing remains
intentionally disallowed.

## Callback `0x05`: desk and scale DOWN, including a natural refusal

The original `GATEMAIN.OVL:0x6958d–0x69648` (section 50, live
`4990:047d`, trampoline `2c24:156f`) is a **shared** callback, referenced
by `DOWN` in Study `0x2bc` and in Guess Your Weight booth `0x047`.
It accepts event `0x0f` and always returns `AX=0`. Its first native
`0157:000c(player, 0x2bf)` tests the type-7 player's *direct relation*
to the maple desk; the second tests direct relation to scale `0x087`.
This service is **relation-slot membership**, not recursive containment.

If on the desk, the code blocks `DOWN` for signed `0x7904` in **9–12**
inclusive (original `0x695af–0x695d3`, text `f000:bc12`). Otherwise it
describes getting down (`f000:bc13`) and invokes the original location
setter with the current room from `[DS:9632]:35c6`. The scale branch
uses text `f000:bc14` and the same setter, **without** a Study counter
test. Neither branch returns a navigable room ID in AX.

In a second **unmodified** keyboard replay, `DOWN` immediately after
`JUMP ON DESK`, with progress **10**, entered callback `0x05`, returned
zero and **did not move**. After `SING`, `DANCE`, `DANCE`, progress was
**13**. `DOWN` at **114200 ms** executed the same original callback,
wrote player **`0x2bf → 0x2bc`** through the verified native service,
returned `AX=0`, and then advanced progress 13→14. The full replay's
non-interrupted stage-13 `DOWN` makes that same move at **111200 ms**.
The `0x087` scale positive path is *statically* decoded but has not been
replayed. `StoryModel::apply_desk_scale_down` deliberately models only
the bounded type-7 player location effect, not its dialogue output.

`tools/inspect_study_therapy.py` verifies the full and early-DOWN traces
as a **pair**: original counter initialization and every contiguous stage
increment, 19 action-specific time slots, watched player word writes,
correct callback ID/section/trampoline/AX, the actual `0x6958d` event
entry, refusal at counter 10, success at counter 13 and final refusal at
counter 20. It rejects missing/reordered writes, extra pokes, missing
native callback execution and spurious movement. Thirteen synthetic
negative/positive tests exercise these constraints.

### Reproduce the two unmodified native replays

```sh
prefix='east,northwest,up,lie on couch,@space,@space,@space,@space,@space,@space,@space,lie on couch,wear collar,set switch to study,push button,@space,@space,@space,look'
phases='scream,feel couch,up,hop,hit myself,step on hand,smell hand,climb walls,jump on desk,sing,dance,dance,down,attack couch,clean desk,cry,rub hand,kiss floor,sit on couch,look,up'
gate='scream,feel couch,up,hop,hit myself,step on hand,smell hand,climb walls,jump on desk,down,sing,dance,dance,down,look'

python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" --data ../gateway-dos \
  --verify-overlays --trace-navigation --trace-object-writes d3 \
  --trace-byte-write 62d64 --extra-trace-entry 49d7d \
  --extra-trace-entry 4bab5 --output build/study-therapy-full \
  --command-sequence "$prefix,$phases"
python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" --data ../gateway-dos \
  --verify-overlays --trace-navigation --trace-object-writes d3 \
  --trace-byte-write 62d64 --extra-trace-entry 49d7d \
  --extra-trace-entry 4bab5 --output build/study-therapy-desk-gate \
  --command-sequence "$prefix,$gate"
python3 tools/inspect_study_therapy.py \
  build/study-therapy-full/native build/study-therapy-desk-gate/native
python3 tools/inspect_study_script.py build/study-therapy-full/native \
  --strings ../gateway-dos/GATESTR.DAT
```

The physical `0x62d64` counter watch is specific to the fingerprinted
replays' DS/segment allocation: the inspector independently follows the
actual native pointer and rejects a different address. An alternate DOS
layout must derive a new watched address before reproducing it.

## Study room handler jump table: 23 exact targets, Ghidra limitation

The original Study object handler begins at **OVL `0x6b521`**, section 51.
At `0x6b528–0x6b539`, it subtracts one from the event, bounds it to
`0..22`, doubles the index and jumps through **`CS:[BX+0x0b82]`**.
The section's verified file image base is `0x6acd0`, so the table begins
at original **OVL `0x6b852`** and has **23 u16 offsets**, each relative
to that section base. The original bytes give:

| Native event IDs | Target OVL file offset |
| --- | ---: |
| `1` | `0x6b692` |
| `6` | `0x6b82d` |
| `8`, `9`, `11` | `0x6b56b` |
| `14` | `0x6b5bf` |
| `22` | `0x6b53e` |
| `23` | `0x6b555` |
| `2–5`, `7`, `10`, `12–13`, `15–21` | `0x6b5ba` (default) |

These target offsets have been decoded from the verified table, **not**
represented as separately validated Ghidra function entries. Reproduce
them with `tools/disassemble_story.py --start 0x6b521 --size 0x1f
--section-base 0x6acd0 --event-table 0x6b852 --events 23` and the
original OVL file path. Their detailed event semantics remain partial:
event 14 prints an introduction and invokes the known property-set service
`0157:03e0(object, 0x07)` on **12 objects**: `0x2be–0x2c8` plus
Sigfreid `0x2ca` (OVL `0x6b5bf–0x6b68f`). Events 8, 9 and 11 print a
Study description selected by property `0x09` of `0x2c8` (`0x6b56b`).
Event 6 (`0x6b82d`) checks current room and native action `0x3a`;
events 22 and 23 call distinct `1c24` services with section-data pointers,
but their higher-level meanings remain unresolved. These details come
from static x86, not separately verified runtime branches.

A fresh Ghidra 11.4.2/JDK21 import with 44 callback labels, three selected
Study objects and two selected native code offsets (`0x6ce85`,
`0x6cfda`) exported **69/69 contexts** and revalidated all **58 overlay
sections / 13,947 fixups**. It recognized `0x6ce85` as a function entry
and `0x6cfda` as an interior context of that same function. Ghidra still
reports an unresolved read of `ram:4000:0b82` for the Study object's
computed event jump. The table above is the original-instruction cross-
check; **69/69 decompilations are not proof of correct indirect control
flow**, and no synthetic overlay-bank flattening has been introduced.

The importer now marks `gw_study_event_jump_table` at section-51 offset
`0x0b82`, types all 23 verified words as u16 data, and creates same-overlay
references to their internal branches. Ghidra remains unable to resolve
the segmented indirect branch in its C-like output; the explicit references
provide an original-byte-checked navigable fallback. The runner verifies
the import report's 23 annotations for fingerprinted Gateway originals,
without imposing this mapping on synthetic images.

A subsequent fresh headless import additionally verified **nine** original
instruction byte anchors within event 1, giving them named *interior-code*
labels (`gw_study_actor_guard`, `gw_study_target_lookup`,
`gw_study_primary_compare`, `gw_study_alternative_loop`, and five related
match/write labels). The import report records
`study_matcher_path_labels=9` alongside the 23 jump-table words, and the
runner checks both counts. The fresh run exported **69/69** contexts,
again with 58 overlays and 13,947 relocations. These labels create no
fictitious C functions and do not claim to repair Ghidra's segmented switch.
