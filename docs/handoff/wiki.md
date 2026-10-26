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
| Bugs-Fixed-and-Quirks-Kept.md | pushed; 0.02.50 pass uncommitted: 85 entries (44 fixed, 20 avoided, 20 kept/held, 1 not a bug) + "Bugs in the port" |
| How-the-Port-Was-Made.md | pushed |
| Archipelago.md | pushed |
| Modding.md | pushed (overview only; mod manager named as coming) |
| Building-from-Source.md | pushed |
| FAQ.md | pushed |
| Contributing.md | pushed |
| _Sidebar.md, _Footer.md | pushed |
| Making-a-Mod / Installing-and-Managing-Mods / Submitting-a-Mod | pushed (5bb484b, 2026-10-23); the note that follows is the original deferral: `docs/notes/mods.md` exists (design), but `docs/handoff/modmanager.md` reports only "design doc written, next: engine module". Write them from mods.md once the manager is reported done, and update Modding.md's "Coming next" section |

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

## For wiki (from release, 2026-10-25 ~20:30)

The 0.02.50 release changes these facts; `docs/release_notes/0.02.50.md` (draft, uncommitted)
words them for players, and README.md's Archipelago section was updated to match. Please keep
the wiki in step (or tell the release agent, docs/handoff/release.md, where they disagree):
- **Versions:** port 0.02.50 shipping so2.apworld 0.2.0 (`world_version` 0.2.0, still
  `minimum_ap_version` 0.6.8). Downloads `ShatteredStars-0.02.50-win64.zip`,
  `ShatteredStars-0.02.50-linux-x86_64.tar.gz`, `so2.apworld`, `Star.Ocean.The.Second.Story.yaml`.
- **Seeds and ports:** 0.2.0 seeds need port 0.02.50. Port 0.02.00 refuses version 2 / 3 seeds
  ("unsupported version 2 (this build reads 1)"), and even a version-1 0.2.0 seed plays wrong on
  it (it reads only the old option names, so the route and first-drop checks are lost). Port
  0.02.50 reads seed versions 1-3: every seed of 0.1.0, 0.1.1 and 0.2.0.
- **Renamed YAML options, no alias:** `protagonist` -> `story_route`, `drops_as_checks` ->
  `enemy_dropsanity`. Archipelago ignores the old keys with a warning and uses the defaults
  (`any`, `off`); old YAMLs must be edited. (`goal: final_boss` still works as `indalecio`.)
- **New options:** world_mode `hub` + `hub_start` (fun_city / arlia / random_town of 16 towns) +
  `fienal_requirement` (fields_and_berle / fields / all_bosses); `goal` (indalecio /
  indalecio_limiter_off / indalecio_limiter_off_and_iseria_queen); `difficulty` (earth / galaxy
  / universe / any); `craftsanity` (553) + `no_fail_crafting`; `killer_move_checks` (133) +
  `killer_move_progression` (never / hero / any); `skillsanity` (none / skills_only /
  special_only / insanity; 148 learn points) + `skillsanity_shop_share` (default 10);
  `drop_progression` (allow / force / never) + `area_keys_in_drops` (default on);
  `starting_character` (hero / any / a name; `any` = the character select at New Game) +
  `exclude_characters` (open world and hub).
- **Counts:** chests 308 (0.1.x's three "Cave of Trials Level 1: Portrait F / G / J chest" are
  gone: they were learn spots, not chests); gift sites 101; bosses 41 (+ the hub-only Cross Cave
  guardians); first drops 89; shop slots up to 550. Cave of Trials locations are named by their
  real floor (76 renames + 3 boss names; ids unchanged).
- **Existing players:** no need to regenerate 0.1.1 seeds; the port alone fixes the open-world
  Lacour soft-lock (Port Hilton's sailor refused to sail in chapters 23-24), the shop purchase
  (buying a slot gives the items and the check; the Archipelago logo marks an unchecked slot),
  and the Cave of Trials learn chests (no memory corruption). A run in progress: the first start
  copies that seed's saves from the player's card to the seed's own card, once; the player's
  card is untouched. 0.1.0 open-world seeds keep that version's logic errors.
- **Port, setup:** seeds from the `AP_<n>.zip` directly (Browse starts in Archipelago's output
  folder; "Your slot" list for several SO2 players); each seed on its own memory card
  (`archipelago/<seed>_<slot>/memcard1.mcd` beside config.ini; the page's "Saves" line and "Open
  folder"); the Archipelago on / off switch on the sidebar row; no debugging in a windowed
  seed game; the save guard ("The data could not be loaded", not anti-cheat); the Gameplay page
  (Instant text, Auto-advance 0.5-3 s, off by default, any game); story_route claude / rena skips
  the hero select and naming; open world / hub: members join at the party's median level.
- **Fixes:** Claude's sword in Rena's game (every weapon user outside party slot 0); the menus'
  window-art over-read; original bug W4 (ext 0xCB, the Field of Courage / Eluria Tower Berle
  scenes); W5 (courier orders past 20) fixed with a seed only.

## 0.02.50 pass (2026-10-25, agent "wiki")

The user: "We will also need to update the Wiki to be factually correct to the new information."
Release: **port 0.02.50, shipping so2.apworld 0.2.0**. A full factual pass over every page.

Rules changed for this pass: **no commits and no pushes in the wiki repo** (the user reviews, then
commits and pushes): changes stay in scratchpad/wiki's working tree. Main repo: only this log and
dated "For release" notes in docs/handoff/release.md. Otherwise the rules above (audience, facts
only from the repo, no Prima guide, no assets, images by raw URL from docs/images/, --help only
with a scratch XDG_CONFIG_HOME under scratchpad/wiki_scratch/ and --headless).

**Already in the checkout when I started** (uncommitted, by the agent "release" before the wiki
was handed to me): Installing-and-Setup.md (setup has eight pages; a new "Gameplay" section:
instant text, auto-advance and its wait; the first-launch step names Gameplay) and Playing.md
(Gameplay in the settings table; a "Messages" row in "What's different"). Checked against
setup_ui.cpp page_gameplay(), launcher/settings.h kAutoAdvanceDelays {500..3000} and
docs/handoff/dialogue.md (scrolling at once: state 5): correct; kept and folded in.

### Progress
- Read: wiki.md, the wiki pages, git log fd687ad..HEAD, the player guide and setup guide,
  apworld.md (Locations, Hub mode, Open world, Changes), seed_format.md (version rule),
  hub.md, setup.md (10-25), apv2.md, charselect.md, shopboth.md, cavefix.md, claudesword.md,
  menufix.md, skillsanity.md, original_bugs.md (W4, W5), engine/README.md, README.md.
- Checked by script: hub reach from data.py (290 chests / 73 in the cave, Eluria Tower's 23
  included; 9 gifts; 22 chambers; 16 starts; 41 / 42 shops), the catalog (functions.csv 5536
  rows, 172 modules, all confirmed; ported 4547 incl. the malformed row, not needed 901, PsyQ
  88), the Reference-* pages (regenerated to scratch: all 21 identical, no change needed).
- Release's "For wiki" note (above, ~20:30) and docs/release_notes/0.02.50.md (draft) read and
  folded in; where they add facts (Arlia's guide back to the arena; 0.02.00 plays a version-1
  0.2.0 seed without its route / first-drop options; a Fol check lost in 0.02.00 shops; hub
  split scenes fought with the whole party; the hub's left-out optional bosses) the wiki says
  the same. No disagreement left between the notes and the wiki.

### Done (2026-10-25 ~20:40): every page checked; nothing committed or pushed

Changed in scratchpad/wiki (uncommitted, 11 files; internal links / anchors / image URLs / repo
paths checked by scratchpad/wiki_scratch/check_links.py: 0 broken):
- **Archipelago.md**, rewritten for so2.apworld 0.2.0 + port 0.02.50: the version box (0.2.0
  seeds need 0.02.50); the three world modes; counts 308 chests / 75 in the cave (was 311 /
  "78 chests and 3 gifts"); the new location kinds (crafts 553, killer moves 133, guild 45,
  learn points 139 + 9); never-progression list (gifts now too); three goals; dungeon keys and
  skill items; story vs open-world / hub party rules, the join level, the starting character
  and the character select, the leader stand-in (open world only, per leader.cpp); open world
  (keys, Port Hilton's captain, the El gate, exit gates, the teleporter + Arlia's guide); hub
  mode (start, Disc 2, 16 towns, 18 keys, 22 chambers, Fienal requirement, Shigeo / Marsilio
  questions, what it includes 217 / 290 chests, 7 / 9 gifts, what it leaves out); shops (item
  + check, the logo, orders, waiting items); drops; craftsanity; killer moves; skillsanity;
  difficulty; Expel; the full options table (renamed story_route / enemy_dropsanity); YAML
  example (hub) and the old-name warning; generating (the zip, output folder); connecting
  (zip, slot list, Browse); the switch; a card per seed, adoption, the save guard; no
  debugging; offline (--ap-seed-entry); known issues; troubleshooting; What changed in 0.2.0
  (world + port) and 0.1.1 (kept, shortened; its anchor is linked from FAQ).
- **Installing-and-Setup.md**: release's Gameplay section kept; 0.02.50 package names; Disc 2 /
  hub note; Memory cards page's seed note; the Archipelago page's three parts and the switch;
  New Game note; seeds' cards in "Where your files live"; backing up a seed's folder.
- **Playing.md**: release's two Gameplay lines kept; seeds' own cards; the Archipelago settings
  row; `--ap-seed` takes the zip, `--ap-seed-entry`; no debug console in an Archipelago game.
- **Bugs-Fixed-and-Quirks-Kept.md**: W4 (crash table, fixed) and W5 (field table, kept:
  gameplay; with a seed the overflow waits); counts 85 = 44 fixed / 20 avoided / 20 kept or held
  / 1 not a bug (field area 12 = 6 / 4 / 2); a new "Bugs in the port" section: Claude's sword,
  the menus' window-art over-read, the cave learn chests in 0.1.x seeds, the 0.02.00 shop mark.
- **How-the-Port-Was-Made.md**: catalog 5,536 functions / 172 modules (149 NA + 23 FR / DE), all
  confirmed, ported 4,547 (was 5,452 / 149 / 4,463: stale since 0.02.00, the FR / DE rows were
  already there); tests "run before a change counts as verified" + checkpoint.sh (the hourly
  checkpoint is retired); "What's left": the .apworld ships with each release (was "still to
  come"), the mod manager is done (was "being built"), polish line.
- **Home.md**: version 0.02.50 (+ so2.apworld 0.2.0), a dialogue highlight, Archipelago modes in
  the highlight and the status row, download line.
- **FAQ.md**: dialogue speed; Archipelago modes; which world version; saves while playing a
  seed; seeds' cards under "Where are my saves?"; Disc 2 for the hub; the 0.1.0 advice reworded.
- **Building-from-Source.md**: "0.02.50 today"; headless runs with a seed (SS_AP_SEED_CARD /
  SS_AP_ANY_SAVE). **Contributing.md**: package name; debug options ignored with a seed.
- **Installing-and-Managing-Mods.md**: the example error "(this is 0.02.50)"; seed conflicts only
  with Archipelago on; "the chests that are its locations". **_Footer.md**: 0.02.50.
- Not changed (checked): Modding.md, Making-a-Mod.md, Submitting-a-Mod.md, _Sidebar.md (no mod
  tool changed since fd687ad; min_port_version's "0.01.00" is an example), Modding-Reference.md
  and the 20 Reference-* pages (regenerated to scratch with tools/mod_reference_wiki.py: all
  identical).

Couldn't confirm by running (read from code / docs only): the leader stand-in being open world
only (leader.cpp candidate(); the option docstring and player guide say hub too: "For release"
note); the hub needing Disc 2 at New Game (hubcore.md: field_run's disc check shows the
insert-disc screen); the save-guard message text ("The data could not be loaded. Your data may
be faulty.": setup_en.md / engine/README.md; it's disc text).

"For release" notes left in docs/handoff/release.md (2026-10-25 ~20:35): the player guide's stale
hub counts (185 / 258, "Eluria Tower's chests are not locations yet"); the starting character's
hub stand-in claim; README's 0.02.00 bug counts and catalog numbers; the malformed
functions.csv row (line 3528, title 0x80042cc8) and the stale COVERAGE.md; engine/README's
"in progress" hub; known limitations to add; cavefix's "0.03.00"; the outdated
setup-archipelago.png.

Still open / to watch: hubproof2's item 4 (the hub arena's prize gift) is not run; if it becomes
a hub location, Archipelago.md's "Left out" list changes. cleanup's `--debug` exit-hang fix
(runtime.cpp) is in progress; not on the wiki (a debug-console matter; add to "Bugs in the
port" if wanted once verified). The user reviews, then commits and pushes the wiki.
- (release, ~20:45) Done from your "For release" note (release.md): the player guide's hub
  counts (217 chests, 290 with the cave; 7 gifts, 9 with it; Eluria Tower / Fienal maze
  included; the "not locations yet" sentence gone); README's 85 bugs (44 / 20 / 8 / 4 / 6 / 2 / 1)
  and catalog (5,536 functions, 172 modules, 4,547 ported); engine/README's hub "in progress"
  (already dropped); the notes' known limitations gained the afterimages / long scenes, the
  captain's chapter-23 Hoffmann lines and skillsanity's "learned" text. The player docs also
  name all four releases now (they said North American only) and gained two Troubleshooting
  entries (the renamed options; "unsupported version 2 (this build reads 1)"). Left for the
  user, as you suggested: starting_character's hub wording vs the open-world-only stand-in,
  functions.csv line 3528's unquoted comma, a new setup-archipelago.png.
- (release, 21:05) docs/images/setup-archipelago.png and setup-discs.png recaptured for 0.02.50
  (the switch, the Gameplay page, the version, the Saves part, the four releases' text); the wiki
  links them by raw URL, so they change once the main repo is pushed.
- (release, 21:58) A fix the wiki may want (FAQ / Known issues / Bugs Fixed): on Windows, saving
  failed silently when the memory card's folder had a non-ASCII name (a Windows user name like
  "José", a custom card path, or a seed's slot name like "Cléo"): the save seemed to work but the
  card on disk never changed. Fixed in 0.02.50 (engine/src/psx/memory_card.cpp); Linux was never
  affected. docs/release_notes/0.02.50.md "Fixes" words it for players.

## For wiki (from winfix, 2026-10-26: the Windows check's fixes, in 0.02.50)
Facts the wiki pages may want (uncommitted code; docs/handoff/winfix.md):
- **shattered-stars.log** (beside config.ini, written by every windowed run on Windows *and*
  Linux) now holds the game's own `[ap]`, `[port]` and `[mods]` lines as well as the launcher's,
  on every platform: Installing-and-Managing-Mods.md ~line 189 and Making-a-Mod.md ~line 423 can
  say the log has the mod lines on Linux too ("On Linux, start the game from a terminal" is no
  longer needed). It is replaced at each start and stays under 4 MB (it keeps the run's first MB,
  a "left out" line and the newest 2 MB).
- **Windows command line:** paths with any letters (Ä, 日本, ...) now work in every option
  (--disc1/2, --memcard1/2, --ap-seed, --mods, setup's --set) and in the SS_* variables.
- **Archipelago:** the client announces version 0.6.8 to the server (was 0.6.6); the game's
  "Archipelago: connecting to ..." line names it.
- **Setup's Auto-detect** with Disc 1 set says "Found Disc 2 next to your Disc 1."
