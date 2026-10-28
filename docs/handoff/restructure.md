# Restructure (2026-10-28)

Agent "restructure": prune outdated docs, bring the wiki into the repo, reorganise the repo into
sections (Port, APWorld, Trackers, Docs, Wiki), verify. The coordinator reruns the 0.03.00 RC on
the new layout afterwards. Start commit: 0fefa1b.

## Rules (from the coordinator, the user's decisions 2026-10-28)
- Layout ("Full sections, port/"): port/ (engine/ renamed; port/res, port/tools), apworld/ (so2/,
  tools/), trackers/ (poptracker/, tools/), mods/ (unchanged), docs/ (port/, modding/, apworld/,
  trackers/, release/, images/, plans/, handoff/), wiki/, tools/ (shared + RE only).
- Step 1 prune docs (own commit), step 2 wiki/ + publish script (own commit), step 3 moves (as few
  commits as keep each building; docs first), step 4 verify + new docs/HANDOFF.md.
- Published release notes (0.01.00, 0.02.00, 0.02.50, 0.02.51, 0.2.1) stay as written.
- Headless / -cli only; no GUI, no config saves. Game processes under tools/memcap.py, max 2.
- Big scratch in scratchpad/restructure/. Cap output. Waits have deadlines. No `pgrep -f`.
- Disc images and ~/.local/share/Archipelago read-only. Never push/tag/release/publish the wiki.
- Commit each verified step locally, ending with the Co-Authored-By line.
- A refused command: stop, record it here, report.
- Don't run the RC; fix rc-0.03.00.sh paths + the ut_map zip-path bug.

## Status
- [x] Step 1 prune docs (commit: see log)
- [ ] Step 2 wiki
- [ ] Step 3 moves
- [ ] Step 4 verify + HANDOFF.md

## Log
- start: no RC / game processes running; last RC (rc030-coord) stopped after `identity`.
- step 1: `git rm` 85 docs/handoff logs (kept release, phaseb, windows_check, utzip + this log),
  docs/HANDOFF.md, docs/archipelago_plan.md, docs/CHECKLIST.md, docs/plans/trackers.md.
  - plans/trackers.md: every rule that still governs behaviour is already in docs/ap/poptracker.md /
    tracker_maps.md (versions.json path D4, no `apmanual`/`aphintgame`, nothing from the disc D5,
    user-override, community-packs D7). Its section 6 (ER + trackers) folded into docs/plans/next.md
    Phase C ("From the tracker plan (section 6), for ER").
  - old HANDOFF.md: "Conventions to keep" -> new docs/CONVENTIONS.md (verbatim; referenced by
    next.md, images/README.md, release_checklist.md, memcap.py, smoke script); "Debug options" ->
    engine/README.md "Debugging switches" (it pointed there); the ASan recipe -> engine/README.md Tests.
  - docs rewording: `docs/handoff/NAME.md` -> "the NAME handoff log" in docs/ap/*.md, docs/notes,
    docs/catalog/COVERAGE.md, docs/images/README.md, engine/README.md, tools/refs/README.md, next.md
    (scratchpad/restructure/fix_handoff_refs.py); archipelago_plan / plans/trackers refs reworded
    ("the tracker plan's B3", "the Archipelago work").
  - Left on purpose (covered by the docs/HANDOFF.md removed-files line): code comments in
    apworld/so2, engine/src and tools citing removed handoff logs / plans; the generated "about"
    strings in docs/ap/hub_*.json; docs/modding/reference/map_names.csv; the kept live handoff logs.
  - next.md trimmed: phases A/B/party -> "Done" (3 bullets); Japan deferral and Phase C intact.
