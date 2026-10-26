# winfix: the Windows machine's findings on the 0.02.50 RC (2026-10-25)

Agent "winfix". The 0.02.50 RC (c9a8200, pushed) was checked on a real Windows 11 machine
(docs/handoff/windows_check.md); the user ruled: fix all of it before tagging. Then rebuild,
rerun `scratchpad/release/rc-0.02.50.sh all`, and update checklist section 9, the notes'
checksums, the tree hash and release.md's log. Nothing committed or pushed (the user does).

## Rules (given)
- Headless only; never a window or a GUI. Wine only with the release log's safe settings
  (scratch WINEPREFIX under scratchpad/, ShowCrashDialog 0, winedbg disabled, null display
  driver, a dummy WAYLAND_DISPLAY, DISPLAY unset, `wineserver -k` after); never ~/.wine.
- Build dirs: the release scripts' own, or engine/build-winfix (-j8). SS_DISC1 = the NA Disc 1 cue.
- Every game process under the 4 GB cap (tools/memcap.py / systemd-run MemoryMax=4G); at most 2 at
  once. A scratch XDG_CONFIG_HOME for every game run; never a flag that saves
  ~/.config/shattered-stars/config.ini.
- Long jobs in the background; every wait with a deadline; kill only by exact PID.
- Scratch in scratchpad/winfix/, not /tmp. Cap command output.
- No commits, no pushes. On a permission refusal: stop and report the exact command.
- Past ~400k tokens: update this log and end the turn.
- Enhance, never degrade; Linux unchanged except where a fix says otherwise.

## The findings
1. CRASH: a non-ASCII path on the command line (`--memcard1 "...Test Ä..."`, setup
   `--set "memcard1=...Ä..."`) aborts both programs: `filesystem_error ... Cannot convert
   character sequence: Illegal byte sequence`. argv arrives in the ANSI code page.
2. setup Auto-detect with Disc 1 set: fills Disc 2 but says "Found Disc 2, but no Disc 1."
3. "pwnd->destroy" on stderr at every menu window close.
4. shattered-stars.log has only some [ap] lines (report()); the game's own [ap] lines go to
   stderr only. Route them to the log; bound its size.
5. The AP client announces Client(0.6.6) (apclientpp's default); announce 0.6.8 (archipelago.json).
6. Release notes call the YAML `Star.Ocean.The.Second.Story.yaml`; the checklist uses spaces.

## Coordinator (mid-task)
- "appicon" (docs/handoff/appicon.md) owns the Windows .rc and the manifest: no manifest from me
  (a UTF-8 activeCodePage request would go in its log; I'm not asking for one, see below).
- "escquit" (docs/handoff/escquit.md) may touch main.cpp / platform/window.cpp: small hunks there,
  a note in its log if we overlap.
- **Hold the final RC rerun** (rc-0.02.50.sh all, checksums, tree hash) until the coordinator says
  appicon and escquit are done; one RC run covers all three. Report when the fixes are in.

## Status
- Builds: engine/build-winfix (Linux), engine/build-winfix/win (MinGW, the build_windows.sh flags).
  Scratch: scratchpad/winfix/ (wine.sh = the safe Wine runner; wineprefix = a copy of the RC's
  prefix; proof/ = the Ä folder and run logs; logs/ = build and test logs).
- **Reproduced first** with the RC's own Windows binaries under Wine: `shattered-stars.exe
  --headless --memcard1 "Z:\...\Test Ä\card.mcd"` and `setup.exe --set "memcard1=...Test Ä..."`
  both exit 3 with `terminate called after throwing an instance of 'std::filesystem::__cxx11::
  filesystem_error' what(): filesystem error: Cannot convert character sequence: Invalid or
  incomplete multibyte or wide character` (Wine's wording of EILSEQ). No dialog anywhere.
- **Wine 11 doesn't pass XDG_* variables to Windows programs** (checked with `cmd /c echo`): under
  Wine the game's settings are the scratch prefix's %APPDATA%\ShatteredStars, for the RC's runs
  too (its "xdg-wine" folder was never used; harmless: inside the scratch prefix).

## Note from escquit (2026-10-26): my hunks in files you touch
Uncommitted (details docs/handoff/escquit.md). main.cpp: one line after
`rt.audio_tick = platform::audio_vblank;` (`rt.audio_pause = platform::audio_pause;`).
runtime.cpp: `#include "runtime/quit_prompt.h"`, SS_KEYS helpers at the end of the anonymous
namespace, present() split into screen() / frame_border(), new host_events() / ask_quit()
before vblank(), and in vblank() the window pump line replaced by `host_events();`. I use
your util::log_line for the "[port] quit prompt: ..." lines. CMakeLists.txt: one line in
ss_core's list (src/runtime/quit_prompt.cpp) and one target_sources line for ss_game_tests
(tests/test_quit_prompt.cpp). Your log_once line and util/log.h include untouched.

### The fixes (DONE 2026-10-26, uncommitted; the RC rerun HELD for appicon + escquit)
1. **CLI non-ASCII paths (the crash).** Cause: MinGW's main() gets argv from the ANSI code page
   (Ä = 0xC4); libstdc++ (GCC 16) reads every narrow string given to std::filesystem::path as
   UTF-8 and throws on 0xC4 (`Cannot convert character sequence`), uncaught -> std::terminate. The
   SS_* variables (std::getenv: also ANSI) had the same problem, and a name outside the ANSI code
   page (日本 on a Western Windows) could not even be passed. Change: new
   engine/src/util/utf8.{h,cpp} (ss_core; shell32 on Windows): `util::Arguments` (Windows:
   CommandLineToArgvW(GetCommandLineW()) -> UTF-8; elsewhere argv as is), `util::env` (Windows:
   GetEnvironmentVariableW, not _wgetenv: msvcrt may build that one from the ANSI block),
   UTF-8 <-> UTF-16 by hand, `utf8_path` / `path_utf8` / `absolute_utf8` that never throw (a
   non-UTF-8 string on Windows is decoded in the ANSI code page: an "ANSI" config.ini from
   Notepad). main.cpp and setup_main.cpp: the arguments converted first thing; path options via
   util::absolute_utf8 / utf8_path (no throwing std::filesystem::absolute); path-valued and name
   SS_* variables via util::env (SS_DISC1/2, SS_MEMCARD1/2, SS_AUDIO_WAV, SS_SHOT, SS_VRAM_PNG,
   SS_MODS_DIR, SS_MOD_ORDER, SS_AP_SERVER/SLOT/GAME, SS_SETUP_SCRIPT, WINDIR); a function-try-block
   catches std::filesystem::filesystem_error: launcher::fatal("A file or folder could not be
   used: ...") and exit 1. launcher::utf8_path / path_utf8 and config.cpp's env_path forward to
   util; game/ap/ap.cpp's upath/ustr and game/field/mods.cpp's utf8_path too. Console output goes
   through WriteConsoleW when stderr is a console (util/log.cpp), so "Ä" shows right in cmd.
   Linux: argv / getenv / the path bytes exactly as before.
   **No UTF-8 activeCodePage manifest** (appicon owns the manifest; note left in its log): the
   wide-API reads cover every Windows version; the manifest only works from Windows 10 1903, is
   ignored before, and switches every narrow API in the process (msvcrt's fopen/getenv, OpenSSL,
   the debug hooks that pair narrow getenv with narrow fopen) at once; with the conversions in
   place nothing the port needs would change, so it would be risk without gain.
   **Proof:** before (the RC's own binaries under Wine): `--memcard1 "...\Test Ä\card.mcd"`,
   `--disc1 "...\Test Ä\Disc 1 Ä.cue"`, `--ap-seed "...\Test Ä\seed Ä.apso2"` and setup
   `--set "memcard1=...Test Ä..."` all exit 3 (`terminate called ... Cannot convert character
   sequence`). After (engine/build-winfix/win): the same four commands exit 0 (the game "stopped
   after N vblanks", "[ap] seed winlog-1 slot Cléo"; setup's Save writes `memcard1=...Test Ä...`
   in UTF-8 to config.ini). Tests: ss_tests `utf8_*` (4: round trips, invalid sequences, paths
   that never throw, the Windows command-line split with Ä / ö / 日本, the environment through
   SetEnvironmentVariableW) on Linux and under Wine; ss_game_tests
   `game_command_line_non_ascii_paths` (a cue, card and seed in "Test Ä 日本": a save through the
   camp menu lands there, 131072 bytes + sidecar, and loads back with --memcard1 and with
   SS_MEMCARD1 + SS_DISC1) and `setup_command_line_non_ascii_and_autodetect` (setup --set
   memcard1=...Test Ä... -> config.ini), both on Linux and under Wine (there they start the Windows
   game / setup through CreateProcessW, i.e. the reported path).
2. **Auto-detect message.** Cause: setup_ui.cpp auto_detect: a Disc 2 found beside the Disc 1
   already set left found.disc1 empty and fell into "Found Disc 2, but no Disc 1." Change: that
   case says "Found Disc 2 next to your Disc 1." (ok), a Disc 2 from a discs folder with Disc 1 set
   "Found Disc 2. Your Disc 1 stays as it is."; the scripted `--do autodetect` prints
   "setup: autodetect: <notice>" for scripts. Proof: screenshots scratchpad/winfix/proof/
   setup-autodetect-rc.png (RC: yellow "Found Disc 2, but no Disc 1.") and setup-autodetect.png
   (green "Found Disc 2 next to your Disc 1."); the test above checks the line.
3. **"pwnd->destroy".** The original's debug printf in widget_on_close (title 0x80073910 / field
   0x800dfef8), ported as an unconditional fprintf. Removed, a comment marks it
   (menu_widget.cpp). Proof: a save through the camp menu prints it once with the RC binary, never
   with the fixed one (both save the card); smoke 282/0.
4. **[ap] lines in the log.** Cause: only launcher::report() wrote the log; game/ap's log() and
   the others used fprintf(stderr) (ss_game doesn't link the launcher). Change: util/log.{h,cpp}
   in ss_core: log_text / log_line / log_printf write stderr as before, then a sink the launcher
   installs in open_log. Routed: game/ap/{ap,skills,hub}.cpp, title/new_game.cpp,
   field/script_table_a.cpp ([ap]); Runtime::log_once, boot/sound.cpp, runtime/region.cpp
   ([port] once-lines); field/mods.cpp ([mods]); report() itself. The bound (report.cpp): past
   4 MB the log keeps its first quarter (the run's start), one "[log] ... N bytes ... left out
   here ..." line and the newest half, re-trimmed as it grows (rewritten in place, through the
   stream itself when stderr is the log: the double-clicked Windows case). Sizes measured from the
   RC's e2e sessions: the game's own [ap] lines ~55 bytes each (a whole seed's 308 checks + items
   ~80 KB); "server says:" lines ~100 bytes, one per item found anywhere in the multiworld
   (MultiServer broadcasts every ItemSend to the team), e.g. a 30-player game ~9,000 lines ~0.9 MB
   if one session spans it; so the bound only matters for very long sessions or release floods.
   Proof: ss_game_tests `launcher_log_holds_game_lines_and_stays_bounded` (64 KB bound: head kept,
   one marker, newest lines, every line on stderr too) and Windows-only
   `launcher_log_without_a_console` (stderr/stdout redirected into the log: each line once, raw
   stderr and stdout lines kept, bounded), Linux and Wine; `game_archipelago_runs_without_debugging`
   now checks the log holds "[ap] seed lock-1"; a windowed Wine run with a seed (settings in the
   prefix's AppData): the RC binary's log has only "[ap] the seed plays on its own memory card",
   the fixed one also "[ap] seed winlog-1 slot Cléo ..." and "[ap] the title at vblank 0".
5. **Client version.** Cause: ConnectSlot without `ver` -> apclientpp's APCLIENTPP_VERSION_INITIALIZER
   {0,6,6}. Change: engine/CMakeLists.txt reads apworld/so2/archipelago.json's minimum_ap_version
   (string(JSON), CMAKE_CONFIGURE_DEPENDS; 0.6.8 without the file) into SS_AP_VERSION_* for
   ss_ap_net; client.cpp passes it; Client::announced_version(); main.cpp's "Archipelago:
   connecting to X as Y (client version 0.6.8)". Proof: `tools/ap_test_server.sh start 38391`
   + SS_TEST=ap_net: 5 passed (1 wss case skipped: no TLS server); the server log has
   "Client(0.6.8)" x12, none older; stopped by its PID.
6. **YAML name.** GitHub's assets for v0.01.00 and v0.02.00 are `Star.Ocean.The.Second.Story.yaml`
   (`gh release view`), so the notes are right about the download. checklist section 3 now names
   both (built with spaces, uploaded / downloaded with dots); rc-0.02.50.sh's `sums` phase copies
   the template to the dotted name and hashes that (SHA256SUMS = the notes = section 9). Left
   alone: apworld/so2/docs/setup_en.md says the template "ships ... as `Star Ocean The Second
   Story.yaml`" (true of the file Archipelago writes; touching it would change so2.apworld).

### Suites (shared working tree, so with appicon's / escquit's work in progress)
- Linux engine/build-winfix: ss_tests 103/0, ss_game_tests 192/0, smoke 282/0/0.
- Windows engine/build-winfix/win under Wine: ss_tests.exe 103/0, ss_game_tests.exe 193/0.
- Warnings: the pre-existing set only (Linux 12, MinGW 26), none in the files touched.

### Docs touched
docs/release_notes/0.02.50.md (intro + five Fixes items), docs/ap/release_checklist.md (section 1's
client-version row and paragraph, section 3's YAML row), docs/handoff/windows_check.md (Results,
the recheck list), docs/handoff/wiki.md ("For wiki" note), docs/handoff/appicon.md (no manifest
request), docs/handoff/escquit.md (my main.cpp / runtime.cpp hunks), this log.

### Next (after the coordinator's go)
`bash scratchpad/release/rc-0.02.50.sh all` (background, timeout 21600, output
scratchpad/release/rc-run.out); then checklist section 9 (results, SHA-256, tree hash), the notes'
SHA-256 block, release.md's log; clean up scratchpad/winfix (the 2 GB prefix copy, rc-win/,
rc-linux/) once the recheck needs nothing from it.
