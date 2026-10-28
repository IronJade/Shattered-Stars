<!-- A mod for the curated list (mods/README.md). Open this template by adding
     ?template=mod_submission.md to the pull request's address. -->

## The mod

- **Id / name / version:**
- **What it changes** (and why):
- **Made with:** tools/so2_mod.py commands, or how else the lines were written:

## Checklist

- [ ] The package is `mods/<id>/` (the folder named after the id) with `mod.json`, its `.patch` files and at most a `README.md`: no images, programs or other files.
- [ ] Every patch line was written against the **North American** discs (tools/so2_mod.py reads the expected values from your disc).
- [ ] `python3 port/tools/mod_check.py --strict mods/<id>` reports no errors (every risky edit removed or declared).
- [ ] Every risky edit it flags is either removed or declared in `mod.json` (`declared_risks`) with a reason.
- [ ] I ran `python3 port/tools/mod_index.py build` and committed `mods/index.json`; my entry says `"verified": false` (only the maintainer verifies).
- [ ] I played the changed places in Shattered Stars with the mod enabled in setup, and every line applied: the game's `[mods] <id>/...: ... -> ...` lines (in `shattered-stars.log` on Windows; on Linux, start the game from a terminal to see them).
- [ ] The mod is my own work (or I have the author's permission), and I agree to it being distributed from this repository under the project's licence.

## What I tested

<!-- Maps visited, shops opened, battles fought, saves used. -->

## For the maintainer

<!-- The maintainer pastes port/tools/mod_verify.py's report here, then runs
     port/tools/mod_index.py verify <id> --by IronJade. -->
