# releasedocs: B7, the documentation half (tracker release)

Agent "releasedocs", started 2026-10-27. Plan: docs/plans/trackers.md B7.
Writes everything the tracker release needs, so the release candidate can
run from it. No version bumps, no RC, no publishing.

## The release (the user's decisions, docs/handoff/phaseb.md)

Three separate GitHub releases on IronJade/Shattered-Stars:

- port `v0.03.00` (Latest)
- world `apworld-v0.2.2` (not Latest)
- PopTracker pack `poptracker-v1.0.0` (not Latest)

Each release's notes link the other two. The pack's versions.json lives at
tools/poptracker/versions.json on main (raw URL), committed after the
release. Pattern: docs/handoff/release.md "0.02.51" and
docs/ap/release_checklist.md section 11.

## Rules (copied from the assignment)

- Files I may touch: docs/ap/release_checklist.md,
  docs/handoff/windows_check.md, docs/release_notes/0.03.00.md,
  docs/release_notes/0.2.2.md, docs/release_notes/poptracker-1.0.0.md,
  README.md, docs/plans/next.md, scratchpad/wiki/** (uncommitted),
  docs/handoff/releasedocs.md.
- Do NOT touch apworld/, tools/, engine/,
  docs/ap/{poptracker,tracker_maps,apworld,seed_format,data_storage}.md,
  docs/modding/: agent "maptab" works there in parallel and may commit at
  the same time.
- Commit on main after each finished piece (docs only), staged by path
  (never git add -A / -u); message ends with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.
- Never commit inside scratchpad/wiki, never push, tag or publish.
- If git reports index.lock, wait and retry; never delete it.
- On a denied tool call: don't retry; write the blocked step in this log
  and stop, naming the exact command.
- Cap command output.
- Keep this log current after each finished step and at least every 30
  minutes.
- Roll over at ~400k context: update the log and stop.
- Check facts against the logs and docs; don't invent numbers. Where two
  sources disagree, say so here.
- Style: plain, exact English, short sentences, facts with numbers; credit
  AI openly as the README/wiki already do.

## Pieces

1. docs/ap/release_checklist.md: new section 12 (three releases: version table, builds, every
   test with command and expected result, sums / upload sets, the Windows check, drafts ->
   publish order world, pack, port; versions.json after the pack's release; the wiki push; D7
   listing only on the user's word). **Done.**
2. docs/handoff/windows_check.md: "Recheck for 0.03.00" (13 items: version, setup's button incl.
   the first real setup.exe run, the CLI, PopTracker install / connect / autotrack / story / rendered
   maps / offline / old room, Universal Tracker incl. /explain and story awareness, a TODO item for
   maptab, a glance at the rest). **Done.**
3. Release notes: 0.03.00.md finished (intro + links to the two other releases; the keys; the
   render command + setup button; rooms and lettered doorways; updating table incl. 0.2.2 seeds on
   0.02.51; packages; Archipelago; a new "Trackers" section; known limitations = 0.02.51's +
   online-only, the setup button's fixed folder / portable PopTracker, still pictures, the bench
   banner); 0.2.2.md finished (UT yaml-less, /explain, logic_version, the logic export, story
   awareness + its unit, "Older rooms" (0.2.0 Skillsanity warning, hell seeds refused, PopTracker
   checklist), seeds = 0.2.1's but the version number, installing, files, checks, limitations,
   developers); poptracker-1.0.0.md new (needs, what it shows, install, updates, maps from your
   disc + portable PopTracker, limitations, files, how it was checked, licence). Sums TODO.
   **Done.**
4. README.md: a "Trackers that follow you" highlight; "Download" names the world's and the pack's
   own releases (apworld-v0.2.2, poptracker-v1.0.0); a new "Trackers" subsection under
   Archipelago (UT, the pack, following the game, maps from your own disc, the wiki link); the
   world link -> 0.2.2; tools/ line and the build line name the pack's generator / build script.
   **Left for B7b's bump** (checklist 12.1): the badge and the two package names (still 0.02.51).
   **Done.**
5. Wiki (scratchpad/wiki, uncommitted): Trackers page + links. (pending)
6. docs/plans/next.md: Phase B status, Phase C pointer. (pending)

## Placeholders left for maptab

- docs/ap/release_checklist.md section 12: rows and notes marked **TODO (maptab)**: the UT map
  tab's files and the package size (12.2), the pack's room placeholders / size (12.2), the world
  tests' count, the identity check's possible new key, the pack checker's counts, the pack's
  tests (rooms, doors, panel, click), the smoke's door-click step, `SS_TEST=tracker_` counts if
  the layout changes, and one row "the map tabs" (layout --check after the area audit, UT's map
  tab loading headless).
- Release notes: 0.03.00.md "What's new" bullet (the pack's room maps and doors, UT's map tab);
  0.2.2.md "What's new: Universal Tracker" last bullet (UT's map tab) and the counts paragraph;
  poptracker-1.0.0.md "What it shows" last bullet (rooms and doors), "the rooms" in "The real
  maps", and the counts paragraph. All marked **TODO (maptab)**; the DRAFT banners name them.
- docs/handoff/windows_check.md "Recheck for 0.03.00" item 11 (TODO (maptab)): the room maps,
  the door panel, click switching, auto tab to rooms, UT's map tab.

## Facts not confirmed / sources that disagree

- **0.2.2.md said the .apso2 files are "byte for byte" 0.2.1's.** True only before the version
  bump: `apworld_version` is in the .apso2's options (world.py slot_options), so the bump to
  0.2.2 changes that one field, as 0.2.0 -> 0.2.1 did ("Only the version number written into the
  seed changes"). Spoilers name only Archipelago's version (checked on an RC spoiler), so they
  stay byte for byte. Corrected in the checklist (12.1, 12.3) and to be corrected in 0.2.2.md.
- **The pack's sha256 at storyaware's build:** docs/handoff/storyaware.md says versions.json ok
  "sha256 1cc996fd...", docs/ap/poptracker.md "Pack checker" says "(sha256 aac3c4dc...)" for the
  same 401-file build. Probably two builds (before / after the chapter-0 fix, bc3cc21); not
  checked. Irrelevant for the release (the sums are taken at the RC), not quoted anywhere.
- The RC's packaged e2e copies (scratchpad/release/rc251_e2e_so2.sh, ap_e2e_so2_packaged.sh)
  have no step 9 (grep: 0 hits for so2_map / ap_ds_read): the RC must derive them again.
- The Windows full-suite count under Wine for 0.03.00 isn't measured (setupthread ran only
  `SS_TEST=tracker_` 10/10 under Wine); the checklist says "Linux + 1", from 0.02.51's 207 / 208.
- The scratch identity scripts of the Phase B agents compared each step with the previous one;
  the chain starts at the released 0.2.1 world (git diff 2c38965 6d2c518 -- apworld/so2 is empty).
  The checklist asks the RC for one direct comparison with apworld-v0.2.1's world.
- **The 0.03.00 draft said the render takes "about half a minute"**; tracker_maps.md says about
  21 s, setupthread measured 20.5-20.9 s. Written as "about 20 seconds on the development machine".
- **"Install the same so2.apworld the room was generated with"** (setup_en.md, the old 0.2.2 draft)
  vs "rooms made with 0.2.0 / 0.2.1 track with 0.2.2 installed" (apworld.md "Older rooms"): the
  older worlds have no yaml-less support, so for them 0.2.2 is the one to install. Written as "the
  room's so2.apworld (0.2.2 for a room made with 0.2.0, 0.2.1 or 0.2.2)". setup_en.md is not mine.
- **UT's story awareness on 0.2.0 / 0.2.1 story rooms**: by the design it should work (story.prepare
  runs in any UT regeneration of a story-mode room with 0.2.2 installed), but no test covers an old
  room; the docs claim only the pack's need for a 0.2.2 room.
- storyaware's "a player may recolour Inspect in PopTracker's settings": not checked (PopTracker
  has a colors.json); not claimed.

## Commits

- fd6add9 log started.
