# Shattered Stars engine

A native, hand-ported reimplementation of *Star Ocean: The Second Story* in C++20,
following the method of [Severed Chains](https://github.com/Legend-of-Dragoon-Modding/Severed-Chains):
each game function is rewritten by hand from the decompilation, on top of a native
replacement for the PS1 hardware. It is not an emulator, and it contains no game code
or data: everything is read at runtime from your own disc images.

## Status

| Layer | State |
|---|---|
| Disc image (`.bin` / `.cue` / `.iso`) | done |
| Archive table + SLZ decompression | done, byte-identical to the original |
| GPU (VRAM, polygons, sprites, lines, transfers, blending, mask, dithering) | software renderer, unit-tested |
| Window, OpenGL presentation, keyboard and gamepad | done |
| GTE + libgte | done, bit-exact with PCSX-Redux / Sony's PsyQ 4.2 code |
| MDEC + STR movies | decoder and player done, exact against jPSXdec's PS1 mode |
| SPU (sound), memory cards | SPU, libspu and the game's sound driver ported (music and effects match PCSX-Redux); memory cards (raw 128 KB images) |
| Game code | boot, title (logos, movies, menu, New Game, Continue, Voice Collection); field maps (event scripts, messages, actors, doors, chests, shops, encounters, effects, embedded map code, the credits and the end screen); world map (vehicles, bunny, courier, map); every camp menu page, item creation, save / load (byte-compatible with real memory cards); battles (combat, HUD, result screen, every enemy class, Claude, Rena, Celine, Leon, Noel, Ernest, Bowman, Chisato, battle items). Spells, the other character modules and party AI are in progress — see `docs/CHECKLIST.md` |

Port progress per function is the `port` column of `docs/catalog/functions.csv`.

## First run

You need images of your own discs, North American release: each disc's `.cue` file with
its `.bin`, or an `.iso` / `.img` image. Disc 2 is optional until the story asks for it.

**Windows:** unzip `ShatteredStars-win64.zip` anywhere and run **`setup.exe`**. On the
Discs page, choose Disc 1 (and Disc 2) with *Browse...*. The other pages are Display,
Sound, Memory cards and Mods. Then press *Save & Play*. Or just run
`shattered-stars.exe`: it opens setup by itself while it has no disc.

**To skip browsing,** put the disc images in the `discs` folder next to the game
(subfolders are fine). The game finds them by itself, and setup's *Auto-detect* fills
them in.

**Linux:** the same two programs sit side by side in the build folder, `shattered-stars`
(the game) and `shattered-stars-setup` (setup). They work the same way.

| | Windows | Linux |
|---|---|---|
| The game | `shattered-stars.exe` | `shattered-stars` |
| Setup | `setup.exe` | `shattered-stars-setup` |
| Settings | `%APPDATA%\ShatteredStars\config.ini` | `~/.config/shattered-stars/config.ini` (or `$XDG_CONFIG_HOME/shattered-stars/`) |
| Memory card 1 | `memcard1.mcd` beside `config.ini` | the same |
| Mods | `mods\` beside `config.ini` | `mods/` beside `config.ini` |
| Disc folders | `discs\` next to the game, or beside `config.ini` | `discs/` next to the game, or beside `config.ini` |

While playing, F11 or Alt+Enter switches between the window and full screen, and Esc
quits. `config.ini` is plain `key=value` text: setup writes it, and keys it doesn't know
are kept. The keys are listed in `src/launcher/config.h`.

## Build

Needs CMake 3.24+, a C++20 compiler and, ideally, SDL3 development files
(`sudo dnf install SDL3-devel` / `apt install libsdl3-dev`). Without them the build
fetches SDL 3.4.18 (pinned by SHA-256) and builds it, but that copy only has audio
if your system has the PipeWire, PulseAudio or ALSA development headers.

```sh
cmake -S engine -B engine/build -G Ninja
cmake --build engine/build
```

### Windows

The Windows build is cross-compiled from Linux with MinGW-w64 (on Fedora:
`sudo dnf install mingw64-gcc-c++ mingw64-SDL3`):

```sh
tools/build_windows.sh   # -> dist/ShatteredStars-win64.zip: shattered-stars.exe, setup.exe,
                         #    SDL3.dll, this README and an empty discs\ folder
```

The C / C++ runtimes are linked in, so the zip holds everything the game needs (see
*First run*). The Windows build renders the same frames as the Linux one (checked byte for
byte), and its test suites pass under Wine (`engine/build-win/ss_tests.exe`,
`ss_game_tests.exe`).

### Linux package

```sh
tools/build_linux.sh   # -> dist/ShatteredStars-linux-x86_64.tar.gz: shattered-stars,
                       #    shattered-stars-setup, libSDL3.so.0, README, discs/
```

libstdc++ is linked in (`sudo dnf install libstdc++-static`), and SDL3 is copied beside
the programs, which load it from there (RUNPATH `$ORIGIN`). The player needs only glibc and
OpenGL 3.3. The glibc version is that of the system the package was built on (Fedora 44:
2.43); the script prints it and writes it in the package's `README.txt`. Build on an older
distribution to support older systems.

### Setup program

The setup program uses Dear ImGui (MIT licence), fetched by CMake and pinned by SHA-256.
`-DSS_BUILD_SETUP=OFF` leaves it out. To check its layout without opening a window,
`tools/setup_screenshots.sh [PROGRAM] [OUT_DIR]` renders every page and state offscreen as
PNGs, with scratch settings. Pass `engine/build-win/setup.exe` to run the Windows build
under Wine. setup's own test options are listed at the top of `src/setup/setup_main.cpp`.

## Run

```sh
# Settings window (discs, display, sound, memory cards, mods):
engine/build/shattered-stars-setup
# Play: opens the game window. Disc paths given here are remembered in
# ~/.config/shattered-stars/config.ini, so later runs need no arguments.
engine/build/shattered-stars --disc1 ".../Star Ocean - The Second Story (Disc 1).cue" \
                             --disc2 ".../Star Ocean - The Second Story (Disc 2).cue"
engine/build/shattered-stars
```

Memory card 1 is `memcard1.mcd` next to `config.ini` (created formatted on first use);
`--memcard1 FILE` / `--memcard2 FILE` use other raw 128 KB images. A DexDrive save
(`.gme`) converts with setup's *Make a .mcd copy* button or with
`tools/gme_to_mcd.py SAVE.gme CARD.mcd` — point `--memcard1` at a copy, since the game
writes to the card it uses. Headless runs (`--headless`) use only cards named for the run
(`--memcard1` / `SS_MEMCARD1`) and never write `config.ini`.

Keyboard pad: arrows = d-pad, Z = Cross, X = Circle, A = Square, S = Triangle,
Enter = Start, Backspace = Select, Q/W = L1/R1, 1/2 = L2/R2. Gamepads work through SDL and read as a DualShock in analog mode: the left stick walks
(and works the d-pad in menus), and the game's Vibration setting drives the rumble.

Other modes:

```sh
# Texture viewer: Left/Right change entry, Up/Down change block, V shows all of VRAM.
engine/build/shattered-stars --viewer
# Write an entry's textures as PNGs instead of opening a window.
engine/build/shattered-stars --entry 10 --dump analysis/render/entry10
```

World edits: patch files change map scripts and game data as they load. Put `*.patch`
files in `mods/` next to `config.ini` (or pass `--mods DIR`); they apply in name order,
and every patch carries the value the disc holds, so the game skips (and logs) a patch
whose data no longer matches. The disc images are never modified. `tools/so2_mod.py`
reads your disc and writes the lines (`-o FILE` appends to a file):

```sh
tools/so2_mod.py list    DISC.cue MAP                # a map's chests and doors
tools/so2_mod.py chest   DISC.cue MAP FLAG --item ID # (or --fol N)
tools/so2_mod.py door    DISC.cue MAP INDEX --to MAP2 --x X --z Z
tools/so2_mod.py items   DISC.cue ITEM               # an item record's fields
tools/so2_mod.py item    DISC.cue ITEM FIELD VALUE   # e.g. item 0x15 price 4321
tools/so2_mod.py shops   DISC.cue                    # every shop's stock
tools/so2_mod.py shop    DISC.cue SHOP INDEX ITEM    # INDEX `add` appends, ITEM 0 removes
tools/so2_mod.py enemies DISC.cue [TYPE]             # enemy stats from the encounter packs
tools/so2_mod.py enemy   DISC.cue TYPE FIELD VALUE   # e.g. enemy 4 hp 9999
```

The patch lines (numbers in decimal or 0x hex, `#` comments; details in
`src/game/field/mods.h`):

| line | changes |
| --- | --- |
| `script MAP WORD EXPECTED VALUE` | a word of map MAP's event script |
| `item ITEM FIELD EXPECTED VALUE` | item ITEM's record, wherever the game loads it (menus, shops, battles, the field) |
| `shop SHOP INDEX EXPECTED VALUE` | stock entry INDEX of shop SHOP |
| `enemy TYPE FIELD EXPECTED VALUE` | the stats of every enemy of type TYPE when a battle loads |

Item fields: `price`, `atk`, `def`, `amount`, `effect`, `type`, `sell`; enemy fields:
`hp`, `mp`, `exp`, `fol`, `speed`, `drop1`, `drop2`, `atk`, `def`, `int`, `level`, `eva`,
`acc`, `guts`, `drop1_odds`, `drop2_odds`; or a byte offset `OFFSET[:SIZE]`.

Debug console: start with `--debug` and type commands into the terminal (`help` lists
them): `where`, `warp MAP [X Y Z [DIR]]`, `fol N`, `item ID [COUNT]`, `heal`,
`flag N [0|1]`, `global OFF [VALUE]`, `battle ENC [STAGE]`, `noclip`. The status line
(map, field state, the leader's position, Fol) and the console's last lines are shown
over the top of the picture and in the window title; without `--debug` neither appears.

Debugging switches (start on a map, battles, traces, headless runs) are listed in
`docs/HANDOFF.md`.

## Tests

```sh
SS_DISC1=".../Star Ocean - The Second Story (Disc 1).cue" engine/build/ss_tests
SS_DISC1=".../Star Ocean - The Second Story (Disc 1).cue" engine/build/ss_game_tests
tools/smoke_field_events.sh engine/build/shattered-stars   # whole-game headless checks
```

The disc tests compare against reference values from `tools/so2_archive.py` and the
live RAM dumps; without `SS_DISC1` they are skipped and only the unit tests run.

## Layout

| Path | Contents |
|---|---|
| `src/disc/` | disc image, archive table, SLZ |
| `src/psx/` | replacements for the PS1 hardware (GPU, GTE, MDEC) |
| `src/runtime/` | PsyQ-shaped libraries (libgpu, libgte, VSync) over the host |
| `src/platform/` | SDL3 window, OpenGL presentation, input |
| `src/game/` | ported game code, named as in `docs/catalog/functions.csv` |
| `src/util/` | PNG writer |
| `tests/` | unit and disc regression tests |
