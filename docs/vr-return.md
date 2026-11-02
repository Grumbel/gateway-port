# Native VR-return dispatcher: shared result branches and pending far-call frame

Investigation: 2026-11-02, original fingerprinted `GATEMAIN.EXE` and
`GATEMAIN.OVL`, Ghidra 11.4.2 / JDK 21, three new **unmodified** Study
Unicorn replays plus the earlier unmodified Beach-success replay. Original
OVL offsets below are **file offsets**, not guessed DOS load addresses.

## Result

Section-51 native function **`GATEMAIN.OVL:0x6d564–0x6d747`** is shared by
the Study therapy's refusal and terminal `SLEEP` branches and by successful
Beach VR. It selects one of several *native story-result branches*, performs
common cleanup, asks a resident engine service to transfer to the VR Terminal,
and then continues **later**, after that resident call returns. The player
therefore moves from the virtual room to **Terminal room `0x29b`** before
being placed inside **Terminal couch `0x2a1`**. A bounded snapshot stopped
between those stages reflects a real intermediate state, not a wrong location.

This is a pending **ordinary 16-bit far-call frame**, **not a new script
opcode or an established coroutine API**. A later no-follow-up-input control
proved that the VR frame completes on its own after about three seconds;
the SCREAM/LOOK input in earlier replay scripts was *not* the cause of its
return. Ghidra's linear decompilation displays both moves, but a C++ rewrite
must preserve any intervening room-entry/presentation work.

## Branch selection: original 16-bit instructions

The function first emits text `F000:70b8` and calls resident service
`048e:10f5` (service semantics not yet established). It checks the recursive
relation `0157:00c5(technician 0x2a3, Terminal 0x29b)` at `0x6d579`; failure
skips the result-specific branch and enters common cleanup. With the
technician present, precedence is:

| Guard (original OVL) | Path / text selected | Original side effects | Evidence level |
| --- | --- | --- | --- |
| `:78fa != 0` (`0x6d590`) **and** pin directly belongs to technician (`0x6d59c`) | Beach reward `F000:70b9` | Move pin `0x2a9 → 0x29b`, clear its flag `0x08` (`0x6d5c0–0x6d5df`) | Native Beach success and pin move; static branch order |
| Otherwise, word `:7920 == 0` (`0x6d5e3`) | No-result `F000:70be` | Store `:44e4 = 0x0038`, call `02e3:002b(0x2b,1)` (`0x6d68e–0x6d6a6`) | Both Study refusal and SLEEP native branch hooks |
| Otherwise, word `:7920 != 7`, byte `:7927 == 0` | `F000:70ba`, then `F000:70bc` | No local `:7927` write | Static only |
| Otherwise, word `:7920 != 7`, byte `:7927 != 0` | `F000:70ba`, then `F000:70bb` | No local `:7927` write | Static only |
| Otherwise, word `:7920 == 7`, byte `:7927 == 0` | `F000:70ba`, then `F000:70bd` | Call `1c24:434b(3)`, clear `:6b1a`, set `:7927=1` | Static only |
| Otherwise, word `:7920 == 7`, byte `:7927 != 0` | `F000:70ba`, then `F000:70bb` | Clear `:6b1a`, write `:7927=1`; no `434b` call | Static only |

Both `:78fa` and `:7920` are stored through original DS segment-pointer
words, not absolute flat addresses. The **meaning** of the raw `:7920`
result and the one-time `:7927` marker remains unknown. The names of
model paths describe the literal condition, **not an inferred story ending**.
The Beach reward takes precedence even if `:7920` is nonzero.

Every branch reaches a common path beginning at **`0x6d6a9`**:

1. Relocate object `0x0e4` to location zero (observed during both Study
   returns), store `:3942=1`, and invoke the native setter for property
   `0x0b` on object `0x165` (observed as an actual changed bit).
2. Invoke resident `02e3:00a2` for values `0x44,0x47,0x4f,0x52,0x66`.
   Their higher-level meaning has not been established.
3. Relocate the player to the *current virtual room* supplied by
   `[DS:965a]:35c6` at `0x6d71f`. For Study this is `0x2bc`, so the
   player first gets out of leather couch `0x2be`.
4. Call **`048e:0284(0x29b)`** at `0x6d72b`. This service invokes the
   verified native player-location move `Study 0x2bc → Terminal 0x29b`.
   Its nested event and presentation calls can remain active while the
   original section-51 far-call frame is still pending.
5. Once the resident `RETF` executes (without requiring a new command in
   the idle-tail control), continue at `0x6d73b`, issue
   `1c24:13d5(player 0x0d3, couch 0x2a1, slot 1)`, and reach the
   actual `retf` at `0x6d747`.

The last step is **not unreachable dead code**, nor does it necessarily
occur before a short replay's final snapshot. The resident call remains on
the normal 16-bit stack until it returns; the exact nested UI/presentation
wait mechanism is not yet completely identified. See [Resident room transfer](room-transfer.md).

## Native evidence for pending continuation

For these fingerprinted replays, RTLink section 51 lives at `4990:0000`
(linear `0x49900`) with original file image beginning at `0x6acd0`.
Thus the return entry `OVL:0x6d564` is native linear **`0x4c194`**;
its room-transfer instruction `0x6d727` is **`0x4c357`**, and the later
couch move `0x6d73b` is **`0x4c36b`**. No overlay-bank flattening is used.

| Unmodified replay | Return entry `0x4c194` | Transfer `0x4c357` | Couch continuation `0x4c36b`, `retf 0x4c377` | Final player relation |
| --- | ---: | ---: | ---: | --- |
| Repeated LOOK refusal, then SCREAM | 81,041 ms | 81,201 ms | 84,196–84,197 ms | Couch `0x2a1` |
| Repeated LOOK refusal, **no following input**, +10 s idle | Same refusal path (entry not hooked) | 81,201 ms | **84,196–84,197 ms** | Couch `0x2a1` |
| All 19 exercises, SLEEP, stop | 132,278 ms | 132,439 ms | **Not yet called** | Room `0x29b` |
| All 19 exercises, SLEEP, then LOOK | 132,278 ms | 132,439 ms | 135,180 ms | Couch `0x2a1` |

The first two earlier runs watch the native guest bytes for `:78fa`, `:7920`,
`:7927`, and all four Study-response fields. No value-changing writes to
those three return-branch fields occur after gameplay begins. The true
technician-relation result is observed as **`AX=1`** at `0x4c1c0`; both
execute the non-award instruction `0x4c213` and result-zero instruction
`0x4c2ae`, followed by cleanup `0x4c2d9`. Native object-state service
traces verify the `0xe4` removal, `0x165` flag setter, and **two successive
player moves** through Study to Terminal. The resumed runs additionally
verify native `0x29b → 0x2a1`, the original frame continuation, and `retf`.
The final `SLEEP` progress remains 20; the original match flag may remain
set because the response function's completion branch does not clear it.

The earlier **unmodified Beach** replay takes the alternate award path:
`OVL:0x6a118` sets `:78fa 0→1` at 109,251 ms; original return function
`0x4c194` starts at 109,251 ms; the pin moves from technician to Terminal
at **111,159 ms**, and its property `0x08` is cleared by the original
service. Later legitimate player pickup and Casino access were separately
validated in [Beach VR](beach-vr-replay.md). This validates the award
path's effects but does not yet capture all its internal branch PCs.

### Reproduction

Run `gateway-build` under Nix, or use `--probe` for an existing
Unicorn-enabled executable. The original DOS directory is external to Git.
The shared prefix and stage commands are in [Study VR](study-vr.md). To
obtain the new branch traces, add these original native entry hooks to
`tools/trace_game_logic.py --scenario puzzle --native-only --verify-overlays
--trace-navigation --trace-object-writes d3`:

```sh
return_hooks=(--extra-trace-entry 4c194 --extra-trace-entry 4c1c0
  --extra-trace-entry 4c213 --extra-trace-entry 4c2ae
  --extra-trace-entry 4c2d9 --extra-trace-entry 4c2e9
  --extra-trace-entry 4c357 --extra-trace-entry 4c36b
  --extra-trace-entry 4c377)
return_watches=(--trace-byte-write 62d5a --trace-byte-write 62d64
  --trace-byte-write 62d65 --trace-byte-write 62d66
  --trace-byte-write 62d67 --trace-byte-write 62d68
  --trace-byte-write 62d69 --trace-byte-write 62d80
  --trace-byte-write 62d81 --trace-byte-write 62d87)

python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --verify-overlays --trace-navigation --trace-object-writes d3 \
  "${return_hooks[@]}" "${return_watches[@]}" \
  --command-sequence "$prefix,look,look,look,scream" \
  --output build/vr-return-refusal

python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --verify-overlays --trace-navigation --trace-object-writes d3 \
  "${return_hooks[@]}" "${return_watches[@]}" \
  --command-sequence "$prefix,$exercises,sleep" \
  --output build/vr-return-sleep-paused

python3 tools/trace_game_logic.py --scenario puzzle --native-only \
  --verify-overlays --trace-navigation --trace-object-writes d3 \
  "${return_hooks[@]}" "${return_watches[@]}" \
  --command-sequence "$prefix,$exercises,sleep,look" \
  --output build/vr-return-sleep-resumed

python3 tools/inspect_vr_return.py \
  build/vr-return-refusal/native build/vr-return-sleep-paused/native \
  build/vr-return-sleep-resumed/native build/beach-to-casino/native
```

The final inspector argument is the existing original Beach reward replay
with a `0x62d5a` success-byte watch; its complete command script is in the
linked Beach document. If reproducing the resumed replay using all these
hooks, the verifier also accepts the extra watched fields and cleanup hook;
older resumed traces lacking them are checked against their recorded native
setter calls and final snapshot instead. All inputs must be unmodified
original execution: the checker rejects `research-poke` records and
unexpected return, movement or reward ordering.

## C++ and Ghidra boundaries

`StoryModel::evaluate_vr_return(StoryVrReturnInputs)` exposes **only the
ordered result-selection predicate**, original text-token offsets, and
indicated calls/field writes. It takes a caller-supplied recursive technician
relation and direct pin relation; it does not infer those from flat locations.
Neither this method nor the earlier `evaluate_study_response()` performs
the pending return frame, the resident room-transfer service, scene redrawing,
or native event/text calls. They are separate verified slices, not an
integrated C++ story VM.

The fingerprint-gated Ghidra importer now adds **24 original-byte-checked
interior labels** to the return routine, including branch guards, two
special result paths, common cleanup and the deferred couch continuation.
It does not invent function entries for those instructions. A fresh
Ghidra 11.4.2/JDK21 run imported 58 separate RTLink sections, 13,947
relocations, and exported **71/71** selected target contexts (including
the return routine and VR Terminal object handler). The known segmented
computed-jump decompiler warnings persist. The Ghidra pseudocode alone
cannot establish asynchronous native service behavior; the replay timings
above are the necessary cross-check.

The resident `048e:0284` room transaction, entry-event sequence, preserved
far return stack and explicit idle-tail control are now mapped in
[Resident room transfer](room-transfer.md). The result-7 one-time service,
the semantic meaning of `:7920`, and the `:7927` reset rules remain
unvalidated by natural input.
