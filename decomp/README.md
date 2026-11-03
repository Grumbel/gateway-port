# Recovered native-code inventory

`functions.tsv` tracks every known native routine of Frederik Pohl's Gateway
and how far it has been recovered. It is maintained with
`tools/decomp_status.py` and follows
[the decompilation plan](../docs/decompilation-plan.md).

Rows are keyed by the routine's **original file offset**: `exe` rows are
offsets in `GATEMAIN.EXE`, `ovl` rows are offsets in `GATEMAIN.OVL`, plus the
RTLink section that loads them. Runtime segment addresses differ between
sections and launches and are never used as identities.

| Column | Meaning |
| --- | --- |
| `file`, `offset`, `section` | Identity, as above |
| `size` | Ghidra function body bytes; empty if Ghidra has no function there yet |
| `kind` | `service` (engine routine), `action` (verb handler from the 195-entry action table), `handler` (object/event handler), `callback` (room-exit callback), `library`, `rtlink`, `unclassified` |
| `handler`, `objects` | Object handler ID (1–696) and the native object IDs that use it |
| `callback` | Room-exit callback ID |
| `action` | Action IDs dispatched to this routine |
| `name` | Project-chosen C identifier; never game text |
| `state` | `unknown` → `named` → `analyzed` → `ported` → `verified`, or `blocked`/`library`/`data`/`dead` |
| `shape` | Plan step 3.4: `formulaic`, `direct-memory` or `complex` |
| `module` | `src/story/<name>.cpp` holding the C++ reimplementation |
| `test` | Differential test that verifies it |
| `notes` | Evidence: required for `blocked`, `data` and `dead` |

The tracker contains addresses, sizes, IDs and project names only. Never add
original code bytes, Ghidra pseudocode, or game text such as object names
or messages. Files in this directory and in `src/story/` are
`LicenseRef-Gateway-Derived` (see `LICENSES/`).
