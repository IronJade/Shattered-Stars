# Handoff: state of the port (2026-10-23)

The port boots from the user's `.cue` / `.iso`, plays the logos, the opening movie and the
title, starts a New Game or Continues a save, and plays the whole game: field maps, the world
map, battles, both discs and every ending. `docs/CHECKLIST.md` has the item-by-item state;
this file is the quick orientation.

## State (2026-10-23, 18:50)

- **Released:** v0.01.00 (North American discs), https://github.com/IronJade/Shattered-Stars/releases/tag/v0.01.00.
  The README, the screenshots (docs/images), the wiki (35 pages, including the modder reference),
  the mod manager and the curated mods/ list are public.
- **The game:** M7 and M8 are done. Every map, battle and ending is verified, and the late save
  matches PCSX-Redux. The catalog has 5452 of 5452 functions confirmed.
- **0.02.00 (Europe, France, Germany) in progress:**
  - **Regions:** all three play on one build, with region profiles running the NA logic at
    60 Hz on their own text and data. The FR/DE text system is done (287/287 dialogue lines
    exact against Redux). Saves are native per release; all four originals load the port's
    saves. Europe's disc 1 and disc 2 sweeps match NA. See `docs/handoff/regions.md`, which ends
    with a readiness section.
  - **Archipelago:** `so2.apworld` 0.1.1 fixes two open-world ship landings and is fully proven
    (`docs/handoff/apareas.md`). It ships with 0.02.00, by the user's decision. It also gets
    the El voyage gate (`docs/handoff/elgate.md`): in open world, the ship to El waits for the
    El passes, plus the three Energy Nede passes when the teleporter and expel_progression are
    both on (the user's decision, 18:45: the 6-pass gate as built).
  - **Docs:** the 0.02.00 README update is committed. The wiki changes are committed in
    scratchpad/wiki (3dc5c04) but **not pushed**; push them at release. The release-notes draft
    is `docs/release_notes/0.02.00.md`.

### Resume here (a fresh session)
1. **Recreate the hourly checkpoint cron** (CronCreate, `13 * * * *`). Its prompt:
   - Don't run scratchpad/merge_catalogs.retired.py.
   - Copy scratchpad/checkpoint_notes.txt to checkpoint_notes_run.txt and empty the original.
   - With SS_DISC1 set to the NA disc 1 .cue, run
     `timeout 3000 tools/checkpoint.sh scratchpad/checkpoint_notes_run.txt > scratchpad/checkpoint_last.log 2>&1`
     in the background.
   - If no commit was made, append the run notes back.
   - Report the suite totals.
   - Check agent sizes (`~/.claude/scripts/agent_context.py --project Shattered --hours 2`) and
     roll over any agent past 400k from its log.
   - Start queued items in fresh agents.
   - Commit, never push.

   `.claude/scheduled_tasks.lock` is excluded locally (`.git/info/exclude`), so the checkpoint
   doesn't pick it up.
2. **Permission mode:** the user runs "Auto" mode (17:25). In "ask" mode background agents can't
   show prompts, so their requests are refused and they stop. When an agent reports a refusal,
   bring it straight to the user, then pass their decision back to the agent (SendMessage).
3. **El voyage gate:** implemented and tested (`docs/handoff/elgate.md`, 18:12 entry); the
   user chose the 6-pass gate (18:45). Left at 18:50: the late + teleporter proofs with the gate
   build (`scratchpad/elgate/proofs/`, `all.done` when finished), their comparison with
   `docs/ap/late_proof.json`, and apworld.md's Testing section. If the elgate agent is gone,
   finish those from its log.
4. **Regions verification:** the regions-verify agent (opus, started 18:42) compares the FR/DE
   event sweeps and the battle sweeps (queue PID 1279484, `scratchpad/regions/q/queue.log`,
   ending about 19:45) with NA, checks the French battle result screen and the German Scan
   window in Redux, runs ap_e2e_so2.sh on French, and writes the final readiness verdict in
   `docs/handoff/regions.md`. If it is gone, resume from that log.
5. **Then 0.02.00:**
   - resolve the release notes' TO CONFIRM items;
   - bump the version to 0.02.00 (engine/CMakeLists.txt) and update the README badge and file
     names;
   - build the release candidate from a clean worktree as for 0.01.00 (docs/ap/release_checklist.md
     section 7);
   - get the user's go-ahead, then push main, create the draft release, verify, publish, and push
     the wiki.

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
| Release (AP phase 5) | `docs/handoff/release.md`, `docs/ap/release_checklist.md` | **0.01.00 released 2026-10-23** (https://github.com/IronJade/Shattered-Stars/releases/tag/v0.01.00, tag on c773d54): both bundles, `so2.apworld`, the template; notes `docs/release_notes/0.01.00.md`. Known issue: `so2.apworld` 0.1.0's open-world mode misses two ship landings (about 4 in 10 open-world seeds unfinishable; story mode unaffected). Fixed in 0.1.1 (`docs/handoff/apareas.md`), which ships with 0.02.00 by the user's decision; its notes must tell open-world players to regenerate. Open: the upstream Archipelago PR (the user's call) |
| Regions (0.02.00) | `docs/handoff/regions.md`, `regions_text.md` | Europe playable on one build (NA logic at 60 Hz, own text and data; 0 unmapped reads for EU/FR/DE; `tools/memcap.py` caps every game run). In progress: the FR/DE text system (regions-text agent). Next: wire `text_rules.h` for FR/DE and enable them, disc 2 sweeps for all three, the NA-vs-EU battle comparison, then README/wiki and release 0.02.00. Japan later |
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
- Machine budget (20 cores, 62 GB RAM): at most 3 agents running at once; builds with -j8 when
  one or two agents build, -j4 when three do (big C++ files take about 1 GB per compile job);
  at most 4 game processes and 3 PCSX-Redux instances per agent. Four agents building -j8 with
  game, Redux and Wine runs on top filled RAM and swap on 2026-10-23 and ended the session.
  The actual cause that day was one runaway headless run (Europe disc, map 0: 28 GB in
  3.5 minutes), so every headless game run has a memory cap of about 4 GB (RLIMIT_AS, or a
  cgroup MemoryMax for ASan builds); a run that hits it counts as a failure, never a retry.
  `tools/memcap.py` implements it (`SS_MEM_CAP_GB`, default 4) and `tools/event_sweep.py`,
  `tools/battle_sweep.py`, `tools/ref_regress.py` and `tools/smoke_field_events.sh` apply it
  (the sweeps report "memory cap" and don't rerun that map); one-off runs:
  `prlimit --as=$((4<<30)) engine/build-X/shattered-stars ...`. The cause was a credits loop
  over an unreadable European card script with SS_REGION_READ_LOG on; fixed (the card
  script is read as the release's own run, and the region read / miss logs are bounded).
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
