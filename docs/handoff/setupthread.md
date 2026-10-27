# Handoff: setupthread (the setup button's render on a worker thread; maps.csv / doors.csv)

Agent "setupthread", started 2026-10-27 by the Phase B coordinator (docs/handoff/phaseb.md), for
port 0.03.00. Two port-side follow-ups of the tracker work: mapdoors (docs/handoff/mapdoors.md)
made the render ~20.8 s (861 images, ~135 MB), and the setup button (portcheck,
docs/handoff/portcheck.md) runs it on the setup window's own thread, so the window doesn't redraw
meanwhile; and the modding reference wants regenerating for mapdoors' 33 new names.

## The brief
1. The setup button's render on a worker thread: `tools::render_tracker_maps` on a worker with a
   progress report (images done / total, the current image or area name) shown by the UI while it
   runs; the existing "Done: ... written to <folder>" / error / Open folder outcome; the button
   disabled while running; a clean shutdown if the window is closed mid-render (signal the worker
   to stop between images, join it; no half-written PNG left as a final file: temp name + rename).
   Check thread safety honestly (what the render touches vs what the setup UI uses at the same
   time; its own disc handle if needed). A progress callback on the render function (the CLI may
   print progress too, without changing its exit codes or output files). Tests: a unit test of the
   progress/cancel path (headless; SS_DISC1 for the disc case), the CLI e2e
   (tools/ap_tracker_maps_test.py: same bytes as before), the scripted setup run headless with
   screenshots (mid-render with progress if possible, and done). ASan/TSan if the flow allows.
2. Regenerate docs/modding/reference/maps.csv and doors.csv via `tools/mod_reference.py
   <DISC1.cue>` for mapdoors' 33 new names; the diff only what the names explain; run
   tools/test_mod_reference.py (and tools/test_mod_check.py if relevant).
3. Docs: docs/ap/tracker_maps.md (the setup button: progress, cancel), docs/release_notes/0.03.00.md
   (replace the "longer render" note), this log.

Files I may touch: engine/src/setup/**, engine/src/tools/tracker_maps.* (the progress/cancel hook
only), engine/src/main.cpp (CLI progress lines only), engine/tests/**, engine/CMakeLists.txt only
if needed, docs/modding/reference/maps.csv and doors.csv (regenerated, not hand-edited),
docs/ap/tracker_maps.md, docs/release_notes/0.03.00.md, docs/handoff/setupthread.md.
NOT: apworld/, tools/poptracker/**, tools/ap_poptracker*.py, tools/ap_tracker_maps.py,
docs/ap/tracker_maps.json, docs/modding/reference/map_names*.csv, docs/ap/poptracker.md,
docs/ap/seed_format.md, docs/ap/apworld.md, docs/ap/data_storage.md, docs/release_notes/0.2.2.md
(agent "storyaware" works on the world and pack in parallel and may commit at the same time).

## Rules given
- Headless only (--headless, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy); the setup
  program and every child run with DISPLAY, WAYLAND_DISPLAY and DBUS_SESSION_BUS_ADDRESS unset
  (or under ~/.local/bin/xvfb-run), never on the real desktop; nothing may open a window or dialog
  on the user's desktop.
- Every game process under tools/memcap.py (4 GB; a cgroup MemoryMax for ASan/TSan); at most 2
  game processes at once; own build dirs engine/build-setupthread*, builds -j6.
- Scratch XDG_CONFIG_HOME and HOME for every run; never a config-saving flag, never
  ~/.config/shattered-stars/config.ini, the user's saves, a real PopTracker folder or
  ~/.local/share/Archipelago; kill only by exact PID.
- Disc 1: SS_DISC1 = "/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/Star Ocean - The Second Story (Disc 1).cue".
- Commit on main after each verified piece, docs first, staged by path (never git add -A / -u);
  message ends with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never
  commit build output or images. Never push, tag or publish. index.lock: wait and retry, never
  delete it.
- A denied tool call: don't retry; write the blocked step here and stop, naming the exact command.
- Cap command output (head/tail/grep); don't pipe a test binary's output into a pipe drained only
  later (write to a file); long jobs in the background with deadlines; scratch under
  scratchpad/setupthread/ (not /tmp, which is RAM); delete big scratch when done.
- Keep this log current after each finished step and at least every 30 minutes. Roll over at
  ~400k context: update the log and stop.

## State
- **Task 2 done** (maps.csv / doors.csv regenerated; see Log 17:40). Committed (e4f89b1).
- **Task 1 done and verified** (plain, ASan, TSan, MinGW + Wine; see Log): committed as docs
  then code (ids in the Log). Files: engine/src/tools/tracker_maps.{h,cpp} (progress/stop hook,
  temp name + rename), engine/src/main.cpp (CLI progress lines), engine/src/setup/tracker_job.{h,cpp}
  (new: the worker), engine/src/setup/setup_ui.{h,cpp}, setup_main.cpp, engine/CMakeLists.txt
  (setup and ss_game_tests get tracker_job.cpp + Threads::Threads), engine/tests/test_tracker_job.cpp.
- **Task 3 done**: docs/ap/tracker_maps.md ("The port's command": the .part rename, the console
  progress, the progress function; "The setup button": the thread, progress, stopping on close,
  thread safety, the wait-tracker-render script actions), docs/release_notes/0.03.00.md (the
  setup button shows its progress, setup stays usable, closing stops it keeping the images).
- Baseline: engine/build-setupthread built from 994c8ab; its CLI render's sha256 of all 861
  images in scratchpad/setupthread/base_manifest.txt; binaries in scratchpad/setupthread/base/.
- How to verify: `SS_TEST=tracker_ SS_DISC1=<cue> engine/build-setupthread/ss_game_tests` (10);
  `SS_DISC1=<cue> tools/ap_tracker_maps_test.py engine/build-setupthread/shattered-stars --scratch
  scratchpad/setupthread/e2e/root`; `scratchpad/setupthread/setup_runs.sh
  engine/build-setupthread/shattered-stars-setup scratchpad/setupthread/runs [A B C D]` (A
  mid-render screenshot, B done + bytes vs baseline, C no disc, D close mid-render).
- Open (not mine): chests.csv / shops.csv regeneration (names only); the session-bus finding.

## Design (task 1)
- `tools::TrackerRenderOptions::progress` (std::function<bool(const TrackerRenderProgress&)>):
  called before each image ({done, total, path, kind area/world/room, name}) and once after the
  last (path empty, done == total); false = stop before the next image, and
  render_tracker_maps returns `tools::kTrackerRenderStopped` (2; the CLI never sets a progress
  function that stops, so its exit codes stay 0 / 1). Each PNG is written as `<name>.part` and
  renamed into place (if the rename fails, the old file is removed and the rename retried, for a
  system that won't rename over a file); a failed write removes the .part.
- CLI (main.cpp): "tracker maps: rendering N image(s) into DIR" first, "K of N done" every 100,
  besides the per-image lines it already printed; output files and exit codes unchanged.
- `setup::TrackerRenderJob` (engine/src/setup/tracker_job.{h,cpp}, no ImGui): start(disc1 path,
  dir, options) makes a std::thread that opens its **own** disc::DiscImage + disc::Archive from
  a copy of the path, sets options.progress to copy the progress into `status_` under a mutex
  and return `!stop_` (std::atomic<bool>), and records the outcome (kDone / kErrors / kStopped /
  kNoDisc / kFailed: an exception is caught in the thread) with running = false. status() copies
  the state and joins a finished worker; stop() sets the flag and joins; the destructor stops.
  All members are called from the owner's thread only.
- App (setup_ui): start_tracker_render() checks Disc 1, resolves the folder on the window's
  thread (poptracker_override_dir reads getenv / SDL_GetUserFolder) and starts the job; each
  frame() copies job.status() into tracker_status_; the page shows a progress bar ("K of N
  images") and "Rendering room 92 Armory..." / "the area map of X" / "the world map of X", the
  button disabled while running, then Done (count + folder + Open folder) / errors / no disc.
  ~App stops and joins (window closed, Save & Play) and prints the outcome line. The loop waits
  at most 100 ms for events while rendering (250 ms otherwise) so the bar moves when idle.
- Scripts: `render-tracker-maps` also scrolls the Trackers section into view (it lies below the
  fold at 920x660); `wait-tracker-render[:N]` holds the script until the render is over (or N
  images are done), and those frames don't count towards --frames (setup_main), with a 10 ms
  sleep per frame in screenshot runs so the unthrottled loop leaves the CPU to the render.
  stdout: "setup: tracker maps: rendering into DIR" and "setup: tracker maps: <done|stopped|...>
  (K of N images; F frames drawn while it ran)".

## Thread safety (what the worker touches vs the window's thread)
- Disc: its own DiscImage (own std::ifstream) and Archive; the window reads the same file only
  through its own handles (disc_info, the title logo). Separate read-only handles are safe.
- The layout: tracker_layout_json() is a string_view of constant data; each render parses its
  own game::ap::Json (the shared `kNull` is a const global, read only).
- render_tracker_maps and everything below it (render_field_background, render_world_map,
  disc::slz, util::deflate / encode_png_rgb, runtime::overlay_font_column) keep no mutable global
  state: checked by grep, the only statics are constexpr tables and two function-local
  `static const` CRC tables (C++11 thread-safe initialisation, read-only after).
- Output: std::printf / fprintf (the C library locks the stream per call), std::filesystem
  calls (system calls). No logger, no setenv anywhere in setup or launcher.
- getenv / SDL_GetUserFolder only on the window's thread (the folder is resolved before start).
- Shared state: TrackerRenderJob::status_ (mutex) and stop_ (atomic) only; CHECKs in the unit
  test run on the test's own thread (the harness counter is a plain int).

## Log
- 2026-10-27 17:27: log created; read phaseb.md, portcheck.md, mapdoors.md, tracker_maps.md.
- 2026-10-27 17:40: **task 2**: `python3 tools/mod_reference.py <DISC1.cue> --out
  scratchpad/setupthread/modref` (7 s, every check "ok"), then only maps.csv and doors.csv copied
  into docs/modding/reference/. Diff checked by script against map_names.csv before (ae78aa2^)
  and after: maps.csv changes exactly the 33 renamed maps, columns `name` and `place` only, each
  new `place` = map_names.csv's; doors.csv 162 cells in 124 rows, only `map_name` (80) and
  `target_name` (82), each the old -> new name of a renamed map; no other column, no row added
  or removed. tools/test_mod_reference.py (SS_DISC1 set, so FromDisc ran too) 21 OK;
  tools/test_mod_check.py 24 OK. **Not mine, left open:** the same regeneration also changes
  chests.csv (12 `map_name` cells) and shops.csv (14 `where` cells: the Eluria arsenal, map 233),
  both only by the new names; items.csv, enemies.csv and README.md are unchanged. Those two
  files are outside my brief, so they are not committed: whoever may touch them runs the same
  command and copies chests.csv and shops.csv too.
- 2026-10-27 17:45: baseline for "same bytes": engine/build-setupthread from 994c8ab (configure
  as docs/handoff/partybuild.md, deps from engine/build-hubleader/_deps; 370/370, 3 old warnings
  in other files), CLI render 20.5 s, 861 images, manifest kept, images deleted.
- 2026-10-27 17:47: task 1 on the plain build (engine/build-setupthread, no new warning):
  ss_game_tests `SS_TEST=tracker_` with SS_DISC1: **10/10** (7 old + tracker_progress_no_disc,
  tracker_job_no_disc, tracker_progress_disc; the job's stop came 0.037 s after stop() at 4 of
  793). CLI e2e `tools/ap_tracker_maps_test.py`: all checks passed (861 images twice, 20.8 s
  each, nothing stray, XDG untouched), and both runs' 861 sha256 **equal the baseline
  manifest** (before this change). Setup headless (scratchpad/setupthread/setup_runs.sh, runs
  under scratchpad/setupthread/runs/): A mid-render screenshot at 150 images (A_mid.png: bar
  "150 of 861 images", "Rendering room 92 Armory..."), then the exit stopped it: 153 PNGs, no
  .part, 815 frames drawn while it ran, rc 0, 8.6 s; B to the end (B_done.png: "Done: 861
  tracker map images were written to ..." + Open folder): 2014 frames drawn during the render,
  861 PNGs = the baseline bytes, rc 0, 20.9 s; C no disc: refused, nothing written (C_nodisc.png);
  D the close action mid-render (scratch config.ini names the disc, so nothing unsaved): quit at
  51 images, 51 PNGs, no .part, config.ini unchanged, rc 0, 3.8 s. Nothing written to the
  scratch HOME besides the PNGs, nothing to XDG in A-C.
- 2026-10-27 18:00: full plain suites: ss_game_tests (SS_DISC1) **226/0** (223 + my 3),
  ss_tests 103/0.
- ASan (engine/build-setupthread-asan: GCC, `-fsanitize=address -fno-omit-frame-pointer`, the
  copied libasan in scratchpad/setupthread/asan; ASAN_OPTIONS=detect_leaks=1; a systemd user
  scope with MemoryMax=6G): `SS_TEST=tracker_` 10/10, rc 0, no report (leaks included); CLI
  full render 861 images, rc 0, no report, **the baseline bytes**; setup runs A (stopped at 152)
  / B (to the end: 861 = baseline bytes, 46 s) / C (no disc) / D (close at 51): no ASan error;
  each exits 1 only for LeakSanitizer's 248 bytes in 2 allocations under SDL_CreateWindow ->
  OFFSCREEN_GLES_LoadLibrary (Mesa / libstdc++ frames; the same in C, where no render starts):
  not ours, before this change too.
- TSan: clang can't build the tree (mods/catalog.cpp: libstdc++ 16 + clang 22 reject a vector of
  an incomplete pair in an anonymous namespace; not my file), so GCC's TSan with libtsan taken
  the HANDOFF.md way: `dnf download libtsan-16.2.1-2.fc44` (no root), rpm2cpio | cpio into
  scratchpad/setupthread/tsan, `libtsan.so -> libtsan.so.2.0.0`, `-fsanitize=thread
  -L<dir> -Wl,-rpath,<dir>` (engine/build-setupthread-tsan; targets shattered-stars-setup and
  ss_game_tests). Negative control: a deliberate race (scratchpad/setupthread/tsan_control.cpp)
  is reported (rc 66), so the runtime works. `SS_TEST=tracker_` under TSan: **10/10, rc 0, no
  report** (the job tests: polling status() against the worker, stop mid-render after 0.395 s,
  the destructor's stop). Setup runs A/C/D under TSan: no race; the one report in each (also in
  C, with no render) was a lock-order inversion inside libdbus (internal_bus_get /
  _dbus_connection_*), on the main thread.
- **Finding for the coordinator (desktop isolation):** that libdbus report shows SDL in the setup
  program connects to the **desktop's session bus** even with DBUS_SESSION_BUS_ADDRESS unset:
  libdbus falls back to `$XDG_RUNTIME_DIR/bus`. Nothing was shown (SDL reads things like the
  portal's colour-scheme setting), but unsetting the variable alone doesn't keep a headless run
  off the bus. With XDG_RUNTIME_DIR pointed at a scratch dir as well, the connection (and the
  report) is gone: scratchpad/setupthread/run.py now does that for every run (for a sanitizer
  run, inside the systemd-run scope via `env -u DBUS_SESSION_BUS_ADDRESS XDG_RUNTIME_DIR=...`,
  since systemd-run itself needs the real one). Worth adding to the shared rules.
- 2026-10-27 18:04: TSan setup runs again with the scratch XDG_RUNTIME_DIR: C (no disc) rc 0 and
  A (mid-render screenshot, stopped at 152 by the exit, 8795 frames drawn in 112 s) and D
  (close at 51) rc 0 with **no ThreadSanitizer report at all**. B (to the end) running.
- Windows: engine/build-setupthread-win (MinGW, as tools/build_windows.sh; deps from
  engine/build-win-release/_deps, the existing scratchpad/setup/mingw-ssl sysroot): setup.exe,
  shattered-stars.exe, ss_game_tests.exe 359/359, no warning in my files. Under Wine (my own
  scratch prefix, scratchpad/setupthread/wine.sh: portcheck's recipe plus a scratch
  XDG_RUNTIME_DIR): ss_game_tests.exe `SS_TEST=tracker_` with SS_DISC1 **10/10** (the job's
  thread and stop on Windows threads: stopped at 4 of 793 in 0.032 s); shattered-stars.exe
  `--render-tracker-maps` twice into one folder (SS_TRACKER_ONLY three images): rc 0 both, the
  second replacing the first's files, no .part left, the bytes equal the Linux baseline's.
  Prefix (2 GB) and outputs deleted afterwards.
- 2026-10-27 18:07: TSan B (to the end): rc 0, **no report**, 861 PNGs = the baseline bytes,
  21615 frames drawn during the 277.6 s TSan render, no .part. TSan done: unit 10/10 and setup
  A/B/C/D all without a report once the session bus is out of reach.
- 2026-10-27 18:07: committed 5eabbee (docs: tracker_maps.md, 0.03.00 notes, this log) and
  1fe705a (code: tracker_maps.{h,cpp}, main.cpp, setup/tracker_job.{h,cpp}, setup_ui.{h,cpp},
  setup_main.cpp, CMakeLists.txt, tests/test_tracker_job.cpp); earlier e4f89b1 (maps.csv /
  doors.csv). Scratch cleaned: the MinGW tree, the Wine prefix, the ASan and TSan build dirs
  (recipes above; scratchpad/setupthread/build_san.sh has the ASan line, the TSan configure is
  in the Log entry of 18:00) and every rendered image deleted; kept engine/build-setupthread (the
  plain build), scratchpad/setupthread's logs, scripts (run.py, setup_runs.sh, wine.sh),
  base_manifest.txt, the libasan / libtsan copies and the screenshots (runs/A_mid.png,
  runs/B_done.png, runs/C_nodisc.png; the ASan / TSan runs' beside them). **setupthread done.**
