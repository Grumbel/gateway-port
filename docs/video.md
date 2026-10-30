# VGA host model

Gateway's intro executable detects VGA with BIOS `INT 10h AX=1a00`, queries the
current mode with `AH=0fh`, then selects mode `12h`: 640×480, sixteen colors.
The pure C++ `Vga` model stores four 64-KiB planes independently from Unicorn
memory and converts the displayed pixels to opaque ARGB8888 for the SDL host.

The graphics subset implements sequencer plane masks, graphics-controller
set/reset and raster operations, read-map selection, color comparison,
write modes 0–3, DAC palette streams, attribute palette selection, and CRTC
start/pitch registers. Chain-four addressing is available for mode 13h tests;
that mode's full BIOS palette and timing are not yet modeled.

The status port uses synthetic alternating retrace states. It prevents polling
loops from waiting forever but is not a measured hardware timing model. Text
rendering, VGA fonts, arbitrary CRTC timings, odd/even memory addressing, and
hardware interrupts remain outside this subset.

Unit tests exercise independent plane/color output, pitch, latch copies,
set/reset masks, read comparison, write mode 3, DAC access, display offsets, and
chain-four addressing. These tests verify the model's contracts; comparisons
with original DOSBox captures are still required for game-output accuracy.
