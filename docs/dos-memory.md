# Conventional-memory arena

`DosMemory` models paragraph-sized blocks between PSP `0ff0` and the exclusive
conventional-memory limit `a000`. Initially the process owns that entire span,
matching the maximum allocation requested by the supplied MZ headers. Each
block reserves a preceding one-paragraph memory-control block (MCB).

The host owns arena metadata and serializes MCB headers into guest memory:

| MCB offset | Meaning |
| --- | --- |
| `00` | `M` for a middle block, `Z` for the terminal block |
| `01` | Owner PSP, little-endian; zero means free |
| `03` | Data size in paragraphs, excluding the MCB |

The implementation provides first-fit allocation, release, and in-place resize.
Allocation coalesces neighboring free blocks. Shrinking creates a free block;
growing consumes only directly adjacent free memory and never crosses an
allocated neighbor. A failed enlargement retains the largest attainable size,
matching the DOS 5 behavior used by this probe.

DOS calls `48h`, `49h`, and `4Ah` translate these results into carry and register
outputs. Insufficient memory returns error 8 and the available size in BX;
invalid block addresses return error 9. Allocation returns the data segment in
AX. Successful release and resize leave AX unchanged.

This is a conventional-memory subset, not a full DOS allocator: there are no
upper-memory blocks or selectable allocation strategies, and direct guest edits
to MCB metadata are not imported into the host arena. Process execution and
ownership transfer are not implemented yet.

Tests cover neighbor protection, failed enlargement, fragmentation/coalescing,
zero-size allocation, invalid blocks, and MCB layout. Synthetic guest tests cover
resize/allocation register results and failure carry semantics.

The implementation was checked against the control flow of
[DOSBox Staging's memory manager](https://github.com/dosbox-staging/dosbox-staging/blob/main/src/dos/dos_memory.cpp).
Differential tests against an executing DOS reference remain future work.

## List-of-lists memory-chain field

DOS `AH=52h` returns `ES:BX = 0800:0020`. The word at `ES:[BX-2]` points to the
first live MCB, including the environment allocation. This is the only
list-of-lists field implemented so far; it is not a full DOS kernel structure.
The information block lies below the process/environment arena.

The original Escape-skip path dereferences that word at `12a5:42c6`, walks the
MCB chain by size/type, frees process-owned blocks except the PSP and handoff
scratch allocation, then resizes the retained PSP allocation. It now reaches
`AH=1Ah` disk-transfer-address setup. Synthetic guests check the pointer and
walk the actual serialized chain before and after shrinking an allocation.
