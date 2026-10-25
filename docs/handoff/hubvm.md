# Handoff: hubvm (P2 chapter lock + P6 story view in the script VM, behind the hub seed)

Agent "hubvm", started 2026-10-25. Plan and the user's decisions: docs/handoff/hub.md (D2: original
boss scenes through a per-chamber story view). Design: scratchpad/openworld/story_flags.md 3 "M-B",
4.4 P2 / P6, risks R3 / R6.

## Rules given
- Build in engine/build-hubvm (`cmake --build engine/build-hubvm -j4`). Tests:
  `SS_DISC1=<cue> engine/build-hubvm/ss_tests` (99/0 at start), `engine/build-hubvm/ss_game_tests`
  (156/0), `tools/smoke_field_events.sh engine/build-hubvm/shattered-stars` (120 pass). Run the
  suites before reporting anything verified.
- Other agents: "hubdata" (tools/, docs/ap/: hub state, boss table, the seed format's Hub section),
  "gifts" (apworld/). My edits: the VM / field program hook points, engine/src/game/ap/ (hub
  gating), engine/tests/, my subsection of seed_format.md, engine/README.md, catalog rows.
  Keep the tree compiling after each edit.
- Headless only (`--headless`), never a GUI or PCSX-Redux's GUI (`-cli` only), never a flag that
  saves ~/.config/shattered-stars/config.ini; every game process under tools/memcap.py
  (SS_MEM_CAP_GB 4); at most 2 at once.
- The port enhances, never degrades; a normal game and story / open-world seeds play exactly as
  the original; hub behaviour only behind the hub seed.
- This log current at least every 20 minutes. Don't commit. Scratch in scratchpad/hubvm/. Cap
  command output. Permission refusal: stop, report the exact command. Past ~400k tokens: update
  this log and end the turn saying so.

## For hubdata: what the port needs in the seed (proposal, 2026-10-25 ~09:00)

hubdata owns the format text (seed_format.md "Hub"); this is what the port will read. Change
names freely, but tell me here.

- `options.world_mode: "hub"` turns the hub on (Seed::hub). The chapter lock and the story views
  run only then.
- Top-level `bosses` (array), one row per chamber:
  ```json
  {"encounter": 191, "name": "Alen-Tax", "map": 61, "maps": [61],
   "view": {"chapter": 6, "step": 2100, "flags": {"745": 0}, "globals": {"0x1be": 2}, "keep": [745]}}
  ```
  - `encounter`: the boss battle. A victory in it is recorded in the sidecar's `goal_won` (the
    existing list of won encounters that door `won` conditions read; so P4's "Defeat X first"
    doors work with no new sidecar key). The boss is "unbeaten" while it is not there.
  - `map` (+ optional `maps`, default `[map]`): the chamber. While the party is on one of these
    maps and the boss is unbeaten, the view is on.
  - `view` (optional; absent = no view: a flag-guarded boss such as Visseyer 412 plays at the
    hub's own state):
    - `chapter`, `step`: what the chamber's scripts read in G[0xa0] / G[0xa4] (0xa0-0xa7).
    - `flags` {"n": 0|1}: flags the scripts see with these values; their reads and writes go to
      the view. Flags not listed read and write the real flags as usual (the boss's guard flag,
      key items 66-91, chest flags).
    - `globals` {"0xNNN": value} (optional): more script global words (s32, byte offset >= 0xa8)
      redirected the same way (e.g. a quest state).
    - `keep` [n, ...]: the whitelist: listed view flags whose value at the view's end is written
      to the real flags (everything else stored in the view is dropped).
- The view ends at the first map load outside `maps` (the scene's transfer out, or the player
  walking out) or at the first free play after the boss battle was won; New Game / a load reset it.

## Done and verified (2026-10-25 ~13:30)
- Code (engine/build-hubvm builds clean):
  - seed.h/.cpp: `Seed::hub` (world_mode "hub" or 2), `hub_lock_chapter` / `hub_lock_step` /
    `hub_step_maps` (hub.lock.chapter / step / step_maps, default [685]), `bosses` (HubBoss:
    encounter, name, map, maps, game, view {chapter, step, flags, globals (word or {size, value}),
    keep}); version 3 accepted (1-3; test_ap's version check now uses 4).
  - NEW engine/src/game/ap/hub.h / hub.cpp: the lock and the views (g_hub_vm, hub_var_read /
    hub_var_write / hub_flag_read / hub_flag_write, hub_swallows_party, hub_swallows_disc_change,
    hub_start / hub_reset / hub_map_loaded / hub_free_play / hub_boss_encounter / hub_battle_won,
    hub_status, hub_log; SS_HUB_TRACE=1 traces every game-state / flag store in a view and each
    flag's first read). CMakeLists: hub.cpp added to ss_game.
  - ap.cpp hooks: start_with / stop (hub_start), on_new_game / on_loaded (hub_reset),
    on_script_loaded (hub_map_loaded), free_play_update (hub_free_play after the free-play test),
    on_battle_exit (hub boss victory -> goal_won; hub_battle_won), status_text (hub line).
  - script_vm.cpp: every global variable access goes through var_get / var_get16s / var_set
    (incl. inc/dec, block_store's global case), flag_get / flag_set ask the hub first; all behind
    `local == 0 && ap::g_hub_vm`; g_instr_at = the executing instruction's offset.
  - script_ext.cpp ext 0xae / 0xaf: hub_swallows_party first; script_table_b.cpp B 0x6a:
    hub_swallows_disc_change.
- Tests: engine/tests/test_script_vm.cpp ap_hub_seed, ap_hub_chapter_lock (every store form,
  once-per-site log, step map, story seed / no seed unchanged, disc switch), ap_hub_story_view
  (reads / writes redirected byte by byte, through stores, keep / drop, party, battle, free-play
  end, no view once won), ap_hub_view_ends (walk out / re-entry, maps, New Game / load reset,
  game filter, open-world seed). Smoke: ap_hub_arcmene (+ _hubstory / _none), ap_hub_lock,
  ap_hub_lock_story (12 checks).
- Optional view keys (hubdata's proposal, accepted as written): `view.after_maps`, `view.until`
  ("free_play" / "transfer"), `view.cut_to` {map, x, y, z, dir} (seed.h StoryView; hub.cpp
  in_view_maps / hub_map_transfer, called first in ap::on_map_transfer). Unit test
  ap_hub_view_keys; not proven headless on their chambers yet.
- Suites: ss_tests 99/0; ss_game_tests 161/0 (156 + 5 hub tests); smoke 132/0 (120 + 12 hub
  checks: smoke2.log; smoke3.log = the re-run on the final code, see the report).
- Headless proofs (scratchpad/hubvm/run.py: the late proof's state; seeds in scratchpad/hubvm/seeds;
  logs in scratchpad/hubvm/runs; state diffs with scratchpad/hubvm/cmp.py):
  - visseyer (control, no view; 409 door T6 -> 412 trigger T2, flag 483 cleared): scene + battle at
    chapter 60, check 1364328657, goal_won; real state: flag 483 set (its guard), nothing else but
    the map-entry counter G[0x1d8], the speaker byte G[0xc6] and the battle's own exp/items.
  - alentax (68 door T6 -> 61): view on (chapter 6 step 2100, flag 745=0); Rena's party_add dropped;
    scene, battle 191, check 1364328639; the aftermath's transfer to Arlia 53 ends the view
    ("dropped step 3000, flag 745=1, 1 party changes"); after: chapter 60 step 4000, no flag
    changed, globals: only G[0x1d8].
  - arcmene (418 door T2 arriving at y -200 -> 417): view on (chapter 40 step 1000, flags 745 / 11 /
    12 = 0, keep [11]); scene, battle 522, check 1364328970; the transfer to the Nede world map ends
    it ("dropped chapter 41, step 0, flag 12=1, flag 745=1; kept flag 11=1"); after: chapter 60
    step 4000, no flag changed. Through (not redirectable): set_point (table A 0x19, native) parks
    Nede's Synard at the cave's exit where the party lands (G[0x314] / 0x318 / 0x31c / 0x330),
    G[0xbc] music -1, G[0xc6] speaker byte, G[0x1d8] counter.
  - harfainx (515 door T4 -> 523 trigger T2; view chapter 24 step 2000): an in-place ending: the
    view ends at free play in the chamber ("dropped step 3000"); firing the trigger again after it
    starts nothing (its routine re-tests chapter / step); after: chapter 60 step 4000, flag 77 set
    (the key item the scene gives, through: key items are never tested).
  - controls: alentax_noseed / arcmene_story: chamber empty at chapter 60 (no scene), as the
    original. lock547 (hub, no 547 row): the Cross Cave chest's "chapter store 13 dropped at map
    547 offset 0x6a80" and "step store 0 dropped ... 0x6a88", chapter stays 60 / 4000;
    lock547_story: chapter 13 step 0 (original).
- Docs: seed_format.md "The port: the chapter lock and the story views" (end of the Hub section)
  and the version note in "The version rule" (the port reads 3); engine/README.md Archipelago
  bullet "The hub"; docs/catalog/globals.csv script_flags (flag 745) and game_state (+0x26,
  +0x12a / +0x12c, +0x138).

## What a view can't handle (per-boss checks for whoever writes the chamber specs)
1. Native game-state writes are not redirected: table A 0x19 set_point (Arcmene parks Nede's
   Synard next to the party: harmless), the music word, battle results. Script stores to flags /
   globals not listed go through (key items: Harfainx gives 77).
2. In-place endings keep the room as the view set it up (actors, registered triggers) until the
   next load; a trigger whose routine does not re-test the story state could start the scene
   again with the real state. Harfainx re-tests (proven). To check: Azamgil 463, Guardian 603,
   Lover 596, Shigeo 281, Ghost 479, the Fienal pairs 657 / 669 / 674.
3. Scenes that need a particular arrival: Arcmene starts only when the party arrives at y < 0
   (418's lower door, routine 0x6660); a view entered another way shows the chapter-40 room
   (Arcmene standing) without the scene. hubcore's chamber doors must keep the scene's arrival.
4. Chambers on town maps (Shigeo 281 / Marsilio 305 in Fun City, Berle 306 in Armlock): the whole
   map shows the view's chapter (NPCs, doors) on every visit until the boss is beaten.
5. Multi-map scenes are cut at the first transfer out of `maps`; the rest (in the next map) does
   not play: Berle 575 -> 576 (its chapter-34 store dropped by the lock; the disc switch never
   comes), Synard 269 -> 271, Shin 447 -> 439, Magichand 554 -> 555, Guardian 642 -> 625,
   Varmillion 462 -> 135, Bark 454 -> 455, Marsilio 305 -> 296, Berle 306 -> 314. Rewards given
   after the cut are lost unless `maps` spans that map (then that map runs in the view too).
6. Party changes are dropped in views: split scenes fight with the whole party (Azamgil in Rena's
   game: Rena alone in the story); B 0x29 / 0x2a party isolation is not touched.
7. Flags a scene only reads must be listed at the values its story point had; SS_HUB_TRACE prints
   each flag's first read in the view (value and whether the view or the real state served it).

## For hubdata (answers, 2026-10-25 ~12:00; ~13:50: the three proposed keys)
- `view.after_maps`, `view.until`, `view.cut_to`: accepted unchanged and implemented (drop
  "proposed" from the format text). cut_to applies to the first script transfer to a map outside
  maps / after_maps after the win, ahead of the door table; the view then ends at that load even
  when cut_to.map is in `maps` (Berle's 557).
- `game`, `version: 3`, `world_mode` "hub" / 2, `hub.lock` read as written. Step exception: I read
  an optional `hub.lock.step_maps` [maps] (default [685]); write it if your list grows.
- Byte globals: the port takes either form: `"0x1cc": word` (all 4 bytes redirected), or
  `"0x1cf": {"size": 1, "value": 8}` (only that byte; offset aligned to size). Prefer the object
  form for byte / half globals (the word form freezes the neighbouring bytes at the seed's values).
- **Arcmene: don't keep flag 12.** It is CLEAR in the late save (flag 11 set): the story clears it
  later (flag 12 makes a disc of Nede's world map unwalkable, worldmap_bg world_terrain_query).
  The aftermath sets 11 and 12; keep only 11 (or neither: 11 is in the hub state). Proven with
  view flags {745: 0, 11: 0, 12: 0}, keep [11].
- Flag 745 (0x2e9) is set by both the Alen-Tax and Arcmene scenes (a scene doing its own fade-in;
  the set-up's default fade-in runs only while it is clear). Every map's library set-up clears it
  (61 0x38a8, 417 0x5730), so listing it in a view is harmless but not needed.
- Party ext 0xae / 0xaf in a view are dropped (Alen-Tax's scene adds G[0xc5], the other hero).
- What a view can't redirect: native writes to the game state (table A 0x19 set_point: the
  Synard's spot; the music word; battle results). Arcmene's aftermath parks Nede's Synard next to
  the party at the cave (harmless). SS_HUB_TRACE=1 lists every game-state store a scene makes in
  its view ("through" / "view"): use it to write the flag lists.

## In progress
- Nothing (final report sent 2026-10-25 ~14:00).

## Design decisions
- var_ptr / flag_ptr were NOT the only paths (block_store 0x61-0x63 wrote globals directly; inc/dec
  used one pointer for read and write; native code reads flags 0-23, the chapter on the world map
  (worldmap_bg) and the save icon's progress, and writes the game state natively: set_point, music,
  battle results). Now every script global access goes through var_get / var_set and flag_get /
  flag_set, which ask the hub (byte-granular) only with a hub seed.
- Step lock: yes. Outside a view, at chapter 60 the only legitimate step stores are Fienal's
  (657 / 669 / 674 move 1000 -> 4000 inside their views per D10; 685 stores 5000 on the way to the
  endings) and L'Aqua 686's 0 -> 1000 (not reached at the hub's step 4000). Every other step store
  that can run at chapter 60 is paired with a dropped chapter store (547's / 576's / Arlia 22's /
  the Fields' "chapter N, step 0") or flag-guarded from an earlier chapter, and would knock the
  hub's step off Fienal's 4000. So: dropped except on hub.lock.step_maps (default [685]).
- The view starts at the chamber map's script load (before its set-up runs), stays on across the
  battle (the field reloads the same map), ends at a map load outside `maps` or at the first free
  play (field state 0, control, script depth 0) after its boss was won. A load / New Game resets
  it; loading a save in a chamber turns the view on again with the map's load (boss unbeaten).
- Party ext 0xae / 0xaf are dropped only inside a view (D7: characters are items; the party v2
  rules handle joins elsewhere). B 0x6a (disc switch) is dropped in hub mode always.

## Next steps
1. (Per-boss work, hubcore / hubproof) headless checks of the other chambers' views with
   hub_bosses.json, SS_HUB_TRACE for their flag lists; the in-place endings' re-armed triggers;
   after_maps / until / cut_to on Courage 642, Power 603/604, Berle 575.
2. (Later, with hubdata's table) more bosses' views; multi-map scenes (Berle 575 -> 576 -> 625 is
   cut at 576 by the view's end; its chapter-34 store there is dropped by the lock).

## How to verify
- `cmake --build engine/build-hubvm -j4`; `engine/build-hubvm/ss_game_tests` (ap_hub_* tests);
  `SS_DISC1=<cue> engine/build-hubvm/ss_tests`; `tools/smoke_field_events.sh
  engine/build-hubvm/shattered-stars` (ap_hub_* cases).
- A proof run: `python3 scratchpad/hubvm/run.py NAME SEED FRAMES MAP KEY=VALUE...` (late state,
  memcap'd, headless; FLAGS+=!483 appends to SS_SWEEP_FLAGS), e.g.
  `python3 scratchpad/hubvm/run.py arcmene hub.apso2 12000 418 SS_SWEEP_PLAY=418:T2
  SS_STATE_DUMP=30:scratchpad/hubvm/runs/arcmene.pre SS_SWEEP_PLAY_DUMP=scratchpad/hubvm/runs/arcmene.post`,
  then `grep '\[ap\]' scratchpad/hubvm/runs/arcmene.log` and `python3 scratchpad/hubvm/cmp.py
  scratchpad/hubvm/runs/arcmene.pre scratchpad/hubvm/runs/arcmene.post`.
  Doors / triggers used: 409:T6 -> 412 (Visseyer T2), 68:T6 -> 61, 418:T2 -> 417, 547:K60 (chest).

## From hubdata (2026-10-25 ~11:10): the format is written (seed_format.md "Hub (`world_mode: hub`)")
- Your `bosses` row shape is adopted unchanged: `encounter`, `name`, `map`, `maps`, `view`
  {`chapter`, `step`, `flags` {"n": 0|1}, `globals` {"0xNNN": word}, `keep` [n]}; victory into
  `goal_won`; view ends at the first map load outside `maps` or the first free play after the win.
- Added for the port: `game` ("claude" / "rena", optional): the row applies only in that hero's
  game (Varmillion 462 is Claude's, Azamgil 463 Rena's). Reference keys the port may ignore:
  `location`, `dungeon`, `keys`, `trigger`, `late`.
- `version: 3` for hub seeds (a wave-2 port refuses them). `world_mode: "hub"` (or 2).
- `hub` block (for hubcore, P1; you need only `lock`): `chapter` 60, `step` 4000, `disc` 2,
  `flags`, `globals` {"0xADDR": word}, `games` {claude / rena: flags, globals}, `start`,
  `lock` {"chapter": true, "step": true}. Step lock exception: map 685 (the goal fight's
  aftermath stores step 5000 before the endings) -- I am still checking which maps need it; will
  note here if the list changes.
- Values in the section's example rows are illustrative until docs/ap/hub_bosses.json exists.
- Question for you: any byte-granular global a view needs (the floor G[0x1cf]) I give as its
  word with the hub's other bytes. OK, or do you want {addr, size, value}?

## From hubdata (2026-10-25 ~12:40): table written, three optional view keys proposed
- docs/ap/hub_bosses.json (tools/ap_hub_bosses.py): 22 chamber rows (`bosses`), 20 dungeon key
  groups. Adopted your answers: `hub.lock.step_maps` [685]; byte globals in the object form; 745
  listed as 0 wherever a scene stores it; Arcmene {745: 0, 11: 0, 12: 0}, keep [11]. No row needs
  `view.globals` yet. The party flags 740-744 (stored by every boss scene) are listed at the hub's
  value (0) so they drop.
- Views with an approach room in `maps` (solve P5 without a door override): Bark [453, 454] at
  (55, 0) (453's door picks 455 at chapter > 55); Synard [264, 269] at (38, 4000) (264's doors
  pick 270 after chapter 38); Berle 575 over the whole tower 557-575 at (33, 0) (Shin 569 plays
  inside it).
- Proposed optional keys (seed_format.md "Hub", marked "proposed"); please accept / rename / reject:
  - `view.after_maps` [maps]: the view continues into these when the scene transfers there from a
    view map, but they never turn it on themselves (Field of Courage 642 -> 625 vision -> 642 in
    Claude's game; Synard 269 -> 271; Magichand 554 -> 555 / 628 / 556).
  - `view.until`: "free_play" (default) or "transfer": Field of Power [603, 604], whose end scene in
    604 is guarded only by flags (528 == 1 && 163 == 0): the view must last until the party leaves.
  - `view.cut_to` {map, x, y, z, dir}: after the win, the first transfer out of the view goes there
    instead. Only Berle 575 needs it: 576 stores chapter 34 unconditionally and moves on to 625
    (disc-switch scene map) -> cut_to = 557 (-92, 0, 1907, dir 270), the tower's world arrival.
  Without these three, those four chambers fall back to: Courage/Synard/Magichand cut at the first
  transfer (landing on a scene map at chapter 60: 625 / 271 / 555, unproven), Power's 604 scene
  running outside a view, Berle landing in 576 -> 625.
