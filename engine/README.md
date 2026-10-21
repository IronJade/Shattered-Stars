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
| MDEC + STR movies | decoder done, exact against jPSXdec's PS1 mode; player being ported |
| SPU (sound), memory cards | not started (stubbed; see `docs/CHECKLIST.md`) |
| Game code | boot, title program (logos, title screen, New Game screens) ported; field program next |

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
# Texture viewer: Left/Right change entry, Up/Down change block, V shows all of VRAM.
engine/build/shattered-stars --disc "Source BIN files/Star Ocean - The Second Story (Disc 1).bin"

# Write an entry's textures as PNGs instead of opening a window.
engine/build/shattered-stars --disc "…Disc 1.bin" --entry 10 --dump analysis/render/entry10
```

Keyboard pad: arrows = d-pad, Z = Cross, X = Circle, A = Square, S = Triangle,
Enter = Start, Backspace = Select, Q/W = L1/R1, 1/2 = L2/R2.

## Tests

```sh
SS_DISC1="Source BIN files/Star Ocean - The Second Story (Disc 1).bin" engine/build/ss_tests
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
