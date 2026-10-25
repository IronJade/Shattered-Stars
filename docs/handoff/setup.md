# Setup program (port feature): continuation log

Owner area: the setup program, the launcher library, main.cpp's start-up, the platform window /
audio, the packages, and the Archipelago network client (`engine/src/ap_net/`).

## 2026-10-25 (main session): seeds from Archipelago's output archive, Browse in its output folder

The user's request: setup asked for the `.apso2` itself, while Archipelago hands out the
multiworld's `AP_<seed>.zip` (every game's files; the `.apso2` is one of them), and Browse should
open Archipelago's own folder (`~/.local/share/Archipelago/output` on the user's Linux box).

- `engine/src/launcher/ap_seed.{h,cpp}` (new): `archipelago_dir()` / `archipelago_output_dir()`
  (as Archipelago's Utils.user_path / home_path: $XDG_DATA_HOME or ~/.local/share/Archipelago,
  legacy ~/Archipelago; %ProgramData%\Archipelago; ~/Library/Application Support/Archipelago;
  host.yaml's general_options.output_path, default "output"), `list_archive_seeds`,
  `read_seed_text` (an .apso2, or an archive: the only seed, or the one named), `seed_file` (the
  seed copied out to config_dir()/archipelago/, rewritten only when it changed). ss_launcher now
  links ss_mods (the zip reader).
- `mods/zip.{h,cpp}`: `ZipReader::open_file` reads only the end record and the directory, and each
  entry from the file when it is read (a multiworld's archive can be large); `open()` /
  `open_memory()` unchanged for mod packages.
- config.ini: `ap_seed` = the .apso2 or the archive; new `ap_seed_entry` = the seed in an archive
  holding several. `main.cpp`: `--ap-seed FILE` takes either, `--ap-seed-entry NAME`; the seed is
  copied out before `game::ap::start` (ap.cpp / seed.cpp untouched).
- Setup's Archipelago page: Browse filter ".zip, .apso2"; starts in the current seed's folder,
  else Archipelago's output folder, else Downloads, else the disc folders; a "Your slot" list
  (slot name + file name) when the archive holds several Star Ocean seeds (yellow status and
  sidebar dot until one is chosen; a new Browse pick clears the choice); errors for an archive
  with no Star Ocean seed. The mods page's seed lines read through the same path.
- Tests (ss_game_tests, in test_mods.cpp): mods_zip_reader_from_file,
  launcher_archipelago_seed_archives, launcher_archipelago_output_folder.
- Checked by hand: the game with `--ap-seed` on a real generator archive
  (scratchpad/deathlink/mw/out/AP_63564604658444247939.zip): loads, the copy is identical; a
  two-seed archive (Python zipfile, deflated, + a 200 KB other game's file) stops with the list
  of seeds without `--ap-seed-entry` and loads with it; setup screenshots (offscreen) of one /
  choose / chosen / none in scratchpad/apzip/shots; `--do save` writes ap_seed + ap_seed_entry.
- Docs: apworld/so2/docs/setup_en.md, README.md (How to play), engine/README.md, seed_format.md's
  opening. Left for the next release: the wiki's Archipelago page and the release notes.

### Same day: each seed on its own memory card (the user)

The user: each seed being played must have its own memory card, so nobody loads a finished game
into a seed or saves a seed over an unmodified save.

- `launcher::seed_memcard(seed, slot)` = config_dir()/archipelago/<seed>_<slot>/memcard1.mcd
  (characters a file system refuses -> '_'). `main.cpp` reads the seed before choosing cards:
  with a seed, slot 1 = the seed's card and slot 2 empty; `--memcardN` / `SS_MEMCARDN` still win;
  headless runs only with `SS_AP_SEED_CARD=1` (tests keep their cards and never touch the
  settings folder otherwise).
- `launcher::adopt_seed_saves`: when the seed's card doesn't exist yet, the first of the player's
  cards (config memcard1 / memcard2) whose sidecars name this seed and slot is copied to it with
  every other file erased, plus those sidecars; the player's card is never written. Logged as
  `[ap] N save(s) of this seed copied from your memory card to ...`.
- Setup: Archipelago page "Saves" (the seed's card: no saves yet / N saves / unreadable, Open
  folder); Memory cards page note while a seed is chosen.
- Not changed: ap.cpp's load handling (a save without this seed's sidecar still "starts fresh",
  another seed's sidecar is still used with a log line). With per-seed cards only a hand-copied
  save gets there; a refusal at load would be hubcore's file (ap.cpp), left for later if wanted.
- Tests: launcher_archipelago_seed_memcard (path, sanitizing, adoption keeps only this seed's
  save S03 of S01/S03/S05 + another game's file, \u escapes in the sidecar, once only, the
  player's card byte-identical). Headless (scratchpad/apzip/e2e, scratch XDG_CONFIG_HOME,
  SS_AP_SEED_CARD=1): a save under the real generator seed lands on
  archipelago/63564604658444247939_Claude/memcard1.mcd + .slot01.ap.json and no memcard1.mcd is
  made; the same save moved to the ordinary card is adopted on the next start ("1 save(s) ...
  copied"), the ordinary card's sha1 unchanged. Screenshots scratchpad/apzip/shots/saves*.png,
  cards.png.

## RESUME HERE (2026-10-23, packaging round: DONE; nothing running)

All four packaging tasks this round are complete and verified; nothing left open for this
round. If picked up again, re-run the Verification commands below first since other agents
keep editing shared files.

### Progress this round (all four tasks DONE)
1. **Linux container + OpenSSL/zlib:** `tools/linux-build/Containerfile` now installs
   `libssl-dev zlib1g-dev` (Debian 12 ships both the shared and static `.a` archives for
   both); `tools/build_linux.sh`'s container cmake line adds
   `-DOPENSSL_USE_STATIC_LIBS=TRUE -DZLIB_USE_STATIC_LIBS=ON`. Rebuilt the podman image and
   the package: dist tarball **8.1 MB** (was 5.8 MB without ap_net). Verified: `objdump -T`
   on both programs tops out at GLIBC_2.36; `ldd` shows only `libm.so.6`, `libc.so.6`,
   `ld-linux-x86-64.so.2` (and vdso); `strings` shows TLS/wss error text and CA-bundle paths,
   confirming OpenSSL linked statically (not silently skipped, since `find_package(OpenSSL
   QUIET)` not finding it is not a build error, only a message). smoke_field_events.sh
   against the unpacked package: **37/37** (grown from 29 since the last round; 0 FAIL). The
   host build (`engine/build`, which already had system OpenSSL) ss_tests 78/0,
   ss_game_tests 98/0 including `ap_net_connects_over_tls` against
   `tools/ap_test_server.sh start-wss`.
2. **Windows release with wss:** `tools/build_windows.sh` with `MINGW_SSL_SYSROOT=scratchpad/
   setup/mingw-ssl/usr/x86_64-w64-mingw32/sys-root/mingw`. Two real bugs found and fixed:
   - CMake's `FindOpenSSL` has a separate MinGW-specific search path that resolves its own
     `LIB_EAY` / `SSL_EAY` cache variables via a fresh `find_library()`, ignoring whatever
     `OPENSSL_SSL_LIBRARY` / `OPENSSL_CRYPTO_LIBRARY` were pre-seeded to (and the MinGW
     toolchain's `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY` means a HINTS-based
     `OPENSSL_ROOT_DIR` just gets the sysroot wrongly prepended onto our already-absolute
     path). Fix: pre-seed `LIB_EAY` / `SSL_EAY` directly (`tools/build_windows.sh`), which
     short-circuits the search the same way `ZLIB_LIBRARY` already did.
   - Once OpenSSL actually linked, the final link failed:
     `undefined reference to '__imp_PathCchRemoveFileSpec'` (OpenSSL 3's static default
     provider calls it to find its module directory; MinGW ships it in a separate
     `libpathcch.a`, not pulled in automatically). Fix: `engine/CMakeLists.txt` links
     `pathcch` for `ss_ap_net` on `WIN32` when `OpenSSL_FOUND`.
   - Result: `dist/ShatteredStars-win64.zip` **6.5 MB** (was 4.7 MB without OpenSSL/zlib;
     `shattered-stars.exe` alone 10.2 MB now, `setup.exe` unchanged at 2.68 MB since it
     doesn't link `ss_ap_net`). Verified under Wine (scratch prefix
     `scratchpad/wineprefix`, `HOME`/`XDG_CONFIG_HOME` unset, built
     `scratchpad/setup/wine/game-release.sh` pointing at `engine/build-win-release`):
     `ss_tests.exe` **78/0**; `ss_game_tests.exe` **99/0** including `PASS
     ap_net_connects_over_tls` against `tools/ap_test_server.sh start-wss`; smoke via
     `game-release.sh` **37/37**. `engine/build-win` (dev, no OpenSSL) and
     `engine/build-setup` still link after the CMakeLists.txt change (the new `pathcch`
     line is inside `if(OpenSSL_FOUND)`, so the OpenSSL-less dev build is unaffected).
3. **Licences:** both `tools/build_linux.sh` and `tools/build_windows.sh` now call
   `tools/third_party_licenses.sh BUILD_DIR SDL_LICENCE` instead of writing
   THIRD-PARTY-LICENSES.txt inline. Fixed the zlib gap the previous round flagged: no
   licence file ships on this host for zlib, so `third_party_licenses.sh` now has a
   `zlib_section()` that falls back to extracting the licence comment out of the zlib.h the
   build actually used (via `CMakeCache.txt`'s `ZLIB_INCLUDE_DIR`), finally a note that the
   zlib licence needs no binary notice. Checked inside both rebuilt packages: all 9 sections
   present with real text (SDL, Dear ImGui, apclientpp, wswrap, WebSocket++, Asio,
   nlohmann/json, OpenSSL, zlib).
4. **README:** `engine/README.md` has a new `## Archipelago` section: setup's page (seed
   file Browse, offline vs. online with Server / Slot / Password), DeathLink (follows the
   seed's own option), `--ap-seed`, the debug console commands; `tools/ap_test_server.sh`
   / `tools/ap_e2e.sh` are called out as a developer-only note, not something a player needs.
5. `.gitignore`: added `engine/build*.configure.log` (a stray sibling-of-the-build-dir file
   `tools/build_windows.sh` has always written, not covered by the existing `engine/build*/`
   pattern).

### Rules (from the coordinator; unchanged all along)
- Never pop windows: SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy for every run.
- Never touch ~/.config/shattered-stars or the user's cards: always a scratch XDG_CONFIG_HOME
  under scratchpad/setup/ (Wine: the scratch prefix scratchpad/wineprefix, which keeps the
  settings in its own AppData; unset HOME / XDG_CONFIG_HOME for wine).
- Disc images: read only. Scratch on disk in scratchpad/ (never /tmp). Builds -j8. Kill only
  by exact PID (or a process group you started).
- No commits (the coordinator commits hourly). Log here at least every 20 minutes.
- Keep engine/build, engine/build-setup and engine/build-win linking; suites green:
  ss_tests, ss_game_tests (SS_DISC1), tools/smoke_field_events.sh (SS_DISC1).
- Edit only: main.cpp, launcher/, setup/, ap_net/, platform/window* / audio*, your CMake
  lines, tools/build_*.sh and your tools, README; flag anything else.
- Archipelago: localhost servers only; never send anything to archipelago.gg or any
  public server. Pulling public base images / packages is fine; never push anything.
- Policy: enhance, don't degrade (docs/notes/original_bugs.md); make player-facing things friendly.

### State: everything builds, suites green at the last run
engine/build, build-setup, build-win and build-win-release all build every target. Linux
(host) ss_tests 78/0, ss_game_tests 98/0 (ap_net cases including wss against a local
server), smoke 37/37 on the packaged container game. Wine (build-win-release): ss_tests.exe
78/0, ss_game_tests.exe 99/0 (incl. wss), smoke 37/37. Full detail in "Progress this round"
above.

### 1. Portable Linux package (DONE, including OpenSSL/zlib this round)
`tools/build_linux.sh --container`: podman, Debian 12 (tools/linux-build/Containerfile,
FROM debian@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61, image
localhost/shattered-stars-linux-build:bookworm), GCC 12.2 / CMake 3.25; SDL 3.4.18 static
(all backends dlopened) and ImGui from FetchContent; libstdc++ static; libssl-dev /
zlib1g-dev added, OpenSSL and zlib linked in statically
(-DOPENSSL_USE_STATIC_LIBS=TRUE -DZLIB_USE_STATIC_LIBS=ON); repo mounted read-only
(--security-opt label=disable), build in engine/build-linux-container. Result:
dist/ShatteredStars-linux-x86_64.tar.gz (8.1 MB, up from 5.8 MB pre-ap_net), GLIBC_2.36 max
(objdump -T), NEEDED libc / libm / ld-linux only (ldd; confirmed via strings that OpenSSL's
TLS code actually linked in, not silently skipped). Verified: smoke 37/37 from the unpacked
package; wss works (ss_game_tests' ap_net_connects_over_tls, host build, against
tools/ap_test_server.sh start-wss). Host mode (no --container) needs
LIBSTDCXX_STATIC_DIR=scratchpad/setup/static/usr/lib/gcc/x86_64-redhat-linux/16 here and
gives glibc 2.43 (not touched this round; still has no OpenSSL/zlib flags, since host build
wasn't asked for and already differs in the glibc floor it reports).

### 2. Archipelago network client (WORKING; integration done; packaging open)
Interface agreed with the world agent (a4efaa8a3ecfb676d, owner of engine/src/game/ap/):
- `engine/src/ap_net/link.h` (plain types; game/ap uses only this): ConnectInfo (server,
  slot, password, game, uuid, items_handling = 0b001 others' items only, deathlink), State,
  NetItem (item, location, player, flags, index), Event (kStatus, kConnected {slot, team,
  seed_name, checked, missing, slot_data_json}, kRefused, kItems, kDeathLink {source,
  text}, kMessage {text, item_send}), abstract Link (check, goal, deathlink, say, poll,
  state, player_name, item_name, location_name).
- `engine/src/ap_net/client.{h,cpp}`: `Client : Link` on apclientpp v0.6.6 (+ wswrap
  aeba7ac, websocketpp 0.8.2, asio 1.28.2, nlohmann/json 3.11.3; FetchContent, SHA-256
  pinned in CMakeLists; target ss_ap_net, C++17; defines ASIO_STANDALONE AP_NO_SCHEMA
  AP_NO_DEFAULT_DATA_PACKAGE_STORE _WEBSOCKETPP_CPP11_STL_ _WEBSOCKETPP_CPP11_THREAD_).
  Own thread, one mutex around APClient, in-memory data package store, own DeathLink echo
  dropped, Linux CA bundle discovery, OpenSSL / zlib optional (WSWRAP_NO_SSL /
  WSWRAP_NO_COMPRESSION without them).
- game/ap (theirs): `connect(unique_ptr<Link>)` installs Runtime::net_poll, polls per
  vblank, matches seed_name, re-sends checks / goal, gives items by index in free play,
  DeathLink both ways, notices. `deathlink_enabled()` from the seed's options.
- main.cpp (mine): after their `ap::start` block, on a windowed run with config ap_server:
  builds `ap_net::Client` (slot = ap_slot or the seed's slot_name, password, deathlink,
  uuid = config ap_uuid made once and saved unless a test run; env SS_AP_GAME overrides the
  game name for tests) and calls `game::ap::connect`. CMake links ss_ap_net into the game
  and defines SS_HAVE_AP_NET.
- Setup's Archipelago page is live: seed file (Browse, .apso2, checked with the game's
  `game::ap::load_seed_file`: setup compiles game/ap/seed.cpp + json.cpp), server, slot
  (hint: the seed's slot), password (Show). Keys ap_seed, ap_server, ap_slot, ap_password.
- Verified end to end (scratchpad/setup/e2e): the windowed game (offscreen) with a test
  seed for slot Alice (seed 13515657874892102023, ChecksFinder ids 81000.. / 80000..,
  SS_AP_GAME=ChecksFinder) connected, matched the seed, sent `ap_check 81000` (server: "Alice
  sent Map Width to Bob"), received Bob's items and gave them in free play (5 given, 10
  waiting behind the banner); ap_status "connected | DeathLink". Tell the world agent.
**Open (all DONE this round — see "Progress this round" at the top for how):**
- ~~Windows release with wss~~: done; needed pre-seeding `LIB_EAY` / `SSL_EAY` (not
  `OPENSSL_SSL_LIBRARY` / `OPENSSL_CRYPTO_LIBRARY`, which CMake's MinGW-specific FindOpenSSL
  path ignores) and linking `pathcch` for OpenSSL 3's static provider. engine/build-win
  (dev) still has no OpenSSL: ws:// only, unaffected.
- ~~Linux container~~: done, see section 1.
- ~~README~~: done, `## Archipelago` section added.
- A test of setup's seed check (good seed / bad seed) would still be cheap — not done this
  round (out of scope: the four packaging tasks only).

### Tools and how to run them
- `tools/setup_screenshots.sh [PROGRAM] [OUT]`: every setup page and state offscreen as
  PNGs (scratch settings); PROGRAM may be engine/build-win/setup.exe (Wine). Needs SS_DISC1
  for most states. Default out: scratchpad/setup/shots/.
- `tools/ap_test_server.sh start [PORT] | start-wss [PORT] | stop | address`: local
  MultiServer (127.0.0.1 only; scratchpad/ap: uv Python 3.13, Archipelago 0.6.8 clone, venv,
  ChecksFinder multiworld Alice / Bob seed 12345 = seed name 13515657874892102023; own
  process group; --disable_save, fresh at each start; start-wss = self-signed cert
  scratchpad/ap/cert.pem). Goal announcements happen once per server run.
- `tools/build_linux.sh [--container]`, `tools/build_windows.sh` (see above),
  `tools/third_party_licenses.sh BUILD_DIR SDL_LICENCE`.
- `scratchpad/setup/wine/game.sh`: wraps the Windows game for
  `tools/smoke_field_events.sh scratchpad/setup/wine/game.sh` (smoke under Wine).
- `scratchpad/setup/e2e/`: the end-to-end seed, config and bob.py (Python client as Bob).

### Verification commands
```sh
D1=".../Star Ocean - The Second Story (Disc 1).cue"   # see the task text for the full path
export SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy XDG_CONFIG_HOME=$PWD/scratchpad/setup/cfg_x
SS_DISC1="$D1" engine/build/ss_tests; SS_DISC1="$D1" engine/build/ss_game_tests
SS_DISC1="$D1" tools/smoke_field_events.sh
tools/ap_test_server.sh start; tools/ap_test_server.sh start-wss
SS_AP_SERVER=ws://localhost:38281 SS_AP_SERVER_WSS=wss://localhost:38282 \
  SS_AP_CERT=$PWD/scratchpad/ap/cert.pem SS_DISC1="$D1" engine/build/ss_game_tests
tools/ap_test_server.sh stop
tools/build_linux.sh --container; objdump -T dist/ShatteredStars-linux-x86_64/shattered-stars | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1

# Windows release (wss-capable): MINGW_SSL_SYSROOT points at the already-unpacked RPMs.
MINGW_SSL_SYSROOT=$PWD/scratchpad/setup/mingw-ssl/usr/x86_64-w64-mingw32/sys-root/mingw tools/build_windows.sh
cmake --build engine/build-win-release -j8 --target ss_tests ss_game_tests
export WINEPREFIX=$PWD/scratchpad/wineprefix WINEDEBUG=-all; unset XDG_CONFIG_HOME
D1W=$(env -u HOME winepath -w "$D1" 2>/dev/null)   # capture stderr separately: EGL noise corrupts it otherwise
CERTW=$(env -u HOME winepath -w "$PWD/scratchpad/ap/cert.pem" 2>/dev/null)
(cd engine/build-win-release && SS_DISC1="$D1W" env -u HOME wine ss_tests.exe)
tools/ap_test_server.sh start; tools/ap_test_server.sh start-wss
(cd engine/build-win-release && SS_AP_SERVER=ws://localhost:38281 SS_AP_SERVER_WSS=wss://localhost:38282 \
  SS_AP_CERT="$CERTW" SS_DISC1="$D1W" env -u HOME wine ss_game_tests.exe)
tools/ap_test_server.sh stop
SS_DISC1="$D1" tools/smoke_field_events.sh scratchpad/setup/wine/game-release.sh   # wraps build-win-release
```

## Round 3 (2026-10-23 ~02:30): portable Linux package, then the Archipelago client

- **Linux package for older systems** (done): `tools/build_linux.sh --container` builds in
  podman on Debian 12 (`tools/linux-build/Containerfile`, FROM debian@sha256:854dd8ab...,
  image tag localhost/shattered-stars-linux-build:bookworm; GCC 12.2, CMake 3.25). SDL 3.4.18
  (static, all backends dlopened) and ImGui come from the project's FetchContent (pinned by
  SHA-256); libstdc++ / libgcc static; repo mounted read-only (label=disable, no SELinux
  relabelling), build in engine/build-linux-container. Result: 5.8 MB tarball, both programs
  need GLIBC_2.36 at most (objdump -T), NEEDED only libc / libm / ld-linux. Checked: the
  unpacked package passes tools/smoke_field_events.sh (29/29) and renders every setup
  screenshot (OpenGL) on this machine; inside plain debian:12 (glibc 2.36, no GL, no fonts)
  the game runs headless and setup renders through the SDL software renderer with the logo.
  apt packages in the image are Debian 12's current ones (not snapshot-pinned).
- **Archipelago network client** (in progress, working):
  - Interface agreed with the world agent: `engine/src/ap_net/link.h` (plain types: Link,
    ConnectInfo with items_handling 0b001, State, NetItem, Event); `ap_net/client.h`
    `Client : Link` on apclientpp v0.6.6 + wswrap aeba7ac + websocketpp 0.8.2 + asio 1.28.2
    + nlohmann/json 3.11.3 (all FetchContent, SHA-256 pinned; target ss_ap_net, C++17;
    _WEBSOCKETPP_CPP11_THREAD_ for MinGW). Own thread; one mutex around APClient; events
    drained by poll(); own DeathLink echo dropped; in-memory data package store (nothing
    written to ~/.cache); Linux finds the distribution's CA bundle itself (static OpenSSL
    in the release); OpenSSL / zlib optional (MinGW here: ws:// only).
  - Local server: `tools/ap_test_server.sh start|start-wss|stop` (scratchpad/ap: uv
    Python 3.13, Archipelago 0.6.8, ChecksFinder slots Alice / Bob, seed 12345, 127.0.0.1
    only, own process group; start-wss with a self-signed localhost certificate).
  - Tests `tests/test_ap_net.cpp` (in ss_game_tests, skip without SS_AP_SERVER): connect +
    names, checks / items 0..24 / reconnect resume, DeathLink (other slot only) + goal +
    InvalidSlot, wss, unreachable server. Pass on Linux and under Wine (ws).
  - Setup's Archipelago page is live: seed file (Browse, .apso2 filter, quick check with
    game/ap/json.cpp), server, slot (hint: the seed's slot), password (Show). Keys ap_seed,
    ap_server, ap_slot, ap_password (left out when empty). Screenshots archipelago*.png.
  - Waiting on the world agent: `game::ap::connect(unique_ptr<Link>)` +
    `deathlink_enabled()` (then main.cpp builds the Client after their ap::start block),
    and the seed reader split into game/ap/seed.{h,cpp} (asked).
  - To do: Containerfile + build_linux.sh: libssl-dev / zlib1g-dev, static OpenSSL; the
    Windows zip without OpenSSL is ws-only (mingw64-openssl needed).

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
