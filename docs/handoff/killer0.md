# Handoff: killer0 (option (b): no killer-move checks with encounter_rate 0) - STOPPED, SUPERSEDED

Agent "killer0", 2026-10-27. **Stopped by the coordinator mid-task**: the user moved away from option
(b) to a "hell seed" version (killer-move checks stay at encounter_rate 0, arena-gated, the Fun City
arena's fee waived on such seeds). A fresh agent builds that. Nothing of (b) was committed; the work
is kept as a patch. main's apworld/ and tools/ are back at HEAD (25fb5d3).

## The superseded decision (2026-10-27, verbatim)
"b) Leave killer-move checks out of the world when the encounter rate is 0."
Context: the last known 0.2.1 failure, seed 75's mix + `encounter_rate: 0` (docs/handoff/skilllogic.md
"Open for the user"; the release notes' TO CONFIRM limitation).

## Rules given
- Touch only apworld/so2/, tools/ap_batch*, docs/ap/, docs/release_notes/ and this file. Not
  engine/ ("queuebypass" changes the port on main), never scratchpad/wt/party.
- Commits on main after each verified piece, docs first, staged by path; message ends with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never push, tag, publish.
- Scratch in scratchpad/killer0/. Low load: pytest -n 4, one heavy job at a time, Python
  generation under prlimit --as 8 GB. Kill only by exact PID.

## What (b) changed (uncommitted; scratchpad/killer0/b_wip.patch, 280 lines, `git apply --check` clean on 25fb5d3)
- locations.py: new `killer_moves_on(options)` = killer_move_checks and encounter_rate > 0;
  `choose` and hub.py's `choose` add the levels only then; `choose_killer_moves(party)` lost its
  encounter_rate parameter (EXCLUDED only for "maybe" members).
- rules.py `killer_rule`: the `encounter_rate == 0` no-progression branch dropped (unreachable).
- world.py: warning in generate_early ("killer_move_checks needs random battles; ignored with
  encounter_rate 0"); `seed_version` and slot data `killer_move_checks` use killer_moves_on (the
  effective value, as slot data already does for expel_teleporter / starting_character / hub
  character_items).
- tests: test_v2.py `TestKillerNoEncounters` rewritten + `NoKillerChecks` mixin (no levels, no
  progression move / character items, slot data false, version 1 in story mode, the warning via
  `assertLogs` around `self.world_setup()`), `TestKillerNoEncountersOpenSpecial` (open world,
  character items, special_only), `TestKillerNoEncountersHub`, `TestKillerLowEncounters`
  (encounter_rate 1 keeps the levels; held > 0). test_skillsanity.py
  `TestSkillsanitySeed75NoEncounters` = `dict(TestSkillsanitySeed75.options, encounter_rate=0)`.
- tools/ap_batch_check.py: `check_killer_option` (error if a seed says killer_move_checks on with
  encounter_rate 0), called from `check()`.
- Not started: docs, batches, identity, the package.

## Results with (b) applied (2026-10-27)
- World suite (-n 4): **559 passed**, 39980 subtests (scratchpad/killer0/t/world1.log; HEAD: 537).
- `TestSkillsanitySeed75NoEncounters` on HEAD's world code (world files swapped in, then restored):
  test_fill FAILS (FillError), test_seed fails: a working reproducer of the enc-0 failure
  (t/old.log). The hell-seed version can reuse this class as its reproducer.
- Fill rates (scratchpad/killer0/fill: run.sh, fillrate3.py, summary.txt; 100 solo seeds each):
  seed 75 + enc 0, + story_route claude, + killer_move_progression any: **0/100 FillError each**,
  all filled seeds accessible and beatable; 0 killer locations, 0 progression moves / characters.

## Port check (by reading; holds for any version)
- engine/src/game/ap never reads `options.killer_move_checks` / `killer_move_progression` (no hit
  in engine/src); seed_format.md marks both "reference". The checks are the seed's `killer_move`
  locations: seed.cpp parses them; ap.cpp `scan_killer_moves` loops over them at free play after
  a battle or a load (`killer_scan_pending`), compares record + 0x8a + 2 * move with `uses`, and
  queues an own item in `pending`. No such location = nothing scanned. Random encounters off:
  ap.cpp `random_encounters_off()` (encounter_rate 0), field_program.cpp forces the rate to 0
  (the arena's fights: not checked).

## How killer moves, move progression and encounter_rate interact (HEAD 25fb5d3)
- locations.choose_killer_moves: with encounter_rate 0 every level is EXCLUDED; "maybe" members'
  too. rules.killer_rule: no progression when not allowed by killer_move_progression, when
  encounter_rate 0, or EXCLUDED. Access: member's item (if progression_characters), the stage
  (learn level <= 30: disc switch; else Final Save Point; hub: start / Fienal open), and under
  skillsanity special the move item (skillsanity.move_need).
- **The failure's cause:** skillsanity.plan makes the move of every chosen killer level progression
  (`world.skill_needs`) regardless of EXCLUDED / encounter_rate; with encounter_rate 0 no guild
  check, learn level or killer level may hold progression, so in seed 75's mix (open world, Rena,
  expel_progression / boss_checks off, special_only, killer hero) ~60 progression move items have
  too few places. Likewise world.generate_early's `progression_characters` takes every pool member
  with a chosen killer level under killer_move_progression any / hero, even at encounter_rate 0
  (their items become progression with nothing needing them).
- So a hell-seed version that keeps the levels at encounter_rate 0 must either let some of them
  hold progression (an arena-gated rule: then the move / character progression is needed and has
  room), or stop skillsanity.plan / progression_characters from counting levels that can't hold
  progression. Skillsanity's guild checks (Fol) and learn levels (EXP) are also battle-gated
  (`battles = encounter_rate > 0` in guild_rule / learn_rule / learn_characters) and drops too
  (rules.drop_rule, hub.py): arena gating may matter for them as well.
- No seed in the current 0.2.1 batches (scratchpad/skilllogic/batch: sk_full, sk_hub, solo) rolls
  encounter_rate 0 (the YAMLs use {100: 1, random: 1}), so a change confined to encounter_rate 0
  should leave all 288 seeds byte-identical (identity: scratchpad/skilllogic/batch/identity.py,
  run as `python3 identity.py OLD NEW` with the per-set rc files).

## Reusable for the hell-seed version
- scratchpad/killer0/fill/fillrate3.py (fill + test_fill's sphere / beatable check + counts of
  killer locations, progression moves and characters) and run.sh (seed 75's mix as S75).
- The reproducer class and the NoKillerChecks-style assertions in b_wip.patch (adapt: levels kept).
- The port reading above.

## Commits
None for (b). This handoff is the only file committed by killer0.
