# GATE.EXE/GATE.RTL: batch decompilation with Ghidra

This is the **intro**, not the `GATEMAIN.EXE`/`GATEMAIN.OVL` game-logic
workflow in [ghidra.md](ghidra.md). It uses Ghidra's real x86-16 C
decompiler, seeded with previously identified functions, and follows other
direct call targets. The generated pseudocode is an analysis aid; it is neither
original recovered source nor a playable native executable.

The raw `GATE.RTL` has unresolved relocation placeholders. **Do not import it
as if its on-disk bytes were linked machine code.** This pipeline uses a
specific 22-second 1 MiB guest-memory snapshot and verifies its segment/file
identities against both SHA-256-pinned DOS binaries. It imports five
independently established resident RTL segments, not the complete RTL file,
and preserves their potentially overlapping addresses using Ghidra overlay
spaces. The newly added `0c33` file-service bank has four independent entry
signatures and five known far-entry targets. A linked far-call destination
outside these segments remains unresolved, not a guessed function.

## Capture the reference snapshot

Given the original game in `../gateway-dos`, build the original Unicorn probe.
For example, the native/static probe can produce the necessary snapshot:

```sh
mkdir -p build/intro-analysis
build/local-probe/gateway-probe ../gateway-dos/GATE.EXE \
  --headless --read-only --deterministic-clock --milliseconds 24000 \
  --dump-at 22000:build/intro-analysis/at-22000
```

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
| `intro-ghidra-plan.json` | Original file identities, source offsets, linked bank map and witnesses |
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

**Current coverage limitation:** the six verified RTL spans are
`0a46:0000..0a7f`, `0c33:0000..01a9`, `0c4d:0000..08ff`, `0cdc:0000..10ff`, and
`0f10:0000..1aff`, plus EXE-relative `02a5:0000..3cff` (resident
`12a5:0000..3cff`) in the captured load layout. The latter maps
`GATE.RTL:56de..93dd`, including linked `strlen` and the C runtime random
generator. Its verified start is based on the on-disk/linked byte correlation;
its end deliberately stops before the next region overwrites memory near
`RTL:9480`. They deliberately exclude unknown sections and external service
segments. All missing calls must be mapped/verified before the intro is
complete. An imported label or successful pseudocode export is not evidence
that the original calling convention, signedness, data structure or guest
side effects are correct.

The newly mapped `0a46` span is anchored by the first startup FAR call in
`GATE.EXE:134c`: original `0a46:01ec` appears at resident `1a46:01ec`
in the 22-second snapshot. Its unique first instruction signature matches
`GATE.RTL:c3bd`, establishing the section's file base at `RTL:c1d1`.
Six separate source/linked instruction windows verify that base, including
the call entry at `c3bd`; the span stops at relative `0a80` because a later
image overwrites the following region. Importing this function is **not**
the same as recovering its source semantics or making the C intro playable.

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
