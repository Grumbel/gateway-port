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
The same debugger now reads room hitboxes from Spellcasting 101/201/301 and
Gateway II, including Gateway II's polygonal regions; these additions have
been checked against game resources but not yet visually confirmed in play.
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

For restricted analysis containers and the user-supplied Unicorn ZIP, see
[workspace setup](docs/workspace-setup.md) for a tested GCC/static-Unicorn
headless probe build that does not require CMake or Nix. Interactive SDL3
recordings still require the normal development environment and private Xvfb.

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

### Experimental widescreen UI shell

The original-code runtime has an opt-in SDL3-renderer widescreen layout:

```sh
gateway-run ../gateway-dos/GATE.EXE --ui-layout widescreen
```

It preserves the complete original 4:3 game framebuffer in the middle, without
cropping or stretching it, and uses any sufficient left/right window margins
for native action labels and a provisional room-candidate list. Mouse coordinates,
cursor drawing, debugger clicks and window resizing are mapped through the same
computed scene rectangle. Narrow windows automatically hide the side panels.
The original view remains the default (`--ui-layout original`). No OpenGL API
is used: the shell uses SDL3's built-in 2D renderer.

This is an early **noninteractive UI preview**, not a replacement for the
in-game action menu. The action names are placeholders. The right panel uses
live Gateway object locations, a provisional disclosure flag and known drawer
state to narrow static RGN candidates; it **does not** know which objects the
original renderer actually drew. Candidates with no confirmed current-room
relationship are omitted, so the list can be incomplete. The temporary 8x8
SDL debug font will be replaced with a
scalable font renderer. Native save/load screens with room thumbnails,
command completion, maps, and extracted/upscaled room-image composition are
planned but not implemented by this option. The original DOS save/restore and
input continue to work in the center framebuffer.

### Opt-in native room compositor preview

The SDL runtime can also exercise the host-side PIC compositor for the
first-room picture, without changing guest execution:

```sh
gateway-run ../gateway-dos/GATE.EXE --host-scene-preview
```

This experimental switch pre-decodes the base and three XOR layers from
`GATE_102.PIC`. Before presentation, it checks whether the original 640×480
VGA picture area matches **exactly one** possible native composition. Only
then does it replace the matching room pixels with the host-rendered result.
Unrecognized scenes, nonmatching graphics, palette changes, and the rest of
the UI fall through untouched. Original guest animation and drawing still
run, so this is an opt-in **presentation bridge**, not acceleration or a
replacement for the original scheduler. The eight idle-blink and 32
action-interleaved original captures are covered by private differential
tests; interactive SDL behavior still needs a user-side check.

### Ghidra: decompile the original Gateway logic

The optional `nix develop .#ghidra` shell provides Ghidra without adding its
large Java package to the normal development environment. Run `gateway-build`,
then create a fresh headless Ghidra project containing the original
`GATEMAIN.EXE` and all **58 individually relocated RTLink overlay sections**:

```sh
nix develop .#ghidra
gateway-build
python3 tools/run_ghidra.py \
  ../gateway-dos/GATEMAIN.EXE ../gateway-dos/GATEMAIN.OVL \
  --overlay-tool "${GATEWAY_BUILD_DIR:-build}/gateway-overlay" \
  --output build/ghidra
```

It verifies all **13,947 RTLink fixups**, keeps overlapping sections separate,
imports documented data types and known action/room-handler entry labels, and
exports preliminary C-like listings to `build/ghidra/pseudocode.txt`.
`--prepare-only` works without installing Ghidra. An optional `--objects-json`
adds labels for the **696 mapped object/event handlers** from a native snapshot.
See [Ghidra workflow and limitations](docs/ghidra.md).

### Historical Microsoft C 5.1 toolchain (optional)

Gateway's `GATE.RTL` contains a Microsoft C runtime signature dated 1988,
making Microsoft C 5.1 a useful compiler-code-generation reference. The
historical compiler is **proprietary software**. The repository contains none
of its binaries; the following opt-in Nix targets fetch a separately published
1988 distribution, with a pinned SHA-512 hash, and unpack it into a Nix store
path. Do not redistribute the resulting compiler closure.

From the Gateway checkout, with **no floppy or compiler setup required**:

```sh
nix run .#msc51-compare                       # download and compare original CRT
nix run .#msc51-probe                         # compile and run 32 rand steps in DOSBox
nix run .#msc51-shell                         # interactive DOSBox compiler
nix run .#msc51-prepare                       # prepare C89 source only (no download)
nix build .#msc51                             # optional: inspect assembled DOS tree
nix develop .#msc51                           # optional DOSBox toolchain shell
```

The opt-in `msc51-archive` Nix package downloads
[WinWorld's Microsoft C 5.1 5¼-inch distribution](https://winworldpc.com/product/microsoft-c-c/5x)
from either of its two mirrors using the archive's published SHA-512 checksum.
`msc51` unpacks the 7z file and extracts the contained FAT floppy images using
mtools, automatically assembling `BIN/CL.EXE`, `C1.EXE`, `C2.EXE`, `C3.EXE`,
`LINK.EXE`, `LIB/LLIBCR.LIB` and `INCLUDE/STDIO.H`. DOSBox mounts that tree
**read-only** as drive C and a writable local staging directory as drive D.
`nix flake check` itself does not download the proprietary compiler.

Some distribution disks contain **different copies of the same executable**
(observed: `CL.EXE`, `C1L.EXE`, `C2.EXE`, `C3.EXE` and `LINK.EXE`). The 5.1
distribution includes real-mode DOS and protected-mode OS/2 variants, which
cannot be mixed indiscriminately. The assembler recognizes MZ/NE/PE/LE/LX
headers and prefers a real-mode DOS MZ executable over an incompatible
protected-mode copy. Other duplicates still use the first sorted source.
Every choice and rejected conflicting file is recorded in
`compiler-manifest.json` with the original disk and SHA-256; the tool emits
diagnostics when choosing. For forensic comparison,
`msc51-unpack --strict-duplicates` instead rejects any conflicting bytes.
The assembled toolchain must contain real-mode `CL`, `C1`, `C1L`, `C2`, `C3`
and `LINK`; tools known to be protected-mode are rejected before DOSBox runs.
This is not a complete executable compatibility audit, nor proof that the
historical compiler can rebuild the probe.

For an existing download containing raw floppy `.img`/`.ima` images, you can
import it offline instead of fetching it again:

```sh
nix run .#msc51-unpack -- \
  "/path/to/Microsoft C 5.1 Optimizing Compiler (5.25)" build/msc51-local
nix run .#msc51-compare -- --compiler build/msc51-local
```

The `GATEWAY_MSC51_DIR` environment variable is for calling the underlying
Python scripts directly. The Nix `msc51-compare`, `msc51-probe` and
`msc51-shell` apps deliberately select their pinned compiler by default, even
if that variable is set. Pass `--compiler DIR` to any app to use your own
**assembled** installation, not a directory containing floppy images.

For historical reference, the original interactive DOS installer remains
available via `nix run .#msc51-install -- --install-dir DIR --floppy SETUP.img
--floppy COMPILER1.img ...`; it is no longer necessary for the usual workflow.

The probe's generated files are in `build/intro-msc51/`. `PROBE.C` and `RANDOM.C`
are the recovered original-code functions, changed only to include a tiny
generated C89 compatibility header (Microsoft C 5.1 predates `<stdint.h>`).
`BUILD.BAT` compiles them using `/AL /Od` and writes `INTROCHK.EXE`,
`BUILD.LOG`, and `TEST.LOG`. `msc51-probe` checks that the DOS executable
actually reports all 32 original `rand()` states correct. To customize the
toolchain directory or the staging output directory, pass `--compiler DIR`
or `--output DIR` after `--` in the `nix run` commands.

`BUILD.BAT` explicitly runs `EXIT` at the end so DOSBox closes on its own.
If DOSBox starts but the compilation fails, `msc51-probe` prints the
compiler's `BUILD.LOG` and, when present, `TEST.LOG` automatically. `START.LOG`
confirms that DOSBox actually reached the batch file; `CLFAIL.LOG` flags a
nonzero Microsoft compiler exit status. The wrapper clears these files and
the old executable before each run, and recognizes DOSBox-created filenames
regardless of case on a case-sensitive host. The compiler's installation is
still read-only, and its scratch files are directed to the writable D: drive.
If the failure persists, capture the **new** `BUILD.LOG` output; a successful
DOSBox process exit by itself does not establish that the DOS compilation ran.

These tools do **not** build a playable Gateway DOS executable; they are a
compiler comparison/probe environment. The `msc51-tooling` check runs without
fetching Microsoft binaries, including a synthetic FAT floppy-image import
test with mtools.

The `msc51-compare` app reads the SHA-256-pinned original `GATE.RTL` and the
assembled `LLIBCR.LIB`. It searches the raw library for
the original `srand` (`rtl:0x91d8`) and `rand` (`rtl:0x91ea`) instruction
sequences while ignoring only the mutable address/call operands. The optional
JSON report contains locations, lengths, and file hashes, **not binary data**.
Matching both code sequences would be strong evidence of an identical CRT
implementation; a miss is inconclusive when code is split into OMF records
or generated with different options. This compares the historical *runtime*,
not the compiler's code generation for Gateway's game logic.

### In-game Gateway debugger (F10)

Launch the original Gateway 1 executable, enter a room, then press **F10**:

```sh
gateway-run ../gateway-dos/GATE.EXE
# Or choose an explicit location for snapshots:
gateway-run ../gateway-dos/GATE.EXE --debug-dir build/gateway-snapshots
# Also observe the original mouse picker return values, beside the hover map:
gateway-run ../gateway-dos/GATE.EXE --debug-native-picker
```

The host-drawn menu does not modify guest VGA memory. Its keyboard shortcuts
are reserved by the host and never sent to DOS; ordinary game input resumes
when the menu closes. Use Up/Down and Enter to select a menu item, or:

| Key | Debug action |
| --- | --- |
| **F10**, **Esc** | Open/close the menu (Esc only when the menu is open) |
| **H** | Toggle merged, colored and translucent hotspots; hover for the object ID/name. With `--debug-native-picker`, also see the last actual native picker selection. |
| **R** | Toggle between filtered hotspots and every raw RGN candidate (debug menu open) |
| **A** | Toggle automatic scene selection from recent PIC reads |
| **Left/Right** (or PageUp/PageDown) | Examine previous/next RGN frame manually |
| **D** | Dump guest registers, memory, framebuffer, regions and object state |

The menu starts in the lower-right text pane, beneath the scene picture. Drag
its title bar to move it. Merged hotspot outlines and translucent fills stay
on screen after closing the menu until toggled off. The current file and RGN
entry, guest CS:IP/DS, and recognized Gateway object count are shown in the
panel. Hotspots default to a conservative host-side filter for Gateway 1;
**R** exposes every raw candidate for comparison. This is not the game's
original conditional-hitbox selector. Hitboxes come from the original `GATE_*.RGN`, `S101_R*.RGN`,
`S201_R*.RGN`, `S301_*.RGN`, or `HOME_*.RGN` resources. Spellcasting uses
packed rectangles; Gateway II uses polygon vertices (and two-point boxes),
shown with the same merged outlines and translucent fills. All coordinates
are relative to the room picture in the right-hand panel. The new dialects
pass resource-fixture tests but need a live visual/alignment check. Gateway 1
object names are confirmed against its native state. Gateway II's 521-object
name table was also recovered from a live guest memory snapshot. Spellcasting
201/301 and Gateway II use a conservative lookup of native object records: names
appear only after hundreds of records and their `OBJECT.DAT` tokens validate.
If the layout is not recognized, numeric IDs remain visible. Spellcasting 101
has no `OBJECT.DAT` and still needs a separate name-source investigation.
Auto-selected scenes are **only an estimate** based on which picture
was recently read, and can lag, select transitional art, or fail to identify a
scene. Use manual frame selection to compare frames if that happens.

Dumping writes numbered `gateway-<guest-ms>-<sequence>` files under the DOS
state directory's `debug/` folder (or under `build/debug/` in read-only mode).
Override the destination with `--debug-dir DIR`. Every dump includes `.regs`
(CPU registers), `.bin` (1 MiB guest memory), `.ppm` (undecorated framebuffer),
`.regions.tsv` (all static region candidates, polygon vertices and provisional
guest-state presence classification) and `.debug.txt`
(selection provenance). A
`.objects.tsv` is also saved when the *Gateway 1* 734-object live table can be
validated at DS; it includes object IDs, types, handler indices, name tokens,
locations and raw state bytes. A `.names.tsv` is saved for the other games if
their native names can be validated. An unrecognized state is reported, not guessed.
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

For a room-art image-generation queue, see [the remaster pipeline](docs/remaster-pipeline.md).

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
the host. Close the window to stop, or press Ctrl-C in the launching terminal
to terminate the process immediately even if emulation is stalled.
Escape, Space, Enter, letters, digits, arrow keys,
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

The main reverse-engineering effort has now switched to **decompiling the
original launcher into source-shaped 16-bit C**, preserving its functions,
far calls, data offsets and control flow before making a modern translation.
The first translated routines and their remaining uncertainties are in
[`decomp/launcher/README.md`](decomp/launcher/README.md).

The original source-C intro now runs the full `GATE.EXE:1302` presentation
sequence (images, palette fades, animations, and text) through the recovered
C functions. The **live** SDL3 runner plays original AdLib MIDIPlay music
through a native OPL3 adapter and forwards keys to the recovered C input
handler:

```sh
nix run .#intro-source-c-live -- ../gateway-dos
```

To play the recovered source-C intro and then enter the original game, use
the opt-in hybrid launcher:

```sh
nix run .#intro-source-c-play -- ../gateway-dos
```

Escape skips the rest of the intro and continues into the original game;
closing the intro window cancels it, while Ctrl-C terminates the launcher
without proceeding to the game. This starts `GATEMAIN.EXE` independently
with the verified original default numeric launcher arguments. It **does not**
yet transfer source-C guest RAM or DOS process state to the game. The
source-C intro now starts natively from the original DOS asset files,
without executing any Unicorn/DOS startup instructions. The live runner
and its transition have not been compiled/tested with SDL3 here.

An independent original source-C caption-only diagnostic remains available,
separate from the C++ renderer below. Given a captured 1 MiB
original DOS memory image, `nix run .#intro-source-c --
build/intro-analysis/at-22000.bin ../gateway-dos` displays eighteen pages
through the recovered `GATE.EXE:117e` source-C functions. The headless
`nix run .#intro-source-c-dump -- build/intro-analysis/at-22000.bin
../gateway-dos build/source-c-pages` exports the same frames (create the
output directory first). The **full** intro's default live and replay modes
reconstruct their linked startup state from the original EXE/RTL/PIC assets
with no guest-RAM capture. The hybrid game transition still starts a fresh
emulated main-program process. SDL audio and timing parity remain to be
tested against original DOS. See
[source-C intro status](docs/intro-source-c.md) for snapshot setup and tests.
The independent original C image path is also runnable as
`nix run .#intro-source-c-picture-dump -- build/intro-analysis/at-22000.bin
../gateway-dos build/source-c-picture.pgm`, using the original PIC decoder
host boundary and the recovered C image-loader and drawing functions.
Use `--resource 0x3105 0 --rgb` after the output filename to render an
original intro background in full color through the source-C DAC service.
Use `--sequence` with an existing output directory to render thirteen
successive original source-C picture calls with persistent guest memory and
palette state; see [source-C intro status](docs/intro-source-c.md).

A separate native C++ intro prototype renders the initial Legend splash,
logo sequence, all eighteen narrative pages, and several background/sprite
animations directly from `GATE.EXE`, the original PIC archives, and its
proportional font, without executing DOS instructions. This SDL viewer is
retained as an independent reference:

```sh
nix run .#intro -- ../gateway-dos
./build/gateway-intro-dump ../gateway-dos --ms 22000 build/native-intro.ppm
./build/gateway-intro-dump ../gateway-dos --ms 303000 build/intro-mid-glyph.ppm --ink-pixels 2627
```

This is still a preview, not a full launcher replacement: music, the exact
animation clock/input loop and game handoff are unfinished. In the captured
intro sequence, 85 of 87 sampled frames from 19 to 322 seconds now match the
original framebuffer byte-for-byte, including fully drawn text captions,
later spacecraft frames and the title's palette-revealed credit. The seven
intro-logo red bars also use the original compiled DAC order and a calibrated
64-step ramp; 201 of 206 densely sampled 20ms frames match byte-for-byte.
The mid-update 259,500ms image also matches exactly: the original VGA DAC
has brightened the first palette indices while leaving the rest unchanged.
The original RTL printer's per-word and per-pixel drawing order is now
recovered: a diagnostic render of the first 2,627 ink writes exactly matches
the partially drawn final caption at 303,000ms, although the normal preview
still lacks the precise intra-frame timing.
The launcher also contains a recoverable sequential program: all 18 compiled
caption calls are mapped, with a shared `2000 + 45 * strlen(text)` millisecond
timeout and an interruptible keyboard/mouse wait. Inspect the original call
arguments with `python3 tools/inspect_intro_program.py ../gateway-dos/GATE.EXE`.
See [native intro reconstruction](docs/intro-reconstruction.md) for the
observed resource/scene mapping and reproducible comparisons.

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

To begin reconstructing native story logic, disassemble a bounded Gateway room
or object routine with text-token annotations:

```sh
python3 tools/disassemble_story.py ../gateway-dos/GATEMAIN.OVL \
  --start 0x72a97 --size 0x25
```

See [native story disassembly](docs/game-logic.md#first-reproducible-annotated-disassembly)
for an initial C-like translation and its limitations.
The [Your Quarters case study](docs/room-your-quarters.md) follows the first
complete 24-entry room-event jump table, annotates story text, and partially
reconstructs the `look` handler in C-like pseudocode.
To investigate its parser/action dispatch with the original game, inspect the
word table and run the scripted native story trace:

```sh
python3 tools/gateway_vocab.py ../gateway-dos/VOCAB.DAT look open close pull --groups
python3 tools/trace_game_logic.py --scenario objects --services state \
  --output build/story-commands
python3 tools/summarize_story_trace.py build/story-commands/native.trace --actions
```

Six observed top-level action codes are now associated with typed commands;
see [story command tracing](docs/room-your-quarters.md#mapping-the-parsers-numeric-command-codes).
The summarizer accepts older unannotated entry traces too; use `--details` to
see each room/drawer event with the nearest recently completed typed input.
New traces also capture the drawer's command byte directly from AX at its
native comparison site (`0x4a390`), avoiding the original pointer-probe gap.
Live tracing establishes that normal `open drawer` (`0x71`) and `close drawer`
(`0x1d`) fall through the drawer-specific handler to shared action logic.
To inspect the native action-table pointers from the existing replay dump:

```sh
python3 tools/inspect_action_dispatch.py build/story-commands-043/native
```

The five observed nonresident action-table pointers address **executable
ten-byte RTLink trampolines**, not embedded reference operands. OPEN and
CLOSE enter shared section-5 routines that respectively contain property-12
set and clear operations. Both mutations are **confirmed in a native replay**
for drawer object `0x124`; the shared OPEN path also **calls the setter for
property `0x02`**, as confirmed by the next callsite trace. The research replay
now hooks all five section-5 action entries and the generic flag/location
mutation services, including calls from nested action helpers. To summarize
observed calls:

```sh
python3 tools/summarize_action_effects.py build/action-section5-check/native.trace
```

See [native action dispatch](docs/action-dispatch.md) for a partial C-like
translation, exact runtime addresses, original overlay file offsets, and
remaining branch-verification work.

The current native replay confirms book location changes (`0xd3` when taken,
`0xee` when dropped) and flags `0x02`, `0x08`, and `0x0a` on TAKE. The game also
issues many repeated `set(...,0x07)` calls while describing rooms and inventory.
To distinguish actual writes from redundant calls, run:

```sh
python3 tools/trace_game_logic.py --scenario objects --native-only \
  --verify-overlays --trace-state-writes --output build/action-write-check
python3 tools/summarize_action_effects.py \
  build/action-write-check/native.trace --changed-only
```

This traces native object-state writes without replacing the game's routines.
In the original room, the trace found actual transitions for the drawer's
open/interaction flags (`0x0c`, `0x02`), the newly exposed book's `0x07`, and
the book's location/interaction flags when taken and dropped. Repeated LOOK
and INVENTORY commands made **no additional flag changes**. The
[action-dispatch notes](docs/action-dispatch.md#which-service-calls-actually-changed-state)
give a controlled `--command-sequence` replay for testing repeated actions.

That repeat replay confirmed persistent object flags: reopening the drawer
changes only `0x0c`, and taking the book a second time changes only its
location. An initial, independent **C++ story-state model** now reproduces
those confirmed mutations without executing the DOS game:

```sh
ctest --test-dir "${GATEWAY_BUILD_DIR:-build}" -R story-model --output-on-failure
```

This model is deliberately limited to the *effects of already-successful*
OPEN, CLOSE, TAKE and DROP commands, plus a read-only reproduction of the
ordinary OPEN/CLOSE property checks (including their original order). It is
not a new parser or interpreter; earlier action guards, containment updates
and story callbacks are documented in
[action-dispatch.md](docs/action-dispatch.md#first-standalone-c-state-model).

To investigate several action sequences in a single run, after `gateway-build`:

```sh
python3 tools/run_action_matrix.py --output build/action-matrix-001
```

The first six clean native replays cover ordinary/repeated OPEN, CLOSE, TAKE
and DROP as well as potentially invalid orders; two additional scenarios
probe visibility after closing a drawer. Each produces a trace; a combined
`report.md` lists only **verified changed object-state writes** per command.
It does not infer success from the DOS action return code or assert full
gameplay equivalence. The runner supports `--list`, `--only`, and `--dry-run`;
see [action-dispatch.md](docs/action-dispatch.md#run-several-action-experiments-in-one-batch).

The user-supplied original six-run report is now an automated regression for
the standalone C++ object-state model (42 scripted commands). Compare a new
matrix directly, with no DOS guest required for the comparison itself:

```sh
"${GATEWAY_BUILD_DIR:-build}/gateway-story-check" build/action-matrix-001/report.md
```

This checker intentionally implements **only the first-room hypotheses** for
command reachability. A mismatch is useful evidence for refining that logic,
not proof that the native engine or the original game is faulty. See
[action-dispatch.md](docs/action-dispatch.md#results-of-the-first-complete-six-scenario-batch).

The two follow-up visibility scenarios both matched the C++ model. Further
investigation now targets a **separate, earlier action-selector callback** in
the original executable. TAKE and DROP have different nonzero selector bytes
(`0x08` and `0x04`) while OPEN/CLOSE have zero. Add `--trace-admission` to
`tools/run_action_matrix.py` to collect the live callback pointer, selector
index, raw return AX, and subsequent action dispatch for each guest command
slot. See [the action research notes](docs/action-dispatch.md#trace-the-earlier-selector-callback-separately-from-action-dispatch)
for the bounded three-scenario replay; the selector's meaning remains unknown.

That three-scenario replay has now identified **actual TAKE/DROP pre-action
handlers**, originally at `GATEMAIN.OVL:0xfbaf` and `0xd804`. For the first
room, repeated TAKE when the book is held and repeated DROP when it is not
held return `AX=1` and intercept the action; successful commands return zero
from the selector and proceed to the ordinary handler. A hidden book in a
closed drawer never reaches the selector at all. The C++ first-room model
reproduces all nine supplied scenarios and their selector observations; nested
containers and general parser visibility remain unimplemented. See
[the callback disassembly](docs/action-dispatch.md#verified-takedrop-pre-action-callbacks).

A separate **parser candidate traversal** is now partially decoded. The
original EXE uses a child-slot list distinct from the handler-side relation
links, and a flag-based slot predicate (`0385:0a9a`) when a caller requests
restricted enumeration. The snapshot exporter includes both `child_links` and
`relation_links`; the standalone C++ model reproduces the slot predicate but
does not yet use it to resolve commands. To collect native evidence around
OPEN/CLOSE, run:

```sh
gateway-build
python3 tools/trace_game_logic.py --scenario objects --native-only \
  --verify-overlays --trace-visibility --output build/parser-child-check
python3 tools/summarize_visibility_trace.py \
  build/parser-child-check/native.trace --object 0x124
```

This logs the predicate's AX result separately from child lookups; the
summary's command labels are *time windows*, not confirmed parser decisions.
The first native replay now confirms that OPEN/CLOSE changes the drawer's
slot-0 eligibility while TAKE empties its child pointer without closing the
drawer. To align both effects with native service calls in the same trace:

```sh
python3 tools/correlate_visibility_trace.py \
  build/parser-child-check/native.trace --container 0x124 --child 0x123
```

The original **indexed child-link setter** is now identified at `0157:08fc`
(file `0x2e3a`). It is separate from the object's direct-location setter.
The watched replay confirms that TAKE invokes this setter to clear the book
from the type-2 drawer's first child-list head at offset `+0x14`. Original
`GATEMAIN.OVL:0xc830` contains a linked-list relocation routine, now
translated for type-2 records into the standalone `StoryLinkedChildren`
research model. Use `--trace-object-writes 124 --trace-visibility` with the
object scenario and `tools/summarize_object_writes.py --child-only` to inspect
the actual writer and its far return address. The saved trace returns to
`4319:169e`, resolved through RTLink section 4 to the direct far call at
`GATEMAIN.OVL:0xc879`. Optional `--overlay-map` and `--overlay-file`
arguments check original call bytes before reporting source offsets (important
because multiple RTLink sections share a runtime bank). `--all-setters`
lists *every* child setter call recorded during the same run, including
destination containers and subsequent actions; it requires no new replay.
With the original OVL/RTLink map provided, `--relocations` pairs the
byte-verified unlink/insert callsites and reports the inferred move. The
repeated first-room replay now shows the book moving drawer → inventory →
room → inventory. The second TAKE restores room head **`0x175`**, evidence
that a head word points into a sibling chain rather than representing the
room's entire object list. The saved native snapshot confirms that `0x175`
is the **picture**, followed by the plant, furniture, desk and player, and
that the book is once again inside the player. `inspect_object_state.py` now
exports original `child_slot`/`next_sibling` fields;
`tools/inspect_child_chains.py` follows
complete ordered child lists from a snapshot and checks inconsistent links.
Its `--audit` option scans every decoded container for dangling children,
metadata disagreements, repeated references and cycles. The supplied full
snapshot contained 182 nonzero heads, 404 distinct linked objects and no
consistency findings. The remaining 330 records can now be classified by
native type, owning-root ancestry and their direct-location fields using
`--audit --list-unlinked`; a missing indexed parent is not necessarily an error.
The C++ linked-child
research model now tests the actual mixed-type room hierarchy (type 1 room,
type 8 picture/plant, type 2 furniture, type 7 player), including the
picture-head restoration after DROP/TAKE.
See [the child-list writer research](docs/action-dispatch.md#identify-the-native-child-list-writer)
for the commands, mapped calls, and the distinction between observed writes
and verified changes. See [snapshot child-chain inspection](docs/object-state.md#enumerating-siblings-instead-of-just-child-heads)
for use against room, drawer, or inventory.

`tools/export_story_world.py` can now turn a checked native object snapshot
into a JSON containment forest or a Markdown list of type-1 room candidates,
their scene objects, nested contents, and separately identified unattached
records. Schema 2 additionally preserves native relation words and presents
type-6 door/archway *connector candidates*, with referenced object names and
types. The captured full snapshot includes 110 type-1 room candidates, but
only 50 have direct type-8 children; those children are often scenery and
cannot be assumed to select the background image. This preserves ordered
siblings, object types, names, handler IDs, and separate relation fields as
a starting point for a future native renderer, **not** a claim to have
recovered the room-transition graph or the game scripts. See
[the world-snapshot export](docs/object-state.md#export-an-initial-typed-world-snapshot-for-a-native-renderer)
for example commands and limitations.

The follow-up schema-2 snapshot confirms eight type-6 records holding two
distinct room IDs (seven other records have zero links). For example, a door
references Travel Pod and Satellite, and a rope ladder references Garden and
Tree House. `--format dot` draws only these **unverified, undirected passage
candidates** for visual inspection; it does not claim every room has an exit
or that traversability has been recovered.

The [original 1992 hintbook maps](docs/gateway-map-reference.md) provide an
independent reference: eleven map panels covering Gateway Station, six
planetary locations and both endgame phases. All eight populated type-6
endpoint pairs occur on those maps. The research notes cross-reference the
printed room names with native object-ID ranges and distinguish up/down
arrows and puzzle-gated passages; no copyrighted map scans are bundled.
The scripted EAST/WEST replay has now **verified four native player moves**:
`0x0ee` Your Quarters → `0x0f1` Heecheetown → `0x0ee`, repeated twice.
`tools/summarize_navigation_trace.py` correlates those `0x0d3` location-set
services with direct record writes, their guest return addresses, and—when
given the original `GATEMAIN.OVL` and RTLink manifest—**byte-verified**
original far-call sites. All four calls returned to `4319:16e7`, and the
user's original-overlay check **confirmed** direct `CALL 0157:064b` at OVL
`0xc8c2` inside the same shared object-move helper used by TAKE/DROP. The
child-head setter trace independently maps the original unlink/insert sites
and shows the player being prepended to each destination room; the second
EAST restores Quarters' former head, picture `0x175`. The optional
`--child-heads` report combines these observations. A `--trace-navigation`
replay hook records entry stacks at the shared move helper. Its first native replay confirms
the player/destination/slot arguments and identifies the RTLink manager
return at `2c24:0701`; saved BP returns consistently reach `148e:065a`,
`148e:0c28`, and `1000:04da`. The new `--move-callers` summary filters out
unrelated startup object moves. Disassembling those resident frames led to
the **original EXE room-exit scanner** at `148e:05f6` (file offset `0x60cf`):
type-1 rooms provide an exit count and one-based first entry, and the global
exit table contains four-byte `{direction, kind, argument}` records.
`tools/inspect_room_exits.py` exports every room's entries from a native
memory snapshot as text, JSON or a conservative direct-destination DOT graph.
The replay now also hooks the scanner; `summarize_navigation_trace.py
--direction-calls` correlates its direction argument with nearby verified
player movement. The user's repeated replay confirms numeric **EAST `0x03`**
and **WEST `0x07`** and finds both use kind-2 exits. Original EXE code
verifies that kind 2 indexes a second four-byte table containing a destination
and an object ID whose flags gate movement. The inspector now decodes those
records and optionally draws **dashed conditional candidate edges** with
`--format dot --include-conditional`; `--format stats` reports coverage.
Other compass codes, kind-2 flag behavior and scripted exit kinds still need
native verification. The follow-up snapshot identifies **`0x1ab` room
door** as the shared condition object for the three first-room entries.
`--condition-flags` reports guard bits in the final guest snapshot. Across
the full world there are **518 exit records**, including **236 kind-4
callbacks**. Original EXE disassembly verifies that these callbacks can
return dynamic destination IDs; the replay now captures their AX returns,
and `summarize_navigation_trace.py --callback-returns` correlates them with
verified movement when a kind-4 exit is exercised. See
[the navigation trace and next probes](docs/gateway-map-reference.md#verified-ordinary-first-room-movement).

The standalone C++ model exposes the corresponding *local slot-filtering*
stage, but it still does not perform parser traversal or name resolution.
See [resident parser candidate iterator](docs/action-dispatch.md#resident-parser-candidate-iterator)
for offsets, controls, limitations, and a repeated OPEN/CLOSE command sequence.

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

## Decompilation

Gateway's game logic is compiled native code, not bytecode: 696
object/event handlers, 195 verb handlers and 44 room callbacks call shared
engine routines. [The decompilation plan](docs/decompilation-plan.md)
reconstructs that layer in C++, services first and then handlers, inside the
running original. Each routine is checked against the native code, and
progress is tracked in [`decomp/functions.tsv`](decomp/README.md).

## License

The project has three licensing layers, tracked per file with REUSE
(`REUSE.toml` and SPDX headers; `nix flake check` runs `reuse lint`):

| Part | License |
| --- | --- |
| Runtime, tools, tests and documentation | GPL-3.0-or-later **with a linking exception** ([GPL text](LICENSES/GPL-3.0-or-later.txt), [exception](LICENSES/GPL-3.0-linking-exception.txt)) |
| Reconstructed game logic (`src/story_model.*`, `tests/story_model_tests.cpp`) | [Gateway-Derived Source Notice](LICENSES/LicenseRef-Gateway-Derived.txt): **not GPL**; derived from the original program, whose rights stay with its holders; no license to the original is granted |
| `third_party/nuked-opl3` | LGPL-2.1-or-later (Nuke.YKT) |
| `third_party/font8x8` | Public domain (Daniel Hepper) |
| Original game executables, data, art, text, sound and music | Not part of this repository; they keep their own rights and must come from your own copy of Gateway |

Code that reimplements game logic from tables, constants or control flow read
out of the original program (rather than file formats or hardware behavior)
carries the `LicenseRef-Gateway-Derived` license identifier. The GPL section 7
exception allows the GPL code to be linked or combined with those files and
the result to be conveyed, provided their source accompanies it. It grants
nothing for the original game. Never commit original assets, executable
bytes or Ghidra pseudocode.
