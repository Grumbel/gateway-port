<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
-->

# Gateway native engine-service catalogue

This is the §3.5 catalogue under construction. A routine is considered
*analyzed* only after its original ABI, observed global effects and game
meaning are corroborated by disassembly and native traces. A static call
count alone is **not** evidence for a function's semantics or calling convention.

## Native call census (provisional)

`tools/rank_engine_services.py` consumes the Ghidra instruction addresses but
does **not** trust Ghidra's resolved far-call destinations. It decodes each
CALL from fingerprinted original code, validates the ten-byte RTLink stubs,
and resolves resident far-code segments by matching independent original
code windows against a live native RAM snapshot. This matters because the
same linked far segment may contain code from both `GATEMAIN.EXE` and
`GATE.RTL`; the simple `MZ header + segment*16 + offset` formula is wrong.

From the 2026-11-03 objects replay, 10,178 Ghidra CALL sites were examined,
and 857 still lack a verified source mapping. Eighteen destinations are used
by at least 20 **distinct native object-handler functions**:

| Original code identity | Object handlers | Actions | Status / known name |
| --- | ---: | ---: | --- |
| `rtl:0x0169f4` | 600 | 170 | External RTL, uncatalogued |
| `ovl:0x00b1e0` (section 4) | 186 | 108 | Service, unnamed |
| `exe:0x00105e` | 185 | 67 | Unclassified |
| `exe:0x00254a` | 157 | 15 | `gw_native_relation_contains` |
| `exe:0x0028e4` | 153 | 46 | `gw_test_object_flag` |
| `ovl:0x00c830` (section 4) | 147 | 17 | `gw_relocate_child` |
| `exe:0x00291e` | 87 | 3 | `gw_set_object_flag` |
| `exe:0x003e9c` | 74 | 13 | Not yet tracked |
| `rtl:0x00e284` | 70 | 0 | External RTL, uncatalogued |
| `exe:0x002603` | 68 | 11 | Unclassified |
| `rtl:0x0169db` | 61 | 7 | External RTL, uncatalogued |
| `exe:0x006557` | 57 | 1 | Unclassified |
| `exe:0x005c30` | 53 | 1 | Unclassified |
| `exe:0x002951` | 51 | 3 | `gw_clear_object_flag` |
| `exe:0x00259c` | 31 | 8 | Unclassified |
| `exe:0x0060cf` | 30 | 8 | `gw_room_exit_dispatch` |
| `exe:0x003f85` | 29 | 3 | Unclassified |
| `exe:0x005bd1` | 23 | 0 | Unclassified |

The `rtl:` identities are file offsets into the original `GATE.RTL`, not
runtime addresses. The current `decomp/functions.tsv` permits only `exe` and
`ovl`; external RTL routines are recorded here until their role is established.
Call counts are *static caller coverage*, not execution frequencies. The
generated per-callee evidence, including native match lengths and segment
support, remains in ignored `build/ghidra-shapes/native-service-fanout.json`.

## Analyzed services

Not populated yet. The first cluster is the object-property query/set/clear
services, for which prior native traces and C++ HLE tests exist. Each entry
will specify original offset, native calling convention, argument and return
registers, stack cleanup, guest-memory side effects, call-site and trace
evidence, and unresolved questions before gaining `state=analyzed`.
