# World map / field gaps: continuation log

Worker: world agent (phase 3: closing the field program's remaining gaps). Build dir:
`engine/build-world`. Scratch (on disk): `scratchpad/world/` (Redux driver `world.lua` +
`run_redux.sh`, comparison scripts `cmp.py` / `side.py`, Ghidra helpers `ghd.sh` /
`gha.sh` on the project `analysis/ghidra_world`). Nothing is committed by the worker; the
coordinator commits.

## AP port fixes from the APWorld agent (sixth world agent, 2026-10-23 ~05:30-06:00): DONE, handed back

Assignment (coordinator): (1) a received Celine must never be stuck in reserve (Mars 462 / 126,
flag 112), and the same check for every character item; (2) `battle N` typed on the title
segfaults; (3) map 413's event sweep loops on reload. Suites, ap_e2e(_so2), ref_regress --quick,
a smoke case for Celine. Build `engine/build-world` (ninja -j8); scratch `scratchpad/world/celine/`
(fn.py MAP OFF: the routine around an offset; strs.py MAP IDX: decoded strings; leaves.py: every
library leave call with its unequip flag -> leaves.txt; party.py DUMP: party / flag 112 / chapter
of an SS_STATE_DUMP; env.sh; runs/), `scratchpad/world/loop/` (run413.sh), `scratchpad/world/title/`.

Done so far (uncommitted):
- (2) field_program() was left dangling when the Field was deleted (battle, title, credits):
  `field_program_delete` (field_program.cpp, used in field_program.cpp / encounter.cpp /
  field_section6.cpp) nulls it. Title run: the old binary segfaults (exit 139), the new one says
  "not in the field" 4 times and exits 0 (scratchpad/world/title/run.sh; give it stdin
  </dev/null, or the console's stdin reader keeps the process alive after the frame limit).
- (3) event_sweep.cpp: `reloads` / kMaxReloads 6: after 6 reloads in a row whose main script
  leaves the map before settling, the next entry ends "loop" and the sweep moves on. Map 413
  (late proof route 2:T21,419:T0): 7 entries, T0 map 419, T1-K64 "loop", done at vblank 2598.
- (1) Findings (scripts): Celine's real recruitment is the Cross Cave choice (map 524 0x7000,
  "Travel together with Celine?": 0 keeps her, 1 = leave routine 0x63e4(2, unequip 1) at 0x72bc).
  Map 131 (chapter 15) stores flag 112 = party_has(Celine). Claude's game: 124 (step 1000) adds
  her if absent; 462 T7 (step 2400) sends her home: flag 112 -> 0x63e4(2,0) (temporary), else
  unequip item 0x2e7 in slot 4 and 0x63e4(2,1) (for good). Rena's game: 126 (step 1000) removes
  Claude and Celine (Rena's solo); 125 (step 3300, setup) re-adds Celine (0x7dc0: ext 0xae + A
  0x3f) only with flag 112. Other partings for good: Leon 234 0x6eb0 (Eluria colony, choice 0
  "Waiting here is better"), Opera 479 0x8250 (choice 1 "this is where we part", after her own
  join at 0x8244). Everything else that removes an item character is temporary (193/195,
  444/447, 468-474 tournament, 548 ship, 422 Leon -> 230, 462 Dias -> 434/439, 71 Ashton).
- (1) Port: ap::keeps_flag (script_vm.cpp flag_set: a received Celine's flag 112 cannot be
  cleared) + keep_story_flags (set on receipt and in free play); ap::on_party_remove (script_ext
  ext 0xaf, before the remove): the four parting sites (kPartings) re-queue a received member
  for the next free play; any other leave of a received member drops a join still waiting.
- Runs (scratchpad/world/celine/runs): c1 (Claude's game, 462 T7 with Celine received, flag 112
  clear at start): flag 112 set on receipt, the scene sends her home keeping her gear, 125 at
  step 3300 rejoins her and records the check; c0 = the same on the pre-fix binary: flag 112
  stays 0, she stays in reserve. c524 (seed patch forcing choice 1): "Celine leaves at map 524
  ... rejoin at the next free play", then "Celine joins the party (slot 2)".
- Later the same turn: on_party_remove runs BEFORE the remove and only for an active member; a
  temporary leave of a received member drops a join still waiting (found by r1: the item's
  banner delayed her join, map 126 took her out, then join_waiting pulled her back mid-solo).
  on_party_add logs "join site M:O: X was received: the game's join"; keeps_flag logs each kept
  clear.
- More runs: r1 (Rena's game: 126 at step 1000 takes Claude + Celine out, "for a while"; 125 at
  step 3300 rejoins her, check recorded), k1 (map 131's store of 0 kept), l1 (Leon 234 choice 0
  -> rejoins), o1 (Opera 479 choice forced to 1 by patch `script 479 0x1e9d 0x0b000000
  0x12000001` -> rejoins). Cross Cave's choice patch: `script 524 0x1c09 0x0b000000 0x12000001`.
- Unit test ap_received_members_stay (ss_game_tests 108/0). Smoke: 7 new checks ap_cel_mars
  (4), ap_cel_131, ap_cel_cave (2); `scratchpad/world/celine/smoke_cel.sh` runs only them (7/7).
- Docs: seed_format.md "A received character stays recruited"; formats.md flag 112 + the leave
  routine; field.md Celine (flagged [world agent]).
- For the APWorld agent (logic): 124's join only runs when Celine is absent at step 1000, i.e.
  only if the player sent her away at Cross Cave and she was not received; a received or kept
  Celine skips it. 125 needs flag 112 (kept, or received). So 124 can be unreachable; in Rena's
  game choice 1 leaves no Celine site.
- No seed: the Mars scene (462 T7, Celine in the party, flag 112 clear) with the new build and
  the pre-fix build (scratchpad/late_proof/bin): state dumps at vblanks 500 / 4000 / 8000 /
  10000 byte-identical, sweep logs identical (she goes home for good, as on the console).
- Suites (engine/build-world, SS_DISC1, scratch XDG): ss_tests 78/0, ss_game_tests 108/0, smoke
  86/86 (79 + 7 ap_cel_*), tools/ap_e2e.sh 23/23, ref_regress --quick 28/28 (0 regress),
  ap_e2e_so2 24/25: the one FAIL is the APWorld agent's new session 2 (edited 05:37, in
  progress), which expects Bob's Map Bombs in Hearn's slot 2; the generated seed put an own
  Paralysis Trap there and the port recorded that check correctly.
- WARNING for whoever runs tools/ap_e2e_so2.sh: it does `rm -rf scratchpad/ap_e2e_so2` BEFORE
  its port check. My first try found port 38292 busy, so it may have removed a concurrent run's
  work dir (the APWorld agent's?) before exiting; later runs used a private copy
  (scratchpad/world/celine/e2e/ap_e2e_so2.sh, W in my scratch, AP_E2E_PORT=38295). The script
  should check the port first (the APWorld agent's file; not changed by me).

### Files changed this turn (none committed by me; the 05:16 checkpoint took the first part)
- engine/src/game/ap/ap.{h,cpp}: on_party_remove, keeps_flag, kPartings, keep_story_flags,
  character_received, the received-join log line.
- [world agent] hooks: field/script_ext.cpp (ext 0xaf), field/script_vm.cpp (flag_set),
  field/field.h + field_program.cpp + encounter.cpp + field_section6.cpp (field_program_delete),
  field/event_sweep.cpp (reloads / kMaxReloads).
- engine/tests/test_ap.cpp: ap_received_members_stay.
- tools/smoke_field_events.sh: ap_cel_mars / ap_cel_131 / ap_cel_cave (7 checks).
- docs: ap/seed_format.md, ap/field.md (flagged), catalog/formats.md, catalog/script_opcodes.csv
  (B 0x73 summary), handoff/world.md.

## AP phase 4, port side (fifth world agent, 2026-10-23 ~04:xx-05:xx): DONE, handed back

Finished the fourth agent's "Open / next" list (all four items). Build `engine/build-world`
(`ninja -j8`); scratch `scratchpad/world/ap4/shop/` (seeds, trace logs, frame captures,
`dump2300.bin` an `SS_STATE_DUMP` of the inventory after the shop run). Checked with the
enemy agent's log (`docs/handoff/enemy.md` "Phase 4 seed fields ... CONFIRMED"): the fourth
agent's proposal was confirmed as written by the fourth AP agent on 2026-10-23, no port
change needed, so nothing here adapts to an amendment.

1. **Shop shuffle, a real game run** (Hearn's store, map 26, shop 1). Seed: shop 1 entry 0
   (Sandals) is Alice's slot holding Bob's Hookshot (no `item` filter); entry 2 (Lavender,
   item 224) is Claude's own Revival Card. Headless run (`--debug`, console `fol 50000` first):
   walk to Hearn, confirm his Yes/No prompt (3 presses after the talk opens it), Buy, add 1
   Sandal, move to entry 2, add 3 Lavenders, confirm, confirm the settle dialog.
   - Log: `check 1365246464 (Arlia shop: Sandals): Hookshot for Alice` then
     `shop 1 entry 0 (item 728): the slot's check took the first copy of 1` (nothing given);
     `check 1365246466 (Arlia shop: Lavender): Revival Card` then
     `shop 1 entry 2 (item 224): the slot's check took the first copy of 3`.
   - Fol: 50000 -> 48520 (1x10 + 3x490), proving the purchase itself went through, not just the
     check.
   - `SS_STATE_DUMP` of the inventory after settling: item 21 (Revival Card) x1, item 224
     (Lavender) x2 (the two copies the shop itself still sells), item 728 (Sandals, sent to
     Alice) absent. Matches `ap_shop_slots` exactly, now proven in situ.
   - Unchecked-slot colour: a temporary trace in `draw_row` (removed again) confirmed
     `ap::shop_slot_open` returns true for entries 0 and 2 and false for 1/3-6 while the list is
     drawn, i.e. `set_colour(2)` does fire for the right rows. Visually the difference is subtle
     at this resolution (colour 2 in the item list's own CLUT is a muted grey next to colour 1's
     near-white, not the bright pink colour 2 uses in the money box's different font group) -
     that is the original game's palette for this CLUT, not a port bug.
   - Frame capture: `scratchpad/world/ap4/shop/cap2/frame01600.png` (the Buy list open: Sandals,
     Rose Hips, Lavender, Aquaberry, Blackberry, Blueberry, with the Fol/Purchase/Sales panel).
   - Smoke case `ap_p4_shop` added to `tools/smoke_field_events.sh` (6 checks: the list opens,
     the other slot's item is sent, none of it is given, our slot's first copy is the check, the
     other 2 copies are the shop's own item, the Fol total). Full smoke: 79/79 (73 + 6 new).
2. **tools/ap_e2e.sh, two new sessions** against the same private MultiServer and ChecksFinder
   stand-in multiworld, using more of its spoiler (`scratchpad/ap/out/*_Spoiler.txt`): Alice's
   Tile 7 (location 81006) and Tile 3 (location 81002) were unused by the first five sessions.
   - **Session 6 (key unlocks a door):** `seed_p4door.apso2` (`world_mode: open_world`, one
     door: map 17 -> Arlia, id 1116181, key 1, "Arlia Pass"; `items` relabels id 80000, Alice's
     own "Map Width", as the key). A second Python client (`bob_key.py`) connects and checks
     Bob's Tile 7 (location 81006, which the multiworld gives to Alice) without waiting for
     anything first. The port (reusing the smoke test's exact walk: right into the door,
     refused, idle, left back through it) is refused first (`door 1116181 ... locked, needs
     Arlia Pass`), then receives the key over the real network (`key 1 (Arlia Pass) received`,
     no console shortcut), and map 18 loads on the second approach.
   - **Session 7 (a shop check going out):** `seed_p4shop.apso2` (`shuffle_shops: full`; Hearn's
     shop 1 entry 2 is location 81002, Alice's Tile 3, holding Bob's "Map Bombs"). The port buys
     one Lavender (the same walk-and-buy sequence as the smoke case, trimmed to one slot); the
     check goes out over the network (`check 81002 ...: Map Bombs for Bob`,
     `shop 1 entry 2 (item 224): the slot's check took the first copy of 1`) and a third Python
     client (`bob_shop.py`) logs receiving it.
   - All 7 existing sessions still pass. Full run: 23/23 (16 existing + 7 new), twice.
3. **The APWorld's confirmation**: read (no port change needed; see above).
4. **Not done** (left optional, as the fourth agent left it): a disc-backed `--ap-validate` for
   doors. The port still checks each seed door's word as its map loads and logs/ignores a bad
   one; no separate pre-flight validator was added this turn.

Suites (engine/build-world, SS_DISC1, scratch XDG_CONFIG_HOME): `ss_tests` 78/0, `ss_game_tests`
107/0, `tools/smoke_field_events.sh` 79/79, `tools/ap_e2e.sh` 23/23 (run twice, both clean).

### Files changed this turn (none committed)
- `engine/src/game/field/menu_shop.cpp`: no net change (temporary trace added and removed while
  investigating the colour; the diff is only the fourth agent's).
- `tools/smoke_field_events.sh`: new case `ap_p4_shop` (shop shuffle in a real run).
- `tools/ap_e2e.sh`: sessions 6 (key unlocks a door) and 7 (a shop check out), two new seeds
  (`seed_p4door.apso2`, `seed_p4shop.apso2` written inline) and two new Python stand-ins
  (`bob_key.py`, `bob_shop.py` written inline), plus the header comment.
- `docs/handoff/world.md` (this file).
- Scratch: `scratchpad/world/ap4/shop/` (seed files, trace/dump logs, `cap2/` frame captures,
  full-suite logs).

## AP phase 4, port side (fourth world agent, 2026-10-23 ~03:00-04:00): done (port + smoke); its two "Open / next" items are closed above

Assignment (coordinator): open world, port side. (1) area keys gating doors (world_mode =
open_world), key list and door ids agreed with the APWorld agent (enemy line in
scratchpad/agents.txt); (2) the Expel teleporter on the game's own mechanism (design in
seed_format.md first; disc-swap flow intact); (3) shop shuffle (shuffle_shops specials /
full); (4) entrance-shuffle groundwork: seed-driven door targets + a validator. Unit tests,
smoke cases, ap_e2e.sh where a server matters; prove the no-seed path (suites +
`python3 tools/ref_regress.py --quick`).

Agreed with the APWorld agent (a1e6498ce791a7ac4, provisionally; it rolled over, its successor
confirms): door ids map * 65536 + offset / 4, docs/ap/doors.json from tools/ap_doors.py; key
items {"kind": "key", "key": N} (AP id ITEM_BASE + 0x40000 + N), top-level "doors": [{id, key,
to}]; teleporter as below; shop slots check.type "shop" {shop, index, item}, id type 10 =
0x50320000 + (10 << 21) + (shop << 8) + index; doors.py validator mine, the shuffle theirs.

Done so far (build engine/build-world, all uncommitted):
- tools/ap_doors.py -> docs/ap/doors.json: 2529 doors (80 stack form), 66 world entrances in
  35 key groups, 2044 arrival points.
- seed.{h,cpp}: kind key, check type shop, options world_mode / expel_teleporter /
  shuffle_shops, the doors table (id or map+offset; maps; routine; key; to).
- ap.{h,cpp}: keys (sidecar "keys"), on_script_loaded (canonical script map; a door word that
  is no transfer is ignored), door_blocked_at_trigger, on_map_transfer (locked: skipped; to:
  args rewritten), on_shop_buy / shop_slot_open, teleporter patch lines (open world), console
  `ap_give key:N`, status line.
- Hooks [world agent]: script_ext.cpp transfer case, script_triggers.cpp (trigger fire),
  map.cpp (script loaded), menu_shop.cpp (settle buy + order; slot names in colour 2).
- Unit tests ap_phase4_seed, ap_doors_and_keys, ap_shop_slots: ss_tests 78/0, ss_game_tests
  107/0.
- Game runs (scratchpad/world/ap4/runs): map 17 door 1116181 locked ("Arlia Pass needed"),
  key received, re-entered: map 18 loads; world map 2 door 68685998 (North City, save
  dex_north_city.mcd) locked then open; redirect 1116181 -> map 23; teleporter at chapter 40:
  303 K25 -> map 18 (open world) / talk only (story); 18 K26 exists only in open world, its
  trip branch -> 303 (forced with a test patch, choice1/c.patch).

- Design written: docs/ap/seed_format.md "Phase 4" (options, keys, doors, the teleporter with
  its three patch lines and why not flag 730, shop slots, the door table and validator);
  archipelago_plan.md phase 4 status; field.md hook table (doors, point of no return).
- apworld/so2/doors.py (validate_doors + CLI) and apworld/so2/test/test_doors.py (2 tests, run
  standalone `python3 apworld/so2/test/test_doors.py`, or in AP's test run). The 66 world
  entrances mapped to their own targets validate clean.
- Smoke (tools/smoke_field_events.sh engine/build-world/shattered-stars): 73/73, 11 new
  (ap_p4_door x4, ap_p4_redirect x2, ap_p4_tele_open x2, ap_p4_tele_story, ap_p4_world x2;
  the world one needs analysis/saves/dex_north_city.mcd like north_city).
- No-seed path: every hook returns at once without a seed (unit test ap_doors_and_keys ends
  with that); the transfer case keeps its fetch counts and side effects in order.
  `ref_regress.py --quick --build engine/build-world`: 18 pass, 2 regress (e10_3, f10_3: the
  same at the 03:12 checkpoint with engine/build, enemy area), 8 review (world_spell "could not
  parse compare output": the same with the 03:12 engine/build binary, which has none of this
  code, so a harness / scratch issue for the tools agent, not phase 4).

Open / next (for the next world agent): closed by the fifth world agent above (shop slots in a
real run + the smoke case; the ap_e2e.sh key/shop sessions; the APWorld's confirmation read).
Still open:
4. Optional: a disc-backed `--ap-validate` for doors (today the port checks each seed door's
   word when its map loads and ignores a bad one).
Scratch: scratchpad/world/ap4/runs (door / wdoor / redirect / ow / story seeds and logs,
choice1/c.patch forces K26's yes branch), regress/ (the quick run).

Earlier state (01:37 usage-limit stop): research only, no project file edited.
Scratch in scratchpad/world/ap4: full.json (`ap_extract --full`: 2529 transfers), maps.pkl
(every map's disassembly, cache_maps.py), dis.py / pdis.py (disassembly helpers).

Findings so far:
- World-map entrances: 66 transfers in the two chunk scripts (Expel 909: 44, Nede 1048: 22)
  to 56 entry maps. Each is a tiny routine `frame; ext_ba(60) (music fade); map_transfer_c;
  return`, registered by ext 0x76 (worldmap_trigger_add) with the routine's word index.
- Field doors: `frame; map_transfer_c; return` routines registered by thread_start
  triggers. Triggers fire on entry only (script_triggers.cpp last_trigger / active_trigger),
  so a blocked door does not refire while the leader stays inside.
- Plan for a gated door: block at trigger fire when the trigger's routine is a locked door's
  routine (nothing of it runs: no music fade); fallback at the transfer instruction itself
  (script_ext.cpp ext 0x7d / 0xa4 / 0xda) for doors reached another way. Seed door targets
  (entrance shuffle) redirect at the same instruction, so stack-argument transfers work too.
- Virtual Expel: map 303 0x4c14 (arena old man) tests flag 730 && !flag 2 → map 18; the way
  back is map 18 0x1fec → 303 under chapter >= 60 && !flag 2 && !flag 753. Flag 730 is also
  read by map 236 (Central City PA 0x200, chapter >= 58, Filia's event) and map 18 0x2738
  (chapter >= 60): so setting 730 early is not side-effect free.

## AP phase 3, port side (third world agent, 2026-10-23 ~00:50-02:40): DONE, handed back

Assignment (coordinator): character items, first-drop checks, traps (ambush / Fol loss /
status), exp / fol multipliers and encounter_rate, DeathLink edge cases, plus (added by the
coordinator) the seed's protagonist held at New Game. Everything is written up in
docs/ap/seed_format.md "Phase 3" (options, item kinds, check types, rules, the DeathLink
safe-point table, sidecar keys, console). Seed fields agreed with the APWorld agent (now
a1e6498ce791a7ac4; its predecessor ad2e106211b3b492c agreed the names: drop ids type 8, per-site
join ids, a consistent party pool on its side) and the battle agent (result.cpp hooks OK'd).
Build engine/build-world (ninja -j8); scratch scratchpad/world/ap3 (env.sh: SS_DISC1,
XDG_CONFIG_HOME, SDL drivers; runs/ holds seeds and logs; credits/run.py drives the endings
plan 0 from map 581).

Port code (nothing committed; the coordinator commits):
- seed.{h,cpp}: kinds character / trap, checks join / drop, options character_items,
  drops_as_checks, exp/fol_multiplier, encounter_rate, protagonist (any / claude / rena);
  out-of-range values fail the load; character_name / character_by_name.
- ap.{h,cpp}: on_party_add (join sites by map + `at` or the innermost call - 4; held back
  unless received; check recorded), join_waiting (received characters join in free play
  through party_add_member + clear_bond: not in PA mode flags 2/4, 8 slots incl. reserve,
  Leon only with flag 1, Dias only without), spring_trap / start_ambush (field state 0x21
  with +0x264 = -group), on_enemy_drop, scale_battle_rewards, random_encounters_off /
  scale_encounter_distance, required_protagonist, on_program (Scene: title / field / battle
  / credits), DeathLink rules (dropped on the title, in the credits, in the ending =
  final boss 539/540 won or script flag 0x2f1; cleared by game over / load / new game; in
  the field it applies in free play or after 30 frames of player control inside a scene;
  battle_frame waits in a battle with field_flags +0x7e = 0), sidecar keys characters /
  characters_waiting / first_drops / ambushes, status_text lines.
- Hooks (flagged [world agent]): script_ext.cpp ext 0xae; script_table_a.cpp A 0x3f (slot -1
  guard); script_ext.cpp script_string (32-bit index wrap: a pre-existing segfault with
  index -1, maps 71 / 277 from --map starts; menu agent told); field_program.cpp
  field_update_encounters (rate 0, distance scale); battle/result.cpp award_exp_money /
  roll_drops; boot/system.cpp run_program (ap::on_program); title/new_game.cpp
  character_screen (protagonist lock).

Verified:
- ss_tests 78/0, ss_game_tests 104/0 (SS_DISC1, scratch XDG). Unit tests added: ap_phase3_seed,
  ap_character_items, ap_drops_rewards_encounters, ap_traps, ap_deathlink_scenes.
- Smoke (tools/smoke_field_events.sh engine/build-world/shattered-stars): 62/62, of which 25
  are the new phase-3 checks (ap_p3_*, ap_hero_*; the world-map ones need
  analysis/saves/dex_north_city.mcd, as north_city does).
- No-seed Redux proof for the result.cpp hooks: victory_2_3 (scratchpad/battle/ref): RNG
  equal on all 7 samples b600-b1240 (335 at the exit), handoffs 3 / 4 byte-identical, 2 and 6
  only at the known fields (+0x14 swap counter, +0x50/+0x52/+0x1a0/+0x1a2 sound test, 6 +0x820
  item-table pointer). A seed with every option off: same log and handoffs byte for byte.
  (Transient +10 RNG diffs at b270-b400 in the fight are older than these hooks: battle
  agent's area.) Encounter rate: no seed vs 100% identical logs on world map 2.
- Game runs (scratchpad/world/ap3/runs): Ashton's choice site 71 0x7348 (check, held back /
  received: joins in free play, slot 1); traps on world map 2 (ambush group 50 stage 2, Fol
  50%, paralysis); encounter_rate 200% 10 encounters vs 5, 50% 3, 0% none; Berle 535: item 240
  withheld, check sent, exp 99000 -> 297000; protagonist lock both ways; DeathLink: world map
  (at once), camp menu (waits for it to close), ambush transition (falls in the battle), the
  ship's losable Shin fight 517 (waits; the loss sends nothing; falls 30 frames after the
  field gets the controls back), the endings and credits from map 581 (both dropped).
- tools/ap_e2e.sh: sessions 4 (character + traps from Bob, the first-drop check out, Bob's
  DeathLink in with no echo) and 5 (Alice's game over sends a DeathLink) added: 16/16.
- Dias received in Claude's game: dropped with its banner; Leon joins (slot 1).

Sent to the APWorld agent (a1e6498ce791a7ac4) after reviewing its phase-3 contents (18 join
sites, pool, traps 0x50350001-5, 89 drop types, 41 boss encounters as type 9): list Leon's
229 0x73dc in rena seeds (else the ship exception adds him free); check whether Celine's 74
join is really temporary. Both answered (resolved): its locations.choose() drops only the
OTHER protagonist's rows, so 229 0x73dc is listed in rena / any seeds; Celine 74 is temporary
(she leaves at 524 0x72bc, 126 0x6710, 462 0x701c / 0x7030; its tool
scratchpad/enemy/ap3/removes.py CHAR...). seed_format.md now names id types 8 (drop) and
9 (boss).

Commands (from the repo root):
- `source scratchpad/world/ap3/env.sh` (SS_DISC1, scratch XDG_CONFIG_HOME, SDL drivers); for
  disc-2 maps also `export SS_DISC2="${SS_DISC1//Disc 1/Disc 2}"`.
- Build `cd engine/build-world && ninja -j8`; tests `./ss_tests` (78), `./ss_game_tests` (104).
- Smoke `tools/smoke_field_events.sh engine/build-world/shattered-stars` (62; never edit it
  while it runs: bash reads it as it goes). End to end `tools/ap_e2e.sh
  engine/build-world/shattered-stars` (16; private MultiServer on 127.0.0.1:38291, stopped by
  its process group).
- Join-site run: `SS_SCRIPT_GLOBALS="a0=17,1bc=100" SS_SWEEP_FLAGS=113 SS_EVENT_SWEEP=1
  SS_EVENT_SWEEP_SKIP=2 ... --ap-seed SEED --map 71` (Ashton). Ship / losable battle:
  `SS_SCRIPT_GLOBALS="a0=29,a4=30" SS_EVENT_SWEEP=1 SS_SWEEP_PARTY=1 SS_SWEEP_PLAY=9999:T0
  --map 548` (Shin 517 at vblank ~409-2595). Endings and credits: `python3
  scratchpad/world/ap3/credits/run.py OUT ["V:cmd;..."]` (credits from vblank 9294).
- No-seed Redux proof: port run with SS_BATTLE_TICKS=1 SS_BATTLE_STRONG=1 SS_BATTLE_RNG=1
  SS_BATTLE_DUMP=... SS_BATTLE_QUIT_AT_EXIT=1 SS_BATTLE_RAM=scratchpad/battle/ref/victory_2_3/
  ram_field_exit_004046.bin --reference-dither --battle 2:3, cross every 45; then
  scratchpad/battle/rngcmp.py and a compare of the handoffs with ram_exit_01236.bin
  (3 at 0x8009a9c0, 4 at 0x8009acd0, 2 at 0x8009b360, 6 at 0x8009b610).

Open / next:
- The APWorld side (a1e6498ce791a7ac4) will emit these fields (character and trap items, join
  sites per site, drop locations, the options in seed options). When it does, add the
  phase-3 steps to its tools/ap_e2e_so2.sh too (Bob's plando into SO2 character / trap items).
- Not covered in a game run: a character's join site where the item came earlier AND the
  party is full (unit-tested); Leon / Dias refused in the wrong game (code path only).

## HANDOFF: start here (2026-10-23 about 01:40, after the second world agent's turn)

Both queue items from the coordinator are done; nothing is running (no Redux, no server).
Suites in engine/build-world with SS_DISC1 and a scratch XDG_CONFIG_HOME: ss_tests 78/0,
ss_game_tests 99/0, smoke (tools/smoke_field_events.sh engine/build-world/shattered-stars)
37/37; tools/ap_e2e.sh 7/7. Nothing committed by me; the coordinator commits hourly.

### Item 1, M7's spell part: closed

- docs/notes/spell_coverage.md reads verified for every spell and module, and its Open list is
  empty. The four party class runs were run in Redux and compared (`sweep_cmp.sh`); the new
  section "The party class runs" has the numbers:
  - iLv: exact. RNG 290/290, enemies 290/290, party 288/288.
  - iCv: enemies 280/280, party 278/278; RNG 277/280, the 3 differences inside the console's
    3-tick slow runs (Ray b910, Angel Feather b2300), the next sample agreeing.
  - iNa: enemies 255/255, party 253/253; RNG 254/255 (b850, a 4-tick run under Energy Arrow).
  - iNb: exact. RNG 280/280, 280/280, 278/278, no slow frames.
  - Cast and finish frames are identical in all four. No port difference: nothing to fix.
- gen_cov.py now lists pending runs in Open too, and writes the run table.
  results.json has the new keys (backup in results.json.bak).
- The battle agent's sweep (passes 1 and 2, docs/notes/battle_sweep.md and
  scratchpad/battle/sweep/pass2/missing.json) lists no spell among its "not ported yet" lines:
  only the victory voice, particle kind 4 and stage code.

### Item 2, AP phase 2 (port side): done

- **Gift cross-check** (seed_format.md "Gifts: the cross-check";
  scratchpad/world/ap/gifts/xcheck.py over ap_extract's disassembly, cached in maps.pkl):
  - every reward site in field.json was followed from its give to the next message window;
  - 94 item + 5 Fol gift sites and the Fol gift: the library routines open their window in
    the same vblank, so "Got X." / "Sent X to Y." replaces the text;
  - 1 inline give (map 75, the second Weighty Ring) has its window before it: banner;
  - 33 key-item flag gifts stay vanilla (agreed with the AP agent: no placeholder in story
    mode);
  - the 215 prize-table inline gives come in runs and loops, with the window before them or
    after a wait.
- **Port changes for it** (ap.cpp / ap.h):
  - all placeholders reached in one vblank become pages of that vblank's window (0x4001), or
    a banner each;
  - a placeholder reached again in the same vblank is ignored (loops);
  - Fol placeholders 0xf00000-0xffffff (seed.h `is_fol_placeholder`), because the money
    routine's push is imm24; 0x7f000000+ still reads;
  - test `ap_placeholders_same_vblank`.
- **Goal default [539, 540]** (the battle agent: two versions of the one final fight).
- **apworld/so2/patches.py** (mine; the API is in seed_format.md):
  - placeholder_for, placeholder_patches and placement_patches, plus a CLI;
  - agreed with the enemy / AP agent (ad2e106211b3b492c), which imports it from its
    apworld/so2/__init__.py;
  - field.json now carries word / expected / role per site (its regeneration). The CLI reads
    it: all 585 chest / gift / prize locations give valid lines.
- **tools/ap_e2e.sh** (new, mine): the scripted multiworld session, 7/7:
  - it starts a private MultiServer on 127.0.0.1:38291 from ap_test_server.sh's install, so
    the shared 38281 server, which other agents use, is not disturbed;
  - Alice's crate sends Bob's Map Width ("Sent Map Width to Bob." captured);
  - Bob's checks give Alice 3 items in free play (banner captured).
  - Port switches it added (flagged `[world agent]`): main.cpp `SS_AP_SERVER` / `SS_AP_SLOT`
    connect any run, headless too; runtime.cpp `SS_PACE=1` paces a headless run.
- **Still open, later phases:**
  - DeathLink edge cases as before (no DeathLink from a losable story battle);
  - first-drop checks and exp / fol multipliers (phase 3);
  - key-item flags as AP items (phase 4: the AP agent says only for an open-world mode).

### Files of mine changed this turn (none committed)

- engine/src/game/ap/{ap.cpp, ap.h, seed.cpp, seed.h}
- engine/tests/test_ap.cpp
- apworld/so2/patches.py (new)
- tools/ap_e2e.sh (new)
- docs: ap/seed_format.md, notes/spell_coverage.md (generated), handoff/world.md
- Flagged edits elsewhere: engine/src/main.cpp (SS_AP_SERVER), engine/src/runtime/runtime.cpp
  (SS_PACE), docs/HANDOFF.md (the debug-options line), engine/README.md (the AP paragraph)
- scratchpad/world/cov/{gen_cov.py, results.json}

## Earlier handoff (context rollover, 2026-10-23 about 00:30)

The coordinator's queue (2026-10-22, late) is below. Items 1 and 2 are both partly done, and
nothing runs in the background (no Redux, no queued jobs). Suites at the handoff, all with a
scratch XDG_CONFIG_HOME and SS_DISC1: ss_tests 78/0, ss_game_tests 98/0, smoke
(tools/smoke_field_events.sh engine/build-world/shattered-stars) 37/37. Nothing is committed;
the last checkpoint is 93dee59, and the coordinator commits hourly.

### Rules (unchanged; given by the coordinator)

- **Build:** in engine/build-world, `ninja -j8`.
- **Scratch:** on disk in scratchpad/world, never /tmp.
- **Runs:**
  - Headless only.
  - Every game, test and sweep runs with XDG_CONFIG_HOME pointed at a scratch dir
    (scratchpad/world/xdg; run_port.sh sets scratchpad/world/spell/xdg), plus --no-memcard
    unless a card is named.
  - Scratch memory cards only.
  - Kill processes by exact PID only (never pkill / pgrep -f with a pattern your own shell
    contains).
- **Redux:** with -cli and a scratch HOME only, at most 3 instances of mine at once (other
  agents run their own).
- **Commits and logging:**
  - Don't commit.
  - Log here and in the catalog at least every 20 minutes.
- **Other agents' files:** keep edits buildable; keep edits there minimal and flagged
  `[world agent]`.
- **Enhance policy:**
  - Don't reproduce original bugs (`// ORIGINAL BUG (original_bugs.md Sn)` with the fix below
    it) and never model slowdown.
  - Play-state rule (user): a fix must not change hit timing, the number of hits, damage,
    difficulty or battle pacing. Each is measured against Redux; if one would change, hold the
    fix and send the numbers to the user.
- **Verified** means ss_tests, ss_game_tests (with SS_DISC1) and smoke all pass, plus a Redux
  match where play is involved.

### Queue item 1: spell coverage (M7's spell part)

- **docs/notes/spell_coverage.md** is generated by `scratchpad/world/cov/gen_cov.py`. Its
  inputs, all in scratchpad/world/cov:
  - `spells.json`, from `spells_table.py`: names from the disc's #1980, and from a Redux RAM
    dump the effect kind 0x8007002f, MP, target class and data entries 0x8006d1f2 × 5 variants.
  - `class_lists.json`: the class lists 0x8006b1b8 / 1d0 / 1e4 / 1fc.
  - `enemy_spells.json`, from `enemy_spells.py`: each monster's 4 spell slots, stats record
    +0x3c.
  - The verification record: a hard-coded `ver()` list plus `results.json`.
- **Variants:** monsters 0, Rena 1, Leon 2, Celine 3, Noel 4 (`unit_caster_variant`). The same
  module code runs with different data per variant.
- **State:**
  - Every module and every monster variant is Redux-verified.
  - New since the rollover-eve: eA-eF, the wizard on encounter 340 casting one spell at a
    time.
  - Still pending: 4 party runs for the class variants and the party Shot spells not yet
    covered. They are configured in sweep_cfg.py and dry-run in the port (every cast plays to
    its end), but their Redux references are not run:

    | Run | Caster | Spells | FOR |
    |---|---|---|---|
    | iLv | Leon | Wounds, Word of Death, Ice Needle, Deep Mist v2, Haste v2, Delay v2, Growth v2, Protection v2 | 2900 |
    | iCv | Celine | Firebolt, Thunderbolt, Wind Blade, Ray v3, Starlight v3, Anti v3, Angel Feather v3 | 2800 |
    | iNa | Noel | Wind Blade, Grave, Energy Arrow v4, Mind Absorber v4, Heal, Cure Light, Fairy Light v4, Cure All v4 | 2550 |
    | iNb | Noel | Fairy Heal v4, Silence v4, Dispel v4, Curse v4, Bless v4, Neutral v4 | 2800 |

- **To run them:** `cd scratchpad/world/spell && ./start_sweep.sh iLv 2900`, then iCv 2800,
  iNa 2550 (3 at once), then iNb 2800. When each ends:
  1. `./sweep_cmp.sh RUN`.
  2. Add `"0xSP:V": ["RUN"]` entries to cov/results.json (V = the variant number, or `p` for a
     party Shot spell).
  3. `python3 gen_cov.py`.

  The doc's "Open" list must end empty.
- **Reading a result:** "verified" = cast and finish frames equal, every enemy sample exact,
  RNG exact outside the console's slow frames. Inside slow frames (3- and 4-tick runs) a few
  samples differ transiently and the next sample agrees; that is within-frame sampling.
  - Known, the battle agent's: a party sample now and then shows the hit reaction (s3) one
    frame early in the port.
  - eA-eF all read this way: eC and eE fully exact; eA, eB, eD and eF differ only inside slow
    frames (Explode, Thundercloud / Meteor Swarm, Extinction, Earthquake).
- **The battle agent's sweep** (tools/battle_sweep.py): when it lands, take any "not ported
  yet" spell lines from its ranking rather than duplicating it.
- **New drivers for enemy casts:**
  - Port: `SS_BATTLE_ENEMY_SPELLS="F:SLOT:s0,s1,s2,s3[:HP[:MP]];..."` (spells.cpp, mine).
    Each entry applies in order once battle frames >= F, no SpellCast runs, and the previous
    entry's s0 has been cast (or 1500 frames have passed).
  - Redux: `BW_ENEMY_SPELLS`, the same, at input_dispatch (redux/explore.lua).
  - sweep_cfg.py `ESPELLS` / `EFOR` / `OWN_SIDE`: own-side spells set the wizard to 5000 HP,
    and MP goes back to 200 at each switch. The wizard's AI odds are `0,255,0,0,0`.

### Queue item 2: Archipelago phase 2, the port side (game side done and tested; parts open)

- **Done:**
  - **Seed reader:** split into game/ap/seed.{h,cpp} (json.h only), at the setup agent's
    request. Setup links it; CMake lists src/game/ap/seed.cpp.
  - **Seed format additions** (docs/ap/seed_format.md updated): a location's `placeholder`,
    top-level `goal.encounters` (default [540]), `options.deathlink`, sidecar `goal`.
  - **Placeholders** (chests and gifts, any item, own or another player's):
    `ap::on_give_item` / `on_give_money`.
    - Hooked from script_table_b.cpp give_item (B 0x60) and script_ext.cpp ext 0xd4. Own items
      are given; another player's are sent. Either way the check is recorded.
    - The next message window (script_ext.cpp `message_text`, ext 0x64 family) shows "Got X." /
      "Sent X to Y." in place of the script's string. Without a window that vblank, a banner
      says it.
    - Verified by capture: map 18's crate shows "Sent Hookshot to Alice." and gives no Fol.
  - **Boss checks and the goal:** `ap::on_battle_exit(encounter, outcome)` in
    battle_program.cpp battle_exit_to_field (flagged). Boss locations by encounter; their own
    items are given in free play ("Got X").
  - **DeathLink:**
    - Out: `ap::on_game_over()` in field_program.cpp field_begin_game_over, never for a death a
      DeathLink caused.
    - In: `ap::battle_frame()` in battle_program.cpp's loop (flagged). In a fight, with no
      special move running, every standing member gets HP 0, death 1, and the battle loses as
      on the console. In field free play it is a game over at once (field_begin_game_over).
    - Console: `ap_deathlink [FROM]`.
  - **Server link:**
    - `ap::connect(std::unique_ptr<ap_net::Link>)`, `deathlink_enabled()`, `vblank()` and
      `connected()`.
    - Runtime: `net_poll` hook (runtime.h / runtime.cpp, flagged), called once per vblank.
    - kConnected with a matching seed re-sends all checks (+ goal) and marks the server's
      checked; another seed stays offline with a notice.
    - kItems are kept by server index (net_items); duplicates are dropped and items are given
      in free play from SaveState::received_index on. Local items (console, boss rewards) go
      in a separate queue that does not move the index.
    - kDeathLink sets a pending DeathLink when options.deathlink is on. kRefused shows a
      notice.
  - **Tests:** test_ap.cpp gains `ap_server_link` (a FakeLink: wrong seed, resync, items by
    index, goal, boss, DeathLink in and out) and `ap_placeholders`. Smoke gains ap_placeholder,
    ap_boss, ap_deathlink_battle and ap_deathlink_field.
- **Agreed with the setup agent** (ae6c5dd2de7b4fb9f, now handing over to a fresh setup
  agent: docs/handoff/setup.md):
  - **Ownership:** it owns engine/src/ap_net/ (link.h: Link, ConnectInfo, State, NetItem, Event;
    client.h: `Client : Link`, CMake ss_ap_net, C++17 sources) and the setup page. I own the
    game side.
  - `items_handling` 0b001, because own items are given offline.
  - Our own DeathLink bounce is dropped inside ap_net.
  - `Event::item_send` flags ItemSend lines; I only log kMessage.
  - main.cpp (theirs) builds the Client when ap_server is set, on windowed non-test runs.
    - The slot is ap_slot, else `seed()->slot_name`.
    - deathlink comes from `deathlink_enabled()`.
    - It then calls `game::ap::connect`.
    - I told them `connect` exists, so they can drop their SS_AP_CONNECT_READY guard.
  - End-to-end against a local MultiServer works: connect, a check sent, 15 items received
    and given in turn. Their files are in scratchpad/setup/e2e.
- **Open (phase 2 left):**
  1. **Gift locations:** a gift's check comes from its placeholder (the generator patches the
     gift's item push). Story / NPC gifts that give through inline give_item or add_money
     also work, since the hook sits in the op. Nothing keys gifts by offset; Location::offset
     is only for validation.
     - To do: cross-check against the enemy agent's ap_extract output (docs/ap/field.json,
       tools/ap_extract.py): each gift's call site and its routine's message (if the message
       comes much later, the banner is the fallback).
     - Not handled: the fanfare branch (value >= 10000: key items setting script flags). It
       needs the area-key design of phase 4.
  2. **A generator-side helper** (`tools/so2_mod.py` or the APWorld) that writes
     `placeholder` patches for chests and gifts. Not written. Chest example: `script 18 0x82c
     0xffffff38 0x00000400`, with the location's `"placeholder": 1024`.
  3. **DeathLink edge cases:** a received one during a menu, an event or the title waits for
     free play or a fight. A death in a losable story battle (field flag 0x7e = 0) is no game
     over, so no DeathLink is sent. That matches "on a game over"; say so in seed_format.md
     if the user asks.
  4. **The rest of phase 2:** the plan's first-drop checks (result.cpp roll_drops, the battle
     agent's) and exp / fol multipliers are phase 3. Nothing is started.
- **Files of mine in progress** (all build; none committed since 93dee59):
  - game/ap/{ap.h, ap.cpp, seed.h, seed.cpp, json.*}
  - tests/test_ap.cpp, tools/smoke_field_events.sh
  - docs/ap/seed_format.md, docs/notes/spell_coverage.md
  - game/battle/spells.cpp (SS_BATTLE_ENEMY_SPELLS)
  - field: script_table_b.cpp, script_ext.cpp, field_program.cpp, debug_console.cpp
  - flagged in others' files: battle_program.cpp, runtime.h / runtime.cpp, menu_save.cpp,
    memcard.*, title_program.cpp, main.cpp (`--ap-seed`)

### Tools and scripts (scratchpad/world)

- **spell/:** the Redux spell sweeps.
  - **Running:**
    - `sweep_cfg.py RUN [FOR]` prints the exports for a run. Run kinds: i* idle class casts,
      s* timed, e* enemy, x*/m* items, clashes and menus.
    - `start_sweep.sh RUN FOR` starts the Redux reference into ref/RUN_3 (captures in
      ref/sweep_raw/RUN) and prints its PID.
    - `run_port.sh` runs the port side. `menu_cmp.sh RUN` (MENU_D=4) is for pad runs;
      `itemC_cfg.sh` / `item_cfg.py` are for item runs.
  - **Comparing:** `sweep_cmp.sh RUN [FOR]` runs the port with the reference's slow frames
    replayed (SS_BATTLE_TICK_MAP) plus `cmp.py`. `sweep_cap.sh RUN D B...` pairs captures.
  - **Redux driver** (`redux/explore.lua`, `redux/run.sh`): BW_CAST, BW_CAST_IDLE, BW_ITEM,
    BW_ENEMY_SPELLS, BW_TICK_LOG, BW_SPELL_TRACE, BW_RNG_TRACE, BW_BATTLE_INPUT, BW_CARD_TAG.
  - **Modules:** `register.py` / `cat_mods.py` register them and add their catalog rows;
    `modules/index.txt` maps hash -> entries -> spells.
- **cov/:** spells_table.py, enemy_spells.py, gen_cov.py, results.json, map18.dis (unused).
- **ap/:** tiny.apso2 and ph.apso2 (the smoke seeds), xdg/, nomods/.
- **Port debug variables:**
  - SS_SPELL_TRACE, SS_BATTLE_CAST(_IDLE), SS_BATTLE_ITEM, SS_BATTLE_ENEMY_ODDS,
    SS_BATTLE_ENEMY_SPELLS
  - SS_BATTLE_TICK_MAP, SS_HELD_FIXES, SS_ORIGINAL_S1, SS_RNG_SITES
  - SS_DEBUG_INPUT with --debug

## Done and verified (committed)

- Phase 1-2: world map overlay 0xc1e, vehicles, bunny, courier, minimap, insert-disc
  screen (7fc05c0, f74aa77, 233756a). Notes: `docs/notes/field_overlay_c1e.md`.
- Phase 3 step 1 (catalog triage, party_swap_characters, screen clamp flag, draw_list
  bounds): 6eb6afb.
- Phase 3 step 2 (afterimages, encounter voice streaming, field_load_map /
  field_unload_map teardown, voice budget floor): c7c60b4.
- Phase 3 steps 3-4 (Voice Collection, readability renames): 590b8c3.
- Phase 4 step 1 (saves byte-compatible: field save icon, buffer leftovers, snapshot
  refresh; SS_STATE_DUMP; test_save): 6243fd7.
- Phase 4 step 2 (mods item / shop / enemy overrides, debug overlay, smoke cases): 10da278.

## Phase 5 plan (coordinator, 2026-10-22): battle spells and items

Split off from the battle agent's queue (its step 4). New files battle/spells*,
battle/spellcast*. Build `engine/build-world`, scratch `scratchpad/world/`.

1. The spell system: spell_cast 0x80062c24 and the spell routines (0x80062000-0x80064fff),
   the #2129 SpellCast API (0x80085518-0x80088054), the spell data loader (spell_data_*
   0x80048000-0x80048fff), the unit side (0x80052000-0x80053fff: unit_can_cast,
   unit_spell_mp_cost, unit_request_spell_data, unit_begin_cast, unit_cast_effects,
   unit_update_casting), shot_hit_magic 0x800547d4 (shot.cpp, flagged), the spell-clash /
   skill-notice / scan windows. Then the callers: the party cast states (battle agent's
   party_states.cpp) and the enemy spell path (enemy_try_cast / enemy_state_chant /
   enemy_state_cast / enemy_release_spell in enemy_ai.cpp); delete the enemy agent's
   stand-ins in enemy_support.{h,cpp} as the real ones land (flagged).
2. Battle items: the command ring menu and item list (input_ring_menu_open / update
   0x8004c000-0x8004dfff, input_open_member_select), item use in battle.

Verify against Redux (RNG draw counts, unit state / HP / MP, effect frames) with
scratchpad/enemy/cmp.py's per-reference alignment (CMP_D). Casters: wizard (encounter
340), Dreamshade (412: heal / time stop), Rena's heal (party setup: ask the battle agent).

## Phase 4 plan (coordinator, 2026-10-22)

1. Save / load compatible with the original memory card data, on scratch cards only:
   (a) a port save loads in PCSX-Redux with the same state, (b) a Redux save loads in the
   port with the same state, (c) the port's save block matches the original's byte for
   byte apart from random / time fields (each difference explained). Fix mismatches
   (menu_save.cpp is the menu agent's: minimal edits, flagged).
2. World edits part 2: mods data overrides (item records, shop inventories, enemy stats
   per type via a hook in the battle encounter loader), checked against the original
   value and logged; an in-window overlay with the debug console's status / output (only
   with --debug); smoke cases; engine/README.md.

(Phase 3 plan, done: triage, field TODOs, Voice Collection, readability.)

Sub-steps (reported to the coordinator): 1a spell data cache, spell_cast + the ten
built-in spells (Shot kinds 3/4/6-0xf/0x1d), unit cast side, party / enemy cast states,
shot_hit_magic, windows; 1b SpellCast object + API + kinds 1/2 + a module registry and the
modules of the test casters (wizard: Cure All 0x32, Magnum Tornado 0xb, Foehn 0xa;
Goathead 277: Cure All, Thundercloud 0x10); 1c the other ~50 spell modules; 2 items.

Battle agent's answers (2026-10-22): no character module overrides PartyUnit slots 11 /
40 / 41 (I add them to unit.h + spell_units.cpp; two lines in party_states.cpp); slot 42
spell_hook after spell_cast; party AI stays theirs. Rena setup: port
`SS_BATTLE_POKE="3:0x0=2/2,3:0x20=200/2" SS_BATTLE_RAM=scratchpad/battle/ref/victory_2_3/
ram_field_exit_004046.bin --battle 2:3` (ids 1 Claude 2 Rena 3 Celine 4 Bowman 5 Dias 6
Precis 7 Ashton 8 Leon 9 Opera 10 Ernest 11 Noel 12 Chisato); Redux (copy of
run_victory.sh) `BW_EXTRA_POKE="4047@0x8009a9c0=0x2/2;4047@0x8009a9e0=0xc8/2"` (handoff 3
at 0x8009a9c0, handoff 4 slot-0 record 0x8009acd0); her spell list bytes are in her
record (to find); D = 2 for that dump.

## State at the weekly-limit cutoff (2026-10-22, about 11:10; committed unverified in 416d7b2)

The weekly limit stopped this worker before it reported phase 5. What is in 416d7b2, none of
it compared with PCSX-Redux yet:
- Step 1a, written: `battle/spells.{h,cpp}` (the spell tables, the spell-data cache spell_data_*
  0x800482a8-0x80048c5c, spell_cast 0x80062c24), `battle/spell_shots.cpp` (the ten built-in
  spells as Shot kinds 3 / 4 / 6-0xf / 0x1d; kind 0x12 with +0x10c 1 still logs),
  `battle/spell_units.cpp` (the unit casting side 0x8005287c-0x80053054: chant, release,
  clash; EnemyUnit::state_cast / release_spell / enemy_state_chant), `battle/spell_windows.{h,cpp}`
  (spell-clash, skill-notice and scan windows), `battle/spell_fx.{h,cpp}` (drawing helpers the
  spells use).
- Step 1b, partly: `battle/spellcast.{h,cpp}` (the SpellCast object and its API; a kind with no
  handler logs "not ported yet" and ends the cast), `battle/spell_modules.{h,cpp}` (registry by
  block hash) with one module, `battle/spell_mod_foehn.cpp` (spell 0xa).
- Callers wired (flagged edits in other agents' files): `unit.h` (Unit [3] can_cast is
  unit_can_cast; PartyUnit [3] can_cast, [11] release_spell, [40] state_cast, [41]
  state_spell_release, all in spell_units.cpp), `party_states.cpp` (states 0xe / 0xf and
  command 4 -> unit_begin_cast), `enemy_ai.cpp` (enemy_state_chant, spell_data_cancel_owner
  in state 0, unit_begin_cast; enemy_try_cast / state_cast / release_spell moved to
  spell_units.cpp), `combat.cpp` (unit_release_spell_data cancels the spell data),
  `shot.cpp` / `shot.h` (the spell kinds through spell_shot_update in Shot::update's default;
  shot_hit_magic 0x800547d4 next to shot_apply_damage), `sprite.cpp` (effect_skill_popup ->
  skill_notice_show), `battle_program.cpp` (spell_data_update in the main loop,
  spell_data_clear_all at victory / the common end, spells_reset + spell_windows_reset at
  init and battle_free_all). The enemy agent's spell stand-ins and screen_fade_create are gone
  from enemy_support.{h,cpp} (it includes spells.h / spellcast.h / spell_fx.h);
  enemy_mod_bark's kind-0x36 handler and enemy_mod_indalecio's kind-0x3b handler run on the
  real SpellCast (spellcast_update dispatches them); cyril / decus / vesper read
  spellcast_slot0() (dias / zand: the coordinator's follow-ups).
- Step 2, started: `battle/command_menu.{h,cpp}`: the command ring (input_ring_menu_open /
  update 0x8004cf5c / 0x8004d178), the member select (0x8004b1ac / 0x8004b65c), escape
  (0x8004d914) and the #1984.0 list menus (spell, item, tactics) with the description balloon.
  NOT yet in CMakeLists.txt nor called from input.cpp (written, never compiled);
  item_use_battle 0x80082dec is a logging stand-in there.
- No catalog rows for phase 5 yet (the +118 rows in 416d7b2's functions.csv are other agents').
- HEAD (416d7b2) builds and passes every suite: ss_tests 78/0, ss_game_tests 74/0 with and without the disc, smoke 21/21; encounters 2 10 47 65 277 280 285 315 340 412 457 run 2000 frames headless with no crash and no unported-code line (coordinator, after the cutoff).

## In progress (since the restart, 2026-10-22 afternoon)

- Verified against Redux (scratchpad/world/spell, cmp.py at the reference's own D; all RNG
  samples and all unit samples equal): encounter 516 Shin (Foehn 0xa, module), 27 Magius
  (Firebolt x3, Starlight x3), 41 Beastmaster (Wind Blade, Starlight x2), 3000 / 2000 vsyncs.
  340 wizard and 277 Goathead match too (they never cast in 6000 vsyncs idle).
- Catalog: the port column of every phase-5 function set (scratchpad/world/spell/cat_port.py),
  rows for the Foehn / Starlight modules (module `battle_spell_<name>`, README updated;
  scratchpad/world/spell/catalog/*.csv + cat_mods.py).
- New since 416d7b2 (on disk): `battle/spell_mod_starlight.cpp` (spell 0x12, af4ef66c53;
  in a28ca95), kind 0x31 vortex + effect_spawn_t31_at (spell_shots.cpp / spell_fx.{h,cpp}),
  spellcast.h `aux_point[4]` (+0x124), spell_modules.h helpers (spellcast_leads_camera,
  spellcast_model), `SS_SPELL_TRACE=1` (a line per spell_cast) and
  `SS_BATTLE_CAST="FRAMES:SLOT:SPELL:p0|e0;..."` (a forced party cast, spells.cpp); the Redux
  driver copy `scratchpad/world/spell/redux/explore.lua` has the same `BW_CAST`, and
  run.sh takes `BW_EXTRA_POKE`.
- 1b modules written: magnum_tornado (0xb, Redux 197 exact), cure_all (0x32), thundercloud
  (0x10) (+ effect_spawn_streak_at in spell_fx); Foehn renamed (was "Feehn"); original_bugs.md
  S1 (Foehn's effects on the wrong side) fixed; catalog rows for all five modules.
- Step 2, item use: VERIFIED (18:30). `battle/items.{h,cpp}`: item_use_battle 0x80082dec (all
  effect kinds), its helpers, Shot kinds 0x2e / 0x2f / 0x30, shot_spawn_sparkle_on_target.
  Redux item tests on encounter 340 (`scratchpad/world/spell/item_cfg.py`: 16 items poked
  into the inventory, forced uses): `ref/it340_3` (7 items: HP heal, kind 0x40, status 2:
  0x40000000, petrify all / one (the battle ends: the wizard turned to stone, as on the
  console), stun, MP drain) and `ref/iu340_3` (11: drain, scan, rolling boulder, 3 balls,
  bouncing balls, bombs x3 kinds, stun, heal+curse, kind 0x47): every RNG sample and every
  unit sample exact (one party sample at it340 b1290 differs: a hit reaction one frame early in
  the over state, the wizard's melee: battle agent's area); captures match (scan window, the
  boulder, the bomb's dark sphere near the camera). Suites green: ss_tests 78/0,
  ss_game_tests 74/0 (SS_DISC1), smoke 21/21.
  - original_bugs.md S2 (kind 0x5c's blasts use the enemy's y as z) fixed; S3 (a monster's
    counter-cast after an item hit has no target; the Shot spells aim at "address 0") fixed.
  - spellcast_retarget now reads a null target the way the console does (kernel bytes at
    0x123 / 0x125: party slot 2), so such a SpellCast spell goes to the first standing member
    from slot 3 on (it was dropped: found by iu340, the wizard's Foehn after the bombs).
  - Flagged edits: unit.h / unit.cpp unit_check_move takes `const BObj*`; title/new_game.cpp
    scale_colour keeps IR0's low 16 bits; input.cpp input_update calls
    `debug_forced_items()`; spell_fx.h declares voice_thank_healer.
  - Test drivers: `SS_BATTLE_ITEM="FRAMES:SLOT:INDEX:TARGET;..."` / Redux `BW_ITEM`;
    `BW_RNG_TRACE=LO:HI` logs every RNG call's site in Redux; run.sh `BW_CARD_TAG`.
- 1c: ALL 55 modules registered and building (18:30): groups A (13), B (14), C (16) from
  the helpers + my 5 + the enemy agent's 7. `scratchpad/world/spell/register.py NAME...`
  registers (kModules key, header, CMake); `cat_mods.py NAME...` adds the catalog rows (all
  added). Not yet verified against Redux.
  - Missing: model_shatter 0x8005f30c + the PolyShard pool (0x800600a0-0x800606f8): Southern
    Cross, Delay and Haste have empty stand-ins (RNG differs from the shatter on). A helper
    (started 18:32) ports it into new `battle/shatter.{h,cpp}`; then switch the three modules.
  - Helpers' ORIGINAL BUG? notes to decide after verification (listed in their reports;
    examples: Black Saber / Sonic Saber 4-entry hit lists overflowing, Gremlin Lair's waits
    without timer ticks, Press / Gravity Press stars at 0/120/120/240, Deep Mist without the
    pause check, Lunar Light's second spark ring, Raise Dead's funnel y, Silence / Blood
    Sucker / Curse heights, Delay / Haste dz bounds, Shadow Flare's shimmer phase).
  - Port gaps from the helpers: Earthquake / Explode set a stand-in of g_stage_height_fn
    0x8006fe04 (unit_draw_model's hook, TODO in unit.cpp / stage.cpp: units don't ride the
    waves yet); Divine Comedy reads Indalecio's +0x2f8 through `caster->flying` (needs an
    accessor); Raise Dead's music save / restore mapping to check.
  - Shared helpers: all moved (item_fx_sparkle, draw_disc, draw_wave_grid, voice_release,
    model_find_vertices_near / sv_distance_xz, camera_orbit_unit on any BObj); left local:
    Earthquake's obj_set_matrix_parent / child and Reflection's line_trail (one user each).
- Rena test redone with the forced casts applied where the port applies them (both at
  input_dispatch now; the old per-vsync Redux poke landed a frame later than the port's,
  which was the whole "+4 RNG at Cure Light" of r340): `ref/rn340_3` vs `prn340`: all 240
  RNG samples and all enemy samples exact over 2400 frames (Heal, Cure Light, Antidote,
  Grave, Word of Death, the wizard's Foehn and Magnum Tornado); 3 party samples show the
  hit reaction a frame early in the port (battle agent's area, as in it340).
- Module sweeps (Redux, encounter 340, max 3 instances): the caster is the class's
  character (Rena 2 / Leon 8 / Celine 3 / Noel 11, the party loader sets the class from it)
  with every list spell learned (handoff 4 record +0x3c..), HP 30000, MP 999; the wizard made
  passive (AI odds 0, SS_BATTLE_ENEMY_ODDS / BW poke during the intro). `sweep_cfg.py RUN
  [FOR]`; runs i9a i9b i10a i10b i11a i11b i12 cast the class's spells one after another
  (SS_BATTLE_CAST_IDLE / BW_CAST_IDLE: each waits until the caster is idle, no command
  pending and no SpellCast runs); `start_sweep.sh RUN FOR` starts one (captures kept in
  `ref/sweep_raw/RUN`). Port side: `(source sweep_RUN.sh; NO_MASH=1 ./run_port.sh 340 3
  ref/i340_3 pRUN FOR)`, then cmp.py. All 7 idle runs play every cast to its end in the port.
  - `ref/s9a_3` (fixed-time casts): exact through Cure All, Fairy Light, Dispel, Silence,
    Deep Mist, Energy Net (b3310); Growth then differs: the console ran slow (frame ticks 3
    under Growth's load; the port never slows down).
  - So the Redux driver now logs the slow frames (`BW_TICK_LOG=1`: "[bw] tick F T" at
    input_dispatch) and the port can replay them (`SS_BATTLE_TICK_MAP="F:T,..."`, F the
    battle frame counter before the frame: battle_debug.cpp, flagged); and the spell casts /
    finishes (`BW_SPELL_TRACE=1`, as SS_SPELL_TRACE).
  - VERIFIED (19:03; every RNG / unit sample exact or within a frame, cast and finish frames
    identical, captures alike): i10a Shadow Bolt, Gremlin Lair, Deep Freeze, Noah, Black
    Saber, Acid Rain; i11a Explode, Thundercloud, Energy Arrow, Lunar Light, Forget; i12
    Tetanus Wind, Sonic Saber, Magnum Tornado, Foehn, Earth Grave, Earthquake, Blood Sucker
    (i12 with Foehn's S1 fix reverted for the run: the fix moves the leaf sprites' draws; the
    rest then matches). `sweep_cmp.sh RUN` compares, `sweep_cap.sh RUN D B...` pairs captures
    (cap_RUN/pair_bN.png). Gremlin Lair's imps show 4 trail copies in the port and 2 on the
    console: the battle agent's afterimage fix B2, not a module difference.
  - VERIFIED too (19:08): i9a Cure All, Fairy Light, Dispel, Silence, Deep Mist, Energy Net,
    Growth, Protection, Anti; i9b Angel Feather, Press, Tractor Beam, Star Flare, Fairy Heal,
    Haste, Delay (model_shatter included), Gravity Press, Light Cross; i10b Daemon's Gate,
    Starlight, Extinction, Shadow Flare, Dark Circle. (Angel Feather's angel shows more
    trail copies in the port: the afterimage fix B2 again.)
  - VERIFIED (19:16): i11b Neutral, Bless, Eruption, Thunderstorm, Southern Cross, Meteor
    Swarm, Reflection, Curse, Mind Absorber (695 samples exact).
  - VERIFIED (19:30): x9 (items 3 / 4 make the user cast The Scream 0x41 / Judgement Day
    0x42; then Ray 0x1a, the built-in SpellCast kind 1) and x9b (Celine / Leon KO'd from
    the start: Raise Dead revives p1, the revive item 0xf3 p2; three members, 771 party
    samples exact).
  - VERIFIED (19:40): x11 (Celine and Leon casting together: three spell clashes, slot-1
    SpellCasts, combos 1 / 2 / 4; 1195 party samples exact). Fixed on the way: the clash
    banner's label read the table at 0x8007001c as bytes; it is a word table (combo 1 0x29,
    2 0x2b, 3 0x28, 4 0x2b; entry 0 is g_shatter_ot_bias): captures now match.
  - VERIFIED (19:40): m9 (Rena through the menus with the pad: the ring, the magic list and
    a cast, the item list and an item on a member, the tactics page; `menu_cmp.sh m9` with
    MENU_D=4: all RNG / unit samples exact; ring / list / member-select captures alike).
    It showed S20 (a whole-party spell from the menu with no current target does nothing
    after paying the MP): fixed after the faithful match.
  - Not covered by a sweep: Divine Comedy (Indalecio),
    spell 0x40 (a party move, variant 0 only), Mind Blast / Spicule / Wind of Destruction
    (the enemy agent's bosses).
  - original_bugs.md S4-S18 decided after the verification: fixed S4 / S5 (blade hit lists,
    return speed), S6 (Earthquake's third centre), S8 (Lunar Light's ring), S9 (Shadow
    Flare's phase), S11 (Deep Mist's pause), S12 (Explode's combo side), S13 (shatter's
    shard kinds / colours / offsets); "decide" (the user decides: intent unclear, or a fix
    changes the pacing / look; faithful code kept meanwhile): S7, S10, S14-S19. The code
    marks them "ORIGINAL BUG (... Sn)" / "Original quirk (... Sn, to decide)".
- model_shatter + PolyShard ported by the helper (`battle/shatter.{h,cpp}`, in CMake;
  shatter_reset from spells_reset); Southern Cross / Delay / Haste use it. The task list is
  now 320 entries (battle.h, flagged: g_tasks is 320 on the console; a shatter adds up to
  192 tasks).
- Shared helpers moved: item_fx_sparkle (items.h, 6 local copies gone), draw_disc /
  draw_wave_grid (spell_fx, 5 + the enemy agent's star_flare copy gone: flagged),
  g_stage_height_fn 0x8006fe04 (spell_fx; unit_draw_model now lifts grounded units by it:
  unit.cpp, flagged), enemy_indalecio_transformed (enemy_mod_indalecio.cpp, flagged) for
  Divine Comedy.
- Port debug: `SS_RNG_SITES=LO:HI` prints the call site of RNG draws LO..HI (rng.cpp;
  run under `setarch x86_64 -R`, PORT_WRAP in run_port.sh, then addr2line); Redux
  `BW_RNG_TRACE=LO:HI` the same on the console.
- Findings:
  - Spell data load timing: the port's data is ready one frame after the request; the
    console's CD read can take longer, so a monster whose chant is short releases its spell a
    frame or two later on the console (m111 Goathead's Thundercloud: ref b602, port 600; then
    everything shifts). Not a port bug (no slowdown modelling); verify modules with casts whose
    data loads in time (the party sweeps).
  - Rena test r340: resolved (test-driver timing, see above).
  - Never `pgrep -f` / kill by a pattern that the current shell's own command line contains
    (it matched the shell itself once); use the PIDs a listing printed.
  - Redux: `pgrep -f` waits on a pattern also match their own command line (the r340d queue
    never started); use a pid file or a pattern the waiter does not contain.

## Since the resume (22:15)

- W1 fixed (field/post_battle.cpp: the report window opens 0xd lines high; checked with a
  port capture: `--debug --map 18`, SS_DEBUG_INPUT "300:item 0x1eb;360:battle 1",
  `--battle-end 400:0`: Claude's auto-equip report with the MAG row).
- User decisions on the spell bugs (original_bugs.md, rule "fixes never change the play
  state"): fixed S10, S16 (visual-only RNG: spell_fx visual_rng_*), S18, each re-verified
  (i9b / x9b sample for sample); kept S7 / S14 / S15. Held, measured (numbers in the rows):
  S17, S19. Held under the rule too (restored to the original, fix behind the switch): S1,
  S5, S11, S12, S20; S3 / S4 "avoided" (crash safety). The switch: SS_HELD_FIXES="S17,..."
  (spell_fx held_fix_enabled).
- run_port.sh now sets XDG_CONFIG_HOME to scratchpad/world/spell/xdg (my port runs had
  written the user's real config: the disc2= line, and memcard1.mcd probably from ss_tests).
  Run everything with a scratch XDG_CONFIG_HOME.

- S1 fixed visual-only (default; RNG equal on k516 / i12), S3 fixed play-identical (the
  kernel's address-0 bytes as unit_null_target; iC340: RNG 220/220, units exact), S20 fixed
  by the user's decision (menu Fairy Light with no target heals 20000 -> 22200, MP 999 ->
  959 once; with a target present m9b equals Redux: RNG 240/240, units exact, cast 1612 ->
  end 2032 on both). Rows updated in original_bugs.md. Still held: S5, S11, S12, S17, S19.
- Archipelago Phase 1, port side (done 23:55; ss_tests 78/0, ss_game_tests 91/0 with SS_DISC1,
  smoke 29/29 with the 8 new ap_* checks):
  - engine/src/game/ap/: json.{h,cpp} (reader / writer, 64-bit ids, "0x" strings),
    ap.{h,cpp} (seed loader, location index by id and by chest flag, check API, receive
    queue given in free play with the notice banner, sidecar <card>.slotNN.ap.json).
  - Hooks (flagged in others' files): field_program.cpp field_update (free_play_update),
    script_vm.cpp flag_set (a global flag turning on -> on_flag_set), menu_save.cpp
    ProgressDialog::frame + load_save_direct (on_saved / on_loaded), memcard.{h,cpp}
    memcard_image(slot), title_program.cpp start_new_game (on_new_game), main.cpp
    `--ap-seed FILE` / config ap_seed (windowed runs only), field/mods.cpp (seed lines first).
  - Debug console: ap_status, ap_check ID, ap_give ITEM [FROM].
  - Tests: engine/tests/test_ap.cpp (5 cases: JSON, seed, seed errors, checks / queue,
    sidecar files and the save / load path). Smoke by hand (scratchpad/world/ap): the
    one-chest seed on map 18 (crate 0x962 -> 5000 Fol by the seed's patch line), the chest
    check recorded by its opened flag, ap_give -> "Received Revival Card from Alice" banner
    (captured), SS_LOAD_SAVE with a sidecar -> "[ap] loaded ... 1 checks, received index 3",
    the card byte-identical.
  - The banner needs a timed wait (code 0x4006) at the end of its text, or it slides out as
    soon as the text is shown; placed like the map-name notice (script ext 0xb9).

## Next steps

Both queue items are done (see "HANDOFF: start here"). Waiting for the coordinator's next
queue. Candidates: Divine Comedy (enemy agent), spell 0x40 (effects agent), and AP phase 3
(first-drop checks, exp / Fol multipliers).

## Findings not yet in the catalog

- Spell data entries 0x8e1-0x981: command lists (5 sound, 6 code, 3 texture, 4 model, 7/9
  part, 8 raw); 55 distinct code modules (block k linked for slot k: 0x8008cc00 /
  0x8008e600); `scratchpad/world/spell/modules/index.txt` maps module hash -> entries ->
  spells. Spell names: #1980 string 0x3e7 + spell, each byte + 1 (0x0a Foehn, 0x0b Magnum
  Tornado, 0x10 Thundercloud, 0x32 Cure All ...).
- Analysis program: `battle_s2.elf` in analysis/ghidra_world (the #2129 image with #1984.0
  / .1 patched in, the SpellCast code kept; PSX language: the default MIPS64 R6 pick breaks
  `jr ra`), global labels applied (`scratchpad/world/ApplyLabels.java`).
