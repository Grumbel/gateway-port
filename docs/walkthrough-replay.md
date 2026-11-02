# Long-form native walkthrough replay (bundle 098)

The original Gateway runs inside `gateway-probe` with a reviewed sequence of
BIOS keys. Unlike the first-room `--scenario puzzle` experiments, this input
format supports **more than 48 commands**, up to **100,000 timed keys over one
hour of emulated time**. It can drive additional original story code for RTLink
call census, object-state snapshots, exit traces and Ghidra target selection.

It is **not a verified start-to-finish autoplay yet**. Current native proof is
the seven-command opening below. Completing a full route requires interpreting
menu interactions, waiting for story events, choosing dynamic values, and
checking the resulting state. In particular, merely injecting every key does
not prove the game accepted every command.

## Walkthrough sources and input provenance

The following publicly available guides are useful, but **none is committed to
this repository**; their text is third-party content:

- [Complete solution and points list, 1600/1600](https://www.scribd.com/document/304568766/Frederik-Pohl-s-Gateway): a detailed command transcript starting at `open drawer`, interspersed with `Type` / `Press` UI instructions and score markers. Its indexed text appears to include duplicate or interleaved image text, so **review any extracted sequence**.
- [Harmen de Weerd, Gateway walkthrough](https://www.harmendeweerd.nl/frederik-pohls-gateway/): a compact start-to-finish `>`-style walkthrough with explicitly noted waiting and variable input.
- [Walkthrough King](https://www.walkthroughking.com/text/gateway.aspx): a prose cross-check for puzzle ordering.
- [Original hint booklet](https://mocagh.org/legend/gateway1-hintbook.pdf): contemporary background for rooms, puzzles and scoring.

Use an independently obtained **plain-text** copy of the first guide, then
convert it into an ignored `build/` file for editing:

```sh
python3 tools/run_walkthrough.py convert /path/to/your-guide.txt build/full-gateway.walk
```

The conservative converter only recognizes lines beginning `}` or `>` and
explicit `*** Type: ... ***` / `*** Press: ... ***` annotations. A simple
`Type` becomes raw keyboard text **without** Enter. Comma-separated menu
notation and every `Press` instruction become `MANUAL`: commas may mean
several successive PV/menu choices, and a screen button may need a mouse
click. Unmarked prose about waiting, dynamic passwords and special controls
is **not automatically recovered**. Inspect the original document, remove
duplicate text, encode the timing/choices, and check missing steps. A converted
script is **untrusted input, not a validated playthrough**.

## Reviewable `.walk` directives

| Directive | Meaning |
| --- | --- |
| `COMMAND open drawer` | Type ASCII parser text at 30 ms/key; then Enter |
| `TYPE 1` | Type ASCII only, without Enter (e.g. a menu digit) |
| `KEY space` | One key: `space`, `enter`, `escape`, or arrow names |
| `KEY 0x4800` | Raw 16-bit BIOS code (hex); equivalent to Up |
| `WAIT_MS 7000` | Add 7,000 emulated milliseconds before the next action |
| `LABEL got_book` | Named prefix boundary selected with `--through` |
| `ASSERT_LOCATION 123 d3` | Assert *final snapshot* object `0x123` is at `0xd3` |
| `MANUAL click screen button` | Stop before injecting an unverified UI step |

Blank lines and full-line `#` comments are accepted; input strings must be
printable ASCII of length 1–64. An action starts every 3,000 guest ms by
default, adjusted to prevent key overlap. The first user-scripted action starts
at 18,000 ms; the recorded startup sequence sends three splash-space keys, an
initial `look` command, and three more spaces. This is the same bootstrap as
`tools/story_scenarios.py`'s bounded puzzle scenario.

`ASSERT_LOCATION` applies to the **last selected input prefix only**, not to
earlier intermediate states in a full replay. To assert progress at each
milestone, replay the prefix ending at its label separately. This costs extra
simulation but avoids falsely claiming in-flight checks were made. The
snapshot inspector requires exactly 734 original Gateway object records; an
unrecognized object table aborts assertion checking.

## Reproduce the verified opening

```sh
cmake --build build --target gateway-probe
python3 tools/run_walkthrough.py run tests/fixtures/gateway-opening.walk \
  --probe build/gateway-probe --data ../gateway-dos \
  --output build/walkthrough-opening --verify-overlays --trace-navigation

# Replay only through TAKE BOOK, asserting book location at the player (d3).
python3 tools/run_walkthrough.py run tests/fixtures/gateway-opening.walk \
  --probe build/gateway-probe --data ../gateway-dos \
  --through book_taken --output build/walkthrough-book-taken

# The 55-action, 183-second native Beach VR → Casino route.
python3 tools/run_walkthrough.py run tests/fixtures/gateway-beach-casino.walk \
  --probe build/gateway-probe --data ../gateway-dos \
  --output build/walkthrough-beach-casino --verify-overlays --trace-navigation

# Inspect a converted prefix without original game files or execution.
python3 tools/run_walkthrough.py run build/full-gateway.walk \
  --max-actions 10 --plan-only
```

The driver writes an ignored `replay.keys` input, plus `native.trace`,
`native.rtlink-calls`, `native.{bin,regs,ppm}`, `native.dos` and `native.out`.
`--trace-target SECTION:CS:IP` additionally captures bounded trampoline
entry/return evidence; `--trace-navigation` enables known resident exit and
location hooks. For focused native mutation evidence, add
`--trace-object-writes OBJECTHEX` and/or `--trace-byte-write LINEARHEX`
(up to 16 addresses within 64 bytes). Object watch mode enables the
independently checked state-service ABI, **not replacement/HLE**.
All addresses are default-load native **linear** hook
addresses, not `GATEMAIN.OVL` file offsets. Use
[`inventory_native_handlers.py`](../tools/inventory_native_handlers.py),
[`handler-evidence.md`](handler-evidence.md), and the Ghidra imports to
correlate new section-qualified targets with handler offsets.

To extend a reviewed route beyond 48 commands, either increase the number of
`COMMAND`/`TYPE`/`KEY` lines or run a checked prefix with `--max-actions N`.
The duration limit is one guest hour **per replay**. For longer routes the
runner needs explicit DOS save/restore or a further bounded-time extension;
the current runner does not implement checkpoint save/restore.

### Lower-level probe input

The driver compiles its actions to the native probe's opt-in `--key-script`:

```text
# decimal emulated milliseconds : 16-bit hex BIOS key
18000:006f
18030:0070
18060:0065
18090:006e
```

Records must have strictly increasing timestamps and no surrounding
whitespace, except whitespace before a `#` comment. The file must contain
1–100,000 records, and the last key must be *strictly before* `--milliseconds`.
`--key-script` cannot be combined with `--key-at`. The ordinary `--key-at`
mode keeps the old 180,000-ms limit; durations above that require an explicit
script. The probe stops at the requested time or earlier DOS termination; the
driver checks the count of delivered keys and fails on early termination.

## Native evidence (original fingerprinted DOS distribution)

On 2026-11-02, using `GATE.EXE` and the original DOS game data external to
the repository:

| Selected prefix | Guest time | BIOS keys delivered | Validated RTLink calls | Distinct targets | State |
| --- | ---: | ---: | ---: | ---: | --- |
| Through `book_taken` (3 actions) | 27,000 ms | 38 | 5,434 | 38 | Book `0x123` at player `0xd3` |
| Full checked opening (7 actions) | 39,000 ms | 76 | 18,364 | 42 | Book `0x123` back in room `0xee` |
| Beach VR → Casino, plus observation tail (55 actions) | 183,000 ms | 491 | 439,760 | 70 | Player `0x0d3` in Pedroza Lounge `0x0f6`, pin `0x2a9` held by player |

The seven actions were `open drawer`, `look`, `take book`, `inventory`,
`drop book`, `close drawer`, `look`. With `--verify-overlays`, the full run
validated **181 original RTLink section loads**, encompassing **9 section
IDs**. These are **observed execution counts**, not a measure of how much of
the whole story has been understood; repeated calls to engine helpers dominate
the census.

The 55-action fixture [`gateway-beach-casino.walk`](../tests/fixtures/gateway-beach-casino.walk)
faithfully reproduces the 39-action **native-only** Beach/pin/Casino route
from [beach-vr-replay.md](beach-vr-replay.md), then adds 16 LOOK/INVENTORY
observations to cross the former duration/input limits. Its original guest
state reports Beach-success byte **1**, bartender drink counter **5**, and
membership pin carried by the player. The native loader validated **860
RTLink section loads in 18 sections**. Against the exported original object
handler and callback inventories, the replay reached **19/696 handler entries**
and **2/44 room callbacks** (the first-room seven-command replay reached only
5 handlers and no callbacks). Some invoked RTLink targets are shared engine
services rather than story handlers; notably one section-0 helper was invoked
438,271 times. Those repetitions are not 438,271 distinct game behaviors.

To reproduce the handler mapping against already prepared Ghidra data:

```sh
python3 tools/inventory_native_handlers.py build/ghidra-objects.json \
  build/ghidra/ghidra-plan.json --callbacks-json build/room-callbacks.json \
  --rtlink-calls build/walkthrough-beach-casino/native.rtlink-calls
```

Use the new long replay first to discover novel executed handlers and
callbacks, then spend decompilation effort on their section-qualified OVL
entries. It enables coverage experiments on much longer routes once
source-specific menus, timers and dynamic inputs are transcribed.
