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
| The APWorld (`world_version`) | `0.1.1` | `apworld/so2/archipelago.json` (hand-edited; bump before each release) |
| Archipelago core this world targets (`minimum_ap_version`) | `0.6.8` | `apworld/so2/archipelago.json` |
| The port's own release version | **`0.02.00`** | `engine/CMakeLists.txt`'s `project(... VERSION 0.02.00)`; reaches the code as the `SS_VERSION` compile definition on `ss_core` (so every target that links it — the game, setup, `ss_tests`, `ss_game_tests` — sees the same string) |

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

The Archipelago client's version reporting (`engine/src/ap_net/client.cpp`): checked —
`ConnectSlot(info.slot, info.password, info.items_handling, tags)` is called with no `ver`
argument, so apclientpp's default `APCLIENTPP_VERSION_INITIALIZER` is sent. That `"version"`
field in the `Connect` packet **is** the Archipelago network protocol version the client
speaks (per the protocol: a compatibility check against the server), not a free-form
client/game version — there is no second, separate "client version" field anywhere in
`ap_net` to attach `SS_VERSION` to, and the task's rule not to touch the protocol version
means nothing here needed changing. `tags` carries only `"DeathLink"` when that option is on.

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
```
Expected (got this pass): `worlds/so2/test` — **95 passed, 7986 subtests**; `test/general` —
**379 passed, 116984 subtests**. No apworld failures appeared; see section 4 for the three
style-only fixes made after the upstream gap analysis, re-verified with the same counts.

### 2.5 This repo's own apworld regression (docs/ap/apworld.md, "Testing")
Already green at phase 4 (95 passed / 7986 subtests, AP general 379 / 116983, a 64-seed /
127-option-combination batch with 0 checker errors, `tools/ap_e2e_so2.sh` 25/25 against the
dev symlink) — re-run after any further apworld change:
```sh
SKIP_REQUIREMENTS_UPDATE=1 scratchpad/ap/ap-venv/bin/python -m pytest -q -n 8 worlds/so2/test   # in scratchpad/ap/Archipelago
scratchpad/enemy/ap5/batch/run_batch.sh <first> <last>; scratchpad/enemy/ap5/batch/check.py <out>   # 20+ seeds
SS_DISC1="<disc 1 .cue>" tools/ap_e2e_so2.sh
```
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
| Its options template | `dist/Star Ocean The Second Story.yaml` | `tools/build_apworld.sh` (Archipelago's `Options.generate_yaml_templates`) |
| Linux release | `dist/ShatteredStars-0.01.00-linux-x86_64.tar.gz` | `tools/build_linux.sh --container` |
| Windows release | `dist/ShatteredStars-0.01.00-win64.zip` | `tools/build_windows.sh` |

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
  2.6) — not done this pass since no engine behavior changed, only a rebuild.

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
