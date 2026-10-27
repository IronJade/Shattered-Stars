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

1. docs/ap/release_checklist.md: new section for this release. (pending)
2. docs/handoff/windows_check.md: "Recheck for 0.03.00". (pending)
3. Release notes 0.03.00.md, 0.2.2.md, poptracker-1.0.0.md. (pending)
4. README.md: tracker features and the three downloads. (pending)
5. Wiki (scratchpad/wiki, uncommitted): Trackers page + links. (pending)
6. docs/plans/next.md: Phase B status, Phase C pointer. (pending)

## Placeholders left for maptab

(none yet)

## Facts not confirmed / sources that disagree

(none yet)

## Commits

(none yet)
