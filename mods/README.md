# The curated mod list

Mods for Shattered Stars that the maintainer has checked with the game and the discs.
Each folder here is one mod package; `index.json` lists them with the sha256 of every
file and whether the mod is verified. **Only the mods with `"verified": true` in
`index.json` are on the list.**

To use one, download its folder as a .zip (or zip it yourself) and choose **Install from
.zip...** on setup's Mods page, or copy the folder into your mods folder and enable it
there. The full design is in [docs/notes/mods.md](../docs/notes/mods.md).

## What "verified" means

The maintainer ran `tools/mod_verify.py` on exactly the files recorded in `index.json`,
with the North American discs and the port version recorded there, and it reported:

- every patch line applying in the game (none skipped);
- every map the mod touches loading and running all its events (trigger regions, Private
  Actions, NPC talk) with no crash, hang, new timeout, new game over or new stand-in,
  compared with the same maps without the mod;
- every battle with an enemy type the mod edits starting and running without a crash.

The maintainer also read the static check's list of risky edits and the mod's README. A
verified mod does what its lines say and doesn't inject anything that breaks the game in
those checks; it isn't a promise about balance, or about every save. Any change to a
verified mod's files makes it unverified until it is verified again.

## A mod package

```
your-mod-id/
  mod.json        the manifest
  changes.patch   one or more patch files, in the order mod.json lists them
  README.md       optional: what it does, in plain text (shown in setup)
```

Nothing else: no images, no programs, no subfolders (the project's rule allows no game
assets outside `docs/images/`). The patch lines are the four commands `script`, `item`,
`shop` and `enemy` (see `engine/src/game/field/mods.h`); write them with
`tools/so2_mod.py` from your own disc, which fills in the value each line expects to
find. Limits: 32 patch files, 256 KiB each, 1 MiB per package.

`mod.json`:

```json
{
  "format": 1,
  "id": "your-mod-id",
  "name": "A short name",
  "version": "1.0.0",
  "author": "You",
  "description": "What it changes, in a sentence or two.",
  "min_port_version": "0.01.00",
  "tags": ["chests"],
  "homepage": "https://...",
  "patches": ["changes.patch"]
}
```

`tags` and `homepage` are optional. The folder must be named after the `id`.

## Submitting a mod

1. Make your patches with `tools/so2_mod.py` and put them in `mods/<your-mod-id>/` with
   a `mod.json` (and a README.md if you like). Try the mod in the game.
2. Run the static check and fix every error:

       python3 tools/mod_check.py --strict mods/<your-mod-id>

   It also flags risky edits (an unknown script word, a door sent elsewhere, a key item
   chest changed, a boss with 0 HP, a free item, ...). The pull-request check runs in strict
   mode, so an undeclared risky edit fails it. If you mean one, declare it in `mod.json` with
   a reason, so the reviewer sees why:

       "declared_risks": [{"rule": "door-retarget", "lines": ["changes.patch:4"],
                           "reason": "The shortcut back to Arlia; the old door still works."}]

3. Regenerate the index and commit it with your folder (your entry is unverified):

       python3 tools/mod_index.py build

4. Open a pull request using the mod submission template: add
   `?template=mod_submission.md` to the pull request's address, and tick its checklist.
   The `mods` check runs the static check, the index check and the checker's tests.

## Verifying (the maintainer)

`.github/CODEOWNERS` makes the maintainer (@IronJade) the reviewer of everything here,
and only the maintainer marks a mod verified (the workflow fails a push by anyone else
that adds a verification to `index.json`).

1. Read the diff, the check's job summary (errors, warnings, declared risks) and the
   README.
2. Check the pull request out and run, with the discs and a build:

       SS_DISC1=<disc 1 .cue> python3 tools/mod_verify.py mods/<id> --bin engine/build/shattered-stars

   It writes `report.md` (the path is printed); paste it into the pull request.
3. If it passes: `python3 tools/mod_index.py verify <id> --by IronJade`, commit
   `mods/index.json` to the pull request, and merge.
