# Setup program (port feature): continuation log

Task: a separate SETUP program for non-technical players (SDL3 + OpenGL + Dear ImGui), a
shared launcher library, the game's side (validate disc 1, open setup when needed, honour
the display / sound keys), packaging and README. Build dirs: `engine/build-setup` (Linux),
`engine/build-win` (Windows). Scratch: `scratchpad/setup/` (scratch configs only:
always set `XDG_CONFIG_HOME=$PWD/scratchpad/setup/...`; under Wine the settings land in
the scratch prefix's AppData, Wine ignores a Unix APPDATA).

## Round 2 (coordinator, 2026-10-23 ~00:30): logo header, GUI exe, Controls, Linux package

Done so far:
- **Setup header logo** (user's request): `launcher/title_art.{h,cpp}` decodes the title
  picture (archive entry 12, block 0: 640x480, 8-bit + CLUT) from the Disc 1 on screen,
  in memory only; the band x 20-620, y 106-216 (logo + TM, no copyright line) fills the
  header height (5.6em), fading into the starfield (a mesh with per-vertex alpha),
  linear filtering; GL texture or SDL texture. Swaps live when a valid Disc 1 is picked;
  text header without one. Screenshots: shots/discs-ok, discs-live-swap, discs-empty
  (Linux, GL) and shots_win/ (Wine, SDL renderer). Test
  `launcher_title_picture_from_disc1` (640x480, >200 colours, logo in the band, the
  copyright just below it; skipped without SS_DISC1).
- **Game exe is a Windows GUI program** (CMake WIN32): `launcher/report.{h,cpp}`:
  attach to the parent console (cmd / PowerShell), keep redirected pipes / files (tests,
  Wine), a console window of its own for --debug without a terminal; windowed player runs
  write `shattered-stars.log` beside config.ini (replaced each start; double-clicked on
  Windows: all output goes there); fatal errors also show an SDL message box when nobody
  sees stderr (never with SDL_VIDEO_DRIVER offscreen / dummy). stdout / stderr in binary
  mode on Windows ("\n" line ends: the smoke script's `$` anchors failed under Wine on
  "\r\n" before; smoke under Wine now 21/21 via scratchpad/setup/wine/game.sh). Test
  `game_windowed_run_writes_its_log`. Checked under Wine: `wine start /wait /unix` (no
  console) puts everything in the log, nothing on the terminal.
- ss_game_tests 83/0 (Linux); Windows targets build.

- **Controls page** (setup) and the game honouring it: `platform::Controls` (window.h:
  per PS1 button one key + one gamepad button; the triggers now give L2 / R2, which they
  didn't before), table-driven key / button maps in window.cpp; config keys `key_<b>` /
  `pad_<b>` (SDL names, "none"; defaults left out of the file), `stick_deadzone` (%),
  `stick_dpad` (launcher/settings.h). Stick: `game::pad_set_player_stick` (FLAGGED edit in
  game/boot/pad.{h,cpp}: the player's dead zone / stick-as-d-pad win over the game's own
  pad_set_stick_* calls; -1 = the game's, the default). Setup: click a box, press a key /
  button (Esc cancels, Delete unbinds, F11 refused), duplicates warned and highlighted,
  Reset to defaults, dead-zone slider with "Game's". Screenshots controls*.png. Tests:
  launcher_controls_round_trip, pad_player_stick_settings_win_over_the_game,
  window_honours_the_controls (offscreen forced by an SDL hint). ss_game_tests 86/0 on
  Linux and under Wine.

- **Linux package** `tools/build_linux.sh` -> dist/ShatteredStars-linux-x86_64.tar.gz
  (4.5 MB): the game, setup, libSDL3.so.0 (the system's, RUNPATH $ORIGIN), README.md,
  README.txt (glibc floor), THIRD-PARTY-LICENSES.txt (SDL zlib, ImGui MIT), discs/ note.
  Release build in engine/build-linux with libstdc++ / libgcc static. This machine has no
  libstdc++-static: the RPM is unpacked in scratchpad/setup/static (run with
  LIBSTDCXX_STATIC_DIR=scratchpad/setup/static/usr/lib/gcc/x86_64-redhat-linux/16).
  glibc floor 2.43 = Fedora 44's (its SDL3 and the new float maths in libm ask for it; the
  game alone needs 2.38): documented; building on an older distribution lowers it.
  Checked unpacked: SDL loads from the package folder, no libstdc++ dependency, the game
  finds discs/ by itself, setup renders (OpenGL) with the logo.
- Windows zip also carries THIRD-PARTY-LICENSES.txt (CRLF) now.
- Wide-font fixes (Wine has no Segoe UI): Controls columns sized from their labels and
  shrunk to the page; field labels push their widget right when wider than the column.

All suites (2026-10-23 ~02:00): Linux ss_tests 78/0, ss_game_tests 86/0, smoke 21/21;
Wine ss_tests.exe 78/0, ss_game_tests.exe 86/0, smoke 21/21 (via
`tools/smoke_field_events.sh scratchpad/setup/wine/game.sh`, a wrapper running the .exe
with the scratch prefix). engine/build, build-setup, build-win, build-linux build.

Nothing in progress.

## DONE (2026-10-22 ~23:40): works on Linux and Windows (Wine)

Nothing in progress. All suites green on engine/build: ss_tests 78/0, ss_game_tests 81/0
(7 new launcher / command-line cases), smoke 21/21; under Wine: ss_tests.exe 78/0,
ss_game_tests.exe 81/0. engine/build, engine/build-setup and engine/build-win build all
targets. `tools/build_windows.sh` -> dist/ShatteredStars-win64.zip (4.1 MB: the game,
setup.exe, SDL3.dll, README, discs\ with a note).

Checked:
- Screenshots of every page and state, Linux (OpenGL) and Wine (SDL-renderer fallback:
  Wine's offscreen driver has no GL): `tools/setup_screenshots.sh [BIN] [OUT]`; the
  Linux set is kept in `scratchpad/setup/shots/`, three Wine ones in `shots_win/`.
- Flows (Linux; Wine where noted): Save keeps comments and unknown keys; game with no
  disc opens setup, `play` returns to the game which runs (Wine: up to the window, which
  needs GL); quitting setup exits the game with the message (Wine too); standalone
  Save & Play starts the game next to setup; missing game -> error popup; the unpacked
  zip under Wine: the game finds discs\ by itself, setup auto-fills both discs.
- Game window modes (fullscreen with a mode, borderless, windowed scale 1/8, a missing
  monitor, vsync off, volume 50, audio off) run offscreen.

Open / suggestions (not done):
- shattered-stars.exe is a console program: players double-clicking it get a console
  window beside the game. Making it a GUI program (WIN32) would need AttachConsole /
  AllocConsole for --debug and for messages; a decision for the coordinator.
- Under Wine, no Segoe UI file: setup uses ImGui's built-in vector font (real Windows
  has segoeui.ttf; Linux uses Noto / DejaVu / Liberation).
- `scratchpad/setup/old/disc_setup.{h,cpp}`: the coordinator's first version (never
  committed), superseded by launcher/discs + setup; delete when no longer wanted.
- The Archipelago page shows the world agent's keys read-only (ap_seed, ap_server,
  ap_slot, ap_password); making the seed picker live is a small change when asked.

## 23:00

- **Headless memory-card guard** (coordinator): headless runs use only cards named for
  the run (--memcardN / SS_MEMCARDN); else both slots empty, nothing made in the settings
  folder. Test extended (boots to the title, then asserts no memcard*.mcd / config.ini in
  the scratch settings folder). ss_game_tests 81/0.
- **Setup UI checked from screenshots** (`tools/setup_screenshots.sh [BIN] [OUT]`, new:
  20 states, offscreen, scratch settings; works for setup.exe under Wine too):
  path boxes show the END of the path (click to edit), smaller checkboxes / radios, the
  Display page fits (modes on one line, one description), Archipelago placeholder shows
  the world agent's keys (ap_seed, ap_server, ap_slot, ap_password) read-only.
- **Flows checked on Linux** (scratchpad/setup/flow): Save keeps comments and unknown
  keys; game with no disc -> setup (`SS_SETUP_SCRIPT="set:disc1=...,play"`) -> game runs,
  disc 2 found and remembered; setup quit -> game exits 1 with the message; standalone
  Save & Play starts the game next to setup (fake game), missing game -> error popup.
- Next: Wine (setup screenshots, the game->setup launch, ss_tests / ss_game_tests),
  build_windows.sh + README, full suites.

## 22:20 (resumed)

- **Event-sweep exit 2 fixed** (main.cpp): booting from Disc 2 is allowed again for an
  image given for the run (SS_DISC1 / --disc1) or any headless run; explicit SS_DISC2 /
  --disc2 used as given; only windowed config.ini runs insist on Disc 1. A bad boot image
  prints "disc 1 image expected, <path> is disc 2" (or the reason) before the usage text.
  Menu agent and coordinator told.
- `tests/test_launcher.cpp` written (7 cases in ss_game_tests, incl. the game's headless
  command line run as a subprocess). engine/build ss_game_tests 81/0.
- engine/build-win: game, setup.exe, ss_tests, ss_game_tests build (MinGW).
- Next: setup screenshots (Linux, then Wine), the game<->setup loop test, packaging, README.

## State at the pause (2026-10-22 ~19:50, usage limit)

Everything below is on disk and builds: `engine/build-setup` (all targets) and
`engine/build` (shattered-stars, ss_tests, ss_game_tests) link with no errors.
**Not yet built: `engine/build-win`** (do this first on resume). No suites run yet.

### Done (built, lightly run)
- **Launcher library** `engine/src/launcher/` (CMake target `ss_launcher`):
  - `config.{h,cpp}`: `config_dir()` / `config_path()` (XDG_CONFIG_HOME, %APPDATA% read as
    UTF-16, ~/.config), `utf8_path` / `path_utf8`, `Config` (keeps every line: unknown keys
    and comments survive; trims, strips CR and a BOM; saves via `config.ini.new` + rename).
  - `discs.{h,cpp}`: `identify_disc` (kinds: none / missing / archive / unsupported /
    unreadable / not SO2 / other release / disc 1 / disc 2; checks the archive's TOC word
    0x34829314 like loader_is_disc, plus the SYSTEM.CNF game ID SCUS_944.21 / .22 and
    entries 6 / 7), `disc_number`, `is_disc1`, `describe` (the player's status line),
    `find_discs`, `disc_folders`, `find_disc2_near`. (Old `disc_number` accepted any PS1
    disc whose garbage TOC had a non-zero entry 6; fixed by the TOC-word check.)
  - `settings.{h,cpp}`: DisplaySettings / SoundSettings with defaults; keys `window`
    (windowed|borderless|fullscreen), `window_scale`, `resolution` (WxH[@Hz]|desktop),
    `monitor` (1 = primary), `vsync`, `dither`, `audio`, `volume`; `memcard_path`,
    `default_memcard`, `mods_folder` (new key `mods`).
  - `process.{h,cpp}`: `sibling_program`, `run_and_wait`, `start_program`
    (SDL_CreateProcess); names `shattered-stars(.exe)` / `shattered-stars-setup`
    (`setup.exe` on Windows); setup exit codes kSetupPlay 0 / kSetupQuit 1.
  - Link-order note (fixed the coordinator's ss_game_tests failure): ss_launcher does NOT
    declare ss_core (that moved ss_core after ss_platform and broke runtime.o's Window
    references); programs list ss_launcher before ss_core.
- **Shared-file edits (flagged):** `runtime/boot_exe.{h,cpp}`: new `boot_exe_name(disc)`
  (SYSTEM.CNF parse split out of boot_exe_open; behaviour unchanged).
- **Removed** `engine/src/disc_setup.{h,cpp}` (moved to `scratchpad/setup/old/`; its
  identification / search now lives in launcher/discs, ask_for_discs is replaced by setup).
- **main.cpp:** uses launcher::Config (`saved` = file, `config` = run's values);
  command-line remembered options saved only when not headless; SS_DISC1/2 override for
  the run; disc 1 validated (`is_disc1`); then the `discs` folders silently; then (windowed,
  no SS_DISC env) runs setup `--from-game`, waits, reloads config.ini; quit / no setup ->
  message + exit 1 (headless / env runs: message + usage, exit 2). Disc 2 validated, else
  searched next to disc 1 and in the discs folders. Display / sound keys passed to the
  platform. Checked: headless SS_DISC1 run OK; windowed run with no disc opens setup.
- **platform/window.{h,cpp}:** `WindowOptions` (mode, windowed size shrunk to fit the
  monitor, exclusive mode via SDL_GetClosestFullscreenDisplayMode or the desktop mode,
  monitor, vsync), `display_order()`, `set_fullscreen` (F11 / Alt+Enter toggle kept; hides
  the cursor in full screen). Old `create(title, w, h)` kept (viewer).
- **platform/audio.{h,cpp}:** `AudioOptions::volume` 0-100 -> SDL_SetAudioStreamGain
  ((v/100)^2); WAV dumps unaffected.
- **CMake:** `ss_launcher`; option `SS_BUILD_SETUP` (ON): FetchContent Dear ImGui
  v1.92.9b (SHA-256 21d8a0a5...7f99, MIT), `ss_imgui` (core + stdlib + SDL3 / OpenGL3 /
  SDLRenderer3 backends), `shattered-stars-setup` (WIN32 GUI; OUTPUT_NAME setup on
  Windows; SDL3.dll copied); ss_game_tests gets `tests/test_launcher.cpp` (placeholder,
  empty) + ss_launcher.

### In progress
- `engine/src/setup/setup_main.cpp` (complete, compiles, NOT yet run): args
  `--from-game --screenshot FILE --frames N --page NAME --set K=V --state NAME --do A,B
  --renderer gl|sdl`, env `SS_SETUP_SCRIPT`; GL 3.3 with SDL_Renderer fallback; system font
  (Noto / DejaVu / Liberation; segoeui / tahoma on Windows) else ImGui's vector font;
  screenshot via glReadPixels / SDL_RenderReadPixels -> util::write_png.
- `engine/src/setup/setup_ui.{h,cpp}` (complete, compiles, NOT yet run): header with a
  starfield, sidebar (Discs, Display, Sound, Memory cards, Mods, Archipelago greyed "soon"),
  bottom bar (status + Quit / Save / Save & Play; Save needs a valid disc 1 and two
  different card files), pages as specified; unsaved-changes and error modals; async
  file dialogs; auto-detect (also silently on start when disc1 is empty); DexDrive .gme ->
  "Make a .mcd copy"; wrong disc in a slot moves to the right slot.

### Next steps (in order)
1. `tools/build_windows.sh`-style build of `engine/build-win` (game + setup) and fix any
   MinGW issues; keep engine/build and build-win linking at every step.
2. Run setup screenshots (SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy,
   XDG_CONFIG_HOME=scratchpad/setup/cfg*) for each page and state; view the PNGs; fix the
   layout. Check the imgui GL loader works with the offscreen EGL context (else use --renderer sdl).
3. Write `tests/test_launcher.cpp` (Config round trip keeps unknown keys / comments / CRLF;
   identify_disc on SS_DISC1 / the test .iso in scratchpad/discsetup/iso; describe texts;
   settings defaults).
4. Game <-> setup loop test: game windowed offscreen with empty scratch config +
   `SS_SETUP_SCRIPT=autodetect,play` / a `discs` folder of symlinks; and the quit path.
   Save & Play standalone: copy setup next to a fake game script in scratchpad.
5. Wine runs (WINEPREFIX=scratchpad/wineprefix WINEDEBUG=-all, APPDATA scratch, HOME /
   XDG_CONFIG_HOME unset): setup screenshots (likely the SDL-renderer fallback), the game's
   setup launch.
6. `tools/build_windows.sh` adds setup.exe; README "First run" section (+ Linux mapping).
7. Suites: ss_tests, ss_game_tests (SS_DISC1), tools/smoke_field_events.sh (SS_DISC1).
