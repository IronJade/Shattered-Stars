# Handoff: state of the port (2026-10-22)

The port boots from the user's `.cue`, plays the logos, the opening movie and the title,
starts a New Game or Continues a save, and plays field maps, the world map and battles.
`docs/CHECKLIST.md` has the item-by-item state; this file is the quick orientation.

## What works (verified against PCSX-Redux where marked)

- **Title and boot:** logos, opening / attract movies, title menu, Continue / load screen
  (Redux-identical apart from known GPU steps).
- **Field:** map loading, the event-script VM, message windows, fades, notices, actors,
  walking and collision, camera, music; trigger regions, doors, chests, talking
  (`tools/smoke_field_events.sh`); random and scripted encounters; Callways (songs, money
  items, walk effects); poison steps; the post-battle Equipment Wizard; the voice queue;
  game over; the SceneRenderer and the script effects (primitives equal the original's RAM).
- **World map (overlay 0xc1e):** terrain, sky, fog, world actors, walking and encounters;
  the user's save (`analysis/saves/dex_north_city.mcd`) loads onto map 2 like Redux.
- **Menus:** the camp menu's first page, Data → Save / Load (a port save loads in the
  original), the title's Continue page.
- **Battles:** entry from the field, combat (movement, attacks, enemy AI, damage, KO,
  victory / defeat / escape), the HUD and the result screen (#1981), the return to the field
  with the map's music; a forced battle matches Redux frame for frame.
- **Controller:** keyboard as a digital pad; a gamepad as an analog DualShock (the left stick
  steers, the Vibration setting drives rumble).
- **Disc 2:** with `--disc2`, the loader swaps archives where the original prompts.

## In progress (agents)

Each agent keeps a continuation log in `docs/handoff/<area>.md` (done and verified, in
progress with the uncommitted files, exact next steps), updated after every finished step.
Resume an area from its log.

| Agent | Area | Next steps |
|---|---|---|
| Battle | `game/battle/` | killer moves and the character modules (Claude first), Shot / Particle kinds, spells and items, party AI, voices |
| Enemy | `game/battle/enemy_*` (new files) | the 16 resident enemy families, the slot A-D default modules, the encounter packs' enemy code; enemy spells wait on the battle agent |
| Menu | `game/common/menu_*`, `game/field/menu_camp.*`, `game/common/party.*` | the camp pages: Items (0xbbe) next, then Equipment, Status, Skills, Specialty, Options, Combat |
| World | memory cards, `game/field/mods.*`, `tools/so2_mod.py` | save / load round trips with the original (both directions, byte-level), then world edits part 2: item / shop / enemy data overrides and an in-window console overlay |
| Effects | `game/field/field_sub_overlays.*`, `effects*`, `runtime/gpu*` | #3107 (the credits program, A 0x2d), then GPU accuracy: textured-quad and gouraud / UV stepping, subtractive blending |

Main session: integration and commits, the field program's glue, play-testing the story
route.

**Stopping work:** when weekly usage reaches 95%, every agent is told to bring its log up
to date and stop; the main session verifies and commits what is finished, and the logs
carry the rest to the next session.

## Build and tests

- `cmake --build engine/build`; `SS_DISC1=<disc 1 .cue> engine/build/ss_tests` (76) and
  `engine/build/ss_game_tests` (71); `tools/smoke_field_events.sh` (16 checks; `SS_DISC1` must
  be set, disc 2 is found next to it). All green at `6c88bd4`.
- ASan without root: `dnf download libasan` (x86_64), `rpm2cpio | cpio -idm` into a scratch
  dir, symlink `libasan.so -> libasan.so.8.0.0`, configure a build dir with
  `-fsanitize=address` and `-L<dir> -Wl,-rpath,<dir>`. `gdb.minimal` is installed.

## Debug options

- `--map N` new game (Claude) on field map N; `--map-pos X,Y,Z[,FACING[,CAMERA]]` the
  leader's start (also after Continue); `SS_SCRIPT_GLOBALS="a0=6,a4=0x834"` script globals
  for a `--map` start.
- `--battle ENC[:STAGE]`, `--battle-end FRAMES[:OUTCOME]`; `SS_BATTLE_STUB=1` (instant
  victory); `SS_BATTLE_RAM=<dump>`, `SS_BATTLE_STRONG=1`.
- `--debug`: a console on the terminal (`help`; where / warp / fol / item / heal / flag /
  global / battle / noclip) and a status line in the window title.
- `--mods DIR` / `SS_MODS_DIR` world-edit patches (default `mods/` next to config.ini;
  `tools/so2_mod.py` writes them).
- `SS_LOAD_SAVE=N` load save N without menus; `SS_SAVE_ANYWHERE=1` save outside save points.
- `SS_STATE_DUMP="VBLANK:PATH,..."` the state block, config, party, records and inventory at those vblanks; `SS_MENU_DUMP=prefix` (with `SS_MENU_DUMP_OPEN`, `SS_MENU_DUMP_DELAY=n`) the same blocks around field menus.
- `SS_DEBUG_INPUT="VBLANK:cmd;VBLANK:cmd"` types debug console commands at set vblanks (with `--debug`).
- `SS_TRACE=1` VM milestones, map loads, trigger regions, encounter settings, voice banks;
  `SS_TRACE_LEADER=N` the leader's position, view position, control and Fol every N
  vblanks; `SS_WORLD_DBG=N` world camera / leader; `SS_WORLD_VSYNC=3` the console's 20 fps
  world-map pacing.

## Conventions to keep

- No game code or assets in git; data is read from the user's discs at run time.
- Headless only (`--headless`, `SDL_VIDEO_DRIVER=offscreen`, `SS_VRAM_PNG`); never pop
  windows; never play audio audibly in tests (`SDL_AUDIO_DRIVER=disk` / `dummy`).
- Never touch `~/.config/shattered-stars/config.ini` (no `--audio` / `--no-audio` or other
  flags that save the config; use `--headless` and `SS_*` variables) or
  `~/.config/pcsx-redux`; run PCSX-Redux only with `-cli` and a scratch HOME, on scratch
  memory-card copies — never the user's saves.
- Kill processes only by exact PID.
- `/tmp` is RAM-backed (tmpfs): keep large scratch (captures, dumps, extra builds) on disk under
  the git-ignored `scratchpad/<name>/` in the repo, and delete finished scratch promptly.
- Catalogue every reversed function in `docs/catalog` (LF line endings; write CSVs with
  `lineterminator="\n"`, and never open a file for writing in the same expression that
  reads it); prose goes in `docs/notes/`. Keep each module's rows in address order inside
  its existing block; don't reorder the file (it hides real changes in the diff).
- Parallel agents: separate build dirs (`engine/build-<name>`) and Ghidra project dirs; the
  REA server is shared, so re-open your binary before each batch of REA calls and check
  the result's subject.
- Commit after each completed piece, docs first; stage only finished work (build blobs from
  HEAD plus the finished hunks when agents share a file) and verify the staged tree with
  `git checkout-index` + build + tests before committing.
