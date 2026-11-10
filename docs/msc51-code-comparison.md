# Comparing reconstructed Gateway C with the original 16-bit code

The source-C launcher builds with the real Microsoft C 5.1 compiler, but a
successful DOS executable is **not** evidence that our C compiles into the
original instructions. There are three separate kinds of evidence:

| Level | Test | What it establishes |
| --- | --- | --- |
| Raw object bytes | Same bytes and span length as a bounded original procedure | Object-code identity **before** Microsoft LINK relocation; no claim about absolute linked addresses |
| Relocation-aware bytes | All unmodified OMF bytes agree, same span length, at least 12 fixed bytes | Potential identity after linker fixups; the actual relocated values still need checking |
| Behavioral oracle | Both programs start from matching state and produce matching results, state changes and ordered external calls | Behavioral parity for **those inputs and observations**, regardless of generated instructions |

An exact executable comparison is stronger still: both object and original
code must be loaded at known segments and all linker FIXUPP/EXE relocation
operands resolved. We do not currently have such a comparison for recovered
launcher procedures.

## Current repeatable byte comparison

```sh
python3 tools/msc51_code_compare.py \
  --stage build/msc51-all \
  --original-game-dir ../gateway-dos \
  --json build/msc51-all/code-comparison.json
```

The tool reads only the **49 objects named in `LINK.RSP`**, not probe objects
left in the same directory. It validates the original `GATE.EXE` and
`GATE.RTL` SHA-256 identities, locates compiled CODE publics using OMF
`PUBDEF`, translates Microsoft C's leading underscore and our staged
31-character `SYMBOL.MAP` aliases, and reconstructs each CODE segment from
contiguous `LEDATA` and its associated `FIXUPP` masks. Unsupported fixups,
overlapping fragments and `LIDATA` do not become guessed code. It currently
compares **18 original-disassembly-bounded procedures**: nine from the
launcher EXE and nine from the RTL. The report contains offsets, byte counts,
agreement counts and statuses, but never original executable byte sequences.

The end of an OMF public is *not* explicit. We conservatively stop at the next
public or known code extent; these compiled spans can include unnamed static
helper functions. Therefore **compiled public span size is not necessarily
the size of one compiled C function**. In particular, differing span sizes
cannot alone establish different behavior.

The Microsoft C 5.1 `/AL /Od` objects compiled on 2026-11-10 had these
representative comparisons:

| Recovered function | Original bytes | Compiled public span | Equal fixed aligned bytes |
| --- | ---: | ---: | ---: |
| `gate_exe_fade_in_picture` | 217 | 344 | 9 / 169 |
| `gate_exe_play_intro` | 1,810 | 2,543 | 50 / 1,297 |
| `gate_rtl_fade_palette_in` | 175 | 205 | 19 / 143 |
| `gate_rtl_upload_vga_palette` | 524 | 973 | 22 / 420 |

All **18/18 currently differ** by the strict positional byte check. There
are zero exact object matches and zero full relocation-aware matches. These
figures are an *initial code-generation baseline*, not a measure of the
semantic accuracy of the decompilation. The original optimization settings
and complete linker placement are not yet established; `/Od` is deliberately
unoptimized. The generated code also calls our host-service boundaries in
places where the original uses different linked resident procedures.

Both `gateway-msc51 compile-all` and `gateway-msc51 test-dos-entry` now run
the comparison after their existing real-DOS checks pass, writing
`CODECMP.JSON` beside the built DOS objects. A report with differences does
**not** fail the build. A malformed OMF object, untrusted original executable
or failed comparison does. Any future known-matching function may be made a
strict regression with `--require-exact SYMBOL`; it will fail unless the
object bytes and complete bounded span agree.

## Existing behavioral evidence (distinct from byte identity)

- Source-C host tests check 16-bit signedness, FAR offset wrap, calls,
  resource state and representative palette/pixel operations against
  independently established original disassembly and captures.
- The source-C full-intro tests assert deterministic image/DAC/timestamp
  recordings and final framebuffer hashes using original read-only assets.
  Many of these baselines come from earlier source-C runs, so they are
  **regression checks**, not independent original-executable oracles.
- `GATEWAY.EXE /CHECK`, `/PICTURE`, `/SECOND`, `/TITLE`, `/PALTIME`,
  `/PAL320`, `/TRANS` and `/SCENE` actually run reconstructed code under
  DOSBox. Some probes read back real VGA mode, DAC, pixels or BIOS state.
  They do **not** execute the corresponding original instructions in the
  same machine state and compare a complete state delta.
- `tools/compare_intro_frames.py` supports private comparison of original
  DOS framebuffer captures against native source-C recordings. The entire
  sequence still has unverified visual transitions and timing.

## Next milestones

1. Collect comparable compiler output under several documented MS C 5.1
   optimization options, without overwriting the reproducible `/Od` baseline.
2. Recover exact executable/RTL procedure extents, identify compiler-private
   helpers, apply OMF FIXUPP and linker segment placement, and disassemble
   both sides into canonical 8086 instruction forms. Instruction similarity
   is diagnostic, not semantic proof.
3. Execute **both** versions of selected bounded functions from identical
   guest snapshots; compare return registers, affected RAM, VGA/DOS I/O and
   the ordered call trace, with explicit masks for genuine nondeterminism.
   Start with pure palette/math routines, then the full picture transition.
4. Promote each independently established byte or behavioral match to a
   required test. Preserve the original artifacts outside Git.

