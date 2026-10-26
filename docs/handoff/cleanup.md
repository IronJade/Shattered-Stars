# Cleanup before 0.02.50 (agent "cleanup", 2026-10-25)

Rules (given): headless only; own build dir engine/build-cleanup (configured like
build-hubcore), -j8; SS_DISC1 = the NA disc 1 .cue; every game process under the 4 GB cap, at
most 2 at once; a scratch XDG_CONFIG_HOME per game run, SDL_AUDIO_DRIVER=dummy, never a flag
that saves config.ini; kill by exact PID; every wait with a deadline; scratch in
scratchpad/cleanup/; cap output; no commits; suites with counts before done (baseline
ss_tests 99/0, ss_game_tests 182/0, smoke 270/0); enhance, never degrade.

Tasks: (1) the menu_shop.cpp:74 warning + a warning diff against c50b530; (2) the `--debug`
exit hang (docs/handoff/claudesword.md); (3) the stale "story-mode seeds are 0.1.1's" line in
docs/ap/apworld.md. Tell "release" (docs/handoff/release.md) for the notes.

## State: DONE (20:35), nothing running. Not committed (the user reviews and commits).

## Done
- **(1) The warning.** `ItemList::add()` builds an `Entry` from a four-value brace list; the
  skillsanity shop rows added `ap`, `ap_price` (defaults) and `std::string ap_name` (none), so
  GCC warned -Wmissing-field-initializers for `ap_name`. Fix: `std::string ap_name{};` in
  engine/src/game/field/menu_shop.cpp (every member now has a default; no behaviour change).
- **Warning diff** against c50b530 (git archive of engine/ into scratchpad/cleanup/base, built
  the same way), by file + message (line numbers move):
  - Linux (RelWithDebInfo, -Wall -Wextra -Wpedantic -Wshadow), full builds: c50b530 3, now 3,
    the same: psx/gpu.cpp:235 -Wshadow, game/field/event_sweep.cpp (now :482)
    -Wformat-truncation, game/battle/spell_windows.cpp:24 -Wunused-function. All pre-existing,
    left. No new warning remains; the only new one was menu_shop.cpp:74 (fixed).
  - MinGW (Release, fresh builds of both trees): 27 and 27, the same: event_sweep.cpp 9
    -Wformat + 6 -Wformat-extra-args (`%zu` under msvcrt printf), message_draw.cpp:252 8
    -Wstringop-overflow, boot/pad.cpp:68 -Warray-bounds, spell_windows.cpp:24, gpu.cpp:235, and
    one -Wcpp in apclientpp's header. All pre-existing (their lines predate c50b530), left.
- **(2) The `--debug` exit hang.** Cause: Runtime::start_debug_console's detached thread ran
  `std::getline(std::cin)`; cin is synced with stdio, so the thread sat in getc(stdin) holding
  stdin's FILE lock while blocked in read(2). After main returned, exit() flushes every stream
  and takes each one's lock: with stdin a terminal or a pipe that stays open it waited forever.
  Seen in a hung c50b530 run (/proc): main thread in `__futex_wait` (after "stopped after 300
  vblanks"), the reader in `anon_pipe_read`. Fix (engine/src/runtime/runtime.{h,cpp}): the
  reader reads the OS handle itself, read(2) on fd 0 (EINTR retried) / on Windows ReadFile on
  GetStdHandle(STD_INPUT_HANDLE), splits lines as getline did (a last line without a newline
  counts; "\r\n" trimmed; on Windows Ctrl+Z ends the input as the CRT text mode did), and
  never touches the C stream, so it holds no CRT lock and exit() ends the thread wherever it
  waits. The queue moved out of Runtime into a never-freed block, so a line arriving during
  exit never touches a destroyed Runtime. The SS_DEBUG_INPUT path and the commands are
  unchanged.
  - Windows (reasoned, cross-compiled clean): the MinGW CRT's exit also flushes / locks each
    stream (`_flushall`) before ExitProcess, so the old reader could hang there the same way.
    Now the reader is in ReadFile with no CRT lock; ExitProcess terminates it. The handle is the
    one the CRT's stdin was opened on: a redirected handle, or (double-click --debug) the console
    launcher::attach_console allocates (AllocConsole sets the std handles). With no handle at
    all it says so once and only SS_DEBUG_INPUT runs (before: a silent failed cin).
- **(3) docs/ap/apworld.md** ("Changes", 0.2.0 intro): "story-mode seeds are 0.1.1's with
  `drop_progression: never`" replaced: not in every case; a seed with a `cave_of_trials` player
  fills otherwise (3 locations fewer, the cave's names by floor), and so does one where 0.1.1 put
  progression on a gift site; cavefix's numbers (16 all-story seeds: 3 without a cave player
  identical but options, 13 differ, 3 of them already before the cave change).
- tools/smoke_field_events.sh: new case `debug_exit` (2 checks): stdin a fifo fed "help\n" and
  then held open by `sleep 300` (killed by its PID); `timeout 60 --debug --frames 120 --map 18`;
  the help runs and the exit status is 0. Against the c50b530 binary it FAILs (exit 124 after
  60 s), now PASS.
- docs/handoff/claudesword.md: a dated "fixed" line under its stdin note.
- release.md: dated note for the release notes.

## How to verify
- Build: `cmake --build engine/build-cleanup -j8` (log scratchpad/cleanup/build1.log: 3 warnings,
  base: scratchpad/cleanup/base/build.log 3; MinGW win_cur.build.log / win_base.build.log 27 / 27).
- Exit proof: `scratchpad/cleanup/exit_test.sh BIN NAME MODE [DEADLINE]` (modes devnull, file,
  pipe, pipe_cmd, tty, tty_cmd; PACE=1 paces 300 frames to 5 s; tty = a pty via on_pty.py,
  `script` is not installed; stamps every line and the EOF). Results (runs/*/log):
  - new binary: pipe 10/10 runs exit 0 with the EOF at the last line (0.28-0.29 s, wall 0.30 s);
    tty 5/5 (0.29-0.30 s; stdin /dev/pts/4); devnull, file ("help" without newline: runs),
    pipe_cmd / tty_cmd (paced, "help" typed at 2 s: runs), pipe paced: all exit within 3 ms of
    "stopped after 300 vblanks".
  - c50b530 binary: pipe hung (killed at 90 s and 12 s), tty hung (killed at 30 s).
- Suites (engine/build-cleanup, scratch XDG_CONFIG_HOME / TMPDIR): ss_tests 99/0,
  ss_game_tests 182/0, smoke 272/0 (270 + the 2 new debug_exit checks;
  scratchpad/cleanup/suite/smoke2.log; the first pass before the new case: 270/0).
