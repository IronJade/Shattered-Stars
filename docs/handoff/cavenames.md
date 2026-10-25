# Handoff: the Cave of Trials' boss names (agent "cavenames")

The user's ruling: Cave of Trials locations are named by their real floor. skillsanity2 renamed
the 76 chests and gifts this way (tools/ap_world_tables.py `CAVE_FLOORS`; docs/handoff/skillsanity.md
"skillsanity2") but left three boss locations wrong, because the boss-naming code used a different,
older rule (the nearest map's banner text below it) instead of `CAVE_FLOORS`.

## What changed

`tools/ap_world_tables.py` `build()`: a cave boss's `place` was set twice — once correctly from
`area_label(s0)` (the same `RELEASED_AREAS` / `CAVE_FLOORS` table the chests use), then immediately
overwritten for `cls == "cave"` by a walk backward from the boss's map to the nearest map in
`field.json`'s `map_names` with a banner string. That walk is gone; `place` now stays the
`area_label(s0)` value for every boss, cave or not.

Checked all 13 Cave of Trials boss encounters (field.json `bosses`, maps 698-825; `data.py`'s own
count: `boss classes {..., 'cave': 13}`) against `CAVE_FLOORS`: 10 already matched (D-feather 705
Level 1, Bloodgerell 716 Level 2, Dreamshade 727 Level 3, Miel32 737 Level 4, F-thiefL99 756
Level 6, M-eater 763 Level 7, E-beast 775 Level 8, Wisesorcerer 786 Level 9, Geo-guardian 798
Level 10, D-tyrant 807 Level 11). 3 were wrong and are now fixed:

| Boss | Map | Encounter | Location id | Old name | New name |
|---|---|---|---|---|---|
| Weirdbeast | 711 | 507 | 1364328955 | `Cave of Trials Level 1: Weirdbeast boss` | `Cave of Trials Weird Beast Arena: Weirdbeast boss` |
| Phoenix | 809 | 514 | 1364328962 | `Cave of Trials Level 11: Phoenix boss` | `Cave of Trials Level 12: Phoenix boss` |
| G-Celesta | 815 | 515 | 1364328963 | `Cave of Trials Level 12: G-Celesta boss` | `Cave of Trials Level 13: G-Celesta boss` |

Ids and every other field (map, encounter, region, item, missable, story_route, boss class, group,
late/late_keys/tele/tele_keys) are unchanged; only `name` and `area` differ, and only for these 3
rows (3 fixed + 10 already-matching = all 13 cave boss encounters).

## Files changed

- `tools/ap_world_tables.py`: removed the obsolete `map_names`-banner override for `cls == "cave"`
  in `build()` (now a 4-line comment + the existing `area_label(s0)` line stands alone).
- `apworld/so2/data.py`: regenerated (`python3 tools/ap_world_tables.py -o apworld/so2/data.py`).
  Diff is exactly the 3 boss rows above (`name`/`area` only). A second regeneration (`cmp`) is
  byte-identical.
- `docs/ap/apworld.md`: the "Changes" 0.2.0 entry "The Cave of Trials by its real floors" ended
  with "The cave's boss names are another rule's and unchanged." — replaced with a new dated bullet
  (agent cavenames) describing the fix and the three renames.
- `apworld/so2/docs/en_Star Ocean The Second Story.md`: checked (grep for "Cave of Trials Level",
  "Weird Beast Arena", "Treasure Vault", "Phoenix", "G-Celesta", "Weirdbeast"); the guide never
  names a Cave of Trials boss by its full location string (only mentions "G-Celesta gives you ...
  the Silver Trumpet" as an in-game item, unrelated to location naming) — nothing to change.

## Not touched (out of scope, noted for the record)

- `docs/ap/hub_proof.json` line ~20962 has a stale `"name": "Cave of Trials Level 1: Weirdbeast
  boss"` row. This file is a captured snapshot from a past hub e2e proof run
  (`tools/ap_late_proof.py --state hub`, run against the real game); its `name` field is
  descriptive only (`ap_world_tables.py` matches hub-proof rows by `id`, never by `name`), so the
  stale text doesn't affect anything the build reads. Refreshing it needs an actual game run,
  which this task said isn't needed and the scope didn't list this file. Left as is.
- No test in `apworld/so2/test/` names these locations by string or id (checked: no hits for
  1364328955 / 1364328962 / 1364328963 or "Phoenix"/"G-Celesta"/"Weirdbeast"/"Weird Beast" in
  `apworld/so2/test/`), so no test file needed updating.

## Tests (docs/handoff/apworld2.md "How to verify")

    ln -sfn $PWD/apworld/so2 scratchpad/ap/Archipelago/worlds/so2   # already linked
    cd scratchpad/ap/Archipelago
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 worlds/so2/test
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 test/general

- World tests: **380 passed** (29332 subtests), matching baseline.
- AP general tests: **379 passed** (117754 subtests), matching baseline.
- `python3 tools/ap_world_tables.py -o <tmp>.py && cmp <tmp>.py apworld/so2/data.py`: OK, twice.

## Note

While this was running, another process committed the large pending skillsanity/difficulty batch
(commit `e5e1ccb`, "so2.apworld 0.2.0: skillsanity, join levels by the party median, the difficulty
option") that had been staged since before this task started. That commit is not this agent's; this
agent made no commits (per its rules) and the 3-file diff above (`tools/ap_world_tables.py`,
`apworld/so2/data.py`, `docs/ap/apworld.md`) sits unstaged on top of it.

## Done

- 2026-10-25: renamed the 3 boss locations, regenerated and verified `data.py` (cmp-identical
  twice), ran world + AP general tests (380 / 379, both matching baseline, nothing to update),
  updated `docs/ap/apworld.md`, checked the player guide (nothing to change there). Not committed.
