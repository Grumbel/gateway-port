# GATE.EXE/GATE.RTL: batch decompilation with Ghidra

This is the **intro**, not the `GATEMAIN.EXE`/`GATEMAIN.OVL` game-logic
workflow in [ghidra.md](ghidra.md). It uses Ghidra's real x86-16 C
decompiler, seeded with previously identified functions, and follows other
direct call targets. The generated pseudocode is an analysis aid; it is neither
original recovered source nor a playable native executable.

The raw `GATE.RTL` has unresolved relocation placeholders. **Do not import it
as if its on-disk bytes were linked machine code.** This pipeline uses a
specific 22-second 1 MiB guest-memory snapshot and verifies its segment/file
identities against both SHA-256-pinned DOS binaries. It imports nine
independently established resident RTL segments, not the complete RTL file,
and preserves their potentially overlapping addresses using Ghidra overlay
spaces. The newly added `0c33` file-service bank has four independent entry
signatures and five known far-entry targets. A linked far-call destination
outside these segments remains unresolved, not a guessed function.
The first `0600h`/`0640h` bytes of the two later graphics windows
were observed to load between 250 and 350ms and remain unchanged through
1500ms. The longer portions are independently verified in the 22-second
reference, without asserting that they were already loaded at 350ms. The
windows contain **nine independently verified source-file fragments**: six
in the first window and three in the second. A newly mapped preceding
`244c` window has six more verified fragments, for **15 graphics fragments**
in three contiguous resident windows. Their mappings account for metadata
gaps in the original RTL file. Every fragment is reverified on each import.

## Capture the reference snapshot

Given the original game in `../gateway-dos`, run the probe through the
Nix flake. It builds the probe automatically; no `build/local-probe/`
executable is expected in a normal checkout:

```sh
mkdir -p build/intro-analysis
nix run -L .#probe -- ../gateway-dos/GATE.EXE \
  --headless --read-only --deterministic-clock --milliseconds 24000 \
  --dump-at 22000:build/intro-analysis/at-22000
```

Alternatively, `nix build -L .#default` installs the same binary at
`result/bin/gateway-probe`. In `nix develop`, `gateway-build` puts the
binary at `$GATEWAY_BUILD_DIR/gateway-probe` (normally
`/tmp/gateway-port-build/gateway-probe`). The `build/local-probe/` path is
**only** created by the manual static-Unicorn ZIP recipe in
[workspace-setup.md](workspace-setup.md).

`at-22000.bin` must be exactly 1 MiB, and the probe's guest layout must use
the default relative load segment `0x1000`. The preparer validates the actual
code matching each segment; a snapshot from a different layout fails rather
than silently supplying wrong addresses. The binaries and captures remain
outside the Git history.

## Run the actual Ghidra decompiler

From the repository root:

```sh
nix run .#intro-ghidra -- \
  ../gateway-dos/GATE.EXE ../gateway-dos/GATE.RTL \
  --snapshot build/intro-analysis/at-22000.bin \
  --output build/ghidra-intro
```

Or use the already available Ghidra development shell:

```sh
nix develop .#ghidra
python3 tools/run_intro_ghidra.py \
  ../gateway-dos/GATE.EXE ../gateway-dos/GATE.RTL \
  --snapshot build/intro-analysis/at-22000.bin \
  --output build/ghidra-intro
```

Without downloading Ghidra, validate all original and live-linked inputs:

```sh
python3 tools/run_intro_ghidra.py \
  ../gateway-dos/GATE.EXE ../gateway-dos/GATE.RTL \
  --snapshot build/intro-analysis/at-22000.bin \
  --output build/ghidra-intro --prepare-only
```

Ghidra 12's Python scripts require PyGhidra. The Nix invocation uses the
source-built `ghidra` package, OpenJDK 21, and `pyghidra` already configured
for the main-game workflow. An explicit `--headless` is supported for older
Ghidra/Jython installations.

The runner creates a **new** project; it refuses existing report/project
paths to avoid confusing old pseudocode with a new run. Choose a new `--output`
for another batch, or open the existing `project/GatewayIntro.gpr` manually.

## Artifacts (all ignored under `build/`)

| Path in `build/ghidra-intro/` | Purpose |
| --- | --- |
| `intro-ghidra-plan.json` | Original file identities, source offsets, linked bank map, witnesses and graphics zero-word observations |
| `sections/*.linked.bin` | Executable bytes **from live DOS memory**, retaining relocations |
| `intro-ghidra-import-report.json` | Ghidra import and EXE signature resolution |
| `intro-ghidra-functions.tsv` | All Ghidra-discovered intro functions and their original offset identities |
| `intro-pseudocode.txt` | All generated C-like function bodies (not suitable for Git) |
| `intro-ghidra-callgraph.json` | Per-function static calls and explicit unresolved targets |
| `intro-ghidra-unresolved.tsv` | Unmapped calls ranked by target and frequency, with original caller identities |
| `intro-ghidra-report.json` | Coverage, decompile failures, missing entries and unresolved call count |

Known C recovery seeds include 10 `GATE.EXE` functions, reconstructed
`GATE.RTL` routines, and additional linked-entry candidates, including
the not-yet-reconstructed `rtl:0x130ab` animation entry. The original
`rtl:0x8298` `strlen` is now verified at runtime `12a5:2bba` (physical
`0x1560a`, exact 23-byte match) and is imported in the fifth RTL bank.
The post-analysis script additionally follows statically discovered direct
calls where the callee belongs to the verified linked blocks, including signed
near calls whose original targets are inside the same verified RTL overlay,
and enumerates all functions Ghidra finds in those blocks. This is intended to produce large
batches for inspection instead of manually translating six functions per turn.

**Current coverage limitation:** the nine verified RTL banks are
`0a46:0000..0a7f`, `0c33:0000..01a9`, `0c4d:0000..08ff`, `0cdc:0000..10ff`, and
`0f10:0000..1aff`, plus EXE-relative `02a5:0000..3cff` (resident
`12a5:0000..3cff`) in the captured load layout. The latter maps
`GATE.RTL:56de..93dd`, including linked `strlen` and the C runtime random
generator. Its verified start is based on the on-disk/linked byte correlation;
its end deliberately stops before the next region overwrites memory near
`RTL:9480`. A newly established EXE-relative graphics window
`144c:0000..0dff` (resident `244c:0000..0dff`) immediately precedes the
two previously mapped graphics windows `152c:0000..0aaf`
(resident `252c:0000..0aaf`) and `15d7:0000..0d78` (resident
`25d7:0000..0d78`). The first A fragment starts at runtime `+000a`
(`RTL:18e00`); the first B fragment at `+0004` (`RTL:19997`).
Direct comparison with the pinned 22-second guest RAM verifies **3383/3582**
covered source bytes unchanged in `244c`, **2600/2726**
covered source bytes unchanged in A and **3198/3443** in B, each with
multiple independent witnesses. Ten leading A bytes, four leading B bytes,
two `244c` post-return boundary bytes `+0115` and `+0b03`, and B's
single-byte boundaries `+04ad` and `+0c15` lack verified RTL file
identities. Seven trailing B bytes from an earlier proposed mapping were
zeroed in live memory and are now excluded from the import; they are not
treated as copied code. B's previous 489 mismatches against one *linear*
1600-byte source were largely caused by projecting across RTL metadata gaps.
Other unverified sections and external
service segments remain excluded. All missing calls must be mapped/verified before the intro is
complete. An imported label or successful pseudocode export is not evidence
that the original calling convention, signedness, data structure or guest
side effects are correct.

The source-name lookup also follows `decomp/launcher/symbol-names.tsv`: the
original file-offset identities remain intact even though reconstructed C
functions have readable names. The pinned 22-second preparation check now
produces **nine linked sections, 107 source RTL entry labels, and zero
unmapped known RTL entry labels**. This does not mean the RTL's complete
function set or cross-bank calls have been recovered.

The 22-second capture's identified graphics `CALL/JMP FAR` operands account
for **78 static transfers**. Their physical 20-bit addresses map **67**
unambiguously into the three verified graphics banks; the other **11** all
target resident segment `2409`, which is not yet imported. `244c` is a
direct bank base, while `25b7` aliases an offset in `252c` and `2621`
aliases `25d7` (starting at linked offset `04a0`). The plan's
`graphics_far_targets` records each observed segment/offset pair and its
source-backed target, or an explicit unresolved result. The Ghidra batch
decompiler now resolves FAR aliases by guest physical address and uses each
fragment's own `source_spans` for original file identities, including near
calls. It never assigns an RTL file offset to a two-byte boundary gap.

The reconstructed graphics-fragment mapping is explicitly recorded in each
Ghidra plan section's `source_spans`. `offset` is the runtime offset *inside*
the bank; `file_base` is the first corresponding original RTL file byte.
Relative offsets are not interchangeable across fragments:

| Runtime graphics window | Runtime offset range | Original RTL file range |
| --- | --- | --- |
| `244c` | `0000..0114` | `17e6d..17f81` |
| `244c` | `0116..02d1` | `17f98..18153` |
| `244c` | `02d2..03c9` | `18187..1827e` |
| `244c` | `03ca..05bb` | `18296..18487` |
| `244c` | `05bc..0b02` | `184e8..18a2e` |
| `244c` | `0b04..0dff` | `18a95..18d90` |
| `252c` | `000a..00e3` | `18e00..18ed9` |
| `252c` | `00e4..0181` | `18edf..18f7c` |
| `252c` | `0182..0667` | `18f8c..19471` |
| `252c` | `0668..0771` | `194d7..195e0` |
| `252c` | `0772..08b7` | `19606..1974b` |
| `252c` | `08b8..0aaf` | `19777..1996e` |
| `25d7` | `0004..04ac` | `19997..19e3f` |
| `25d7` | `04ae..0c14` | `19ec2..1a628` |
| `25d7` | `0c16..0d78` | `1a6ee..1a850` |

### Linked graphics word changes

The original bytes within these independently validated spans differ from
the 22-second linked image only at originally `0000h` two-byte word sites.
The preparer records each candidate's **original RTL file offset**, runtime
offset, observed word and conservative machine-instruction context in the
`zero_word_changes` section of `intro-ghidra-plan.json`. For a compact
console summary:

```sh
python3 tools/prepare_intro_ghidra.py \
  ../gateway-dos/GATE.EXE ../gateway-dos/GATE.RTL \
  --snapshot build/intro-analysis/at-22000.bin \
  --output build/ghidra-graphics-words \
  --graphics-word-changes
```

| Linked window | Originally zero word sites | Changed bytes accounted for | Changed covered bytes unexplained |
| --- | ---: | ---: | ---: |
| `244c` | 109 | 199 | 0 |
| `252c` | 64 | 126 | 0 |
| `25d7` | 134 | 245 | 0 |
| **Total** | **307** | **570** | **0** |

The newly recovered `244c` bank contributes 109 word sites: nine DS loads,
43 words within 22 FAR calls, 48 16-bit memory immediates, and nine direct
memory displacements. Every site has a checked local opcode context.

For the previously mapped `252c` and `25d7` banks, local 8086 opcode
inspection assigned a specific operand role to all **198 word sites**, using
unchanged opcode and addressing bytes in both the original RTL file and
linked capture:

| Verified local opcode context | Changed word sites | Interpretation |
| --- | ---: | --- |
| `MOV AX,imm16; MOV DS/ES,AX` | 31 | 30 DS loads, one ES load; all observed `2a7ah` |
| `CALL FAR ptr16:16` | 83 | 53 changed segment words and 30 changed offset words; 23 calls retain a zero offset |
| `JMP FAR ptr16:16` | 6 | Three changed segment/offset pairs |
| `MOV r16,imm16` | 37 | Immediate operands for SI, DI and BX |
| `MOV [BP+disp8],imm16` | 12 | Local stack data initialization |
| `MOV [BX+disp8],imm16` | 22 | Eleven verified FAR-pointer table entries (two words each) |
| Direct `[disp16]` references | 6 | Direct-memory addresses in `MOV`, `CMP`, or `MOV byte` |
| `CMP AX,imm16` | 1 | Immediate comparison operand |
| **Total** | **198** | **No residual site lacks a local opcode pattern** |

In resident bank `25d7`, a `MOV BX,imm16` followed by eleven pairs of
`C7 47 0C` and `C7 47 0E` stores, each ending with `ADD BX,14h`, writes an
apparent FAR-pointer table. The captured DS=`2a7ah` contains exactly the
eleven corresponding pointers at `DS:2a9b + 14h * index`, all targeting
segment `2621h`. Every stored offset and segment matches the 22-second
RAM image. The plan records the original instruction file offsets, live
destinations, target words, stride, and per-entry memory validation under
`far_pointer_tables`. A synthetic test deliberately corrupts one destination
and checks that the report no longer labels the table as verified.

A one-byte substitution is classified as a word candidate only when the
adjacent high byte was zero in *both* images.

**This is a classification of bytes, not a working relocation table.** An
originally zero word becoming nonzero is not proof of why or when the linker
wrote it. Runtime values can depend on allocations and load layout; the
opcode patterns alone do not prove the bytes were executed, identify the
original relocation records, or establish when the 11 table entries were
written. The nine code fragments still need a real DOS loader before a
standalone EXE can use them.

The additional 22 seeded entries are recognized by matching the original
Microsoft C function prologues (before the patched `DS` immediate), not by
guessing every function inside these source ranges. They are analysis
candidates until their calls and semantics are independently verified.

The newly mapped `0a46` span is anchored by the first startup FAR call in
`GATE.EXE:134c`: original `0a46:01ec` appears at resident `1a46:01ec`
in the 22-second snapshot. Its unique first instruction signature matches
`GATE.RTL:c3bd`, establishing the section's file base at `RTL:c1d1`.
Six separate source/linked instruction windows verify that base, including
the call entry at `c3bd`; the span stops at relative `0a80` because a later
image overwrites the following region. Importing this function is **not**
the same as recovering its source semantics or making the C intro playable.
The original entry checks relocated `DS:0662 & 6` and `DS:0660 != 0`
before starting music. The original 10-second probe reaches the routine,
opens `GATE_004.MUS`, and installs an IRQ 8 handler. The first two checks
are independently recovered in `gate_rtl_audio_startup.c`, allowing the
silent/unconfigured fast exit to be decided in source C. **The music-loader
branch, interrupt lifecycle, and actual intro startup call remain unported.**

An independent original-entry/return boundary capture (physical
`0x107b2..0x107b7`) verifies that this call modifies exactly one interrupt
vector, IRQ 8 (`f000:0100` to `172c:00ca` in this run), while mode-13h VGA
memory remains byte-identical. Audio flag `DS:0662` changes from `0x0a`
to `0x5a`. This supports a separate optional host music service, but does
not prove that skipping enabled music reproduces all original side effects.

The fourth bank correlates the following *original file offsets*, not just
runtime addresses:

| Linked entry | `GATE.RTL` entry | Candidate operation |
| --- | --- | --- |
| `0c33:0040` | `0xe531` | Open resource file |
| `0c33:0091` | `0xe582` | Seek to file offset |
| `0c33:00d4` | `0xe5c5` | Read word |
| `0c33:00f2` | `0xe5e3` | Read bytes |
| `0c33:019a` | `0xe68b` | Close file handle |

These five entries and eight neighboring RTL functions have now been transcribed
into C (`gate_rtl_file.c`), preserving the original call order and argument
widths. Five resident `1c29` INT 21h file thunks are now reconstructed and
bound to a tested native file adapter. The host adapter additionally
implements only the original intro's verified `12a5` filename format
`%s_%03d.%s` and read-permission check, not the generic C runtime.
The last function's code overlaps the
next loaded segment in physical RAM; the import stops at its original `RETF`.
A different snapshot can have different occupancy.

**Critical address-space distinction:** the importer adds the EXE load
segment `0x1000` to its *relative* RTL identifiers. Its `24a7` staging
copy therefore lives physically at `0x34a70` and really does contain
unrelocated RTL file bytes from offset `0x101ef`. **The real executable VFX
rasterizer is already resident at the absolute guest address `24a7:000c`
(physical `0x24a7c`) in that same snapshot.** It starts with a standard
BP prologue, calls VFX initialization at `24a7:0120` and dispatches image
formats through an indirect FAR jump. Likewise the resident file thunks
live at absolute `1c29:0000..0093` (physical `0x1c290..0x1c323`), not
`0x2c290`. These absolute resident code banks must not be conflated with
the different `0x1000`-relative RTL imports.

## Next verification and implementation work

1. Run Ghidra and inspect `intro-ghidra-report.json`; fix any unresolved
   EXE signatures, split functions or missing RTL entry boundaries.
2. Review `intro-ghidra-unresolved.tsv` and extend the section map using
   additional linked call-site and guest-memory witnesses, then rerun Ghidra
   in a **fresh** output/project directory.
3. Prioritize the now-located absolute `24a7:000c` VFX decoder and its
   format dispatch, general `12a5` CRT calls still used elsewhere, and the other
   hardware/keyboard/time services.
4. Derive readable C from the pseudocode, verifying disassembly, original
   register/stack state and differential behavior; **never paste generated
   Ghidra pseudocode into the repository**.
5. Link the complete intro with verified narrow DOS/hardware host adapters
   as an actual executable in `flake.nix`. Until that executable runs and its
   output matches the original, `.#intro-decompile` remains a C archive and
   `.#intro` remains the separate native C++ presentation prototype.

No main-game decompilation is part of this work.
