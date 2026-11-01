# Gateway Linux port

Incremental C++ port of Frederik Pohl’s Gateway to Linux, using SDL3, CMake,
and Nix. The first target is execution of the original real-mode code with DOS
and hardware interfaces replaced by host implementations. Recovering the data
formats and replacing the original interpreter is the long-term goal.

## Current status

The original-code runtime reaches **Your Quarters**, renders the first room, and
accepts keyboard commands. Keyboard input, save/restore, and audio have also been
confirmed working by the user. Spellcasting 101 reaches gameplay, and its intro
music now plays correctly; Spellcasting 201 starts and renders its game interface
correctly in user testing. Spellcasting 301 reaches its first room, Beach, in a
headless run. Gateway 1's F10 debugger has been user-tested: automatic scene
selection, merged hitbox highlighting, and object labels work as expected.
No title has been verified through a complete playthrough. It provides:

- A checked DOS MZ loader with segment relocation and initial register setup.
- `gateway-inspect`, which reports loaded image and entry information.
- `gateway-probe`, a bounded Unicorn real-mode startup probe that traces DOS
  services and stops explicitly on unsupported calls.
- A tested DOS memory arena and planar VGA model with framebuffer dumps.
- `gateway`, an SDL3 runtime presenting guest VGA output and streaming AdLib PCM.
- `gateway-host`, a separate SDL3 graphics/audio diagnostic.
- `gateway-extract`, which converts PIC files to indexed PNGs.

Original game files remain external in `../gateway-dos/` and are never needed
for package builds or unit tests. The probe opens game files read-only unless given `--state-dir`; the SDL runtime
keeps writes in a separate persistent state directory.

## Legend Entertainment adventure-engine compatibility

Legend's original DOS adventure-engine family runs from *Spellcasting 101*
(1990) through *Callahan's Crosstime Saloon* (1997). The first seven games use
the illustrated text-parser interface closest to this port. The six later games
use the evolving graphical/point-and-click branch of the same engine lineage;
**shared ancestry does not imply executable or resource-format compatibility**.
The status below describes actual observations with `gateway`, not a prediction
based on the game's engine family. Only the five games listed with an executable
path are available in the current test archive.

### Illustrated parser games

| Year | Game | Executable in test archive | Status |
| --- | --- | --- | --- |
| 1990 | *Spellcasting 101: Sorcerers Get All the Girls* | `../101/S101.EXE` | **User-tested:** intro sound and keyboard skipping work; reached gameplay. Full playthrough unverified. |
| 1991 | *Timequest* | — | **Not tested:** no game files available. |
| 1991 | *Spellcasting 201: The Sorcerer's Appliance* | `../201/S201.EXE` | **User-tested:** starts, displays graphics, verbs, objects, and story text correctly. Full playthrough unverified. |
| 1992 | *Frederik Pohl's Gateway* | `../gateway-dos/GATE.EXE` | **User-tested:** reaches Your Quarters; keyboard, mouse controls, save/restore, and audio work. F10 scene hitboxes merge correctly and show matching object names. Full playthrough unverified. |
| 1992 | *Spellcasting 301: Spring Break* | `../301/S301.EXE` | **Headless-tested:** reaches Beach and renders the opening screen. Interactive gameplay and audio unverified. |
| 1993 | *Eric the Unready* | — | **Not tested:** no game files available. |
| 1993 | *Gateway II: Homeworld* | `../gateway2-dos/HOME.EXE` | **Headless-tested:** reaches the Living Room with readable menus and opening text. Interactive input and audio unverified. |

### Later graphical adventures in the same lineage

These titles have increasingly different interfaces and data formats. **None
has been run with this port**; they should not be considered supported merely
because their engine descends from Legend's earlier adventures.

| Year | Game | Status |
| --- | --- | --- |
| 1993 | *Companions of Xanth* | **Not tested:** no game files available. |
| 1994 | *Death Gate* | **Not tested:** no game files available; resource formats are related but differ from Gateway's. |
| 1994 | *Superhero League of Hoboken* | **Not tested:** no game files available. |
| 1995 | *Mission Critical* | **PIC extraction works:** Crusher! and raw-pixel decoder produced 481 PNGs from nine supplied archives with no failures. Full-frame XOR reconstruction works; scene-dependent layering and executable startup remain untested. |
| 1995 | *Shannara* | **Not tested:** no game files available. |
| 1997 | *Callahan's Crosstime Saloon* | **Not tested:** no game files available. |

This lists individual games, not compilation releases such as *Spellcasting:
Party Pak* or *The Lost Adventures of Legend*. Later Legend projects, including
*Star Control 3*, *John Saul's Blackstone Chronicles*, and the Unreal-engine
games, are outside this port's established engine scope. For the game catalog
and engine history, see [IFWiki's Legend list](https://www.ifwiki.org/Legend_Entertainment),
[MobyGames' Legend catalog](https://www.mobygames.com/company/117/legend-entertainment-company/games/),
and [The Digital Antiquarian's engine history](https://www.filfre.net/2025/06/the-last-adventures-of-legend/).
For detailed local checks, see [Spellcasting research](docs/spellcasting.md),
[Mission Critical PIC research](docs/mission-critical.md), and
[Gateway II's headless startup](docs/video.md#bios-font-fallback-without-fixed-game-fonts).

## Build and run

From this directory:

```sh
nix develop
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/gateway-inspect ../gateway-dos/GATE.EXE
./build/gateway-probe ../gateway-dos/GATE.EXE
./build/gateway-host
```

### Debugging with `nix develop`

The development shell follows the `gamepkgstd` workflow. From the checkout:

```sh
nix develop
gateway-run ../201/S201.EXE --trace-video
gateway-run-gdb ../101/S101.EXE
```

The SDL runtime defaults to a **4:3 display aspect ratio**, matching the
intended proportions of conventional DOS EGA/VGA output even when the guest's
pixel grid is not square (for example 640x350). Override it with
`--aspect-ratio native` for uncorrected square pixels or with a custom ratio:

```sh
gateway-run ../201/S201.EXE --aspect-ratio 4:3
gateway-run ../201/S201.EXE --aspect-ratio native
gateway-run ../201/S201.EXE --aspect-ratio 16:10
```

The image is scaled by SDL; the guest framebuffer resolution and DOS mouse
coordinates remain unchanged. This option does not change headless dump sizes.

### In-game Gateway debugger (F10)

Launch the original Gateway 1 executable, enter a room, then press **F10**:

```sh
gateway-run ../gateway-dos/GATE.EXE
# Or choose an explicit location for snapshots:
gateway-run ../gateway-dos/GATE.EXE --debug-dir build/gateway-snapshots
```

The host-drawn menu does not modify guest VGA memory. Its keyboard shortcuts
are reserved by the host and never sent to DOS; ordinary game input resumes
when the menu closes. Use Up/Down and Enter to select a menu item, or:

| Key | Debug action |
| --- | --- |
| **F10**, **Esc** | Open/close the menu (Esc only when the menu is open) |
| **H** | Toggle merged, colored and translucent hotspots; hover for the object ID/name |
| **A** | Toggle automatic scene selection from recent PIC reads |
| **Left/Right** (or PageUp/PageDown) | Examine previous/next RGN frame manually |
| **D** | Dump guest registers, memory, framebuffer, regions and object state |

The menu starts in the lower-right text pane, beneath the scene picture. Drag
its title bar to move it. Merged hotspot outlines and translucent fills stay
on screen after closing the menu until toggled off. The current file and RGN
entry, guest CS:IP/DS, and recognized Gateway object count are shown in the
panel. The hitboxes come from the original `GATE_*.RGN`
resources; their coordinates are relative to the picture in the right-hand
panel. Auto-selected scenes are **only an estimate** based on which picture
was recently read, and can lag, select transitional art, or fail to identify a
scene. Use manual frame selection to compare frames if that happens.

Dumping writes numbered `gateway-<guest-ms>-<sequence>` files under the DOS
state directory's `debug/` folder (or under `build/debug/` in read-only mode).
Override the destination with `--debug-dir DIR`. Every dump includes `.regs`
(CPU registers), `.bin` (1 MiB guest memory), `.ppm` (undecorated framebuffer),
`.regions.tsv` (visible hitboxes) and `.debug.txt` (selection provenance). A
`.objects.tsv` is also saved when the *Gateway 1* 734-object live table can be
validated at DS; it includes object IDs, types, handler indices, name tokens,
locations and raw state bytes. An unrecognized state is reported, not guessed.
RGN hotspot numbers **are engine object IDs**, but are **not** indexes into the
`OBJECT.DAT` string list. Labels are resolved through the live engine table.
The user confirms that merged regions render correctly and that their names now
match objects in the scene. The automatic scene heuristic also works in the
tested rooms; it is not yet proven accurate for every transition.
See [the debugger's evidence and limits](docs/debugger.md) before using these
observations to design a native story interpreter.

`gateway-configure` configures an out-of-tree Ninja Debug build (default
`/tmp/gateway-port-build`, override with `GATEWAY_BUILD_DIR`),
`gateway-build` builds it, and both run commands build before launching. The
first configure links `build` to that directory only if `build` does not
already exist. Set `CMAKE_BUILD_TYPE=RelWithDebInfo` to override Debug on
initial configuration. `nix develop -c gateway-run-gdb ../101/S101.EXE`
also works without an interactive shell.

GDB starts the SDL runtime immediately and stays at the prompt if the process
crashes or exits unsuccessfully. For a guest that appears frozen, press Ctrl-C
in GDB, then use `bt` and `continue`. GDB sees the host C++/Unicorn execution,
not DOS `CS:IP` directly; use the bounded guest trace instead:

```sh
gateway-run ../101/S101.EXE --headless --milliseconds 15000 \
  --dump-prefix /tmp/s101-stall --trace-blocks /tmp/s101-blocks.txt
```

For a title-screen loop accompanied by repeated DOS reads, record the file
offsets and actual read lengths (and a final guest block trace) with:

```sh
gateway-run ../101/S101.EXE --headless --milliseconds 15000 \
  --trace-research /tmp/s101-reads.txt --trace-blocks /tmp/s101-blocks.txt
```

If reads advance continuously it may be an animation or music stream; repeated
reads at the same offset, or repeated zero-length reads, suggest a stuck guest
loop. These details are needed before changing the DOS file or timer model.

For visual glitches that happen after a keypress, `--trace-video` prints BIOS
mode/page changes and CRTC start/pitch/mode writes to stderr. This helps
distinguish the renderer's state from the guest's page-flipping commands.
When reporting a shifted framebuffer, capture the entire stderr trace from a
run that changes from correct to incorrect, including the keypress that causes
the transition; a screenshot alone cannot identify the responsible VGA state.

Build a package with `nix build`; check it with `nix flake check`. Use `path:.`
while testing new untracked files that Git-backed flakes would otherwise omit. The flake locks
nixpkgs and provides SDL3, Unicorn, the compiler, CMake, and research tools.

For loader-only development without SDL3 or Unicorn:

```sh
cmake -S . -B build-core -DGATEWAY_WITH_SDL=OFF -DGATEWAY_WITH_UNICORN=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

The host diagnostic displays color bars and queues a short 440 Hz test tone.
Escape or closing the window exits. A bounded headless API check is available:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/gateway-host --smoke-test
```

This checks initialization and submission, not audible output or visible pixels.
The probe is limited to ten million instructions and two seconds of CPU execution.
It reports unsupported services rather than silently returning fabricated success.

## Extracting pictures

`gateway-extract` writes `DIR/<file>/<entry>.png` and needs neither SDL3 nor Unicorn.
It decodes Gateway and the three Spellcasting games, and detects the format per file:

```sh
./build/gateway-extract -o build/pictures/gateway ../gateway-dos/*.PIC
./build/gateway-extract -o build/pictures/s101 ../101/*.PIC
```

Pass all of a game's files together, because some rely on a palette from an earlier
file. XOR overlays are merged onto their picture; `--raw-overlays` keeps the deltas.
`--dialect s101|s201|gateway` overrides detection. The exit status is non-zero if any
entry fails to decode.

Mission Critical uses a newer 20-byte PIC directory and **Crusher! 3.0**
compression (or uncompressed indexed pixels). The native decoder reconstructs
the indexed pixels, applies the latest VGA palette update, and writes a PNG
for each image entry:

```sh
./build/gateway-extract -o build/pictures/mc ../MissionCritical/*.PIC
```

Pass files in game order to preserve palette updates across files. If no palette
has been seen, images use a grey ramp. Some `0003h` records are **XOR frame
deltas**, not corrupted pictures: for instance, `MC007.PIC` #32 must be XORed
with the pixels from #31 to make a normal hangar frame. Full-size deltas are
composed automatically. Smaller `0003h` images may be independent sprites or
layers that rely on a particular scene, so they remain standalone by default.
Use `--mc-all-overlays` to compose all positioned deltas against the most recent
covering full image, or `--raw-overlays` to preserve every delta exactly as
decoded. Neither option can reconstruct arbitrary scene-dependent combinations.
To inspect the original compressed streams instead, export their current 768-byte
VGA palette snapshots and a tab-separated metadata index:

```sh
./build/gateway-extract --raw-chunks -o build/pictures/mc ../MissionCritical/*.PIC
```

Files are `DIR/<file>/<entry>.crusher`, `<entry>.pal` (when a palette has been
encountered), and `index.tsv`. The `.pal` bytes are RGB triples with 6-bit VGA
components. All 55 streams in four supplied sample archives were decoded and
visually checked. See [Mission Critical's format notes](docs/mission-critical.md).

## Research

See [executable findings](docs/executable.md), [data file formats](docs/data-files.md),
the [Spellcasting comparison](docs/spellcasting.md), the
[ScummVM engine comparison](docs/scummvm-legend.md), and [next steps](TODO.md).
The neighboring EF2000 port informs the external-data, system-boundary, and
reference-testing approach. Its protected-mode native harness is not directly
applicable to Gateway’s real-mode startup.

[Risca’s Death Gate resource manager](https://github.com/Risca/dgate_resource_manager)
is a format-research reference. Gateway shares its string compression and PIC
directory layout but differs elsewhere; see [the comparison](docs/data-files.md#comparison-with-death-gate).

To retain a snapshot at the startup boundary:

```sh
./build/gateway-probe ../gateway-dos/GATE.EXE --dump-prefix build/gate
```

This writes `build/gate.bin` (guest memory) and `build/gate.ppm` (VGA pixels),
including on unsupported-service stops. These are diagnostic artifacts, not
proof that the rendered game output is correct.

## Running original code with SDL3

```sh
./build/gateway ../gateway-dos/GATE.EXE
# Or, from the installed Nix package:
nix run . -- ../gateway-dos/GATE.EXE
```

F11 or Alt-Enter toggles desktop fullscreen; these shortcuts are handled by
the host. Close the window to stop. Escape, Space, Enter, letters, digits, arrow keys,
and common punctuation are forwarded to the guest BIOS keyboard queue. Mouse
motion and left/right/middle buttons reach the DOS mouse driver, with scaling
for resized windows. **Press Escape at the command prompt to enter mouse mode**
and activate the clickable controls; typing returns to keyboard input. Inventory
clicks have been verified in the original game. Normal DOS child EXEC remains
unimplemented. Escape during the intro loads the main executable through the
original overlay trampoline; startup
creates `RESTART.DAT` in writable state and reaches Your Quarters. Space advances
MORE prompts; typing `look` and Enter redraws the room description in the tested
SDL session. Complete intro playback and reference timing still need validation.

For a bounded capture (1–60000 ms of virtual guest time):

```sh
./build/gateway ../gateway-dos/GATE.EXE --milliseconds 5000 --dump-prefix build/intro
./build/gateway ../gateway-dos/GATE.EXE --headless --milliseconds 5000 --dump-prefix build/intro
```

The scheduler currently advances 1 ms per 5,000 guest instructions (5 MIPS),
using 16 ms presentation/audio blocks. Timing remains provisional. Dummy-driver
integration tests exercise real guest VGA instructions through the SDL runtime.

Writable game state defaults to `$XDG_STATE_HOME/gateway-port`, or
`$HOME/.local/state/gateway-port` when XDG state is unset. Override it with
`--state-dir DIR`; use `--read-only` for a run without guest writes. Original
assets remain unchanged, including when a writable name shadows an asset.
For a probe with local state:

```sh
./build/gateway-probe ../gateway-dos/GATE.EXE --press escape \
  --state-dir build/runtime-state --milliseconds 22000 --dump-prefix build/main
```

Spellcasting 301 uses the same runtime with its own fonts and resources:

```sh
./build/gateway ../301/S301.EXE --state-dir build/s301-state
```

Keep different games' state directories separate. See the
[runtime findings](docs/spellcasting.md#spellcasting-301-runtime-check-2026-10-31)
for the verified startup path and current limits.

## Native game-logic research

A traced `look` → `open drawer` → `look` sequence now exercises the native room
handler with an optional C++ replacement for the shared object-flag query:

```sh
python3 tools/trace_game_logic.py --scenario drawer
```

This compares native, verification, and HLE runs using separate writable state
and matching final framebuffers. See [engine boundaries](docs/engine-boundary.md)
for recovered dispatch tables, call contracts, research options, and limits.
The replacement is experimental and disabled during normal play.

The extended experiment also replaces flag setters and direct object locations,
compares all 734 final object records, and exports native handler mappings:

```sh
python3 tools/trace_game_logic.py --scenario objects --services state
python3 tools/inspect_object_state.py build/game-logic/hle \
  --names ../gateway-dos/OBJECT.DAT > build/object-handlers.json
```

See [object-state findings](docs/object-state.md) for the 696 recovered handler
entry points, service contracts, name mapping, and validation limits.

RTLink sections and all handler entry file offsets can now be extracted:

```sh
./build/gateway-overlay ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --output build/rtlink-sections --fixups > build/rtlink-manifest.json
python3 tools/trace_game_logic.py --scenario objects --services state --verify-overlays
```

The [RTLink findings](docs/rtlink.md) describe all 58 sections, 13,947 fixups,
parent/residency rules, and the live comparison against 187 native section loads.

To supply section bytes and relocations from C++ while preserving native RTLink
residency and returns, use the experimental host-loading replay:

```sh
python3 tools/trace_game_logic.py --scenario objects --services state --hle-overlays
```

It matches native execution while replacing all 187 section fills in that replay;
see [host loading limits](docs/rtlink.md#opt-in-host-loading-step).
