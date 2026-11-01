# Timer interrupt bridge

The probe executes up to 5,000 instructions per simulated millisecond, split
at channel-0 PIT expirations so high-rate IRQ0 handlers can run between timer
edges. This is a provisional 5 MIPS schedule, not a calibrated CPU model.
Headless execution remains bounded by two seconds of simulated time and a
two-second wall-clock deadline unless an explicit duration is requested.
`--trace-timing` reports achieved simulated-time versus wall-time speed once
per simulated second, to help diagnose audio underruns from slow emulation.
It also reports cumulative `irq0` (delivered timer interrupts), `pit_edges`
(channel-0 hardware expirations), and `speaker_writes` (channel-2 data writes)
in decimal, distinguishing dropped/coalesced IRQs from slow host execution.

`Pit` supports binary periodic modes 2 and 3, low/high counter programming,
zero-as-65536 reloads, counter latching, and IRQ0 cadence. The master PIC subset
provides IRQ masks and non-specific end-of-interrupt. Pending IRQ0 is injected
only when guest IF is set, IRQ0 is unmasked, and no IRQ is in service.

Injection pushes FLAGS, CS, and IP onto the guest stack and clears IF/TF. The
BIOS timer trampoline updates the BIOS tick counter, invokes the installed
`INT 1Ch` handler, acknowledges the PIC, and returns with IRET. Existing guest
interrupt vectors are dispatched similarly for unhandled non-DOS interrupts.

Tests verify default/programmed PIT cadence, latched counter stability, and a
synthetic guest receiving its own timer handler before completing. Mode-3 reads
currently expose the modeled remaining count rather than the exact square-wave
counter sequence. One-shot binary modes and PC-speaker pulse-width averaging
are modeled, while BCD, read-back, PIC initialization, and exact ISA/CPU timing
remain unsupported.

## Virtual calendar

DOS date and time start from the host local clock, then advance with guest device
time. Set-date/time requests modify only this virtual clock. Gateway saves,
calibrates, and restores its clock during startup, so these setters are required
even without gameplay. Invalid dates and times return AL=FFh and preserve the
previous value; valid setters return AL=00h. Tests cover leap days, midnight
rollover, invalid setters, and a guest set/get round trip. Host time is never set.

## BIOS tick reads and Gateway II

`INT 1Ah/AH=00h` returns the IRQ0-maintained BIOS tick counter at `0040:006C`
through CX:DX and the midnight flag at `0040:0070` through AL, then clears the
flag. The timer trampoline sets the flag when the counter wraps at 1800B0h.
Dispatch uses AH, since Gateway II calls this service with AX=00EAh.
Synthetic guests verify the returned words, flag consumption, and IRQ0 rollover.

On Linux, the following 30-second headless SDL runtime check completed without
an unsupported service after adding this handler (2026-10-31):

```sh
./build/gateway ../gateway2-dos/HOME.EXE --headless --milliseconds 30000 \
  --read-only --dump-prefix build/home-long
```

The trace progresses from `LEGEND.PIC` to `GW2-LOGO.PIC`, `HOME_303.PIC`, and
`HOME_301.FNT`. The earlier missing `HOME_200.FNT` lookup remains nonfatal.
Interactive graphics, keyboard/mouse input, audio, and gameplay are unverified.
