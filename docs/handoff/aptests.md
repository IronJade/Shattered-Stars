# Handoff: aptests (Phase A, the APWorld's tests to Archipelago's testing guide)

Agent "aptests", started 2026-10-26 ~20:50. Plan: docs/plans/next.md "Phase A".

## The user's words
- 2026-10-25: "After we get everything settled, we need to follow this guide
  https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/tests.md".
- 2026-10-26: "Yes, do the apworld test work running as well" (alongside another agent's port work).

## Rules given
- TESTS ONLY: the world's behaviour must not change. Edit only apworld/so2/test/, tools/ (the
  release workflow script), docs/ap/apworld.md (Testing), docs/ap/release_checklist.md,
  docs/plans/next.md (Phase A status only), this file. Never apworld/so2/*.py outside test/.
- The user's options.py docstring and the Salva Drift port fix were committed by the coordinator
  (c445867, 2026-10-26): nothing of the user's left in the tree; still stage only my own files.
- A denied tool call: don't retry; write the blocked step here and stop with a message.
- The session's cwd may start in scratchpad/wt/party (the party-bench worktree, another agent's):
  never touch it; my work is by absolute path in the main checkout (branch main).
- Stage own files by path only (no `git add -A` / `.` / `commit -a`). Commit on main after each
  verified piece, docs first; message ends with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never push.
- If a new test exposes a logic bug: stop and report, don't fix the world.
- Scratch in scratchpad/aptests/. Memory cap (prlimit) on Python; at most 2 heavy processes at once;
  long jobs in the background with deadlines; never `pgrep -f` on own pattern; kill by exact PID.
- Don't update scratchpad/ap/Archipelago (no git pull) without a written reason.
- Coming later (not mine): the `bench` option (docs/handoff/bench.md "The user's answers"): make
  the party tests easy to extend (a mix class), don't implement it.
- Roll over at ~400k tokens.

## The guide
Live page fetched 2026-10-26 (scratchpad/aptests/tests_live.md): identical to the local copy
scratchpad/ap/Archipelago/docs/tests.md (commit 54803be). No changes since.

## Baseline (2026-10-26 ~21:00, before any change; machine load ~20 on 20 cores)
- World tests (`pytest -q -n 6 --durations=0 worlds/so2/test`): **380 passed, 29338 subtests,
  9.53 s**; slowest single test 0.55 s (TestHubAnyLean::test_all_state_can_reach_everything);
  302 calls >= 5 ms, sum 27.4 s. Nothing over a second. Log: scratchpad/aptests/base_world.log.
- test/general (`-n 4`): **379 passed, 117765 subtests, 129 s** (base_general.log).
- Scoped: `AP_TEST_WORLDS=so2 pytest -n 4 test/general worlds/so2`: 433 passed (34608 subtests);
  from the root without paths: 434 passed + 14 errors (test/webhost: no flask in ap-venv), the
  434th is test/multiworld's TestTwoPlayerMulti{so2} (base_scoped*.log).
- Batches through the OLD scratch script (scratchpad/apworld2/batch/run_batch.sh + check.py;
  wrapper scratchpad/aptests/base_batches.sh): players_sk -> base/sk_full, players_hub_sk ->
  base/sk_hub, seeds 1-96: 96/96 exit 0 each, 2 .apso2 each; check.py 0 errors on 192 + 192
  (9924 / 8673 progression). Hash manifest of the 384 .apso2 + 192 spoilers:
  scratchpad/aptests/base/manifest.txt.

## Found so far
- test_hub.py's doors_json() built the repo root lexically from `__file__` (abspath), which in
  the dev install is scratchpad/ap/Archipelago/worlds/so2/... (the symlink), so it looked for
  Archipelago/docs/ap/doors.json, found none and **skipped validate_doors in every hub seed
  test**. test_world.py's version (`..` joins, resolved by the kernel through the link) worked.
  The shared helper (bases.doors_json, realpath) fixes it: the hub tests now validate.

## World bug: North City Pass has no legal spot (found 2026-10-26; the user decides the fix)
**Open world with `expel_progression: false` and the teleporter on (`expel_teleporter`, default
on) can't be generated unless another game supplies locations.** North City Pass has no legal
location: it's a pool key (a starting key only with the teleporter off), the first Energy Nede
stage (data.STAGES, chapter 37-38) needs it, `expel_progression` off keeps everything but our own
Expel-story keys out of Expel (rules.open_world_progression "expel keys"; North City Pass is a Nede
key, not in items.EXPEL_STORY_KEYS), and nothing on Energy Nede is reachable before it (the
teleporter's old man needs stage 16, the Synard).
- **Reproducer, the real generator:** scratchpad/aptests/solo/players/Claude.yaml
  (`world_mode: open_world`, `expel_progression: "false"`, everything else default), alone:
  `cd scratchpad/ap/Archipelago && SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python Generate.py
  --player_files_path ../../aptests/solo/players --outputpath ../../aptests/solo/out --seed 1`
  (also 2, 3) -> rc 1, `FillError: No more spots to place 1 items. Remaining locations are
  invalid. ... Unplaced items: North City Pass (Player 1)`. Two SO2 players with these options and
  no other game (solo/players2): seeds 1 and 2 also FillError (North City Pass, Noel's House Pass
  and the other player's Nede keys).
- **Reproducer, the tests:** test_open_world.py `TestOpenWorldNoExpelProgression` (options
  open_world, expel_progression off, story_route rena): WorldTestBase's own test_fill raises the
  FillError. Its three tests that fill (test_fill, test_voyage_gate_placements, test_seed_doors)
  are `@unittest.expectedFailure` with a comment naming this section; every other test of the
  class passes. When the fix lands they pass, which an expectedFailure reports as a failure
  (unexpected success): **take the three decorators off with the fix.**
- **Counts** (scratchpad/aptests/fillrate.py N k=v...: WorldTestBase's setup + the fill, solo;
  scratchpad/aptests/ncp_spots.py k=v...: per pool key, the locations that may hold it and are
  reached with every other item; both run as `PYTHONPATH=$PWD ../ap-venv/bin/python
  ../../aptests/<script>` in scratchpad/ap/Archipelago, other worlds' import errors on stderr are
  harmless):
  - FillError rate: story_route rena 30/30 seeds, any 10/10, claude 10/10; with
    `expel_teleporter: false` 0/10 (rena and any). The unplaced items: North City Pass alone
    (20 of 30), with Noel's House Pass (6), or most Nede keys (4).
  - Spots: North City Pass **0**, Noel's House Pass **1**, Red Crystal Pass 4, Salva Pass 7,
    Central City Pass 13, Power Pass 25 (expel off, teleporter on). With expel_progression on the
    fewest is the Salva Pass's 7. With the teleporter off North City Pass is a starting key and
    Noel's House Pass still has 1 spot (0/10 failed, but fragile).
- **Why the batches missed it:** every batch multiworld (players, players_hub and their _sk sets,
  scratchpad/apworld2/batch) has the ChecksFinder player Alice, whose locations take the keys; no
  batch or test ever generated a solo open-world seed with expel_progression off. The phase-4 fix
  (2026-10-23: three batch seeds failed to fill; our own Expel-story keys may stay in Expel)
  covers the Expel-story keys only; North City Pass is a Nede key.
- **Candidate fixes (the user's call; a world release, 0.2.x):**
  1. Give North City Pass at the start when expel_progression is off, as the teleporter-off
     case already does (world.create_items `start`; regions / rules unchanged; check.py's
     starting-keys rule and seed_format.md follow).
  2. Let the Nede keys of the stages before the teleporter (North City, and Noel's House) go to
     Expel locations the teleporter proof reached, as the Expel-story keys may (rules "expel keys"
     widened to those keys), keeping other worlds' progression out of Expel.
  Either way re-count Noel's House Pass's spots (ncp_spots.py): 1 is fragile even with the
  teleporter off; and add a solo expel-off seed to the release workflow's generation.

## Done
- **The restructure** (2026-10-26 ~21:25, verified; commit: see git log "Tests: the APWorld's tests
  to Archipelago's testing guide"). Files: apworld/so2/test/bases.py, test_world.py,
  test_open_world.py (new), test_party.py (new), test_hub.py, test_v2.py, test_skillsanity.py,
  test_difficulty.py. World tests **430 passed, 3 xfailed** (the bug's), 30148 subtests, 10.6 s at
  `-n 4` under load ~20; slowest 0.81 s (TestInsanityIseriaQueen::test_skill_items); plain
  `python -m unittest` agrees (OK, expected failures=3).
  - bases.py: SO2TestBase (`side` property from story_route; assertSweptAccessDependency: the
    guide's dependency check through event chains) and every shared helper (own_locations,
    own_pool, rows_of, fill, progression_item, doors_json with realpath, assert_no_gift_progression,
    check_seed). Every test module imports from bases.py; none from another test module.
  - test_world.py: story mode (TestDefault, TestStoryRouteClaude (was Teststory_routeClaude),
    missables, the cave, Expel without progression, the late proof, bosses off, drops and traps,
    shop slots).
  - test_open_world.py: OpenWorldChecks moved from test_world.py, plus test_area_keys (every
    pool key: assertAccessDependency over every location, the expected set from key_ways(), the
    design-level ways), test_voyage_needs_each_pass, test_ship_routes, test_voyage_gate split
    into its logic and test_voyage_gate_placements (the fill); TestOpenWorldNoExpelProgression
    (new mix, the bug's reproducer).
  - test_party.py: CharacterChecks (from test_world.py) and PartyChecks with its classes (from
    test_v2.py); PartyChecks.party_size() / healer_guaranteed() hooks for the bench option.
  - test_hub.py: test_dungeon_keys (every dungeon key, assertAccessDependency over every
    location), test_fienal_gate_needs_each_key (per key, and the goal through the events, for
    each fienal_requirement value the mixes have: fields_and_berle, fields, all_bosses).
  - test_v2.py: KillerChecks.test_killer_move_items (character items -> their killer-move levels).
  - test_skillsanity.py: SkillsanityChecks.test_skill_items (every skill / move item the logic
    needs -> the crafts, Machinery learn points, the Iseria Queen, killer levels whose every way
    needs it); the Iseria Queen per skill and the goal through the events.
  - Written reasons for the two new tests fixed on the way (not loosened):
    - test_area_keys' "every key opens something" was false by design: open world gives all 35
      area keys whatever the options (the seed's 66 doors carry them), so a key whose area holds
      nothing in a mix opens no location: the Gallery Pass (map 463's Nede entrance, never the
      story's, no location behind it) and the Cave of Trials Pass with the cave's locations off
      and no Iseria Queen goal. Now: such idle keys (no way of any location mentions them) may
      only be the keys of areas the story never enters (apworld.md "Open world": Lasguss, the
      Mountain Palace, the Cave of Trials, Giveaway, the Gallery), the Cave of Trials Pass never
      with the cave or goal 2; every other key must open something outright.
    - test_ship_routes' landing case asked every early-only chest of a landing area to exist:
      a landing area's chests also have the late proof's way (the Final Save Point and late_keys,
      no landing key), so no Town of Lacour chest depends on the Hilton Pass outright, only its
      early way does (test_ship_landings, kept, checks that with the stages' states); 4 Hoffmann
      Ruins chests have no late way and depend on the Map 231 Pass outright (tested).
  - Old assertions: all kept (scratchpad/aptests/methods.py old vs new: only the class rename);
    stronger: TestExpelWithoutProgression (every Expel location, not the first; and some outside
    take it); the hub seed tests' validate_doors (was skipped, "Found so far"). Changed only for
    the new mix: test_progression_rules' Nede / other-world rule gains `and expel_progression`.
  - No class only tests a helper with a world it doesn't need: TestDoorValidator and TestHubAsk
    are unittest.TestCase already; so `run_default_tests = False` had no taker.
- The bug write-up above. Observation (not a bug): in mixes without the cave / goal 2, the Cave
  of Trials Pass and the Gallery Pass are progression keys that open no location (the doors).

- test/general after the restructure: **379 passed** (117751 subtests; the count varies with the
  random fills), as the baseline (scratchpad/aptests/after_general.log).

## In progress
- The release workflow: tools/ap_batch.sh, tools/ap_batch_check.py (scratchpad/apworld2/batch
  check.py with relative paths; the checks unchanged, diff = 2 lines), tools/ap_batch_players/
  {story_open,hub} (players_sk, players_hub_sk copied). Running both sets 1-96 into
  scratchpad/aptests/after/ to compare with base/manifest.txt (byte for byte) and the check
  outputs. Docs written, not committed: apworld.md "Testing", release_checklist.md 2.4 / 2.5,
  next.md Phase A status.

## Next steps
1. The release-workflow script in tools/ (the 96-seed batches + check.py), then the
   byte-identical check against scratchpad/aptests/base/manifest.txt.
2. Docs: apworld.md "Testing" (the AP_TEST_WORLDS form, the classes, the workflow script),
   release_checklist.md 2.4 / 2.5, next.md Phase A status.
3. When the world fix lands (another assignment): take the three expectedFailure decorators off.

## How to verify
- World tests: `cd scratchpad/ap/Archipelago && SKIP_REQUIREMENTS_UPDATE=1 prlimit --as=$((4<<30))
  ../ap-venv/bin/python -m pytest -q -n 4 -p no:cacheprovider --durations=30 worlds/so2/test`.
- The bug: `SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python Generate.py --player_files_path
  ../../aptests/solo/players --outputpath ../../aptests/solo/out --seed 1` (rc 1, FillError);
  `PYTHONPATH=$PWD ../ap-venv/bin/python ../../aptests/fillrate.py 10 world_mode=open_world
  expel_progression=false`; `.../ncp_spots.py world_mode=open_world expel_progression=false`.
  (fillrate / ncp_spots print other worlds' import errors on stderr: harmless.)
