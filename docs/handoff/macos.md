# Handoff: the port on macOS (Apple Silicon)

For the agent on the user's Mac (session "Shattered-Stars Mac"). Coordinator: the Linux session
("Shattered-Stars Linux", shattered-stars-6f), which merges this work; report to it with
SendMessage at each milestone and on any problem. Started 2026-10-28, in parallel with the
0.03.00 release (it doesn't hold that release up).

## Rules (the coordinator's, 2026-10-28, on top of docs/CONVENTIONS.md)
- A pure hand port (C++20 + SDL3 + OpenGL): no recompilation, no emulation. "Enhance, don't
  degrade": no CPU-slowdown modelling. Archipelago off means the disc's game untouched.
- Every change keeps Linux and Windows building: guard it by platform (`__APPLE__`,
  `if(APPLE)`), and don't change shared behaviour unless it's a real bug, which is then reported.
- No game data from the disc in git, ever.
- Never push. The work stays on the local `macos` branch; each finished milestone is committed
  (docs first) and sent as `git format-patch linux/main..macos -o ~/ss-patches/`, served briefly
  with `python3 -m http.server 8767 --bind 192.168.0.27 --directory ~/ss-patches` until the
  coordinator has fetched it.
- Builds and data only in scratch folders (`port/build-macos`, the session scratchpad); never
  the user's other files, never `~/Library/Application Support/ShatteredStars` (tests run with
  HOME or XDG_CONFIG_HOME pointed at scratch).
- Installing build tools is fine (the user: the Mac is a development device); Homebrew needs the
  user's admin password, so only with the user at the Mac.

## The machine
macOS 27.0.1, Apple M5 (10 cores), 24 GB RAM. Xcode 27.1 (Apple clang 21.0.0). No Homebrew.
CMake 4.4.4 and Ninja 1.13.2 from `pip3 install --user cmake ninja` (Apple's Python 3.9), in
`~/Library/Python/3.9/bin` (not on PATH by default). zlib comes from the SDK. No OpenSSL, so the
Archipelago client speaks ws:// only (a wss:// build needs OpenSSL, e.g. from Homebrew).

## Build and test
```sh
export PATH="$HOME/Library/Python/3.9/bin:$PATH"
cmake -S port -B port/build-macos -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build port/build-macos -j8            # about 40 s from clean (SDL 3.4.18 built from source)
S=<a scratch folder>
env HOME=$S XDG_CONFIG_HOME=$S/.config SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy port/build-macos/ss_tests
env HOME=$S XDG_CONFIG_HOME=$S/.config SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy port/build-macos/ss_game_tests
port/tools/setup_screenshots.sh port/build-macos/shattered-stars-setup $S/shots
```

## Status
**Base:** linux/main = 27c5fca (the coordinator's bundle of 2026-10-28; the message named 8ad9a7a,
which the bundle doesn't hold).

**M1 done** (2026-10-28): shattered-stars, shattered-stars-setup, ss_tests and ss_game_tests
build with Apple clang (RelWithDebInfo, Ninja) in port/build-macos, with no errors and two
warnings, both clang-only and not macOS-specific (menu_skills.cpp:173 unused private field
`info_`, spell_windows.cpp:24 unused function `image_u8`; left alone). Without SS_DISC1:
ss_tests 103 tests, 0 failed (15 disc tests skipped); ss_game_tests 230 tests, 0 failed (69
skipped).

**M2, the parts without a window, done:** `--version` / `--help` (both programs); the game's no-disc
and bad-disc messages (headless); setup draws every no-disc page offscreen
(setup_screenshots.sh, SDL's renderer); OpenGL 3.3 core works on a hidden Cocoa window (below).
**Waiting:** setup's and the game's real windows (the conventions say never pop windows: needs
the user's OK at the Mac), then M3 (needs the user's disc images on the Mac).

## What changed for macOS, and why
1. **`util::absolute_utf8("")`** (port/src/util/utf8.cpp; a real bug, shared code): libstdc++'s
   `std::filesystem::absolute("")` fails, so the empty string came back; libc++ (macOS) returns
   the working directory, so `--disc1 ""` would have remembered the current folder as the disc.
   ss_tests' utf8_paths_never_throw caught it. Empty now stays empty before the call; Linux and
   Windows behave as before.
2. **Settings folder** (launcher/config.cpp): `~/Library/Application Support/ShatteredStars`
   (config.ini, memory cards, mods/, archipelago/, the log), where SDL_GetPrefPath and
   Archipelago (launcher/ap_seed.cpp) keep theirs on macOS. `$XDG_CONFIG_HOME` still wins, so
   the test scripts' scratch settings work unchanged. Built from $HOME, not SDL_GetPrefPath: that
   asks Foundation (NSSearchPathForDirectoriesInDomains), which ignores $HOME, so a run with a
   scratch HOME would still reach the player's folder.
3. **Setup's fonts** (setup_main.cpp find_font): Tahoma, then Arial, from
   `/System/Library/Fonts/Supplemental` (plain .ttf; Windows' own fallbacks after Segoe UI).
   Before, no font matched and setup fell back to ImGui's default with no bold.
4. **SDL's offscreen driver can't open a window on macOS** (port/tools/setup_screenshots.sh): SDL
   makes every macOS window an OpenGL one when the driver offers OpenGL (SDL_video.c,
   SDL_DefaultGraphicsBackends), the offscreen driver's OpenGL is EGL, and macOS has no libEGL,
   so SDL_CreateWindow fails ("Could not initialize OpenGL / GLES library"), even for SDL's
   software renderer. The dummy driver works (no OpenGL: setup uses its SDL-renderer fallback).
   The script uses dummy on Darwin. The game's headless runs (smoke_field_events.sh sets
   offscreen) are checked in M3. SS_SHOT (documentation screenshots, an OpenGL hidden window) can't
   work under either driver on macOS.
5. **OpenGL: no change needed.** The port asks for 3.3 core with no flags; SDL's Cocoa backend
   asks macOS for NSOpenGLProfileVersion3_2Core for any core request (SDL_cocoaopengl.m) and gets
   the newest, 4.1. Checked on a hidden window (SDL_MAC_BACKGROUND_APP=1, so nothing showed):
   "4.1 Metal - 91.7", Apple M5, GLSL 4.10, with and without SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG
   (a GLFW requirement, not SDL's).
6. Already fine: ap_seed.cpp had macOS's Archipelago folder; the PopTracker override folder
   (tracker_maps.cpp) uses $HOME and ~/Documents as on Linux; report.cpp, log.cpp, runtime.cpp and
   main.cpp take the POSIX branches; client.cpp's CA-bundle search is skipped on macOS.

## Next
- M2 windows (with the user's OK): setup's window opens and draws (its OpenGL path); the game
  without a disc opens setup (main.cpp: a windowed run with no disc opens setup).
- M3 (once a disc is there, never in the repo): the title; `--render-tracker-maps`;
  ss_game_tests with SS_DISC1; port/tools/smoke_field_events.sh. Expected work: the smoke script
  sets SDL_VIDEO_DRIVER=offscreen (use dummy on Darwin, as setup_screenshots.sh), and uses
  `timeout` and `prlimit`, which macOS lacks; tools/memcap.py's RLIMIT_AS isn't enforced on
  macOS (to check), so the 4 GB cap needs another guard there.
- Later, not now: an .app bundle and port/tools/build_macos.sh; signing and notarization are the
  user's call. In a bundle SDL_GetBasePath is Contents/Resources (sibling_program, the discs
  folder next to the game).
