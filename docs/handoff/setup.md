# Setup program (port feature): continuation log

Task: a separate SETUP program for non-technical players (SDL3 + OpenGL + Dear ImGui), a
shared launcher library, the game's side (validate disc 1, open setup when needed, honour
the display / sound keys), packaging and README. Build dirs: `engine/build-setup` (Linux),
`engine/build-win` (Windows). Scratch: `scratchpad/setup/` (scratch configs only:
always set `XDG_CONFIG_HOME=$PWD/scratchpad/setup/cfg`).

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
