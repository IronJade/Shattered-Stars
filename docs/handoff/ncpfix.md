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
3. [ ] Spot counts (scratchpad/aptests/ncp_spots.py) and solo fill rates (fillrate.py, 100 seeds x
   story_route claude / rena / any x teleporter on / off, expel_progression off).
4. [ ] Solo expel-off players set: tools/ap_batch_players/solo_noexpel/Claude.yaml (made: one SO2
   player, expel_progression "false", world_mode open_world 3 : story 1 : hub 1, the rest as
   story_open's Claude plus hub's hub_start / fienal_requirement), tools/ap_batch.sh knows it.
   To run: 96 seeds.
5. [ ] Byte-identical check vs scratchpad/aptests/base/manifest.txt (APWORLD_VERSION still 0.2.0).
6. [ ] Version 0.2.1, docs, release notes, package.
7. [ ] Suites (world, general, scoped) and the batches through the checker at 0.2.1.
