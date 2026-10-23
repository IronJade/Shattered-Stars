# The SO2 APWorld (`apworld/so2`)

Phase 2 of [the Archipelago plan](../archipelago_plan.md): the Archipelago world for Star
Ocean: The Second Story. The game name is "Star Ocean: The Second Story"; it has a colon, so
quote it in YAMLs. The world targets Archipelago 0.6.8's World API and writes one `.apso2`
seed per player ([seed_format.md](seed_format.md)), which the port loads.

## Story mode

**Locations.**
- **Chests:** 311 locations, one per opened flag ([field.md](field.md), "Chests by opened
  flag").
- **Gift sites:** 101, the sites that give an item or Fol.
- **Left out:**
  - two chests and 33 gift sites that give key items (script flags 66-91), because the
    story needs them where they are: Cross Cave's chest (map 547, key flag 68) and Cave of
    Trials level 1's (map 706, key flag 93);
  - prize tables, Private Action ids (their rewards are gift sites), bosses, recruitments
    and shops.
- **Totals:** 412 locations. 81 are in the Cave of Trials, which is off by default; the
  default world has 331.

**Items.**
- The pool is the vanilla contents of the world's locations.
- Item ids:
  - a game item n is `0x50320000 + n` (1-1023);
  - a Fol bundle is `0x50330000 + amount` (10 amounts).
- Equipment (items 75-162 and 485-754) is useful; everything else is filler.
- There are no progression items of our own: the story's key items are flags and stay put.
- The filler name is Blueberry.

**Regions and events.** Menu → Expel → Energy Nede → Fienal → Cave of Trials. Three event
items order the spheres the way the story does:
- "Arrived on Energy Nede" at the disc switch;
- "Saved at the Final Save Point" (Fienal 9F, map 671): it opens the Cave of Trials and
  Indalecio;
- "Victory" at Indalecio, the goal. The seed's `goal.encounters` is [539, 540].

**Expel after the disc switch.** Virtual Expel reopens every Expel map that is `late` in
field.json, so Expel's locations stay reachable and may hold progression
(`expel_progression`).

**Missable locations.** `tools/ap_world_tables.py` gives each location its reasons from
field.json. The counts are over the 412 locations:

| Reason | Meaning | Count |
|---|---|---|
| window | a chapter test closes it (`missable`) | 40 |
| time-limited | its map is not open at chapter 60 (`late` false: old Clik, map 387...) | 8 |
| private action | a Private Action gives it (`private_action`) | 19 |
| optional member | it needs Celine, Bowman, Dias, Precis, Ashton, Leon, Opera, Ernest or Chisato | 9 |
| never | no protagonist can run it | 0 |

It also records `protagonist` ("claude" / "rena") when only one game runs it: 9 Claude-only,
2 Rena-only. These come from the three-valued evaluation of the location's conditions with
flag 1 fixed.

## Options

| Option | Values | Effect |
|---|---|---|
| `goal` | final_boss | Indalecio (539 / 540) |
| `protagonist` | any (default) / claude / rena | claude / rena: the other game's locations are left out; any: one-game locations count as missable |
| `missable_locations` | exclude (default) / include / remove | exclude: EXCLUDED (filler only); remove: left out with their items |
| `expel_progression` | true (default) / false | false: an item rule keeps progression out of Expel |
| `cave_of_trials` | false (default) / true | add the Cave of Trials (after the Final Save Point) |
| `death_link` | false / true | written to the seed as `options.deathlink` |

slot_data repeats the options with `seed_format` and `goal_encounters`.

## The seed (`generate_output`)

`SO2World.seed_data()` writes:
- every own location with its check (chest: map + flag; gift: map + call-site offset) and
  its item (own: kind / game_id / amount; another player's: name / ap_id / player_name);
- its placeholder and its patch lines, from the world agent's `apworld/so2/patches.py`
  (`placement_patches`; every location gets a placeholder, since the port checks gifts only
  through theirs);
- `items`, `start` (start_inventory: items and Fol), `options` and `goal`.

The world never reads the discs. Each site's `word` / `expected` come from field.json.

## Files

| File | Contents |
|---|---|
| `world.py` | `SO2World`, `SO2Web`, `seed_data`, `generate_output`, `fill_slot_data` |
| `items.py`, `locations.py`, `regions.py`, `rules.py`, `options.py` | as named |
| `data.py` | generated tables (do not edit) |
| `patches.py` | placeholder patches (world agent) |
| `archipelago.json`, `docs/` | manifest, setup and game pages |
| `test/` | WorldTestBase tests (7 classes, 30 tests with AP's default ones) |

Regenerate the tables after `tools/ap_extract.py`:

    tools/ap_world_tables.py          # field.json + battles.json -> apworld/so2/data.py

## Testing (scratch install)

The install is `scratchpad/ap` (tools/ap_test_server.sh: uv, Python 3.13, `ap-venv`, AP
0.6.8, pytest and pytest-xdist added). Always set `SKIP_REQUIREMENTS_UPDATE=1`, or
ModuleUpdate waits at a prompt. Link the world in:

    ln -sfn $PWD/apworld/so2 scratchpad/ap/Archipelago/worlds/so2
    cd scratchpad/ap/Archipelago
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q worlds/so2/test
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 test/general
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python Generate.py --player_files_path P --outputpath O --seed N

Results (2026-10-23):
- The world's tests: 30 passed (1687 subtests).
- AP's general tests with so2 installed: 379 passed (116700 subtests).
- 50 seeds, two SO2 players with random options plus ChecksFinder
  (`scratchpad/enemy/ap2/batch/run_batch.sh`): all generate and fill. That is 100 `.apso2`
  files over 32 option combinations, 258-412 locations each. Every file has unique ids and
  placeholders and well-formed patch lines.
- In the port (headless, `--ap-seed`):
  - a ChecksFinder + SO2 seed loads with all 356 patch lines;
  - the chests on maps 18, 67 and 402 and the gifts on maps 402 and 233 pay what the
    spoiler says, including "Map Bombs for Alice";
  - the inventory holds the placed items, not the vanilla ones;
  - a start_inventory seed gives its starting items.

## Next

- Phase 3 (agreed with the world agent, a6cb3fe962f8c58f8):
  - character items with join checks (`{"type": "join", "map", "offset"}`; per-site ids
    still to add to ap_extract), first-drop checks (location type 8), traps, multipliers;
  - the pool will hold a consistent party (Leon or Dias by protagonist, one of Bowman /
    Precis, Ashton or Opera + Ernest).
- Boss checks: `battles.json` `boss_encounters` (47), checked by the port already.
- The port should hold the player to the seed's `protagonist`.
- A `.apworld` zip for custom_worlds (phase 5).
