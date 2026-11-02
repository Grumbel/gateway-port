# Native Gateway PV commset, receptionist and ship-handling class (bundle 099)

This is a reproducible, **unmodified original-game** path beyond the first
room, recovered by correlating published gameplay guidance with native
Unicorn RAM/framebuffer checkpoints, the 734-object state table, and a fresh
Ghidra 11.4.2 RTLink import. All files under `build/` are generated and
ignored; the original DOS binaries remain external to the repository.

## The PV menu accepts individual raw keystrokes

The [1600-point command guide](https://www.scribd.com/document/304568766/Frederik-Pohl-s-Gateway)
uses `Type: 1,N,N,0,0` after inserting the debit card. The independently
written [Harmen de Weerd guide](https://www.harmendeweerd.nl/frederik-pohls-gateway/)
also has the same five entries; [Walkthrough King](https://www.walkthroughking.com/text/gateway.aspx)
confirms their purpose is to read new messages. Previously the converter left
this notation as an unresolved manual instruction.

The native replay now confirms that the commas are **separators between five
successive ASCII keys**, with *no Enter key* in between. Each selection can
be timed independently. These are not parser commands: the game temporarily
switches to an unrelated green-on-black NAT&T PV GUI. A normal `COMMAND 1`
would append Enter and is **not equivalent**.

| Native checkpoint | Guest time | PV state observed on the original framebuffer |
| --- | ---: | --- |
| After `PUT DEBIT CARD IN PV SLOT` | 23,999 ms | Quarters output ending in `MORE` |
| After raw `1` | 26,999 ms | First new message from Hector Gomez |
| After first raw `N` | 29,999 ms | Second new message from Thom Seldridge |
| After second raw `N` | 32,999 ms | Third new message from Terri Neilson |
| After first raw `0` | 35,999 ms | Main NAT&T menu (0 = end session) |
| After second raw `0` | 38,999 ms | Returns to the quarters, debit card with player |

In `tools/run_walkthrough.py`, use `MENU_KEYS 1,N,N,0,0`; internally it
schedules a single ASCII key every three guest seconds (configurable). The
converter emits `MENU_KEYS` only for **2–64 single ASCII alphanumeric
choices**. Multi-character entries, unusual punctuation and the published
guide's mouse `Press` instructions remain `MANUAL` review barriers. This
rule has only been checked in the PV system, not every mini-game or entry
field in the story.

The timeline is useful even before implementing adaptive input: the `MORE`
screen did *not* prevent the first raw `1` from reaching the PV interface.
Do not assume that a visible `MORE` means every pending BIOS key is lost.

## Book exchange at the Corporation Office Lobby

The replay then issues **OPEN DRAWER, TAKE BOOK, READ BOOK, EAST,
NORTHWEST, GIVE BOOK TO RECEPTIONIST**. The game itself prints the expected
exchange story and a **five-point score increase**. At 57,000 guest ms,
independent native object records confirm:

| Object | ID | Native final parent/location |
| --- | ---: | ---: |
| Player | `0x0d3` | Corporation Office Lobby `0x0ec` |
| Book | `0x123` | `0` (removed) |
| Debit card | `0x126` | Player `0x0d3` |
| DataMan | `0x130` | Player `0x0d3` |
| Receptionist | `0x0ff` | Lobby `0x0ec` |

The verified object entry for the receptionist is handler **250**, RTLink
**section 40**, file offset **`GATEMAIN.OVL:0x56f9f`**. Fresh Ghidra output
(`build/ghidra-099-office/`) recognizes the provisional function and its
event switch. Original 16-bit disassembly and native state agree on a bounded
**event-4 / action-code-`0x3c` / book-`0x123`** branch at file offsets
`0x570a9–0x57114`:

```cpp
// Partial translation ONLY: the already-admitted receptionist event branch.
if (event == 4 && native_action_code == 0x3c && target == 0x123) {
    text(/* F000:a83e, GATESTR segment 42 / index 62 */);
    move(0x130, 0x0ff, 0);   // DataMan -> receptionist, 1c24:13d5
    award_points(5);         // 048e:0a7e
    move(0x123, 0, 0);      // remove book, 1c24:13d5
    give_to_player(0x130);  // 048e:0ad6, terminal location verified
    return 1;
}
```

The source calls and arguments above are **original instructions**, not
invented addresses. A focused original RTLink entry/return trace now verifies
the **entire ordered sequence**, including its intermediate DataMan move:

| Guest time | Observed original native behavior |
| ---: | --- |
| 54,817 ms | Enter section-40 `4990:066f` with event-4 argument |
| 55,002 ms | DataMan `0x130` relation changes `0 → 0x0ff` (receptionist) |
| 55,025 ms | DataMan `0x130` relation changes `0x0ff → 0x0d3` (player) |
| 55,025 ms | Entry returns **AX=1**; book `0x123` location `0x0d3 → 0` |

The matched far-call ID is 1, with **six changed native object-record bytes**
across the player, book, and DataMan. The concrete `object-write` observations
show the transient reception-desk parent; this part is no longer inferred
solely from the original helper call. Reproduce with:

```sh
python3 tools/run_walkthrough.py run tests/fixtures/gateway-pv-office.walk \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" \
  --data ../gateway-dos --output build/receptionist-native \
  --check-labels --trace-target 40:4990:66f \
  --trace-object-writes 130 --verify-overlays
```

The full `GIVE` parser, reachability/admission predicates, score service,
other receptionist actions and text-rendering side effects remain native.
The bounded `StoryModel::apply_receptionist_book_exchange()` applies the
three ordered object-location effects, returning text token and point delta,
without claiming to replace those other systems.

### Ghidra workflow

Reproduce the selected original overlay entry with:

```sh
python3 tools/inspect_object_state.py build/walkthrough-pv-office/native \
  --names ../gateway-dos/OBJECT.DAT > build/ghidra-objects.json
python3 tools/run_ghidra.py \
  ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --overlay-tool "${GATEWAY_BUILD_DIR:-build}/gateway-overlay" \
  --objects-json build/ghidra-objects.json \
  --decompile-object 0xff --decompile-object 0x130 \
  --decompile-object 0x126 --decompile-object 0x123 \
  --decompile-object 0xec --output build/ghidra-office \
  --project-name GatewayOffice
```

The original test imported all **58 distinct overlay address spaces** and
**13,947 validated fixups**, then exported **37/37 selected contexts**.
Some Ghidra indirect-call and shared RAM references remain unresolved; C-like
output is provisional. For branch verification independent of Ghidra's
decompiler, disassemble the original `0x570a9–0x57114` bytes using
`tools/disassemble_story.py` and check the live object deltas.

## Ship-handling class: a time-dependent normal route

From the office, `SOUTHEAST, NORTH, DOWN, WEST` moves the original player to
Room T20 (`0x0f7`). `WAIT UNTIL 15:00` reaches the class start; four
ordinary `WAIT` commands advance the original story to the badge award.
After those waits, the original game reports a **ten-point increase** and
the native object table has:

- Player `0x0d3` in Room T20 `0x0f7`.
- Blue badge `0x132` directly carried by player `0x0d3`.

The intermediate screen snapshots distinguish the teacher's arrival,
presentation, invitation for questions, and the dismissal; after a later
six-wait tail the room is empty. This is original conditional story behavior
under normal inputs, not a patched timer or injected badge.

### Blue Hell Bar: scheduled original NPC appearance

From T20 the player goes **EAST, UP, SOUTH, EAST** to Blue Hell Bar
(`0x0f4`). In the earlier checkpoint the native `Thom Seldridge` object
(`0x100`) has no location (`0`). The original command
`WAIT UNTIL 20:01`, followed by `WAIT`, advances the encounter naturally:

| Native object | Before evening wait | After evening wait |
| --- | --- | --- |
| Player `0x0d3` | Bar `0x0f4` | Bar `0x0f4` |
| Thom `0x100` | `0` (absent) | `0x137` (Thom's seat) |
| Blue badge `0x132` | Player | Player |

The original framebuffer independently depicts Thom sitting at the bar and
the robot bartender bringing his drinks. The uninterrupted 24-input-action
replay produced **71 distinct section-qualified RTLink targets** and reached
**16 of 696 object handler entries**. This is *entry coverage*, not the
percentage of story logic translated into C++.

## Run and capture checkpoints in one pass

```sh
python3 tools/run_walkthrough.py run tests/fixtures/gateway-pv-office.walk \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" \
  --data ../gateway-dos --output build/walkthrough-pv-office \
  --check-labels --verify-overlays --trace-navigation

python3 tools/run_walkthrough.py run tests/fixtures/gateway-orientation.walk \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" \
  --data ../gateway-dos --output build/walkthrough-orientation \
  --check-labels --verify-overlays --trace-navigation

python3 tools/run_walkthrough.py run tests/fixtures/gateway-bar-evening.walk \
  --probe "${GATEWAY_BUILD_DIR:-build}/gateway-probe" \
  --data ../gateway-dos --output build/walkthrough-bar-evening \
  --check-labels --verify-overlays --trace-navigation
```

The optional `--capture-labels` records a full `checkpoint-LABEL.{bin,regs,ppm}`
at each named milestone without restarting from DOS for every prefix. The
opt-in `--check-labels` additionally evaluates the preceding
`ASSERT_LOCATION`s against **each real intermediate native object table**.
The probe's lower-level `--dump-at MS:PREFIX` offers the same behavior for
up to 32 explicitly timed screenshots and RAM snapshots per replay; it
rejects an unreachable timestamp instead of reporting a stale snapshot.

The same runner still checks final assertions separately, and normal
`--through LABEL` replays remain useful for controlled experiments.
No checkpoint is a resumable save file; they are read-only observations.
