# Handoff: validate the Windows port on a real Windows machine (0.02.50 RC)

For the agent on the user's Windows machine (Remote Control session "Windows Shattered-Stars"),
after the user has pushed the 0.02.50 release prep to GitHub. Coordinator: the Linux session
("Linux Shattered-Stars"); report back to it with SendMessage, and keep this file's "Results" current
(the user commits it).

## What you need on the Windows machine
- The repo: `git pull` (main) after the user's push.
- **The user's disc images, copied by the user from the Linux machine** (never in git, never pushed,
  never copied into the repo folder): Star Ocean: The Second Story, North American Disc 1 and Disc 2,
  each a `.cue` with its `.bin`. Optional for region checks: Europe / France / Germany, two discs each.
- **The Windows release bundle** from the Linux RC run (the zip the release agent lists in
  docs/ap/release_checklist.md section 9 / docs/handoff/release.md), copied by the user. Test the
  bundle first: it is what players get. Building from source on Windows is a second, optional check
  (wiki "Building-from-Source").
- Optional for Archipelago: Archipelago 0.6.8 or newer for Windows (installs to
  `C:\ProgramData\Archipelago` by default) and the `so2.apworld` from the bundle's release files.

## Rules
- Use a test settings folder, not the user's: run the game and setup with `APPDATA` pointing at a
  scratch folder (e.g. `set APPDATA=%USERPROFILE%\ss-test\appdata`) so `%APPDATA%\ShatteredStars`
  is a fresh one. Never delete or overwrite the user's own saves or memory cards.
- No game code, data or screenshots of game data in git, except what the project already allows
  (docs/images, the user's own decision).
- Report a crash with its full message / backtrace and the exact steps.

## The checks

### 1. Install and setup
1. Unzip the bundle into a path with a space and a non-ASCII character (e.g. `C:\Games\Shattered Stars
   Ä\`), and also into a short path. Both must work.
2. Start setup (setup.exe): the Discs page finds / accepts Disc 1 (Browse), Disc 2 found next to it;
   the header shows the title logo from the disc; the Disc 1 and 2 status dots.
3. config.ini is created at `%APPDATA%\ShatteredStars\config.ini` (setup's header shows the path).
4. Display / Sound / Controls / Gameplay pages: change something, Save, reopen: kept.

### 2. Memory cards and save slots (the user's focus)
1. Memory cards page: slot 1 defaults to `%APPDATA%\ShatteredStars\memcard1.mcd` (created formatted
   on first use); slot 2 empty by default; Browse to another card; Default; Open folder opens the
   right Explorer folder.
2. Play: New Game, save at the first save point to card slot 1, file #1 and #3; quit; Continue: both
   saves listed and load. Check the files exist where the page says, and that their size is
   131072 bytes.
3. Card 2: insert a second card, save to it, load from it.
4. A path with spaces / non-ASCII for a card (Browse): save and load.

### 3. Archipelago (setup and game)
1. Generate a solo seed with Archipelago (or use one the user provides): the output is
   `C:\ProgramData\Archipelago\output\AP_<seed>.zip`.
2. Setup's Archipelago page: **Browse** opens Archipelago's output folder by default; pick the
   **zip** (no unpacking); with two Star Ocean players in one zip, the "Your slot" list appears;
   the status line shows the seed, the slot and the locations.
3. **Saves:** the page shows "No saves yet" and, after a game, "N saves on this seed's card" with
   Open folder -> `%APPDATA%\ShatteredStars\archipelago\<seed>_<slot>\` holding `memcard1.mcd` and
   `memcard1.slotNN.ap.json`; the seed copied out as `%APPDATA%\ShatteredStars\archipelago\
   AP_..._P1_<slot>.apso2`. The ordinary memcard1.mcd is NOT written while the seed plays.
4. The sidebar's **Archipelago switch**: off -> the game plays the original on the ordinary cards (no
   `[ap]` lines in `%APPDATA%\ShatteredStars\shattered-stars.log`); on -> the seed again.
5. In the game with a seed: New Game (the story route skip if the seed fixes the hero; the character
   select in an open-world / hub seed with starting_character any; the difficulty), a chest check,
   a save, quit, Continue, the load works. Copy an ordinary save onto the seed's card with a card
   manager or by hand (or edit one byte of the seed's save): the load must be refused ("The data could
   not be loaded. Your data may be faulty.").
6. Online (optional): connect to a local or archipelago.gg room; a check goes out, an item comes in.
7. `--debug` with a seed (start the game from a console: `shattered-stars.exe --debug`): the log says
   it was ignored; without a seed `--debug` opens the console and exits cleanly when the game closes
   (the 0.02.50 fix: no hang at exit).

### 4. The test suites natively (developer check)
- If building from source: `ss_tests.exe`, `ss_game_tests.exe` with `SS_DISC1` set to the Windows
  path of the Disc 1 `.cue`. Report any failure; in particular whether `field_text_layout_map7` (it
  crashed under Wine in the RC run: a null read in field_create_state_block) passes on real Windows.

## Results

### 2026-10-25: the 0.02.50 RC (c9a8200) on Windows 11
Run by the Windows agent (Remote Control session "Windows Shattered-Stars") with the RC's
`ShatteredStars-0.02.50-win64.zip` (scratchpad/release/rc-0.02.50-dist) and so2.apworld 0.2.0,
the files in `C:\Users\hitsu\Documents\Star Ocean 2\`, Archipelago freshly installed at
`C:\ProgramData\Archipelago`. Written into the repo by winfix from the coordinator's relay (the
Windows agent's summary: scratchpad/agents.txt "windows").

**Passed:** every player-facing check of sections 1-3 (install and setup, memory cards and save
slots, Archipelago in setup and in the game). The 0.02.50 fix for saves under a non-ASCII folder
(engine/src/psx/memory_card.cpp, the RC's "Status 21:58") is confirmed on real Windows.
**Not run:** section 4 (the native test suites): no toolchain on that machine.
(The per-check pass list, if the coordinator has one beyond this summary, belongs here; release.md's
coordinator section had none when this was written.)

**Found (all fixed before tagging, the user's ruling; agent winfix, docs/handoff/winfix.md):**
1. **Crash: a non-ASCII path on the command line aborts both programs.** E.g.
   `shattered-stars.exe --headless --memcard1 "C:\...\Test Ä\card.mcd"` and
   `setup.exe --set "memcard1=...Test Ä..."`: `terminate called after throwing an instance of
   'std::filesystem::__cxx11::filesystem_error' what(): filesystem error: Cannot convert character
   sequence: Illegal byte sequence`. argv arrives in the ANSI code page (Ä = 0xC4) and was read as
   UTF-8; every path argument (--disc1/2, --memcard1/2, --ap-seed, ...) and the SS_* path variables
   were affected. Players going through setup were not (Save & Play passes no arguments, and
   config.ini is UTF-8).
2. **Setup's Auto-detect** with Disc 1 already set filled Disc 2 correctly but said "Found Disc 2,
   but no Disc 1."
3. **"pwnd->destroy"** went to stderr every time a menu window closed (an original debug printf).
4. **shattered-stars.log had only some [ap] lines** (the ones through report(): "plays on its own
   memory card", "ignored"); the seed / title / check / saved / loaded lines reached the console only.
5. **The Archipelago client announced Client(0.6.6)** (apclientpp's own version) to a 0.6.8 server.
6. **Release-notes nit:** the notes name the YAML `Star.Ocean.The.Second.Story.yaml`, the checklist
   `Star Ocean The Second Story.yaml`.

**The fixes, in short:**
1. Both programs take their arguments from `GetCommandLineW` and the SS_* variables from
   `GetEnvironmentVariableW`, converted to UTF-8 once (engine/src/util/utf8.{h,cpp}); paths from
   UTF-8 never throw (a string that isn't UTF-8, e.g. a config.ini Notepad saved as "ANSI", is read in
   the ANSI code page), and a filesystem error that still escapes is reported and exits cleanly
   (exit 1) instead of `std::terminate`. Console lines are written through `WriteConsoleW`, so "Ä"
   shows as "Ä" in cmd / PowerShell. No UTF-8 `activeCodePage` manifest (not needed, Windows 10
   1903+ only, process-wide; winfix.md).
2. Auto-detect says "Found Disc 2 next to your Disc 1." (or "Found Disc 2. Your Disc 1 stays as it
   is." when it came from a discs folder).
3. The print is gone (a comment marks where the original has it).
4. Every [ap], [port] and [mods] line goes through one reporter (engine/src/util/log.h) to the
   console as before and to the log; the log stays under 4 MB (it keeps the run's first MB, a "left
   out" line and the newest 2 MB). The game's own lines average ~55 bytes: a whole seed's checks and
   items are ~80 KB; the bulk is the server's broadcast lines ("server says: X sent Y to Z", ~100
   bytes, one per item found anywhere in the multiworld).
5. The client announces the version the world targets: archipelago.json's `minimum_ap_version`,
   0.6.8 (read by CMake); the game's "Archipelago: connecting to ..." line names it.
6. The download really is `Star.Ocean.The.Second.Story.yaml` (GitHub turns spaces into dots; the
   v0.01.00 and v0.02.00 assets are named so): the notes stay, the checklist and SHA256SUMS now
   use that name too.

### Recheck after the fixes (on the Windows machine, with the new bundle)
Use a test settings folder (`set APPDATA=%USERPROFILE%\ss-test\appdata`) and a folder with a
non-ASCII name, e.g. `%USERPROFILE%\ss-test\Test Ä 日本\` (copy Disc 1's .cue/.bin and an
`AP_<seed>.zip` into it).
1. **The CLI Ä cases** (from cmd or PowerShell): no crash, and the paths work:
   - `shattered-stars.exe --headless --frames 600 --memcard1 "%USERPROFILE%\ss-test\Test Ä 日本\card.mcd"`
     -> "stopped after 600 vblanks".
   - `shattered-stars.exe --disc1 "...\Test Ä 日本\<Disc 1>.cue" --memcard1 "...\Test Ä 日本\card.mcd"`
     (windowed): New Game, save to card 1, quit; the card is in that folder (131072 bytes);
     Continue loads it. config.ini then holds both paths with "Ä" (UTF-8).
   - `shattered-stars.exe --ap-seed "...\Test Ä 日本\AP_<seed>.zip"`: the seed loads ("[ap] seed ...").
   - `set SS_MEMCARD1=...\Test Ä 日本\card.mcd` then `shattered-stars.exe --headless --frames 600`.
   - `setup.exe --set "memcard1=...\Test Ä 日本\card.mcd"`: setup opens, the Memory cards page shows
     the path; Save writes it to config.ini.
   - The console shows "Ä" and "日本" in those paths as written (not "Ã„").
2. **The Auto-detect message:** setup with Disc 1 chosen (Browse) and Disc 2 empty, no discs folder:
   Auto-detect fills Disc 2 and says "Found Disc 2 next to your Disc 1." (green).
3. **The log lines:** play a seed from a console; `%APPDATA%\ShatteredStars\shattered-stars.log`
   holds the same [ap] lines as the console (seed, title, check, saved, loaded, ...). Double-clicked
   too. No "pwnd->destroy" anywhere when menu windows close.
4. **The client version:** connect to a room (local or archipelago.gg): the server's join message
   says `Client(0.6.8)`, the game's log "Archipelago: connecting to ... (client version 0.6.8)",
   and checks / items still flow.
5. Optional, with a toolchain: `ss_tests.exe` (the utf8_* cases) and `ss_game_tests.exe`
   (game_command_line_non_ascii_paths, setup_command_line_non_ascii_and_autodetect,
   launcher_log_holds_game_lines_and_stays_bounded, launcher_log_without_a_console).
6. **Icon and names** (docs/handoff/appicon.md; the gold shattered star on a midnight-blue tile):
   - Explorer shows that icon on `shattered-stars.exe` and `setup.exe`; Properties -> Details:
     "Shattered Stars" / "Shattered Stars Setup", product "Shattered Stars", version 0.02.50,
     copyright "Copyright (c) 2026 DreamGlyph Studios".
   - `setup.exe` starts without a UAC (administrator) prompt, double-clicked and from the game.
   - The game's window is titled "Shattered Stars" (with `--debug`: "Shattered Stars - map ...").
   - While each runs: the title bar, the taskbar and Alt+Tab show the icon, at 100% and at a
     higher display scale (125-150%); Task Manager's Processes tab lists "Shattered Stars" /
     "Shattered Stars Setup" with the icon (0.02.50 RC: "POSIX WinThreads for Windows").
   - Any error box either program shows looks like a normal Windows 10/11 dialog (the manifest
     asks for Common Controls 6).

### Recheck for 0.02.50 RC2 (2026-10-26)

Three more agents' work landed since the list above: winfix (items 1-5 above), appicon (item 6
above), and escquit (the Esc quit confirmation, new this round). One pass on the real Windows
machine with the RC2 bundle covers all three; same scratch `%APPDATA%` and `Test Ä 日本` folder
as above.

1. **winfix** — non-ASCII CLI paths and SS_* vars no longer crash (console shows Ä/日本
   correctly); Auto-detect with Disc 1 set says "Found Disc 2 next to your Disc 1."; no
   "pwnd->destroy" when a menu window closes; `shattered-stars.log` holds the same [ap]/[port]/
   [mods] lines as the console; a room connection shows `Client(0.6.8)`, not `Client(0.6.6)`.
2. **appicon** — the gold-star icon on both exes in Explorer, Task Manager, Alt+Tab and the
   taskbar at 100% and 125-150% scale; Properties → Details: "Shattered Stars" /
   "Shattered Stars Setup", version 0.02.50, copyright "Copyright (c) 2026 DreamGlyph Studios",
   no "POSIX WinThreads for Windows"; `setup.exe` starts with no UAC prompt; window titles
   "Shattered Stars" / "Shattered Stars Setup" (`--debug`: "Shattered Stars - map ...").
3. **escquit** — Esc opens "Quit Shattered Stars?" (never an immediate close), by keyboard and
   by gamepad; the same behavior windowed and fullscreen; **Keep playing** resumes with no
   state change, **Quit** exits cleanly; audio pauses cleanly while the prompt is up (no pop or
   stutter) and resumes normally after Keep playing; keys/buttons held while the prompt is up
   don't leak into the game once it closes.

### 2026-10-26: RC2 (877b771) on Windows 11: all checks PASS
Run by the Windows agent with the RC2 files (zip 1d573a50…, so2.apworld aa57e9d9…, the dotted YAML
adada8d0…), unpacked fresh into "…\Shattered Stars Ä\" and a plain path, a scratch APPDATA (the user's
real %APPDATA%\ShatteredStars never created). Keys sent to the game window through the Windows API;
command-line cases from PowerShell.
- **Command line:** PASS. `--headless --memcard1`, `SS_MEMCARD1` + `SS_DISC1`, windowed `--disc1` +
  `--memcard1` (save #1 / #3, Continue, load), `--ap-seed …\AP_….zip`, `--ap-seed-entry
  "AP_…_P2_Cléo.apso2"`, `setup.exe --set "memcard2=…日本\…" --do save`, all under
  "C:\Users\hitsu\ss-test\Test Ä 日本\": no crash, config.ini in UTF-8, the console shows Ä / 日本.
- **Auto-detect:** PASS ("Found Disc 2 next to your Disc 1.", green).
- **Log:** PASS: console and double-clicked runs log the seed, title, loaded, [mods], check and quit
  prompt lines; no "pwnd->destroy" anywhere (the string is gone from the exe).
- **Online:** PASS: "connecting … as Cléo (client version 0.6.8)", the server's Client(0.6.8);
  "Cléo sent Star Ruby to Bowie", Bowie received it.
- **Names and icon:** PASS: VersionInfo "Shattered Stars" / "Shattered Stars Setup", 0.02.50,
  DreamGlyph Studios; no "WinThreads"; manifests asInvoker / PerMonitorV2 / Common Controls 6; Task
  Manager descriptions and window titles right; setup.exe opens with no UAC prompt (double-click and
  from the game); the gold star on the exes, both title bars, two taskbar buttons and Alt+Tab.
- **Esc:** PASS (keyboard): the prompt, Left / Right / Enter, Esc Esc back, Esc Right Enter quits
  (exit 0, ~1.2 s); audio without clicks across three prompts; works in borderless and exclusive
  fullscreen (by the log and exit code); X / Alt+F4 quit at once.
- **Core rerun:** PASS: card slots 1 and 2 on the 日本 cards, the seed's own card, the save guard
  (a changed byte and a foreign save refused).
- **Not covered:** a gamepad (none attached), display scaling above 100 %, fullscreen visuals
  (screen capture can't see a fullscreen GL window).
- **Observations (small, for a later update):** setup's font has no CJK glyphs (日本 shows as boxes
  on the Memory cards page); a double-clicked run's closing "stopped after N vblanks" stdout line
  isn't in the log; the --debug console window is titled with the exe's full path.

**Released:** v0.02.50 on 877b771, published 2026-10-26 06:23 UTC
(https://github.com/IronJade/Shattered-Stars/releases/tag/v0.02.50); the wiki pushed (a72a063).

## Recheck for 0.02.51 (2026-10-27)

For the Windows machine, with the 0.02.51 release candidate: the port's
`ShatteredStars-0.02.51-win64.zip` (scratchpad/release/rc-0.02.51-dist/port/, sum in its SHA256SUMS
and docs/ap/release_checklist.md section 11) and the world's `so2.apworld` 0.2.1 with
`Star.Ocean.The.Second.Story.yaml` (scratchpad/release/rc-0.02.51-dist/apworld/, the same files as
dist/apworld-0.2.1). The two now ship as separate GitHub releases (`v0.02.51` and
`apworld-v0.2.1`). Same rules as above: a scratch `%APPDATA%` (`set APPDATA=%USERPROFILE%\ss-test\appdata`),
the `Test Ä 日本` folder, never the user's own saves. Replace the old `so2.apworld` in
`C:\ProgramData\Archipelago\custom_worlds` with the 0.2.1 one (one copy only).

What changed for a Windows player since 0.02.50: the version; setup's Archipelago page warns about a
hell seed; the bench (Party Order / Tactics flip with L1 / R1, its save); the starting member in
the party's first place; the saves' sidecars (items waiting, the bench, a held Dias). No change to
the icon, the version resource's layout, Esc, or the path handling, so those get a glance only.

Seeds (generate on the Windows machine, `Generate.py` or the Launcher, each from its own YAML):
- **hell**: `world_mode: hub`, `hub_start: fun_city`, `story_route: claude`, `encounter_rate: 0`,
  `killer_move_checks: true` (disc 2 needed: hub mode asks for it at New Game).
- **ordinary**: the template as it is (story mode).
- **bench**: `world_mode: open_world`, `character_items: true`, `story_route: claude`,
  `starting_character: ashton`, and `start_inventory:` `{Rena: 1, Celine: 1, Bowman: 1, Dias: 1,
  Precis: 1, Leon: 1, Opera: 1, Ernest: 1, Noel: 1, Chisato: 1}` (all 12 at once: 8 in the party,
  4 on the bench). Its `.apso2` says `"version": 4` and `"bench": true`.

1. **The version.** `shattered-stars.exe --version` / `setup.exe --version`: "Shattered Stars
   0.02.51" / "Shattered Stars Setup 0.02.51"; setup's header "Setup · v0.02.51"; Explorer →
   Properties → Details on both `.exe`s: version 0.02.51 (file version 0.2.51.0), "DreamGlyph
   Studios", no "POSIX WinThreads".
2. **Setup's Archipelago page, hell seed:** Browse to the hell seed's `AP_<seed>.zip`: under the
   seed's line the warning "Hell Seed: no random battles. Killer-move levels and learn points are
   trained in the Fun City arena, which is free on this seed. Expect a very long grind." Save &
   Play: `%APPDATA%\ShatteredStars\shattered-stars.log` holds the same line with the seed's `[ap]`
   lines. In the game (New Game, Fun City): at the arena desk, **Register.** says "An entry fee of
   0 FOL" and your Fol stays as it was; a fight there pays its EXP and Fol. Optional: walk out to the world map: no random battle.
3. **Setup's Archipelago page, ordinary seed:** Browse to the ordinary seed: the seed's line and no
   warning; after a run the log has no "Hell Seed" line. Switch back and forth between the two
   seeds: the warning comes and goes with the seed.
4. **The bench seed, New Game:** after the opening, at the first free play everyone arrives:
   Ashton in the party's first place (the one you control in battle, first in the camp menu's
   member list), Claude second; six more fill the 8; four go to the bench (one banner each).
5. **Party Order's flip:** camp menu → Party Order: L1 / R1 (keyboard: as bound) flips the
   right-hand column between the reserve and the bench (Opera / Ernest / Noel / Chisato there).
   Pick a reserve member, flip, pick a benched one: they swap (the first pick stays held across the
   flip). Picking Claude and then a bench place is refused. **Tactics** flips the same way. A
   benched member's Equipment / Skills / Status pages open.
6. **Save and load with a bench:** save (world map or a save point) to the seed's card; quit
   (Esc → Quit); start again and Continue: the load works, and Party Order shows the same 8 and the
   same bench, in the same order. In `%APPDATA%\ShatteredStars\archipelago\<seed>_<slot>\` the
   save's `memcard1.slotNN.ap.json` has `"version": 3` and a `"bench"` list. The log's
   "[ap] loaded ..." line ends with ", bench: " and the four benched members.
7. **A glance at the rest** (unchanged since the 0.02.50 RC2 pass, which passed): Esc opens "Quit
   Shattered Stars?" (Keep playing / Quit); the gold-star icon on both `.exe`s, the title bars and
   the taskbar; the bundle unpacked into "…\Shattered Stars Ä\" starts and saves; a
   `--memcard1 "…\Test Ä 日本\card.mcd"` run doesn't crash.
8. Optional, with a toolchain: `ss_tests.exe` / `ss_game_tests.exe` (the RC ran them under Wine:
   section 11).

## Recheck for 0.03.00 (written 2026-10-27, agent releasedocs; to run with the 0.03.00 drafts)

For the Windows machine, with the three drafts of docs/ap/release_checklist.md section 12
(`gh release download TAG --repo IronJade/Shattered-Stars`, then `sha256sum -c SHA256SUMS` or
`Get-FileHash` against each release's SHA256SUMS):

- the port, `v0.03.00`: `ShatteredStars-0.03.00-win64.zip`;
- the world, `apworld-v0.2.2`: `so2.apworld` 0.2.2 and `Star.Ocean.The.Second.Story.yaml`;
- the PopTracker pack, `poptracker-v1.0.0`: `so2_shattered_stars_1.0.0.zip`.

New programs for this pass (players' own, both third-party): **PopTracker 0.36.0** for Windows
(https://github.com/black-sliver/PopTracker/releases, the Windows zip) and **Universal Tracker
0.3.6** (`tracker.apworld` of https://github.com/FarisTheAncient/Archipelago/releases, tag
`Tracker_v0.3.6`). Archipelago 0.6.8 or newer as before, in `C:\ProgramData\Archipelago`.

Same rules as above: a scratch `%APPDATA%` (`set APPDATA=%USERPROFILE%\ss-test\appdata`), the
`Test Ä 日本` folder, never the user's own saves. Replace the old `so2.apworld` in
`C:\ProgramData\Archipelago\custom_worlds` with the 0.2.2 one (one copy only).

**The PopTracker folder is the real one.** The map render's default target is PopTracker's own
override folder for this pack, which is what the feature is for: `PopTracker\user-override\
so2_shattered_stars` under `%USERPROFILE%` if PopTracker keeps its folder there (it makes
`%USERPROFILE%\PopTracker\packs` when it installs a pack), else under `Documents`. It writes only
PNGs, and only in that folder. Before starting, note whether `%USERPROFILE%\PopTracker` and
`Documents\PopTracker` exist, and whether an `so2_shattered_stars` folder is already in either; say
in the results which folder the render used. If Documents is redirected (OneDrive), say so too.

What changed for a Windows player since 0.02.51: the version; while connected to its room the game
writes its map and story position to the server (for trackers); setup's Archipelago page has a
new **Trackers** part with the **Render tracker maps for PopTracker** button; the
`--render-tracker-maps [DIR]` command; the world 0.2.2 (Universal Tracker without a YAML,
`/explain`, story awareness, the logic for PopTracker); the pack. Nothing else in the game
changed, so the 0.02.51 items get a glance only.

Seeds (generate on the Windows machine, each from its own YAML):
- **story**: the template as it is (story mode). For story awareness.
- **open**: `world_mode: open_world`, `character_items: true`, `craftsanity: true`,
  `killer_move_checks: true`. For the logic colours (green / red).
- Optional, **old**: any seed made with so2.apworld 0.2.1 (the `apworld-v0.2.1` release; swap the
  `.apworld` back to 0.2.2 after generating). For the older-room behaviour.

Host each on a local server (the Launcher's Host, or `ArchipelagoServer.exe`) and play it with the
game connected (setup's Archipelago page: the room's address `localhost:38281`, the slot name).

1. **The version.** `shattered-stars.exe --version` / `setup.exe --version`: "Shattered Stars
   0.03.00" / "Shattered Stars Setup 0.03.00"; setup's header "Setup · v0.03.00"; Explorer ->
   Properties -> Details on both `.exe`s: version 0.03.00 (file version 0.3.0.0), "DreamGlyph
   Studios", no "POSIX WinThreads".
2. **Setup's render button** (setup's Archipelago page; scroll down to **Trackers**). **This is
   setup.exe's first real run of it:** setup.exe has never run under Wine; the render's thread did,
   inside `ss_game_tests.exe` (docs/handoff/setupthread.md).
   - With no Disc 1 chosen (a fresh scratch `%APPDATA%`): the button is disabled, its tooltip
     "Choose a valid Disc 1 image first.", and nothing is written.
   - With Disc 1 chosen: press it. A progress bar ("150 of 862 images") and the image in progress
     ("Rendering room 92 Armory...", "Rendering the area map of Arlia Village...") move while it
     runs; the button stays disabled; the rest of setup keeps working meanwhile (switch pages,
     scroll, the window redraws, no "Not responding"). It takes about 20 s on the development
     machine; note the time here.
   - At the end: "Done: 862 tracker map images were written to <folder>." with **Open folder**,
     which opens Explorer there. The folder holds `images\maps\` (68 PNGs: 66 areas, 2 world maps)
     and `images\rooms\` (794 PNGs), about 135 MB, no `.part` file, nothing else. config.ini
     unchanged by the button (its time and contents), no memory card touched.
   - Close setup (the window's X) halfway through a second render: setup closes within a moment
     (no hang); the folder holds whole PNGs only (no `.part`); pressing the button again renders
     all 862.
3. **The command line** (PowerShell or cmd):
   - `shattered-stars.exe --render-tracker-maps`: the console lists the images, "rendering 862
     image(s) into <folder>" first and "300 of 862 done" every 100; exit code 0; the same folder as
     the button's; no window opens; no `shattered-stars.log` line, no config.ini change.
   - `shattered-stars.exe --render-tracker-maps "%USERPROFILE%\ss-test\Test Ä 日本\maps"`: 862 PNGs
     there, the console shows the path as written, exit 0.
   - Without a disc anywhere (a scratch `%APPDATA%` with no config.ini, no `discs` folder): exit 1
     with the no-disc message, nothing written.
4. **PopTracker: install and load.** Start PopTracker 0.36.0 and drag `so2_shattered_stars_1.0.0.zip`
   into its window (or copy it into `%USERPROFILE%\PopTracker\packs`). Load -> "Star Ocean: The
   Second Story (Shattered Stars)" -> **Map Tracker**: the item column (Keys, Party, Skills, Moves,
   Room) on the left, the map tabs (Expel, Energy Nede, Fienal, Cave of Trials, the boards) on the
   right; the banner "not connected". **Items Only** loads too. No error box.
5. **PopTracker: connect and autotrack** (the open seed). With the game connected to the room,
   click **AP** in PopTracker and connect to the same room and slot (and password). Then:
   - the banner "Logic: from the room"; the Room tab shows the seed's options;
   - the squares green where the check is in logic, red where items are missing; only the room's
     own locations show;
   - open a chest in the game: its square clears in PopTracker within a moment; an item received
     appears in the item column;
   - walk from one map to another (Arlia's streets into Hearn's general store, then out to the
     world map): PopTracker's tabs follow by themselves (**Auto tab**, on by default); walking on
     the world map doesn't switch tabs; with Auto tab off they stay;
   - the server's console lists the game and PopTracker on the slot at once; nothing breaks for the
     game while PopTracker is connected.
6. **PopTracker: story awareness** (the story seed, with the game connected). The **Chapter** item
   shows the chapter the game reports, and rises with the story. Checks in places the
   story hasn't reached show **blue** (PopTracker's "Inspect": in logic by the items, later in the
   story); the ones it has reached green; red ones need items. Play into the next stage (Salva,
   chapter 5) or load a later save: blue squares turn green live; load an earlier save: they go
   blue again. The **Story** item off: nearly everything green (the plain logic); on: back. With the
   game not connected, clicking Chapter (left +1, right -1) sets a chapter by hand.
   **For the user:** does blue read well for "later in the story"? (Red is a one-line change in
   tools/poptracker/logic.lua's `so2_access`; docs/handoff/storyaware.md "Open".)
7. **PopTracker: the rendered maps.** After item 2 or 3, reload the pack (or restart PopTracker):
   the area maps show the real rooms from the disc instead of the plain stand-ins, each room's name
   in the strip above it; the squares sit on the chests and on the people who hand things out (look
   at Arlia Village: the 200 Fol crate; Salva; a dungeon); the two world maps show the terrain from
   above with the squares on the towns. Nothing changes in the pack's own files (the override
   folder only).
8. **PopTracker: offline play.** Play a check with the game offline (setup's Address empty, or the
   server stopped), then connect again: the check reaches the server, and PopTracker clears it, on
   the reconnect; the map tab and the chapter follow once the game is connected again.
9. **PopTracker: an older room** (optional, the old seed): the banner "Logic unavailable for this
   room: checklist", every square shown as reachable, no blue; auto tab still works with the game
   connected.
10. **Universal Tracker.** Put `tracker.apworld` and so2.apworld 0.2.2 in `custom_worlds` (one
    so2.apworld), restart the Launcher: **Universal Tracker** is listed. Open it and connect to the
    room with the slot name; **no YAML** in its Players folder is needed (none should be there for
    this game). Then:
    - its tracker tab lists the locations in logic and still to check, in story order (Arlia's
      first, the Cave of Trials last, then the skill guilds, learn levels, Item Creation and killer
      moves);
    - `/explain Town of Salva: Heavy Ring chest` (or any location) prints its rule, what you have
      green and what is missing red;
    - with the game connected and checking, the list shrinks as checks are made;
    - **story awareness** (the story seed, the game connected): UT lists only what the story has
      reached (a fresh story game: the first stage's places, Shingo Forest and Arlia); entering the next stage adds
      that stage's checks live; with `enforce_deferred_entrances: "off"` under `universal_tracker`
      in `C:\ProgramData\Archipelago\host.yaml` (restart UT) it lists everything the logic allows;
    - the game, PopTracker and UT all connected to the slot at once: each works.
    - Optional, the old seed: UT tracks a 0.2.1 room with 0.2.2 installed.
11. **The room maps, the door panel and click switching** (maptab, docs/handoff/maptab.md).
    In PopTracker, after loading the pack (item 4): each area's tab has an **Overview** and then
    a tab per room, titled by its map number, in story order; on a rendered room (after item 2 or
    3) a small lettered box sits beside each doorway (A, B, C...). A **Doors** panel beside the
    map lists where each leads, for example "A -> 412 Salva Drift" (a story-only door shows a
    trailing `*`); each door's marker on the map is a **yellow** diamond (the one colour no check
    uses: checks are green, red, blue, grey or mixed; utzip, 2026-10-27), and the colour reads well
    beside the checks' squares; click a line, or a door's own marker/diamond on the map, and the
    room it leads to opens its own tab. With the game connected, the tabs follow the room your party is in
    (**Auto tab**, item 5) down to the room level, not just the area. Universal Tracker's map tab
    (item 10) follows the same room pages and shows a player marker on the two world maps, but has
    **no doors**: a door is not an AP location, entrance or event, so it carries no door panel and
    no clickable markers — doors are PopTracker-only. 1,775 door locations in all, across 794 room
    maps.
12. **A glance at the rest** (unchanged since 0.02.51, which passed): Esc opens "Quit Shattered
    Stars?"; the gold-star icon on both `.exe`s, the title bars and the taskbar; the bundle
    unpacked into "…\Shattered Stars Ä\" starts and saves; a seed's New Game, a check, a save and
    a load; the bench seed's Party Order flip.
13. Optional, with a toolchain: `ss_tests.exe` / `ss_game_tests.exe` (the RC runs them under Wine).
