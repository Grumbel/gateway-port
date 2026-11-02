# Game logic: native code and RTLink overlays

Investigation: 2026-10-31, original Gateway distribution. Binary identities are
in [executable.md](executable.md). In particular, `GATEMAIN.OVL` SHA-256 is
`05e14a53904c356cba5f9fbbc9de98cf8fb1a5bdb3b6bcbeff2ae97482982d71`.
All offsets below are hexadecimal **file offsets**, not runtime addresses.

## Conclusion and limits

Game-specific behavior is implemented in native 16-bit x86 routines in
`GATEMAIN.OVL`, alongside engine routines and data. `GATEMAIN.EXE` is the resident
main program and contains additional engine code and tables. RTLink pages overlay
code into conventional memory; it is an executable overlay manager, not evidence
of a story-bytecode VM. No separate story script or bytecode file has been found.

This establishes native implementation for the concrete behaviors below. It does
not prove that every behavior is native, exclude embedded interpreted tables, or
identify the original authoring language. The routines have C-like stack frames,
argument passing, caller stack cleanup, and far returns; C or a language compiled
to C is plausible, but not established from those conventions alone.

## File responsibilities

| File | Role and evidence |
| --- | --- |
| `GATEMAIN.OVL` | Primary target for recovering story behavior: native state tests, updates, calls, and story-text selection demonstrated below; also parser/UI routines and tables |
| `GATEMAIN.EXE` | Resident main executable, runtime/engine code and tables; references `vocab.dat`, `object.dat`, and `RESTART.DAT`; exact story/engine split remains unmapped |
| `GATE.EXE` | Intro/launcher; hands off to the main executable |
| `GATE.RTL`, `RTLINKST.COM` | RTLink-loaded support and startup; `GATE.RTL` includes graphics/resource/audio support evidence; not identified as the main story container |
| `GATESTR.DAT` | Compressed text selected by code; not an identified executable script |
| `VOCAB.DAT` | Parser words and linking/classification tables; semantics of link groups remain unresolved |
| `OBJECT.DAT` | Object/room names, not complete world behavior |
| `*.RGN`, `*.PIC`, `*.FNT`, `*.MUS`, `*.RS` | Hit regions, graphics, fonts, music, and sound; no identified story instruction stream |
| `RESTART.DAT`, saves | Generated writable state, not missing original story scripts |

The existing [handoff trace](dos-handoff.md#rtlink-overlay-reads-and-translated-code-coherence)
confirms that DOS reads from `GATEMAIN.OVL` replace executable code. For example,
`build/fresh-main.trace` opens it on handle 6, seeks to `0x9300`, reads `0x1c60`
bytes, and subsequently loads other sections. Runtime segment addresses depend
on relocation and the current overlay; static far-call operands below should not
be used directly as runtime breakpoints.

## Native story behavior: salamander

At `GATEMAIN.OVL:0x72a97`, native instructions load a state segment through
`DS:0x970c`, compare `ES:0x35c6` against `0x276`, and conditionally select text:

```asm
; File offsets, on-disk operands before runtime relocation
72a97  mov es, word ptr ds:0x970c
72a9b  cmp word ptr es:0x35c6, 0x276
72aa2  jne 0x72ab6
72aa4  mov ax, 0xb464
72aa7  mov dx, 0xf000
72aaa  push dx
72aab  push ax
72aac  call 0x17cd:0x105b
72ab1  add sp, 4
```

The selected text is `GATESTR.DAT` segment 45, string 100: the salamander emerges
from a tank and leaves the hut. Immediately preceding code selects `0xf000b463`
(segment 45, string 99), describing the salamander carrying items from the lake.
It supplies two additional far-pointer arguments, consistent with that text's
two `%s` placeholders. This ties the native instructions to specific story events,
rather than just to generic rendering or a possible VM implementation.

The semantic name of the tested state field and value `0x276` remains unknown.
The routine ends at `0x72ab6` with a zero result, stack-frame restoration, and
`retf`. Structured records and literal text follow; the overlay mixes code/data.

## Native state update and dialogue

At `0x6cfda`, the routine compares `ES:0x7904` against 19. The following code
increments that field, multiplies its old value by four, fetches a far pointer
from a table, and supplies it to the same text-call target:

- `0x6cfe6`: selects message `0x70b0` on one branch.
- `0x6cff1`: increments the state word at `ES:0x7904`.
- `0x6cff6`–`0x6cff8`: two `shl bx,1` instructions index four-byte entries.
- `0x6cffe`–`0x6d003`: pushes the selected pointer's two words.
- `0x6d008`–`0x6d010`: selects `0xf00070b1` and calls `0x17cd:0x105b`.
- `0x6d018`–`0x6d031`: clears three other state fields.

Decoded text under the addressing rule below:

| Token | Segment/index | Text meaning |
| --- | --- | --- |
| `0xf00070af` | 28/175 | A speaker says “I'd like you to %s” and looks expectantly; two `%s` substitutions |
| `0xf00070b0` | 28/176 | The player drifts into relaxed sleep |
| `0xf00070b1` | 28/177 | `%s scribbles something inside his notebook, nodding a bit.` |
| `0xf00070b2` | 28/178 | Response explaining that `PASSWORD` is not the password |

These are native branching/state-changing routines that directly choose story
responses. The [Study VR investigation](study-vr.md) identifies `0x7904`
as the signed Study therapy progress word, confirms original initialization
from `0 → 1`, and subsequently recovers **all 19 ordinary player actions**
needed to advance it naturally through **`1 → 2 → ... → 20`**. Two native
far-pointer tables (`[DS:9652]:0x3a2` and `:0x3f2`, four-byte entries)
provide prompts and responses; original x86 at `0x6cff1` performs every
successful increment. Ghidra recognizes the containing section-51 function
at `0x6ce85`; a reproducible snapshot decoder reads the real mixed F000
tokens and guest-resident strings. The earlier single-word intervention
is retained as a historical cross-check but is no longer needed to reach
Study `UP`'s positive branch.

## Text tokens: correction to the earlier hypothesis

For these six checked references the addressing rule is:

```text
token = 0xf0000000 | (segment_index << 10) | string_index
segment_index = (token & 0x0fffffff) >> 10
string_index = token & 0x3ff
```

This fits all six selected strings and the argument counts of the observed calls.
It differs from the earlier proposed `0xFsssnnnn` split into a 12-bit segment and
16-bit string number. For example, treating `0xb464` as a string number in segment
0 would be out of range; segment 45/string 100 fits exactly. The decoding logic
of the text-call target itself has **not** yet been traced, so the general token
format beyond these checked references remains to be verified there.

## Reproduction and next useful work

Disassembly needs only GNU binutils:

```sh
objdump -D -b binary -m i8086 -M intel \
  --start-address=0x72a60 --stop-address=0x72abc ../gateway-dos/GATEMAIN.OVL
objdump -D -b binary -m i8086 -M intel \
  --start-address=0x6cf90 --stop-address=0x6d04e ../gateway-dos/GATEMAIN.OVL
```

Decode the referenced segment/string pairs using the Huffman layout in
[data-files.md](data-files.md#gatestrdat). This investigation used a throwaway
Python decoder; it has not added a production text parser or runtime changes.
No original assets were modified, and no new DOSBox gameplay comparison was run.

Next recover RTLink section/relocation mappings, locate the text-call target in
a loaded snapshot, and trace a simple command from parser output through handler
dispatch to state changes and text. Map room/object/action tables and native
handler pointers before designing a replacement interpreter. Asset decoding
alone will not recover the demonstrated story routines.

## Follow-up: traced dispatch and first host replacement

[Engine-boundary investigation](engine-boundary.md) now traces a typed `look`
through action and room-handler dispatch, confirms the text-token split in the
lookup routine, and replaces the shared object-flag query in a bounded replay.
The original and host query agree on 620 calls across `look`, `open drawer`, and
`look`, with identical final framebuffers in native, verification, and HLE modes.
The remaining work is mapping the rest of the handler/state ABI and RTLink data.

## First reproducible annotated disassembly

`tools/disassemble_story.py` is a bounded linear-sweep disassembler for the
original 16-bit overlay. It annotates the confirmed `mov ax,token; mov dx,f000;
push dx; push ax; call far` pattern with the `GATESTR.DAT` segment and string
index. It **does not** decompile C, resolve RTLink relocations, or decide whether
an arbitrary sequence is code rather than embedded data. Treat every printed
text match as a candidate until its call target is identified.

```sh
python3 tools/disassemble_story.py ../gateway-dos/GATEMAIN.OVL \
  --start 0x72a97 --size 0x25
```

On the binary identified above, this shows the native conditional at `0x72a9b`
and annotates the far call at `0x72aac` with **segment 45, string 100**. The
behavior can be provisionally expressed as this C-like *pseudocode* (not source
reconstruction):

```c
if (read_word(read_segment(DS, 0x970c), 0x35c6) == 0x0276) {
    display_story_text(/* GATESTR segment 45, string 100 */);
    goto earlier_handler_code; // file offset 0x72a41
}
return 0; // far return from this native routine
```

`DS:970c` is a pointer to a state segment; `ES:35c6` is an **unidentified**
state word, not a named `salamander_state` field yet. The caller, action/event
that reaches this branch, and meaning of `0x276` need dynamic confirmation.
Far call operands printed from disk are pre-relocation, and should not be used
as live breakpoints without the RTLink map. The utility's tests intentionally
reject annotations when an intervening instruction breaks the known sequence.

For a modern engine, the next useful slice is a single *room event*:
resolve its handler entry from the exported object inventory and overlay
manifest, identify the event switch, follow all state reads/writes and story
tokens, then replay the same player command to validate the C-like translation.
This is an incremental translation of native routines, not extraction of a
standalone script language.

The first full room-event table and partial `look` translation are now recorded
in [Your Quarters](room-your-quarters.md), including confirmed text 3/0–3/5,
all 24 jump destinations, object flag reads/writes and outstanding field names.

The kind-4 room-exit path also has a [complete 44-entry native callback
directory](room-callbacks.md): 236 records map to verified RTLink trampolines
in 22 overlay sections. Ghidra now emits pseudocode for every entry, and a
Casino EAST Unicorn replay validates one callback's event `0x0f`, `AX=0`
result and no-move outcome. Other callbacks' branches remain static leads.
An opt-in, logged single-word pin-relation intervention now also validates
that callback's positive return `AX=0x0f6` and corresponding original
native room move; it is deliberately not described as ordinary gameplay.
The later [complete Beach-to-Casino replay](beach-vr-replay.md) established
the legitimate pickup and entry without intervention. A subsequent
[Tanning Room callback analysis](tanning-room.md) demonstrates a different
kind-4 outcome: event `0x0f` returns `AX=0` **after** moving the player
from a couch back to its enclosing room. A modern engine must model such
side effects separately from the room destination returned in AX.
The later [Terminal and Beach `UP` replays](vr-up-callback.md) establish
two more branches of callback `0x2b`: a worn-collar refusal in the Terminal,
and a nested Beach get-up followed by a native glass-property clear call.
Both return `AX=0` even when the player moves; room `0x2bc` remains
only statically examined in those earlier runs. The subsequent
[Study VR investigation](study-vr.md) identifies the state word `ES:7904`
as a signed Study progress counter. Original input confirms the blocked
`UP` at value 1; a separately audited word intervention confirms the
positive `3–19` branch moves the player from the leather couch back into
the Study room, **also returning `AX=0`**. The subsequent normal gameplay
replay independently takes that branch at counter **3**, completes all
19 native therapy stages through counter **20**, and validates the shared
desk/scale `DOWN` callback `0x05`: a natural desk refusal at counter **10**
and a native desk-to-Study move at **13**, both returning zero.

The next native slice reconstructs the **Study room event-1 therapy
action matcher** (`GATEMAIN.OVL:0x6b6c0–0x6b7e8`): 19 records each
contain an original parser action byte and expected object ID, with five
additional two-action alternative records. Its 16-bit stage index comes
from `ES:7904`; a successful comparison writes completion byte `ES:7909`,
which the *separate* section-51 response function consumes. Native watches
confirmed `SCREAM` and `FEEL COUCH` matching in the room handler, while
`UP` marks completion in the response handler after its exit callback.
`StoryModel::evaluate_study_action` expresses the bounded inner predicate
without claiming full parser, event, or text behavior. The original RAM
tables are recoverable as JSON through `tools/inspect_study_script.py`;
the native flag path is checked by `tools/inspect_study_action_match.py`.
See [Study VR](study-vr.md) for exact offsets and evidence boundaries.

Further original instruction and **two unmodified native replays** reveal
the follow-on Study response state machine (`0x6ce85–0x6d04d`) and a
**20th terminal instruction, `SLEEP`**, with action byte `0x84`. Unlike
the preceding 19 exercise instructions, SLEEP triggers native text
`f000:70b0` and auxiliary handler `0x6d564` without incrementing progress
beyond 20. Two original location writes move the player from the Study
couch through the Study room back to VR Terminal. A contrasting repeated
LOOK sequence sets reminder byte `:7908`, calls the same auxiliary native
handler on insistence, and leaves the therapy counter at 1. The new
`StoryModel::evaluate_study_response` expresses only the branch/field
effects and emits original text-token identifiers, not a story renderer.
`tools/inspect_study_response.py` checks all 19 match origins and the
terminal/refusal state writes. The Ghidra importer now exposes 18 interior
response instructions and seven exact computed-switch references, though
the decompiler's segmented branch remains unresolved.
