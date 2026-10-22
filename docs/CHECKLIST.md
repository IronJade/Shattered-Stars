# Shattered Stars — port checklist

Goal: *Star Ocean: The Second Story* running natively in a standalone window, read
from the user's own `.bin`/`.cue` images:

- `/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/`
- `/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 2)/`

Method: pure hand port in C++20 + SDL3 + OpenGL (see `engine/README.md`). An item is
ticked only when it is implemented **and** verified (test, byte comparison against the
original, or a rendered result checked by eye). Names follow `docs/catalog/`.

## Milestones

- [x] **M1** Standalone window boots from the `.cue` and shows the startup logos (SCEA, tri-Ace, Enix) — pixel-identical to the original (no-dither comparison)
- [x] **M2** Opening movie plays — studio logo, opening and attract movie through the ported player (`game/title/movie`); every frame shown exactly as decoded (jPSXdec-exact), within ±3 levels of PCSX-Redux's MDEC; frame timing from a drive/MDEC model (same flip vblank as Redux for the opening's first 135 frames; Redux's 60.008 Hz vblank then drifts 1 field per ~230 frames, same wall-clock length)
- [x] **M3** Title screen is interactive — menu, cursor, confirm/fade, attract timer; pixel-identical to the original (with a memory card inserted)
- [ ] **M4** New Game reaches the first map and the player can walk
- [ ] **M5** Events: dialogue, doors, chests, shops
- [ ] **M6** Menus (items, equipment, skills, save/load)
- [ ] **M7** Battles
- [ ] **M8** Disc 2 swap; the game can be finished

## 0. Foundations

- [x] Disc image reader: `.bin` (2352), `.cue`, `.iso`
- [x] Archive table of contents decryption (`loader_read_toc`)
- [x] SLZ decompression, methods 0/1/2 (byte-identical to the console)
- [x] Texture pack format + `vram_upload_packed_block`
- [x] Software GPU core: VRAM, polygons, sprites, lines, fills, transfers, blending, mask, dither
- [x] SDL3 window + OpenGL presentation + keyboard/gamepad → PS1 pad bits
- [x] Test harness with disc regression tests
- [x] Function / global / opcode / format catalog (`docs/catalog/`)

## 1. Hardware layer and PsyQ replacements

- [x] Launcher: `--disc1` / `--disc2` accept `.cue`; remembered in `~/.config/shattered-stars/config.ini`
- [x] Frame timing: `VSync(n)`, 59.94 Hz pacing, VSync callbacks
- [x] libgpu layer: DRAWENV / DISPENV, ordering tables (`ClearOTag[R]`, `addPrim`, `DrawOTag`), `DrawSync`, `SetDispMask`, `SetDrawMode`
- [x] PsyQ primitive structs and set* helpers: POLY_F3/F4/FT3/FT4/G3/G4/GT3/GT4, LINE_F2-4/G2-4, SPRT/SPRT_8/SPRT_16, TILE/TILE_1/8/16, DR_MODE/AREA/TPAGE/OFFSET/TWIN/MOVE/STP/LOAD
- [x] Reference dithering mode (`--reference-dither`: gouraud only, as PCSX-Redux's software renderer) for comparisons
- [ ] GPU: hardware-exact gouraud/UV interpolation (fixed-point stepping); today ±1 LSB vs PCSX-Redux on gradients
- [ ] GPU: subtractive blending over modulated sprites differs from PCSX-Redux by up to 2 LSB (New Game shade frames over portraits) — find which side is right
- [x] Display output from DISPENV (incl. 24-bit)
- [ ] 480i display
- [x] GTE: all 22 commands bit-exact with PCSX-Redux (incl. FLAG); libgte (PsyQ 4.2) matched against Sony's object code run in Redux (`psx/gte`, `runtime/libgte`)
- [x] Controller module the game reads (held / pressed / repeat, `vsync_callback`)
- [ ] Controller vibration setup (0x8001408c, 0x80013c0c/60/a4)
- [x] Memory card: raw 128 KB card images on the host (`psx/memory_card`: directory, block chains, checksums; .mcd/.mcr compatible), the BIOS card events / `_card_*` / `bu00:` file calls (`runtime/libcard`), and the game's card manager + driver (`game/common/memcard`); title screen pixel-identical with a card inserted
- [ ] SPU: ADPCM voices, ADSR, pitch, reverb, transfers; SDL3 audio output
- [ ] Game sound system (System + 0x5aa4): bank format, sequencer, music/SFX
- [x] MDEC + STR movie decoding: all 23,869 frames on both discs decode; exact against jPSXdec's PS1-accurate mode (`psx/mdec`, `disc/str`)
- [x] Movie audio: nothing to do — Star Ocean 2's STRs are video only; the music is a sequence the game starts once 10 frames are in (sound system, still stubbed)
- [x] CD streaming reads for movies: the boot loader's two-slot ring (STR header tables + payloads) filled as `cd_sector_ready_callback` does, sectors timed at double speed on a console clock (`game/title/movie.cpp`; to move into the Loader with asynchronous reads and a seek model)

## 2. Boot and title program (#1)

- [x] Character starting data for the title program: Claude / Rena (`init_claude`, `init_rena`, `roll_talents`); characters 3-12 belong to the field program's copy
- [x] New Game option screens (`new_game_sequence` 0x80042cc8): Sound Select, Vibration, Claude/Rena, name entry, combat mode, Battle Rank, memory card notice — window/text system ported; screen timings identical to the original (window constructors at the same vsync), static frames within ±1 of PCSX-Redux
- [x] Background load queue (`async_load` family, 0x80048f34-0x800492d0)
- [x] Prologue player after New Game (0x8004fa98 class; entries #2680/#2681, #4504-#4507) — class ported (`Prologue` in `game/title/movie.h`, cue lists #4504/#4507 run in `ss_game_tests`); called from `start_new_game` for both characters (all three of Claude's parts with the background loads, Rena's part); the run reaches the field hand-off. Sound clips/music still stubbed
- [ ] Disc 1 prompt before New Game (0x8003c5ec)
- [x] Reference comparison: `tools/redux_reference.lua` (original game in PCSX-Redux) + `tools/compare_frames.py`
- [x] Scripted controller input for tests (`--input FRAME:buttons,...`)

- [x] `System` object (loader, pad, sound, handoff registry) and boot flow (`boot_main`)
- [x] Loader API on the host: entry loads (complete immediately), spanning reads, cancel, poll, disc check
- [x] Program chaining: `boot_run_main_program` → native programs (title done; field / battle pending)
- [x] Boot pad / clock module (0x80013fb8-0x8001441c) and the title screen manager (double buffering, arenas, OTs)
- [x] `title_program_main` flow up to the title screen (sound bank / config / game-state creation)
- [x] `title_intro_sequence`: logos and tri-Ace particles (pixel-exact); studio and opening movies; sound bank stubbed
- [x] `title_screen`: menu, cursor, confirm, fade, attract movie, movie-viewer list
- [ ] Voice Collection (0x8003fb78)
- [ ] Load-game screen (0x800317e0) — needs the memory card
- [x] Hand-off to the field program after New Game (`start_new_game`: config reset, leader slot, name, #3100 loaded, `boot_run_main_program`)

## 3. Field program (#3100)

Openings differ by leader: **Claude** starts on **map 7** (entry 3214, Milokeenia: Ronixis
and the survey team, "Well? Any results different from the pre-landing survey?");
**Rena** starts on **map 18** (entry 3225, Arlia: Rena and her mother Westa, "I'll be going." /
"Wait!").

- [ ] Map container load (entry 3207 + map) and section 0 / 2 formats
- [ ] Map renderer (geometry, textures, camera)
- [ ] Actors and character models
- [ ] Event-script VM port (core, extended, tables A/B)
- [ ] Event trigger table (what binds doors/chests/NPCs to script functions)
- [ ] Text: glyph table and font rendering
- [ ] Message windows and choices
- [ ] Map overlays (0x8007e000 family) and the 0x801ea000 / 0x801f9800 code
- [ ] Field menus

## 4. Battle program (#2129) and its overlays

- [ ] Identify and catalog the battle program
- [ ] Battle flow, rendering, AI overlays, effects

## 5. Completion

- [ ] Save / load compatible with the original memory card data
- [ ] Disc 2 swap
- [ ] Windows build
- [ ] Modding hooks (data overrides for chests, doors, items)
