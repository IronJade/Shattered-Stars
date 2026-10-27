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
3. [ ] Port proofs (headless, engine/build-skilllogic): a key and a character at a guild check, a
   key at a learn point, from a hand-edited .apso2.
4. [ ] Batches story_open / hub / solo_noexpel 96/96, checker 0 errors (extend ap_batch_check.py if
   it asserts these never hold progression). Identity: seeds without skillsanity byte-identical to
   scratchpad/ncpfix/v021; list the changed seeds (each must have skillsanity on). AP general suite
   and the scoped AP_TEST_WORLDS=so2 run.
5. [ ] Docs: apworld.md (skillsanity, logic, 0.2.1 changelog final), the player guide, options.py
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

### Next
- Step 3 (port proofs), then 4-6.
