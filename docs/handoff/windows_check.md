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
(the Windows agent fills this in: date, bundle version, each check pass / fail with details)
