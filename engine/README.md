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
| Game code | boot, title (logos, movies, menu, New Game, Continue); field maps (event scripts, messages, actors, doors, chests, encounters, effects); world map; camp menu (first page, save / load); battles (combat, HUD). Result screen, shops, most menu pages and vehicles are in progress — see `docs/CHECKLIST.md` |

Port progress per function is the `port` column of `docs/catalog/functions.csv`.

## Build

Needs CMake 3.24+, a C++20 compiler and, ideally, SDL3 development files
(`sudo dnf install SDL3-devel` / `apt install libsdl3-dev`). Without them the build
fetches SDL 3.4.18 (pinned by SHA-256) and builds it, but that copy only has audio
if your system has the PipeWire, PulseAudio or ALSA development headers.

```sh
cmake -S engine -B engine/build -G Ninja
cmake --build engine/build
```

## Run

```sh
# Play: opens the game window. The disc paths are remembered in
# ~/.config/shattered-stars/config.ini, so later runs need no arguments.
engine/build/shattered-stars --disc1 ".../Star Ocean - The Second Story (Disc 1).cue" \
                             --disc2 ".../Star Ocean - The Second Story (Disc 2).cue"
engine/build/shattered-stars
```

Memory card 1 is `memcard1.mcd` next to `config.ini` (created formatted on first use);
`--memcard1 FILE` / `--memcard2 FILE` use other raw 128 KB images. A DexDrive save
(`.gme`) converts with `tools/gme_to_mcd.py SAVE.gme CARD.mcd` — point `--memcard1` at a
copy, since the game writes to the card it uses.

Keyboard pad: arrows = d-pad, Z = Cross, X = Circle, A = Square, S = Triangle,
Enter = Start, Backspace = Select, Q/W = L1/R1, 1/2 = L2/R2. Gamepads work through SDL.

Other modes:

```sh
# Texture viewer: Left/Right change entry, Up/Down change block, V shows all of VRAM.
engine/build/shattered-stars --viewer
# Write an entry's textures as PNGs instead of opening a window.
engine/build/shattered-stars --entry 10 --dump analysis/render/entry10
```

World edits: `tools/so2_mod.py list DISC.cue MAP` shows a map's chests and doors;
`tools/so2_mod.py chest DISC.cue MAP FLAG --item ID` (or `--fol N`) and
`tools/so2_mod.py door DISC.cue MAP INDEX --to MAP2 --x X --z Z` append patches to a file
(`-o`). Put `*.patch` files in `mods/` next to `config.ini` (or pass `--mods DIR`); the
game applies them as maps load and skips any patch whose original word has changed. The
disc images are never modified.

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
