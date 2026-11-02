# Membership pin: from the Beach VR to the Casino

This is a **native-code reconstruction**, not yet a playable puzzle replay.
It joins three previously separate observations: the pin's starting location,
the Beach virtual-reality completion branch, and the tested Casino EAST gate.
Addresses below are **original, unmodified** `GATEMAIN.OVL` file offsets;
the file's RTLink sections are loaded into different overlay banks at runtime.

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
not yet a native TAKE-pin trace proving the complete downstream mutation.

## Beach VR success flag: the first half of the normal reward path

The Beach is room/object `0x2ae` in RTLink section **50**, and its frosted
glass is `0x2b4` (handler `0x69d63`). The original glass-handler code at
**`0x6a0fc–0x6a123`** checks byte `[word DS:9644]:78fa`, then executes:

1. If that byte is zero, call `048e:0a7e` with argument `0x0a`.
2. Store **1** at `[word DS:9644]:78fa` (`0x6a114–0x6a11d`).
3. Call `1c24:44d1` and jump back inside the containing handler.

Immediately before this code, the handler emits `f000:bc51`. That text
describes putting the frosted glass onto the scanner and a runaway cascade
of virtual drinks that breaks the simulation. The byte is therefore a
**strong candidate for the Beach-success flag**; the value and assignment
are exact original instructions, while the gameplay conditions for arriving
at that instruction have not been traced. In the earlier native startup
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

The destination `0x29b` is a room, **not the player `0xd3`**. The normal
remaining puzzle step is to pick up the now-accessible pin; the exact
TAKE-pin outcome needs a native trace. This distinction explains why the
earlier Casino replay had to poke the pin relation *directly to the player*
to exercise the successful EAST callback: it bypassed both the Beach reward
and the later pickup.

The later branch at `0x6d5e3` handles the non-award return paths, including
other VR-result state and messages. These have not been modeled.

## Casino access: already validated in Unicorn

The Casino's kind-4 EAST exit invokes callback ID `0x14` in section **48**,
OVL file `0x665f9`. It performs the direct membership check
`0157:000c(0x2a9,0xd3)`; **only then** does it return destination `0x0f6`
(Pedroza Lounge). In the baseline replay the callback returns zero and the
player stays in Casino `0x0f5`. In the controlled research replay, replacing
just the pin's relation word with `0xd3` causes the native callback to
return `0x0f6` and moves the player there. That earlier replay validates the
**gate**, but does **not** validate obtaining the pin through gameplay.

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
pseudocode, with unresolved far calls and indirect jumps. To extend the
chain, also select Beach glass `--decompile-object 0x2b4`. Native validation
still needed: solve the Beach puzzle through typed commands, capture the
`0x78fa` mutation and `0x6d5c0` call, TAKE the dropped pin, and finally
attempt Casino EAST without a research memory poke.
