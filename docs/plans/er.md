# Plan: the entrance randomizer (so2.apworld 0.3.0)

Written 2026-10-28 by agent "erplan" for the user's review, from docs/plans/next.md "Phase C" and the
user's decisions of 2026-10-28. Nothing here is built yet. Implementing agents work from this file
and keep their own logs in `docs/handoff/<name>.md`; section 11 holds what still needs the user.

## 0. Summary

**What a player gets.** With `entrance_randomizer` on, the places of Star Ocean 2 are reconnected
for each seed. At the first level, `world_map`, walking into Arlia's spot on Expel's world map might
take you into Cross Cave, and leaving Cross Cave puts you back on the world map at Arlia's spot
(coupled, the default). At `dungeons` the doors inside each dungeon also lead to other rooms of that
dungeon; at `all_doorways` the doors inside towns do too. Story mode refuses it (open world and hub
mode only). The trackers follow along without spoiling: a door reads "A -> ?" until the player has
been through it, and Universal Tracker and the PopTracker pack count a place as reachable only once
the player has found a way into it.

**Release.** so2.apworld 0.3.0 with all three levels, port **0.04.00** (seed format 5: older ports
refuse entrance-randomized seeds) and PopTracker pack **1.1.0**, released together. Built and
proven level by level: `world_map` (hub mode first, then open world), then `dungeons`, then
`all_doorways`.

| Level | What moves | Shuffled pairs (strict pool, section 3.2) |
|---|---|---|
| `off` (default) | nothing: the game's own layout | 0 |
| `world_map` | the world-map entrances of towns and dungeons, among the same world's | hub 38 (Expel 23, Nede 15); open world 37 (22 + 15), shuffled within each continent or island |
| `dungeons` | also the doors inside each dungeon, among that dungeon's own | + 137 (Expel 115, Nede 22) |
| `all_doorways` | also the doors inside each town, among that town's own | + 233 (Expel 155, Nede 78) |

**The central idea.** A door only trades places with doors the game reaches under exactly the
same conditions (a **class**: the same world, the same continent or island, the same part of a
dungeon that is always open). Then every proof the world already relies on (the late proof, the
teleporter and voyage proofs, the hub proof) stays true in every seed, the logic is the same rules
as today, and the one-way points of open world (the El voyage, the disc switch) keep their meaning.
Generic ER (Archipelago's `entrance_rando.py`) picks the pairings within the classes. On the world
map the port does not just move the landing point: it runs **the destination's own entrance
routine** at the spot the player walked into, so everything the original entrance does (Lacour's
entrance stores chapters 18 and 23, Cross's and Armlock's story arrivals, the keys and gates) comes
along with the place.

**The big risks** (section 10): story scenes inside towns and dungeons that assume the original
neighbouring rooms (open world at `dungeons` and `all_doorways`); arriving in a room on the wrong
side of a ledge or switch (needs a walk table per room, which doesn't exist yet); world-map side
effects (the Synard's parking spot, ship voyages that pass through world-map points); and the size
of the job (three levels, two modes, one release). Hub mode is the safe ground: the chapter is
locked at 60, nothing closes and the Synard flies on both worlds, so it goes first at every level.

---

## 1. Decisions

The user's (2026-10-28), fixed:
1. **Three levels, one option.** `entrance_randomizer`: `off` (default) / `world_map` / `dungeons` /
   `all_doorways`, each including the one before it. Built and proven in that order, all in so2.apworld
   0.3.0.
2. **Modes.** Open world and hub only. In story mode any value but `off` is an option error at
   generation (`OptionError` in `generate_early`; with weighted YAMLs a roll of story + ER fails the
   generation, and the message says why).
3. **Coupled by default** (going back through a door returns you where you came in); **uncoupled** is
   an option value.
4. **The two worlds separate by default** (Expel's entrances among Expel's, Energy Nede's among Energy
   Nede's). Mixing them: designed in 4.8, built later (in hub mode it looks nearly free; question 4).
5. **Also proposed for 0.3.0** (to confirm, section 11): the indirect-conditions audit first (done in
   section 4.4: nothing in the world reads a region today, and this plan adds nothing that does), and
   real names for the 214 items named by the game's unappraised names.

Proposed by this plan (each is in section 11 for the user):
- In open world, `world_map` shuffles within each continent or island (classes); hub mode across the
  whole world. Whole-world mixing in open world is a later item (it needs a full region-graph logic).
- The world-map spot runs the destination's entrance routine (section 5.2); exits are redirected by
  their original landing point (5.3).
- Fixed doors (section 3.7): never moved, at any level. Notably Fienal's entrance (map 687), every
  boss chamber and story-view room, the Cave of Trials, the Portal of Sealing, Private Action
  entrances, ship voyages, scene transfers, story-gated and one-way doors.
- `all_doorways` is labelled experimental in 0.3.0.

---

## 2. The options

As a player reads them in the YAML (the option text is `options.py`'s docstring; Archipelago prints it):

```yaml
  # Shuffles where entrances lead (open world and hub mode; story mode refuses it).
  # off: every entrance leads where it does in the game.
  # world_map: the world-map entrances of towns and dungeons lead to other towns and dungeons of the
  #   same world. Inside a place nothing moves. In open world each continent or island keeps its own
  #   places, so the ships and the story's journeys still work; in hub mode any place of the world
  #   can be behind any entrance.
  # dungeons: world_map, and the doors inside each dungeon lead to other rooms of that dungeon. Towns
  #   stay as they are.
  # all_doorways: dungeons, and the doors inside each town lead to other rooms of that town.
  #   Experimental. Boss rooms, Fienal, the Cave of Trials and doors the story changes never move.
  entrance_randomizer:
    off: 50
    world_map: 0
    dungeons: 0
    all_doorways: 0

  # How shuffled entrances pair up (only with entrance_randomizer on).
  # coupled: going back through a door returns you where you came from.
  # uncoupled: every door leads somewhere of its own; going back may take you elsewhere.
  entrance_coupling:
    coupled: 50
    uncoupled: 0
```

Names are working names (question 1). Later (designed, not built): `entrance_worlds: separate /
mixed` (4.8).

**Interactions.**

| Option | With the entrance randomizer |
|---|---|
| `world_mode: story` | option error (decision 2) |
| `world_mode: open_world` | classes by continent / island (3.3); area keys follow the place, not the spot (the port locks a spot with the key of the place it now leads to, 5.2); the logic's rules are today's |
| `world_mode: hub` | one class per world at `world_map`; dungeon keys follow their dungeon; the start town's exit leads to the spot paired with it, and the Synards wait there (5.6) |
| `hub_start` | any start works; `start` stays the start town's own arrival (inside the town) |
| `fienal_requirement` | unchanged: Fienal's entrance is fixed (3.7), its gate (`won`) stays on door 68685691 |
| `expel_teleporter` | unchanged: the arena's old man (Fun City 303) and Arlia's guide (map 18) are inside their places; leaving Arlia after the trip puts the party at the spot paired with Arlia |
| `expel_progression` | unchanged (the classes keep the teleporter proof true, 4.1) |
| the El voyage gate, the exit gates | unchanged in open world: El's three places stay on El (their own class), the gates are the same doors with the same keys |
| `goal` | unchanged: the goal gate (43978493, inside Fienal), Filia's scene (Central City 239, inside Central City), the Iseria Queen (the Cave of Trials, fixed inside) |
| `cave_of_trials` | the cave's inside is fixed (nearly every cave map reads the previous map, 3.7); its world spot is its island's only spot in open world (so it stays), shuffled in hub mode |
| `boss_checks`, `character_items`, `bench`, `starting_character`, `exclude_characters` | no effect: chambers and join scenes are inside places, which move whole |
| `shuffle_shops`, `skillsanity`, `craftsanity`, drops, killer moves, traps, multipliers, `difficulty`, `death_link` | no effect |
| `story_route` | no effect on the shuffle (both heroes' chambers are fixed rooms) |
| `entrance_coupling` with `entrance_randomizer: off` | ignored (no error) |

**Spoiler log.** An "Entrances" section: every pairing by readable name, `<=>` when coupled
(`multiworld.spoiler.set_entrance(..., "both", player)`), `=>` when uncoupled.

**Slot data** (what trackers read; the .apso2's `options` gets the first two only):
- `entrance_randomizer`, `entrance_coupling`: the options (`ut.SLOT_OPTIONS`; absent = `off` /
  `coupled`, so 0.2.x rooms restore as before);
- `er`: `{"pairs": {"<doorway id>": <doorway id>, ...}}`: every shuffled doorway and the doorway it
  now comes out at (both directions when coupled). The pack's provisional name for this was
  `door_pairs` (docs/trackers/poptracker.md "Rooms and doors"); this plan replaces it.
- `logic_version`: **2** for a room with the randomizer on (a 0.2.2 world can't rebuild it: it has no
  shuffled entrances). A room with it off stays 1 if the identity batch (section 7) shows the same
  logic.
- `tracker` (the logic export): the post-ER graph with the shuffled entrances left unconnected, plus
  the pairs (section 6.1).

**The seed** (.apso2): `version` 5 when the randomizer is on (else as today, 1-4, so non-ER 0.3.0
seeds still play on port 0.03.00), `options.entrance_randomizer` / `entrance_coupling`, a new top-level
`er` block and more `doors` rows (section 5.1).

---

## 3. The entrance model

### 3.1 Terms

- **Door**: one map-transfer instruction (docs/apworld/doors.json; id = `map * 65536 + offset / 4`).
- **Doorway**: what the player walks into: the doors one trigger starts (one routine, or triggers on
  one pixel). Its id is its lowest door id (docs/trackers/tracker_maps.md "Door ids"). An entrance of
  the randomizer is a doorway: every door of a doorway goes to the same new destination.
- **Spot**: a doorway on a world map (the walk-in trigger of a town or dungeon; doors.json `world`).
- **Pair**: a doorway and its vanilla other end, the layout's `to[].back` (for a spot: the place's
  exit doorway that leads back near it). A shuffled pair is two Generic ER entrances: an exit and an
  ER target of the same name in each doorway's region (Generic ER's coupled naming).
- **"A comes out at C"**: going through doorway A, the party arrives where the game itself puts a
  party arriving next to doorway C, that is at the vanilla destination of C's other end
  (`to(A) = vanilla to(back(C))`). Coupled: C comes out at A. So arrivals are always the game's own
  arrival points next to a doorway, never new positions.
- **Class**: the doorways that may trade places: the same world and level, and reached under exactly
  the same conditions (3.3-3.5). Generic ER's `randomization_group`.

### 3.2 Counts (how they were computed)

From the repo's JSON (docs/apworld/doors.json, field.json, hub_bosses.json, late_proof.json,
hub_proof.json and docs/trackers/tracker_maps.json) with a throwaway script
(`scratchpad/erplan/count.py`, git-ignored; er-model turns its method into a committed tool):

| What | Count | How |
|---|---|---|
| Doors | 2,529 (2,449 inline, 80 stack); 2,044 arrival points | doors.json `doors`, `arrivals` |
| World-map entrance doors | 66: Expel 44, Nede 22, in 35 key groups | doors.json `world` 1 / 2 |
| ... walk-in | 46 doors in **40 doorways** (Expel 29 / 24, Nede 17 / 16) | `via` by `ext_76` |
| ... Private Action | 16 (Expel 11, Nede 5), one doorway each | `via` by `B_0e` (docs/apworld/field.md "B 0x0e") |
| ... ship landings | 4 doors in one routine (0x57d8, Expel) | no `via` |
| Doors to a world map (exits) | 80 (Expel 55, Nede 25) in 74 (map, routine) pairs; 46 distinct world-map landing points | `to.map` 1 / 2, not `world_script` |
| ... with a trigger | 48: 44 walk exits, 4 talk (the ship voyages: Herlie's sailor, Port Hilton's captain and Hoffmann ship, map 231's soldier) | tracker_maps markers, `via` |
| ... without a trigger | 32 (Private Action and scene exits) | no marker |
| Rooms, room groups | 794 rooms; 66 room groups (Expel 48, Nede 18) | tracker_maps.json |
| Doorways in rooms | **1,775** (1,822 markers; 1,734 trigger regions, 41 talk) | `rooms[].doors`, unique `id` |
| ... by kind | 46 exits to a world map, 1,008 inside a dungeon, 29 between a dungeon and a town, 692 inside towns | dungeon = a map in hub_bosses.json `dungeons[].maps` (448 maps, 20 dungeons; 439 of the 794 rooms) |
| ... per world (dungeon / boundary / town / exit) | Expel 722 / 14 / 447 / 30; Nede 270 / 15 / 244 / 15; the hub maps (625, 654-658) 16 / 1 / 1 / 1 | field.json `map_worlds` |
| ... with a symmetric other end | 1,285 (back(back(A)) = A); 209 asymmetric; 281 with none or several | `to[].back` |
| Scene transfers listed on rooms | 380 | `rooms[].scene_doors` |
| Arrival-sensitive maps | 147: the Cave of Trials (698-825 but 699 and 754), Fienal's stair rooms (647, 648, 650-653, 673), and 14 others: 53, 155 and 320 in towns, 341-344, 357, 363, 368 (Mountain Palace), 384 (Lasguss Mountain), 481, 488, 489 (Hoffmann Ruins) | proof nodes with state (`map@...` in late_proof / hub_proof `routes`): their scripts read the floor G[0x1cf], the previous map G[0x1c0] or flag 157 |
| Boss chamber and view maps | 42 (22 chambers, with approach rooms and Berle's tower-wide view 557-575) | hub_bosses.json `bosses[].maps` |
| Chapter-store maps | 52 | field.json `story.chapters[].set_at` (step stores need apworld/tools/ap_extract.py's steps again: scratchpad/enemy/ap5/steps.json is gone) |

**The strict pool** (what a first build can move): a doorway qualifies when it is two-way with a
symmetric other end, unconditional, has one destination, touches no arrival-sensitive map and no
boss or view map, and is not in Fienal, the Cave of Trials or the Portal of Sealing; a pair is in the
pool when both ends qualify.

| | Expel | Energy Nede | Total |
|---|---|---|---|
| Spots (`world_map`), hub | 23 of 24 | 15 of 16 | 38 |
| Spots, open world | 22 of 24 | 15 of 16 | 37 |
| Dungeon pairs (`dungeons`) | 115 (114 inside one dungeon) | 22 (20) | 137 (274 doorways) |
| Town pairs (`all_doorways`) | 155 | 78 | 233 (466 doorways) |
| Qualifying doorways whose other end doesn't | | | 108 |

Why the rest stays (each doorway counted once, by its first reason): dungeons: in Fienal / the Cave of
Trials / the Portal 439, boss or view map 73, story variants (several destinations) 87,
arrival-sensitive 52, conditional 40, one-way 20; towns: conditional 103, boss map 18, story variants
17, one-way 17, arrival-sensitive 15; the hub maps 17. So `all_doorways` moves about 740 of the
1,775 room doorways plus the spots, not all 1,800: the user's "every doorway" is every doorway that
is safe to move. The pool shrinks further with the open world's story rooms and the always-open
components (3.4), which er-model computes.

Per dungeon (strict-pool doorways, before pairing): Hoffmann Ruins 76, Cross Cave 48, Sanctuary of
Linga 34, Field of Power 30, Salva Drift 23, Zand's Mansion 22, the Mars dungeon (Heraldry Forest) 19,
Field of Courage 15, Mountain Palace 14, Lacour Front Line 14, Cave of Red Crystal 8, Mihne Cavern 8,
Field of Love 8, Lasguss Mountain 4, Eluria Tower 2, Field of Intelligence 1.

### 3.3 Level 1: `world_map`

**What moves.** The 40 walk-in spots, minus the fixed ones: in open world both of Port of Clik's
(old Clik until chapter 14, new Clik after: two triggers registered by chapter, so a place behind them
would be reachable only for a while) and Fienal's (map 687); in hub mode old Clik's (never registered
at chapter 60) and Fienal's. Appendix B lists the 40.

**Pairs.** A spot and its place's exit doorway (`back`). Two-spot places (Town of Salva 43 / 44, Mars
Village 124 / 126, Lasguss Mountain 381 / 382, L'Aqua 579 / 686) are two pairs: each of its exits leads
back to the spot paired with it, as the game already does (Salva's exits pick the landing point by the
map the party came in from, `local[0x0] == 60`).

**Classes.**
- **Hub mode: one class per world.** Nothing closes at chapter 60 and both Synards fly (the hub's
  `flags` hold 11, its `globals` both parking spots), so every spot of a world is reachable once the
  party is on that world's map: any place may sit behind any spot of its world. Expel 23, Energy Nede 15.
- **Open world: one class per access class**, the spots the game reaches under the same conditions.
  The story's ships and one-way points (the Herlie ferry from chapter 16, the Hoffmann ship, the El
  voyage at chapter 27 step 7000, the disc switch, the transporter, the Synard from stage 16) connect
  the continents; inside a class the party walks. Estimated from the teleporter proof's islands
  (`TELEPORTER_ISLANDS = {229, 231, 232, 478, 557, 704}`, apworld/tools/ap_late_proof.py:118) and the
  stages; **er-model computes the real ones** (apworld/tools/ap_walkmap.py for both worlds and for
  each terrain version: Expel's block 48 changes at chapter 15):

  | World | Class (estimate) | Spots |
  |---|---|---|
  | Expel | the Cross continent (from the start) | Arlia, Salva x2, Mars x2, Herlie, Cross, Cross Cave, Lasguss Mountain x2, Mountain Palace (11) |
  | Expel | the Lacour continent (the Herlie ship, stage 6) | Port Hilton, Lacour, Linga, Sanctuary of Linga, Lacour Front Line (5) |
  | Expel | El (the voyage, stage 10) | El Beach, Eluria Colony, Eluria Tower (3) |
  | Expel | Hoffmann's island (the ship, chapters 23-59) | map 231, Hoffmann Ruins (2) |
  | Expel | the Cave of Trials' island (Virtual Expel) | Cave of Trials (1: in effect fixed) |
  | Nede | Central City and North City's island (the disc switch) | Central City, North City (2) |
  | Nede | Noel's island (the transporter, chapters 38-40) | Noel's House, Cave of Red Crystal (2; to check) |
  | Nede | the Synard (stage 16) | Giveaway, Armlock, Field of Intelligence, Mihne Cavern, L'Aqua x2, the Gallery (463), Field of Courage, Field of Power, Fun City, Field of Love (11) |

  Why not the whole world: a Cross-continent place (Cross Cave, stage 4) behind a Lacour-continent
  spot would need the ship of stage 6 first, and the story could never get there; El's places behind
  Cross-continent spots would undo the voyage gate. Keeping each doorway in its class keeps every
  route's keys and every proof the same (4.1). Whole-world shuffling in open world needs the full graph
  logic of 4.8.

**Private Action entrances** (16): not shuffled. The PA prompt at a spot still enters the spot's
original town in Private Action mode, locked by that town's key as today (its door is in the town's
key group). The logic never counted PA routes (they only add ways in). A PA that follows its walk-in
twin (the destination town's PA, or none) is question 6.

### 3.4 Level 2: `dungeons`

**What moves.** Doorways with both ends in the same dungeon (hub_bosses.json `dungeons[].maps`), in
the strict pool. A dungeon's front doors (its world spot, or a town's door into it: Salva 43 -> Salva
Drift 63, Alen's Mansion 53 -> 62, Mars 124 -> 465, Herlie 142 -> Zand's Mansion 143, North City 264 /
272 -> the Synard Plant) stay its front doors, so the dungeon keys (hub D4) and the story's way in are
unchanged; where the front room's inner doors lead is what changes.

**Classes: one per always-open component of each dungeon.** Take a dungeon's rooms and its
unconditional two-way doorways: the connected pieces are the always-open components. Doorways shuffle
only within their component. Then every room is reachable under exactly the conditions it was before
(the conditional doors that join components are fixed, so the component graph is the same), and the
proofs, which say which rooms each game state reaches, stay true for every seed (4.1). In hub mode the
components are taken at the hub state (a conditional door that is open at chapter 60 counts as open,
since the chapter never moves), which gives the hub more to shuffle; er-model computes both sets.

**Open world, also fixed:** every doorway of a room where the open world's story stores a chapter or
step or runs a scene (the El voyage's meeting rooms 444 / 447 / 448, the Synard Plant's approach, the
Front Line), so the story always meets its rooms through their original doors.

### 3.5 Level 3: `all_doorways`

**What moves.** Also the strict-pool doorways inside towns (and the town-side buildings: Alen's
Mansion, Rena's Room, the Armory, the Prince's Chambers, King's Room, Lacour Castle's rooms...), within
each town's always-open components, with the same story-room rule in open world. **Experimental** in
0.3.0: towns hold most of the story's scenes, so this level carries the most risk (section 10).

### 3.6 Towns and dungeons: the 66 area groups

The rule: a **map** is a dungeon map when hub_bosses.json lists it in a dungeon's `maps` (the hub's 20
dungeons, decided with the user for hub mode's keys, 448 maps); every other map is a town map. A room
group (tracker_maps.json `room_groups`, the tracker's areas) is a dungeon when all its rooms are dungeon
maps, a town when none is, and mixed otherwise (mixed groups are split by map). Result: Expel 26
dungeon / 20 town / 2 mixed; Energy Nede 9 / 6 / 3. The full list is Appendix A.

Note the two counts of 66: the open world's **66 world-map entrance doors** (44 + 22, in 35 key
groups) and the tracker's **66 room groups** are different things that happen to share the number.

Not classified with confidence (flagged, er-model decides with the data):
- **Shingo Forest** (7 rooms, random encounters, no hub key): a town by the rule; it is an outdoor
  field like a dungeon. Recommendation: dungeon. Its first maps are story rooms anyway.
- **Field of Power**'s group holds six maps that are not the Field: 30 (Rena's dream), 620-623 (the
  transporter hub to the Temple and to Eluria Tower) and 625 (the disc switch). All fixed (story maps,
  `map_worlds` 0).
- **North City**'s group holds 270 (the Synard Plant) and 593 (the Field of Love's platform); **Town
  of Salva**'s holds 63 (Salva Drift); **Mars Village**'s holds the Heraldry Forest (456-466, the Mars
  dungeon): split by map as the rule says.
- **The Gallery (463, Nede) belongs to the Mars dungeon.** hub_bosses.json lists it among the Mars
  dungeon's maps, and both proofs have the edges 462 -> 463 -> Energy Nede's world map: the Heraldry
  Forest on Expel (Varmillion's chamber 462) leads to the Gallery on Energy Nede (Azamgil's chamber
  463). So the worlds are already joined there, besides the disc switch and the teleporter. Both maps
  are chamber maps (fixed); the Gallery's world spot is a Nede spot like any other.

### 3.7 What never moves (fixed), and why

| Fixed | Why |
|---|---|
| Scene transfers (380 listed on rooms; doors with no trigger) | the story moves the party there; a scene assumes its rooms. Multi-map scenes (the El voyage 444 -> 422 -> 169 -> 548, the disc switch at 625, the Outer Wall 617-619, Inn Hilton Island at night 168-182, Planet Milocinia 4-8) are chains of these |
| Story-gated doors (any `requires`: 656 doors; doorways marked `conditional`) and story variants (125 doorways with several destinations) | the door does different things in different story states; open world plays the story. Hub mode may count the ones decided open at chapter 60 as open (3.4) |
| One-way doors (no symmetric other end) | Generic ER pairs one-ways only with one-ways; not worth it in 0.3.0 |
| Arrival-sensitive maps (147: nearly all of the Cave of Trials, Fienal's stair rooms, and 14 rooms of towns and dungeons, 3.2) | their scripts pick where the exits go from how the party came in (the previous map, the floor, flag 157) |
| Boss chambers and their story views (42 maps) | each chamber's view and fight assume its approach (Arcmene's lower door 418 -> 417; Cyril's east side 674, which the hub's P5 door 44502760 enforces; Bark's 453, the Synard's 264); the hub's `ask` rows (Shigeo 281, Marsilio 305 and the antechamber door 19929509) and P5 doors stay as they are |
| Fienal: its entrance spot (68685691 -> 687) and all 40 maps | the endgame: the Fienal requirement's gate (hub), the stages that enter 687 (20, 23), the stair rooms, three chambers, the Final Save Point (671) and the goal gate (43978493). One spot of 16 is little to lose |
| The Cave of Trials' inside, the Portal of Sealing (incl. the Heraldry Weapons Laboratory) | the cave reads the previous map everywhere; the Portal is story-gated (out of hub mode) |
| The disc switch (map 625, B 0x6a) and the El voyage gate (room 444's trigger, door 29106503) | one-way story points; their rooms are story rooms |
| Vehicles: ship voyages (the 4 talk exits and the landing routine 0x57d8), the Expel teleporter (Fun City 303's old man, Arlia 18's guide), North City's transporter (272), the Synard | talk doorways (41) and vehicles move the party by script; they stay, and they connect places (whole), not spots |
| Private Action entrances (16) | 3.3; question 6 |
| Port of Clik's two spots (open world), old Clik's (hub) | registered by chapter (3.3) |

**Fun City's spot is not fixed** (the design note in docs/apworld/apworld.md "Entrance shuffle"
suggested keeping it): the arena's teleporter is inside Fun City and moves with it, the Fun City Pass
is a starting key whatever spot leads in, and after the Final Save Point the Synard reaches every Nede
spot. The hub's default start (Fun City) works anywhere; Shigeo's street keeps asking (its doors are
fixed). Question 7.

### 3.8 Two-way and one-way, groups

Every shuffled entrance is two-way (`EntranceType.TWO_WAY`); one-ways are fixed in 0.3.0. Uncoupled
mode still uses two-way entrances (Generic ER then pairs each direction on its own).

| Level | `randomization_group` (packed int) | `target_group_lookup` |
|---|---|---|
| `world_map`, hub | (world, side) with side = spot or place | Expel spot <-> Expel place; Nede spot <-> Nede place |
| `world_map`, open world | (world, class, side) | spot of class k <-> place exit of class k |
| `dungeons`, `all_doorways` | (world, area, component) | each group to itself |

`preserve_group_order` is not needed (one target group per source group).

### 3.9 Unsure (for er-model to settle, not guessed here)

- The open world's classes (3.3): the Cross continent may be split by terrain (Lasguss Mountain's two
  spots, the Mountain Palace, Clik's north); Noel's island; whether any walk route depends on the
  chapter. ap_walkmap.py today reads Expel at one chapter only.
- Whether each place's world doorways are connected inside it (two-spot places), or the place needs
  two regions.
- The 209 asymmetric `back` doorways (two doors into the same room, "nearest the arrival"): which
  pairing is the real one.
- Within-room walkability for the interior levels (section 10, risk 2): a doorway's arrival may not
  reach every other doorway or chest of its room (ledges, switches). The proofs fire triggers without
  walking, so nothing measures this yet.
- The open world's story rooms: the chapter stores are known (52 maps), the step stores must be
  extracted again.
- Each spot's world-map landing point when several exits of a place land at different points
  (Private Action variants at Arlia 19, Salva 45 / 60, Mars 125 / 127).

---

## 4. Logic

### 4.1 The principle: the same rules, new regions

Because doorways only trade places within classes of identical reachability (3.3-3.5), every place
is reached under the same conditions as before, with the same keys on every route (an area key
follows its place: whatever spot leads into Salva needs the Salva Pass). So:
- **The location and event rules stay exactly today's** (rules.py `open_world_access`, regions.py
  `stage_rules`, hub.py, skillsanity.py): the stages, the early / teleporter / late modes and their
  route keys, the gates, the drops, the Fienal requirement.
- **The progression rules stay too**: the late, teleporter, voyage and hub proofs say which rooms each
  game state reaches, and those sets don't change (classes preserve them). No proof has to run again
  per seed.
- **What changes is the region graph**, so that Generic ER has entrances to shuffle and the trackers
  can tell found places from unfound ones (6.4). In a real generation every shuffled entrance is
  connected, so every region is reachable exactly when today's rules say: same truth values, a
  different random stream (Generic ER draws from `world.random`), so ER seeds differ from 0.2.2's for
  the same seed number. With the randomizer off nothing runs and the output must be byte for byte
  0.2.2's (section 7).

### 4.2 The region graph

| Level | Regions (with the randomizer on) |
|---|---|
| `world_map` | Menu; one world-map region per world; two regions per place (the 35 key groups): an arrival region holding the place's ER targets and its exits to the world map (Generic ER's coupled naming needs a target and its reverse exit in one region), and an inner region with its locations (today's rows move into their group's; rows with no group, such as world-map drops, Item Creation and the guilds, stay where they are); Fienal and the Cave of Trials as today. The world region holds the spots |
| `dungeons` | + one region per room of a dungeon with shuffled doorways (its locations move into it); fixed doorways are plain entrances |
| `all_doorways` | + one region per room of a town with shuffled doorways |

Fixed connections stand for the story's own moves and the vehicles: Menu -> the start place (open
world: Arlia's key group, where the story starts; hub: the start town); the disc switch -> Central
City (the scenes of chapters 36-37 land there); Fun City <-> Arlia (the teleporter, its event); Port
Hilton <-> Herlie and <-> map 231 (ships); the Front Line -> El Beach (the voyage); Energy Nede's map ->
map 687 -> Fienal; Fienal -> the Cave of Trials (Virtual Expel). Their rules are the existing events'
and stages' (a ship: the stage that first sails it). In a real generation they add nothing (the
location rules already carry every stage and landing key); they are there so that a tracker following
found entrances counts a place the story or a ship brought the party into before any of its doors was
found.

**Keys on the graph.** A place's arrival region leads to its inner region through a gate with the
place's key (open world: the area key and, from 4.1, nothing else; hub: the dungeon key, towns open),
and each of the place's exits carries the same rule, so in uncoupled mode the logic never walks in at
one spot and out at another without the key (the port refuses such a spot outright). Without the key
the arrival region is a dead end with nothing in it, as the refused spot is in the game. In open world the
gate is implied by every location rule already (each mode includes the area's key, and a story boss's
stage includes it), which the identity tests check.

**Story stages, chapter gates and the El gate.** Unchanged: the stage events keep their rules (the
previous stage and the keys of the places the story walks into); the El voyage's gate needs El's three
passes (the El class is El's own three places); the exit gates (Central City until chapter 38, Noel's
House until 40) are the same doors. The disc switch leaves Expel behind as before; the teleporter's
reach is the teleporter proof's, which holds because the Cross and Lacour continents keep their own
places.

### 4.3 What the trackers see (deferred entrances)

In UT's regeneration with `enforce_deferred_entrances` not "off", and in the PopTracker pack unless
the player shows all doors, shuffled entrances start unconnected and are connected as the player
finds them (6.2-6.4). Then a place's checks are in logic only once a found route reaches it: leave
Arlia once and Expel's map is reachable; walk into a spot once and its place is.

### 4.4 The indirect-conditions audit

Generic ER refuses a world without explicit indirect conditions (scratchpad/ap/Archipelago/
entrance_rando.py:383). SO2World doesn't override `explicit_indirect_conditions`, so it is True
(Archipelago's default, worlds/AutoWorld.py:324), and the world registers no indirect condition. That
is correct only while no entrance rule reads a region. The audit:

| Site | What it sets | Reads a region? |
|---|---|---|
| apworld/so2/regions.py:41-44 | the story's entrances: New Game, Disc Switch (`Has` event), To Fienal, Virtual Expel (`Has` event) | no |
| apworld/so2/regions.py:87-95 | open world's stage events, the Disc Switch, the Final Save Point, the teleporter: `need` / `Has` | no |
| apworld/so2/rules.py:273, 290-297, 303, 329, 353 | killer-move levels (`need`), the goals, the completion, crafts (skillsanity's `craft_rule`), locations (`ways` of open-world modes) | no |
| apworld/so2/hub.py:269-273 | the hub's entrances: New Game per town, `Into <dungeon>` (`Has` key), Into Fienal (`Has` event) | no |
| apworld/so2/hub.py:336-342, 358, 369 | the Fienal gate, Indalecio, the Iseria Queen, the completion, crafts, drops (`need`, `any_of`) | no |
| apworld/so2/skillsanity.py:441, 451, 456 | guild checks and learn levels (`ways`, `need`) | no |
| apworld/so2/story.py:229, 239, 256 | UT's story events (`True_` / `False_`, `Has`) | no |
| apworld/so2/logic.py:72 | `open_by_default`: `True_` | no |

No rule uses `can_reach` or a `CanReach...` rule; `logic.RESOLVED_KINDS` (logic.py:24) has only
`True_`, `False_`, `Has`, `HasAll`, `HasAny`, `And`, `Or`, and test/test_logic.py:120
(`test_no_region_reads`) fails on anything else. **Result: nothing to register.** This plan keeps it
that way: the gates of 4.2 are item rules, and anything that would depend on a region becomes an
event placed in that region instead (an event's location is re-checked on every sweep, so it needs no
indirect condition). If a later version needs a region read on an entrance, Rule Builder's
`CanReachRegion` registers its condition through `set_rule` (docs/apworld/apworld.md "Rules as data");
it then goes into `logic.RESOLVED_KINDS`, `logic.as_rule` and the export's `["region", r]` body (both
evaluators read it already). er-world adds a test that `explicit_indirect_conditions` stays True.

### 4.5 Using Generic ER

In `SO2World.connect_entrances` (new; the only step Generic ER may run in):
1. Skip unless the randomizer is on. In UT's regeneration (`ut_slot_data` set) don't randomize:
   connect from the slot data's `er.pairs`, or leave them hanging for deferred tracking (6.2), as
   Tunic does.
2. Build the class of each shuffled doorway from er-model's table; give each its
   `randomization_type` (TWO_WAY) and `randomization_group` (3.8); `disconnect_entrance_for_randomization`
   on every shuffled entrance (it names the ER targets for coupling).
3. `state = randomize_entrances(self, coupled, target_group_lookup)` with the lookup of 3.8.
4. Check (4.6), then keep `state.pairings` (exit name, target name) for slot data, the seed and the
   spoiler.

**Custom `Entrance` class** (`SO2Entrance`, both methods calling `super()` as the guide asks):
`can_connect_to` refuses a doorway's own reverse target (a door to itself) and anything outside its
group (the lookup already does this; the check is a safety net). No sphere-1 constraint is needed: the
logic is today's, and sphere 1 with it.

**Generic ER's own checks** run with every item of this world collected (`ERPlacementState`), so keys
never matter to it, only events; it places dead-end places last and refuses to cap the graph off.

### 4.6 Failure and retry

Generic ER doesn't retry ("no attempt made to save a failed randomization"). The world wraps it:
- on `EntranceRandomizationError`, or when a post-check fails, undo every placement (disconnect the
  placed exits, put the ER targets back) and try again with the next draws of `world.random`
  (deterministic), up to 20 times; then fail the generation with a message that names the level and
  the class that couldn't be placed;
- post-checks: every class still forms one always-open component (interior levels: Generic ER may
  reconnect a component's rooms into two loops that are each reached through some fixed door, which
  would break 4.1); in uncoupled mode every class is strongly connected (every region of it reaches
  every other), so no one-way trap appears;
- the batches (section 7) report how often a retry was needed per level and mode; a class that fails
  often is fixed rather than retried.

### 4.7 Accessibility (every key stays reachable from the current play state)

The project's rule holds as it does today:
- hub mode: nothing closes and every move is two-way (coupled) or strongly connected within its class
  (uncoupled), so from anywhere the party can reach any place of the world again; the Synards follow
  the party (5.6);
- open world: the gates and the progression rules are the existing ones, and they stay true because
  the classes preserve every state's reach (4.1). The one-way points (the El voyage, the disc switch)
  are the same doors with the same gate keys.
- A coupled door always leads back where the party came from; uncoupled classes are checked strongly
  connected (4.6).

### 4.8 Later items (designed, not built)

- **Whole-world shuffling in open world** (an option value or a separate toggle): spots of different
  continents trade places. This needs the full graph logic: world-map regions per class with the
  ships and the Synard as entrances, each stage an event per place it walks into (placed in that
  place's region, so no region reads), the teleporter's and the voyage's reach computed per seed by a
  graph search over the post-ER graph with each era's vehicles, and the El gate's keys from the
  post-voyage routes. Large, and the proofs no longer hold per seed (er-proof would have to replay
  them per seed).
- **Mixing the worlds** (`entrance_worlds: mixed`). Open world: needs the item above and more (an Expel
  place behind a Nede spot opens after the disc switch). **Hub mode: nearly free**: one class per side
  across both worlds; both Synards fly from the start and nothing closes; cross-world pairs can't run
  the destination's routine (Expel's spots live in script 909, Nede's in 1048), so the port uses a plain
  `to` redirect for them, which is safe in the hub because the chapter lock drops the routines' story
  stores anyway (to verify: each world map's music and state on arrival from the other world).
  Recommendation: build it for hub mode after `world_map` is proven, if er-proof's replays pass
  (question 4).
- One-way doorways in the pool (paired with one-ways), and Private Action entrances that follow their
  walk-in twin (question 6).

---

## 5. Port work

The rules: **Archipelago off is the disc's game untouched** (none of this runs without an ER seed),
**enhance, don't degrade** (no feature of the game is taken away; the Synard following the party is an
enhancement), no `--debug` or `SS_*` hooks in an AP run, no save editing (the sidecar is the port's).

### 5.1 Seed format 5

`version` 5 when `options.entrance_randomizer` is not `off`; a port that reads 1-4 refuses it
("unsupported version 5 (this build reads 1 to 4)", port/src/game/ap/seed.cpp:395). Proposed fields
(er-port and er-world settle them in docs/apworld/seed_format.md first):

```json
"er": {
 "level": "world_map", "coupled": true,
 "pairs": {"59576065": 2824838, "2824838": 59576065, ...},
 "spots": [{"map": 909, "routine": "0x3bf8", "doorway": 59576065, "runs": "0x4ac8"}, ...]
},
"doors": [ ... today's rows ..., {"id": 2824838, "doorway": 2824838, "to": {"map": 1, "x": 50920, "y": -150, "z": 16999, "dir": 109}}, ... ]
```

(The example: Arlia's spot now leads into Salva; walking into it runs Salva's own entrance routine
0x3c6c, and Salva 43's exit, door F (2824838), lands where leaving Arlia would, next to Arlia's spot.)

- `er.pairs`: doorway -> the doorway it comes out at (the same object as slot data's).
- `er.spots`: each shuffled world-map spot: the trigger's routine (in the shared world script: 909 for
  Expel's chunks 909-980, 1048 for Nede's 1048-1110) and the routine to run instead (5.2).
- `doors` rows with `to` (the existing mechanism, applied ahead of the door table: ap.cpp
  `on_map_transfer`) for every exit door and interior door that moves, each with its `doorway` id for
  the found key. Keys, `keys`, `until`, `won`, `flags` stay on their doors as today.
- `doors.validate_doors` (apworld/so2/doors.py) grows the ER checks: every `to` is an arrival of the
  game; every `runs` is a walk-in routine of the same world; `pairs` is an involution when coupled;
  no fixed door (3.7) moves.

### 5.2 World-map spots: run the destination's routine

When a walk-in trigger on a world map is about to start its routine (the ext 0x76 regions; the same
point where `door_shut_by` checks keys today, ap.cpp:3256), the port starts `runs` instead. So the
player walking into Arlia's spot gets exactly what walking into the destination's own spot does: its
key check (the destination's door rows carry its key, so the banner names the right pass), its gates
(Fienal's `won` would follow too, but Fienal is fixed), its story stores (Lacour's routine 0x47e8
stores chapter 18 at chapter 17 and chapter 23 at chapter 22 step 9999: with a plain `to` redirect the
story would store them at the wrong spot and never at Lacour's), its arrival variants (Cross 0x45c0:
map 87 at chapter 9 step 0, else 74; Armlock 0x3c34: 307 at chapter 52 step 0; Eluria Colony 0x5514;
Lacour's four), its music. The trigger's own registration conditions stay the spot's. A refused spot
(a pass missing) behaves as today: the leader keeps the controls, the banner names the destination's
pass.

### 5.3 Exits: redirected by landing point

An exit of place X onto the world map lands at one of the landing points of X's spot(s). With X now
behind spot S, the APWorld writes a `to` row for every exit door whose original landing point belongs
to X's paired spot, sending it to S's landing point (coupled), or to the landing point of the spot its
own pairing names (uncoupled). This catches every variant without special cases: Private Action exits,
story exits (Salva 43's chapter-5 conditions, Lacour 211's step conditions) and the exits that pick a
landing point by the map the party came in from.

Never redirected (fixed): the 4 voyage exits (Herlie's sailor 0x7298, Port Hilton's captain 0x714c and
Hoffmann ship 0x79c0, map 231's soldier 0x6a4c) and the landing routine, which carry the party to the
sea and the ship; the disc-switch map's exit (625 -> (16115, 65012)); Fienal's (687). These are
excluded by door, not by landing point: map 231's soldier sails from (93687, 59682), the same point
231's walk exit (0x672c) lands at. er-model gives the table: each of the 46 landing points -> its spot,
or "fixed", and the excluded doors.

### 5.4 Interior doorways

`to` rows on every door of a moved doorway: `to(A) = vanilla to(back(C))` (3.1). Nothing else of the
door changes: its routine (with its conditions, music, fades) runs as before. To check per door
(er-model): the transfer's `transition` and `extra` arguments, which stay the source's; same-map
transfers are left out.

### 5.5 Arrival safety

Every arrival is one the game itself uses next to that doorway, so the game's own rule applies (a
trigger fires again only after the leader has left it): no bounce-back loops. What can still go wrong
is covered elsewhere: rooms whose scripts read how the party came in (fixed), story rooms in open world
(fixed), a room whose arrival can't walk to the rest of it (the walk table, 3.9). er-proof replays
every moved doorway headless (section 7).

### 5.6 The Synard

Town scripts store the Synard's parking spot when the party visits (Arlia's 17 / 19 and Virtual
Arlia's arrival store Arlia's spot, map 279 stores Fun City's; table A 0x19 `set_point`), and the hub's
New Game writes both from the start town's spot. With places moved, the Synard would wait at the
place's *original* spot, possibly on another island. So in an ER seed: **whenever a redirected exit
puts the party on a world map where the Synard flies (Nede after flag 11; Expel in Virtual Expel or the
hub), the port parks it at the landing point**, after any script store. The hub's start globals are
written the same way by the APWorld (the start town's exit's paired spot). In vanilla the Synard
already waits where the party leaves a town, so this keeps the game's own behaviour; in uncoupled mode
it is what keeps the party from being stranded away from it. er-port first lists every parking store
(both worlds) to confirm the order.

### 5.7 Story-gated and scene doors

Fixed (3.7), so nothing to do: their door rows, keys and gates are today's. The open world's port
behaviours (the harbour captain, the landing checks, the teleporter patches, the exit gates) are inside
places and unchanged.

### 5.8 The hub's views and `ask` rows

Chamber maps are fixed at every level, so views (`cut_to`) and `ask` rows keep their doors. Shigeo's
row asks at every transfer into 281 from outside it, after the door table (`hub_ask_transfer` gets the
redirected arguments), so it would follow a moved door anyway; Marsilio's names door 19929509, which
is fixed. If a later version moves chamber doors, `ask` rows must name destinations, not door ids.

### 5.9 Saves and loads

The redirects are the seed's, not the save's, so a save made anywhere loads as today. What the port
keeps per save, in the sidecar: the found doorways (5.10). Not saved, as today: the `ask` visit state.
The Synard's spot is game state (saved by the game). Loading another seed's save stays refused
(`load_allowed`).

### 5.10 The found-entrances key

`so2_entrances_{team}_{slot}` (reserved in docs/apworld/data_storage.md; the spot is marked at the end
of `publish_tracker_keys`, ap.cpp:2425). Proposed:
- **Value:** an object `{"<doorway id>": <doorway id>, ...}`: each moved doorway the player has been
  through and the doorway it came out at (a subset of `er.pairs`); in coupled mode the reverse too (the
  player has seen both ends). Integers as JSON numbers, keys as strings.
- **Written:** the first time a doorway is used in a session, with the protocol's `Set` and one
  `update` operation (a dict merge; `default` `{}`), so a write never loses another's entries;
  after every connect and reconnect the whole found set once. Throttled like the other keys.
- **Kept:** in the save sidecar (`er_found`), so offline play and reloads keep what was found; a
  tracker sees the union of what the server has. A spot refused for a missing pass also counts as
  found (the banner names the destination's pass; question 8).
- The pack's provisional name `so2_doors_{team}_{slot}` (a list) is replaced by this one.

---

## 6. Trackers

### 6.1 Slot data and the logic export

- Slot data: `entrance_randomizer`, `entrance_coupling`, `er.pairs`, `logic_version` 2 (2.).
- The export (`tracker`, format 1; docs/apworld/seed_format.md "The tracker export"): written after
  `connect_entrances` as today, but every shuffled entrance is written with `to` -1 (unconnected), and
  an optional key `er`: `{"key": "so2_entrances_{team}_{slot}", "coupled": true, "pairs": [[entrance
  index, region index], ...], "doorway": {"<entrance index>": <doorway id>}}`. A format-1 reader that
  doesn't know `er` sees a hanging graph (only the start place in logic), which is safe; it is not a
  format change. Both evaluators connect from it (`TrackerLogic.connect`, the pack's
  `so2_logic_connect`, both built and tested).
- **Size**: test/test_tracker.py keeps the export at or under 64 KB; with room regions and ~1,600
  entrances an `all_doorways` export will be well over that (an estimate: 100-200 KB). The budget for
  ER rooms is raised (proposal: 256 KB, measured), and entrance names are kept short in the export
  (indices; names once).

### 6.2 Universal Tracker

- **Regeneration** (`re_gen_passthrough`): `connect_entrances` never calls Generic ER; it rebuilds the
  same regions and either connects every pair from `er.pairs` (host.yaml `enforce_deferred_entrances:
  "off"`: the full post-ER logic, spoilers included) or leaves them hanging (`"default"` / `"on"`):
  the world then sets `found_entrances_datastorage_key = ("so2_entrances_{team}_{player}",)` and its
  `reconnect_found_entrances` connects each found exit to its pair's region (and the reverse when
  coupled), as story.py does for the story key (apworld/so2/story.py:214-260). ER never runs in story
  mode, so the two keys never meet; the attribute is a tuple anyway.
- **ut.py:** `entrance_randomizer` and `entrance_coupling` in `SLOT_OPTIONS` (absent = off / coupled);
  `er` in the restored choices (like `characters`); `LOGIC_VERSION = 2`, `LOGIC_VERSIONS = (1, 2)`
  (this world still rebuilds 0.2.x rooms); `check_restored` compares the new keys; refusals name
  so2.apworld 0.3.0.
- **Map tab:** door sections named as the AP entrances on the room pages (the room markers) and on the
  world pages (new spot markers, 6.3), so UT colours them (red / green unconnected, grey connected).
  The tracker/ JSON is regenerated with the pack (`trackers/tools/ap_poptracker.py --ut-out
  apworld/so2/tracker`).

### 6.3 The PopTracker pack (1.1.0)

- **Door panel live labels** (docs/trackers/poptracker.md "Rooms and doors": the seam is built):
  `so2_door_destinations(id)` reads `er.pairs` from slot data and the found key; a paired doorway reads
  "A -> ?" until found, then the room it comes out in. Unpaired (fixed) doorways keep their vanilla
  lines. The Clear handler subscribes to `so2_entrances_*` (Get + SetNotify).
- **World-map spots:** tracker_maps.json format 1.2 adds `spots` to the two world images (each walk-in
  doorway's id, its landing point in image pixels, its key group); the pack draws a marker and a panel
  line per spot ("Arlia Village -> ?").
- **Colours:** a door not found yet gets Inspect (blue): story awareness, the only other user of blue,
  never runs in an ER room (story mode refuses ER); a known door stays SequenceBreak (yellow).
  `so2_door_access` is the one place (question 10).
- **Logic:** `scripts/logic.lua` connects found entrances from the export's `er` (as the reference does).
  A pack setting "Show every door (spoiler)" connects all pairs at once.
- **Version:** pack 1.1.0 (format 1.2 maps); pack 1.0.0 shows an ER room as hanging logic with vanilla
  door labels, so the release notes say to update.

### 6.4 Non-spoiling play

Both trackers show nothing the player hasn't seen: hanging entrances until the port reports them,
door labels "?" until found, checks of unvisited places out of logic. The spoiler-ish paths are opt-in:
UT's `enforce_deferred_entrances: "off"`, the pack's setting. Slot data does carry every pairing (any
client of the slot can read it), as in every Archipelago entrance randomizer.

---

## 7. Testing and proof

- **World tests** (apworld/so2/test, Phase A style, `SO2TestBase`): a new test_er.py: the option error
  in story mode; each level x mode x coupling generates and is beatable (`WorldTestBase`'s own checks);
  pairs are an involution when coupled; every pair is within its class; no fixed doorway moves;
  `validate_doors` passes; the routine swaps stay within their world; the same truth value for every
  location rule as the world without ER on every state tried (the logic-unchanged property, as 0.2.2's
  Rule Builder test did); `explicit_indirect_conditions` True and no region reads (test_logic.py);
  slot data and seed keys; the retry path (a forced failure undoes and retries).
- **UT** (test_ut.py): every ER mix rebuilds the room from `er.pairs` and matches it sphere by sphere;
  deferred mode: only the start place before anything is found, then each found doorway connects its
  pair (and the reverse when coupled); refusals of rooms this world can't rebuild.
- **Tracker** (test_tracker.py, the pack's tests): the reference evaluator and the pack's Lua equal the
  world at every sphere with every pair connected, and with hand-made found sets; the export's `er`
  shape; the size budget.
- **Batches** (apworld/tools/ap_batch.sh, ap_batch_check.py): {hub, open world} x {world_map, dungeons,
  all_doorways} x {coupled, uncoupled} x the option mixes (bench, teleporter off, expel_progression
  off, the cave, each goal, a hell seed), 96 seeds each at release: every seed generates and every
  multiworld is beatable; the retry counts reported. **The identity batch**: with the randomizer off,
  the three release sets are byte for byte 0.2.2's (after the item-name changes, which er-world
  separates: same ids, same placements).
- **The proof mode (er-proof).** Two tools on the port's headless sweep (apworld/tools/ap_late_proof.py's
  machinery, which follows real transfers and so applies a seed's door table and spot swaps):
  1. *Transitions*: for a generated seed, every moved doorway: start at its arrival state, fire it,
     check the landing (map and position) is the expected pair's, then fire the way back (coupled) and
     check the return. Seconds each; all of them for a sample of seeds per level and mode.
  2. *Reach*: the hub proof (hub state) and the late, teleporter and voyage proofs (open world) with an
     ER seed's door table: the locations each reaches must equal the vanilla proofs' (4.1's invariant,
     tested directly). Hours per run, so a few seeds per level and mode, run as systemd --user units
     (the RC's pattern), two game processes at most.
- **End to end:** apworld/tools/ap_e2e_hub.sh and ap_e2e_so2.sh with an ER seed against a real
  MultiServer: the port loads version 5, walks a moved spot, the found key appears in data storage
  (apworld/tools/ap_ds_read.py), UT (trackers/tools/ap_ut_check.py with an ER room, deferred and not) and
  ap_ut_connect.py's cases still pass.
- **Windows and Mac checks** at release (section 9): the port's ER seed on Windows (the user); the
  tracker side (UT, the pack) on the Mac.

---

## 8. Work breakdown

At most two agents at a time on this machine (the standing rule; game processes two each). Sizes:
S (under a day), M, L, XL; risk low / medium / high.

| Agent | Inputs | Outputs | Depends on | Size, risk |
|---|---|---|---|---|
| **itemnames** | data.py ITEMS (214 "?..." names), the game's item table, the Prima guide text (scratchpad/guide) | appraised names in data.py and the 17 location names that contain them, tests, a name map for the identity batch | none | M, low |
| **er-model** (W) | doors.json, field.json, tracker_maps.json, the proofs, ap_walkmap.py | a committed tool (apworld/tools/ap_er_model.py) and docs/apworld/er_model.json: the 40 spots, PA and landings, each landing point -> spot or fixed, the routine table, the open world's classes (walkmap both worlds, each terrain version), the Synard parking stores, two-spot places' inner connectivity | none | M, medium |
| **er-world** (W1: hub) | er_model.json, this plan | options, the region layer, `connect_entrances` with Generic ER, groups, `SO2Entrance`, retry and post-checks, slot data, seed v5 fields, spoiler, UT regen and deferred, the export's `er`, test_er.py | er-model W | L, medium |
| **er-port** (W) | seed_format.md's v5 section (written first with er-world) | seed v5, the routine swap, exit `to` rows (exist), the Synard re-park, the found key with the sidecar, unit tests (test_ap.cpp, test_ap_ds.cpp), logs | the seed fields | L, medium |
| **er-proof** (W) | a built port, generated ER seeds | the transition and reach tools, their runs (hub, then open world), an ER e2e | er-world W1, er-port W | M, medium |
| **er-trackers** | er_model.json, the export's `er`, the found key | tracker_maps format 1.2 (spots), pack 1.1.0 (labels, markers, colours, logic, the spoiler setting), UT map tab sections, the reference evaluator, tests | er-world W1 | M, low |
| **er-world** (W2: open world) | the classes | the open world's groups and checks | W1 proven | S, medium |
| **er-model** (I) | the port's field collision, the scripts | the walk table per room (a flood fill on each map's walk mesh: from each arrival, which doorways and check sites it reaches), the always-open components (open world and hub state), the open world's story rooms (chapter and step stores, scene rooms), the asymmetric `back` fixes | W done | L, **high** (new measuring tool) |
| **er-world / er-port / er-proof** (D, then A) | the I tables | per level: room regions, component groups, interior `to` rows (no new port mechanism), the found key for interior doorways, replays | er-model I | M each, high for A |
| **er-docs** (sonnet) | everything above | seed_format.md (v5, `er`), apworld.md (an ER section replacing "Entrance shuffle (later)"), data_storage.md (the key), tracker docs, the player guide (setup_en.md, the game page), the wiki, release notes 0.3.0 / 0.04.00 / pack 1.1.0 | each stage | M, low |
| **batches** (sonnet) | a stage's world | the batch runs and their reports | each stage | S each, low |

**Order** (two at a time):
1. itemnames || er-model W.
2. er-world W1 || er-port W (they agree the v5 fields in seed_format.md on day one).
3. er-proof W || er-trackers.
4. er-world W2 || er-model I.
5. er-world D || er-port D; then er-proof D || batches.
6. The same for A; er-docs runs alongside from stage 2 (it is light).

**What "done" means for each stage:**
- **W (`world_map`)**: hub and open world, coupled and uncoupled; test_er.py and the UT / tracker tests
  green; the batches beatable with retry rates reported; every spot of sample seeds replayed (in and
  back out); the reach proofs equal the vanilla ones for a hub seed and an open-world seed per game;
  an ER e2e with the found key; the identity batch with the randomizer off.
- **D (`dungeons`)**: the same, with the walk table and components in; every moved dungeon doorway of
  sample seeds replayed; the reach proofs equal.
- **A (`all_doorways`)**: the same for towns; if open world's town replays find story breakage that
  the fixed rules don't cover, A ships hub-only or stays out of 0.3.0 (question 3), the user's call.

---

## 9. Release plan

- **Versions:** so2.apworld **0.3.0** (archipelago.json `world_version`, `APWORLD_VERSION`); the port
  **0.04.00** (reads seed versions 1-5); the PopTracker pack **1.1.0** (tracker maps format 1.2).
  `ut.LOGIC_VERSION` 2.
- **Together:** an ER seed needs port 0.04.00, its trackers need so2.apworld 0.3.0 (UT) and pack 1.1.0.
  A 0.3.0 seed with the randomizer off is version 1-4 and still plays on port 0.03.00 (the release
  notes say so). Universal Tracker itself is unchanged (tracker.apworld 0.3.6).
- **Release checks:** the RC (as 0.03.00's: a systemd --user unit, docs/release/release_checklist.md
  section 12's commands with ER added: the batches, the identity batch, UT, the pack's smoke test, the
  e2e), the user's Windows check (an ER seed played, the found key, UT and the pack on Windows), the
  Mac's tracker-side check.
- **The macOS build:** ships with port 0.04.00 (the user, 2026-10-28; merged to main as e210ebd). Before: the Mac work is on the `macos` branch; whether port
  0.04.00 can ship a macOS build alongside depends on that branch's state at release (question 14).

---

## 10. Risks, ranked

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| 1 | **Story breakage in moved interiors (open world, `dungeons` / `all_doorways`)**: a scene in a room expects the party to have passed its original neighbours (flags set on the way), or a room's scene fires from an unexpected side | high / high | fix story rooms' doorways (chapter and step stores, scenes) in open world; components; er-proof replays; hub first; `all_doorways` experimental, and hub-only or later if replays find more (question 3) |
| 2 | **Within-room walking**: an arrival on the wrong side of a ledge, a switch or a puzzle, so a doorway or chest of the room can't be reached from it | medium / high | er-model's walk table; rooms that aren't one piece from every arrival are split into regions or their doorways fixed; replays |
| 3 | **World-map side effects**: routine stores, the Synard's parking, voyages through world-map points | medium / medium | the routine swap (5.2), the re-park (5.6), voyage exits fixed (5.3); every spot replayed |
| 4 | **Size of the job** (three levels x two modes x coupling in one release) | high / medium | strict staging; `world_map` alone is a complete feature; each level ships only when its "done" holds |
| 5 | **The classes estimate is wrong** (a continent split by terrain or by chapter) | medium / medium | walkmap per world and terrain version; the reach proofs per mode catch a wrong class |
| 6 | **Generic ER failures in small classes** | medium / low | bounded retry with undo; batch-reported rates; a class that fails often is fixed |
| 7 | **Tracker load**: the export over its budget, PopTracker with hundreds more entrances | medium / low | measured budget, compact export; the pack's tests and smoke test |
| 8 | **Uncoupled traps** (a one-way loop) | low / high | strongly connected classes (4.6); the Synard re-park |
| 9 | **Compatibility**: an older port playing an ER seed, an older world tracking an ER room | low / high | seed version 5; `logic_version` 2; refusals with messages |
| 10 | **ER off changes** (byte identity lost by the region layer) | low / medium | the region layer only exists with the randomizer on; the identity batch |

---

## 11. Open questions for the user (each with a recommendation)

1. **Names.** `entrance_randomizer: off / world_map / dungeons / all_doorways` and `entrance_coupling:
   coupled / uncoupled`; entrances named for people with the doorway id kept for the trackers, e.g.
   "Expel map: Town of Salva entrance (43)" and "Town of Salva (43): door to the world map [2824838]".
   *Recommendation:* keep the option names; use those entrance name shapes.
2. **Mark `all_doorways` experimental** in 0.3.0 (the YAML text and the player guide). *Recommendation:*
   yes.
3. **If `all_doorways` can't be proven in open world in time**: ship it hub-only, or hold it to 0.3.1.
   *Recommendation:* decide when stage A's replays are in; hub-only is the likely fallback.
4. **Mixing the worlds.** Later for open world; for hub mode it looks nearly free (4.8).
   *Recommendation:* add `entrance_worlds: mixed` for hub mode in 0.3.0 if er-proof passes after stage
   W, else 0.3.1.
5. **Open world shuffles within each continent or island** in 0.3.0 (whole-world later, 4.8).
   *Recommendation:* yes; it keeps the story's ships and the El voyage meaningful and the logic proven.
6. **Private Action entrances**: stay pointing at their original town (first build), or follow their
   walk-in twin (the destination town's PA; no PA where the destination has none). *Recommendation:*
   original town in 0.3.0; follow-the-twin later, after er-model compares each PA's registration
   conditions.
7. **Fun City's spot shuffled** (the old design note kept it). *Recommendation:* shuffle it (3.7);
   Fienal's spot stays fixed.
8. **A spot refused for a missing pass counts as found** for the trackers (its banner names the
   destination's pass). *Recommendation:* yes.
9. **Item names in 0.3.0**: the 214 items named "?MACHINE (286)" etc. get their appraised names (and the
   17 location names that contain them). *Recommendation:* yes, in 0.3.0, as its own small agent first.
10. **The colour of a door not found yet** in the pack. *Recommendation:* Inspect (blue): story
    awareness, its only other user, never runs with ER.
11. **The UT bug report** (UT 0.3.6 drops the connection when Auto Tab is off before the first connect:
    `load_map(None)` returns early, so VisualTracker has no `location_icons` when
    `update_location_icon_widgets` runs; docs/handoff/utcrash.md). *Recommendation:* report it upstream
    (FarisTheAncient/Archipelago) from your account with trackers/tools/ap_ut_connect.py's `auto-tab-off`
    case as the reproduction; an agent can draft the text for you to post.
12. **The open upstream Archipelago PR for so2.** *Recommendation:* bring it to the released 0.2.2 now
    and keep entrance randomization for a follow-up PR after 0.3.0 has shipped and settled: a smaller
    diff to review, and Generic ER worlds get extra scrutiny.
13. **Seed format 5 only for ER seeds**, so non-ER 0.3.0 seeds still play on port 0.03.00.
    *Recommendation:* yes.
14. **A macOS build with port 0.04.00?** **Decided (the user, 2026-10-28): yes, macOS ships with 0.04.00.** Depends on the `macos` branch; just noting it.
15. **The found key's name and shape**: `so2_entrances_{team}_{slot}` (as reserved), an object doorway
    -> doorway written with `update`, kept in the sidecar; the pack's provisional `so2_doors_*` and
    slot data `door_pairs` are replaced. *Recommendation:* yes.
16. **Port version 0.04.00** for the ER port. *Recommendation:* yes.

---

## 12. Other apworld updates (not ER)

- **Item names** (question 9), if not folded into 0.3.0's ER work: its own agent either way.
- **Tooling:** apworld/tools/ap_test_server.sh:29 tests `[ -x "$ap/python/cpython-3.13"*/bin/python3.13 ]`;
  when the glob matches two directories, `[` gets too many arguments, fails, and the script runs
  `uv python install 3.13` every time. Fix: loop over the matches (or `compgen -G`) and test each.
- **The port's other-seed sidecar reconnect** (docs/handoff/loadsend.md "The reconnect path after
  another seed's or slot's sidecar"): only with `SS_AP_ANY_SAVE=1` (headless tests), a loaded sidecar
  of another seed or slot becomes the state the next `kConnected` sends. Fix: don't adopt a state whose
  seed / slot isn't the seed file's (or don't send it at connect).
- **Small follow-ups from docs/plans/next.md:** setup's notice pushes "Render tracker maps for
  PopTracker" below the fold at 125 % scaling (Archipelago off); the bench join banner "joined the
  party  bench" (no parentheses in the game's font, ap.cpp ~1766); Party Order's refusal to bench the
  hero has no text; docs/images/setup-archipelago.png still shows v0.02.50; a double-clicked Windows
  run's "stopped after N vblanks" isn't in shattered-stars.log; the --debug console's title is the
  exe's path; setup's font has no CJK glyphs; not yet covered on Windows: a gamepad with the Esc prompt,
  display scaling above 100 %.

---

## Appendix A: the 66 area groups (tracker_maps.json `room_groups`)

Class by the rule of 3.6; rooms and doorways from the layout; "dungeon" names the hub dungeon.

| World | Group | Rooms | Doorways | Class |
|---|---|---|---|---|
| Expel | Alen's Mansion | 7 | 15 | town |
| Expel | Arlia Village | 27 | 45 | town |
| Expel | Armory | 4 | 8 | town |
| Expel | Cave of Trials Levels 1-13, Treasure Vault, Weird Beast Arena (15 groups) | 128 | 318 | dungeon: Cave of Trials (fixed inside) |
| Expel | Cross Cave | 24 | 53 | dungeon |
| Expel | El Beach | 2 | 3 | town |
| Expel | Elder's House | 2 | 4 | town |
| Expel | Eluria Colony | 4 | 7 | town |
| Expel | Eluria Tower | 22 | 42 | dungeon (Berle's view covers 557-575: nearly all fixed) |
| Expel | Hoffmann Ruins | 46 | 113 | dungeon |
| Expel | King's Room | 4 | 9 | town |
| Expel | Lacour Castle Registration | 3 | 9 | town |
| Expel | Lacour Front Line | 5 | 11 | dungeon |
| Expel | Lacour meeting room | 1 | 1 | dungeon: Lacour Front Line |
| Expel | Lasguss Mountain | 5 | 10 | dungeon |
| Expel | Mars Village | 26 | 52 | mixed: the village (town) and the Heraldry Forest 456-466 (dungeon: the Mars dungeon) |
| Expel | Meeting Room | 5 | 10 | dungeon: Lacour Front Line (story rooms) |
| Expel | Mountain Palace | 33 | 68 | dungeon |
| Expel | Port Hilton | 29 | 66 | town |
| Expel | Port of Clik | 18 | 34 | town |
| Expel | Rena's Room | 1 | 2 | town |
| Expel | Rosalia's Room | 1 | 1 | town |
| Expel | Salva Drift | 11 | 27 | dungeon |
| Expel | Sanctuary of Linga | 11 | 37 | dungeon |
| Expel | Shingo Forest | 7 | 9 | town by the rule; recommended dungeon (3.6) |
| Expel | Ship to El | 1 | 0 | town (a scene map) |
| Expel | Tavern Stone | 1 | 1 | town |
| Expel | The Prince's Chambers | 7 | 16 | town |
| Expel | Town of Cross | 21 | 51 | town |
| Expel | Town of Herlie | 15 | 35 | town |
| Expel | Town of Lacour | 19 | 43 | town |
| Expel | Town of Linga | 27 | 58 | town |
| Expel | Town of Salva | 12 | 32 | mixed: the town, and 63 (Salva Drift) |
| Expel | Zand's Mansion | 10 | 24 | dungeon |
| Nede | Armlock | 10 | 19 | town |
| Nede | Cave of Red Crystal | 6 | 13 | dungeon |
| Nede | Cavern of the Red Crystal | 1 | 4 | dungeon: Cave of Red Crystal (Arcmene's chamber 417) |
| Nede | Central City | 32 | 47 | town |
| Nede | Field of Courage | 14 | 41 | dungeon |
| Nede | Field of Intelligence | 9 | 27 | dungeon |
| Nede | Field of Love | 8 | 18 | dungeon |
| Nede | Field of Power | 27 | 64 | mixed: the Field (21 maps) and the story maps 30, 620-623, 625 (fixed) |
| Nede | Fienal | 40 | 96 | dungeon (fixed) |
| Nede | Fun City | 29 | 68 | town |
| Nede | Giveaway | 23 | 49 | town |
| Nede | Heraldry Weapons Laboratory | 16 | 27 | dungeon: Portal of Sealing (fixed) |
| Nede | L'Aqua | 12 | 28 | town |
| Nede | Mars (the Gallery, 463) | 1 | 2 | dungeon: the Mars dungeon (Azamgil's chamber) |
| Nede | Mihne Cavern | 7 | 15 | dungeon |
| Nede | Noel's House | 3 | 6 | town |
| Nede | North City | 14 | 31 | mixed: the city, 270 (Synard Plant), 593 (Field of Love) |
| Nede | Synard Plant | 3 | 6 | mixed: 269 / 270 (dungeon), 263 (the Synard Office, town) |

Counts: Expel 26 dungeon groups (15 of them the cave's), 20 town, 2 mixed; Nede 9 / 6 / 3.

## Appendix B: the 40 walk-in spots

Doorway id = the lowest door id of the routine. "Variants": the routine sends the party to different
maps or positions by story state (the routine swap keeps them). PA: the town's Private Action
entrance maps (fixed). Fixed spots in bold.

| World | Doorway | Routine | Place | Entry map(s) | Variants | PA |
|---|---|---|---|---|---|---|
| Expel | 59576065 | 0x3bf8 | Arlia Village | 17 | | 19 |
| Expel | 59576094 | 0x3c6c | Town of Salva | 43 | | 60 / 45 |
| Expel | 59576123 | 0x3ce0 | Town of Salva | 44 | | (as above) |
| Expel | 59576152 | 0x3d54 | Mars Village | 124 | | 125 / 127 |
| Expel | 59576181 | 0x3dc8 | Mars Village | 126 | | (as above) |
| Expel | 59576210 | 0x3e3c | Town of Herlie | 144 | | 145 |
| Expel | 59576242 | 0x3ebc | Lasguss Mountain | 382 | | |
| Expel | 59576259 | 0x3f00 | Lasguss Mountain | 381 | | |
| Expel | 59576276 | 0x3f44 | Mountain Palace | 355 | | |
| Expel | 59576293 | 0x3f88 | map 231 (Hoffmann's beach) | 231 | | |
| Expel | 59576310 | 0x3fcc | Hoffmann Ruins | 478 | | |
| Expel | 59576700 | 0x45c0 | Town of Cross | 74 / 87 | chapter 9 step 0 | 73 |
| Expel | **59576750** | 0x46ac | Port of Clik (old) | 107 | registered until chapter 14 | 112 |
| Expel | **59576779** | 0x4720 | Port of Clik (new) | 108 | registered from chapter 15 (shuffled in hub mode) | |
| Expel | 59576800 | 0x4774 | Port Hilton | 169 | | 171 |
| Expel | 59576837 | 0x47e8 | Town of Lacour | 211 / 214 | stores chapters 18 and 23; four doors | 214 |
| Expel | 59576967 | 0x4a10 | Town of Linga | 183 | | 209 |
| Expel | 59576996 | 0x4a84 | Sanctuary of Linga | 402 | | |
| Expel | 59577013 | 0x4ac8 | Cross Cave | 524 | | |
| Expel | 59577030 | 0x4b0c | Lacour Front Line | 438 | | |
| Expel | 59577638 | 0x548c | Eluria Tower | 557 | | |
| Expel | 59577655 | 0x54d0 | El Beach | 229 | | |
| Expel | 59577681 | 0x5514 | Eluria Colony | 232 | chapter 31 step 0 | |
| Expel | 59577733 | 0x55b8 | Cave of Trials | 704 | | |
| Nede | 68685555 | 0x3bc0 | Giveaway | 317 | | 318 |
| Nede | 68685593 | 0x3c34 | Town of Armlock | 306 / 307 | chapter 52 step 0 | 307 |
| Nede | 68685640 | 0x3d14 | Field of Intelligence | 549 | | |
| Nede | 68685657 | 0x3d58 | Mihne Cavern | 449 | | |
| Nede | 68685674 | 0x3d9c | L'Aqua | 686 | | |
| Nede | **68685691** | 0x3de0 | Fienal (map 687) | 687 | the Fienal requirement's gate (hub) | |
| Nede | 68685710 | 0x3e24 | the Gallery (Secret Charity / Fake Gallery) | 463 | | |
| Nede | 68685923 | 0x4180 | L'Aqua | 579 | | |
| Nede | 68685969 | 0x4238 | Central City | 235 | | 236 |
| Nede | 68685998 | 0x42ac | North City | 260 | | 261 |
| Nede | 68686027 | 0x4320 | Noel's House | 277 | | |
| Nede | 68686044 | 0x4364 | Cave of Red Crystal | 419 | | |
| Nede | 68686061 | 0x43a8 | Field of Courage | 629 | | |
| Nede | 68686143 | 0x43ec | Field of Power | 597 | | |
| Nede | 68686160 | 0x4534 | Fun City | 279 | | 279 |
| Nede | 68686412 | 0x4920 | Field of Love | 588 | | |

Also on the world maps, not shuffled: the ship landing routine 0x57d8 (Expel: 59578036 and 59578440 ->
Port Hilton 169, 59578174 -> Herlie 144, 59578306 -> map 231, each by its voyage's flag) and the 16
Private Action doors.

## Sources

docs/plans/next.md "Phase C"; docs/apworld/apworld.md ("Open world", "Hub mode", "Entrance shuffle
(later: a design note)", "Rules as data", "Trackers"); docs/apworld/seed_format.md ("Phase 4", "The
version rule", "The tracker export", "Hub"); docs/apworld/data_storage.md ("Reserved"); docs/trackers/
tracker_maps.md ("Rooms", "Door ids", "For the pack (part 2)"); docs/trackers/poptracker.md ("Rooms and
doors"); apworld/so2/ (world.py, regions.py, rules.py, hub.py, story.py, ut.py, logic.py, data.py);
port/src/game/ap/ap.cpp (`on_map_transfer` ~3285, `door_shut_by` ~3256, `publish_tracker_keys` ~2396);
Archipelago 0.6.8's entrance_rando.py and "docs/entrance randomization.md" (scratchpad/ap/Archipelago);
Universal Tracker 0.3.6's docs/apworld-integration.md "Deferred Entrances" and docs/map-integration.md
(scratchpad/ap_ut/tracker.apworld).
