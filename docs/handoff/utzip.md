# Handoff: utzip (the port's UT zip of its own renders; door diamonds yellow; a test temp-folder fix)

Agent "utzip", started 2026-10-27 by the Phase B coordinator (docs/handoff/phaseb.md), before the
0.03.00 / 0.2.2 / pack 1.0.0 release candidate (rc030 is held at its task 4 until this is done;
docs/handoff/release.md "0.03.00").

## The user's words (2026-10-27, via the coordinator)
"2. Include it now. 4. Yellow works for me." (phaseb.md, maptab's open points 2 and 4.)

## The brief
- **A. The player's own rendered maps in Universal Tracker.** UT's map tab (apworld/so2/ut_map.py,
  `tracker_world`, `external_pack_key: "ut_pack_path"`) takes its map data from so2.apworld and only
  the pictures from a .zip the player picks once; UT can't read PopTracker's user-override folder. So
  `tools::render_tracker_maps` (CLI `--render-tracker-maps [DIR]` and setup's button) also writes a
  zip for UT holding the rendered pictures at the pack's image paths, plus everything else UT's map
  tab loads from the zip (at least the player marker `ut_map.pages()["player_icon"]`, drawn
  originally in the port; the complete list verified with UT's own loader).
  - A small store-method zip writer in the port (deterministic: fixed timestamps, sorted entries;
    the port's CRC32), written as `.part` then renamed; a progress step for setup's worker; stop /
    cancel safe.
  - The zip's place: easy to find, never in a folder PopTracker scans for packs, never where it
    would act as an override; a CLI flag to choose the path or turn it off; the default per OS
    documented. Setup shows the zip's path with a one-line hint ("choose this file when Universal
    Tracker asks for the pack").
  - so2.apworld: UT's dialog text (ut_map.py `ut_dialog_name` / description) names both choices;
    the docs. No change to generation.
  - Tests: unit (zip structure, CRCs, Python's zipfile reads it, same bytes twice, nothing outside
    the targets, cancel leaves no final zip), tools/ap_tracker_maps_test.py (the zip's entries =
    the rendered PNGs byte for byte), UT's own map loader against the port's zip
    (tools/ap_poptracker_utmap.py, 15/15 as with the pack zip), setup headless with a screenshot of
    the path. Sizes and times (render with and without the zip).
- **B. Door diamonds yellow.** tools/poptracker/doors.lua: known doors SequenceBreak (yellow);
  unknown (ER) doors yellow too for now (the Doors panel shows "?"); docs/ap/poptracker.md says the
  unknown doors' colour is a Phase C decision. Pack tests + smoke (tools/ap_poptracker_test.py,
  tools/ap_poptracker_smoke.py); rebuild/validate (tools/build_poptracker.sh all).
- **C. engine/tests/test_tracker_maps.cpp's scratch folders**: fix as b82b176 did for
  test_tracker_job.cpp (no folder left in the cwd).
- Docs: docs/ap/tracker_maps.md, docs/ap/poptracker.md, docs/ap/apworld.md (UT map tab),
  apworld/so2/docs/setup_en.md (UT section), docs/release_notes/0.03.00.md, 0.2.2.md,
  poptracker-1.0.0.md (keep the DRAFT banners), docs/handoff/windows_check.md "Recheck for 0.03.00"
  (UT with the port's zip; door colour), docs/ap/release_checklist.md section 12 (rows/counts that
  change), scratchpad/wiki/Trackers.md (uncommitted; links checked). This log, with a list of every
  count changed so the RC agent can recheck them.

Files I may touch: engine/src/tools/tracker_maps.*, engine/src/util/** (the zip writer),
engine/src/setup/** (the path line), engine/src/main.cpp (the CLI flag), engine/tests/**,
engine/CMakeLists.txt, apworld/so2/ut_map.py and apworld/so2/docs/, tools/poptracker/**,
tools/ap_poptracker*.py, tools/build_poptracker.sh, tools/ap_tracker_maps*.py, the docs listed
above, docs/handoff/utzip.md. Nothing else; no generation change (if anything a seed could depend
on is touched: the batch comparison, 288 seeds identical against a frozen baseline with real
copies of the top-level scripts).

## Rules given
- Headless only (--headless, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy). Nothing on the
  user's desktop: the port, setup, UT, PopTracker and AP clients run with DISPLAY, WAYLAND_DISPLAY
  and DBUS_SESSION_BUS_ADDRESS unset and XDG_RUNTIME_DIR pointed at a scratch folder, or under
  ~/.local/bin/xvfb-run (UT's file dialog must never appear: give it the zip path in its scratch
  host.yaml).
- Every game process memory-capped (tools/memcap.py as the other tools use it, or
  `prlimit --as=$((4<<30))`; cgroup MemoryMax for ASan); at most 2 game processes at once; own
  build dir engine/build-utzip, builds -j8 (the only agent).
- Scratch XDG_CONFIG_HOME and HOME; never a config-saving flag, never
  ~/.config/shattered-stars/config.ini, the user's saves, a real PopTracker folder or
  ~/.local/share/Archipelago (read-only); kill only by exact PID.
- Python from scratchpad/ap/Archipelago with ../ap-venv/bin/python and SKIP_REQUIREMENTS_UPDATE=1.
- SS_DISC1 = "/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/Star Ocean - The Second Story (Disc 1).cue".
- Commit on main after each verified piece (B, then A, then C, or as fits), docs first, staged by
  path (never git add -A / -u); message ends with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never commit build
  output, images or zips; never commit or push the wiki. Never push, tag or publish. index.lock:
  wait and retry; never delete it.
- A denied tool call: don't retry; write the blocked step here and stop, naming the exact command.
- A choice that is the user's: write it here with my recommendation and continue.
- Cap command output; long jobs in the background with deadlines; don't pipe a program's output
  into a pipe drained later (write to a file); scratch under scratchpad/utzip/ (not /tmp, RAM).
- Keep this log current after each finished step and at least every 30 minutes. Roll over at
  ~400k context: update the log and stop.

## Design (A; reconstructed by utzip2 from utzip's uncommitted tree, 2026-10-28)

As found in the working tree (`git diff` + the new files), before utzip2 changed anything:
- **What UT loads from the zip** (UT 0.3.6, worlds/tracker/TrackerClient.py `load_pack` /
  `set_root_pack_path` / `load_map` / `update_location_icon_widgets`): with `map_page_folder` set
  (hybrid) the JSON comes from so2.apworld and only pictures from the zip, as
  `ap:zip:<zip>/<path>`: each map's `img` (apworld/so2/tracker/maps/maps.json: 867 = 68 area and
  world maps + 794 rooms + 5 boards) and the location icon `map_pages.json` `player_icon`
  (images/status/you.png). No items (no `item_page_items`). So the zip needs 868 files.
- **Where** (`tools::tracker_ut_zip_path(dir)`, `kTrackerUtZipName`): `so2_shattered_stars_ut.zip`
  in DIR's parent. With the default DIR (PopTracker's override folder for the pack) that is
  `<PopTracker>/user-override/so2_shattered_stars_ut.zip`, next to the rendered folder. Checked in
  PopTracker's source (scratchpad/trackers/poptracker: src/poptracker.cpp 370-420 and
  src/core/pack.cpp): packs are listed only from the `packs` search paths, and an override is only
  `<override path>/<uid>` when it is a directory, so a zip there is neither a pack nor an override.
- **CLI** (main.cpp): `--ut-zip FILE` / `--no-ut-zip` (the last one given wins); default as above.
  The CLI's progress lambda prints one line before the zip's first file.
- **What goes in** (`write_ut_zip`, after the images): every layout image (`images` then `rooms`)
  found as a file under DIR, read back from disk (so the zip holds exactly what PopTracker reads,
  also images of an earlier run when SS_TRACKER_ONLY limits this one; missing ones are counted in
  the summary line), plus `render_tracker_ut_extras`: the 5 boards and the player marker, drawn by
  the port (a name both a render and an extra is an error). Sorted by name.
- **The boards**: tools/poptracker/boards.json, written by tools/ap_poptracker.py (always beside the
  pack folder; `--boards-out` updates the committed copy) with each board's path, size, title,
  hint, and each cell's box [x, y, w, h, label], plus the style colours, label_x and margin, and
  the marker's path and size. CMake compiles it into generated/tracker_layout_data.cpp next to the
  layout (`tracker_boards_json()`). The port draws each board at the pack's size with the cells at
  the pack's boxes (the location squares sit at the cell's (24, h/2), from the same numbers), in
  the overlay's 5x7 font (title 4x, hint 2x, labels 2x in up to two lines, else 1x). Only text and
  numbers come from the pack generator; nothing from the disc, nothing of the pack's pixels.
- **The marker**: images/status/you.png, 64x64 RGBA: the pack's design (a white disc r26, a red
  #e0302a disc r20, a white centre r8 on clear), drawn in the port (`encode_png_rgba`, new; the
  RGB encoder is the same code with 3 channels).
- **util/zip_writer.{h,cpp}** (new, `ss::util::ZipWriter`): stored entries only, every entry dated
  1980-01-01 00:00, version 2.0 (made by MS-DOS), no extra fields, no comment, no data descriptors,
  external attributes 0, the UTF-8 flag for non-ASCII names, safe names only (relative, no `..`,
  `\`, `:`), no zip64 (65535 entries / 4 GiB refused). Written as `<zip>.part`, renamed by
  finish() (remove + rename again where rename won't replace); abandon() / the destructor remove
  the .part. `util::crc32` is now exported from util/deflate (it was file-local).
- **Progress / stop**: kind "zip" calls before each of the zip's files (done / total count the
  zip's files, path = the zip, name = the entry); a stop there abandons (no zip, no .part; an
  earlier run's zip stays as it was) and render_tracker_maps returns kTrackerRenderStopped. A zip
  that can't be written makes the exit code 1 (the images stay).
- **Not done when utzip stopped**: setup (path line, the zip phase in the progress), every test,
  ut_map.py's dialog text, the docs, a pack test that the committed boards.json is the build's.

Changes by utzip2 (each with its reason):
- (filled in as made; see the Log)

## State
### Done
- **B. Door diamonds yellow** (verified): tools/poptracker/doors.lua `so2_door_access` returns
  SequenceBreak for every door (known or not; the unknown doors' own colour is written down as a
  Phase C decision in docs/ap/poptracker.md "Door markers"). Tests: tools/ap_poptracker_test.py
  expects 5 (SequenceBreak) for every door section, also in the ER seam test (found or not), and a
  new `TestDoorsLua.test_door_colour_is_no_checks` (only doors.lua names SequenceBreak among the
  pack's scripts; so2_door_access is 5); tools/ap_poptracker_smoke.py step 8 prints the door
  section's `AccessibilityLevel` and checks it is SequenceBreak. Checked in PopTracker's source
  that a `^$` rule returning 5 gives SEQUENCE_BREAK (core/tracker.cpp) and yellow (StateColors[3]).
  Results: `tools/build_poptracker.sh all`: 1,201 files, zip 5,559,656 bytes (sha256 db2a9931...),
  pack checker strict "Validated 11 files" (zip + folder), versions.json ok, **pack tests 42/42 OK**;
  real PopTracker 0.36.0 smoke **45/45** (the new check: "PopTracker says 5"), peak 558 MB.
  Docs: poptracker.md (door markers, ER seam, Testing, smoke result), tracker_maps.md (part 2
  status), setup_en.md, the 0.2.2 / pack 1.0.0 notes, windows_check item 11, checklist 12.3 rows,
  the wiki's Trackers.md.

- **C. test_tracker_maps.cpp's scratch folders** (utzip2, verified): `scratch()` makes
  `<temp dir>/ss_tracker_maps_test_<name>` (one level, removed whole by each test), as b82b176 did
  for test_tracker_job.cpp. engine/build-utzip, `SS_TEST=tracker_` from a scratch cwd with a
  scratch TMPDIR (scratchpad/utzip/run_tests.sh C tracker_): 10 tests, 0 failed; nothing left in
  the cwd or the TMPDIR.

### In progress
- A (the UT zip): design above; utzip2 finishing it (setup, tests, docs, ut_map.py).

### Next
- A's port side, then A's world/docs side.

### Counts changed in the release docs (for the RC agent to recheck)
- Pack tests 41 -> **42** (checklist 12.3 "The pack's tests"; poptracker-1.0.0.md "its own tests
  (42"; poptracker.md "Testing").
- PopTracker smoke 44 -> **45** (checklist 12.3 PopTracker row and the door-click row;
  poptracker-1.0.0.md "45 checks"; 0.2.2.md "its 45 checks"; poptracker.md smoke result).
- Pack zip 5,559,587 -> **5,559,656** bytes (checklist 12.3 "Build and check"; 12.2's prose still
  says maptab's 5,559,587 with "at maptab's build"; the RC's own build gives the final number;
  "about 5.6 MB" in the notes unchanged). 1,201 files unchanged.

## Log
- 2026-10-27: B done and verified (pack tests 42/42, smoke 45/45); committing docs then code.
- 2026-10-27: log created; read phaseb.md, maptab.md, setupthread.md, mapdoors.md,
  tracker_maps.md, release.md "0.03.00".
- 2026-10-28 (coordinator): **utzip stopped mid-task** (the account's weekly API limit, HTTP 429)
  while adding `--ut-zip FILE` / `--no-ut-zip` to main.cpp. Nothing of A is committed. Left
  uncommitted in the tree: engine/src/util/zip_writer.{h,cpp} (new), engine/src/util/deflate.{h,cpp},
  engine/src/tools/tracker_maps.{h,cpp} (+~230 lines), engine/src/main.cpp (flags, unfinished),
  engine/CMakeLists.txt, tools/ap_poptracker.py (writes boards.json: the boards' cells and labels and
  UT's player marker, for the port to draw them into the UT zip), tools/poptracker/boards.json (new;
  the committed copy the port compiles in). The design was never written here: read the diff. A
  fresh agent (utzip2) continues A, then C.
- 2026-10-28 (utzip2): read the log, phaseb.md, maptab.md, setupthread.md, release.md "0.03.00",
  tracker_maps.md, the diff; wrote "## Design" above from it. The tree builds as left (no warning).
  C done and verified (State); committing C.
