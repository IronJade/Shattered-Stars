# Handoff: where the project stands (2026-10-28)

**Released** (2026-10-27): Shattered Stars **v0.02.51** and **so2.apworld 0.2.1** (`apworld-v0.2.1`).

**Built, not released:** port **0.03.00**, so2.apworld **0.2.2** and the PopTracker pack **1.0.0**
(tracker support). Next: rerun the release candidate on the new layout
(`bash scratchpad/release/rc-0.03.00.sh all`; docs/release/release_checklist.md section 12; log
docs/handoff/release.md), then the user's Windows check (docs/handoff/windows_check.md "Recheck
for 0.03.00"), then publishing on the user's go-ahead (checklist 12.6: the three releases,
trackers/poptracker/versions.json on main, `tools/wiki_publish.sh --push`).

**After that:** so2.apworld **0.3.0**, the entrance randomizer (docs/plans/next.md, Phase C). The
Japanese discs are deferred (same file).

## Layout (since 2026-10-28, docs/handoff/restructure.md)
- `port/` the C++ port: src/, tests/, data/, res/, tools/ (builds, smoke, sweeps, refs/, mod tools)
- `apworld/` so2/ (the Archipelago world) and tools/ (extractors, batches, e2e, build_apworld.sh)
- `trackers/` poptracker/ (the pack's Lua and boards) and tools/ (generator, tests, maps, UT check)
- `mods/` the curated mod list (its own CI, .github/workflows/mods.yml)
- `docs/` port/ (catalog, notes), apworld/, trackers/, release/ (checklist, notes/), modding/,
  images/, plans/, handoff/; CONVENTIONS.md holds the standing rules
- `wiki/` the GitHub wiki's pages, published by tools/wiki_publish.sh
- `tools/` shared: the so2_* readers, so2_mod.py, memcap.py, the RE scripts, checkpoint.sh

Build: `cmake -S port -B port/build -G Ninja && cmake --build port/build` (port/README.md).

## Live handoffs (docs/handoff/)
release.md (the 0.03.00 RC), phaseb.md (the tracker build), windows_check.md, utzip.md,
restructure.md.

## Removed files
The finished handoff logs (cited as `docs/handoff/<name>.md` in code comments and data, as "the
<name> handoff log" in the docs), the old docs/HANDOFF.md, docs/archipelago_plan.md ("the
Archipelago plan"), docs/CHECKLIST.md and docs/plans/trackers.md ("the tracker plan") were removed
in commit 0bcf561. Read one with `git show 0bcf561^:docs/handoff/<name>.md` (or that file's path).
They use the old layout: engine/ is port/, and docs/handoff/restructure.md lists every other move.
