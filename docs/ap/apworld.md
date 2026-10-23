# The SO2 APWorld (`apworld/so2`)

Phases 2 and 3 of [the Archipelago plan](../archipelago_plan.md): the Archipelago world for Star
Ocean: The Second Story. The game name is "Star Ocean: The Second Story"; it has a colon, so
quote it in YAMLs. The world targets Archipelago 0.6.8's World API and writes one `.apso2`
seed per player ([seed_format.md](seed_format.md)), which the port loads.

## Locations

Story mode's locations come from the discs through `docs/ap/field.json` (`tools/ap_extract.py`,
[field.md](field.md)); `tools/ap_world_tables.py` turns them into `data.py`, 560 rows:

| Kind | Rows | Option | Id ([field.md](field.md), "Location ids") |
|---|---|---|---|
| Chests | 311 (78 in the Cave of Trials) | always | type 1, one per opened flag |
| Gift sites | 101 (3 in the cave) | always | type 2, one per reward site that gives an item or Fol |
| Boss fights | 41 (13 in the cave) | `boss_checks` (default on) | type 9 + encounter |
| Join sites | 18 | `character_items` | type 4, one per site |
| First drops | 89 (3 met only in the cave) | `drops_as_checks: first_drop` | type 8 + enemy type |

- **Left out:**
  - two chests and 33 gift sites that give key items (script flags 66-91), because the
    story needs them where they are: Cross Cave's chest (map 547, key flag 68) and Cave of
    Trials level 1's (map 706, key flag 93);
  - prize tables, Private Action ids (their rewards are gift sites), recruitments other than
    the join sites, and shops (phase 4);
  - the goal fights 539 / 540, and boss encounters 0 / 263 / 534 / 561, which no script
    starts with a constant encounter.
- **The Cave of Trials** (`cave_of_trials`, off by default) adds its 81 chests and gift sites
  and its 13 bosses.
- **Bosses** by class: 21 story bosses (the story fights them; a loss is a game over), 13 cave
  bosses (each floor's door), 4 optional (Flarelizard, Nightmare, XINE, Ghost: side quests
  other choices rule out) and 3 losable (the script lets the party lose and goes on: Dias at
  the tournament 204, Shin on the ship 517, Marsilio's first Fienal fight 530).
- **Join sites** (character_items), agreed with the port side: Celine 124 / 125; Bowman 208;
  Dias 130 / 439 / 229 (0x7c9c); Precis 209; Ashton 71 (three states of his quest); Leon 230 /
  229 (0x73dc, the Rena-game exception: listed so he never joins there for free); Opera 374 and
  479 (three); Ernest 479 (0x7c30); Chisato 242. Not listed: the scenes that take a member
  along for a while and the restores after a split, which the port plays as the original.
  Celine's first join (map 74) is one of those: she leaves the party at Mars in both games
  (Claude's: the Varmillion scene on map 462, chapter 15 step 2400, whatever flag 112 holds;
  Rena's: map 126, step 1000; checked headless) and comes back at 124 / 125. Noel joins in the
  story and is not shuffled.
- **First drops:** the 89 enemy types that random encounters meet and that drop something.
  Each one's region is the earliest region where the type is met; its pool item is the type's
  first drop.

## Items

| Item | AP id | Classification |
|---|---|---|
| a game item n (1-1023) | `0x50320000 + n` | equipment (75-162, 485-754) useful, the rest filler |
| a Fol bundle (10 amounts) | `0x50330000 + amount` | filler |
| a character (Celine, Bowman, Dias, Precis, Ashton, Leon, Opera, Ernest, Chisato) | `0x50340000 + party id` (1 Claude ... 12 Chisato) | useful |
| a trap: Ambush, Fol Loss (10 %), Poison, Paralysis, Stone | `0x50350000 + 1..5` | trap |
| an area key (phase 4) | `0x50360000 + N` | (phase 4) |

- **The pool** is the vanilla contents of this world's locations. A boss, join or drop
  location adds its vanilla item (a Blueberry for bosses and join sites); the party's
  characters replace join sites' Blueberries; `traps` % of the filler becomes traps.
- There are no progression items of our own: the story's key items are flags and stay put.
- **The cave's gems stay put.** The Cave of Trials doors on maps 751 and 764 take the Red Lotus
  Gem (317; gifts on maps 711 and 754) and test the Cracked Gem (289; the chest on map 771).
  Those three locations keep their own item, locked (`items.PUZZLE_ITEMS`), so a door never
  needs a gem placed behind it.
- The filler name is Blueberry.

**The party** (character_items, `world.choose_party`): one of Bowman / Precis (Precis rules
Bowman out); Ashton, or Opera with Ernest (Ashton rules Opera out, Ernest comes only with
Opera); Celine; Chisato; Leon in Claude's game or Dias in Rena's (neither with protagonist
"any"). With Claude, Rena and Noel the party is at most 8: when there is one too many, Ernest
or one of the single members stays out. The seed's `options.characters` lists them.

## Regions and events

Menu → Expel → Energy Nede → Fienal → Cave of Trials. Three event items order the spheres the
way the story does:
- "Arrived on Energy Nede" at the disc switch;
- "Saved at the Final Save Point" (Fienal 9F, map 671): it opens the Cave of Trials and
  Indalecio;
- "Victory" at Indalecio, the goal. The seed's `goal.encounters` is [539, 540].

## Logic

Story mode needs no items, so the only risk is a location that holds progression and can be
lost. Two rules keep that out.

**1. Missable locations.** `tools/ap_world_tables.py` gives each location its reasons, from
field.json's conditions (three-valued evaluation with flag 1 fixed per game):

| Reason | Meaning | Rows |
|---|---|---|
| window | a chapter test closes it (`missable`) | 54 (40 gifts, 13 joins, 1 boss) |
| time-limited | its map is not open at chapter 60 (`late` false: old Clik, map 387...) | 9 |
| private action | a Private Action gives it | 19 gifts |
| optional member | it needs Celine, Bowman, Dias, Precis, Ashton, Leon, Opera, Ernest or Chisato | 9 gifts |
| join | a join site (character_items) | 18 |
| optional quest | an optional boss | 4 |
| losable | a fight the story lets the party lose | 3 |
| protagonist | only one game runs it (under protagonist "any") | Claude / Rena only: gifts 9 / 2, bosses 1 / 1, joins 2 / 4 |

"window" and "time-limited" are soft: a diligent player avoids them, so `missable_locations:
include` makes them ordinary locations (but see rule 2). Every other reason is hard: two such
locations can rule each other out (a choice, a member, the other game), so they are always
EXCLUDED (filler only), whatever the option. `remove` leaves out every missable location but the
join sites.

**2. The late proof: progression only where the end of the game can still reach.**
Archipelago assumes a location stays reachable once it is in logic, but maps close in Star
Ocean 2: Expel at the disc switch (Virtual Expel reopens it after the Final Save Point), towns
as the story changes them. The story always reaches the Final Save Point, so a location that
can still be done from there can never be lost. `tools/ap_late_proof.py` proves which ones,
headless in the port, and writes `docs/ap/late_proof.json`; `ap_world_tables.py` copies the
games each location was proven in into its row (`late`).

- **The state:** the user's own save at the Final Save Point (a Claude game at chapter 60,
  step 4000, with flag 730 from that save, which opens Virtual Expel, the Fienal floor globals
  and 1406 story flags), with every location's own "done" flags cleared (each chest's opened
  flag, each gift's guard flags, each cave boss's guard flag: 378 flags), as for a player who
  skipped them all. The message speed (a setting) stays the `--map` default. Rena's game is
  the same state with flag 1 cleared and the protagonist bytes swapped. The party is Claude,
  Rena and Noel.
- **The search:** breadth-first over event sweeps, one headless process per node. Each sweep
  fires every trigger, talk and pickpocket entry of the map from the same snapshot, answers
  windows and choices, and wins battles at once (`SS_BATTLE_STUB`, which reports the victory
  to the seed). An entry that leaves the player in control on another map is an edge; Private
  Action entries are not followed. A node found later is entered by replaying its route from
  its root entry by entry (`SS_SWEEP_PLAY`), so it has the globals and flags the real
  transfers leave. A map whose script reads the floor G[0x1cf], the previous map G[0x1c0] or
  Fienal's stair flag 157 (Fienal's stair rooms, the Cave of Trials, a few dungeon rooms) is a
  node per value of what it reads, which the sweep prints after every entry (`SS_SWEEP_SHOW`);
  an entry that changes it without leaving (a stair latch) is an edge too. A run whose map
  reloads into another map over and over is stopped and resumed after the entry.
- **The roots:** map 671, and Energy Nede's world map: the party walks back out of Fienal
  after saving (Prima p. 130), but the sweep can't follow that through Fienal's stair rooms.
- **Proven:** the port loads a proof seed with every chest, gift and boss location, and a
  location is proven in a game when a sweep of that game's search logs its `[ap] check`.
- **The rule** (rules.py `may_hold_progression`): a location may hold progression only when it
  was proven in the protagonist's game (in both games under "any"). Story bosses are exempt
  (the story fights them); first drops (dice rolls) and join sites never may; with
  `expel_progression` off, nothing in Expel may.
- **Conservative by design:** every entry starts from the map's arrival state, so doors behind
  switches, puzzles the save had not solved or a choice's other answers stay shut; such
  locations only lose the right to hold progression.

**Results** (2026-10-23, `docs/ap/late_proof.json`). Each game's search swept 610 maps (732
nodes in Claude's game, 731 in Rena's), and both prove the same 278 of the 453 chests, gifts and
bosses:

| | Rows | Proven |
|---|---|---|
| Chests | 311 | 258: Expel 122 / 155, Energy Nede 46 / 52, Fienal 17 / 26, Cave of Trials 73 / 78 |
| Gift sites | 101 | 9: Expel 6 / 89, Energy Nede 1 / 9, Cave of Trials 2 / 3 |
| Bosses | 41 | 11: the cave's 10 / 13 and one in Expel (story bosses are exempt anyway) |

- Chests not proven: Eluria Tower's 23 (the tower can't be entered after the disc switch), old
  Clik's 4 and the Heraldry Weapons Laboratory's 1 (as `late` said), Hoffmann Ruins map 502
  (4), the Mountain Palace's map 367 (2), North City map 593 (3), the Field of Intelligence map
  550 (2), Fienal's stair rooms 647 / 673 (9: floor states the search did not reach) and five
  cave chests behind puzzles (maps 710, 740, 798, 799).
- Gifts are mostly rewards of story scenes tied to a chapter or step, which no longer run at
  chapter 60; most were excluded already (window, Private Action, member).
- So a seed puts progression in at most 258 chests, 9 gifts, the 21 story bosses and 10 cave
  bosses (fewer with the options): every multiworld in the batch below filled.
- Rerun the proof after a change to the port's field code, the event sweep or field.json
  ("Files", below).

**Characters are useful, never progression.** Member-gated logic would not be monotonic
(Ashton rules out Opera), so every member-gated location is excluded instead.

## Options

| Option | Values | Effect |
|---|---|---|
| `goal` | final_boss | Indalecio (539 / 540) |
| `protagonist` | any (default) / claude / rena | claude / rena: the other game's locations are left out and the port starts that game; any: one-game locations are missable |
| `missable_locations` | exclude (default) / include / remove | exclude: EXCLUDED (filler only); include: soft reasons become ordinary; remove: left out with their items |
| `expel_progression` | true (default) / false | false: no progression in Expel at all |
| `cave_of_trials` | false (default) / true | add the Cave of Trials (after the Final Save Point) |
| `boss_checks` | true (default) / false | the 41 boss fights are locations |
| `character_items` | false (default) / true | the party's members join through items, their join sites are locations |
| `drops_as_checks` | off (default) / first_drop | each enemy type's first drop is a location |
| `traps` | 0-100 (default 0) | the share of filler turned into traps |
| `exp_multiplier`, `fol_multiplier` | 1-10 (default 1) | the battle's EXP / Fol total |
| `encounter_rate` | 0-200 (default 100) | random-encounter frequency, percent |
| `death_link` | false / true | written to the seed as `options.deathlink` |

slot_data and the seed's `options` repeat them, with `seed_format`, `goal_encounters` and
`characters` (the party). The port reads the phase-3 ones ([seed_format.md](seed_format.md),
"Phase 3").

## The seed (`generate_output`)

`SO2World.seed_data()` writes:
- every own location with its check (chest: map + flag; gift: map + call-site offset; boss:
  encounter; join: map + offset + character; drop: enemy type) and its item (own: kind and
  its fields; another player's: name / ap_id / player_name);
- each chest's and gift's placeholder and patch lines, from the world agent's
  `apworld/so2/patches.py` (`placement_patches`; every chest and gift location gets one, since
  the port checks gifts only through theirs);
- `items`, `start` (start_inventory: items and Fol), `options` and `goal`.

The world never reads the discs. Each site's `word` / `expected` come from field.json.

## Files

| File | Contents |
|---|---|
| `world.py` | `SO2World` (`choose_party`, `create_items`), `SO2Web`, `seed_data`, `generate_output`, `fill_slot_data` |
| `items.py`, `locations.py`, `regions.py`, `rules.py`, `options.py` | as named |
| `data.py` | generated tables (do not edit) |
| `patches.py`, `doors.py` | placeholder patches; the phase-4 door validator (world agent) |
| `archipelago.json`, `docs/` | manifest, setup and game pages |
| `test/` | WorldTestBase tests |

Regenerate the tables after `tools/ap_extract.py` or a new late proof:

    tools/ap_world_tables.py          # field.json + battles.json + late_proof.json -> data.py

Rerun the late proof after a change to the port's field code, the event sweep or field.json
(about an hour per game; keep the machine's total at 6 game processes: `--jobs 3` each, or one
game after the other; logs in scratchpad/late_proof/, `--resume` reuses finished nodes):

    tools/ap_late_proof.py --bin BIN --game claude --jobs 3 -o scratchpad/late_proof/claude.json &
    tools/ap_late_proof.py --bin BIN --game rena --jobs 3 -o scratchpad/late_proof/rena.json &
    wait; tools/ap_late_proof.py --reparse --game claude --game rena   # -> docs/ap/late_proof.json

It needs SS_DISC1 (disc 2 next to it) and a memory card with a save at the Final Save Point
(`--save`, default `scratchpad/latesave/card.mcd`, `--save-frame 13`).

## Testing (scratch install)

The install is `scratchpad/ap` (tools/ap_test_server.sh: uv, Python 3.13, `ap-venv`, AP
0.6.8, pytest and pytest-xdist added). Always set `SKIP_REQUIREMENTS_UPDATE=1`, or
ModuleUpdate waits at a prompt. Link the world in:

    ln -sfn $PWD/apworld/so2 scratchpad/ap/Archipelago/worlds/so2
    cd scratchpad/ap/Archipelago
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 worlds/so2/test
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 test/general
    SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python Generate.py --player_files_path P --outputpath O --seed N

Results (2026-10-23):
- The world's tests: 66 passed (4793 subtests; 17 classes: the defaults, protagonists,
  missable options, the cave and its gems, Expel without progression, bosses off, characters
  per protagonist, drops and traps, the late proof per protagonist).
- AP's general tests with so2 installed: 379 passed (116824 subtests).
- 60 seeds (`scratchpad/enemy/ap4/batch/run_batch.sh`): two SO2 players (Claude, Rena) with
  every option random (protagonist, missable_locations, expel_progression, cave_of_trials,
  boss_checks, character_items, drops_as_checks, traps, both multipliers, encounter_rate,
  death_link, accessibility) plus ChecksFinder. All generate and fill: 120 `.apso2` files over
  101 option combinations, 258-560 locations each. `check.py` finds no error: unique ids and
  placeholders, well-formed patch lines (one per word), the cave's gems in place, a party the
  story allows with every member in the items table, and each of the 1295 progression items
  placed in SO2 locations where `may_hold_progression` allows it.
- In the port (headless, engine/build-enemy):
  - `tools/ap_e2e_so2.sh`: 19 / 19, twice (the phase-3 steps: a received Celine joins, a Fol
    Loss Trap takes 10 %, Kitty's first drop goes out as a check);
  - three batch seeds (Claude's, Rena's and "any"): a received character joins, a Fol Loss
    trap takes 1000 of 10000 Fol, the rewards scale (EXP x2 / x6 / x8, Fol x6 / x7 / x9), and
    the first drop of a random-encounter enemy (encounter 110, Kitty, type 90) is withheld
    and checked (an own item, or "Bloody Helm for Claude" in the other slot);
  - Ashton's join site (map 71, a sweep at chapter 17 with flag 113): the check goes out, Ashton
    is held back (not received), and the site's item (a Paralysis Trap) is given.

## Next

- Phase 4 (open world, shop shuffle; seed fields confirmed in [seed_format.md](seed_format.md),
  "Phase 4"): area keys on the world-map entrances, the Expel teleporter, shop slots.
- A `.apworld` zip for custom_worlds (phase 5).
