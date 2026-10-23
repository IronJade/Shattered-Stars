# Regions text (FR / DE): continuation log

Worker: regions-text agent (started 2026-10-23 12:05). Task: reverse the French and German
text system (encoding, font, wrapping, message-box sizing, name entry, menus / HUD), catalog
every function read under `fr:` / `de:` modules, write `docs/notes/regions_text.md` (how it
works, how it differs from NA, the port's design and how it plugs into the region profile),
and implement it as self-contained engine code with unit tests (not wired into the game).

## Rules given

- No game code, text or assets in git: addresses, formats, counts, short descriptions only.
  Disc images read-only; extracts only under `scratchpad/regions_text/`.
- Discs: `Emulation/Roms/Star Ocean 2/Star Ocean - The Second Story (France|Germany) (Disc 1|2)/`
  (.cue/.bin, ignore zips); NA in `Emulation/Roms/PS/Star Ocean - The Second Story (Disc N)/`.
- Headless only; scratch `XDG_CONFIG_HOME` under `scratchpad/regions_text/`; never touch
  `~/.config/shattered-stars` or `~/.config/pcsx-redux` (Redux only `-cli`, scratch HOME, max 2).
- Kill by exact PID; wait loops with deadlines; no `pgrep -f` of own pattern; /tmp is RAM;
  build in `engine/build-text` with **-j4** (new machine budget); max 4 game processes and
  **1 PCSX-Redux** at a time (the region agent uses Redux too).
- NA behaviour must not change; run ss_tests and ss_game_tests (NA disc) from the build dir
  before calling anything done.
- Don't edit the region agent's files (launcher, setup, disc recognition, region profile /
  address-map files); don't wire into the game. Don't commit or push.
- One line per finished step in `scratchpad/checkpoint_notes.txt`, prefixed `regions-text: `.
- Keep this log current after each step and at least every 30 minutes. Past ~350k context:
  finish the step, update the log, end the turn.
- Catalog under the lock: `flock scratchpad/catalog.lock python3 scratchpad/fx_worker/cat3/apply3.py <file>`.
- REA server is shared: re-open the binary before each batch and check the subject (the
  REA CLI `scratchpad/regions/rea.sh` has its own Ghidra and avoids that).

## Done

- 12:40 (agent 2): state checked: no half-written engine / doc files from agent 1 (only
  scratch). `engine/build-text` configured (Ninja, RelWithDebInfo) and built -j4; NA
  baseline ss_tests 83/0, ss_game_tests 121/0 (`scratchpad/regions_text/work/suites.sh TAG`
  -> `logs/suites_TAG.txt`).

- 13:05 step 1 (reverse) done in scratch: every FR / DE-specific code change read (47 rows
  that differ from NA but not EU, plus the EU-changed rows with extra FR / DE changes:
  text_render_line, rename_screen, title_screen, field_main_loop). Findings:
  `scratchpad/regions_text/findings.md` (fonts, glyph numbers) and `consts.csv` (every moved
  constant: module, function, NA instruction, port file / site, NA / FR / DE values).
- 13:15 step 4 code written (not wired): `engine/src/game/common/text_rules.{h,cpp}` (in
  ss_core): map glyph numbers, ascii_to_glyph, format_number / number_width, layout_line
  (text_layout_line over a LayoutHost, NA / FR / DE rules), wrap_class, ascii_high_bias
  (title / battle text_from_ascii), name keyboards, cvram_choose, Layout (all moved
  constants per language). Tests: `engine/tests/test_text_rules.cpp` (ss_tests, 12 cases, no
  disc) and `engine/tests/test_text_rules_game.cpp` (ss_game_tests: NA layout == ported
  field text_layout_line over a sample of map text; FR / DE disc sweeps with SS_DISC1_FR /
  SS_DISC1_DE). CMakeLists: 3 lines (text_rules.cpp in ss_core, the two test files).

- Suites with the new tests (`work/suites.sh t1`): ss_tests 98/0, ss_game_tests 125/0 (NA
  layout equivalence: 140 maps, 3,302,816 layouts, 0 differ; FR / DE disc sweeps pass).
- `docs/notes/regions_text.md` written (sections 1-9; section 9's Redux numbers pending);
  `docs/catalog/formats.md` Text gained the FR / DE paragraph.

## In progress (13:45)

- Redux line-break check: `scratchpad/regions_text/redux/` (`run.sh fr|de OUT [VBLANKS]`,
  `text_log.lua`: breakpoints on FR / DE text_layout_line 0x8005c24c / ret 0x8005c878, logs
  F/N/V/T/L lines, the text up to its end code (a parameter byte may be 0); `TL_RENA=1` picks
  Rena; screenshots `shot_*.raw`, `raw2png.py`). Replay: `SS_TEXT_REDUX_LOG=<log>[:<log>]
  work/game_one.sh` (test `text_rules_redux_lines`). Done: German Rena start (`runs/de_rena`):
  59 lines, 59 identical (28 wrapped). French run 1 had the old logger (texts cut at a 0
  parameter byte: 5 false mismatches, all on 0x400c 0); rerunning: `all.sh` -> runs/fr2
  (Claude), fr_rena, de2 (Claude), summary in runs/all.log. The opening gives ~90 lines
  then the player has control (mashing Cross stops producing text). Name entry screenshots
  (FR / DE) confirm 3 buttons, the cursor on Delete, capitals only.
- Catalog: `scratchpad/regions_text/catalog/spec.py` (41 FR + 43 DE rows, modules fr_* /
  de_*), `append.py` appends new module blocks under the lock (apply3.py cannot create a
  module); dry run OK, not applied yet (add the Redux evidence to text_layout_line first).

## Next steps

1. Replay runs/fr1; DE run (and Rena starts for more lines); fill notes section 9.
2. Apply the catalog rows (`flock scratchpad/catalog.lock python3
   scratchpad/regions_text/catalog/append.py scratchpad/regions_text/catalog/spec.py`).
3. Final suites, checkpoint note, report.
