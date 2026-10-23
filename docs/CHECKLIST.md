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
- [x] **M4** New Game reaches the first map and the player can walk: Claude's opening on map 7 plays through (13 windows; timing and pixels match PCSX-Redux from the fade-in on, apart from the known 1-step window-body and shadow blend differences), the script moves to map 8, its scene plays, then the D-pad walks Claude around
- [x] **M5** Events: dialogue, doors, chests, shops — verified through the whole game loop (`tools/smoke_field_events.sh`: Arlia → Hearn's store, the 200-Fol chest, talking to Mr. Hearn, the ruins' shut exit, a patched shop); shops compared with PCSX-Redux at Hearn's store (Fol, inventory, recent list)
- [x] **M6** Menus (items, equipment, skills, save/load): every camp page (1-11), item creation (0xf / 0x10 / 0x13), renaming (0xe), the query page and save / load, each compared with PCSX-Redux
- [ ] **M7** Battles — combat, HUD, result screen, every enemy class, Claude's and the casters' moves, the Shot kinds the monsters use are done and compared with PCSX-Redux; left: spells and battle items (in progress), the remaining character modules, party AI, voices
- [ ] **M8** Disc 2 swap; the game can be finished. Audit in `docs/notes/m8_audit.md`:
  - Every event-script service and opcode is real. 87 services keep a stand-in only for
    VM unit tests; no shipped script reaches one.
  - An event sweep ran every trigger, Private Action, talk and pickpocket entry of all
    827 maps on both discs: 5326 entries each, no crash, no stand-in, no unknown
    instruction. What remains is 3 trigger walks the sweep cannot place and the
    endless final screen.
  - Fixed on the way: music on about 50 maps (SLZ), V1 and V2 (original_bugs.md).
  - Played through: disc 1 -> disc 2 on map 625 with the disc swap, and the credits and
    final screen from map 581.
  - Left: the private endings, and a check against PCSX-Redux from a real late-game
    save.

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
- [x] Reference dithering mode (`--reference-dither`: gouraud polygons only, no modulated textures or lines, as PCSX-Redux's software renderer) for comparisons
- [x] GPU: hardware-exact gouraud/UV interpolation: per-triangle planes with 12-bit gradients and a half-unit round (Mednafen's model), identical to the hardware VRAM dumps of ps1-tests gpu/triangle and gpu/uv-interpolation; PCSX-Redux's edge walker is up to 1 LSB off them (`docs/notes/gpu_accuracy.md`)
- [x] GPU: blending of dithered / modulated pixels: the hardware dithers and saturates the colour to 5 bits, then blends (ps1-tests gpu/lines); PCSX-Redux blends the unsaturated colour (and in 8 bits before dithering), hence its 1-2 LSB differences (window bodies, New Game portraits) — the port is right there
- [x] GPU: lines walked as the hardware does (32.32 stepping from the pixel centre, every line dithered), identical to the ps1-tests gpu/lines VRAM dump (`docs/notes/gpu_accuracy.md`)
- [x] GPU: LoadImage's DMA ignores the source address's low two bits (a misaligned image lands one pixel right: the end screen's logo, as on PCSX-Redux)
- [x] Display output from DISPENV (incl. 24-bit)
- [x] 480i display: an interlaced DISPENV taller than 256 lines shows both fields (`PutDispEnv`; the insert-disc screen's 640x480)
- [x] GTE: all 22 commands bit-exact with PCSX-Redux (incl. FLAG); libgte (PsyQ 4.2) matched against Sony's object code run in Redux (`psx/gte`, `runtime/libgte`)
- [x] Controller module the game reads (held / pressed / repeat, `vsync_callback`)
- [x] Controller: a host gamepad reads as a DualShock in analog mode (`game/boot/pad.cpp`): the Vibration setting drives SDL rumble (pad_vibrate 0x80013e14); the stick set-up the title and the save menu do (0x80013c0c / 60 / a4, the driver start 0x8001408c) is ported with pad_stick (0x80013cb8) and the vsync step that turns the left stick into the d-pad, and the field main loop fills its analog block (0x800762fc) each frame, so the left stick steers the leader as on the console. The keyboard alone stays a digital pad
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
- [x] Disc 1 prompt before New Game (0x8003c5ec): the shared insert-disc screen (`game/common/disc_screen.cpp`), pixel-identical to PCSX-Redux except its random background
- [x] Reference comparison: `tools/redux_reference.lua` (original game in PCSX-Redux) + `tools/compare_frames.py`
- [x] Scripted controller input for tests (`--input FRAME:buttons,...`)

- [x] `System` object (loader, pad, sound, handoff registry) and boot flow (`boot_main`)
- [x] Loader API on the host: entry loads (complete immediately), spanning reads, cancel, poll, disc check
- [x] Program chaining: `boot_run_main_program` → native programs (title done; field / battle pending)
- [x] Boot pad / clock module (0x80013fb8-0x8001441c) and the title screen manager (double buffering, arenas, OTs)
- [x] `title_program_main` flow up to the title screen (sound bank / config / game-state creation)
- [x] `title_intro_sequence`: logos and tri-Ace particles (pixel-exact); studio and opening movies; sound bank loaded and music played by the sound driver
- [x] `title_screen`: menu, cursor, confirm, fade, attract movie, movie-viewer list
- [x] Voice Collection (0x8003fb78, `game/title/voice_collection.cpp`): heard voices from the cards, playback, the 50 / 75 / 95% unlocks, the reacting 32x40 picture grid; the menu is pixel-identical to PCSX-Redux, the grid up to GPU rasterisation
- [x] Load-game screen (0x800317e0): Continue → slot → save list → "Do you want to load this save?" → the field on the saved map (`game/title/load_screen`, menu framework `game/common/menu_*`); save files read, unpacked (zero-run code) and checksum-verified as the original does. `SS_LOAD_SAVE=N` (or `S:N`) loads card position N without the menus
- [x] Hand-off to the field program after New Game (`start_new_game`: config reset, leader slot, name, #3100 loaded, `boot_run_main_program`)

## 3. Field program (#3100)

Openings differ by leader: **Claude** starts on **map 7** (entry 3214, Milokeenia: Ronixis
and the survey team, "Well? Any results different from the pre-landing survey?");
**Rena** starts on **map 18** (entry 3225, Arlia: Rena and her mother Westa, "I'll be going." /
"Wait!"). Set by the field's new-game state block (field 0x8005ea54: game_state+0x1a = 7 or 0x12
from config+0x45).

- [x] Field program skeleton: state block (new game / load snapshot), system data #3110, screen, task list, main loop (0x80049bfc)
- [x] Map container load (entry 3207 + map): sections 0 (textures, CLUTs, scene) / 1 (script, font) / 2 (sprites); party sprite packs
- [x] Async load queue (0x80043050) and preload table (0x80056374-0x800572c0); field_load_entry goes through them
- [x] Map container sections 3 (world terrain) and 5-12 (embedded map code), including section 6 (#3107, the credits program)
- [x] BG renderer (overlay 0x8008672c / bg_draw 0x80086f00): static, parallax and scrolling pieces, animation and colour records; map 7 draws into VRAM
- [x] Subsystems created in the original order (field_create_subsystems 0x800494dc): Screen, ScreenFade, ScriptCtx, MsgSystem, TextPanels, LoadQueue, Notice; map loads start the VM (vm_init) and the message font; the VM's services call the real field program, message, notice, fade, BG, sound, music and preload code (`vm_services.cpp`)
- [x] Game over (field_begin_game_over 0x8004b418, field_update states 6-8): the field fades, the fallen stay, "GAME OVER" (UI atlas) fades in and out, then the System restarts like a reset and the title runs
- [x] Character starting data for all twelve (party_slot_init 0x8007a20c, init_claude .. init_chisato, roll_talents; `game/field/characters.cpp`, generated from the decompilation and checked byte-for-byte against the title's port for Claude / Rena); party_move_character_to_slot; field_special_command (script ext 0xa8: names, party order, BG animation colours, scroll layers, Dias, disc-2 flag)
- [x] Field music: install (field_install_music 0x8004b2b4), load-and-start on map loads (field_load_music_sync 0x8004b6b0, field_start_pending_music 0x80052bf4: game_state +0x1c after loading a save, the battle's kept music via handoff 0xc), music kept across a battle (handoff 0xf, table B 0x5a / 0x5b)
- [x] field_run's return path (0x80048a84): battle outcome to script register R, escape count, preloads back from handoff 10 / 0xc, party slot ids, encounter flags; inventory stash (handoff 0x11, table A 0x10 / 0x11 / 0x34) — untested until the battle program exists
- [x] Callways (`game/field/callways.cpp`, 0x800313c0-0x80032360): camp-menu songs (field music saved and resumed, song modifiers in config +0x184..+0x196 and the encounter modifier, summon / heal songs), per-second money items 0x1da / 0x1e0, walking regen / EXP flags, battle bonuses before the battle program, song stop on map loads / music changes / script ext 0xa8 op 0x13; poison steps (field_poison_step 0x8004c250) with the step tick every 400 steps. The walk flags (+0x0b) wait on stat_calc_scan_effects (0x8003121c, post-battle stat path)
- [x] Actor shading regions (`fieldactor_build_light_split` 0x80083d70, 124 maps per disc): shaded actors are clipped into the light polygons' colours; all 3063 calls of a Redux run on map 18 (natural and three forced variants) give byte-identical primitives
- [x] SceneRenderer and the field effects (effects agent; `docs/notes/field_effects.md`): OT layers spliced into the screen OT, camera matrices, ring / streaks / textured ring / beam / burst / edge glow; each script-used effect's primitives equal the original's RAM word for word
- [x] FieldPanel (0x80055cd4): not needed: the field creates it but nothing ever opens its TextPanel (world agent)
- [x] Battle start from the field (`game/field/encounter.cpp`): random encounters (field_update state 0: distance from the area rate, battle-data preloads), forced / script battles (state 0x21, VM wait states 0xb-0xd), battle_build_load_list, the BattleTransition swirl / split, field_start_battle, field_exit_to_program (handoffs 5 BattleSetup, 0xb, 0xd, 0xf, 0x10, 0x11), field_destroy_subsystems, field_construct resets; the round trip back (field_run's return path, actors / VM / BG / music restored) runs repeatedly with the `SS_BATTLE_STUB=1` stand-in battle (clean under AddressSanitizer). Encounter maps: `docs/notes/encounter_maps.md`
- [x] Actors, sprites, collision, camera follow, CamScript (`actor_base`, `actor_field`, `actors`, `sprite_*`, `collision`, `camera`); map 7's cast walks where the original's does
- [x] After battles and item gifts (`game/field/post_battle.cpp`): equipment rescan (ability bits 0x80076190 for the encounter rate and barred encounters, the Callways walk flags), auto-equip of better new items (StatCalc + MenuSession from the shared menu library), the Equipment Wizard report (states 0xf / 0x10, side text 0xfc2, field_side_load); verified with Arlia's Leather Armor chest (`tools/smoke_field_events.sh`). The report's fifth row (MAG) falls under the window's clip, as the code reads; not yet compared with a PCSX-Redux capture
- [x] field_update (0x8004c334): play state (encounters, poison, post-battle report, save points, menu requests, world-map checks) and every state 1-0x21, with the menu pages and the embedded map code they call
- [x] Actors and character models (field sprites, world-map sprites, vehicle models)
- [x] Event-script VM port (core, extended, tables A/B): `game/field/script_*.cpp`, one thread + 58 trigger regions, wait states, sync calls, talk probe, choice prompt; every other subsystem behind `vm_services.h` (stand-ins until ported). Unit-tested; the openings of maps 7 / 18 and every map's main function (964 scripts) run with no unclaimed instruction; the openings execute the same ext instructions, in the same order, as the original in PCSX-Redux (`tools/redux_vm_trace.lua`: map 7 132 of 132 up to Ronixis's first window, map 18 86 of 86 up to Rena's)
- [x] Event triggers: there is no table on disc; each map script builds them at load time (trigger regions ext 0x74/0x75/0xd8/0xd9, NPC talk entries ext 0x70/0xf3, chests B 0x6c). Regions fire on the leader's view position (2D fields), doors transfer maps, chests pay out once, NPCs talk; `SS_TRACE` logs regions added / fired, `SS_TRACE_LEADER=N` the leader's position, view position, control and Fol every N vblanks
- [x] Text: glyph table and font rendering (`game/field/message_text.cpp`): map font + 0xfc2 font, byte-stream codes, all control codes 0x4000-0x400f, word wrap / justification / centring, names, numbers, item strings, ruby, icons; lines rendered with shadow into 4-bit cells in VRAM. Glyph positions equal the original's RAM; the text is pixel-identical to PCSX-Redux on map 7 (Ronixis, Science Officer, the map-name notice)
- [x] Message windows and choices (`game/field/message*.cpp`, `notice.cpp`, `fade.cpp`): MsgSystem (20 windows, cell allocator), open/print/scroll/wait/close state machine, frames, gradient body, button prompt, choice cursor, notice banner, ScreenFade. Compared with PCSX-Redux captures (tests/test_field_message.cpp): 539 window frames (opening, typing, button wait, close, a second window) and 237 notice frames identical except window bodies one step off on some gouraud pixels (GPU, section 1) and the notice icon spin / sparkle (scaled or transposed textured quads: GPU texture stepping; the primitives equal the original's RAM); the map fade-in is identical over its 120 frames. Choices checked numerically (no capture of the original yet); wipes, iris and cross-fade ported but not compared
- [x] Field code overlay 0xc1d catalogued (108 functions: BG, field actors, walking/collision, camera pan queue, effects); 0xc1e is the world-map overlay
- [x] World-map overlay 0xc1e catalogued (82 functions, `docs/notes/field_overlay_c1e.md`) and ported (`game/field/worldmap*.cpp`, `field_world.cpp`): terrain streaming and rendering with depth fog, sky, texture animations, map 2's whirlwind walls, the map (corner map / whole map, place markers), WorldActor sprites / shadows / walking / encounters, follow camera, the vehicles (packs 0x10e2-0x10e4, the field model renderer 0x8006053c, steering, boarding / landing: field_update states 9-0xe), the bunny (0x11-0x1a) and the courier (0x1b-0x20). Map 2 from the user's save renders like PCSX-Redux; walking and flying match it frame for frame with its frame pacing replayed
- [x] World map, part 2 (world agent): vehicles (boarding, flight with turns and climbs, landing on water and ground; 549 / 575 and 267 / 267 frames identical to Redux, the rest uninitialised memory in the original), vehicle packs and models, the bunny, the courier service (camp menu result 4, states 0x1b-0x20), the corner minimap and whole-map view with place markers
- [x] Embedded map code, part 1 (effects agent): container section 5 (#3103, the field movie player at 0x801e9000; map 548's movie matches Redux to a mean 0.4/255) and the section bookkeeping
- [x] Embedded map code, part 2 (effects agent): the 0x801f9800 overlays #3104 (rainbow), #3105 (final screen), #3106 (mirrors), #3108 (scripted paths), #3109 + entry #2 (the Cooking Master gauge), each checked against Redux (`docs/notes/field_effects.md`); the B_16 / B_70 preload size and the main-loop rate during movies checked against Redux
- [x] #3107: the credits program (A 0x2d on map 581, `game/credits/credits_program.cpp`): cue list, movies decoded to RAM, the vblank handler that shows them, the name cards with their GTE blend, the hand-back to the field; every movie opens within a vblank of PCSX-Redux, the name cards are pixel-identical
- [x] Field menus, part 1 (menu agent; `game/field/menu_camp.cpp`, `docs/notes/menu.md`): the menu button / script requests / save points open the menu (field_update state 0 checks, state 3, launcher and runners 0x80030470-0x80030c64, Callways hooks); camp page 1 (overlay 0xba6) matches PCSX-Redux captures apart from the known 1-step gouraud pixels; Data → Save / Load through the shared save page; a save written by the port loads in the original
- [x] Shops (menu agent; `game/field/menu_shop.cpp`): buy / sell lists, prices with haggling and discounts, settling; a purchase at Hearn's leaves Fol, config and inventory identical to the original; the item viewer (3D model, description) and the list / scroll-bar / number library pieces
- [x] Field menus, part 2 (menu agent): Items (3, with the item effects of entry 3001), Equipment (4), Skills (5), Status (6), Options (7), Combat (8-10), Specialty (2), item creation (0xf / 0x10), the portrait viewer (0x13), renaming (0xe), the query page, element kind 8; each compared with PCSX-Redux. 0xc-0xd are debug pages

## 4. Battle program (#2129) and its overlays

- [x] Identify and catalog the battle program: #2129 confirmed (field_exit_to_program 0x80051c9c runs it with the 0xc0-byte setup in handoff 5; it writes the outcome to config+0x178 and returns to #3100). 931 functions, 187 overlay functions (#1984 blocks 0-10, #1981), 405 globals in `docs/catalog`; structure, data formats (encounter packs #1405-#1968, stage lists, item table #1983, party code #2130+) and a port order in `docs/notes/battle.md`
- [x] Battle program entry and round trip (`game/battle/`, battle agent, phase 1): handoff 5 setup, the 12-entry load list, main loop at 30 fps, stage and units rendered, intro camera; exit back to the field with the map's preloads and music (handoffs 0xc / 10). `--battle ENC[:STAGE]` starts a battle directly; `--battle-end FRAMES[:OUTCOME]` ends it
- [x] Battle combat (battle agent, phase 2; `docs/notes/battle_port.md`): 30 fps loop, stage / units / camera (17 modes), the frame director, battle sound front end, movement (swept-box path probe), PartyUnit and EnemyUnit state machines with the enemy AI, player input, `unit_attack_targets` (accuracy, damage, crits, procs, knockback), victory / defeat / escape rules. A forced battle (encounter 2, stage 3) matches a PCSX-Redux run frame for frame (positions, animations, damage numbers, KO frame) apart from the missing HUD and dithering
- [x] Battle HUD (step 1): window system, #1980 proportional text, the HUD (member HP / MP gauges, item / move icons, target select), the message banner; appears on the same frame as Redux and matches its layouts
- [x] Battle result screen #1981 (step 2): EXP / Fol award, drops, level-ups with growth and learn tables, the defeat / escape / time-up messages, battle_fade_task, the victory jingle and voice picks (RNG draws; clips not yet); the handoffs at battle_exit_to_field equal the original's byte for byte (party, records; inventory apart from a RAM pointer), and the result pages match Redux frame for frame
- [ ] Battle, part 2. Done and compared with PCSX-Redux: Claude's module and the killer-move framework, Rena / Celine / Leon / Noel, Bowman, Dias, Ernest, the jump attack (pc_state_air_attack), the party AI (fighter / healer / caster, the attack-spell choice), equipment effects (pc_apply_equip_effects, pc_update_effects), the Particle pool, Shot kinds 0x10 / 0x11 / 0x16 / 2 / 0x2c / 0x32 and shot_apply_damage, all 16 enemy families and the 43 encounter-pack / image enemy modules (`docs/notes/battle_enemies.md`), stage 8 lightning and the stage code blocks of stages 0x10 / 0x15 / 0x1a / 0x24 / 0x29. The encounter sweep (`tools/battle_sweep.py`, `docs/notes/battle_sweep.md`) runs all 564 encounters without a crash. Ported, check pending: stages 0xd / 0xe / 0x17 / 0x1f / 0x33, Particle kinds 4 / 5. Left: voices (the victory voice and the opening voices), spells and battle items (world agent), Precis / Ashton / Opera / Chisato (effects agent), the ASan sweep

## 5. Completion

- [x] Save / load compatible with the original memory card data: a port save loads in PCSX-Redux and a Redux save loads in the port with identical state; the save blocks match byte for byte apart from three ranges the original fills with uninitialised heap (the item-record pointer, the leader position's pad word, the snapshot's last 0x30 bytes) and the checksums / packed length that follow from them. The field's save page builds the icon from entry #3027 by story progress and leaves the freed icon sheet's bytes where save_build writes nothing; both are ported (`game/common/save_icon.cpp`)
- [x] Disc 2 swap (host side): `--disc2` (or `SS_DISC2`) is opened at start; `loader_insert_disc` attaches the archive holding marker entry 5 + disc wherever the original prompts (field_run / field_load_map check config +0x4c, script B 0x6a, New Game on the title). When no configured image is that disc, the insert-disc screen (0x8006f958 / title 0x8003c5ec, `game/common/disc_screen.cpp`) is shown: the action button stands for closing the lid and tries the images again ("Wrong Disc" otherwise), Select carries on with the disc in the drive (both discs hold the same field / battle entries; they differ in entries 6 / 7, 4504-4522 and the movies)
- [x] Windows build: cross-compiled with MinGW-w64 (`tools/build_windows.sh` -> `dist/ShatteredStars-win64.zip`, the exe plus SDL3.dll, runtimes linked in); ss_tests 78/0 and ss_game_tests 74/0 under Wine; frames byte-identical to the Linux build
- [x] World edits, part 1: script patches applied as maps load (`game/field/mods.cpp`; `*.patch` files in `mods/` next to config.ini, `--mods DIR`, `SS_MODS_DIR`), each checked against the original word; `tools/so2_mod.py` lists a map's chests and doors from the disc image and writes patches to change chest contents (items / Fol) and door targets. Verified in the smoke script (Arlia's chest pays 5000 Fol, the store door leads to map 33)
- [x] Debug console (`--debug`, `game/field/debug_console.cpp`): terminal commands run on the game thread between frames — where, warp (a map's own spot or X Y Z), Fol, items, heal, script flags and globals, forced battles, noclip — and a live status line in the window title
- [x] World edits, part 2: `item` / `shop` / `enemy` data overrides in the mods files (item records wherever the game unpacks them, shop stock, enemy stats per type as a battle loads), each checked against the original value and logged; `tools/so2_mod.py items / shops / enemies` read the originals from the disc; with `--debug` an in-window overlay shows the console's status and output (smoke: mod_shop, mod_enemy, debug_console)

## 6. Archipelago (plan: `docs/archipelago_plan.md`)

- [x] Phase 1, foundations: `tools/ap_extract.py` location / item tables (`docs/ap/field.json`, `docs/ap/battles.json`: 785 field locations, story order, regions; `docs/ap/field.md`, `docs/ap/battle.md`), the `game/ap` module (seed loader, checks, receive queue with notices, `--ap-seed`, the `ap_*` debug commands), the seed format (`docs/ap/seed_format.md`) and the save sidecar; smoke ap_seed / ap_save / ap_load
- [ ] Phase 2, first playable: chest and gift shuffle in story mode, goal = final boss, the native client (apclientpp), receiving with notices, setup's Archipelago section
- [ ] Phase 3: character items, boss and drop checks, DeathLink, traps, EXP / Fol / encounter options
- [ ] Phase 4: open world (area keys, the Expel teleporter), shop shuffle, then entrance shuffle
- [ ] Phase 5: a custom .apworld release, then the pull request for official support
