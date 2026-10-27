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
| The APWorld (`world_version`) | `0.2.1` | `apworld/so2/archipelago.json` (hand-edited; bump before each release) and `apworld/so2/world.py`'s `APWORLD_VERSION` (written into each seed's slot data as `apworld_version`; keep the two equal) |
| Archipelago core this world targets (`minimum_ap_version`) | `0.6.8` | `apworld/so2/archipelago.json`; the port's client announces the same version (`engine/CMakeLists.txt` reads it from there at configure time) |
| The port's own release version | **`0.02.50`** | `engine/CMakeLists.txt`'s `project(... VERSION 0.02.50)`; reaches the code as the `SS_VERSION` compile definition on `ss_core` (so every target that links it — the game, setup, `ss_tests`, `ss_game_tests` — sees the same string) |

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
Since 2026-10-26 (docs/handoff/aptests.md) worlds/so2/test follows Archipelago's testing guide
(docs/ap/apworld.md "Testing"); expected then: **430 passed, 3 xfailed** (the expel_progression
bug's reproducer, until its fix); since 0.2.1 (docs/handoff/ncpfix.md) **449 passed**, the
decorators gone and the mix `TestOpenWorldNoExpelProgressionNoBosses` added, each test under a
second (`--durations=30`); since skillsanity progression (still 0.2.1, docs/handoff/skilllogic.md)
**537 passed**. Expected (got this pass): `worlds/so2/test` — **95 passed, 7986 subtests**; `test/general` —
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
Desktop identity (from 0.02.50; `docs/handoff/appicon.md`): both `.exe`s carry one icon group
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
`scratchpad/setup/wine/game-release.sh`, `docs/handoff/setup.md`'s Verification commands
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
player's own disc images at run time (`docs/HANDOFF.md`). None of this was committed to git
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
| DeathLink boss-check wording | **MET** | Verified from the real scenes (`docs/handoff/deathlink.md`): boss checks record on a win, on time-up (the survival fights 516 / 520) and on the scripted defeat of the lose-allowed fights (204 / 517 / 530), never on an escape; the player docs say so. |

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
end-to-end runs) is in docs/handoff/regions.md ("0.02.00 readiness"), docs/handoff/soulmaster.md
and docs/handoff/elgate.md.

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
agent's log, docs/handoff/wiki.md), to push with the release.

## 10. so2.apworld 0.2.1 (the world alone): verification run (2026-10-26, agent ncpfix; redone 2026-10-27, agent skilllogic2)

A world-only release: the North City and Noel's House passes at the start unless the teleporter
and expel_progression are both on, and skillsanity's guild checks and learn levels may hold
progression (docs/ap/apworld.md "Changes"; docs/handoff/ncpfix.md, skilllogic.md). No port
build: Shattered Stars 0.02.50 plays 0.2.1 seeds (`apworld_version` is reference only; no new
seed field; the port gives a key and a character from a guild check and from learn points,
skilllogic.md "Step 3"). Notes: docs/release_notes/0.2.1.md (draft). The run below is
skilllogic2's, from HEAD 9a44244 (ncpfix's first run of this section: 166651 bytes, 449 / 379 /
502 tests, solo_noexpel 95/96, sums 657098e9... / 21d81e67..., superseded).

| Check | Result |
|---|---|
| `tools/build_apworld.sh dist/apworld-0.2.1` | `so2.apworld` (173130 bytes, 29 files: HEAD's 28 tracked files of apworld/so2 byte for byte and the merged `archipelago.json`, `test/` in, no `__pycache__`; its manifest `world_version` 0.2.1, `compatible_version` 7, `version` 7) and `Star Ocean The Second Story.yaml` (the new Skillsanity text) |
| Packaged world alone (a copy of the AP install in scratchpad/skilllogic/pkg, no `worlds/so2`, the `.apworld` in `custom_worlds`) | loads by zipimporter from `custom_worlds/so2.apworld` as 0.2.1, `failed_world_loads` empty, no manifest error; Generate.py, exit 0, one `.apso2` each, nothing matching error / traceback / exception: the template's defaults solo (seed 777); the solo_noexpel set at seed 75 (the reproducer: byte-identical to the dev batch's seed 75, 35 learn levels holding progression); seed 75's rolled options solo at seeds 1-3; those with ChecksFinder (777) |
| `worlds/so2/test` (`-n 4`) | **537 passed**, 37687 subtests; `python -m unittest`: 537, OK |
| `test/general` (`-n 4`) | **379 passed**, 117750 subtests |
| `AP_TEST_WORLDS=so2 ... test/general worlds/so2` | **590 passed**, 42996 subtests |
| `tools/ap_batch.sh story_open / hub / solo_noexpel`, seeds 1-96 | **96/96 each**, `ap_batch_check.py` 0 errors on 192 + 192 + 96 files (progression at guild checks / learn points 1046 / 1291, 1028 / 1625, 410 / 871); against ncpfix's 0.2.1 run (scratchpad/ncpfix/v021) every seed with no skillsanity player byte-identical (6 / 3 / 20), every changed one (90 / 93 / 76) with one (docs/ap/apworld.md "Testing") |
| SHA-256 | `bdca8abebf32b2d4d5db35786443634e883d3463f7d7f6f7f78906283ab396b4  so2.apworld`, `8980fbe61ea55c6b057f53f380c40c98dc3c7bd1f1d9a641105eb844c9d6b71c  Star.Ocean.The.Second.Story.yaml` |

**Publishing (the user's; not run).** Nothing tagged, pushed or uploaded. A world-only release
would attach the two files (the YAML copied to the dotted name first), for example:

```sh
D=dist/apworld-0.2.1
cp "$D/Star Ocean The Second Story.yaml" "$D/Star.Ocean.The.Second.Story.yaml"
gh release create <tag> "$D/so2.apworld" "$D/Star.Ocean.The.Second.Story.yaml" \
  --draft --target main --title "so2.apworld 0.2.1" --notes-file docs/release_notes/0.2.1.md
```

The tag's name is the user's to choose (the port's releases are `v0.0X.YY`).
