# Handoff: hubcore (P1 hub New Game, the start option, dungeon-key doors + Fienal gate, P5, sidecar)

Agent "hubcore", started 2026-10-25. Plan and the user's decisions: docs/handoff/hub.md (D1-D10
override scratchpad/openworld/story_flags.md). Format: docs/ap/seed_format.md "Hub". Data:
docs/handoff/hubdata.md; the VM side: docs/handoff/hubvm.md.

## Rules given
- Work in order, each piece verified before the next: (1) P1 hub New Game, (2) the start option
  D9 (Fun City default, Arlia, random = any town in both worlds; `starts` in hub_state.json),
  (3) dungeon-key doors D4 + the Fienal gate D6 (no Fienal key; Lasguss key only if XINE is a hub
  location; Portal of Sealing: P5 look or it leaves hub mode), (4) P5 Eluria Tower 557 -> 558 and
  the arena 305, (5) sidecar / saves, (6) unit tests, smoke cases, docs.
- Own: engine/src/game/ap/ap.cpp (New Game, start, doors), seed.{h,cpp} (hub block, starts; small
  edits, tell hubchambers), the field / door code P5 needs, engine/tests/, smoke cases,
  tools/ap_hub_state.py, docs/ap/hub_state.json, tools/ap_doors.py (adding), my part of
  seed_format.md "Hub", engine/README.md, docs/catalog. NOT hub.{h,cpp} (hubchambers; a hook only
  by agreement in their log), tools/ap_hub_bosses.py, apworld/.
- Other agents: hubchambers (docs/handoff/hubchambers.md), hubworld (docs/handoff/hubworld.md).
  Notes go into THEIR log as dated "For hubchambers" / "For hubworld"; read "For hubcore" here.
- Build: engine/build-hubcore (configured like build-hubvm; deps from build-hubvm/_deps),
  `cmake --build engine/build-hubcore -j8`. Keep the shared tree compiling after every edit.
- Tests before reporting verified: `SS_DISC1=<disc 1 .cue> engine/build-hubcore/ss_tests`,
  `engine/build-hubcore/ss_game_tests`, `tools/smoke_field_events.sh
  engine/build-hubcore/shattered-stars`. Baseline (build-hubvm): 99/0, 161/0, 132/0.
- Headless only; PCSX-Redux only `-cli` with scratch HOME; never a flag that saves
  ~/.config/shattered-stars/config.ini (no --audio / --no-audio); SDL_AUDIO_DRIVER=dummy.
  Every game process under tools/memcap.py (SS_MEM_CAP_GB 4); at most 2 at once; a cap hit is a
  failure to investigate, never a retry.
- Enhance, never degrade: without a hub seed nothing changes.
- Scratch in scratchpad/hubcore/ (not /tmp). Cap output. Kill only by exact PID. Wait loops have
  deadlines; long jobs in the background.
- Don't commit / push. Log current after each piece and at least every 20 minutes.
- Permission refusal: stop, report the exact command. Past ~400k tokens: update the log, end the
  turn saying so. A user decision (D1-D10, content, restaged fallback): stop and report with a
  recommendation.

## Done and verified
- **P1 hub New Game** (2026-10-25, headless proofs; suites not yet re-run on the final code):
  - seed.h / seed.cpp: `HubState` (`Seed::hub_state`): hub.chapter / step / disc / flags /
    globals / games {claude, rena: flags, globals} / start {name, map, x, y, z, dir, globals}.
    On only with world_mode hub and a hub block holding `flags` or `start` (a lock-only block, as
    in hubvm's tests and runs, builds nothing). Words checked: 0xc4-0x33c, 4-aligned, never
    0xc8-0x1bb (names, message speed). Hub mode forces character_items + party_rules open (D7).
  - ap.cpp `on_new_game_state(block, debug_start)`: called at the end of
    field_create_state_block's new-game branch (after the original's hero, names, chapter 3/4);
    order: flags cleared, hub.flags, hub.globals, games[hero] flags / globals, the route flag
    forced to the hero picked (flag 1 set in Claude's game, clear in Rena's), chapter / step,
    config +0x4c = disc - 1 and loader_insert_disc, start.globals, then the start: game_state
    +0x1a map, +0x08/+0x0c/+0x10 position, +0x18 facing (dir * 4096 / 360). `take_start_position`
    makes field_main_loop's first map load keep that position (keep_position); field_run's disc
    check also runs for it (the insert-disc screen if disc 2 is missing). A --map debug start is
    left alone unless SS_HUB_STATE=1 (then the state is applied on the --map map, no start).
    start.items / goal.flags / the starting character come from the existing first-free-play code.
  - map.cpp: an SS_TRACE line where the world map creates the Synard ("world: vehicle 1 (the
    Synard) parked at X,Y,Z on map M") for the start proofs.
  - Headless (scratchpad/hubcore/run_ng.py: title -> New Game -> prologue skipped): Claude and
    Rena: "[ap] hub New Game: Claude's game, 1063 flags, 25 globals (+1 / 1 the hero's ...),
    chapter 60 step 4000, disc 2; Fun City (map 279 at 470, 0, 68, facing 180)"; free play on 279
    at 470,0,68 (control 0 / 1); state dump vs hub_state.json (chk_state.py): flags exact (1064 /
    1063), globals equal except the map set-up's own G[0x1ca] / G[0x1cc] / G[0x1d8] counter;
    279:T4 -> map 2, Synard parked at 81546,-258,33880; chapter 60 step 4000. With a starting
    character (Ashton), start items + Fol, goal.flags [445, 446]: goal flags set, Ashton joins and
    leads, inventory given; `ap_status`: "hub | lock chapter on step on | chambers 22".
  - Unit tests (new engine/tests/test_ap_hub.cpp, in ss_game_tests): ap_hub_state_seed,
    ap_hub_new_game_order. ss_game_tests 163/0.

- **(2) The start option** (2026-10-25):
  - tools/ap_hub_state.py: `STARTS` is a list of every town (the guide's: Expel 10, Energy Nede
    6), each with its world-map entrance door and its walk-out entry; `starts_table` reads the
    arrival from docs/ap/doors.json (checked open at the hub state: holds_at_hub evaluates the
    door's conditions at chapter 60 / step 4000 / the hub's flags) and the town's own Synard spot
    from its script (table A 0x19 set_point with constant arguments, direct or through the
    5-argument wrapper; exactly one per start map). docs/ap/hub_state.json `starts` = LIST of 16
    rows {key, name, world, map, x, y, z, dir, door, exit, synard_expel, synard_nede, globals};
    `hub` unchanged (Fun City; hub.globals identical), `start_random` / notes updated.
  - Starts (key, map, arrival, exit; Synard spot of its world): fun_city 279 (470,0,68,180) T4
    (81546,33880); arlia 17 (-126,0,-614,90) T0 (51209,17464); salva 44 (1673,0,263,178) T4
    (43749,22168); cross 74 (393,0,-47,180) T0 (48365,32644); clik 108 (-887,0,95,229) T1
    (40754,66262); mars 124 (2008,-60,4596,270) T0 (64059,44042); herlie 144 (-295,0,1974,353)
    T0 (74378,38823); hilton 169 (-3795,0,163,269) T7 (91889,32115); lacour 211 (733,0,79,197)
    T0 (103192,24707); linga 183 (-3392,0,-1182,340) T27 (90815,13368); eluria_colony 232
    (971,0,16,188) T0 (16052,64393); central_city 235 (759,0,229,180) T0 (54840,24253);
    north_city 260 (9081,0,-850,181) T0 (57503,27589); armlock 306 (703,30,-867,180) T0
    (81595,21506; 306 is Berle 535's town chamber: its view is on from the arrival); giveaway 317
    (1585,0,377,188) T0 (46820,42786); laqua 579 (92,0,-2486,273) T6 (71971,40740).
  - Proven headless, **all 16 in both games** (scratchpad/hubcore/starts.py; runs/starts_claude.txt,
    starts_rena.txt: 16 PASS each): New Game -> the start's arrival, free play, the exit -> the
    world map (control there), the Synard parked at the row's spot (SS_TRACE "world: vehicle 1"),
    chapter 60 step 4000.
  - Cross-world routes, no key: Fun City start: 279:T0, 280:T12, 281:T3, 297:T4, 299:T6, 303:K25
    -> map 18 (Virtual Arlia), then 18:K26 option 1 (Arlia's guide) -> 303 (the arena), 303:K25 ->
    18 again. Arlia start: 17:T9 -> 18, 18:K26:1 -> 303 (Energy Nede), 303:K25 -> 18. (Runs
    cross_nede / cross_expel.) For the guide's "go" (the second option) the event sweep got a
    play-step option: SS_SWEEP_PLAY "MAP:K26:1" (event_sweep.cpp: the cursor goes down N times,
    once the choice window is up, then confirm; logged "choice on window W: down / confirm").
- **(3) Dungeon-key doors and the Fienal gate** (2026-10-25):
  - ap.cpp: keys lock in hub mode too (missing_keys: open_world or hub); status line "world hub";
    measure_door_banners: at the first free play (font loaded) every seed door banner ("<key>
    needed" with its purpose, each condition's text) is measured once; too wide -> logged ("door
    banners measured: N, M too wide"; SS_TRACE lists each width).
  - Every door hub_bosses.json lists (29) is a map transfer on the disc at its offset and in
    docs/ap/doors.json with its routine (scratch check). Routine-level locks: every keyed routine
    leads only into its dungeon except Zand's 142 0x77cc (also -> 157): hubworld told to write
    9314034 without `routine`. Missing rows: the Synard Plant's 264 -> 269 doors 17308906 /
    17308938 (the transfers the Synard view at chapter 38 takes) and 17832691 (272 -> 269): asked
    hubchambers (tools/ap_hub_bosses.py) to add them. Other doors into keyed dungeons (hubworld's
    list) are scene / vision maps or need chapters the hub never has outside views: none needed.
  - Banners measured headless (scratchpad/hubcore/runs/banners.log): all 19 Fienal banners and
    the 18 key texts fit; widest "Defeat Berle (Eluria Tower) first" 223 px (max 236).
  - Headless (doors_test.json): world entrance Cave of Red Crystal 68686044 (2:T21) refused
    "needs Cave of Red Crystal Key", with the key -> map 419; town field door Salva 43 -> 63
    (2824679, 43:T8) refused, with key 1 -> 63; Fienal gate 68685691 won [524] refused with its
    banner, after a stub win of 524 (console `battle 524`, SS_BATTLE_STUB) -> map 687.
  - Rulings applied: no Fienal key (won gate only). **Portal of Sealing leaves hub mode** (key 19
    and its one chest, the Mind Ring at 387, flag 2474): the way in is three story-gated doors
    (306 -> 308, 308 -> 313 at chapter 52 step 1000; 313 -> 400 at 52/2000), the lab's way out is
    gated too (400 -> 401 at 53/4000) and 306 is Berle 535's chamber: not cheap or safe for one
    chest. Lasguss Mountain: nothing but XINE behind it; its key follows hubchambers' XINE answer.
  - Unit test ap_hub_dungeon_doors (world door, field door, routine-less door, the won gate's
    banners in order, an escape is no win, sidecar keys / goal_won, story seed: keys lock nothing).
  - Notes sent: hubworld (starts, doors, banners, Portal of Sealing, party under any: confirmed
    headless "Claude is in the party already" / "Rena joins"), hubchambers (seed.h, SS_HUB_STATE,
    Synard doors, Portal, XINE).

## In progress (2026-10-25, context rollover: a fresh agent continues from here)
(Superseded: hubcore2 finished all of it; see "hubcore2" below.)
- **(4) P5** (findings so far; nothing in the engine for P5 yet):
  - **Eluria Tower 557 -> 558: no override needed.** Why the late sweep never fired it: the door
    routine (557 0x7808) at chapter > 32 jumps to 0x8584, which walks the leader to the door
    (library call 0x04d8: actor move, then wait_state 5 "actor idle") before the transfer
    (0x8658). The sweep fires the routine with the leader at the arrival, from where that scripted
    walk never ends (late proof log: "stuck ... wait_state 5"). Walking into the region works:
    run p5_tower (--map 557 --map-pos -92,0,1907,3072, SS_HUB_STATE=1, SS_ROUTE=557:up): trigger 0
    fires at the door, the scene plays, map 558 loads; Berle 520's view stays on (557-575).
  - **The climb: Berle 520's view must be at step 20, not 0 (hubchambers' row; proposal sent?
    NOT YET: tell them).** BFS (scratchpad/hubcore/bfs.py, from 559 at 880,-60,842; logs
    scratchpad/hubcore/bfs/tower0, tower20):
    - view 33/0 (as hub_bosses.json has it): reaches 557-573 (Shin 519 fights at 569), but 573 ->
      574 needs `not (chapter == 33 && step < 20)`: Berle 575 unreachable; and **566:T2 -> 567
      plays the 33/0 split scene, which transfers to 627 (a vision map outside the view's maps):
      the view ends there and the party is stranded at chapter 60 (627 has no exit: a soft-lock).**
    - view 33/20 (seed hub_tower20.apso2): 567 is harmless (-> 566 only), the whole climb works:
      559 -> 560 -> 562 -> 563 -> 566 -> 568 -> 569 (Shin 519 won) -> 570 -> 573 -> 574 -> 575
      (Berle's scene, battle 520 won) -> cut_to 557; view off. (Map 573's log in tower20.)
    - 558 -> 559 is physical: the door routines (558 0x3c00 / 0x3c60, regions T4 screen x 20-41 /
      T2 x 282-299, screen y ~141-180) transfer only when the leader's height (ext_e4, y >> 12)
      is > 400 (stairs down inside the region). Not walked yet: --input up (30-560) reaches screen
      (160, 213); left / right then stop at z +-591 (runs p5_558_a / _b, hub_tower20n.apso2 has
      encounter_rate 0). Next: find the stairs (try up further from (100,212) / (221,213), or read
      the region quads in 558's set-up 0x3d94) and walk 558 -> 559.
  - **The arena 305 (Marsilio 532):** no door enters 305 at chapter 60 (279 -> 305 and 301 -> 305
    only at 57/0, 302 -> 305 with flag 511, 297's receptionist only through the paid arena
    registration: choice 1, fee, rank, members; it sets the arena state G[0x217]). 305's set-up
    registers Marsilio's talk (actor 16, routine 0x6e78, choice 2) at 57/20 whatever the
    arrival, so any way in works for the scene. Note: the hub state has G[0x217] = 1 (0x214 word
    0x100010a), so 305's set-up also calls 0x8220 (an arena routine: check what it does).
    Proposed override (data only, existing format): seed door 19929509 (304 -> 296, routine
    0x668c, the antechamber's T4; 296 stays reachable from 299 / 300) with `to` {305, -13, 0, 276,
    271} (the registration's arrival). Test seed ready, NOT run: scratchpad/hubcore/seeds/
    hub_arena.apso2; route 279:T0,280:T12,281:T3,297:T4,299:T2,304:T4 then 305:K16:2 with
    SS_BATTLE_STUB=1 (expect the view on at 305, battle 532, check 1364329004). If a permanent
    redirect is unwanted, a port option: a door `to` that applies only while a hub boss is
    unbeaten (e.g. `"while_unbeaten": 532`). Then hub_state.json could carry `p5_doors` for the
    APWorld (tell hubworld).
  - **Shigeo's town chamber 281 lies on the only Nede -> Expel route** (279 -> 280 -> 281 -> 297
    -> 299 -> 303's old man): its view (57/2000) is on whenever the party crosses 281 and its
    scene starts from a trigger strip (region [[82,234],[83,241],[347,187],[338,181]]). The sweep
    fires doors without walking, so a real walk across 281 may start the Shigeo fight for a fresh
    party (level 1 at a Fun City start). To check with a walk; tell hubchambers / the coordinator.
- (5) saves, (6) smoke cases + docs: not started.
- Files touched (all compiling): engine/src/game/ap/{ap.h,ap.cpp,seed.h,seed.cpp},
  engine/src/game/field/{field_program.cpp,map.cpp,event_sweep.cpp}, engine/CMakeLists.txt (one
  target_sources line), engine/tests/test_ap_hub.cpp (new), tools/ap_hub_state.py,
  docs/ap/hub_state.json; notes in docs/handoff/hubworld.md, hubchambers.md.
- Suites on build-hubcore at the rollover: ss_tests 99/0, ss_game_tests 168/0 (161 + my 3 +
  hubchambers' tests), smoke 132/0 (no hub smoke cases of mine yet).

## Next steps
1. P5: walk 558 -> 559; send hubchambers the Berle 520 step-20 proposal (and the 567 -> 627
   soft-lock at step 0); run the arena seed; decide the arena override (data `to` or a port
   `while_unbeaten`), put it where the APWorld reads it, tell hubworld.
2. (5) Saves: a save in hub mode + its load keeps the hub (lock on, New Game state not
   re-applied: on_new_game_state runs only in field_create_state_block's new-game branch; a load
   takes the snapshot branch), a view back on when loading inside an unbeaten chamber. Use
   SS_SAVE_ANYWHERE + the camp-menu save input from the smoke (ap_save) and SS_LOAD_SAVE.
3. (6) Smoke cases (hub New Game per start kind: Fun City (Nede), Arlia (Expel), a random town
   e.g. Linga; a key door: Red Crystal 2:T21; the Fienal gate with SS_BATTLE_STUB + console
   `battle 524`), docs: seed_format.md "Hub" (New Game, `starts`, start.globals, doors in hub
   mode, P5, banners measured, the Portal of Sealing out), engine/README.md hub bullet,
   docs/catalog rows (557 0x7808 / 558 0x3c00 door routines, set_point wrapper, ext_e4 = actor y,
   the arena 305 set-up 0x9d9c), full suites.

## How to verify
- Build: `cmake --build engine/build-hubcore -j8`. Suites: `SS_DISC1=<cue> engine/build-hubcore/ss_tests`,
  `engine/build-hubcore/ss_game_tests` (ap_hub_state_seed, ap_hub_new_game_order,
  ap_hub_dungeon_doors), `tools/smoke_field_events.sh engine/build-hubcore/shattered-stars`.
- Seeds: `python3 scratchpad/hubcore/mkseed.py OUT --hero claude|rena|any [--start KEY] --bosses
  [--doors FILE.json] [--keys N,..]` (hub_state.json's hub block, hub_bosses.json rows, every
  dungeon key as item 900NN: console `ap_give 900NN`).
- New Game runs: `python3 scratchpad/hubcore/run_ng.py NAME SEED FRAMES claude|rena|any
  [KEY=VALUE..] [MAP=N] [ARGS+=..] [INPUT+=..]` (title -> New Game -> prologue skipped; field at
  ~vblank 3012); state check `python3 scratchpad/hubcore/chk_state.py DUMP HERO`.
- Starts: `python3 scratchpad/hubcore/starts.py claude|rena [KEY..]` -> runs/starts_HERO.txt.
- Doors: seeds doors_fc / doors_fc_k13 / doors_salva / doors_salva_k1 (doors_test.json), runs
  door_d1 / d2a / d2b / d3 / d4 (see "Done").
- Choices in play mode: `SS_SWEEP_PLAY=18:K26:1` answers that step's choices with option 1.
- Tower BFS: `python3 scratchpad/hubcore/bfs.py NAME SEED ROOT X,Y,Z,FACING MAPS` (hub state on a
  --map start via SS_HUB_STATE=1; logs scratchpad/hubcore/bfs/NAME).
- Exits / triggers per map: the late proof's sweep logs scratchpad/late_proof/claude/map_NNN.log
  ("entry N Tk end map M").

## Open questions
- Synard spots for Expel starts: the brief said "Expel's = Arlia's"; D9 (the user) says each town
  needs "its world's Synard parking spot", and a start on another Expel continent (Lacour, Linga,
  Hilton, Eluria Colony) with the Synard at Arlia could leave the party unable to fly. Done as:
  the start world's Synard at the start town's own spot (its set_point), the other world's at its
  arrival town (Expel: Arlia, Nede: Fun City), whose arrival scripts re-park it anyway (Virtual
  Arlia 18 0x5a4, the arena's old man 303 0x4ff4). The two given starts are unchanged.
- (For the coordinator) Shigeo's chamber 281 on the Nede -> Expel route: see "In progress".
- (For hubchambers) Berle 520's view step 20 (the climb; the 567 -> 627 soft-lock at step 0).

## For hubcore (from hubworld, 2026-10-25)
What the APWorld writes into a hub seed (apworld/so2/hub.py; docs/handoff/hubworld.md "Decisions"):
1. `version` 3; `options.world_mode` "hub", `party_rules` "open", `character_items` true,
   `expel_teleporter` false; no open-world doors (no 66 entrances, El gate, exit gates).
2. `hub` = hub_state.json `hub` (chapter, step, disc, flags, globals, games, lock) with
   `start` = the chosen row of `starts` **copied whole** (key, name, world, map, x, y, z, dir,
   door, exit, synard_expel, synard_nede, globals): the port takes `start.globals` after
   `hub.globals` as your `start_random` text says. `options.hub_start` is the option
   (`fun_city` / `arlia` / **`random_town`**: Archipelago reserves `random` for a Choice, where it
   rolls among the option's own values) and `options.hub_start_town` the key picked. The
   tool picks up your 16-row list (data.HUB_STARTS); `random_town` = any of them.
3. Dungeon keys: items `{"kind": "key", "key": N, "name": "Salva Drift Key"}`, N = hub_bosses.json
   `dungeons[].key` (1-19; Fienal's 20 never; the Portal of Sealing's 19 only once you decide it
   gets a P5 look: tell me, it is `hub.PORTAL_OF_SEALING`). A key exists only when something of
   this seed lies behind it; a dungeon without one has **no door rows** (its doors stay open).
   Doors: `{"id", "key": N, "routine": "0x....", "name": "Salva Drift (map 43 -> 63)"}` for every
   door hub_bosses.json lists for the dungeon (world-map entrances included, with their routine).
   The port must lock keys in hub mode (door_locked's comment says keys lock only in open world).
4. Fienal's entrance (68685691, routine 0x3de0): `{"id", "routine", "won": [...], "banners":
   [...], "name"}`, no key. `won` per fienal_requirement: [524, 525, 527, 528, 535] (default),
   [524, 525, 527, 528], or every chamber outside Fienal (a one-game chamber only when
   story_route names the game). Banners, please measure them with banner_text_width (<= 236):
   "Defeat Alen-Tax first", "Clear Cross Cave first", "Defeat Varmillion first", "Defeat Azamgil
   first", "Defeat Visseyer first", "Defeat Harfainx first", "Defeat Shin (Front Line) first",
   "Defeat Shin (Eluria Tower) first", "Defeat Berle (Eluria Tower) first", "Defeat the Synard
   first", "Defeat Arcmene first", "Defeat Magichand first", "Defeat Power's Guardian first",
   "Defeat Courage's Guardian first", "Defeat Lover first", "Defeat Bark first", "Defeat Marsilio
   first", "Defeat Shigeo first", "Defeat Berle (Armlock) first" (33 characters at most; the 35-
   character "Talk to Filia in Central City first" measured 225). The goal gate 43978493 as before.
5. `bosses`: all 22 rows always (whatever boss_checks), the port's keys as hub_bosses.json has
   them (`view` left out when null) + location, dungeon, keys ([N] or []), trigger, late.
6. **Party under story_route any:** no join scene runs at chapter 60, so `options.characters`
   holds **both Claude and Rena** (except one who is the starting character). The port must let
   the other hero join on their item and do nothing with the item of the hero picked at New Game
   (already in the party). Under story_route claude / rena the other hero is an ordinary item
   (party v2). Please confirm or tell me what the port needs instead.
7. **Doors into keyed dungeons that hub_bosses.json does not list** (docs/ap/doors.json, from a map
   outside the dungeon; "late" = the source map is one the late proof reached). If any can fire
   in the hub (in a story view too) it would bypass the key; they would belong in the dungeon's
   `doors` (the APWorld copies whatever hub_bosses.json lists):
   - Synard Plant: **17308906, 17308938 (264 -> 269, `chapter <= 38`: exactly what the Synard
     view on [264, 269] makes true)**, 17832691 (272 -> 269, unconditional, late), 17243118 /
     17243125 (263 -> 269);
   - Field of Power: 1967087 (map 30 -> 604, late), 40702453 (621 -> 624), 40768474 (622 -> 604);
   - Field of Love: 20316649 (310 -> 596, late), 17177602 (262 -> 596);
   - Salva Drift 3414860 (52 -> 62); Zand's Mansion 9512400 (145 -> 166); Lacour Front Line
     727656 / 727664 (11 -> 439 / 447); Eluria Tower 40833525 (623 -> 626), 40966369 (625 -> 627);
     Field of Intelligence 925456 (14 -> 556); Field of Courage 40965800 (625 -> 642); Fienal
     40966633 (625 -> 657); Portal of Sealing 20185245 (308 -> 313). (Most look like scene
     transfers from vision / disc-switch maps; the three "late" ones and Synard's 264 doors first.)

## For hubcore (from hubchambers, 2026-10-25)
1. **A small seed.{h,cpp} edit, one optional view key (I make it unless you object here):**
   `view.done_flag` (int, a script flag 0-2943). StoryView gets `int done_flag = -1;`, parsed
   next to `until` (`get_int_or(v["done_flag"], -1)`, refused outside 0-2943 with "view.done_flag:
   a flag 0-2943 expected"). Meaning (hub.cpp, mine): after the boss is won, the row's view still
   turns on in its `maps` while that REAL flag is clear (marked won from the start). Why: the Field
   of Power (525) has a walk between the Guardian (603) and its end scene (604's region, guarded
   only by flags 528 && !163, no chapter). Proven headless: leave 603 after the win and come back,
   and the vision runs at chapter 60, its step store is dropped and the party is left on 622 at
   chapter 60, 163 never set. With `done_flag: 163` the view comes back until the vision's end.
   Only row 525 uses it so far. I keep the edit to those lines; tell me if you'd rather write it.
2. FYI, hub.cpp (mine): a won view now ends at free play only after the field's own free-play rule
   (no event / talk, the thread not waiting on a load / fade / music / choice, no fade or window,
   controls unlocked) has held 20 frames in a row. ap's in_free_play() is true for a frame when a
   battle hands the field back mid-scene (Shin 447: the view ended there and the aftermath ran
   outside it). No change in ap.cpp needed.
3. On hubworld's item 7 (doors into keyed dungeons): several of those are the scenes' own vision
   returns played INSIDE a story view: 622 -> 604 and 30 -> 604 (Power), 262 -> 596 and 310 -> 596
   (Love), 14 -> 556 (Intelligence), 625 -> 642 (Courage). Locking them with their dungeon's key is
   harmless (the party holds that key whenever it is in the chamber), but nothing else (no `won`,
   no redirect) may stop them, or the scene strands the party on the vision map.
- **From hubworld, 2026-10-25 (applied your answers):** the Portal of Sealing is out of hub mode in
  the APWorld (no key registered, nothing there a location). Door rows now carry `routine` only
  when every transfer of that routine on that map leads into the dungeon (doors.json, the tool's
  `door_routine`): Zand's Mansion 9314034 is written without it, as you advised; the other 27
  keep theirs. The Synard doors (17308906 / 17308938 / 17832691) reach the seed as soon as they are
  in hub_bosses.json `dungeons` (the tool copies whatever it lists; their routines 0x7390 / 0x7410
  / 0x677c only lead into the plant, so they keep them).

## Coordinator rulings (2026-10-25, after hubcore's hand-back at ~477k)
- **Expel Synard spot:** accepted as done: the start world's Synard at the start town's own spot
  (its set_point), the other world's at its arrival town (Expel: Arlia, Nede: Fun City). It is
  what D9 says ("each town needs its start point and its world's Synard parking spot").
- **Arena 305 override:** a redirect that applies only while Marsilio (encounter 532) is unbeaten
  (a door-row condition on goal_won, or an equivalent port key), so after the win the antechamber
  door 304 -> 296 is the original again: nothing permanent, nothing one-way. hubworld has ended:
  hubcore2 adds the row to the APWorld itself (apworld/so2/hub.py), regenerates and reruns the
  world tests.
- **Shigeo 281 on the Nede -> Expel route:** hubchambers checks it with a real walk. If crossing
  281 starts the fight, STOP and report: the fix is the user's call (D2 / D9).
- **Berle 520 view at chapter 33 step 20:** hubchambers applies it (their row); hubcore2 rechecks
  the climb through it once it lands.
- Continue from "In progress" / "Next steps" as agent **hubcore2** (same rules, own build
  engine/build-hubcore).

## hubcore2 (continues from "In progress" / "Next steps"; same rules; started 2026-10-25)

### Log
- **558 -> 559 walked (no override needed).** The stairs are ramps in 558's floor zones (cell 0
  list 14, read with tools/map_zones.py): zones 0/1 (left, z 570-998) and 2/3 (right)
  go from y 0 at x 685 down to y 450 at x -250, landings 8-11 (y 450), then zones 14-17 down to
  y 650. Walk: arrival (1834, 0, 1) -> left (z ~750) -> up (-x) down the ramp to (-506, 450, 826)
  -> left: y 490 at z 1150, trigger 4 (0x3c00, needs y > 400) -> map 559 at (880, -60, 842);
  mirrored right: trigger 2 (0x3c60) -> 559 at (935, -54, -830). Berle 520's view (step 20 seed)
  stays on into 559. Runs scratchpad/hubcore/keep/w558_s3.log / w558_s4.log (input
  `30:left,210:up,790:left` / `30:right,210:up,790:right`, --map 558 --map-pos 1834,0,1,2048,
  SS_HUB_STATE=1, seed hub_tower20n).
- **Tower climb rechecked on hubchambers' landed row** (Berle 520 view 33/20, hub_bosses.json
  13:28; seed hub_tower_now.apso2 = mkseed --bosses; BFS scratchpad/hubcore/bfs/towernow, summary
  bfs_towernow.out): from 559 every map 557-573 reached; 567 -> 566 only (no 627 split scene);
  569 Shin 519 won; 573:T4 -> 574 -> 575, Berle's scene, 520 won, "the transfer to map 576 goes to
  map 557 (-92, 0, 1907) instead (cut_to)", view off at 557 (dropped step 1000, 535, 745). With
  p5_tower (557 -> 558) and the 558 walks, **P5 Eluria Tower is done.**
- **SS_HUB_STATE=rena** (hubproof's request): on a --map debug start builds Rena's game (games.rena,
  flag 1 clear). Run hs_rena (--map 558): "hub New Game: Rena's game", flag 1 = 0, G[0xc4] =
  0x20001; unit test in ap_hub_new_game_order. Answered in docs/handoff/hubproof.md. (ruling: only while Marsilio 532 is unbeaten). Port: seed door key
  `to.while_unbeaten` (seed.h Door::to_while_unbeaten; seed.cpp parses it, refuses a non-id);
  ap.cpp on_map_transfer: a target door whose encounter is in goal_won keeps its own target
  (logged "door N: encounter 532 won, the door's own target (map 296)"). APWorld: hub.P5_DOORS
  (door 19929509 -> 305 (-13, 0, 276, 271), while_unbeaten 532) appended by seed_doors;
  test_hub.test_seed checks it; scratchpad/apworld2/batch/check.py accepts exactly that row.
  World tests 312 passed (22695 subtests). data.py NOT regenerated yet: hub_bosses.json is still
  changing under hubchambers (its rows differ from data.py now); regenerate at the wrap-up.
  Headless (seed scratchpad/hubcore/seeds/hub_arena2.apso2, doors arena_door.json; runs arena_a,
  arena_b): New Game (Fun City) -> 279:T0, 280:T12, 281:T3, 297:T4, 299:T2, 304:T4 -> "door
  19929509: map 296 (-6, 0, 1014) -> map 305 (-13, 0, 276)", view on (57/20) -> K16 option 2 ("Go
  see Narl") -> battle 532 (stub) won -> the scene's transfer to 296, view off (dropped step 2000,
  745, 2327) -> 296:T2 -> 304:T4 -> "encounter 532 won, the door's own target" -> map 296 at
  (-6, 0, 1014).
- **Finding (user decision):** 305 inside Marsilio's view has ONE entry, the Clerk K16 (training /
  rest / "Go see Narl"); no walk-out exit (sweep arena_sw). A party entering 305 while 532 is
  unbeaten can only leave by winning Marsilio (it can train and rest there). True for any way in,
  the paid arena registration at 297 too.

- **(5) Saves in hub mode: proven** (seeds hub_save_fc / hub_save_arm; cards and sidecars in
  scratchpad/hubcore/guard/; runs hsave_* / hload_*). Hub New Game -> camp-menu save
  (SS_SAVE_ANYWHERE, smoke's input shifted to ~vblank 3600) -> a new process loads it
  (SS_LOAD_SAVE=0): "loaded ...slot01.ap.json", no "hub New Game" line (the New Game state is not
  re-applied: on_new_game_state runs only in field_create_state_block's new-game branch), the
  party at the saved position (480, 0, 68 after a walk; the start is 470, 0, 68), `ap_status`
  "hub | lock chapter on step on | chambers 22", G[0xa0] 60 / G[0xa4] 4000. Saved inside Berle
  535's town chamber (Armlock start, map 306, view on): the load turns the view back on with the
  map's load ("story view on: map 306 (Berle, encounter 535): chapter 58 step 0") while the real
  G[0xa0] stays 60 (the save holds the real state).

### The save guard (the user, 2026-10-25: "block save editing for the archi runs"; all modes)
- Design (coordinator's): with a seed on, a save loads only when its sidecar names this seed and
  slot and holds the hash of the save as it is on the card; refused before it reaches the game
  (the game's own damaged-save path); runs in progress stay playable (adoption stamps the hash).
- Code: engine/src/util/sha256.h (new, header-only SHA-256 + `save_digest`: "sha256:" + the hash of
  the save file's first N bytes, N = u16 at +0x21a, the save's length); game/ap: SaveState
  `save_hash`, sidecar version 2 (reader takes 1 and 2), on_saved(card, index, file, length)
  stamps the hash, `load_allowed(card, index, file, length)` (no sidecar / other seed or slot / no
  hash / mismatch -> false + "[ap] load refused, save N of CARD: ..."; SS_AP_ANY_SAVE=1 lets it
  through, logged: headless tests only, windowed seed runs clear SS_*); game/common/menu_save.cpp:
  ProgressDialog passes the read-back write_buffer to on_saved; load_finish calls load_allowed
  before touching the buffer and returns -2 (the "load failed" path; SS_LOAD_SAVE: "no valid save
  at that position"); launcher/ap_seed.cpp adopt_seed_saves: each copied sidecar stamped
  (`stamp_sidecar`: save_hash replaced or added first, version -> 2).
- Proven: unit tests ap_save_guard (SHA-256 vectors; this seed's save loads; refused: no sidecar,
  an edited byte, another seed, another slot, no hash, the field stripped, a wrong hash; a byte
  past the save's length is not the save's) and launcher_archipelago_seed_memcard (the adopted
  sidecar stamped, text otherwise kept; a version-1 sidecar adopted reads as version 2 and its
  save passes the guard, an edited byte doesn't). Headless title-menu load of an edited save
  (run tload_edit, input 850:start, 1051:cross x5): "load refused ... changed outside the game"
  and the game's "The data could not be loaded. Your data may be faulty." (screenshot
  scratchpad/hubcore/guard/t4.png). Smoke: new cases ap_guard_hash / ap_guard_ok / ap_guard_edit
  / ap_guard_plain / ap_guard_any (mini run: all PASS; ap_save's card-identity still PASS); the
  older cases that load an ordinary save with a seed (ap_load, the wrun world-map cases,
  ap_p4_world) now run with SS_AP_ANY_SAVE=1.
- Docs: seed_format.md "The save sidecar" (save_hash, the guard, adoption, not cheat-proof),
  apworld/so2/docs/setup_en.md Troubleshooting entry, engine/README.md bullet.

### (6) Wrap-up
- **Hub smoke cases** (tools/smoke_field_events.sh, last block; seeds made by an inline python3 from
  hub_state.json / hub_bosses.json; skipped without disc 2): ap_hub_ng_fc (Claude, Fun City: the
  New Game line, the party at 470,0,68, the Synard at 81546,-258,33880, Red Crystal refused for
  its key, Fienal refused for Magichand, banners fit, chapter 60 step 4000), ap_hub_ng_arlia
  (Rena, Arlia: the start, out to map 1, the Synard at 51209,-150,17464), ap_hub_ng_linga
  (Claude, Linga for random_town: out to map 1, the Synard at 90815,-150,13368), ap_hub_key (key
  13 in start.items: 2:T21 -> 419), ap_hub_fienal (console `battle 524` + stub: 2:T10 -> 687),
  ap_hub_arena (the redirect, the view, 532 won, the view off at 296, then the original door).
  22 checks, all PASS alone (scratchpad/hubcore/guard/mini_hub.sh, 23 s).
- Docs: seed_format.md "Hub": "The port: New Game, the starts, the doors, P5" (new), its
  "Sidecar" (saves proven), the door table's `to.while_unbeaten`; engine/README.md hub bullet;
  docs/catalog/formats.md (map data section 0's scene / floor zones; "The hub's P5 places": 557
  0x7808, 558 stairs and 0x3c00 / 0x3c60 / 0x3c44, 305's set-up and the Clerk); new tool
  tools/map_zones.py (a map's floor zones).
- APWorld: data.py regenerated from hub_bosses.json 13:28 (Berle 33/20, the Synard doors, the
  Portal group `hub: false`; 40 lines differ from before; regenerates identically); world tests
  312 passed (22700 subtests); AP general 379 passed (117125 subtests).

- **Suites (build-hubcore, final code, 2026-10-25 ~13:50):** ss_tests 99/0 (SS_DISC1), ss_game_tests
  170/0, smoke 161/0 (132 + 7 save guard + 22 hub). APWorld: hub batch scratchpad/apworld2/batch/
  hub_p5 (players_hub, seeds 1-96): 96/96 exit 0, check.py 0 errors on 192 files (it now also
  checks the P5 door row); world tests 312 passed, AP general 379 passed.

### hubcore2 status
- Done: P5 (Eluria Tower walks + the climb on the landed row; the arena redirect while 532 is
  unbeaten, port + APWorld), (5) saves in hub mode, the save guard (user request, all modes), (6)
  smoke cases, docs, catalog, suites; SS_HUB_STATE=rena for hubproof.
- Decisions for the user (reported, not fixed):
  1. **Shigeo 281** (hubchambers' walk: forced): his view (57/2000) is on whenever the party is on
     281 and the strip T0 (x 82..347, z 181..241) lies between the 280 doors and the 297 / 292
     doors, so every crossing of 281 while Shigeo is unbeaten starts his scene and fight. 281 is on
     the only Nede -> Expel route (279 -> 280 -> 281 -> 297 -> 299 -> 303's old man) and on the way
     to the arena. Options: (a) accept; (b) a port prompt when the strip fires in the view ("Face
     Shigeo now?": yes plays the original scene, no lets the party pass); (c) a key / gate for the
     town chamber's view (against D3 / D4 "towns open").
  2. **The arena 305**: inside Marsilio's view the Clerk (K16) is 305's only entry, so a party that
     comes in (the P5 door, or the paid registration at 297) leaves only by winning Marsilio (it can
     train and rest there). Options: (a) accept; (b) a prompt at the antechamber door / the
     registration ("Go to Marsilio's arena?"); (c) a port-side way out of 305 while the view is on.
- Open (others): hubchambers' remaining chambers (Fienal pairs etc.), XINE; hubproof's proof; ext
  0xCB stack underflow (hubchambers reported it to the coordinator). data.py must be regenerated
  again if hub_bosses.json changes after 13:28 (`python3 tools/ap_world_tables.py`, then the world
  tests and a hub batch).

### How to verify (hubcore2)
- Build `cmake --build engine/build-hubcore -j8`; suites as above (`SS_DISC1=<cue>`).
- Tower: `python3 scratchpad/hubcore/bfs.py NAME SEED 559 880,-60,842,0 557-576,627` (seed from
  `scratchpad/hubcore/mkseed.py OUT --hero claude --bosses`); 558 walks: run_ng.py with MAP=558,
  SS_HUB_STATE=1, `ARGS+=--map-pos 1834,0,1,2048 --input 30:left,210:up,790:left`.
- Arena: seed with scratchpad/hubcore/arena_door.json, SS_EVENT_SWEEP=1 SS_BATTLE_STUB=1
  SS_SWEEP_PLAY=279:T0,280:T12,281:T3,297:T4,299:T2,304:T4,305:K16:2,296:T2,304:T4 (smoke
  ap_hub_arena does it).
- Saves: run_ng.py with SS_SAVE_ANYWHERE=1, SS_MEMCARD1=CARD and the camp-menu input from 3600
  (log "hsave_*"); load with NOINPUT=1 SS_LOAD_SAVE=0 SS_MEMCARD1=CARD.
- Guard: smoke ap_guard_*; unit tests ap_save_guard, launcher_archipelago_seed_memcard.

## For hubcore2 (from hubproof, 2026-10-25 13:30)
1. **Request: Rena's game on a `--map` debug start with `SS_HUB_STATE`.** The hub proof sweeps from
   the hub New Game state through your P1 code (`SS_HUB_STATE=1` on a `--map` root), but a debug
   start is always Claude's game (field_create_state_block only takes `--map` in Claude's branch),
   so on_new_game_state applies `games.claude` (flag 1, G[0xc4] = 0x20100) and Rena's G[0xc4]
   (0x20001) can't be put back afterwards (SS_SCRIPT_GLOBALS is written before it). Proposal, in
   on_new_game_state only: `SS_HUB_STATE=rena` on a debug start picks Rena's game (`claude =
   false`: games.rena, flag 1 clear); `1` / `claude` as today. The party I fix myself
   (SS_SWEEP_PARTY_DROP=0, SS_SWEEP_PARTY=1). Until it lands I sweep Rena's game with the late
   proof's env state (SS_SCRIPT_GLOBALS / SS_SWEEP_FLAGS from hub_state.json). Please tell me here
   ("For hubproof" in docs/handoff/hubproof.md) when it's in, or if you'd rather not.
2. FYI, Eluria Tower in the hub proof: the sweep fires a door routine without walking, so it can't
   follow your two walks (557 -> 558, 558 -> 559). Unless you plan a sweep-side way (e.g. putting
   the leader in the trigger's region first), the proof adds 558 (its arrival) and 559 (your walk's
   arrival 880, -60, 842) as extra roots, citing your runs w558_s3 / s4 and p5_tower. The tower's
   rows are rerun once hubchambers' Berle 520 view is at step 20.
3. The arena 305: when your redirect row (while Marsilio 532 is unbeaten) is in hub.py, the hub
   proof's seed takes the same row; tell me its form here.

## For hubcore (from hubchambers, 2026-10-25 ~15:30)
1. Done in tools/ap_hub_bosses.py / docs/ap/hub_bosses.json: Berle 520's view is at **33/20**; the
   Synard Plant group lists 17308906 / 17308938 / 17832691; the Portal of Sealing group is marked
   `"hub": false`. `view.done_flag` is in seed.{h,cpp} (appended to StoryView, parsed after `until`).
2. **Shigeo 281 crossing: yes, it is forced.** 281's entries in the view (57/2000): the Shigeo strip
   T0 (0x855c) spans x 82..347, z 181..241 (trigger space), between the 280 doors at the bottom
   (T5 z 300..316, T6 the right edge) and the 297 door (T3 z 119..137) / 292 door (T1 z 11..25) at
   the top; the region's routine re-tests 57/2000 (true in the view). So while Shigeo is unbeaten,
   crossing 281 in either direction starts his scene and fight (as the story intends at 57/2000).
   I report it as a user decision with options (a key / a gate for the town chamber's view, or
   accept it); proven pieces: shigeo chamber run PASS (280:T12 -> 281:T0).
3. ext 0xCB (sound_start_clip, script_ext.cpp) fetches 5 stack args, but both call sites in the game
   (642 0x6e3c Courage's Guardian, 575 0x761c Berle's tower scene) push 4: the port underflows the
   script value stack (ASan: heap-buffer-overflow in script_vm.cpp push, with or without a seed).
   Not mine to fix (field VM); reported to the coordinator.

## For hubcore (from hubchambers, 2026-10-25 ~17:00)
- FYI: a second `to.while_unbeaten` row is proposed to hubworld: door 44502760 (679 -> 674) lands on
  674's east arrival (882, 0, 30, 180) while Cyril 538 is unbeaten (from the west the scene's hero
  walk never ends). Proven with your port code from a seed (fienal3b). Nothing for you to do.
- The Eluria Tower climb with Berle at 33/20 is proven from 568 (568:T1 -> 569 Shin 519 -> 570:T3
  -> 573:T4 -> 574 -> 575 Berle 520 -> cut_to 557), both heroes; hub_bosses.json has step 20.
