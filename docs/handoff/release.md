# Release (AP phase 5): continuation log

Owner: the release agent. Scope: `.apworld` packaging, player docs, port bundle check,
`docs/ap/release_checklist.md`, upstream readiness. Preparation only — publishing (GitHub
release, push, PR) is the user's call later. Rules: never contact archipelago.gg or any
public server (127.0.0.1 only); no publishing; no commits (coordinator commits hourly); no
game code/assets in git or dist/; never the user's email anywhere; headless only; never
touch ~/.config/shattered-stars or ~/.config/pcsx-redux; kill by exact PID; scratch under
scratchpad/release/; builds -j8; at most 6 game processes at once.

## RESUME HERE (2026-10-23, starting)

Read: docs/HANDOFF.md, docs/archipelago_plan.md, docs/ap/apworld.md, docs/ap/seed_format.md,
docs/handoff/enemy.md (phase 4 APWorld side, line ~250, is the latest; phase 4 is fully done:
open world, shop slots, 95 world tests, AP general 379, 64-seed batch, e2e 25/25 twice),
docs/handoff/setup.md (packaging: Linux container + Windows/wss done as of this round;
engine/src/game/ap/ap.{h,cpp} changed again at HEAD a687725, after that round, so the
bundles need a rebuild).

### Plan
1. `tools/build_apworld.sh` -> `dist/so2.apworld` + template YAML in `dist/`.
2. Copy `scratchpad/ap` -> `scratchpad/release/` (Archipelago clone; reuse the existing
   `ap-venv`), custom_worlds test (no worlds/so2), generate solo + multiworld + non-default
   seeds, MultiServer on 127.0.0.1, then `tools/ap_e2e_so2.sh` adapted to the packaged world.
3. Player docs in `apworld/so2/docs/` (game info + setup), wired into `SO2Web`.
4. Port bundles: rebuild (ap.cpp changed since setup.md's last verified round); decide
   ship-with-or-beside; README check.
5. `docs/ap/release_checklist.md`.
6. Upstream readiness: `worlds/so2` copy, AP's `test/general` + `worlds/so2/test`; read AP's
   contributing / world api / world maintainer / style docs; gap list.

### Done
- **`tools/build_apworld.sh`** (new). Writes `dist/so2.apworld` and
  `dist/Star Ocean The Second Story.yaml`.
  - **Real defect found and fixed in the script, not in the world:** a hand-rolled zip of
    `apworld/so2/` loads with a logged manifest error ("Invalid or missing manifest file ...
    This apworld will stop working with Archipelago 0.7.0") because `archipelago.json`
    correctly omits the zip-only `version` / `compatible_version` fields (per `docs/apworld
    specification.md` in the AP install: "Do not write these fields yourself" — a folder
    world must not have them). The script now shells out to Archipelago's own "Build
    APWorlds" Launcher component (`Launcher.py "Build APWorlds" -- "Star Ocean: The Second
    Story" --skip_open_folder`) against `scratchpad/ap/Archipelago` (its `worlds/so2` dev
    symlink is what the component needs; the script adds a temporary one if absent), which
    adds the correct manifest (`version: 7, compatible_version: 7`) and applies the global
    `.apignore` (drops `__pycache__`; keeps `test/`). Verified clean load (below).
  - The template YAML uses `Options.generate_yaml_templates` (Archipelago's own generator)
    into a scratch folder, copying out only our game's file.
- **Packaged-world verification**, in a separate copy `scratchpad/release/Archipelago`
  (`cp -a` of `scratchpad/ap/Archipelago`, its dev `worlds/so2` symlink removed;
  `scratchpad/ap/Archipelago` itself untouched and still has the symlink): `dist/so2.apworld`
  in `custom_worlds/`, no `worlds/so2`.
  - Loads clean: `AutoWorldRegister.world_types` has "Star Ocean: The Second Story",
    `world_version` parses as 0.1.0, `worlds.failed_world_loads` empty, no manifest error
    logged (contrast with the hand-rolled zip above).
  - Generated 4 seeds from `scratchpad/release/verify/` player files (seed 777, using the
    repo's `scratchpad/ap/ap-venv`): solo (defaults), a 2-player multiworld (SO2 + ChecksFinder,
    defaults), and two non-default option sets (open_world + character_items + shuffle_shops
    full + traps 30 + death_link; story_route rena + missable_locations include +
    expel_progression false + multipliers). All 4 generated `.apso2` + spoiler with no errors.
  - `MultiServer.py` served the multiworld zip on 127.0.0.1:38391 ("Hosting game at
    127.0.0.1:38391"), loaded our embedded data package; stopped by exact PID (note: an
    earlier manual test left a stray MultiServer.py running because `exec setsid ... &`
    makes the *python* process the new session leader, not the subshell — `kill -- "-$!"`
    missed it; killed it afterward by its own PID. Fixed by always reading back the real
    PID/PGID if reusing that pattern elsewhere, not assumed from `$!`).
  - **`tools/ap_e2e_so2.sh` adapted copy**: `scratchpad/release/ap_e2e_so2_packaged.sh`
    (not committed to tools/, per the task's "otherwise adapt a copy"). Changes from the
    original: `AP=` points at `scratchpad/release` (not `scratchpad/ap`); a separate `VENV=`
    var keeps using `scratchpad/ap/ap-venv` (the existing venv, not duplicated); the
    `ln -sfn .../worlds/so2` line is replaced with a check that `custom_worlds/so2.apworld`
    exists and `worlds/so2` does not; the repo-root `cd` fix (`$(dirname "$0")/..` assumed
    the script lives one level under the repo root, true for `tools/` but not for this copy
    under `scratchpad/release/`, so the root is set explicitly instead).
    **Result: 25/25 PASS** against the packaged world (character_items, enemy_dropsanity,
    traps, open_world + full shops, the goal), same checks as the original script's run
    against the dev symlink. Log: `scratchpad/release/e2e_packaged.log`.
- Engine binary check: `engine/src/game/ap/ap.{h,cpp}` changed at HEAD (a687725, after
  `docs/handoff/setup.md`'s last verified packaging round); `engine/build/shattered-stars`
  was already rebuilt as part of that same checkpoint commit (`cmake --build engine/build`
  confirms "no work to do"), so the e2e run above is against current code.

### In progress
- Two background agents running (docs only; upstream gap analysis only — no code edits):
  player docs (`apworld/so2/docs/*.md`, `world.py` WebWorld wiring check) and an Archipelago
  upstream merge-readiness read (contributing/world api/world maintainer/style docs vs. our
  world's current state), for `docs/ap/release_checklist.md`.
- Next, while those run: deliverable 3 (rebuild `tools/build_windows.sh` /
  `tools/build_linux.sh --container`, since ap.cpp changed since the last packaging round;
  decide ship-with-or-beside for so2.apworld + template YAML; README check).

### Done (continued)
- **Deliverable 3, port bundles rebuilt** (ap.cpp/ap.h changed at HEAD since setup.md's last
  verified round): `tools/build_linux.sh --container` and `MINGW_SSL_SYSROOT=... tools/
  build_windows.sh` both exit 0. `dist/ShatteredStars-linux-x86_64.tar.gz` (8.1M, glibc
  2.36 max via objdump), `dist/ShatteredStars-win64.zip` (6.6M). Smoke on the rebuilt Linux
  package: 86 PASS / 0 FAIL (`scratchpad/release/smoke_linux_package.log`). Host
  `engine/build` was already current (`cmake --build` → "no work to do"). Both READMEs
  already have a `## Archipelago` section (from setup.md's prior round); not re-verified
  under Wine this round (no engine code changed, only a rebuild) — still a good idea before
  actually publishing, noted in the checklist.
  - Decision (ship-with-or-beside): **beside**, not inside, the port bundles. Reasoning in
    the checklist: so2.apworld/template update independently of port releases, and most
    players get the world from wherever the multiworld's host points them (often not this
    project at all), not from the port's own zip/tarball.
- **Deliverable 5, upstream readiness, test runs done:** swapped
  `scratchpad/release/Archipelago` from the custom_worlds test to the upstream placement
  (`custom_worlds/so2.apworld` moved aside to `.bak`; `worlds/so2` symlinked to
  `apworld/so2`). `worlds/so2/test`: 95 passed / 7986 subtests. `test/general`: 379 passed /
  116984 subtests (both `-n 8`, `SKIP_REQUIREMENTS_UPDATE=1`, matches the repo's own
  previously-reported counts). No apworld failures to fix.
  - **Style fixes applied** (from the upstream gap-analysis agent's findings, `docs/
    style.md`'s 120-char line limit): `apworld/so2/regions.py:63-64`,
    `apworld/so2/doors.py:84-85`, `apworld/so2/patches.py:253`, and one more the agent's
    scope didn't cover, `apworld/so2/test/test_doors.py:32` — all wrapped, no behavior
    change (confirmed: `ast.parse` + rerun of `worlds/so2/test`, still 95/7986).
  - Upstream gap analysis (full checklist in the agent's report, folded into
    `docs/ap/release_checklist.md`): tests/docs/manifest/WorldTestBase conventions all MET;
    CODEOWNERS/maintainer NOT MET (expected, user's call); licensing is **N/A/open** — no
    LICENSE file at the repo root or in apworld/so2, worth an explicit decision before a PR
    (Archipelago itself is MIT); no squash/branch/changelog/label requirements found in
    contributing.md.
- **Player docs written** (by a background agent, then I added a DeathLink section per the
  coordinator's mid-task message): both files substantially rewritten; `world.py`'s
  `SO2Web` checked and left alone (`Tutorial` entry and `game_info_languages` default
  already correct).
  - **DeathLink section added** (`en_Star Ocean The Second Story.md`, after "Where does
    Expel stand?"): the three facts as given — a DeathLink sends only on a real Game Over;
    the three story-scripted losses (Shin on the ship to El, Dias at the Lacour Tournament,
    Marsilio's first Fienal fight) never send or apply one, a received one waits for normal
    play; Shin at the Lacour Front Line is a normal survival fight, DeathLinked as usual.
    `docs/handoff/deathlink.md` does not exist yet (checked twice), so the boss-check
    counting-both-ways mechanism (the other agent's in-progress engine change) is flagged
    "to confirm" in the release checklist rather than asserted in the player doc — the doc
    itself doesn't claim anything about how those three locations check on a loss.
  - **Found and fixed a factual error that appeared in the doc by the time I re-read it**:
    after my edit, the file had an extra unsourced clause appended ("This is the same for
    Berle fight at the end of disc 1", malformed punctuation) claiming Berle's fight is
    also a scripted loss. Checked against `apworld/so2/data.py`: Berle's two boss rows
    (encounters 520, 535) are `'boss': 'story'`, `'missable': []` — ordinary story bosses,
    NOT `'boss': 'losable'` like the three genuine ones (204, 517, 530, each `'missable':
    ['losable']`). Removed the clause as factually wrong; flagging here rather than just
    silently deleting it, per the harness's guidance on concurrent edits — if whoever added
    it has independent evidence, raise it, but `data.py` (generated from the field sweep) is
    the authoritative source and doesn't support it.

### Done (final)
- `docs/ap/release_checklist.md` written: version numbers (+ the port-version gap as a
  TODO), every verification command with its expected result, the artifact paths and the
  ship-beside decision, what publishing would involve, and the upstream gap table (style
  fixes MET after this pass; CODEOWNERS/maintainer and licensing left as explicit
  user-decision TODOs, not invented; DeathLink boss-check wording flagged "to confirm").
- **20-seed regression batch after the style-only apworld edits**
  (`scratchpad/enemy/ap5/batch/run_batch.sh 9000 9019`, `scratchpad/release/
  batch_9000_9019.log`): all 20 generate (`exit 0`, 2 `.apso2` each); `check.py` over the
  full accumulated out/ (168 seed files, old + new): **0 errors**, 4893 progression items
  all at allowed SO2 locations. Satisfies the task's "after any apworld change, rerun ...
  and 20+ seed batches" rule even though the 4 edits were pure line-wraps with no logic
  change (already confirmed behaviorally identical via `ast.parse` + unchanged
  `worlds/so2/test` counts before this batch).

## State: ALL FIVE DELIVERABLES DONE, nothing running

Processes checked clean (`pgrep -af "MultiServer.py\|shattered-stars --headless"`: no
output). Build dirs used: `engine/build` (unchanged, already current),
`engine/build-linux-container`, `engine/build-win-release` (both rebuilt, exit 0).
Scratch under `scratchpad/release/`: `Archipelago/` (a separate copy; its `custom_worlds/
so2.apworld` was moved to `.apworld.bak` partway through so the same copy could also
exercise the upstream `worlds/so2` placement — both are documented in the checklist;
nothing here is needed again unless re-verifying), `verify/` (the 4 generation test player
files + outputs), `ap_e2e_so2_packaged.sh` (adapted copy of `tools/ap_e2e_so2.sh`),
`e2e_packaged.log` (25/25 PASS), `build_linux.log` / `build_windows.log` (both exit 0),
`smoke_linux_package.log` (86/0), `upstream_tests/` (so2 95/7986, general 379/116984),
`batch_9000_9019.log`.

Nothing was committed, pushed, or published; no public server was contacted. If resuming
this area later: re-run docs/ap/release_checklist.md section 2's commands fresh before
actually publishing (source moves fast here), and check whether `docs/handoff/deathlink.md`
now exists to close the one "to confirm" line.

### Verify (so far)
```
tools/build_apworld.sh                      # -> dist/so2.apworld, dist/Star Ocean The Second Story.yaml
unzip -p dist/so2.apworld so2/archipelago.json   # version/compatible_version present, no error
SS_DISC1="<disc1.cue>" bash scratchpad/release/ap_e2e_so2_packaged.sh engine/build/shattered-stars
  # -> scratchpad/release/e2e_packaged.log, 25/25 PASS
```

## RESUME HERE (2026-10-23, continued): the three release decisions

The user made the three decisions phase 5 had flagged as open (maintainer, licence, port
version). This pass applies them. Same rules as above (127.0.0.1 only; no publish; no
commits; headless; scratch under `scratchpad/release2/`; kill by exact PID; at most 6 game
processes). Another agent owns `engine/src/game/ap/ap.{h,cpp}`, `engine/tests/test_ap.cpp`,
`docs/ap/apworld.md`, `docs/ap/seed_format.md` (DeathLink) — none touched here.

### Decision 1: maintainer = IronJade

- `apworld/so2/archipelago.json`'s `authors` and `apworld/so2/world.py`'s `Tutorial(...)`
  authors list both changed from the placeholder `["Shattered Stars"]` to `["IronJade"]`.
  Grepped the rest of `apworld/so2` for "Shattered Stars": every other hit names the port
  itself (docstrings, the setup guide), not an author — left alone.
- `docs/ap/release_checklist.md` section 5's maintainer row: **MET**, with the proposed
  upstream `.github/CODEOWNERS` line `/worlds/so2/ @IronJade` and a note that `@name` must
  be the maintainer's actual GitHub account name (not invented/verified here — that's the
  user's to confirm before a PR).
- Rebuilt with `tools/build_apworld.sh`: `unzip -p dist/so2.apworld so2/archipelago.json`
  shows `"authors": ["IronJade"]` alongside the packaging-added `version`/`compatible_version`.
  Reloaded the packaged world in `scratchpad/release/Archipelago` (restored to the
  custom_worlds state: `worlds/so2` symlink removed, `custom_worlds/so2.apworld` replaced
  with the freshly built one, the stray `.bak` from the prior upstream-placement test
  removed) — `AutoWorldRegister.world_types['Star Ocean: The Second Story'].web.tutorials[0]
  .authors == ['IronJade']`, no failed loads. Generated one seed (seed 778, the existing
  `scratchpad/release/verify/solo/Solo.yaml` player file) into
  `scratchpad/release/verify/out/maintainer_check/`: `AP_<seed>.zip` with the spoiler + one
  `.apso2`, exit 0, nothing in the log matching error/traceback. Also re-ran
  `worlds/so2/test` in `scratchpad/ap/Archipelago` (the dev symlink checkout) to confirm the
  author edit didn't regress anything: still 95 passed / 7986 subtests.

### Decision 2: licence = MIT

- `LICENSE` added at the repo root: standard MIT text, "Copyright (c) 2026 IronJade".
- `apworld/so2/LICENSE`: same text, copied in. Checked Archipelago's "Build APWorlds"
  component (`worlds/LauncherComponents.py`'s `_build_apworlds`) before adding it: it zips
  every file under the world folder that `.apignore` doesn't exclude (no validation beyond
  the manifest's `game` field), and `data/GLOBAL.apignore` has no pattern that would catch a
  plain `LICENSE` file. Confirmed after rebuilding: `unzip -l dist/so2.apworld | grep -i
  license` shows `so2/LICENSE` — packs clean, no logged complaint.
- `tools/build_linux.sh` / `tools/build_windows.sh`: both now `cp LICENSE "$out/LICENSE"`
  next to `THIRD-PARTY-LICENSES.txt` (`tools/third_party_licenses.sh`'s output, unchanged).
- `engine/README.md`: no licence section existed before; added two lines right after the
  intro paragraph naming the MIT licence and pointing at `LICENSE` / `THIRD-PARTY-LICENSES.txt`
  (this file is also what ships as each bundle's `README.md`, so one edit covers both the
  repo and the bundles). The Linux bundle's separate `README.txt` (written by
  `tools/build_linux.sh` via `printf`, not copied from `engine/README.md`) got one added
  line too: "Licence: MIT (LICENSE); third-party components: THIRD-PARTY-LICENSES.txt."
- `docs/ap/release_checklist.md` section 5's licensing row: **MET** (was "OPEN, needs a
  decision").

### Decision 3: port version = 0.01.00

- `engine/CMakeLists.txt`: `project(ShatteredStars LANGUAGES C CXX VERSION 0.01.00)`.
  Checked first with a throwaway `project(Test VERSION 0.01.00)` in `/tmp` that CMake keeps
  the literal string in `PROJECT_VERSION` ("0.01.00", not normalized to "0.1.0") — confirmed
  again in the real configure: `CMAKE_PROJECT_VERSION:STATIC=0.01.00` in
  `engine/build-release2/CMakeCache.txt`. Added `target_compile_definitions(ss_core PUBLIC
  SS_VERSION="${PROJECT_VERSION}")` right after `ss_core`'s include dirs, so every target
  that links it (the game, setup, `ss_tests`, `ss_game_tests`, `mdec_probe`) sees the same
  `SS_VERSION` string with no extra wiring.
- `engine/src/main.cpp`: added `--version` (prints `Shattered Stars 0.01.00`, exit 0, never
  touches config — it's handled inside the existing arg-parsing loop, before the
  post-loop `saved.save()` call) and an explicit `--help` (now exits 0 via `usage()` instead
  of falling through to the unknown-flag path, which returned 2). `usage()` itself now also
  prints the version line first. Top-of-file usage comment updated to mention both flags.
- `engine/src/setup/setup_main.cpp`: same pattern — a pre-scan for `--version`/`--help` right
  after `attach_console`, before any SDL/window setup, printing `Shattered Stars Setup
  0.01.00`. `usage()` also prints the version line first.
- `engine/src/setup/setup_ui.cpp`'s `App::header()`: the subtitle under the "Shattered
  Stars" logo (`"Setup"` / `"Setup for Star Ocean: The Second Story"` / the from-game
  variant) now has `"   ·   v0.01.00"` appended, in the same small text as before — the
  natural "About" spot the task asked for, no new UI element added.
- Bundle names: `tools/build_linux.sh` / `tools/build_windows.sh` both read the version out
  of `engine/CMakeLists.txt` with `sed -n 's/^project(.*VERSION \([0-9]\+\.[0-9]\+\.[0-9]\+\)).*/\1/p'`
  (anchored to the `project(` line specifically — the *first* `VERSION` match in the file is
  actually `cmake_minimum_required(VERSION 3.24)`, which a naive "first VERSION match" grep
  would have picked up by mistake; caught this before it shipped). Output/top-folder names
  are now `ShatteredStars-0.01.00-linux-x86_64(.tar.gz)` and
  `ShatteredStars-0.01.00-win64(.zip)`. Every reference to the old fixed names in `tools/`
  and `docs/ap/release_checklist.md` and `engine/README.md` updated to the versioned form
  (`apworld/so2/docs/setup_en.md` checked — it only ever said "the Shattered Stars release"
  generically, no literal filename, nothing to change there). Left `docs/handoff/setup.md`
  and `docs/CHECKLIST.md` alone: those are other agents' dated logs of what already
  happened under the old naming, not living reference docs.
- Archipelago client version: checked `engine/src/ap_net/client.cpp` — `ConnectSlot` is
  called with no explicit `ver` argument, so apclientpp's own
  `APCLIENTPP_VERSION_INITIALIZER` (the AP network protocol version) goes out as the
  Connect packet's `"version"` field. That field **is** the protocol version the task said
  not to touch, not a separate client/game version slot — there's nothing else in `ap_net`
  sending any version. No code change; documented the finding in the checklist instead.
- `apworld/so2/archipelago.json`'s own `world_version` stays `0.1.0` (unchanged) — documented
  why in the checklist (Archipelago parses it as a plain integer tuple; `0.01.00` would read
  back identical to `0.1.0`, so there's no reason to break from the rest of the ecosystem's
  convention for that one field).
- `docs/ap/release_checklist.md` section 1 rewritten top to bottom: both version numbers
  decided, the CMake literal-string check, where `SS_VERSION` surfaces, the `world_version`
  note, and the AP-client-version finding, all in one place.

### Verify (this pass)

```
cmake -S engine -B engine/build-release2 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build-release2 -j8                 # 337/337, 0 errors
SS_DISC1="<disc1.cue>" engine/build-release2/ss_tests        # 78/78 PASS
SS_DISC1="<disc1.cue>" engine/build-release2/ss_game_tests    # 110/110 PASS
tools/smoke_field_events.sh engine/build-release2/shattered-stars   # 86 PASS / 0 FAIL
XDG_CONFIG_HOME=<scratch> engine/build-release2/shattered-stars --version
  # -> "Shattered Stars 0.01.00", exit 0, no config.ini written (checked: only the run log appears)
XDG_CONFIG_HOME=<scratch> engine/build-release2/shattered-stars --help       # version line + usage, exit 0
XDG_CONFIG_HOME=<scratch> engine/build-release2/shattered-stars-setup --version
  # -> "Shattered Stars Setup 0.01.00", exit 0
tools/build_apworld.sh                                   # -> dist/so2.apworld (authors: IronJade)
worlds/so2/test (dev symlink checkout)                    # 95 passed / 7986 subtests, unchanged
one seed generated against the rebuilt .apworld           # exit 0, AP_<seed>.zip with spoiler + .apso2
```
tools/build_linux.sh --container          # -> dist/ShatteredStars-0.01.00-linux-x86_64.tar.gz, 8.1M, exit 0
MINGW_SSL_SYSROOT=... tools/build_windows.sh   # -> dist/ShatteredStars-0.01.00-win64.zip, 6.6M, exit 0
tar tzf / unzip -l on each                 # versioned top folder + LICENSE present, confirmed
dist/.../shattered-stars --version         # "Shattered Stars 0.01.00" on the packaged binary
tools/smoke_field_events.sh dist/.../shattered-stars   # 86 PASS / 0 FAIL (matches the dev build)
rm -rf dist/ShatteredStars-linux-x86_64{,.tar.gz} dist/ShatteredStars-win64{,.zip}  # old unversioned, deleted
```
All of the above ran this pass and passed; results folded into
`docs/ap/release_checklist.md` sections 1-3 and 5.

## 0.01.00 release candidate (2026-10-23, prepared by the RC agent)

Scope: prepare the 0.01.00 release candidate for the user's review only — build, verify,
package, write player-facing release notes, and update the checklist with the exact publish
command. **Nothing was tagged, released, uploaded, pushed, or committed.** Rules followed:
build from a clean worktree (not the dirty main tree other agents are using), headless only,
never touch `~/.config/shattered-stars`/`~/.config/pcsx-redux`/the user's own `~/.wine`, kill
processes only by exact PID, cap command output, no publishing.

### Steps done
1. **Clean-tree build.** `git worktree add scratchpad/rc-0.01.00 e0fb27f` (commit `e0fb27f`,
   `HEAD` at the start of this pass). Ran `tools/build_linux.sh --container` and
   `tools/build_windows.sh` (`MINGW_SSL_SYSROOT` pointed at the existing
   `scratchpad/setup/mingw-ssl/...` sysroot) in the worktree, both in the background, both
   exit 0. `tools/build_apworld.sh` was pointed (`AP_INSTALL=`) at a **fresh copy** of the
   Archipelago install (`scratchpad/rc-ap-build/Archipelago`, made from `scratchpad/ap/
   Archipelago` with its `worlds/so2` symlink removed before building, then deleted again
   afterward) rather than the shared `scratchpad/ap/Archipelago`, whose `worlds/so2` symlink
   points at the *main tree's* `apworld/so2` (which other agents may be editing) — the
   `.apworld` had to come from the worktree's clean, committed copy instead. Output written
   straight to `scratchpad/rc-0.01.00-dist/`.
   - Worktree removed at the end (`git worktree remove scratchpad/rc-0.01.00`); the scratch
     AP-install copy deleted too.
2. **Collected artifacts** in `scratchpad/rc-0.01.00-dist/`: `ShatteredStars-0.01.00-linux-x86_64.tar.gz`
   (8,651,059 bytes), `ShatteredStars-0.01.00-win64.zip` (6,997,812 bytes), `so2.apworld`
   (75,675 bytes), `Star Ocean The Second Story.yaml` (9,674 bytes).
3. **Linux check:** unpacked into `scratchpad/rc-linux-unpack/`. 9-entry archive: versioned top
   folder, `shattered-stars`, `shattered-stars-setup`, `LICENSE`, `README.md`, `README.txt`,
   `THIRD-PARTY-LICENSES.txt`, `discs/` placeholder note — no game files, no debug/scratch
   files. `--version` / `--help` on the packaged binary (scratch `XDG_CONFIG_HOME`) both print
   `Shattered Stars 0.01.00`, exit 0, write only a log (no `config.ini`). `tools/
   smoke_field_events.sh` against the packaged binary, `SS_DISC1` set: **73 PASS / 0 FAIL / 1
   SKIP** (the skip is a dev-only save fixture not shipped in the package — expected). glibc
   floor via `objdump -T`: `2.36`, matching the README.
4. **Windows-under-Wine check**, headless: scratch `WINEPREFIX=scratchpad/rc-wine/wineprefix`
   (new, never the user's `~/.wine`), `WINEDEBUG=-all`, `DISPLAY` unset, `SDL_VIDEO_DRIVER=
   offscreen`, `SDL_AUDIO_DRIVER=dummy`. `wineboot --init` once to set up the prefix, no window
   appeared (no `DISPLAY`, no X connection possible). `shattered-stars.exe --version` /
   `--help`: both print `Shattered Stars 0.01.00`, exit 0 (the text lands on the combined
   stream rather than cleanly on stdout under Wine — a known Wine console quirk, already
   documented elsewhere in this file, not a new finding). A 300-vblank headless boot
   (`--headless --no-memcard --frames 300 --map 18`, `SS_DISC1` converted with `winepath -w`,
   `SS_TRACE=1`) loaded field map 18 (Arlia): its triggers register with the same code words
   the Linux smoke test's Arlia case checks (491, 478, ...), and the run exits 0 after
   "stopped after 300 vblanks" — no crash. **Not done** (out of this pass's scope): the full
   `ss_tests.exe` / `ss_game_tests.exe` suites under Wine — only the version flags and one
   headless boot were asked for. Wine processes stopped afterward with `wineserver -k` under
   the scratch prefix; confirmed none left running (`ps aux | grep -i wine`).
5. **`.apworld` check:** copied the freshly built `so2.apworld` into the existing
   `scratchpad/release/Archipelago/custom_worlds/` (replacing the older copy there; no
   `worlds/so2` present). Loads clean: `AutoWorldRegister.world_types` has the game, no
   `worlds.failed_world_loads`. Manifest and registry both confirm `authors: ["IronJade"]`
   and `world_version` `0.1.0`. Generated one seed from the built template YAML
   (`Generate.py --seed 90100`, into `scratchpad/rc-ap-seed/`): exit 0, `AP_<seed>.zip`
   written, nothing in the log matching error/traceback.
6. **sha256sums:** `scratchpad/rc-0.01.00-dist/SHA256SUMS`, all four files.
7. **Release notes:** [`docs/release_notes/0.01.00.md`](../release_notes/0.01.00.md), written
   in the main repo (not the worktree) — what it is, the disc requirement, highlights, what's
   in each file, install steps with wiki links, Archipelago in brief, known limitations, the
   MIT licence and legal line, and the sha256 sums. Every claim in it traces back to the
   README, the apworld's own player docs, or this pass's own verification.
8. **Checklist updated:** `docs/ap/release_checklist.md` section 7, this run's results plus
   the exact (not run) `gh release create --draft` command, tagging `v0.01.00` at
   `--target main` (whatever commit is at the tip of `main` once these docs are pushed), the
   four artifact files (expected to be rebuilt fresh in the main tree's own `dist/` right
   before that command runs, not reused from `scratchpad/rc-0.01.00-dist/`), and
   `--notes-file docs/release_notes/0.01.00.md`.

### Not done / blocked
- Nothing was blocked. The one deliberately narrower check against the original ask: the
  Windows-under-Wine verification covered `--version`/`--help` and one headless boot (as the
  task scoped it), not the full `ss_tests.exe`/`ss_game_tests.exe` suites — flagged above and
  in the checklist's section 6 "to review" list for before an actual publish.

### Rules this pass followed
Clean-tree build via a disposable worktree; headless only everywhere (no window could appear
under Wine: no `DISPLAY`, offscreen video driver, dummy audio); never touched
`~/.config/shattered-stars`, `~/.config/pcsx-redux`, or the user's own `~/.wine` (a fresh
scratch `WINEPREFIX` was used throughout, and never referenced after this pass);
processes killed only by exact PID or by `wineserver -k` under the scratch prefix; every wait
used a bounded deadline, no bare `pgrep -f` of this task's own command; long builds ran in the
background at `-j8`; no tag, no GitHub release, no upload, no push, and no commit — the
coordinator commits; the files other agents own (`tools/so2_mod.py`, `tools/
mod_reference*.py`, `docs/modding/`, the wiki clone) were not touched; the user's email never
appears anywhere in this pass's output.

### Still open after this pass

- The Windows bundle's own suites under Wine, not re-run this pass (section 2.6/6 of the
  checklist) — only a rebuild, no engine behavior changed on the Windows side beyond what
  the Linux build already re-verified.
- DeathLink boss-check wording in the checklist's upstream table is still "TO CONFIRM",
  pending `docs/handoff/deathlink.md` from the other agent.
- Publishing itself (GitHub release, PR, tags) is still entirely the user's call, not done.

## State: all three decisions applied, verified, nothing running

Processes checked clean. `engine/build-release2` built (337/337, 0 errors); `ss_tests` 78/78,
`ss_game_tests` 110/110, `smoke_field_events.sh` 86/0 all against it; `--version`/`--help`
checked on both programs with a scratch `XDG_CONFIG_HOME` (no config.ini written). Both
release bundles rebuilt with the versioned names and `LICENSE` inside; the Linux one's smoke
re-run 86/0; the old unversioned `dist/` files deleted. `.apworld` rebuilt with the new
author, reloaded clean in `scratchpad/release/Archipelago` (restored to its custom_worlds
state), one seed generated; `worlds/so2/test` re-run unchanged at 95/7986. Nothing committed,
pushed or published; no server outside 127.0.0.1 contacted.
