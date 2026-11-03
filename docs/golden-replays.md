# Deterministic Gateway golden replays (§3.6)

The native `gateway-probe` already captures one 1 MiB RAM image and CPU register
file at each reviewed `LABEL` in a walkthrough. `tools/run_golden.py` adds
checkpoints after every tenth **`COMMAND`** (not every key or action), and
at the end of every campaign stage. It compares hashes of guest-owned memory
in playback order. Checkpoints name the stage, label, action ordinal, command
ordinal and guest millisecond; original command text is not stored in hashes.

## Run on NixOS

Build first (`gateway-build` writes to `/tmp/gateway-port-build/`), then run
the complete suite on an **isolated Xvfb** display:

```sh
gateway-build
xvfb-run -a python3 tools/run_golden.py run \
  tests/fixtures/gateway-campaign.json \
  --probe /tmp/gateway-port-build/gateway-probe \
  --data ../gateway-dos \
  --output build/golden/first-verified-run \
  --every 10 \
  --write-baseline build/golden/native-baseline.json
```

The runner executes **two independent nine-stage native campaigns**, each
with its own DOS save directory. Each run uses the existing reviewed save/
restore transitions and intermediate location assertions. It writes just
hash manifests to `build/golden/first-verified-run/run-{a,b}.json` and checks
their equality, stopping at the **first** divergent stage and checkpoint.
The optional `--write-baseline` is only honored if both runs agree. It is
safe to cancel or rerun with a *new* `--output`; existing run directories
are intentionally not overwritten.

The hash manifest also records the SHA-256 of each stage's **verified native
saved game**. A mismatch fails at that stage's `save` boundary, even if all
of its RAM checkpoints agree. This catches divergent save/restore handoffs
before they affect the next stage. When comparing older manifests without
save hashes, supply `--left-campaign` and `--right-campaign` to recover
the original recorded save hashes from local stage metadata without replay.

Golden replay mode enables `gateway-probe --deterministic-clock` automatically.
The virtual DOS date/time starts at **1992-01-01 12:00:00 UTC** and advances
only by guest emulation time, even when host runs start minutes apart or use
different timezones. This is deliberate: an initial independent-host replay
on 2026-11-03 diverged at office checkpoint `initial_items`, first byte
`0x55ae6`, and the original probe initialized its DOS clock from wall time.
The clock is a confirmed nondeterministic input, **not yet a proven sole
cause** of the RAM difference. Normal interactive execution retains the
host-local clock; a direct probe replay can opt in via `--deterministic-clock`.
Save slots created in the writable DOS state layer also get deterministic
**guest-visible directory timestamps** in this mode. The host files are not
retimestamped: only their DOS `findfirst`/`findnext` date/time fields are
normalized, because host creation time would otherwise leak across stages.
Original game assets keep their own directory metadata.

The first subsequent **full** fixed-clock campaign matched through
`armory_class` but diverged at `bar_nubar/golden-command-000010` (the first
`wait` following `drink`), after 10 commands; the two runs also made 13
different RTLink calls in that stage. The `office`-only fixed-clock campaign
matched all nine checkpoints. The cause of the later mismatch is not yet
established. The subsequent offline comparison confirmed **different CPU
CS:IP and SS:SP**, but **identical raw state for all 734 objects and identical
stage-end original saves**. This is evidence of different in-flight callbacks
or interrupt stack depths at the timed checkpoints, not (so far) divergent
persisted story state. Do not mask the changed RAM pages without proving they
are volatile; the strict full-RAM golden comparison must remain meaningful.

When the first mismatch is in an early stage, rerun *only* the affected prefix
while diagnosing it; this avoids replaying the full campaign each time:

```sh
xvfb-run -a python3 tools/run_golden.py run \
  tests/fixtures/gateway-campaign.json \
  --probe /tmp/gateway-port-build/gateway-probe \
  --data ../gateway-dos \
  --output build/golden/office-fixed-clock \
  --through-stage office
```

This still makes two independent native runs and compares all `office`
checkpoints, but does **not** establish a full-campaign baseline. Choose a
fresh output path after any failed attempt; prior RAM dumps are kept for
diagnosis. First-mismatch output includes the number of changed unmasked
bytes and the five most affected 4-KiB pages (addresses and counts only).

To investigate a **late** stage quickly, reuse the preceding stage's verified
original save as the starting point for *each independent* run. The runner
checks the source save hash and stage/script identity, then copies the save
into two separate writable state trees; it does not replay earlier stages or
reuse mutable state between runs. For example, after the full replay above:

```sh
xvfb-run -a python3 tools/run_golden.py run \
  tests/fixtures/gateway-campaign.json \
  --probe /tmp/gateway-port-build/gateway-probe \
  --data ../gateway-dos \
  --from-stage bar_nubar \
  --through-stage bar_nubar \
  --seed-from build/golden/full-fixed-clock/run-a \
  --output build/golden/bar-from-verified-save \
  --every 5
```

This isolates the bar's commands and increases checkpoint frequency from
every tenth to every fifth command. It cannot serve as a full-campaign
baseline: only `bar_nubar` is hashed. A missing, modified or stale seed save
is rejected; the original save remains under ignored `build/` paths.

The first seeded `bar_nubar` retry **matched at all 16 checkpoints** in two
independent runs, with 40,265 RTLink calls in each. The preceding full
campaign instead recorded 40,272 and 40,259 calls in the two bar runs. This
narrows the issue, but one matched pair does not prove determinism across
starts. The original full campaign used independently produced `armory_class`
saves, whereas the seeded retry copied one saved game into both runs. The host
compared the original source saves:

```sh
sha256sum build/golden/full-fixed-clock/run-{a,b}/armory_class/state/GATE004.SAV
```

**Both hashes matched** (`2644077990e90e998e62b1c0dda650581a592325ad39921acd0f505e89251c82`).
Different prior saved-game bytes are therefore not the cause. The full and
seeded bar stages also produced different RTLink counts while using the same
source save, so the next step is to locate the first differing call during
bar execution, not to mask changing memory.

### Diagnose intermittent RTLink differences

The native probe's optional `--trace-rtlink-sequence FILE` writes one line for
**every validated trampoline invocation**, in execution order: ordinal, guest
millisecond, RTLink section, target `CS:IP` and original caller `CS:IP`.
These are code identities only; no game text or executable bytes are logged.
The golden runner propagates `--trace-rtlink-sequence` through both campaign
and walkthrough runners and writes one `native.rtlink-sequence` per stage.
As tracing can slow emulation and produce large files, it is opt-in.

For an intermittent failure, run more than two independent replays from the
*same verified save* in one invocation:

```sh
gateway-build
xvfb-run -a python3 tools/run_golden.py run \
  tests/fixtures/gateway-campaign.json \
  --probe /tmp/gateway-port-build/gateway-probe \
  --data ../gateway-dos \
  --from-stage bar_nubar --through-stage bar_nubar \
  --seed-from build/golden/full-fixed-clock/run-a \
  --output build/golden/bar-rtlink-diagnostic \
  --every 5 --runs 4 --trace-rtlink-sequence
```

Up to eight independent runs (`--runs 2..8`, default two) are supported.
Every run gets a new private save directory; the runner checks each subsequent
run against `run-a` immediately, stopping at the first mismatch. The error
now also reports the first *different RTLink target/caller*, the two guest
millisecond timestamps and the preceding calls. If code-call order agrees,
it instead shows the first timestamp drift. This tells us whether the RAM
mismatch accompanies a new code path or a pure timing difference. The
sequence traces remain in the ignored output directory for further analysis.

If the first group of four happens to match, use a fresh output path and
repeat; one passing group is evidence only for those runs. No new exclusion
has been added to the strict RAM oracle.

Compare subsequent native builds or a replacement-enabled probe against the
same baseline:

```sh
xvfb-run -a python3 tools/run_golden.py run \
  tests/fixtures/gateway-campaign.json \
  --probe /tmp/gateway-port-build/gateway-probe \
  --data ../gateway-dos \
  --output build/golden/second-verified-run \
  --baseline build/golden/native-baseline.json
```

To hash an existing *golden-enabled* campaign without rerunning it:

```sh
python3 tools/run_golden.py capture \
  tests/fixtures/gateway-campaign.json \
  --campaign build/golden/first-verified-run/run-a \
  --every 10 --output build/golden/recovered-hashes.json
python3 tools/run_golden.py compare \
  build/golden/first-verified-run/run-a.json \
  build/golden/first-verified-run/run-b.json \
  --left-campaign build/golden/first-verified-run/run-a \
  --right-campaign build/golden/first-verified-run/run-b
```

For an already-recorded *failed* full campaign, use `--all-checkpoints` to
see whether the mismatch continues or disappears later. The first mismatch
also reports CPU register differences (`CS:IP`, stack, data segments and
flags), changes among the 734 native object records, and whether each stage's
original saved-game bytes have identical SHA-256 hashes. The all-checkpoints
listing additionally displays stage-end save agreement for each divergent
stage:

```sh
python3 tools/run_golden.py compare \
  build/golden/full-fixed-clock/run-a.json \
  build/golden/full-fixed-clock/run-b.json \
  --left-campaign build/golden/full-fixed-clock/run-a \
  --right-campaign build/golden/full-fixed-clock/run-b \
  --all-checkpoints
```

This reads the existing local snapshots and does **not** need to rebuild or
replay the game. The diagnostic counts differing RAM bytes only outside the
**union** of the two excluded live-stack ranges. When `SS:SP` differs, this
avoids misidentifying an extra frame in one run as retained-memory corruption.
The strict golden comparison is unchanged: different `SS:SP`, excluded
ranges or conventional RAM hashes still fail. Only IDs and difference counts
are printed, never original game bytes or object contents. The object-record
check requires a verified 734-object table at `DS` or `ES` in both snapshots;
if either CPU snapshot is in another segment it reports that fact rather than
guessing.

The `--probe-arg=FLAG` option forwards a backend flag to each probe invocation
for future opt-in C++ replacements (§3.8). This is **not** a replacement
registry: no replacement flag is supported by the current native probe yet.

## Canonical RAM regions

The input is the 1,048,576-byte native `.bin`. The SHA-256 covers precisely
linear addresses **`0x00000` through `0x9ffff`** (640 KiB conventional RAM).
The following byte ranges are zeroed *before* hashing, with exact start/end
addresses retained in each manifest:

| Exclusion | Address range | Rationale |
| --- | --- | --- |
| BIOS clock | `0x046c..0x046f` | The four-byte BIOS Data Area tick count is timekeeping state, not story state. |
| Live stack tail | `SS*16 + SP` through `SS*16 + 0xffff`, clipped to `< 0xa0000` | Return addresses, transient arguments and stale frames should not affect story-state hashes. The `SS` and `SP` values and the exact excluded range must themselves match. |

**Do not exclude the entire SS segment.** Many 16-bit programs share DS and
SS, and doing so would hide real object-state mutations. Other proposed
volatile masks must be justified by observed independent-run divergence and
documented before adding them. IVT, PSP, DOS memory, game heap and the rest
of conventional memory remain hashed; VGA memory above `0xa0000` is outside
scope. A one-byte retained-memory mutation changes the hash.

Each manifest includes the reviewed campaign file SHA-256, per-stage script
SHA-256 and checkpoint schedule, so a script edit fails comparison instead of
silently comparing mismatched replays. Differences in stack bounds fail
comparison even if the remaining RAM bytes match. When two local memory
images are available, the reporter also prints the **first unmasked guest
linear address** that differs, never the original bytes.

Native RAM snapshots, DOS save files, screenshots and traces must stay in
ignored `build/` directories. Only reviewed **hash manifests and region
descriptions** may enter Git. No baseline is checked in until two real native
runs agree; synthetic unit tests exercise the deliberate corruption case but
do not establish native determinism.

## Acceptance remaining

1. Run the nine-stage suite twice in succession on NixOS and inspect any
   first mismatch before deciding whether the engine needs a timing fix.
2. Preserve a verified baseline JSON (hashes only) for future changes.
3. Once the replacement registry exists, enable a deliberately wrong
   replacement, confirm that the first divergence points to the expected
   command/checkpoint, then restore the correct implementation.

Until all three happen, §3.6 remains in progress rather than complete.
