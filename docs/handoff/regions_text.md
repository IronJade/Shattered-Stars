# Regions text (FR / DE): continuation log

Worker: regions-text agent (started 2026-10-23 12:05; agent 3 from 13:56, also wires it in). Task: reverse the French and German
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
- Agents 1-2: don't edit the region agent's files, don't wire. **Agent 3 (13:56): wire it
  yourself** (replace the call sites, enable France / Germany in disc recognition, make
  `SS_REGION_TRY` unnecessary); if large, finish the self-contained part first. Every game
  run under `tools/memcap.py` / prlimit 4 GB (hitting it = failure). Before "done": ss_tests,
  ss_game_tests, tools/smoke_field_events.sh, `tools/ref_regress.py --quick --build
  engine/build-text` (NA), region tests. Don't commit or push.
- One line per finished step in `scratchpad/checkpoint_notes.txt`, prefixed `regions-text: `.
- Keep this log current after each step and at least every 20 minutes (agent 3). Past ~350k context:
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

- 14:00 (agent 3): state checked: tree clean at a123cbd (agent 2's code / tests / notes are
  in that commit); no process left running; build-text up to date. `engine/tests/test_main.cpp`
  gained `SS_TEST=a,b` (run only cases whose name contains one; default unchanged).
- Redux line-break check (agent 2's runs, replayed by agent 3): `text_rules_redux_lines` over
  runs fr2 (Claude, 88 lines), fr_rena (56), de_rena (59), de2 (Claude, 84): **287 lines, 287
  identical (137 wrapped)**, maps 7 and 18 (the opening). fr1 (old logger) deleted. None of
  these lines is decided by the " ?" / " !" rule (`redux/rulecheck.py`); that rule is covered by
  the unit tests and the disc sweep.

- 14:35 (agent 3) wiring done (build-text builds; suites not yet run):
  - field/message_text.cpp: `text_layout_line` dispatches to `text::layout_line` for France /
    Germany (`FieldLayoutHost`; NA keeps its ported body, renamed `layout_line` in the
    anonymous namespace), `text_ascii_to_glyph` likewise; number width / format, the render
    line's dropped space from `map_glyphs`. `SS_TEXT_LOG=<file>` (debug) logs every laid-out
    line in the Redux logger's L format. field/message.cpp: the print push range.
  - title/text.cpp, battle/window.cpp: `ascii_high_bias`. title/window.cpp + new_game.cpp,
    field/menu_rename.cpp: NameKeyboard (first column, buttons, step, OK column, set 0 only,
    no L / R switch), rename hand / cursor / blank u, heading / subtitle, the 5th VRAM area.
  - common/menu_gfx.cpp `cvram_alloc` -> `cvram_choose`; common/menu_save.cpp (slots,
    labels, check dialog); field/post_battle.cpp, menu_options.cpp, menu_specialty.cpp,
    menu_skills.cpp (FR / DE heading = group 0xf message 0), menu_equip.cpp,
    field_sub_overlays.cpp (the_end), battle/result.cpp (FR order), battle/spell_windows.cpp
    (DE scan from page 1), title/voice_collection.cpp, title/window.cpp (sound / vibration).
  - runtime/region.cpp: France and Germany `supported = true` (SS_REGION_TRY no longer
    needed); tests: test_launcher (FR / DE discs OK, pairs, `game_runs_the_french_release` /
    `_german_release`, the Japanese message lists four releases), test_region.
- Port vs original, the whole pipeline (`SS_TEXT_LOG` against the Redux logs,
  `scratchpad/regions_text/work/cmp_lines.py`; port runs `work/run.sh fr|de NAME ...` with
  `work/mash.py`'s input = the Redux script's): French opening 88 of 88 lines identical,
  German 84 of 84 (runs/fr_open2, de_open). French map 18 Hearn: "aider ?" stays together
  (runs/fr18talk/b.png).

- 14:55 (agent 3): name entry: the title keyboard first showed wrong characters ("Olaude")
  in France / Germany: the title's ASCII table (NA 0x8004eafc, whose first 0x20 entries
  overlap the keyboard column map, which France / Germany changed) was read through a base
  pointer translated in a 16-byte region-map segment that `tools/region_map.py` aligned 12
  bytes off (it matched the changed column-map words by value; FR ('0x1f2e8', 0x10, -340)
  against -352 around it; the 0xbce overlay the same at 0x80082b78, with column-map entries
  6-9 unmapped). Port fix: the ASCII tables (title/text.cpp, field/menu_rename.cpp) are read
  entry by entry, the rename page's column map from its start. Then the French and German
  title keyboards match the Redux screens (runs/fr_kb, de_kb vs redux/runs/fr2, de2
  keyboard16.png); input checked (row 1 from Delete lands on '3' per the column map, a full
  name jumps to OK = column 0, Left / Right wrap over three buttons: runs/fr_kb2).
  Region-map finding reported in docs/handoff/regions.md (Known).
- Catalog applied (14:50, under the lock): 84 rows, 23 modules fr_* / de_* (41 FR + 43 DE),
  port column = the wired port function (text_rules API in brackets).
- Suites w1 (`work/full.sh w1`, before the column-map fix; region discs exported): ss_tests
  98/0, ss_game_tests 128/0 (incl. game_runs_the_french_release / _german_release), smoke
  86/0, ref_regress quick 28/28. Rebuilt with the fix 15:05.
- Notes section 8 (wired, port-only text) and 9 (results) written; regions.md section 10
  profiles paragraph; text_rules.h header lists the sites.

- 15:25 (agent 3): camp menu + Options screens 1 / 2, French and German, port (runs/fr_menu2,
  de_menu2) vs Redux (redux/runs/fr_menu, de_menu; comparison images runs/fr_menu_cmp.png,
  de_menu_cmp.png): identical layout. German keyboard rechecked with the final build
  (runs/de_kb3). A French debug battle (`--battle 1 --battle-end 400`) runs to the field
  (runs/fr_battle). Final suites (`work/full.sh final`, logs/full_final.txt): ss_tests 98/0,
  ss_game_tests 128/0, smoke 86/0, ref_regress quick 28/28.

## State (agent 3, 15:25): done

France and Germany are supported releases with their own text system. Nothing running.

## Left (not blocking)

- The rename page (overlay 0xbce, menu page 14) was not opened in a run (shares the title
  keyboard's rules, whose screens match Redux).
- Battle result screen (French message-first order) and the German Scan window (from page
  1) are wired but not compared with Redux.
- Port-only texts (Archipelago messages) in France / Germany: characters outside the
  original's ASCII table (apostrophe, '-', brackets) show the Latin font's glyph at that byte.
- `tools/region_map.py`: equal-length changed tables can be aligned a few words off (see
  docs/handoff/regions.md Known); the text code reads around the one found.
- Disc 2 / sweeps for France and Germany are the region agent's next steps.
