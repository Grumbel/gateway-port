<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: LicenseRef-Gateway-Derived -->

# Original GATEMAIN C reconstruction

This directory reconstructs the lost *source-level behavior* of the original
16-bit DOS `GATEMAIN.EXE`. It is distinct from the C++ host engine in
`src/`: no native redesign, change to the original state layout, or invented
game scripting language is introduced here. The recovered C is **not**
claimed to reproduce the original source's spelling, declarations or
compiler-generated machine code byte-for-byte.

## Object-property routines

Five contiguous FAR functions at original **file offsets** `0x27db..0x2985`
are reconstructed in `gatemain_flags.c`:

| File offset | Function | Source-level behavior |
| --- | --- | --- |
| `0x27db..0x284a` | `gatemain_exe_27db` | Check positive signed object ID against unsigned maximum, read object record, ensure type 1..8, return its FAR state address at offset +4. |
| `0x284b..0x28e3` | `gatemain_exe_284b` | Resolve state byte and low-three bit position for an applicable object/property pair, returning a null FAR pointer on failure. |
| `0x28e4..0x291d` | `gatemain_exe_28e4` | Shift the selected state byte, return its flag as `AX=0` or `AX=1`. |
| `0x291e..0x2950` | `gatemain_exe_291e` | OR the selected flag bit into the state byte. |
| `0x2951..0x2985` | `gatemain_exe_2951` | AND the inverse of that bit into the state byte. |

The first helper's actual **eight-way** original near jump table at file
`0x2833..0x2842` has eight *identical* destinations, all `CS:02ea` (file
`0x2828`). The compiled `switch` is therefore equivalent to one `+4`
offset for **all** types 1..8; the C retains that fact without inventing
per-type logic.

The property helper accesses the original tables:

- `[word DS:8d54]:38dc` gives the object-count limit.
- `[word DS:8d50]` points to six-byte object-directory entries at
  `0xba + 6*object`. Entry byte 0 is type; entry words +2/+4 form the
  original FAR state pointer.
- `[word DS:8d56]` points to property definitions at `4*property`; each
  definition has bit-position byte +0x14 and applicability mask +0x16.
- The helper returns `state.offset + 4 + (position >> 3)` in **the same
  segment**, writing `position & 7` to the caller's FAR output word only
  after all checks succeed.

The signed object-ID rejection, unsigned maximum check, property-zero
rejection, 16-bit multiplication/offset wrap, and bit-preserving OR/AND
come directly from original instructions. In particular, 16-bit property
index wrap means `0x4001` aliases property 1; no invented upper-bound
check was added. An invalid request must leave the caller's bit output
unchanged, even if it contains an arbitrary previous value.

`gatemain_ds_word()` and the `gatemain_far_*()` functions are temporary
intrinsics for the original segmented C pointers; they are not a new
object API. The source-level `uint16_t *bit` represents the original FAR
pointer to the caller's stack word. Compiler prologues and saved registers
are intentionally absent. The set/clear routines have no asserted return
value because the original callers do not consume one.

## Evidence and remaining limits

- `gatemain-flags-decompile` checks all eight valid types, missing objects,
  property applicability, 16-bit aliasing, high bit indices, read/set/clear,
  unaffected neighboring bits and wrapped FAR offsets on synthetic memory.
- `gatemain-flags-decompile-binary` checks original entry/exit boundaries,
  exact local call displacements, source-visible branch signedness, the
  complete jump table and the three distinct bit operations against the
  pinned original `GATEMAIN.EXE` SHA-256. It skips without the game binary.
- The existing native/HLE object-flag replay and `object-flags` C++ test
  independently establish the original game-state model
  (`docs/engine-boundary.md`, `docs/engine-services.md`).

**Not yet proven:** direct entry/return comparison of the reconstructed C
against the original DOS guest for each function, particularly on malformed
structures, and 16-bit compiler binary equivalence. The five tracker entries
stay `analyzed`, not `ported` or `verified`, until differential testing is
available. Next source-first candidates are the nearby native relationship
services `exe:0x254a..0x266f` and object-state accessors `exe:0x2986+`.
