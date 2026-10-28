# Handoff: the port on macOS (Apple Silicon)

For the agent on the user's Mac (session "Shattered-Stars Mac"). Coordinator: the Linux session
("Shattered-Stars Linux", shattered-stars-6f), which merges this work; report to it with
SendMessage at each milestone and on any problem. Started 2026-10-28, in parallel with the
0.03.00 release (it doesn't hold that release up).

## Rules (the coordinator's and the user's, 2026-10-28, on top of docs/CONVENTIONS.md)
- A pure hand port (C++20 + SDL3 + OpenGL): no recompilation, no emulation. "Enhance, don't
  degrade": no CPU-slowdown modelling. Archipelago off means the disc's game untouched.
- Every change keeps Linux and Windows building: guard it by platform (`__APPLE__`,
  `if(APPLE)`, `uname -s` = Darwin), and don't change shared behaviour unless it's a real bug,
  which is then reported.
- No game data from the disc in git, ever. The user's NA discs are on the Mac in
  `~/ss-discs/disc1` and `~/ss-discs/disc2` (each .cue beside its .bin; read-only; outside the
  repo); captures and maps made from them stay in scratch folders.
- Never push. The work stays on the local `macos` branch; each finished milestone is committed
  (docs first) and sent with `git format-patch linux/main..macos -o ~/ss-patches/`, each patch's
  full text in a message to the coordinator between `=== BEGIN <file> ===` / `=== END <file> ===`
  lines (the user's choice: this Mac's permission check blocked a LAN patch server).
- Builds and data only in scratch folders (`port/build-macos`, the session scratchpad); never
  the user's other files, never `~/Library/Application Support/ShatteredStars` (every run with
  HOME and XDG_CONFIG_HOME pointed at scratch: even a headless run writes its log there).
- Real windows (setup's, the game's) may be opened for checks (the user); anything automated
  stays headless. No audible sound (`SDL_AUDIO_DRIVER=dummy`).
- Installing build tools is fine (the user: the Mac is a development device); Homebrew needs the
  user's admin password, so only with the user at the Mac.

## The machine
macOS 27.0.1, Apple M5 (10 cores), 24 GB RAM. Xcode 27.1 (Apple clang 21.0.0). No Homebrew.
CMake 4.4.4 and Ninja 1.13.2 from `pip3 install --user cmake ninja` (Apple's Python 3.9), in
`~/Library/Python/3.9/bin` (not on PATH by default). zlib comes from the SDK. No OpenSSL, so the
Archipelago client speaks ws:// only (a wss:// build needs OpenSSL, e.g. from Homebrew).
/bin/bash is 3.2. No GNU `timeout`, no `prlimit`. The application firewall is off.

## Build and test
```sh
export PATH="$HOME/Library/Python/3.9/bin:$PATH"
cmake -S port -B port/build-macos -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build port/build-macos -j8            # about 40 s from clean (SDL 3.4.18 built from source)
S=<a scratch folder>; D=~/ss-discs
RUN=(env HOME=$S XDG_CONFIG_HOME=$S/.config SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy
     SS_DISC1="$D/disc1/Star Ocean - The Second Story (Disc 1).cue")
"${RUN[@]}" python3 tools/memcap.py 1800 port/build-macos/ss_tests
"${RUN[@]}" python3 tools/memcap.py 1800 port/build-macos/ss_game_tests
"${RUN[@]}" SS_DISC2="$D/disc2/Star Ocean - The Second Story (Disc 2).cue" \
    port/tools/smoke_field_events.sh port/build-macos/shattered-stars
port/tools/setup_screenshots.sh port/build-macos/shattered-stars-setup $S/shots
"${RUN[@]}" python3 tools/memcap.py 900 port/build-macos/shattered-stars \
    --render-tracker-maps $S/maps/so2 --ut-zip $S/maps/ut.zip
```
SS_DISC2 matters: the smoke test finds disc 2 only beside disc 1 otherwise, and without it
ap_p4_tele_open / ap_p4_tele_story FAIL (they need it) instead of skipping, and ap_hub_ng skips.

## Status
**Base:** linux/main = 27c5fca (the coordinator's bundle of 2026-10-28).

**M1 done** (2026-10-28; commits a60440d, b571ee0, sent as patches): the four programs build with
Apple clang (RelWithDebInfo, Ninja), no errors, two clang-only warnings that aren't
macOS-specific (menu_skills.cpp:173 unused private field `info_`, spell_windows.cpp:24 unused
function `image_u8`; left alone).

**M2 done** (2026-10-28): `--version` / `--help` (both programs, 0.03.00); setup's real window on
its OpenGL path (1840x1320 at Retina 2x; both NA discs "OK", the title art from disc 1) and every
no-disc page offscreen (setup_screenshots.sh); a windowed game without a disc says "No usable Disc
1 image yet: opening shattered-stars-setup", setup opens (SS_SETUP_SCRIPT=quit closes it) and the
game exits 1 with the no-disc help; headless no-disc and bad-disc messages; config.ini never
written by those runs.

**M3 done** (2026-10-28), with the NA discs:
- ss_tests 103 tests, 0 failed (3 skipped: other regions' discs); ss_game_tests 230 tests, 0
  failed (29 skipped: other regions' discs, PCSX-Redux references, a local Archipelago server,
  and the 3 window tests below).
- The title: headless (`--headless --frames 12000 --capture DIR --capture-frames 11999`, no
  input) frame 11999 is the title menu, sha256 ff289d35b566508db03b5b0e42d8a3e37bdd1dceb89eddcd9e6cb97bc794cb21
  (the same file at frame 1100 with Start pressed at 200, 400, ..., 1000); in a real window
  (1200 vblanks in 20.3 s: real-time pacing) and through SS_SHOT (the game's OpenGL presenter on
  Cocoa's GL 4.1), both after the SS_SHOT fix below.
- `--render-tracker-maps`: 862 images in 16 s, manifest (sha256 of the sorted per-file sha256
  list, paths `./images/...`) 1cc770b531385539aae5d323fef94116b02aa6f9bab09113851fe4f903b9c0c7; the
  Universal Tracker zip 868 files, 134.8 MB, sha256 134a764faf57b26239b358648b6eda3d1317b59f6073a9e39e461680a159bed2.
  For the coordinator to compare with Linux (the software renderer should match byte for byte).
- port/tools/smoke_field_events.sh with SS_DISC1 and SS_DISC2: see "M3 smoke" below.

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
   setup_screenshots.sh uses dummy on Darwin. The game's headless runs never initialise SDL
   video (only platform::Window does), so the smoke test's offscreen setting is harmless.
   Three ss_game_tests window tests (window_honours_the_controls,
   window_escape_asks_and_the_prompt_keeps_its_keys, window_gamepad_and_rumble_with_the_prompt)
   force offscreen and so skip on macOS; they run on Linux.
5. **OpenGL: no change needed.** The port asks for 3.3 core with no flags; SDL's Cocoa backend
   asks macOS for NSOpenGLProfileVersion3_2Core for any core request (SDL_cocoaopengl.m) and gets
   the newest, 4.1. Checked on a hidden window (SDL_MAC_BACKGROUND_APP=1, so nothing showed):
   "4.1 Metal - 91.7", Apple M5, GLSL 4.10, with and without SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG
   (a GLFW requirement, not SDL's).
6. Already fine: ap_seed.cpp had macOS's Archipelago folder; the PopTracker override folder
   (tracker_maps.cpp) uses $HOME and ~/Documents as on Linux; report.cpp, log.cpp, runtime.cpp and
   main.cpp take the POSIX branches; client.cpp's CA-bundle search is skipped on macOS.
7. **The memory cap on macOS** (tools/memcap.py): macOS refuses RLIMIT_AS (`ulimit -v`: "cannot
   modify limit: Invalid argument"; Python's setrlimit likewise, so memcap's preexec_fn would
   have made every capped Popen fail), and a 6 GB allocation went through. On Darwin there is no
   preexec limit; a Watch reads the process's physical footprint (proc_pid_rusage
   ri_phys_footprint, what Activity Monitor calls Memory: resident plus compressed; checked
   against a 1.5 GB allocation) and run_capped kills the run once it passes SS_MEM_CAP_GB.
   memcap.py is also a command now, GNU timeout under the cap: `tools/memcap.py DURATION CMD...`
   (the command's status; 124 when the time runs out; 137 and a "stopped at the memory cap"
   line at the cap). Linux behaves as before (RLIMIT_AS, VmPeak).
8. **smoke_field_events.sh on macOS**: no `ulimit -v` on Darwin; memcap.py goes on PATH as
   `timeout` (a two-line shim in $WORK/bin). Two bash 3.2 fixes that change nothing elsewhere: an
   empty array under `set -u` is "unbound" before bash 4.4 (AP_ARGS and seedarg, now
   `${a[@]+"${a[@]}"}`; the run stopped at the plain save case), and BSD `wc -c` pads its count
   with spaces (ap_tdias_load compared "     302" with "302": the bytes were identical).
9. **SS_SHOT read back nothing on macOS** (platform/window.cpp; real bugs, shared code). The shot
   window read the default framebuffer after SDL_GL_SwapWindow, where the back buffer is
   undefined; Linux's drivers keep it, macOS hands back zeros (a probe: before the swap the
   picture reads back, hidden window or shown; right after it, zeros). Now the window SS_SHOT
   opens keeps its frames (`Window::keep_frames`): present reads the picture back before the
   swap and read_pixels returns that copy; the game's own window does no extra work. Also,
   present now makes its own context current: with SS_SHOT's window open in a windowed run, the
   context made last stayed current for both windows. On macOS SS_SHOT's window shows while it
   works (no offscreen OpenGL there); keeping it hidden would need a WindowOptions flag.

## M3 smoke
port/tools/smoke_field_events.sh port/build-macos/shattered-stars with SS_DISC1 and SS_DISC2 (the
command above): exit 0, 360 PASS, 0 FAIL, 1 SKIP (north_city: analysis/saves/dex_north_city.mcd,
a save only the Linux machine has), 3 min 21 s. Before the fixes in 8: the first run stopped at
the plain save case (bash 3.2), the second had ap_tdias_load FAIL (BSD wc) and, without
SS_DISC2, ap_p4_tele_open / ap_p4_tele_story FAIL and ap_hub_ng SKIP.

## Next
- Later, not now: an .app bundle and port/tools/build_macos.sh; signing and notarization are the
  user's call. In a bundle SDL_GetBasePath is Contents/Resources (sibling_program, the discs
  folder next to the game). OpenSSL for wss:// (Homebrew, with the user at the Mac).
- Possible: the three window tests on macOS (a hidden Cocoa window with SDL_MAC_BACKGROUND_APP
  instead of the offscreen driver); SS_SHOT's window hidden on macOS; tools/memcap.py's Watch in
  the other sweeps (event_sweep.py, battle_sweep.py, ref_regress.py) once they run on the Mac.
