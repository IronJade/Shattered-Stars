# Release checklist: Archipelago phase 5

Preparation only. This checklist builds, verifies and documents the `.apworld` package and
the port's release bundles; it does not publish anything. Publishing (a GitHub release on the
user's own account) and the upstream pull request are the user's call, made later and by hand.
Nothing here contacts archipelago.gg or any public server; every server used below is
127.0.0.1-only (`tools/ap_test_server.sh`).

## 1. Version numbers

**Decided (2026-10-23).** Two independent version numbers, on purpose — they bump on
different schedules and Archipelago can only read one of them as a real version:

| What | Value | Set in |
|---|---|---|
| The APWorld (`world_version`) | `0.2.2` | `apworld/so2/archipelago.json` (hand-edited; bump before each release) and `apworld/so2/world.py`'s `APWORLD_VERSION` (written into each seed's slot data as `apworld_version`; keep the two equal) |
| Archipelago core this world targets (`minimum_ap_version`) | `0.6.8` | `apworld/so2/archipelago.json`; the port's client announces the same version (`engine/CMakeLists.txt` reads it from there at configure time) |
| The port's own release version | **`0.03.00`** | `engine/CMakeLists.txt`'s `project(... VERSION 0.03.00)`; reaches the code as the `SS_VERSION` compile definition on `ss_core` (so every target that links it — the game, setup, `ss_tests`, `ss_game_tests` — sees the same string) |
| The PopTracker pack version | `1.0.0` | `tools/ap_poptracker.py`'s `PACK_VERSION` |
| UT's `ut.LOGIC_VERSION` | `1` (unchanged for 0.2.2) | `apworld/so2/ut.py`; bump it whenever a logic change could make an older room's rebuild wrong (docs/ap/apworld.md "Trackers") |

The port version is written `0.01.00`: two-digit minor and patch fields, chosen so either
can go past 9 later without reordering (`0.01.00` -> `0.10.00` -> `0.99.00`, never `0.10.0`
vs `0.1.10` ambiguity). Checked: CMake keeps the literal string in `PROJECT_VERSION` (does
not strip the leading zeros to parse it as `0.1.0`) — verified with a throwaway
`project(Test VERSION 0.01.00)` that prints `PROJECT_VERSION=0.01.00` back. It shows up in:
- `shattered-stars --version` / `--help` and `shattered-stars-setup --version` / `--help`
  (prints `Shattered Stars 0.01.00` / `Shattered Stars Setup 0.01.00`, exits 0, touches no
  file — checked with a scratch `XDG_CONFIG_HOME`, no `config.ini` appears);
- setup's header, in small text next to the "Setup" subtitle (`src/setup/setup_ui.cpp`'s
  `App::header()`);
- the release bundle names (section 3).

**The APWorld's `world_version` stays `0.1.0`, not `0.01.00`.** Archipelago parses
`world_version` as a tuple of plain integers for compatibility checks (`0.01.00` would read
back as `(0, 1, 0)`, identical to `0.1.0` — no ambiguity there, but also no benefit from the
two-digit convention), so there is no reason to break from the rest of the Archipelago
ecosystem's `x.y.z` world-version convention for this one field. The two numbers are
unrelated on purpose: an apworld bump doesn't need a new port build and vice versa (section
3's "ship beside, not inside" decision).

The Archipelago client's version reporting (`engine/src/ap_net/client.cpp`): the `"version"`
field of the `Connect` packet is the Archipelago version the client speaks (the server checks it
against the slot's minimum, and with `compatibility 0` against its own), not a free-form
client/game version; there is no separate field to attach `SS_VERSION` to. Until 0.02.50's
Windows check it was apclientpp's default (`APCLIENTPP_VERSION_INITIALIZER`, 0.6.6, its own
release), so a 0.6.8 server listed the game as `Client(0.6.6)`. Since then `ConnectSlot` gets the
world's `minimum_ap_version` (0.6.8; `engine/CMakeLists.txt` reads `apworld/so2/archipelago.json`
and passes `SS_AP_VERSION_*` to `ss_ap_net`; 0.6.8 if the file is missing), so bumping the
manifest bumps the client too; the configure log says "Archipelago client announces version
...". Checked against the local server (`tools/ap_test_server.sh`): every `ap_net` case passes
and the server logs `Client(0.6.8)`. `tags` carries only `"DeathLink"` when that option is on.

## 2. Verification commands and expected results

All of these were run this pass (2026-10-23) and passed. Re-run before actually publishing,
since source keeps changing (hourly checkpoints).

### 2.1 The `.apworld` package
```sh
tools/build_apworld.sh
# -> dist/so2.apworld, dist/"Star Ocean The Second Story.yaml"
unzip -l dist/so2.apworld | grep -i __pycache__      # expect: no match
unzip -p dist/so2.apworld so2/archipelago.json       # expect: "compatible_version": 7, "version": 7 present
```
Expected: both files written; the manifest carries `version`/`compatible_version` (added by
Archipelago's own "Build APWorlds" Launcher component, which the script calls — **do not**
hand-roll the zip, see the note in `tools/build_apworld.sh`'s header: a hand-rolled zip loads
with a logged manifest error and stops working in Archipelago 0.7.0, because
`archipelago.json` correctly omits those two fields for a *folder* world per `docs/apworld
specification.md` in the Archipelago install — they must be added by the packaging step, not
written by hand).

### 2.2 Packaged-world verification (a separate install copy, not the dev one)
```sh
cp -a scratchpad/ap/Archipelago scratchpad/release/Archipelago   # once; scratchpad/ap/Archipelago untouched
rm -f scratchpad/release/Archipelago/worlds/so2                  # must NOT be present
mkdir -p scratchpad/release/Archipelago/custom_worlds
cp dist/so2.apworld scratchpad/release/Archipelago/custom_worlds/
SKIP_REQUIREMENTS_UPDATE=1 scratchpad/ap/ap-venv/bin/python -c "
import worlds; from worlds.AutoWorld import AutoWorldRegister
assert 'Star Ocean: The Second Story' in AutoWorldRegister.world_types
assert not worlds.failed_world_loads
"
```
Expected: no `AssertionError`, and no "Invalid or missing manifest file" logged (that error
is exactly what a hand-rolled zip produces; see 2.1).

Generate a solo seed, a multiworld seed, and at least two non-default option sets, all from
the template YAML (`dist/"Star Ocean The Second Story.yaml"`) as a starting point:
```sh
# player YAMLs under scratchpad/release/verify/{solo,multi,nondefault1,nondefault2}/
SKIP_REQUIREMENTS_UPDATE=1 scratchpad/ap/ap-venv/bin/python Generate.py \
  --player_files_path <dir> --outputpath <out> --seed 777      # run once per player-set, in scratchpad/release/Archipelago
```
Expected: each run writes `AP_<seed>.zip` containing one `.apso2` per Star Ocean slot and a
spoiler, with exit 0 and nothing in the log matching `error|traceback|exception`. Verified
this pass for: defaults solo; defaults in a 2-player multiworld (SO2 + ChecksFinder);
`world_mode: open_world` + `character_items` + `shuffle_shops: full` + `traps: 30` +
`cave_of_trials` + `death_link`; `story_route: rena` + `missable_locations: include` +
`expel_progression: false` + the EXP/Fol/encounter-rate options.

Serve one multiworld zip and confirm it binds to 127.0.0.1 only:
```sh
SKIP_REQUIREMENTS_UPDATE=1 scratchpad/ap/ap-venv/bin/python MultiServer.py <zip> \
  --host 127.0.0.1 --port <port> --disable_save --loglevel info
```
Expected: log line `Hosting game at 127.0.0.1:<port>`, `Loading embedded data package for
game Star Ocean: The Second Story`; stop by the exact PID (not a pattern match).

### 2.3 Port end-to-end, against the packaged world
```sh
SS_DISC1="<disc 1 .cue>" bash scratchpad/release/ap_e2e_so2_packaged.sh engine/build/shattered-stars
```
This is an adapted copy of `tools/ap_e2e_so2.sh` (not merged into `tools/`, since the
original hardcodes `scratchpad/ap`): it points at `scratchpad/release` instead, requires
`custom_worlds/so2.apworld` present and `worlds/so2` absent (fails loudly otherwise), and
still uses the shared `scratchpad/ap/ap-venv`. Expected: **25/25 PASS**
(`scratchpad/release/e2e_packaged.log`) — character items, first drops, traps, open world +
full shop shuffle, the goal, all against the packaged `.apworld`, not the dev symlink.

### 2.4 Upstream readiness: the general suite + this world's own tests
```sh
mv scratchpad/release/Archipelago/custom_worlds/so2.apworld{,.bak}   # don't load the world twice
ln -sfn "$PWD/apworld/so2" scratchpad/release/Archipelago/worlds/so2
cd scratchpad/release/Archipelago
SKIP_REQUIREMENTS_UPDATE=1 ../../ap/ap-venv/bin/python -m pytest -q -n 8 worlds/so2/test
SKIP_REQUIREMENTS_UPDATE=1 ../../ap/ap-venv/bin/python -m pytest -q -n 8 test/general
# the guide's scoped run (docs/tests.md "Running Tests for Specific Worlds"): the general
# tests that iterate over worlds, scoped to so2, plus worlds/so2/test (test/webhost needs flask)
AP_TEST_WORLDS=so2 SKIP_REQUIREMENTS_UPDATE=1 ../../ap/ap-venv/bin/python -m pytest -q -n 8 test/general worlds/so2
```
Since 2026-10-26 (the aptests handoff log) worlds/so2/test follows Archipelago's testing guide
(docs/ap/apworld.md "Testing"); expected then: **430 passed, 3 xfailed** (the expel_progression
bug's reproducer, until its fix); since 0.2.1 (the ncpfix handoff log) **449 passed**, the
decorators gone and the mix `TestOpenWorldNoExpelProgressionNoBosses` added, each test under a
second (`--durations=30`); since skillsanity progression (still 0.2.1, the skilllogic handoff log)
**537 passed**; since the hell seed (still 0.2.1, the hellseed handoff log) **634 passed**; since the bench (still 0.2.1, the benchworld handoff log) **699 passed** (scoped 752). Expected (got this pass): `worlds/so2/test` — **95 passed, 7986 subtests**; `test/general` —
**379 passed, 116984 subtests**. No apworld failures appeared; see section 4 for the three
style-only fixes made after the upstream gap analysis, re-verified with the same counts.

### 2.5 This repo's own apworld regression (docs/ap/apworld.md, "Testing")
Already green at phase 4 (95 passed / 7986 subtests, AP general 379 / 116983, a 64-seed /
127-option-combination batch with 0 checker errors, `tools/ap_e2e_so2.sh` 25/25 against the
dev symlink) — re-run after any further apworld change:
```sh
SKIP_REQUIREMENTS_UPDATE=1 scratchpad/ap/ap-venv/bin/python -m pytest -q -n 8 worlds/so2/test   # in scratchpad/ap/Archipelago
tools/ap_batch.sh story_open scratchpad/batch/full 1 96    # the release workflow: exit 0 = all generate, 0 checker errors
tools/ap_batch.sh hub scratchpad/batch/hub 1 96
tools/ap_batch.sh solo_noexpel scratchpad/batch/solo 1 96   # since 0.2.1: one SO2 player alone, expel_progression off
SS_DISC1="<disc 1 .cue>" tools/ap_e2e_so2.sh
```
(Since 2026-10-26 the batches are `tools/ap_batch.sh` + `tools/ap_batch_check.py` with the players
sets in tools/ap_batch_players/: the 0.2.0 release's scratchpad/apworld2/batch run_batch.sh,
check.py, players_sk and players_hub_sk, moved; the same Generate.py call, so the same files per
seed. The older `scratchpad/enemy/ap5/batch` lines below are history.)
This pass only touched `regions.py`, `doors.py`, `patches.py` and `test/test_doors.py` for
line-length wraps with no behavior change (confirmed: `ast.parse` succeeds, and
`worlds/so2/test` still reports 95 passed / 7986 subtests before and after). Nothing here
required a new 20-seed batch; one is still owed before the next apworld logic change ships.

### 2.6 Port release bundles
```sh
tools/build_linux.sh --container      # -> dist/ShatteredStars-0.01.00-linux-x86_64.tar.gz
MINGW_SSL_SYSROOT=scratchpad/setup/mingw-ssl/usr/x86_64-w64-mingw32/sys-root/mingw tools/build_windows.sh
                                       # -> dist/ShatteredStars-0.01.00-win64.zip
objdump -T dist/ShatteredStars-0.01.00-linux-x86_64/shattered-stars | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
SS_DISC1="<disc 1 .cue>" tools/smoke_field_events.sh dist/ShatteredStars-0.01.00-linux-x86_64/shattered-stars
```
Desktop identity (from 0.02.50; the appicon handoff log): both `.exe`s carry one icon group
(16, 24, 32, 48, 64, 128, 256 px), a version resource (ProductName `Shattered Stars`,
FileDescription `Shattered Stars` / `Shattered Stars Setup`, FileVersion = the port version,
and no `POSIX WinThreads` one) and a manifest (`asInvoker`): check with REA's
`inspect_pe_resources` or `x86_64-w64-mingw32-objdump -s -j .rsrc`. The Linux bundle has
`install-desktop-entry.sh` and `desktop/` (two `.desktop` files, `desktop-file-validate`
clean, and `icons/hicolor/`).

The version in the filename (and the top folder inside each archive) is read out of
`engine/CMakeLists.txt` by the scripts themselves (section 1), not hand-written in the
scripts — bumping the one `project(... VERSION ...)` line is enough. Both scripts also now
copy the repo-root `LICENSE` next to `THIRD-PARTY-LICENSES.txt` (section 5's licence
decision).

Verified this pass (2026-10-23, rebuilt because `engine/CMakeLists.txt` / `main.cpp` /
`setup_main.cpp` / `setup_ui.cpp` changed for the version decision): both builds exit 0.
`dist/ShatteredStars-0.01.00-linux-x86_64.tar.gz` (8.1M) and
`dist/ShatteredStars-0.01.00-win64.zip` (6.6M); `tar tzf` / `unzip -l` on each show the
versioned top folder and `LICENSE` alongside `README.md`/`README.txt` and
`THIRD-PARTY-LICENSES.txt`; glibc floor `2.36` (unchanged). `shattered-stars --version` on
the packaged Linux binary prints `Shattered Stars 0.01.00`; smoke on the rebuilt Linux
package: **86 PASS / 0 FAIL** (matches the dev build's `engine/build-release2` run). The old unversioned
`dist/ShatteredStars-linux-x86_64.tar.gz` / `dist/ShatteredStars-win64.zip` (and their
unpacked folders) were deleted once the versioned ones existed. The Windows package's own
suites under Wine (`ss_tests.exe` / `ss_game_tests.exe` / smoke via
`scratchpad/setup/wine/game-release.sh`, the setup handoff log's Verification commands
section) were **not** re-run this pass (no engine behavior changed beyond the version
strings, same reasoning as the prior round) — still owed before actually publishing.

## 3. Artifacts and their paths

All under `dist/` (git-ignored; confirmed `.gitignore` already has `dist/` at line 15 — no
change needed):

| Artifact | Path | From |
|---|---|---|
| The APWorld | `dist/so2.apworld` | `tools/build_apworld.sh` |
| Its options template | `dist/Star Ocean The Second Story.yaml`, uploaded and downloaded as `Star.Ocean.The.Second.Story.yaml` (GitHub turns the spaces of an asset's name into dots, as v0.01.00's and v0.02.00's assets show; the release notes and SHA256SUMS use that name) | `tools/build_apworld.sh` (Archipelago's `Options.generate_yaml_templates`) |
| Linux release | `dist/ShatteredStars-<version>-linux-x86_64.tar.gz` (0.02.50: `ShatteredStars-0.02.50-linux-x86_64.tar.gz`) | `tools/build_linux.sh --container` |
| Windows release | `dist/ShatteredStars-<version>-win64.zip` (0.02.50: `ShatteredStars-0.02.50-win64.zip`) | `tools/build_windows.sh` |

**Decision: the `.apworld` and template YAML ship *beside* the port bundles, not inside
them.** Reasoning: the two update on different schedules (an apworld bump doesn't need a new
port build and vice versa), and most players will get the world from wherever their
multiworld's host or the Archipelago site points them — not necessarily from this project at
all, once/if the world is merged upstream (section 5). Until then, a GitHub release page
listing all four files side by side (not zipped together) is the plan — see section 4.

No game data or disc assets are in any of these; the port and setup program read the
player's own disc images at run time (`docs/CONVENTIONS.md`). None of this was committed to git
and none of it was pushed or published.

## 4. What publishing would involve (the user's call, not done)

1. Bump `world_version` in `apworld/so2/archipelago.json` if anything changed since `0.1.0`,
   and bump the port's own `0.01.00` in `engine/CMakeLists.txt` if anything changed there
   (section 1; both are decided conventions now, not open questions).
2. Re-run section 2's commands fresh (source moves fast here — hourly checkpoints).
3. Create a GitHub release under the user's own account/repo, attaching the four files from
   section 3 (not the whole `dist/` folder, and never anything under `scratchpad/`).
4. Write release notes — this checklist and `docs/handoff/release.md` have the raw material
   (what's randomized, what's new, known gaps from section 5).
5. Nothing in this pass pushed a commit, opened a PR, or contacted any server outside
   127.0.0.1. All of the above is still to do, by the user, whenever they choose.

## 5. The upstream pull request route (ArchipelagoMW/Archipelago)

Read from the Archipelago 0.6.8 install's own docs (`docs/contributing.md`, `"docs/world
api.md"`, `"docs/world maintainer.md"`, `docs/style.md`, `"docs/apworld specification.md"`,
`docs/tests.md`, `"docs/adding games.md"`, `.github/CODEOWNERS`, `LICENSE`,
`.github/pull_request_template.md`), checked against this world's current state:

| Requirement | Status | Evidence |
|---|---|---|
| World-folder structure (`__init__.py`, `World` subclass, item/location id maps, `origin_region_name`, a completion condition) | **MET** | `apworld/so2/__init__.py`, `world.py:30-41` |
| Game info page, filename `{lang}_{game}.md` | **MET** | `apworld/so2/docs/en_Star Ocean The Second Story.md` (rewritten this pass) |
| Setup guide, filename matching the `Tutorial`'s path | **MET** | `apworld/so2/docs/setup_en.md` (rewritten this pass); `world.py`'s `Tutorial(...)` entry checked against the convention and left as-is |
| Unit tests, `WorldTestBase` conventions | **MET** | `apworld/so2/test/` (`bases.py`'s `SO2TestBase(WorldTestBase)`); 95 passed / 7986 subtests (section 2.4) |
| `archipelago.json` manifest fields for a folder world | **MET** | `game`, `minimum_ap_version`, `world_version`, `authors` present; `version`/`compatible_version` correctly *absent* (those are added only when packaged, section 2.1) |
| Maintainer / `CODEOWNERS` entry | **MET (decided 2026-10-23)** | Maintainer: **IronJade**. `archipelago.json`'s `authors` and `world.py`'s `Tutorial(...)` authors list both changed from the placeholder `["Shattered Stars"]` to `["IronJade"]` (verified by reloading the packaged world: `tutorials[0].authors == ['IronJade']`). The proposed line for Archipelago's own `.github/CODEOWNERS` (added only at PR time, in that repo, not this one): `/worlds/so2/ @IronJade` — **the `@name` must be the maintainer's actual GitHub account name**, not a display name or the project name; confirm it matches before opening the PR. |
| Style guide (120-char lines, double-quoted strings, modern lowercase generics) | **MET**, after 3 small fixes | Found by the upstream gap-analysis read: `regions.py:63-64`, `doors.py:84-85`, `patches.py:253` over 120 chars (plus `test/test_doors.py:32`, found independently) — all wrapped this pass, no behavior change (section 2.5). No other violations spotted (double-quote and typing conventions already followed throughout). |
| Licensing compatibility (Archipelago is MIT) | **MET (decided 2026-10-23)** | Shattered Stars is MIT-licensed: `LICENSE` at the repo root ("Copyright (c) 2026 IronJade"), bundled with both port releases (section 2.6) and mentioned in `engine/README.md`. `apworld/so2/LICENSE` carries the same text — checked that Archipelago's "Build APWorlds" component packs it without complaint (it zips every file under the world folder not matched by `.apignore`; a plain `LICENSE` file matches no exclusion pattern in `data/GLOBAL.apignore`, and the built `dist/so2.apworld` does contain `so2/LICENSE`). Merging upstream now explicitly carries this license forward instead of implicitly inheriting Archipelago's MIT on merge. |
| PR mechanics (title format, what/how-tested sections, screenshots if graphical) | **N/A until PR time** | `.github/pull_request_template.md` wants a scoped title (e.g. "MyGame: implement new game") and what-you-tested notes; `docs/contributing.md` requires the style guide, tests for critical changes, no regressions, Python ≥3.11 support, and reviewers who state what they tested. No squash-commit, branch-naming, changelog-entry, label, or mandatory-Discord-discussion requirement was found. |
| `test/general/` suite passes with this world installed | **MET** | 379 passed / 116984 subtests (section 2.4) |
| DeathLink boss-check wording | **MET** | Verified from the real scenes (the deathlink handoff log): boss checks record on a win, on time-up (the survival fights 516 / 520) and on the scripted defeat of the lose-allowed fights (204 / 517 / 530), never on an escape; the player docs say so. |

**Bottom line:** every row is met or waits for PR time.

## 6. Items flagged "to review" (not refactored speculatively, per the task's rules)

- Re-running the Windows package's suites under Wine before actually publishing (section
  2.6) — not done this pass since no engine behavior changed, only a rebuild. **Done for
  0.02.50** (section 9): `ss_tests.exe` 99 / 0, `ss_game_tests.exe` 182 / 0, after the fixes it
  led to (a Windows save bug and two test-order effects).

## 7. Release candidate 0.01.00: verification run (2026-10-23)

Built from commit `e0fb27f` (then `HEAD`) in a disposable git worktree
(`git worktree add scratchpad/rc-0.01.00 e0fb27f`, removed afterward with
`git worktree remove`), so uncommitted work elsewhere in the main tree could not reach the
build. Preparation only — nothing was tagged, released, pushed or committed by this run.

| Check | Result |
|---|---|
| `tools/build_linux.sh --container` | exit 0. `ShatteredStars-0.01.00-linux-x86_64.tar.gz`, 8.3M. glibc floor `2.36` (`objdump -T` on both binaries). |
| `tools/build_windows.sh` (`MINGW_SSL_SYSROOT` set) | exit 0. `ShatteredStars-0.01.00-win64.zip`, 6.7M. |
| `tools/build_apworld.sh` (against a scratch Archipelago install with no pre-existing `worlds/so2`, so it packaged the worktree's clean `apworld/so2`, not the main tree's working copy) | exit 0. `so2.apworld` (76K), `Star Ocean The Second Story.yaml` (9.7K). |
| Linux archive contents | 9 entries: versioned top folder, `shattered-stars`, `shattered-stars-setup`, `LICENSE`, `README.md`, `README.txt`, `THIRD-PARTY-LICENSES.txt`, `discs/` with its placeholder note. No game files, no debug/scratch files. |
| Linux `--version` / `--help` (packaged binary, scratch `XDG_CONFIG_HOME`) | Both print `Shattered Stars 0.01.00`, exit 0, no `config.ini` written (only a run log). `shattered-stars-setup --version` prints `Shattered Stars Setup 0.01.00`. |
| Linux smoke (`tools/smoke_field_events.sh`, packaged binary, `SS_DISC1` set) | **73 PASS / 0 FAIL / 1 SKIP** (the skip is a dev-only fixture, `analysis/saves/dex_north_city.mcd`, not shipped in the package — expected). |
| Windows under Wine (scratch `WINEPREFIX` under `scratchpad/rc-wine/`, `WINEDEBUG=-all`, `DISPLAY` unset, `SDL_VIDEO_DRIVER=offscreen`, `SDL_AUDIO_DRIVER=dummy`; wine-staging 11.17) | `shattered-stars.exe --version` / `--help`: both print `Shattered Stars 0.01.00`, exit 0. A 300-vblank headless boot onto field map 18 (Arlia) via `--headless --no-memcard --frames 300 --map 18` with `SS_DISC1` (via `winepath -w`) and `SS_TRACE=1`: the map's own triggers register (code words 491/478/etc., matching the Linux smoke test's Arlia case) and the run exits 0 after "stopped after 300 vblanks" — no crash. The *full* Windows test suites (`ss_tests.exe`/`ss_game_tests.exe`) were **not** re-run this pass; only the version flags and a headless boot were in scope. Wine processes stopped by `wineserver -k` with the scratch prefix; confirmed none left running. |
| `.apworld` install + seed | Installed into `scratchpad/release/Archipelago/custom_worlds/so2.apworld` (replacing the copy already there), with no `worlds/so2` present. Loads clean (`AutoWorldRegister`, no `failed_world_loads`). `archipelago.json`: `"authors": ["IronJade"]`, `world_version` 0.1.0 (confirmed both from the unzipped manifest and from `AutoWorldRegister.world_types[...].world_version == Version(0, 1, 0)`). One seed generated from the built template YAML (`Generate.py --seed 90100`): exit 0, `AP_<seed>.zip` written, nothing in the log matching error/traceback. |
| sha256sums | `scratchpad/rc-0.01.00-dist/SHA256SUMS`, all four files. |

Artifacts collected at `scratchpad/rc-0.01.00-dist/` (git-ignored, not published):
`ShatteredStars-0.01.00-linux-x86_64.tar.gz`, `ShatteredStars-0.01.00-win64.zip`,
`so2.apworld`, `Star Ocean The Second Story.yaml`, `SHA256SUMS`.

Release notes for players: [`docs/release_notes/0.01.00.md`](../release_notes/0.01.00.md).

Nothing was tagged, released, pushed, or committed by this pass; the commit of these docs
(and the eventual publish) is the coordinator's call. The command below is what publishing
would run once the docs above are committed and pushed — **not run by this pass**:

```sh
gh release create v0.01.00 \
  dist/ShatteredStars-0.01.00-linux-x86_64.tar.gz \
  dist/ShatteredStars-0.01.00-win64.zip \
  dist/so2.apworld \
  "dist/Star Ocean The Second Story.yaml" \
  --draft --target main \
  --title "Shattered Stars 0.01.00" \
  --notes-file docs/release_notes/0.01.00.md
```

`--target main` ties the new `v0.01.00` tag to whatever commit is then at the tip of `main`
(the pushed commit with these release docs); `--draft` means nothing goes live until the
draft is explicitly published. The four files above should be rebuilt fresh in the main
tree's own `dist/` immediately before running this (section 4's standing advice: source moves
fast here, re-verify before actually publishing), rather than reusing this pass's
`scratchpad/rc-0.01.00-dist/` copies, which exist only for this review.

## 8. Release candidate 0.02.00: verification run (2026-10-23)

Built from commit `47dbc37` in a disposable worktree by `scratchpad/rc-0.02.00.sh` (the
section 7 procedure as one script: worktree, builds, checks, sums, worktree removed; log and
results in `scratchpad/rc-0.02.00-dist/`). Nothing was tagged, released or pushed by this run.

| Check | Result |
|---|---|
| `tools/build_linux.sh --container` | exit 0. `ShatteredStars-0.02.00-linux-x86_64.tar.gz`, 8.5M. glibc floor `2.36`. |
| `tools/build_windows.sh` (`MINGW_SSL_SYSROOT` set) | exit 0. `ShatteredStars-0.02.00-win64.zip`, 6.9M. |
| `tools/build_apworld.sh` (fresh Archipelago copy without `worlds/so2`, so it packaged the worktree's `apworld/so2`) | exit 0. `so2.apworld` (80K), `Star Ocean The Second Story.yaml`. |
| Linux `--version` (game and setup, scratch `XDG_CONFIG_HOME`) | `Shattered Stars 0.02.00`, exit 0, no `config.ini` written. |
| Linux smoke on the packaged binary | from the worktree: 73 PASS / 0 FAIL / 1 SKIP (the git-ignored local fixtures are absent, as for 0.01.00); from the main tree with its fixtures: **86 PASS / 0 FAIL / 0 SKIP**, the same as the development build. |
| Windows under Wine (scratch prefix, headless, 4 GB scope) | `shattered-stars.exe --version` prints `Shattered Stars 0.02.00`, exit 0; a 300-vblank headless boot onto Arlia (map 18) registers its triggers (code words 491, 497, 478, ...) and stops after 300 vblanks, exit 0. |
| `.apworld` in a clean Archipelago (`custom_worlds`, no `worlds/so2`) | loads, `world_version` 0.1.1, no failed world loads; one seed from the built template (`Generate.py --seed 90200`): exit 0, `AP_<seed>.zip`, no traceback. |
| SHA-256 | in `docs/release_notes/0.02.00.md` and `scratchpad/rc-0.02.00-dist/SHA256SUMS`. |

The game's own verification for 0.02.00 (sweeps, Redux checks, Archipelago proofs and
end-to-end runs) is in the regions handoff log ("0.02.00 readiness"), the soulmaster handoff log
and the elgate handoff log.

## 9. Release candidate 0.02.50: verification run (2026-10-25, superseded by RC2 2026-10-26)

### RC2 (2026-10-26): winfix + appicon + escquit on top of the pushed c9a8200

The pushed commit `c9a8200` (tagged as the 0.02.50 RC and sent to the user's Windows machine)
came back with six findings from `docs/handoff/windows_check.md`; the user ruled: fix all of it
before tagging for real. Three agents did, all uncommitted on top of `c9a8200`:

- **winfix**: non-ASCII command-line arguments and `SS_*` variables no longer crash either
  program (UTF-16 argv/env on Windows); setup's Auto-detect message fixed; the original's
  `"pwnd->destroy"` debug print removed; every `[ap]`/`[port]`/`[mods]` line now reaches
  `shattered-stars.log`, bounded to 4 MB; the Archipelago client announces `0.6.8` instead of
  apclientpp's default `0.6.6`; the template YAML's two names (as built vs. as GitHub serves it)
  reconciled in the checklist and the RC script's `sums` phase.
- **appicon**: an original icon (7 sizes), a Windows version resource (`ProductName`/
  `FileDescription` "Shattered Stars" / "Shattered Stars Setup", `FileVersion` 0.02.50,
  `CompanyName`/`LegalCopyright` "DreamGlyph Studios") and manifest (`asInvoker`, `PerMonitorV2`,
  `supportedOS`, no `activeCodePage`), with the stray libwinpthread `"POSIX WinThreads for
  Windows"` VERSIONINFO removed from both `.exe`s; app ids `io.github.IronJade.ShatteredStars`
  (game) and `.Setup`; a Linux desktop entry (`desktop/`, `install-desktop-entry.sh`); both
  programs' window titles ("Shattered Stars" / "Shattered Stars Setup").
- **escquit**: Esc now opens a "Quit Shattered Stars?" overlay (Keep playing / Quit) instead of
  closing at once, by keyboard or gamepad; the game pauses (no vblank/SPU/net poll) and audio is
  paused, not stopped; the window's close button and Alt+F4 still close immediately.
- **Coordinator**: both `LICENSE` files changed to "Copyright (c) 2026 DreamGlyph Studios"
  (MIT text unchanged; `archipelago.json`'s `authors` stays `["IronJade"]`); README.md and the
  wiki gained a "Written with AI" passage.
- **This RC2 pass**: `apworld/so2/docs/setup_en.md` now names both the built template
  (`Star Ocean The Second Story.yaml`) and the name GitHub gives the download
  (`Star.Ocean.The.Second.Story.yaml`), since the apworld rebuild below ships that doc; folded
  winfix's, appicon's and escquit's Windows recheck items into one "Recheck for 0.02.50 RC2"
  list in `docs/handoff/windows_check.md`.

Built and checked by `scratchpad/release/rc-0.02.50.sh all` again (2026-10-26, 00:31-00:53; log,
results and artifacts in `scratchpad/release/rc-0.02.50-dist/`, appended to the same
`summary.txt` as the first run — the first run's 21:59-22:20 lines above are still in that file
as history). Source: a fresh snapshot (disposable worktree at HEAD `c9a8200` + `git diff HEAD
--binary`, 41 files changed/1208 insertions/177 deletions + 29 untracked files copied in). At
the end the main tree's `engine/`, `apworld/`, `tools/`, `mods/` and `LICENSE` were confirmed
unchanged since the snapshot (tree id `5605ab1c0fa4ce01`). The worktree and scratch Archipelago
installs were removed afterwards. Nothing was tagged, committed, pushed or published, and no
server outside 127.0.0.1 was contacted. **Every phase exited 0; no FAIL anywhere** (checked
individually against each phase's own log, not just the summary line).

| Check (section) | Result (RC2, 2026-10-26) |
|---|---|
| 2.1 `tools/build_apworld.sh` (a fresh copy of the Archipelago install with no `worlds/so2`, so it packaged the snapshot's `apworld/so2`) | exit 0. `so2.apworld` 159,070 bytes (+100 bytes: the `setup_en.md` download-name note), 27 files, no `__pycache__`; manifest `world_version` 0.2.0, `minimum_ap_version` 0.6.8, `authors` IronJade, `version` / `compatible_version` 7 (added by the packaging). Template `Star Ocean The Second Story.yaml`, 21,341 bytes (unchanged). |
| 2.2 the packaged world in a clean install (`custom_worlds/so2.apworld`, no `worlds/so2`) | loads: `world_version` 0.2.0, no failed world loads, no manifest error. Same five seeds as the first run (`Generate.py --seed 90250`), each exit 0 with no traceback, same versions (1 / 1 / 2 / 3 / 2). `check.py` over the five: 0 errors (same location/progression ranges as the first run — the apworld's logic is unchanged, only its docs). `MultiServer.py` on the multiworld: "Hosting game at 127.0.0.1:38491", the world's embedded data package loaded; stopped by its PID. |
| 2.3 port end to end, the packaged Linux binary against the packaged world | **37 / 37 PASS** (so2: character items, first drops, traps, open world with full shops, the shop slot's item and check, the goal, the v2 Iseria Queen goal) and **15 / 15 PASS** (hub: New Game, the key received, the locked door, Arcmene's chamber, the battle, the check, the tracker) — both against the RC2 binary (Esc-quit-confirm, the new icon/version resource, and the UTF-8 argument/env handling all compiled in; none of the e2e scripts touch Esc or the icon, so no behavior difference from the first run was expected or seen). |
| 2.4 the world's tests and Archipelago's general tests (`worlds/so2` linked to the snapshot) | `worlds/so2/test`: **380 passed** (29323 subtests). `test/general`: **379 passed** (117753 subtests). (Subtest counts vary a little run to run; both still fully green.) |
| 2.5 the apworld regression | the same 48-seed batch: 48 / 48 exit 0; `check.py` **0 errors** on 48 + 48 seed files (1939 and 1971 progression items in SO2 locations, identical to the first run). |
| 2.6 `tools/build_linux.sh --container` | exit 0. `ShatteredStars-0.02.50-linux-x86_64.tar.gz`, **9,407,748 bytes, 39 entries** (grew from 9: now also `desktop/` with two `.desktop` files + `icons/hicolor/{16,24,32,48,64,128,256}/apps/` + the scalable SVG, and `install-desktop-entry.sh`). glibc floor `2.36`. `--version`: `Shattered Stars 0.02.50` / `Shattered Stars Setup 0.02.50`, exit 0, no `config.ini` written. `LICENSE` reads "Copyright (c) 2026 DreamGlyph Studios"; `desktop-file-validate` on both `.desktop` files: exit 0. |
| 2.6 smoke on the packaged Linux binary (from the main tree, with its fixtures) | **282 PASS / 0 FAIL / 0 SKIP** (unchanged). |
| 2.6 `tools/build_windows.sh` (`MINGW_SSL_SYSROOT` set) | exit 0. `ShatteredStars-0.02.50-win64.zip`, **7,800,956 bytes**, 9 entries: `shattered-stars.exe`, `setup.exe`, `SDL3.dll`, `README.md`, `LICENSE`, `THIRD-PARTY-LICENSES.txt`, `discs/`. Both `.exe`s re-inspected with REA's `inspect_pe_resources`: one `RT_GROUP_ICON` (7 images, 16-256 px), one `RT_VERSION` and one `RT_MANIFEST` resource each; UTF-16LE strings confirm `ProductName`/`FileDescription` "Shattered Stars" / "Shattered Stars Setup", `FileVersion`/assembly version `0.02.50`/`0.2.50.0`, `CompanyName`/`LegalCopyright` "DreamGlyph Studios", app ids `io.github.IronJade.ShatteredStars`(`.Setup`); manifest strings `asInvoker`, `PerMonitorV2`, `supportedOS`; **zero** occurrences of `"POSIX WinThreads"` in either file. |
| 2.6 Windows under Wine (scratch prefix `scratchpad/release/rc-wine/wineprefix`; no display: DISPLAY unset, `WAYLAND_DISPLAY` pointed at nothing, the null display driver, winedbg and its crash dialog off; 4 GB scopes) | `shattered-stars.exe --version`: `Shattered Stars 0.02.50`. A 300-vblank headless boot onto Arlia (map 18): its triggers register (26 trigger lines), "stopped after 300 vblanks", exit 0. |
| The engine suites (engine/build-release2, Linux) | `ss_tests` **103 / 0** (was 99 — 4 new `utf8_*` cases), `ss_game_tests` **193 / 0** (was 182 — 11 new: app_icon ×3, quit_prompt ×5 via window tests, the game-loop Esc cases, utf8/launcher-log cases). |
| The Windows suites under Wine (no Archipelago server, so its network cases skip) | `ss_tests.exe` **103 / 0**, `ss_game_tests.exe` **194 / 0** (one more than Linux: the Windows-only `launcher_log_without_a_console` case). Zero `FAIL` in either log; the only skips are the optional region-disc and AP-server cases, as expected headless. |

**What the Windows suites found** (the first RC, 21:11-21:31, had every step green but
`ss_game_tests.exe` 182 / 2 failed; details in docs/handoff/release.md, "Status 21:58"):
- **A real Windows bug, fixed for this release:** `psx::MemoryCard::save()` named its temporary
  file through `path::string()`, which is UTF-8 on Windows while a narrow file name is read in the
  ANSI code page. With a non-ASCII letter anywhere in the card's path (a Windows user name, a
  custom card path, a seed's slot name) every save stayed in memory only. Present since 0.01.00;
  now the temporary name is a `std::filesystem::path` (engine/src/psx/memory_card.cpp).
- Two test-order effects (the Windows build registers the cases in another order): the program
  image cache kept an empty image when asked before a disc was open, so a later field test
  crashed (engine/src/game/common/program_image.cpp no longer caches without a disc; not
  reachable in the game), and the hub story-view test inherited a queued fade (the test now
  empties it).

SHA-256, RC2 (`scratchpad/release/rc-0.02.50-dist/SHA256SUMS`; also in the release notes).
**Every sum changed from the first run** (winfix + appicon + escquit + the LICENSE/README/
setup_en.md changes are all in these artifacts now):
```
22c42eed449683a4fe0fd363f0b383bdaf704729be475defcbfe6ae64e8c6f1b  ShatteredStars-0.02.50-linux-x86_64.tar.gz
1d573a50e0c4ed8f1753496fa46884cc8e8e97820292f6eae75d8e4aef639e85  ShatteredStars-0.02.50-win64.zip
aa57e9d991f2aaf97e4517f43a9139d46e1aa013730a8aef5061bf1e48780cf5  so2.apworld
adada8d01d8bc01b568968124146fb8f02057b9c165575e50a39caff5d5b8914  Star.Ocean.The.Second.Story.yaml
```
(The template YAML's sum is unchanged — its content didn't move, only the apworld packaging
around it did.)

**First run (2026-10-25, c9a8200 only, superseded):**
```
336faf3a33417d3a55dc0037c2025bf2aff23b43d70392a40638a3e8c3ae21f8  ShatteredStars-0.02.50-linux-x86_64.tar.gz
44f08f3a21c0fc05e8406ee0be0bbdbbe085909dfa5b5f4200ef9d196c5e3932  ShatteredStars-0.02.50-win64.zip
bac356cc78428bc5d557ed873450a86cb690fff1e5f2a6d1337e7a0e0be0e298  so2.apworld
adada8d01d8bc01b568968124146fb8f02057b9c165575e50a39caff5d5b8914  Star Ocean The Second Story.yaml
```

Release notes for players: [`docs/release_notes/0.02.50.md`](../release_notes/0.02.50.md).

**Publishing (the user's; not run).** The artifacts above were built from the RC2 snapshot, so
they match the tree the user commits if that commit takes the working tree as it stood at
00:31 on 2026-10-26 (the `engine/ apworld/ tools/ mods/ LICENSE` part; docs may differ). To
check after committing, hash those sources' contents, which doesn't depend on the commit:

```sh
git ls-files -co --exclude-standard -z -- engine apworld tools mods LICENSE | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-16
# the RC2's sources: 96c00f03f8432efb (630 files)
# (the first run's sources, superseded: c3a0177296fe677d, 618 files)
```

If it prints anything else, rerun `bash scratchpad/release/rc-0.02.50.sh all` before publishing.
Then:

```sh
D=scratchpad/release/rc-0.02.50-dist
cp "$D/Star Ocean The Second Story.yaml" "$D/Star.Ocean.The.Second.Story.yaml"   # the name GitHub would give it
gh release create v0.02.50 \
  "$D/ShatteredStars-0.02.50-linux-x86_64.tar.gz" \
  "$D/ShatteredStars-0.02.50-win64.zip" \
  "$D/so2.apworld" \
  "$D/Star.Ocean.The.Second.Story.yaml" \
  --draft --target main \
  --title "Shattered Stars 0.02.50" \
  --notes-file docs/release_notes/0.02.50.md
```

`--draft`: nothing goes live until the draft is published; `--target main` puts the tag on the
pushed `main`. The wiki's pages for 0.02.50 are uncommitted in `scratchpad/wiki` (the "wiki"
agent's log, the wiki handoff log), to push with the release.

## 10. so2.apworld 0.2.1: verification run (2026-10-26, agent ncpfix; redone 2026-10-27, agents skilllogic2, hellseed2 and benchworld)

The North City and Noel's House passes at the start unless the teleporter and expel_progression
are both on, skillsanity's guild checks and learn levels may hold progression, the hell seed
(`encounter_rate` 0 with killer-move checks or learn points: the arena's fee waived by a patch
line, `hell_seed`, the arena rules), and the bench (the `bench` option, on by default: 12 places
in open world with character_items and hub mode, `options.bench`, seed version 4)
(docs/ap/apworld.md "Changes"; the ncpfix, skilllogic, hellseed and
benchworld handoff logs). Shattered Stars 0.02.50 plays 0.2.1 seeds without the bench (the port gives a key
and a character from a guild check and from learn points, the skilllogic handoff log, "Step 3"; the fee line is
an ordinary version-1 patch); a bench seed is version 4 and needs 0.02.51, which ships with it
(the bench, party-bench merged at 319f5ba) and adds the hell seed's warning (setup's Archipelago
page, the log). Notes: docs/release_notes/0.2.1.md (draft). The run below is benchworld's, from
HEAD 64a7d20 (hellseed2's from 674e81b: 182160 bytes, 634 / 379 / 687 tests, sums 8a66353b... /
45fa58f2..., superseded; skilllogic2's from 9a44244: 173130 bytes, sums bdca8abe... /
8980fbe6..., superseded; ncpfix's first: 166651 bytes, sums 657098e9... / 21d81e67...,
superseded).

| Check | Result |
|---|---|
| `tools/build_apworld.sh dist/apworld-0.2.1` (benchworld, HEAD 64a7d20) | `so2.apworld` (186265 bytes, 31 files: HEAD's 30 tracked files of apworld/so2 byte for byte and the merged `archipelago.json`: `world_version` 0.2.1, `compatible_version` 7, `version` 7; `test/` in, no `__pycache__`) and `Star Ocean The Second Story.yaml` (24325 bytes, with `bench` in "Characters and items") |
| Packaged world alone (benchworld: scratchpad/benchworld/pkg/run.sh, an AP copy without `worlds/so2`, the `.apworld` in `custom_worlds`; deleted after) | loads by zipimporter as 0.2.1, `failed_world_loads` empty; Generate.py at seed 777, exit 0, one `.apso2` each, nothing matching error / traceback / exception: the template's defaults solo (story: version 1, no `bench`); open world with character_items, Claude's game, starting_character any, Leon excluded (**the bench: version 4, `bench` true, 10 characters, the select 11**); the same with `bench: false` (version 2, 7 characters, the select 8); hub mode with a starting Ashton (version 4, `bench` true, 11 characters); the open-world player with ChecksFinder (version 4). Every `.apso2` and spoiler byte-identical to the dev world's. The open, no-bench and hub seeds load in the merged port (engine/build-benchworld): `[ap] seed ...`, no refusal |
| End to end on the merged port (benchworld: scratchpad/benchworld/e2e/run.sh, headless) | a generated bench seed (open world, character_items, Claude's game, starting Ashton, `start_inventory` the other 10 members; version 4): New Game (`--map 18`): Ashton slot 0, Claude slot 1; six fill the 8, Opera / Ernest / Noel / Chisato join the bench (places 8-11); the save's sidecar version 3 with the 4 places; loaded, Party Order: Leon (slot 7) held across R1's flip and swapped with Opera (place 8), Claude to the bench refused; closed `Ashton, Claude, Rena, Celine, Bowman, Dias, Precis, Opera \| bench: Leon, Ernest, Noel, Chisato`; saved and loaded again with that bench |
| `worlds/so2/test` (`-n 4`; benchworld) | **699 passed**, 50730 subtests; `python -m unittest`: 699, OK |
| `test/general` (`-n 4`; benchworld) | **379 passed**, 117745 subtests |
| `AP_TEST_WORLDS=so2 ... test/general worlds/so2` (benchworld) | **752 passed**, 56003 subtests |
| Batches (benchworld) | with `bench: "false"` appended to the sets' SO2 YAMLs: all 288 seeds identical to hellseed2's (480 .apso2 byte for byte, the spoilers but their "Bench: No" lines); as they are (the bench on by default): story_open, hub, solo_noexpel **96/96 each, 0 checker errors** (bench seeds 60 / 168 / 49; the checker knows the bench) |
| Bench fills (benchworld, fillrate3.py) | open world with character_items; + killer moves any and special_only; + expel / boss checks off and killer moves any; hub; hub + killer moves any and special_only: **0/100 FillError each**, all accessible and beatable |
| SHA-256 (benchworld) | `83af433fa5f17a09e59b29eae859efd9df945e3b3eaefc59b04d6749f019849f  so2.apworld`, `93c41087a011bb2e8c54419c3a7ca1c44d96b2ccd1c8583a4fb277728eb7d035  Star.Ocean.The.Second.Story.yaml` |

hellseed2's run (superseded by the above; kept for the record):

| Check | Result |
|---|---|
| `tools/build_apworld.sh dist/apworld-0.2.1` | `so2.apworld` (182160 bytes, 31 files: HEAD's 30 tracked files of apworld/so2 byte for byte, `hell.py` and `test/test_hell.py` among them, and the merged `archipelago.json`, `test/` in, no `__pycache__`; its manifest `world_version` 0.2.1, `compatible_version` 7, `version` 7) and `Star Ocean The Second Story.yaml` (the hell seed in the encounter_rate, killer-move and Skillsanity text) |
| Packaged world alone (a copy of the AP install in scratchpad/hellseed/pkg, no `worlds/so2`, the `.apworld` in `custom_worlds`; deleted after) | loads by zipimporter from `custom_worlds/so2.apworld` as 0.2.1, `failed_world_loads` empty; Generate.py, exit 0, one `.apso2` each, nothing matching error / traceback / exception: the template's defaults solo (seed 777); the solo_noexpel set at seed 75 (byte-identical to the dev batch's seed 75); seed 75's options solo at seeds 1-3; **seed 75's options with `encounter_rate: 0` at seeds 1-3 (hell seeds: `hell_seed` true, the fee line; the one from seed 1 loads in the 0.02.51 build and logs the warning)**; seed 75's options with ChecksFinder (777). The template, seed 75 (1-3) and ChecksFinder outputs are byte-identical (.apso2 and spoiler) to skilllogic2's package run |
| `worlds/so2/test` (`-n 4`) | **634 passed**, 44972 subtests; `python -m unittest`: 634, OK |
| `test/general` (`-n 4`) | **379 passed**, 117745 subtests |
| `AP_TEST_WORLDS=so2 ... test/general worlds/so2` | **687 passed**, 50303 subtests |
| `tools/ap_batch.sh story_open / hub / solo_noexpel`, seeds 1-96 | **96/96 each**, `ap_batch_check.py` 0 errors on 192 + 192 + 96 files (progression at guild checks / learn points 1046 / 1291, 1028 / 1625, 410 / 871); no seed rolls encounter_rate 0, and all 288 seeds (768 .apso2 and spoiler files) are byte-identical to skilllogic2's run (scratchpad/skilllogic/batch; that run against ncpfix's: every seed with no skillsanity player identical, docs/ap/apworld.md "Testing"). A solo set with `encounter_rate: 0` (scratchpad/hellseed/batch/players_hell, seeds 1-64): 64/64, 0 checker errors (55 hell seeds) |
| Hell-seed fills (killer0's fillrate3.py) | seed 75's options with `encounter_rate: 0`, + story_route claude, + killer_move_progression any: 0/100 FillError each, all accessible and beatable |
| SHA-256 | `8a66353b9a93066b1fb5144c0ec9fe77e212058e0eacd1a857df80419660f7cb  so2.apworld`, `45fa58f2d484fbb2e54ff89151e8faa7a9e69f0c265559878504576fba1ce0f9  Star.Ocean.The.Second.Story.yaml` |

**Publishing (the user's; not run).** Nothing tagged, pushed or uploaded. A world-only release
would attach the two files (the YAML copied to the dotted name first), for example:

```sh
D=dist/apworld-0.2.1
cp "$D/Star Ocean The Second Story.yaml" "$D/Star.Ocean.The.Second.Story.yaml"
gh release create <tag> "$D/so2.apworld" "$D/Star.Ocean.The.Second.Story.yaml" \
  --draft --target main --title "so2.apworld 0.2.1" --notes-file docs/release_notes/0.2.1.md
```

The tag's name is the user's to choose (the port's releases are `v0.0X.YY`).

**Released apart (2026-10-27):** the world is published as its own GitHub release, tag
`apworld-v0.2.1`, beside the port's `v0.02.51` (section 11 has both commands).

## 11. Release candidate 0.02.51 + so2.apworld 0.2.1 as two releases (2026-10-27, agent release251)

The user (2026-10-27) asked for the port and the world as **separate GitHub releases on the same
repo**, so either can be updated, and its notes edited, on its own. Two releases:

| Release | Tag | Title | Assets | Notes |
|---|---|---|---|---|
| The port | `v0.02.51` | Shattered Stars 0.02.51 | the Linux and Windows bundles + `SHA256SUMS` | `docs/release_notes/0.02.51.md` (marked Latest) |
| The world | `apworld-v0.2.1` | so2.apworld 0.2.1 | `so2.apworld`, `Star.Ocean.The.Second.Story.yaml` + `SHA256SUMS` | `docs/release_notes/0.2.1.md` (not Latest) |

The notes link each other by those tags; README.md and the wiki link `apworld-v0.2.1` for the world
(`releases/latest` stays the port's). To use other tag names: `grep -rn 'v0\.02\.51\|apworld-v0\.2\.1'
README.md docs/release_notes scratchpad/wiki` and the commands below.

Built and checked by `scratchpad/release/rc-0.02.51.sh all` (12:40-13:02; adapted from
rc-0.02.50.sh: builds with `JOBS=4`; the world is **not rebuilt**, dist/apworld-0.2.1 is section
10's package, checked file by file against the snapshot; log, results and the two upload sets in
`scratchpad/release/rc-0.02.51-dist/`). Source: the committed tree, a disposable worktree at
`12a3a6c` (the release-prep commits 2a35882 + 12a3a6c on 3bce4a2), no uncommitted or untracked
source; the main tree unchanged at the end (tree id `7b567444d1b8f613`). **Every step exited 0; no
FAIL in any log.**

| Check | Result (2026-10-27) |
|---|---|
| `tools/build_linux.sh --container` | exit 0. `ShatteredStars-0.02.51-linux-x86_64.tar.gz`, **9,484,490 bytes, 39 entries** (the versioned folder; `desktop/` + `install-desktop-entry.sh` as in 0.02.50), no game data (no disc image, card, seed or asset file in the list). `--version`: `Shattered Stars 0.02.51` / `Shattered Stars Setup 0.02.51`, no `config.ini` written. glibc floor `2.36`. `LICENSE` "Copyright (c) 2026 DreamGlyph Studios", `THIRD-PARTY-LICENSES.txt` present; `desktop-file-validate` on both `.desktop` files: exit 0 |
| smoke on the packaged Linux binary | **373 PASS / 0 FAIL / 0 SKIP** (282 at 0.02.50: the bench, hero-slot, pending, queue, door and Dias cases since) |
| `tools/build_windows.sh` (`MINGW_SSL_SYSROOT` set) | exit 0. `ShatteredStars-0.02.51-win64.zip`, **7,875,246 bytes, 9 entries** (`shattered-stars.exe`, `setup.exe`, `SDL3.dll`, `README.md`, `LICENSE`, `THIRD-PARTY-LICENSES.txt`, `discs/` with its note), no game data. Version resource of both `.exe`s: `FileVersion` 0.02.51, `CompanyName` DreamGlyph Studios; no "POSIX WinThreads" |
| Windows under Wine (scratch prefix, no display, 4 GB scopes) | `shattered-stars.exe --version`: `Shattered Stars 0.02.51`; a 300-vblank headless boot onto Arlia (map 18): 26 trigger lines, "stopped after 300 vblanks" |
| The world's package (dist/apworld-0.2.1, section 10) | `so2.apworld` 186,265 bytes: its 31 files equal the snapshot's 31 tracked `apworld/so2` files byte for byte, `archipelago.json` but for the packaging's `version` / `compatible_version` 7; no `__pycache__`; manifest `world_version` 0.2.1, `minimum_ap_version` 0.6.8, `authors` IronJade. Template 24,325 bytes |
| 2.2 the packaged world in a clean install | loads as 0.2.1, no failed loads, no manifest error. Seven seeds (`Generate.py --seed 90251`), each exit 0, no traceback: solo (template, v1), SO2 + ChecksFinder (v1), open world with every new option (**v4, bench**), the same with `bench: false` (v2), hub (**v4, bench**), story (v2), a hell seed (story, `encounter_rate: 0`, killer moves, `special_only`: v2, **`hell_seed` true**). `tools/ap_batch_check.py` over the seven: **0 errors**. The solo, hub and hell seeds generated again with the dev world: the `.apso2` files identical. `MultiServer.py` on the multiworld: "Hosting game at 127.0.0.1:38491", the embedded data package loaded; stopped by its PID |
| 2.3 e2e, the packaged Linux binary against the packaged world | so2 **37 / 37 PASS**, hub **15 / 15 PASS** |
| 2.4 / 2.5 pytest and batches | not rerun: the world's source is unchanged since section 10's runs (benchworld, 64a7d20: 699 / 379 / 752 passed; batches 96/96, 0 errors) |
| The engine suites (engine/build-release2, Linux) | `ss_tests` **103 / 0**, `ss_game_tests` **207 / 0** (23 optional cases skipped as at 0.02.50: region discs, AP server, reference logs) |
| The Windows suites under Wine | `ss_tests.exe` **103 / 0**, `ss_game_tests.exe` **208 / 0** (the Windows-only `launcher_log_without_a_console` the one more) |

SHA-256 (`scratchpad/release/rc-0.02.51-dist/port/SHA256SUMS` and `.../apworld/SHA256SUMS`; also in
the two release notes):
```
6021fd980bdadbbec7afafa17548a7a9e002acd754236e9ad52ea0f58e08903d  ShatteredStars-0.02.51-linux-x86_64.tar.gz
d75c22c29ff3059a686a4f44daec15f30fedff11b5c8f2ef2c82d74cda3d9d92  ShatteredStars-0.02.51-win64.zip

83af433fa5f17a09e59b29eae859efd9df945e3b3eaefc59b04d6749f019849f  so2.apworld
93c41087a011bb2e8c54419c3a7ca1c44d96b2ccd1c8583a4fb277728eb7d035  Star.Ocean.The.Second.Story.yaml
```

The RC's sources' content id (commit-independent; section 9's one-liner):
**`7a190be7a51cceae`** (646 files of engine/ apworld/ tools/ mods/ LICENSE). The commits after
12a3a6c are docs only; if the one-liner prints anything else at the tag, rerun the RC.

**Publishing (the coordinator's, after the user's checks; not run).** Both tags lightweight, on the
pushed `main` (as v0.01.00 - v0.02.50); the world's release first, so the port's ends up Latest:

```sh
D=scratchpad/release/rc-0.02.51-dist
git push origin main
C=$(git rev-parse HEAD)
git tag apworld-v0.2.1 "$C" && git tag v0.02.51 "$C"
git push origin apworld-v0.2.1 v0.02.51
gh release create apworld-v0.2.1 "$D/apworld/so2.apworld" "$D/apworld/Star.Ocean.The.Second.Story.yaml" \
  "$D/apworld/SHA256SUMS" --verify-tag --latest=false --title "so2.apworld 0.2.1" \
  --notes-file docs/release_notes/0.2.1.md
gh release create v0.02.51 "$D/port/ShatteredStars-0.02.51-linux-x86_64.tar.gz" \
  "$D/port/ShatteredStars-0.02.51-win64.zip" "$D/port/SHA256SUMS" --verify-tag --latest \
  --title "Shattered Stars 0.02.51" --notes-file docs/release_notes/0.02.51.md
(cd scratchpad/wiki && git add -A && git commit -m "0.02.51 and so2.apworld 0.2.1: ..." && git push origin master)
```

## 12. Release candidate 0.03.00 + so2.apworld 0.2.2 + PopTracker pack 1.0.0 as three releases (to run; written 2026-10-27, agent releasedocs)

Tracker support (the tracker plan, Phase B; the build log docs/handoff/phaseb.md). The user
(2026-10-27) asked for the pack in **its own release**, as the world already has ("It's own release
works for me. We are already doing that with the apworld anyway."). Three GitHub releases on
IronJade/Shattered-Stars, each one's notes linking the other two:

| Release | Tag | Title | Assets | Notes | Latest |
|---|---|---|---|---|---|
| The world | `apworld-v0.2.2` | so2.apworld 0.2.2 | `so2.apworld`, `Star.Ocean.The.Second.Story.yaml`, `SHA256SUMS` | `docs/release_notes/0.2.2.md` | no |
| The pack | `poptracker-v1.0.0` | PopTracker pack 1.0.0 | `so2_shattered_stars_1.0.0.zip`, `SHA256SUMS` | `docs/release_notes/poptracker-1.0.0.md` | no |
| The port | `v0.03.00` | Shattered Stars 0.03.00 | the Linux and Windows bundles, `SHA256SUMS` | `docs/release_notes/0.03.00.md` | yes |

The pack zip's name and tag are fixed by its `versions.json` entry: `download_url` =
`https://github.com/IronJade/Shattered-Stars/releases/download/poptracker-v1.0.0/so2_shattered_stars_1.0.0.zip`
(tools/build_poptracker.sh's default tag). The name has no spaces, so GitHub keeps it as it is. To
use other tag names: `grep -rn 'v0\.03\.00\|apworld-v0\.2\.2\|poptracker-v1\.0\.0' README.md
docs/release_notes scratchpad/wiki`, the commands below, and `build_poptracker.sh build --tag`.

**maptab's work is in** (the maptab handoff log, done 2026-10-27): the map_names.csv `area`
audit, the pack's room maps, door sections, live door panel and click switching, and Universal
Tracker's map tab. The rows below, filled in by this RC (agent "rc030") from maptab's log, were
marked **TODO (maptab)**. The release itself still needs the user's go-ahead (phaseb.md:
"release only with the user's go-ahead").

### 12.1 Version numbers (B7b)

| What | Now | For this release | Where |
|---|---|---|---|
| The port | 0.02.51 | **0.03.00** | `engine/CMakeLists.txt` `project(... VERSION ...)` and its comment on the .rc fields (0,3,0,0); README.md (the badge, the two package names under "Download"); engine/README.md (the example package name); section 1's table |
| The world | 0.2.1 | **0.2.2** | `apworld/so2/archipelago.json` `world_version` and `world.py` `APWORLD_VERSION` (keep the two equal) |
| The pack | 1.0.0 | 1.0.0 | `tools/ap_poptracker.py` `PACK_VERSION` (already 1.0.0); manifest min / target PopTracker 0.36.0 (`POPTRACKER_VERSION`) |
| `ut.LOGIC_VERSION` | 1 | **1** (unchanged) | apworld/so2/ut.py: the logic is 0.2.1's (rulebuilder's 93,083,978 truth values, every batch identical; apworld.md "Trackers", "When to bump") |
| The wiki | | already written for 0.03.00 / 0.2.2 / 1.0.0 | scratchpad/wiki, uncommitted (the releasedocs handoff log) |

The world's bump changes each `.apso2` in one field only, `apworld_version` ("0.2.1" -> "0.2.2"),
as 0.2.0 -> 0.2.1 did. The seed's own `version` (1-4) is unchanged: Shattered Stars 0.02.51 plays
every 0.2.2 seed.

### 12.2 Builds

```sh
JOBS=4 tools/build_linux.sh --container        # dist/ShatteredStars-0.03.00-linux-x86_64.tar.gz
JOBS=4 MINGW_SSL_SYSROOT=scratchpad/setup/mingw-ssl/usr/x86_64-w64-mingw32/sys-root/mingw \
  tools/build_windows.sh                       # dist/ShatteredStars-0.03.00-win64.zip
tools/build_apworld.sh dist/apworld-0.2.2      # so2.apworld + "Star Ocean The Second Story.yaml"
tools/build_poptracker.sh all                  # dist/poptracker/: so2_shattered_stars_1.0.0.zip, its
                                               # .sha256, versions.json, ut_name_mapping.json; then
                                               # the pack checker and the pack's tests
```

- **One layout for the port and the pack.** The port compiles `docs/ap/tracker_maps.json` in
  (generated/tracker_layout_data.cpp) and the pack draws its placeholders from the same file, so
  both must be built from the same commit. First `SS_DISC1=<disc 1 .cue> python3
  tools/ap_tracker_maps.py --check` (the committed layout is the generator's output). The port
  also compiles `tools/poptracker/boards.json` in (the pack's boards, which it draws into the
  Universal Tracker zip: docs/ap/tracker_maps.md "The Universal Tracker zip"); `tools/build_poptracker.sh
  all` fails (the pack's `TestUT.test_boards_for_the_port`) when the committed copy is not the
  generator's (`tools/ap_poptracker.py --boards-out tools/poptracker/boards.json`, then rebuild the port).
- **The pack is generated from the tree's world.** `build_poptracker.sh` reads the world's tables
  through `$repo/scratchpad/ap` (worlds/so2 linked to `$repo/apworld/so2`). A worktree has no
  `scratchpad/`, so the RC script either links it in for the build (and points worlds/so2 at the
  worktree's apworld/so2) or builds in the main tree at the RC's commit. The zip is reproducible
  (sorted, fixed timestamps), so both ways must give the same sha256.
- The world's package now also holds `ut.py`, `logic.py`, `tracker_export.py`, `story.py`,
  `ut_map.py` and `apworld/so2/tracker/` (`maps/maps.json`, `locations/locations.json`,
  `layouts/maps.json`, `map_pages.json`, the UT map tab's own data) and their tests:
  **281,228 bytes at maptab's build** (world_version still 0.2.1 at that point, not yet bumped;
  the map tab added about 56 KB over storyaware's 223,553; 186,265 for 0.2.1 at ncpfix's);
  **281,557 bytes at utzip2's build** (0.2.2, `ut_map.py`'s dialog text naming both zips). The
  RC's own build (12.4) has the exact size at 0.2.2.
- The pack at storyaware's build: 401 files, 287 items, about 3 MB zipped. **maptab's room
  placeholders and doors** (794 room maps, 1,775 door locations) brought it to **1,201 files,
  5,559,587 bytes zipped (about 5.56 MB)** — +2.56 MB over storyaware's, close to mapdoors'
  ~2.5 MB estimate and well under the ~20 MB budget.

### 12.3 Verification: commands and expected results

The expected numbers are the last ones measured, with the agent that measured them. maptab's
work changes some; its log gives the new ones.

**The world** (in scratchpad/ap/Archipelago, worlds/so2 linked to the RC's apworld/so2;
`SKIP_REQUIREMENTS_UPDATE=1` on every line):

| Check | Command | Expected |
|---|---|---|
| The world's tests | `../ap-venv/bin/python -m pytest -q -n 6 worlds/so2/test` | **882 passed**, 3 skipped (67988 subtests; storyaware); unchanged by maptab's area audit and UT map tab (its own rerun also got 882 passed, 3 skipped) |
| Scoped general | `AP_TEST_WORLDS=so2 ../ap-venv/bin/python -m pytest -q -n 6 test/general worlds/so2` | **935 passed**, 3 skipped (73299 subtests; storyaware) |
| AP's general tests | `../ap-venv/bin/python -m pytest -q -n 6 test/general` | **379 passed** (117757-117763 subtests: other worlds' random options; rulebuilder) |
| The slow tests | `SO2_SLOW_TESTS=1 ../ap-venv/bin/python -m pytest -q -n 6 worlds/so2/test` | **885 passed** (68692 subtests, about 35 s; storyaware): TestUTManySeeds 320 rooms (16 mixes x 20) matching UT's rebuild at every sphere, TestTrackerManySeeds 320 rooms (the reference evaluator = the world's logic at every sphere), TestStoryManySeeds 60 rooms (UT = the reference at 27 story positions) |
| The batches | `tools/ap_batch.sh story_open OUT/story_open 1 96`, the same for `hub` and `solo_noexpel` (from the repo root) | **96/96 each**, `tools/ap_batch_check.py` 0 errors |
| The same seeds as 0.2.1 | the three sets from the released world (`git archive apworld-v0.2.1 apworld/so2` in a `cp -as` farm of scratchpad/ap/Archipelago with **real copies** of the top-level scripts and host.yaml: the baseline trap, the utworld handoff log) against the RC's; a comparer adapted from scratchpad/storyaware/identity.py | 480 `.apso2` equal but `apworld_version`; 288 spoilers byte for byte (they name only Archipelago's version); every multidata equal once each Star Ocean slot's new slot data keys are taken out: `logic_version` 1, `exclude_locations`, `tracker` (format 1; with `story` in the story-mode slots). The checker's output the same. The UT
map tab adds **no new slot-data key** (maptab: batches identical including multidata); it reads
the new host.yaml group `so2_options: ut_pack_path` instead (set by UT's one-time file-dialog
prompt, not part of the seed) |
| The real Universal Tracker | `env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS SKIP_REQUIREMENTS_UPDATE=1 prlimit --as=$((8<<30)) scratchpad/ap/ap-venv/bin/python tools/ap_ut_check.py --tracker scratchpad/ap_ut/tracker.apworld --apworld dist/apworld-0.2.2/so2.apworld --ut-dir scratchpad/release/ap_ut --port 38441` (from the repo root) | **"UT MATCHES THE WORLD", 59 comparisons** (storyaware): four slots, fresh and after each of the first 4 spheres, through UT's CLI and TrackerCore; the logic_version 2 room refused; step 5, the story slot following a story key Set as the port sets it: 142, 204, 227, 319, 426, 450, 532, 532, 578 in logic at chapters 3, 9, 12, 20, 36, 41, 58, 60/3000, 60/4000, and 578 with `enforce_deferred_entrances: "off"`. `--no-story` skips step 5; `--spheres 1` gives 35. No dialog, no process left |
| UT's `/explain` (optional) | `scratchpad/ap/ap-venv/bin/python scratchpad/rulebuilder/ut_explain.py --apworld dist/apworld-0.2.2/so2.apworld` | 64 locations and events of four slots, each printed as its rule, UT exit 0 (rulebuilder) |

`scratchpad/ap_ut/tracker.apworld` is Universal Tracker 0.3.6's release asset (sha256 39e9bc0f...5cb750,
the release's digest): `gh release download Tracker_v0.3.6 --repo FarisTheAncient/Archipelago
--pattern tracker.apworld --dir scratchpad/ap_ut`. `tools/ap_ut_check.py` makes its own
Archipelago copy under `--ut-dir` (an empty `Players` folder, UT's `player_files_path` set to it,
`XDG_RUNTIME_DIR` at `<ut-dir>/run`), so no folder dialog can open.

**The pack** (from the repo root):

| Check | Command | Expected |
|---|---|---|
| Build and check | `tools/build_poptracker.sh all` | PopTracker/pack-checker v1.7.0 `--strict`: **"Validated 11 files"** for the zip and for the folder (was 10; maptab's `doors.lua`), no warning, exit 0; versions.json valid against the packlist's schema and matching the zip; **1,201 files, zip 5,559,656 bytes** (utzip's build with the yellow doors; 5,559,587 at maptab's); the tests (next row) OK |
| The pack's tests | `SKIP_REQUIREMENTS_UPDATE=1 scratchpad/ap/ap-venv/bin/python tools/ap_poptracker_test.py --pack dist/poptracker/so2_shattered_stars` | **43 OK** (utzip2: + `TestUT.test_boards_for_the_port`; 42 at utzip: + `TestDoorsLua.test_door_colour_is_no_checks`; 41 at maptab, 24 at storyaware): the same ids / progression / layout / JSON / Lua-in-Lua-5.4 / evaluator (16 rooms x 246 item states, 243,885 section evaluations) / story-awareness (15 positions) checks, plus `TestRooms` (5: the room placeholders, the layout) and `TestDoorsLua` (5: the Lua door panel, `BadgeText`, the click handler's reset + `ActivateTab`), and `TestUT` (5: the UT map tab's new files; the boards the port draws for its UT zip = the pack's) |
| PopTracker itself, headless | `env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS SKIP_REQUIREMENTS_UPDATE=1 prlimit --as=$((8<<30)) scratchpad/ap/ap-venv/bin/python tools/ap_poptracker_smoke.py --zip dist/poptracker/so2_shattered_stars_1.0.0.zip --scratch scratchpad/release/smoke --port 38391` | **37/37** (storyaware): both variants load and connect, the slot data, items, checked sections, chapter 12, the tabs from `so2_map_*` {"map": 26}; the logic equal to the world's at three points (115, 99, 40 in logic); step 7 (the story room, on PORT + 1): Normal / Inspect fresh 583 / 0, chapter 3 37 / 546, 12 108 / 475, 36 291 / 292, 60 step 3000 393 / 190, 60 step 4000 583 / 0; no Lua error or warning. `--no-story` skips step 7 (24/24). PopTracker 0.36.0 is the AppImage in scratchpad/packgen/poptracker (minisign-verified). maptab's final smoke (1,201 files, the door step included): 44/44; utzip's (the door step also checks the door's colour, SequenceBreak yellow): **45/45** |
| The door click in PopTracker (maptab) | the smoke's door step | clicking door A of map 26 (as a popup click does) resets its section and opens the destination room's tab path (`Expel > Arlia Village > Arlia Village > 18`), without marking it cleared; map 26 from `so2_map_*` opens its own room tab too; the door is SequenceBreak (yellow: the colour no check uses; utzip). Counted in the **45/45** above |

The smoke runs PopTracker with a cleared environment (no display, no D-Bus, an empty
`XDG_RUNTIME_DIR`). PopTracker also fetches the community pack list and the pack's `versions_url`
(read-only GETs); until versions.json is on main it logs "Could not fetch", which is harmless.

**The port** (a build of the RC's tree, engine/build-rc; `SS_DISC1` = the NA disc 1 `.cue`; every
game process under tools/memcap.py, headless, a scratch `XDG_CONFIG_HOME` and `HOME`):

| Check | Command | Expected |
|---|---|---|
| The tracker keys | `SS_TEST=ap_ds engine/build-rc/ss_game_tests` | **9/9** (dskeys) |
| The map render | `SS_TEST=tracker_ SS_DISC1=... engine/build-rc/ss_game_tests` | **14/14** (utzip2: + `tracker_zip_writer`, `tracker_ut_zip_path`, `tracker_ut_extras`, `tracker_disc_ut_zip`: the Universal Tracker zip; 10/10 at setupthread: the layout, the disc command, rooms, progress, the job's stop); unchanged by maptab's area audit (794 rooms now, was 793 — maptab's own rerun still got 10/10; no count in `engine/tests/test_tracker_maps.cpp` needed to change) |
| All the suites | `SS_DISC1=... engine/build-rc/ss_game_tests`, `engine/build-rc/ss_tests` | **230 / 0** (utzip2: the 4 zip tests; 226 at setupthread), **103 / 0** |
| The net client | `tools/ap_test_server.sh start 38281`; `SS_TEST=ap_net SS_AP_SERVER=ws://localhost:38281 engine/build-rc/ss_game_tests`; `tools/ap_test_server.sh stop` | **5/5** (the wss case skipped without `SS_AP_SERVER_WSS`) |
| The render, end to end | `SS_DISC1=... python3 tools/ap_tracker_maps_test.py engine/build-rc/shattered-stars --scratch scratchpad/release/maps` | all checks pass (setupthread; maptab's area audit brought it from 861 to **862 images**, 66 area + 2 world + 794 rooms), rendered twice with the same bytes, nothing else written, XDG untouched, the default folder under a scratch HOME's PopTracker; about 21 s and 135 MB a render. utzip2: **34 checks**, with the Universal Tracker zip: beside each folder (`<HOME>/PopTracker/user-override/so2_shattered_stars_ut.zip`, DIR's parent), `--ut-zip FILE`, `--no-ut-zip` (one room each); Python's zipfile reads it (testzip), stored, dated 1980-01-01, sorted, exactly the 862 images + 5 boards + the marker (868 files), the map entries the rendered PNGs byte for byte, both full runs' zips the same bytes (134,897,214 bytes) |
| The four releases' discs (optional) | `scratchpad/mapdoors/regions.sh` | NA, EU, FR and DE disc 1 render the same 862 images byte for byte (mapdoors; was 861 before maptab's area audit) |
| End to end | `SS_DISC1=... tools/ap_e2e_so2.sh engine/build-rc/shattered-stars`; `SS_DISC1=... tools/ap_e2e_hub.sh engine/build-rc/shattered-stars` | **49 PASS / 0 FAIL** (dskeys; step 9, the tracker keys: a watcher sees {"map":18}, {"map":26}, then world-map positions at most every 30 vblanks; Get after the runs gives both keys of Claude and Dias under the Connected packet's team / slot); hub **15 / 15** |
| End to end, packaged | the RC's copies of the two scripts against dist/apworld-0.2.2/so2.apworld and the packaged Linux binary. **Derive them again from today's tools/ap_e2e_so2.sh**: scratchpad/release/rc251_e2e_so2.sh predates step 9 | so2 **49 / 49**, hub **15 / 15** |
| Smoke | `SS_DISC1=... tools/smoke_field_events.sh <the packaged Linux binary>` | **373 PASS / 0 FAIL / 0 SKIP** (from the main tree, with its fixtures) |
| Setup's button, headless | `scratchpad/setupthread/setup_runs.sh engine/build-rc/shattered-stars-setup scratchpad/release/setup A B C D` (the display variables unset, `XDG_RUNTIME_DIR` at scratch) | A: a mid-render screenshot (the bar "150 of 862 images", "Rendering room ..."); B: "Done: 862 tracker map images were written to ...", the PNGs = the CLI's bytes; C (no disc): refused, nothing written; D (closed mid-render): no `.part` file, config.ini unchanged; each rc 0 (setupthread; counts updated for maptab's area audit, was 861). B compares with scratchpad/setupthread/base_manifest.txt: make a new manifest from the CLI render (the layout changed). utzip2: B also shows "For Universal Tracker: <...>/user-override/so2_shattered_stars_ut.zip", the hint and **Copy path** (the section scrolled back into view after the wait); the zip = the CLI's bytes; a run whose exit comes during the zip (scratchpad/utzip/setup_runs.sh M, Z): "Writing the zip for Universal Tracker..." and "K of 868 files" on screen, then no zip and no `.part` |
| ASan | a GCC `-fsanitize=address -fno-omit-frame-pointer` build as scratchpad/setupthread/build_san.sh (the libasan copy; tools/memcap.py runs it in a systemd scope with MemoryMax=6G) | no AddressSanitizer report in: `SS_TEST=ap_ds` 9/9, `SS_TEST=tracker_` 14/14 (10 before utzip2), test_ap_net 5/5, one full `--render-tracker-maps` (862 images, the same bytes, and the Universal Tracker zip), tools/ap_e2e_so2.sh with `ASAN_OPTIONS=detect_leaks=0` (49/49), setup runs A-D (portcheck, setupthread). Setup exits 1 under LeakSanitizer only for 248 bytes in 2 allocations under SDL_CreateWindow -> OFFSCREEN_GLES_LoadLibrary (Mesa), which predate this release |
| TSan (optional) | setupthread's recipe (GCC `-fsanitize=thread`, libtsan from `dnf download`, its Log at 18:00) | `SS_TEST=tracker_` 14/14 (10 before utzip2), setup A-D: no report (with `XDG_RUNTIME_DIR` at scratch: libdbus otherwise reports a lock-order inversion of its own) |
| Windows bundle | `tools/build_windows.sh` | exit 0; the 9 entries of 0.02.51's zip; both `.exe`s' version resource 0.03.00, DreamGlyph Studios, no "POSIX WinThreads" |
| Windows under Wine | a scratch prefix with the safe settings (the portcheck handoff log, "Conventions found"; scratchpad/setupthread/wine.sh adds a scratch `XDG_RUNTIME_DIR`) | `--version` "Shattered Stars 0.03.00"; the Arlia boot (26 trigger lines); `--render-tracker-maps DIR` 862 images byte-identical to the Linux render, and its `so2_shattered_stars_ut.zip` in DIR's parent byte-identical to the Linux zip (utzip2), and without DIR into `C:\users\<user>\Documents\PopTracker\user-override\so2_shattered_stars` (the zip beside it); `ss_game_tests.exe` `SS_TEST=tracker_` 14/14 (10 before utzip2); the full suites `ss_tests.exe` 103 / 0 and `ss_game_tests.exe` one more than Linux (the Windows-only `launcher_log_without_a_console`, as 207 / 208 at 0.02.51) |
| The map tabs (maptab) | the layout's `--check` after the area audit; the pack's room and door tests; UT's map tab loading headless | `tools/ap_tracker_maps.py --check`: up to date (794 rooms, 862 images); the pack's `TestRooms` / `TestDoorsLua` / `TestUT` above; `tools/ap_poptracker_utmap.py`: **16/16** (UT's own map loader headless, both against the repo world and the packaged `so2.apworld`; 15/15 before utzip2's check that every page's picture and the marker load through UT's own image loader), each with the pack zip and with the port's `so2_shattered_stars_ut.zip` (`--zip`, from the e2e or the setup run; not under a folder whose name ends in "zip": UT splits its picture paths at the first `zip/`) |

**Already proven, not rerun** (unless the code under them changes): AP off, no seed and offline
seeds byte-identical before and after the tracker keys and the render command (dskeys,
maprender, mapdoors: 12 headless runs, 20 files, 0 differ, scratchpad/mapdoors/ab.sh); the
0.2.0 rooms in UT (logicexport: 85 rooms, 51 equal, 29 conservative with the warning, 5 hell seeds
refused; the user's own room loads with the warning).

### 12.4 Sums and the upload sets

Three folders under `scratchpad/release/rc-0.03.00-dist/`, each with its own `SHA256SUMS`:
`port/` (the two bundles), `apworld/` (`so2.apworld`, the template copied to
`Star.Ocean.The.Second.Story.yaml`), `poptracker/` (`so2_shattered_stars_1.0.0.zip`). Keep the
RC's `versions.json` beside them (`rc-0.03.00-dist/versions.json`, not an asset). The pack's sum
must equal `dist/poptracker/so2_shattered_stars_1.0.0.zip.sha256` and versions.json's `sha256`.
Fill the sums into the three notes (0.03.00.md, 0.2.2.md, poptracker-1.0.0.md), and the RC's
content id (section 9's one-liner) into this section. Before tagging:
`grep -n 'TODO' docs/release_notes/0.03.00.md docs/release_notes/0.2.2.md docs/release_notes/poptracker-1.0.0.md`
prints nothing.

### 12.5 The Windows check

docs/handoff/windows_check.md "Recheck for 0.03.00", on the user's machine with the drafts (12.6).
setup.exe itself has never run under Wine (its render thread has, inside ss_game_tests.exe): its
button is first seen on Windows there.

### 12.6 Publishing (the coordinator's, after the user's checks and go-ahead; not run)

All three tags lightweight on the pushed `main`, as before. The releases as drafts first (the
Windows session downloads them with `gh release download TAG` and checks the sums); then
published world, pack, port, so the port's ends up Latest. Then the pack's `versions.json`, then
the wiki.

```sh
D=scratchpad/release/rc-0.03.00-dist
git push origin main
C=$(git rev-parse HEAD)
git tag apworld-v0.2.2 "$C" && git tag poptracker-v1.0.0 "$C" && git tag v0.03.00 "$C"
git push origin apworld-v0.2.2 poptracker-v1.0.0 v0.03.00
gh release create apworld-v0.2.2 "$D/apworld/so2.apworld" "$D/apworld/Star.Ocean.The.Second.Story.yaml" \
  "$D/apworld/SHA256SUMS" --verify-tag --draft --latest=false --title "so2.apworld 0.2.2" \
  --notes-file docs/release_notes/0.2.2.md
gh release create poptracker-v1.0.0 "$D/poptracker/so2_shattered_stars_1.0.0.zip" "$D/poptracker/SHA256SUMS" \
  --verify-tag --draft --latest=false --title "PopTracker pack 1.0.0" \
  --notes-file docs/release_notes/poptracker-1.0.0.md
gh release create v0.03.00 "$D/port/ShatteredStars-0.03.00-linux-x86_64.tar.gz" \
  "$D/port/ShatteredStars-0.03.00-win64.zip" "$D/port/SHA256SUMS" --verify-tag --draft \
  --title "Shattered Stars 0.03.00" --notes-file docs/release_notes/0.03.00.md
# after the Windows check and the user's go-ahead:
gh release edit apworld-v0.2.2 --draft=false --latest=false
gh release edit poptracker-v1.0.0 --draft=false --latest=false
gh release edit v0.03.00 --draft=false --latest
for t in apworld-v0.2.2 poptracker-v1.0.0 v0.03.00; do
  (mkdir -p "scratchpad/release/pub/$t" && cd "scratchpad/release/pub/$t" &&
   gh release download "$t" --repo IronJade/Shattered-Stars && sha256sum -c SHA256SUMS)
done
```

**The pack's update file, after its release is public** (the manifest's `versions_url` reads it
from main; committing it before the zip is downloadable would offer players a download that
fails):

```sh
cp scratchpad/release/rc-0.03.00-dist/versions.json tools/poptracker/versions.json
git add tools/poptracker/versions.json
git commit -m "PopTracker pack 1.0.0: versions.json on main for PopTracker's update check (release poptracker-v1.0.0)"
git push origin main
curl -sL https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/tools/poptracker/versions.json | head -20
curl -sL https://github.com/IronJade/Shattered-Stars/releases/download/poptracker-v1.0.0/so2_shattered_stars_1.0.0.zip | sha256sum
# = versions.json's "sha256" and poptracker/SHA256SUMS
```

From then on `tools/build_poptracker.sh build` puts each new version's entry first in front of the
committed file's (docs/ap/poptracker.md "Building"). This commit comes after the tags, so the
content id at the tags is the RC's.

**The wiki** (scratchpad/wiki, its own repo: the new Trackers page and the pages that link it;
the releasedocs handoff log lists them):

```sh
(cd scratchpad/wiki && git add -A && git commit -m "0.03.00, so2.apworld 0.2.2 and the PopTracker pack 1.0.0: trackers" && git push origin master)
```

**Not part of this release unless the user says so:** listing the pack in PopTracker's
`community-packs.json` (D7: a PR to black-sliver/PopTracker's `packlist` branch, public under the
user's name; docs/ap/poptracker.md "Distribution" has the fields).
