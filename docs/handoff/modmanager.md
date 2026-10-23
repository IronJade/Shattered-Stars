# Handoff: mod manager and the curated mod list (agent "modmgr")

Design: `docs/notes/mods.md` (the source for the README / wiki writer).

## Rules this agent was given (all mandatory)

- Never break existing behaviour: with no mod.json anywhere the game plays exactly as now; the
  port never changes the original's play state unless a mod asks for it.
- No game code or assets in git; no images in mods/. Headless only (--headless,
  SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy); setup's screenshot mode must not open a
  visible window (tools/setup_screenshots.sh does it offscreen).
- Never touch ~/.config/shattered-stars or ~/.config/pcsx-redux: a scratch XDG_CONFIG_HOME
  under scratchpad/modmgr/ for every run, setup included.
- Kill processes only by exact PID; wait loops have a deadline and wait on a PID, a
  done-marker or `pgrep -x` (never `pgrep -f` on our own pattern); long jobs in the background.
- /tmp is RAM: scratch under scratchpad/modmgr/. Build in engine/build-modmgr with -j8. At most
  6 game processes at once.
- SS_DISC1="/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/<the .cue>".
- Other agents: release (setup_ui.cpp header/About/version, main.cpp --version, build scripts,
  apworld/so2), DeathLink (engine/src/game/ap/*, engine/tests/test_ap.cpp, docs/ap/*). Re-read a
  file right before each edit; stay out of their areas. Catalog writes under
  `flock scratchpad/catalog.lock` with scratchpad/fx_worker/cat3/apply3.py.
- Before done: ss_tests, ss_game_tests (SS_DISC1), tools/smoke_field_events.sh,
  `tools/ref_regress.py --quick --build engine/build-modmgr`, the new Python tests.
- Cap command output. Don't commit or push (the coordinator commits hourly); note in this log
  that pushing .github/workflows may need extra token scope. When done, append ONE line to
  scratchpad/checkpoint_notes.txt prefixed "modmgr: ".
- Keep this file current after each deliverable and at least every 30 minutes. Past ~400k
  tokens: update it and end the turn. If blocked, say so here and stop.

## State

(updated as work proceeds; newest first)

- 11:40 DONE, all deliverables. Suites from engine/build-modmgr (scratch XDG):
  build OK; ss_tests 78/78; ss_game_tests 119/119 (9 new mods_* tests); smoke 86 PASS,
  0 FAIL; ref_regress --quick 28/28; test_mod_check.py 24/24. Screenshots (final binary):
  scratchpad/modmgr/shots/mods-{manager,conflict,readme,cannot-load,loose,reordered,
  remove,install-refused,replace,installed}.png plus the older mods.png / mods-missing.png.
  Not done / open for the user: whether CI should require declaring risky edits
  (`mod_check.py --strict`); whether a later mod should win a conflict (implemented, the
  user's wording) or the guard's first-wins (the old behaviour, kept for loose files);
  browsing/downloading the list inside setup (designed for, not built). Nothing blocked.
  Files: engine/src/mods/*, engine/src/game/field/mods.{h,cpp}, engine/src/launcher/
  {config.h,settings.h,settings.cpp}, engine/src/runtime/runtime.h, engine/src/main.cpp
  (mod_order read), engine/src/setup/setup_ui.{h,cpp}, engine/CMakeLists.txt (ss_mods,
  test), engine/tests/test_mods.cpp, tools/{mod_check,mod_index,mod_verify,test_mod_check}.py,
  tools/setup_screenshots.sh, mods/ (README, index.json, 2 samples), .github/, docs/notes/
  mods.md, docs/HANDOFF.md (debug options line), engine/README.md (mods paragraph).

- 11:00 deliverables 3-5 done; suites running (scratchpad/modmgr/suites/run.sh ->
  status.txt, done-marker `done`):
  - `tools/mod_check.py` (+ `tools/test_mod_check.py`, 24 tests, pass; set MOD_CHECK_TMP to a
    scratch dir locally). Script word bound measured: largest script map 474 = 25889 words
    -> limit 0x8000.
  - `tools/mod_index.py` (build / check / verify / guard); `mods/index.json` built, both
    samples verified ("IronJade (sample, verified by the port's tooling)").
  - `tools/mod_verify.py`: PASS for both samples (reports in
    scratchpad/modmgr/verify/{arlia-crate-fol,hearn-store-plus}/report.md); a self-test
    package (scratchpad/modmgr/selftest) FAILs on its skipped line as it should and
    exercises battle 19.
  - `mods/README.md`, `.github/CODEOWNERS` (/mods/, /.github/, tools/mod_check.py,
    tools/mod_index.py -> @IronJade), `.github/workflows/mods.yml`,
    `.github/PULL_REQUEST_TEMPLATE/mod_submission.md`.
  - For the coordinator: pushing `.github/workflows/mods.yml` needs a token with the
    `workflow` scope.

- 10:25 deliverable 2 (engine + setup) working, not yet run through the full suites:
  - new library `ss_mods` (engine/CMakeLists.txt): `engine/src/mods/patch.{h,cpp}` (the
    grammar, moved out of game/field/mods.cpp unchanged), `catalog.{h,cpp}` (manifests with a
    small JSON reader, scan, load order, conflict analysis, zip install/remove),
    `zip.{h,cpp}` (zip reader + inflate). ss_game and setup link it.
  - `game/field/mods.cpp` uses it: seed lines, then the enabled entries in `mod_order`;
    later mod overrides an earlier one (same EXPECTED, different VALUE) - the earlier line
    is logged "overridden ... not applied"; skips name the earlier line that set the value;
    script lines from mods now log "applied" (seed lines don't); load order / not-enabled /
    conflict lines only printed when packages exist. `Runtime::mod_order` (runtime.h);
    main.cpp reads config `mod_order` only when the folder isn't overridden (--mods /
    SS_MODS_DIR); `SS_MOD_ORDER` overrides. `launcher/settings.{h,cpp}` read/write_mod_order.
  - setup: Settings::mod_order; page_mods rebuilt (table with checkboxes, Install from .zip,
    Move up/down, Remove..., info panel, conflicts, README); popups "Replace mod" / "Remove
    mod"; sidebar dot; actions mods-install:/mods-select:/mods-toggle:/mods-up/mods-down/
    mods-remove/mods-show-info.
  - tests: `engine/tests/test_mods.cpp` (9 tests, in ss_game_tests). A quick standalone build:
    `g++ ... engine/tests/test_main.cpp engine/tests/test_mods.cpp` + the build's libs ->
    engine/build-modmgr/quick_mods_tests (all 9 pass incl. the game run).
    Note: item lines apply when an item table loads (battle start, shops, menus, maps with
    section 11) - a `--map 26` run doesn't load one; the game test uses `--battle 19`.
  - sample mods in `mods/arlia-crate-fol` (chest 0x962 on maps 18 + 21 -> 500 Fol) and
    `mods/hearn-store-plus` (shop 1 appends item 0x115), made with tools/so2_mod.py.
  - screenshots: `tools/setup_screenshots.sh` gained 10 mods-* shots (own scratch settings
    folder) -> scratchpad/modmgr/shots/.
- 09:55 design doc written (`docs/notes/mods.md`). Next: engine module.

Next steps: mod_check.py (+ tests), mod_verify.py, mods/index.json + tools/mod_index.py,
mods/README.md, .github (workflow, PR template, CODEOWNERS), verify the samples, full
suites, checkpoint note. Pushing `.github/workflows/mods.yml` may need a token with the
`workflow` scope (tell the coordinator).
