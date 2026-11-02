# Long-form native walkthrough replay (bundles 098–099)

The original Gateway runs inside `gateway-probe` with a reviewed sequence of
BIOS keys. Unlike the first-room `--scenario puzzle` experiments, this input
format supports **more than 48 commands**, up to **100,000 timed keys over one
hour of emulated time**. It can drive additional original story code for RTLink
call census, object-state snapshots, exit traces and Ghidra target selection.

It is **not a verified start-to-finish autoplay yet**. Native proof now
includes the seven-command opening, the 55-action Beach VR/Casino route, and a
24-action PV/office/class/Blue Hell route with six checked native milestones.
Completing a full route requires interpreting other menu interactions,
waiting for story events, choosing dynamic values, and checking the resulting
state. Merely injecting every key does not prove the game accepted a command.

## Walkthrough sources and input provenance

The following publicly available guides are useful, but **none is committed to
this repository**; their text is third-party content:

- [Complete solution and points list, 1600/1600](https://www.scribd.com/document/304568766/Frederik-Pohl-s-Gateway): a detailed command transcript starting at `open drawer`, interspersed with `Type` / `Press` UI instructions and score markers. Its indexed text appears to include duplicate or interleaved image text, so **review any extracted sequence**.
- [Harmen de Weerd, Gateway walkthrough](https://www.harmendeweerd.nl/frederik-pohls-gateway/): a compact start-to-finish `>`-style walkthrough with explicitly noted waiting and variable input.
- [Walkthrough King](https://www.walkthroughking.com/text/gateway.aspx): a prose cross-check for puzzle ordering.
- [Original hint booklet](https://mocagh.org/legend/gateway1-hintbook.pdf): contemporary background for rooms, puzzles and scoring.

The first real native menu/office/class replay is documented separately in
[PV commset and T20](pv-commset.md).

Use an independently obtained **plain-text** copy of the first guide, then
convert it into an ignored `build/` file for editing:

```sh
python3 tools/run_walkthrough.py convert /path/to/your-guide.txt build/full-gateway.walk
```

If you locally downloaded the first guide's Scribd HTML page, use
`convert ... --scribd-html` to extract only its document element before
converting. The original guide and downloaded page stay outside Git. The
current page extraction yields **733 parser commands, 11 explicit yes/no
replies, eight single-key menu sequences, and 16 manual barriers**; these
numbers count extracted directives,
**not** verified gameplay. In particular, the guide's embedded prose and
generated passwords still need review.

The conservative converter recognizes lines beginning `}` or `>`, explicit
`*** Type: ... ***` / `*** Press: ... ***` annotations, and annotated
yes/no prompt answers. A simple
`Type` becomes raw keyboard text **without** Enter. Comma-separated lists of
**single alphanumeric menu keys** become `MENU_KEYS`, a convention confirmed
in the original [PV GUI](pv-commset.md). Other menu notation and every
`Press` instruction still become `MANUAL`: a screen button may need a mouse
click. Unmarked prose about waiting, dynamic passwords and special controls
is **not automatically recovered**. Inspect the original document, remove
duplicate text, encode the timing/choices, and check missing steps. A converted
script is **untrusted input, not a validated playthrough**.

## Reviewable `.walk` directives

| Directive | Meaning |
| --- | --- |
| `COMMAND open drawer` | Type ASCII parser text at 30 ms/key; then Enter |
| `ANSWER N` | Reply to a native yes/no dialog (N followed by Enter) |
| `TYPE 1` | Type ASCII only, without Enter (e.g. a menu digit) |
| `MENU_KEYS 1,N,N,0,0` | Five individual GUI menu keys, spaced like actions, no Enter |
| `MOUSE_CLICK 160 114` | Position and click the original DOS left mouse button (640-wide coordinate grid) |
| `KEY space` | One key: `space`, `enter`, `escape`, or arrow names |
| `KEY 0x4800` | Raw 16-bit BIOS code (hex); equivalent to Up |
| `WAIT_MS 7000` | Add 7,000 emulated milliseconds before the next action |
| `LABEL got_book` | Named boundary selected with `--through`, or native snapshot |
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
milestone, use `--check-labels`, which captures and validates all intermediate
assertions during **one native execution**, or replay a prefix with `--through`
for isolated experiments. Without `--check-labels`, the earlier assertions
are deliberately not checked during a full replay. The
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

`--capture-labels` records up to 32 native RAM/CPU/framebuffer snapshots at
named milestones (one millisecond before the next planned action). With
`--check-labels`, the replay also checks every `ASSERT_LOCATION` immediately
preceding each label. The underlying probe accepts `--dump-at MS:PREFIX`,
which logs each actual capture in `native.trace`; a guest exit before any
scheduled capture is an error. This makes intermittent PV screens and timed
story events inspectable without repeating every prefix from startup.

GUI-only choices can be scripted through the original DOS `INT 33h` driver,
not through story RAM writes. `MOUSE_CLICK X Y` emits a 100-ms left-button
press/release at the current action time; coordinates use the DOS 640-column
mouse grid and an unscaled Y row (for a 320×200 ship-panel framebuffer,
`MOUSE_CLICK 160 114` targets approximately visible pixel `(80,114)`). The
probe's direct `--mouse-click-at MS:X:Y` supports the same operation and
rejects overlapping clicks, unreachable releases, malformed coordinates and
more than 4,096 clicks. A synthetic native `INT 33h` program verifies the
button transition and coordinates in real guest memory.

The original game's [ship manual](https://www.freegameempire.com/games/Gateway-Frederik-Pohls/manual-pdf)
also documents shortcut keys `C` (course list), `G` (go), `R` (return),
`O` (orbit), `L` (land), `I` (info) and `V` (view). The first real cockpit
experiment selected a displayed course-code row with a DOS mouse click and
`TYPE G` began the original spacecraft departure animation. Full mission
arrival and sequential course-code selection still require native checks.

### Resume through the original DOS saved-game format

The original [Gateway manual](https://www.freegameempire.com/games/Gateway-Frederik-Pohls/manual-pdf)
documents `SAVE` and `RESTORE` as native parser commands. The original save
screen accepts a description up to 33 characters, followed by Enter. A
verified uninterrupted PV/office run executed:

```text
COMMAND save
TYPE OFFICE100
KEY enter
```

This wrote `GATE001.SAV`, **21,777 bytes**, with `OFFICE100` in its fixed-width
33-byte ASCII description header. From a **fresh DOS process** with that save
copied into its isolated writable state directory, the sequence

```text
COMMAND restore
TYPE OFFICE100
KEY enter
ASSERT_LOCATION d3 ec
ASSERT_LOCATION 123 0
ASSERT_LOCATION 130 d3
LABEL office_restored
```

restored the original player, book and DataMan relationships: **3/3 native
object assertions passed**. The runner now handles the file import and
post-save existence checks without copying DOS game binaries into Git:

```sh
# A reviewed script ending with the three save commands above:
python3 tools/run_walkthrough.py run build/save-office.walk \
  --data ../gateway-dos --output build/office-save --expect-save OFFICE100

# A second, fresh replay beginning with the restore commands above:
python3 tools/run_walkthrough.py run build/restore-office.walk \
  --data ../gateway-dos --output build/office-restored \
  --import-save build/office-save/state/GATE001.SAV --check-labels
```

`--import-save` only accepts a bounded original `GATE###.SAV` with a readable
description; it refuses to overwrite a different existing slot.
`--expect-save` demands a newly written named native save in that run.
Only `.SAV` files are imported, **not** the boot-time `RESTART.DAT`. Thus
the game, rather than an incomplete host-side emulator serialization, performs
the story-state restoration. These saved games cannot replace intermediate
CPU/framebuffer dumps for analysis, and their long-term compatibility with
other DOS distributions remains untested.

To extend a reviewed route beyond 48 commands, either increase the number of
`COMMAND`/`TYPE`/`KEY` lines or run a checked prefix with `--max-actions N`.
The duration limit is one guest hour **per replay**. Divide longer routes
into native-save stages, each including an explicit SAVE/RESTORE interaction.

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
