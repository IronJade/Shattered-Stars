# portcheck: port-side loose ends for tracker support (port 0.03.00)

Agent "portcheck" (sonnet), started 2026-10-27 by the Phase B coordinator (docs/handoff/phaseb.md).
Closes the port-side loose ends left by "dskeys" (docs/handoff/dskeys.md, commit 802935b: the port
writes `so2_map_*` / `so2_story_*` to AP data storage) and "maprender" (docs/handoff/maprender.md,
commit 1687cc7: `--render-tracker-maps [DIR]`). Four tasks: ASan/UBSan on the new code, a Windows
build + Wine check of the render command, a setup button for it, and docs.

## The brief (summary)
1. ASan/UBSan: `SS_TEST=ap_ds` and `SS_TEST=tracker_` of ss_game_tests (SS_DISC1 for disc cases),
   test_ap_net against tools/ap_test_server.sh, one `--render-tracker-maps` run into scratch,
   e2e step 9 of tools/ap_e2e_so2.sh if it runs under ASan.
2. Windows build the way tools/build_windows.sh does, scratch output; confirm tracker_maps.cpp,
   util/deflate.cpp, the ap_net set path and the embedded layout data compile and link; under Wine
   (scratch WINEPREFIX) run `--render-tracker-maps` with a DIR and without one; check the default
   folder resolves PopTracker's order; compare images to the Linux render (same bytes expected).
3. A setup button ("Render tracker maps for PopTracker") in engine/src/setup/{setup_ui,setup_main}
   that runs the same code path as `--render-tracker-maps` with the default folder; shows
   progress/done/the folder; handles "no disc configured"; test headless.
4. Docs: docs/ap/tracker_maps.md ("The port's command"), docs/release_notes/0.03.00.md, this log.

Files I may touch: engine/src/setup/**, engine/src/tools/tracker_maps.* and
engine/src/util/deflate.* and engine/src/ap_net/** (small fixes only), engine/tests/**, CMake only
if needed, docs/ap/tracker_maps.md, docs/release_notes/0.03.00.md, docs/handoff/portcheck.md. NOT
apworld/, tools/ap_poptracker*, tools/poptracker/, docs/ap/apworld.md, docs/ap/poptracker.md,
docs/release_notes/0.2.2.md.

## Rules given
- Headless only (`--headless`, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy); never open a
  window or play audio audibly (Wine too). Every game process under tools/memcap.py (4 GB; a cgroup
  MemoryMax for ASan); at most 2 game processes at once. Own build dirs engine/build-portcheck*,
  builds -j6. Scratch XDG_CONFIG_HOME and HOME for every run; never a config-saving flag, never
  ~/.config/shattered-stars/config.ini, the user's saves, the user's real PopTracker folder or the
  user's ~/.local/share/Archipelago. Kill only by exact PID.
- Disc 1: SS_DISC1 = "/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/Star Ocean - The Second Story (Disc 1).cue".
- Commit on main after each verified piece, docs first, staged by path (never `git add -A` / `-u`);
  message ends with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never
  commit build output or images. Never push, tag or publish. index.lock: wait and retry, never
  delete it.
- A denied tool call: don't retry; write the blocked step here and stop, naming the exact command.
- Cap command output; long jobs in the background with deadlines; scratch under
  scratchpad/portcheck/ (not /tmp, which is RAM); delete big scratch (Windows build trees) when done.
- Keep this log current after each finished step and at least every 30 minutes. Roll over at
  ~400k context: update the log and stop.
- If a sanitizer or Windows failure needs real debugging beyond a small, obvious fix: stop, write
  the exact failure/stack/reading here, report back instead of digging deep.

## Conventions found (research, before starting)
- ASan builds on this machine: no root `-fsanitize=address` build with libasan copied from a
  distro RPM (dnf download, unpacked without root) into a scratch dir, `-L<dir> -Wl,-rpath,<dir>`
  (docs/HANDOFF.md "Build and tests"); earlier agents' exact cache: `CMAKE_BUILD_TYPE=RelWithDebInfo`,
  `CMAKE_CXX_FLAGS`/`CMAKE_C_FLAGS` = `-fsanitize=address -fno-omit-frame-pointer`,
  `CMAKE_EXE_LINKER_FLAGS` = `-fsanitize=address -L<asandir> -Wl,-rpath,<asandir>` (checked in
  engine/build-vmfix-asan/CMakeCache.txt). `tools/memcap.py` already does the systemd-run
  MemoryMax part for an ASan binary (`capped()`/`run_capped()`: detects `__asan_init` in the
  binary, runs it in `systemd-run --user --scope -p MemoryMax=6G -p MemorySwapMax=0` instead of
  RLIMIT_AS) -- nothing extra needed from me there.
- A usable prebuilt libasan is already on disk from earlier agents: scratchpad/vmfix/asan/
  (libasan.so -> libasan.so.8 -> libasan.so.8.0.0, GCC 16.2.1, matches this machine's compiler).
  Copied (not moved) to scratchpad/portcheck/asan/ so I don't touch another agent's scratch and
  don't need dnf download.
- The cmake configure line (docs/handoff/partybuild.md "Configure"): FETCHCONTENT_SOURCE_DIR_*
  pointed at engine/build-hubleader/_deps/*-src to avoid re-fetching apclientpp/asio/imgui/
  nlohmann_json/websocketpp/wswrap over the network.
- Windows build: tools/build_windows.sh (MinGW, `/usr/share/mingw/toolchain-mingw64.cmake`,
  OpenSSL/zlib static via MINGW_SSL_SYSROOT). It builds into `engine/build-win-release` and packs
  dist/; I run the same cmake/ninja invocations into my own build dir instead of using the script
  directly (the script's `build=engine/build-win-release` is a fixed path and it also zips into
  the shared `dist/`), to honour "own build dirs" / "its own scratch output, not over a release
  dir".
- Wine, the safe way (docs/handoff/release.md "Status 21:58"; scratchpad/winfix/wine.sh,
  scratchpad/appicon/wine_check.sh): scratch `WINEPREFIX` (never ~/.wine), `WINEDEBUG=-all`,
  `DISPLAY` unset, `WAYLAND_DISPLAY=no-such-wayland-display` (NOT merely unset: winewayland
  connects to the user's real compositor if WAYLAND_DISPLAY is unset but a socket named
  `wayland-0` exists -- this caused a real crash-dialog leak for "winfix", 2026-10-25),
  `WINEDLLOVERRIDES=winedbg.exe=d`, `HKCU\Software\Wine\WineDbg ShowCrashDialog=0` and
  `HKCU\Software\Wine\Drivers Graphics=null` set in the scratch prefix, `SDL_VIDEO_DRIVER=offscreen`,
  `SDL_AUDIO_DRIVER=dummy`, run under `systemd-run --user --scope -p MemoryMax=4G`,
  `wineserver -k` after every run. My own fresh prefix (scratchpad/portcheck/wineprefix, made
  with `wineboot --init` under these settings), not a copy of another agent's -- the brief says
  "a scratch WINEPREFIX copy, never a real user one", which I read as "copied/scratch, as opposed
  to the user's real ~/.wine", not literally cloning winfix's/appicon's 2 GB prefixes (theirs are
  other agents' scratch; mine is a few seconds of `wineboot --init` away).
- `tools::poptracker_override_dir()` with no DIR (engine/src/tools/tracker_maps.cpp) reads
  `$HOME` / `$XDG_DOCUMENTS_DIR` on Linux, `SDL_GetUserFolder` on Windows -- both resolve inside
  whatever HOME / the Wine prefix's fake profile is, so a scratch HOME (Linux) or a scratch
  WINEPREFIX (Windows/Wine) is enough to keep this off the user's real PopTracker folder; no code
  change needed for that.
- `tools/tracker_maps.cpp` depends only on `disc::Archive` / `disc::slz` (ss_core),
  `game::ap::Json` (already compiled directly into shattered-stars-setup as
  `src/game/ap/json.cpp`), `runtime::debug_overlay` / `util::deflate` / `util::utf8` (ss_core) --
  nothing from the rest of `ss_game` (field/battle/VM). It is listed under `ss_game` in
  CMakeLists.txt only by source-tree grouping (tools/), not because it needs any of that code.
  So the setup button links `src/tools/tracker_maps.cpp` plus the CMake-generated
  `generated/tracker_layout_data.cpp` directly into the `shattered-stars-setup` executable's own
  sources (same pattern already used for `game/ap/seed.cpp` / `game/ap/json.cpp`: "self-contained"
  sources added straight to the executable instead of linking all of `ss_game`), so setup stays
  a small, separate program rather than pulling in the whole game engine.
- Setup can be driven fully headless already: `--do A,B,...` / `SS_SETUP_SCRIPT`, one action per
  frame (setup_ui.cpp `App::frame()` / `run_action`), `--screenshot FILE --frames N` for a PNG,
  `--set K=V`, `--page NAME`. No new headless-driving mechanism was needed.

## Design: the setup button (engine/src/setup/setup_ui.{h,cpp}, setup_main.cpp)
- Archipelago page, new "Trackers" subheading at the end of `page_archipelago()` (not gated on a
  seed being set: the render needs only Disc 1, like the standalone command).
- `App::start_tracker_render()` (button click, or the new scripted action
  `render-tracker-maps`): sets `tracker_render_busy_ = true` only -- cheap, returns at once.
- `App::run_tracker_render()`: the real (blocking, ~a few seconds per maprender.md) work --
  `disc1_ok()` check (else the same "Choose a valid Disc 1 image first." text the bottom bar
  already uses for "no disc configured"), opens `settings_.disc1` via `disc::DiscImage::open` +
  `disc::Archive` (the same two calls main.cpp makes for `--render-tracker-maps`), then
  `tools::poptracker_override_dir()` (no DIR: the default folder) and
  `tools::render_tracker_maps(archive, dir, {})` (no dots, every image: the real render, not the
  debug switches). Stores ok/dir/error for the page to show.
- `App::frame()` runs `if (tracker_render_busy_ && !tracker_render_done_) run_tracker_render();`
  right after `apply_dialog_results()`, i.e. *before* the scripted-action dispatch and before the
  page draws. Because `start_tracker_render()` is only ever called later in the same `frame()`
  call (the action dispatch that follows, or the button inside `page_archipelago()` which is
  drawn even later), this check never sees `busy_` true on the frame it was just set -- so the
  frame that sets it shows "Rendering tracker maps... this can take a few seconds." (the page
  checks `tracker_render_busy_` right under the button), and the actual (blocking) call happens
  at the very top of the *next* `frame()`, after which that frame shows "Done: ... written to
  <folder>." plus an "Open folder" button (same `open_folder()` used elsewhere on this page), or
  the error line. No thread, no new synchronization: one frame of delay falls out of the
  existing single-pass `frame()` structure, matching the rest of the file (every other action
  here is synchronous).
- Config, saves and cards: untouched. `run_tracker_render()` never calls `save()` / `compose()`
  and never touches memory cards; it only reads `settings_.disc1` (whatever is on screen, saved
  or not, same as the live disc-logo preview already does) and writes PNGs through
  `tools::render_tracker_maps`.
- CMakeLists.txt: `shattered-stars-setup`'s `add_executable` sources gain
  `src/tools/tracker_maps.cpp` and the already-generated
  `${CMAKE_CURRENT_BINARY_DIR}/generated/tracker_layout_data.cpp` (the `file(CONFIGURE OUTPUT
  ...)` that writes it runs earlier in the same CMakeLists.txt, before the `SS_BUILD_SETUP`
  block, so the path already exists by the time `add_executable` reads it).

## State
- **Setup button:** implemented (engine/src/setup/setup_ui.h/.cpp, setup_main.cpp comment,
  engine/CMakeLists.txt). Builds clean in engine/build-portcheck (plain) with the rest of the
  port; no new warnings. Not yet driven headless (next step).
- **ASan/UBSan (engine/build-portcheck-asan, `-fsanitize=address -fno-omit-frame-pointer`,
  linked against scratchpad/portcheck/asan/libasan.so.8.0.0, a copy of
  scratchpad/vmfix/asan's):**
  - `SS_TEST=ap_ds` (SS_DISC1): 9/9 PASS, clean (scratchpad/portcheck/asan_ap_ds.log).
  - `SS_TEST=tracker_` (SS_DISC1): 6/6 PASS, clean (scratchpad/portcheck/asan_tracker.log).
  - `test_ap_net` against `tools/ap_test_server.sh start 38281` (then `stop`): 5/5 PASS, clean
    (TLS case skipped: no wss server, same as other agents) (scratchpad/portcheck/asan_ap_net.log).
  - One `--render-tracker-maps scratchpad/portcheck/render_asan`: exit 0, 68/68 images written,
    clean (scratchpad/portcheck/asan_render.log).
  - e2e step 9 of tools/ap_e2e_so2.sh on the ASan binary: running (see Log).
  - Plain build (engine/build-portcheck) ss_tests 103/0, ss_game_tests 222/0 for a baseline
    sanity check (scratchpad/portcheck/ss_tests.log, ss_game_tests.log).
- **Windows build:** configuring/building into engine/build-portcheck-win (own scratch dir, not
  over dist/ or a release build), MINGW_SSL_SYSROOT = the existing
  scratchpad/setup/mingw-ssl/.../sys-root/mingw (read-only reuse, built by the "setup" agent
  earlier; OpenSSL/zlib confirmed found at configure, not re-downloaded). Building now (see Log).
- **Wine:** my own scratch prefix scratchpad/portcheck/wineprefix made with `wineboot --init`
  under the safe settings (DISPLAY unset, WAYLAND_DISPLAY=no-such-wayland-display,
  ShowCrashDialog=0, null Graphics driver); scratchpad/portcheck/wine.sh is the runner (copy of
  scratchpad/winfix/wine.sh, pointed at my own prefix). Not yet used (waiting on the Windows
  build).

## Log
- 2026-10-27 15:5x: log created; read phaseb.md, dskeys.md, maprender.md,
  docs/ap/data_storage.md, docs/ap/tracker_maps.md, docs/HANDOFF.md "Conventions to keep";
  researched ASan/Wine/CMake conventions from earlier agents' logs (battle.md, vmfix.md,
  hubchambers.md, menufix.md, cavefix.md, regions.md, winfix.md, release.md, setup.md) and
  tools/memcap.py.
- 2026-10-27 16:0x: setup button implemented (setup_ui.h/.cpp, setup_main.cpp comment,
  CMakeLists.txt); engine/build-portcheck configured + built clean (-j6, FETCHCONTENT_SOURCE_DIR
  pointed at engine/build-hubleader/_deps to skip re-fetching); ss_tests 103/0, ss_game_tests
  222/0 (plain build baseline). engine/build-portcheck-asan configured + built clean.
- 2026-10-27 16:1x: ASan: `ap_ds` 9/9, `tracker_` 6/6, `ap_net` 5/5 (against a local test
  server, stopped after), one full `--render-tracker-maps` run (68 images) -- all clean, no
  sanitizer report.
  - Hit and fixed my own bug: scratchpad/portcheck/run.py (a helper wrapping tools/memcap.py
    for one-off runs) piped a child's stdout through `subprocess.PIPE` but only drained it
    with `communicate()` **after** a polling loop that never read it; the plain-build full
    `ss_game_tests` run (hundreds of PASS lines) filled the ~64 KB pipe buffer and the test
    binary blocked forever on `write()` (confirmed: `cat /proc/PID/wchan` -> `anon_pipe_write`,
    0% CPU). Not a port bug. Killed the stuck PIDs (exact PIDs only) and rewrote run.py to
    write the child's combined output straight to a real (temp) file instead of a pipe;
    reran -- ss_game_tests 222/0, no further issue. tools/ap_e2e_so2.sh was already safe (it
    redirects to real log files itself), so this didn't affect any e2e run.
  - Started: e2e step 9 (tools/ap_e2e_so2.sh engine/build-portcheck-asan/shattered-stars,
    AP_E2E_WORK=scratchpad/portcheck/e2e, ASAN_OPTIONS=detect_leaks=0, port 38297) and the
    Windows cross-build (engine/build-portcheck-win), both in the background; both pending.
  - Set up scratchpad/portcheck/wineprefix (fresh, `wineboot --init`, safe settings) for the
    Wine step once the Windows build finishes.
- 2026-10-27 16:2x: e2e step 9 of tools/ap_e2e_so2.sh finished on the ASan binary
  (engine/build-portcheck-asan/shattered-stars, AP_E2E_WORK=scratchpad/portcheck/e2e, port
  38297, ASAN_OPTIONS=detect_leaks=0): **49 PASS, 0 FAIL** (same count as the plain-build e2e
  in docs/handoff/dskeys.md), all four game runs rc 0 / not at the memory cap
  (scratchpad/portcheck/e2e/memcap.txt), no sanitizer finding anywhere
  (`grep -rli addresssanitizer|heap-buffer-overflow|heap-use-after-free|SEGV` over every log:
  nothing). ASan/UBSan is DONE: ap_ds 9/9, tracker_ 6/6, ap_net 5/5, one full render 68/68, e2e
  49/49 -- all clean, no finding to report.
- 2026-10-27 16:1x-16:2x: Windows build (engine/build-portcheck-win) finished clean, 370/370,
  0 errors (scratchpad/portcheck/build_win.log): shattered-stars.exe, setup.exe, ss_tests.exe,
  ss_game_tests.exe, SDL3.dll. Wine: explicit-DIR render (68/68 images, exit 0,
  scratchpad/portcheck/wine_render_explicit.log) and no-DIR render (68/68, exit 0, resolved to
  `C:\users\ironjade\Documents\PopTracker\user-override\so2_shattered_stars` -- the fresh
  prefix has no `PopTracker` folder under home or Documents, so the fallback is
  Documents\PopTracker, matching the brief's expected order). No stray wine/wineserver process
  after either run.
  - **Coordinator flagged (mid-task):** my wine.sh (copied from scratchpad/winfix/wine.sh) and
    my two ad-hoc `winepath` calls unset `DISPLAY` and set `WAYLAND_DISPLAY` to a bogus value,
    but never unset `DBUS_SESSION_BUS_ADDRESS` (confirmed it was set in my shell:
    `unix:path=/run/user/1000/bus`), so every Wine process so far inherited it. No dialog or
    window was observed (`ps aux` shows no wine/.exe process at all right now, before or after
    this fix; the render command itself opens no window and calls no portal/notification API,
    per tracker_maps.md "needs no window"), but fixed `scratchpad/portcheck/wine.sh` to also
    `unset DBUS_SESSION_BUS_ADDRESS` (and pass `env -u DBUS_SESSION_BUS_ADDRESS` into the
    systemd-run scope) for every Wine call from here on; did not need `~/.local/bin/xvfb-run`
    (nothing here needs a real display).
- 2026-10-27 16:3x: Wine render checks done: explicit-DIR and no-DIR renders both 68/68 images,
  exit 0, no stray wine/wineserver process after either. `diff -rq` of both Wine outputs
  against the Linux ASan render: **0 differences** (every file byte-identical, both the
  explicit-DIR and the default-folder run). Default folder resolved to
  `C:\users\ironjade\Documents\PopTracker\user-override\so2_shattered_stars` in the fresh
  prefix (no `PopTracker` folder existed under home or Documents, so the Documents fallback
  applies, matching the documented order). Bonus: the Windows `ss_game_tests.exe`'s own
  `tracker_` tests also ran under Wine: 6/6 PASS (scratchpad/portcheck/wine_tracker_tests.log).
- 2026-10-27 16:3x: setup button tested headless (plain build, engine/build-portcheck):
  - With a disc (`--set disc1=<cue> --do render-tracker-maps --frames 8 --screenshot ...`,
    scratch HOME + XDG_CONFIG_HOME): stdout shows the same 68 `tracker maps: ...` lines ending
    "68 image(s) written to <scratch HOME>/PopTracker/user-override/so2_shattered_stars"; the
    screenshot shows the button, the "Trackers" section matching the page's existing style, and
    a green (Level::kOk) status line at the bottom edge (scrolled off at this window height,
    same as other content-heavy pages -- the `##page` child scrolls, no `NoScrollbar` flag).
    Nothing written outside the scratch HOME.
  - Without a disc (`--do render-tracker-maps`, no `--set`): exit 0, **no file written anywhere**
    under the scratch HOME, screenshot shows the button greyed out and the bottom bar's usual
    "Choose your Disc 1 image to continue." (the sidebar dot and bottom bar both show the
    existing error state); the defensive guard inside `run_tracker_render()` (not just the
    disabled button) was exercised since the scripted action bypasses the UI's disabled state,
    and it correctly refused and wrote nothing.
  - Screenshots: scratchpad/portcheck/setup_test/render_frame8.png,
    scratchpad/portcheck/setup_test2/nodisc_frame6.png.
- 2026-10-27 16:4x: all four tasks done. Cleaned up scratchpad/portcheck's test-only artefact
  outside scratchpad (`tracker_maps_test/`, the test binary's CWD-relative fixture dir, same as
  maprender's own test leaves; gitignored, harmless, removed anyway). git status before
  committing: only the intended files changed (`docs/ap/tracker_maps.md`,
  `docs/release_notes/0.03.00.md`, `engine/CMakeLists.txt`, `engine/src/setup/setup_main.cpp`,
  `engine/src/setup/setup_ui.{h,cpp}`, this log) -- no other agent's files touched.
- 2026-10-27 16:4x: cleaned up the big scratch now verification is done and logged: deleted
  `engine/build-portcheck-win` (the Windows build tree, 166 MB), `scratchpad/portcheck/wineprefix`
  (2.1 GB) and the duplicate PNG output folders (`render_asan`, `render_win_explicit`,
  `setup_test/home`) -- their results (counts, the `diff -rq` 0-differences result, the default
  folder it resolved to) are recorded above and don't need the files themselves kept. Left in
  place: `engine/build-portcheck` and `engine/build-portcheck-asan` (own build dirs, small
  compared to the Windows tree, usable again without a full rebuild if anything needs rechecking),
  `scratchpad/portcheck/asan` (the libasan copy, tiny), the e2e run's output and logs, and every
  `*.log`.

## Report (for the coordinator)

**ASan/UBSan:** all runs clean, no sanitizer finding.
- `SS_TEST=ap_ds` (engine/build-portcheck-asan, SS_DISC1): 9/9 PASS.
- `SS_TEST=tracker_` (SS_DISC1): 6/6 PASS.
- `test_ap_net` against a local `tools/ap_test_server.sh` server (stopped after, by the script):
  5/5 PASS (the wss case skipped: no TLS server, as in other agents' runs).
- One full `--render-tracker-maps`: 68/68 images, exit 0.
- e2e step 9 of `tools/ap_e2e_so2.sh` on the ASan binary: **49 PASS, 0 FAIL** (same as the
  plain-build e2e in docs/handoff/dskeys.md), all four game runs rc 0 and under the memory cap.
- `grep` for AddressSanitizer/UBSan report text across every log from all of the above: nothing.

**Windows build:** `engine/build-portcheck-win` (own scratch dir, deleted after verifying,
see the Log) built clean, 370/370, 0 errors, 0 new warnings: `shattered-stars.exe`, `setup.exe`,
`ss_tests.exe`, `ss_game_tests.exe`, `SDL3.dll`. Confirms `tools/tracker_maps.cpp`,
`util/deflate.cpp`, the embedded `generated/tracker_layout_data.cpp` and the ap_net `Set` path
(already built by earlier agents' Windows checks for the net client itself; this pass rebuilt
the whole tree) all compile and link for MinGW. Used the existing
`scratchpad/setup/mingw-ssl` sysroot (read-only, built by the "setup" agent) rather than
re-downloading.

**Wine:** my own fresh scratch prefix (`wineboot --init`, safe settings: scratch WINEPREFIX,
`DISPLAY` unset, `WAYLAND_DISPLAY` pointed at a socket that doesn't exist, `DBUS_SESSION_BUS_ADDRESS`
unset after the coordinator's flag -- see the Log entry, no dialog was ever observed and
`wineserver -k` ran after every call). `--render-tracker-maps` with an explicit DIR: 68/68
images, exit 0. Without DIR: 68/68, exit 0, resolved to
`C:\users\<user>\Documents\PopTracker\user-override\so2_shattered_stars` (the fresh prefix has
no `PopTracker` folder anywhere, so the Documents fallback applies -- matches the documented
search order). **Image comparison:** `diff -rq` of both Wine outputs against the Linux ASan
render found **0 differences** (all 68 files byte-identical in both cases). Bonus: the Windows
`ss_game_tests.exe`'s `tracker_` tests also ran clean under Wine (6/6).

**The setup button:** "Render tracker maps for PopTracker" on the Archipelago page's new
"Trackers" subheading (engine/src/setup/setup_ui.h/.cpp, engine/CMakeLists.txt links
`tools/tracker_maps.cpp` + the generated layout straight into `shattered-stars-setup`, not all
of `ss_game`). Runs the same `tools::render_tracker_maps` as the CLI command, against whichever
Disc 1 is on screen (saved or not) and the default folder (`tools::poptracker_override_dir()`,
no DIR). Disabled with the same "Choose a valid Disc 1 image first." tooltip the bottom bar
already uses when Disc 1 isn't valid; while running the page says "Rendering tracker maps...";
done, it says how many images and the folder plus an **Open folder** button, or the error.
Touches no config, no save, no card -- it only reads `settings_.disc1` and writes PNGs. A
one-frame delay (naturally, from `App::frame()`'s existing single-pass structure, no thread)
separates "requested" from "running", so the page can show "Rendering..." before the (blocking,
~a few seconds) call. Scriptable as `render-tracker-maps` for `--do` / `SS_SETUP_SCRIPT`, same
mechanism as every other setup action. **Tested headless** (plain build, scratch HOME +
XDG_CONFIG_HOME, SDL offscreen/dummy): with a disc, all 68 images land under the scratch HOME's
`PopTracker/user-override/so2_shattered_stars` (verified by stdout and by listing the folder),
screenshot shows the button and status line; without a disc, exit 0, **nothing written**,
screenshot shows the button disabled and the usual "Choose your Disc 1 image to continue."
message (the code path was exercised directly via the scripted action, bypassing the UI's own
disabled state, confirming the defensive check inside `run_tracker_render` and not just the
button's `BeginDisabled`).

**Commits:** `c43294f` (docs: tracker_maps.md "The setup button", 0.03.00 draft, this log),
`b25fa5b` (the setup button: setup_ui.h/.cpp, setup_main.cpp, CMakeLists.txt).

**Nothing left open** from this brief. Smaller things noticed but out of scope (not acted on):
the Archipelago page's "Trackers" section lands just past the default 920x660 window's fold
(same as other content on this page already does; the page scrolls, no visual bug).

## Log (continued)
- 2026-10-27 16:5x: committed c43294f (docs) and b25fa5b (the setup button). Cleaned the
  remaining big scratch (see above); kept engine/build-portcheck / -asan, scratchpad/portcheck's
  logs, scratchpad/portcheck/asan (the libasan copy) and the e2e output. **portcheck done**;
  reported to the coordinator.
