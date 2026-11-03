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
