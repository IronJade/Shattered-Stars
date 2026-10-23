# Mods: packages, the manager, and the curated list

This note is the design for the port's mod support beyond the original loose `*.patch`
files: mod packages with a manifest, the Mods page in setup (enable, disable, load order,
conflicts, install from .zip, remove), and the curated mod list in this repository, with
its static check (CI, no discs) and the maintainer's runtime verification (with discs).
The README and the wiki are written from it.

Status (2026-10-23): implemented as described here; where a detail differs in the code,
the code wins and this note is corrected.

| Part | Where |
|---|---|
| Patch grammar (unchanged) | `engine/src/game/field/mods.h` (the four commands), parser in `engine/src/mods/patch.{h,cpp}` |
| Packages, load order, conflicts, .zip | `engine/src/mods/catalog.{h,cpp}`, `engine/src/mods/zip.{h,cpp}` (library `ss_mods`, used by the game and setup) |
| Applying patches in the game | `engine/src/game/field/mods.cpp` |
| Config key | `mod_order` in config.ini (`engine/src/launcher/settings.h`) |
| Setup's Mods page | `engine/src/setup/setup_ui.cpp` (`page_mods`) |
| Writing patches from the disc | `tools/so2_mod.py` |
| Static check (CI) | `tools/mod_check.py`, tests `tools/test_mod_check.py` (also tests `mod_index.py`) |
| Runtime verification (maintainer, discs) | `tools/mod_verify.py` |
| Registry | `mods/` (one folder per mod), `mods/index.json` (`tools/mod_index.py`), `mods/README.md` |
| Tests (C++) | `engine/tests/test_mods.cpp` (in ss_game_tests) |
| Screenshots | `tools/setup_screenshots.sh` (the `mods-*` shots) |
| GitHub | `.github/workflows/mods.yml`, `.github/PULL_REQUEST_TEMPLATE/mod_submission.md`, `.github/CODEOWNERS` |

## 1. What a mod can change

A mod is data, never code. Its `*.patch` files hold lines in the grammar of
`game/field/mods.h`, exactly four commands:

| Command | Changes |
|---|---|
| `script MAP WORD EXPECTED VALUE` | one 32-bit word of a field map's event script (chest contents, door targets, ...) |
| `item ITEM FIELD EXPECTED VALUE` | a field of an item's record (price, attack, defence, effect, sell %) |
| `shop SHOP INDEX EXPECTED VALUE` | one entry of a shop's stock (replace, remove, append) |
| `enemy TYPE FIELD EXPECTED VALUE` | a field of an enemy type's stats (HP, EXP, Fol, drops, level, ...) |

Every line carries the value the North American disc holds (`EXPECTED`). The engine
changes a value only while the data still holds `EXPECTED`, so a line written for other
data is skipped and logged instead of corrupting anything. `tools/so2_mod.py` writes the
lines, reading `EXPECTED` from the player's disc. Nothing on the disc is modified; the
patches apply in memory as the game loads each map, item table, shop and battle.

## 2. Packages

### Layout

A package is a folder (or a .zip of one) holding:

```
arlia-chest-tweak/
  mod.json          the manifest (required)
  chests.patch      one or more patch files, listed in mod.json in the order they apply
  README.md         optional: plain text shown in setup's info panel
```

Nothing else is allowed: no images, no binaries, no subfolders. (The repository's
no-game-assets rule allows game imagery only in `docs/images/`; a mod shows itself in
words.) Limits: at most 32 patch files, each at most 256 KiB; `README.md` 64 KiB;
`mod.json` 16 KiB; the whole package 1 MiB. Text files are UTF-8.

In the mods folder a package is a direct subfolder holding `mod.json`; its folder should
be named after its id (setup's installer always does that). Deeper folders are not
scanned.

### mod.json

```json
{
  "format": 1,
  "id": "arlia-chest-tweak",
  "name": "Arlia chest tweak",
  "version": "1.0.0",
  "author": "IronJade",
  "description": "The 200 Fol crate in front of Hearn's store holds 500 Fol.",
  "min_port_version": "0.01.00",
  "tags": ["chests"],
  "homepage": "https://github.com/IronJade/Shattered-Stars",
  "patches": ["chests.patch"]
}
```

| Key | Required | Rule |
|---|---|---|
| `format` | yes | `1` (this layout) |
| `id` | yes | 1-48 characters: lower-case letters, digits, `-`, `_`, `.`; starts with a letter or digit. Unique. |
| `name` | yes | 1-60 characters |
| `version` | yes | one to four dot-separated numbers (`1`, `1.2`, `1.2.3`), compared number by number |
| `author` | yes | 1-60 characters |
| `description` | yes | 1-1000 characters |
| `min_port_version` | yes | the oldest port version it works with, in the port's form (`0.01.00`) |
| `tags` | no | up to 8, each 1-24 of `a-z 0-9 -`. Suggested: `chests`, `doors`, `items`, `shops`, `enemies`, `difficulty`, `economy`, `qol`, `fix` |
| `homepage` | no | an `https://` address |
| `patches` | yes | 1-32 file names ending in `.patch` (no folders), each present, no repeats: the load order inside the package |
| `declared_risks` | no | edits the static check flags as risky, declared on purpose with a reason (section 7) |

Unknown keys are ignored by the game and setup (a newer format's additions) and reported
by the static check.

A package the game can't use is shown in setup with the reason and never loaded: a
manifest that doesn't parse or misses a required key, a listed patch file that is missing,
or a `min_port_version` newer than the running port.

### Loose patches (backward compatibility)

`*.patch` files directly in the mods folder keep working as before. Setup shows them as
one entry, **Loose patches**, which is enabled unless the player turns it off; its files
apply in name order, exactly as before packages existed. With no `mod.json` anywhere and
no `mod_order` key in config.ini, the game loads exactly what it loaded before.

## 3. The enabled set and the load order

Both live in config.ini, in one key written by setup (through `launcher/settings.h`):

```
mod_order=(loose),arlia-chest-tweak,-hearn-shop-stock
```

- The entries in load order, comma-separated. `-id` is installed but disabled (it keeps
  its place in the list). `(loose)` stands for the loose patches.
- An installed entry the key doesn't mention gets its default: loose patches enabled, at
  the front; packages disabled, at the end (by id). So a package copied into the folder by
  hand appears in setup turned off, and the game logs that it is installed but not
  enabled.
- No key at all: loose patches on, every package off — the behaviour before packages.
- Entries for mods that are no longer installed are dropped the next time setup saves.

The game never writes this key (headless runs, tests and the game itself only read
config.ini for it). Test runs can override it with `SS_MOD_ORDER` (same syntax). When the
folder itself is overridden (`--mods DIR` or `SS_MODS_DIR`), config.ini's `mod_order` is
not used, since it describes a different folder: the defaults apply unless `SS_MOD_ORDER`
is set. That keeps the test suites hermetic whatever a player's config.ini says.

What applies, in order:

1. an Archipelago seed's edits (`--ap-seed` / `ap_seed`), which always come first and are
   never overridden by a mod;
2. each enabled entry in `mod_order`: loose patches in file-name order, a package's files
   in its manifest's `patches` order.

## 4. Conflicts

### When two mods conflict

Two lines touch the same place when they are the same command on the same target and
position: the same map and script word, the same item and record bytes, the same shop and
stock index, or the same enemy type and stats bytes. Shop appends (`EXPECTED` 0 at the end
of the list) never conflict: every mod's appended item is added.

For two enabled lines from different entries, the earlier one A and the later one B in
the load order:

| Case | What happens | Shown as |
|---|---|---|
| same `EXPECTED`, same `VALUE` | both make the same change | not a conflict (info) |
| B's `EXPECTED` is A's `VALUE` | B was written on top of A (a compatibility patch): both apply, B's value ends up there | "builds on" (info) |
| same `EXPECTED`, different `VALUE` | **conflict, the later one wins**: the engine leaves A's line out (logged as overridden), so B's guard still sees the disc's value and applies | warning |
| different `EXPECTED` | at most one of them matches the disc. If A applies, B then finds A's value instead of its `EXPECTED` and is skipped; the log and setup name A as the reason | warning |
| overlapping but different bytes (`price` against `0:2`) | the earlier one applies first; the later one is most likely skipped | warning |

Within one entry (two lines of the same package, or two loose files) nothing is
overridden: the guard decides, as it always has, so the first line wins and the second
is skipped. The static check refuses such lines inside one package. Loose files keep
their old behaviour exactly.

An Archipelago seed's edits are never overridden: a mod line at a place the seed changes
is skipped (the seed's value is there), and setup says "the Archipelago seed changes this
too; the seed wins".

### How it is explained

- **Setup** analyses the enabled entries (and the seed, when one is set) without the
  disc, on every change to the list. A conflicted entry gets a warning badge in the list;
  the page shows a summary ("2 conflicts between enabled mods") and, for the selected
  entry, each conflict in a sentence: "*Hearn shop stock* and *Cheaper shop* both change
  shop 1, entry 0: Hearn shop stock to item 0x15, Cheaper shop to item 0xe4. Cheaper shop
  is later in the load order, so its change is used and Hearn shop stock's is not. Move
  Hearn shop stock below it to use its change instead."
- **The game** logs the order and every conflict once at start (only when there are
  packages: with loose patches alone its log is as before), then each line as it applies,
  is skipped or is left out (each once):

```
[mods] load order: (loose) 1 file(s), cheap 1.2.0, dear 1.2.0
[mods] tomorrow 0.3 is installed but not enabled (setup's Mods page enables it)
[mods] conflict: item 21 price: cheap/p.patch:1 is overridden by dear/p.patch:1 (later in the load order)
[mods] 0 script, 3 item, 0 shop, 0 enemy patch(es) from /home/.../mods
[port] [mods] cheap/p.patch:1: overridden by dear/p.patch:1 (later in the load order); not applied
[port] [mods] dear/p.patch:1: item 21 +0 0xc8 -> 0x190
[port] [mods] other/x.patch:7: item 21 +0 holds 0x190, not 0xc8: set by dear/p.patch:1 earlier in the load order; skipped
```

Line labels are `file:line` for loose patches (as before), `id/file:line` for a package's
lines and `seed:N` for the seed's. Script lines from mods now log when they apply, like
the other commands always did (a seed's script lines don't: there are thousands).

## 5. Setup's Mods page

```
 Mods
 World edits change chests, doors, items, shops and enemies as the game loads them ...

 Mods folder  [ ~/.config/shattered-stars/mods            ] [Browse...] [Default] [Open folder]

 Installed mods (applied top to bottom; later ones win a conflict)
 +-------------------------------------------------------+  [Install from .zip...]
 | [x] Loose patches          2 files                    |  [Move up  ]
 | [x] Arlia chest tweak 1.0.0  IronJade   chests        |  [Move down]
 | [ ] Hearn shop stock 1.0.0   IronJade   shops   (!)   |  [Remove...]
 +-------------------------------------------------------+
 (!) 1 conflict between enabled mods.

 Arlia chest tweak  1.0.0  by IronJade
 id arlia-chest-tweak · needs port 0.01.00 or newer · chests · homepage
 The 200 Fol crate in front of Hearn's store holds 500 Fol.
 Files: chests.patch (1 script line)
 Conflicts: ...
 README: ... (scrolls)
```

- **List**: one row per entry with a checkbox (enabled), the name and version, the author,
  the tags and a status mark: a warning for a conflict, an error for a package that can't
  load (shown disabled with the reason). Clicking a row selects it.
- **Move up / Move down** reorder the selected entry. Checkbox and order changes are
  settings like any other: unsaved until Save (or Save & Play), and Quit asks about them.
- **Install from .zip...** opens a file dialog (filter `.zip`). The archive must hold a
  package at its root or in one top-level folder; it is checked like the static check's
  file rules (allowlist, sizes, manifest, listed files present, no paths escaping the
  folder) and extracted to `<mods folder>/<id>/` through a temporary folder, so a failed
  install leaves nothing behind. If that id is installed already, setup asks first
  ("Replace Arlia chest tweak 1.0.0 with 1.1.0?"). A newly installed mod is enabled at the
  end of the list (unsaved until Save, like any change). Errors open setup's error popup
  with the reason.
- **Remove...** asks for confirmation ("Remove Hearn shop stock 1.0.0? Its folder is
  deleted: ...") and deletes the package's folder. It is offered only for packages (loose
  patches are files the player manages; **Open folder** shows them).
- **Info panel** for the selected entry: name, version, author, id, folder, minimum port
  version (an error if newer than this build), tags, homepage, description, the files with
  their line counts by command, the conflicts that involve it, and the README text.
- The **folder choice** (path, Browse, Default, Open folder) stays as it was.
- The sidebar's Mods item shows a dot: yellow for a conflict among enabled mods, red for
  an enabled package that can't load.
- Test hooks (screenshot mode never opens dialogs or deletes anything; actions run one per
  frame): `--do mods-install:PATH` installs a .zip, `mods-select:ID`, `mods-toggle:ID`,
  `mods-up`, `mods-down`, `mods-remove` (shows the removal question for the selected
  entry; never confirmed in screenshot mode), `mods-show-info` (scrolls to the selected
  entry's details). `tools/setup_screenshots.sh` renders the page's states with them.

Browsing and downloading the curated list inside setup are out of scope for now. The
pieces are laid out for it: `mods/index.json` names each listed mod's files and their
sha256, so a later setup can fetch the index, download a package's files, check the
hashes and hand them to the same installer, and can mark installed packages that match a
verified index entry.

### Logging

The game logs to stderr (and the Windows log file) as shown in section 4: the load order,
packages installed but not enabled, packages that can't load and why, conflicts, then
each line applied, skipped or overridden (once each). Setup prints its install and remove
actions to stderr (`setup: installed arlia-chest-tweak 1.0.0 into ...`).

## 6. The curated list

The curated list lives in this repository under `mods/`:

```
mods/
  README.md                 how to submit
  index.json                generated by tools/mod_index.py: every mod, its files' sha256, verification
  arlia-chest-tweak/        one folder per mod, named after its id
    mod.json
    chests.patch
    README.md
```

**Only mods with `"verified": true` in `mods/index.json` are on the list.** Verified
means: the maintainer ran `tools/mod_verify.py` on exactly these files (the index records
their sha256) with the North American discs, on the port version recorded in the index,
and it reported every patch line applying, every map the mod touches loading and running
its events without a crash or a new stand-in, and every battle with an enemy type it
edits starting and running; the maintainer also read the static check's risky-edit list
and the mod's README. It does not mean the mod is balanced or bug-free in every save — it
means the mod does what its lines say and doesn't inject anything that breaks the game in
those checks.

### index.json

```json
{
  "format": 1,
  "generated_by": "tools/mod_index.py",
  "mods": [
    {
      "id": "arlia-chest-tweak",
      "name": "Arlia chest tweak",
      "version": "1.0.0",
      "author": "IronJade",
      "description": "...",
      "tags": ["chests"],
      "min_port_version": "0.01.00",
      "homepage": "https://github.com/IronJade/Shattered-Stars",
      "path": "mods/arlia-chest-tweak",
      "files": {"README.md": "<sha256>", "chests.patch": "<sha256>", "mod.json": "<sha256>"},
      "verified": true,
      "verified_by": "IronJade (sample, verified by the port's tooling)",
      "verified_port_version": "0.01.00",
      "verified_date": "2026-10-23"
    }
  ]
}
```

`tools/mod_index.py build` regenerates it from `mods/*/mod.json` and the files. It keeps
an entry's verification only while the recorded sha256 of every file still matches, so
any change to a verified mod makes it unverified until the maintainer verifies it again.
`tools/mod_index.py verify ID --by NAME [--port-version V]` records a verification (the
maintainer runs it after `mod_verify.py` passes). `tools/mod_index.py check` regenerates
in memory and fails if the committed index differs (CI).

### Submitting (creators)

1. Make the patches with `tools/so2_mod.py` from your own disc and put them in a package
   folder `mods/<id>/` with `mod.json` (and a README.md if you like).
2. Run `python3 tools/mod_check.py mods/<id>` and fix every error; declare any risky edit
   you mean (section 7) with a reason.
3. Run `python3 tools/mod_index.py build` and commit `mods/index.json` with the folder.
   Your entry has `"verified": false`.
4. Open a pull request with the **mod submission** template (`?template=mod_submission.md`)
   and tick its checklist. The `mods` check runs the static check and the index check.

### Verifying (the maintainer)

`.github/CODEOWNERS` makes the maintainer (`@IronJade`) the reviewer of everything under
`mods/` (and of `.github/` and the two checking tools, because a pull request runs its
own copy of the workflow and tools); only the maintainer flips `verified`.

1. Read the diff, the static check's output (the job summary lists errors, warnings and
   declared risks) and the README.
2. Check out the PR locally and run `tools/mod_verify.py mods/<id>` (needs `SS_DISC1` and
   a build). It writes a Markdown report; paste it into the PR.
3. If it passes, run `tools/mod_index.py verify <id> --by IronJade`, commit the index
   change to the PR and merge.

The workflow (`.github/workflows/mods.yml`: the static check of every mod with annotations
and a job summary, the index check, the checker's tests) also fails a pull-request push
that adds a verification to `mods/index.json` unless the push is the maintainer's
(`github.actor`; `tools/mod_index.py guard`). Dropping a verification (any changed file
does) is allowed. It runs on `pull_request` (a read-only token, safe for forks), Python
only, with no network beyond `actions/checkout` and `actions/setup-python`.

## 7. The static check: tools/mod_check.py

Runs anywhere with Python 3.10+ and this repository (no discs, no network):

```
python3 tools/mod_check.py PACKAGE_DIR_OR_ZIP [...]   one or more packages
python3 tools/mod_check.py --all                      every mods/*/ folder
  [--json] [--github] [--strict]
```

It exits 1 on any error, 0 otherwise. Warnings (undeclared risky edits) don't fail it
unless `--strict` is given. `--github` prints workflow annotations and a job summary.

### Errors

| Rule | What |
|---|---|
| `manifest` | `mod.json` missing, not JSON, a required key missing, a wrong type, or a value outside the rules in section 2 (including a `min_port_version` newer than this repository's port version, from `engine/CMakeLists.txt`) |
| `folder-name` | in `mods/`, the folder isn't named after the id |
| `files` | a file outside the allowlist (`mod.json`, the listed `*.patch` files, `README.md`), a subfolder, a link, a `.patch` not listed in `patches`, or a listed one missing |
| `size` | over the limits in section 2 |
| `text` | a file that isn't UTF-8 text (or holds NUL bytes) |
| `grammar` | a line the engine wouldn't understand: only the four commands, four numbers / field names each, as `mods.h` defines them |
| `range` | a number out of range: a script map that isn't a field map, a script word past the largest script on the disc, an item outside 1-1023, a shop that doesn't exist, an enemy type that doesn't exist, a field offset past its record, a value or `EXPECTED` wider than its field |
| `expected` | an `EXPECTED` that contradicts the disc data in the repository's tables (a chest's word, a shop's stock entry, an enemy type's HP, EXP, Fol, level or drops): the line wasn't written against the North American disc and would always be skipped |
| `same-place` | two lines of the package at the same place where the second could never apply |
| `declared` | a malformed `declared_risks` entry (unknown rule, empty reason) |

### Risky edits (warnings unless declared)

| Rule | Flags |
|---|---|
| `script-unknown` | a script word that isn't a chest's contents or a door's target: the checker can't tell what it does, so the maintainer reads it by hand |
| `door-missing-map` | a door retargeted to a map that isn't a field map in the tables |
| `door-retarget` | any door retargeted: it may strand the party or skip the story; say where it leads and how the party gets back |
| `key-item` | an edit to a chest or gift that gives a story key item (script flags 66-93: the Cross Cave chest on map 547, the Cave of Trials chest on map 706, the 33 key-item gift sites) |
| `chest-empty` | a chest set to nothing (item 0 or 0 Fol) |
| `chest-other-sites` | a chest that exists on several maps (versions of one place at different points of the story) patched on only some of them |
| `item-unknown` | a chest, shop or drop set to an item number with no name in the tables |
| `boss-zero` | a boss's HP or EXP set to 0 (any enemy type that appears in a boss encounter) |
| `enemy-zero-hp` | any enemy's HP set to 0 |
| `price-zero` | an item's price set to 0 |
| `price-high` | a price over 21,691,754 (99 of them overflow a purchase) or over the 999,999,999 Fol cap |
| `sell-over-price` | a sell percentage over 100: buying and selling makes unlimited Fol |
| `item-behaviour` | an item's effect kind or type changed: how it works in menus and battle |
| `raw-field` | an item or enemy field given as a raw offset the checker has no name for |
| `drop-odds` | drop odds over 100 |
| `shop-only-source` | an item removed from a shop that was the only shop selling it |

A risky edit the mod means is declared in `mod.json`:

```json
"declared_risks": [
  {"rule": "price-zero", "lines": ["prices.patch:3"], "reason": "Blueberries are free: the point of this mod."}
]
```

`lines` is optional (without it the declaration covers every line the rule flags in the
package). A declared risk is listed as such (with its reason) instead of a warning; a
declaration that matches nothing is a warning.

### Tables and bounds

The checker reads the repository's own extracts of the disc (made by the port's tools,
no game data beyond numbers and names): `docs/ap/field.json` (field maps, chests and their
words, key-item gift sites, shops and their stock, item names), `docs/ap/doors.json`
(doors and their instructions), `docs/ap/battles.json` (enemy types with HP, level, EXP,
Fol and drops; encounters and which are bosses). Bounds not in the tables are fixed
conservatively in `tools/mod_check.py` and noted there: the largest script word (measured
over every field map of the North American disc, plus a margin), the record sizes
(items 0x30 bytes, enemies 0x5c), the stock index (below 0x100, as the engine).

## 8. Runtime verification: tools/mod_verify.py (maintainer, needs discs)

```
SS_DISC1=<disc 1 .cue> python3 tools/mod_verify.py mods/<id> [--bin engine/build/shattered-stars]
    [--out scratchpad/modverify/<id>] [--jobs 4]
```

Every run is headless (`--headless`, offscreen video, dummy audio) with a scratch settings
folder under `--out` (never the player's), `--no-memcard`, and the package copied into a
scratch mods folder enabled through a scratch config.ini's `mod_order`.

1. **Static check** first (as section 7); errors stop it.
2. **Maps**: every map the mod's script lines touch, plus the map of every shop it edits,
   runs through the event sweep (`tools/event_sweep.py`: each trigger region, Private
   Action and NPC talk entry, battles stubbed), once with the mod and once without. A
   crash, a hang, a new timeout or game over, or a new stand-in service with the mod is a
   failure; other differences (a door now leading elsewhere) are listed.
3. **Battles**: for every enemy type the mod edits, its first encounter
   (`docs/ap/battles.json`) starts headless with `SS_ENEMY_TRACE`; the battle must run its
   frames without a crash and trace the type.
4. **Lines**: every patch line must log as applied in some run (the engine logs each one).
   Script lines apply as their map loads; shop lines when the shop opens (the sweep talks
   to the shopkeeper); item lines when an item table loads (a battle's start, the shop and
   camp menus, some maps), so a mod with item lines and no enemy lines gets one battle
   too. A line that never applies is a failure if it was skipped (its `EXPECTED` didn't
   match) and a warning if no run reached it.
5. **Report**: `report.md` in `--out` — the package, its sha256s, the port version and
   commit, each line's result, each map's sweep against the baseline, each battle — ready
   to paste into the pull request. The exit code is 0 for a pass.

## 9. The samples

Two small mods in `mods/`, made with `tools/so2_mod.py`, verified as the maintainer's
sample (`verified_by`: "IronJade (sample, verified by the port's tooling)", port 0.01.00,
2026-10-23):

| Mod | Change | mod_check | mod_verify |
|---|---|---|---|
| `arlia-crate-fol` | chest 0x962 (the crate in front of Hearn's store) holds 500 Fol instead of 200, on both versions of the square (maps 18 and 21) | 0 errors, 0 warnings | PASS: both lines applied; maps 18 (36 entries) and 21 (13) sweep the same with and without |
| `hearn-store-plus` | shop 1 (Hearn's general store) also sells Resurrection Bottles (item 0x115 appended) | 0 errors, 0 warnings | PASS: the line applied when the sweep opened the shop on map 26; same results with and without |

A self-test package (an enemy edit, an item edit, and a script line written for other
data) showed the other paths: the enemy and item lines applied in battle 19 with the type
traced, and the script line was reported skipped, so the verdict was FAIL.
