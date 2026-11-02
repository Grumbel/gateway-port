# Event-aware native room callback coverage (bundle 096)

The 44 native room-exit callback IDs in [room-callbacks.md](room-callbacks.md)
are **not exclusively used for room movement**. The same RTLink function can
be invoked for object events and room-exit events, with different arguments
and results. Bundle 095's `--trace-rtlink-calls` counted invocations accurately
but could not, by itself, distinguish those reasons for calling the function.

`tools/audit_callback_coverage.py` correlates that section-qualified call
census with the original resident callback gate's verified call/return hooks
(`1474:0019` / `1474:001e`, guest linear `0x14759` / `0x1475e`). It matches
the original `SS:BP` frame and verifies callback ID, event argument, RTLink
trampoline pointer and section against a freshly decoded native snapshot.
For **room-exit** attribution it additionally requires the resident room
scanner's call and matching returned `AX` at `148e:0797` / `148e:079c`.

The audit rejects missing return hooks, mismatched callback pointers or
sections, inconsistent native return values and cases where gate-call counts
exceed verified trampoline counts. A native callback can return `AX=0` while
still changing object location, so **AX alone never proves or excludes a
movement**. The existing player-record and pin verifiers provide independent
state evidence below.

## Four original, unmodified replays

All replays ran native 16-bit x86 through Unicorn, with native overlay loading
and `--verify-overlays`; none used story HLE or research memory interventions.
They differ only in normal scripted user input. The Ghidra mapping of
callback `0x14` is section 48 (`GATEMAIN.OVL:0x665f9`), and callback `0x2b`
is section 50 (`0x693fc`).

| Replay | All verified RTLink calls / distinct targets | Callback gate calls | Verified room-exit returns |
| --- | ---: | --- | --- |
| Casino EAST, no pin | 22,432 / 36 | `0x14`: 13 | `0x14`: **1**, `AX=0` |
| Full Beach reward, pin pickup, Casino EAST | 188,001 / 69 | `0x14`: 5; `0x2b`: 24 | `0x14`: **1**, `AX=0x00f6` |
| VR Terminal collar/UP sequence | 88,118 / 42 | `0x2b`: 11 | `0x2b`: **2**, both `AX=0` |
| VR Beach nested/direct UP sequence | 87,901 / 47 | `0x2b`: 13 | `0x2b`: **2**, both `AX=0` |

The event/AX breakdown is the key observation:

| Replay and ID | Event `0x0001` (non-exit) | Event `0x000f` (scanner-confirmed) |
| --- | --- | --- |
| Casino refusal, `0x14` | 12 calls → `AX=0x00f6` | 1 call → `AX=0` |
| Casino admission with pin, `0x14` | 4 calls → `AX=0x00f6` | 1 call → `AX=0x00f6` |
| Full Beach reward, `0x2b` | 24 calls → `AX=0` | 0 |
| Terminal UP, `0x2b` | 9 calls → `AX=0` | 2 calls → `AX=0` |
| Beach UP, `0x2b` | 11 calls → `AX=0` | 2 calls → `AX=0` |

For the unmodified full Beach route,
`tools/inspect_beach_pin.py --expect-beach-casino` independently verified
the native Beach reward byte, transfer of the membership pin to the terminal,
pickup by the player and **Casino `0x0f5 → Pedroza Lounge 0x0f6`** player
movement immediately after the positive callback return (132,268 guest ms).
The ordinary Casino refusal did **not** move the player through that exit.

For the two VR runs, `tools/inspect_vr_up.py` independently checked the
original player-location words: a worn collar vetoed Terminal `UP`, removing
it allowed **couch `0x2a1 → room 0x29b`**, Beach `UP` moved the nested player
**`0x2b1 → 0x2ae`**, and the second Beach `UP` left the player there.
Every one of those room-scanner returns had `AX=0`. This is a genuine
state-changing callback path, not a destination selected by return value.

These outcomes establish **specific branches in these two callback IDs**.
They are not evidence that all 44 callbacks have been reached, or that the
story is proportionally understood. In particular, a frequent event-1
return containing an object or room ID must **not** be mistaken for an
accepted directional exit.

## Reproduce the census and audit

Generate the callback directory once from any valid native snapshot plus the
fingerprinted `gateway-overlay --fixups` manifest, as described in
[room-callbacks.md](room-callbacks.md#reproduce-and-inspect). Then collect an
ordinary Casino negative branch:

```sh
python3 tools/trace_game_logic.py \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" \
  --data ../gateway-dos --scenario objects --native-only \
  --verify-overlays --trace-navigation --trace-rtlink-calls \
  --command-sequence 'east,east,east,east,look,inventory,look' \
  --output build/callback-casino-negative

python3 tools/audit_callback_coverage.py \
  build/callback-casino-negative/native.trace \
  build/callback-casino-negative/native.rtlink-calls \
  build/room-callbacks.json --format json
```

The longer ordinary reward/pin route is reproduced with the exact 39-command
sequence in [beach-vr-replay.md](beach-vr-replay.md#reproduce-and-inspect);
append `--trace-rtlink-calls` to that native replay command and feed the two
resulting traces to the auditor. For the two other sequences, see
[vr-up-callback.md](vr-up-callback.md#two-original-game-replays) and likewise
enable `--trace-rtlink-calls`. The JSON output contains per-ID event/AX
counts and the verified room-scanner return entries; by itself it makes no
claim about physical memory writes.

The auditor is intentionally strict about trace completeness: it needs
`--trace-navigation`, a native snapshot's byte-verified RTLink callback
directory (including the original overlay file offsets), and the complete
call census from the *same replay*. It cannot legitimately reconstruct
event outcomes from an older `--trace-blocks` last-256-block dump or from
the census alone.

## Ghidra context

The original room callback entries were already isolated into their distinct
Ghidra overlay address spaces. Their provisional C-like output is useful
for candidate guard branches but cannot establish runtime AX results or
mutations without native traces. In particular, the native results above
verify that callback `0x14`'s event-`0x000f` path depends on an inventory
relation while its event-1 path returns the Lounge ID without navigation;
callback `0x2b` has room-dependent effects even though its room-exit return
is zero. See [vr-up-callback.md](vr-up-callback.md) for its original x86
branch offsets, and [native-handler-coverage.md](native-handler-coverage.md)
for the larger 696-handler inventory and limits of static prefix grouping.

A fresh Ghidra 11.4.2/JDK21 run for this batch imported **all 58 sections
and 13,947 segment fixups** and exported **76/76 requested contexts**, including
all 44 callback entries. At `0x665f9` (callback `0x14`), its provisional
listing explicitly takes the *non-`0x0f`* path to `AX=0x0f6` without the
membership test. For event `0x0f`, it has separate refusal (`AX=0`) and
admission (`AX=0x0f6`) paths. At `0x693fc` (callback `0x2b`), it returns
zero immediately for non-`0x0f` events, and branches on native room IDs
`0x29a`, `0x29b`, `0x2ae` and `0x2bc` only for exit events. These branching
structures agree with the original native event/AX counts above; the
decompiler still omits some far-call arguments and does not justify a
complete game-logic port.

## Bounded C++ callback translation

`StoryModel::apply_casino_callback(player, event)` now represents just the
confirmed section-48 callback-`0x14` state/AX contract:

- For **event other than `0x0f`**, return `0x0f6`, with no state mutation.
- For **event `0x0f`** with membership pin `0x2a9` directly related to the
  player, return `0x0f6`, again without a mutation.
- For **event `0x0f`** without that direct relation, return zero and clear
  heavy oak door `0x1ad` property `0x0c` if set.

The sparse model records **state deltas**. The original native refusal
branch *calls* the flag-clear service even when the flag is already clear;
the model does not record that no-op call, print narrative text, test parser
reachability, or perform a resident room transfer. Its C++ unit tests cover
non-exit use, refusal, admission and an already-clear flag, alongside the
existing verified `0x2b`/`0x05` branch models.
