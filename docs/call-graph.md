<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
-->

# Static call graph: engine and story code

Investigated 2026-11-13 from the NASM base in [disassembly.md](disassembly.md)
and the handler inventory in [native-handler-coverage.md](native-handler-coverage.md).
This is a static result: no trace confirms the individual edges, and edges
through function pointers are missing.

## Reproduce

```sh
python3 tools/disassemble_gateway.py ../gateway-dos/GATEMAIN.EXE \
    ../gateway-dos/GATEMAIN.OVL --rtl ../gateway-dos/GATE.RTL \
    --manifest build/handler-coverage/rtlink-manifest.json \
    --output build/disasm --verify
python3 tools/report_callgraph.py build/disasm/callgraph.json \
    --inventory build/handler-coverage/inventory.json \
    --actions build/handler-coverage/actions.json \
    --section 38 --json build/disasm/classification.json
```

`callgraph.json` lists every decoded function (body found by following
branches and switch tables, not calls) and every near, far, tail and
trampoline edge with its call sites. A resident RTLink stub
(`CALL gate; JMP FAR x; DW sid`) is recorded as `trampoline_to` its section
target; the report resolves calls through stubs to that target.

## Method

Object handlers (694 decoded of 696), room-exit callbacks (44) and action
handlers (191) are reached only through data tables, so they have no static
callers. They are roots of their own kind. The fourth kind, *engine*, is the
MZ entry plus every resident function without a static caller (119): the
RTLink start-up patches its jump into the C start-up at run time
(`jmp far [cs:4]`), and the engine calls more code through pointers. Caller-less
RTLink stubs are excluded, because the handler tables point at them.

Each walk stops at the roots of the other kinds, so a story handler doesn't
become engine code just because the engine dispatches it. A function's class is
the set of root kinds that reach it. **Story-private** means reached only from
object handlers and/or room callbacks.

## Result

| Class | Functions | Bytes |
| --- | ---: | ---: |
| object (story-private) | 819 | 269,768 |
| shared by all four kinds (CRT, text, object model) | 383 | 66,863 |
| engine only | 218 | 65,255 |
| action only (verbs) | 244 | 38,196 |
| unreached (pointer-only entries, misdecoded seeds) | 284 | 28,396 |
| other mixes | 232 | 39,589 |

Sorted by location:

- **Resident** (`GATEMAIN.EXE` + `GATE.RTL`, 130,520 bytes): engine and shared
  services; only 3,688 bytes are story-private.
- **Sections 0–8**: engine overlays. Sections 5–7 hold almost only action
  (verb) handlers; section 4 holds object-model services
  (`gw_format_object_phrase`, `gw_relocate_child`); sections 1 and 52 are
  reached by none of the roots.
- **Sections 9–57** (except 52): story. In all overlays together, 272,120 bytes
  are story-private and 103,822 bytes are not, mostly sections 0–8. Story
  sections contain small amounts of shared code (for example `ovl:0x076223` in
  section 56, used by 79 handlers).

So the engine and story are separated by section, apart from section 0 and some
helpers in story sections. The interface is narrow: story-private code
directly calls **169** functions outside itself. Ranked by the number of
distinct handlers that use them, the top 10 account for 51 % of all uses,
the top 25 for 77 %, and the top 50 for 90 %. The head of the list matches
the Ghidra-based census in [engine-services.md](engine-services.md): the RTL
formatter `rtl:0x0169f4`, the relation/flag/relocation services, action-context
dispatch, timed events and score.

"Users" in the report counts handlers transitively: a story helper that 80
handlers share makes each of its callees count 80 users. "Sect" and "sites"
give the direct view (distinct calling sections and call instructions).

## Section 38 (Your Quarters)

Section 38 holds 28 object handlers: Your Quarters (handler 233, entry
`ovl:0x0514b5`), Heecheetown, and the objects in the quarters (desk, drawer,
book, bed, locker, vent, PV slot, …). Of its 47 functions, 30 are
story-private, `ovl:0x053729` is also reached from an action handler, and 16
(`ovl:0x053019`–`ovl:0x053373`) have no static caller; nothing in the code loads
their offsets as immediates. They're probably entries of a data table (region
or picture conditions are candidates). The section calls 57 outside functions,
which form the first service set the lifter has to handle.

## Limits

- Calls through registers or memory (`call far [bx]`, 48 sites) and the 15
  indirect jumps outside switch tables add no edges.
- Engine roots include every caller-less resident function, so code that is
  only data decoded as code by a bad seed can enlarge the engine class.
- The real C `_main` isn't identified; the runtime address `1000:04d5` in
  [engine-boundary.md](engine-boundary.md) is not a linked offset.
