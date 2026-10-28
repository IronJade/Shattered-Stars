# The SO2 APWorld (`apworld/so2`)

Phases 2 to 4 of the Archipelago work: the Archipelago world for Star
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
  drops" below: their places are judged by the same proofs), killer-move levels
  `killer_move_progression` ("Killer moves"), and skillsanity's guild checks and learn levels
  their own rules (0.2.1: "Skillsanity"); with `expel_progression` off, nothing in Expel may.
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
  bosses (fewer with the options; options add drops, killer-move levels and, from 0.2.1,
  skillsanity's guild checks and learn levels): every multiworld in the batch below filled.
- Rerun the proof after a change to the port's field code, the event sweep or field.json
  ("Files", below).

**Characters are useful, not progression.** Member-gated logic would not be monotonic
(Ashton rules out Opera), so every member-gated location is excluded instead. The one
exception (0.2.0) is `killer_move_progression: any`, where a killer-move level of a member who
joins by their item needs the item, which is then progression; the pool never holds two
members who rule each other out, so a received member always joins ("Killer moves"). Skillsanity's
learn levels (0.2.1) do the same for such a member ("Skillsanity").

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
to Expel, and the only way to Virtual Expel), plus the North City Pass and the Noel's House
Pass unless the teleporter and `expel_progression` are both on. The first Energy Nede stages
need them (14 and 15) and before them Energy Nede holds no location of this world that may take
them but the Synard boss (North City has only shop slots; the teleporter's old man needs the
Synard, stage 16), so otherwise they must sit in Expel; with either option off Expel holds only
our Expel-story keys (below). In a solo world the North City Pass then had no legal spot and the
Noel's House Pass one, the Synard boss, which `boss_checks` off takes away (0.2.0 gave the North
City Pass at the start with the teleporter off only: every solo fill with the teleporter on and
expel_progression off failed, and every one with boss_checks off and either option off; 0.2.1,
"Changes"; the ncpfix handoff log).

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

**The El voyage and the other one-way points** (0.1.1; the elgate handoff log). The story
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
  seed_format.md "The harbour"; the ships handoff log). Entering Lacour at chapter 22 step 9999
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
  in the ships handoff log.

The teleporter proof's state has the save's chapter-60 flags, which Energy Nede's maps test,
so it is used for Expel's maps only. **Known limits** (accepted, conservative elsewhere): a map
that opens later in the story than its area's first visit, or closes for a while in the middle
of it, is not modelled (Lacour's fifth entrance in chapters 25-59 and Mars's second at chapter
15 have open siblings; the arena's route is shut in chapter 57, where only the Fun City Pass,
a starting key, is needed). The late proof fires the world maps' entrance triggers without
walking there too, which Virtual Expel's Synard (flight anywhere, Prima p. 130) justifies after
the Final Save Point. Central City at chapters 36-37 (the apareas handoff log, "Leads"):
settled by its exit gate (above).

## Hub mode (`world_mode: hub`; 0.2.0)

The user's decisions D1-D10 (the hub handoff log), the seed's blocks (seed_format.md "Hub"), the
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
  #2129; the killer_prof handoff log): one per proficiency level of each killer move a learn
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
  never in the party: left out. With `encounter_rate: 0` the seed is a hell seed (0.2.1: "The
  hell seed" below): the levels stay ordinary locations, trained in the free Fun City arena.
- **Access** (rules.py `killer_rule`): the member, at the level that learns the move, and the
  battles. Conservatively, a move learned by level 30 counts from the disc switch (a party there
  is well past it; 53 locations), any other from the Final Save Point (80). On a hell seed every
  level counts from the arena instead ("The hell seed").
- **`killer_move_progression`:** never (default): no progression. hero: the story members'
  moves only (Claude's, in Claude's game and under story_route any; Rena has no killer moves).
  any: every member the logic counts on (sure or by item). Under any, a level of a member who
  joins by their item also needs the item, which becomes progression
  (`world.progression_characters`: Archipelago's CollectionState counts only progression
  items); under never and hero the item is not required (nothing there holds progression, and
  full accessibility reaches every item anyway), unless the item is progression anyway: under
  skillsanity special the member's learn levels need it (0.2.1, `skillsanity.learn_characters`),
  and then their killer-move levels need it too.
- **The check:** `{type: killer_move, character, move, level, uses}`, `uses` = level x the
  move's divisor in the armed table; the port compares the raw use count.

## Skillsanity (`skillsanity`, `skillsanity_shop_share`; 0.2.0)

The user's design and decisions (the skillsanity handoff log); the seed and the port:
seed_format.md "Skillsanity"; the tables: `skills.py` (tools/ap_make_skills.py, from the disc),
the code: `skillsanity.py`. `skillsanity`: **none** (default: the original), **skills_only**,
**special_only**, **insanity** (both).

- **Skills** (skills_only / insanity). Each of the 12 guild sets' purchase (flag 700 + set, at any
  of its guilds: docs/notes/menu.md "Skill guilds") is one location per skill in it, "Skill Guild:
  Sensibility 2 (1 of 4)" (45); each skill is an item, "Skill: Perseverance", that lets the party
  learn it with SP. With `cave_of_trials`, the Level 11 prize (Float) is a location and Float an
  item. Members keep the skills they join with. A guild check's access and what it may hold:
  "Progression at guild checks and learn levels" below (0.2.1; 0.2.0: any of the set's guilds,
  never progression).
- **Special** (special_only / insanity). Every member keeps their learn table's first move (Claude's
  Air Slash, Rena's Heal, ...: known from the start or their join); every other killer move and
  spell is an item ("Claude: Shooting Stars"), and each learn point a location: a learn-table level
  ("Claude Lv 7: Shooting Stars", 139), Precis's and Opera's machines (the items 464-469 and the
  arena's Private Action: "Precis: Holo-holograph (Holoprojector)", 6) and, with `cave_of_trials`,
  the cave's learn chests ("Cave of Trials Level 4: Leon's Extinction", 3). Claude's Phase Gun is
  left out (the user). A member who can never join has none; one who joins only by the player's
  choice holds filler only (EXCLUDED). Access as killer moves': a learn level up to 30 from the disc
  switch, any other from the Final Save Point (hub mode: from the start / Fienal opened); the
  machines after the Final Save Point (hub: any time) with Machinery; the cave's in the cave. What
  a learn point may hold: below (0.2.1; 0.2.0: never progression).
- **Progression at guild checks and learn levels** (0.2.1; the user, 2026-10-27: "let learn points
  hold progression, and the skill guilds should also be able to hold progression, as long as they
  are within reach routing wise ... You can beat the game without ever visiting an item shop";
  the skilllogic handoff log; `skillsanity.guild_rule`, `learn_rule`, `learn_characters`). Item
  shops stay progression-free. Neither holds progression with `encounter_rate` 0 (no Fol, no
  levels), unless the seed is a hell seed (0.2.1: killer-move checks or learn points with
  `encounter_rate` 0; "The hell seed" below), whose free Fun City arena pays both.
  - *Learn levels* (learn-table levels only) of a member the logic counts on (fighter_status
    "sure", or "item": then the level needs the member's character item, which becomes
    progression, as with `killer_move_progression: any`); a "maybe" member's stay EXCLUDED. Access
    as above; in open world under the story's party rules Noel's early levels also need stage 15
    (his house; `STORY_JOINS`), unless he is the starting character. Story mode needs no item to
    reach him, so the disc switch stands for it there.
  - *Never progression:* Precis's and Opera's machines (465-469 come only from rare Machinery
    results, 464 from a shuffled gift or a Private Action), the cave's three learn chests (the
    late proof didn't reach them) and Float (no proof covers the prize).
  - *Guild checks:* each of the set's guilds is a place, as a first drop's are (`rules.drop_rule`):
    judged as a chest on the guild's map (story mode: the late proof, outside Expel with
    expel_progression off; open world: `open_world_progression` / `open_world_access`, so never a
    guild that closes at the El voyage, Port of Clik's; hub mode: the hub proof's maps in every game,
    outside Expel with expel_progression off). The check holds the best class of its places and is
    reached only through the places of that class. Every set has a late-proven guild on Energy Nede
    (Central City, North City or Armlock), so with battles every guild check may hold progression.
  - *The Fol* (the user: "Fol rule is fine as written"). Random battles pay a set (Earth, Fol x1,
    the floor; docs/ap/battles.json's enemy Fol over field.json's encounter tables): Expel's world
    map ~460 a battle, Mars ~400, the Sanctuary of Linga ~920, Hoffmann Ruins ~2100, Eluria Tower
    ~3500; Energy Nede's world map ~520, its dungeons 2400-3600. A set up to 1600 Fol (the 1 and 2
    sets) counts from any guild that sells it (its first guilds: Cross, stage 3, 300-400 Fol;
    Herlie, stage 5, 1500-1600: about four world-map battles). A set from `DEAR_PRICE` 2700 (the 3
    sets, 2700-4500 Fol) counts through an Expel guild only from the end of stage `FOL_STAGE` 9
    (Hilton and Hoffmann Ruins: two Hoffmann battles pay the dearest); Energy Nede's guilds come
    later anyway. Story mode (no stage events): a cheap set from the start through a proven Expel
    guild, a dear one, and any set through an Energy Nede guild, from the disc switch. Hub mode:
    the cheap sets from the start, the dear ones once Fienal opens.
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

## The hell seed (`encounter_rate: 0` with killer moves or learn points; 0.2.1)

The user (2026-10-27): "I believe that there is a fee to use the training arena in Fun City, but
if we can, I think a better route would be to eliminate the fee to train there IF and ONLY IF those
rules are set on the seed. Something like that is usually called a "hell" seed, where the grind is
intentionally made preposterously hard." And: "For the Hell seeds, we should have the setup
archipelago seed read throw a warning that the seed being loaded is a "Hell Seed" as well."
(the hellseed handoff log; the code: `hell.py`.)

- **What it is** (`hell.is_hell_seed`): `encounter_rate` 0 (no random battles) and something that
  still needs training: `killer_move_checks` (a move's use count) or `skillsanity` `special_only`
  / `insanity` (learn points: levels). Every other seed, `encounter_rate` 0 with only drops or
  guild checks included, is as before.
- **The arena** (verified headless in the port, the hellseed handoff log "Phase 1"): the Fun City
  Fighting Arena's fights are ordinary battles (map 305's ext 0xb7, no battle rule, never reading
  the encounter rate): a killer move's uses count, the fighter gains the enemy's EXP and the party
  its Fol (a rank E Duel: 104 EXP, 102 Fol; Bullying up to ~12,000 Fol a registration). Any member
  in the party may enter. Its fee, 2000 Fol a registration (map 297's script, Bargaining +/- 3 % a
  level), is waived on a hell seed only: the seed's patch line `script 297 0x177f 0x120007d0
  0x12000000` (`hell.FEE_PATCH`; seed_format.md "The hell seed"), so a registration never costs
  Fol and every fight pays.
- **When the arena is open** (`hell.arena_need`): story mode after the Synard, which the story
  reaches with no item after the disc switch, so the disc switch stands for it (as for killer
  moves already); open world from the end of stage 16 (`TELEPORTER_STAGE`, the Cave of Red
  Crystal, the Synard) with the Fun City Pass (a start key; the Expel teleporter's rule, whose old
  man is in the same building); hub mode from the start (Fun City is a hub town). Fun City is shut
  in chapter 58 only, which the story passes with no new key. The gate's 75 Fol a person (before
  the N.F.I.D.) is a pre-existing assumption of every Fun City rule.
- **The rules on a hell seed** (`hell.trained`: the party has battles to train in): killer-move
  levels (`rules.killer_rule`), learn levels (`skillsanity.learn_rule`) and guild checks
  (`skillsanity.guild_rule`: the arena's fights pay the Fol) count from the arena, with their usual
  member, move and character-item needs, and may hold progression as they would with battles
  (EXCLUDED "maybe" members' still none). So a level past 30 no longer waits for the Final Save
  Point, nor a dear guild set for stage 9 (story mode: everything from the disc switch; hub mode:
  everything from the start, Fienal not needed). Guild checks in open world: each guild place's
  way combined with the arena's (`hell.arena_way`: no sooner than stage 16, and the Fun City Pass);
  an Expel guild's own early way is dropped (Expel closes at the El voyage, before the arena opens;
  the teleporter's and the late proof's ways stay). Drops stay progression-free at `encounter_rate`
  0 (the arena's enemies are not the field's).
- **The seed:** the patch line and `hell_seed: true` (seed options and slot data) on a hell seed,
  on no other seed (absent, not false, so every other seed reads as before). No new seed version:
  the patch line is a version-1 feature, `hell_seed` is display only, and a hell seed is version 2
  or 3 anyway. The port's 0.02.51 shows `hell_seed` as a warning on setup's Archipelago page and
  logs it when the seed loads; 0.02.50 plays a hell seed the same, without the warning.
- **Why** (0.2.1's last failure, the killer0 handoff log): with `encounter_rate` 0 every
  killer-move level was EXCLUDED and no learn level or guild check could hold progression, but
  skillsanity still made each chosen killer level's move progression, so solo_noexpel seed 75's
  mix with `encounter_rate: 0` failed every fill. On a hell seed those locations hold progression
  again.

## Party v2 (open world with `character_items`; 0.2.0)

The new character rules are open world only (the user, 2026-10-25); story mode keeps the
story's party rules ("The party" above). `world.plan_party` returns the `Party`: the rules
(`party_rules` in the seed), the story members, the starting member, the pool, who is sure to
be in the party and who can ever be. With the bench (0.2.1, on by default; "The bench" below)
the party has 12 places instead of 8.

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
  `Party.choices` is whom the select offers: the story members and the pool (at most 8; with the
  bench every member not excluded), or under the story's party rules every member not excluded;
  the seed's `starting_character` is
  that list. The pick's own item, when it comes, finds them in the party. A member the
  generator picks: Archipelago's `random` (one of the option's values). Story mode ignores it (a
  warning). The member joins at the start and leads (the port: the field sprite and the name
  swap, seed_format.md "Party v2"). `hero` is the default because a start would make every
  open-world seed a version-2 seed and change the game for players who did not ask.
- **The pool** (`choose_open_party`): every other member not excluded and not the start, as many
  as fit 8 with the story members and the start (a random sample when more are allowed; under
  `any` the start's place goes to the pool too); with the bench, 12 (`world.party_size()`), so
  every one of them. No
  lockouts: Leon and Dias in either game, Bowman with Precis, Ashton with Opera and Ernest.
- **The healer guarantee** (without the bench): when none of the story members, the start and
  the pool is Rena or Noel (the only healers), an allowed healer takes a random pool member's
  place (the pool is full then); with both excluded, none (the player's choice).
- **Join sites:** every non-story member's, the three new ones included; all EXCLUDED. Members
  not in the pool (and neither story members nor the start) never join.
- Open world without character_items keeps the story's party rules (`party_rules: "story"`),
  plus the starting member.

### The bench (`bench`; 0.2.1, on by default)

The user (2026-10-26, the bench handoff log "The user's answers" B7: "a `bench` option, on by
default"; 2026-10-27: "Bench needs to be rolled into the 0.02.51 + 0.2.1 release"), to "make
every character obtainable ... keep from causing any issues wherein the user gets a full party
and there are 4 checks that were the character items ... It also guarantees that the player can
choose to use whoever they would like in the party." The port keeps a bench of four places (8-11)
after the game's 8 slots: a member received with the 8 full joins the bench, and Party Order
swaps anyone but the hero between the party and the bench (seed_format.md "The bench", port
0.02.51; built on branch party-bench, the partybuild handoff log).

- **Where it applies** (`world.bench_applies()`, kept as `world.bench`): open world with
  `character_items` (`party_rules: "open"`) and hub mode, exactly where the port reads
  `options.bench`. Story mode and open world without character_items keep the story's party
  rules and the game's 8 places; the option is ignored there (open world without
  character_items: a warning; story mode: an info line, since the option is on by default).
- **The party:** `world.party_size()` 12 (items.BENCH_PARTY_SIZE = PARTY_SIZE 8 + BENCH_SIZE 4).
  The pool is every member not excluded but the story members and the start, with no random draw
  (the world's RNG is not touched there), so every character item a player could want is in the
  pool, and none is left out by a draw. The healer guarantee has nothing to do (both healers are
  in unless excluded) and is skipped. `starting_character: any` offers every member not
  excluded.
- **The logic is unchanged:** a received member can be swapped in at any free play, so every
  pool member is "item" to `fighter_status` (their killer-move and learn levels need their item,
  which is then progression where those may hold it), the story members and the start "sure".
  The members the 8 places would have left out add their killer-move levels, crafts and learn
  levels (`Party.possible` is every member not excluded).
- **The seed:** `options.bench: true` (slot data `bench`) and **version 4** (`SEED_FORMAT_V4`,
  open world and hub alike), only where the bench applies: a port that reads 1 to 3 (0.02.50)
  refuses the seed ("unsupported version 4") instead of playing up to 12 members in 8 places.
  Without it `bench` is absent and the seed is byte for byte what it was before the option
  (Testing, "Results ... the bench": all 288 release-workflow seeds with `bench: false` pinned).
- **Off** (`bench: false`): 0.2.0's 8 places, the random pool and the healer guarantee, seed
  version 2 (open world) or 3 (hub).

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
| `bench` | true (default) / false | open world with character_items, hub mode: 12 places (the game's 8 and the port's bench of 4), the pool every member not excluded, no healer guarantee; seed version 4 ("The bench"); ignored elsewhere |
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
pool's members), `party_rules` ("story" / "open") and `apworld_version`; `bench: true` only where
the bench applies (absent otherwise, like `hell_seed`). The port reads the
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
goal needs a victory. Checked in the scenes themselves: the deathlink handoff log.

## The seed (`generate_output`)

`SO2World.seed_data()` writes:
- `version`: 1, or 2 when the seed uses a field an older port would ignore (a limiter-off goal,
  `party_rules: "open"`, a starting character, craft or killer-move locations,
  `no_fail_crafting`, a `difficulty` other than earth: `seed_version`; seed_format.md "The
  version rule"), so the 0.02.00 port
  refuses such a seed instead of playing it wrong; 3 for a hub seed (and its `hub` and `bosses`
  blocks, "Hub mode" above); 4 for a seed with the bench, open world or hub ("The bench");
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

## Rules as data (Rule Builder; 0.2.2)

Every access rule (locations, events, entrances and the goal) is an Archipelago **Rule Builder**
rule (`rule_builder/`, Archipelago 0.6.7 and later; its `docs/rule builder.md`), not a lambda:
the tracker plan's B2, the user's decision D3. The rule sites work out what a place needs from
the world's data and options, as before, and set it with `world.set_rule(spot, rule)` /
`world.set_completion_rule(rule)` (an entrance: `Region.connect(target, name, rule)`, which calls
set_rule). Resolving a rule for the world simplifies it into frozen dataclasses (`Rule.Resolved`),
interned: the same rule twice is one object. Item rules (`add_item_rule`: what a location may
hold) stay functions. The logic is unchanged ("Testing", 0.2.2: the same truth value for every rule
on every state tried, the same seeds byte for byte), so `ut.LOGIC_VERSION` stays 1.

`apworld/so2/logic.py` builds the rule data's three shapes:
- `need(names)`: all of these items and events (`HasAll`; none: open, `True_`). A story stage's
  previous stage and keys, the disc switch, the Final Save Point, the teleporter, the goal events,
  a killer-move level, a learn level, the hub's Fienal gate and Indalecio;
- `ways(modes)`: any of the (event or None, keys) ways, each its event and all its keys (`Or` of
  `HasAll`; none: never). An open-world location's modes (rules.open_world_access), a drop's
  (rules.drop_rule), a guild check's (skillsanity.guild_rule), open world's cave and Float learn
  points;
- `any_of(alternatives)`: any of these item tuples, all of one. A specialty's skill items per
  member (skillsanity.specialty_needs: a craft, the Iseria Queen's song, the machines' learn
  points), a hub drop's dungeons' accesses (hub.drop_rule);
- `both(rule, extra)`, and `open_by_default`, which `set_rules` ends with: every location, event
  and entrance that set no rule of its own gets `True_`, so each carries a Rule Builder rule.

The sites: regions.py (the story's Disc Switch and Virtual Expel entrances; open world's
`stage_rules`), rules.py (`goal_rules`, the completion, `killer_rule`, crafts, the open-world modes
and drops), skillsanity.py (`needs_rule`, `set_location_rule`), hub.py (the dungeon and Fienal
entrances, the gate / Indalecio / Iseria Queen events, the completion, crafts, drops). Resolved,
they use only `True_`, `False_`, `Has`, `HasAll`, `HasAny`, `And` and `Or`
(`logic.RESOLVED_KINDS`), with items and events by name. No rule reads a region, a location or an
entrance (no `CanReach...`), so no indirect condition is needed; an entrance shuffle's rules that
do would get theirs registered by the Rule Builder itself (`set_rule` on an entrance).

What it gives:
- Universal Tracker's `/explain <location>` prints the location's rule with what you have in
  green and what is missing in red, and the rules of the entrances into its region ("Trackers"
  below has a sample); `/get_logical_path` uses them too.
- The tracker export (the tracker plan's B3) walks the resolved rules: `logic.as_rule(resolved).to_dict()`
  serialises one (JSON), and `SO2World.rule_from_dict` of that, resolved for the same world, is the
  same rule.
- Generation is as fast as with the lambdas. The Rule Builder's result cache
  (`CachedRuleBuilderWorld`) measured no faster for rules this flat (mostly one `HasAll`, or an `Or`
  of a few), so the world is a plain `World` ("Testing", 0.2.2).

**Adding a rule.** Work the need out from the data at the rule site, build it with `logic.need` /
`ways` / `any_of` / `both` (or the Rule Builder's `Has`, `HasAll`, `HasAny`, `And`, `Or`; `&` and
`|` combine rules), and set it with `world.set_rule` (`Region.connect` for an entrance,
`world.set_completion_rule` for the goal). Never assign `access_rule` or write a lambda for one:
test/test_logic.py fails on both. A rule refuses `bool()`: test an optional rule with
`is not None`, never `if rule:` or `rule or ...`. A new rule kind (a custom Rule class, a
`CanReachRegion`) goes into `logic.RESOLVED_KINDS` and `logic.as_rule`, and changes the tracker
export's format (B3) and the PopTracker pack's evaluator. A rule that changes what is in logic
bumps `ut.LOGIC_VERSION` ("Trackers", "When to bump").

**The no-lambda test** (test/test_logic.py). Eleven worlds (story, open world and hub with their
defaults, and the rich mixes: Skillsanity insanity with craftsanity, killer moves, drops and the
Iseria Queen goal in each mode, forced drops, the teleporter off, hell seeds in story and open
world): every location, event and entrance and the goal holds a `Rule.Resolved` made only of
`RESOLVED_KINDS`, naming only this world's items and events, set through `set_rule` (the
`NOT_RULE_BUILDER` list of exceptions is empty); the rule as set serialises through JSON
(`to_dict`) and comes back (`rule_from_dict`, resolved) as the same rule, and so does the resolved
rule through `as_rule`; each explains itself (`explain_json`, `explain_str`). Also: rules are
interned, no rule reads a region, and a scan of the world's modules for `.access_rule =`,
`completion_condition[...] =` and `lambda state`.

## Trackers (Universal Tracker; 0.2.2)

The tracker plan (B1 is this section) is built: PopTracker in docs/ap/poptracker.md, the map tab in docs/ap/tracker_maps.md.
[Universal Tracker](https://github.com/FarisTheAncient/Archipelago/releases) (UT: FarisTheAncient's
`worlds/tracker`, 0.3.6 for Archipelago 0.6.2 and later) is an Archipelago client. It loads
so2.apworld itself, builds the world again on the player's machine (generate_early ..
generate_basic, a new random seed, `generation_is_fake`), and lists the locations our rules reach
with what the server says was received and not yet checked. No export from us; what it needs is
that the rebuilt world is **the same world**.

**For players.** Put `tracker.apworld` (UT's release asset) and **the so2.apworld the room was
generated with** in Archipelago's `custom_worlds`, open "Universal Tracker" from the Launcher,
connect with the slot name. No YAML: the world is yaml-less. The port can be connected at the same
time (trackers connect with tags `Tracker`, items_handling 0b111; the server refuses checks from a
tracker). The player guide (apworld/so2/docs/setup_en.md "Tracking with Universal Tracker") says
the same, and that checks made offline show once the port reconnects.

**`/explain <location>`** (0.2.2, "Rules as data") prints the location's rule against what the
server says was received (UT's colours: green had, red missing), then its region and the rules of
the entrances into it. The real UT 0.3.6 on a local room, fresh (scratchpad/rulebuilder/ut_explain.py;
open world, then hub):

    /explain Alen's Mansion: Portrait B chest
      (Missing some of (Found: Stage 1 done; Missing: Salva Pass) | Missing some of (Found: Fun City
      Pass; Missing: Expel Teleporter, Salva Pass) | Missing some of (Found: Fun City Pass; Missing:
      Salva Pass, Saved at the Final Save Point))
      Parent region (Expel)
      Menu (True): New Game : True
    /explain Central City: Chisato joins
      Missing some of (Missing: Central City Pass, Stage 16 done)
      Parent region (Energy Nede)
      Expel (True): Disc Switch : Missing Arrived on Energy Nede
    /explain Iseria Queen (Cave of Trials)
      Missing some of (Missing: Skill: Music Instrument, Skill: Musical Notation)
      Parent region (Cave of Trials)
      Expel (True): Into Cave of Trials : Missing Cave of Trials Key

**How (apworld/so2/ut.py).**
- `SO2World.ut_can_gen_without_yaml = True` and a static `interpret_slot_data` that returns the
  slot data: UT skips the YAML and regenerates once with `multiworld.re_gen_passthrough =
  {game: slot_data}` from the default options.
- `generate_early` first: `ut.passthrough` (None in a real generation: nothing below runs, and the
  batches stay byte-identical), then `ut.restore`: the room's `logic_version`, then every option
  through **one table**, `ut.SLOT_OPTIONS` (slot key -> option, a reader, what an absent key
  means): `starting_character` reversed (None -> hero, a list -> any, a name -> that member),
  `bench` absent -> off, `hub_start` / `fienal_requirement` required in hub mode only,
  `exclude_locations` absent (0.2.0 / 0.2.1) -> none. `characters` and, in hub mode,
  `hub_start_town` must be there.
- The random choices that shape the world come from the slot data: `choose_open_party` /
  `choose_party` return `characters` (without the bench the pool of 8 is a random draw; with it the
  pool is every allowed member already, so this only matters for bench-off seeds and 0.2.0
  rooms), `hub.choose_start` returns `hub_start_town` (no rule reads it, but the rebuilt world
  names it). Everything else that is random places items only (UT never fills).
- `generate_early` last: `ut.check_restored`. The rebuilt world's `slot_options()` (the .apso2's
  `options`: options, party, derived values) must equal the room's, key for key, but
  `apworld_version` and 0.2.2's two keys. The derived keys (`seed_format`, `goal_encounters`,
  `party_rules`, `fienal_won`, `dungeon_keys`, `hell_seed`) prove the restore and that this world
  computes from it what the room's world did.
- `fill_slot_data` = `slot_options()` + `logic_version` (`ut.LOGIC_VERSION`, 1) +
  `exclude_locations` (sorted names; UT runs exclusion_rules after set_rules). The .apso2 keeps
  `slot_options()` only, so seeds are byte for byte 0.2.1's (seed_format.md "Slot data and
  trackers").
- **Refusals (D10).** When the room's `logic_version` is not in `ut.LOGIC_VERSIONS`, a key is
  missing or holds a value this world doesn't know, or the check above fails, `ut.refuse` raises
  UT's `TrackerException` (imported when needed; `ut.TrackerRefusal` without UT) with a message that
  names the so2.apworld to install, and logs it. UT shows "your options are incompatible";
  `/faris_asked` prints ours.
- `custom_ut_sort(region, location)` = `ut.sort_key`: (story stage of the area, area, name). An
  area's stage is its locations' most common entrance group's (data.KEY_GROUPS `stage`); an area
  the story never enters comes after its world's stages, the Cave of Trials after both, then the
  skill guilds, learn points, Item Creation's specialties and killer moves by member; a line that
  is no location of ours (an entrance) last. UT's default `sorting_method: apworld` uses it.

**`logic_version`: when to bump it.** Whenever the same slot data would rebuild other logic:
another location set, region, entrance, event or rule for the same options and party (rules.py,
regions.py, hub.py, skillsanity.py's rules, locations.choose / hub.choose, plan_party), or a new
random choice that shapes them. Bump `LOGIC_VERSION`; keep the old number in `LOGIC_VERSIONS` only
if this world can still rebuild such rooms exactly (it then has to branch on the room's version).
A new option is not a bump by itself: rooms without its key restore with what it meant before
(give SLOT_OPTIONS an `absent` value), as `bench` does.

**Older rooms.** Rooms from 0.2.0 and 0.2.1 have no `logic_version`: version 1 when they restore
cleanly. 0.2.1 is 0.2.2's logic. 0.2.0's differs in two places, measured by generating rooms with
0.2.0's world (v0.02.50's apworld/so2 in a separate Archipelago copy) and rebuilding them with this
one, 17 mixes x 5 seeds (scratchpad/utworld/oldroom.py): every mix without Skillsanity and without a
hell seed matches in every sphere (story, open world with every party shape, the teleporter off,
hub); a hell seed (`encounter_rate: 0` with killer moves, which 0.2.1 made a hell seed) would fail
the check above (its rebuilt `hell_seed`); and with `skillsanity` on, 0.2.1's new guild-check and
learn-level rules make UT list fewer locations, although the slot data restores cleanly.
- **0.2.0 hell seeds are refused** (`ut.OLD_LOGIC`: "Track it with so2.apworld 0.2.0 and your YAML in
  Universal Tracker's Players folder").
- **0.2.0 Skillsanity rooms are tracked with a warning** (the user, 2026-10-27: "accept with a
  warning", on the condition that the rebuilt logic is conservative). Measured
  (scratchpad/logicexport/oldroom_subset.py over the same 85 rooms: at every sphere, UT's in-logic
  unchecked locations against the room's own sphere, and nothing left after the last): of the 45
  Skillsanity rooms (9 mixes: story, open world and hub with insanity, skills_only and
  special_only), **29 list fewer at some sphere and none ever lists more**, 16 match exactly; the
  user's own 0.2.0 hub room (insanity; its multidata's 13 spheres, read in memory) lists 36 fewer
  at sphere 1 and 6 at sphere 10, never more. So such a room's checks show in UT late, never early.
  `ut.restore` logs (`ut.OLD_LOGIC_WARNINGS`): "This room was made with so2.apworld 0.2.0 with
  Skillsanity on: 0.2.1 changed the logic of Skillsanity's guild checks and learn levels, so
  Universal Tracker may show some of them later than they really became reachable (never earlier).
  Regenerate the room with so2.apworld 0.2.2 or later for exact tracking." UT prints it in its log.

With that, every 0.2.0 room tested matches in every sphere, is a subset with the warning, or is
refused (a hell seed).

**Tests.** test/test_ut.py (see "Testing"): `ut_rebuild` / `in_logic` mirror UT's TMain and
updateTracker without UT installed, and each mix class runs UT's fuzzer-hook check (for every
sphere of the real, filled generation, UT's in-logic locations are exactly that sphere's) with two
UT seeds: story (default, a random story party, Rena's route), open world (every check option;
without the bench; starting_character any with and without the bench; a named start with
exclusions; the healer guarantee; the teleporter off; a hell seed; exclude_locations), hub (full
with random_town; without the bench; fields on Claude's route). TestUTRoom: the hooks, the new
keys (in the slot data, not in the .apso2), 0.2.0 / 0.2.1 rooms, every refusal, UT's own
exception class (a stand-in module). TestUTCoverage: every option of SO2Options is in
SLOT_OPTIONS or `ut.NO_EFFECT` (Archipelago's common options: fill, locality, hints, start
inventory), the world's code reads none of NO_EFFECT, and every slot data key is an option, a
restored choice, derived (compared) or new. TestUTSort: the order. `SO2_SLOW_TESTS=1` adds 20
room seeds per mix (TestUTManySeeds).

**The real UT, scripted** (the release workflow; `tools/ap_ut_check.py`): it makes its own
Archipelago copy for UT (a symlink farm of scratchpad/ap/Archipelago with its own host.yaml,
`tracker.apworld` and our world: the repo's folder, or a built so2.apworld with `--apworld`), so the
shared install is never changed. It generates a room (three Star Ocean players: story with a random
story party, open world without the bench and starting_character any, hub with a random town and
an excluded location), hosts it on 127.0.0.1 (MultiServer, `--disable_save`), and at three points
(fresh, after the room's first sphere is checked, after the second; checks sent by a client
connected as the slot, as the port sends them) runs UT headless (`Launcher.py "Universal Tracker"
-- --nogui --count --list --connect localhost:PORT --name SLOT`) for each slot. UT's count and list
must be the world's own (the generated multiworld's reachable, unchecked locations, worked out the
way updateTracker does), and its list in custom_ut_sort's order.

    gh release download Tracker_v0.3.6 --repo FarisTheAncient/Archipelago --pattern tracker.apworld \
        --dir scratchpad/ap_ut          # sha256 39e9bc0f...5cb750 (the release's digest)
    SKIP_REQUIREMENTS_UPDATE=1 prlimit --as=$((8<<30)) scratchpad/ap/ap-venv/bin/python \
        tools/ap_ut_check.py --tracker scratchpad/ap_ut/tracker.apworld [--apworld dist/so2.apworld]

### The logic export (for PopTracker; tracker plan B3, 0.2.2)

A tracker that can't run the world (the PopTracker pack's Lua: decision D2, no game logic copied
into Lua) reads the world's logic from the slot data instead: `fill_slot_data`'s **`tracker`** key,
written by `tracker_export.export(world)` (format 1: docs/ap/seed_format.md "The tracker export").
- **What it walks.** The world as it stands when fill_slot_data runs (after connect_entrances, so
  an entrance randomizer's final graph is the one exported): the regions (the origin first), every
  entrance, every event (a location with no id holding an event item: the story stages, the Disc
  Switch, the Final Save Point, Fienal's gate, the goals), every location with an id, and the
  completion condition. Each spot's rule is a Rule Builder resolved rule ("Rules as data"), written
  as a JSON body: `true` / `false`, `["has", ref, n]`, `["all", ref...]`, `["any", ref...]`,
  `["and", rule...]`, `["or", rule...]`. Items are AP ids, events their item's name. Entrances are
  `[name, from, to, rule]`.
- **Interned.** Each body is in the `rules` table once (children first: an `and` / `or` names its
  children by index); spots name their rule by index; the locations come in groups of the same
  (region, rule). The resolved rules are often one shared object, so the walk memoises by identity
  too. At the richest open world: 1,988 locations, 259 bodies, 35 KB.
- **Not in the .apso2** (seed_data writes `slot_options()` only): the port never sees it, and the
  seeds stay byte for byte 0.2.1's. Universal Tracker ignores it (`ut.NEW_KEYS`: check_restored
  skips it; a rebuilt world exports the same, which test_ut's room check also compares).
- **Its own version**, `format`, is what a tracker checks; `ut.LOGIC_VERSION` is unchanged (the
  logic is). A new rule kind (Phase C's CanReachRegion: `["region", r]`, which both evaluators read
  already) needs its form in tracker_export first; anything else raises a TypeError at generation.
- **Room for the entrance randomizer** (the user, 2026-10-27: the logic engine must "follow routes
  to get from one place to another" through unconnected doors): per-door entrances named by their
  door ids, deferred entrances written with `to` -1 and connected by the trackers when found
  (`TrackerLogic.connect`, the pack's `so2_logic_connect`), and optional keys such as a region ->
  map ids hint, all within format 1 (seed_format.md "The tracker export"). No ER is built yet.

**The reference evaluator** (`tools/ap_tracker_logic.py`, plain Python, no Archipelago): from an
export and a dict of received item counts, the region sweep and events to a fixpoint, then each
location's access; `TrackerLogic(export).evaluate(counts)` gives the reached regions, the events,
the locations in logic and the goal. The pack's `scripts/logic.lua` is the same algorithm in Lua.

**Tests** (test/test_tracker.py; the evaluator is loaded from the repo's tools/, so from a packaged
.apworld these skip):
- One class per mix of test_ut.MIXES (16: story, open world, hub, bench on and off, the start
  shapes, the healer guarantee, the teleporter off, the hell seed, the rich hub, excluded
  locations): the room (seed 11) generated and filled, its export read through JSON as a tracker
  gets it. At every sphere of the generation (the items found so far, the precollected ones first,
  as the server sends them), after the last, on the empty state, with every item and on 8 random
  item states, the reference's reached regions, events, locations in logic and goal are the
  world's own (a CollectionState with those items and the events swept), and its unchecked
  locations in logic are the sphere's. Each test well under a second (0.5 s at the most).
- `SO2_SLOW_TESTS=1` (TestTrackerManySeeds): the same for 20 seeds per mix, 320 rooms.
- TestTrackerExport: the shape (origin first, every region, entrance, event and location once,
  equal bodies once, children before parents, refs that are this world's item ids or events); the
  round trip (each body read back as Rule Builder rules resolves to the spot's own rule, the goal
  too); every resolved kind has a form, any other raises; not in slot_options.
- TestReferenceEvaluator: a hand-made export for counts, events counted per location, several
  passes, the "region" kind behind an entrance (an indirect condition), the goal, a deferred
  entrance (`to` -1) reaching nothing until `connect`; unknown formats and kinds refused. TestTrackerExportSize: at most 64 KB at the richest mix of each mode.

### Story awareness (both trackers, story mode; tracker plan B6, decision D8, 0.2.2)

The user (2026-10-27): "Story awareness in story mode would be a fantastic addition, yes." Story
mode's stages need no items, so a tracker following the logic alone lists nearly every check from
the start, though the party can't walk to most of them yet. The port publishes how far the story
has got (`so2_story_{team}_{slot}` = `{"chapter": C, "step": S}`: docs/ap/data_storage.md), and
with it both trackers show only what the story has reached; the PopTracker pack also marks what
comes later in the story. One source of truth, `apworld/so2/story.py`, used by both:

- **The points.** Stage k of data.STAGES is reached when the port reports its first chapter: each
  chapter value is stored in exactly one place in the game's scripts (docs/ap/field.md "Story
  order"; tools/ap_world_tables.py STAGES lists each stage's chapters from those stores), so a
  stage begins with the store of its first chapter. A 24th point, **the Final Save Point**, is
  chapter 60 step 4000: step 4000 is stored when Cyril falls (map 674, Floor 7, under chapter 60
  step 3000), the room before the final ascent to the save point (map 671), and Indalecio stores
  5000 (map 685) (the step stores: scratchpad/enemy/ap5/steps.json, from tools/ap_extract.py's story
  points); the late proof's state, a save made at the Final Save Point, is chapter 60 step 4000
  ("The late proof"). Saving there sets flag 730, which opens Virtual Expel; the key has no flags,
  so the point is the step before the save.

  | Point | Reached at (chapter, step) | Point | Reached at |
  |---|---|---|---|
  | 1 Shingo Forest and Arlia | 3 (Rena's game starts at 4) | 13 Energy Nede | 36 (map 618, after the disc switch's 35) |
  | 2 Salva and Salva Drift | 5 | 14 Synard | 38 |
  | 3 Cross | 9 | 15 Noel | 39 |
  | 4 Cross Cave | 12 | 16 Cave of Red Crystal | 40 |
  | 5 Clik and Mars | 14 | 17 Central City | 41 |
  | 6 Herlie | 16 | 18 The four fields | 42 |
  | 7 Lacour's tournament | 18 | 19 L'Aqua | 46 |
  | 8 Linga and its sanctuary | 20 | 20 Fienal's first visit | 48 |
  | 9 Hilton and Hoffmann Ruins | 23 | 21 Armlock and Mihne | 52 |
  | 10 Lacour Front Line | 26 | 22 Fun City | 57 |
  | 11 El | 29 | 23 The last chapters | 58 |
  | 12 Eluria Tower | 32 | 24 the Final Save Point | 60, step 4000 |

- **A location's point** (`location_stage`): the first stage in which the story can take the party
  there, from the data the open world's logic already uses: its area's entrance group's first stage
  (data.KEY_GROUPS `stage`, rules.early_stage), raised to the stage of a gift's first chapter (its
  `window`); a story boss's chapter; a first drop's or guild check's earliest place; a member's story
  join for his or her killer moves and learn levels (a member who joins by a character item: any
  time). Expel is left behind at the disc switch, so an Expel place the story first reaches after
  stage 12 (Lacour's chapter-60 gifts), Lacour's King's Room (a chapter-60 scene, an Energy Nede row
  behind an Expel entrance) and the Cave of Trials are reached through Virtual Expel: point 24. What
  has no stage of its own follows its region: Item Creation and the areas the story never walks into
  (Lasguss Mountain, the Mountain Palace, a world map's drops) from point 1 in Expel, Giveaway and
  the Gallery from 13 on Energy Nede; Fienal's floors from 23 (as open world counts them).
- **The events** (`EVENT_STAGES`): the disc switch ("Arrived on Energy Nede") needs point 13, the
  Final Save Point point 24, on top of their rules; so the regions behind them, the killer moves and
  learn levels counted from them (Energy Nede for levels up to 30, the Final Save Point above) and
  the goal's events follow the story too.
- So a location is reachable for the story when its own point and every story event its rule needs
  are reached. An approximation where it must be one: a stage is the unit (a gift whose first chapter
  is in the middle of its stage shows from the stage's start), and Fienal is entered at chapter 60
  step 1000, two chapters into its stage.

**Universal Tracker** (UT's documented deferred events: worlds/tracker/docs/apworld-integration.md
"Deferred Entrances", map-integration.md "Deferred events"). Only in UT's own regeneration of a
story-mode room (`re_gen_passthrough`) and only while host.yaml's `enforce_deferred_entrances` is
not `"off"` (UT's default is `"default"`; absent, as in UT's fuzzer and every real generation, counts
as off):
- `story.prepare` (end of set_rules) adds 23 events `Story reached: <point>` (points 2-24) in Menu,
  open (True_) until the port reports, and puts each location's and story event's point on top of
  its rule (`And(rule, Has("Story reached: <point>"))`, still Rule Builder rules, so `/explain`
  names the point); then it sets **this world's** `found_entrances_datastorage_key` to
  `("so2_story_{team}_{player}",)`, the port's key. Open world and hub mode never set it, so UT
  watches no key there.
- UT's client formats the key, Gets and SetNotifies it, and calls
  `SO2World.reconnect_found_entrances(key, value)` on each Retrieved / SetReply (then updates the
  tracker, with a fresh state each time). `story.reconnect` opens the events of the points the
  position has reached and shuts the later ones (a position that goes back, an earlier save
  loaded, shuts them again). `null` (the port never connected) or a value that isn't a position
  (or has chapter 0 or below) opens them all: UT then shows what the logic alone says, as before. So does `"off"`.
- So UT lists only what the story has reached; a later check is out of logic until its chapter.
  `logic_version` stays 1: the same room, with no position, is the same logic.

**The PopTracker pack** reads the export's `story` key (docs/ap/seed_format.md "Story awareness":
the points, the story events, the locations' points), written by `story.export` from the same
`overlay(world)`, and evaluates it as the reference does (`tools/ap_tracker_logic.py`
`evaluate(counts, story)`: `View.locations`, `View.later`); a section in logic by the items but later
in the story shows as Inspect (docs/ap/poptracker.md "Story awareness").

**Tests.** test/test_story.py (20 tests, one of them under SO2_SLOW_TESTS): the points (the stages'
first chapters in order, the Final Save Point last, `reached` of positions, at least the first point,
and of values that aren't one, chapter 0 among them); every story-mode location's
point (the Cave of Trials 24, Energy Nede and Fienal 13 or later, Expel's places 12 or before or 24,
story bosses in their chapter's stage, Lacour's chapter-60 gifts and the King's Room 24); no story
key outside story mode; the export's shape; a real generation has no story events. Per story mix
(story default, story party, story party rena; the room generated and filled, rebuilt in UT's way
with enforce `"default"`, UT's client steps mirrored): UT watches `so2_story_0_1` (`so2_story_2_7`
for team 2, slot 7); with no position, `null` or a bad value, sphere by sphere, UT's in-logic set is
the room's own, as with enforce `"off"` (no events, no key); at 27 story positions (chapter 2,
before the first one stored, each point, chapter 60 steps 3000 and 5000) on the first, a middle and the last sphere's
items, UT's in-logic set equals the reference evaluator's on the room's export, only grows, is the
plain set less `later`, and going back to chapter 3 shuts the later points again; with every item,
Expel's locations alone before the disc switch, Energy Nede's at chapter 36 and not at 35 step 9999,
nothing of the Cave of Trials before the Final Save Point. Open world and hub (enforce `"default"`
and `"on"`): no key, no events, no `story` in the export. `SO2_SLOW_TESTS=1` (TestStoryManySeeds):
20 rooms per story mix (60), UT equal to the reference at all 27 positions with the start's items and
with every item.

**The real Universal Tracker** (tools/ap_ut_check.py step 5, Tracker_v0.3.6, the four-slot room of
seed 2026, display variables unset): on the fresh room a client of the story slot (UTStory: character
items, crafts, killer moves, Skillsanity insanity; 578 locations in logic by the items) Sets
its `so2_story_{team}_{slot}` (from its `Connected`) as the port does, and each time UT headless lists exactly the reference's set, as does
TrackerCore in-process after `reconnect_found_entrances`:

| Position (chapter, step) | 3 | 9 | 12 | 20 | 36 | 41 | 58 | 60, 3000 | 60, 4000 |
|---|---|---|---|---|---|---|---|---|---|
| UT in logic | 142 | 204 | 227 | 319 | 426 | 450 | 532 | 532 | 578 |
| later in the story | 436 | 374 | 351 | 259 | 152 | 128 | 46 | 46 | 0 |

With host.yaml's `enforce_deferred_entrances: "off"` (at chapter 3) UT lists all 578 again; with the
key back to `null` the four spheres' points match the world's own as before (UT MATCHES THE WORLD:
every point of the four slots, the CLI and TrackerCore).

**The real generation is unchanged** but the export's new key (2026-10-27): the three release sets
(tools/ap_batch.sh, seeds 1-96 each) from the committed world against a frozen copy of the world
before it (HEAD 812f9f6 in a farm with real top-level scripts, scratchpad/storyaware/identity.py):
288 of 288 seeds identical, 480 .apso2 and 288 spoilers byte for byte, the 288 multidata equal once
`tracker.story` is taken out of the 93 story-mode slots (59, 13 and 21 in the three sets; 4.2 to
12.8 KB each), present in exactly those, and the checker's output the same (0 errors).

### Universal Tracker's map tab (tracker plan B6, 0.2.2)

UT can show a map tab: PopTracker-format maps with a square per location, coloured by UT's own logic
(docs in UT's `docs/map-integration.md`). so2.apworld's (`ut_map.py`, the world's `tracker_world` and
`settings: SO2Settings`) is **hybrid**, in UT's terms: the JSON is in the APWorld (`tracker/`: 66 area
maps, 2 world maps, 5 boards and 794 rooms in `maps/maps.json`; the squares in
`locations/locations.json` with every section named by its AP location name, so UT matches them
directly and needs no `poptracker_name_mapping`; UT's map menu in `layouts/maps.json`; and
`map_page_index`'s table `map_pages.json`), about 610 KB of JSON, 56 KB zipped: the .apworld grows
from about 220 KB to about 280 KB. The **pictures** come from a zip the player points UT at once:
the PopTracker pack's (`so2_shattered_stars_<version>.zip`, docs/ap/poptracker.md: placeholders) or
the port's own `so2_shattered_stars_ut.zip` (the player's render: docs/ap/tracker_maps.md "The
Universal Tracker zip"), which hold the same picture paths.
Why: the pictures are 4-6 MB (the rooms' placeholders alone 4.1 MB), past the APWorld's budget; the
pack is the one place the pictures live (placeholders, nothing from the disc); and the JSON in the
APWorld always matches the installed world's location names, whatever pack version the player has
(the image paths are tracker_maps.md's, stable).

- **Setup (players).** Install so2.apworld 0.2.2+ and UT as for tracking ("Trackers" above), and
  have one of the two zips: the port's `so2_shattered_stars_ut.zip` (written by
  `--render-tracker-maps` or setup's button beside the rendered maps; setup shows its path) or the
  pack zip (the `poptracker-v1.0.0` release, or the copy in PopTracker's `packs` folder). The first
  time UT connects to a Star Ocean slot it asks for it with a file dialog ("Select the Shattered
  Stars maps: so2_shattered_stars_ut.zip (your own, rendered by Shattered Stars) or the PopTracker
  pack so2_shattered_stars_*.zip"; host.yaml's comment and `description` name both too); the choice
  is kept in Archipelago's `host.yaml` as `so2_options: ut_pack_path`. Cancel: no map tab
  (`ut_pack_path: null`), and UT doesn't ask again until `/reload_pack`. The path can also be set in
  host.yaml by hand (empty: UT asks again on the next connect). The tab then follows the game: UT watches the port's `so2_map_{team}_{slot}`
  (`map_page_setting_key`): a field map opens its room's page, a world map its world map with a
  **player marker** at the party's x / z (`location_setting_key`, `location_icon_coords`, the marker
  `images/status/you.png` from the pack), a map with no page keeps the page. The menu lists the maps
  as the pack's tabs: Expel / Energy Nede > area group > area > its overview and its rooms, by
  "<map> <name>"; the boards (Shops, Item Creation, ...) too. `/load_map`, `/list_maps` and
  `/reload_pack` are UT's commands.
- **The player's own maps.** UT reads pictures only from the zip it is pointed at (`ap:zip:`), not
  from PopTracker's user-override folder. So the port's render also writes
  `so2_shattered_stars_ut.zip` (port 0.03.00; docs/ap/tracker_maps.md "The Universal Tracker zip"):
  the rendered images at the pack's paths, and the boards and the player marker drawn by the port
  (the boards at the pack's sizes and cells, from tools/poptracker/boards.json): every picture of
  the 867 pages and the marker. By default it lies beside the rendered folder,
  `<PopTracker>/user-override/so2_shattered_stars_ut.zip` (no pack folder, no override). The zip is
  for the player's own use (it holds their disc's pictures). UT splits `ap:zip:<zip>/<path>` at the
  first `zip/`: no folder on the zip's path may end in "zip" (on Windows too: the file dialog's
  path has `/`).
- **Doors.** UT draws squares only for AP locations, entrances and events of the world it rebuilt,
  and has no text or click actions on a map. The doors are neither (the world's regions are Menu,
  Expel, Energy Nede, Fienal and the Cave of Trials), so UT's map tab has no door markers, no door
  panel and no click-to-follow: the rooms are pages of the menu, and the port's key moves between
  them. With the entrance randomizer (Phase C), once each doorway is an entrance named by its door
  id (tracker_maps.md "Door ids"), UT can show them by name (`poptracker_entrance_mapping`, or door
  sections named as the entrances): UT colours a disconnected entrance red / green and a connected
  one grey, and reconnects deferred ones from `found_entrances_datastorage_key`.
- **Generation.** `tracker_world`, `settings` and `ut_map` are read only by UT (and AP's settings
  file): the real generation is unchanged. Regenerate `tracker/` with the pack
  (`tools/ap_poptracker.py --ut-out apworld/so2/tracker`); tools/ap_poptracker_test.py fails when it
  is out of date.
- **Tests.** tools/ap_poptracker_test.py `TestUT` (the committed JSON is this build's, under 1 MB,
  every section an AP location name and every location of the pack there, the pages and the menu,
  `map_page_index` / `location_icon_coords` / the keys / `settings_key`). tools/ap_poptracker_utmap.py
  runs **UT's own map loader** headless (`TrackerGameContext.load_pack` / `load_map` /
  `update_location_icon_coords` with a stand-in for the Kivy UI, in a UT copy whose host.yaml points
  `ut_pack_path` at the zip, no display and no D-Bus) on a generated open-world room rebuilt
  yaml-less by UT, and loads every page's picture and the marker through UT's own image loader
  (TrackerKivy's `ap:zip:` load, decoded by Kivy). `--zip` takes either zip: the pack's or the
  port's.
- **Results (2026-10-27, maptab).** `TestUT` 4/4 (pack tests 41/41). UT's loader (UT 0.3.6's
  TrackerClient, the repo's world and again the packaged so2.apworld from custom_worlds): 15/15
  each: the slot rebuilt yaml-less; the map key `so2_map_0_1`; 867 pages (794 rooms, 68 area and
  world maps, 5 boards), the JSON from the APWorld and the pictures from the pack zip; all 794 rooms
  named in UT's menu; `{"map": M}` opens room M for the 176 rooms with the slot's checks, with
  exactly the layout's room squares (UT's own name matching); every page's picture in the zip and
  every square's locations the slot's; no door squares; `{"map": 1, "x": 51200, "z": 18500}` opens
  the Expel world map with the marker at (948, 1250); a room hides the marker; a map with no page
  (the opening's map 7) and no value keep the page. The package: so2.apworld 281,228 bytes, the map
  tab 56 KB of it (ut_map.py and tracker/: 612 KB of JSON). **The real generation is unchanged**:
  the three release sets (tools/ap_batch.sh, seeds 1-96 each) from the committed world against a
  frozen copy of the world before it (HEAD c544a40, a farm with real top-level scripts;
  scratchpad/maptab/identity.py): ALL IDENTICAL, 288 of 288 seeds, 480 .apso2, 288 spoilers and 288
  multidata equal (no new slot-data key), the checker's output the same; world tests 882 passed,
  `AP_TEST_WORLDS=so2` test/general + world 935, SO2_SLOW_TESTS 885; tools/ap_ut_check.py UT MATCHES
  THE WORLD (59 matches, the story points, logic_version 2 refused).
- **Results (2026-10-28, utzip2: the port's zip).** `TestUT` 5/5 (+ `test_boards_for_the_port`; pack
  tests 43/43). UT's loader with the new image check (every page's picture and the marker through
  TrackerKivy's `ap:zip:` load, decoded by Kivy, at the layout's / the boards' sizes): **16/16** with
  the port's `so2_shattered_stars_ut.zip` (868 files, 134,897,214 bytes) and 16/16 with the pack's
  zip, each against the repo world and the packaged so2.apworld (281,557 bytes; only `ut_map.py`'s
  text changed in the world, nothing a seed reads). A negative control, the same zip under
  scratchpad/utzip/ (a folder ending in "zip"): the image check fails (IsADirectoryError: UT splits
  at the first `zip/`), the other 15 pass.

## Files

| File | Contents |
|---|---|
| `world.py` | `SO2World` (`plan_party`, `choose_party`, `choose_open_party`, `create_items`, `create_hub_items`, `fill_hook`), `SO2Web`, `seed_data`, `seed_goal`, `goal_doors`, `seed_version`, `generate_output`, `fill_slot_data` / `slot_options`; Universal Tracker's hooks (`ut_can_gen_without_yaml`, `interpret_slot_data`, `custom_ut_sort`, `reconnect_found_entrances`) |
| `ut.py` | Universal Tracker ("Trackers"): `LOGIC_VERSION`, the slot data -> option table `SLOT_OPTIONS`, `NO_EFFECT`, `restore` / `check_restored`, `refuse` (UT's `TrackerException`), `sort_key` |
| `hub.py` | hub mode: `choose` (the locations), `needed_keys`, `fienal_won`, `create_regions`, `set_rules`, `drop_rule`, the seed's `hub` / `bosses` / `doors` (`seed_hub`, `seed_bosses`, `seed_doors`); `OUT_OF_HUB` (the Portal of Sealing), `CONFIRMED_OPTIONAL` (waiting on hubchambers) |
| `items.py`, `locations.py`, `regions.py`, `rules.py`, `options.py` | as named |
| `logic.py` | rules as data ("Rules as data"): the Rule Builder builders `need`, `ways`, `any_of`, `both`; `open_by_default`; `RESOLVED_KINDS`, `as_rule` |
| `tracker_export.py` | the slot data's `tracker` key ("The logic export"): `export(world)`, `FORMAT` |
| `ut_map.py`, `tracker/` | UT's map tab ("Universal Tracker's map tab"): `tracker_world`, `SO2Settings` (`ut_pack_path`), `map_page_index`, `location_icon_coords`; the generated JSON (do not edit: `tools/ap_poptracker.py --ut-out apworld/so2/tracker`) |
| `story.py` | story awareness ("Story awareness"): the points (`POINTS`, `FINAL_SAVE_POINT`), `reached`, `location_stage`, `overlay`, the export's `story` key (`export`), Universal Tracker's deferred story events (`prepare`, `reconnect`) |
| `data.py` | generated tables (do not edit) |
| `crafts.py`, `moves.py` | craftsanity's 553 results with their specialties and makers; the 69 killer moves with their learn levels and divisors (generated by tools/ap_make_crafts.py and tools/ap_make_moves.py from docs/ap/data/; do not edit) |
| `skills.py`, `skillsanity.py` | skillsanity's tables (the guild sets, the skills, the starting skills, the specialties, the learn tables with each member's first move, the other learn points; generated by tools/ap_make_skills.py from the disc; do not edit) and its locations, items, logic and shop prices |
| `patches.py`, `doors.py` | placeholder patches; the phase-4 door validator (world agent), used by the tests |
| `archipelago.json`, `docs/` | manifest (`world_version`), setup and game pages |
| `test/` | WorldTestBase tests (`test_world.py`, `test_doors.py`; 0.2.0's in `test_v2.py`, hub mode's in `test_hub.py`, Universal Tracker's in `test_ut.py`, the rules as data in `test_logic.py`, the logic export and its reference evaluator in `test_tracker.py`, story awareness in `test_story.py`) |

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

Results (2026-10-27, 0.2.2: story awareness; the storyaware handoff log; "Story awareness" above):
- The world's tests: **882 passed**, 3 skipped (67988 subtests), 10.9 s at `-n 6`: test_story.py's
  19 (UT's deferred story events equal to the reference's `evaluate(counts, story)` at 27 positions,
  three story mixes) and test_tracker.py's shape test allowing `story`. `SO2_SLOW_TESTS=1`: **885
  passed** (68692 subtests, 35 s), TestStoryManySeeds' 60 rooms among them. `AP_TEST_WORLDS=so2 ...
  pytest test/general worlds/so2`: **935 passed**, 3 skipped (73299 subtests).
- Identity: the three release sets, seeds 1-96, from a frozen copy of HEAD 812f9f6's world and from
  the final code (md5s checked): **all 288 seeds identical but the new key**, `tracker.story` in the
  93 story-mode slots (scratchpad/storyaware/identity.py; "The real generation is unchanged" above).
- The PopTracker pack (tools/ap_poptracker_test.py): Lua equal to the reference with story positions
  in the three story rooms, never Inspect in the 13 others; the real PopTracker 0.36.0 smoke 37/37,
  its story room following a story key Set as the port does (docs/ap/poptracker.md "Smoke test").
- The real UT (tools/ap_ut_check.py, Tracker_v0.3.6, the packaged so2.apworld built from the final
  code, 223,553 bytes, world version 0.2.1 unchanged): **UT MATCHES THE WORLD**, 59 comparisons: the
  four slots' five points (CLI and TrackerCore), the logic_version 2 room refused, and the story
  slot's nine story positions (CLI and TrackerCore) and enforce "off" ("Story awareness" above).

Results (2026-10-27, 0.2.2: the logic export and 0.2.0 Skillsanity rooms; the logicexport handoff log;
"The logic export" and "Older rooms" above):
- The world's tests: **863 passed** (65878 subtests; was 788), 14.8 s at `-n 6`: test_tracker.py's
  16 mix classes (the reference evaluator is the world's own logic at every sphere and on 10 more
  item states each; the slowest 0.5 s) and its export, evaluator and size tests.
  `SO2_SLOW_TESTS=1`: **865 passed** (66555 subtests): TestTrackerManySeeds' **320 rooms** (16 mixes
  x 20 seeds) and TestUTManySeeds' 320, 55 s. `AP_TEST_WORLDS=so2 ... pytest test/general
  worlds/so2`: **916 passed** (71197 subtests).
- Identity: the three release sets, seeds 1-96, before (rulebuilder's after batch, the same world
  code as HEAD before this change, md5s checked) and after: **all 288 seeds identical but the new
  key**: 480 .apso2 and 288 spoilers byte for byte, the checker's output the same, every multidata
  equal once each Star Ocean slot's `tracker` (a format-1 export; 4.6 to 31.8 KB in these sets) is
  taken out (scratchpad/logicexport/identity.py).
- The PopTracker pack's evaluator against this reference (tools/ap_poptracker_test.py): 16 rooms x
  246 item states, 243,885 section evaluations equal; in the real PopTracker 0.36.0
  (tools/ap_poptracker_smoke.py) its reachable sections were the world's own at three points
  (115, 99, 40 in logic).
- 0.2.0 rooms ("Older rooms"): 85 rooms, 51 equal, 29 subsets (Skillsanity), 5 refused (hell
  seeds), none listing more than the room's logic; the user's own room loads with the warning, its
  1,627 locations the room's.
- The real UT (tools/ap_ut_check.py, Tracker_v0.3.6, the packaged so2.apworld, 212,635 bytes, world
  version 0.2.1 unchanged): **all 20 points match** (CLI and TrackerCore, 40 comparisons), the
  logic_version 2 room refused.

Results (2026-10-27, 0.2.2: rules as data; the rulebuilder handoff log; "Rules as data" above):
- The world's tests: **788 passed** (65886 subtests; was 783): test_logic.py's 5 (11 worlds, every
  location, event, entrance and the goal; the round trips; the source scan), 12.7 s at `-n 6`.
  `SO2_SLOW_TESTS=1`: 789 passed, TestUTManySeeds' **320 rooms matching in every sphere**.
  `AP_TEST_WORLDS=so2 ... pytest test/general worlds/so2`: 841 passed (71162 subtests, as before).
  AP's general tests: 379 passed (117757-117760 subtests from run to run: other worlds' random
  options).
- Each way of breaking the test (a killer-move lambda, `open_by_default` taken out, a rule set
  without `set_rule`) fails it.
- The same truth values: each rule of the lambda world (HEAD 43b0d20's apworld/so2 frozen in a
  separate Archipelago copy) and of the Rule Builder world on the same states. 21 mixes (test_ut's
  16 and 5 more: plain open world and hub, a story hell seed, open world with the Iseria Queen and
  expel_progression off, hub with goal 1 and the fields) x seeds 1-3, every location, event and
  entrance and the goal, on the empty state, the full one, each item or event alone, all but each,
  and 400 random states: the same spots and all **93,083,978 truth values the same**
  (scratchpad/rulebuilder/truth.py).
- Identity: the three release sets, seeds 1-96, before (the frozen lambda world) and after: **all
  288 seeds identical**: 480 .apso2 and 288 spoilers byte for byte, every multidata equal in full
  (slot data too: no key added), the checker's output the same (scratchpad/rulebuilder/identity.py).
- Generation time, the three sets' seeds 1-24 run alternately with the lambda world, the Rule Builder
  world and a `CachedRuleBuilderWorld` variant (71 seeds each): **no slowdown**. A Generate.py run
  2.30 / 2.28 / 2.24 s median (totals 178.1 / 170.4 / 176.1 s); Main's own time 19.09 / 18.15 /
  18.93 s in total. Caching measured no faster, so the world stays a plain `World`. (The release
  sets' full batches: 665.5 s before, 587.1 s after, on a busy machine.)
- The real UT (tools/ap_ut_check.py, Tracker_v0.3.6, the packaged so2.apworld): **all 20 points
  match**, as with 0.2.2's first piece (59 to 643 locations, the TrackerCore tab in our order, the
  `logic_version` 2 room refused). UT's `/explain` through its console (`--nogui`, commands on
  stdin; scratchpad/rulebuilder/ut_explain.py): 64 locations and events of the four slots, each
  printed as its rule (sample in "Trackers"). (A fresh UT copy needs its `universal_tracker`
  `player_files_path` folder to exist, or UT opens a folder dialog: ut_explain.py's copy has an
  empty `Players`.)

Results (2026-10-27, 0.2.2: Universal Tracker; the utworld handoff log; "Trackers" above):
- The world's tests: **783 passed** (65867 subtests; was 699): test_ut.py's 84 (16 mix classes with
  WorldTestBase's defaults, TestUTRoom, TestUTCoverage, TestUTSort), 4.1 s at `-n 6`, slowest UT
  test 0.43 s. `SO2_SLOW_TESTS=1`: TestUTManySeeds, 16 mixes x 20 room seeds, **320 rooms, every
  sphere matching** (21 s). AP's general tests: 379 passed (117763 subtests).
  `AP_TEST_WORLDS=so2 ... pytest test/general worlds/so2`: 836 passed (71162).
- Each part of the restore taken out in turn (the open party, the story party, the hub town,
  starting_character's reverse map, exclude_locations, bench's absent value, expel_teleporter,
  the round-trip check, the logic version, the 0.2.0 rule) fails some test
  (scratchpad/utworld/mutate.py: 10 of 10 caught).
- Identity: the three release sets, seeds 1-96, before (HEAD 6d2c518's apworld/so2 frozen in a
  separate Archipelago copy) and after: **all 288 seeds identical**: 480 .apso2 and 288 spoilers
  byte for byte, the checker's output the same, and each multidata equal but for every Star Ocean
  slot's slot data gaining exactly `logic_version: 1` and `exclude_locations: []`
  (scratchpad/utworld/identity.py; a symlinked Generate.py imports the shared install's world,
  Python resolving a script's directory, so the frozen copy has real top-level scripts).
- 0.2.0 rooms ("Older rooms" above): 17 mixes x 5 seeds generated with 0.2.0's world; 40 rooms
  match in every sphere, 45 (Skillsanity or a hell seed) are refused; none tracks wrong. A
  player's own 0.2.0 hub room (Skillsanity insanity) is refused with the 0.2.0 message; without
  that rule its slot data restores cleanly.
- The real UT (tools/ap_ut_check.py, Tracker_v0.3.6 on the scratch Archipelago 0.6.8, the packaged
  so2.apworld): four Star Ocean slots (story, open world without the bench, hub, a player's own
  rich hub YAML), fresh and after each of the room's first 4 spheres: **UT's in-logic count and list
  are the world's own at all 20 points** (59 to 643 locations), the same through TrackerCore
  in-process with its tab in our order, each slot rebuilt yaml-less with its own party, and a
  `logic_version` 2 room refused through UT's TrackerException with our message.

Results (2026-10-27, 0.2.1: the bench; the benchworld handoff log):
- The world's tests: **699 passed** (50730 subtests; was 634; `python -m unittest`: 699, OK).
  test_party.py's `BenchChecks` (party_size 12, no healer guarantee; the pool every allowed
  member in party order, drawn without touching the world's RNG; fighter_status "item" for each,
  "sure" for the heroes and the start; the select every member not excluded; version 4, slot data
  `bench`) over 6 mixes: the default on, any start, healers excluded, a named start, received
  members (killer moves any, special_only, craftsanity: all 133 killer-move levels and all crafts
  in the world, and each of Claude's game's 11 pool members' killer-move and learn levels
  depending on their item alone, `assertAccessDependency`), and ignored in story mode / without
  character_items; the 5 earlier party v2 mixes pinned `bench: false`. test_hub.py's HubChecks
  bench-aware (version 4, every member not excluded) with a no-bench mix (version 3, 8 places);
  test_v2.py open-world killer moves with and without the bench. AP's general tests: 379 passed
  (117745 subtests). `AP_TEST_WORLDS=so2 ... pytest test/general worlds/so2`: 752 passed (56003).
- Identity: the three release sets with `bench: "false"` appended to each SO2 YAML
  (scratchpad/benchworld/players_off; `bench` in a YAML as a plain value draws no random number,
  and an option left out takes its default without one), seeds 1-96, against
  scratchpad/hellseed/batch (made at b2d7a75, whose apworld/so2 is HEAD's before this change):
  **all 288 seeds identical** (288 rc's; 480 .apso2 byte for byte; 288 spoilers but for their new
  "Bench: No" line, one per SO2 player; scratchpad/benchworld/identity.py, identity.txt); the
  checker's output the same but its new `('bench', False)` count.
- The release workflow with the bench on by default (scratchpad/benchworld/on): story_open, hub,
  solo_noexpel **96/96 each, `ap_batch_check.py` 0 errors**; bench seeds (version 4) 60 of 192 /
  168 of 192 / 49 of 96 (every open-world player with character_items and every hub player). The
  checker learned the bench: `options.bench` only `true`, only where it applies, exactly when the
  spoiler's "Bench:" line is Yes there; version 4 (hub and open world); 12 places, the pool every
  allowed member in party order; the select every member not excluded.
- Solo fills with the bench (killer0's fillrate3.py, 100 seeds each; scratchpad/benchworld/fill):
  open world with character_items; + killer moves any, special_only, Claude's game (11
  progression characters a seed, 133 killer-move locations, 60 progression moves); + expel off,
  boss checks off, killer moves any (7 progression characters); hub; hub + killer moves any,
  special_only, Rena's game (11): **0/100 FillError each, every seed accessible and beatable**.
- End to end on the merged port (main 319f5ba's engine, engine/build-benchworld; headless;
  scratchpad/benchworld/e2e/run.sh): a generated bench seed (open world, character_items, Claude's
  game, starting Ashton, `start_inventory` the other 10 members): New Game puts Ashton in slot 0
  and Claude in slot 1, six members fill the 8 and Opera, Ernest, Noel and Chisato join the bench
  (places 8-11); the save keeps them (sidecar version 3); after a load, Party Order holds Leon
  (slot 7) across R1's flip to the bench and swaps him with Opera (place 8), refuses Claude to the
  bench, and the save and a second load keep `bench: Leon, Ernest, Noel, Chisato`. The packaged
  world's open-world (bench and `bench: false`) and hub seeds load in the same build.

Results (2026-10-27, 0.2.1: the hell seed; the hellseed handoff log):
- The world's tests: **634 passed** (44972 subtests; was 537): `test_hell.py` (the table above),
  test_skillsanity.py's helpers hell-aware (`may_hold`, `learn_need`) and its encounter_rate 0 mix
  split (skills_only: not a hell seed; insanity: a hell seed), test_v2.py's
  `TestKillerNoEncounters` a hell-seed test, and `check_seed` (every mix) asserting the patch line
  and `hell_seed` exactly on hell seeds. The reproducer: with the hell rule taken out,
  `TestHellSeed75` and its Claude neighbour fail test_fill (FillError, 13 progression items more
  than locations). Each part of the design taken out in turn (the killer / learn / guild arena
  rules, stage 16 in the arena's need, arena_way's stage floor, the patch line, `hell_seed`, the
  rule's learn points, `trained`) fails some test (scratchpad/hellseed/dbg/mutate.py); dropping
  an Expel guild's own early way is equivalent today (with the teleporter that way equals the
  teleporter's, without it Expel guilds are never a check's best class) and stays as a safeguard.
  Slowest test 0.56 s at `-n 4`; `python -m unittest`: 634, OK. AP's general tests: 379 passed
  (117745 subtests). `AP_TEST_WORLDS=so2 ... pytest test/general worlds/so2`: 687 passed (50303).
- Solo fills (scratchpad/hellseed/fill: killer0's fillrate3.py, with test_fill's sphere and
  beatable check), 100 seeds each: seed 75's options with `encounter_rate: 0`, and with story_route
  claude, and with killer_move_progression any: **0/100 FillError each**, every filled seed
  accessible and beatable (133 killer-move locations and 60 progression move items a seed).
- The release workflow, seeds 1-96 (scratchpad/hellseed/batch): story_open, hub, solo_noexpel
  96/96 each, `ap_batch_check.py` 0 errors (its output identical to skilllogic's); no seed rolls
  `encounter_rate` 0, and **all 288 seeds (768 .apso2 and spoiler files) are byte-identical** to
  skilllogic's batches. The checker learned the hell seed (`hell_seed`, `trained`, `check_hell`:
  the patch line and `hell_seed` exactly on hell seeds, worked out from the options; each made-up
  error refused). An extra solo set with `encounter_rate: 0` (solo_noexpel's YAML with
  expel_progression and killer_move_checks random; 55 hell seeds, 9 ordinary) generated 64/64
  with 0 checker errors; progression at killer-move levels / learn levels / guild checks 166 /
  488 / 306.

**The tests follow the guide** (2026-10-26, the aptests handoff log; the guide re-read live, the
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
| test_party.py | the story's party rules (any / claude / rena), party v2 with `bench: false` (open members, any start, healers excluded, healer guarantee, a named start), the starting character without items / in story / the hero; 0.2.1's bench (`BenchChecks`: the default on, any start's select, healers excluded, a named start, received members with killer moves / learn levels / crafts, ignored in story mode and without character_items) | every pool member's killer-move and learn levels -> their character item (the bench: all 11 members of Claude's game); `PartyChecks.party_size()` / `healer_guaranteed()`, which `BenchChecks` overrides (12, none) |
| test_hub.py | hub default, Claude at Arlia (fields, cave, killer moves), Rena all bosses (Iseria Queen, forced drops, specials), any lean, fields without checks (all with the bench, the default: version 4, every member not excluded), no bench (version 3, 8 places); a story / open-world seed without hub fields | every dungeon key -> its dungeon's locations, the drops met only there, the Fienal gate; each key of the Fienal requirement -> the gate, all of them -> the goal (fields_and_berle, fields, all_bosses) |
| test_v2.py | drops (allow, force, never, no encounters, open world, no keys in drops), craftsanity (story, open party), no-fail only, the four goals, killer moves (story, hero, any with items, open world any with and without the bench, no encounters) | character items -> their killer-move levels (killer_move_progression any) |
| test_skillsanity.py | none, skills_only with crafts, special_only in open world, insanity in hub with shops, insanity with the Iseria Queen; 0.2.1: guilds in open world (with and without expel_progression) and in story mode (both), learn levels in open world, with character items and in hub mode, encounter_rate 0 (skills_only: not a hell seed; insanity: a hell seed), solo_noexpel seed 75's mix | every skill / move item the logic needs -> the crafts, Machinery learn points, the Iseria Queen and killer levels whose every way needs it; each Musical Talent skill -> the Queen, both -> the goal; 0.2.1 (listed): Knowledge 1 -> the Cross / Linga / Central City Pass (not Clik's), the Nede guilds -> the Central City / Armlock Pass, the dear sets -> the Hoffmann Pass (swept), the early / late learn levels -> the Eluria Tower / Mihne Pass (swept), each pool member -> their learn levels; every mix: what each guild check / learn point may hold (shop slots: nothing, our own skill / move items neither), each learn level's exact needs |
| test_hell.py | 0.2.1's hell seed: the rule's truth table and the fee line (`unittest.TestCase`); seed 75's mix with encounter_rate 0 (the reproducer: WorldTestBase's test_fill) and its Claude neighbour; open world with guilds (teleporter on / off); story mode and hub mode with killer moves and insanity; encounter_rate 1 (killer moves; insanity): ordinary; the patch line and `hell_seed` over 8 mixes | open world: every killer-move level, learn level and guild check -> the Red Crystal Pass (stage 16, swept), none needs the Mihne Pass (the Final Save Point); without the teleporter Knowledge 1 -> the Central City Pass (an Expel guild's early way dropped), with it not; story mode: each from the disc switch alone (every item, no event: none); hub mode: each from the start (every item, no event); what each may hold |
| test_difficulty.py | earth, galaxy, universe (open world, hub), any | (no logic change: the same locations as on Earth) |
| test_doors.py | `unittest.TestCase` | the door validator on a made-up table |

A fixed world bug stays in the suite as its reproducer: `TestOpenWorldNoExpelProgression` (open
world, expel_progression off, the teleporter on, a solo world) fills in WorldTestBase's
test_fill, test_voyage_gate_placements and test_seed_doors. Before 0.2.1 the North City Pass had
no legal spot there and every solo fill raised FillError (the aptests handoff log "World bug");
the three were `@unittest.expectedFailure` until the fix (0.2.1: the North City and Noel's House
passes at the start, test_keys_and_start; the ncpfix handoff log).

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

Results (2026-10-23, 0.1.1; the apareas handoff log):
- The world's tests: 98 passed (7986 subtests; new: test_ship_landings in the three open-world
  classes). AP's general tests: 379 passed (116984 subtests).
- 96 seeds (the phase-4 batch's players, seeds 1-96): 192 `.apso2`, 189 option combinations,
  all generate and fill, `check.py` finds no error, `validate_doors` passes on all 192; the 16
  seeds whose players are both in story mode are byte-identical to 0.1.0's. 0.1.0 put an
  unreachable landing key (the Hilton Pass in Rosalia's Room, the Map 231 Pass in Hoffmann
  Ruins) in 44 of these 114 open-world slots; 0.1.1 in none.
- `tools/ap_e2e_so2.sh`: 25 / 25, twice.

Results (2026-10-23, 0.1.1 with the El voyage gate; the elgate handoff log):
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

Results (2026-10-25, 0.2.0; the apworld2 handoff log):
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

Results (2026-10-25, gift sites never hold progression; the apworld2 handoff log "Session 4"):
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

Results (2026-10-25, hub mode; the hubworld handoff log):
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

Results (2026-10-25, `starting_character: any` as New Game's pick; the charselect handoff log):
- The world's tests: 318 passed; AP's general tests: 379 passed.
- Both batches regenerated (players -> scratchpad/apworld2/batch/full_cs, players_hub -> hub_cs,
  seeds 1-96): 96/96 exit 0 each, `check.py` 0 errors on 192 + 192 files (it now checks the
  list: the heroes and the pool, at most 8, nobody excluded; under the story's party rules
  everyone not excluded). Against the batches before (`full_hubask`, `hub_ask`;
  `cs_identity.py`): every seed with no `any` slot byte-identical (142 / 138 files, story mode
  among them); the 25 / 30 `any` slots differ as meant (no random start; the old start is in the
  new list in every one, and where the party rules are open the pool has the start's place: one
  member more), and with them the other slot of the same multiworld (the shared fill).

Results (2026-10-25, the hub proof; the hubproof handoff log):
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
- The late proof's driver fix (2026-10-25; the elgate handoff log "Proof gap explained"): each
  entry has its own reload budget, and a run is resumed at the first entry the sweep skipped
  for reloads ("end loop", event_sweep.cpp kMaxReloads) instead of past it. Rerun from the gate
  build's node logs (only the nodes with reload loops rerun): both games 278 proven, the same set
  as docs/ap/late_proof.json, the same checks per map, maps and edges (the Cave of Trials L1 / L11
  bosses back).

Results (2026-10-25, `difficulty`; the difficulty handoff log):
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

Results (2026-10-25, skillsanity; the skillsanity handoff log):
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

Results (2026-10-26, 0.2.1: the North City and Noel's House passes at the start unless the
teleporter and expel_progression are both on; the ncpfix handoff log):
- The world's tests: 449 passed (30513 subtests; was 430 passed, 3 xfailed: the three
  expectedFailures gone, the new mix `TestOpenWorldNoExpelProgressionNoBosses`); `python -m
  unittest` agrees (449, OK). The slowest test 1.13 s (TestInsanityIseriaQueen::test_skill_items,
  unchanged by this fix) at `-n 8` with the machine's load at ~25. AP's general tests: 379 passed
  (117755 subtests). `AP_TEST_WORLDS=so2 ... pytest test/general worlds/so2`: 502 passed (35824
  subtests; was 483 passed, 3 xfailed).
- Solo fills (open world, expel_progression off, the rest default; scratchpad/aptests/fillrate.py
  and ncp_spots.py's copies in scratchpad/ncpfix): 0/100 FillError in each of 24 setups
  (story_route any / claude / rena x teleporter on / off x boss_checks on / off x killer-move
  progression off / on), 2400 fills; 0.2.0 failed 30/30 with the teleporter on and every one with
  boss_checks off. The tightest key is now the Red Crystal Pass: 4 legal spots (3 with
  boss_checks off; 13 / 12 with killer-move progression), then the Salva Pass's 7.
- The release workflow, seeds 1-96: story_open and hub 96/96 each, `ap_batch_check.py` 0 errors
  on 192 + 192 files; at 0.2.0's version string, 111 of the 192 seeds byte-identical to the
  aptests baseline (every .apso2 and spoiler) and the 81 others exactly those with an open-world
  SO2 player without both options on (96 such slots), each changed only by that player's added
  start keys and the fill around them: options, locations and doors identical, that player's
  keys / characters / skills / moves the base's minus the new start keys, only filler-kind items
  otherwise moved, every other player's items the same. The new `solo_noexpel` set: 95/96, the
  checker 0 errors on 95 files (0.2.0 + the North City Pass alone: 85/96, the 11 failures
  open world with boss_checks off). Seed 75 fails another way, not the start keys': open world,
  expel_progression, teleporter and boss_checks off, skillsanity special_only, killer moves for
  the hero; 91 progression items (60 move items) and only 63 locations that may hold a move item
  ("Not enough locations for progression items"; open to the user, the ncpfix handoff log).

Results (2026-10-26, the tests to Archipelago's testing guide; the aptests handoff log; tests and
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

Results (2026-10-27, 0.2.1: skillsanity progression at guild checks and learn levels;
the skilllogic handoff log):
- The world's tests: **537 passed** (37687 subtests; was 449): `test_skillsanity.py`'s old "no
  progression" test reversed into `test_what_skillsanity_locations_hold` and
  `test_learn_level_needs` for every mix, and 9 new mixes (the table above). Each part of the
  design taken out in turn (the Fol tier, Noel's stage, the learn characters, a guild's ways
  through every place, the learn level's character item, the machines) fails some test
  (scratchpad/skilllogic/dbg/mutate.py). Slowest test 0.86 s at `-n 4`; `python -m unittest`:
  537, OK. AP's general tests: 379 passed (117750 subtests). `AP_TEST_WORLDS=so2 ... pytest
  test/general worlds/so2`: 590 passed (42996 subtests).
- Solo fills (scratchpad/skilllogic/fill: ncpfix's fillrate2.py and s75/capacity.py), 100 seeds
  each: **0/100 FillError** for solo_noexpel seed 75's options (room for a move item 114, was 63)
  and each of 14 neighbours: skillsanity insanity / skills_only, story_route claude / any, the
  teleporter on, boss_checks on, killer_move_progression any, starting_character hero,
  enemy_dropsanity first_drop, character_items, expel_progression on, story mode, hub mode,
  insanity with character_items, the cave and craftsanity. With `encounter_rate: 0` added, that
  mix still fails (91 of 91 run): no battles, so nothing new holds progression, and its
  killer-move levels' moves stay progression (as in 0.2.1 before; for the user).
- The release workflow, seeds 1-96 (scratchpad/skilllogic/batch): story_open, hub and
  solo_noexpel **96/96 each** (solo_noexpel's seed 75 too), `ap_batch_check.py` 0 errors on 192 +
  192 + 96 files; progression at guild checks / learn points 1046 / 1291, 1028 / 1625, 410 / 871.
  The checker now judges them on its own (`sk_may_hold`; a gate key at a guild check only where a
  guild passes the voyage proof, never at a learn point) and was checked against made-up
  placements (encounter_rate 0, the machines, a member the logic can't count on: each refused).
  Against the batches before (scratchpad/ncpfix/v021, 0.2.1's string too): every seed with no
  skillsanity player byte-identical (story_open 6, hub 3, solo_noexpel 20), every changed seed
  (90, 93, 76) with one (identity.py: 0 unexpected); 2 solo seeds with skillsanity unchanged
  (story mode, nothing progression to move).

## Changes

**0.2.2** (2026-10-27, not released yet; the utworld handoff log; docs/release_notes/0.2.2.md is
the draft). Tracker support, first piece (the tracker plan's B1): Universal Tracker, yaml-less
("Trackers" above). No new option, item or location; the same seeds as 0.2.1 byte for byte (the
.apso2 and the spoiler); the slot data gains `logic_version` (1), `exclude_locations` and `tracker`.
- *Rules as data* (tracker plan B2; the rulebuilder handoff log; "Rules as data" above): every
  access rule is a Rule Builder rule (logic.py), so Universal Tracker's `/explain` works. The same
  logic (every rule's truth table the same, `logic_version` still 1) and the same seeds and slot
  data byte for byte.
- *The logic export* (tracker plan B3; the logicexport handoff log; "The logic export" above): the
  slot data gains `tracker`, the world's resolved logic as data (format 1, 4 to 35 KB), which the
  PopTracker pack evaluates (pack 1.0.0); `tools/ap_tracker_logic.py` is its reference evaluator.
  The same seeds byte for byte; `logic_version` still 1.
- *0.2.0 Skillsanity rooms* in Universal Tracker: tracked with a logged warning instead of refused
  (the user's decision; measured conservative: "Older rooms"); 0.2.0 hell seeds still refused.

**0.2.1** (2026-10-26 / 27, not released yet; the ncpfix, skilllogic,
hellseed and benchworld handoff logs, the user's decisions). No new item or location; one new option,
`bench` (on by default), and two new seed fields, `hell_seed` (display only) and `bench` (with
seed version 4). The port's 0.02.50 plays every 0.2.1 seed without the bench (skilllogic2's
headless proofs: a key and a character given from a guild check and from learn points;
the skilllogic handoff log "Step 3"; hellseed's: the arena's fee waived by the seed's patch line,
the hellseed handoff log); a bench seed (version 4) needs 0.02.51, which also adds the hell
seed's warning in setup and the log.
- *Fixed: open world with `expel_progression` or the teleporter off couldn't fill solo*
  (the aptests handoff log "World bug"). The first Energy Nede stages need the North City and
  Noel's House Passes, and before them Energy Nede has no location of this world that may hold
  them but the Synard boss; with either option off Expel holds only our Expel-story keys. So in
  a world with no other game's locations, 0.2.0 failed every fill with the teleporter on and
  expel_progression off (the North City Pass had no legal spot), and every one with boss_checks
  off and either option off (the Noel's House Pass's only spot was the Synard boss; the
  release workflow's new solo set: 11 of 96 seeds). The user (2026-10-26): "North City Pass fix
  is the way to go for Expel prog off games", then "Follow the North City Pass for Noels House
  pass". Now both passes are starting keys unless the teleporter and expel_progression are both
  on (items.NEDE_START_KEYS; 0.2.0 gave the North City Pass with the teleporter off only).
  Multiworlds with another game generated before, its locations taking the passes.
- *Skillsanity: guild checks and learn levels may hold progression* (the user, 2026-10-27; they
  never could in 0.2.0; "Skillsanity", "Progression at guild checks and learn levels"). A guild
  check through any of its set's guilds that may hold progression, the 2700-4500 Fol sets from
  stage 9 (story mode: the disc switch; hub: Fienal open); a learn-table level of a member the
  logic counts on as a killer-move level (a member who joins by their item needs it, which is
  then progression); none with encounter_rate 0; the machines, the cave's learn chests, Float
  and every shop slot still none. This also fills the release workflow's solo_noexpel seed 75
  (open world, Rena, expel_progression / boss_checks / drops off, special_only, killer moves for
  the hero), which no 0.2.x before could: 60 move items progression and only 63 locations that
  could hold one (now 114).
- *The hell seed* (the user, 2026-10-27; "The hell seed"): `encounter_rate` 0 with
  `killer_move_checks` or skillsanity's learn points (`special_only`, `insanity`). The Fun City
  arena's 2000-Fol fee is waived by a seed patch line, `hell_seed: true` tells the port to warn the
  player, and killer-move levels, learn levels and guild checks count from the arena (story mode
  the disc switch, open world stage 16 with the Fun City Pass, hub mode the start) and may hold
  progression as with battles. Before, with `encounter_rate` 0, killer-move levels were all
  EXCLUDED and nothing new could hold progression, so seed 75's mix with `encounter_rate: 0`
  failed every solo fill (0.2.1's known limitation until then); now 0 of 100 fail, and two
  neighbours likewise. No seed of the release workflow rolls `encounter_rate` 0: all 288 are
  byte-identical to the batches before (Testing, "Results ... the hell seed").
- *The bench* (the user, 2026-10-26 / 27: B7 "a `bench` option, on by default", "Bench needs to
  be rolled into the 0.02.51 + 0.2.1 release"; "Party v2", "The bench"): open world with
  character_items and hub mode get 12 places (the port's bench of 4 after the game's 8), so the
  pool is every member not excluded, with no random draw and no healer guarantee, and
  `starting_character: any` offers every member not excluded. The logic is unchanged (every pool
  member is "item"); the members the 8 places used to leave out bring their killer-move levels,
  crafts and learn levels. The seed says `bench: true` and is version 4 (a 0.02.50 port refuses
  it). Story mode and open world without character_items ignore it. With `bench: false` every
  seed is byte for byte the one before the option (all 288 of the release workflow's seeds,
  pinned; spoilers but their "Bench: No" line).
- Seeds where no open-world player has either option off and no player has skillsanity on are
  0.2.0's but for `apworld_version` (and, with the bench on by default, every open-world player
  with character_items and every hub player is now a bench player, whose pool, locations and
  seed version change; the comparisons below were made before the option or with it off): the passes' change alone kept 111 of the 192 batch seeds
  (Testing, "Results ... 0.2.1"); in the others that player starts with the passes, or a
  skillsanity player's guild checks and learn levels take progression, and the fill places the
  multiworld otherwise (skilllogic: every changed seed has a skillsanity player; Testing,
  "Results ... skillsanity progression").

**0.2.0** (2026-10-25, released with the port's 0.02.50 on 2026-10-26; the apv2 handoff log, the user's request and decisions;
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
(the cavefix handoff log).
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
  the cavefix handoff log). 0.1.x's "Cave of Trials Level 1: Portrait F / G / J chest" (ids
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
- *Fixed: two ship landings missing from the logic* (the apareas handoff log). The Lacour
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
- *Fixed: the teleporter does not reach Expel's islands* (the apareas handoff log). The
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
- *Fixed: keys left behind by the El voyage* (the elgate handoff log; "The El voyage and the
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
