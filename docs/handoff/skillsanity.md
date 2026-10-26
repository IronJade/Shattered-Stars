# Handoff: skills as items, the skill guilds as checks (a new option)

Queued 2026-10-25 by the main session. No agent yet.

## The user's request (verbatim)
> The game also has people 'buy' the skills from skill guilds. Could the skills be picked up as
> drops or checks as well? Could we make that an option, and the skill guilds just give a list of
> drops per the number of skills they would have gotten? Like sensability 1 gives I believe 5
> skills, so the user with this option on could instead get 2 blackberries, a resurection mist, a
> sword for Claude, and a Key for an area instead of the skills. Then the user opens a chest and
> actually gets Motormouth, or Below the Belt, or Knife, as an item drop instead?

## The user's extension (2026-10-25, verbatim)
> With the skills being able to be gotten as individual items/drops, should we add it as a
> "Skillsanity" setting, and include not just skills, but the killer moves and spells for the
> characters as well, instead of those being driven by level, they are driven by finding them.
>
> Skillsanity:
> none - Off, skills, killer moves/spells are gained per standard.
> skills only - Skills are found, killer moves/spells are gained per standard.
> special only - Killer moves/spells are found, skills are gained per standard.
> insanity - All skills, killer moves/spells are found from checks/drops.

**The user's answers on the design (2026-10-25):**
- **Learn points become checks.** Where a character used to learn a killer move or spell (a
  level-up, or an event / item that teaches it), that moment sends a check instead (e.g. "Claude
  Lv 3"), one location per move: items and locations stay balanced and levelling stays rewarding.
- **First move given.** Under special only / insanity each character keeps the first killer move /
  spell they'd learn (Claude's Air Slash, Rena's Heal, ...): known when they join; every later move
  is found.

**So the option is `skillsanity`: none / skills_only / special_only / insanity** (Archipelago
identifiers; the display names as the user wrote them). Skills = the guild sets (below); special =
killer moves and spells (character-specific items, e.g. "Claude: Air Slash"; received before the
member joins: kept and applied at the join, party v2). Interactions: the killer_move level checks
need the move; craftsanity needs the specialty skills; the hub (characters as items, its levels).

## The user's second extension (2026-10-25, verbatim): sold in shops
> Skills, Killer Moves, and Spells should also be able to have a chance of being found in the
> shops as unique items being sold. It introduces oddities, but the cost of them should be relative
> to the level it would have been gained at x100. A level 3 special should be 300, etc. A single
> skill, like perseverance should be whatever the cost of it's original package cost divided by the
> number of skills in that guild pack. I believe sensibility 2 had perseverance and cost 1600 at
> Herlie. It had 4 skills making each in that pack a possible 400 fol item if it appeared in a shop.

To work out (with "shopboth", which reworks the shop slots: docs/handoff/shopboth.md):
- With shuffle_shops on, a shop slot holding one of this world's skill / killer move / spell items
  sells it as itself ("Perseverance", "Claude: Air Slash") at the user's price: a move = the level
  it would have been learned at x 100 (an event-taught move: say what level it stands for); a skill
  = its guild set's price / the number of skills in the set (verify the user's example, Sensibility
  2 at Herlie, 1600 / 4 = 400). Buying it gives the item (and is the slot's check).
- "A chance": how often such items land in shop slots (a weight or a share; propose).
- Shop slots never hold progression today (rules.py). Skills / moves can be progression under
  skillsanity (craftsanity's specialties, killer-move checks). Options: keep shops progression-free
  (only non-progression skill / move items there), or allow these items in shops (a price is
  always payable by farming; the logic can't model Fol). Recommend one; it is the user's call.

## The design to work out (research first, then report before building)
- Reverse the guilds: which guilds sell which skill sets (shop / menu code, docs/notes/menu.md),
  each set's skills, prices, how "a skill is available to learn" is stored (bits in the game state
  or the config), and how learning with SP works afterwards; catalog it.
- Research also: every member's killer moves and spells with how each is learned (level, event,
  item), and where the game stores 'learned' per member; then the option (APWorld, so2.apworld
  0.2.0 unreleased) as above. For skills: each skill becomes an
  item ("Skill: Motormouth": received -> available to the party, as buying the set would make it);
  each set's purchase becomes N locations, N = the skills in the set (bought as now, for its price;
  each of the N checks gives or sends its item).
- Logic: skills gate things. Item creation and craftsanity locations need specialties (Customize,
  Cooking, Machinery...); super specialties need combinations; some talents / killer moves may too.
  With the option on, those locations must require the skill items (they become progression),
  in every world mode (story, open world, hub; the guilds the hub reaches: the hub proof).
- Port: the guild purchase gives the N checks instead of the skills; a received skill item sets
  the "available" state; the menus show it as usual. Sidecar / save guard unaffected (check).
- Tests, the batches (check.py extended), docs (seed_format.md, apworld.md, the player guide).
- If something must be the user's call (for example skills that matter to the story, or a set
  the hub can't reach): stop and report with a recommendation.

## Rules
As in claudesword.md (own build dir engine/build-skillsanity, scratchpad/skillsanity/). APWorld
tests and the batches as docs/handoff/apworld2.md says.

## Done (log; agent "skillsanity")
- 2026-10-25 ~16:00 started (step 1: research, report before building). Scratch:
  scratchpad/skillsanity/ (dis/d1: every disc-1 map script disassembled with strings;
  tables.py, spells.py, guild_menus.py: the readers).
- ~16:20 research done; written down in docs/notes/menu.md "Skill guilds" (sets, prices, flags,
  guild maps, skill ids, starting skills, the moves / spells outside the learn tables) and the
  catalog (globals.csv k_book_groups + script_flags 700-711; functions.csv field_special_command
  0x12 Float; script_opcodes.csv ext 0x73 call sites). The report went to the coordinator.
- Findings outside this task, in the report: the three Cave of Trials "Portrait F / G / J chest"
  rows (maps 740 / 798 / 799, flags 2675 / 2657 / 2721) are the learn chests of Extinction /
  tri-Ace / Meteor Swarm (the routine's push of the move index was read as an item); a
  cave_of_trials seed patches that index with a placeholder and the port's ext 0x73 then writes
  past the 0x680-byte records block. The hub state presets flags 700-711 (all 12 sets bought).

## In progress
- Waiting for the coordinator's go-ahead and the user's calls (the report's list).

## Next steps (step 2, after the go-ahead)
- Port: guild flags 700-711 -> the set's checks; skill_book_allowed from received skills;
  learn tables / joins / ext 0x73 / items 464-469 / special 0x12 send checks instead of teaching;
  received skill and move items (sidecar, applied at joins); first moves given.
- APWorld: option, items, locations, rules, seed fields; tests, batches (check.py), docs.

## How to verify
(filled in as the work lands)

## The user's decisions on the step 1 report (2026-10-25)
1. **Join level (new, applies beyond skillsanity): always, in the open world and the hub.** The
   user: "when they join, they join at a scaled median level of the main party and they get their
   checks fire as points they are already past ... say she drops when the main party is average
   level 90. She should join at level 90 and all the checks she would have had by then fire at
   once." Ruled: **always in open world / hub (no option), both ways** (a member recruited by a
   level-10 party joins at 10 even if the original join level is 40). The target = the median
   level of the party members present at the join (say how "the main party" is counted: the
   active party, without the joining member). Stats at the target level as the game's growth
   would give them (up: the level-up routine; down: from the growth tables; research it; if
   down-scaling can't be exact, stop and report). Story mode keeps the original join levels.
   Under skillsanity special_only / insanity: the member joins knowing only the first move, and
   every learn point up to the join level fires as a check at the join.
2. **Hub guilds: un-bought in every hub seed** (clear flags 700-711 in hub_state.json's hub
   flags: tools/ap_hub_state.py; the hub proof's guild reach is unchanged).
3. **Shops stay progression-free:** only non-progression skill / move / spell items in shop slots.
4. **Accept all the other recommendations:** Phase Gun left out; the 9 item / event-taught moves
   included as items; Float included when the Cave of Trials is on; starting skills stay the
   members'; prices: level x 100, item-taught 5000, cave prizes 8000, Float 900, skills set price /
   set size; `skillsanity_shop_share` 0-100 %, default 10; guild checks never progression.
- Also from the user: shopboth's courier fix (W5) applies with a seed only.
- Bug found in step 1: the three Cave of Trials learn chests (740 / 798 / 799) as chest rows: the
  agent "cavefix" removes them and bounds-checks ext 0x73 / 0xfc / A 0x21 (docs/handoff/cavefix.md);
  skillsanity brings them back as learn points.

## skillsanity2 (step 2; started 2026-10-25)
Rules as the coordinator gave them (own build dir engine/build-skillsanity, scratch
scratchpad/skillsanity/, headless, 4 GB cap, at most 2 game processes, no commits; small hunks in
shared files with dated notes in cavefix.md / hubproof.md; past ~400k tokens update this log and
stop). Order: 1 join level, 2 guild / skill items, 3 moves and spells, 4 shops.

### Done
- Read the spec (this file), menu.md "Skill guilds", shopboth.md, seed_format.md, apworld.md,
  apworld2.md "How to verify", cavefix.md.
- **Join level, the down-scaling research (the spec's "research it first"): it can't be exact.**
  Stopped here and reported to the coordinator, as the spec says. Nothing built, no source file
  changed. Evidence (scratchpad/skillsanity/growth.py: the growth table off the disc;
  downscale.py: the inverse):
  - A join is `party_slot_init` -> `character_init_data` (field/characters.cpp, the decompiled
    init_* of 0x8007a37c-0x8007b0c0): every recruit's stats at the join level are authored
    constants (Bowman L25 HP 1500 ..., Noel L40, Chisato L40), not growth from level 1.
  - A level-up (battle/result.cpp levelup_apply / levelup_gain_hp_mp_sp, 0x80085518-0x80085ed4)
    adds random gains: HP L*3/2 + (rng(3)+3)*8 below L50 (else rng(3)*8 + 86), MP rng(3)+4, SP
    about L, and six attributes rng(range)+base from the member's row of 0x80087f94 (STR +0x2a,
    INT +0x42/+0x44, DEX +0x3c/+0x3e, CON +0x30/+0x32, slot +0x48/+0x4a, record +6/+8). So a
    level has no single "stats at level N" to go back to: up is exact (the routine), down isn't.
  - The authored stats don't fit the growth either: INT (+0x42) is 0 for every recruit at any
    join level (no init writes it; levels add 1-3 each), and the mean-gain inverse goes negative
    for big drops (Leon L30 -> L1: HP 27, STR -4, DEX -8; Noel L40 -> L1: DEX -8).
  - Exact parts: the level and EXP (EXP = the exp table's threshold of the target level), the
    moves (the learn tables: clear the entries above the target, the level-ups relearn them).

- **The user's answers (2026-10-25, via the coordinator): go ahead with step 2, all pieces.**
  1. Scale down: option 1, the mean gain per level subtracted, floored at the lower of Claude's /
     Rena's level-1 values (HP 100, MP 20, STR 5, DEX 10, CON 2, INT 0, slot +0x48 20, record
     +6 10); deterministic; SP and equipment as the member joins. Up: the level-up routine.
  2. The starting character scales too (the character select's pick included): the hero's level,
     1 at New Game.
  3. "The main party": the 8 slots (positive ids) without the joiner or anyone set aside; the
     median, an even count the mean of the middle two rounded down; clamped 1-255; every new
     join (a slot init) in open world and hub; a returning member keeps their data.
  4. New small item: rename the Cave of Trials chest locations by their real floor (today all 75
     say "Cave of Trials Level 1: ..." by RELEASED_AREAS); keep the ids; floors as LEARN_CHESTS;
     data.py regenerated and cmp-identical; docs and the player guide. (Trackers read names from
     each multiworld's own data package, so old rooms keep theirs.)

- **Piece 1, the join level (port): built and unit-tested.** engine/src/game/ap/skills.{h,cpp}
  (new): `on_member_init` from field vm_svc_party_slot_init (every join) and special 0xd's Dias
  re-init; `join_target_level` (median), `scale_down_stats` (option 1), up = battle levelup_apply
  (+ levelup_learn_skills); battle/result.{h,cpp}: LevelUpCtx.config (outside a battle), #1981
  read from the disc outside a battle, `learn_table(character)`. Disc data only with the disc
  open (`disc_ready`: program_data caches an empty image). Tests: ap_join_target_level,
  ap_join_scale_down. Headless: runs/start.log (Ashton 17 -> 1 at New Game, Dias 35 -> 1),
  runs/late_up.log (the late save, party trimmed by SS_SWEEP_PARTY_DROP: Dias 35 -> 220 and
  Celine 8 -> 220 through the level-up routine, all their table moves known). Smoke ap_v2_start
  gained two checks.
- **Skillsanity port (pieces 2-3) built:** seed.{h,cpp} (option `skillsanity`, kinds skill / move,
  checks guild / learn, shop `price`), ap.{h,cpp} hunks (give(), sidecar `skills` / `moves`,
  deliver_check, on_flag_set 700-711, give_start -> skills_new_game), hooks: result.cpp learn
  tables (on_learn_point), script_ext ext 0x73, item_effect character_flag (464-469) and
  skill_book_allowed, special 0x12 (Float). Tests ap_skillsanity_seed / ap_skillsanity_checks.
  ss_game_tests 180/0.
- **Item 4, the Cave of Trials renamed by floor:** tools/ap_world_tables.py `CAVE_FLOORS` (from
  map_names.csv; 710 Treasure Vault, 711 Weird Beast Arena): 76 names (73 chests and gifts' areas,
  3 Level-1 rows lost their "(map N)"), ids unchanged; data.py regenerated, cmp OK. Note: the
  cave's boss names come from another rule and say Phoenix "Level 11" (map 809, Level 12) and
  G-Celesta "Level 12" (815, Level 13), Weirdbeast "Level 1" (711): not renamed (not asked).
- tools/ap_make_skills.py -> apworld/so2/skills.py (sets, skills, starting skills, specialties,
  learn tables with the first move, the 9 extra moves).

- **APWorld skillsanity (pieces 2-4) built:** apworld/so2/skillsanity.py (new: the items "Skill: X"
  SKILL_BASE ITEM_BASE+0x60000+id and "Member: Move" MOVE_BASE +0x70000+(pid<<8)+index; the
  locations: 45 guild checks type 13, 148 learn points type 14 + Float; choose / plan /
  classify / rules / place_in_shops / price_of / seed_check), apworld/so2/skills.py (generated,
  tools/ap_make_skills.py). Hooks: options.py (Skillsanity, SkillsanityShopShare), items.py,
  locations.py, rules.py (craft rule, killer_rule's move, Iseria Queen's Musical Talent), hub.py,
  world.py (plan, classify, pool names, place_in_shops before the traps, version 2, slot data,
  shop `price`). data.GUILD_PLACES (tools/ap_world_tables.py GUILD_MAPS). Guild checks and learn
  points never hold progression (learn points: my call, as crafts; say so in the report).
  Skill items needed (craftsanity's specialties not covered by a sure member's starting skills,
  Musical Talent for the Iseria Queen, Machinery for the six machine moves) and move items a
  killer-move level needs are progression; the rest useful.
- **Hub guilds un-bought:** tools/ap_hub_state.py clears 700-711 -> docs/ap/hub_state.json (1063
  -> 1051 hub flags; reproduced byte for byte before), data.py HUB_STATE minus those 12.
- **Port shops:** menu_shop.cpp entries `ap` (name, price; cart <= 1; no inventory_add; hover /
  held 0), menu_gfx TextRenderer::draw_ascii / measure_ascii (the 0x4006 name path), skills
  `shop_ap_item`. Capture cap/shop/frame01890.png ("Perseverance 400", "Shooting Stars 700", both
  marked).
- **Tests:** ss_tests 99/0, ss_game_tests 181/0 (4 new), smoke 270/0 (11 new: ap_v2_start join
  level x2, ap_skill_guild x3, ap_skill_join x2, ap_skill_shop x4); world tests 380 passed (39 in
  test_skillsanity.py), AP general 379 passed. Headless proofs (scratchpad/skillsanity/runs):
  start.log, late_up.log, guild_K17.log (Herlie's guildmaster K17 sells Knowledge 2 -> 3 checks),
  shop.log (Fol 50000 -> 48900, the skill and the move given), late_sp.log (the late save,
  special_only: Dias 35 -> 220 "9 learn points checked, 3 moves to find"; Celine 8 -> 220 "19
  checked, 2 to find, 1 received known").
- scratchpad/apworld2/batch/check.py: check_skillsanity() (backup check_pre_sk.py) + the v2 term;
  players_sk / players_hub_sk (skillsanity and the share random).

- **Batches (2026-10-25 ~21:15):** scratchpad/skillsanity/batch: sk_full (players_sk) and sk_hub
  (players_hub_sk) 96/96 exit 0 each, check.py 0 errors on 192 + 192 files (stats.py: every
  skillsanity mode in story / open world / hub; 9938 and 8673 progression items in SO2 locations,
  own skill / move items 489 / 1366 and 453 / 1151; priced shop slots 3098 in 90 slots and 3519 in
  99). The identity (skillsanity none): sk_id_full / sk_id_hub (players / players_hub as they
  were) 96/96 each, check.py 0 errors; vs scratchpad/apworld2/batch/full_hp / hub_hp
  (identity.py, identity2.py): byte-identical but for `options` (the new skillsanity keys, and
  difficulty's) and the hub's `hub.flags` (700-711 gone): 62 + 82 files; the others are exactly the
  multiworlds where a slot has cave_of_trials (the renamed cave locations change the fill order of
  both slots); none differs otherwise.
- **Docs:** seed_format.md ("The join level", "Skillsanity", the version rule, the options table),
  apworld.md (Locations / Items rows, "Skillsanity", options rows, Files, regenerate line, Changes x3,
  Results), the player guide (Skillsanity paragraph, the join level, two options rows), menu.md
  "Skill guilds" (the port), catalog globals.csv levelup_growth (the attributes' names, the recruits'
  authored stats). Rechecked at the end: data.py and skills.py regenerate identically; world tests
  380 passed.

### Release notes line
(release notes: docs/release_notes/0.02.50.md)

### In progress
- Nothing: done and verified (2026-10-25). Not committed (rule: no commits).

### Next steps
- Coordinator: commit (docs first). Open questions in the final report: the cave bosses' floor
  names (Phoenix 809 says Level 11, is 12; G-Celesta 815 says 12, is 13; Weirdbeast 711 says
  Level 1, its arena); learn points never progression (my call); the cave learn chests' scene
  still says "learned" (the move comes as its item).

### How to verify
- `python3 scratchpad/skillsanity/growth.py "$SS_DISC1"` (the growth table);
  `python3 scratchpad/skillsanity/downscale.py` (the recruits' join stats and the mean-gain
  inverse to L1 / L10).
- `cmake --build engine/build-skillsanity -j8`; `engine/build-skillsanity/ss_tests`,
  `ss_game_tests` (ap_join_*, ap_skillsanity_*); the smoke: `XDG_CONFIG_HOME=<scratch>
  SDL_AUDIO_DRIVER=dummy SS_DISC1=<cue> tools/smoke_field_events.sh engine/build-skillsanity/shattered-stars`.
- `scratchpad/skillsanity/run.sh NAME ...` (capped, scratch XDG / card): the proofs' command lines
  are the smoke's ap_skill_* cases and, for the late save, SS_EVENT_SWEEP=1 SS_BATTLE_STUB=1
  SS_SWEEP_PLAY=999:T0 SS_SWEEP_PARTY_DROP=2,3,4,5,6,7,8,9,11 SS_SWEEP_PARTY=4 SS_AP_ANY_SAVE=1
  SS_LOAD_SAVE=0 SS_MEMCARD1=cards/late.mcd (copy of scratchpad/latesave/card.mcd) with
  seeds/ow.apso2 or seeds/late_sp.apso2.
- APWorld: `cd scratchpad/ap/Archipelago && SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m
  pytest -q -n 8 worlds/so2/test` (and test/general); batches: `cd scratchpad/apworld2/batch &&
  ./run_batch.sh players_sk ../../skillsanity/batch/sk_full $(seq 1 96)` then check.py.
- Tables: `python3 tools/ap_make_skills.py "$SS_DISC1"` (skills.py), `python3
  tools/ap_world_tables.py -o x.py && cmp x.py apworld/so2/data.py`, `SS_DISC1=... python3
  tools/ap_hub_state.py` (hub_state.json).

## The user's decisions on skillsanity2's join-level report (2026-10-25)
- Scale down: subtract the average gain per level with floors (skillsanity2's option 1).
- The starting character scales too (joins at the hero's level: 1 at New Game).
- skillsanity2's counting of "the main party" stands.
- Rename the Cave of Trials chest locations by their real floor (the user: trackers read names from
  each generation's own data package; checked: Main.py stores data_package in the multidata,
  WebHostLib/tracker.py reads it from there). Location ids unchanged.

## For skillsanity2 / hubproof2 (from difficulty, 2026-10-25 ~18:30)
Small hunks in shared files, all marked [difficulty agent]; nothing of yours touched:
- apworld/so2/options.py: class `Difficulty` (earth / galaxy / universe / any) after EncounterRate,
  `difficulty: Difficulty` in SO2Options, and in the "Battles and crafting" group.
- apworld/so2/world.py: `seed_version` (a non-earth difficulty is v2) and one slot_data line.
- apworld/so2/test/test_difficulty.py (new file).
- engine/src/game/ap/seed.h (`Seed::difficulty`), seed.cpp (the parse after story_route), ap.h /
  ap.cpp (`seed_difficulty()` after required_story_route); engine/tests/test_ap.cpp
  TEST(ap_difficulty_seed) after ap_phase3_seed.
- tools/smoke_field_events.sh: the ap_diff_* block after the character select block (5 runs).
- scratchpad/apworld2/batch/check.py: the version rule counts a non-earth difficulty as v2;
  a `difficulty` kind; an invalid value is an error.
- Docs: seed_format.md (the version rule, the options table, "### Difficulty"), apworld.md
  (options table, "The seed", Changes 0.2.0), the player guide (options table row).
