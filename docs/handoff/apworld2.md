# Handoff: apworld2 (so2.apworld 0.2.0)

Agent "apworld2", started 2026-10-25 by the main session (plan item **apworld2** in
docs/handoff/apv2.md, whose "The user's decisions" are binding). The first agent stopped at
~490k tokens (roll-over) on 2026-10-25; a fresh agent continues from this file. (The previous
version of this log: scratchpad/apworld2/apworld2_log_prev.md.)

**Session 3** (2026-10-25, successor agent): batch rerun, AP general tests, docs/ap/apworld.md,
the player guide. Progress notes under "Session 3" below.

**Session 4** ("gifts" agent, 2026-10-25, the hub plan's docs/handoff/hub.md): gift sites never
hold progression, in either world mode. Progress notes under "Session 4" below.

## Rules given

- Edit only apworld/, tools/ap_world_tables.py (then regenerate data.py), docs/ap/apworld.md,
  docs/ap/seed_format.md ("v2 fields" section only; agent "ships" edits Phase 4 and other
  parts: re-read before editing, it changes under you), the player guide
  (apworld/so2/docs/en_Star Ocean The Second Story.md), and this log. Never engine/. Don't
  commit.
- No game runs needed; if ever, headless only under tools/memcap.py, never a GUI or a
  config-saving flag.
- Scratch in scratchpad/apworld2/ (not /tmp). Cap command output; never print big JSON/logs.
- On a permission refusal: stop and report the exact command.
- Roll over at ~400k tokens (log up to date, end the turn).
- Don't edit apworld code while a seed batch is running (each Generate process imports the
  world at start; mid-edit runs mix versions).

## Status

| Item | State |
|---|---|
| 1 seed contract | **done**: docs/ap/seed_format.md "## v2 fields (so2.apworld 0.2.0, 2026-10-25)" (version rule, options, goals, party v2, craftsanity + no-fail, killer moves, drops, example) |
| 2 drop progression | **done**: `drop_progression` allow/force/never; rules.py `drop_chance`, `drop_rule`, `place_row`; force = `SO2World.fill_hook`; generator adds drop rows' `odds` / `places` |
| 3 area keys in drops | **done**: `area_keys_in_drops` (DefaultOnToggle; item rule on drop locations: no Star Ocean area key; either world mode since the batch fix) |
| 4 craftsanity / no-fail | **done**: apworld/so2/crafts.py (529; 553 with Publishing since session 3), locations.CRAFT_ROWS / choose_crafts, `craftsanity`, `no_fail_crafting` |
| 5 party v2 | **done**: world.plan_party / choose_start / choose_open_party (Party NamedTuple), `starting_character` (new default `hero`), `exclude_characters`, items Claude/Rena/Noel, 3 open-only join rows |
| 6 goals | **done**: `goal` indalecio / indalecio_limiter_off / indalecio_limiter_off_and_iseria_queen (alias final_boss), events, rules.goal_rules, seed goal object + gate door |
| 7 killer moves | **done**: apworld/so2/moves.py (69 moves, 133 locations), `killer_move_checks`, `killer_move_progression` never/hero/any, rules.killer_rule; contract section written |
| 8 version + docs | **done** (session 3): world_version 0.2.0; docs/ap/apworld.md; the player guide; AP general tests; the full batch's check; Publishing's books in crafts.py (553 crafts) |

## Next steps (for the successor) — all done in session 3 (see "Session 3" below); kept for the record

1. **Full batch: rerun it.** The run of 2026-10-25 (seeds 1-96, every option random incl. the
   v2 ones; players in scratchpad/apworld2/batch/players) generated 96/96 (exit 0, 2 .apso2
   each). check.py found 18 files with errors: 11 were the checker's own "teleporter off"
   check not knowing drops / crafts / killer moves (fixed in check.py), 7 were real: another
   player's area key in a drop of a **story-mode** world with `area_keys_in_drops` off (the
   item rule was added only in open world). Fixed in rules.py (the rule now applies in either
   world mode; options.py text updated); world tests still 235 passed. Rerun the batch to
   confirm 0 errors: `cd scratchpad/apworld2/batch && rm -rf full full.txt full.finished && ./run_batch.sh players full $(seq 1 96)` (~5 min), then
   `cd scratchpad/ap/Archipelago && SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python ../../apworld2/batch/check.py ../../apworld2/batch/full 2>&1 | grep -v pkg_resources | tail -5`
   and `awk '{print $4}' ../../apworld2/batch/full.txt | sort | uniq -c` (exit codes).
   (The run_batch.sh used here doesn't run check.py itself or write full.finished; add
   `; touch full.finished` when backgrounding it.)
2. **AP general tests**: `cd scratchpad/ap/Archipelago && SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 test/general`
   (0.1.1: 379 passed). Not run yet for 0.2.0.
3. **docs/ap/apworld.md** (re-read first; others edit it): Locations table (+ crafts type 11:
   529; killer moves type 12: 133; join sites 18 + 3 open-only), Items (Claude / Rena / Noel
   character items; characters progression when killer_move_progression any needs them; craft
   / killer pool filler), Logic (drops: places, Luck floor, force via fill_hook, keys in drops;
   party v2; crafts; killer moves; goals: events and the gate), Options table (goal's three
   values, drop_progression, area_keys_in_drops, craftsanity, no_fail_crafting,
   killer_move_checks, killer_move_progression, starting_character, exclude_characters),
   Files (crafts.py, moves.py, test_v2.py; make_crafts.py / make_moves.py in
   scratchpad/apworld2), Changes "0.2.0" (all of the above + the decisions below), Testing
   results (numbers below + the batch + general tests).
4. **Player guide** `apworld/so2/docs/en_Star Ocean The Second Story.md`: the goal section
   (three goals; Limiter Off needs Filia in Central City City Hall after the Final Save Point;
   Iseria Queen: the cave cleared, G-Celesta's Silver Trumpet, "The Evil Melody", a member at
   Musical 10 with Pitch and Sense of Rhythm, the trumpet not sellable until composed), items
   (Claude/Rena/Noel items in open world), locations table (+ crafts, killer moves), "Who can
   join" (open world rules), options rows for all new options (fix `starting_character`:
   default `hero`), drops.
5. No YAML template exists in the repo (AP generates one); nothing to do there.
6. Optional: Publishing (super specialty 5, a book per party member, tables 0x80083D30 /
   0x80083D4C in overlay 0xbc2) isn't in crafts.py; adding it needs the disc (tools/so2_data
   Disc + so2_archive, like the research's decoder). The contract already lists Publishing
   among the specialties whose produce() checks.
7. Final report to the coordinator (built per item, contract summary, test/batch results,
   decisions, what's left, this log's path).

## What was built (files)

- `tools/ap_world_tables.py`: drop rows get `odds` (both slots) and `places` (`drop_places`,
  `map_proofs`, `unconditional`: maps with no encounter condition merged by area and proofs,
  each with region, group, group_near, late / tele / voyage games, late / tele route keys;
  world maps as `world_map` places); `OPEN_JOIN_SITES` adds join rows Rena 9 0x6b94 (claude),
  Claude 12 0x6e08 (rena), Noel 277 0x6fe0 with `open_only: True`. data.py regenerated: only
  the 89 drop rows and the 3 new join rows differ (the generator reproduced the old data.py
  byte for byte first).
- `apworld/so2/options.py`: CHARACTER_NAMES; Goal (3 values + alias_final_boss);
  StartingCharacter (hero=13 default, any=0, claude..chisato=1..12); ExcludeCharacters
  (OptionSet); DropProgression; AreaKeysInDrops; Craftsanity; NoFailCrafting;
  KillerMoveChecks; KillerMoveProgression; option groups.
- `apworld/so2/items.py`: ALL_CHARACTER_ITEMS (CHARACTER_ITEMS kept unchanged: story-mode sort
  order), CHARACTER_NAMES, HEALERS, EVENT_LIMITER_OFF, EVENT_ISERIA_QUEEN, pool_filler().
- `apworld/so2/locations.py`: CRAFT_ROWS (type "craft", id BASE + (11<<21) + item, name
  "Specialty: Item", groups "Crafts" + per specialty), KILLER_ROWS (type "killer_move", id
  BASE + (12<<21) + (pid<<14) + (move<<10) + level, name "Member: Move level N", groups
  "Killer Moves", "Killer Moves: Member"), ALL_ROWS; choose(options, party) (join rows per
  party rules), choose_crafts, fighter_status, choose_killer_moves; seed_check for both.
- `apworld/so2/crafts.py` (generated by scratchpad/apworld2/make_crafts.py), `moves.py`
  (generated by scratchpad/apworld2/make_moves.py from scratchpad/apworld2/moves_head.py +
  scratchpad/killer_prof/move_levels.csv; move_levels()).
- `apworld/so2/world.py`: Party, plan_party, story_members, choose_start, choose_open_party,
  choose_party (story rules, unchanged RNG), progression_characters + create_item, fill_hook
  (forced drops), goal_encounters, seed_goal, goal_doors, seed_version, new slot data keys
  (goal, drop_progression, area_keys_in_drops, craftsanity, no_fail_crafting,
  killer_move_checks, killer_move_progression, starting_character, exclude_characters,
  party_rules, apworld_version), seed version / goal / doors.
- `apworld/so2/regions.py`: LIMITER_OFF_EVENT ("Limiter Off (Filia, Central City)", Energy
  Nede), ISERIA_QUEEN_EVENT ("Iseria Queen (Cave of Trials)", Cave of Trials).
- `apworld/so2/rules.py`: proven_at_gate for places; drop section; killer_rule; goal_rules;
  set_rules branches for drops / crafts / killer moves.
- `apworld/so2/test/test_world.py` adjusted (check_seed: version, goal, craft checks; drop
  tests follow drop_rule); new `test/test_v2.py` (Drop*, Craft*, Party*, Start*, Goal*,
  Killer* classes).
- `apworld/so2/archipelago.json`: world_version 0.2.0.

## Test and batch results

- World tests: **235 passed, 19700 subtests** (2026-10-25, after killer moves).
- Story-mode identity: the 16 all-story seeds of the 0.1.1 batch (scratchpad/elgate/batch/gate2,
  seeds 7 10 14 15 39 45 51 60 65 68 69 71 77 89 94 96), regenerated with renamed YAML keys +
  `drop_progression: never` (scratchpad/apworld2/batch/base; compare.py): all 32 .apso2
  identical except the `options` object (renamed keys, new keys, no value changed). With the
  default `allow` (scratchpad/apworld2/batch/allow): 10 identical but options, 22 differ in
  `locations` = the seeds where a player has first_drop on (drops may now hold progression).
  (Byte-identical files are impossible since the rename: `options` keys changed.)
- Solo open world, drop_progression force (scratchpad/apworld2/gen): 5/5 generate; 17-21 keys
  in the 22 forced drops.
- Batch tooling: scratchpad/apworld2/batch/run_batch.sh PLAYERS OUT SEEDS..., check.py (v2
  checks; run with the AP venv from scratchpad/ap/Archipelago), compare.py NEW BASE;
  scratchpad/apworld2/drops.py (drop classes per mode, `-v` per drop).

## The specs received (binding)

**Goals** (the user, 2026-10-25, via the coordinator; goals.md section 3): Limiter Off keeps
Filia's scene: the port presets flags 445 / 446 (Clik meeting) at New Game and after every load
via seed `goal.flags`, and gates door 43978493 (map 671 -> 684, routine 0x3bec) until flag 556;
the APWorld adds a "Limiter Off" event (Final Save Point, + Central City Pass in open world)
that Indalecio needs. In Iseria Queen goal seeds the Silver Trumpet (item 48) can't be sold
until The Evil Melody (song 7, flag 53) is composed (port side). Seed format: `goal.encounters`
stays any-of; `goal.all` all-of (`{all: [561, 540]}`), wins in sidecar `goal_won`; door
conditions `flags` / `won` with `banners`, applying in story mode too; `goal.flags`; version 2
when any is used. "Iseria Queen" event in the Cave of Trials region (Final Save Point, + Cave of
Trials and Fun City Passes in open world); don't force cave_of_trials on; keep `final_boss` as
an alias of `indalecio`. (All implemented as specified.)

**Killer moves** (the user, 2026-10-25): variant "every proficiency level": one location per
level of each killer move (scratchpad/killer_prof/move_levels.csv; level = min(uses / div,
cap), uses cap 999; docs/handoff/killer_prof.md). The 69 learn-table moves; the 8 without a
learn row only if shown how they're learned (left out: not shown). Fine-step moves get at most
3 milestones at use counts comparable to tiered moves; Dead Triangle none; ~130 locations.
Toggle `killer_move_checks` (default off). Port: records a level location when the move's
count (record + 0x8a + 2 * move) reaches it, may test after each battle. Progression option:
`never` (default), `hero` (only the story route's hero's moves; Rena has none), `any` (only
where the logic guarantees the fighter: the hero, or a member who joins by a character item,
requiring the item; story-only members with character_items off never). Access: fighter + the
level to learn the move (conservative). (All implemented.)

## Decisions (and why)

- **Seed version 2 only when needed:** a 0.2.0 seed with no v2 port feature stays version 1
  (the 0.02.00 port plays it); one using goal gates/flags/all, party_rules open, a starting
  character, craft or killer_move locations, or no_fail_crafting is version 2, which the old
  port refuses instead of silently ignoring (an old port would end an AND-goal seed at 561).
- **starting_character default `hero`** (new value: nobody extra, the hero leads). "any" means
  a random member (the user's decision) but as a default it would make every open-world seed
  version 2 and change the game for players who didn't ask. Numbers kept (any 0..chisato 12,
  hero 13). Story mode ignores it (warning). A named member wins over exclude_characters
  (warning); the hero of a one-hero route = nobody extra; under story_route any, Claude/Rena as
  start is written (joins at the start in the other game, no extra slot).
- **Party v2 (open world + character_items, `party_rules: "open"`):** story members = the hero,
  both heroes under story_route any (the player picks at New Game, so neither can be an item);
  pool = every other member not excluded and not the start, as many as fit 8 with the heroes
  and the start (random sample when more); healer guarantee: if no Rena/Noel among heroes,
  start and pool, an allowed healer replaces a random pool member (the pool is full then);
  both excluded -> no guarantee (honours the choice); the hero can't be excluded (warning).
  Members not in the pool never join. Join locations: every non-story member's sites incl. the
  new Rena/Claude/Noel rows; all EXCLUDED. Open world without character_items keeps the
  story's rules (`party_rules: "story"`), plus the starting member.
- **Drops, Luck floor:** `drop_chance` = roll_drops over both slots at Luck 0 (Luck only adds).
  Never progression under 1/64 per kill (21 types: 11 that can't drop without Luck: Magius,
  Ericodus, Shout, Starguarder, the odds-5 slimes...; Rikiha/Burst 1/256; the odds-10 types
  3/256; Guarder just under). Force threshold 20/256 (~8 %, ~1 in 13 kills): 25 types, 22 pass
  the safety rule.
- **Drops, "re-encounterable":** only maps where random encounters meet the type with no
  condition on the site (210 of 306 sites), judged like chests: story mode = the late proof
  reached the map (or a world map), with expel_progression off outside Expel; open world =
  each place as a chest on its maps (open_world_progression / open_world_access: early with
  keys, teleporter, late; nothing that closes before the El voyage), world maps only after the
  Final Save Point (cells not mapped to islands). The drop's class = its best place; its
  open-world access = only the modes of places that allow progression (never through a place
  that can close). 62 of 89 may hold progression in story mode; 60 + 2 Expel-keys-only in open
  world with the teleporter.
- **Force is not AP priority:** AP 0.6.8's priority fill raises FillError when a priority
  location can't take any remaining progression (5/5 solo open-world seeds failed). Instead
  `fill_hook` runs fill_restrictive(allow_partial=True) on the forced drops first with any
  player's non-deprioritized progression; never errors.
- **area_keys_in_drops off:** item rule on this world's drop locations (either world mode): no area key of any Star
  Ocean world.
- **Crafts:** a location per craft result item (529; all verified game items; none found
  unmakeable). Makers from the disc decode: party_selected_char() is the 1-based party id, so
  "character N" = party id N: Customize lists = Claude..Ashton, Machinery rare = Precis (6) /
  Opera (9), Authoring rare = Leon (8; the research guessed Chisato), Art portraits A-L = one
  member each (a random active member's), Cooking's Grain teas = not Claude/Rena/Precis/Leon
  (they cook from a special list). Member-locked: left out if no maker can ever join, EXCLUDED
  unless a maker is guaranteed (story rules: Claude, Rena, Noel + start; open rules: heroes +
  start). Never progression (item rule), always reachable (no access rule). Pool item: the
  crafted item when filler, else Blueberry (equipment stays out of the pool). Check =
  common_kind of the produced item (rare kinds count as their base); Identify / Survival /
  Reproduction don't count. Metalwork's per-member equip filter not modelled; Publishing not
  covered.
- **Killer moves:** 69 learn-table moves, 133 locations (112 tiered levels of 61 moves + 3
  milestones at 100/200/300 uses for the 7 fine-step moves: Poison Pills {2,4,5}, Pillory
  {6,12,18}, Secret Medicine {100,200,300}, Whirlwind Fist {4,8,12}, Photon Prison {10,20,30},
  Cold Wind {2,4,5}, Gravity Shell {1,2,3}). Check by raw use count (`uses` = level x armed
  divisor), so bare-handed tables don't matter. Access: learn level <= 30 -> the disc switch
  event, else the Final Save Point (conservative). Fighter status: sure (guaranteed) / item
  (pool) / maybe (story recruit by choice -> EXCLUDED) / impossible (left out). encounter_rate
  0 -> EXCLUDED, never progression. `any` mode: item fighters' levels need the character item,
  which then becomes progression (`world.progression_characters`; CollectionState only counts
  progression items); never/hero don't require the item (no progression there; full
  accessibility reaches every item anyway). `hero` = story members' moves (Claude in Claude's
  game and under story_route any).
- **Goals:** goal_encounters [539, 540] / [540] / [561, 540]; seed goal object per the spec
  (flags only in `goal.flags`, not `start.flags`, so story seeds keep `start.flags: []`); gate
  door `{id 43978493, routine 0x3bec, flags [556], (won [561]), banners [...], name}` in both
  world modes; banners "Speak with Filia in Central City first" (since the coordinator's fix: "Talk to
  Filia in Central City first", 225 px), "Defeat the Iseria Queen
  first".

## Session 3 (successor, 2026-10-25)

- **Full batch rerun** (scratchpad/apworld2/batch/full, seeds 1-96, players/): 96/96 exit 0,
  192 .apso2, check.py **0 files with errors** (192 option combinations; 276-1744 locations;
  5960 progression items in SO2 locations; versions: 9 v1, 183 v2). The area-key-in-drop fix
  is confirmed. scratchpad/apworld2/batch/stats.py: progression in drops: allow 214 in 33
  open-world slots / 61 in 18 story slots; force 391 in 19 open-world / 103 in 6 story; never
  0; killer moves: any 180 in 36 slots, hero 71 in 28, never 0; crafts 0 in 59 slots.
- **Story identity** (scratchpad/apworld2/batch/base, players_base, the 16 all-story seeds,
  `drop_progression: never`): 16/16 exit 0; compare.py vs scratchpad/elgate/batch/gate2: all 32
  identical but `options` (11 keys added, no value changed).
- **Tests:** world 235 passed (19713 subtests); AP general 379 passed (117030 subtests); no
  warning from so2 (logs scratchpad/apworld2/test_world.log, test_general.log).
- **docs/ap/apworld.md: done** (Locations table + crafts / killer / join bullets, Items, Regions
  and events, Logic rule + characters, new sections Enemy drops / Craftsanity / Killer moves /
  Party v2 / Goals, Options table, The seed, Files, Testing 0.2.0 results, Changes 0.2.0, 0.1.1
  marked released, Next).
- **Player guide: done** (six location kinds; the goal section with the three goals, Filia's
  scene = Central City entered as a Private Action then City Hall, per goals.md; items:
  characters as progression under killer any; locations table + drops / craftsanity / killer
  paragraphs; "In open world" party rules; options rows for every new option, the
  `starting_character` row fixed: hero / any / names, default hero, open world only).
- **Template:** dist/"Star Ocean The Second Story.yaml" is generated by tools/build_apworld.sh
  (AP's generate_yaml_templates) from options.py and git-ignored: nothing to edit. Generated it
  into scratchpad/apworld2/templates: rc 0, the new options render (goal's three values,
  starting_character hero default, exclude_characters []).
- **Small code fixes:** goal gate door `name` now "Indalecio (Limiter Off)" / "Indalecio
  (Limiter Off, Iseria Queen)" (was "Indalecio (indalecio_limiter_off)"; matches the contract's
  example; the port only logs it); Goal docstring: Central City as a Private Action;
  DropProgression docstring: no "priority locations"; items.classification docstring.
- **Publishing added** (optional item 5): make_crafts.py now reads Super5's tables from
  analysis/archive/disc1/3010.0.unpacked (overlay 0xbc2, base 0x8007e000): member table
  0x80083D68 = [1,2,3,8,5,9,4,7,6,11,12,10], books 0x80083D30 (common, items 196-207) /
  0x80083D4C (rare, 10 %, 208-219); makers = the subject (party_slot_active: any of the 8 slots).
  crafts.py: 553 crafts, 156 member-locked (the generator reproduced the old crafts.py byte for
  byte first; backup scratchpad/apworld2/crafts_529.py, make_crafts_529.py). Verified all 553
  are their own common_kind (item pack entry 3000 blocks 3-5: 14 rare/common pairs, none a
  craft), so every craft check can fire. Tests: counts 529 -> 553; world tests 235 passed.
  Docs (apworld.md, guide, options.py) say 553. No port change: the hook already covers page
  0x10 variant 5.
- AP general tests rerun on the final code (Publishing, door name): 379 passed (117030
  subtests).
- **Contract fix from the coordinator** (the hooks agent measured it at map 671 with the port's
  banner_text_width): the goal gate banner "Speak with Filia in Central City first" is 248 px,
  over the 236 px limit; now **"Talk to Filia in Central City first"** (225 px) in world.py
  GOAL_BANNERS, seed_format.md (v2 Goals table and example), apworld.md and the player guide.
  "Defeat the Iseria Queen first" (192 px) stays. (apv2.md's decision record and this log's
  "Decisions" quote the old text as it was decided; left as history.)
- **Final-code batch** (after Publishing and the door name; before the banner text, a string
  only): 96/96 exit 0, 192 .apso2, check.py 0 errors, 192 combinations, 276-1768 locations,
  5954 progression in SO2 locations (versions 9 v1 / 183 v2); stats.py: drops allow 195 in 33
  open / 74 in 18 story, force 400 in 19 / 97 in 6, never 0; killer any 187 in 36, hero 68 in
  28, never 0; crafts 0 in 59. Base (16 all-story seeds, drop_progression never): 32 identical
  but `options` (11 keys added, none changed).
- After the banner fix: world tests 235 passed; one generated seed (scratchpad/apworld2/
  bannercheck, open world, goal 2, craftsanity) has the gate door with "Talk to Filia in
  Central City first" / "Defeat the Iseria Queen first", name "Indalecio (Limiter Off, Iseria
  Queen)", 553 craft locations incl. the 24 books.
- **Session 3 done.** Left: make_crafts.py / make_moves.py live in scratchpad/apworld2 (not in
  git); moving them to tools/ would keep crafts.py / moves.py reproducible (outside this
  agent's edit rights). The ships agent's proposal (Hoffmann via the teleporter's ship window)
  is not taken up (docs/handoff/ships.md).
- **Generators moved** (bookkeeping, 2026-10-25): make_crafts.py / make_moves.py are now
  tools/ap_make_crafts.py and tools/ap_make_moves.py (tracked, executable), reading
  docs/ap/data/craft_results.csv, craft_decode.txt and move_levels.csv instead of the
  scratchpad copies; crafts.py / moves.py are reproducible from git again. The scratchpad
  originals (scratchpad/apworld2/make_crafts.py, make_moves.py) are left in place unchanged.

## Session 4 (gifts agent, 2026-10-25)

The hub plan's "gifts" work item (docs/handoff/hub.md): the user's decision "NPC gift checks
should NOT hold progression" (before: the 9 gift sites the late proof reaches in story mode
could, and in open world Eluria Colony's 2 gifts could hold our own Expel-story keys).

- **rules.py:** `may_hold_progression` and `open_world_progression` now treat `"gift"` as
  never-progression, the same way as `"drop"`, `"join"` and `"shop"` (one added entry in each
  tuple); the module docstring's list of never-progression kinds updated. No other logic
  changed: a gift location's reachability (`open_world_access`, the region graph in story mode)
  is untouched, only whether it may hold progression. This also closes the one open-world
  exception gift sites had: Eluria Colony's 2 gifts can no longer hold even this world's own
  Expel-story keys.
- **Tests:** `test_world.py` gains `assert_no_gift_progression` (checks `can_fill` is false for
  every gift location against both this world's own progression, e.g. an area key, and another
  world's) and a `test_gifts_never_hold_progression` method on `LateProofChecks` and
  `OpenWorldChecks`, so it runs under the existing `TestLateProofAny`, `TestLateProofRena`,
  `TestOpenWorld`, `TestOpenWorldRenaShops` and `TestOpenWorldNoTeleporter` (story and open
  world, with/without the teleporter, cave_of_trials, missable_locations, shuffle_shops and
  character_items). Also fixed `OpenWorldChecks.test_progression_rules`, which asserted a gift
  in Expel could hold progression when the teleporter or late proof reached it (no longer true):
  it now special-cases `row["type"] == "gift"` to assert `can_fill` is always false there. World
  tests: 240 passed, 19751 subtests (was 235 / 19741; +5 new test methods).
- **Batch:** reran the 96-seed batch (`scratchpad/apworld2/batch/full`, same `players/`): 96/96
  exit 0, 192 `.apso2` over 192 option combinations, 276-1768 locations each, all generate and
  fill. `check.py` extended (`allowed` / `allowed_open` add `"gift"` to the never-progression
  types, matching rules.py) finds **0 errors**; 5957 progression items in SO2 locations (3-73
  per slot; was 5954 before this fix, noise from the fill RNG). No fill failure in open world
  with `expel_progression` off, where the Eluria Colony gifts had been one of the few places an
  Expel-story key could land: still found room among the chests and drops in every slot.
- **Docs:** docs/ap/apworld.md ("Logic" — the rule and the "9 gifts" / Eluria Colony results
  text; "Changes" 0.2.0 — new bullet; "Testing" — a new dated results entry). The player guide
  (`apworld/so2/docs/en_Star Ocean The Second Story.md`) never claimed gift sites may hold
  progression (checked: it only states what shops/drops/crafts/killer moves do), so it needed
  no change. Not touched: seed_format.md, data.py (no data-table change, pure rules.py logic),
  tools/ap_world_tables.py, engine/.
- **Not released yet:** this folds into the still-unreleased so2.apworld 0.2.0 (no version bump
  of its own).

## How to verify

    ln -sfn $PWD/apworld/so2 scratchpad/ap/Archipelago/worlds/so2
    cd scratchpad/ap/Archipelago
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 worlds/so2/test
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 test/general
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python ../../apworld2/batch/check.py ../../apworld2/batch/full
    cd ../../apworld2/batch && python3 compare.py base ../../elgate/batch/gate2
    python3 tools/ap_world_tables.py -o scratchpad/apworld2/x.py && cmp scratchpad/apworld2/x.py apworld/so2/data.py
