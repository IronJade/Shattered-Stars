# Handoff: the public GitHub wiki (agent "wiki")

The wiki is a separate git repo cloned at `scratchpad/wiki` (remote
https://github.com/IronJade/Shattered-Stars.wiki.git, branch master). Pages are written
there and pushed; nothing in the main repo changes except this log and one line in
`scratchpad/checkpoint_notes.txt`.

## Rules this agent was given (all mandatory)

- Audience: players, Archipelago players, modders, would-be contributors. Plain English,
  short paragraphs, tables where they help; no internal jargon (agent names, scratchpad
  paths, handoff logs, vblank counts) except on contributor pages, and explain terms.
- Every fact from the repo (code, docs/, tools/ --help, apworld/so2/docs/). Never quote or
  paraphrase the Prima guide; never invent features; say plainly what isn't built.
- Images: none in the wiki repo; reference only names that exist in `docs/images/` by their
  raw.githubusercontent.com URL (main branch). None existed when the pages were written.
- Git (wiki repo only): `git config user.email` must print
  `314439719+DreamGlyphStudios@users.noreply.github.com` before each commit; never change git
  config; never put the user's email in a page. Commit messages end with
  "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>". Push to the wiki's
  origin after each few pages. Never push the main repo, never force-push, no other remote.
- Main repo: write only this file and ONE line in `scratchpad/checkpoint_notes.txt`
  ("wiki: ..."). No code edits. No game code or assets anywhere.
- Context budget: past ~400k tokens push, update this file and stop.
- --help checks only with a scratch XDG_CONFIG_HOME under `scratchpad/wiki_scratch/` and
  `--headless`; never touch ~/.config/shattered-stars.

## Pages

| Page | State |
|---|---|
| Home.md | pushed |
| Installing-and-Setup.md | pushed |
| Playing.md | pushed |
| Bugs-Fixed-and-Quirks-Kept.md | pushed (80 entries: 40 fixed, 20 avoided, 19 kept/held, 1 not a bug) |
| How-the-Port-Was-Made.md | pushed |
| Archipelago.md | pushed |
| Modding.md | pushed (overview only; mod manager named as coming) |
| Building-from-Source.md | pushed |
| FAQ.md | pushed |
| Contributing.md | pushed |
| _Sidebar.md, _Footer.md | pushed |
| Making-a-Mod / Installing-and-Managing-Mods / Submitting-a-Mod | deferred: `docs/notes/mods.md` exists (design), but `docs/handoff/modmanager.md` reports only "design doc written, next: engine module". Write them from mods.md once the manager is reported done, and update Modding.md's "Coming next" section |

Wiki commits: 3736cfb, ff37a22, f8d9b79 (all pushed to origin/master). Internal links and
anchors checked by script: 0 broken.

## Next steps

- When screenshots land in `docs/images/` (check `docs/images/README.md` for names), add them
  by raw URL `https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/<name>.png`:
  Home (title screen), Installing-and-Setup (setup's Discs, Display, Controls pages), Playing
  (field, battle, world map, insert-disc screen), Archipelago (setup's Archipelago page, a
  "Sent ... to ..." message, a "Received ..." banner), Modding (setup's Mods page),
  Bugs-Fixed-and-Quirks-Kept (a before/after of a visual fix, e.g. W1 or S10).
- When the first GitHub release is published, replace the "once published" wording on Home,
  Installing-and-Setup, Archipelago and FAQ with direct asset links (the two bundles,
  so2.apworld, the template YAML).
- Refresh the catalog numbers on How-the-Port-Was-Made (5452 rows / 5446 confirmed / 6 likely;
  4463 ported / 901 not needed / 88 PsyQ at writing) and the reference-suite totals.

## Sources used

docs/HANDOFF.md, docs/CHECKLIST.md, engine/README.md, docs/notes/original_bugs.md,
docs/ap/apworld.md, docs/ap/release_checklist.md, docs/archipelago_plan.md,
apworld/so2/docs/*.md, docs/catalog/README.md + COVERAGE.md + functions.csv (live counts),
tools/refs/README.md, docs/notes/m8_audit.md, docs/notes/endings.md,
engine/src/game/field/mods.h, tools/so2_mod.py, engine/src/setup/setup_ui.cpp (page text),
engine/src/launcher/{config.h,settings.*,discs.cpp}, engine/src/platform/window.cpp
(default bindings, hotkeys, GL 3.3), engine/CMakeLists.txt, tools/build_*.sh headers,
the packaged binary's --help (dist/, scratch config).

## Open items / inconsistencies noticed

- `apworld/so2/docs/setup_en.md` says offline play is command-line only and "isn't exposed on
  the setup program's Archipelago page"; setup_ui.cpp and engine/README.md say a seed with an
  empty server plays offline from setup. The wiki follows the code.
- engine/README.md's status table still says spells, the other character modules and party AI
  are in progress; HANDOFF/CHECKLIST say M7 is done.
- docs/catalog/COVERAGE.md's totals (confirmed 5244 / likely 156 / guess 52) lag
  functions.csv (5446 confirmed, 6 likely of 5452); the wiki uses functions.csv.
- The coordinator's brief mentioned "four special fights" for DeathLink; the docs list five
  (three scripted losses 204 / 517 / 530 and two timed survivals 516 / 520). The wiki follows
  the docs.
