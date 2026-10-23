# Handoff: state of the port (2026-10-23)

The port boots from the user's `.cue` / `.iso`, plays the logos, the opening movie and the
title, starts a New Game or Continues a save, and plays the whole game: field maps, the world
map, battles, both discs and every ending. `docs/CHECKLIST.md` has the item-by-item state;
this file is the quick orientation.

## State (2026-10-23, about 01:30)

- **M8 done:** the game can be finished. Every map's events on both discs run (event sweep:
  0 crashes, 0 stand-ins), all 85 endings play, and the user's late-game save (map 671,
  Fienal 9F) matches PCSX-Redux through the final battle and its endings
  (`docs/notes/m8_audit.md`, `docs/notes/endings.md`).
- **M7 done:** every character module, enemy module, spell (70) and spell module (55) is
  verified; all 564 encounters run without crashes (`docs/notes/battle_sweep.md`); the battle
  talk layer (`battle/party_talk.cpp`) matches Redux (RNG equal to the exit; the talk pose is
  shorter by the clip's CD load time, accepted).
- **Archipelago:** phase 1 and the port side of phase 2 are done; the APWorld
  (`apworld/so2`, `docs/ap/apworld.md`) generates and fills story-mode seeds, the native
  client works end to end against a local server. In progress: a real SO2-slot end to end,
  boss checks, phase 3 (characters, drops, traps, multipliers) on both sides.
- **Packaging:** setup program, Windows zip and Linux tarball (glibc 2.36) with Archipelago
  networking (`tools/build_windows.sh`, `tools/build_linux.sh --container`).
- **Original bugs:** decided by the user one by one (`docs/notes/original_bugs.md`); the rule is
  that a fix never changes the play state.

## What works (verified against PCSX-Redux where marked)

- **Title and boot:** logos, opening / attract movies, title menu, Continue / load screen.
- **Field:** maps, the event-script VM, messages, actors, camera, music, triggers, doors,
  chests, talking, encounters, menus (every camp page, shops, item creation), saves that load
  in the original; the world map with vehicles, the bunny and the courier.
- **Battles:** everything listed under M7 above, the HUD, the result screen and the return to
  the field.
- **Controller:** keyboard or gamepad, rebindable in setup.
- **Disc 2:** the loader swaps archives where the original prompts.

## In progress (agents)

Each agent keeps a continuation log in `docs/handoff/<area>.md` (done and verified, in
progress with the uncommitted files, exact next steps), updated after every finished step.
Agents are replaced by fresh ones from those logs once their context passes ~400k tokens
(rule in `~/.claude/CLAUDE.md`); the current area → agent id list is `scratchpad/agents.txt`,
and `~/.claude/scripts/agent_context.py` shows sizes. Resume an area from its log.

| Area | Log | Now |
|---|---|---|
| Battle | `docs/handoff/battle.md` | stage references, Weirdaxe debris, final boss first AI draw, B13, M7 tick |
| Effects | `docs/handoff/effects.md` | battle talk verified; field menu voice Redux check; then the catalog's unported rows |
| Catalog | `docs/handoff/catalog.md`, `effects.md` | complete: 5429 rows, gap scan 0 missing over 32 programs (`tools/catalog_gaps.py`, `docs/catalog/gaps_explained.csv`), port column 0 empty; left: confidence pass (699 likely, 166 guess) |
| Confidence pass | `docs/handoff/confidence_field.md`, `confidence_battle.md`, `confidence_boot.md`, `port_leads.md` | done (2026-10-23): all 5452 rows confirmed; the leads it raised are closed (B16-B20, the courier's voice streaming, the save page's Callways stop) |
| Save page | `docs/handoff/savepage.md`, `port_leads.md` | done: sprite pack 0xbd2 and the Callways stop both match Redux |
| Release (AP phase 5) | `docs/handoff/release.md` | prepared (09:20): `tools/build_apworld.sh` -> `dist/so2.apworld` + template YAML (packaged world: 4 seed sets, e2e 25/25), player docs, both bundles rebuilt, AP's general tests pass with the world in place; `docs/ap/release_checklist.md`. Open: the user's choices (maintainer, licence, port version), the DeathLink wording; publishing is the user's call |
| Spell | `docs/handoff/spell.md` | re-recording the 12 lenient world_spell refs with `BW_TICK_LOG` |
| World | `docs/handoff/world.md` | Archipelago phase 3, port side; protagonist from the seed |
| Enemy (APWorld) | `docs/handoff/enemy.md` | real SO2-slot end to end, boss locations, phase 3 APWorld side, logic soundness |
| Tools | `docs/handoff/tools.md` | reference regression suite (`tools/ref_regress.py`, quick set in checkpoints) |
| Menu, Setup | `docs/handoff/menu.md`, `setup.md` | idle (M8 done; packaging done) |

Main session: coordination, the user's decisions, the hourly commits.

**Hosting (2026-10-23):** `origin` is https://github.com/IronJade/Shattered-Stars (public; the
maintainer is IronJade); the old repo is the `dgs` remote. The history was rewritten once
before the first push (commit emails changed to the GitHub noreply address, set repo-locally
in `.git/config`), so commit ids from before 09:15 that day changed; the docs were updated.
Commits stay local; push only when the user asks.

**Never losing work:** agents write straight to disk and keep their logs and catalog rows
current at least every 20 minutes. The main session commits once an hour with
`tools/checkpoint.sh [notes]` (a CronCreate job at :13; session-only, so recreate it in a
new session; it fires only while the session is idle). The script builds; if the build
passes, it runs ss_tests / ss_game_tests / smoke and commits everything with the results
and the list of what was verified since the last checkpoint; otherwise it commits `docs/`
only. Agents run the suites themselves before reporting a step as verified.

## Build and tests

- `cmake --build engine/build`; `SS_DISC1=<disc 1 .cue> engine/build/ss_tests` (76) and
  `engine/build/ss_game_tests` (71); `tools/smoke_field_events.sh` (16 checks; `SS_DISC1` must
  be set, disc 2 is found next to it). All green at `3cffe28` (ss_tests 78, ss_game_tests 104,
  smoke 62).
- ASan without root: `dnf download libasan` (x86_64), `rpm2cpio | cpio -idm` into a scratch
  dir, symlink `libasan.so -> libasan.so.8.0.0`, configure a build dir with
  `-fsanitize=address` and `-L<dir> -Wl,-rpath,<dir>`. `gdb.minimal` is installed.

## Reference regression suite

`tools/refs/refs.csv` inventories every PCSX-Redux reference recording under `scratchpad/`
(battle killer moves and stages, enemy families/modules, the world agent's spell sweeps, the
effects agent's party-module moves, plus menu/world-map/legacy-effects refs not yet wired
up) with how its port side is reproduced, how it's compared, and the expected result.
`tools/ref_regress.py --all|--quick|--area X|--id A,B [--build DIR]` replays the automatable
ones (223 of 339) through the owning agent's own scripts — never PCSX-Redux — judges each
against its documented expectation (plus a `resync` column for references that cross a
CD-timed load or a boss-intro-voice draw), and writes
`scratchpad/regress/<timestamp>/summary.md`: 219 pass, 3 regress, 1 review, 116 skip at HEAD
(`docs/handoff/tools.md`). `--quick` (~28 refs, under 10 minutes; in practice ~35s) runs
after smoke in `tools/checkpoint.sh` and its total goes into the commit message, same as the
other suites (a failure is reported, never
blocks the commit). Gaps and how to close them: `tools/refs/README.md`.

## Debug options

- `--map N` new game (Claude) on field map N; `--map-pos X,Y,Z[,FACING[,CAMERA]]` the
  leader's start (also after Continue); `SS_SCRIPT_GLOBALS="a0=6,a4=0x834"` script globals
  for a `--map` start.
- `--battle ENC[:STAGE]`, `--battle-end FRAMES[:OUTCOME]`; `SS_BATTLE_STUB=1` (instant
  victory); `SS_BATTLE_RAM=<dump>`, `SS_BATTLE_STRONG=1`.
- `SS_BLOAD_DRIVE_TIME=1` (comparisons only, default off): the battle's disc reads take the
  console drive's time (seek + single-speed sectors). The port normally loads at once, so a
  voice line's talk pose (the clip's load and upload) is shorter than the original's (6
  frames vs 10-16); with the switch it matches Redux. `SS_TALK_TRACE=1` logs the party's
  talk events (lines, talk pose, ally reaction, fury, weapon throws) with the rng count;
  `SS_TALK_OFF=1` skips the opening lines (A/B checks); `SS_TALK_FLEE=F` makes the party
  flee at battle frame F.
- `SS_MENU_VOICE_TRACE=1` logs the field menus' voice streaming (init, reload after a
  killer-move / party change, battle banks and field clips queued, done, shutdown) with the
  menu / field state, budget and the sound slots; `SS_MENU_RNG=<dump>` loads the original's
  random generator (a Redux `rng_open.bin`) when a field menu opens; `SS_FIELD_CLIP_RNG=<dump>`
  the same before the field's first voice-clip pick. `SS_MENU_SOUND_DUMP=<prefix>` (each menu
  voice step) and `SS_TALK_SOUND_DUMP=<path>` (battle start) write the sound driver's voice
  slots 0-3 in the console's layout, for a byte compare with a Redux dump of the driver
  (`scratchpad/fx_worker/mv/cmp_snd.py`).
- `--debug`: a console on the terminal (`help`; where / warp / fol / item / heal / flag /
  global / battle / noclip) and a status line in the window title.
- `--mods DIR` / `SS_MODS_DIR` world-edit patches (default `mods/` next to config.ini;
  `tools/so2_mod.py` writes them): loose `*.patch` files and mod packages (`mod.json`;
  `docs/notes/mods.md`), applied in config.ini's `mod_order` (setup's Mods page). With a
  folder override the config's order is ignored; `SS_MOD_ORDER="(loose),id,-id"` sets one
  for a test run. `tools/mod_check.py` / `tools/mod_verify.py` check packages.
- `SS_LOAD_SAVE=N` load save N without menus; `SS_SAVE_ANYWHERE=1` save outside save points.
- `SS_STATE_DUMP="VBLANK:PATH,..."` the state block, config, party, records and inventory at those vblanks; `SS_MENU_DUMP=prefix` (with `SS_MENU_DUMP_OPEN`, `SS_MENU_DUMP_DELAY=n`) the same blocks around field menus.
- `SS_SHOT="VBLANK:path.png[,VBLANK:path2.png,...]"` (default off; documentation
  screenshots, `docs/images/`, `docs/handoff/readme_shots.md`) writes the frame actually
  presented at each listed vblank -- the OpenGL output a player's window shows (scaled,
  letterboxed to 4:3) -- not the raw PS1 display buffer `--capture` / `SS_VRAM_PNG` dump. A
  headless run opens no window otherwise; this opens a second, hidden one just to render
  into and read back with `glReadPixels`.
- `SS_DEBUG_INPUT="VBLANK:cmd;VBLANK:cmd"` types debug console commands at set vblanks (with `--debug`).
- `--ap-seed FILE` loads an Archipelago seed (docs/ap/seed_format.md; console `ap_status`,
  `ap_check ID`, `ap_give ITEM [FROM]`; `[ap]` log lines). `SS_AP_SERVER=ws://localhost:PORT`
  (+ `SS_AP_SLOT`, `SS_AP_GAME`) connects any run to a local server, headless ones too;
  `SS_PACE=1` paces a headless run in real time; `tools/ap_e2e.sh` is the end-to-end check.
- `SS_TRACE=1` VM milestones, map loads, trigger regions, encounter settings, voice banks;
  `SS_TRACE_LEADER=N` the leader's position, view position, control and Fol every N
  vblanks; `SS_WORLD_DBG=N` world camera / leader; `SS_WORLD_VSYNC=3` the console's 20 fps
  world-map pacing.

## Conventions to keep

- Enhance, don't degrade: the port runs at full speed (no modelling of the original's
  slowdown), and the original's bugs are commented out and corrected, each listed in
  `docs/notes/original_bugs.md`. Save data stays byte-compatible.
  A fix never changes the play state: hit timing, hits, damage, difficulty and pacing stay
  the original's, measured against PCSX-Redux before the fix goes in.
- Agents keep their `docs/handoff/<area>.md` complete enough to resume from, because each
  one is replaced by a fresh agent once its context passes ~400k tokens (cost per step
  grows with context size).
- No game code or assets in git; data is read from the user's discs at run time. One
  exception, the user's decision (2026-10-23): a handful of documentation screenshots of the
  port running, in `docs/images/` only, used by the README and wiki. Nothing else from the
  discs (no extracted textures, sprites, audio, logos or data files).
- Headless only (`--headless`, `SDL_VIDEO_DRIVER=offscreen`, `SS_VRAM_PNG`); never pop
  windows; never play audio audibly in tests (`SDL_AUDIO_DRIVER=disk` / `dummy`).
- Never touch `~/.config/shattered-stars/config.ini` (no `--audio` / `--no-audio` or other
  flags that save the config; use `--headless` and `SS_*` variables) or
  `~/.config/pcsx-redux`; run PCSX-Redux only with `-cli` and a scratch HOME, on scratch
  memory-card copies — never the user's saves.
- Kill processes only by exact PID.
- Machine budget (20 cores): at most 3 PCSX-Redux instances per agent at once, builds with -j8.
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
