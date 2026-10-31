# AdLib host backend

`Opl` translates guest port `388h`/`389h` register writes into Nuked OPL3
synthesis at 48 kHz, interleaved signed 16-bit stereo. Upstream source and
licensing information are retained in `third_party/nuked-opl3/`.

The wrapper separately models the AdLib detection timers: 80 µs and 320 µs
increments, overflow flags, masks, and reset. Hardware accesses advance a minimal
one-microsecond bus interval. This allows register/detection code to run; it is
not a measured ISA-bus timing model. The backend also accepts explicit elapsed
time from the guest scheduler. Rendering samples does not advance timer state.

Tests check both timer overflow paths, reset/masking, and a two-operator voice
that must produce a varying PCM waveform. Gateway executes its initial register
writes and then reaches PIT initialization at port `43h`. SDL streaming of game
audio, music interrupt scheduling, and Sound Blaster digital effects are still
pending; the diagnostic's test tone is not game audio.
