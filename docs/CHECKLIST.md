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
- [x] SPU: ADPCM voices, ADSR, pitch, reverb, transfers; SDL3 audio output (`psx/spu`, `runtime/libspu`, `platform/audio`; unit tests; title music within 0.03 dB / 0.975 envelope correlation of PCSX-Redux; builds against the system SDL3 (PipeWire, PulseAudio, ALSA backends))
- [x] Game sound system (System + 0x5aa4): bank format, sequencer, music/SFX (`game/boot/sound`; bank #8 byte-identical in SPU RAM, the title music's 974 driver calls and the confirm effect identical to the original in PCSX-Redux, Sound Select tune's notes identical; prologue voice clips stream through SpuSt as in the original: 2078 refills, IRQ addresses and timing identical; the field program's sound calls ported)
- [x] MDEC + STR movie decoding: all 23,869 frames on both discs decode; exact against jPSXdec's PS1-accurate mode (`psx/mdec`, `disc/str`)
- [x] Movie audio: nothing to do — Star Ocean 2's STRs are video only; the music is a sequence the game starts once 10 frames are in (played by the ported sound driver)
- [x] CD streaming reads for movies: the boot loader's two-slot ring (STR header tables + payloads) filled as `cd_sector_ready_callback` does, sectors timed at double speed on a console clock (`game/title/movie.cpp`; to move into the Loader with asynchronous reads and a seek model)

## 2. Boot and title program (#1)

- [x] Character starting data for the title program: Claude / Rena (`init_claude`, `init_rena`, `roll_talents`); characters 3-12 belong to the field program's copy
- [x] New Game option screens (`new_game_sequence` 0x80042cc8): Sound Select, Vibration, Claude/Rena, name entry, combat mode, Battle Rank, memory card notice — window/text system ported; screen timings identical to the original (window constructors at the same vsync), static frames within ±1 of PCSX-Redux
- [x] Background load queue (`async_load` family, 0x80048f34-0x800492d0)
- [x] Prologue player after New Game (0x8004fa98 class; entries #2680/#2681, #4504-#4507) — class ported (`Prologue` in `game/title/movie.h`, cue lists #4504/#4507 run in `ss_game_tests`); called from `start_new_game` for both characters (all three of Claude's parts with the background loads, Rena's part); the run reaches the field hand-off. Music plays; the streamed voice clips (sound_open_clip 0x800197ec) are still stubbed
- [ ] Disc 1 prompt before New Game (0x8003c5ec)
- [x] Reference comparison: `tools/redux_reference.lua` (original game in PCSX-Redux) + `tools/compare_frames.py`
- [x] Scripted controller input for tests (`--input FRAME:buttons,...`)

- [x] `System` object (loader, pad, sound, handoff registry) and boot flow (`boot_main`)
- [x] Loader API on the host: entry loads (complete immediately), spanning reads, cancel, poll, disc check
- [x] Program chaining: `boot_run_main_program` → native programs (title done; field / battle pending)
- [x] Boot pad / clock module (0x80013fb8-0x8001441c) and the title screen manager (double buffering, arenas, OTs)
- [x] `title_program_main` flow up to the title screen (sound bank / config / game-state creation)
- [x] `title_intro_sequence`: logos and tri-Ace particles (pixel-exact); studio and opening movies; sound bank loaded and music played by the sound driver
- [x] `title_screen`: menu, cursor, confirm, fade, attract movie, movie-viewer list
- [ ] Voice Collection (0x8003fb78)
- [ ] Load-game screen (0x800317e0) — needs the memory card
- [x] Hand-off to the field program after New Game (`start_new_game`: config reset, leader slot, name, #3100 loaded, `boot_run_main_program`)

## 3. Field program (#3100)

Openings differ by leader: **Claude** starts on **map 7** (entry 3214, Milokeenia: Ronixis
and the survey team, "Well? Any results different from the pre-landing survey?");
**Rena** starts on **map 18** (entry 3225, Arlia: Rena and her mother Westa, "I'll be going." /
<<<<<<< HEAD
"Wait!"). Set by the field's new-game state block (field 0x8005ea54: game_state+0x1a = 7 or 0x12
from config+0x45).
=======
"Wait!").
>>>>>>> 09fbfde30dc39f6a0ee5f6db5a697df5b7d9647c

- [x] Field program skeleton: state block (new game / load snapshot), system data #3110, screen, task list, main loop (0x80049bfc)
- [x] Map container load (entry 3207 + map): sections 0 (textures, CLUTs, scene) / 1 (script, font) / 2 (sprites); party sprite packs
- [x] Async load queue (0x80043050) and preload table (0x80056374-0x800572c0); field_load_entry goes through them
- [ ] Map container sections 3 (world terrain) and 5-12 (embedded map code, ~25 maps)
- [x] BG renderer (overlay 0x8008672c / bg_draw 0x80086f00): static, parallax and scrolling pieces, animation and colour records; map 7 draws into VRAM
- [x] Subsystems created in the original order (field_create_subsystems 0x800494dc): Screen, ScreenFade, ScriptCtx, MsgSystem, TextPanels, LoadQueue, Notice; map loads start the VM (vm_init) and the message font; the VM's services call the real field program, message, notice, fade, BG, sound, music and preload code (`vm_services.cpp`)
- [x] Field music install (field_install_music 0x8004b2b4: bank to the sound driver, sequence unpacked); music kept across a battle (handoff 0xf, table B 0x5a / 0x5b)
- [x] field_run's return path (0x80048a84): battle outcome to script register R, escape count, preloads back from handoff 10 / 0xc, party slot ids, encounter flags; inventory stash (handoff 0x11, table A 0x10 / 0x11 / 0x34) — untested until the battle program exists
- [ ] ActorSystem, FieldPanel, Callways, SceneRenderer in field_create_subsystems; actor and camera-script services
- [ ] Camera follow / scroll from the leader (needs actors)
- [ ] field_update (0x8004c334): play state (no-encounter path) and state 1 done; encounters, camp menu, battle start/return, vehicles to do
- [ ] Actors and character models
- [x] Event-script VM port (core, extended, tables A/B): `game/field/script_*.cpp`, one thread + 58 trigger regions, wait states, sync calls, talk probe, choice prompt; every other subsystem behind `vm_services.h` (stand-ins until ported). Unit-tested; the openings of maps 7 / 18 and every map's main function (964 scripts) run with no unclaimed instruction; the openings execute the same ext instructions, in the same order, as the original in PCSX-Redux (`tools/redux_vm_trace.lua`: map 7 132 of 132 up to Ronixis's first window, map 18 86 of 86 up to Rena's)
- [ ] Event trigger table: there is none on disc; each map script builds it at load time (trigger regions ext 0x74/0x75/0xd8/0xd9, NPC talk entries ext 0x70/0xf3, chests B 0x6c). VM side ported; needs actors and the walk code
- [x] Text: glyph table and font rendering (`game/field/message_text.cpp`): map font + 0xfc2 font, byte-stream codes, all control codes 0x4000-0x400f, word wrap / justification / centring, names, numbers, item strings, ruby, icons; lines rendered with shadow into 4-bit cells in VRAM. Glyph positions equal the original's RAM; the text is pixel-identical to PCSX-Redux on map 7 (Ronixis, Science Officer, the map-name notice)
- [x] Message windows and choices (`game/field/message*.cpp`, `notice.cpp`, `fade.cpp`): MsgSystem (20 windows, cell allocator), open/print/scroll/wait/close state machine, frames, gradient body, button prompt, choice cursor, notice banner, ScreenFade. Compared with PCSX-Redux captures (tests/test_field_message.cpp): 539 window frames (opening, typing, button wait, close, a second window) and 237 notice frames identical except window bodies one step off on some gouraud pixels (GPU, section 1) and the notice icon spin / sparkle (scaled or transposed textured quads: GPU texture stepping; the primitives equal the original's RAM); the map fade-in is identical over its 120 frames. Choices checked numerically (no capture of the original yet); wipes, iris and cross-fade ported but not compared
- [x] Field code overlay 0xc1d catalogued (108 functions: BG, field actors, walking/collision, camera pan queue, effects); 0xc1e is the world-map overlay
- [ ] World-map overlay 0xc1e; the 0x801ea000 / 0x801f9800 code
- [ ] Field menus

## 4. Battle program (#2129) and its overlays

- [x] Identify and catalog the battle program: #2129 confirmed (field_exit_to_program 0x80051c9c runs it with the 0xc0-byte setup in handoff 5; it writes the outcome to config+0x178 and returns to #3100). 931 functions, 187 overlay functions (#1984 blocks 0-10, #1981), 405 globals in `docs/catalog`; structure, data formats (encounter packs #1405-#1968, stage lists, item table #1983, party code #2130+) and a port order in `docs/notes/battle.md`
- [ ] Battle flow, rendering, AI overlays, effects

## 5. Completion

- [ ] Save / load compatible with the original memory card data
- [ ] Disc 2 swap
- [ ] Windows build
- [ ] Modding hooks (data overrides for chests, doors, items)
