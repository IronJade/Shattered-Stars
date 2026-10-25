# Handoff: so2.apworld v2 features (open world v2, any leader, craftsanity, ...)

Started 2026-10-25 by the main session. Research phase; nothing implemented yet beyond the rename.

## The user's request (2026-10-25, verbatim)

> Open World
>
> Flags for story - Flags for the story progression are gating open world movements - Look for
> the story flags and pass them all except for the first visit of a boss chamber. The boss
> chamber should trigger the next "stage" of the quest line that it is a part of. Keys hidden
> behind chest or boss checks is fine, not behind shops or drops.
>
> Can we add a options text box in the game where the at the boat location I think it may just
> be Port Hilton ship captain, but a selection to choose to go back to Cross, to Hoffman Ruins,
> or to El(if we have the keys to do El). He may already have it, but keeping those options all
> open even after returning would be important. This allows the Open-World to remain Open and
> restricts the one way trip to El behind the keys and checks needed to complete Disc 1.
>
> Options::
>
> Locations:
> Rename Protagonist - Story Route
> Add option for Selecting Any playable character as the lead. - Add code to codify that
> whatever character is the "Leader" is the sprite used by the player(in lieu of the protag)
> Rename Drops as checks - Enemy Dropsanity - Drop CAN hold progression, but don't have to.
> Options to force drops progression, and force not hold progression items.
>
> Characters and Items:
> Character Items - Any characters can drop for any Story Route, no lockouts, including Claude
> and Rena. This would allow for a potential party of Claude, Dias, Ashton, Opera, Precis,
> Bowman, Rena, Leon. Having it unlocked is nice. If no one is excluded in the list, then we
> should have a check that the 8th character item(if Rena or Noel are NOT in the party) gives
> one of them, as they are the only two healers.
> Look into adding "Craftsanity" - The end result of ANY recipe is a unique archipelago item.
> Add checkbox to remove failure chance on crafting as an option for the yaml.
>
> Items & Locations:
> Exclude Characters - Allows users to exclude certain characters from dropping as items,
> ensuring that they get their wanted 8 characters.
>
> Killer Moves - Profeciency per Killer move is considered a CHECK
>
> Character changes and logic for flags while keeping bosses is seen within this
> https://github.com/Super-Mario-RPG-Randomizer/smrpg_web_randomizer archipelago.
> https://github.com/Skuldier/so2r-archipelago.git This one claims to have Open-World where the
> story isn't forced, and the player can move about the maps uninhibited. They also seem to
> have the same thing I was adding, the select your main character from any of the characters.

Same day: "No more need for hourly checkpoints or pushing to Git, as long as workers here are
writing directly to disc." (memory commit-hourly updated; HANDOFF "Resume here" step 1 changed.)

## Done

- **The renames** (the user's own edit, 2026-10-24, a global replace): option `protagonist` →
  `story_route` ("Story Route"), `drops_as_checks` → `enemy_dropsanity` ("Enemy Dropsanity"),
  through apworld, engine, tools, tests and docs; plus a stub `StartingCharacter` option
  (options.py; not in slot_data or the seed, the port doesn't read it) and an empty
  `apworld/so2/moves.py`. Checked 2026-10-25: builds; ss_tests 99/0, ss_game_tests 129/0, smoke
  passes, apworld tests 108 passed.
- **Old seeds keep working** (2026-10-25): released 0.1.0 / 0.1.1 seeds name the options
  `protagonist` / `drops_as_checks`; after the rename the port read only the new names, so such
  a seed lost its route lock and its drop checks silently. seed.cpp now reads either name
  (test_ap: legacy names load, a bad legacy value names the old key); seed_format.md notes it.
  ss_game_tests 129/0, ss_tests 99/0.
- StoryRoute's docstring, garbled by the global replace, rewritten. Comments elsewhere that now
  say "story_route" for "protagonist" were left alone (they read acceptably).

## In progress: research (read-only agents, notes in scratchpad/openworld/)

| Topic | Notes file | Model |
|---|---|---|
| so2r-archipelago + SMRPG randomizer survey | `scratchpad/openworld/refs.md` | sonnet |
| Open world v2: story flags, quest lines, boss chambers, design | `scratchpad/openworld/story_flags.md` | opus |
| Ships (captain's menu), leader stand-in, no lockouts, healers | `scratchpad/openworld/party_ships.md` | opus |
| Craftsanity, no-fail crafting, killer-move proficiency, drops | `scratchpad/openworld/craft_moves_drops.md` (+ `craft_results.csv`) | sonnet |
| Goals: Filia's chain / flag 0x22c, Iseria Queen 561, gates | `scratchpad/openworld/goals.md` | opus |
| Killer-move proficiency increment (port omission?) — done: not missing | `docs/handoff/killer_prof.md` | opus |

- **refs: done** (2026-10-25). so2r-archipelago targets the 2023 Unity remake (SO2 R), not the
  PS1 game; its client is an unpublished BepInEx plugin, so no flag or address table is
  portable; no LICENSE. Its open world starts a new save at one fixed late hub point
  (HUB_PROGRESS 5900200, both planets and the Psynard open), towns free, one Area Key per
  dungeon (15), story bosses restarted at their triggers (19-row table); "starting character"
  only changes the lead slot (no sprite or dialogue swap); recruitment checks can't generate
  (no verified sites); drops never checks; no killer-move or craft checks. SMRPG: offline IPS
  patcher (MIT), assumed-fill solver, bosses may hold required Star Pieces, join-order shuffle;
  Mario is always the overworld sprite, so no stand-in problem. Details: refs.md.

- **craft / moves / drops: done** (2026-10-25; `craft_moves_drops.md`, `craft_results.csv`
  546 rows). Crafting: `CreationPage::decide()` (field/menu_creation.cpp:764) picks the result
  (specialty `compute()`, the success roll, a separate 30 % rare swap); `produce()` (:802) gives
  it (`inventory_give`): the check hook. No-fail = force success in `decide()` after `compute()`
  (one place for all 19 specialties). 11 of 19 specialties make enumerable items: **529
  distinct results** (+12 fail items); per-recipe counts aren't stable (Customize unbounded),
  so a check per distinct result item. Rare / member-locked tables (10 % rolls, specific
  members) and Customize chains make many hard: candidates for never-progression.
  Killer moves: learned by level only (`learn_from_table`, battle/result.cpp:88-104), **69
  moves over 8 fighters** (Claude, Bowman, Dias 10; Ashton 9; Ernest, Chisato 8; Opera,
  Precis 7). Use count / proficiency at record +0x8A + 2*move. "Learned" as the check: 69
  locations, hook at result.cpp:96-99. Proficiency milestones: the port has **no increment
  site** for killer-move proficiency (spells increment in release_spell) — a suspected port
  omission affecting normal play (Claude's 100/200-use upgrades, Dias's range at 100, charge
  levels); agent `killer_prof` (docs/handoff/killer_prof.md) is finding the original's
  increment and porting it. The user (2026-10-25, ambiguous): "I believe it only updates once
  the Cave of Trials has been completed" — checked by killer_prof and the goals agent.
  Drops: `roll_drops()` (battle/result.cpp:282): chance = odds + (avg Luck of standing members
  >> 4) - 7 of 256, doubled with reward flag 2; first success → `ap::on_enemy_drop()`. Of 89
  drop types: 11 at <= 2 %, 18 at 2.3-3.9 %, 44 at 4-8 %, 9 above 16 % (at Luck 0). Magius (27)
  and Ericodus (56) have base odds 0: their first drop needs average Luck >= 128 (not
  impossible; the agent's "unreachable" assumed Luck 0); Shout (110) >= 96, Starguarder >= 80,
  the odds-5 types >= 48. Drops already use the Final Save Point in open-world access
  (rules.py:126); progression is barred only by policy in may_hold_progression /
  open_world_progression (rules.py:73, 148). Recommendation: "force progression" via priority
  locations limited to the better-odds types; never on Luck-gated ones.

- **killer_prof: done** (2026-10-25; docs/handoff/killer_prof.md). Not a port omission: the
  original counts at `pc_begin_action` 0x80037098, store 0x800373b4 (`record + 0x7e +
  2*kind` = +0x8a + 2*move, kind = move + 6): +1 each time a move starts (MP paid, a target),
  cap 999, no flag or story condition (counts from the first battle; so not tied to the Cave
  of Trials). The port's `PartyUnit::begin_action` (battle/party_states.cpp) matches; Redux vs
  port equal (uses 0 -> 2, 997 -> 999, 998 -> 999). Only a comment and the catalog row changed.
  Levels: `pc_move_charge_level` = uses / div (move data +2) capped at cap (+4), per move;
  the per-move table is being extracted (scratchpad/killer_prof/move_levels.csv).
- **party / ships: done** (2026-10-25; `party_ships.md`). **Soft-lock in today's open world
  (released 0.1.1):** entering Lacour (214) at ch 22 step 9999 stores ch 23 (909 routine
  0x47e8); the Hilton sailor (169 actor 17, routine 0x714c, refusal test 0x71e0/0x71ec, line
  107) refuses the Cross passage at ch 23-24 and is the only way off the Lacour continent; ch
  25 is stored on map 231 after the Hoffmann trip, which needs the Map 231 and Hoffmann Passes,
  and the logic can put them on the Cross continent. Not modelled. Fix: lift the ch 23/24
  refusal in open-world seeds (port patch lines; also fixes existing 0.1.1 seeds), and/or a gate.
  Ships: world-chunk routine 0x57d8 sails by flags 16 + 17/18 (Herlie<->Hilton) / 21/22
  (Hilton<->map 231); the Hoffmann soldier (169 actor 24, 0x79c0) only ch 23-27; map 231's
  soldier returns until ch 59; El has no NPC (444's ch 27/7000 trigger = the El gate). Plan:
  step 1 patch lines (lift refusals, Hoffmann ship ch 23-59), step 2 a "where to?" choice at
  the sailor (Cross / Hoffmann / El / not now) via a new string-override hook + port script op;
  El only at ch 27/7000 with every El-gate key, checked before the chapter store.
  Leader: the walking actor is slot 0/1 from flag 1 (map.cpp); scripts address the hero by
  slot/flag 1 (G[0xc4] ~25,900 reads, flag 1 ~2,900, {name:00/01} ~9,000); every active
  member's field sprite is loaded; non-heroes lack run animations, and have 4 of 21 world-map
  ones. Plan: free-play sprite follows the Formation leader (config +0x41), fallbacks, hero on
  the world map, no name swap. Party: only `story_route_refuses` blocks Leon/Dias; hazards:
  split restores add back only expected members (maps 195, 447, 469/474, 477, 232, 567, 556),
  and a temporary join with 8 members overwrites the first reserve slot. **The hero can't be
  an item** (slot 0 at New Game, PAs keep only the hero, opening fights); the other hero can.
  Healers confirmed: only Rena and Noel (Raise Dead, Antidote: Rena only).
- **story flags: done** (2026-10-25; `story_flags.md`). **SO2 has no story flags to pass:** the
  story is one counter, chapter G[0xa0] / step G[0xa4] (363 maps test it; 173 test several
  stages); key-item flags 66-91 are never tested. Bosses fire at exact chapter/step (only
  Visseyer flag 483, Shin in Eluria Tower 587, optional bosses and the cave are flag-guarded).
  World-map entrances don't block; the real gates are water and transport (Herlie ferry,
  ships, El voyage, disc switch, North City's transporter, the Synard flag 11 at ch 41).
  Mechanisms: open transport only (story stays linear); M-A hub start at ch 60 in the late
  proof's state + bosses restaged in their chambers (so2r analogue; loses story scenes, ~90
  gifts, join scenes, old Clik, the tournament); M-B = M-A + per-chamber story view; M-C a
  chapter counter per quest line (keeps scenes, not provable over all orders; not recommended).
  Recommended: hub with boss chambers, six regional lines chained, keys only in hub-proven
  chests (258) and boss checks (~25), a new world_mode value beside today's open world. Port
  P1-P6, proofs (late proof from the hub + `--bosses`), ~40-60 agent-hours. Decisions D1-D10
  in story_flags.md section 5.

Next: merge the four into one design with the user's decisions, then implementation agents.

## The user's decisions

- **Area keys in drops** (2026-10-25): today area keys are kept out of drop locations entirely.
  Instead, an option lets them be in the enemy-dropsanity drop locations, and the player can
  opt out of it, so that no area is gated behind a specific fight's drop. Reading: default =
  keys may be in drops; the opt-out keeps them out. Separate from the drops' general
  progression mode (the request: drops CAN hold progression; options to force progression
  there, or to keep it out). Logic consequence to design: a drop holding progression needs its
  enemy type re-encounterable from the current play state, and encounter_rate > 0.
- **Craftsanity** (2026-10-25): the player still gets the crafted item; the craft also counts
  as a check.

- **The new character rules are open world only** (2026-10-25): starting character (any
  member as the lead / stand-in), characters as items with no lockouts (Claude and Rena
  included), the healer guarantee and exclude-characters apply only with `world_mode:
  open_world`. In story mode `character_items` stays exactly as it is ("works beautifully for
  Story Mode").

- **Goals** (2026-10-25): "We should have Indalecio (as it stands now, either version counts.
  We should separate that.), Indalecio Limiter Off, or Indalecio Limiter Off && Iseria Queen."
  Reading: `goal` = `indalecio` (539 or 540, today's `final_boss`, kept as an alias) /
  `indalecio_limiter_off` (540 only) / `indalecio_limiter_off_and_iseria_queen` (561 and 540).
  Known: 540 runs when flag 0x22c is set, only by Filia's event in Central City after the Final
  Save Point (Prima: needs the Clik PA with Filia before Clik's destruction: a missable chain);
  Iseria Queen 561 is the Cave of Trials' deepest boss, started without a constant encounter;
  beating Indalecio ends the game, so Iseria must fall first. Research:
  `scratchpad/openworld/goals.md` (agent).

- **Craft checks** (2026-10-25): never progression for the seed; each gives a random
  Archipelago item (any world's non-progression), and the player keeps the crafted item.
- **Dialogue** (2026-10-25, verbatim): "if that's the case where we keep it intact for open
  world, then we need a method to auto progress the dialogue (to any choice text) and an option
  for instant dialogue box completion, similar to severed chains. The start of the story is
  nearly an hour of exposition as it sits."
- **Open world model** (2026-10-25): "Both: roads now, hub later". Today's open_world keeps the
  story in chapter order and gets its roads opened (the captain's menu, the ferry refusals
  lifted, El one-way behind its keys) plus the dialogue options; the hub with boss chambers
  (story_flags.md section 4) comes later as a new world_mode value.
- **Hero** (2026-10-25): the hero stays in the party, the starting character is the leader and
  the field sprite: "This should only be in 'Open World' mode". Story mode unchanged.
- **Talk** (2026-10-25): "Yes, swap names": in open world the leader's name replaces the hero's
  in dialogue and speaker labels.
- **starting_character any** (2026-10-25): a random member (never an excluded one) joins at
  the start and leads.

- **Goals research done** (2026-10-25; scratchpad/openworld/goals.md). Filia: Clik PA (map
  112 sets G[0x2e0] only at chapter <= 14; map 105 routine 40620 sets flag 445 Claude / 446
  Rena; closed for good by Clik's destruction, ch 15; field.json wrongly marks it not
  missable) -> flag 730 (Final Save Point, 671) -> Central City PA (236 routine 34876: flag
  556 clear, ch >= 58, 445/446, 730) -> City Hall 239 routine 28468: Israfil's Tear (location
  1349893120) and **flag 556 at 30416** (the only setter) -> 685 routine 28592 picks 540 if 556.
  The way in: 671 -> 684 (door 43978493) -> 685. No cave condition anywhere (the cave only picks
  the end screen's caption on map 581: flag 920 = cave cleared). Iseria Queen 561: no script;
  the Musical song "The Evil Melody" (song 7) summons 551 + level (10 -> 561); composed with
  the Silver Trumpet (item 48, uses a Feather Pen; only from map 815 after G-Celesta, routine
  28788), played with a Conductor's Baton by a member at Musical 10 with Pitch and Sense of
  Rhythm; works at encounter_rate 0, repeatable; beating her sets no flag (the port records
  the win). Selling the trumpet before composing loses her.
- **Limiter Off** (the user, 2026-10-25): "Keep Filia's scene": the port presets flags 445 /
  446 (New Game and after every load: seed `goal.flags`), gates door 43978493 until flag 556
  ("Speak with Filia in Central City first"); the APWorld adds a "Limiter Off" event (Final Save
  Point, + Central City Pass in open world). Costs only the Clik scene.
- **Silver Trumpet** (the user, 2026-10-25): in Iseria Queen goal seeds it can't be sold until
  The Evil Melody is composed.
- Seed format for goals (goals.md): `goal.encounters` any-of stays; new `goal.all` (all must be
  won; wins in the sidecar as `goal_won`), door conditions `flags` / `won`, `goal.flags`
  (applied at New Game and after every load), seed `version` 2 when any is used. The AND goal:
  `goal: {all: [561, 540]}` + the flags + the door gated on 556 and 561 won; an "Iseria Queen"
  event in the Cave of Trials region (Final Save Point, + Cave of Trials and Fun City Passes in
  open world); `cave_of_trials` not forced on.
- **Killer-move levels** (scratchpad/killer_prof/move_levels.csv): 69 learn-table moves (+8
  with data but no learn row: Phase Gun, Precis x3, Opera x3, Ashton's Tri-Ace, a cave
  treasure). Level = uses / div capped; most moves have 1-3 levels (L1 median 100 uses, max
  median 200, up to 999); fine-counting moves: Secret Medicine 999, Healing Star 249, Photon
  Prison 99, Pillory 29, Whirlwind Fist 20, a few 5-9. Ashton's Dead Triangle has none.

- **Killer-move checks** (the user, 2026-10-25): "Every proficiency level" (one location per
  level of each move; fine-counting moves capped at 3 milestones; ~130). Progression:
  "Make these options": `never` (default) / `hero` (the route hero's moves only) / `any` (where
  the logic guarantees the fighter: the hero or a character-item member). Sent to apworld2.

## The plan (2026-10-25)

Wave 1 (started 2026-10-25; agents listed in scratchpad/agents.txt):
- **ships** (port; log docs/handoff/ships.md): open-world seeds only. Lift the Hilton
  sailor's ch 23/24 refusal (the soft-lock; existing 0.1.1 seeds get it from the port) and the
  ch 18 step >= 6000 one; Hoffmann ship ch 23-59 (its first trip still stores ch 24); the
  captain's "where to?" choice (Cross / Hoffmann Ruins / El / Not now): Hoffmann from ch 23,
  El only when the story's voyage is due (ch 27 step 7000) and every El-gate key is held (same
  effect as room 444's routine; keys checked before the chapter store), never after the disc
  switch; a landing-key pre-check at the NPC.
  **Built 2026-10-25** (docs/handoff/ships.md): 12 port-added patch lines in open-world seeds +
  port-only table A ops 0x70-0x72 + a menu-text hook; ss_game_tests 135/0 (ap_harbour), ss_tests
  99/0, headless demo (Cross at ch 23 sails to Herlie, story mode refuses, El refused without the
  passes and sailing to 548 with them, Hoffmann / Herlie landing keys checked first), smoke
  93/0 with 3 new harbour cases. Front Line -> Port Hilton walk at ch 27/7000: possible (exits + terrain). Proposal for
  apworld2 in the log (Hoffmann via the teleporter, needs a proof rerun and version coupling).
- **dialogue** (port; log docs/handoff/dialogue.md): two settings, default off, any game
  mode: instant text (a box completes at once) and auto-advance (advances after a short wait,
  stops at choice windows, never answers them). Settings, not seed options.
- **apworld2** (apworld; log docs/handoff/apworld2.md): so2.apworld 0.2.0. Options and logic:
  goal (three values, after the goals research), enemy_dropsanity's progression mode (allow /
  force / never; never on Luck-gated types; only types re-encounterable from the current play
  state), area keys in drops (opt-out toggle, default on), craftsanity (529 results, never
  progression) + no-fail crafting, party v2 for open world (exclude_characters,
  starting_character with "any" = random, the other hero and Noel as items, the healer
  guarantee, party <= 8 with the hero), killer-move checks (after killer_prof). It writes the
  seed contract for every new field into docs/ap/seed_format.md first.
- Running research/fixes: **goals** (scratchpad/openworld/goals.md), **killer_prof**
  (docs/handoff/killer_prof.md).

- **dialogue: done** (2026-10-25; docs/handoff/dialogue.md). `instant_text`, `auto_advance`,
  `auto_advance_delay` (ms, default 1000), off by default, every mode; setup's new Gameplay
  page; env overrides SS_INSTANT_TEXT / SS_AUTO_ADVANCE (headless ignores config.ini for them).
  Auto-advance goes through the real confirm path, never with a choice or a script-requested
  menu up. Opening (Claude, map 7 to map 12): 24075 vblanks pressing 1 s after each page ->
  20763 instant + auto 1 s -> 18393 at 0.5 s, hands-free; auto runs byte-identical to pressing on
  the same frame. build-dialogue: ss_tests 99/0, ss_game_tests 135/0, ref_regress quick 28/28,
  smoke 85/1 (ap_p4_tele_open: expected 3 patches; ships has since updated it to 15: smoke 93/0). Left: the battle result screen's wait_button (battle/result.cpp; hook in the log) and the
  post-battle equipment report aren't covered (wave 2).
- Roll-over (2026-10-25): ships (446k) and apworld2 (487k) asked to update their logs and stop;
  fresh agents continue from docs/handoff/ships.md and apworld2.md.

- **ships: done** (2026-10-25; docs/handoff/ships.md). Open-world seeds (from world_mode, no new
  field): 12 patch lines on maps 169 / 144 calling port script ops table A 0x70-0x72 (patched
  words checked on NA/EU/FR/DE; DE's map 169 offsets shift +84 from 0x7c70), a menu-text hook.
  The Hilton sailor no longer refuses at ch 23-24 or ch 18 step >= 6000 (0.1.1 seeds fixed by
  the port alone); the Hoffmann soldier ch 23-59; the captain's menu "Where to, matey?" Cross /
  Hoffmann Ruins (ch 23-59) / El (ch 27 step 7000, disc 1; listed always then, the El gate's
  banners when keys are missing; keys checked before any store) / Not now; landing-key
  pre-checks (captain, Hoffmann soldier, Herlie's sailor). Front Line -> Port Hilton at ch
  27/7000: walkable (exits to the world map shown headless; one landmass per ap_walkmap).
  build-ships: ss_game_tests 135/0 (ap_harbour new), ss_tests 99/0, smoke 93/0 (3 new cases;
  ap_p4_tele_open now 15 lines). Demo scratchpad/ships/demo/run.sh. Open: the menu text is
  English in every release; choosing Hoffmann at ch 23 from the captain skips the soldier's
  first-time lines (their flag has no other reader). Proposal (optional, apworld): the ship at
  ch 23-59 lets the teleporter's party reach 231 / Hoffmann Ruins; prove it (teleporter proof
  seed open_world, 231/478 out of TELEPORTER_ISLANDS, ferry in ap_walkmap) and tie it to a
  version that needs this port.

- **apworld2 (first agent): items 1-7 built** (2026-10-25; docs/handoff/apworld2.md; stopped at
  ~490k). World tests 235 passed (19,700 subtests). Seed contract in seed_format.md "v2 fields"
  (version 2 only when an old port would silently ignore something). drop_progression allow /
  force / never: progression only where the enemy is met with no story condition on maps the
  late / teleporter / voyage proofs reached, never at encounter_rate 0, never below 1/64 per kill
  at Luck 0 (21 types); force = >= 20/256 (25 types, 22 safe), done by `fill_hook` filling those
  drops first (AP priority locations raised FillError on 5/5 solo open-world seeds).
  area_keys_in_drops (default on; off = no SO2 area key in this world's drops, both modes).
  Craftsanity 529 (crafts.py), member-only results filler-only unless the member is sure,
  left out if they can never join; Authoring's rare books need Leon; Publishing not covered.
  Party v2: exclude_characters, Claude / Rena / Noel as items (3 new filler-only join sites),
  cap 8, healer guarantee; starting_character default `hero` (nobody extra; "any" as default
  would make every open-world seed version 2). Goals as decided (door 43978493 conditions, both
  modes). Killer moves: moves.py, 133 locations (fine-counting: 100/200/300 uses); access: learn
  level <= 30 from the disc switch, higher from the Final Save Point; progression never / hero /
  any. Batch: 96/96 generated; checker flagged 18: 11 checker gaps (fixed), 7 real (area keys in
  story-mode drops with the option off; fixed, unconfirmed). Successor started for the batch
  rerun, general tests, apworld.md, the player guide.
- **apworld2: done** (2026-10-25, successor; docs/handoff/apworld2.md "Session 3"). Batch 96/96,
  192 .apso2, check.py 0 errors (the area-key fix confirmed); the 16 all-story seeds with
  drop_progression never identical to 0.1.1 but `options`; world tests 235, AP general 379.
  docs/ap/apworld.md (new sections Enemy drops / Craftsanity / Killer moves / Party v2 / Goals,
  Options, Testing, Changes 0.2.0) and the player guide updated. Publishing's 24 books added to
  craftsanity (553 crafts). Goal banner now "Talk to Filia in Central City first" (coordinator).

Wave 2 (started 2026-10-25: agents party, leader, hooks; logs docs/handoff/party.md, leader.md,
hooks.md; builds engine/build-party, -leader, -hooks): port party v2 (no lockouts in open world, received
members never overwritten, split windows, leader sprite + name swap; party_ships.md B/C);
port craft hooks (check on produce, no-fail in decide), goal gates (flags, `won`, `goal.all`,
`goal.flags`, the trumpet lock), killer-move level checks, auto-advance on the battle result
screen and equipment report.

- **leader: done** (2026-10-25; docs/handoff/leader.md). Open-world seeds only. New
  field/leader.{h,cpp} + hooks in actor_field.cpp (fieldactor_draw) and script_ext.cpp
  (`leader_message_text` for the message / default-window / notice ops). The walking actor
  stays the hero (slot, model, animation, collision); only its drawing swaps to the Formation
  leader's sprite (config +0x41), runs show the leader's walk, a missing pack is read from the
  disc for drawing. `{name:00}` (Claude's game) / `{name:01}` (Rena's) read as the leader in
  text and speaker labels. Sprite and names switch together back to the hero on world maps,
  in PAs, under script sprites (bunny, disguise), and for the rest of a scene once the leader's
  own actor appears, a message names the leader, or the scene asks a hero-only gesture.
  Same state on/off (SS_LEADER_STANDIN=0): byte-identical dumps and RNG signatures (opening with
  Ashton / Noel, the final scene 685, map 9, Rena's Arlia). build-leader: ss_tests 99/0,
  ss_game_tests 155/0 (7 leader_* new), smoke 120/0 (ap_leader_ow / ap_leader_story new).
  Open: afterimage ghosts show the hero; a long scene keeps the hero (opening maps 5-12); name
  length changes typing time (as renaming does in the original).
- Contract fixes (2026-10-25): goal banner "Talk to Filia in Central City first" (248 -> 225 px);
  killer_move `move` 1-15 (Mirror Slice is 11). Generators moved: tools/ap_make_crafts.py,
  tools/ap_make_moves.py, inputs in docs/ap/data/.

- **hooks: done** (2026-10-25; docs/handoff/hooks.md). Seed versions 1-2 (3+ refused); goal.all
  with sidecar `goal_won` (outcome 0, merged on load), goal.flags (first field frame after New
  Game, every load, `--map` starts), door conditions flags / won / banners (trigger and transfer,
  every world mode), Silver Trumpet off sell lists while flag 53 clear (goal.all has 561);
  craft checks in CreationPage::produce() (12 specialties, common_kind, success or failure);
  no-fail in decide() + Art's Creation1::compute (picks only after a successful roll); killer-move
  level checks at the next free play after each battle / load (party slots only); auto-advance on
  battle/result.cpp wait_button and field/post_battle.cpp's equipment report. build-hooks:
  ss_tests 99/0, ss_game_tests 149/0 (7 ap_v2_*), smoke 120/0, ap_e2e_so2.sh 37/37 (a third,
  version-2 session "Dias": 540 alone doesn't send the goal, 561 completes it). Open: (the Art
  creation plan's 4-byte difference was a stale reference, see below); L1 never fired a killer move in a
  console-started battle (not investigated); one smoke run concurrent with e2e failed ap_load /
  ap_p4_world once (passed alone and in the final sequential run).

- **party: done** (2026-10-25; docs/handoff/party.md, scratchpad/party/splits.md). seed fields
  party_rules ("story" / "open"; else the seed fails) and starting_character (name, id, or
  null / "hero" / "" / 0). Under "open" (ap.cpp `on_party_add` rewritten): only the hero (both
  under "any"), received members and the starting character join; listed sites still check (the
  3 new ones too); no lockouts; no story or recruited member's slot is overwritten (a full party
  takes another member's reserve slot or the join waits); new partings for good: Leon in Rena's
  game (234 0x74c8), Noel's "stay" choice (417 0x7db8). Starting character queued first at the
  first free play, made Formation leader, counts as received. Sidecar: `temporary`, `stash`.
  `kSplitWindows`: every restore re-adds exactly whom its split took out; non-received members
  let in only at Shingo Forest (map 12, ch 4 step 150, Rena's game: Claude) and the tournament
  (ch 19: Dias; Claude in Rena's game), removed at the window's end. Contract corrected in
  seed_format.md (Linga, Front Line, ship, Fields need no temporary member; Rena alone at 439 /
  229 if Dias / Leon weren't received: no fights there). build-party: ss_game_tests 156/0,
  ss_tests 99/0, smoke 120/0 (4 new). Open: the tournament's own restore (469) not reached
  headless (flag logic read; the window's end re-adds); front_c (444, Claude's game) release
  not reached; members a split doesn't list stay in (a received Leon in the tournament's 1-on-1
  fights); e2e not run (client path untouched).
- **Integration** (2026-10-25): the combined tree in engine/build: ss_tests 99/0, ss_game_tests
  156/0 (129 + dialogue 5, ships 1, hooks 7, leader 7, party 7), smoke 120/0
  (scratchpad/integration.log); ref_regress quick, apworld tests and e2e running
  (scratchpad/integration2.log).
- **The Art creation 4-byte diff: not a regression** (2026-10-25, agent artregress,
  docs/handoff/artregress.md). The Redux reference scratchpad/menu/artr was recorded (10-22
  09:51) with the old plan that closes the menu and dumps at frame 1750; at 17:31 crplan.py
  rewrote every creation plan to keep the menu open (dump at END + 12) and runredux.sh
  re-recorded ten cases but not Art. The 4 bytes are what closing the menu changes in the
  original's field_update 0x8004c334 (music id back to the field track; the rescan flag
  0x80076278 -> party_auto_equip / stat_calc_auto_equip, record 2 +0xa 0x4d -> 0x45, and
  inventory_clear_new_flags 0x80030f84 clearing the new-item bit). A fresh Redux recording with
  the current plan matches the port (ad0fb2f, dc37aed and the combined tree: 0 diffs). The fresh
  recording is now scratchpad/menu/artr (the old one kept as artr_close1750).

Later: the hub (story_flags.md section 4, decisions D1-D10 to re-ask then).

## Open questions for the user (to ask with the design)

- Are NPC gift sites allowed to hold area keys ("keys behind chest or boss checks is fine, not
  behind shops or drops")? Default meanwhile: as today (the 9 proven gifts may).
- Killer moves: which proficiency milestones count (after the mechanics are known).

## Rules given to the agents

Read-only on engine/apworld/tools/docs; notes in scratchpad/openworld/; headless `-cli` runs
only under tools/memcap.py (4 GB), at most 2 game processes each; never the PCSX-Redux GUI or a
config-saving flag; on a refusal stop and report the exact command; enhance, never degrade
(a normal game plays as the original; AP behaviour behind seed options); roll over at ~400k.
