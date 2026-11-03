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

### 3.5 [ ] Engine-service catalogue (`docs/engine-services.md`)

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

### 3.6 [ ] Golden replay suite

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
```

`run_ghidra.py` refuses an existing output directory; delete `build/ghidra`
or pass another `--output`. Seeding is idempotent: a fresh run against an
unchanged tracker reports `0 added, 0 fields updated`. The handler
inventory step needs the plan from the Ghidra step; its object JSON includes
object names from `OBJECT.DAT`, which must stay in `build/`.
