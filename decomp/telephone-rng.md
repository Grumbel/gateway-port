<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: LicenseRef-Gateway-Derived
-->

# Telephone generator: stable identities and native contract

## Evidence checked locally, 2026-11-04

The original animation/telephone random generator is **GATE.RTL:0x0091ea**
through its far return at `0x009211` (40 bytes). This is external CRT code,
not an EXE/OVL tracker row. The adjacent seed setter starts at `0x0091d8`:
one word at BP+6, stored as the low state word, with the high word cleared;
far return, caller cleanup. Seed-setter execution has not been traced here.

The RNG has no arguments, no frame, and returns via RETF. It passes two
32-bit operands to the far multiplication helper: the current state and
214013. The helper consumes those operands (the RNG has no ADD SP before
return). It adds 2531011 with low-word ADD/high-word ADC and stores the
32-bit result. AX receives the high word masked with 0x7fff; DX retains the
unmasked high word. Do not assume other registers or flags survive.
The caller sign-extends AX before signed division; the mask guarantees a
nonnegative dividend.

The 2026-11-07 comparison against the actual Microsoft C 5.1 `LLIBCR.LIB`
also identified these two functions through the library's original OMF public
symbols: `GATE.RTL:0x0091d8` matches `rand.c:_srand` (13/17 fixed bytes),
and `GATE.RTL:0x0091ea` matches `rand.c:_rand` (28/40 fixed bytes). Both
symbols are in the `rand.c` module at library file offset `0x173c0`, in
segment 1 at offsets `0x0000` and `0x0012`. These are linked CRT identities,
not proof that the original game logic was compiled with that exact compiler.

In the captured layout the entry is `1805:3b06` (physical `0x1bb56`), with
state words at `DS:358e`/`DS:3590`, DS=5b46. Comparing all 40 original bytes
against RAM changes only the four state-word address operands and the far
multiply call operand. This supports the RTL identity independently of
matching the multiplier alone. No claim of per-routine replacement
verification is made.

## Section 47 telephone branch

Original `OVL:0x064209` calls the RNG; `0x064216` calls it again. These are
instruction sites inside a larger routine, **not recovered function entries**.
The first result is reduced modulo 9999 and incremented, then pushed. The
second is reduced modulo 99 and incremented, then pushed. Two far pointers
follow: the format and destination. The formatter call is at `0x064230`,
followed by caller cleanup of twelve bytes. Thus the variable arguments
are the second result's field first, then the first result's field.
The branch is selected by a local word equal to 4; its complete enclosing
handler and event semantics remain unresolved. No tracker row is invented
at the branch address.

The full **7,056-byte section 47 image** at original file offset `0x062b80`
was compared with RAM at physical `0x49900` from
`build/golden/terri-sprintf-source/at-63366.bin`. Applying exactly the
manifest's section-47 relocation fixups (+0x1000 per segment word) makes
all bytes agree. This resolves overlay identity despite reuse of the runtime
address by other sections. The trace's last section-47 load before this
snapshot is at 63,196 ms. The same physical telephone site contains other
code in later snapshots; never identify it from the address alone.

The format is stored at original `EXE:0x022edd`; in this layout it is
DS:744a. The destination is DS:7228 (physical `0x62688`). The formatter
entry at 63,367 ms records destination and format as far pointers, then
arguments 0x12 and 0x1b79, followed by the caller's local word 4.

## Independent arithmetic check against the captures

The two snapshots `at-63366.bin` and `at-63369.bin` bracket generation:

| Point | Unsigned state | Returned result |
| --- | --- | ---: |
| Before | 0x0abbfbe4 | — |
| First advance | 0xc2873b17 | 17031 |
| Second advance | 0x5588097e | 21896 |

The first field calculation gives 7033; the second gives 18. The final
snapshot contains the resulting seven-character number and NUL, with
state exactly 0x5588097e. The native entry trace has two RNG calls at
63,367 ms, returning to `4990:168e` and `4990:169b`, precisely the two call
sites above. Together these validate call order, arithmetic, final state,
and destination; snapshots alone would not establish the caller identity.

Next work: recover the enclosing handler boundary and all branch effects;
identify callers of the seed setter; capture registers and memory at RNG
entry/return for the differential harness. No C++ replacement is enabled.
