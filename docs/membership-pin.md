# Membership pin: from the Beach VR to the Casino

This analysis began as a **native-code reconstruction** and now has a
successful original-game replay **through the Beach reward, actual pin
pickup and normal Casino entry**. It joins three
previously separate observations: the pin's starting location,
the Beach virtual-reality completion branch, and the tested Casino EAST gate.
Addresses below are **original, unmodified** `GATEMAIN.OVL` file offsets;
the file's RTLink sections are loaded into different overlay banks at runtime.

**Update:** The Beach-success, technician pin-drop, player pickup and
Casino EAST positive branches have all been exercised without intervention
in an original-game Unicorn replay.
See [beach-vr-replay.md](beach-vr-replay.md) for the scripted input, live
byte/record writes and limitations. The sections below retain the earlier
static-analysis provenance to distinguish evidence obtained at each stage.

## Native starting state and pin handler

The fingerprinted native snapshot exported by `inspect_object_state.py` shows:

| Object | Native ID | Initial relation/location | OVL entry | Section |
| --- | ---: | ---: | ---: | ---: |
| VR Terminal room | `0x29b` | Room | `0x66f54` | 49 |
| Chair | `0x2a0` | `0x29b` | `0x679d3` | 49 |
| Technician | `0x2a3` | `0x2a0` | `0x673de` | 49 |
| Uniform | `0x2a4` | `0x2a3` | `0x67da9` | 49 |
| Membership pin | `0x2a9` | **`0x2a3`** | **`0x67cf6`** | 49 |
| Reader | `0x2ab` | `0x2a3` | `0x680b7` | 49 |

The actual pin handler is `gw_object_2a9_membership_pin`, not a decoded
story-script opcode. Ghidra 11.4.2 decompiled its event-6 branch. The byte at
`[DS:9478]:e664` is compared to the following command IDs (only TAKE is
confirmed by prior input traces; other verb names remain unresolved):

| Command | Pin-handler behavior | Evidence |
| --- | --- | --- |
| `0x35` | If selection ID at `ES:055c` is `0xd4`, delegates to another routine | OVL `0x67d0b` onward |
| `0x3a` | Displays text token `f000:3843` describing the pin; returns AX=1 | OVL `0x67d20` onward |
| **`0x46`** | Checks `0157:000c` for direct relation `(pin 0x2a9, technician 0x2a3)`. If held, prints `f000:3842` (technician refuses to hand it over), returns AX=1; otherwise follows a property-9 check/set and returns AX=0 | OVL `0x67d2d` onward |

Both text tokens decode through `GATESTR.DAT` (resource group 14, indices
`0x42` and `0x43`). The previously verified TAKE action-dispatch behavior
applies only when the pin handler does **not** intercept the action; there is
now native TAKE-pin traces for both the *blocked* technician-owned state and
the *successful* pickup after the Beach reward (pin `0x29b → 0x0d3` at
117678 ms in the full replay).

## Beach VR success flag: the first half of the normal reward path

The Beach is room/object `0x2ae` in RTLink section **50**, and its frosted
glass is `0x2b4` (handler `0x69d63`). The original glass-handler code at
**`0x6a0fc–0x6a123`** checks byte `[word DS:9644]:78fa`, then executes:

1. If that byte is zero, call `048e:0a7e` with argument `0x0a`.
2. Store **1** at `[word DS:9644]:78fa` (`0x6a114–0x6a11d`).
3. Call `1c24:44d1` and jump back inside the containing handler.

Immediately before this code, the handler emits `f000:bc51`. That text
describes putting the frosted glass onto the scanner and a runaway cascade
of virtual drinks that breaks the simulation. The byte is now **verified as
the Beach-success flag**: the original replay observed the write from 0 to 1
at 109250 ms after five drink interactions. In the earlier native startup
snapshot this byte is **0** (the segment pointer `DS:9644` resolves to the
original `DS`, with the byte at offset `0x78fa`).

## Return from VR: the pin is placed in the real room

Section **51** has a coherent routine beginning at **`0x6d564`**, immediately
after a `retf`. Unlike an object handler, this is selected through the new
`--decompile-offset` option and is explicitly labeled a **provisional**
function entry. Its native instructions establish this order:

| File offset | Operation | Meaning / limitation |
| --- | --- | --- |
| `0x6d564` | Print text `f000:70b8` | Return from a virtual environment |
| `0x6d579–0x6d58d` | `0157:00c5(0x2a3,0x29b)` | Continue only if the technician is in the VR Terminal room; relation semantics were independently inspected |
| `0x6d590–0x6d59a` | Check `[word DS:969c]:78fa != 0` | Uses the same byte offset as the Beach handler, through another segment pointer |
| `0x6d59c–0x6d5ae` | `0157:000c(0x2a9,0x2a3)` | Direct check that the pin still belongs to the technician |
| `0x6d5b0` | Print text `f000:70b9` | Technician congratulates the player and flips over the promised pin |
| **`0x6d5c0–0x6d5d2`** | **`1c24:13d5(0x2a9,0x29b,0)`** | **Move pin from technician into VR Terminal room** (third argument is relation slot) |
| `0x6d5d3–0x6d5df` | `0157:0413(0x2a9,8)` | Clear property/flag `0x08` on the pin |

The destination `0x29b` is a room, **not the player `0xd3`**. The player must
remove the VR collar, stand up, and TAKE MEMBERSHIP PIN. The full native
replay confirms the original object setter changes the pin's relation
`0x29b → 0x0d3`; earlier controlled research bypassed these steps to test
the Casino callback independently before the complete route was known.

The later branch at `0x6d5e3` handles the non-award return paths, including
other VR-result state and messages. Its purpose was unresolved in the initial
Beach reward investigation.

**Follow-up:** That branch is now decoded and cross-checked with unmodified
Study refusal/SLEEP replays. The native helper selects the Beach reward
*before* testing raw result word `:7920`; zero result emits `F000:70be`
and schedules a separate event. Crucially, the common return routine calls
`048e:0284` to transfer to the Terminal and **resumes later**, after the
next input, to put the player on couch `0x2a1`. The exact branch table,
24 Ghidra labels and native evidence are in [VR return](vr-return.md).

## Casino access: independently and normally validated in Unicorn

The Casino's kind-4 EAST exit invokes callback ID `0x14` in section **48**,
OVL file `0x665f9`. It performs the direct membership check
`0157:000c(0x2a9,0xd3)`; **only then** does it return destination `0x0f6`
(Pedroza Lounge). In the baseline replay the callback returns zero and the
player stays in Casino `0x0f5`. In the controlled research replay, replacing
just the pin's relation word with `0xd3` causes the native callback to
return `0x0f6` and moves the player there. That earlier replay validates the
**gate**, but does **not** validate obtaining the pin through gameplay.
The later **unmodified** 39-step replay did both: native scanner event
`0x0f` at 132176 ms, callback return `AX=0x00f6` at 132268 ms, and a
player move `0x0f5 → 0x0f6` with the pin directly in inventory. See
[beach-vr-replay.md](beach-vr-replay.md) for the reproducible commands.

## Reproduce the decompilation

First obtain the native object and callback reports as described in
[ghidra.md](ghidra.md) and [room-callbacks.md](room-callbacks.md); replace
their paths in the following example:

```sh
python3 tools/run_ghidra.py \
  ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --overlay-tool "${GATEWAY_BUILD_DIR:-build}/gateway-overlay" \
  --objects-json build/ghidra-objects.json \
  --callbacks-json build/room-callbacks.json \
  --decompile-object 0x29b --decompile-object 0x2a3 \
  --decompile-object 0x2a4 --decompile-object 0x2a9 \
  --decompile-object 0x2ab \
  --decompile-offset 0x6d564 \
  --output build/ghidra-pin
```

Only the selected objects are seeded as *candidate* entries; the other native
object pointers remain aliases. Ghidra's output is preliminary C-like
pseudocode, with unresolved far calls and indirect jumps. The subsequent
74/74 headless run also selected Beach `0x2ae`, scanner `0x2b3`, glass
`0x2b4`, and bartender `0x2bb`. The Beach reward has now been traced through
typed commands; follow [beach-vr-replay.md](beach-vr-replay.md) to reproduce
it. The formerly open validation of TAKE after the award and native Casino
EAST traversal has now succeeded without a research memory poke.
