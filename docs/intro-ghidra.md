# GATE.EXE/GATE.RTL: batch decompilation with Ghidra

This is the **intro**, not the `GATEMAIN.EXE`/`GATEMAIN.OVL` game-logic
workflow in [ghidra.md](ghidra.md). It uses Ghidra's real x86-16 C
decompiler, seeded with previously identified functions, and follows other
direct call targets. The generated pseudocode is an analysis aid; it is neither
original recovered source nor a playable native executable.

The raw `GATE.RTL` has unresolved relocation placeholders. **Do not import it
as if its on-disk bytes were linked machine code.** This pipeline uses a
specific 22-second 1 MiB guest-memory snapshot and verifies its segment/file
identities against both SHA-256-pinned DOS binaries. It currently imports
three independently established resident RTL segments, not the complete
RTL file, and preserves their potentially overlapping addresses using Ghidra
overlay spaces. A linked far-call destination outside those segments remains
unresolved, not a guessed function.

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
| `intro-ghidra-report.json` | Coverage, decompile failures, missing entries and unresolved call count |

Known C recovery seeds include 10 `GATE.EXE` functions, 36 reconstructed
`GATE.RTL` routines, and nine additional linked-entry candidates, including
the not-yet-reconstructed `rtl:0x130ab` animation entry. The original
`rtl:0x8298` `strlen` remains explicitly outside these three imported banks.
The post-analysis script additionally follows statically discovered direct
calls where the callee belongs to the verified linked blocks, and enumerates
all functions Ghidra finds in those blocks. This is intended to produce large
batches for inspection instead of manually translating six functions per turn.

**Current coverage limitation:** the three verified RTL spans are
`0c4d:0000..08ff`, `0cdc:0000..10ff`, and `0f10:0000..1aff` in the captured
load layout. They deliberately exclude unknown sections and external service
segments. All missing calls must be mapped/verified before the intro is
complete. An imported label or successful pseudocode export is not evidence
that the original calling convention, signedness, data structure or guest
side effects are correct.

## Next verification and implementation work

1. Run Ghidra and inspect `intro-ghidra-report.json`; fix any unresolved
   EXE signatures, split functions or missing RTL entry boundaries.
2. Extend the section map by verifying additional RTL code against guest
   memory, then rerun Ghidra in a **fresh** output/project directory.
3. Prioritize the rasterizer reached at runtime `24a7:000c`, the linked
   `0c33` file I/O services, animation `rtl:0x130ab`, and the remaining
   hardware/keyboard/time services.
4. Derive readable C from the pseudocode, verifying disassembly, original
   register/stack state and differential behavior; **never paste generated
   Ghidra pseudocode into the repository**.
5. Link the complete intro with verified narrow DOS/hardware host adapters
   as an actual executable in `flake.nix`. Until that executable runs and its
   output matches the original, `.#intro-decompile` remains a C archive and
   `.#intro` remains the separate native C++ presentation prototype.

No main-game decompilation is part of this work.
