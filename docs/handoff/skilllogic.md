# Handoff: skilllogic (guild checks and learn points may hold progression)

Agent "skilllogic", started 2026-10-27; its permission prompts failed before it could test
anything. Continued by agent "skilllogic2" (2026-10-27), same rules.

## The user's decision (2026-10-27, verbatim)
"Absolutely, let learn points hold progression, and the skill guilds should also be able to hold
progression, as long as they are within reach routing wise. That is a logic ladder check, but it
would make sense, as logically in the game you would normally get skills to be able to beat the
game. There is nothing illogical about the skill guilds holding progression. You can beat the game
without ever visiting an item shop. You could probably beat the game without skills, but it would
be WAY harder, and most bosses nearly require the combat skills anyway."

Item shops stay progression-free. Only skill guild checks and learn points change.

On the design below (the user, 2026-10-27): "Fol rule is fine as written"; "Character Item learn
points can count as progression, that's fine."

**Release label (the user, 2026-10-27, verbatim): "This is staying in 0.2.1".** Write the change
into 0.2.1's changelog and release notes as final. When everything is verified, rebuild
dist/apworld-0.2.1/ (so2.apworld + template YAML) from HEAD per docs/ap/release_checklist.md, check
it loads alone from custom_worlds and generates solo, the seed 75 reproducer and a ChecksFinder
multiworld; report the new sha256 values. Version stays 0.2.1. No tag, push or publish.

## Why
docs/handoff/ncpfix.md "Open for the user": solo_noexpel seed 75 (open world, Rena, expel_progression
/ boss_checks / enemy_dropsanity off, skillsanity special_only, killer_move_checks hero) can't fill:
60 move items progression, only 63 locations may hold one.

## The design (skilllogic; the code: apworld/so2/skillsanity.py guild_rule / learn_rule /
## learn_characters / set_location_rule, world.py progression_characters, rules.py killer_rule doc)
- **Learn points:** only learn-table levels may hold progression. They follow killer-move timing:
  up to level 30 (EARLY_LEVEL) from the disc switch (EVENT_ENERGY_NEDE), otherwise from the Final
  Save Point; hub mode: from the start, or once Fienal is open.
  - Open world with the story's party rules (character_items off): Noel's early levels also need
    "Stage 15 done" (STORY_JOINS), since he only joins at his house (unless he is the start).
  - A member who joins by a character item needs that item, which becomes progression
    (skillsanity.learn_characters -> world.progression_characters, as killer_move_progression any
    already does).
  - Members the logic can't count on (fighter_status not "sure" / "item") and EXCLUDED rows stay
    progression-free. With encounter_rate 0, no learn point holds progression.
- **Still no progression:** the machines (items 465-469 come only from rare Machinery crafts, 464
  from a shuffled gift or a Private Action), the cave's three learn chests (the late proof never
  reached them), Float (no proof covers the prize).
- **Guild checks:** each of a set's guilds counts as a place, like first-drop logic
  (rules.drop_rule). A check holds the best class among its places ("any" / "expel keys" / "none")
  and is reached only through places of that class, so it never counts through a guild that
  closes at the El voyage (an Expel guild when Expel can't hold progression). Every set has a
  late-proven guild on Energy Nede.
- **The Fol rule** (from battles.json enemy Fol over field.json's encounter tables, Earth, x1 Fol;
  scratchpad/skilllogic/fol.py, fol2.py):
  - sets up to 1600 Fol count from any guild that sells them (~4 world-map battles at ~460 Fol);
  - sets of 2700 Fol and up (DEAR_PRICE) count from "Stage 9 done" (FOL_STAGE; a Hoffmann Ruins
    battle pays ~2100 Fol);
  - story mode uses the disc switch for the dear sets (and for every non-Expel guild), hub mode
    "Fienal open";
  - no guild holds progression with encounter_rate 0.
- **Port:** by reading, no change. Guild and learn checks go through deliver_check into the same
  give queue as killer-move, boss and craft checks, which already delivers keys and characters.
  To be proven headless (step 3).

## Rules given
- Touch only apworld/so2/, tools/ap_batch*, docs/ap/, docs/release_notes/ and this file. Not
  engine/ ("pendingsave" works there on main; "partybuild5" in scratchpad/wt/party: never touch).
  If the port needs a change, don't make it: write it up and stop.
- Commits on main after each verified piece, docs first, staged by path (never `git add -A` / `.`
  / `-a`); message ends with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.
  Never push, tag, publish.
- Scratch in scratchpad/skilllogic/. Cap output. Long jobs in the background with deadlines, under
  a memory cap; headless only, scratch XDG_CONFIG_HOME; kill by exact PID. Since the crash
  (2026-10-27): at most 1 heavy job at a time, pytest -n 4, every Python generation under prlimit.
- A denied tool call: don't retry or work around; write the blocked step here and stop.
- A rule that turns out unsound in a way that needs a gameplay design choice: stop and report.
- Roll over at ~400k context.

## Steps / status
1. [x] This file (skilllogic2).
2. [x] Verify the logic (2026-10-27 ~08:45; below, "Step 2 so far"; full world suite **537 passed**,
   37668 subtests, t/world2.log, -n 4; committed with the world change): capacity.py and fill rates (100 solo seeds each, seed 75's mix and its near
   neighbours, all generating); world tests with the seed 75 mix as a WorldTestBase class;
   assertAccessDependency for guild checks (area keys, Stage 9 / disc switch / Fienal tiers) and
   learn points (disc switch / Final Save Point, character items); tests that guild checks and
   learn points may hold progression and item shops still can't.
3. [x] Port proofs (headless, engine/build-skilllogic): a key and a character at a guild check, a
   key at a learn point, from a hand-edited .apso2. **No port change needed** (below, "Step 3").
4. [x] (2026-10-27 ~11:00; "Step 4" below) Batches story_open / hub / solo_noexpel 96/96, checker 0 errors (extend ap_batch_check.py if
   it asserts these never hold progression). Identity: seeds without skillsanity byte-identical to
   scratchpad/ncpfix/v021; list the changed seeds (each must have skillsanity on). AP general suite
   and the scoped AP_TEST_WORLDS=so2 run.
5. [x] (42a61fa and the commit after this file's) Docs: apworld.md (skillsanity, logic, 0.2.1 changelog final), the player guide, options.py
   Skillsanity docstrings, docs/release_notes/0.2.1.md (drop the seed 75 limitation; add this).
6. [ ] Package dist/apworld-0.2.1/ from HEAD (release_checklist.md); load alone, solo, seed 75,
   ChecksFinder; sha256.

## State (skilllogic2, 2026-10-27 ~08:40, after the session crash)
- Uncommitted in main's working tree: apworld/so2/skillsanity.py, world.py, rules.py (skilllogic's
  design; matched scratchpad/skilllogic/wip.patch) and apworld/so2/test/test_skillsanity.py
  (skilllogic2's tests). All four saved as scratchpad/skilllogic/wip2.patch (569 lines).
- The session crashed (swap full) mid-step 2. New limits from the coordinator: **one heavy job at a
  time** (batch, fill run, pytest -n, game process); pytest `-n 4`; every Python generation under
  `prlimit --as`. One other agent (pendingsave) builds and runs the port alongside.
- The harness now names scratchpad/wt/party as the working directory; the rules say never touch
  it, so all work stays in main's checkout by absolute path.

### Step 2 so far
- World tests on the patched tree, before new tests (t/world1.log): 445 passed, 4 failed, all the
  old `test_no_progression_in_skillsanity_locations` (the old rule, reversed by design).
- **test/test_skillsanity.py:** helpers `may_hold` (the design's "may hold progression"),
  `learn_need` (a learn level's needs), `state_with`, `swept_without`, `guild_checks`,
  `learn_levels`, CHEAP / DEAR. The SkillsanityChecks mixin: `test_what_skillsanity_locations_hold`
  replaces the old test (guild checks / learn points as may_hold; shop slots refuse another world's
  progression and this world's own progression skill / move items), `test_learn_level_needs` (each
  learn level reached with exactly learn_need; each need taken away keeps it out). New classes:
  TestGuildsOpenWorld (Knowledge 1 through Cross / Linga / Central City, not Port of Clik; the dear
  sets need the Hoffmann Pass, stage 9, the cheap don't), TestGuildsOpenWorldNoExpel (only the Nede
  guilds: Central City / Armlock Pass), StoryTierChecks + TestGuildsStory / TestGuildsStoryNoExpel
  (cheap sets from the start with expel_progression on, else from the disc switch; Noel from the
  switch), TestLearnOpenWorld (Noel's early levels need Stage 15, by event states; early levels the
  Eluria Tower Pass, late ones the Mihne Pass, swept), TestLearnCharacterItems (seven pool members:
  each item progression, their learn levels depend on it), TestLearnHub (Fienal tiers),
  TestSkillsanityNoEncounters, TestSkillsanitySeed75 (seed 75's mix; move room > 100).
  test_skillsanity.py alone: **132 passed** (12077 subtests). Mutations (dbg/mutate.py: no Fol rule,
  no STORY_JOINS, no learn_characters, guild ways from every place, learn without the item,
  machines progression): each caught; skillsanity.py restored (cmp dbg/skillsanity.py.orig).
- Finding: Fun City Pass is a start key with the teleporter on; Noel's House Pass is a start key
  unless the teleporter and expel_progression are on, and then the El voyage needs it. So
  STORY_JOINS' Stage 15 never adds a key beyond the disc switch in today's mixes; kept (true to the
  game, safe if start keys change) and tested by event states.
- **Fill rates** (fill/run.sh A|B with ncpfix's fillrate2.py and s75/capacity.py; summary_A.txt,
  summary_B.txt; 100 solo seeds each): **0/100 FillError in each of 15 mixes**: seed 75's mix
  (move-item room 114, was 63) and + insanity, story_route claude, any, teleporter on, boss_checks
  on, killer_move_progression any, starting_character hero, enemy_dropsanity first_drop,
  skills_only, character_items, expel_progression on, story mode, hub mode, insanity +
  character_items + cave + craftsanity.
- **Seed 75's mix + encounter_rate 0: 91/91 FillError** (f_enc0.txt; cut off by the crash at 91).
  Not a regression: with no battles the design keeps guild checks and learn points
  progression-free exactly as 0.2.1 did, and 0.2.1 already failed this mix with battles on (its
  killer levels, then EXCLUDED, held some). skillsanity.plan still makes the move of every chosen
  killer level progression though those levels are EXCLUDED. For the user (limitation, or a
  follow-up); not changed here.

### Step 3: port proofs (2026-10-27 ~09:10; scratchpad/skilllogic/port: run.sh NAME args, seeds/,
### runs/, cards/ copies of scratchpad/latesave/card.mcd)
- Binary: engine/build-skilllogic/shattered-stars (built 06:51 from HEAD; engine/ unchanged since
  c445867, clean tree at the run), not rebuilt. By reading: guild sets (skills.cpp on_guild_flag)
  and learn points (the join's passed levels and the level-up's on_learn_point) both call
  ap.cpp deliver_check, which queues an own item in `pending`; the free-play loop gives it
  through give() (keys: state.keys; characters: characters_waiting -> join_waiting).
- **Guild** (seeds/guild.apso2, open world + character_items; runs/guild.log; the smoke's
  ap_skill_guild setup: SS_EVENT_SWEEP=1 SS_BATTLE_STUB=1 SS_SWEEP_PLAY=147:K17:0/0/0/0
  SS_SCRIPT_GLOBALS=a0=16,a4=0 SS_DEBUG_INPUT="5:fol 50000;1400:where", --no-memcard --debug
  --frames 1500 --map 147): Herlie's K17 sells Knowledge 2 -> "guild set 4 bought (flag 704): 3
  checks", "(1 of 3): Salva Pass", "(2 of 3): Celine", "(3 of 3): Hookshot for Alice", "key 2
  (Salva Pass) received", "given Salva Pass", "given Celine", "Celine joins the party (slot 1)";
  Fol 50000 -> 48500.
- **Learn points** (seeds/learn.apso2 = skillsanity's late_sp.apso2 with Celine Lv 4 -> our
  Central City Pass, Lv 6 -> our Ashton; runs/learn3.log; the late save: SS_EVENT_SWEEP=1
  SS_BATTLE_STUB=1 SS_SWEEP_PLAY=999:T0 SS_SWEEP_PARTY_DROP=2,3,4,5,6,7,8,9,11 SS_SWEEP_PARTY=4
  SS_AP_ANY_SAVE=1 SS_LOAD_SAVE=0 SS_MEMCARD1=cards/late3.mcd, SS_DEBUG_INPUT "300:ap_give
  Celine Bob" + ap_status every 1000, --debug --frames 12000): Celine joins at 220, her passed
  learn points fire: "(Celine Lv 4: Thunderbolt): Central City Pass", "(Celine Lv 6: Wind Blade):
  Ashton"; then "key 28 (Central City Pass) received", "given Central City Pass", "given Ashton",
  "Ashton joins the party (slot 4)"; ap_status waiting 2 -> 0, checks 28/28.
  Note: in this sweep harness free play comes only now and then (runs/learn.log, 4000 frames,
  ended with the two still waiting; learn2.log: Celine herself given only at ~1550): the
  harness's timing, not the port (the same queue as every other check).
- The level-up path (on_learn_point, battle result) wasn't run (needs a battle with EXP); it calls
  the same deliver_check.

### Step 4 / 5 in progress (2026-10-27 ~09:40)
- tools/ap_batch_check.py (uncommitted; backup scratchpad/skilllogic/ap_batch_check.py.orig):
  new `sk_may_hold(row, s, it)` (its own reading of the rule from the options and data.py:
  encounter_rate 0 none; learn: learn-table levels of sure members (story party rules: Claude,
  Rena, Noel, the start; open: the heroes + start; hub: a one-hero route's hero + start) or pool
  members (`characters`); guild: any place as a chest on its map: story `allowed`, open world
  `allowed_open` and not before the voyage unproven, hub HUB_MAPS in every game and Expel only
  with expel_progression); check_skillsanity reports progression only where sk_may_hold refuses
  and counts `_sk_prog`; the main and hub loops leave guild / learn to it; gate keys: at a guild
  check only if a guild passes drop_gate_ok, never at a learn point; the summary prints
  "progression at skillsanity locations: guild checks N, learn points M". On the 0.2.1 batches
  (scratchpad/ncpfix/v021 sk_full / sk_hub): 0 errors each, the summary lines identical to their
  check.txt, 0 and 0 at skillsanity locations.
- Batches running: scratchpad/skilllogic/batch/run.sh (story_open -> sk_full, hub -> sk_hub,
  solo_noexpel -> solo; rc.txt, all.done; ~19 s a seed with the other agent's load). Then
  `identity.py ../../ncpfix/v021 .` from scratchpad/skilllogic/batch (written, not yet run).
- Docs committed (42a61fa): apworld.md (Logic, Killer moves note, Skillsanity "Progression at
  guild checks and learn levels"), the player guide, options.py Skillsanity docstring.
  Uncommitted: docs/release_notes/0.2.1.md (new section, seed 75 line replaced by a TO CONFIRM
  line on the encounter_rate 0 variant; the test counts and the identity line still to update);
  apworld.md's 0.2.1 changelog and Testing results still to write (need the batch numbers).
- Not mine, uncommitted in main: engine/*, docs/ap/seed_format.md, engine/README.md,
  tools/smoke_field_events.sh (pendingsave). Never stage them.

### Step 4 (2026-10-27 ~11:00)
- Batches (scratchpad/skilllogic/batch/run.sh): story_open, hub, solo_noexpel **96/96 generate
  each** (solo seed 75 too). The first checker pass crashed (`starting_character` a list under
  "any": now read as None, as take_choices does) and then flagged 42 files with "teleporter off:
  progression other than Expel keys in Expel": all guild checks (303) and learn points (384),
  whose row region is nominal (classified, the chests held only our Expel keys); guild / learn
  added to that check's exemptions (sk_may_hold judges them). Then **0 errors on 192 + 192 + 96**;
  progression at guild checks / learn points 1046 / 1291, 1028 / 1625, 410 / 871. The final
  checker on the 0.2.1 batches: unchanged (0 errors, 0 / 0). Made-up placements (encounter_rate
  0 at guilds and learn levels, the machines, maybe members in a story-rules file): each refused.
- **Identity** (batch/identity.py ../../ncpfix/v021 . -> identity.txt): story_open 6 identical /
  90 changed, hub 3 / 93, solo 20 / 76, **0 unexpected** (every changed seed has an SO2 player
  with skillsanity on; every seed without one byte-identical); solo 75 rc 1 -> 0; 2 solo seeds
  with skillsanity unchanged (83, 95: story mode, no progression items to move).
- Suites (scratchpad/skilllogic/suites, -n 4): world **537 passed** (37687 subtests; slowest 0.86
  s), unittest 537 OK, test/general **379 passed** (117750), scoped **590 passed** (42996).
- Docs: apworld.md (test table row, Results block "skillsanity progression", the 0.2.1 changelog
  with the new bullet and identity line), release notes (counts; the TO CONFIRM line is now the
  encounter_rate 0 variant).

### Next
- Commit docs, then the checker; step 6 (package from HEAD; load alone, solo, seed 75,
  ChecksFinder; sha256; release_checklist.md section 10 and the release notes' sums).
