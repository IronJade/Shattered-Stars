# appicon: application name, process name and icon (2026-10-25)

Agent "appicon". User request: the port doesn't show cleanly in the task list or the taskbar;
add an application/process name and an icon. Rulings: the icon is an ORIGINAL design (2-3
variants on a preview sheet, the user picks); setup.exe keeps its name (manifest asInvoker +
version resource "Shattered Stars Setup").

## Rules (given)
- HARD RULE: no game art, logo, trademark or disc data in the icon or anywhere in git.
  The icon is drawn from vector shapes by a script; no text in it.
- Headless only; never a window or GUI. Wine only with the release log's safe settings.
- Build dirs: engine/build-appicon, plus a MinGW one as tools/build_windows.sh configures it. -j8.
- Game processes under the 4 GB cap (tools/memcap.py), at most 2 at once; a scratch
  XDG_CONFIG_HOME for every run; never a flag that saves ~/.config/shattered-stars/config.ini.
- Scratch in scratchpad/appicon/. Cap output. No commits, no pushes.
- I own the .rc and the manifest. winfix (docs/handoff/winfix.md) leaves a note here if it
  wants `activeCodePage` UTF-8 in the manifest.
- The coordinator runs the final RC rerun; tell it when ready, don't run it.

## Notes from other agents
(winfix: put manifest requests here)
- winfix (2026-10-26): **no `activeCodePage` request.** The programs now read their arguments
  and SS_* variables through the wide API (util/utf8.h: GetCommandLineW, GetEnvironmentVariableW)
  on every Windows version, so the manifest would add nothing for them, and it only works on
  Windows 10 1903+ while changing every narrow API in the process (msvcrt's, OpenSSL's). Reasons
  in docs/handoff/winfix.md. Leave it out of the manifest.

## Status
- Step 1 DONE: three variants (scratchpad/appicon/preview.png). The user picked **A. Fractured
  Gold** as is (2026-10-25). Version resource ruling: CompanyName "DreamGlyph Studios",
  LegalCopyright "Copyright (c) 2026 DreamGlyph Studios"; LICENSE files untouched (the
  coordinator asks the user separately). App ids: io.github.IronJade.ShatteredStars (game),
  io.github.IronJade.ShatteredStars.Setup (setup).
- Step 2 (in progress, 2026-10-26):
  - DONE tools/make_icon.py -> res/icon/ (shattered-stars-{16..256}.png, .ico with 7 sizes,
    .svg). Deterministic; pixel-identical to the picked variant.
  - DONE res/windows/app.rc.in + app.manifest.in; engine/CMakeLists.txt ss_windows_resources()
    for both programs (icon, VERSIONINFO, manifest: asInvoker, PerMonitorV2, supportedOS 7..11,
    Common Controls 6).
  - FOUND + FIXED: the Windows exes carried libwinpthread's own VERSIONINFO ("POSIX WinThreads
    for Windows", from version.o, pulled in by --whole-archive -lwinpthread): that was Task
    Manager's name for the game. CMakeLists now links a copy of libwinpthread.a without
    version.o (configure time, <build>/winpthread/). No new DLL dependency.
  - DONE platform/app_icon.{h,cpp} + generated/app_icon_data.cpp (64/128/256 PNGs compiled
    in): set_app_metadata before SDL (game main, setup main), set_window_icon on every window
    (platform::Window::create, setup). Windows: no SDL_SetWindowIcon (SDL's window class
    already uses the exe's .ico at every size; SDL_SetWindowIcon would replace it with one
    image). Wayland: base 64 + 2x/4x alternates (SDL ignores alternates below the base).
  - DONE tests/test_app_icon.cpp (3 cases in ss_game_tests).
  - DONE res/linux/: two .desktop files (validated) + install-desktop-entry.sh (tested with a
    scratch XDG_DATA_HOME and paths with spaces, &, $, " and a backslash; a % in the path: GLib can't
    launch it, the script warns). tools/build_linux.sh bundles desktop/ + the script.
  - Proofs: PE resources (scratchpad/appicon/peres.py, REA inspect_pe_resources) on
    engine/build-appicon-win/{shattered-stars,setup}.exe; Xvfb :91 + xprop
    (scratchpad/appicon/xvfb_proof.sh): WM_CLASS class = app id, _NET_WM_ICON 64x64, both
    programs.
  - DONE suites on engine/build-appicon (scratchpad/appicon/suites.sh, scratch config, 4 GB
    cap): ss_tests 103/0, ss_game_tests 192/0 (3 new app_icon cases), smoke 282 PASS / 0 FAIL
    / 0 SKIP.
  - DONE Debian 12 release container (CMake 3.25, GCC 12, SDL 3.4.18 static) into
    engine/build-appicon-container: builds; app_icon tests 3/3; Xvfb proof same as host.
  - DONE Wine (scratchpad/appicon/wine_check.sh, safe settings, prefix deleted after):
    shattered-stars.exe / setup.exe --version exit 0 (the manifest is accepted), the game's
    300-vblank headless boot on map 18 (26 trigger lines), no config written, wineserver -k.
  - DONE docs: README (First launch: the Linux desktop entry; layout: res/), engine/README
    (First run, Linux package contents, "Icon and program names", layout), release notes
    0.02.50 (one bullet), docs/ap/release_checklist.md 2.6 (what to check in the bundles),
    docs/HANDOFF.md conventions (the icon is original, regenerate with make_icon.py),
    docs/handoff/windows_check.md recheck item 6 (icon, names, no UAC, Task Manager), wiki
    scratchpad/wiki/Installing-and-Setup.md (contents table, "Linux: the application menu and
    the taskbar", update / uninstall). Wiki not committed or pushed.
- **Step 2 DONE (2026-10-26).** Ready for the coordinator's RC rerun. Not run by me:
  tools/build_linux.sh / build_windows.sh (they write dist/); the RC script.

- **Window title (2026-10-26, the user's ruling):** the game's window is titled "Shattered
  Stars" (main.cpp: `window.create(platform::kGameName, ...)`, one line). Checked everything
  that shows or depends on it: setup's title was already "Shattered Stars Setup"; SS_SHOT's
  second window stays "Shattered Stars (SS_SHOT)"; no test, smoke case, tool or doc matched the
  old title (grep of engine/, tools/, docs/, README, wiki), only my Xvfb proof (updated).
  - FOUND + FIXED: the --debug status title ("Shattered Stars — <status>", runtime.cpp) never
    reached the window under X11, before this change too (checked on engine/build-release2):
    SDL's X11 SDL_X11_SetWindowTitle converts the title to the C locale first and silently
    drops one it can't encode (the em dash). Now an ASCII " - "; on Xvfb the title reads
    "Shattered Stars - map 18 | state 0 | 560,0,14 | Fol 0" (scratchpad/appicon/debug_title.sh,
    and xvfb_proof.sh's third probe). Wayland and Windows took the em dash already.
  - Suites after the title change (engine/build-appicon): ss_tests 103/0, ss_game_tests 193/0
    (one more case than before, from another agent's work in the tree), smoke 282 PASS / 0 FAIL
    / 0 SKIP; the MinGW build rebuilt (rc 0, resources unchanged); xvfb_proof.sh: game
    "Shattered Stars", setup "Shattered Stars Setup", the debug title as above.
  - Docs: release notes (title bar in the bullet; the debug-console fix entry), engine/README
    (the debug title example; window titles), windows_check.md item 6, wiki Playing.md.

## Files (all uncommitted)
- New: tools/make_icon.py; res/icon/{shattered-stars-16,24,32,48,64,128,256.png,
  shattered-stars.ico, shattered-stars.svg}; res/windows/{app.rc.in, app.manifest.in};
  res/linux/{io.github.IronJade.ShatteredStars.desktop, io.github.IronJade.ShatteredStars.Setup.desktop,
  install-desktop-entry.sh (755)}; engine/src/platform/app_icon.{h,cpp};
  engine/tests/test_app_icon.cpp.
- Changed (my hunks only; other agents have hunks in some): engine/CMakeLists.txt (MinGW
  winpthread copy; the icon data + ss_windows_resources block after ss_platform; the two
  ss_windows_resources calls; test_app_icon.cpp), engine/src/main.cpp (include +
  set_app_metadata, the window title), engine/src/platform/window.cpp (include + set_window_icon),
  engine/src/runtime/runtime.cpp (the debug title's separator, one line + comment),
  engine/src/setup/setup_main.cpp (include, set_app_metadata replaces the old
  "org.shatteredstars.setup" SDL_SetAppMetadata, set_window_icon), tools/build_linux.sh
  (desktop/ + script in the bundle, README.txt lines), README.md, engine/README.md,
  docs/release_notes/0.02.50.md, docs/ap/release_checklist.md, docs/HANDOFF.md,
  docs/handoff/windows_check.md.

## How to verify
- `tools/make_icon.py` twice: identical bytes (md5).
- `python3 scratchpad/appicon/peres.py <exe>` or REA inspect_pe_resources: icon group with 7
  sizes, VERSION strings, MANIFEST (asInvoker, PerMonitorV2, supportedOS, Common-Controls),
  no "POSIX WinThreads" string in the exe.
- `bash scratchpad/appicon/xvfb_proof.sh <build dir>`: WM_CLASS + _NET_WM_ICON (64x64).
- `desktop-file-validate res/linux/*.desktop`.

## Open / notes for the coordinator
- (Resolved) The window title is now "Shattered Stars" (the user's ruling, above).
- Wayland: KDE Plasma 6 takes the window icon (xdg-toplevel-icon); GNOME ignores window icons
  and needs the installed .desktop file (install-desktop-entry.sh). Not verifiable headlessly
  here (no headless compositor installed); the app_id comes from the same SDL_GetAppID as the
  X11 WM_CLASS, which is proven.
- winfix: no manifest request arrived (no activeCodePage); its fix uses GetCommandLineW.
- Build dirs: engine/build-appicon (host), engine/build-appicon-win (MinGW, flags as
  tools/build_windows.sh, OpenSSL from scratchpad/setup/mingw-ssl).
