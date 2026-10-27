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
2. docs/handoff/windows_check.md: "Recheck for 0.03.00". (pending)
3. Release notes 0.03.00.md, 0.2.2.md, poptracker-1.0.0.md. (pending)
4. README.md: tracker features and the three downloads. (pending)
5. Wiki (scratchpad/wiki, uncommitted): Trackers page + links. (pending)
6. docs/plans/next.md: Phase B status, Phase C pointer. (pending)

## Placeholders left for maptab

- docs/ap/release_checklist.md section 12: rows and notes marked **TODO (maptab)**: the UT map
  tab's files and the package size (12.2), the pack's room placeholders / size (12.2), the world
  tests' count, the identity check's possible new key, the pack checker's counts, the pack's
  tests (rooms, doors, panel, click), the smoke's door-click step, `SS_TEST=tracker_` counts if
  the layout changes, and one row "the map tabs" (layout --check after the area audit, UT's map
  tab loading headless).

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

## Commits

- fd6add9 log started.
