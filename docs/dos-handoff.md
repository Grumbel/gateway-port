# Intro-to-main overlay handoff

Gateway's Escape path uses its C runtime's overlay replacement mechanism rather
than DOS load-and-run child EXEC. These observations apply to the original files
fingerprinted in [executable.md](executable.md).

1. `AH=52h` returns a list pointer whose word at `ES:[BX-2]` names the first MCB.
   The caller at `12a5:42c6` walks that chain, frees its owned allocations except
   the PSP and handoff scratch area, and resizes the remaining process block.
2. `AH=1Ah` sets the DTA to the retained PSP command-tail area. `AH=29h` parses
   the first two new command fields into default FCBs at PSP offsets `5ch/6ch`.
3. The runtime copies a trampoline high in conventional memory, switches stacks,
   and issues `AX=4b03` at `9fe0:0004` in the observed default run. `DS:DX` names
   `GATEMAIN.EXE`; `ES:BX` points to the load-segment/relocation-factor words.
4. The host loads and relocates only the MZ image. It leaves the existing PSP,
   environment, stack, and trampoline intact, invalidates translated code for
   the overwritten range, and returns to the caller with carry clear.
5. Original trampoline instructions set the new stack and entry registers and
   transfer control into the main executable. Its own RTLink startup runs next.

The overlay service accepts separate destination and relocation segments.
Successful calls clear AX/DX, matching the DOSBox reference behavior. Missing,
unreadable, malformed, and out-of-bounds images return DOS errors; failures do
not write a partial image. Normal child EXEC, COM overlays, and arbitrary DOS
kernel list-of-lists fields are outside this subset.

The original Escape replay now reaches main-program vocabulary/table loading,
then creation of the absent `RESTART.DAT` (`AX=3ca4`). That writable-state path
is independent of the intro handoff and remains unimplemented.

Tests cover list-pointer dereferencing and live MCB traversal, DTA round trips,
FCB parsing, independent overlay relocation, image bounds, malformed images,
and execution of replacement code at a previously translated address.

Reference behavior was checked against the primary DOSBox Staging
[DOS API](https://github.com/dosbox-staging/dosbox-staging/blob/main/src/dos/dos.cpp)
and [overlay loader](https://github.com/dosbox-staging/dosbox-staging/blob/main/src/dos/dos_execute.cpp).

## File handles while advancing the intro

The default DOS handle pool has twenty slots including standard handles.
Successful opens take the lowest free slot, and closing a file releases it for
reuse. Reopened standard slots are treated as regular files rather than consoles.
This matters when the intro opens and closes many picture/music files before the
main overlay load. Exhaustion returns DOS error 4 instead of allocating handles
beyond the default table. Extended DOS handle tables are not implemented yet.

## RTLink overlay reads and translated-code coherence

The initial `4B03h` image load is not the only executable replacement. RTLink
subsequently reads sections of `GATEMAIN.OVL` through ordinary DOS `AH=3Fh` into
previously executed addresses. Host-side Unicorn memory writes do not invalidate
its translated blocks. Every successful DOS read now invalidates translations
for the bytes it wrote, just as the initial overlay loader does.

A regression first executes a routine returning 1, loads raw replacement bytes
through DOS read, and calls it again; the correct result is 42. Without the cache
invalidation it returned 1. In the real game, stale blocks eventually returned
through incompatible stack layouts and ran into low DOS memory. With invalidation,
a bounded Escape replay reaches the first room, **Your Quarters**, with the scene,
interface, object lists, and initial description visible.

`--trace-blocks FILE` optionally records the last 256 guest basic blocks with
stack/frame registers. It is disabled by default and keeps diagnostics bounded.
