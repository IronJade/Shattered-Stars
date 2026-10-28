# Handoff: where the project stands (2026-10-28)

**Released** (2026-10-28): Shattered Stars **v0.03.00** (Latest), **so2.apworld 0.2.2**
(`apworld-v0.2.2`) and the **PopTracker pack 1.0.0** (`poptracker-v1.0.0`): tracker support. Log:
docs/handoff/release.md ("RELEASED 2026-10-28"). Before it: v0.02.51 + so2.apworld 0.2.1 (2026-10-27).

**In progress:** the macOS build on the user's Mac (docs/handoff/macos.md; M1 merged, M2/M3 on the
local `macos` branch, to merge now that 0.03.00 is out).

**Next:** so2.apworld **0.3.0**, the entrance randomizer (docs/plans/next.md, Phase C). The
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
restructure.md, macos.md (the macOS build, on the Mac's local `macos` branch).

## Removed files
The finished handoff logs (cited as `docs/handoff/<name>.md` in code comments and data, as "the
<name> handoff log" in the docs), the old docs/HANDOFF.md, docs/archipelago_plan.md ("the
Archipelago plan"), docs/CHECKLIST.md and docs/plans/trackers.md ("the tracker plan") were removed
in commit 0bcf561. Read one with `git show 0bcf561^:docs/handoff/<name>.md` (or that file's path).
They use the old layout: engine/ is port/, and docs/handoff/restructure.md lists every other move.
