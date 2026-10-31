# Timer interrupt bridge

The probe executes up to 5,000 instructions per quantum, advancing device time
by 1 ms after each completed quantum. This is a provisional 5 MIPS schedule,
not a calibrated CPU model. Headless execution remains bounded by ten million
instructions and a two-second wall-clock deadline.

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
counter sequence. One-shot, BCD, read-back, speaker synthesis, PIC initialization,
and exact ISA/CPU timing remain unsupported.

## Virtual calendar

DOS date and time start from the host local clock, then advance with guest device
time. Set-date/time requests modify only this virtual clock. Gateway saves,
calibrates, and restores its clock during startup, so these setters are required
even without gameplay. Invalid dates and times return AL=FFh and preserve the
previous value; valid setters return AL=00h. Tests cover leap days, midnight
rollover, invalid setters, and a guest set/get round trip. Host time is never set.
