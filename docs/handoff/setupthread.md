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
- **Task 2 done** (maps.csv / doors.csv regenerated; see Log 17:40). Committed as docs.
- Task 1 (worker thread): design below; baseline taken (engine/build-setupthread built from
  994c8ab; its CLI render's sha256 of all 861 images in scratchpad/setupthread/base_manifest.txt,
  the binaries copied to scratchpad/setupthread/base/).

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
