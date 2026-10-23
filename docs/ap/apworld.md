# The SO2 APWorld (`apworld/so2`)

Phases 2 to 4 of [the Archipelago plan](../archipelago_plan.md): the Archipelago world for Star
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
| Shop slots | 550 (98 specials) | `shuffle_shops: specials / full` | type 10 + (shop << 8) + entry |

- **Left out:**
  - two chests and 33 gift sites that give key items (script flags 66-91), because the
    story needs them where they are: Cross Cave's chest (map 547, key flag 68) and Cave of
    Trials level 1's (map 706, key flag 93);
  - prize tables, Private Action ids (their rewards are gift sites), recruitments other than
    the join sites, and the shop *site* records (type 5; the slots are type 10, "Shop slots"
    below);
  - the goal fights 539 / 540, and boss encounters 0 / 263 / 534 / 561, which no script
    starts with a constant encounter.
- **The Cave of Trials** (`cave_of_trials`, off by default) adds its 81 chests and gift sites
  and its 13 bosses.
- **Bosses** by class: 21 story bosses (the story fights them; a loss is a game over; two are
  timed survivals the port checks when the minute runs out, Shin at the Lacour Front Line 516
  and Berle in Eluria Tower 520), 13 cave bosses (each floor's door), 4 optional (Flarelizard,
  Nightmare, XINE, Ghost: side quests other choices rule out) and 3 losable (the script lets
  the party lose and goes on: Dias at the tournament 204, Shin on the ship 517, Marsilio's
  first Fienal fight 530; the port checks them on the scripted defeat as on a win). See
  "DeathLink and the fights you can't win".
- **Join sites** (character_items), agreed with the port side: Celine 124 / 125; Bowman 208;
  Dias 130 / 439 / 229 (0x7c9c); Precis 209; Ashton 71 (three states of his quest); Leon 230 /
  229 (0x73dc, the Rena-game exception: listed so he never joins there for free); Opera 374 and
  479 (three); Ernest 479 (0x7c30); Chisato 242. Not listed: the scenes that take a member
  along for a while and the restores after a split, which the port plays as the original.
  Celine's first join (map 74) is one of those: she leaves the party at Mars in both games
  (Claude's: the Varmillion scene on map 462, chapter 15 step 2400, whatever flag 112 holds;
  Rena's: map 126, step 1000; checked headless) and comes back at 124 / 125. Noel joins in the
  story and is not shuffled. Several sites depend on a choice (the world agent, 2026-10-23):
  Celine's real recruitment is the Cross Cave choice (map 524, chapter 13); map 124 runs only
  if she was sent away there and not received, 125 needs flag 112, and in Rena's game sending
  her away leaves no Celine site; Leon (234) and Opera (479) have parting choices. So a join
  site can stay unreachable: every join site is EXCLUDED and never holds progression (hard
  reason "join"), and the port rejoins a received member after the partings.
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
| an area key (open world): "Salva Pass" ... (35) | `0x50360000 + N`, N 1-35 (data.KEY_GROUPS) | progression |

- **The pool** is the vanilla contents of this world's locations. A boss, join or drop
  location adds its vanilla item (a Blueberry for bosses and join sites); the party's
  characters replace join sites' Blueberries; `traps` % of the filler becomes traps.
- There are no progression items of our own in story mode: the story's key items are flags
  and stay put. Open world adds the 35 area keys (progression); each takes the place of a
  filler item in the pool.
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
| losable | a fight the story lets the party lose (the port checks it on the scripted defeat as on a win; the reason and its filler-only rule stay) | 3 |
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

## Open world (`world_mode: open_world`, phase 4)

The port locks each world-map entrance until its area's key arrives ([seed_format.md](seed_format.md),
"Phase 4"). The story still runs in its order, so the keys the story's areas need are the
seed's progression.

**Keys.** One per entrance group of `docs/ap/doors.json` (`key_group`: the 66 entrances of the
two world maps, 44 Expel, 22 Nede, in 35 groups), key N = the group's place in doors.json order,
named "<area> Pass" (`tools/ap_world_tables.py` KEY_NAMES; "Map 231 Pass" for the one-map
area the story visits at chapters 24-25, "Fienal Pass" for map 687, Fienal's entrance). The
seed's `doors` lists all 66 with their key; `apworld/so2/doors.py` validates it in the tests.
**Starting keys:** the Arlia Pass (the start) and the Fun City Pass (the teleporter's way back
to Expel, and the only way to Virtual Expel), plus the North City Pass with the teleporter off
(else nothing on Energy Nede is reachable after the switch).

**The story's stages** (data.STAGES, from every chapter / step store with its conditions and
the area of its map; `scratchpad/enemy/ap5/steps.py`): 23 stages, 12 on Expel (chapters 3-35)
and 11 on Energy Nede (36-60). Each is an event that needs the previous one and the keys of the
areas the story walks into in it:

| Stages | Chapters | Keys |
|---|---|---|
| 1-4 | 3-13 | Arlia; Salva, Arlia; Cross; Cross Cave |
| 5-8 | 14-22 | Clik, Mars, Herlie; Herlie, Hilton, Lacour (the Herlie ship lands at Hilton, and chapter 18 is stored entering Lacour from the world map); Lacour (the tournament, in Lacour Castle's coliseum); Linga, Sanctuary of Linga |
| 9-12 | 23-35 | Hilton, Lacour, Map 231, Hoffmann; Lacour Front Line; El Beach, Eluria Colony; Eluria Tower. The disc switch needs stage 12. |
| 13-17 | 36-41 | none (the scenes after the switch); North City; Noel's House; Red Crystal; Central City |
| 18-20 | 42-51 | the four Fields (Courage, Power, Intelligence, Love) and Armlock; Central City, L'Aqua; Fienal, L'Aqua, Central City |
| 21-23 | 52-60 | Armlock, Mihne; Fun City; Armlock, L'Aqua, Fienal. The Final Save Point needs stage 23. |

A stage must list every key its story needs, from the stage the story first walks into the
area: a key listed a stage late leaves the earlier stage passable without it, and one listed a
stage early moves the area's *early* mode (below) before the story can reach it. Not the
story's: Lasguss Mountain
and the Mountain Palace (Ashton's quest, Prima pp. 48-56), Cave of Trials Level 1, Giveaway, and
map 463's entrance on the Nede map ("Gallery Pass").

**A location is reached** (rules.py `open_world_access`) by any of:
- *early*: the stage before the one that first needs its area's key, plus that key (its area:
  the entrance group whose entrances reach its map over unconditional field doors, else the
  nearest one; a gift not before its window's first chapter; Fienal's floors from stage 23; a
  story boss once its stage is done). An area the story reaches by ship also needs the key of
  the entrance group the ship lands through (data.KEY_GROUPS `landing`): the Lacour continent
  (Lacour's group) through Port Hilton, its only harbour (the Herlie ship, flag 18, world door
  59578036), and Hoffmann Ruins through map 231, its beach (the Hilton ship, flag 22, chapters
  23-27, world door 59578306);
- *teleporter* (Expel, `expel_teleporter` on): the "Expel Teleporter" event (stage 13 done and the
  Fun City Pass: chapter 38, the old man in the arena) plus the keys of the world-map entrances
  on the teleporter proof's route to its map;
- *late*: the Final Save Point plus the keys on the late proof's route (Fun City for Expel,
  then the area's entrance).

**Where progression may go.** Our keys are needed before the Final Save Point, so a location
the player skipped must be reachable again before then. The disc switch closes Expel; the
teleporter reopens it from chapter 38, and the **teleporter proof** (`tools/ap_late_proof.py
--state teleporter`: the late save at chapter 38 step 0, flag 730 cleared, the proof seed's
expel_teleporter on, root Energy Nede's world map; result in late_proof.json's `teleporter`,
the rows' `tele`) says which Expel locations it reaches. **Result** (2026-10-23): Claude's
game 583 maps, 265 locations; Rena's 470 maps, 187 (the difference is the Cave of Trials'
floors only); in both games all 129 Expel locations the late proof proves, so every Expel
location that may hold progression in story mode may in open world too. So in open world:
- an Expel location may hold any progression when the teleporter proof reached it (in the
  protagonist's game, both under "any");
- where only the late proof reached it, only our own Expel-story keys (stages 1-12): the
  player can't pass the disc switch without them, so they are picked up before Expel closes;
- with the teleporter off, or with `expel_progression` off, Expel holds only our Expel-story
  keys (besides non-progression; the user confirmed this default on 2026-10-23);
- an Energy Nede row whose map is entered through an Expel entrance (Lacour's King's Room, map
  429, a chapter-60 scene) has no early mode: it counts from the Final Save Point;
- Energy Nede, Fienal and the cave: the late proof, as in story mode; story bosses as before.

The teleporter proof's state has the save's chapter-60 flags, which Energy Nede's maps test,
so it is used for Expel's maps only. **Known limits** (accepted, conservative elsewhere): a map
that opens later in the story than its area's first visit, or closes for a while in the middle
of it, is not modelled (Lacour's fifth entrance in chapters 25-59 and Mars's second at chapter
15 have open siblings; the arena's route is shut in chapter 57, where only the Fun City Pass,
a starting key, is needed). Open questions (docs/handoff/apareas.md, "Leads"): the proofs fire
the world map's entrance triggers without walking there, which Virtual Expel's Synard (flight
anywhere, Prima p. 130) justifies after the Final Save Point but nothing yet shows for the
teleporter's chapter-38 trip (El and Hoffmann Ruins are reached only by ships of chapters
23-28); and Central City at chapters 36-37 (the party is brought in by a scene; leaving before
the mayor's scene would need the Central City Pass, first required at stage 17).

## Shop slots (`shuffle_shops`, phase 4)

Every entry of the shops a script opens (field.json `shops` with a `shop_sites` record) is a
location: 550 slots in 50 shops. Left out: shop 0x58 (it sells back the books sold to it; its
list grows with flags 0x194 + item), the Familiar's order shops 0x5a-0x5e (a specialty), and
the six shops no script opens. **specials** are the 98 slots whose item no other of these shops
sells (a shop's own stock: Central City's Ignite Sword, Armlock's Force Sword, North City's
instruments...); **full** is all 550. A slot's site gives its area and missable reasons
(Lacour's shops close with the war: window; old Clik's: time-limited), which follow
`missable_locations`. The slot's vanilla item joins the pool.

**Never progression.** A slot can cost up to 40,000 Fol, and with `encounter_rate: 0` Fol is
finite, so a price could block the seed; shop slots hold only useful, filler and trap items
(of any world); the user confirmed this default on 2026-10-23. The seed's check is
`{type: shop, shop, index, item}`.

## Entrance shuffle (later: a design note)

The port already sends a seed door to `to` (an arrival point of the game) and
`doors.validate_doors` checks a table. To build it on this world:
- shuffle among the 66 world-map entrances first (two-way pairs: an entrance and the exit of the
  area back to its world map, so the area's own exit must lead back where the new entrance
  sits); field doors later, if ever (scenes assume their rooms);
- the logic then needs the area graph instead of key groups: a region per entrance group,
  entered through whatever entrance now leads there, and each story stage needing the regions
  its maps lie in (data.STAGES' groups), so a stage's areas must be reachable through the
  shuffled entrances with the keys in hand;
- keep the arrivals the story itself uses (scene transfers, the ships, the disc switch) fixed,
  and keep Fun City's entrance (the teleporter) and Fienal's (map 687) unshuffled, or prove
  them again;
- rerun the late and teleporter proofs with the shuffled table's routes (the proof follows real
  transfers, so `to` must be in the proof seed).

## Options

| Option | Values | Effect |
|---|---|---|
| `goal` | final_boss | Indalecio (539 / 540) |
| `world_mode` | story (default) / open_world | open_world: area keys lock the world-map entrances ("Open world") |
| `expel_teleporter` | true (default) / false | open world only: Fun City's arena takes the party back to Expel from chapter 36 |
| `shuffle_shops` | off (default) / specials / full | shop slots as locations, never progression |
| `protagonist` | any (default) / claude / rena | claude / rena: the other game's locations are left out and the port starts that game; any: one-game locations are missable |
| `missable_locations` | exclude (default) / include / remove | exclude: EXCLUDED (filler only); include: soft reasons become ordinary; remove: left out with their items |
| `expel_progression` | true (default) / false | false: no other world's progression in Expel; in open world our own Expel-story keys still may be there (the user's decision, 2026-10-23) |
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

### DeathLink and the fights you can't win

With `death_link` on, the port sends a DeathLink only from the field's game over
(`field_begin_game_over`), and never for a death a received DeathLink caused. A received one
waits for a safe point ([seed_format.md](seed_format.md), "DeathLink: the safe points"): in a
fight it fells the party, unless the script lets the party lose that fight (script ext 0xb7's
third argument 0, kept in field_flags +0x7E), where it waits for free play. The user's rules
for the story's fights that can't be won (2026-10-23):

- Shin at the Lacour Front Line (516) and Berle in Eluria Tower (520) are timed endurance
  battles that must not be lost, and stay DeathLinked both ways (Berle confirmed by the user
  the same day). A wipe is a real game over and sends a DeathLink. A received DeathLink makes
  the party fall there.
- Shin on the ship (517) and Dias at the tournament (204) must not DeathLink in either
  direction. A loss there sends nothing. A DeathLink received during the fight must not make
  the party fall; it waits until free play.
- Marsilio's first Fienal fight (530) is the same kind of scripted loss, treated like 517 and
  204.

Their boss checks (port side; the classes and the logic above are unchanged): 204, 517 and 530
are checked on the scripted defeat as on a win (the tournament's Dias can be beaten, but the
script then starts the fight again until the party loses); 516, and Berle in Eluria Tower
(520, the same kind of timed fight), on the time-up (battle outcome 4), which is how the story
wins them; a wipe there is a game over and checks nothing. Escapes never check a boss, and the
goal needs a victory. Checked in the scenes themselves: `docs/handoff/deathlink.md`.

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
| `patches.py`, `doors.py` | placeholder patches; the phase-4 door validator (world agent), used by the tests |
| `archipelago.json`, `docs/` | manifest, setup and game pages |
| `test/` | WorldTestBase tests |

Regenerate the tables after `tools/ap_extract.py` or a new late proof:

    tools/ap_world_tables.py          # field.json + battles.json + late_proof.json + doors.json -> data.py

Rerun the late proof after a change to the port's field code, the event sweep or field.json
(about an hour per game; keep the machine's total at 6 game processes: `--jobs 3` each, or one
game after the other; logs in scratchpad/late_proof/, `--resume` reuses finished nodes):

    tools/ap_late_proof.py --bin BIN --game claude --jobs 3 -o scratchpad/late_proof/claude.json &
    tools/ap_late_proof.py --bin BIN --game rena --jobs 3 -o scratchpad/late_proof/rena.json &
    wait; tools/ap_late_proof.py --reparse --game claude --game rena   # -> docs/ap/late_proof.json

and the teleporter proof the same way with `--state teleporter` (work dir
scratchpad/late_proof/teleporter; about 15 minutes per game at --jobs 3; its reparse writes
late_proof.json's `teleporter` key).

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

Results (2026-10-23, phase 4):
- The world's tests: 95 passed (7986 subtests; 22 classes: phase 3's 17, plus TestOpenWorld,
  TestOpenWorldRenaShops, TestOpenWorldNoTeleporter (keys and starting keys, a stage needing its
  key, where progression may go in Expel, the seed's 66 doors through `doors.validate_doors`),
  TestShopsFull and TestShopsSpecials (slot counts, never progression, the shop checks)).
- AP's general tests with so2 installed: 379 passed (116983 subtests).
- 64 seeds (`scratchpad/enemy/ap5/batch/run_batch.sh`): two SO2 players with every option
  random, now with world_mode (story 1 : open_world 2), expel_teleporter and shuffle_shops, plus
  ChecksFinder: 128 `.apso2` files over 127 option combinations, 277-1110 locations each, all
  generate and fill. `check.py` finds no error: phase 3's checks, plus the open-world
  progression rule (teleporter proof / Expel-story keys), no progression in shop slots, the
  shop checks and ids, the 66 doors with their group's key and `validate_doors`, all 35 keys
  in the items table, the starting keys; 3663 progression items placed in SO2 locations.
  (Three seeds of the first run failed to fill: open world with expel_progression off left the
  Expel-story keys nowhere to go; fixed, see "Open world".)
- In the port (headless, engine/build-enemy, `scratchpad/enemy/ap5/spot/run.sh`), three open-world
  batch seeds (protagonist any / claude / rena) and one more: North City's entrance (door
  68685998, key 29) refuses the leader ("North City Pass needed"), `ap_give key:29` gives the
  key and the town loads (vblank 422); Hearn's shop slots 0 and 2 in a full-shops seed pay the
  placed items (our own one given, Rena's sent), the specials seeds leave shop 1 alone.
- `tools/ap_e2e_so2.sh`: 25 / 25, twice (Claude in open world with full shops: the Salva Pass
  arrives from Bob's tile, a second session buys in Hearn's shop and the slot's check reaches
  the server).

Results (2026-10-23, 0.1.1; docs/handoff/apareas.md):
- The world's tests: 98 passed (7986 subtests; new: test_ship_landings in the three open-world
  classes). AP's general tests: 379 passed (116984 subtests).
- 96 seeds (the phase-4 batch's players, seeds 1-96): 192 `.apso2`, 189 option combinations,
  all generate and fill, `check.py` finds no error, `validate_doors` passes on all 192; the 16
  seeds whose players are both in story mode are byte-identical to 0.1.0's. 0.1.0 put an
  unreachable landing key (the Hilton Pass in Rosalia's Room, the Map 231 Pass in Hoffmann
  Ruins) in 44 of these 114 open-world slots; 0.1.1 in none.
- `tools/ap_e2e_so2.sh`: 25 / 25, twice.

## Changes

**0.1.1** (2026-10-23, not released yet). Open world only; story mode, the item pool, location
names and ids are unchanged.
- *Fixed: two ship landings missing from the logic* (docs/handoff/apareas.md). The Lacour
  continent is reached only by the Herlie ship, which lands in Port Hilton through a door the
  Hilton Pass opens, and Hoffmann Ruins only by the Hilton ship, which lands on map 231 (the Map
  231 Pass). 0.1.0 let the Hilton Pass be placed in Rosalia's Room (Lacour Castle) and the Map
  231 Pass in Hoffmann Ruins' chests, which the player can't reach without them: such a seed
  can't be finished (44 of the 114 open-world slots of a 96-seed batch, in 38 seeds, had one;
  the same batch with 0.1.1 has none). Now:
  - the Herlie stage (chapters 16-17) needs the Hilton and Lacour Passes: chapter 18 is stored
    entering Lacour from the world map (stage 7, the tournament, needs Lacour's);
  - an area reached by ship needs the landing's key too (data.KEY_GROUPS `landing`): Lacour's
    locations the Hilton Pass, Hoffmann Ruins' the Map 231 Pass.
- Area labels: field.json's `map_areas` agrees with docs/modding/reference/map_names.csv for 158
  more maps (tools/ap_extract.py `AREA_FIXES`; field.md "Place names"). The logic never read
  them; location names keep their 0.1.0 labels.

## Next

- Entrance shuffle on the door table ("Entrance shuffle (later)" above).
- Tighter open-world proofs: a mid-Nede state with the flags of its time (the teleporter
  proof's chapter-60 flags keep it to Expel's maps).
- A `.apworld` zip for custom_worlds (phase 5).
