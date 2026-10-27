# The SO2 APWorld (`apworld/so2`)

Phases 2 to 4 of [the Archipelago plan](../archipelago_plan.md): the Archipelago world for Star
Ocean: The Second Story. The game name is "Star Ocean: The Second Story"; it has a colon, so
quote it in YAMLs. The world targets Archipelago 0.6.8's World API and writes one `.apso2`
seed per player ([seed_format.md](seed_format.md)), which the port loads.

## Locations

Story mode's locations come from the discs through `docs/ap/field.json` (`tools/ap_extract.py`,
[field.md](field.md)); `tools/ap_world_tables.py` turns them into `data.py`, 1110 rows (the
first six kinds below). The crafts and the killer-move levels (0.2.0) come from `crafts.py` and
`moves.py`, tables written from the disc research (Files, below):

| Kind | Rows | Option | Id ([field.md](field.md), "Location ids") |
|---|---|---|---|
| Chests | 308 (75 in the Cave of Trials) | always | type 1, one per opened flag |
| Gift sites | 101 (3 in the cave) | always | type 2, one per reward site that gives an item or Fol |
| Boss fights | 41 (13 in the cave) | `boss_checks` (default on) | type 9 + encounter |
| Join sites | 18, + 3 under open world's party rules | `character_items` | type 4, one per site |
| First drops | 89 (3 met only in the cave) | `enemy_dropsanity: first_drop` | type 8 + enemy type |
| Shop slots | 550 (98 specials) | `shuffle_shops: specials / full` | type 10 + (shop << 8) + entry |
| Crafts | 553 | `craftsanity` | type 11 + item: one per item Item Creation can make |
| Killer-move levels | 133 (69 moves) | `killer_move_checks` | type 12 + (party id << 14) + (move << 10) + level |
| Hub-only boss | 1: the Cross Cave guardians (set battle 523) | `world_mode: hub` with `boss_checks` | type 9 + encounter (`data.HUB_LOCATIONS`) |
| Guild checks | 45 (12 sets) | `skillsanity: skills_only / insanity` | type 13 + (set << 8) + the skill's place in the set |
| Learn points | 139 learn-table levels, 9 other learn points (3 in the cave), the Float prize (cave) | `skillsanity: special_only / insanity` (Float: `skills_only / insanity`) | type 14 + (party id << 8) + record index (Float: 46) |

Hub mode (`world_mode: hub`, "Hub mode" below) uses only what the hub reaches of these rows.

- **Left out:**
  - two chests and 33 gift sites that give key items (script flags 66-91), because the
    story needs them where they are: Cross Cave's chest (map 547, key flag 68) and Cave of
    Trials level 1's (map 706, key flag 93);
  - the Cave of Trials' three learn chests (0.2.0; `LEARN_CHESTS`): maps 740 / 798 / 799,
    flags 2675 / 2657 / 2721, which teach Leon Extinction (level 4), Ashton tri-Ace and Celine
    Meteor Swarm (level 10) and open only with that member in the party (Changes, 0.2.0);
  - prize tables, Private Action ids (their rewards are gift sites), recruitments other than
    the join sites, and the shop *site* records (type 5; the slots are type 10, "Shop slots"
    below);
  - the goal fights 539 / 540, and boss encounters 0 / 263 / 534 / 561, which no script
    starts with a constant encounter (561, the Iseria Queen, is summoned by a song: an event of
    the Iseria Queen goal, "Goals" below).
- **The Cave of Trials** (`cave_of_trials`, off by default) adds its 78 chests and gift sites
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
  Rena's: map 126, step 1000; checked headless) and comes back at 124 / 125. Under the story's
  party rules Noel joins in the story and is not shuffled. Open world's party rules ("Party v2",
  below) add three sites (tools/ap_world_tables.py `OPEN_JOIN_SITES`, `open_only` in data.py):
  Rena's first join in Claude's game (map 9, 0x6b94), Claude's in Rena's game (map 12, 0x6e08)
  and Noel's (map 277, 0x6fe0), each listed only when that member is not a story member, and
  leave out the story members' own sites. Several sites depend on a choice (the world agent, 2026-10-23):
  Celine's real recruitment is the Cross Cave choice (map 524, chapter 13); map 124 runs only
  if she was sent away there and not received, 125 needs flag 112, and in Rena's game sending
  her away leaves no Celine site; Leon (234) and Opera (479) have parting choices. So a join
  site can stay unreachable: every join site is EXCLUDED and never holds progression (hard
  reason "join"), and the port rejoins a received member after the partings.
- **First drops:** the 89 enemy types that random encounters meet and that drop something.
  Each one's region is the earliest region where the type is met; its pool item is the type's
  first drop. Whether one may hold progression: "Enemy drops" below.
- **Crafts** (0.2.0): one per distinct item Item Creation can make, named after the first
  specialty that makes it ("Cooking: Fried Rice"); location groups "Crafts" and one per
  specialty. See "Craftsanity" below.
- **Killer-move levels** (0.2.0): one per proficiency level of each killer move a learn table
  teaches ("Bowman: Pillory level 6"); groups "Killer Moves" and "Killer Moves: <member>". See
  "Killer moves" below.

## Items

| Item | AP id | Classification |
|---|---|---|
| a game item n (1-1023) | `0x50320000 + n` | equipment (75-162, 485-754) useful, the rest filler |
| a Fol bundle (10 amounts) | `0x50330000 + amount` | filler |
| a character: Celine, Bowman, Dias, Precis, Ashton, Leon, Opera, Ernest, Chisato; under open world's party rules any member but the story route's hero (Claude, Rena and Noel too) | `0x50340000 + party id` (1 Claude ... 12 Chisato) | useful; progression when `killer_move_progression: any` needs it ("Killer moves") |
| a trap: Ambush, Fol Loss (10 %), Poison, Paralysis, Stone | `0x50350000 + 1..5` | trap |
| an area key (open world): "Salva Pass" ... (35) | `0x50360000 + N`, N 1-35 (data.KEY_GROUPS) | progression |
| a dungeon key (hub mode): "Salva Drift Key" ... (19 registered, at most 18 in a seed) | `0x50370000 + N`, N = docs/ap/hub_bosses.json `dungeons[].key` 1-19 (data.HUB_DUNGEONS) | progression |
| a skill (skillsanity): "Skill: Perseverance" ... (46) | `0x50380000 + skill id` | useful; progression when the logic needs it ("Skillsanity") |
| a killer move or spell (skillsanity): "Claude: Shooting Stars" ... (148: every member's but the first) | `0x50390000 + (party id << 8) + record index` | useful; progression when the logic needs it |

- **The pool** is the vanilla contents of this world's locations. A boss, join or drop
  location adds its vanilla item (a Blueberry for bosses and join sites); a craft adds the
  crafted item when that is filler, else a Blueberry (so equipment does not grow the pool's
  useful share; `items.pool_filler`); a killer-move level adds a Blueberry; the party's
  characters replace join sites' Blueberries; `traps` % of the filler becomes traps.
- There are no progression items of our own in story mode: the story's key items are flags
  and stay put (the one exception: character items that `killer_move_progression: any` needs).
  Open world adds the 35 area keys (progression); each takes the place of a filler item in the
  pool.
- **The cave's gems stay put.** The Cave of Trials doors on maps 751 and 764 take the Red Lotus
  Gem (317; gifts on maps 711 and 754) and test the Cracked Gem (289; the chest on map 771).
  Those three locations keep their own item, locked (`items.PUZZLE_ITEMS`), so a door never
  needs a gem placed behind it.
- The filler name is Blueberry.

**The party** (character_items under the story's party rules, `world.choose_party`): one of
Bowman / Precis (Precis rules Bowman out); Ashton, or Opera with Ernest (Ashton rules Opera
out, Ernest comes only with Opera); Celine; Chisato; Leon in Claude's game or Dias in Rena's
(neither with story_route "any"). With Claude, Rena and Noel the party is at most 8: when there
is one too many, Ernest or one of the single members stays out. The seed's
`options.characters` lists them. Open world with character_items has its own rules ("Party
v2", below).

## Regions and events

Menu → Expel → Energy Nede → Fienal → Cave of Trials. Three event items order the spheres the
way the story does:
- "Arrived on Energy Nede" at the disc switch;
- "Saved at the Final Save Point" (Fienal 9F, map 671): it opens the Cave of Trials and
  Indalecio;
- "Victory" at Indalecio, the goal. The seed's `goal.encounters` is [539, 540] with `goal:
  indalecio`.

The limiter-off goals add "Limiter Off" (Energy Nede region, location "Limiter Off (Filia,
Central City)"), which Indalecio then needs, and the Iseria Queen goal "Iseria Queen defeated"
(Cave of Trials region, "Iseria Queen (Cave of Trials)"); see "Goals" below.

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
| story_route | only one game runs it (under story_route "any") | Claude / Rena only: gifts 9 / 2, bosses 1 / 1, joins 2 / 4 |

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
  the same state with flag 1 cleared and the story_route bytes swapped. The party is Claude,
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
  was proven in the story_route's game (in both games under "any"). Story bosses are exempt
  (the story fights them); join sites, shop slots, gift sites and crafts never may (the user,
  2026-10-25: an NPC gift check should never hold progression, this world's own keys or another
  world's alike, whatever the proof reaches); first drops follow `drop_progression` ("Enemy
  drops" below: their places are judged by the same proofs) and killer-move levels
  `killer_move_progression` ("Killer moves"); with `expel_progression` off, nothing in Expel may.
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
  chapter 60; most were excluded already (window, Private Action, member). The 9 the proof
  reaches never hold progression either way (`may_hold_progression` excludes every gift site
  outright, the user, 2026-10-25): the table's gift row and this proof still matter for open
  world's reachability ("Open world" below), not for what a gift may hold.
- So a seed puts progression in at most 258 chests, the 21 story bosses and 10 cave
  bosses (fewer with the options): every multiworld in the batch below filled.
- Rerun the proof after a change to the port's field code, the event sweep or field.json
  ("Files", below).

**Characters are useful, not progression.** Member-gated logic would not be monotonic
(Ashton rules out Opera), so every member-gated location is excluded instead. The one
exception (0.2.0) is `killer_move_progression: any`, where a killer-move level of a member who
joins by their item needs the item, which is then progression; the pool never holds two
members who rule each other out, so a received member always joins ("Killer moves").

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
to Expel, and the only way to Virtual Expel), plus the North City Pass unless the teleporter
and `expel_progression` are both on. The first Energy Nede stage needs it and nothing on Energy
Nede is reachable before it (the teleporter's old man needs the Synard, stage 16), so it must sit
in Expel; with either option off Expel holds only our Expel-story keys (below), and it would
have no legal spot in a solo world (0.2.0 gave it at the start with the teleporter off only; with
the teleporter on and expel_progression off, every solo fill failed: 0.2.1, "Changes").

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
- *teleporter* (Expel, `expel_teleporter` on): the "Expel Teleporter" event (stage 16 done and the
  Fun City Pass: the old man in the arena, once the Synard flies; "The El voyage", below) plus
  the keys of the world-map entrances on the teleporter proof's route to its map;
- *late*: the Final Save Point plus the keys on the late proof's route (Fun City for Expel,
  then the area's entrance).

**Where progression may go.** Our keys are needed before the Final Save Point, so a location
the player skipped must be reachable again before then. The disc switch closes Expel; the
teleporter reopens it from chapter 38, and the **teleporter proof** (`tools/ap_late_proof.py
--state teleporter`: the late save at chapter 38 step 0, flag 730 cleared, the proof seed's
expel_teleporter on, root Energy Nede's world map; result in late_proof.json's `teleporter`,
the rows' `tele`) says which Expel locations it reaches. The sweep fires a world map's entrance
triggers wherever they lie, but the teleporter's party lands in Arlia with no flight on Expel
(Expel's Synard spot, game_state +0x26c, is set only by Virtual Arlia's arrival, map 18 0x5a4,
chapter 60 with flag 730; the Synard flag 11 only on Energy Nede): it walks the Cross
continent and sails the Herlie ferry (map 144's sailor, chapter 16 on, for a fare) to the
Lacour continent. The El continent (El Beach, Eluria Colony, Eluria Tower), Hoffmann Ruins'
island (with map 231) and the Cave of Trials' island are reached only by the story's ships
(Port Hilton: chapters 23-27 to map 231, chapter 28 to El) or by Virtual Expel's Synard, so
the teleporter search does not follow their entrances (`TELEPORTER_ISLANDS`;
`tools/ap_walkmap.py` reads the world map's walkable terrain, finds those islands and
`--check`s the list). **Result** (2026-10-23, 0.1.1): both games 418 maps, 153 locations; 101
of the 129 Expel locations the late proof proves (not Hoffmann Ruins' 26 chests, which hold
only our Expel-story keys in open world, or Eluria Colony's 2 gifts, which hold none: gift
sites never hold progression, not even our own keys, "Logic" above). So in open world (chests
and bosses; gift sites are always "none", above):
- an Expel location may hold any progression when the teleporter proof reached it (in the
  story_route's game, both under "any"): the Cross and Lacour continents;
- where only the late proof reached it, only our own Expel-story keys (stages 1-12): the
  player can't pass the disc switch without them, so they are picked up before Expel closes;
- with the teleporter off, or with `expel_progression` off, Expel holds only our Expel-story
  keys (besides non-progression; the user confirmed this default on 2026-10-23);
- an Energy Nede row whose map is entered through an Expel entrance (Lacour's King's Room, map
  429, a chapter-60 scene) has no early mode: it counts from the Final Save Point;
- Energy Nede, Fienal and the cave: the late proof, as in story mode; story bosses as before.

**The El voyage and the other one-way points** (0.1.1; docs/handoff/elgate.md). The story
sails to El from the Lacour Front Line without free play on the way: at chapter 27 step 7000
(stored in meeting room 448) the meeting room 444 holds a walk-in trigger (routine 0x7c28)
whose scene stores chapter 28 and sends the party to Lacour Castle (422), whose scene sends it
to Port Hilton (169), whose scene sails (548, chapter 29). El is left only through the disc
switch; the Cross and Lacour continents reopen through the teleporter, Hoffmann Ruins' island
(whose ship sails only at chapters 23-27) after the Final Save Point. So (data.STORY_GATES,
from tools/ap_world_tables.py):
- the stage that ends with the voyage (stage 10) also needs the El Beach, Eluria and Eluria
  Tower Passes (regions.py `stage_rules`), so the fill puts them where the player gets them
  before sailing, in this world or another;
- the seed's door table holds the trigger's door (29106503, the transfer to 422 in routine
  0x7c28) with those `keys`; the port keeps the trigger shut until every one has arrived and the
  banner names each missing one ("Eluria Pass needed to sail", seed_format.md "Phase 4"; without the purpose when the text would not fit the banner);
- an Expel location the story reaches before the voyage (its early mode is at stage 10 or
  before) holds progression only where the **voyage proof** reached it
  (`tools/ap_late_proof.py --state voyage`: the late save at chapter 27 step 7000, flag 730
  cleared, disc 1, Claude and Rena, root map 444; an entry that moves the story past chapter
  27 is not followed and its checks don't count, nor are Expel's entrances to El and the Cave of
  Trials; Hoffmann Ruins' island is, its ship still sails). **Result** (2026-10-23): both games
  289 maps, 129 locations, among them every Expel location the logic lets hold progression
  before the voyage (Hoffmann Ruins' 26 chests included), so no location's class changed.
- **The teleporter needs the Synard.** Energy Nede's world map (archive 0x1060 + i, read as
  tools/ap_walkmap.py reads Expel's) puts Central City and North City on one island and Fun City
  (279, entered only from that map) on another; before the Synard (flag 11, set at map 417
  where chapter 41 is stored) the party moves only by North City's transporter (the director's office, 272) (267 to Noel's
  House, chapters 38-40). So the "Expel Teleporter" event needs stage 16 (data
  TELEPORTER_STAGE), not 13, and with `expel_teleporter` and `expel_progression` on, when Energy
  Nede's keys and other worlds' progression may sit on the Cross and Lacour continents, the
  voyage also needs the keys of the stages until then (North City, Noel's House, Red Crystal
  Passes: the gate's `teleporter_groups`), held before sailing.
- **Exits kept shut for a while** (data.EXIT_GATES, no logic). The story brings the party into
  Central City (the scenes 619 to 248, chapters 36-37) and Noel's House (the transporter,
  chapter 38) without their entrance, and on those islands nothing else may hold their key.
  Until chapter 38 (Central City, exit 235 to the world map, routine 0x670c) and 40 (Noel's
  House, exit 277, routine 0x7164) the seed door needs the area's key (`until`; banner "Central
  City Pass needed to leave").
- **The other one-way points, checked.** The disc switch closes Expel: everything Expel holds
  is either an Expel-story key, which the switch's stage needs, or (teleporter on) on the
  continents the teleporter reopens, from the Synard, which the voyage gate covers. With
  `expel_progression` off Expel holds only our Expel-story keys. Fienal: its floors count from
  stage 23 (the Final Save Point's), and no key is needed inside it. The Front Line itself shuts
  its exit (438) at chapter 26 from step 1000 and at chapter 27 step 3000, while the story stays
  inside and no key is needed. Hoffmann's island closes with its ship at chapter 28, after the
  gate. No key item is given around the Front Line (Shin 516 is a story boss at 447; Leon joins
  at El Beach, 230), and every join site is EXCLUDED anyway.
- **The Lacour continent's soft-lock and Port Hilton's harbour** (port, 2026-10-25;
  seed_format.md "The harbour"; docs/handoff/ships.md). Entering Lacour at chapter 22 step 9999
  stores chapter 23, and Port Hilton's sailor, the continent's only way off, refused the passage
  to Cross at chapters 23 and 24; the stage-9 keys (Map 231 and Hoffmann Passes) the fill may
  put on the Cross continent were then out of reach until chapter 25, which needs them: a
  soft-lock in released 0.1.0 / 0.1.1 open-world seeds that the logic did not model. The port
  now lifts that refusal (and the chapter-18 Tournament Pass one) in every open-world seed,
  decided from `world_mode`, so the logic's assumption (the Cross continent stays reachable) is
  true without a logic change and existing seeds get it with a port update. The sailor also
  becomes the captain the user asked for: **Cross / Hoffmann Ruins / El / Not now**, each only
  when open (Hoffmann Ruins from chapter 23 to 59, the Hoffmann soldier's own sail; El only at
  the story's voyage point, chapter 27 step 7000, with every key of the El gate, doing what the
  Front Line's trigger does, the keys checked before anything is stored; never after the disc
  switch). The party can walk from the Front Line to Port Hilton at that point (exit 438 is
  open at step 7000, and ap_walkmap.py puts both on one walkable piece of the continent at
  chapter 27). The Hoffmann ship's soldier stays at Port Hilton until chapter 59, and voyages
  check their landing's key at the NPC, before the fare. The logic does not yet use the ship's
  longer window (Hoffmann's island stays in `TELEPORTER_ISLANDS`): a proposal for the apworld is
  in docs/handoff/ships.md.

The teleporter proof's state has the save's chapter-60 flags, which Energy Nede's maps test,
so it is used for Expel's maps only. **Known limits** (accepted, conservative elsewhere): a map
that opens later in the story than its area's first visit, or closes for a while in the middle
of it, is not modelled (Lacour's fifth entrance in chapters 25-59 and Mars's second at chapter
15 have open siblings; the arena's route is shut in chapter 57, where only the Fun City Pass,
a starting key, is needed). The late proof fires the world maps' entrance triggers without
walking there too, which Virtual Expel's Synard (flight anywhere, Prima p. 130) justifies after
the Final Save Point. Central City at chapters 36-37 (docs/handoff/apareas.md, "Leads"):
settled by its exit gate (above).

## Hub mode (`world_mode: hub`; 0.2.0)

The user's decisions D1-D10 (docs/handoff/hub.md), the seed's blocks (seed_format.md "Hub"), the
port's side (agents hubvm, hubcore, hubchambers). Code: `apworld/so2/hub.py`; tables: data.py's
hub section (`tools/ap_world_tables.py` from docs/ap/hub_state.json, hub_bosses.json,
hub_proof.json and field.json). Story mode and open world are unchanged by it.

**What it is.** New Game starts after the Final Save Point (chapter 60, step 4000, flag 730,
disc 2) with every location undone and the chapter held at 60 (the port's chapter lock): both
worlds and every town are open from the start, and nothing closes (no disc switch, no El voyage,
no exit gate, no teleporter). Each dungeon has one key (D4). Each story boss waits in its chamber
and plays its original scene through a story view (D2); beating it is its location, open once its
dungeon's key has arrived (D3: every boss independent; the town chambers, Berle in Armlock 306,
Shigeo in Fun City 281 and Marsilio in the arena 305, need none). Fienal has no key: its entrance
opens on the Fienal requirement (D6), and the goal lies behind it. Characters are always items
(D7). Since the chapter never moves and nothing is one-way, every key stays reachable from any
play state (the accessibility rule holds by construction).

**The locations: what the hub reaches** (`hub.choose`, `hub.reached_games`): the hub proof's
(docs/ap/hub_proof.json, `tools/ap_late_proof.py --state hub`, agent hubproof: the late proof's
search run from the hub's New Game (the port builds the state, SS_HUB_STATE), the start map Fun
City 279 as its root, the chapter lock on, every chamber row of hub_bosses.json in its seed, the
hero alone; Eluria Tower's two walks the sweep can't fire (557 -> 558 -> 559) as extra roots;
Fienal's floor-6 maze 673 followed room by room). data.HUB_REACHED (the games per location) and
HUB_MAPS (the maps per game) carry it:
- chests, gifts, the Cave of Trials' bosses and the side-quest bosses the hub proof proved in
  the game(s) of the story_route: 290 chests in both games (73 in the cave; the late proof's 258
  plus Eluria Tower's 23 and the 9 of Fienal's maze rooms 647 / 673, which the late proof's
  sweep never walked to), 9 gifts (2 in the cave), 10 cave bosses, XINE (the only side-quest
  boss it reached; it plays at the hub state in both games, so it may hold progression:
  `hub.CONFIRMED_OPTIONAL = {199}`, and the Lasguss Mountain key stays);
- the 22 chambers (`boss_checks`; hub_bosses.json, proven headless by hubchambers): the 21 story
  bosses of story mode but the losable fights (D5: Dias 204, Shin 517, Marsilio 530, and Zand 265,
  which is no location), plus the Cross Cave guardians (set battle 523), a hub-only row; the
  other game's chamber is left out (Varmillion is Claude's, Azamgil Rena's; under story_route
  "any" both stay, filler only);
- shop slots of the shops open at the hub state (data.HUB_SHOPS: a site on a map the hub proof
  reached whose conditions can hold at chapter 60, step 4000 with the hub's flags, outside a
  Private Action: 41 shops in Claude's game, 42 in Rena's; old Clik's and Lacour's war-time
  shops are closed);
- first drops of the enemy types random encounters meet at the hub state (data.HUB_DROPS, per
  game the dungeons where they are met, on the hub proof's maps: 85 of the 89 types);
- crafts and killer-move levels as in open world.
- Never: join sites (no join scene runs at chapter 60), the losable fights, anything the hub
  proof didn't reach (old Clik; the tournament; the story scenes' gifts; the arena's prize gift).
- Missable reasons: a window or a time limit doesn't apply (nothing closes); the hard reasons
  (a choice, a member, a side quest) stay EXCLUDED, or left out with `missable_locations:
  remove`; "story_route" when the hub reaches a location in only one of the route's games.

**The dungeon keys** (`hub.needed_keys`; data.HUB_DUNGEONS, from hub_bosses.json `dungeons`). A key
exists only where something of this world lies behind it (the coordinator's ruling): one of its
locations (a drop aside), a chamber of the Fienal requirement, or the Cave of Trials for the
Iseria Queen goal. So a dungeon with nothing behind it in this seed has no key and no door rows:
it is open. Never a Fienal key (the requirement instead) or a Portal of Sealing key: the Portal
is out of hub mode (hubcore, 2026-10-25: the late state never reaches it, its doors are
story-gated at chapter 52 and Armlock's 306 is Berle's chamber, for one chest: `hub.OUT_OF_HUB`);
Lasguss Mountain's exactly when XINE is a location. With the defaults (cave off) 17 keys; 18 with
the cave: Salva Drift, Cross Cave, the Mars dungeon, Zand's Mansion, the Sanctuary of Linga,
Hoffmann Ruins, the Lacour Front Line, Eluria Tower, the Mountain Palace, Lasguss Mountain, the
Cave of Trials (Expel); the Synard Plant, the Cave of Red Crystal, the four Fields, Mihne Cavern
(Energy Nede). No starting key: sphere 1 is the towns (their chests, the town chambers, shops,
gifts, crafts). The keys and the characters take the place of as many filler items in the pool
(there are no join sites to hold the characters).

**Regions and rules.** Menu -> Expel and Energy Nede (the towns, open) -> one region per dungeon
with a key, entered with it; Energy Nede -> Fienal on the event "Fienal opened" (location "Fienal
Gate (Fienal requirement)": the keys of the requirement's chambers; by D3 "can beat boss X" is
"has X's dungeon key"). Indalecio (Victory) is in Fienal and needs Limiter Off / the Iseria Queen
with those goals; Limiter Off (Filia's scene, Central City City Hall) has no requirement (the hub
is at chapter 60 with flag 730); the Iseria Queen is in the Cave of Trials (its key). A drop is
reached through any of the dungeons it is met in (a town or a world map: nothing). Killer-move
levels learned by level 30 count from the start, the others from "Fienal opened".

**`fienal_requirement`** (D6): `fields_and_berle` (default: Magichand 524, the Guardians of Power
525 and Courage 527, Lover 528, Berle in Armlock 535), `fields` (the four), `all_bosses` (every
chamber outside Fienal; Varmillion / Azamgil only when story_route names the game: under "any"
the seed can't name the player's). The seed's Fienal entrance (door 68685691, routine 0x3de0)
carries them as `won` with one banner each ("Defeat Magichand first", ...); the goal gate (door
43978493) keeps the goal's own conditions.

**Where progression may go** (`hub.may_hold_progression`, `hub.drop_rule`): a chamber; a chest
or cave boss the hub proof reached in every game the player may play; XINE (confirmed); never a
gift, a shop slot, a craft or an EXCLUDED location; with `expel_progression` off nothing in
Expel. Drops as in story mode (`drop_progression`, the Luck floor, encounter_rate 0), through the
maps of places where the type is met with no condition that the hub proof reached in every game
the player may play, and only through those; `area_keys_in_drops` off keeps every Star Ocean key (area or
dungeon key, any player's) out of the drops. Killer moves per `killer_move_progression`.

**The party (D7).** `party_rules` is "open" and characters are items whatever `character_items`
says. With story_route claude / rena: open world's rules (the other hero and Noel can be items,
no lockouts, the healer guarantee, `starting_character`, `exclude_characters`). With "any": no
join scene runs at chapter 60, so both heroes are items (but a starting Claude or Rena); the one
picked at New Game is in the party already, so their item does nothing. Under
`killer_move_progression: hero` that makes Claude's item progression (his moves need him).

**The options in hub mode.** `hub_start` (`fun_city` default, `arlia`, `random_town`: any row of
hub_state.json `starts`, every town of both worlds; Archipelago reserves `random` for a Choice,
where it picks among the option's own values); `fienal_requirement` as above. `story_route`
picks the hero's chamber and whether both heroes are items; `expel_teleporter` and the El voyage
gate don't apply (no such doors in a hub seed); `missable_locations` as above; `character_items`
is forced on; `cave_of_trials` puts the cave's locations behind its key; `boss_checks` off keeps
the chambers out of the locations (they still play, and the Fienal requirement still needs
them); the goals work as in the other modes (Filia's scene and the Clik flags preset by
`goal.flags`).

**The seed** (version 3; seed_format.md "Hub"): `hub` = data.HUB_STATE (chapter, step, disc,
flags, globals, games, lock) with `start` = the chosen row of hub_state.json `starts` as it is
(with its own `globals`, the start's Synard words, which the port writes after `hub.globals`);
`bosses` = every chamber row (the port's keys as hub_bosses.json has them, `view` left out when
null, plus location, dungeon, `keys` = [this seed's key] or [], trigger, late), whatever
`boss_checks` says; `doors` = every door hub_bosses.json lists for each dungeon with a key
(`{id, key, routine, name}`; no `routine` when that routine also leads elsewhere, as Zand's
Mansion's 9314034, which also goes to Herlie: the port then refuses the transfer alone),
Fienal's entrance (`won`, `banners`), the goal gate, and the P5 doors (`hub.P5_DOORS`: the arena
antechamber's door 19929509 leads to Marsilio's arena 305 while he is unbeaten, `to` with
`while_unbeaten` 532; Fienal's 679 -> 674 door 44502760 lands on 674's east side while Cyril 538
is unbeaten, since his scene never ends from the west (hubchambers); the port's P5,
docs/ap/seed_format.md "Hub"). Two `bosses` rows carry `ask` (`hub.ASK`; below). `options`
gains `hub_start`, `hub_start_town` (the start picked), `fienal_requirement`, `fienal_won`,
`dungeon_keys` (hub seeds only; story and open-world seeds are unchanged).

**The chambers that ask first** (the user's rulings, 2026-10-25; `hub.ASK`, the port's `ask`:
seed_format.md "Hub", "The port: the questions"; agent hubask). Shigeo's street (Fun City 281) lies
on the only Energy Nede -> Expel route: each door into it asks "Shigeo is on the rampage ahead. /
Face Shigeo now?"; no lets the party pass (281 at the hub's own state). Marsilio's arena 305: the
antechamber's door asks (no: the lobby 296), the arena desk's first menu gets "Face Marsilio."
beside "Register." (the normal arena: its ranking battles for training at any time), and in his
view the Clerk's menu gets "Leave the arena." No answer records anything: the boss stays unbeaten
and the question comes again on the next visit (`again: "visit"`, the user's ruling: after a no
the row is quiet until the party has been on a world map; agent hubask2). Logic: nothing changes (both are town chambers, keyless, open from
the start; a no only defers the location). The arena's gift (1349960704, map 305, the survival
battle's prize) needs no Marsilio win any more: the normal arena is open while he is unbeaten, so
its rule is the town's (none), as before.

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

**Buying (2026-10-25, the port alone).** The first purchase of a slot gives what was bought (every
copy) and the slot's check, as Severed Chains does; the port marks a slot still to check with the
Archipelago logo in the shop's list. Up to port 0.02.00 the first copy *was* the check (the user's
report: Fol gone, the items not the ones bought) and the colour-2 mark it meant to show never
drew. Seeds are unchanged: existing 0.1.x / 0.2.0 seeds get the new behaviour from the port. No
logic change (no progression in shops).

## Enemy drops (`drop_progression`, `area_keys_in_drops`; 0.2.0)

With `enemy_dropsanity: first_drop`, an enemy type's first drop may now hold progression (0.1.x:
never). `drop_progression`: **allow** (default: it may, but needn't), **force** (the fair-odds
drops take progression first), **never** (0.1.x's rule). In every mode (rules.py `drop_rule`):

- **The odds.** `roll_drops` (battle/result.cpp, 0x800869fc) rolls a downed enemy's two drop
  slots in turn, each at odds + (average Luck of the standing members >> 4) - 7 out of 256, and
  stops at the first success. Luck only adds, so the chance at Luck 0 is the floor
  (`drop_chance`). A drop holds progression only from 1 in 64 kills at Luck 0
  (`MIN_DROP_CHANCE`, 4 / 256). That keeps out 21 of the 89 types: the 11 that drop nothing
  without Luck (Magius 27 and Ericodus 56 need an average Luck of 128, Shout 96, Starguarder
  80, the seven odds-5 types such as the slimes 48), Rikiha and Burst (1 / 256), the seven
  odds-10 types (3 / 256) and the Guarder (3.99 / 256). Never with `encounter_rate: 0`.
- **Met again.** Archipelago counts a location reached for good, so the enemy must be there to
  fight from wherever the logic reaches the drop. `tools/ap_world_tables.py` (`drop_places`)
  gives each drop row its `places`: the maps where random encounters meet the type with no
  condition on the encounter site or the calls reaching it (`unconditional`), merged by area
  and proofs (region, entrance group, the games whose late / teleporter / voyage proof reached
  the map, their route keys), and each world map it is met on. Each row also gets its two slots'
  `odds`.
  - **Story mode:** a place the late proof reached (or a world map it walked), as for chests,
    and outside Expel with `expel_progression` off. 62 of the 89 drops may hold progression
    (25 with `expel_progression` off).
  - **Open world:** each place counts as a chest on its maps would (`open_world_progression`,
    `open_world_access` through `place_row`: early in its area's stage with the keys, through
    the teleporter, after the Final Save Point; nothing that closes before the El voyage); a
    world map only after the Final Save Point (its encounter cells are not mapped to islands).
    The drop holds the best class among its places and is reached only through the access
    modes of the places that allow progression, so the logic never counts it through a place
    that can close. With the teleporter and `expel_progression` on: 60 drops any progression,
    2 only our Expel-story keys; teleporter off: 52 and 10; `expel_progression` off: 25 and 30.
- **force:** the drops that may hold progression and drop at least 20 / 256 at Luck 0
  (`FORCE_DROP_CHANCE`, about 8 %, one in 13 kills: a handful of fights; 25 types, of which 22
  pass the rules above, 13 in story mode with `expel_progression` off, 18 in open world) are
  filled first: `SO2World.fill_hook` runs `fill_restrictive(allow_partial=True)` on them with the
  multiworld's progression (not the deprioritized), before the main fill; what can't go there
  is filled as usual. They are not Archipelago's priority locations: AP 0.6.8's priority fill
  raises a FillError when a priority location can't take any of the progression left, as a late
  drop can't in a solo open world (5 of 5 test seeds failed that way).
- **`area_keys_in_drops`** (default on; the user, 2026-10-25): off adds an item rule to this
  world's drops, in either world mode: no area key of any Star Ocean world (this world's or
  another SO2 player's), so no area waits on one fight's drop. On, keys follow the drop's class
  like any progression.

## Craftsanity (`craftsanity`, `no_fail_crafting`; 0.2.0)

- **The locations** (`crafts.py`, written by scratchpad/apworld2/make_crafts.py from the craft
  research: scratchpad/openworld/craft_results.csv and the specialties' tables decoded off the
  disc at the addresses `engine/src/game/field/menu_creation.cpp` uses): one per distinct item
  Item Creation can make, 553, by the specialty that names them: Cooking 104, Customize 100,
  Metalwork 92, Compounding 49, Blacksmith 44, Art 39, Machinery 29, Authoring 25, Publishing
  24, Master Chef 21, Alchemy 16, Reverse Side 10. Every result is a game item; none was found
  unmakeable. A location per result, not per recipe: a recipe's results are not a stable set
  (Customize chains have no end). Failure items (12) are not locations; Identify, Survival and
  Reproduction hand back or copy items and have none. Publishing (super specialty 5,
  `Super5::compute`) makes a book about the party member picked: the member's index in the
  table 0x80083D68 (12 party ids) picks a book from 0x80083D30 (common) or, in 10 % of tries,
  0x80083D4C (rare); make_crafts.py reads the three tables from overlay 0xbc2 (archive entry
  3010, loaded at 0x8007e000): 24 books, items 196-219. The research's tables plus these: all
  553 items are their own `common_kind` (the item pack's blocks 3-5, entry 3000), so every
  check can fire.
- **Who can make it** (`makers`; the disc decode: `party_selected_char()` is the 1-based party
  id): Customize's lists are one member's each (Claude to Ashton), Machinery's rare tables
  Precis's or Opera's, Authoring's rare books Leon's, Art's portraits A-L one member's each (the
  portrayed member must be in the active party), Publishing's books their subject's (who must
  be in the party), Cooking's Grain teas anyone's but Claude's, Rena's, Precis's and Leon's (they
  cook from another list): 156 of the 553. Metalwork's per-member equip filter is not
  modelled.
- **The rule:** never progression (an item rule; the user, 2026-10-25: a craft check gives a
  random Archipelago item, any world's non-progression), and no access rule (Item Creation works
  anywhere once a specialty is learned). A member-locked craft is EXCLUDED unless a maker is
  sure to be in the party (the story's party rules: Claude, Rena, Noel and the starting
  character; open world's: the heroes and the starting character), and left out when no maker
  can ever join (open world's rules: a member who is not a hero, the start or in the pool).
- **The check:** `{type: craft, item}`: the port checks the `common_kind` of what
  `CreationPage::produce()` gives (a rare kind counts as its common item), and the player keeps
  the crafted item (the user, 2026-10-25).
- **`no_fail_crafting`** is written to the seed for the port (the attempt succeeds whenever the
  specialty picked a result); no logic change.

## Killer moves (`killer_move_checks`, `killer_move_progression`; 0.2.0)

- **The locations** (`moves.py`, written by scratchpad/apworld2/make_moves.py from
  scratchpad/killer_prof/move_levels.csv: the learn tables in archive #1981 and the move data in
  #2129; docs/handoff/killer_prof.md): one per proficiency level of each killer move a learn
  table teaches (the user, 2026-10-25: "every proficiency level"), 133 over 69 moves (Claude,
  Bowman and Dias 10 moves, Ashton 9, Ernest and Chisato 8, Precis and Opera 7). A move's level
  is min(uses / div, cap); the use count (record + 0x8a + 2 * move) goes up by 1 each time the
  move starts (the original's `pc_begin_action`, battle/party_states.cpp), at most 999. The 61
  moves with at most 3 levels have a location per level (112); the 7 that count in fine steps
  (Bowman's Poison Pills, Pillory, Secret Medicine and Whirlwind Fist, Opera's Photon Prison,
  Cold Wind and Gravity Shell) have three, at the levels 100, 200 and 300 uses reach (21), the
  counts the tiered moves' levels mostly need. Ashton's Dead Triangle has no levels; the 8 moves
  without a learn-table row (Phase Gun, Precis's three, Opera's three, Ashton's Tri-Ace) are
  not listed.
- **The member** (`locations.fighter_status`): sure to be in the party (a hero, the story's
  fixed members, the starting character) or joining by their character item: an ordinary
  location; joining only by the player's choice in the story (character_items off): EXCLUDED;
  never in the party: left out. With `encounter_rate: 0` every level is EXCLUDED (the uses need
  battles).
- **Access** (rules.py `killer_rule`): the member, at the level that learns the move, and the
  battles. Conservatively, a move learned by level 30 counts from the disc switch (a party there
  is well past it; 53 locations), any other from the Final Save Point (80).
- **`killer_move_progression`:** never (default): no progression. hero: the story members'
  moves only (Claude's, in Claude's game and under story_route any; Rena has no killer moves).
  any: every member the logic counts on (sure or by item). Under any, a level of a member who
  joins by their item also needs the item, which becomes progression
  (`world.progression_characters`: Archipelago's CollectionState counts only progression
  items); under never and hero the item is not required (nothing there holds progression, and
  full accessibility reaches every item anyway).
- **The check:** `{type: killer_move, character, move, level, uses}`, `uses` = level x the
  move's divisor in the armed table; the port compares the raw use count.

## Skillsanity (`skillsanity`, `skillsanity_shop_share`; 0.2.0)

The user's design and decisions (docs/handoff/skillsanity.md); the seed and the port:
seed_format.md "Skillsanity"; the tables: `skills.py` (tools/ap_make_skills.py, from the disc),
the code: `skillsanity.py`. `skillsanity`: **none** (default: the original), **skills_only**,
**special_only**, **insanity** (both).

- **Skills** (skills_only / insanity). Each of the 12 guild sets' purchase (flag 700 + set, at any
  of its guilds: docs/notes/menu.md "Skill guilds") is one location per skill in it, "Skill Guild:
  Sensibility 2 (1 of 4)" (45); each skill is an item, "Skill: Perseverance", that lets the party
  learn it with SP. With `cave_of_trials`, the Level 11 prize (Float) is a location and Float an
  item. Members keep the skills they join with. A guild check's access: any of its set's guilds
  (open world: their towns' keys, data.GUILD_PLACES; hub mode: the towns, open). Never
  progression (the user).
- **Special** (special_only / insanity). Every member keeps their learn table's first move (Claude's
  Air Slash, Rena's Heal, ...: known from the start or their join); every other killer move and
  spell is an item ("Claude: Shooting Stars"), and each learn point a location: a learn-table level
  ("Claude Lv 7: Shooting Stars", 139), Precis's and Opera's machines (the items 464-469 and the
  arena's Private Action: "Precis: Holo-holograph (Holoprojector)", 6) and, with `cave_of_trials`,
  the cave's learn chests ("Cave of Trials Level 4: Leon's Extinction", 3). Claude's Phase Gun is
  left out (the user). A member who can never join has none; one who joins only by the player's
  choice holds filler only (EXCLUDED). Access as killer moves': a learn level up to 30 from the disc
  switch, any other from the Final Save Point (hub mode: from the start / Fienal opened); the
  machines after the Final Save Point (hub: any time) with Machinery; the cave's in the cave.
  Learn points hold no progression (they need a member and levels, which no proof places).
- **Logic.** With skills shuffled, Item Creation needs its specialty (a sure member who joins
  with its skills: Rena's Cooking, the heroes' and the starting character's; else the skill items:
  `SPECIALTIES` / `SUPER_SPECIALTIES`), so craftsanity's locations need them; the Iseria Queen goal
  needs Musical Talent's (the song at Musical 10); the machines Machinery's. With special shuffled a
  killer-move level (killer_move_checks) needs its move. Those items are progression
  (`world.skill_needs`); every other skill and move item is useful.
- **Shops** (with `shuffle_shops`; the user's second extension). `skillsanity_shop_share` (0-100 %,
  default 10): that share of this world's skill and move items that the logic doesn't need is
  placed in this world's shop slots (DEFAULT ones) before the fill (`place_in_shops`; shop slots
  stay progression-free, the user); such a slot (and any the fill gives one of them) carries a
  `price` and the port sells the item as itself: a move its learn level x 100 Fol, an
  item-taught move 5000, a cave prize 8000, Float 900, a skill its set's price / the set's size
  (Sensibility 2 at Herlie: 1600 / 4 = 400).
- **Hub mode:** the hub state no longer sets flags 700-711 (tools/ap_hub_state.py): the guilds sell
  in every hub seed, skillsanity or not.
- **The join level** (the port, open world and hub, whatever skillsanity says: seed_format.md "The
  join level"): a member joins at the party's median level; under special, every learn point up to
  it is checked at the join. The logic is unchanged by it (learn points count by the level the party
  reaches).

## Party v2 (open world with `character_items`; 0.2.0)

The new character rules are open world only (the user, 2026-10-25); story mode keeps the
story's party rules ("The party" above). `world.plan_party` returns the `Party`: the rules
(`party_rules` in the seed), the story members, the starting member, the pool, who is sure to
be in the party and who can ever be.

- **The story members:** the story route's hero, or Claude and Rena under story_route any (the
  player picks at New Game, so neither can be an item). The hero is never an item (slot 0 at New
  Game, the Private Actions; party_ships.md) and can't be excluded (a warning).
- **`starting_character`** (open world and hub, either party rules; default `hero`): hero means
  nobody extra (the hero leads, as in the original); a name, that member (it wins over
  `exclude_characters`, with a warning; the hero of a one-hero route means nobody extra; the
  port shows them on New Game's character select to confirm). **any** (agent "charselect",
  2026-10-25; until then a random member): the player picks at New Game on the port's character
  select (seed_format.md "The character select"), so `Party.start` is None (the logic counts on
  nobody extra: the pick is an ordinary pool member, "item" to `fighter_status`) and
  `Party.choices` is whom the select offers: the story members and the pool (at most 8), or
  under the story's party rules every member not excluded; the seed's `starting_character` is
  that list. The pick's own item, when it comes, finds them in the party. A member the
  generator picks: Archipelago's `random` (one of the option's values). Story mode ignores it (a
  warning). The member joins at the start and leads (the port: the field sprite and the name
  swap, seed_format.md "Party v2"). `hero` is the default because a start would make every
  open-world seed a version-2 seed and change the game for players who did not ask.
- **The pool** (`choose_open_party`): every other member not excluded and not the start, as many
  as fit 8 with the story members and the start (a random sample when more are allowed; under
  `any` the start's place goes to the pool too). No
  lockouts: Leon and Dias in either game, Bowman with Precis, Ashton with Opera and Ernest.
- **The healer guarantee:** when none of the story members, the start and the pool is Rena or
  Noel (the only healers), an allowed healer takes a random pool member's place (the pool is
  full then); with both excluded, none (the player's choice).
- **Join sites:** every non-story member's, the three new ones included; all EXCLUDED. Members
  not in the pool (and neither story members nor the start) never join.
- Open world without character_items keeps the story's party rules (`party_rules: "story"`),
  plus the starting member.

## Goals (`goal`; 0.2.0)

The user (2026-10-25): Indalecio either version, Indalecio with his limiter off, or both the
limiter-off Indalecio and the Iseria Queen (research: scratchpad/openworld/goals.md; the seed:
seed_format.md "Goals").

| `goal` | Wins | The seed | Logic |
|---|---|---|---|
| `indalecio` (default; `final_boss` an alias) | 539 or 540 | `goal: {encounters: [539, 540]}`, as in 0.1.x; no gate | Victory: the Final Save Point |
| `indalecio_limiter_off` | 540 | `{encounters: [540], flags: [445, 446]}` and the goal gate (a version-2 seed) | Victory also needs "Limiter Off": the Final Save Point, and in open world the Central City Pass |
| `indalecio_limiter_off_and_iseria_queen` | 561, then 540 | `{all: [561, 540], encounters: [540], flags: [445, 446]}` and the gate with `won: [561]` (version 2) | Victory also needs "Iseria Queen defeated": the Final Save Point, and in open world the Cave of Trials and Fun City Passes |

- **Limiter off:** map 685 picks encounter 540 when flag 556 is set, set only by Filia's scene in
  Central City City Hall (map 239) after the Final Save Point, which needs her Clik meeting
  (flags 445 / 446, a Private Action closed by Clik's destruction). The port presets 445 and
  446 at New Game and after every load (`goal.flags`; this costs only Clik's Private Action),
  and keeps the Final Save Point floor's way to Indalecio (door 43978493, map 671 -> 684,
  routine 0x3bec) shut until flag 556 (banner "Talk to Filia in Central City first"), in
  either world mode.
- **The Iseria Queen** (561) has no battle site: the song "The Evil Melody" (song 7) summons her,
  composed from the Silver Trumpet (item 48, which G-Celesta gives at the bottom of the Cave of
  Trials, map 815) and played with a Conductor's Baton by a member at Musical 10 with Pitch and
  Sense of Rhythm; it works with encounter_rate 0. The gate also needs her win (`won`, banner
  "Defeat the Iseria Queen first"), and the port keeps the trumpet out of the shops' sell lists
  until the song is composed (flag 53). The Cave of Trials region exists whether or not
  `cave_of_trials` adds its locations, which this goal does not turn on.

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
| `goal` | indalecio (default; alias final_boss) / indalecio_limiter_off / indalecio_limiter_off_and_iseria_queen | Indalecio, either fight (539 / 540); the limiter-off fight (540); the Iseria Queen (561) and then 540 ("Goals") |
| `world_mode` | story (default) / open_world / hub | open_world: area keys lock the world-map entrances ("Open world"); hub: the late-game hub with a key per dungeon and the boss chambers ("Hub mode") |
| `hub_start` | fun_city (default) / arlia / random_town | hub mode: the start point; random_town: any town of either world |
| `fienal_requirement` | fields_and_berle (default) / fields / all_bosses | hub mode: the bosses that open Fienal (D6) |
| `expel_teleporter` | true (default) / false | open world only: Fun City's arena takes the party back to Expel from chapter 36 |
| `shuffle_shops` | off (default) / specials / full | shop slots as locations, never progression |
| `story_route` | any (default) / claude / rena | claude / rena: the other game's locations are left out and the port starts that game; any: one-game locations are missable |
| `starting_character` | hero (default) / any / claude ... chisato | open world and hub mode: a member who joins at the start and leads; hero: nobody extra; any: the player picks at New Game among the members this world allows (the port's character select; "Party v2") |
| `exclude_characters` | a set of member names (default empty) | open world with character_items, hub mode: never in the pool, so they never join; not the hero |
| `missable_locations` | exclude (default) / include / remove | exclude: EXCLUDED (filler only); include: soft reasons become ordinary; remove: left out with their items |
| `expel_progression` | true (default) / false | false: no other world's progression in Expel; in open world our own Expel-story keys still may be there (the user's decision, 2026-10-23) |
| `cave_of_trials` | false (default) / true | add the Cave of Trials (after the Final Save Point) |
| `boss_checks` | true (default) / false | the 41 boss fights are locations |
| `character_items` | false (default) / true | the party's members join through items, their join sites are locations; open world: party v2; hub mode: always on, no join sites |
| `enemy_dropsanity` | off (default) / first_drop | each enemy type's first drop is a location |
| `drop_progression` | allow (default) / force / never | whether first drops may hold progression; force: the fair-odds ones first ("Enemy drops") |
| `area_keys_in_drops` | true (default) / false | false: no Star Ocean area key in this world's drops |
| `craftsanity` | false (default) / true | the 553 crafts are locations, never progression ("Craftsanity") |
| `no_fail_crafting` | false (default) / true | Item Creation never fails by chance (the port) |
| `killer_move_checks` | false (default) / true | the 133 killer-move levels are locations ("Killer moves") |
| `killer_move_progression` | never (default) / hero / any | which killer-move levels may hold progression |
| `skillsanity` | none (default) / skills_only / special_only / insanity | skills, and / or killer moves and spells, as items; the guild sets' purchases and the learn points as locations ("Skillsanity") |
| `skillsanity_shop_share` | 0-100 (default 10) | with shuffle_shops: the share of the non-progression skill / move items sold in this world's shops as themselves |
| `traps` | 0-100 (default 0) | the share of filler turned into traps |
| `exp_multiplier`, `fol_multiplier` | 1-10 (default 1) | the battle's EXP / Fol total |
| `encounter_rate` | 0-200 (default 100) | random-encounter frequency, percent |
| `difficulty` | earth (default) / galaxy / universe / any | New Game's Battle Rank: the port sets it and skips the screen; any: the screen offers all three. No logic change (it scales only the monsters' stats: "Difficulty" in seed_format.md) |
| `death_link` | false / true | written to the seed as `options.deathlink` |

slot_data and the seed's `options` repeat them (`starting_character` as the member chosen, null,
or under `any` the list of members New Game's select offers; `exclude_characters` as a list), with `seed_format`, `goal_encounters`, `characters` (the
pool's members), `party_rules` ("story" / "open") and `apworld_version`. The port reads the
phase-3 ones ([seed_format.md](seed_format.md), "Phase 3") and, of 0.2.0's, `no_fail_crafting`,
`starting_character`, `party_rules` and `difficulty` ("v2 fields").

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
- `version`: 1, or 2 when the seed uses a field an older port would ignore (a limiter-off goal,
  `party_rules: "open"`, a starting character, craft or killer-move locations,
  `no_fail_crafting`, a `difficulty` other than earth: `seed_version`; seed_format.md "The
  version rule"), so the 0.02.00 port
  refuses such a seed instead of playing it wrong; 3 for a hub seed (and its `hub` and `bosses`
  blocks, "Hub mode" above);
- every own location with its check (chest: map + flag; gift: map + call-site offset; boss:
  encounter; join: map + offset + character; drop: enemy type; craft: item; killer move:
  character + move + level + uses) and its item (own: kind and its fields; another player's:
  name / ap_id / player_name);
- each chest's and gift's placeholder and patch lines, from the world agent's
  `apworld/so2/patches.py` (`placement_patches`; every chest and gift location gets one, since
  the port checks gifts only through theirs);
- `items`, `start` (start_inventory: items and Fol), `options`, `goal` (`seed_goal`) and `doors`
  (open world's, then the goal gate, `goal_doors`).

The world never reads the discs. Each site's `word` / `expected` come from field.json.

## Files

| File | Contents |
|---|---|
| `world.py` | `SO2World` (`plan_party`, `choose_party`, `choose_open_party`, `create_items`, `create_hub_items`, `fill_hook`), `SO2Web`, `seed_data`, `seed_goal`, `goal_doors`, `seed_version`, `generate_output`, `fill_slot_data` |
| `hub.py` | hub mode: `choose` (the locations), `needed_keys`, `fienal_won`, `create_regions`, `set_rules`, `drop_rule`, the seed's `hub` / `bosses` / `doors` (`seed_hub`, `seed_bosses`, `seed_doors`); `OUT_OF_HUB` (the Portal of Sealing), `CONFIRMED_OPTIONAL` (waiting on hubchambers) |
| `items.py`, `locations.py`, `regions.py`, `rules.py`, `options.py` | as named |
| `data.py` | generated tables (do not edit) |
| `crafts.py`, `moves.py` | craftsanity's 553 results with their specialties and makers; the 69 killer moves with their learn levels and divisors (generated by tools/ap_make_crafts.py and tools/ap_make_moves.py from docs/ap/data/; do not edit) |
| `skills.py`, `skillsanity.py` | skillsanity's tables (the guild sets, the skills, the starting skills, the specialties, the learn tables with each member's first move, the other learn points; generated by tools/ap_make_skills.py from the disc; do not edit) and its locations, items, logic and shop prices |
| `patches.py`, `doors.py` | placeholder patches; the phase-4 door validator (world agent), used by the tests |
| `archipelago.json`, `docs/` | manifest (`world_version`), setup and game pages |
| `test/` | WorldTestBase tests (`test_world.py`, `test_doors.py`; 0.2.0's in `test_v2.py`, hub mode's in `test_hub.py`) |

Regenerate the tables after `tools/ap_extract.py` or a new late proof:

    tools/ap_world_tables.py          # field.json + battles.json + late_proof.json + doors.json
                                      # (+ hub_state.json + hub_bosses.json: the hub section) -> data.py
    tools/ap_make_crafts.py           # docs/ap/data/craft_results.csv + craft_decode.txt + the overlay -> crafts.py
    tools/ap_make_moves.py            # docs/ap/data/move_levels.csv -> moves.py
    tools/ap_make_skills.py DISC1.cue # disc 1 + engine/src/game/field/characters.cpp -> skills.py

Rerun the late proof after a change to the port's field code, the event sweep or field.json
(about an hour per game; keep the machine's total at 6 game processes: `--jobs 3` each, or one
game after the other; logs in scratchpad/late_proof/, `--resume` reuses finished nodes):

    tools/ap_late_proof.py --bin BIN --game claude --jobs 3 -o scratchpad/late_proof/claude.json &
    tools/ap_late_proof.py --bin BIN --game rena --jobs 3 -o scratchpad/late_proof/rena.json &
    wait; tools/ap_late_proof.py --reparse --game claude --game rena   # -> docs/ap/late_proof.json

and the teleporter proof the same way with `--state teleporter` (work dir
scratchpad/late_proof/teleporter; about 15 minutes per game at --jobs 3; its reparse writes
late_proof.json's `teleporter` key), and the voyage proof with `--state voyage` (work dir
scratchpad/late_proof/voyage, disc 1, root map 444; about 15 minutes per game at --jobs 2; key
`voyage`, which `tools/ap_world_tables.py` copies into data.STORY_GATES `proven`). Every game run is under the memory cap (tools/memcap.py,
SS_MEM_CAP_GB, 4 GB): a run that dies at it fails the proof (exit status 3), never retried.
`tools/ap_walkmap.py --check` confirms the teleporter's islands (`TELEPORTER_ISLANDS`) from
the world map's terrain.

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

Scoped to this world, as Archipelago's testing guide (docs/tests.md, "Running Tests for Specific
Worlds") runs it: `AP_TEST_WORLDS` names world folders, and implies `-m world` (the general tests
that iterate over every world, scoped to so2, plus worlds/so2/test and test/multiworld's
two-player fill for so2). Name the test directories, or add `--continue-on-collection-errors`
(the venv has no flask for test/webhost):

    AP_TEST_WORLDS=so2 SKIP_REQUIREMENTS_UPDATE=1 ../ap-venv/bin/python -m pytest -q -n 8 test/general worlds/so2

**The tests follow the guide** (2026-10-26, docs/handoff/aptests.md; the guide re-read live, the
same as the checkout's copy at 54803be): an empty `test/__init__.py`; the base in `test/bases.py`
(`SO2TestBase(WorldTestBase)`, `game = GAME`, plus the shared helpers and
`assertSweptAccessDependency`: the guide's dependency check through event chains); files
`test_*.py`, classes `Test*` (one option mix each: WorldTestBase's three default tests run for
every one) or `unittest.TestCase` where no multiworld is needed (TestDoorValidator, TestHubAsk);
checks shared by several mixes in `...Checks` mixins, never collected alone; no
runner-specific features (`python -m unittest` runs them too); each test under a second (the
slowest about 0.8 s at `-n 4` on a loaded machine; `pytest --durations=30`). The logic is
tested with `assertAccessDependency` (every location checked unless noted):

| File | Mixes (classes) | What the access dependencies cover |
|---|---|---|
| test_world.py | story mode: default, story_route claude, missables remove / include, the cave, expel_progression off, late proof (any, rena), bosses off, drops and traps, shops full / specials | (story mode has no items of its own: the events, the late proof's rule, `can_fill`) |
| test_open_world.py | open world default, Rena with full shops and the cave, no teleporter, expel_progression off (the North City Pass fill failure's reproducer, below) | every pool area key -> exactly the locations and story stages whose every way needs it; each El voyage pass -> the voyage stage, all of them -> the Final Save Point and the goal (through the events); the Hilton and Lacour passes -> the Herlie stage; a landing's key -> its area's early-only chests |
| test_party.py | the story's party rules (any / claude / rena), party v2 (open members, any start, healers excluded, healer guarantee, a named start), the starting character without items / in story / the hero | `PartyChecks.party_size()` / `healer_guaranteed()`: the hooks a bench option overrides |
| test_hub.py | hub default, Claude at Arlia (fields, cave, killer moves), Rena all bosses (Iseria Queen, forced drops, specials), any lean, fields without checks; a story / open-world seed without hub fields | every dungeon key -> its dungeon's locations, the drops met only there, the Fienal gate; each key of the Fienal requirement -> the gate, all of them -> the goal (fields_and_berle, fields, all_bosses) |
| test_v2.py | drops (allow, force, never, no encounters, open world, no keys in drops), craftsanity (story, open party), no-fail only, the four goals, killer moves (story, hero, any with items, no encounters) | character items -> their killer-move levels (killer_move_progression any) |
| test_skillsanity.py | none, skills_only with crafts, special_only in open world, insanity in hub with shops, insanity with the Iseria Queen | every skill / move item the logic needs -> the crafts, Machinery learn points, the Iseria Queen and killer levels whose every way needs it; each Musical Talent skill -> the Queen, both -> the goal |
| test_difficulty.py | earth, galaxy, universe (open world, hub), any | (no logic change: the same locations as on Earth) |
| test_doors.py | `unittest.TestCase` | the door validator on a made-up table |

A fixed world bug stays in the suite as its reproducer: `TestOpenWorldNoExpelProgression` (open
world, expel_progression off, the teleporter on, a solo world) fills in WorldTestBase's
test_fill, test_voyage_gate_placements and test_seed_doors. Before 0.2.1 the North City Pass had
no legal spot there and every solo fill raised FillError (docs/handoff/aptests.md "World bug");
the three were `@unittest.expectedFailure` until the fix (0.2.1: the pass at the start,
test_keys_and_start; docs/handoff/ncpfix.md).

**The release workflow** (many random seeds, outside the unit tests, as the guide asks):
`tools/ap_batch.sh SET OUT FIRST LAST` generates seeds FIRST..LAST of a players set
(`story_open`: two SO2 players, story 1 : open world 2, every option random, and ChecksFinder;
`hub`: one SO2 player in hub mode, one hub / story / open world 3 : 1 : 1, and ChecksFinder;
`solo_noexpel` (0.2.1): one SO2 player alone, `expel_progression` off, open world / story / hub
3 : 1 : 1, the rest random, so no other game's locations take what this world can't place, as
ChecksFinder's hid 0.2.0's North City Pass failure; tools/ap_batch_players/) with Generate.py
into OUT/<seed>/, then `tools/ap_batch_check.py` checks every .apso2 (OUT.check.txt); exit 0
only when every seed generated and no file has an error. A problem a batch finds gets a minimal
reproducer in the tests (one option mix), as the phase-4 batch's expel_progression fill
failures did (`TestOpenWorldNoExpelProgression`):

    tools/ap_batch.sh story_open scratchpad/batch/full 1 96     # ~5 min
    tools/ap_batch.sh hub scratchpad/batch/hub 1 96
    tools/ap_batch.sh solo_noexpel scratchpad/batch/solo 1 96

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
  batch seeds (story_route any / claude / rena) and one more: North City's entrance (door
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

Results (2026-10-23, 0.1.1 with the El voyage gate; docs/handoff/elgate.md):
- The world's tests: 108 passed (7986 subtests; new: test_voyage_gate and
  test_teleporter_needs_the_synard in the three open-world classes, test_gate_doors for the
  validator). AP's general tests: 379 passed (116979 subtests).
- The logic diff against 0.1.1 before the gate (scratchpad/apareas/logicdiff.py, classdiff.py):
  no location's access modes or progression class changed, in story mode or open world; the
  changes are the Front Line stage's rule (the three El passes, plus North City, Noel's House and
  Red Crystal with the teleporter and expel_progression on) and the teleporter event (stage 16).
  data.py only gains TELEPORTER_STAGE, STORY_GATES and EXIT_GATES.
- 96 seeds (the same players, seeds 1-96): 192 `.apso2`, 189 option combinations, all generate
  and fill; `check.py` (now also: the gate and exit doors, the gate's keys only where the voyage
  proof reached them, no progression where it did not) finds no error, `validate_doors` passes on
  all 192; the 16 all-story seeds are byte-identical to the batch before the gate. Stranded keys
  the voyage's window needs (scratchpad/elgate/batch/elcount2.py): 0 (0.1.0's batch: an El pass
  off El, which the player could sail away from, in 110 of its 114 open-world slots).
- `tools/ap_e2e_so2.sh`: 27 / 27. The port's engine suites: ss_tests 99/0, ss_game_tests 129/0,
  AP 26/0 (ap_gate_door new, in ss_game_tests), the field smoke script 86 PASS, none failing.
- In the port (headless, scratchpad/elgate/demo/run.sh): at chapter 27 step 7000 the meeting
  room's trigger refuses without the passes (the banner names each, the story stays at chapter
  27, the leader keeps the controls), with five of six names only the last, and with all six
  the scenes store chapter 28 and sail (map 548, chapter 29); Central City's exit at chapter 37
  refuses without the Central City Pass and lets the party out at chapter 38 or with the pass.
- The late and teleporter proofs, rerun with the gate build (scratchpad/elgate/proofs/,
  compare.py against docs/ap/late_proof.json): the teleporter key is identical in both games
  (proven, checks and maps). The late key's maps and edges are identical too, but 2 of the 278
  checks the committed file proves are absent from this rerun's proven set in both games: the
  Cave of Trials Level 1 (map 705) and Level 11 (map 807) boss checks (ids 1364328951,
  1364328961; the cave's "10 / 13" bosses in the Results table above). The cause is the proof
  driver's timing, not the gate (A/B, scratchpad/proofab/): replaying the two nodes with the
  gate and the pre-gate binary, each on both routes the proofs took into the cave (via 702 or
  709), gave no boss on map 705 in either swap and the boss on map 807 in both. Both maps settle
  to the same state and entry list from either route. The driver kills a run after 6 reloads
  (wall clock, polled every 0.5 s) and goes on after the last entry started, so under load a
  boss entry that starts just before the kill is skipped before its fight ends; which
  predecessor wins a node's route is also first-come with `--jobs` > 1.
  docs/ap/late_proof.json is unchanged (two earlier runs prove both bosses).

Results (2026-10-25, 0.2.0; docs/handoff/apworld2.md):
- The world's tests: 235 passed (19741 subtests, a count that varies with the random fills;
  `test_v2.py`'s classes: drops in each mode, force filling first, no keys in drops; craftsanity in story and open world, no-fail alone;
  party v2's members, start, healer guarantee and exclusions; the goals' events, gate and seed;
  killer moves per progression mode and with encounter_rate 0). AP's general tests with so2
  installed: 379 passed (117030 subtests).
- 96 seeds (scratchpad/apworld2/batch: `run_batch.sh`, `check.py`; the 0.1.1 batch's players
  with every 0.2.0 option random too; two SO2 players and ChecksFinder): 192 `.apso2` over 192
  option combinations (183 version 2, 9 version 1), 276-1768 locations each, all generate and
  fill. `check.py` (0.1.1's checks plus: the version rule; craft and killer-move checks and ids;
  no progression in crafts; drops' progression by `drop_rule`, never under 1 / 64 or with
  encounter_rate 0 or `never`; killer moves' by mode; no area key in a drop with
  `area_keys_in_drops` off; the goal object and gate; party v2's size, exclusions, healer and
  join sites; the story's party rules) finds no error; 5954 progression items in SO2 locations.
  Progression placed: in drops 195 in 33 open-world slots and 74 in 18 story slots with allow,
  400 in 19 and 97 in 6 with force, none with never; in killer-move levels 187 in 36 slots with
  any, 68 in 28 with hero, none with never; none in the crafts of 59 slots (`stats.py`). (The
  goal gate's first banner was shortened after this run, a text change only: a seed generated
  since carries it.) The first run found another SO2 player's area key in a story-mode world's
  drop with `area_keys_in_drops` off (7 files; the item rule then applied in open world only):
  fixed, none in the reruns.
- The 16 seeds whose players are both in story mode, generated with `drop_progression: never`
  (the one new option that changes a story seed by default): all 32 `.apso2` identical to the
  0.1.1 batch's but for the `options` object (11 keys added, none changed).

Results (2026-10-25, gift sites never hold progression; docs/handoff/apworld2.md "Session 4"):
- The world's tests: 240 passed (19751 subtests; new: `test_gifts_never_hold_progression` in
  `LateProofChecks` and `OpenWorldChecks`, run under `TestLateProofAny`, `TestLateProofRena`,
  `TestOpenWorld`, `TestOpenWorldRenaShops` and `TestOpenWorldNoTeleporter` — story and open
  world, with and without the teleporter, cave_of_trials, missable_locations, shuffle_shops and
  character_items on; `can_fill` is false for every gift location against both this world's own
  progression, e.g. an area key, and another world's).
- 96 seeds (scratchpad/apworld2/batch: the same players, `full`): 192 `.apso2` over 192 option
  combinations, 276-1768 locations each, all generate and fill (96/96 exit 0). `check.py`
  (`allowed` / `allowed_open` now treat `gift` as never-progression, like `drop` / `join` /
  `shop`) finds 0 errors; 5957 progression items in SO2 locations (3-73 per slot). No fill
  failure in open world with `expel_progression` off, where the 2 Eluria Colony gifts had been
  one of the few places for an Expel-story key: the keys still found room among the chests and
  drops.

Results (2026-10-25, hub mode; docs/handoff/hubworld.md):
- The world's tests: 312 passed (22635 subtests; new: `test_hub.py`, six option mixes: what the
  hub reaches, the chambers and their regions, the keys (something behind each, never Fienal's or
  the Portal of Sealing's, Lasguss with XINE), every boss reachable with its key alone, the Fienal
  requirement (no key of it missing), where progression may go, the party, the version-3 seed's
  `hub` / `bosses` / `doors` through `validate_doors`; and a story / open-world seed without any
  of it). AP's general tests: 379 passed (117127 subtests). data.py reproduces from the tool, and
  the data.py before hub mode is a byte prefix of it.
- 96 seeds of the hub players (scratchpad/apworld2/batch/players_hub: one SO2 player always in hub
  mode, one in hub / story / open world 3 : 1 : 1, every hub and older option random, and
  ChecksFinder): 96/96 exit 0, 192 `.apso2` over 192 option combinations (154 hub slots: 41
  Arlia, 38 Fun City, 75 random_town starts; requirement 76 / 39 / 39; 13-18 dungeon keys),
  192-1671 locations each. `check.py` (new hub checks: the version, the hub block and start, the
  requirement, the keys and their doors, the Fienal gate and banners, the chambers' rows, what the
  hub reaches, where progression went, the party; it catches each of ten deliberately broken
  seeds, scratchpad/hubworld/neg) finds 0 errors. Progression in the hub slots: 3136 in chests, 306
  in chambers and cave bosses, 622 in drops, 299 in killer-move levels; none in a gift, shop or
  craft.
- The 0.2.0 batch's players (story and open world, seeds 1-96) regenerated with hub mode in the
  code: all 192 `.apso2` byte-identical to the batch before it (`compare.py`), and `check.py` 0
  errors.

Results (2026-10-25, `starting_character: any` as New Game's pick; docs/handoff/charselect.md):
- The world's tests: 318 passed; AP's general tests: 379 passed.
- Both batches regenerated (players -> scratchpad/apworld2/batch/full_cs, players_hub -> hub_cs,
  seeds 1-96): 96/96 exit 0 each, `check.py` 0 errors on 192 + 192 files (it now checks the
  list: the heroes and the pool, at most 8, nobody excluded; under the story's party rules
  everyone not excluded). Against the batches before (`full_hubask`, `hub_ask`;
  `cs_identity.py`): every seed with no `any` slot byte-identical (142 / 138 files, story mode
  among them); the 25 / 30 `any` slots differ as meant (no random start; the old start is in the
  new list in every one, and where the party rules are open the pool has the start's place: one
  member more), and with them the other slot of the same multiworld (the shared fill).

Results (2026-10-25, the hub proof; docs/handoff/hubproof.md):
- The hub proof (`tools/ap_late_proof.py --state hub`; docs/ap/hub_proof.json): from the title's
  state New Game builds (the port's own code, SS_HUB_STATE, in each hero's game), the start Fun
  City 279 as the root, the chapter lock on, all 22 chamber rows and the P5 doors in its seed, the
  hero alone in the party; Eluria Tower's two walks (557 -> 558 -> 559, hubcore2) as extra roots
  searched after the start's own; Fienal's floor-6 maze 673 (a 4 x 4 grid of rooms) followed room
  by room. Claude's game 773 nodes, 625 maps, 329 locations; Rena's 774, 626, 328 (Varmillion is
  Claude's chamber). Against the late proof: nothing lost; gained 51 / 50: Eluria Tower's 23
  chests and its two bosses, the 9 chests of Fienal's maze rooms (647, 673: the late proof's
  sweep never walked the maze), and the chambers whose scenes don't run at the late state. The
  chapter lock dropped exactly one store pair in either game, from a free-play path: Cross Cave's
  key chest (547, talk K60: chapter 13 and step 0, guarded by flag 2569 only, the known one). No
  edge it found enters a dungeon other than by a door hub_bosses.json lists.
- `--bosses`: every chamber spec of tools/ap_hub_chambers.py from the title's New Game, along the
  hub proof's route through the dungeon's locked door with its key in the starting inventory
  (no door refused): 43 of 46 specs pass (view, battle won by the stand-in, the check, goal_won,
  the story at 60 / 4000), both games, every chamber row covered; 3 skipped (an arrival-position
  spec, and Cyril's west side, which lies behind his room from the start). The goal walk
  (Fienal's maze -> the Final Save Point 671 -> 684 -> 685, the final battle) passes from the
  hub start in both games.
- The APWorld reads it (data.HUB_REACHED, HUB_MAPS): the world's tests 318 passed (23214
  subtests; the default hub world now has 217 chests, 290 with the cave); AP's general tests 379
  passed (117124 subtests); data.py regenerates identically and its part before the hub section
  is unchanged. The hub players' batch (seeds 1-96, `hub_hp`): 96/96 exit 0, `check.py` (its hub
  reach now from HUB_REACHED / HUB_MAPS) 0 errors on 192 files; in the 154 hub slots 3352
  progression items in chests (350 of them in the 32 chests only the hub proof reaches), 304 in
  chambers and cave bosses, 582 in drops. The story / open-world players' batch (`full_hp`): 96/96
  exit 0, 0 errors, all 192 `.apso2` byte-identical to the batch before (`full_cs`).
- The late proof's driver fix (2026-10-25; docs/handoff/elgate.md "Proof gap explained"): each
  entry has its own reload budget, and a run is resumed at the first entry the sweep skipped
  for reloads ("end loop", event_sweep.cpp kMaxReloads) instead of past it. Rerun from the gate
  build's node logs (only the nodes with reload loops rerun): both games 278 proven, the same set
  as docs/ap/late_proof.json, the same checks per map, maps and edges (the Cave of Trials L1 / L11
  bosses back).

Results (2026-10-25, `difficulty`; docs/handoff/difficulty.md):
- The world's tests: 341 passed (318 + test_difficulty.py's 23: default, galaxy, universe in
  open world, any, universe in hub mode; each checks slot data, the version, the seed and that
  Earth would choose the same locations of the same kinds); AP's general tests: 379 passed.
- Identity (scratchpad/difficulty/batch, `id_compare.py`): seeds 1-16 (story / open-world
  players) and 1-8 (hub players) generated with `difficulty: earth` and with `universe` (scalars,
  so no roll): all 32 + 16 `.apso2` identical but for `options.difficulty` and the version (story
  and open world 1 -> 2 where it was 1), and the spoilers identical but for the Difficulty line.
- Mixed batches (`difficulty` weighted over all four): 32 + 16 seeds, exit 0; `check.py` (now with
  the version rule's difficulty and a `difficulty` kind) 0 errors on 64 + 32 files; with the
  identity batches 192 files, 0 errors.

Results (2026-10-25, skillsanity; docs/handoff/skillsanity.md):
- The world's tests: 380 passed (29330 subtests; `test_skillsanity.py`: none, skills_only with
  craftsanity, special_only in open world with killer moves, insanity in hub mode with shops,
  insanity with the Iseria Queen goal). AP's general tests: 379 passed.
- 96 seeds of each players set with `skillsanity` and the share random (scratchpad/apworld2/batch
  players_sk, players_hub_sk -> scratchpad/skillsanity/batch sk_full, sk_hub): 96/96 exit 0 each,
  `check.py` (new: the guild checks and learn points by the option, their ids and checks, never
  progression, the item kinds, no first move as an item, every shop price) 0 errors on 192 + 192
  files; 9938 and 8673 progression items in SO2 locations, of them own skill / move items 489 / 1366
  and 453 / 1151; priced shop slots 3098 (90 slots) and 3519 (99).
- The identity (skillsanity none: the players and players_hub sets as they were -> sk_id_full,
  sk_id_hub; check.py 0 errors) against full_hp / hub_hp: every file is byte-identical but for
  `options` (the new keys) and, in hub seeds, `hub.flags` without 700-711, except in the
  multiworlds where a slot has `cave_of_trials` (the renamed cave locations change the fill).

Results (2026-10-26, the tests to Archipelago's testing guide; docs/handoff/aptests.md; tests and
tools only, no world change):
- The world's tests: 430 passed, 3 xfailed (30148 subtests; was 380 passed, 29338): every old
  test kept (one class renamed), plus the access dependencies of the table above and the new
  mix `TestOpenWorldNoExpelProgression`. Slowest test 0.81 s (before 0.55 s), at `-n 4` with the
  machine's load at ~20 on 20 cores; `python -m unittest` agrees. AP's general tests: 379 passed,
  before and after. `AP_TEST_WORLDS=so2 ... pytest test/general worlds/so2`: 433 passed before.
- The release workflow (`tools/ap_batch.sh`, both sets, seeds 1-96): 96/96 exit 0 each,
  `tools/ap_batch_check.py` 0 errors on 192 + 192 files (9924 / 8673 progression items); every
  .apso2 and spoiler byte-identical to the same seeds through the old scratchpad/apworld2/batch
  run_batch.sh / check.py, and the checker's output identical too.
- A world bug, found by the new mix and left for the user (a 0.2.x): open world with
  `expel_progression` off and the teleporter on can't generate solo, North City Pass having no
  legal spot (Generate.py FillError on every solo seed; with two SO2 players and no other game
  too). The batches never saw it: each of their multiworlds has a ChecksFinder player.

## Changes

**0.2.0** (2026-10-25, not released yet; docs/handoff/apv2.md, the user's request and decisions;
the seed's new fields: seed_format.md "v2 fields"). A seed using none of the new features is a
version-1 seed, but it still needs port 0.02.50: 0.2.0 writes the renamed options only
(`story_route`, `enemy_dropsanity`) and the 0.02.00 port reads only the old names
(`protagonist`, `drops_as_checks`), so there it loses its route and its first-drop checks
[release agent, 2026-10-25]. A story-mode seed with `drop_progression: never` (above)
is 0.1.1's no longer in every case: with the same seed number, one with a `cave_of_trials`
player places its items otherwise (3 cave locations fewer, the cave's names by floor), and so
does one where 0.1.1 put progression on a gift site (gift sites never hold it now). Of the 16
all-story seeds of the batch, the 3 without a cave player are 0.1.1's but for `options`; the 13
with one differ, 3 of them (5 files) already before the cave change, likely from the gift rule
(docs/handoff/cavefix.md).
- *Goals:* `goal` indalecio (0.1.x's `final_boss`, kept as an alias) / indalecio_limiter_off /
  indalecio_limiter_off_and_iseria_queen, with the events "Limiter Off" and "Iseria Queen
  defeated", the seed's `goal.flags` / `goal.all` and the gate on the way to Indalecio
  ("Goals").
- *Drops may hold progression:* `drop_progression` allow (default) / force / never, only for
  enemies met again from where the logic reaches the drop and with fair odds at Luck 0;
  `area_keys_in_drops` (default on; 0.1.x kept area keys out of drops) ("Enemy drops").
- *Craftsanity:* 553 craft locations (Publishing's books included), never progression;
  `no_fail_crafting` for the port ("Craftsanity").
- *Killer moves:* 133 killer-move level locations; `killer_move_progression` never / hero / any
  ("Killer moves").
- *Party v2* (open world with character_items): no lockouts, the other hero and Noel as items,
  three new join sites, `exclude_characters`, the healer guarantee; `starting_character`
  (open world, default hero) ("Party v2"); `starting_character: any` is the player's pick at
  New Game (the port's character select; the seed lists whom it offers), no longer a random
  member.
- *Difficulty* (2026-10-25, agent "difficulty"; the user's request): `difficulty` earth (default)
  / galaxy / universe / any, in every world mode. The port sets the seed's Battle Rank at New Game
  and skips the screen; `any` offers the screen with all three (the original offers it only once
  more than half of the Voice Collection is heard, which a seed's own memory card never is). It
  scales only the monsters' stats (HP, MP, attack, intelligence, accuracy, evasion), so the logic
  is unchanged; a non-earth value makes a story / open-world seed version 2.
- slot_data and `options` gain the new options, `party_rules` and `apworld_version`; the seed
  gains `version` 2 when needed. Location and item ids of 0.1.1 are unchanged; the new ones are
  types 11 (crafts) and 12 (killer moves), the characters Claude, Rena and Noel.
- *Hub mode* (2026-10-25; the user: hub mode is part of 0.2.0, "the first major revision of the
  apworld as is"): `world_mode: hub`, `hub_start`, `fienal_requirement`; dungeon keys (item ids
  0x50370000 + N), the Cross Cave guardians' boss location (hub only), the event "Fienal opened";
  version-3 seeds with `hub` and `bosses` ("Hub mode"). Story-mode and open-world seeds are
  byte-identical to the batch before it (Testing).
- *Fixed: the Cave of Trials' learn chests are no locations* (2026-10-25, agent cavefix;
  docs/handoff/cavefix.md). 0.1.x's "Cave of Trials Level 1: Portrait F / G / J chest" (ids
  1348308992, 1348368384, 1348369409; maps 740 / 798 / 799, really levels 4 / 10 / 10) are the
  routine that teaches Leon Extinction, Ashton tri-Ace and Celine Meteor Swarm: field.json read
  its push of the move index (0xc / 9 / 0xd) as items 12 / 9 / 13. They opened only with that
  member in the party, and their placeholder patch put the move index past the record's 0x20
  moves: the 0.02.00 port then read and wrote outside its member records. 0.2.0 leaves them out
  (with Portraits F, G and J from the pool), so a seed with `cave_of_trials` has 3 locations
  fewer and places its items differently; seeds without it are unchanged. The port's fix makes
  0.1.x seeds safe: nothing is learned or written, and the chest gives or sends its location's
  item (when the member opens it). Skillsanity is to bring them back as learn points.
- *Skillsanity* (2026-10-25, agent skillsanity2; the user's design): `skillsanity`, `skillsanity_shop_share`;
  location types 13 (guild checks) and 14 (learn points), item ids 0x50380000 (skills) and
  0x50390000 (moves); a seed with it is version 2 ("Skillsanity").
- *The Cave of Trials by its real floors* (2026-10-25, the user's ruling): its chests and gifts were
  all "Cave of Trials Level 1: ..." (0.1.x's name rule); now "Cave of Trials Level 7: ...", the
  treasure vault (map 710) "Cave of Trials Treasure Vault", the Weird Beast's arena (711) "Cave of
  Trials Weird Beast Arena" (`CAVE_FLOORS`, docs/modding/reference/map_names.csv). 76 names, the ids
  unchanged. Trackers read the names from each multiworld's own data package, so rooms generated
  before keep theirs; a seed with `cave_of_trials` fills otherwise (Archipelago sorts locations by
  name).
- *Fixed: the Cave of Trials' boss names* (2026-10-25, agent cavenames): the boss fights' place
  came from the nearest map's banner text below it, not `CAVE_FLOORS`, and disagreed with the
  chests' floors for three of the thirteen: Phoenix ("Cave of Trials Level 11: Phoenix boss", map
  809, really floor 12), G-Celesta ("Level 12", map 815, really floor 13), Weirdbeast ("Level 1",
  map 711, its own arena, now "Cave of Trials Weird Beast Arena: Weirdbeast boss"). The other
  ten already matched; ids unchanged.
- *Hub mode: the skill guilds sell* (2026-10-25): the hub state no longer sets flags 700-711 (1063
  -> 1051 hub flags).
- *Fixed: gift sites never hold progression* (2026-10-25, the user: "NPC gift checks should NOT
  hold progression"). Before, the late proof let the 9 gift sites it reaches hold progression in
  story mode, and in open world Eluria Colony's 2 gifts could hold our own Expel-story keys
  (0.1.1's "Fixed: the teleporter does not reach Expel's islands", below). Now `may_hold_progression`
  and `open_world_progression` (rules.py) treat `gift` as never-progression, the same as `drop`,
  `join` and `shop`, in either world mode and for this world's own keys and other worlds'
  progression alike ("Logic", "Open world").

**0.1.1** (2026-10-23, released with the port's 0.02.00). Open world only; story mode, the item
pool, location names and ids are unchanged.
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
- *Fixed: the teleporter does not reach Expel's islands* (docs/handoff/apareas.md). The
  teleporter proof fired the Expel world map's entrance triggers wherever they lie, so it
  counted Hoffmann Ruins' 26 chests and Eluria Colony's 2 gifts as reachable again after the
  disc switch, and 0.1.0 let them hold any progression, a key the story needs after the switch
  among it. But the party the teleporter lands in Arlia can only walk the Cross continent and
  take the Herlie ferry to the Lacour continent: Hoffmann Ruins' island and the El continent
  are reached only by the ships of chapters 23-28, and Expel's Synard only after the Final Save
  Point (tools/ap_walkmap.py reads the walkable terrain). A key there that the player had not
  picked up before the switch was lost (the Red Crystal Pass, say: the Synard is won with it);
  43 of the 45 open-world slots with the teleporter and expel_progression on in the same
  96-seed batch put a post-switch key (65) or another player's progression (78) there. Now the
  teleporter proof does not follow those entrances (`TELEPORTER_ISLANDS`), and those 28
  locations hold only our Expel-story keys in open world, as other Expel locations the
  teleporter does not reach; their teleporter access is gone (the story's visit and the Final
  Save Point remain).
- *Fixed: keys left behind by the El voyage* (docs/handoff/elgate.md; "The El voyage and the
  other one-way points" above). From the Front Line's meeting at chapter 27 step 7000 the story
  sails to El with no free play on the way, and the Cross and Lacour continents and Hoffmann
  Ruins' island stay out of reach until the teleporter or the Final Save Point; 0.1.0 put an El
  pass there in 110 of the 114 open-world slots of the 96-seed batch (0.1.1-pre: 114), lost if
  the player sailed without it. Now the voyage's stage needs the three El passes, the seed's
  door table gates the meeting's trigger on them (the port: "El Beach Pass needed to sail to
  El"), and an Expel location the story reaches before the voyage holds progression only where
  the new voyage proof reached it at chapter 27 step 7000 (all of them did).
- *Fixed: the teleporter needs the Synard.* Fun City lies on another island of Energy Nede than
  Central City: the arena is reached from stage 16 (the Cave of Red Crystal), not stage 13. With
  the teleporter and expel_progression on, the voyage also needs the North City, Noel's House
  and Red Crystal Passes, which the story needs before then (0.1.0 could leave them on Expel's
  continents: 85 in the 0.1.1-pre batch).
- *Fixed: walking out of Central City (chapters 36-37) or Noel's House (38-39)*, which the story
  enters without their entrance, onto an island with no way back in: until chapter 38 / 40 the
  exit needs the area's key (seed doors with `until`; no logic change).
- Area labels: field.json's `map_areas` agrees with docs/modding/reference/map_names.csv for 158
  more maps (tools/ap_extract.py `AREA_FIXES`; field.md "Place names"). The logic never read
  them; location names keep their 0.1.0 labels.

## Next

- Entrance shuffle on the door table ("Entrance shuffle (later)" above).
- Hub mode: the arena's prize gift (map 305, reachable after "no" at the ask through 297's
  desk, with Fol for the fee: not proven by the hub proof, which starts with New Game's Fol);
  the late proof's own search could walk Fienal's maze as the hub proof does (its 9 maze chests
  stay non-progression in story mode and open world until then).
- Tighter open-world proofs: a mid-Nede state with the flags of its time (the teleporter
  proof's chapter-60 flags keep it to Expel's maps).
- A `.apworld` zip for custom_worlds (phase 5).
