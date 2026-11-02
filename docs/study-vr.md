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
The latter is **not** part of this model.

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
