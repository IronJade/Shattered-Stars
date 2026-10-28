# Restructure (2026-10-28)

Agent "restructure": prune outdated docs, bring the wiki into the repo, reorganise the repo into
sections (Port, APWorld, Trackers, Docs, Wiki), verify. The coordinator reruns the 0.03.00 RC on
the new layout afterwards. Start commit: 0fefa1b.

## Rules (from the coordinator, the user's decisions 2026-10-28)
- Layout ("Full sections, port/"): port/ (engine/ renamed; port/res, port/tools), apworld/ (so2/,
  tools/), trackers/ (poptracker/, tools/), mods/ (unchanged), docs/ (port/, modding/, apworld/,
  trackers/, release/, images/, plans/, handoff/), wiki/, tools/ (shared + RE only).
- Step 1 prune docs (own commit), step 2 wiki/ + publish script (own commit), step 3 moves (as few
  commits as keep each building; docs first), step 4 verify + new docs/HANDOFF.md.
- Published release notes (0.01.00, 0.02.00, 0.02.50, 0.02.51, 0.2.1) stay as written.
- Headless / -cli only; no GUI, no config saves. Game processes under tools/memcap.py, max 2.
- Big scratch in scratchpad/restructure/. Cap output. Waits have deadlines. No `pgrep -f`.
- Disc images and ~/.local/share/Archipelago read-only. Never push/tag/release/publish the wiki.
- Commit each verified step locally, ending with the Co-Authored-By line.
- A refused command: stop, record it here, report.
- Don't run the RC; fix rc-0.03.00.sh paths + the ut_map zip-path bug.

## Status
- [x] Step 1 prune docs: 0bcf561
- [x] Step 2 wiki: ae1e895 (docs: wiki/ import), 64dc480 (tools: wiki_publish.sh, mod_reference_wiki default), ca5a8b7 (Reference-* regenerated)
- [x] Step 3 moves: f4081eb (one commit: the moves, the rewrites, verified)
- [x] Step 4 verify (results below) + new docs/HANDOFF.md (the commit after f4081eb)

## Moves (2026-10-28; `git mv`, history kept)
- engine -> port
- res -> port/res
- docs/notes -> docs/port/notes
- docs/catalog -> docs/port/catalog
- docs/ap/poptracker.md -> docs/trackers/poptracker.md
- docs/ap/tracker_maps.md -> docs/trackers/tracker_maps.md
- docs/ap/tracker_maps.json -> docs/trackers/tracker_maps.json
- docs/ap/release_checklist.md -> docs/release/release_checklist.md
- docs/ap -> docs/apworld
- docs/release_notes -> docs/release/notes
- tools/ -> port/tools/: build_linux.sh, build_windows.sh, linux-build, make_icon.py,
  smoke_field_events.sh, event_sweep.py, battle_sweep.py, compare_audio.py, compare_frames.py,
  compare_sound_events.py, ref_regress.py, refs, redux_reference.lua, redux_sound.lua,
  redux_vm_trace.lua, setup_screenshots.sh, gme_to_mcd.py, so2_loadwatch.lua, mod_check.py,
  mod_index.py, mod_reference.py, mod_reference_wiki.py, mod_verify.py, test_mod_check.py,
  test_mod_reference.py, third_party_licenses.sh, region_map.py, regions
- tools/ -> apworld/tools/: ap_batch_check.py, ap_batch_players, ap_batch.sh, ap_battles.py,
  ap_doors.py, ap_ds_read.py, ap_e2e_hub.sh, ap_e2e.sh, ap_e2e_so2.sh, ap_extract.py,
  ap_hub_bosses.py, ap_hub_chambers.py, ap_hub_state.py, ap_late_proof.py, ap_make_crafts.py,
  ap_make_moves.py, ap_make_skills.py, ap_survey.py, ap_test_server.sh, ap_walkmap.py,
  ap_world_tables.py, build_apworld.sh
- tools/ -> trackers/: poptracker
- tools/ -> trackers/tools/: ap_poptracker.py, ap_poptracker_smoke.py, ap_poptracker_test.py,
  ap_poptracker_utmap.py, ap_tracker_logic.py, ap_tracker_maps.py, ap_tracker_maps_test.py,
  ap_ut_check.py, build_poptracker.sh
- Stay in tools/ (shared): memcap.py, checkpoint.sh, ghidra_scripts/, catalog_coverage.py,
  catalog_gaps.py, match_functions.py, merge_catalog.py, psxexe2elf.py, map_zones.py,
  so2_archive.py, so2_data.py, so2_mod.py, so2_names.py, so2_script.py (+ so2_script_args.json),
  and the new wiki_publish.sh.

## Log
- start: no RC / game processes running; last RC (rc030-coord) stopped after `identity`.
- step 1: `git rm` 85 docs/handoff logs (kept release, phaseb, windows_check, utzip + this log),
  docs/HANDOFF.md, docs/archipelago_plan.md, docs/CHECKLIST.md, docs/plans/trackers.md.
  - plans/trackers.md: every rule that still governs behaviour is already in docs/ap/poptracker.md /
    tracker_maps.md (versions.json path D4, no `apmanual`/`aphintgame`, nothing from the disc D5,
    user-override, community-packs D7). Its section 6 (ER + trackers) folded into docs/plans/next.md
    Phase C ("From the tracker plan (section 6), for ER").
  - old HANDOFF.md: "Conventions to keep" -> new docs/CONVENTIONS.md (verbatim; referenced by
    next.md, images/README.md, release_checklist.md, memcap.py, smoke script); "Debug options" ->
    engine/README.md "Debugging switches" (it pointed there); the ASan recipe -> engine/README.md Tests.
  - docs rewording: `docs/handoff/NAME.md` -> "the NAME handoff log" in docs/ap/*.md, docs/notes,
    docs/catalog/COVERAGE.md, docs/images/README.md, engine/README.md, tools/refs/README.md, next.md
    (scratchpad/restructure/fix_handoff_refs.py); archipelago_plan / plans/trackers refs reworded
    ("the tracker plan's B3", "the Archipelago work").
  - Left on purpose (covered by the docs/HANDOFF.md removed-files line): code comments in
    apworld/so2, engine/src and tools citing removed handoff logs / plans; the generated "about"
    strings in docs/ap/hub_*.json; docs/modding/reference/map_names.csv; the kept live handoff logs.
  - next.md trimmed: phases A/B/party -> "Done" (3 bullets); Japan deferral and Phase C intact.
- step 2: wiki/ = scratchpad/wiki working tree (diff -rq identical before edits), then 2 link
  fixes (How-the-Port-Was-Made CHECKLIST, Contributing HANDOFF/CHECKLIST). scratchpad/wiki untouched
  (still 11 uncommitted changes, HEAD 6a1624f). tools/wiki_publish.sh tested against a throwaway
  clone (scratchpad/restructure/wiki-test, origin set to example.invalid): --dry-run lists 12 pages,
  default mode syncs + shows diff, nothing committed. Never ran --push.
  - Found: the wiki's Reference-* pages were stale vs docs/modding/reference (maptab's area audit,
    fa8b0df / 0e5b3ea): mod_reference_wiki.py changed 9 pages. Committed separately (ca5a8b7) so
    it can be reverted on its own.
- step 3 (in progress): `git mv` done per scratchpad/restructure/placements.txt (70 moves; the
  untracked engine/build* dirs moved with engine -> port, all stale). Placements not in the
  coordinator's list / judgment calls:
  - tools/so2_mod.py stays in tools/ (shared library: imported by apworld/so2/patches.py,
    apworld/tools ap_extract / ap_battles / ap_hub_state / ap_survey and port/tools
    test_mod_reference; also the modder-facing CLI the wiki and mods/README document).
  - apworld/tools/ap_ds_read.py (reads the port's data-storage keys; its only caller is
    ap_e2e_so2.sh). apworld/tools/ap_test_server.sh (as listed; also used by ss_game_tests ap_net).
  - port/tools/region_map.py + port/tools/regions/ (port-only: writes port/data/regions, imported
    by battle_sweep). tools/map_zones.py stays shared (RE). so2_script_args.json stays with so2_script.
  - trackers/tools/ap_tracker_logic.py (the pack's reference evaluator; apworld tests load it by path).
  - docs/CONVENTIONS.md stays at docs/ (project-wide rules).
- step 3 rewrites (2026-10-28 ~09:00):
  - scratchpad/restructure/rewrite_paths.py (NOT idempotent for tool paths: run once only) over
    every tracked text file except the 5 published release notes and this log: 7202 substitutions
    in 271 files (engine/ -> port/, res/ -> port/res/, each moved tools/X, docs/ap|notes|catalog|
    release_notes, Python os.path.join pieces). Then by hand: `cmake -S port`, `${1:-port/...}`
    defaults, moved shell scripts `cd "$(dirname "$0")/../.."`, Python ROOT/REPO one level deeper
    + `sys.path[:0] = [own folder, ../../tools]` where a shared module is imported
    (scratchpad/restructure/fix_python.py), ap_poptracker LUA_SRC -> trackers/poptracker,
    CMake SS_RES_DIR, .gitignore (drop "Source BIN files/"), .vscode, checkpoint.sh area labels,
    README "Project layout", wiki Contributing "Where things live", checklist relative links.
  - Kept handoff logs (release, phaseb, utzip, windows_check) were rewritten too (they're live; the
    commands in them now run on the new layout). Historical exceptions kept: release_checklist.md's
    0.02.50 content-id command (`-- engine apworld tools mods LICENSE`, with a note), release.md's
    rc030b description of ph_verify.
  - Scratchpad (RC): rc-0.03.00.sh rewritten (backup scratchpad/restructure/orig/): BUILD_RC=port/build
    (+ new `native_build` step at the start of ph_build), SRC=(port apworld trackers tools mods
    LICENSE docs/trackers/tracker_maps.json), adapt_e2e matches `/../..`, cmake -S port, deps from
    port/build/_deps; UT zip fix: render_capture renders to linux-render/maps with
    `--ut-zip linux-render/so2_shattered_stars_ut.zip`, win_render to win-render/maps + its own
    `--ut-zip`, ph_wine diffs both folders (maps + zip) and adds a `win_ut_zip_identical` cmp step;
    ut_map_*_port read linux-render/$UTZIP; asan_render likewise. ut_explain skips with a note when
    scratchpad/rulebuilder/ut_explain.py is missing (it was deleted in today's scratch cleanup).
    utzip/setup_runs.sh: bin defaults to port/build (UTZIP_SETUP_BIN overrides).
  - Build dirs: port/build re-configured fresh (RelWithDebInfo, Ninja, FETCHCONTENT_SOURCE_DIR_* =
    port/build/_deps/<dep>-src, the old fetched sources kept in place); deleted the stale
    build-utzip, build-win-release(+.configure.log), build-linux-container (the RC's build scripts
    configure those fresh; they download the pinned deps again).
  - Pre-existing, not the move's: apworld/tools/ap_survey.py never imported os/sys/re (NameError at
    line 5, before and after).
- step 4 verification (on the new layout, before the step 3 commit; logs in scratchpad/restructure/logs):
  1. stale-path grep (scratchpad/restructure/stale_grep.py: engine/, every moved tools/<name>,
     docs/ap, docs/notes, docs/catalog, docs/release_notes, tools/poptracker; published notes
     excluded): 4 files left, all justified: this log; release_checklist.md's 0.02.50 note on the
     old layout; poptracker.md's note that versions.json was tools/poptracker/ before; (README's
     "(tools/refs/)" fixed). Not matched by the grep but kept on purpose: release.md's rc030b account
     of ph_verify, the checklist's 0.02.50 `-- engine apworld tools mods LICENSE` command.
     Relative markdown links: 0 broken. GitHub blob/tree URLs (README, wiki, docs): all exist.
  2. port/build fresh (configure rc 0, build 372/372 rc 0); ss_tests **103 / 0**; ss_game_tests
     SS_TEST=tracker_ **14 / 0**, SS_TEST=ap_ds **9 / 0** (memcap'd, scratch XDG).
  3. smoke (port/tools/smoke_field_events.sh port/build/shattered-stars, from the repo root):
     **373 PASS / 0 FAIL / 0 SKIP** (= the 0.03.00 RC's linux smoke before the move).
  4. apworld/tools/build_apworld.sh -> so2.apworld 281,629 bytes; unzipped it equals the RC's
     07:36 build with the path rewrite applied (diff empty); YAML identical. World tests (venv,
     scratchpad/ap/Archipelago): worlds/so2/test **882 passed, 3 skipped**; scoped (AP_TEST_WORLDS=so2
     test/general worlds/so2) **935 passed, 3 skipped**; SO2_SLOW_TESTS **885 passed**; AP general
     **379 passed** (all = the RC's counts; subtest totals vary run to run). apworld/tools/ap_batch.sh
     hub 1-3: 3/3 exit 0, checker 0 errors, the 6 .apso2 byte-identical to the RC's batch.
     ap_world_tables.py regenerates apworld/so2/data.py byte-identical; ap_walkmap --check ok.
  5. trackers/tools/build_poptracker.sh all: rc 0, pack checker "Validated 11 files" x2, versions.json
     ok, ap_poptracker_test.py **43 / OK**; the zip's manifest has
     versions_url .../main/trackers/poptracker/versions.json; vs the RC's zip only that URL and a
     comment path in archipelago.lua differ. ap_tracker_maps.py --check: up to date.
  6. mods.yml steps: mod_check --all --github --strict (2 OK), mod_index check, guard, test_mod_check
     **24 OK**, test_mod_reference **21 OK** (with SS_DISC1, 0 skipped; without it 1 skip, as before);
     mod_reference --check all ok and its tables regenerate byte-identical to docs/modding/reference.
  7. ref_regress (port/build, 2 at a time): stage13 (battle), chis_b1 (fx_battle), d10_3 (enemy),
     iLv_3 (world_spell), combref2 (menu): **5 / 5 pass**; also --quick: **28 pass, 0 regress**.
     No input missing after today's scratchpad trim.
  8. bash -n: rc-0.03.00.sh, setupthread/setup_runs.sh, utzip/setup_runs.sh, every .sh in port/tools,
     apworld/tools, trackers/tools, tools: ok. Every path rc-0.03.00.sh names exists except the ones
     it creates (port/build-rc030b-asan/-tsan, port/build-win-release, the dist outputs) and
     scratchpad/rulebuilder/ut_explain.py (gone; the step now skips with a note). adapt_e2e simulated
     on both e2e scripts: its checks pass. The hub e2e's chamber heredoc runs (arcmene: ENC=522).
- Done. Next (the coordinator): rerun the RC on the new layout, from a clean tree:
  `mv scratchpad/release/rc-0.03.00-dist scratchpad/release/rc-0.03.00-dist.pre-restructure`, then
  `bash scratchpad/release/rc-0.03.00.sh all` in the background (summary.txt in the dist folder).
  The wiki's uncommitted 0.03.00 edits now live in wiki/ (scratchpad/wiki untouched); publish with
  tools/wiki_publish.sh at release time.
