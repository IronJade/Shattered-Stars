# Handoff: the hub open world (`world_mode: hub`)

Started 2026-10-25 by the main session ("Start the hub open world"). The design is
`scratchpad/openworld/story_flags.md` sections 3-5 (mechanisms M-A / M-B, the boss table in
section 2, the port pieces P1-P6, work items W1-W10, risks R1-R8); this file holds the user's
decisions, which override that design where they differ, and the plan.

## The user's decisions (2026-10-25)

- **Mode:** a new `world_mode` value (`hub`) beside `story` and `open_world`, which keep the
  story (decided earlier: "Both: roads now, hub later").
- **D1 mechanism:** the hub start: New Game starts after the Final Save Point (chapter 60, the
  late proof's state, flag 730, disc 2, every location's done flags cleared); both worlds open
  (Energy Nede by the Synard, Expel by the Fun City arena's old man / Virtual Expel's Synard);
  nothing one-way; a chapter lock keeps the chapter at 60.
- **D2 boss scenes: "Original scenes from the start".** Each story boss plays its original scene
  in its chamber through a per-chamber story view (story_flags.md P6 / M-B: while the boss is
  unbeaten, that chamber's map sees the boss's chapter / step / flags; the view ends at the
  scene's first transfer out or at free play; stores made in it are dropped except a whitelist).
  The story view is core work, not optional. A restaged battle (P3) is only the fallback for a
  boss whose scene can't be made self-contained; say which and why.
- **D3 unlocking: "Every boss independent".** No chained quest lines: each chamber is open as soon
  as its dungeon's key has arrived. Beating a boss is its location (check).
- **D4 keys: towns open, ~20 dungeon keys** (one per dungeon, including dungeons entered from a
  town: Salva Drift, the Mars dungeon, Zand's Mansion, the Synard Plant, the Portal of Sealing...).
- **D5:** the four losable fights (Zand 265, Dias 204 / the tournament, Shin on the ship 517,
  Marsilio's first Fienal fight 530) are not hub chambers. The Cross Cave guardians (set battle
  523, chest flag 2569) are a chamber.
- **D6 Fienal's requirement: "Make these options in the yaml".** A YAML option, three values: the
  four Fields' bosses + Berle at Armlock (default), the four Fields only, every hub story boss.
- **D7:** characters are always items in hub mode (join scenes don't run at chapter 60); the
  open-world party v2 rules and the starting character apply.
- **D9 start: "YAML option":** Fun City (279, default), Arlia, or random; **random picks from any
  town in both worlds** (the user, 2026-10-25: "Any town"), so each town needs its start point and
  its world's Synard parking spot.
- **D10 Fienal: "Their original scenes":** the three Fienal pairs (Ruprecht & Nicolus 657,
  Vesper & Decus 669, Cyril 674) play their scenes at their earlier steps through the story view;
  Fienal is proven again.
- **Gifts (all modes, 2026-10-25): "NPC gift checks should NOT hold progression."** (Before:
  the 9 gift sites the late proof reaches could.)
- Keys go in chests (hub-proven) and boss checks; drops per `drop_progression` /
  `area_keys_in_drops`; never shops, gifts or crafts.
- Goals (indalecio / limiter_off / + Iseria Queen) work in hub mode too (Filia's scene needs
  chapter >= 58 and flag 730, which the hub has; the Clik flags are preset by `goal.flags`).
- **Version (2026-10-25): hub mode ships in so2.apworld 0.2.0** (the user: "This is the first
  major revision of the apworld as is. The rest have up to now been fixes."). No 0.3.0; 0.2.0
  stays unreleased until the hub is done.

## The plan

Order (story_flags.md 5.1, adjusted for D2 / D3): W1 hub state data + W3 boss chamber table →
the VM (P2 chapter lock + P6 story view) → P1 hub New Game → chamber specs + headless checks per
boss → P5 the chambers the hub can't reach (Eluria Tower past 557: Shin 569 / Berle 575; the
Synard Plant 269; the Field of Intelligence's 554; Mihne 454; the arena 305) → the hub proof
(late proof from the hub start with the chapter lock) and a `--bosses` check → W6 APWorld
(world_mode hub, regions, dungeon keys, boss locations, the Fienal option, the start option,
seed blocks) → docs.

| Agent | Work | Log |
|---|---|---|
| gifts | APWorld: gifts never progression, all modes (0.2.0) | docs/handoff/apworld2.md |
| hubdata | W1 hub state data (`docs/ap/hub_state.json` + tool) and W3 the boss chamber table (chambers, guards, scene entry, story-view spec per boss), and the seed's `hub` / `bosses` format in seed_format.md | docs/handoff/hubdata.md |
| hubvm | Port: P2 chapter lock and P6 story view in the script VM (var_ptr / flag_ptr), behind the hub seed | docs/handoff/hubvm.md |
| hubcore | Port: P1 hub New Game, the start option (every town: start points + Synard spots), sidecar, dungeon-key doors and the Fienal gate, P5 (Eluria Tower 557 -> 558, the arena 305, the Portal of Sealing look) | docs/handoff/hubcore.md |
| hubchambers | The 22 chambers proven headless from the late state (SS_HUB_TRACE flag lists, in-place-ending re-fire, after_maps / until / cut_to, Fienal pairs D10, the map 14 vision), rows fixed in tools/ap_hub_bosses.py, a chamber runner in tools/ | docs/handoff/hubchambers.md |
| hubworld | APWorld W6 (world_mode hub, regions, dungeon keys, boss locations, D6 / D9 options, characters as items, seed v3 blocks, tests, docs) | docs/handoff/hubworld.md |
| (next) hubproof | W7: the late proof from the hub start with the lock, `--bosses`, e2e | |

## Status

- 2026-10-25: decisions taken; gifts, hubdata, hubvm started.
- 2026-10-25: **gifts done.** rules.py `may_hold_progression` / `open_world_progression` treat
  `gift` as never-progression (like `drop` / `join` / `shop`), in either world mode, for this
  world's own keys and other worlds' progression alike. World tests 240 passed (was 235); the
  96-seed batch (full) regenerated, 96/96 exit 0, check.py (extended with the gift rule) 0
  errors; no fill failure with expel_progression off. docs/ap/apworld.md and
  docs/handoff/apworld2.md ("Session 4") updated; the player guide needed no change (it never
  claimed gifts may hold progression). Folds into the unreleased so2.apworld 0.2.0.
- 2026-10-25: **hubdata done** (docs/handoff/hubdata.md). tools/ap_hub_state.py ->
  docs/ap/hub_state.json (step 4000; 1,063 flags set: 24 reset from the late state: 9 boss
  guards, 13 PA, flag 556 Limiter Off, key item 84; relationships are config +0x58 / +0xe8 and
  take New Game's values; saved party order +0x280 zeroed; Expel's Synard spot = Arlia's, Nede's =
  the start town's; starts Fun City 279 (470,0,68,180) and Arlia 17 (-126,0,-614,90)).
  tools/ap_hub_bosses.py -> docs/ap/hub_bosses.json: 22 chambers, 19 with a story view
  (Visseyer, Shin 569, the Cross Cave guardians play by their cleared guards); Bark, Synard and
  the Field of Intelligence opened by their views; still P5: Eluria Tower 557 -> 558 (Shin 569,
  Berle 575) and the arena 305's doors; no restaged-battle fallback needed so far. 20 dungeon
  key groups (towns' chambers Armlock 306, Fun City 281, the arena 305 need none). Format: the
  "Hub" section of seed_format.md, version 3. Coordinator rulings on its questions: a key only
  where a check lies behind it (Lasguss / Portal of Sealing: decide from what's behind them);
  no Fienal key beyond the D6 requirement; town chambers keyless (towns open); the Field of
  Intelligence's vision on map 14: hubvm / hubcore decide (cut unless harmless).
- 2026-10-25: **hubvm done** (docs/handoff/hubvm.md). engine/src/game/ap/hub.{h,cpp}; every
  global access in script_vm.cpp through var_get / var_set / flag_get / flag_set (block store
  0x61-0x63 and inc/dec bypassed var_ptr), asking the hub only when `ap::g_hub_vm`; party changes
  dropped inside a view, the disc switch dropped in hub mode; seed: world_mode "hub", version 3,
  hub.lock, bosses rows with view {chapter, step, flags, globals, keep, after_maps, until,
  cut_to}. Chapter and step stores locked outside views (steps except `step_maps` [685]).
  Proven headless: Visseyer (control), Alen-Tax, Arcmene (chapter 41 / flag 12 dropped, flag 11
  kept), Harfainx. Limits: native game_state writes aren't redirected; unlisted flags pass
  (Harfainx's key item 77); in-place endings keep the view's room set-up until the next load
  (check Azamgil 463, Guardian 603, Lover 596, Shigeo 281, Ghost 479, Fienal 657 / 669 / 674);
  Arcmene needs 418's lower door; town chambers show the boss's chapter town-wide until beaten;
  multi-map scenes need maps / after_maps / cut_to (Berle 575, Synard 269, Shin 447, Magichand
  554, Guardian 642, Varmillion 462, Bark 454, Marsilio 305, Berle 306); split scenes fight with
  the whole party. build-hubvm: ss_tests 99/0, ss_game_tests 161/0, smoke 132/0.

Next (a fresh session): **hubcore** (P1 hub New Game from hub_state.json, the start option with
every town for "random" (start points + Synard spots), sidecar, dungeon-key doors, P5 for
Eluria Tower 557 -> 558 and the arena 305, the per-boss specs checked headless for every chamber
with SS_HUB_TRACE, the in-place-ending re-fire check), then **hubworld** (APWorld W6: world_mode
hub, regions, 20 dungeon keys, boss locations, the Fienal option (D6), the start option (D9),
characters forced to items, seed v3 blocks) and **hubproof** (the late proof from the hub start
with the lock on, `--bosses`, e2e).
- 2026-10-25 (new session): hubcore, hubchambers (split out of hubcore: the per-chamber checks run
  from the late state and need no P1) and hubworld started. Rulings passed on: no Fienal key (D6
  gate only: 19 keys at most); Lasguss Mountain keeps its key only if XINE is a hub location; the
  Portal of Sealing gets a P5 look (hubcore) or its key and chest leave hub mode; the map 14
  vision: hubchambers decides (cut unless harmless).
