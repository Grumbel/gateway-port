# Gateway decompilation and reconstruction plan

This plan turns Gateway's native game logic into maintainable C++ while the
original program keeps running. It is written so that an agent can execute
it step by step. Read **§1 Ground rules** and **§4 Operating loop** at the
start of every session, then continue at the first unfinished step.

Status markers: `[ ]` not started, `[~]` in progress, `[x]` done. Update the
markers and the progress log (§9) in the same commit that completes a step.

Scope: the SHA-256-pinned `GATEMAIN.EXE` / `GATEMAIN.OVL` listed in
[executable.md](executable.md). Spellcasting 101/201/301 and Gateway II are
later targets (§8.3), never mixed into Gateway's tables.

---

## 0. Where the project stands (baseline, 2026-11-03)

- `gateway` runs the original real-mode code under Unicorn with DOS, VGA,
  AdLib, mouse and keyboard replaced by host implementations. Gateway reaches
  Your Quarters and is playable as far as tested; no complete playthrough
  is verified.
- RTLink is understood: 58 overlay sections, 13,947 fixups, ten-byte
  trampolines `CALL manager; JMP far target; WORD section`
  ([rtlink.md](rtlink.md)). Sections share physical addresses, so a routine's
  identity is its **original file offset**, never its runtime address.
- **There is no story bytecode.** Game logic is compiled native code:
  - 696 object/event handlers, reached via `[DS:8db4]`/`[DS:8db6]`
    ([object-state.md](object-state.md));
  - 44 room-exit callbacks ([room-callbacks.md](room-callbacks.md));
  - 195 verb (action) handlers in the table at `[DS:8da8]`
    ([engine-boundary.md](engine-boundary.md)).

  They call shared engine routines (object flags, text tokens, relations,
  room transfer) and also touch shared memory directly.
- One HLE boundary exists: the object-flag query runs in C++ while the
  original `look` handler runs natively ([engine-boundary.md](engine-boundary.md)).
- Ghidra 12 imports the EXE and all overlays reproducibly through PyGhidra
  ([ghidra.md](ghidra.md)) and exports 1,512 functions (502 resident, 1,010
  overlay).
- The recovery tracker `decomp/functions.tsv` has **1,778 routines**:

  | Kind | Count |
  | --- | ---: |
  | `handler` | 696 |
  | `action` | 191 |
  | `callback` | 44 |
  | `service` | 51 |
  | `unclassified` | 796 |

  - 266 rows have no Ghidra function yet: 75 handlers, 177 actions and 14
    services (verified against the original tracker before §3.2.1).
  - Handler bodies range from 15 to 5,801 bytes (median 161).
  - 20 rows are named; none are ported.
- Regression material: walkthrough replays (`tests/fixtures/*.walk`,
  `gateway-campaign.json`), `tools/trace_game_logic.py` scenarios with
  native/HLE comparison, and the RTLink call census.
- No compiler or copyright strings are present in the executables; the
  compiler and runtime library are unidentified.

## 1. Ground rules (read every session)

These extend `AGENTS.md`; on conflict, `AGENTS.md` wins.

1. **Original assets are read-only and never committed.** That covers:
   - the executables, data files, saves and snapshots;
   - Ghidra projects and pseudocode;
   - framebuffer dumps and audio;
   - game text, including object names and messages.

   Generated material lives under `build/`. Tests that need the game must
   skip cleanly when the data is absent.
2. **Reconstructed game logic is derived work, not GPL.** It goes only into
   `src/story/` (C++) and `decomp/` (tracker and notes), both
   `LicenseRef-Gateway-Derived`. Engine services that reimplement DOS,
   hardware or file formats stay GPL in `src/`. Never paste Ghidra output
   verbatim into any file.
3. **Identity is the original file offset.** Every routine, table and
   global is named by `exe:0x......` or `ovl:0x...... (section N)`. Runtime
   `segment:offset` values are only valid for one launch layout; quote them
   with that caveat or not at all.
4. **Behavior preservation beats readability.** Keep original quirks,
   evaluation order, 16-bit wraparound, signedness, unusual return values and
   bugs. Mark a suspected bug `// ORIGINAL BUG: ...` and keep it.
5. **Ghidra is a reading aid, not the truth.** Real-mode decompilation drops
   far-call arguments, invents functions, and confuses segment registers.
   Check every claim against disassembly and a native trace.
6. **Game state stays in the original memory layout** until §7. C++ code reads
   and writes guest memory through typed accessors (§2.3). This keeps original
   saves working and lets native and C++ routines interleave in any order.
7. **Nothing counts as done without evidence.** `verified` means the C++
   routine passes the per-routine differential test (§3.7) and the golden
   replays (§3.6) with the C++ routine enabled.
8. **Small commits.** One routine, or one tightly coupled cluster (at most
   about 10 routines), per commit. Use a prefix (`story:`, `services:`,
   `tools:`, `ghidra:`, `docs:`). The body names offsets, evidence, and the
   exact commands run.
9. **Time-box.** If a routine has not converged after about two hours, mark it
   `blocked` with notes on what failed, and move on.
10. **Never break the native path.** Every replacement is opt-in per routine
    (§3.8) until verified; the unmodified original must always run.
11. **UI tests and input injection run only on a private Xvfb display.**

## 2. Target architecture

### 2.1 Strategy: the handlers are the script

Gateway's "script" is the set of handlers above. Each one is a small C
function: typically a `switch` on an event or verb that tests flags, prints
text tokens and moves objects through engine services. The plan recovers
that layer in place:

1. **Services first** (§5). Name every engine routine that the script layer
   calls, pin down its arguments, results and side effects, and implement
   it in C++. This is a few dozen routines. It is also the part most likely
   to carry over to the other Legend games, once checked per game.
2. **Handlers next** (§6). Once its callees are named and implemented, a
   handler is a readable mix of control flow and service calls. It is
   rewritten as C++ against a small API that mirrors those services. That
   API is the reconstructed scripting language.
3. **Remove the emulator last** (§7). When no game routine needs Unicorn, the
   guest data segment becomes a native state structure and the port becomes
   a normal C++ program.

This is a functional reconstruction: C++ routines must produce the same
guest RAM state and the same service-call sequence as the original. It is
not a matching decompilation; recompiling a 16-bit RTLink program gains
nothing for the goal.

### 2.2 Equivalence criterion

A C++ routine is equivalent to the original when, from the same entry
state, it produces all of the following:

1. **The same guest memory.** All of conventional memory must match, apart
   from stack bytes below the entry SP. Overlay images and RTLink
   bookkeeping are allowed to differ only where §3.7 says so.
2. **The same return registers and flags,** as far as callers consume them
   (§5.1 records which ones that is, per service).
3. **The same ordered sequence of service calls, with the same arguments.**
   Output that reaches the player must therefore also match: text tokens,
   pictures, music, and sound effects.

### 2.3 C++ shape of reconstructed code

```cpp
// src/story/your_quarters.cpp  (LicenseRef-Gateway-Derived)
// ovl:0x0514b5 (section 38), handler for the current room object.
GW_HANDLER(0x0514b5, your_quarters_events)
Result your_quarters_events(Story& s, Event event)
{
	switch (event.id) {
	case 8: // describe
		if (!s.flag(Object{0x124}, Property{0x0c}))
			s.say(Token{0xf0000c03});
		return Result::fallthrough;
	// ...
	}
}
```

- `Story` wraps guest memory with typed accessors for the original globals
  (`s.global<std::uint16_t>(Ds{0x7904})`). All state access goes through it,
  so §7 can swap the backing store.
- Names such as `flag`, `say` and `Result` are placeholders until §5.1
  fixes the real service semantics. Use numeric object, property and token
  IDs; never copy game text into source.
- `GW_HANDLER(offset, name)` registers the routine for the replacement
  registry (§3.8) under its original offset.
- `src/story_model.*` is earlier research code: fold its verified parts into
  `src/story/` and delete the rest when §6 reaches the Study.

### 2.4 Validation oracles (strongest first)

1. Per-routine differential test from captured entry states (§3.7).
2. Golden replays: walkthrough scripts with RAM checkpoints (§3.6).
3. Native traces: RTLink census, object-write and visibility traces.
4. DOSBox comparison for anything the emulated path cannot show.

## 3. Phase 1 — Infrastructure

### 3.0 [x] Reproducible Ghidra workspace

`nix develop .#ghidra` and `tools/run_ghidra.py` import the original EXE
plus 58 overlay spaces through PyGhidra on Ghidra 12.1.4. The run finishes in
about 1.5 minutes and also writes `ghidra-functions.tsv` (one row per
function, keyed by original file offset; resident bytes are checked against
the EXE). See §11 for the command.

### 3.1 [x] Licensing

REUSE with three layers: GPL with a linking exception, LicenseRef-Gateway-Derived
for `src/story/`, `decomp/` and `src/story_model.*`, and the third-party
licences. `nix flake check` runs `reuse lint`.

### 3.2 [~] Recovery tracker (`decomp/functions.tsv`)

Done: `tools/decomp_status.py` (`seed`, `summary`, `next`, `check`). The
tracker is seeded from:

- the Ghidra function export;
- the handler and callback inventory;
- the action table.

The seed is idempotent and keeps manual columns. `nix flake check` validates
the tracker. Column meanings are in [decomp/README.md](../decomp/README.md).

Steps:

1. **[x] Seed action entries into Ghidra.** `--actions-json` validates all
   195 action-table entries and labels the 191 RTLink-mapped action entries.
   Ghidra established functions for every tracked action, including the 177
   that previously lacked sizes. Exit: the reseeded tracker has no `action`
   row without a size.
2. **[x] Resident actions.** Actions `0x5f`, `0x6d`, `0x98` and `0xaf` share
   resident segment `148e` in the captured native layout. Each entry has a
   unique 8-byte match in the fingerprinted EXE, and the four offsets share
   the same offset base. The 96-byte neighborhoods match except for six far
   `CALL` segment words relocated by `+0x1000`. Ghidra established all four
   function boundaries, and the tracker records them as `kind=action`:

   | Action | Original EXE offset | Ghidra size |
   | --- | ---: | ---: |
   | `0x5f` | `0x006669` | 24 |
   | `0x6d` | `0x00670e` | 20 |
   | `0x98` | `0x006681` | 141 |
   | `0xaf` | `0x006722` | 214 |

   Runtime segment `148e` is snapshot-specific, not a routine identity.
3. **[x] Handlers without a function.** All 75 missing handler-table entries
   were Ghidra boundary failures: with explicit `--decompile-offset` entries,
   Ghidra reported 75/75 successful independent function entries at their
   original OVL offsets (no containing function or data case). Their recovered
   sizes are in the tracker. The `notes` field records *provisional* function
   boundaries; native calling conventions have not been independently verified.
   `mark-handler-boundaries` validates the report before annotating, and
   `run_ghidra.py --tracked-handler-boundaries decomp/functions.tsv` reproduces
   those entry points on subsequent runs. The 14 remaining unsized routines
   are services, outside this step.
4. **[ ] Classify `unclassified` rows** as they are touched; do not mass-label.

### 3.3 [x] Compiler and runtime-library identification

1. Compare the resident startup and helper routines with Microsoft C 5.x/6.x
   and Borland/Turbo C 2.x/3.x runtimes: prologues, far-call helpers, long
   arithmetic, `_chkstk`. Ghidra's Function ID can match only if a matching
   library database exists; otherwise compare by hand.
2. Mark identified runtime routines `kind=library, state=library`, and the
   RTLink manager and startup `kind=rtlink, state=library`, each with the
   evidence in `notes`.

Exit: the compiler family is recorded in this document, with evidence, and
`next` no longer proposes runtime-library routines.

**Finding (2026-11-03): Microsoft C-compatible, 5.x-generation runtime;
precise compiler version remains unproven.** The original `GATE.RTL` contains
an explicitly Microsoft-branded C run-time library copyright marker at file
offset `0x01aa4e` (dated 1988), with Microsoft `R6005`–`R6007` process-spawn
error diagnostics at `0x01a996` onward. These same error codes and meanings
are documented in the original *Microsoft C 5.0 User's Guide*, error-message
appendix ([manual](https://www.bitsavers.org/pdf/microsoft/msdos_c/Microsoft_C_5.0_1987/410840001-500-R04-0887A_Microsoft_C_5.0_Users_Guide_1987.pdf)).
The compiled game functions use 16-bit far calls/returns, BP stack frames and
caller stack cleanup (`ovl:0x00d66b`, `exe:0x00254a`), consistent with the
Microsoft C large-memory-model ABI. The runtime marker establishes the linked
**CRT**, not a unique compiler-version fingerprint for every game object;
Microsoft C 5.1 is plausible, and 6.x cannot yet be excluded. RTLink itself
is a third-party overlay linker and is not diagnostic of a compiler.

Three individually verified resident RTLink routines are now excluded from
`next` by `kind=rtlink, state=library`: the MZ/RTLink startup entry at
`exe:0x000671`, the overlay byte loader at `exe:0x00a23c`, and the section
directory lookup at `exe:0x00a630`. Their disassembly and the loader's live
replay evidence are described in [rtlink.md](rtlink.md) and
[executable.md](executable.md). `exe:0x00065f` is a misleading Ghidra function
starting inside a startup marker; it must not be classified as native C code.
Microsoft CRT implementations reside in `GATE.RTL`, outside this tracker's
`exe`/`ovl` identity domain; none is claimed as a matched individual function
without a library signature. Additional anonymous helpers remain unclassified
until §3.4 supplies call and memory facts.

### 3.4 [x] Measure handler shapes (decides how much to automate)

The Ghidra post-script `ExportGatewayFacts.py` writes, per function:

- callees, with original offsets;
- far calls through trampolines, with section;
- direct memory references through DS or ES, with offsets;
- whether the function has loops;
- its instruction count.

`tools/classify_shapes.py` conservatively fills the tracker `shape`
column:

| Shape | Rule |
| --- | --- |
| `formulaic` | No loops; memory access only through services or reads of constant tables; at most one level of `switch` |
| `direct-memory` | Like formulaic, but reads or writes shared globals directly |
| `complex` | Loops, pointer arithmetic, calls through computed pointers, or more than about 400 instructions |

Record the counts in §9. **Decision point:**

- If `formulaic` covers 70% or more of handlers and actions, build the
  semi-automatic translator (§6.2) before hand-porting at scale.
- Otherwise, hand-port and revisit after 100 routines.

**Result (2026-11-03): hand-port first; revisit after 100 routines.** The
fresh 58-bank Ghidra run exported 1,792 native functions, 143,722 decoded
instructions and 622 RTLink trampoline targets with original section/offset
identities. Across the 891 story handlers and actions (696 + 195), the static
classifier finds **44 formulaic (4.9%), 195 direct-memory (21.9%), and 652
complex (73.2%)**. Shapes are also recorded for the 44 callbacks, 935 entries
in all. Formulaic is far below 70% even when allowing for analyzer errors.

The classification is deliberately an **estimate, not routine verification**:
Ghidra's back-edges indicate possible loops; BX/SI/DI indexing is treated as
pointer arithmetic; any unproven DS/ES absolute reference counts as shared
memory rather than a constant table. One computed jump is allowed as a switch,
but multiple computed jumps, computed calls or over 400 instructions make a
routine complex. Ghidra often omits direct read/write qualifiers, so the fact
export records offsets without pretending to know their access direction.
Far-call trampoline destinations are resolved from validated original
`CALL`/RTLink stub patterns, not the unstable Ghidra segment address. The
generated JSON and all original bytes remain in `build/`.

### 3.5 [x] Engine-service catalogue (`docs/engine-services.md`)

For every routine that handlers, actions or callbacks call, whether
resident or reached through a trampoline:

- **Calling convention:** far or near, which arguments are on the stack and
  which are in registers, who cleans the stack, and which result registers
  or flags callers actually read.
- **Side effects:** the globals it reads and writes, and any guest memory
  it allocates.
- **Meaning:** what it does in game terms, with trace evidence.

Order the work by how many distinct handlers call each service (from
§3.4), highest first. Record the result in the tracker (`kind=service`, a
name, state `analyzed`), and the details in the catalogue.

Exit: every callee of at least 20 handlers is catalogued.

### 3.6 [~] Golden replay suite

1. Verify that the existing walkthrough campaign is deterministic: two
   native runs must produce identical RAM checkpoints. Fix any
   nondeterminism first (host time, uninitialized host memory, audio
   pacing).
2. At each stage of `gateway-campaign.json`, and after every N commands,
   record the SHA-256 of conventional memory, minus the stack and any
   ranges documented as volatile.
3. Add `tools/run_golden.py`, which runs the suite natively or with a chosen
   set of C++ replacements, and reports the first diverging checkpoint and
   command.

Store only hashes and region lists in the repository, never memory images.

Exit: the native suite passes twice in a row, and an intentionally broken
replacement is detected at the right checkpoint.

`tools/run_golden.py` and [the runner guide](golden-replays.md) now implement
two independent native campaign replays, retained-memory SHA-256 normalization,
command-interval and stage-end checkpoints, and first-divergence reporting.
Synthetic tests prove a one-byte unmasked story mutation is detected at its
first affected command. The native two-run baseline and deliberate §3.8
replacement-failure acceptance tests remain **pending on the host**; no
determinism or replacement correctness is inferred from synthetic tests.

The first two complete native campaigns **did not match**: the first difference
was `office/initial_items` after command 2 at RAM `0x55ae6`. Source review
identified host-wall-time initialization of the virtual DOS calendar as one
source of nondeterminism, and newly written save-file metadata as another
potential source. Golden runs now force fixed UTC noon on 1992-01-01 and
normalize guest-visible saved-slot timestamps, while interactive runs retain
their host-local behavior. The independent fixed-clock `office` replay now
matches all nine checkpoints; the full nine-stage retry matches through
`armory_class` and first differs at `bar_nubar/golden-command-000010`, after
the first `wait` following `drink`. The replay also shows differing RTLink
call totals in that stage. The cause is unproven: timed snapshots may land in
different in-flight callbacks/interrupt frames. Golden comparison now offers
CPU register, verified object-record and stage-end original-save diagnostics,
plus an all-checkpoints drift listing, to distinguish transient differences
from persistent game-state divergence without committing original bytes.
The host's detailed replay comparison verified different `CS:IP` and `SS:SP`
but identical object records (734/734) and original bar-stage saved-game
SHA-256. A late-stage seed option now permits two private replays of
`bar_nubar` directly from a verified `armory_class` save, avoiding repeated
startup of the earlier stages while investigating command timing. No volatile
RAM exclusion has yet been justified by these observations.
The first seed-based `bar_nubar` rerun matched 16/16 checkpoints and 40,265
RTLink calls in both runs. This establishes reproducibility for that pair,
not yet for all launches: the earlier full campaign
recorded 40,272 versus 40,259 RTLink calls. The golden runner now retains
and compares native save SHA-256 values at every stage boundary, including
legacy replay metadata. The host verified the two preceding `armory_class`
original saves were *byte-identical*, excluding differing save payloads as
the cause. `gateway-probe` can now record an opt-in RTLink target/caller
timeline for every validated call. The golden runner propagates this trace,
reports the earliest code-call or guest-millisecond divergence and accepts
2–8 independent seeded runs to expose intermittent behavior. The strict
RAM comparison and its exclusions are unchanged. The host ran four independent
seeded bar replays: all 16 checkpoints and all 40,265 chronological RTLink
entries matched in every pair. Added an opt-in common-checkpoint comparison
for the original full-campaign (every 10 commands) and seeded bar (every 5)
recordings. Next: compare their shared snapshots to see whether divergence
is already present after loading the same save or appears only after waiting.

### 3.7 [ ] Per-routine differential tester

Add a mode to `gateway-probe` that, during a replay, stops at the entry of
a selected routine (identified by section and file offset):

1. Snapshot the CPU context and guest memory.
2. Run the original until the matching far or near return. Record the
   memory diff, the return registers, and the service-call trace.
3. Restore the snapshot, then run the C++ replacement through the same
   service layer. Record the same three things.
4. Compare under §2.2, then continue with the original's result.

Also save each entry snapshot under `build/` so that it can be replayed
offline, without running the game up to that point. Report coverage: which
events, verbs, branches and objects were exercised.

Exit: the tester catches a deliberately wrong flag test in a trivial
replacement and passes the correct one.

### 3.8 [ ] Replacement registry in the runtime

- `src/story/registry.*` maps original offsets to C++ routines.
- The runtime intercepts entry, at the trampoline target for overlay code or
  at the resident entry otherwise. It runs the C++ routine, then performs
  the original return: the right `RETF`/`RET n` and the stack adjustment.
- Enable replacements per routine with `--replace ovl:0x0514b5,...` or
  `--replace-verified` (all tracker rows in state `verified`). The default
  is native.

Exit: one handler runs in C++ for a whole golden replay with no divergence.

### 3.9 [ ] Phase-1 exit criteria

All of §3.2–3.8 are done, and at least one service and one handler are
`verified`.

## 4. Operating loop (per routine or cluster)

1. `python3 tools/decomp_status.py next` (or `next --kind service`). Take the
   first proposal unless a dependency forces another order. Prefer
   routines whose callees are all `verified` or `library`.
2. **Read:**
   - the disassembly in Ghidra;
   - the callers and callees;
   - the existing docs for that room, object or verb;
   - `tools/build_handler_evidence.py` output, if the routine was traced.
3. **Name and analyze:**
   - choose a C identifier;
   - set the `shape`;
   - write down the event or verb cases and the services used;
   - set `state=analyzed`.

   Commit this if the analysis alone is useful.
4. **Capture entry states:** run the replays that reach the routine with
   §3.7 snapshot capture. If no replay reaches it, extend a `.walk` script
   (player commands only, no copied walkthrough text) or note the gap in
   `notes`.
5. **Port:**
   - write the C++ in `src/story/<room-or-system>.cpp`;
   - register it in the registry;
   - set `module` and `state=ported`.
6. **Verify:**
   - the differential tester passes on every captured entry state;
   - the golden suite passes with `--replace-verified` plus this routine;
   - set `state=verified` and `test`.
7. **Commit:**
   - run `python3 tools/decomp_status.py check`, `ctest`, and
     `nix flake check`;
   - one commit, with the evidence in the body.

If step 6 fails, fix the C++ first; never "fix" the oracle. If you are stuck
past the time-box, mark the routine `blocked` with notes and move on.

## 5. Phase 2 — Engine services

### 5.1 [ ] Core object model

Object records, property flags (test, set and clear, already labelled
`gw_*_object_flag`), parent/child relations (`gw_native_relation_contains`,
`gw_relocate_child`), and object location and visibility. The existing
object-write and visibility traces are the oracle.

### 5.2 [ ] Text and output

Token lookup (`segment f000`, index `>> 10`, mask `0x3ff`;
[engine-boundary.md](engine-boundary.md)), the formatter, the MORE prompt
and the output window. Text resources stay external: C++ uses token IDs
only.

### 5.3 [ ] Dispatch

- The action dispatcher (action ID 1–195, six-byte records, selector
  callbacks).
- The object/event dispatcher (bank/index split of `handler_index-1`).
- The room callback and exit dispatch (`gw_room_*`).

Replacing these lets C++ handlers be called without trampolines.

### 5.4 [ ] Room transfer, timers and scoring

`gw_room_transfer`, the timer countdown ([timing.md](timing.md)), score and
death handling.

### 5.5 [ ] Save and restore

Match the exact layout in [save-games.md](save-games.md). Original saves must
load in both the native and the C++ paths.

### 5.6 [ ] Parser

Tokenizer, vocabulary (`tools/gateway_vocab.py`), and noun and verb
resolution to action IDs. Do this last in this phase: it is large and it
only needs to match its outputs (action ID and objects) and its global side
effects.

## 6. Phase 3 — Handlers, actions and callbacks

### 6.1 [ ] Waves

Port in story order, so that every wave is covered by golden replays:

1. Opening and Your Quarters (the `look`, drawer and book routines are
   already traced).
2. The office and Dataman stages.
3. The VR, Armory and Bar stages, following the campaign manifest.
4. Everything the campaign does not reach yet. Extend the `.walk` fixtures
   first.

Within a wave, do the shared verb handlers (`kind=action`) before room and
object handlers; actions are called from everywhere.

### 6.2 [ ] Optional semi-automatic translator

Build this only if §3.4 says so. `tools/lift_handler.py` reads
`ExportGatewayFacts` plus the Ghidra p-code of one routine and emits a C++
draft against the §2.3 API. It stops with an error on any construct it
does not understand, rather than guessing.

Drafts are a starting point only: each one still goes through §4 steps 5–6.
A draft must never be committed without passing the differential test.

### 6.3 [ ] Exit

All rows of kind `handler`, `action` and `callback` are `verified`, or are
`dead` with evidence that no game path reaches them. The full campaign
passes with `--replace-verified`.

## 7. Phase 4 — Native state and emulator removal

1. Replace the guest-memory backing of `Story` with a C++ struct that keeps
   the original field layout, so that saves still load (§5.5 tests).
2. Port the remaining resident engine code that the story still uses, as
   needed.
3. Build a configuration without Unicorn, which runs only C++ routines. The
   emulated build stays as the oracle.
4. Exit: the full campaign and save tests pass in both builds.

## 8. Phase 5 — Modern features and other titles

1. **Presentation.** Scaling, aspect-correct and widescreen layout for the
   picture and text panes, high-DPI fonts, modern input and paths.
2. **Quality of life.** Undo, transcripts, and save slots beyond the
   original limits. These must remain opt-in, behind a configuration
   setting.
3. **Other Legend titles.** Reuse the verified services for Spellcasting 301
   and Gateway II only after checking that each service matches per game
   ([spellcasting.md](spellcasting.md)). Every game gets its own tracker and
   its own `src/story/<game>/`.

## 9. Progress log

| Date | Step | Result |
| --- | --- | --- |
| 2026-11-03 | 3.0 | Ghidra 12.1.4 via PyGhidra; 58 sections, 13,947 fixups, 76/76 decompile targets with object and callback labels; 1,512 functions exported (502 resident, 1,010 overlay; 13 outside known blocks skipped) |
| 2026-11-03 | 3.1 | REUSE licensing, 216/216 files |
| 2026-11-03 | 3.2 | Tracker seeded: 1,778 routines (696 handlers, 191 actions, 44 callbacks, 51 services, 796 unclassified), 266 without Ghidra functions, 20 named |
| 2026-11-03 | 3.2.1 | `--actions-json`: 191 validated RTLink entries; Ghidra 267/267 targets, 1,709 functions; tracker 1,803 rows after reseed (25 new unclassified), no action missing a size, 89 still without functions (75 handlers, 14 services); repeat seed 0 added, 0 updated |
| 2026-11-03 | 3.2.2 | Four resident actions byte-verified at EXE offsets `0x006669`, `0x00670e`, `0x006681`, `0x006722`; Ghidra 271/271 targets, 1,712 functions; tracker now 1,807 routines including all 195 actions with sizes; repeat seed 0 added, 0 updated |
| 2026-11-03 | 3.2.3 | Explicitly seeded 75 previously unsized native object handlers spanning 39 OVL sections: Ghidra decompiled 346/346 targets with 75 independent entries, 1,792 exported functions; all 696 handlers now have sizes. Tracker now 1,813 rows, 14 unsized services; boundary evidence marked provisional in notes; repeat seed 0 added, 0 updated. |
| 2026-11-03 | 3.3 | Identified Microsoft C 5.x-era CRT in `GATE.RTL` from 1988 vendor marker and R6005–R6007 codes; compiler family likely Microsoft C large model (exact version unresolved). Classified three confirmed RTLink routines at `exe:0x000671`, `exe:0x00a23c`, `exe:0x00a630` as library; CRT remains external to tracked files. |
| 2026-11-03 | 3.4 | Exported 1,792 function fact records, 143,722 decoded instructions, 622 resolved RTLink stub destinations; classified all 891 handlers/actions: formulaic 44 (4.9%), direct-memory 195 (21.9%), complex 652 (73.2%); 44 callbacks also measured. Decision: hand-port, revisit after 100 routines. |
| 2026-11-03 | 3.5 (ongoing) | Rebuilt the high-fan-out call census from 10,178 original CALL sites, with native snapshot-backed resident EXE/RTL identity recovery and verified RTLink stubs. Found 18 destinations called by at least 20 distinct object handlers (including three in external GATE.RTL); 857 Ghidra call sites remain unresolved. Catalogue triage in `docs/engine-services.md`; native ABI/effects verification remains to be done. |
| 2026-11-03 | 3.5 (ongoing) | Analyzed high-fan-out external `GATE.RTL:0x169f4` as a printf-style runtime text formatter (600 handler callers), and the resident relation predicate and three property-flag services. The `objects` native replay passed per-call comparison on 1,149 flag reads, 62 sets and 3 clears and captured 35 relation entries. Four `exe` tracker rows moved to `analyzed`; no ports or per-routine §3.7 verification claimed. |
| 2026-11-03 | 3.5 (ongoing) | Analyzed the 186-handler section-4 object phrase formatter, original OVL `0xb1e0`, using near caller argument/cleanup, output-buffer and object-property write sites, and captured native entry/epilogue stacks. Marked tracker `gw_format_object_phrase` analyzed; the formatter's less common flag variants remain unclassified. |
| 2026-11-03 | 3.5 (ongoing) | Analyzed the 185-handler resident action-context coordinator at EXE `0x105e`: three-word ABI, nested parser context save/restore, six object event phases, selector and final action dispatch. Native `objects` replay captured eight calls; the native caller checks AX for the special result 2. Left exact event outcomes and nested overflow semantics open. |
| 2026-11-03 | 3.5 (ongoing) | Analyzed the 147-handler section-4 child-list relocation service (three-word far ABI, list unlink/insert plus location/slot/sibling mutations) and 74-handler scheduled-event insertion (`exe:0x3e9c`, five live events matched saved timer records). Corrected three fragmented resident Ghidra boundaries in the seeder; tracker reseed 0 added/0 updated, reduced false native-function rows from 1813 to 1810. |
| 2026-11-03 | 3.5 (ongoing) | Analyzed the 68-handler recursive relation reachability and 31-handler optional-slot relation predicate (`exe:0x2603`/`0x259c`): verified far arguments against real native `objects` calls, AX Boolean semantics and read-only relation access. Captured the original early-false-on-zero behavior and missing cycle guard. Fixed Ghidra's spurious `0x2631` internal-loop function; tracked functions now 1809 with idempotent reseed. |
| 2026-11-03 | 3.5 (ongoing) | Analyzed the 61-handler external RTL character writer at `GATE.RTL:0x169db` (one-word far ABI, in-place argument high-byte zeroing, shared stream writer). Native `objects` replay captured 18 entry stacks at temporary `0x28d12`. Kept it external to the EXE/OVL tracker; six ≥20-caller targets remain. |
| 2026-11-03 | 3.5 (ongoing) | Analyzed the 30-handler original EXE room-exit dispatcher `0x60cf`–`0x6381`: direction-word far ABI, five record-kind branches, shared room-transfer/callback/conditional-effects and handled-versus-moved distinction. A 12-action native office walkthrough captured three dispatch entries. Corrected Ghidra's nine internal switch-arm phantoms, leaving 1800 tracked native functions; five ≥20-caller targets remain. |
| 2026-11-03 | 3.5 (ongoing) | Analyzed the 57-handler score accumulator `exe:0x6557`: a one-word far award added with native 16-bit wrap at `[DS:3946]`, optional formatter and one-time message gate. Native 12-action office replay captured awards 2 and 3; two resident callers confirm stack cleanup. Four ≥20-caller targets remain. |
| 2026-11-03 | 3.5 (ongoing) | Analyzed the 70-handler external RTL bounded string append at `GATE.RTL:0xe284` (far string pointer, 256-byte destination, strict length-sum guard and AX Boolean; 289 native typing entries) and the 53-handler resident story-text presentation coordinator `exe:0x5c30` (two native calls, far string ABI and display-service sequence). Added `--extra-trace-entry` to the reviewed walkthrough runner. Corrected two Ghidra mid-instruction phantom functions at `0x5bfa`/`0x5c42`; tracker now 1798, reseed 0/0. Two ≥20-caller resident targets remain without native-call evidence. |
| 2026-11-03 | 3.5 (ongoing) | Documented the two remaining ≥20-handler service leads, `exe:0x3f85` event-queue membership and `exe:0x5bd1` conditional object text; named the latter from native source disassembly without falsely marking either `analyzed`. The reviewed extended office, imported-save DATAMAN and VR replays (80 scripted actions total) did not capture these entries; targeted scenario coverage is needed before §3.5 can close. |
| 2026-11-03 | 3.5 (ongoing) | Reached `gw_timed_event_exists` (`exe:0x3f85`) in the authentic armory/class walkthrough: 18 native entries with event IDs 0x37/0x38/0x22, caller `add sp,2` and AX Boolean branch, and byte-level confirmation of the four-byte queue lookup. Marked `analyzed`; only 23-handler conditional object text (`exe:0x5bd1`) remains without live coverage. |
| 2026-11-03 | 3.5 (ongoing) | The authentic 49-action bar/casino stage produced the expected `FULLCASINO` saved game, extending the reviewed campaign to 159 actions, but still did not invoke `exe:0x5bd1`. Static original section-4 caller code at `ovl:0x051aea` confirms its `(object,mode)` two-word far-call ABI and caller stack cleanup, not its runtime effects. Added repeatable native service-entry forwarding to the campaign runner with a mocked saved-game-handoff test. Further injected replays are paused in this environment: private Xvfb cannot start because Unix socket creation is denied (`EPERM`); §3.5 stays open with the service only `named`. |
| 2026-11-03 | 3.5 | Completed the eighteen ≥20-handler service catalogue. A private-Xvfb native three-command replay reached `exe:0x5bd1` twice for `(object=0x124,slot=0)`; its section-4 child-count (`ovl:0xbf3d`) and list-emission (`ovl:0xb910`) callees returned AX=1 on all six traced calls. Qualified RTLink return snapshots confirmed zero count-helper writes and one initial object property-7 write by the list formatter, with no changes on repeated examinations. Resolved both trampolines using third EXE base `0xa73a`; renamed the service `gw_present_child_list` and marked it analyzed, not ported or §3.7-verified. Next: §3.6 deterministic golden replays. |
| 2026-11-03 | 3.6 (ongoing) | Added independently isolated native golden campaign runner with per-ten-command and stage-end checkpoints; SHA-256 of 640 KiB conventional RAM excludes only documented BIOS tick counter and live SS:SP stack tail, with exclusion bounds included in hash manifests. Synthetic tests confirm matching runs ignore transient stack/clock bytes, detect the first one-byte story mutation at its exact command, and reject stale scripts. Awaiting two native host runs and deliberately bad opt-in replacement before marking the phase complete. |
| 2026-11-03 | 3.6 (ongoing) | Host ran both nine-stage campaigns successfully but memory hashes diverged at `office/initial_items` after command 2, first unmasked byte `0x55ae6`; some later RTLink call counts also differed. Audited the runtime and found that virtual DOS INT 21h date/time readings came from `system_clock::now()`, so independent launches were fed different clock seeds. Added an explicit deterministic UTC clock for golden replays (1992-01-01 noon), retaining host time for interactive play, and augmented first-divergence diagnostics with changed-page distribution. Host retest still pending. |
| 2026-11-03 | 3.6 (ongoing) | Full fixed-clock campaign matches through `armory_class` but first RAM difference is `bar_nubar/golden-command-000010`; all 734 object records and the bar stage-end original saves match despite different transient CPU state. Independent bar-only seed replay matched 16/16 checkpoints and 40,265 RTLink calls in both runs, whereas the full run saw 40,272/40,259. Host SHA-256 proved its two preceding `GATE004.SAV` files byte-identical. Added optional per-call RTLink target/caller/guest-time sequence traces and 2–8 independently seeded golden runs to locate first intermittent execution divergence; native trace results still pending. |
| 2026-11-03 | 3.6 (ongoing) | Host completed four independent bar runs seeded from one verified `GATE004.SAV`: all 16 RAM checkpoints and every one of 40,265 ordered RTLink code-call/timestamp records matched in all four. This rules out a repeatable difference in isolated bar execution, but not the earlier discrepancy between full and seeded campaigns. Added `compare --common-checkpoints` to compare existing full-run and isolated-run recordings across different checkpoint frequencies without replaying DOS; no guest data or masks added. |
| 2026-11-03 | 3.6 (ongoing) | Cross-compared old full bar runs with the four-run seeded reference: all bar stage-end save bytes and 734 object records match but timed CPU/stack/RAM checkpoints differ. All target counts match except 6–13 extra/missing calls to section-0 `ovl:0x0d7e`. The seeded trace places all 38,212 calls at RTL return `27cd:06b0`, mapped via two verified RTL address anchors to `GATE.RTL:0x16049`; original instructions show `call far; or ax,ax; jne` retrying at RTL `0x16044`. Added `--trace-rtlink-after-ms` to sample selected entry/return events near the divergent command without exhausting the 256-event cap early. Precise wait condition and remaining RAM differences are unresolved; strict golden mask unchanged. |
| 2026-11-03 | 3.6 (ongoing) | Host sampled section-0 `ovl:0x0d7e` beginning at 53,500 guest ms: 256/256 matched returns, all `AX=1`, zero reported object-record deltas, verified bar-stage save. Original OVL disassembly confirms the helper returns 1 iff two underlying helper results are both zero, explaining the observed repeated `GATE.RTL:0x16044` retry loop as a no-event condition. Added summary formatting for capped return-value samples. The identity of the underlying events/non-object effects and any eventual AX=0 transition remain unverified; RAM mask unchanged. |
| 2026-11-03 | 3.6 (ongoing) | Host completed the terminal RTLink sample starting at 143,995 guest ms: 59/59 matched returns of section-0 `ovl:0x0d7e`, 58 `AX=1` followed by the final `AX=0` at 144,000 ms, with no reported object-record deltas. Thus the external RTL retry loop exits normally. Added opt-in `compare --objects-only` to check all 734 native object record bytes and original save hashes at every previously captured checkpoint, fail closed on missing/ambiguous native tables, and report the first object change separately from the unchanged strict CPU/stack/RAM oracle. Host full-campaign object-only run pending; no baseline accepted. |
| 2026-11-03 | 3.6 (ongoing) | The host's first `--objects-only` full-campaign comparison matched 40/40 object snapshots and original saves through `bar_nubar` (9 office, 6 dataman, 6 VR, 7 armory, 12 bar). Independent full-versus-seeded bar comparison matched all 12 common object snapshots and the save. The full run then stopped because a later CPU snapshot exposed no validated 734-object table at DS/ES. Added an opt-in fallback that scans unmodified conventional RAM for the original object-count indirection (`[DS:8d54]` points to segment containing count at `:38dc`), decodes all 734 records, and rejects missing or ambiguous matches. Full host retry pending; no relaxation to the strict RAM oracle. |
| 2026-11-03 | 3.6 (ongoing) | Host full-campaign object-only retry matched 59/59 native object snapshots across all nine stages; original stage saves match through `fork` but differ starting at `terri` and remain different in `morning` and `mission_one`. These are not object-record differences at the sampled checkpoints but are unresolved persistent-byte differences. Extended the original save-format inspector with `--compare` and fed its classification into the golden comparator, reporting object block versus global-state/timer deltas without disclosing original records or changing RAM masks. Host save-diff classification pending, so no full baseline accepted. |
| 2026-11-03 | 3.6 (ongoing) | Host save-diff classification and 24-byte hex dumps isolated all six persistent-byte differences in Terri, morning and mission-one saves to an ASCII `NN-NNNN` field at save global `+0x282`: a plausible dynamically generated Terri phone number. All 734 serialized object records and scheduled events remain identical. Added read-only `locate_save_ascii.py` to compare verified native RAM checkpoints and locate same-address copies for an existing byte-write watchpoint; generation code and RNG source still require native tracing. No RAM mask changed. |
| 2026-11-03 | 3.6 (ongoing) | A native locator attempt revealed a legacy golden-manifest format with no per-stage `save_sha256`: the diagnostic initially called the stricter new `verified_recorded_save` and failed before scanning RAM. It now verifies saves via their stage metadata and snapshots via their golden hashes, additionally checking a manifest save hash when present. Missing manifest hashes produce an explicit provenance notice; no game-state mask or original capture is changed. Host locator retry pending. |

2026-11-04 continuation (§3.6 remains in progress): two complete local
campaigns finished in `build/golden/full-recheck-v020/`. Strict comparison
first failed at bar command 20 (CPU/stack and 85 retained bytes). The separate
object/save diagnostic passed all nine stages, 59 checkpoints and every
stage save. No baseline or new RAM exclusion was introduced. Preserved the
handoff's traced telephone RNG and animation scheduler findings in
[save-games.md](save-games.md), separating reported host evidence from this
session's validation. Tracker check, all 63 CTests and `nix flake check`
passed. Next: recover stable file identities for the RNG/formatting routines
and capture their ABI before any replacement; strict determinism remains open.

2026-11-04 follow-up: mapped the RNG to external `GATE.RTL:0x0091ea`
and telephone call sites to section 47 `ovl:0x064209`/`0x064216`.
All 7,056 section bytes match a native snapshot after the recorded relocation
fixups. Independently recomputed both RNG advances from bracketing snapshots;
final state, formatted fields and native formatter arguments agree. Recorded
ABI, evidence and unresolved enclosing handler boundary in
[decomp/telephone-rng.md](../decomp/telephone-rng.md). No tracker function or
replacement claimed at these internal instruction sites.

2026-11-04 debugger follow-up (§3.6 remains in progress): disassembled the
original Gateway 1 *runtime* rectangle-picker path at `2649:0541` in the
opening RAM snapshot and identified its per-rectangle `0x80` skip bit.
Correlated all 30 `GATE_002.RGN` first-room rectangles with the live picker
table in four existing native checkpoints: drawer indices 54 and 55 alternate
as it opens and closes. Added a conservative, full-table-matched debugger
annotation with real-snapshot and synthetic tests; retained the raw RGN mode.
See [debugger.md](debugger.md). This does not establish general picture
visibility semantics or complete §3.6 deterministic golden verification.

2026-11-04 further picker analysis (phase 3.6 remains in progress): recovered
the stable source identity of the native picker (GATE.RTL:0x14186), four
active-region toggling helpers (GATE.RTL:0x14d0f–0x14dc5),
and the common conditional-region rule evaluator
(ovl:0x2680, section 0). The three ten-byte rules at
ovl:0x53daa (section 38) link drawer open-state callbacks with the two
alternating rectangles and a debit-card/desk relation gate. Cross-checked
all three records against four earlier native RAM checkpoints. Added an
original-file, section-qualified read-only inspector with synthetic tests
and documented [Your Quarters](room-your-quarters.md#native-conditional-mouse-region-rules).
No story service marked verified and no graphics visibility claim made.

2026-11-04 conditional-region census: found both compiler calling forms to
the same section-0 evaluator, including 17 calls that pass one static resident
DS record. On the pinned original EXE, initialized DS offsets map to
`exe:0x1ba93 + DS_offset`; 16 records match native snapshots byte-for-byte,
and one matches after the original loader's segment relocation. Extended the
read-only source inspector and its synthetic/optional original-file tests to
account for all 75 direct calls: 49 distinct overlay tables/416 records and
17 resident records, 433 total. See
[the room-condition census](room-your-quarters.md#coverage-of-native-region-condition-callers).
These are candidate interaction predicates, not established paint predicates.

2026-11-04 picture-layer analysis (§3.6 still in progress): found the separate
14-byte native picture predicate at `ovl:0x272c` and scene-layer state update
at `ovl:0x2827`, invoked by RTLink `1c24:425b`. Recovered all 81 direct
calls: 49 distinct overlay picture tables/223 records and 21 resident EXE
records, 244 unique source records. All 21 resident records match the retained
guest RAM after accounting for two relocated callback segments. Your Quarters
layer 0 selects `GATE_102.PIC` frame 16 with drawer-open flag 0x0c; the native
DS:adf8 cache alternates 0/1/1/0 and its 48×65 XOR area changes by 2,751
pixels on each open/close transition, independently cross-checked against
original framebuffers. Added read-only inspector, synthetic and optional
original-fixture tests, and [picture-condition documentation](picture-conditions.md).
General native predicate replacement and SDL3 scene rendering remain open.

2026-11-04 native scene compositor: added a host-only C++20 PIC scene layer
compositor (`src/scene_compositor.cpp`) and `gateway-scene` PNG diagnostic.
Without native story predicate substitution, manually selected XOR entries
18+17 (closed) and 18+16 (open) over base PIC entry 15 reproduce all 84,896
room RGB pixels in each original guest screenshot, using screen x=261 as the
verified room origin. The third layer's live `DS:adf8` cache byte does not
by itself account for the indicator's 50-pixel blink: record that limitation
before using the cache as an automatic renderer input. Synthetic C++ and
optional original-PIC/framebuffer checks provide a native presentation
baseline. This does not verify a story routine or wire the compositor into
SDL3 yet; see [picture conditions](picture-conditions.md#first-native-picture-compositor-and-exact-frame-validation).

2026-11-04 picture-layer inference follow-up (§3.6 still open): added a
read-only RGB framebuffer comparator and bounded optional-layer enumeration
to `gateway-scene`. Exactly one XOR-layer combination reproduces all 84,896
room pixels in each of four native snapshots: before=17+18, open=16+18,
taken=16+17+18, closed=17+18. The indicator's original callback
`ovl:0x051480` tests `DS:69fe==1`, confirmed in all four RAM snapshots,
while `DS:adf8[2]` remains zero even when its pixels are visible. Thus
eligibility and cached picture state do not establish the visible blink
phase. Found a distinct native picture handle path at
`ovl:0x0514fa`–`0x05156a`: room event 23 creates a picture `0x0211`
animation handle via far call `1485:1788`, saves `AX` in `DS:6a20`
(observed value 0 in all four native snapshots), and calls `1485:18da`;
the disabled branch invokes two cleanup calls and resets the handle to
`0xffff`. Stable source identities of the graphics services and cadence
were initially unresolved;
see [picture-layer evidence](picture-conditions.md#read-only-visible-layer-inference-and-indicator-callback).

2026-11-04 RTL picture animation follow-up (§3.6): resolved the four
linked room calls using original GATE.RTL and retained guest RAM to
`rtl:0x1328d` (create), `0x133df` (initialize), `0x1342d` (clear), and
`0x136b1` (global free/reset). All share the original-file displacement
`+0x11b05` from linked entry offsets; each has 16 matching instruction
bytes supported by the four entrypoints. Disassembly establishes a
five-sequence limit, up to 20 frames of 0x88 bytes each, frame direction,
frame index, 32-bit deadlines, and scheduler at `rtl:0x134a5` with
mode-dependent looping/termination. Corrected the prior assumption that
the last service removed one animation handle: it actually frees *all*
resident animation data and resets its current `DS:[0x12]`. Added the
read-only source-mapping inspector and synthetic plus optional-original
tests. Animation timer units, RTL DS register provenance, draw/update
interactions, and native implementation remain open.

2026-11-04 live RTL animation timing (§3.6): a new successful headless
opening replay (7 actions, 76 keys, 39,000 guest ms) traced the create
service at physical `0x25fd8` (9,077 ms), initializer `0x2612a`, and
scheduler `0x261f0`, establishing `DS=0x5b46` at runtime. The original RTL
data operands are linker-relocated: original count `DS:0012` becomes
`DS:1428`, but the frame slots, directions, indexes and deadlines have a
*different* base (`DS:9fb4`, `9fe6`, `9ff4`, `9ffa`). All four retained
room RAM checkpoints show one one-frame sequence, frame index zero, and
advancing deadlines. Its immutable 0x88-byte frame record at `81d2:0000`
has duration byte 10, scaled by 50 in RTL. The narrow live deadline watch
logged 59 scheduler writes, 52 of 58 successive deadline increments exactly
500, typically ~500 guest ms apart. Added read-only relocation/state decoder
and write-trace summary with synthetic and optional-original tests. Eight
additional idle original framebuffer captures at 250-ms intervals establish
the *same 50 pixels* toggling every 500 guest ms; the bounded XOR compositor
has exactly one zero-difference match for each frame (17+18 when visible,
18 alone when absent). Frames repeat after 1,000 ms, independently verifying
the single-frame indicator's full blink cycle. Other room-action interactions
and the native replacement remain unverified.

2026-11-04 RTL action/animation interleaving (§3.6): completed another
39,000-ms headless opening replay (7 actions, 76 BIOS keys, 32 additional
frame/RAM pairs). The original PIC compositor matched each of the 32 scenes
uniquely with zero pixel differences: drawer layer 16 changed on close,
indicator layer 17 kept blinking, card layer 18 remained. The active RTL
sequence count and handle stayed fixed while the room picture-cache byte
changed from one to zero. Original disassembly shows the scheduler at
`rtl:0x1356d` calling its local frame renderer at `rtl:0x12ed7`; live
relocations establish its three far calls to video-state save, frame draw
(`2a1c:000c`), and video-state restore. Added read-only linked-call mapping
and synthetic/optional-original tests. Renderer mode-3 details, behavior on
room exit, and a host-side animation port remain open.

2026-11-04 RTL mode-3 rasterizer source identity (§3.6): resolved all three
frame-renderer far calls back to `rtl:0x11078` (state save),
`rtl:0x184e8` (frame draw), and `rtl:0x10f64` (state restore), using the
linked runtime targets and unique 16-byte original source matches. The
original frame draw's two backends both implement mode 3 as XOR: the packed
branch uses CPU XOR, while the VGA branch programs the VGA Graphics
Controller's XOR function through register 3 (`0x18`). An independent
13,000-ms native branch trace reached `rtl:0x187da` (VGA) 17 times and
`rtl:0x18722` (packed) zero times, including the first room's 500-ms
animation entries. Read-only inspector and regression tests now validate
the mapping, opcode evidence and branch counts. Host-side replacement
remains unimplemented and no per-routine differential verification claimed.

2026-11-04 host-only animation prototype (§3.6): added `XorPicAnimation`,
which composes a positioned XOR overlay at explicit guest-clock deadlines,
skips multiple transitions by parity, preserves its visible phase across
background changes and allows resynchronization. The opt-in `gateway-scene`
animation CLI matches all eight original idle first-room screenshots, each
with 0/84,896 differing scene pixels; synthetic tests cover reversible XOR,
skipped frames, deadline validation and phase maintenance. The guest
scheduler, action-dependent timing and SDL3 runtime dispatch are not
replaced, and the routine is **not** marked verified.

2026-11-04 exact-match SDL scene bridge (§3.6): an opt-in
`--host-scene-preview` presentation path now builds all eight original
first-room PIC layer combinations in C++, checks each entire VGA room area
before replacing its display pixels, and falls back unchanged when no
unique match exists. The guest still runs every VGA and animation routine;
no scheduler substitution or replay equivalence is claimed. Synthetic
tests validate mismatch handling and all masks, and a private 44-frame
differential test covers four room states, eight idle blinks and 32
interleaved player actions. Host-only compilation and tests pass; the
interactive SDL/Unicorn binary still requires a full toolchain check.

2026-11-04 resident RTL deadline phase (§3.6): correlated slot-zero's
linker-relocated 32-bit next-frame deadline with 40 private idle/action
frame/RAM pairs. The new host-only `RtlXorDeadlinePhase` predicts 7/7
idle transitions and 28/28 action transitions where the deadline advances
by exact multiples of 500 ms. Three action gaps (900/2010/2620 ms) demand
re-anchoring, not speculative parity. Added the independently driven native
PIC-mask paint API; the regression clears and repaints the full room for
all 44 original captures and confirms 35 confidently deadline-predicted
frames. The live runtime continues to use only the original-image-matched
fallback; this is not a verified RTL replacement or a new golden baseline.

2026-11-04 complete native picker overlay (§3.6): the original mouse picker
maintains five groups, not just the 31-row room-picture group: counts in the
four retained first-room RAM states are 15/31/1/5/5. In addition to 30 room
RGN rectangles, 22 UI control rectangles (18 unskipped) cover command/menu
and side scroll areas. The F10 overlay now exposes validated non-RGN picker
controls, aligns picture rectangles to native X origin 261, and prioritizes
the proven native skip bit over provisional object-location classification.
Synthetic and optional-original snapshot tests cover groups and bounds;
guest picking and game logic remain unchanged.

2026-11-04 picker fallback and priority correction (§3.6): disassembly at
`GATE.RTL:0x14186..0x14244` establishes that every group is scanned from
slot zero and that each successful rectangle overwrites the previously
selected group, slot, and result kind. All five group-first rectangles are
selectable catch-alls, with signed 16-bit coordinate comparisons. The F10
overlay now includes these five large background targets, labels the true
last-match winner and exports their slot/flags in `.picker.tsv`. The host
model is tested against the four private first-room guest-RAM captures;
the guest's original mouse routine and inputs are unchanged. Added an opt-in
Unicorn differential harness that calls the actual picker against the same
RAM and checks its group/slot/kind outputs for a grid and boundary samples;
it requires the user's native Unicorn/Nix toolchain to execute.

2026-11-04 live picker differential tracing (§3.6): added opt-in probe hooks
at the known Gateway 1 picker entry and `retf` instruction. On real mouse
invocations, the hooks capture the original x/y arguments and three far
output pointers, independently predict group/slot/kind from the *current*
native table and PIC/RGN scene model, then compare after the original routine
has written its outputs. A TSV records matches, disagreements, and calls for
which the picker table could not be decoded. `check_picker_trace.py` rejects
incomplete captures or discrepancies, with synthetic regression coverage.
Source compiles against the available Unicorn headers; live original-game
replay is still unverified on this host because its bundled Unicorn library
requires a newer glibc. The hooks are observational, and do not establish
whether a later UI handler consumes any selected click.

2026-11-05 native picker without RGN match (§3.6): removed the debugger's
dependency on successfully correlating the current PIC/RGN frame before
displaying *any* native mouse picker rows. A structurally valid table now
exposes its original group/slot rectangles even when no picture has been
tracked or the room geometry has changed. An unmatched table has no mapped
object flags, so F10 labels its native regions `unmapped`, while the RGN
display remains heuristic. Corrected an optional-word validation ordering
issue in guest table parsing. Synthetic missing-picture, mismatched-geometry
and invalid-table checks plus private first-room RAM checks pass. The live
picker trace now compares any structurally decoded table even without an RGN
association, while still never guessing object identities. Native mouse
selection itself is unchanged; another host must run the live original-game
oracle to verify previously uncorrelated screens.

2026-11-05 winner-based native hitbox overlay (§3.6): the normal F10 view now
rasterizes all enabled native mouse rectangles in source scan order, with each
later row overwriting the previous pixel owner. This shows only the winning
group/slot at each coordinate, respecting signed bounds, inclusive edges,
disabled entries, fallback backgrounds and overlaps. Matched picture slots
retain object colors/names, but other slots remain anonymous. The old raw RGN
and native rectangle outlines are preserved behind the R debug toggle for
comparison. Picker dumps now export all native rows and the optional room-group
association so the overlay can be audited without inferring unknown object IDs.
Synthetic overlap/disabled/priority raster tests and the private
first-room RAM checks pass; a live Unicorn graphical run remains pending.

2026-11-05 native picker return-value debugger (§3.6): opt-in
`--debug-native-picker` installs the existing observational Gateway 1 picker
entry/return hooks even without a TSV trace. The F10/H overlay now keeps the
last original call's coordinates/time, actual AX and three far output words,
independent model result, and match/mismatch/unmodeled verdict visibly
separate from the current hover winner-map. Recent calls have a short-lived
colored position marker; errors are shown instead of a stale success.
Synthetic return comparison and raster tests pass with the host C++ compiler;
the original guest dispatch and click acceptance remain unverified, and
full SDL/Unicorn execution requires the original data and toolchain.

2026-11-05 native picker source provenance (§3.6): the observational hook now
freezes the native rectangle table at picker entry and resolves the *actual*
returned group/slot/kind at picker exit, independently of the model prediction.
The debugger and extended TSV identify group fallbacks, aligned room objects,
other controls, unaligned rows and unresolved returns. Counting all enabled
rectangles at the call's coordinates also exposes overlap/priority cases.
The checker accepts prior twelve-column traces and validates the extended
provenance fields; `report_picker_trace.py` summarizes source counts, overlap
hotspots and mismatches without suppressing disagreements. Synthetic C++ and
Python tests pass; live evidence across multiple rooms and later UI dispatch
are still pending.

2026-11-05 bounded post-picker continuation tracing (§3.6): optional
`--trace-picker-follow FILE` records the original far-return destination from
the real stack and samples 1–1024 subsequent executed guest instructions with
CS:IP, opcode bytes, registers and flags. An independently tested gate waits
for the exact continuation (allowing intervening interrupts), limits the
observation window, and explicitly records missing, interrupted or unfinished
captures. The offline comparator locates the first differing executed address
or instruction bytes across two runs from comparable saved states. This is
observational debugging: no downstream action-acceptance ABI has yet been
proven, and a live original-game continuation trace is still required.

2026-11-05 post-picker side-effect observation (§3.6): the opt-in bounded
continuation tracer now associates Unicorn guest CPU writes (address, width,
value) with each recorded original instruction, including the final sampled
instruction. The comparator validates write counts, detects the first differing
store even when executed code matches, and marks pre-extension traces as
unavailable for side-effect comparison. Synthetic state-machine and parser
regressions pass. CPU stores may be stack/temporary writes; host HLE memory
changes are not captured, and click dispatch/acceptance remains unproven.
Original-game replay with two equivalent saved states is still pending.

2026-11-05 bounded post-picker dispatcher candidates (§3.6): after a confirmed
native picker return, retain a short guest-time association window (default
2 seconds; configurable 1–20 seconds) and hook two resident dispatch sites:
the action-context coordinator (`exe:0x00105e`) and validated indirect action
table CALL (`exe:0x0059ae`). Record action/event ID, context object IDs and/or
the indirect far target, including the original call's age. Both are labeled
*temporal candidates*, not actual accepted mouse actions. A new picker ends the
previous window; mismatched signatures are ignored rather than misattributed.
The F10 native-result panel displays the last candidate; the offline comparator
reports the first candidate-dispatch difference while ignoring timing-only
variation. Synthetic association, parsing and raster tests pass; native mouse
replay is still required to show whether either dispatcher consumes these picks.

2026-11-05 guest mouse-read correlation (§3.6): the HLE DOS mouse interrupt
observer now retains the last 64 genuine guest reads (`INT 33h` AX=3/5/6)
when picker diagnostics are enabled. It records returned buttons, position,
press/release count (before clearing), and resume CS:IP; a bounded lookback
attaches only the most recent read to each completed native picker, with
explicit `none` and coordinate disagreement cases. F10 displays the observed
guest read below the previously added dispatcher candidate; the comparator
validates input markers and reports differing mouse-service results. These
are observations of HLE service delivery, not proof the picker used that
read; live original-game mouse replay is still required.

2026-11-05 per-click native evidence report (§3.6): the original picker
continuation marker now also records the verified native room object ID,
enabled overlap count and host prediction verdict. Added
`tools/report_picker_follow.py` to summarize each actual native return
alongside recent guest mouse reads, bounded instructions/stores and
time-correlated resident context/action-table candidates. For room selections,
it distinguishes dispatch context arguments that reference the same object
from those referencing unrelated objects, without claiming click acceptance.
Legacy trace markers without the new provenance fields remain readable;
synthetic source filtering, grouping, validation and CLI tests pass. The
next step needs original-game clicks from contrasting saved states.

2026-11-05 pinned original-asset audit (§3.6): a supplied archive restored
the exact seven-file Gateway 1 data/executable revision (SHA-256 verified)
outside the repository. With standalone host-built parsing tools, all 40
original PIC files produced 1,504 images without decode failures. The
58-section RTLink manifest accounts for all 13,947 fixups. Independent
static scans of the original EXE/OVL validate all 75 conditional RGN calls
(433 unique source records) and all 81 conditional picture-layer calls
(244 unique records). Added `gateway-original-assets` as an optional CTest
oracle that repeats these checks and skips cleanly without private files;
generated pictures and overlay data remain temporary. No guest picker
execution or accepted-click evidence is inferred: this host lacks the
Unicorn/SDL3 runtime and captured RAM states.

The original PIC/RGN parser audit (`gateway-original-regions`) independently
exercises the scene tracker's ingestion on 22 real picture/region pairs:
773 frames, 2,521 rectangles, 160 with object ID zero. Four out-of-picture
anonymous rectangles appear in the first frame of `GATE_015.PIC` and
`GATE_115.PIC`, outside the 380-pixel local picture width. The standard H
view continues clipping to the scene, grouping adjacent rectangles by
object ID, and excluding GUI groups; raw R can show the unfiltered rows.
These geometry observations are not native picker return traces.

The next picker instrumentation step now records an entry-time copy of every
native picker row alongside each opt-in continuation trace (`# scene`,
`# picker-row`). The independent `report_picker_scene.py` compares those
frozen condition flags and original returned group/slot to the H overlay's
scene-only raster ownership at the click position. It reports cases where a
native room selection has no scene highlight, or a GUI selection overrides a
highlighted room object; it preserves older traces as geometry-unavailable.
Synthetic C++ serialization and Python analysis regressions pass. Actual
original-code picker returns remain pending on a host with Unicorn/SDL3.

Conditional-picker follow-up: `compare_picker_scene.py` now contrasts two
frozen tables for the same PIC frame, rejects changes in geometry or unknown
RGN association, and measures native winner pixels independently of the H
overlay's merged object pixels. It lists actual differing screen coordinates
for separate same-save click trials, with JSON output for an automation
driver. Synthetic before/after drawer-like skip-bit swaps, GUI overlap,
anonymous shape ownership, and same-object merge tests pass; no unrecorded
native game result is claimed.

The picker instrumentation now offers opt-in bounded entry-time RAM/register
captures (`--picker-capture-dir`, up to 128 one-MiB snapshots) and an offline
`native-picker-oracle-tests GATEWAY-DOS --captured DIR` mode. Captures retain
native picker entry registers, original RAM, click coordinates, PIC frame and
return address, with an optional guest-time start filter. Each successful
return now retains original output registers and frozen full picker row
provenance as a separate `.result`, so later batch reports can compare
conditional branches without reading game state after a delay. The oracle
uses the captured RAM to check a grid, row boundaries
and the actual click against original picker execution. It does not treat GUI
rows as scene objects, and never bundles game-owned memory. Capture-format
tests and syntax checks pass; native Unicorn replay still requires the host.

The offline capture batch comparator now validates complete original-picker
entry/return pairs and discovers transitions between unique skip masks only
within identical, fully aligned PIC frames and native rectangle geometries.
It reports native winner changes and merged-by-object scene highlight changes
separately, reusing the existing independent raster oracle. Synthetic
repeated-state, GUI overlap, missing return/RAM and provenance-corruption
cases pass. Native capture and actual click acceptance remain to be verified.
The comparator can also emit a per-capture oracle point list for every changed
screen-pixel signature in its compared transitions; the native Unicorn oracle
accepts that list to replay the exact candidate pixels against both entry
states, beyond its fixed grid and rectangle-edge tests. This still checks
picker selection, not action-dispatch acceptance.

Controlled click trials now have a standalone walkthrough driver. It runs
two independent no-click controls and one isolated injected click per selected
pixel, with deterministic DOS date/time and separate writable game state.
Its strict comparison requires identical pre-click normalized RAM and CPU
registers, plus a stable pair of no-click post states, before it reports
potential click-correlated RAM and object-record differences. It also retains
the original picker returns for each trial and labels missing provenance.
Synthetic baseline drift, input injection, original object-parser failure,
and output-isolation checks pass; actual guest execution remains pending.

The controlled trial workflow now persists an input-identity manifest before
the first native run, hashing the script, native probe, original game-resource
set, optional seed save, planned BIOS schedule and isolated click coordinates.
`--compare-existing` rechecks each recorded run against that plan using the
existing native walkthrough trace verifier, then rederives RAM/CPU/object
differences without executing the guest or overwriting a divergent prior
result. Synthetic tests cover recovery, modified scripts/resources, unrelated
game-directory saves, corrupted manifests, missing snapshots and altered
reports. The original-game trials still require a host with Unicorn/Xvfb;
their downstream action-dispatch semantics remain unproven.

Per-trial reports also include a strict original RTLink target-count
comparison against the no-click control; the two controls must agree on the
entire section-qualified census before click-associated evidence is qualified.
The report lists changed target counts and call-manager totals, while clearly
marking them as *whole-run* differences rather than assigning any untraced
call to the injected click. Synthetic tests distinguish matching CPU/RAM with
divergent native handler activity. Host guest execution remains the next step.

The trial comparison now also joins each original picker selection to the
existing validated continuation evidence: guest mouse-read source and
coordinate agreement, bounded CPU stores, near-time action/context event
records, and whether the selected room object appears in those context
objects. The exact injected coordinate is matched separately from any other
picker calls within the observation window. Synthetic tests cover positive
object references, malformed continuation summaries and legacy traces with
no continuation metadata. These are branch-investigation leads only, not
evidence that a click was accepted by a downstream game handler.

An independent read-only matcher now cross-checks each completed trial's
actual entry-frozen picker scene against one validated private reference
entry capture. It requires matching picture frame and *entire* rectangle
geometry before comparing the per-row dynamic `0x80` disabled flags, and
reports room and GUI/fallback mask differences separately. Trial picker calls
at other coordinates or outside the injection window are excluded. Synthetic
reference capture and trial fixtures cover matched masks, changed room and
GUI conditions, missing geometry and inconsistent experiment provenance.
This closes a possible false attribution when test coordinates are reused in
the wrong story condition; no native acceptance or scene visibility is claimed.

A read-only cross-study RTLink target ranker now aggregates only controlled
trials that have qualified RAM/CPU/handler baselines. The section-qualified
target addresses are mapped through the validated Ghidra overlay plan to
original OVL file offsets, and matched to *exact* tracked function entries
where possible. Synthetic same-runtime-address/different-section fixtures
ensure overlay code identities remain distinct; missing censuses are
explicitly marked as incomplete. Rankings combine whole-run target count
deltas with room-picker, changed-RAM and near-time context observations, but
do not claim any of these as causal evidence for native action acceptance.

On 2026-11-05 the cross-study RTLink ranker gained lossless recovery of target
deltas omitted from the 32-entry `comparison.json` preview. When the paired
original `native.rtlink-calls` sidecars exist, it recomputes and verifies
the **entire** frozen summary, and only then ranks all target deltas; a lone
sidecar is an error. The new 35-target synthetic fixture checks that the
previously omitted three targets are visible and that edited native evidence
is rejected. In a restricted container the supplied Unicorn 2.1.4 ZIP's
static library successfully built and ran the headless probe with GCC 13 and
`-latomic`; the probe's 79 synthetic integration tests passed. The shared
library required a newer glibc; apt's Ubuntu snapshot mirror returned 500/502,
so Xvfb/SDL3 live click experiments remain unrun in that container. The exact
fallback build and limitations are recorded in
[workspace-setup.md](workspace-setup.md). No native click-acceptance result
or verified new C++ story routine is claimed.

The next read-only conditional-hitbox analysis combines captured picker
state matching with the RTLink ranker. The matcher now records SHA-256
digests of the exact native study plan and comparison JSON, and the optional
ranker `--state-check STUDY=REPORT` requires one valid report per study.
Only baseline-qualified trials whose *every* original call at the injected
point shares the captured scene geometry and complete room/GUI skip mask
contribute to the filtered ranking. Mismatched conditions are counted by
reason and cannot inflate candidates for the actual conditional state.
Synthetic tests verify mixed-match rejection, stale-plan/report detection,
incorrect labels, unknown statuses and out-of-window picker calls. This
narrows leads without claiming a recovered downstream acceptance branch.

On 2026-11-05 the F10/H raster's omitted targets were traced to a deliberate
room-only filter: the old renderer never outlined winning GUI and slot-zero
fallback picker rows, and could paint room slots even when another native
group took selection priority. The normal H view now rasterizes the entire
decoded native table in original last-enabled-match order, retaining merged
room-object colors but distinguishing blue UI and muted/hover-only fallback
areas. Synthetic C++ tests cover cross-group overlaps, disabled rows,
anonymous room slots, unaligned groups and a later fallback masking a room.
The standalone g++ raster and original-scene tracker tests pass without
SDL3/Xvfb, as do the 79 probe tests and a read-only native startup smoke
test using the supplied Unicorn archive. The fallback builder was also
fixed to restore owner-executable mode when relinking an existing binary.
Native visual and downstream click-acceptance checks remain pending.
Workspace invocation is recorded in [workspace-setup.md](workspace-setup.md);
§3.6 remains ongoing.

The same day's offline picker audit was aligned with the new F10/H semantics.
`report_picker_scene.py` now reports the full native-table winner at each
recorded call while preserving a separately labeled legacy room-only audit.
`compare_picker_scene.py` adds `display_*` measurements for merged H winners
and keeps historical `h_*` JSON fields as *room-only candidate* measurements
so existing capture consumers do not silently change interpretation.
Conditional comparisons distinguish native row changes with unchanged object
display, GUI masking and fallback retargeting. The Python report, comparison
and batch-capture fixtures pass; actual native click consumption remains an
open experiment under private Xvfb.

The conditional comparison now additionally measures H winners and native
row changes over the **complete 640×480 window**, without changing the
existing picture-only JSON keys or `samples` used by older trial tools.
Full-window coordinates are available as `full_samples`; the private capture
batch can export them using `--oracle-points-scope full`. A synthetic GUI
row at X=20..40, Y=300..320 changes 441 full-window pixels while producing
zero picture-only changes, verifying the omitted-control case without
replaying original input. Real click acceptance still requires private Xvfb.

The next picker-oracle pass recovered an input-independent differential
test from original `GATE.RTL:0x14186..0x14244`, a self-contained 16-bit
rectangle selector. The `native-picker-oracle-tests GATEWAY-DOS --synthetic`
mode loads that original instruction stream directly into private Unicorn
RAM and mirrors generated group/index/rectangle tables at the on-disk and
observed runtime data offsets. Six deterministic cases compare original AX,
group, slot and kind against the reconstructed `DebugSceneView::picker_at`
over rectangular boundaries and sampled screen coordinates. The headless
GCC/Unicorn-ZIP fallback now optionally builds this oracle with
`--with-picker-oracle`; no SDL3/Xvfb input injection is needed. All 3,983
points passed (3,667 hits and 316 misses). This checks picker *instruction
semantics*, not actual RTLink relocation or the
downstream click dispatcher. Read-only headless startup produced zero live
picker-entry captures in 10,000 guest milliseconds; capturing a real entry
and testing mouse acceptance remains pending private Xvfb.

## 10. Risks and how to react

| Risk | Signal | Reaction |
| --- | --- | --- |
| Replays are nondeterministic | Two native runs differ | Stop porting; fix determinism first (§3.6) |
| Handlers depend on overlay residency | A handler reads data in another section, or behaves differently depending on load order | Model residency in the service layer; record the dependency in `notes`; port the routines involved as a cluster |
| Handlers write shared memory directly | A large `direct-memory` share in §3.4 | Add typed globals to `Story` as they are found; document each one in the service catalogue |
| Ghidra boundaries are wrong | Differential test fails at the return; overlapping functions | Fix the boundary in Ghidra and the tracker; port shared tails together with their owners |
| Computed jumps or calls | `switch` tables not found; indirect far calls | Recover the table by hand from disassembly; note its offset; test every case |
| The service API is guessed wrong | Many ported handlers break after a service change | Freeze a service's API once it is `verified`; change it only together with all its callers |
| Tooling scope creep | Days on tools without verified routines | Every Phase-1 step has an exit test; later tool work must be justified by a blocked routine |
| Legal | Derived code is redistributed | The owner decides; agents never publish |

## 11. Quick reference for a work session

```sh
nix develop                     # C++ build, probe, tests
nix develop .#ghidra            # adds Ghidra 12 + PyGhidra
python3 tools/decomp_status.py summary
python3 tools/decomp_status.py next --limit 10
python3 tools/decomp_status.py check
ctest --test-dir "${GATEWAY_BUILD_DIR:-build}" --output-on-failure
nix flake check                 # build + tests, reuse lint, tracker check
```

### Regenerating `build/` and reseeding the tracker

These steps need the original files in `../gateway-dos/`. Everything is
written under `build/`, which is ignored by Git. Without Nix, see
[ghidra.md](ghidra.md#without-nix).

```sh
nix develop .#ghidra
cmake -S . -B build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/rel
B=build/rel H=build/handler-coverage
python3 tools/trace_game_logic.py --probe $B/gateway-probe --data ../gateway-dos \
  --output $H --scenario objects --native-only --verify-overlays --trace-rtlink-calls
# Add --timeout 900 on slower machines if the default 180-second replay limit expires.
python3 tools/inspect_object_state.py $H/native --names ../gateway-dos/OBJECT.DAT > $H/objects.json
$B/gateway-overlay ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL --fixups \
  > $H/rtlink-manifest.json
python3 tools/inspect_room_callbacks.py $H/native --overlay-map $H/rtlink-manifest.json \
  --format json > $H/callbacks.json
python3 tools/inspect_action_dispatch.py $H/native --all \
  --overlay-map $H/rtlink-manifest.json --resident-exe ../gateway-dos/GATEMAIN.EXE \
  --format json > $H/actions.json
python3 tools/run_ghidra.py ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --overlay-tool $B/gateway-overlay --objects-json $H/objects.json \
  --callbacks-json $H/callbacks.json --actions-json $H/actions.json \
  --tracked-handler-boundaries decomp/functions.tsv \
  --output build/ghidra
python3 tools/inventory_native_handlers.py $H/objects.json build/ghidra/ghidra-plan.json \
  --callbacks-json $H/callbacks.json --rtlink-calls $H/native.rtlink-calls \
  --format json > $H/inventory.json
python3 tools/decomp_status.py seed --functions build/ghidra/ghidra-functions.tsv \
  --inventory $H/inventory.json --actions $H/actions.json
python3 tools/decomp_status.py mark-handler-boundaries \
  --decompile build/ghidra/ghidra-decompile-report.json \
  --functions build/ghidra/ghidra-functions.tsv
python3 tools/classify_shapes.py build/ghidra/ghidra-facts.json --write
python3 tools/rank_engine_services.py --facts build/ghidra/ghidra-facts.json \
  --plan build/ghidra/ghidra-plan.json \
  --exe ../gateway-dos/GATEMAIN.EXE --ovl ../gateway-dos/GATEMAIN.OVL \
  --rtl ../gateway-dos/GATE.RTL --native-snapshot $H/native.bin \
  --output build/ghidra/native-service-fanout.json
```

`run_ghidra.py` refuses an existing output directory; delete `build/ghidra`
or pass another `--output`. Seeding is idempotent: a fresh run against an
unchanged tracker reports `0 added, 0 fields updated`. The handler
inventory step needs the plan from the Ghidra step; its object JSON includes
object names from `OBJECT.DAT`, which must stay in `build/`.
