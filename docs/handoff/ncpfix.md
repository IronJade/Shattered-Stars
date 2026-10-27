# Handoff: ncpfix (North City Pass at the start with expel_progression off; so2.apworld 0.2.1)

Agent "ncpfix", started 2026-10-26. The bug: docs/handoff/aptests.md "World bug: North City
Pass has no legal spot".

## The user's decision (2026-10-26)
"North City Pass fix is the way to go for Expel prog off games." So: open world gives the North
City Pass at the start whenever `expel_progression` is off, as it already does with the
teleporter off. Start condition: **not (expel_teleporter and expel_progression)**.

## Rules given
- Touch only apworld/so2/ (world code, tests, docs, archipelago.json), tools/ap_batch*.{sh,py} and
  its players sets, docs/ap/*.md, docs/release_notes/, this file. Nothing in engine/.
- Never touch scratchpad/wt/party (another agent's worktree, branch party-bench).
- Commits on main after each verified piece, docs first, staged by path (never `git add -A` / `.` /
  `-a`); message ends with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.
  Never push, tag or publish (the user releases).
- Scratch in scratchpad/ncpfix/. Cap output. Long jobs in the background with deadlines, under
  prlimit; at most 2 heavy processes at once; kill only by exact PID.
- A denied tool call: don't retry or work around; write the blocked step here and stop.
- Noel's House Pass: if a fill fails or a key keeps one spot that a small option change could
  close, stop and report with numbers; don't widen the fix.
- Roll over at ~400k context.
- When verified: delete scratchpad/aptests/base and scratchpad/aptests/after (Phase A baselines).

## Plan / status
1. [x] The fix (2026-10-26 ~22:10): world.create_items (`later = teleporter and expel_progression`;
   NCP at the start unless later), items.py START_KEYS comment; test_open_world.py
   test_keys_and_start's rule, the three expectedFailures and the unused `import unittest` off;
   tools/ap_batch_check.py's starting-keys rule. Docs: docs/ap/apworld.md ("Starting keys",
   Testing's reproducer note, the release workflow's solo_noexpel), the player guide (area keys,
   "Where does Expel stand?", the expel_teleporter / expel_progression rows), options.py
   docstrings (WorldMode, ExpelProgression, ExpelTeleporter).
2. [x] World tests: **433 passed** (was 430 + 3 xfailed), 30126-30142 subtests (the count varies
   run to run: WorldTestBase's seeds are random); TestOpenWorldNoExpelProgression 16 passed.
3. [x] Spot counts and solo fill rates, 2026-10-26 ~22:30 (scratchpad/ncpfix/fill: run.sh,
   spots_<route>_tele_<on>.txt, fill_*.txt). Open world, expel_progression off, the rest default:
   **0/100 FillError** for each of story_route claude / rena / any x teleporter on / off (600
   solo fills). Spots, the same in all six: **Noel's House Pass 1** (the Synard boss, "Synard
   Plant: Synard boss"), Red Crystal Pass 4 (Noel's House's 3 chests + the Synard boss), Salva
   Pass 7, Central City Pass 13, Power Pass 25, the rest more.
   **STOP CONDITION MET (the rules: report, don't widen the fix).** North City has no chests (49
   shop slots, never progression, and the Synard boss), so before the Noel's House Pass the only
   location of this world that may hold it is the Synard boss, which exists only with
   `boss_checks` (default on). Probes (scratchpad/ncpfix/probe: probe.sh, summary.txt,
   fillrate2.py / spots2.py take JSON option values; story_route any unless noted, 30 seeds):
   - boss_checks off, expel_progression off, teleporter on: Noel's House Pass **0 spots, 30/30
     FillError** (rena too: 30/30); with enemy_dropsanity first_drop too: 30/30 (drops add none).
   - boss_checks off, expel_progression off, teleporter off: 0 spots, 30/30 FillError.
   - boss_checks off, expel_progression ON, teleporter off: 0 spots, 30/30 FillError -- **already
     0.2.0's** (the teleporter-off case had the North City Pass at the start already).
   - boss_checks off, expel_progression on, teleporter on: 74 spots (Expel), 0/30.
   - expel_progression on, teleporter off, bosses on: 7 spots (the Expel story bosses + Synard).
   - The real generator (Generate.py, solo, seeds 1-2 / 1-3): boss_checks "false" with
     expel_progression "false" -> rc 1 (FillError: Noel's House Pass), with expel_teleporter
     "false" -> rc 1; `exclude_locations: ["Synard Plant: Synard boss"]` with expel_progression
     "false" -> rc 1 on seeds 1-3 (fillrate.py doesn't apply exclude_locations: Main.py does).
   With boss_checks off the next scarcest key is the Red Crystal Pass, 3 spots (Noel's House's
   chests).
4. [x] Solo expel-off players set: tools/ap_batch_players/solo_noexpel/Claude.yaml (one SO2
   player, expel_progression "false", world_mode open_world 3 : story 1 : hub 1, the rest as
   story_open's Claude plus hub's hub_start / fienal_requirement); tools/ap_batch.sh knows it.
   Run 2026-10-26 ~22:50 (`tools/ap_batch.sh solo_noexpel scratchpad/ncpfix/solo 1 96`):
   **85/96 generate, 11 FillError, all "Noel's House Pass"** (seed 75 more keys too); the checker
   0 errors on the 85 files. Rolled options per seed (scratchpad/ncpfix/roll.py: Generate.main's
   erargs, no fill; solo_rolls.txt; 0 mismatches with the 85 generated): the 11 are all open world
   with boss_checks off (16 such seeds: 9 of 9 without killer-move progression failed, 2 of 7
   with it); open world with bosses on 37/37, story 21/21, hub 22/22 generate.
5. [x] Identity vs scratchpad/aptests/base (APWORLD_VERSION still 0.2.0; outputs
   scratchpad/ncpfix/id/{sk_full,sk_hub}, `scratchpad/ncpfix/compare.py ../aptests/base id` ->
   compare.txt): both sets 96/96 generate, the checker 0 errors (192 + 192 files), the seed lines
   identical. **140 of 192 seeds byte-identical (every .apso2 and spoiler); 52 changed** (story_open
   45: 6 7 8 9 11 13 14 18 21 23 24 25 26 34 37 38 40 42 43 44 46 48 49 50 51 52 53 54 56 59 61 62
   64 65 68 70 72 74 76 77 78 79 84 93 94; hub 7: 8 15 46 50 73 78 90), **0 unexpected**: each
   changed seed has an SO2 player in open world with the teleporter on and expel_progression off
   (57 slots: Claude 22, Rena 25, both 5 seeds), and every such seed changed. In each: every
   .apso2's options, locations and doors identical; that player's start = base + North City Pass
   (the spoiler's Starting Items too), the other players' identical; the multiworld's location set
   identical; an unaffected player's placed items identical as a multiset; the affected player's
   placed keys / characters / skills / moves = base minus the North City Pass, and only
   filler-kind items (game items, Fol, traps) otherwise differ (fewer keys replace filler, so the
   pool's filler sample and the fill's order differ: most placements move).
6. [ ] Not started (version 0.2.1, release notes, package): after step 8.
7. [ ] Not started (suites at 0.2.1, batches at 0.2.1).
- The Phase A baselines scratchpad/aptests/base and after are **kept**: a wider fix needs the same
  identity check again.

## Noel's House Pass: the user's decision (2026-10-26, verbatim, via the coordinator)
"Follow the North City Pass for Noels House pass" (option a): open world gives the Noel's House
Pass at the start under the same condition, unless the teleporter and expel_progression are
both on. Coordinator's task (same rules): implement it as the North City Pass change (docs first,
then the world change); Red Crystal Pass is then the tightest key: re-count spots, measure the
solo fill rate >= 100 seeds per story_route x teleporter on / off x boss_checks on / off (and
killer-move progression if it changes the spot count), expel_progression off; all must generate,
else stop and report. Then finish 0.2.1 (solo_noexpel count, identity vs the base, version,
changelog, docs/release_notes/0.2.1.md draft, package in dist/, world tests, general suite,
scoped run, both batches through the checker), then delete scratchpad/aptests/base and after.

## Step 8 (Noel's House Pass)
8. [~] Docs: apworld.md "Starting keys" and Testing's note, the player guide (area keys, "Where
   does Expel stand?", the two option rows), options.py docstrings. In progress: world.py,
   items.py comment, test_keys_and_start, ap_batch_check.py.

## Next
- Re-count spots, fill rates (boss_checks on / off), solo_noexpel, identity, then steps 6-7:
  APWORLD_VERSION / archipelago.json 0.2.1, docs/ap/apworld.md changelog,
  docs/release_notes/0.2.1.md (draft), tools/build_apworld.sh dist/apworld-0.2.1, the suites,
  the batches at 0.2.1 (identity modulo the version string: compare.py --ignore-version), then
  delete scratchpad/aptests/base and after.

## How to verify
- `SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 worlds/so2/test` in
  scratchpad/ap/Archipelago.
- scratchpad/ncpfix/fill/run.sh (spots + fill rates), probe.sh (boss_checks / exclusion probes),
  id/run.sh + compare.py (identity), `tools/ap_batch.sh solo_noexpel OUT 1 96`.
