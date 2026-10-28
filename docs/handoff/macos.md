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
Archipelago client speaks ws:// only (a wss:// build: "A macOS release", a). Perl 5.34 and make
are Apple's. /bin/bash is 3.2. No GNU `timeout`, no `prlimit`. No Rosetta (an x86_64 build can't
run here). The application firewall is off.

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
port/tools/build_macos.sh    # the release zip: dist/ShatteredStars-<version>-macos-arm64.zip
```
SS_DISC2 matters: the smoke test finds disc 2 only beside disc 1 otherwise, and without it
ap_p4_tele (map 303, on disc 2) and ap_hub_ng skip.

## Status
**Base:** linux/main = daa3a00 (the coordinator's bundle of 2026-10-28: 0.03.00 released, M1-M3
merged); M1-M3 were made on 27c5fca.

**0.03.00 release candidate on macOS** (2026-10-28, merged main daa3a00, a clean build): see
"The 0.03.00 release candidate" below; everything passes. What a macOS release still needs:
"A macOS release" below.

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
- `--render-tracker-maps`: 862 images and the Universal Tracker zip (868 files, 134.8 MB) in 16 s.
- port/tools/smoke_field_events.sh with SS_DISC1 and SS_DISC2: see "M3 smoke" below.

**The same output as Linux** (2026-10-28, the coordinator's x86-64 build against this Mac's arm64
one): headless, scratch XDG_CONFIG_HOME, SDL_VIDEO_DRIVER=dummy, SDL_AUDIO_DRIVER=dummy, a scratch
SS_MEMCARD1, SS_DISC1 (NA disc 1) and no other SS_*, `--headless --frames 12000 --capture DIR
--capture-frames F`: all 16 frames equal on both, pixel hashes (the first 16 hex digits of
sha256(IHDR + the decompressed IDAT), the coordinator's pixhash.py):
  00060 0dfe28bd35e17def, 00120 0dfe28bd35e17def, 00300 7f63b067fa4af019, 00600 4908a21a31e3b8db,
  00900 64b21b5627d73fc5, 01200 ab0ae8cafaabda0a, 01800 291be3268ecb3301, 02400 eea8240a0a6989b6,
  03600 9698b3d70d47d3ba, 04800 6c2c967cb49bab13, 06000 3cf7e37492bb15e8, 07200 3d37bb6591eaf227,
  08400 155c29aa7087eba4, 09600 89c74dcfca83b5c0, 10800 9d9eefb1264909b6, 11999 (640x480, the title)
  de09d2b4e605df41.
The tracker maps (`--render-tracker-maps DIR --ut-zip FILE`) are byte-identical once
`-ffp-contract=off` is on (10 below): pixels 2753b12843f8cf5d..., files (sha256 of
`find . -type f | LC_ALL=C sort | xargs shasum -a 256`) caf89d6de3e6f132..., ut.zip
35e72e8d7b827506.... Before the flag only the maps differed (pixels 03f276f8...); deflate.cpp is
deterministic (the same pixels give the same bytes). The coordinator's first, different frame
hashes came from a run with other settings (the offscreen driver, no scratch card).

## The 0.03.00 release candidate (2026-10-28, merged main daa3a00)
A clean build (`rm -rf port/build-macos`, RelWithDebInfo, 66 s with the fetches; the same two
clang warnings as M1), then, all headless with scratch HOME / XDG_CONFIG_HOME and both NA discs:
- ss_tests 103 tests, 0 failed; ss_game_tests 231 tests, 0 failed (29 skipped, as in M3).
- smoke_field_events.sh with SS_DISC2: exit 0, 360 PASS, 0 FAIL, 1 SKIP (north_city), 202 s.
- `--render-tracker-maps`: 862 images, files manifest
  caf89d6de3e6f1329eff8b70d39ce4bbbc29538d3bceced6d4fb1368c8a48998, pixels 2753b12843f8cf5d...,
  ut.zip 35e72e8d7b827506f037e627029fa817181a60585f16cf54e6763a3aa162ee06 (134,897,214 bytes):
  Linux's.
- The 16-frame capture (the settings in "The same output as Linux"): all 16 pixel hashes as
  listed there, frame 11999 de09d2b4e605df41.
Real windows (Cocoa, sound off):
- Setup on OpenGL, both discs set: the Discs page (both "North American release: OK"), Display
  and Archipelago, 1840x1320 (Retina 2x), Tahoma; no config.ini written.
- The game windowed to the title (Start at 200, 400, ..., 1000): 1200 vblanks in 21 s; frame 1100
  is the title menu (de09d2b4e605df41, the headless title); SS_SHOT's window beside it wrote the
  presented picture, 2060x1546, 32 distinct byte values, the title as a player sees it.
- SS_SHOT in a headless run (its window shows on macOS): the same picture, byte for byte.
- CoreAudio, silently (volume=0 gives the stream a gain of 0): a 30 s windowed run, SS_AUDIO_STATS
  "0 restarts, 0 drops", mean depth 43 ms. (Headless runs aren't paced: their drops mean nothing.)
The release package (port/tools/build_macos.sh, 12 below):
dist/ShatteredStars-0.03.00-macos-arm64.zip, 5.3 MB, 66 s; both programs Release, stripped,
ad-hoc signed, minos 11.0, only /usr/lib and /System libraries. Unpacked with ditto as a player
would, the disc images linked into its discs/:
`--version` 0.03.00 (both); the game found the discs by itself (no SS_DISC1, no settings) and its
frame 11999 is de09d2b4e605df41 (Release + macOS 11.0 = the RelWithDebInfo frames); setup's
Auto-detect: "Found Disc 1 and Disc 2". Given a quarantine flag (as a browser download has),
`spctl --assess` says "rejected" and `syspolicy_check distribution` "Adhoc Signed App ... not
suitable for distribution" (c below).

## A macOS release (2026-10-28): what's left, with a recommendation each
**a. wss:// (hosted Archipelago rooms).** asio's TLS (and with it websocketpp's and wswrap's) is
OpenSSL's API only, so another backend would mean another transport; OpenSSL 3 it is, as on Linux
and Windows. Recommended: build_macos.sh builds OpenSSL **3.5.9** (the 3.5 LTS, supported to April
2030; github.com/openssl/openssl releases, `openssl-3.5.9.tar.gz`, 53,279,637 bytes, sha256
603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a, matching its .sha256) as static
libraries, cached in port/build-macos-release: `./Configure darwin64-arm64-cc no-shared no-tests
no-docs no-apps --prefix=... --openssldir=/etc/ssl -mmacosx-version-min=11.0`, `make build_libs
install_dev` (the Mac's own perl and make: no Homebrew, no sudo), then OPENSSL_ROOT_DIR
(build_macos.sh takes it already, and the licence from its LICENSE.txt). In the script, not an
ExternalProject in CMakeLists.txt: development builds stay quick and Linux and Windows untouched.
The CA roots: /etc/ssl/cert.pem, Apple's bundle of the system roots on every macOS
(`--openssldir=/etc/ssl` makes it OpenSSL's default, and client.cpp's system_ca_bundle() should name
it on macOS too: one guarded line). It lacks roots added to the Keychain by hand (some company
networks); reading the Keychain (Security framework) into OpenSSL's store would cover those, later
if anyone needs it.
**Blocked:** this session's permission check refused to build the downloaded OpenSSL source ("Code
from External"), as it did the Archipelago setup; the tarball waits, verified, in the session
scratchpad. The user's call. Without wss:// a Mac can join only ws:// servers (a MultiServer on the
player's own network), not archipelago.gg rooms: release macOS with it, not without.

**b. Packaging.** Built: the zip above (both programs, README.md, a macOS README.txt, LICENSE,
THIRD-PARTY-LICENSES.txt, discs/), the same shape as Linux's and Windows'. Its catch: plain Unix
programs open a Terminal window when double-clicked in Finder, and each needs its own Gatekeeper
OK. An .app bundle is what Mac players expect; it needs (all `__APPLE__`-guarded):
- sibling_program (launcher/process.cpp): in a bundle SDL_GetBasePath is Contents/Resources, the
  programs are in Contents/MacOS;
- the discs folder: not inside the bundle (changing a bundle breaks its signature), and not "next
  to the app" either: macOS runs a quarantined app from a random read-only copy (App
  Translocation) until it's moved in Finder. ~/Library/Application Support/ShatteredStars/discs,
  which disc_folders already searches, plus setup's Auto-detect and Browse; setup's tip ("a folder
  named discs next to the game") and Open discs folder would point there;
- Info.plist: CFBundleIdentifier io.github.IronJade.ShatteredStars(.Setup), LSMinimumSystemVersion,
  NSHighResolutionCapable (without it a bundled app draws at 1x on Retina); an .icns from
  port/res/icon (`iconutil`; 512 and 1024 px from the SVG).
Recommended: two bundles, "Shattered Stars.app" and "Shattered Stars Setup.app" (the pair the Linux
menu entries and Windows .exes give), each holding both programs so each works alone (about 14 MB
each), in a .dmg or zip; the plain zip as well if wanted. Minimum macOS: 11.0, the first for Apple
Silicon (builds with no availability errors; only macOS 27 tested here). arm64 only: Intel Macs end
at macOS 26, this Mac can't run x86_64 (no Rosetta), and a universal build needs OpenSSL twice
(`ARCHS="arm64;x86_64"` is there for it).
**Question for the user:** zip, .app bundles, or both? Intel Macs too (untestable here)?

**c. Gatekeeper.** Unsigned isn't an option on Apple Silicon (arm64 code runs only signed; the
linker signs ad hoc). Ad hoc (build_macos.sh today): the first open says "Not Opened"; since macOS
15 there's no Control-click Open, so the player goes to System Settings > Privacy & Security > Open
Anyway, for the game and for setup, or runs `xattr -dr com.apple.quarantine <folder>` once (the
zip's README.txt says both). macOS's privacy prompts (discs in Documents, Downloads or Desktop; the
PopTracker override folder in Documents) are tied to an ad-hoc program's hash, so they come back
after each update. Developer ID + notarization: the user's Apple Developer Program membership (99
USD a year) and its Developer ID Application certificate on the building Mac; `codesign --options
runtime --timestamp`, `xcrun notarytool submit --wait`, `xcrun stapler staple` (an .app or .dmg
takes the ticket; a bare program in a zip is checked online at first launch). The player then
double-clicks and confirms once. The hardened runtime should need no entitlements (no JIT, no
plug-ins), to check once signed. **Question for the user:** a Developer ID (their account), or ad
hoc with the README's instructions?

**d. Archipelago e2e on the Mac** (apworld/tools/ap_e2e_so2.sh, ap_e2e_hub.sh): **blocked** on the
Archipelago 0.6.8 setup (the permission check; the user). Ready for it: PopTracker 0.36.0 and UT
0.3.6 downloaded and verified ("Tracker tools" below). To adapt when it runs: those two and
ap_test_server.sh start the server with `setsid` (none on macOS: a Python `os.setsid()` exec shim)
and use GNU `timeout` (memcap.py, as the smoke test); ap_poptracker_smoke.py expects the AppImage.

**e. What else a Mac player meets.**
- Full screen: F11 is macOS's Show Desktop key (only Fn+F11 reaches the game); Option+Return
  works (SDL's Alt). The green button and Ctrl+Cmd+F (SDL's Window menu) switch to macOS full
  screen behind the game's back: the pointer stays, and the next Option+Return does nothing (the
  game thinks it's windowed). Recommended: platform/window.cpp follows
  SDL_EVENT_WINDOW_ENTER_FULLSCREEN / LEAVE_FULLSCREEN (small, shared: a Linux window manager's own
  full-screen key does the same). Cmd+Q quits at once, as the close button does.
- Retina: both programs draw at the display's pixels (above); a bundle needs
  NSHighResolutionCapable.
- Audio: CoreAudio works (silently checked above); the user to listen once.
- Gamepads: SDL's GameController framework and IOKit HID, rumble through CoreHaptics; untested
  (no pad here): the user to try one.
- Setup's file pickers: SDL's NSOpenPanel; not clicked here (it needs a person): the user to try
  Browse once.
- The PopTracker override folder: PopTracker 0.36.0 searches ~/PopTracker/user-override (when
  ~/PopTracker exists), then ~/Documents/PopTracker/user-override, with no macOS branch
  (src/poptracker.cpp); tools/tracker_maps.cpp does the same, so the path is right on macOS.
  Writing there asks for Documents access the first time (Terminal's, from the zip).
- OpenGL: 4.1, deprecated by Apple since 10.14 but present on macOS 27; a Metal presenter would be
  the long-term answer if a macOS drops it (not now).

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
10. **`-ffp-contract=off`** (port/CMakeLists.txt, every GCC / Clang build): Apple clang on arm64
   fuses a*b+c into one rounding (-ffp-contract=on by default), and the tracker maps' rasterizer
   (tools/tracker_maps.cpp) computes its barycentric weights in float, so edge pixels moved. With
   the flag the maps equal Linux's byte for byte; x86-64 builds have no FMA unless asked for, so
   Linux and Windows output stays as it was (the coordinator to confirm the map manifest
   caf89d6d... there). Linux on arm64 would have differed the same way.
11. **ap_p4_tele skips without disc 2** (port/tools/smoke_field_events.sh; the coordinator's
   call): map 303 is on disc 2, and without one beside disc 1 (or SS_DISC2) the test used to
   FAIL; it now prints "SKIP ap_p4_tele", as ap_hub_ng does.
12. **port/tools/build_macos.sh** (new): the release zip, as build_linux.sh / build_windows.sh
   make theirs: Release, CMAKE_OSX_DEPLOYMENT_TARGET (MACOS_MIN, default 11.0: without it Apple
   clang targets the building Mac's own macOS, 27 here), CMAKE_OSX_ARCHITECTURES (ARCHS, default
   arm64), OPENSSL_USE_STATIC_LIBS (never a Homebrew dylib) and OPENSSL_ROOT_DIR when given;
   strip, then an ad-hoc signature again (strip breaks the linker's), `codesign --verify`, and a
   stop if a program links anything outside /usr/lib and /System. The OPENSSL_ROOT_DIR route is
   untested until the OpenSSL build is allowed (a above).
13. **third_party_licenses.sh**: OPENSSL_LICENSE_FILE (an OpenSSL built from source has no system
   licence file), and no OpenSSL section in a ws://-only build (WSWRAP_NO_SSL in build.ninja); the
   Linux and Windows releases link OpenSSL, so theirs are unchanged.

## Tracker tools on the Mac (for the 0.03.00 tracker checks)
Planned, in the project's own places (apworld/tools/ap_test_server.sh's setup, trackers/tools):
- `scratchpad/ap/`: tools-venv (pip's uv), uv's Python 3.13 (the script's version; the coordinator
  named 3.12), Archipelago 0.6.8 (`git clone --depth 1 --branch 0.6.8`), ap-venv with its
  requirements and ModuleUpdate.py. **Blocked** (2026-10-28): this session's permission check
  refused it ("Code from External"); waiting on the user. Note for later: ap_test_server.sh's
  serve() uses `setsid`, which macOS lacks.
- PopTracker 0.36.0 for macOS: the release's `poptracker_0-36-0_macos_universal.zip` (14,868,967
  bytes, sha256 c0a694b901084fd0f963a323ad72782f4394028deb11cb8e6732bb059f0bdf8f, with a .minisig),
  in scratchpad/packgen/poptracker/ (downloaded 2026-10-28, the hash matches; not unpacked or run:
  poptracker.app, signed). trackers/tools/ap_poptracker_smoke.py expects the Linux AppImage there,
  so it needs a macOS branch.
- Universal Tracker 0.3.6: `tracker.apworld` from FarisTheAncient/Archipelago's Tracker_v0.3.6
  (192,318 bytes, sha256 39e9bc0f733aa7cd96fd57052b1c04233804862ac8543c6f4675af5f2f5cb750), in
  scratchpad/ap_ut/tracker.apworld (trackers/tools/ap_ut_check.py's default; downloaded
  2026-10-28, the hash matches).
- The user's standing instruction (relayed by the coordinator, 2026-10-28): install what build,
  test and run need. This session's permission check still refuses the Archipelago setup and the
  OpenSSL build, so both wait on the user approving them at the Mac.
- The tracker scripts' `prlimit --as=...` lines: on macOS, `tools/memcap.py DURATION CMD...` instead.

## M3 smoke
port/tools/smoke_field_events.sh port/build-macos/shattered-stars with SS_DISC1 and SS_DISC2 (the
command above): exit 0, 360 PASS, 0 FAIL, 1 SKIP (north_city: analysis/saves/dex_north_city.mcd,
a save only the Linux machine has), 3 min 21 s. Before the fixes in 8: the first run stopped at
the plain save case (bash 3.2), the second had ap_tdias_load FAIL (BSD wc) and, without
SS_DISC2, ap_p4_tele_open / ap_p4_tele_story FAIL and ap_hub_ng SKIP. Again with 10 and 11
(2026-10-28): ss_tests 103 / 0 failed, ss_game_tests 230 / 0 failed; the smoke test with SS_DISC2
360 PASS, 1 SKIP (north_city), exit 0; without it 277 PASS, 3 SKIP (ap_p4_tele, north_city,
ap_hub_ng), exit 0.

## Next
- The user's answers to "A macOS release" a-c; then OpenSSL in build_macos.sh (a), the bundles
  (b) and signing (c) as decided; the Archipelago e2e and tracker checks once the setup is
  allowed (d).
- Possible: the three window tests on macOS (a hidden Cocoa window with SDL_MAC_BACKGROUND_APP
  instead of the offscreen driver); SS_SHOT's window hidden on macOS; tools/memcap.py's Watch in
  the other sweeps (event_sweep.py, battle_sweep.py, ref_regress.py) once they run on the Mac.
