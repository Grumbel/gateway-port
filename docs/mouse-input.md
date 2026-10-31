# DOS mouse input through SDL3

Gateway checks INT 33h before using a mouse. A null vector previously selected
its keyboard-only path. The host now publishes a non-IRET entry at `f000:0300`
and handles the mouse API directly; far calls through that vector also work.

Observed startup calls are enable (`20h`), reset (`00h`), horizontal range (`07h`,
0–639), and vertical range (`08h`, 0–479). Main polls position/buttons (`03h`).
Entering mouse mode also exercises cursor visibility (`01h`/`02h`), motion
counters (`0Bh`), and press history (`05h`). The supported subset additionally
provides positioning (`04h`), release history (`06h`), and software reset (`21h`).
Unsupported calls still stop explicitly; custom cursor masks and event callbacks
are not implemented. SDL presents the native system cursor.

SDL events are converted through the renderer's logical presentation before
mapping to DOS's 640-column coordinate grid. Resizing and letterbox offsets are
included. Button presses outside the framebuffer are ignored; releases and focus
loss clear held buttons. Press/release counts survive a click contained within
one host polling interval, although applications polling only current button
state can still miss such clicks. Guest positioning requests warp the host cursor.

## Reproducing the original-game check

```sh
./build/gateway ../gateway-dos/GATE.EXE --state-dir build/mouse-state
```

1. Press Escape during the intro and Space through MORE prompts.
2. At the command prompt, press Escape again to enter mouse mode.
3. Click Inventory: the scene area displays the worn items. Typing returns to
   keyboard input. Also check a resized window with black side margins.

An isolated Xvfb/X11 run verified Inventory clicks at 1280×720, including the
160-pixel side margins and 1.5× content scale. A subsequent sequence of ordinary
200-ms left clicks selected `Take`, then `off` from the updated verb menu. The
first click after entering mouse mode did not select a word in that sequence;
compare this mode-entry behavior with DOSBox before changing the guest API.
Synthetic CTest checks cover driver
presence, coordinate ranges, SDL conversion, button history, focus loss, and
cursor visibility. Keyboard, save/restore, and audible audio were separately
confirmed by the user on 2026-10-31.

UI interactions also exposed port `61h`, now modeled with PIT channel-2 gate and
speaker output. Short square-wave beeps are mixed into the SDL AdLib stream;
reference timing and audible beep fidelity remain to be compared with DOSBox.

The traced main mouse helper was loaded at `23bd:0000` in this run. Input code
checks the vector's first byte, resets the driver, and sets the ranges above.
Binary SHA-256 values:

- `GATE.RTL`: `c187f5ee7be86482cff89e668d934dc961ac50e960df7baa98965910f1ea9566`
- `GATEMAIN.EXE`: `1785bf60f196836c0542dafa4a00085f6b1f3837992f2cc14da12503bc02abcb`

API references: [FreeDOS's INT 33h reference](https://github.com/FDOS/mouse/blob/master/int33.lst)
and [SDL coordinate conversion](https://wiki.libsdl.org/SDL3/SDL_ConvertEventToRenderCoordinates).
