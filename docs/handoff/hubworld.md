# Handoff: hubworld (APWorld W6: `world_mode: hub`)

Agent "hubworld", started 2026-10-25. Plan and the user's decisions: docs/handoff/hub.md (D1-D10,
the gifts rule; they override scratchpad/openworld/story_flags.md). Format: docs/ap/seed_format.md
"Hub (`world_mode: hub`)" and "v2 fields". Data: docs/ap/hub_state.json, docs/ap/hub_bosses.json,
docs/ap/late_proof.json. How the APWorld is built and checked: docs/handoff/apworld2.md.

## Rules given
- Work: (1) options (WorldMode `hub` = 2; HubStart fun_city / arlia / random = any town of both
  worlds, data-driven from hub_state.json `starts`; FienalRequirement fields_and_berle / fields /
  all_bosses; how the existing options behave in hub mode), (2) hub locations (only what the hub
  reaches), (3) regions and rules (towns open, both worlds open, one key per dungeon; no Fienal
  key; Lasguss key only if XINE is a hub location; Portal of Sealing behind a flag until hubcore
  decides; every boss independent; Fienal gate = D6 event; every location reachable), (4)
  placement (progression only in hub-proven chests and boss checks; drops per options; never
  shops / gifts / crafts; characters always items), (5) seed v3 (only for hub; story / open-world
  seeds stay version 2 and identical in content), (6) tests + batch, (7) docs.
- Verify per apworld2.md "How to verify" (scratchpad/ap/Archipelago, ../ap-venv,
  SKIP_REQUIREMENTS_UPDATE=1, -n 8). Baseline: world tests 240 passed; batch 96/96, check.py 0.
- Own: apworld/so2/ (all), tools/ap_world_tables.py (adding only; data.py must regenerate
  identically), docs/ap/apworld.md, the player guide, scratchpad/hubworld/, batch additions.
  NOT engine/, tools/ap_hub_state.py, tools/ap_hub_bosses.py, docs/ap/hub_*.json (ask owners).
- No game runs (if any: headless, tools/memcap.py SS_MEM_CAP_GB 4, at most 2, never GUI, never a
  config-saving flag). Scratch in scratchpad/hubworld/ (not /tmp). Cap output. Kill by exact PID.
  Wait loops with deadlines; long jobs in the background.
- Don't commit / push. Log current after each piece and at least every 20 minutes.
- Permission refusal: stop, report the exact command. Past ~400k tokens: update the log, end the
  turn. A user decision (D1-D10, content): stop and report with a recommendation.
- Notes to other agents: dated "For hubcore" / "For hubchambers" in THEIR log; read "For
  hubworld" here.

## Decisions (and why)
- **Version (the user, 2026-10-25, via the coordinator):** hub mode ships in so2.apworld **0.2.0**
  ("This is the first major revision of the apworld as is. The rest have up to now been
  fixes."). No 0.3.0; apworld.md and the player guide document hub mode as part of 0.2.0.

- **Option names.** `world_mode: hub` (2); `hub_start`: fun_city (default) / arlia /
  **random_town** (Archipelago reserves `random` for a Choice: it would roll among the option's
  own values; `random_town` picks any row of hub_state.json `starts`, all 16 towns since
  hubcore's update); `fienal_requirement`: fields_and_berle (default) / fields / all_bosses.
- **Locations = what the hub reaches** (hub.py `choose`, `reached_games`): chests / gifts / cave
  and optional bosses the late proof proved (258 / 9 / 10 / XINE; **since hubproof, 2026-10-25:
  the hub proof's, data.HUB_REACHED: 290 / 9 / 10 / XINE in both games, docs/ap/hub_proof.json**), the 22 chambers (the 21
  story-boss rows + a new hub-only row "Cross Cave: Guardians boss", id 1364328971, from
  data.HUB_LOCATIONS), shops open at the hub state (tool: map late-reached + conditions possible at
  chapter 60 step 4000 with the hub's flags, not in a PA: 41 Claude / 42 Rena; the hub proof's
  maps since hubproof: unchanged), drops met there (82 types per game; 85 on the hub proof's
  maps), crafts, killer moves. Never: joins (D7), losable fights (D5), unproven rows (Eluria
  Tower's 23 chests etc.: the hub proof added them and Fienal's maze's 9: 290 chests now). Missable reasons: the soft ones
  (window, time-limited) don't apply (nothing closes); hard ones stay (EXCLUDED / remove);
  "story_route" when reached in only one of the route's games (and the other game's chamber
  under story_route any).
- **Keys** (hub.py `needed_keys`): a dungeon's key exists only when one of this world's
  locations (not a drop) lies in it, a chamber of the Fienal requirement, or the Iseria Queen's
  cave (goal 2). Never Fienal; the Portal of Sealing behind `hub.PORTAL_OF_SEALING` (False).
  Lasguss keeps its key exactly when XINE is a location (boss_checks on, not removed). Hub keys
  have their own AP ids (HUB_KEY_BASE = 0x50370000 + key number from hub_bosses.json; the seed
  describes them as `{"kind": "key", "key": N}`; the port matches doors by N).
- **XINE** is proven by the late proof in both games (hub_state.json's note says no optional boss
  is armed; the late proof disagrees for XINE): a location, filler only ("optional quest")
  until hubchambers confirms it (`hub.CONFIRMED_OPTIONAL`).
- **Regions:** Menu -> Expel, Energy Nede (towns, open) -> one region per keyed dungeon; Energy
  Nede -> Fienal on the event "Fienal opened" (location "Fienal Gate (Fienal requirement)": the
  requirement chambers' keys; D3 makes "can beat boss X" = "has X's key"). Events: Indalecio
  (Victory) in Fienal; Limiter Off (no rule: chapter 60 + flag 730); Iseria Queen in the cave.
  Drops: an any-of access over the dungeons they are met in ("" = free). Killer moves: learn
  level <= 30 free, else "Fienal opened" (story mode: the disc switch / Final Save Point).
- **Party (D7):** party_rules open always (character_items forced on; no warning, it would fire
  on the default). Fixed route: open world's rules. story_route any: no join scene runs at
  chapter 60, so **both heroes are items** (the one picked at New Game does nothing; the port
  must accept it: note to hubcore). Characters and keys replace filler in the pool.
  Killer "hero" mode under any: Claude's moves need Claude's item (progression_characters
  generalized: pool members whose levels may hold progression; unchanged elsewhere).
- **all_bosses** = every chamber outside Fienal; a one-game chamber (Varmillion / Azamgil) only
  when story_route names the game (under any the seed can't know which one the player has).
- **Seed:** version 3; `hub` = HUB_STATE + `start` = the chosen start row as hub_state.json has
  it (with its `globals`); `bosses` = every chamber row (port keys + location, dungeon, keys,
  trigger, late; `view` omitted when null); `doors` = each keyed dungeon's doors (id, key,
  routine, name), Fienal's entrance (68685691) with `won` + one banner per boss (<= 33 chars;
  hubcore to measure), the goal gate as before. options gain hub_start, hub_start_town,
  fienal_requirement, fienal_won, dungeon_keys **only in hub mode** (story / open-world seeds
  unchanged).
- data.py: the tool appends a hub section (HUB_STATE, HUB_STARTS, HUB_BOSSES, HUB_TOWN_CHAMBERS,
  HUB_DUNGEONS, HUB_LOCATIONS, HUB_SHOPS, HUB_DROPS); the pre-hub data.py is a byte prefix of the
  new one (checked).
- **Portal of Sealing: out of hub mode** (hubcore's decision, 2026-10-25): items.HUB_KEYLESS =
  (Fienal, Portal of Sealing) (no key registered), hub.OUT_OF_HUB skips its rows (none are proven
  anyway). The flag PORTAL_OF_SEALING is gone.
- **Door routines** (hubcore's finding): a door row carries its `routine` only when every
  transfer of that routine on that map leads into the dungeon (docs/ap/doors.json; the tool's
  `door_routine`); else the row has no routine and the port refuses the transfer alone. Today
  only Zand's Mansion 9314034 (routine 0x77cc also leads to Herlie 157).
- hubcore confirmed: keys lock in hub mode; the 19 Fienal banners fit (widest 223 px); both heroes
  as items under story_route any works ("Claude is in the party already" / "Rena joins"); the 16
  starts proven headless; `hub.start` copied whole is right (port reads name / map / x / y / z /
  dir / globals).

## Done and verified
- 2026-10-25: options, hub.py (locations, keys, regions, rules, seed blocks), items (dungeon
  keys, "Fienal opened"), locations (hub row, "Boss Chambers" group), world.py wiring, rules.py
  (killer_rule hub events; no SO2 key of either kind in drops with area_keys_in_drops off),
  tools/ap_world_tables.py hub section (+ `door_routine`), test/test_hub.py (6 classes).
- World tests: **312 passed, 22635 subtests** (was 240; +72 from test_hub). data.py reproduces
  (cmp), and the pre-hub data.py is a byte prefix of it.
- AP general tests: **379 passed, 117127 subtests** (scratchpad/hubworld/test_general.log).
- First batch (before the Portal / door-routine changes): hub 96/96 exit 0, check.py 0 errors on
  192 files (154 hub slots); identity 96/96, compare.py vs `full`: 192 identical.
- check.py (scratchpad/apworld2/batch/check.py; the pre-hub copy check_pre_hub.py) catches all 10
  mutations of scratchpad/hubworld/neg (mutate.py: progression in a gift, version, no Fienal
  gate, a wrong door key, a missing chamber row, the hub state, the requirement, a starting key,
  an under-filled party, an unproven chest).
- Docs: docs/ap/apworld.md ("Hub mode" section, Locations / Items / Options / The seed / Files /
  Testing / Changes 0.2.0 / Next), the player guide ("Hub mode", items, options), options.py
  docstrings. The YAML template (AP's generate_yaml_templates, as tools/build_apworld.sh) renders
  world_mode hub, hub_start, fienal_requirement (scratchpad/hubworld/templates).
- **Final batches (final code: Portal out, door routines):** hub (players_hub, seeds 1-96):
  96/96 exit 0, check.py **0 errors** on 192 files / 192 combinations (154 hub slots, 38 story /
  open world; starts arlia 41, fun_city 38, random_town 75; requirement 76 / 39 / 39; 13-18 keys;
  192-1671 locations); hub progression: chests 3136, bosses 306, drops 622, killer moves 299,
  none in gifts / shops / crafts. Identity (players, seeds 1-96, `full_hubworld`): 96/96 exit 0,
  compare.py vs `full`: **192 identical**, check.py 0 errors.

## In progress
- Nothing running.

## Next steps
1. Waiting: hubchambers on XINE (set hub.CONFIRMED_OPTIONAL = {199} if it plays at the hub
   state; if not, drop it: add 199 to a "not in the hub" set), on the Synard doors in
   hub_bosses.json, on row changes (each time: `python3 tools/ap_world_tables.py`, the world
   tests, a hub batch); hubproof (replace the late proof's reach at hub.py's "hubproof" places and
   in the tool's `hub_tables`): **done by hubproof 2026-10-25 (docs/handoff/hubproof.md)**.
2. The version stays 0.2.0 (the user's decision); nothing else open for the APWorld.

## How to verify
    ln -sfn $PWD/apworld/so2 scratchpad/ap/Archipelago/worlds/so2
    cd scratchpad/ap/Archipelago
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 worlds/so2/test
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 test/general
    python3 tools/ap_world_tables.py -o scratchpad/hubworld/x.py && cmp scratchpad/hubworld/x.py apworld/so2/data.py
    cd scratchpad/apworld2/batch && ./run_batch.sh players_hub hub $(seq 1 96)    # ~8 min; hub.done
    ./run_batch.sh players full_hubworld $(seq 1 96) && python3 compare.py full_hubworld full
    cd ../../ap/Archipelago && SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python ../../apworld2/batch/check.py ../../apworld2/batch/hub
    (negative test: python3 scratchpad/hubworld/neg/mutate.py <a hub .apso2>, then check.py on scratchpad/hubworld/neg: 10/10 caught)

## Waiting on other agents
- hubcore: done for the APWorld (starts landed, keys lock, banners fit, both heroes as items,
  Portal out, doors advice applied).
- hubchambers: XINE (CONFIRMED_OPTIONAL); the Synard doors 17308906 / 17308938 / 17832691 into
  hub_bosses.json `dungeons` (hubcore asked them); any chamber that can't be made to work.
  Row changes flow in by regenerating data.py.
- hubproof: the real hub proof (replaces late_proof.json's reach: hub.py `reached_games`,
  `may_hold_progression`, `drop_rule`; the tool's `hub_tables` late maps for shops / drops).
  **Done (hubproof, 2026-10-25):** data.HUB_REACHED / HUB_MAPS from docs/ap/hub_proof.json.

## For hubworld (notes from other agents)

- **From hubcore, 2026-10-25 (answers to your note in hubcore.md, and `starts`):**
  1. **`starts` landed** in docs/ap/hub_state.json: a LIST of 16 towns, keys fun_city, arlia,
     salva, cross, clik, mars, herlie, hilton, lacour, linga, eluria_colony (Expel), central_city,
     north_city, armlock, giveaway, laqua (Energy Nede). Row: key, name, world (1 Expel / 2 Nede),
     map, x, y, z, dir (the town's world-map entrance's arrival), door (that entrance), exit (the
     sweep entry that walks back out, for proofs), synard_expel, synard_nede, globals (the seven
     Synard words). Copying the row whole as `hub.start` is right: the port reads name / map / x /
     y / z / dir / globals and ignores the rest; start.globals is written after hub.globals and
     games[hero]. hub.globals still holds Fun City's words. **All 16 proven headless in both
     games** (New Game -> free play at the start -> the exit -> the world map, chapter 60 step
     4000, the Synard parked where the row says). Synard rule (D9: "its world's Synard parking
     spot"): the start world's Synard at the town's own spot (its set_point), the other world's at
     Arlia's / Fun City's (where the cross-world arrivals land and re-park it). Armlock's start is
     map 306, Berle 535's town chamber: its view is on from the arrival (the scene needs its
     trigger region; free play is reached).
  2. **Keys lock in hub mode now** (ap.cpp missing_keys). Your door rows work as written: proven
     headless with a world entrance (Cave of Red Crystal 68686044: refused "Cave of Red Crystal Key
     needed", opens with the key), a town field door (Salva 43 -> Salva Drift 63, 2824679) and the
     Fienal gate (`won` [524] + banner: refused, then map 687 after the win).
  3. **Banners measured** (headless, the map font): all 19 of your Fienal banners and every
     "<key> needed" text fit; widest "Defeat Berle (Eluria Tower) first" 223 px (max 236), "Cave of
     Red Crystal Key needed" 218. The port now logs any too-wide seed banner at the first free play
     ("door banners measured: N, M too wide").
  4. **Portal of Sealing: out of hub mode** (key 19 and its one chest: the Mind Ring, map 387, flag
     2474). The way in is three story-gated doors (306 -> 308 and 308 -> 313 at chapter 52 step
     1000, 313 -> 400 at 52/2000), the lab's way out is gated as well (400 -> 401 at 53/4000), and
     306 is Berle 535's chamber (a view at chapter 58): not cheap or safe for one chest.
  5. **Lasguss Mountain:** nothing but XINE lies behind it (no chests); its key follows
     hubchambers' answer on XINE.
  6. **Both heroes as items under story_route any: confirmed.** Headless (hub seed, any, Claude
     picked): receiving Claude logs "Claude is in the party already" (nothing else), receiving Rena
     "Rena joins the party (slot 1)". Nothing needed from you.
  7. **Doors not in hub_bosses.json:** needed: 17308906 and 17308938 (264 -> 269, live inside the
     Synard view); safe to add: 17832691 (272 -> 269; every transfer of routines 0x7390 / 0x7410 /
     0x677c leads into the plant, so the routine-level lock blocks nothing else). **Zand's Mansion
     9314034 (142, routine 0x77cc):** that routine also leads to 157 (Herlie, door 9314065), so a
     row WITH its routine would block that exit too while the key is missing: write it without
     `routine` (the port then refuses only the transfer) or leave it out (a Private Action path,
     flag 2). The rest of your list are scene / vision maps or need a chapter the hub never has
     outside a view whose maps they are not in: leave them out. I asked hubchambers (owner of
     tools/ap_hub_bosses.py) to add the Synard ones to the table.

### For hubworld (from hubchambers, 2026-10-25 ~15:30): docs/ap/hub_bosses.json rows changed
Regenerated (tools/ap_hub_bosses.py; deterministic). Please re-copy the rows:
- **Every row:** flags 740-744 are gone from `view.flags` (they are recomputed from the inventory at
  every map's set-up, not story state); so no row has `view_check` 740-744 any more.
- **New optional view key `view.done_flag`** (int, a script flag; the port reads it: seed.{h,cpp} +
  hub.cpp): after the win the row's view still comes on in its `maps` until that real flag is set.
  Copy it like `until` / `cut_to` when present. Only row 525 has it (163).
- 524 Magichand: `after_maps` [555, 628, 556, **14**] (Rena's vision on Expel's map 14 is followed:
  cutting it would strand Rena's game on Expel without 556's end scene, key item 82 / flag 162).
- 525 Guardian (Power): `after_maps` [622, 27, 30], `done_flag` 163.
- 527 Guardian (Courage): `view.flags` lost 523 / 524 (door-arrival flags that must pass through).
- 528 Lover: `after_maps` [262, 310]; view flags + 542 / 546 / 549 (262's own, at hub values).
- 531 Bark: `after_maps` [455] (455's 55/1000 scene gives key item 89).
- 535 Berle (Armlock): `after_maps` [314] (the conclusion's dialogue); view flags + 380.
- 520 Berle (Eluria Tower): `view.step` 20 (was 0; hubcore's tower BFS).
- `dungeons`: Synard Plant doors + 17308906, 17308938, 17832691 (with `why`); Portal of Sealing
  group marked `"hub": false` (`why_not`).
- The vision returns into keyed dungeons (622/30 -> 604, 262/310 -> 596, 14 -> 556, 625 -> 642) run
  inside views; a key lock on them is harmless (the party holds the key), nothing else may stop them.
More to come for Marsilio 305, the tower (519 / 520) and Fienal (536-538); XINE answer to follow.

### For hubworld (from hubchambers, 2026-10-25 ~16:40): XINE and the other optional bosses
- **XINE (199, Lasguss 385) plays at the hub state, both games** (proven headless: hub state,
  384:T8 -> 385, the room's region 0x6df0 registered and its battle started; logs
  scratchpad/hubchambers/runs/list384{c,r}_384_T8.log). 385 has two XINE sites: 0x6628 (Ashton's
  quest: flag 113 && G[0x1bc] == 3000) and 0x6df0 (flag 754 && !113); both need 388 == 0 (XINE's
  own guard, set by the fight). The hub state has 754 set, 113 clear, 388 clear; 113 (Ashton's
  quest flag) is only set at Herlie 144 in chapters 16-19, so it can't change at chapter 60: XINE
  stays armed until won. It can hold progression; keep the Lasguss key.
- Not armable at the hub state (agrees with your list): Nightmare 197 (needs flag 113 and Ashton's
  quest G[0x1bc] 2000-2999; hub 9999), Flarelizard 196 (needs Opera's flag 141, set only at Hilton
  174 in chapters 20-22, and G[0x1be] == 2100; hub 0), Ghost 211 (needs chapter 24 step 3000).
- Cave of Trials: the late proof (done flags cleared) proved 503-510, 512, 513 in both games; the hub
  state differs from the late state in 24 flags, none of them 950-962, so those stand.

### For hubworld (from hubchambers, 2026-10-25 ~17:00): two more things for the APWorld
1. **A P5 door row for Fienal's third pair (proposed, proven headless):** entered from the west
   (679's door 44502760, arrival x -964) while Cyril 538 is unbeaten, 674's scene walks the hero
   into the enemies and never ends (a soft lock); from the east (676, x 882) it plays. Please add to
   `P5_DOORS`: `{"id": 44502760, "to": {"map": 674, "x": 882, "y": 0, "z": 30, "dir": 180,
   "while_unbeaten": 538}, "name": "Fienal (679 -> 674: Cyril's east side while unbeaten)"}`
   (tools/ap_hub_chambers.py runs it: fienal3b PASS). Pair 1 (657) is only entered from 687 while
   unbeaten (the west side 655 lies behind it); pair 2 (669) plays from either side.
2. **Logic: the arena's gift location 1349960704 (map 305, item 773, the prize at G[0x217] == 3)**
   is reachable only once Marsilio (532) is beaten: while he is unbeaten 305 is in his view, where
   only the Clerk (K16) exists. It never holds progression (gift), but its region rule should
   require 532 won if the logic tracks it.
