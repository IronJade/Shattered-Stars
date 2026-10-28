# Release (AP phase 5): continuation log

Owner: the release agent. Scope: `.apworld` packaging, player docs, port bundle check,
`docs/release/release_checklist.md`, upstream readiness. Preparation only — publishing (GitHub
release, push, PR) is the user's call later. Rules: never contact archipelago.gg or any
public server (127.0.0.1 only); no publishing; no commits (coordinator commits hourly); no
game code/assets in git or dist/; never the user's email anywhere; headless only; never
touch ~/.config/shattered-stars or ~/.config/pcsx-redux; kill by exact PID; scratch under
scratchpad/release/; builds -j8; at most 6 game processes at once.

## RESUME HERE (2026-10-23, starting)

Read: docs/HANDOFF.md, docs/archipelago_plan.md, docs/apworld/apworld.md, docs/apworld/seed_format.md,
docs/handoff/enemy.md (phase 4 APWorld side, line ~250, is the latest; phase 4 is fully done:
open world, shop slots, 95 world tests, AP general 379, 64-seed batch, e2e 25/25 twice),
docs/handoff/setup.md (packaging: Linux container + Windows/wss done as of this round;
port/src/game/ap/ap.{h,cpp} changed again at HEAD a687725, after that round, so the
bundles need a rebuild).

### Plan
1. `apworld/tools/build_apworld.sh` -> `dist/so2.apworld` + template YAML in `dist/`.
2. Copy `scratchpad/ap` -> `scratchpad/release/` (Archipelago clone; reuse the existing
   `ap-venv`), custom_worlds test (no worlds/so2), generate solo + multiworld + non-default
   seeds, MultiServer on 127.0.0.1, then `apworld/tools/ap_e2e_so2.sh` adapted to the packaged world.
3. Player docs in `apworld/so2/docs/` (game info + setup), wired into `SO2Web`.
4. Port bundles: rebuild (ap.cpp changed since setup.md's last verified round); decide
   ship-with-or-beside; README check.
5. `docs/release/release_checklist.md`.
6. Upstream readiness: `worlds/so2` copy, AP's `test/general` + `worlds/so2/test`; read AP's
   contributing / world api / world maintainer / style docs; gap list.

### Done
- **`apworld/tools/build_apworld.sh`** (new). Writes `dist/so2.apworld` and
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
  - **`apworld/tools/ap_e2e_so2.sh` adapted copy**: `scratchpad/release/ap_e2e_so2_packaged.sh`
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
- Engine binary check: `port/src/game/ap/ap.{h,cpp}` changed at HEAD (a687725, after
  `docs/handoff/setup.md`'s last verified packaging round); `port/build/shattered-stars`
  was already rebuilt as part of that same checkpoint commit (`cmake --build port/build`
  confirms "no work to do"), so the e2e run above is against current code.

### In progress
- Two background agents running (docs only; upstream gap analysis only — no code edits):
  player docs (`apworld/so2/docs/*.md`, `world.py` WebWorld wiring check) and an Archipelago
  upstream merge-readiness read (contributing/world api/world maintainer/style docs vs. our
  world's current state), for `docs/release/release_checklist.md`.
- Next, while those run: deliverable 3 (rebuild `port/tools/build_windows.sh` /
  `port/tools/build_linux.sh --container`, since ap.cpp changed since the last packaging round;
  decide ship-with-or-beside for so2.apworld + template YAML; README check).

### Done (continued)
- **Deliverable 3, port bundles rebuilt** (ap.cpp/ap.h changed at HEAD since setup.md's last
  verified round): `port/tools/build_linux.sh --container` and `MINGW_SSL_SYSROOT=... tools/
  build_windows.sh` both exit 0. `dist/ShatteredStars-linux-x86_64.tar.gz` (8.1M, glibc
  2.36 max via objdump), `dist/ShatteredStars-win64.zip` (6.6M). Smoke on the rebuilt Linux
  package: 86 PASS / 0 FAIL (`scratchpad/release/smoke_linux_package.log`). Host
  `port/build` was already current (`cmake --build` → "no work to do"). Both READMEs
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
    `docs/release/release_checklist.md`): tests/docs/manifest/WorldTestBase conventions all MET;
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
- `docs/release/release_checklist.md` written: version numbers (+ the port-version gap as a
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
output). Build dirs used: `port/build` (unchanged, already current),
`port/build-linux-container`, `port/build-win-release` (both rebuilt, exit 0).
Scratch under `scratchpad/release/`: `Archipelago/` (a separate copy; its `custom_worlds/
so2.apworld` was moved to `.apworld.bak` partway through so the same copy could also
exercise the upstream `worlds/so2` placement — both are documented in the checklist;
nothing here is needed again unless re-verifying), `verify/` (the 4 generation test player
files + outputs), `ap_e2e_so2_packaged.sh` (adapted copy of `apworld/tools/ap_e2e_so2.sh`),
`e2e_packaged.log` (25/25 PASS), `build_linux.log` / `build_windows.log` (both exit 0),
`smoke_linux_package.log` (86/0), `upstream_tests/` (so2 95/7986, general 379/116984),
`batch_9000_9019.log`.

Nothing was committed, pushed, or published; no public server was contacted. If resuming
this area later: re-run docs/release/release_checklist.md section 2's commands fresh before
actually publishing (source moves fast here), and check whether `docs/handoff/deathlink.md`
now exists to close the one "to confirm" line.

### Verify (so far)
```
apworld/tools/build_apworld.sh                      # -> dist/so2.apworld, dist/Star Ocean The Second Story.yaml
unzip -p dist/so2.apworld so2/archipelago.json   # version/compatible_version present, no error
SS_DISC1="<disc1.cue>" bash scratchpad/release/ap_e2e_so2_packaged.sh port/build/shattered-stars
  # -> scratchpad/release/e2e_packaged.log, 25/25 PASS
```

## RESUME HERE (2026-10-23, continued): the three release decisions

The user made the three decisions phase 5 had flagged as open (maintainer, licence, port
version). This pass applies them. Same rules as above (127.0.0.1 only; no publish; no
commits; headless; scratch under `scratchpad/release2/`; kill by exact PID; at most 6 game
processes). Another agent owns `port/src/game/ap/ap.{h,cpp}`, `port/tests/test_ap.cpp`,
`docs/apworld/apworld.md`, `docs/apworld/seed_format.md` (DeathLink) — none touched here.

### Decision 1: maintainer = IronJade

- `apworld/so2/archipelago.json`'s `authors` and `apworld/so2/world.py`'s `Tutorial(...)`
  authors list both changed from the placeholder `["Shattered Stars"]` to `["IronJade"]`.
  Grepped the rest of `apworld/so2` for "Shattered Stars": every other hit names the port
  itself (docstrings, the setup guide), not an author — left alone.
- `docs/release/release_checklist.md` section 5's maintainer row: **MET**, with the proposed
  upstream `.github/CODEOWNERS` line `/worlds/so2/ @IronJade` and a note that `@name` must
  be the maintainer's actual GitHub account name (not invented/verified here — that's the
  user's to confirm before a PR).
- Rebuilt with `apworld/tools/build_apworld.sh`: `unzip -p dist/so2.apworld so2/archipelago.json`
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
- `port/tools/build_linux.sh` / `port/tools/build_windows.sh`: both now `cp LICENSE "$out/LICENSE"`
  next to `THIRD-PARTY-LICENSES.txt` (`port/tools/third_party_licenses.sh`'s output, unchanged).
- `port/README.md`: no licence section existed before; added two lines right after the
  intro paragraph naming the MIT licence and pointing at `LICENSE` / `THIRD-PARTY-LICENSES.txt`
  (this file is also what ships as each bundle's `README.md`, so one edit covers both the
  repo and the bundles). The Linux bundle's separate `README.txt` (written by
  `port/tools/build_linux.sh` via `printf`, not copied from `port/README.md`) got one added
  line too: "Licence: MIT (LICENSE); third-party components: THIRD-PARTY-LICENSES.txt."
- `docs/release/release_checklist.md` section 5's licensing row: **MET** (was "OPEN, needs a
  decision").

### Decision 3: port version = 0.01.00

- `port/CMakeLists.txt`: `project(ShatteredStars LANGUAGES C CXX VERSION 0.01.00)`.
  Checked first with a throwaway `project(Test VERSION 0.01.00)` in `/tmp` that CMake keeps
  the literal string in `PROJECT_VERSION` ("0.01.00", not normalized to "0.1.0") — confirmed
  again in the real configure: `CMAKE_PROJECT_VERSION:STATIC=0.01.00` in
  `port/build-release2/CMakeCache.txt`. Added `target_compile_definitions(ss_core PUBLIC
  SS_VERSION="${PROJECT_VERSION}")` right after `ss_core`'s include dirs, so every target
  that links it (the game, setup, `ss_tests`, `ss_game_tests`, `mdec_probe`) sees the same
  `SS_VERSION` string with no extra wiring.
- `port/src/main.cpp`: added `--version` (prints `Shattered Stars 0.01.00`, exit 0, never
  touches config — it's handled inside the existing arg-parsing loop, before the
  post-loop `saved.save()` call) and an explicit `--help` (now exits 0 via `usage()` instead
  of falling through to the unknown-flag path, which returned 2). `usage()` itself now also
  prints the version line first. Top-of-file usage comment updated to mention both flags.
- `port/src/setup/setup_main.cpp`: same pattern — a pre-scan for `--version`/`--help` right
  after `attach_console`, before any SDL/window setup, printing `Shattered Stars Setup
  0.01.00`. `usage()` also prints the version line first.
- `port/src/setup/setup_ui.cpp`'s `App::header()`: the subtitle under the "Shattered
  Stars" logo (`"Setup"` / `"Setup for Star Ocean: The Second Story"` / the from-game
  variant) now has `"   ·   v0.01.00"` appended, in the same small text as before — the
  natural "About" spot the task asked for, no new UI element added.
- Bundle names: `port/tools/build_linux.sh` / `port/tools/build_windows.sh` both read the version out
  of `port/CMakeLists.txt` with `sed -n 's/^project(.*VERSION \([0-9]\+\.[0-9]\+\.[0-9]\+\)).*/\1/p'`
  (anchored to the `project(` line specifically — the *first* `VERSION` match in the file is
  actually `cmake_minimum_required(VERSION 3.24)`, which a naive "first VERSION match" grep
  would have picked up by mistake; caught this before it shipped). Output/top-folder names
  are now `ShatteredStars-0.01.00-linux-x86_64(.tar.gz)` and
  `ShatteredStars-0.01.00-win64(.zip)`. Every reference to the old fixed names in `tools/`
  and `docs/release/release_checklist.md` and `port/README.md` updated to the versioned form
  (`apworld/so2/docs/setup_en.md` checked — it only ever said "the Shattered Stars release"
  generically, no literal filename, nothing to change there). Left `docs/handoff/setup.md`
  and `docs/CHECKLIST.md` alone: those are other agents' dated logs of what already
  happened under the old naming, not living reference docs.
- Archipelago client version: checked `port/src/ap_net/client.cpp` — `ConnectSlot` is
  called with no explicit `ver` argument, so apclientpp's own
  `APCLIENTPP_VERSION_INITIALIZER` (the AP network protocol version) goes out as the
  Connect packet's `"version"` field. That field **is** the protocol version the task said
  not to touch, not a separate client/game version slot — there's nothing else in `ap_net`
  sending any version. No code change; documented the finding in the checklist instead.
- `apworld/so2/archipelago.json`'s own `world_version` stays `0.1.0` (unchanged) — documented
  why in the checklist (Archipelago parses it as a plain integer tuple; `0.01.00` would read
  back identical to `0.1.0`, so there's no reason to break from the rest of the ecosystem's
  convention for that one field).
- `docs/release/release_checklist.md` section 1 rewritten top to bottom: both version numbers
  decided, the CMake literal-string check, where `SS_VERSION` surfaces, the `world_version`
  note, and the AP-client-version finding, all in one place.

### Verify (this pass)

```
cmake -S port -B port/build-release2 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build port/build-release2 -j8                 # 337/337, 0 errors
SS_DISC1="<disc1.cue>" port/build-release2/ss_tests        # 78/78 PASS
SS_DISC1="<disc1.cue>" port/build-release2/ss_game_tests    # 110/110 PASS
port/tools/smoke_field_events.sh port/build-release2/shattered-stars   # 86 PASS / 0 FAIL
XDG_CONFIG_HOME=<scratch> port/build-release2/shattered-stars --version
  # -> "Shattered Stars 0.01.00", exit 0, no config.ini written (checked: only the run log appears)
XDG_CONFIG_HOME=<scratch> port/build-release2/shattered-stars --help       # version line + usage, exit 0
XDG_CONFIG_HOME=<scratch> port/build-release2/shattered-stars-setup --version
  # -> "Shattered Stars Setup 0.01.00", exit 0
apworld/tools/build_apworld.sh                                   # -> dist/so2.apworld (authors: IronJade)
worlds/so2/test (dev symlink checkout)                    # 95 passed / 7986 subtests, unchanged
one seed generated against the rebuilt .apworld           # exit 0, AP_<seed>.zip with spoiler + .apso2
```
port/tools/build_linux.sh --container          # -> dist/ShatteredStars-0.01.00-linux-x86_64.tar.gz, 8.1M, exit 0
MINGW_SSL_SYSROOT=... port/tools/build_windows.sh   # -> dist/ShatteredStars-0.01.00-win64.zip, 6.6M, exit 0
tar tzf / unzip -l on each                 # versioned top folder + LICENSE present, confirmed
dist/.../shattered-stars --version         # "Shattered Stars 0.01.00" on the packaged binary
port/tools/smoke_field_events.sh dist/.../shattered-stars   # 86 PASS / 0 FAIL (matches the dev build)
rm -rf dist/ShatteredStars-linux-x86_64{,.tar.gz} dist/ShatteredStars-win64{,.zip}  # old unversioned, deleted
```
All of the above ran this pass and passed; results folded into
`docs/release/release_checklist.md` sections 1-3 and 5.

## 0.01.00 release candidate (2026-10-23, prepared by the RC agent)

Scope: prepare the 0.01.00 release candidate for the user's review only — build, verify,
package, write player-facing release notes, and update the checklist with the exact publish
command. **Nothing was tagged, released, uploaded, pushed, or committed.** Rules followed:
build from a clean worktree (not the dirty main tree other agents are using), headless only,
never touch `~/.config/shattered-stars`/`~/.config/pcsx-redux`/the user's own `~/.wine`, kill
processes only by exact PID, cap command output, no publishing.

### Steps done
1. **Clean-tree build.** `git worktree add scratchpad/rc-0.01.00 e0fb27f` (commit `e0fb27f`,
   `HEAD` at the start of this pass). Ran `port/tools/build_linux.sh --container` and
   `port/tools/build_windows.sh` (`MINGW_SSL_SYSROOT` pointed at the existing
   `scratchpad/setup/mingw-ssl/...` sysroot) in the worktree, both in the background, both
   exit 0. `apworld/tools/build_apworld.sh` was pointed (`AP_INSTALL=`) at a **fresh copy** of the
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
7. **Release notes:** [`docs/release/notes/0.01.00.md`](../release/notes/0.01.00.md), written
   in the main repo (not the worktree) — what it is, the disc requirement, highlights, what's
   in each file, install steps with wiki links, Archipelago in brief, known limitations, the
   MIT licence and legal line, and the sha256 sums. Every claim in it traces back to the
   README, the apworld's own player docs, or this pass's own verification.
8. **Checklist updated:** `docs/release/release_checklist.md` section 7, this run's results plus
   the exact (not run) `gh release create --draft` command, tagging `v0.01.00` at
   `--target main` (whatever commit is at the tip of `main` once these docs are pushed), the
   four artifact files (expected to be rebuilt fresh in the main tree's own `dist/` right
   before that command runs, not reused from `scratchpad/rc-0.01.00-dist/`), and
   `--notes-file docs/release/notes/0.01.00.md`.

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

Processes checked clean. `port/build-release2` built (337/337, 0 errors); `ss_tests` 78/78,
`ss_game_tests` 110/110, `smoke_field_events.sh` 86/0 all against it; `--version`/`--help`
checked on both programs with a scratch `XDG_CONFIG_HOME` (no config.ini written). Both
release bundles rebuilt with the versioned names and `LICENSE` inside; the Linux one's smoke
re-run 86/0; the old unversioned `dist/` files deleted. `.apworld` rebuilt with the new
author, reloaded clean in `scratchpad/release/Archipelago` (restored to its custom_worlds
state), one seed generated; `worlds/so2/test` re-run unchanged at 95/7986. Nothing committed,
pushed or published; no server outside 127.0.0.1 contacted.

## 0.02.50

The release (the user's decision, 2026-10-25): **port 0.02.50, shipping so2.apworld 0.2.0.**
Previous: v0.02.00 (2026-10-23, tag on fd687ad, port 0.02.00 + so2.apworld 0.1.1). Owner:
the agent "release". The user reviews everything, then commits, tags and pushes; nothing here
is committed or pushed.

Rules (given): headless only; Wine only as the checklist / this log describe, WINEPREFIX under
scratchpad/; own build dir port/build-release2; SS_DISC1 = the NA disc 1 .cue (other
releases in docs/handoff/regions.md); every game process under the 4 GB cap (tools/memcap.py),
at most 2 at once; a scratch XDG_CONFIG_HOME per game run, never a flag that saves
~/.config/shattered-stars/config.ini; long jobs in the background, every wait with a deadline,
kill by exact PID; scratch in scratchpad/release/, not /tmp; cap output; no commits, no
pushes; on a permission refusal stop and report the exact command; past ~400k tokens update
this log and end the turn. Coordinator (mid-task): don't edit the wiki checkout (a separate
"wiki" agent owns it, log docs/handoff/wiki.md); wiki facts the release changes go in a dated
"For wiki" note there; check the wiki log first so the notes and README agree with it.

The final bundles and RC run wait for "cleanup" (docs/handoff/cleanup.md) and "hubproof2"
(docs/handoff/hubproof.md) to report done.

### Status (2026-10-25)
- Facts gathered (three read-only agents over the apv2 / hub / misc logs, then spot-checked in
  the code: options.py, world.py fill_slot_data, seed.cpp, the v0.02.00 tag's seed.cpp,
  Archipelago's Generate.py, data.py's location counts).
- **Done, uncommitted:**
  - `docs/release/notes/0.02.50.md` (new): what's new, the world 0.2.0 (hub mode, the options
    table, the renames, what else changed), fixes, updating from 0.02.00 (seed / port table,
    runs in progress), files, install, Archipelago how-to, known limitations; SHA-256 pending
    the RC.
  - Release-notes drafts moved out of the logs, each replaced by "(release notes:
    docs/release/notes/0.02.50.md)": shopboth.md (the "Release notes line" block, now followed
    by "## Done (continued)" for its e2e bullet; the "Next steps" pointer), skillsanity.md
    ("### Release notes line"), cavefix.md (the "Release-notes line (for 0.03.00)" bullet),
    setup.md (the "left for the next release" to-do gets the pointer). Task-spec lines that only
    *ask* for a release-notes line (shopboth.md:32-34, cavefix.md:12, ships.md:154-157) are
    the logs' rules, kept. No drafts in the hub*, apv2-wave, charselect, claudesword, cavenames,
    vmfix, menufix, difficulty or apareas logs (apareas' 0.1.1 notes shipped in 0.02.00).
  - Versions: `port/CMakeLists.txt` `project(... VERSION 0.02.50)` + its SS_VERSION comment.
    Readers checked: port/tools/build_linux.sh / build_windows.sh's sed (reads `0.02.50`),
    port/tools/mod_check.py / mod_index.py's regex (`[0-9.]+`), mods/catalog.cpp compare_versions
    (dot-separated integers: 0.02.50 > 0.02.00 > 0.01.00), setup_ui.cpp's header (string
    concatenation), main.cpp / setup_main.cpp `--version`. so2.apworld 0.2.0 in both places it
    lives: `apworld/so2/archipelago.json` `world_version` and `world.py` `APWORLD_VERSION` (the
    seed's `apworld_version`); checklist section 1's table now names both. mods/index.json's
    `verified_port_version` 0.01.00 left alone (it records when a mod was verified).
  - README.md: the version badge, the download names, the Archipelago section (308 chests, the
    new location kinds, the three world modes, the three goals). port/README.md: the
    example package name, the stale "North American release" disc line, the hub's "in
    progress" dropped, one bullet for skillsanity / difficulty / join levels / shop slots.
  - docs/handoff/wiki.md: a dated "For wiki" note with every fact the release changes (the
    coordinator: the "wiki" agent owns the wiki checkout; it had written nothing new yet when
    the notes were drafted, so there was nothing to reconcile).
  - YAML template: a preliminary `apworld/tools/build_apworld.sh scratchpad/release/pre` (fresh
    Archipelago copy scratchpad/release/apbuild/Archipelago, no worlds/so2): exit 0, so2.apworld
    156K (27 files, no __pycache__; manifest `world_version` 0.2.0, `version` / 
    `compatible_version` 7), template 21K. The final one is the RC's.
- **Findings for the user (in the notes, or to decide):**
  - **The old option names have no alias.** 0.1.x YAMLs with `protagonist:` /
    `drops_as_checks:` generate with a warning ("... is not a valid option name ...",
    Archipelago Generate.py) and the defaults (`any` / `off`). The notes tell players to rename
    them; an alias would need a hidden option class (the user's call).
  - **seed_format.md "v2 fields" (line ~727) and apworld.md Changes 0.2.0 say a 0.2.0 seed
    with none of the new features is a version-1 seed "the 0.02.00 port plays as it does
    today". Not quite:** 0.2.0 writes only `story_route` / `enemy_dropsanity`
    (world.py fill_slot_data) and 0.02.00 reads only `protagonist` / `drops_as_checks`
    (v0.02.00 seed.cpp:227, 238), so on 0.02.00 such a seed loses its route and its first-drop
    checks. The notes say 0.2.0 seeds need 0.02.50. seed_format.md:24 also still says "this
    build reads 1 and 2" (it reads 1-3). Not edited here (cleanup owns apworld.md's 0.2.0 line;
    flagged for the coordinator).
- **Waiting:** cleanup (log cleanup.md: "In progress: full rebuild, warning diff, exit proofs,
  suites") and hubproof2 (hubproof.md: items 1-2 done, 3 the full late rerun running, 4 the
  arena gift not run). The final bundles and RC wait for both.
- Preliminary port/build-release2 build (20:20): 339/339, exit 0; `shattered-stars --version`
  prints `Shattered Stars 0.02.50`, `shattered-stars-setup --version` `Shattered Stars Setup
  0.02.50`, exit 0, scratch XDG_CONFIG_HOME, no config.ini written (only the run log). 12
  compiler warnings in the full build (message_draw.cpp x8, pad.cpp, event_sweep.cpp,
  spell_windows.cpp, gpu.cpp): cleanup's warning diff covers them.
- Also fixed (apworld/so2/docs, they ship inside so2.apworld): the game page and setup_en.md
  still said "North American release" only (stale since 0.02.00) — now the four releases; two
  new Troubleshooting entries: the renamed options ("... is not a valid option name ...") and
  "unsupported version 2 (this build reads 1)" (a 0.2.0 seed needs port 0.02.50).
- **The RC script:** `scratchpad/release/rc-0.02.50.sh PHASE...` (snapshot build linux wine
  apworld e2e pytest batch suites winsuites sums verify cleanup, or `all`). The source is a
  snapshot: a worktree at HEAD + `git diff HEAD --binary` + the untracked, not-ignored files
  (the user commits that work after review, so the RC must include it); `verify` checks at the
  end that the main tree's port/ apworld/ tools/ mods/ LICENSE haven't moved. Phases follow
  the checklist: 2.1 build_apworld (fresh Archipelago copy, no worlds/so2), 2.2 load in a clean
  install + 5 seeds (template solo; SO2 + ChecksFinder; open world with every new option;
  hub; story with the rest) + check.py + MultiServer on 127.0.0.1 (stopped by its PID), 2.3
  apworld/tools/ap_e2e_so2.sh and ap_e2e_hub.sh adapted (sed, checked) to the packaged install and
  run with the packaged Linux binary, 2.4 pytest worlds/so2/test + test/general (worlds/so2 ->
  the snapshot), 2.5 a 48-seed batch (the skillsanity players sets, story / open and hub) +
  check.py, 2.6 both bundles + contents + --version + glibc floor + smoke on the packaged
  binary + Wine (version, 300-vblank boot), plus the engine suites (build-release2) and the
  Windows suites under Wine (owed since 0.01.00). Wine: scratch prefix
  scratchpad/release/rc-wine, `env -u HOME XDG_CONFIG_HOME=<scratch>` (config.cpp reads
  XDG_CONFIG_HOME first on every platform), systemd-run MemoryMax=4G scopes.
- **Trial run** (20:25-20:27, RC_DIST=scratchpad/release/pre-dist, the preliminary .apworld,
  phases snapshot apworld pytest batch verify cleanup): every step rc 0. Loads with
  world_version 0.2.0, authors IronJade, no failed loads, no manifest error; the 5 seeds
  generate (versions 1 / 1 / 2 / 3 / 2), check.py 5 files 0 errors; "Hosting game at
  127.0.0.1:38491", the embedded data package loaded; worlds/so2/test 380 passed (29315
  subtests); test/general 379 passed (117748 subtests); batch 3 + 3 seeds exit 0, check.py 0
  errors. `verify` said the tree moved (it did: my apworld/so2/docs edits and cleanup's), as
  it should. The e2e adaptation tested on both scripts (sed output checked, `bash -n`).

### Note from "cleanup" (2026-10-25 20:35): done, for the 0.02.50 notes
All three fixes are in the working tree, not committed; details and proof in
docs/handoff/cleanup.md. Suites on port/build-cleanup: ss_tests 99/0, ss_game_tests 182/0,
smoke 272/0 (270 + 2 new `debug_exit` checks). Files: port/src/runtime/runtime.{h,cpp},
port/src/game/field/menu_shop.cpp, port/tools/smoke_field_events.sh, docs/apworld/apworld.md,
docs/handoff/claudesword.md (one line), docs/handoff/cleanup.md. Final bundles may go ahead
(as far as cleanup is concerned).
- Release-notes lines (the `--debug` exit hang; the apworld.md story-seed correction):
  (release notes: docs/release/notes/0.02.50.md). The docs one is not player-facing; the
  notes' "Updating from 0.02.00" makes no identity claim for story seeds.
- Not for the notes (no user-visible change): the -Wmissing-field-initializers warning in
  menu_shop.cpp (skillsanity shop rows) is gone. Warning diff vs c50b530: Linux 3 / 3, MinGW
  27 / 27, the same pre-existing ones; no new warning.
- Runners: `--debug` runs no longer need stdin from /dev/null.

## For release (from wiki, 2026-10-25 ~20:35)

Facts the wiki pass found that the release docs should have (the wiki follows the code / data
in each case). Your "For wiki" note in wiki.md is folded into the wiki; the notes and the wiki
agree on everything below except where marked.
- **The player guide's hub counts are stale** (apworld/so2/docs/en_Star Ocean The Second
  Story.md lines 217-219): "the chests the game can still reach after the Final Save Point (185,
  or 258 with the Cave of Trials)" and "Eluria Tower's chests are not locations yet". data.py's
  HUB_REACHED (the hub proof) holds 290 chests in both games, 73 in the cave (217 without it),
  Eluria Tower's 23 and Fienal's 9 maze-room chests included; 9 gifts (7 + 2 in the cave);
  apworld.md "Hub mode" says the same. hub.choose() takes them all. The wiki says 217 / 290.
- **starting_character in hub mode:** options.py's StartingCharacter docstring and the player
  guide's options row say the starting character stands in for the hero "on the field and in
  dialogue" in open world *and hub mode*. The port's stand-in (port/src/game/field/leader.cpp
  candidate(), line ~190) applies only when `seed()->open_world`, i.e. world_mode
  "open_world" (seed.cpp:582), matching the user's ruling in apv2.md ("This should only be in
  'Open World' mode"). In a hub seed the starting character joins and leads the formation, but
  the field sprite / name swap doesn't run. The wiki says "In open world you walk around as the
  leader". Either the docstring / guide or the port needs a change (the user's call); not run.
- **README.md, still 0.02.00 numbers:** the bug table (`| 43 | 20 | 8 | 3 | 6 | 2 | 1 | 83 |`)
  is now 44 fixed (W4) / 20 / 8 / 4 kept: gameplay (W5) / 6 / 2 / 1 = **85**; the catalog
  ("5,452 functions across 149 code modules", "4,463 are ported", and the mermaid box) is now
  **5,536 functions across 172 modules** (149 North American + 23 French / German, those 84 rows
  were already in 0.02.00), all confirmed; ported **4,547**, not needed 901, PsyQ 88.
- **The catalog itself:** docs/port/catalog/functions.csv line 3528 (title 0x80042cc8
  new_game_sequence) has an unquoted comma in its summary ("... after the combat mode, the rank
  window's selection ..."), so it parses as 9 fields: its confidence reads as "the rank
  window's ..." and its port as a bad path (added in e5e1ccb). And docs/port/catalog/COVERAGE.md is
  stale (5452 rows; tools/catalog_coverage.py regenerates it once the row is quoted). Not
  touched by me (main repo: this note only).
- **port/README.md** still calls the hub "(seed version 3, `world_mode: hub`; in progress)".
- **Known limitations the notes could add** (all in the wiki's Known issues): with skillsanity
  the Cave of Trials learn chests' scene still says the move was learned (skillsanity.md "Next
  steps"); in open world a dash's afterimages show the hero, and a long scene keeps the hero
  until free play (leader.md "Open items"); the captain's Hoffmann Ruins option at chapter 23
  skips the soldier's first-time lines (ships.md "Open items").
- **cavefix.md's release-notes line** says "(for 0.03.00)"; the release is 0.02.50.
- **docs/images/setup-archipelago.png** predates the on / off switch and the page's "Saves"
  part (the wiki keeps it; a new capture would show both).

### Status 20:45
- **cleanup: DONE** (its note above, 20:35; cleanup.md): menu_shop.cpp warning, the `--debug`
  exit hang (runtime.{h,cpp}), apworld.md's story-seed line, a `debug_exit` smoke case; its
  suites ss_tests 99/0, ss_game_tests 182/0, smoke 272/0. The hang fix is in the notes' Fixes.
- **The wiki agent's "For release" note (above), acted on:** the player guide's hub counts
  (217 / 290 chests, 7 / 9 gifts; Eluria Tower and the Fienal maze rooms are locations now),
  README's bug table (85: 44 / 20 / 8 / 4 / 6 / 2 / 1, Highlights' "85 cases") and catalog
  numbers (5,536 functions / 172 modules / 4,547 ported; checked with csv over functions.csv:
  901 "(not needed", 88 "(PsyQ"), the notes' known limitations (afterimages / long scenes show
  the hero; the captain's chapter-23 Hoffmann trip skips the soldier's lines; skillsanity's
  learn spots still say "learned"). Answered in wiki.md. **Left for the user:**
  starting_character's docstring / guide say the stand-in works in hub mode too, the port does
  it in open world only (leader.cpp; apv2's ruling: open world only); functions.csv line 3528's
  unquoted comma (9 fields; from e5e1ccb) and the stale COVERAGE.md; docs/images/
  setup-archipelago.png predates the switch and the "Saves" line.
- **Waiting: hubproof2** (hubproof.md: item 3, the full late-proof rerun, started 19:54, ~45
  min; then item 4, the arena prize gift, which could add a location to data.py). The final
  snapshot, bundles and RC run start when it reports done.

### Status 20:50
- **Coordinator (mid-task):** the user ruled the leader stand-in must apply in hub mode too
  (as the option text says); agent "hubleader" (log docs/handoff/hubleader.md) fixes
  leader.cpp and leaves a dated note here. **The final bundles and RC now wait for hubproof2
  AND hubleader** (cleanup is done). The wiki agent's "For release" items are mine to handle:
  done (README numbers, guide hub counts, engine README, cavefix's line) plus:
  - **docs/port/catalog/functions.csv line 3528** (title 0x80042cc8 new_game_sequence): the
    summary field quoted (it held an unquoted comma: 9 fields); one-line change, LF kept;
    csv parse: 5536 rows, 0 with a wrong field count. **docs/port/catalog/COVERAGE.md** regenerated
    (`tools/catalog_coverage.py`): 5536 rows, confirmed 5536, ported 4547, not needed 901,
    PsyQ 88, empty 0, bad path 0; `--check` exit 0.
  - **docs/images/setup-archipelago.png**: to regenerate after hubleader, as
    docs/handoff/readme_shots.md did (`port/tools/setup_screenshots.sh` -> `archipelago-seed.png`,
    920x660, Pillow optimize / RGB), with the final setup binary.
- To do after hubleader: the notes' / READMEs' leader stand-in wording ("open world" -> "open
  world and hub mode", as built).

### Note from "hubleader" (2026-10-25 20:55): done, for the 0.02.50 notes
The leader stand-in now applies in hub mode as well as open world (the user's ruling), in the
working tree, not committed; details and proof in docs/handoff/hubleader.md. **The final bundles
may go ahead as far as hubleader is concerned.**
- The change: port/src/game/field/leader.cpp `candidate()` gates on `open_world || hub` (story
  mode, no seed, Archipelago off: unchanged). Nothing else needed: the starting character
  already joined and led in hub mode.
- For the notes: in hub mode you walk the field as the Formation leader (the starting character
  from the start), and the story names them where it would name the hero, the bosses' original
  scenes in their chambers included; the hero comes back on the world maps and for a scene that
  shows the leader as themselves, names them, or needs one of the hero's own gestures (the same
  rule and limits as open world: afterimages, long scenes). Presentation only. So the notes'
  "Your leader walks the field. In open world, ..." (line ~125) becomes "In open world and hub
  mode, ...". The option docstring and the player guide already said so; no APWorld change.
- Proof (build-hubleader): hub New Game with a named member (Ashton) and with the character
  select's pick (Celine): the leader walks Fun City, out to the world map; a save and its load;
  16 chamber runs of both games with Ashton (apworld/tools/ap_hub_chambers.py `--starting-character`):
  all PASS (Alen-Tax names Ashton in 5 lines, Varmillion in 18; Bark shows Ashton's own actor, so
  Claude for that scene); state dumps byte-identical with the stand-in off (Alen-Tax, Bark,
  Varmillion). Suites: ss_tests 99/0, ss_game_tests 182/0, smoke 282/0 (272 + 10 new hub checks).
- Files: port/src/game/field/{leader.cpp, leader.h, script_ext.cpp, actor_field.cpp} (the last
  two comments only), port/tests/test_leader.cpp, apworld/tools/ap_hub_chambers.py,
  port/tools/smoke_field_events.sh, docs/apworld/seed_format.md, port/README.md,
  docs/handoff/hubleader.md; the wiki checkout scratchpad/wiki/Archipelago.md (the leader
  paragraph and the known issue: open world and hub mode; not committed or pushed).

### Status 21:05
- **hubleader: DONE** (its note above, 20:55). The notes' "Your leader walks the field" now says
  open world and hub mode (the starting character from the start; the hub's boss scenes too).
  port/README.md and seed_format.md were updated by hubleader itself.
- **Screenshots** (docs/handoff/readme_shots.md's way, after hubleader): port/build-release2
  rebuilt (7 steps, exit 0), `port/tools/setup_screenshots.sh port/build-release2/shattered-stars-setup
  scratchpad/release/shots/documentation-screenshots/setup-program-pages` (38 PNGs, exit 0; the
  deep scratch path keeps the shown paths free of the disk's mount name), then Pillow
  `convert('RGB').save(optimize=True, compress_level=9)`:
  - docs/images/setup-archipelago.png <- archipelago-seed.png (920x660, 218K): the switch on the
    sidebar row, the Gameplay page, "Setup · v0.02.50", the "Saves" part with Open folder; the
    Password row now falls just below the fold (the page scrolls; the tool has no scroll action).
    README.md's alt text and docs/images/README.md's line updated to match.
  - docs/images/setup-discs.png <- discs-ok.png (198K): also stale (the "North American
    release" text, no Gameplay page, no switch, no version); recaptured the same way.
- **Waiting: hubproof2** (item 3 late_full: Rena's game ~30 min in at 20:56; then item 4).

### Status 21:10: the final RC is running
- **hubproof2: DONE** (hubproof.md "hubproof2 summary", 21:05): e2e hub 15/15; the hub proof
  rerun on the current binary unchanged (claude 329 / rena 328); the full late-proof rerun from
  scratch 278 / 278, identical to docs/apworld/late_proof.json; the arena prize gift reachable (2000
  Fol, no Marsilio win; a gift, so never progression; not made a location). **docs/apworld/
  hub_proof.json and apworld/so2/data.py unchanged.** Its one file: apworld/tools/ap_e2e_hub.sh (Bob's
  YAML needs `ChecksFinder: {}`).
- cleanup, hubproof2, hubleader and wiki all report done (scratchpad/agents.txt); no other
  agent is editing port/ apworld/ tools/.
- Started `bash scratchpad/release/rc-0.02.50.sh all` (21:08; `timeout 21600`; output
  scratchpad/release/rc-run.out; per-step lines in scratchpad/release/rc-0.02.50-dist/summary.txt).
  If this session ends mid-run: read summary.txt, rerun only the failed phases
  (`rc-0.02.50.sh PHASE...`; snapshot first if the worktree is gone), then write checklist
  section 9 from it and fill the notes' SHA-256 block from SHA256SUMS.

### Note from "hubproof2" (2026-10-25, 21:15): docs/apworld/hub_proof.json is final for 0.02.50
Superseding my 21:10 note above ("docs/apworld/hub_proof.json ... unchanged"): the coordinator asked
for a small follow-up after that — the 74 display-name strings in `rows` that had gone stale
against the current area-name tables (commit `9f798b1`) are now refreshed in place. Nothing else
in the file changed (same 329 ids, same `comparison` / `lock_drops` / `unlisted_dungeon_entries`
/ `counts` / `bosses` / `bosses_pending`, reach unchanged); details and the full verification
(every non-name field checked identical id by id, `apworld/tools/ap_world_tables.py -o` still cmp's
identical, `worlds/so2/test/test_hub.py` 74 passed / 3082 subtests) are in docs/handoff/
hubproof.md "hubproof2 (2026-10-25, 21:15)". **docs/apworld/hub_proof.json is now final for 0.02.50**
— no further hub-proof work pending on my end. Not committed (coordinator's call, as usual).

### Status 21:12: RC restarted
- Coordinator (21:09): prerequisites done; go ahead on the current tree; include the hub e2e
  in 2.3 (it is: `rc_e2e_hub.sh`); keep the arena prize gift on the guide's / wiki's "left
  out" lists; hubproof2 is refreshing only the location names in docs/apworld/hub_proof.json (docs
  only: not in the RC's source id, and no build reads the names).
- The guide's hub "left out" paragraph didn't name the arena prize gift (nor the Portal of
  Sealing), so I added both (apworld/so2/docs, which ships inside so2.apworld). The first RC
  run (started 21:06, at its smoke step) was stopped by its exact PIDs so the .apworld would
  carry that edit, cleaned up (`cleanup` phase), its output kept in
  scratchpad/release/rc-aborted-2106/, and the RC restarted at 21:11 on the current tree.

### Status 21:58: the Windows suites found a real Windows bug (fixed), and the Wine crash dialog
**First RC (21:11-21:31), every step rc 0 except one:** Windows `ss_game_tests.exe` under Wine:
182 tests, **2 failed** (launcher_archipelago_seed_memcard, ap_hub_story_view); everything
else passed (results kept in scratchpad/release/rc-0.02.50-dist/, superseded below).

**My fault, reported by the coordinator: a Wine crash dialog reached the user's desktop.**
While bisecting I ran the Windows test program with `DISPLAY` and `WAYLAND_DISPLAY` unset;
with WAYLAND_DISPLAY unset libwayland falls back to `wayland-0`, the user's compositor, so
Wine's winewayland driver connected and winedbg's crash window appeared when
field_text_layout_map7 crashed. Fixed for every later Wine run (and in rc-0.02.50.sh
`wine_env`): `WAYLAND_DISPLAY=no-such-wayland-display`, DISPLAY unset,
`WINEDLLOVERRIDES=winedbg.exe=d`, and in the scratch prefix (scratchpad/release/rc-wine/
wineprefix, never ~/.wine) `HKCU\Software\Wine\WineDbg ShowCrashDialog=0` and
`HKCU\Software\Wine\Drivers Graphics=null`; `wineserver -k` after each run, no Wine process
left (checked with ps).

**The causes** (a debug worktree, scratchpad/release/wincheck/, removed after):
1. **A real Windows bug, since 0.01.00: saving fails when the card's folder name isn't
   ASCII.** `psx::MemoryCard::save()` wrote `path_.string() + ".tmp"` through a narrow
   ofstream. On Windows (MinGW libstdc++) `path::string()` is UTF-8, but a narrow file name is
   read in the ANSI code page, so for "...\S1_Cléo\memcard1.mcd" the temporary file can't be
   opened, the rename fails (the error ignored), and the card on disk never changes; the save
   is only in memory. Proven with a 20-line MinGW probe under Wine (scratchpad/release/
   wincheck/t.cpp: the fs::path overload writes, the `.string()` one doesn't). Players hit:
   any Windows user whose profile path has a non-ASCII letter (%APPDATA%\ShatteredStars holds
   memcard1.mcd), any custom card path with one, and a seed card whose slot name has one. The
   failing test is exactly that (slot "Cléo": the adopted card kept 4 files instead of 1, its
   erase never written). **Fix** (port/src/psx/memory_card.cpp, 3 lines): the temporary
   name is built as a `std::filesystem::path` (`tmp = path_; tmp += ".tmp"`), so the wide
   open is used. Linux: byte-identical behaviour. In the notes' Fixes.
2. **The crash (field_text_layout_map7) is a test-order effect, not reachable in the game.**
   `program_data(0xc1c, 0x800734c8)` returned null in field_create_state_block. The program
   image cache (port/src/game/common/program_image.cpp `image_of`) cached an EMPTY image when
   asked before any disc was open, and returned null forever after. In the Windows build the
   cases run in another order (static registration order: ap_hub_story_view 25th on Linux,
   174th on Windows): field_actor_walks_to_target_and_waits' `field_tables()` asks
   `field_data()` before booting, which poisoned the cache; the next field test then booted
   nothing (the system existed) and crashed. The game always opens the disc before reading a
   program, so players never hit it. **Fix** (cheap, safe): with no archive open, `image_of`
   returns an empty image without caching it, so the image is read once a disc is open.
3. **ap_hub_story_view: a test-isolation effect.** In the Windows order it runs after
   field_fade_in_matches_original, which leaves a fade queued in the global field fade; the
   hub's free-play rule (scene_busy: "a fade") then never let the story view end. **Fix**
   (port/tests/test_script_vm.cpp, test only): the test empties the fade queue first.

Verification after the three fixes: in the debug worktree, Windows `ss_game_tests.exe` under
Wine **182 tests, 0 failed** (and the 131-173 subset that crashed: 44 / 0); Linux
port/build-release2 ss_tests 99/0, ss_game_tests 182/0. The RC runs again from scratch on
the fixed tree.

### Status 22:30: RC 0.02.50 GREEN, everything ready for the user's review
**The final RC** (`rc-0.02.50.sh all`, 21:59-22:20, scratchpad/release/rc-0.02.50-dist/,
written up as docs/release/release_checklist.md section 9): every step rc 0. Snapshot of HEAD 9f798b1
+ the working tree (36 files changed, 4 untracked), tree unchanged at the end. so2.apworld 0.2.0
(158,970 B) + template (21,341 B); clean-install load OK; 5 seeds (v1 / v1 / v2 / v3 / v2) + check.py
0 errors; MultiServer on 127.0.0.1; e2e on the packaged binary and world: so2 37/37, hub 15/15;
worlds/so2/test 380 passed (29310 subtests), test/general 379 passed (117754); batch 48 seeds
(24 + 24) exit 0, check.py 0 errors on 96 files; Linux tarball 9,153,298 B (9 entries, glibc 2.36,
--version 0.02.50, no config.ini), smoke on it 282/0/0; Windows zip 7,483,942 B, --version
0.02.50, 300-vblank Arlia boot OK; Linux ss_tests 99/0, ss_game_tests 182/0; Windows under Wine
ss_tests.exe 99/0, ss_game_tests.exe 182/0. SHA-256 in SHA256SUMS, the notes and section 9.
The RC's sources' content id (commit-independent; section 9 has the one-liner):
**c3a0177296fe677d** (618 files of port/ apworld/ tools/ mods/ LICENSE).

After the RC (docs only, not in that id): docs/apworld/apworld.md (0.2.0 intro) and
docs/apworld/seed_format.md (line 24's "reads 1 and 2" -> 1 to 3; "v2 fields" intro) no longer
claim a version-1 0.2.0 seed plays on port 0.02.00; the notes' intro names the Windows save fix.

**Files I changed (all uncommitted):** docs/release/notes/0.02.50.md (new);
docs/release/release_checklist.md (section 1 table, section 3 names, section 6, section 9);
docs/handoff/release.md (this log); docs/handoff/{shopboth,skillsanity,cavefix,setup}.md
(release-notes drafts -> pointers); docs/handoff/wiki.md ("For wiki" notes); docs/apworld/apworld.md,
docs/apworld/seed_format.md (the compatibility lines); port/CMakeLists.txt (0.02.50); README.md;
port/README.md (also edited by hubleader); apworld/so2/docs/en_Star Ocean The Second Story.md,
apworld/so2/docs/setup_en.md; docs/port/catalog/functions.csv (line 3528), docs/port/catalog/COVERAGE.md;
docs/images/setup-archipelago.png, setup-discs.png, docs/images/README.md;
port/src/psx/memory_card.cpp, port/src/game/common/program_image.cpp,
port/tests/test_script_vm.cpp (the Windows fixes). Scratch (git-ignored): scratchpad/release/
(rc-0.02.50.sh, rc-0.02.50-dist/, rc-first-2111/, rc-aborted-2106/, pre/, pre-dist/, shots/,
wincheck/ (logs and the MinGW probe), rc-wine/ (the prefix), rc-e2e-so2/, rc-e2e-hub/,
apbuild/).

**Other agents' uncommitted work in the same tree (the RC includes it):** cleanup
(runtime.{h,cpp}, menu_shop.cpp, smoke_field_events.sh, apworld.md, claudesword.md, cleanup.md),
hubleader (leader.{h,cpp}, script_ext.cpp, actor_field.cpp, test_leader.cpp,
ap_hub_chambers.py, smoke_field_events.sh, seed_format.md, port/README.md, hubleader.md),
hubproof2 (ap_e2e_hub.sh, hubproof.md, docs/apworld/hub_proof.json); docs/plans/next.md (untracked,
not from this agent).

**For the user before the tag:** review the three code fixes (memory_card.cpp,
program_image.cpp, test_script_vm.cpp); the old YAML names (`protagonist`,
`drops_as_checks`) have no alias (the notes say to rename; an alias would be a hidden option);
commit (the content id above shows whether the commit matches the RC); then tag / push / the
draft GitHub release (section 9's command) / the wiki push (scratchpad/wiki, the wiki agent's
pages). HANDOFF.md's state section and scratchpad/agents.txt are the coordinator's.

Nothing running: no game, Wine, server or build process left (checked); the worktrees removed.

## Coordinator (2026-10-25, after the RC)
- The user reviewed and pushed: c9a8200 on origin/main (tree hash c3a0177296fe677d = the RC's).
- The user: no backwards compatibility for 0.1.x option names (`protagonist`, `drops_as_checks`):
  the warning + the release notes' rename advice stand.
- Windows validation sent to the user's Windows machine (Remote Control session "Windows
  Shattered-Stars"): docs/handoff/windows_check.md, files in C:\Users\hitsu\Documents\Star Ocean 2\,
  Archipelago freshly installed at C:\ProgramData\Archipelago. After its checks come back good: the
  gh release (section 9's draft command) and the wiki push (scratchpad/wiki), both the user's go.
- The user (2026-10-25): the copyright holder is **DreamGlyph Studios** (the user's company):
  both LICENSE files now read "Copyright (c) 2026 DreamGlyph Studios" (MIT text unchanged), and the
  Windows version resource uses CompanyName "DreamGlyph Studios" / LegalCopyright "Copyright (c) 2026
  DreamGlyph Studios" (agent appicon). The GitHub account and links stay IronJade; archipelago.json's
  "authors" stays ["IronJade"] (display metadata; upstream maintainers are listed by GitHub handle).

## RC2 (2026-10-26, agent "rc2"): winfix + appicon + escquit, rerun and checked

The Windows machine's recheck (`docs/handoff/windows_check.md`) found six issues in the pushed
`c9a8200`; the user ruled fix-all-before-tagging. Three agents fixed them, uncommitted on top of
`c9a8200`: **winfix** (non-ASCII CLI args/env, the log routing, the client version, Auto-detect's
message, the `"pwnd->destroy"` print, the YAML's two names), **appicon** (the icon, Windows
version resource and manifest, app ids, the Linux desktop entry, window titles, the stray
libwinpthread VERSIONINFO), **escquit** (the Esc quit confirmation). The coordinator also set
both LICENSE files to "Copyright (c) 2026 DreamGlyph Studios" and added a "Written with AI"
passage to README.md and the wiki. This pass: `apworld/so2/docs/setup_en.md` now names both the
built template name and the dotted name GitHub gives the downloaded asset (so2.apworld ships
that doc, so it had to land before the rebuild below); folded winfix's / appicon's / escquit's
Windows recheck items into one "Recheck for 0.02.50 RC2" list in `docs/handoff/windows_check.md`.

Ran `scratchpad/release/rc-0.02.50.sh all` (00:31-00:53; background, no stray `&`/`disown` —
an earlier attempt that used those was caught and killed cleanly by exact PID before it got past
the Linux build, with no damage: the main tree was never touched, only a disposable worktree and
a podman build that had already finished on its own). **Every phase exited 0, no FAIL in any
underlying log** (checked `ss_tests.log`, `ss_game_tests.log`, both Wine logs, both e2e logs,
both pytest logs and the smoke log individually, not just the summary's one-line-per-step view):
build (apworld 159,070 B; Linux tarball 9,407,748 B; Windows zip 7,800,956 B), Linux checks
(39-entry archive, `--version` 0.02.50, glibc 2.36, smoke 282/0/0), Wine (`--version` 0.02.50,
300-vblank Arlia boot), apworld load + 5 seeds + check.py (0 errors) + a 127.0.0.1 MultiServer,
e2e (so2 37/37, hub 15/15), pytest (`worlds/so2/test` 380 passed/29323 subtests, `test/general`
379 passed/117753 subtests), the 48+48-seed batch (0 errors), the engine suites (Linux
`ss_tests` 103/0, `ss_game_tests` 193/0; Windows under Wine `ss_tests.exe` 103/0,
`ss_game_tests.exe` 194/0 — one more than Linux, the Windows-only `launcher_log_without_a_console`
case, as documented by winfix). SHA-256 and the tree-hash one-liner both changed from the first
RC (every artifact carries the three agents' work now); new values in
`docs/release/release_checklist.md` section 9 and `docs/release/notes/0.02.50.md`.

Bundle spot-checks beyond the script: re-extracted both archives and confirmed by hand — both
Windows `.exe`s carry one `RT_GROUP_ICON` (7 sizes), one `RT_VERSION` (strings: "Shattered
Stars" / "Shattered Stars Setup", `0.02.50`, "DreamGlyph Studios", zero occurrences of `"POSIX
WinThreads"`) and one `RT_MANIFEST` (`asInvoker`, `PerMonitorV2`, `supportedOS`, the
`io.github.IronJade.*` app ids), via REA's `inspect_pe_resources` plus a UTF-16LE `strings`
pass; the Linux tarball has `desktop/` and `install-desktop-entry.sh`
(`desktop-file-validate` clean); both bundles' `LICENSE` reads "Copyright (c) 2026 DreamGlyph
Studios", and both carry `THIRD-PARTY-LICENSES.txt`.

Nothing tagged, committed, pushed or published; no server outside 127.0.0.1 contacted. Scratch:
`scratchpad/release/rc2-inspect/` (the re-extracted bundles for the spot-checks above) can be
deleted once the user is done reviewing; everything else needed is under
`scratchpad/release/rc-0.02.50-dist/`. All files changed are listed in this pass's report to
the coordinator; nothing was committed.
- The user (2026-10-26): "When the release candidate is finished here on the linux box, lets push to
  git, and have the windows machine run testing through it's paces with the minor fixes and updates
  and then push the tag and full release of 0.02.50 and world 0.2.0 out to GH." So, in order, the
  coordinator: (1) after rc2 passes, commits the whole working tree (one commit; tree hash = rc2's)
  and pushes origin main; (2) the user copies rc2's Windows zip to the Windows machine, which runs
  the recheck (windows_check.md); (3) if it passes: tag v0.02.50, push the tag, `gh release create`
  with docs/release/notes/0.02.50.md and the rc2 artifacts, then commit + push the wiki
  (scratchpad/wiki). Any failure: stop and report to the user. Never push dgs or refs/backup/*.
- 2026-10-26: **released.** Windows RC2 recheck all PASS (windows_check.md). Tag v0.02.50 (lightweight,
  on 877b771, as v0.01.00 / v0.02.00) pushed; `gh release create v0.02.50 --verify-tag --latest`
  with the five rc2 artifacts (the four of 0.02.00 + SHA256SUMS), title "Shattered Stars 0.02.50",
  notes docs/release/notes/0.02.50.md; published 06:23:51 UTC, latest; downloaded assets verify
  against SHA256SUMS. Wiki pushed: a72a063.

## 0.02.51 (agent "release251", 2026-10-27)

The user (2026-10-27): "Let's push the current release of both the port and the apworld. Is it
possible to have separate releases for the Port itself, and the APWorld on the same GitHub repo?"
Plan (the coordinator's; tag names still to be confirmed by the user): **two GitHub releases** on
the same repo: the port, tag `v0.02.51`, "Shattered Stars 0.02.51", the Linux and Windows bundles +
SHA256SUMS, marked Latest; the world, tag `apworld-v0.2.1`, "so2.apworld 0.2.1", so2.apworld + the
dotted YAML + its own SHA256SUMS, not Latest. The notes link each other.

Rules (given): no push, tag or release; never the `dgs` remote or refs/backup/*; builds -j4, at
most 1 heavy job at a time, game processes under 4 GB; headless; scratch XDG_CONFIG_HOME; kill by
exact PID; scratch in scratchpad/release/; commits on main, docs first, staged by path, ending with
the Co-Authored-By line; the wiki edits left uncommitted in scratchpad/wiki; on a denied call:
note it here and stop.

Steps: 1 version bump; 2 the two notes; 3 README + wiki (+ apworld/so2/docs download links);
4 commit; 5 RC (scratchpad/release/rc-0.02.51.sh) + sums + commit; 6 Windows recheck list; 7 report.

### Status
- Started. HEAD 3bce4a2 (main, 64 ahead of origin/main 5d43f47); wiki checkout clean at a72a063 =
  origin/master. dist/apworld-0.2.1 holds benchworld's package (83af433f... / 93c41087..., section
  10's sums).
- **Version bump (0.02.51):** port/CMakeLists.txt (`project(... VERSION 0.02.51)` and the two
  comments naming it: SS_VERSION, the .rc's 0,2,51,0); README.md (badge, download names);
  port/README.md (the example package name); docs/release/release_checklist.md section 1's table.
  Everything else reads PROJECT_VERSION: SS_VERSION (--version, setup's header "Setup · v...",
  the mods catalogue's min_port_version), the Windows version resource and manifest
  (port/res/windows/*.in), the bundle names (tools/build_*.sh's sed). The Linux desktop entries carry no
  version. The world stays 0.2.1 (archipelago.json, world.py).
- **port/tools/build_linux.sh / build_windows.sh:** `JOBS` (default 8) for the compile's `-j` (this RC
  builds with JOBS=4, the machine's low-load rule).
- **Notes:** docs/release/notes/0.02.51.md finished (intro, discs, what's new, fixes, updating with
  the seed table, packages, installing, Archipelago from the world's own release, known
  limitations, legal; sums after the RC). Corrected from the draft: the bench applies to open
  world with `character_items` (the draft said `party_rules: open`, a seed field, not an option);
  0.02.50 refuses a version-3 sidecar with "The data could not be loaded" (its log: "unsupported
  sidecar version"), it doesn't say "needs a newer version". docs/release/notes/0.2.1.md: draft
  banner gone, links to the port release, SHA256SUMS in the file table, known limitations point at
  0.02.51's notes; its sums = dist/apworld-0.2.1 (83af433f... / 93c41087..., section 10).
- **Tag names in the docs** (to change if the user picks others: `grep -rn 'v0\.02\.51\|apworld-v0\.2\.1'`):
  README.md, docs/release/notes/0.02.51.md, docs/release/notes/0.2.1.md, the wiki pages.
- apworld/so2/docs/*.md have no download link (setup_en.md says "this project's GitHub release
  page" generically): no rebuild; the world package stays benchworld's (apworld/so2 unchanged
  since 64a7d20).
- **Commits:** 2a35882 (docs: notes, README, port/README, checklist section 1, this log), 12a3a6c
  (port: the version, the build scripts' JOBS).
- **RC script:** scratchpad/release/rc-0.02.51.sh (from rc-0.02.50.sh): the committed tree in a
  worktree; JOBS=4; the world NOT rebuilt: dist/apworld-0.2.1's package, checked file by file
  against the snapshot's apworld/so2 (31 files, archipelago.json + the packaging's version 7);
  seeds: the five of 0.02.50 + nobench + hell (bench v4 in open / hub, hell_seed true), the
  checker apworld/tools/ap_batch_check.py, three of them byte-compared with the dev world's; no-game-data
  greps on both archives; desktop-file-validate; upload sets DIST/port/ and DIST/apworld/, one
  SHA256SUMS each; pytest / batch optional (not in "all"). Trial (snapshot + package check +
  apworld phase) 12:40: all rc 0, check 7 files 0 errors, dev-identical, server on 127.0.0.1.
- **RC started 12:40** (`rc-0.02.51.sh all`, timeout 6 h; scratchpad/release/rc251-run.out;
  per-step lines scratchpad/release/rc-0.02.51-dist/summary.txt). If this session ends mid-run:
  read summary.txt, rerun the failed phases (snapshot first if the worktree is gone).
- Meanwhile: the wiki (scratchpad/wiki, uncommitted).
- **Wiki (scratchpad/wiki, uncommitted, for the coordinator to commit and push with the releases):**
  - Archipelago.md: 0.2.1 / 0.02.51 header and IMPORTANT box (bench seeds need 0.02.51; "unsupported
    version 4"); "What you need" (the world's own release, apworld-v0.2.1); where progression goes
    (guild checks / learn levels may hold it); the open-world / hub party with the bench; a new "The
    bench" subsection (Party Order / Tactics flip, managed not used, loans, saves); the join level
    (the bench doesn't count); the starting character in the first place, the hero second, the
    order kept; open world's starting passes (North City + Noel's House unless teleporter and
    expel_progression are both on); killer moves and skillsanity progression rules; a new "Hell
    seeds" section; hub progression; options table (bench, expel_teleporter, expel_progression,
    encounter_rate); installing (the world's release); the hell warning on setup's page; the
    sidecar keeps items waiting / bench / Dias; the full-inventory banner; known issues (the
    warning is English); troubleshooting ("unsupported version 4", the North City Pass fill
    error); a new "What changed in 0.2.1" (the world, your seeds, and Shattered Stars 0.02.51).
  - Home.md (version 0.02.51 / world 0.2.1 released on its own, the download line, "a party of up
    to twelve"), FAQ.md (downloads: the two releases; which world version), Contributing.md and
    Installing-and-Managing-Mods.md (0.02.51 examples), Installing-and-Setup.md (the hell seed's
    warning on the Archipelago page), Bugs-Fixed-and-Quirks-Kept.md ("And in 0.02.51": the five
    Archipelago fixes, no "Since" column: their first versions aren't recorded).
- **Windows recheck list:** docs/handoff/windows_check.md "Recheck for 0.02.51" (written; committed
  after the RC, since a commit during the run would move HEAD under the verify phase).
- **RC 0.02.51 GREEN** (12:40-13:02, `rc-0.02.51.sh all`, exit 0, no step rc != 0, no FAIL in any
  log; written up as docs/release/release_checklist.md section 11): snapshot 12a3a6c (no uncommitted
  source), tree unchanged at the end; Linux tarball 9,484,490 B (39 entries, no game data,
  --version 0.02.51, glibc 2.36, desktop entries valid), smoke 373/0/0; Windows zip 7,875,246 B (9
  entries, no game data, version resource 0.02.51 / DreamGlyph Studios, no WinThreads), Wine
  --version + Arlia boot; the world's package = the snapshot's apworld/so2 file for file, loads as
  0.2.1, seven seeds (bench v4 in open / hub, nobench v2, hell_seed) 0 checker errors and
  dev-identical, MultiServer on 127.0.0.1; e2e so2 37/37, hub 15/15; Linux ss_tests 103/0,
  ss_game_tests 207/0; Windows (Wine) 103/0, 208/0. Content id 7a190be7a51cceae (646 files).
  SHA-256: port 6021fd98... (tar.gz) / d75c22c2... (zip); world 83af433f... / 93c41087...
  (unchanged). Upload sets: scratchpad/release/rc-0.02.51-dist/port/ and .../apworld/, each with
  its SHA256SUMS. Sums filled into docs/release/notes/0.02.51.md (0.2.1.md's already matched).
- Nothing running (no Wine, server or game process; the worktree and scratch installs removed).
- **Left for the coordinator:** the user's tag-name confirmation; the Windows recheck
  (windows_check.md "Recheck for 0.02.51"); then section 11's commands (push main, the two tags,
  the two gh releases, the wiki commit + push). Optional: docs/images/setup-archipelago.png still
  shows "Setup · v0.02.50" (port/tools/setup_screenshots.sh, as for 0.02.50).
- 2026-10-27: **released, as two GitHub releases.** origin main pushed (5d43f47..2c38965). Both
  releases first created as drafts from scratchpad/release/rc-0.02.51-dist/{port,apworld}; the
  Windows session ("Windows Shattered-Stars") downloaded the drafts with gh, matched every file to
  SHA256SUMS and ran windows_check.md "Recheck for 0.02.51": items 1-7 PASS, item 8 (building from
  source) not run; its notes stay in its own checkout. Published: `apworld-v0.2.1` ("so2.apworld
  0.2.1", not Latest) then `v0.02.51` ("Shattered Stars 0.02.51", Latest), both tags on 2c38965;
  the public downloads verify against their SHA256SUMS. Wiki pushed: 6a1624f.
  Cosmetic, for the next port release: the bench join banner reads "joined the party  bench"
  (the game's font has no parentheses: ap.cpp ~1766 "(bench)"); Party Order's hero-to-bench
  refusal has only the refusal sound (no text); docs/images/setup-archipelago.png still shows
  v0.02.50.

## 0.03.00 (agent "rc030", 2026-10-27)

B7b: three releases (port `v0.03.00`, so2.apworld 0.2.2 `apworld-v0.2.2`, PopTracker pack 1.0.0
`poptracker-v1.0.0`), per docs/release/release_checklist.md section 12 and docs/handoff/phaseb.md.
Scope: fill the 21 `TODO (maptab)` placeholders from docs/handoff/maptab.md, the small
tracker_job_test fix, the version bumps (12.1), the RC builds (12.2), tests (12.3), sums (12.4),
and get the three notes ready. **Never push, tag, create a release/draft, or touch the wiki's
git state.** Rules: headless only, scratch under scratchpad/release/ and scratchpad/rc030/,
builds -j8, at most 3 game processes, commits on main docs-first staged by path (never
`git add -A`), message ends with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.

### Status: held before builds (coordinator), 2026-10-27

The coordinator asked (mid-task) to finish tasks 1-3 and commit, then **stop before task 4
(builds)**: the user is deciding two small changes that would touch the port and the pack (a
Universal Tracker zip of the port's own renders; the door diamonds' colour), and building the RC
now would have to be redone once those land. Tasks 1-3 below are done and committed. **Not
started: task 4 (builds), task 5 (tests), task 6 (sums + this section's RC write-up), task 7
(sums into the release notes).** Waiting for the coordinator to lift the hold (new assignment or
a message to continue).

### Done

**Task 1: the 21 `TODO (maptab)` placeholders**, from docs/handoff/maptab.md's log and its "For
the release notes" / "Facts for releasedocs" sections, plus its "Open, for the user /
coordinator" notes (UT's map tab has no doors; it does show a player marker on the two world
maps). Filled in:
- `docs/release/release_checklist.md` section 12: the intro paragraph (maptab is done now, not
  "still to come"); the world package's size/files (281,228 bytes at maptab's build, +`ut_map.py`
  and `apworld/so2/tracker/`); the pack's size (1,201 files, 5,559,587 bytes, was ~3 MB);
  `worlds/so2/test`'s count (882/3 skipped, unchanged by maptab); the identity check's new
  host.yaml key (`so2_options: ut_pack_path`, no new slot-data key); the pack checker's count
  ("Validated 11 files", was 10); the pack's own tests (41 OK, was 24: +`TestRooms`,
  `TestDoorsLua`, `TestUT`); the door-click row (44/44, `trackers/tools/ap_poptracker_smoke.py`); the map
  render test's row (still 10/10, layout now 794 rooms); and the "map tabs" row
  (`--check` up to date at 794 rooms / 862 images, UT's own map loader 15/15).
  **Also fixed, not a TODO marker but stale from the same area audit**: every "861 images" /
  "793 rooms" left over from before maptab's area audit (which added one room, Port Hilton's
  pier, bringing 861 -> 862 images), in this file's "the render, end to end" / "the four
  releases' discs" / "setup's button" / ASan / Windows-under-Wine rows.
- `docs/handoff/windows_check.md` item 11 (the room maps, the door panel, click switching: what a
  Windows tester does and sees in PopTracker and in UT); same 861->862 / 793->794 fix in items 2-3.
- `docs/release/notes/0.03.00.md`: the DRAFT banner (maptab's part no longer pending), the "What's
  new" bullet (rooms/doors paragraph folded into the maps bullet, from maptab's release-notes
  paragraph), the removed stand-alone TODO bullet, 861->862.
- `docs/release/notes/0.2.2.md`: the DRAFT banner, the UT map tab bullet (hybrid, the file
  dialog, the world-map marker, no doors), the counts paragraph (44 PopTracker checks, UT map
  loader 15/15).
- `docs/release/notes/poptracker-1.0.0.md`: the DRAFT banner, the "What it shows" rooms/doors
  bullet, the "real maps" paragraph (now names rooms too, 861->862), the closing counts
  paragraph (794 room maps, 1,775 doors, 1,201 files, ~5.6 MB; 41 pack tests; 44 PopTracker
  checks).
- `scratchpad/wiki/Trackers.md` (uncommitted, its own repo): replaced the TODO note under "Maps
  from your own disc" with two new subsections, "Rooms and doors in the pack" and "Universal
  Tracker's map tab"; added one cross-referencing bullet to each of PopTracker's and Universal
  Tracker's existing "What it shows" lists; fixed the same 861->862 / 793->794 counts.
- Checked every link and anchor: wrote `scratchpad/rc030/check_wiki_links.py` (a throwaway
  checker: every `[...](...)` in `scratchpad/wiki/*.md`, resolved against that page's own
  headings' GitHub-style slugs) — **0 bad links across 37 wiki pages**. The one same-file anchor
  link added to `docs/release/notes/0.03.00.md` (`#playing-with-archipelago`, `#trackers`)
  checked by hand against that file's own `##` headings.
- Committed: `249ad9d` ("Docs: fill 4 of the 21 TODO (maptab) placeholders ..." — 10 of the 21,
  across windows_check.md and the three release notes; the commit message undercounts them as
  "4" meaning 4 files, worth noting here since the message itself reads ambiguously).

**Task 2: `port/tests/test_tracker_job.cpp`'s stray folder.** `scratch()` built each case's
temp folder under `fs::current_path() / "tracker_job_test" / <name>`; every `TEST()` already
calls `fs::remove_all(dir)` on its own leaf at the end, but that never removed the shared
`tracker_job_test` parent that `create_directories` made along the way, so an empty
`tracker_job_test/` was left in whatever directory `ss_game_tests` ran from. Fixed by making
each case's folder a single-level directory directly under `fs::temp_directory_path()`
(`ss_tracker_job_test_<name>`, not nested under a shared parent this file creates), so the
existing `fs::remove_all(dir)` calls now remove the whole thing, with nothing left behind
anywhere (not even under the system temp dir).
- **Verified**: configured `port/build-rc030` (Ninja, RelWithDebInfo), built `ss_game_tests`
  (338/338 objects, exit 0). Ran `SS_TEST=tracker_` from a scratch cwd
  (`scratchpad/rc030/testcwd`, scratch `XDG_CONFIG_HOME`/`XDG_RUNTIME_DIR`, offscreen/dummy
  drivers, `ulimit -v 4194304` i.e. the 4 GB cap by hand since `tools/memcap.py` has no CLI entry
  point of its own — it's a library other tools import): **10 tests, 0 failed** (3 from
  `test_tracker_job.cpp`, 7 from `test_tracker_maps.cpp`, both under the `tracker_` substring
  filter). After the run, `scratchpad/rc030/testcwd/` held only the *other* file's
  `tracker_maps_test/` (test_tracker_maps.cpp has the identical bug, out of this task's scope,
  not touched) — no `tracker_job_test/` anywhere, and nothing under `/tmp/ss_tracker_job_test_*`
  either. Scratch test dirs removed after (`scratchpad/rc030/testcwd` etc.); `port/build-rc030`
  kept (871 MB) for now, not yet removed, since task 4 (the real RC build) is on hold and a
  pointer build may still be useful.
- Committed: `b82b176` ("Fix: test_tracker_job's scratch() no longer leaves tracker_job_test/ behind").

**Task 3: version bumps (checklist 12.1).**
- `port/CMakeLists.txt`: `project(... VERSION 0.03.00)`, and its two comments naming the old
  value (the `SS_VERSION` comment, the `.rc` fields comment: `0.03.00` -> `0,3,0,0`).
- `README.md`: the version badge, the two package names under "Download"
  (`ShatteredStars-0.03.00-{win64.zip,linux-x86_64.tar.gz}`). The world/pack release links were
  already at 0.2.2 / 1.0.0 (releasedocs); the one "needs Shattered Stars 0.02.51 or later"
  compatibility line (the bench) is correctly a minimum, not this release's version — left as is.
- `port/README.md`: the one example package name (`ShatteredStars-0.03.00-win64.zip`); its
  "so2.apworld 0.2.1" mention is a dated historical note (when the hell seed shipped), left as is.
- `docs/release/release_checklist.md` section 1's table: the world row 0.2.1 -> 0.2.2, the port row
  0.02.51 -> 0.03.00, plus two new rows (the pack 1.0.0, `ut.LOGIC_VERSION` 1 unchanged) so the
  table lists every version this release touches, as 12.1 does. This file's section-12
  placeholder fill (task 1, above) hadn't been committed separately yet when this edit was made
  (both edits were made before any commit touched this file), so **both land in this same
  commit** — its message says so.
- `apworld/so2/archipelago.json`: `world_version` 0.2.1 -> 0.2.2 (checked: still valid JSON).
- `apworld/so2/world.py`: `APWORLD_VERSION` 0.2.1 -> 0.2.2, the same string (checked: `ast.parse`
  still succeeds; the two are equal, per 12.1's rule).
- **Not changed, already correct**: `trackers/tools/ap_poptracker.py`'s `PACK_VERSION` is already "1.0.0";
  `apworld/so2/ut.py`'s `LOGIC_VERSION` is already `1` (12.1: stays 1, this release's logic
  changes are all conservative extensions already covered by it per logicexport's work).
  Grepped the allowed files for other `0.02.51` references: every remaining one is a correct
  historical reference (past RCs in checklist sections 9-11, "Updating from 0.02.51" in the
  0.03.00 notes, "plays on Shattered Stars 0.02.51 and later" compatibility lines in the 0.2.2
  notes) — none needed changing.
- Not yet rebuilt against the new version (holding before task 4, per the coordinator).
- Committed: `2452b35` ("Version bumps: port 0.03.00, world 0.2.2; checklist section 12's
  remaining TODO (maptab) placeholders ..." — this commit carries both this task's version
  bumps and the release_checklist.md edit, which also still held task 1's remaining 10
  placeholders uncommitted at that point; see the note above).

### Next (once the hold lifts)
- Task 4: builds (12.2) into `scratchpad/release/rc-0.03.00-dist/{port,apworld,poptracker}`.
- Task 5: tests (12.3), every row, including re-deriving the packaged e2e copies from today's
  `apworld/tools/ap_e2e_so2.sh` (step 9) and measuring the full Wine suite count.
- Task 6: sums (12.4), write results into checklist section 12 and this section.
- Task 7: release notes' sum/package TODOs replaced with the RC's real values.
- If the user's two pending changes (the UT zip of the port's renders; the door diamonds'
  colour) land before the hold lifts, re-read docs/handoff/phaseb.md and whichever agent's log
  covers them before resuming — the "944 of 964 maps switch a tab" / image-count / test-count
  facts filled in task 1 above may need a second pass if either change touches the layout, the
  pack, or the UT map tab.

### How to verify (so far)
- `grep -rn 'TODO (maptab)' docs/release/release_checklist.md docs/handoff/windows_check.md docs/release/notes scratchpad/wiki` prints only one explanatory (already-past-tense) mention, no blank placeholder.
- `python3 scratchpad/rc030/check_wiki_links.py` -> "OK: 37 wiki pages, 0 bad internal links".
- `grep -n ss_tracker_job_test port/tests/test_tracker_job.cpp` shows the new temp-dir based `scratch()`.
- `grep -n 'VERSION 0.03.00\|SS_VERSION="0.03.00"\|0,3,0,0' port/CMakeLists.txt` (the last one is in a comment, not literal code).
- `python3 -c "import json; print(json.load(open('apworld/so2/archipelago.json'))['world_version'])"` -> `0.2.2`.
- `grep -n APWORLD_VERSION apworld/so2/world.py` -> `"0.2.2"`.

### Rules given (this pass)
Headless only; nothing on the user's desktop (DISPLAY/WAYLAND_DISPLAY/DBUS_SESSION_BUS_ADDRESS
unset, scratch XDG_RUNTIME_DIR, or xvfb-run); every game process under tools/memcap.py (4 GB),
at most 3 at once; builds -j8; scratch XDG_CONFIG_HOME/HOME; scratch WINEPREFIX
(WINEDEBUG=-all, `wineserver -k` after); never a config-saving flag, never the user's own
config/saves/PopTracker folder/~/.local/share/Archipelago (read-only); kill only by exact PID;
Python from scratchpad/ap/Archipelago with ../ap-venv/bin/python, SKIP_REQUIREMENTS_UPDATE=1;
generation under `prlimit --as=$((8<<30))`; commit on main after each finished piece, docs
first, staged by path; never commit build output/images/zips; never push, tag, create a
release/draft, or commit/push the wiki; on a denied tool call, stop and name the exact command;
cap command output; long jobs backgrounded with deadlines; roll over at ~400k context.

## 0.03.00, continued (agent "rc030b", 2026-10-28)

Resumes at task 4 (builds), per phaseb.md's lane table. Read this file's "0.03.00" section above
(rc030's log, the rules), docs/release/release_checklist.md section 12, docs/handoff/utzip.md
(its "Counts changed in the release docs" -- being rechecked against this RC's own runs),
docs/handoff/phaseb.md, release.md "0.02.51" (the prior RC's shape).

**Decision taken (not a change to policy, recorded for anyone resuming):** this RC does **not**
use a disposable worktree, unlike 0.02.50/0.02.51's RC scripts. `git status` is clean at HEAD
(1e535ec, tree 364be25c1f97a457a1a0dc7533bdf026a55d6044) with no other agent running, and this
agent's own allowed files are docs/scratchpad/dist only (never port/apworld/tools/mods/LICENSE),
so the main tree already *is* "one clean commit" for the whole run; a worktree would only have
reintroduced section 12.2's "build_poptracker.sh needs scratchpad/ap" trap (that script hardcodes
`$repo/scratchpad/ap`, which doesn't exist inside a fresh worktree) for no isolation benefit here.
`ph_verify` (last phase) re-checks `git status --short -- engine apworld tools mods LICENSE` is
still empty and HEAD/tree unchanged before the sums are trusted.

**The RC script**: `scratchpad/release/rc-0.03.00.sh PHASE...` (snapshot build linux wine world
batch identity ut pack suites e2e extra asan tsan winsuites sums verify cleanup, or `all`).
Adapted from `scratchpad/release/rc-0.02.51.sh`; new pieces: `identity` (a `cp -as` farm of
scratchpad/ap/Archipelago with worlds/so2 repointed at a `git archive apworld-v0.2.1 apworld/so2`
extraction and every top-level script realized as a real copy, not a symlink -- the "baseline
trap" utworld.md found -- comparing against this RC's own `batch` output with
`scratchpad/release/identity_0_2_1.py`, adapted from `scratchpad/storyaware/identity.py` to strip
four fields instead of one: `.apso2`'s `options.apworld_version`, and the multidata slot_data's
`logic_version` / `exclude_locations` / `tracker`, none of which 0.2.1 ever wrote); `ut` (real
Universal Tracker check, `trackers/tools/ap_ut_check.py`); `extra` (the render end-to-end script, UT's own
map loader against both zips x both worlds, setup headless A-D plus utzip's M/Z zip-interrupt
scenarios); `asan` / `tsan` (own build dirs `port/build-rc030b-asan` / `-tsan`, reusing
`port/build/_deps` for FetchContent sources and `scratchpad/vmfix/asan`'s libasan copy, the
same recipe as `scratchpad/setupthread/build_san.sh`); `winsuites` also renders under Wine and
byte-compares against a Linux capture from `extra`. `BUILD_RC=port/build-utzip`, reused as-is
(confirmed `ninja: no work to do` against this exact commit before the run started) rather than
rebuilt, saving a full engine rebuild.

### Status: build phase running (started 07:33)

`bash scratchpad/release/rc-0.03.00.sh snapshot` ran first in the foreground (confirms the commit
and tree hash above, 0 dirty lines). `bash scratchpad/release/rc-0.03.00.sh build` is running in
the background (nohup, `scratchpad/release/rc030b-build.out`): layout check OK, the Linux
container build in progress. Next once it lands: linux, wine, world, batch, identity, ut, pack,
suites, e2e, extra, asan, tsan, winsuites, sums, verify, cleanup, in that order (the "all" list).
Watching via a Monitor on `scratchpad/release/rc-0.03.00-dist/summary.txt`.

Nothing committed yet this pass (no finished piece to commit). Will update this section again
within 30 minutes or at the next finished phase, whichever comes first.

### Status 07:37: build phase done

All five build steps rc=0, fast (about 4 minutes total): layout check, `port/tools/build_linux.sh
--container`, `port/tools/build_windows.sh`, `apworld/tools/build_apworld.sh`, `trackers/tools/build_poptracker.sh
all`. Artifacts (in `scratchpad/release/rc-0.03.00-dist/`):
- `port/ShatteredStars-0.03.00-linux-x86_64.tar.gz`: 9,906,177 bytes
- `port/ShatteredStars-0.03.00-win64.zip`: 8,315,495 bytes
- `apworld/so2.apworld`: 281,557 bytes (matches utzip2's measured size exactly)
- `apworld/Star.Ocean.The.Second.Story.yaml`: 24,325 bytes
- `poptracker/so2_shattered_stars_1.0.0.zip`: 5,559,656 bytes (matches utzip's measured size
  exactly), 1,201 files, pack checker + `trackers/tools/ap_poptracker_test.py` both ran clean inside
  `build_poptracker.sh all` (re-verified with a dedicated run in the `pack` phase)
- apworld manifest has no `__pycache__`, `archipelago.json`'s `version`/`compatible_version`
  present (packaging-added), `world_version` 0.2.2.

A bug found and fixed in the RC script before running further phases: `ph_wine`'s Windows-vs-
Linux render byte-compare reads `$DIST/linux-render`, which only `ph_extra` writes -- but
`ph_extra` came after `ph_wine` in the original phase order. Fixed by reordering (`extra` now
runs right after `linux`, before `wine`); the script's header and `all` list both corrected.
Noted here since anyone re-running a single `wine` phase in isolation should run `extra` first.

Also confirmed useful for the optional "UT story awareness on a 0.2.1 room" check (task 5): the
existing `dist/apworld-0.2.1/so2.apworld` (186,265 bytes, `world_version` 0.2.1, built earlier by
ncpfix) is still present and usable directly with `trackers/tools/ap_ut_check.py --apworld`, so that check
doesn't need its own apworld build.

Next: `linux`, `extra`, `wine`, `world`, `batch`, `identity`, `ut`, `pack`, `suites`, `e2e`,
`asan`, `tsan`, `winsuites`, `sums`, `verify`, `cleanup`, running in the background.

### 0.03.00, coordinator takeover (2026-10-28)
- rc030b was handed back twice while its background script ran (a subagent can't wait hours); it
  was told to stop. The coordinator owns the rest of the run: background job writing
  `scratchpad/release/rc030-coord/coord_run.log` (waits for PID 2844214 `rc-0.03.00.sh linux extra
  wine`, compares the zips, then runs `world batch identity ut pack suites e2e asan tsan winsuites
  sums verify cleanup`; progress in `scratchpad/release/rc-0.03.00-dist/summary.txt`).
- **`ut_map_repo_port` / `ut_map_pkg_port` rc=1 were an RC-script bug, not a product bug.**
  `--render-tracker-maps DIR` writes the UT zip into DIR's **parent** (as documented), so the
  capture's zip is `rc-0.03.00-dist/so2_shattered_stars_ut.zip`, not `linux-render/...` where the
  script looked; UT got no zip and loaded 0 maps. Rerun by the coordinator against the real zip
  (copied to `scratchpad/release/rc030-coord/linux_ut.zip`, 134,897,214 bytes, sha256 35e72e8d..., =
  utzip2's): **repo world 16/16 ok, packaged so2.apworld 16/16 ok** (logs `rc030-coord/utmap_*.log`).
- Same assumption in `ph_wine`: the Windows render's zip also lands in `rc-0.03.00-dist/` and
  overwrites the Linux one, and the script's diff only compares the image folders. The coordinator's
  job compares the Windows zip with the saved Linux copy byte for byte (result in coord_run.log).
- Script fix to make **after** the run (bash reads a running script as it goes: don't edit it now):
  render_capture / win_render pass `--ut-zip` into their own folders (or the UT-map steps read
  `$DIST/so2_shattered_stars_ut.zip` before ph_wine runs), and ph_wine compares the two zips.
- Next once coord_run.log ends: a fresh agent reads summary.txt + coord_run.log, records every row
  in checklist 12.3 / this log, writes the sums into the three notes (task 7), fixes the script.

### 0.03.00, the restructure before the rerun (2026-10-28, agent "restructure")
The user reorganised the repo into sections before the RC is rerun (docs/handoff/restructure.md
has every move): engine/ is port/ (res/ is port/res/), the tools moved to port/tools,
apworld/tools and trackers/tools (shared ones stay in tools/), docs/ap split into docs/apworld,
docs/trackers and docs/release (the checklist; release_notes is docs/release/notes), docs/notes and
docs/catalog are docs/port/{notes,catalog}, the wiki's pages are wiki/ (published by
tools/wiki_publish.sh). The PopTracker pack's update URL is now
`.../main/trackers/poptracker/versions.json` (baked into the pack; 1.0.0 isn't published).
The paths in this log were rewritten to the new layout; the commands in it run as written.
`scratchpad/release/rc-0.03.00.sh` was updated for the layout:
- `BUILD_RC=$MAIN/port/build` (configured fresh on the new layout, RelWithDebInfo, deps from
  port/build/_deps); `ph_build` starts with a `native_build` step so BUILD_RC is always current.
  The old engine/build-utzip, build-win-release and build-linux-container were stale (absolute paths
  in their caches) and are deleted: `linux` / `windows` configure fresh dirs and download the pinned
  deps again.
- `SRC=(port apworld trackers tools mods LICENSE docs/trackers/tracker_maps.json)` for
  snapshot / verify (tracker_maps.json is compiled into the port).
- The UT-zip bug above is fixed: `render_capture` renders into `linux-render/maps` with
  `--ut-zip linux-render/so2_shattered_stars_ut.zip`, `win_render` into `win-render/maps` with its own
  `--ut-zip`; `ph_wine`'s diff covers both folders (maps and zip) and a new `win_ut_zip_identical`
  step `cmp`s the two zips; `ut_map_*_port` read `linux-render/so2_shattered_stars_ut.zip`.
- `ut_explain` skips with a note: scratchpad/rulebuilder/ut_explain.py was deleted in today's
  scratch cleanup (the row is optional in 12.3).
- `adapt_e2e` matches the e2e scripts' new `cd "$(dirname "$0")/../.."`.
The rerun: start from a clean tree, set the old results aside, run everything:
`mv scratchpad/release/rc-0.03.00-dist scratchpad/release/rc-0.03.00-dist.pre-restructure` then
`bash scratchpad/release/rc-0.03.00.sh all` (in the background; progress in
`scratchpad/release/rc-0.03.00-dist/summary.txt`).
