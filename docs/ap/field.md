# Archipelago: the field side

Phase 1 of [the Archipelago plan](../archipelago_plan.md) for the field. This page covers:
- the location tables read from the discs: chests, scripted gifts, prize tables, Fol,
  recruitments, shops and Private Actions;
- the story order, the point of no return and the regions;
- the location ids;
- the hooks the port offers for checks and placements.

The battle side (bosses, the goal, drops, DeathLink) is in [battle.md](battle.md). Its tool,
`tools/ap_battles.py`, is imported by this one. The APWorld built on these tables is in
[apworld.md](apworld.md).

## The tool: `tools/ap_extract.py`

    tools/ap_extract.py DISC1.cue DISC2.cue --no-battles -o docs/ap/field.json
    tools/ap_extract.py DISC1.cue DISC2.cue --summary
    tools/ap_extract.py DISC1.cue DISC2.cue --full -o FILE    (also every transfer)

Options:
- **Battle tables.** Without `--no-battles`, the output also holds `ap_battles.build()`'s
  tables under `battles`. `docs/ap/battles.json` already holds them, so the committed file
  leaves them out.
- **Python.** `ap_extract.build([disc1, disc2])` returns the same dict.
- **Content.** Only numbers and names (items, characters) are written, no dialogue.
- **Speed.** It takes about a minute per disc.
- **Disc 2.** All 820 map scripts are byte-identical on the two discs (`disc2` in the
  output), so the tables are the same for both.

### How it reads the scripts

**Maps.** Map N's script is archive entry 0xC87 + N, section 1 (`so2_script.py`). 964 maps
have one. A script shared by several maps is read once, and its records list the maps
(`maps`):
- the world script: maps 0-3 and 826;
- Expel's world-map chunks: 909-980;
- Energy Nede's world-map chunks: 1048-1110;
- world map 3's chunks: 1113-1114;
- one dungeon's rooms: 340, 345, 346, 352-354 and 356.

**The library.** Every map carries the same routines at different offsets.
- A routine runs from a `frame` to the next one. Its *shape* is a hash of its instructions,
  with call targets dropped and jump targets made relative.
- A shape found in a third of the maps or more is library code.
- Four library routines give things. `reward_routines` lists where each map's copy starts.

| Role | Arguments (pushed in order) | What it does |
|---|---|---|
| give | item, message | B 0x60 give_item and the "got it" message. An item number of 10000 and up instead sets script flag n - 10000, with the fanfare: key items are flags. |
| money | amount | ext 0xd4 add_money |
| join | character | ext 0xae party_add, then A 0x3f |
| chest | actor, opened flag, value, message | if the flag is clear: open the lid, then give (value >= 0) or money (-value), A 0x01 add_treasure_count, set the flag |

**Locations.** A location is one of these:
- a call into give, money or join with constant arguments, from a map's own code;
- an inline give_item, add_money or party_add there;
- a chest: B 0x6c chest_create registers the opened flag and a handler, and the handler
  pushes the four arguments and calls the chest routine.

A basic block with several calls is one event. Its `sites` list each call (`at`) and the push
that holds its value (`value_at`, `form`):
- **imm24.** A 24-bit push, so a patch can only put another item there.
- **word.** A full word, so any value fits.
- **inline.** The value is an argument of the instruction itself.

129 calls take a computed argument and are listed apart (`dynamic_reward_calls`).

**Offsets.** All offsets are byte offsets from the start of the script's code: the numbers
`so2_script.py dis` prints and call targets use. A `mods.cpp` word number is the offset / 4.

**Conditions.** Each location carries the conditions under which it runs.
- **`requires`:** the decompiled test of every `if` around it in its routine, as a string.
  `not (X)` marks an else branch. The names are:
  - `flag[n]`: a script flag;
  - `chapter`, `step`: the story globals;
  - `has(X)`: ext 0xb1, X is in the party;
  - `saved_party_has(X)`: B 0x2c, X was in the party when the Private Action began;
  - `party_count`: ext 0xd5;
  - `choice`: the option picked at ext 0xdc-0xe0;
  - `G[..]`, `local[..]`: other script globals and locals;
  - `?`: an expression the decompiler doesn't follow.
- **`via`:** how the routine is reached, two levels up. It lists the calls into it, each with
  the conditions at the call, and the instructions that register it as an event, with
  theirs. The registering instructions are:
  - trigger regions: thread_start, ext 0x75;
  - talk handlers: ext 0x70;
  - chests: B 0x6c;
  - others seen holding routine entries: ext 0xf3 / 0xcf / 0xd9 / 0x76 / 0x66, B 0x5c / 0x0a
    / 0x0e.

  In each case the routine's entry is its byte offset / 4.
- **`guard_flags`:** flags its routine both tests and sets to 1, which makes a one-time
  event. Scene flags many routines set (745, and the Private Action flags 2 and 4) are left
  out.
- **`window`, `missable`:**
  - `window` is the chapter range that the plain chapter tests among all those conditions
    allow.
  - `missable` means the story closes that window: there is an upper bound.
  - Expel as a whole closes at the point of no return (below), which this flag does not
    show.

**Story globals.**
- `chapter` is game_state +0 (script byte 0xa0). It is the main story counter: 3 at a new
  game as Claude, 4 as Rena, 60 at the end.
- `step` is game_state +4 (byte 0xa4), the progress inside a chapter, set back to 0 at each
  chapter change.
- **Flag 1** is the story_route: set in Claude's game, clear in Rena's.
- **Flags 2 and 4** are Private Action mode (below).

### The JSON (`docs/ap/field.json`)

One record per line in the record lists. The fields:

```
{
  "shared_scripts": [[maps...]], "field_maps": [...], "world_maps": {"1": "Expel", ...},
  "common_flags": [...], "library_routines": {shape: role}, "reward_routines": {map: {role: offset}},
  "location_id": {"base", "types", "formula"},
  "chests":        [{map, flag, handler, value_at, form, item|fol, name, word, expected, role,
                     registered_at, requires, via, window, missable, region, world, area,
                     location_id, location_type, sites: [{map, handler, value_at, form, item|fol,
                     word, expected, role, ...}], versions?, contents_differ?}],
  "retired_location_ids": [...],   (chest ids merged into another version: not locations)
                    (a chest whose value is 10000 + n gives key item flag n: "key_flag")
  "gifts":         [{map, offset, routine, block, items, item_names, flags_given,
                     sites: [{what, value, at, value_at, form, word, expected, role, location_id}],
                     requires, via, flags_tested, flags_set, guard_flags, story_tests, story_set,
                     kind: "event", window, missable, region, world, location_id,
                     private_action?, pa_partners?}],
  "fol_gifts":     [{... fol, payment?}],     (payment: a negative amount, a fee; no id)
  "prize_tables":  [{... kind: "prize_table"}],  (4+ reward blocks in one routine)
  "recruitments":  [{... joins, characters, join_kind, party_has_tests, party_removes,
                     party_count_test}],
  "characters":    {name: {sites: [{map, offset, kind, window, conditions}], restore_sites,
                     party_has_tests_near, party_cap_tests}},
  "dynamic_reward_calls": [{map, offset, role, args}],
  "shops":         {shop: {stock, names}},  "shop_sites": [{map, offset, shop, ...conditions}],
  "private_actions": [{map, routine, offers: [{registered_at, by, requires}], partners,
                     gives: {items, fol, joins, flags, story, item_names}, flags_tested_elsewhere,
                     window, missable, region, world, location_id?}],
  "private_action_triggers": [{map, offset, routine, offered_if, gives, runs_if}],
  "story":         {chapters: [{chapter, set_at: [{map, offset, requires, via, region, worlds}]}],
                    world_script_warp_chapters, disc_switch: [{map, offset, chapter, transfer_to}]},
  "regions":       [{id, maps, worlds, world_names, entered_from_world, left_to_world,
                     chapters_tested, chapters_stored}],
  "map_worlds":    {map: 1 | 2 | 0},   "one_way_transfers": [[from, to]],
  "late_maps":     [...],   (open at chapter 60 after the Final Save Point; records carry "late")
  "item_names":    {id: name},   "disc2": {...}
}
```

The default output keeps only the Private Actions that give something, store a story value
or set a flag another map tests. `--full` keeps all 741, adds `transfers` (every map transfer
with its conditions) and `story_points` (every store to the two story globals).

## What the discs hold (NA discs)

| Location type | Count | Expel / Energy Nede | Notes |
|---|---|---|---|
| Chests | 313 | 235 / 78 | One per opened flag: 338 handler sites, 25 flags on two versions of a map. 304 items, 7 Fol, 2 key items (`key_flag`: flag 68 in Cross Cave, map 547; flag 93 in the Cave of Trials, map 706). None sits in a closing story window. |
| Gifts (single events) | 109 | 88 / 21 | 95 items. 33 give a key-item flag (flags 66-91). 66 have a guard flag. 39 sit in a closing story window. |
| Fol gifts | 1 | 1 / 0 | Map 148: 100 Fol. Three more add_money calls are fees (maps 82 and 105: -10, -100). |
| Prize tables | 171 | 12 / 159 | Contests and exchanges, one entry per branch: 105 on map 688, 31 on 305, 23 on 295, 8 on 184. |
| Recruitments | 86 | 83 / 3 | Join sites, not counting the world script's add list (11 more). Of the 86, 32 restore the party after a split. |
| Shops | 64 stock lists | sites: 45 / 17 | Opened from 62 B 0x6d sites, which name 55 shop ids. |
| Private Actions | 741 events in 136 maps | 543 / 191 | 18 give items, 86 set flags other maps test, 1 joins a member. |

Notes:
- **The world split.** It is `world` (from `map_worlds`, below). Map 625 and the rooms
  around it (654-658, 687) are the hub between the two worlds: `world` 0.
- **Item names.** They come from the battle's string table (#1980, key 0xbb8 + item - 1).
  Names starting with `?` are the game's unappraised items.
- **Shops without a site.** Shops 14, 45, 51, 66, 79, 80 and 90-94 have stock but no B 0x6d
  site with a constant id. They are opened another way, to be found in Phase 2.

### Recruitments

Each join site has a kind (`join_kind`):
- **scene:** the story adds the member;
- **choice:** the join follows a yes / no choice;
- **restore:** a routine able to add four or more different members, which puts the party
  back together member by member after a split, under flags 758-769 ("member n was in the
  party": 758 + character number);
- **world_script:** the world script's add list, which stops at a full party
  (`party_count == 8`).

The first sites per character (`characters` has them all, with their conditions):

| Character | Where (map, offset) | Chapter | Conditions, in short |
|---|---|---|---|
| Claude / Rena | 9 (0x6ba0 / 0x6b94) | 4 | The other story_route joins at the start. Map 9 adds Claude when flag 1 is clear and Rena when it is set. |
| Celine | 74 (0xafdc) | 11 | Step 0. She rejoins on map 124 (chapter 15, flag 1 set, if she is not in the party) and on map 125 (chapter 15, flag 112). |
| Bowman | 208 (0x6bc4) | 22 | Steps 4000 / 5000. The scene's lines change with Ashton, Celine and Opera (party_has). |
| Dias | 130 (0x5150) | 15 | Rena's game only (flag 1 clear). Then 434 and 477 (chapter 19; 477 has branches for both games), 439 (26, Rena's game), and 229 (30, the El beach, Rena's game: flag 762 set, 765 clear). |
| Precis | 209 (0x8c90) | — | In Private Action mode (flag 2). G[0x2e0] == 1, flag 243, G[0x1e4] == 5000. |
| Ashton | 71 (0x7348) | — | A choice, when flag 113 is set and the map's event state G[0x1bc] == 100. Other sites at G[0x1bc] 998 / 4000. |
| Leon | 230 (0x6c58) | 30 | Claude's game (flag 1): the beach after the shipwreck. He travels with the party in chapter 23 (map 437). Map 229's Rena path needs a party of Claude and Rena alone (Findings below). |
| Opera | 374 (0x70f0) or 479 | 24 on 479 | On 374: a choice, with flag 141 and G[0x1be] == 2000. On 479: step 3000, with flag 141 and G[0x1be] == 3000; choice 0 there (0x7c28) adds Opera and Ernest together. |
| Ernest | 479 (0x7c28) | 24 | With Opera (choice 0). |
| Noel | 277 (0x6fe0) | 39 | Step 1000. |
| Chisato | 242 (0x6ca4) | 40-45 | A choice inside a Private Action: flag 2, saved_party_has(Chisato), G[0x214] == 4. |

- **No party cap at the sites.** No join site tests the party size; only the world script's
  add list does. party_add_member (script_party.cpp) fills an empty slot, and with no empty
  one it overwrites the first reserve slot.
- **So the exclusions come from flags.** Who can still join is decided by flags and the
  map's event globals set by earlier choices: the 758-769 "was in the party" flags, flag 113,
  flag 141, G[0x1bc] / G[0x1be]. The exact rules (who excludes whom) need those flags traced
  to where they are set. That is the first open item below.

### Private Actions

**How a PA starts.** A Private Action begins on the world map: the chunk scripts store
script flags 2 and 4 before the transfer into a town's PA entrance, such as maps 19, 45, 60,
73, 112, 125, 127, 145, 171, 209 and 214 on Expel.

**What a town registers.** The town's set-up then registers its PA events under `flag[2]`:
- talk handlers (ext 0x70);
- ext 0xf3;
- trigger regions (thread_start).

Most of them also sit under `saved_party_has(X)`, X being the partner who was with the
leader when the PA began.

**The records.** `private_actions` has one record per registered routine:
- `offers`: each registration with its conditions;
- `partners`;
- `window` and `missable`, from the chapter tests in its offers;
- `gives`: what the routine and the map routines it calls give or set.

**What they give.**
- **Items.** 18 give items. For example: map 18 (with Celine) gives jewelry and sets flags
  319 / 322; map 75 (with Precis) gives rings and a knife; map 317 (with Precis) gives the
  Nuclear Bomb; map 216 gives a Star Ruby or a Rainbow Diamond.
- **Flags.** 86 set flags that other maps test: relationships and story conditions. The
  `flags_tested_elsewhere` list is the starting point for the open question of which PAs feed
  recruitments or endings.
- **A member.** One adds a member: Chisato, map 242.
- **Missable.** 23 events are missable by chapter. The rest are gated by flags only, plus the
  point of no return.

**B 0x0e.** The 16 B 0x0e sites (`trigger_set_pa` in the catalog) bind a routine to a trigger
id. They are listed apart (`private_action_triggers`). They are not where PAs come from.

### Story order and the point of no return

**One store per chapter value.** Each chapter value is stored in one place, except for the
world script's warp list (`world_script_warp_chapters`). The script code there sets a chapter
and warps; it is reached through computed calls and is not play. So `story.chapters` is the
main story in order: where each chapter starts, and under what conditions.

| Chapters | Where they are set | World |
|---|---|---|
| 4-17 | 6, 9, 17/19, 53, 22, 17, 85, 90, 74, 547, 524, 108, 125, 144 | Expel |
| 18-31 | the Expel chunk script (18, 23), 223/224, 211, 183, 191, 169, 231, 422, 447, 444, 169, 548, 229/230 | Expel |
| 32-35 | 233 (32, 33), 625 (33), 576 (34), 625 (35) | Expel, then the hub |
| 36-37 | 618, 619 | Energy Nede |
| 38-60 | 246, 267, 277, 417, 247, (no store for 43-45), 556/596/604/642 (46), 248, 587, 657 (49, hub), 625 (50, hub), 686, 247, 313, 398, 315, 455, 315, 279, 307, 587 (60) | Energy Nede |

**The point of no return.** It is the disc switch, B 0x6a at map 625, offset 0x57a0 (word
5608). For locations it is only temporary: Virtual Expel reopens Expel at chapter 60
(Findings, below).
- It runs when chapter 34 step 0 calls its routine.
- The routine stores chapter 35, switches to disc 2 and transfers to map 618, which stores
  chapter 36.
- From there the story stays on Energy Nede: map 619 stores chapter 37 and moves on to map
  248 (the region of maps 235-259, where 246 stores chapter 38).

**The hub.** Map 625 is used in both periods:
- chapter 33 begins there (called at chapter 32), and that routine returns to the Expel world
  map (0x540c);
- the disc switch above;
- chapter 50 begins there, called at chapter 49 step 4000;
- in Private Action mode in Claude's game (flags 2 and 1, flag 152 clear), it leads to map 44,
  an Expel map.

What map 625 is, and how the party reaches it in each period, needs checking in play (open
item).

**Two story_routes.** Both games share this order. Flag 1 picks the branches: Rena's game
starts at chapter 4 on map 18, Claude's at chapter 3 on map 7.

### Regions

**How regions are built.**
- A region is the maps joined by doors: transfers that go both ways with no condition either
  way.
- One-way and conditional transfers are scenes and story moves (`one_way_transfers`).
- Each region lists the world maps it is entered from, through the chunk scripts, and left
  to (`map_transfer` to map 1 / 2). It also lists the chapter values its scripts test and
  store.

**The result.** 270 regions, 97 of them with doors or a world-map entrance.

**`map_worlds` gives every map a world:**
- its region's world map, when the region touches one only;
- otherwise the median chapter value its script tests and stores: below 36 is Expel, from 36
  on is Energy Nede;
- otherwise the nearest map with a world, over any transfer.

**The split this gives:**
- **Expel:** maps 1-234, 341-385, 402-412, 420-548, 557-576 and 698-825. 698-825 is the
  Cave of the Seven Stars, entered from Expel's world map at 704.
- **Energy Nede:** maps 235-339, 386-401, 413-419, 449-455, 463, 549-556, 577-624 and
  628-696.
- **Both (the hub):** 625, 654-658 and 687.

**Anomalies.**
- Map 463 is the far end of the dungeon 456-466, which starts at Linga (124, Expel). It
  tests chapter 15 only, and its door back to 462 is conditional. Yet it is entered from
  Energy Nede's chunk script and leaves to world map 2, near Nede's maps 597 / 606. Open
  item.
- The dungeon rooms 340, 345, 346, 352-354 and 356 share one script with no transfers.
  Their doors are not in the script, so they get no world.

**Names.** Region names (towns) are not read yet. The plan's list from memory still has to
be matched to these map numbers.

## Location ids

    id = 0x50320000 + (type << 21) + (map << 10) + index

- `type`: 1 chest, 2 gift, 3 Fol gift, 4 recruitment, 5 shop site, 6 Private Action (with an
  item or Fol), 7 prize-table entry. Types 8-10 carry no map (below).
- `map`: the record's map. A shared script uses its first map; maps run up to 1113, which
  fits the 11 bits.
- `index`: the map's locations of that type in offset order (chests by handler, then flag).

**Types without a map (2026-10-23).**

| Type | What | Id | Records |
|---|---|---|---|
| 8 | an enemy type's first drop | `0x50320000 + (8 << 21) + enemy type` (the stats record's +0 id, battles.json `enemy_types`) | `enemy_drops`: 89 types that random encounters meet and that drop something (Armedknight, type 1: 1362231297) |
| 9 | a boss fight | `0x50320000 + (9 << 21) + encounter` | `bosses`: 47 boss encounters (Alen-Tax, 191: 1364328639) |
| 10 | a shop slot (phase 4) | `0x50320000 + (10 << 21) + (shop << 8) + index` | none here: `tools/ap_world_tables.py` makes them from `shops` and `shop_sites` (550 slots of the 50 shops a script opens, minus 0x58 and the order shops 0x5a-0x5e; [apworld.md](apworld.md), "Shop slots") |

**Join sites.** A recruitment record's first join site keeps the record's id; every other join
site of the record takes the map's next free type-4 index (`sites[].location_id`), as gift
sites do. For example map 479 (Hoffmann Ruins): Opera at 0x6b8c is 1354333184 (the record's
id), and Ernest's site at 0x7c30 is 1354333187. The APWorld uses these per-site ids for the
join locations of `character_items`.

**The battle tables in field.json** (for drops and bosses):
- `bosses`: per encounter, its constant scripted sites (map, offset, routine, stage, param:
  0 means the story lets the party lose) with their conditions, window, `late`, region and
  area;
- `enemy_drops`: per enemy type, its drops (item, odds out of 256), the encounter groups it is
  in, the set-encounter sites of those groups and the world maps whose grids use them;
- `encounter_groups`: the field program's group table (0x80074fe0), groups 1-28 and 30;
- `world_map_groups`: the two 9 x 7 grids of the world maps (0x80074ef0 / 0x80074f70; +1 on
  terrain 5, +2 on Nede's terrains 10-12);
- `encounter_sites`: the 306 calls to a map's set-encounters library routine (rate to
  G[0x25], stage to G[0x12], group to G[0x14]).

The tool assigns 784 ids, all unique. The world script's records and the Fol payments get
none.

**Chests and gift sites (2026-10-23).**
- **A chest is one location per opened flag.** Its id is the lowest map version's id. The
  25 ids of the other versions are retired (`retired_location_ids`) and never reused.
- **A gift has one id per reward site** (`sites[].location_id`: 95 items, 5 Fol, 33 key-item
  flags, in 109 events). The first site keeps the event's id, which is also the event
  record's `location_id`. The 24 other sites take the next free indices of their map.
- **No id changed meaning.** Before, there were 785 ids; now 784 = 785 - 25 retired + 24
  new gift-site ids.

**The patch fields.** Every chest site and every item / Fol gift site carries what a
placeholder patch needs, so the generator never reads the discs:
- `word`: the mods.cpp word number;
- `expected`: the word on the disc;
- `role`: `item` or `fol`.

`word` is the push's own word for imm24 pushes and inline give_item (the item in bits 0-15).
It is the next word for word pushes and inline add_money. A chest's role is always `item`: a
Fol chest takes an item placeholder, and the chest routine takes its item branch.
`apworld/so2/patches.py` (the world agent's) turns sites and a placeholder into patch lines.
All 735 sites pass its checks.

**PA gifts.** The 18 item-giving Private Actions give through gift or prize records
already in the tables. The gift is then the location, tagged with `private_action` (the PA
routines) and `pa_partners`. 17 gift events and the 8 entries of map 184's prize table are
such gifts. The ids the tests and seed_format.md use (map 18: 1347569664 / 1347569665) are
unchanged.
Type 6 ids stay in `private_actions` for reference; the APWorld doesn't use them.

**Stability.** The ids stay stable as long as the discs and the tool's rules stay the same.
A new location type gets a new type number rather than renumbering (8, 9 and 10 were added
that way).

## Hooks in the port

What the port's Archipelago module already does (`engine/src/game/ap`; the seed file is
described in [seed_format.md](seed_format.md)):
- it checks chests by opened flag, when script_vm's flag_set turns the flag on
  (`ap::on_flag_set`);
- it applies the seed's patch lines through `mods.cpp`.

The table lists every hook point the field tables need, including those not wired yet.
- **Gift ids.** A gift event's `offset` is its first call. Every call it makes is in
  `sites[].at`. These are the offsets a seed's `check.offset` names.

| Location | Where | What is known there |
|---|---|---|
| Chest contents | the handler's push at `value_at` | `so2_mod.py chest` already writes the `mods.cpp` patch (`script MAP WORD EXPECTED VALUE`, word = value_at / 4): an item for an item (imm24), item or Fol for a Fol chest (word). |
| Chest check | A 0x01 add_treasure_count (script_table_a.cpp) | Only the chest routine calls it, after giving. Its locals hold the actor (L[0]), the opened flag (L[4]), the value (L[8]) and the message (L[0xc]). The location is (map, flag). |
| Gift / Fol / join check | B 0x60 give_item (script_table_b.cpp), ext 0xd4 add_money and ext 0xae party_add (script_ext.cpp) | If the running routine starts at one of the map's `reward_routines`, the location is the caller's call site. The frame's saved return offset (ctx->call_sp, `call` in script_vm.cpp) minus 4 is the record's `sites[].at`. Otherwise (an inline give) the instruction's own offset is. A gift called from the chest routine is the chest's. |
| Gift contents | the push at `sites[].value_at` | A mods patch, as for chests, when another of the player's own items goes there. For another player's item, the give_item hook skips the inventory_add and records the check. |
| Key-item flags | the give routine's flag branch (item >= 10000) | Story items. Leave them in place in story mode. |
| Recruitment | `party_add_member` (script_party.cpp) | Called from ext 0xae with the same frame rule. With character items, the hook records the check and skips the join; the receive path calls party_add_member. |
| Shops | B 0x6d menu_request_2 (shop id) | Stock comes from the overlay's table; `mods.cpp` `shop` lines already patch it (`so2_mod.py shop`). |
| Private Actions | the events under flag[2] | Check through the same give_item hook. "Mark as seen" for missables can hook the registration (ext 0x70 / 0xf3 / thread_start) of a listed routine. |
| The point of no return | B 0x6a (script_table_b.cpp, `disc_change_2`) | Where a "return to Expel" option or the logic's Expel cut-off hooks in. Phase 4 leaves it alone: the Expel teleporter opens Virtual Expel's route from chapter 36 instead ([seed_format.md](seed_format.md) "Phase 4"). |
| Doors | trigger fire (script_triggers.cpp) and the transfer itself (script_ext.cpp ext 0x7d / 0xa4 / 0xda) | Phase 4: a seed door (id = map * 65536 + offset / 4, `docs/ap/doors.json` from `tools/ap_doors.py`) can need a key (open world) or have a new target (entrance shuffle). |

Notes on these hooks:
- **Receiving and safe moments.** Received items wait for free play (field state 0, no
  script running), as the plan says.
- **The validator.** `--ap-validate` can load each map and check each record's `at` /
  `value_at` holds the instruction this file names, the way `mods.cpp` checks EXPECTED.

## Findings since Phase 1 (2026-10-23)

From the scripts, cross-checked with Prima's official guide (page numbers are Prima's).

**Who excludes whom.**
- **Bowman and Precis.** At chapter 22 step 3000, map 193 records who is in the party in
  flags 758-769 (758 + character). At step 3500, map 195 reads them:
  - with flag 763 set (Precis in the party), the story moves straight to step 5000;
  - otherwise a choice sets step 4000 (yes) or 5000 (no).

  Bowman joins on map 208 only at step 4000. So Precis in the party rules Bowman out, as
  Prima p. 8 / p. 10 say.
- **Ashton and Opera / Ernest.**
  - Ashton's quest flag 113 is set at Herlie (map 144), chapters 16-19, outside Private
    Actions, without Opera in the party.
  - His state is G[0x1bc]: map 70 sets 100; at map 71, yes sets 999 and no sets 200.
  - Opera's quest flag 141 is set at map 174 (Hilton tavern), chapters 20-22, only without
    Ashton (`has(Ashton) == 0`) and after flag 415. Flag 415 comes from a Private Action in
    Cross (map 77, G[0x2e0] == 1).
  - At Salva (maps 43 / 44) with Ashton in the party, flag 141 is cleared.

  So Ashton rules out Opera, and with her Ernest, as Prima p. 7 / p. 9 say.
- **Opera's and Ernest's steps.** Opera's quest runs on G[0x1be]: 1000 at map 174, 2000 at
  map 95, the Mountain Palace steps 2100 / 2200 / 3000 (maps 359 / 373 / 374, where map 374
  offers her a first join). At map 479 (Hoffmann Ruins, chapter 24 step 3000), choice 0 adds
  Opera and Ernest. Ernest never joins without Opera (Prima p. 11).
- **Precis.** A chain of Private Actions in Linga (G[0x2e0] == 1) runs on G[0x1e4]: 0 → 1000
  (map 208, Claude's game) or 2000 (map 183, Rena's) → 3000 (map 197) → 4000 (map 196). Then a
  choice at map 183 gives 5000 (yes, and flag 243) or 99999. She joins at map 209 (Prima p. 8:
  a PA in front of Bowman's pharmacy).
- **Dias: Rena's game only** (the user's play-through and Prima p. 13). A choice at map 130
  (Mars, chapter 15). After the shipwreck he is with Rena on the El beach (map 229, chapter
  30) when flag 762 is set: he was in the party on the ship (map 548).
- **Leon: Claude's game only** (the user's play-through and Prima p. 12). The logic treats
  him so.
  - He travels with the party for chapter 23 (map 437, with or without Bowman).
  - His join is on the beach after the shipwreck: map 230, offset 0x6c58 (routine 0x669c),
    chapter 30, under flag 1 (Claude's game). field.json's recruitments and
    `characters.Leon` carry that condition. Claude and Leon then walk to the Eluria colony.
  - One exception is written into the script, checked headless in the port (2026-10-23):
    - Map 548 (the ship; it stores chapter 30) takes members 2-9 out of the party into
      flags 758-769 and sets flag 765. In Rena's game it clears flag 765 again unless the
      party was Claude and Rena alone.
    - With flag 765, map 229 (the beach, Rena's game) plays its own scene and adds Leon at
      0x73dc (chapter 31).
    - The headless run: `--map 548`, chapter 29 step 30, party Claude + Rena, flag 1
      cleared. It ended with the party Leon, Rena and Claude in reserve.

    It is not the party-restore loop: it needs Rena to have turned down Celine, Bowman,
    Dias, Precis, Ashton, Opera and Ernest. The logic never relies on it.
- **Chisato.** G[0x214] steps: Central City (map 237) 1 → North City (260) 2 → Synard plant
  (271) 3 → Cavern of the Red Crystal (415, chapters 40-41) 4. Then a choice at the Nede
  Chronicle (map 242, chapters 40-45). Prima p. 15 matches: the business card in the Red
  Crystal cavern.
- **Noel, Celine.**
  - Noel joins in the story (map 277, chapter 39).
  - Celine's first join is at map 74 (chapter 11). Prima p. 6 says the permanent join is a
    choice after Cross Cave; maps 124 / 125 (chapter 15) hold her later joins.
  - [world agent, 2026-10-23] The scripts agree: Cross Cave's choice (map 524, chapter 13
    step 0, "Travel together with Celine?") keeps her (0) or sends her away (1). Map 131 stores
    flag 112 = party_has(Celine). Claude's game: map 124 (step 1000) adds her only if she is
    absent, map 462 (step 2400) sends her home, map 125 (step 3300) adds her back only with
    flag 112. Rena's game: map 126 (step 1000) takes her out, map 125 the same. So for the
    logic: with choice 0 the 125 site is reached and 124's join is skipped (she is in the
    party); with choice 1 only 124 is reached, and only in Claude's game; in Rena's game
    choice 1 leaves no Celine site at all. A received Celine keeps flag 112 in the port
    (seed_format.md "A received character stays recruited"), so 125 is always reached then.
- **The party cap.** No join site tests the party size. The exclusive pairs above keep the
  party at eight or fewer; Prima p. 11 says that with Ernest you choose among the last three.
- **Choices through the library.** Choices asked through the library's message-with-choice
  routine count as `choice` now (`CHOICE_SHAPES`). That adds Dias (map 130) to the choice
  joins.

**Shops without a script site.**
- Every B 0x6d has a constant id (62 sites).
- Shops 90-94 (0x5a-0x5e) are the order shops. The Familiar specialty opens them
  (menu_specialty.cpp: `half + 0x5A`, using item 0x1a2), not a script.
- Shops 14, 45, 51, 66, 79 and 80 have stock lists but nothing opens them: B 0x6d is the only
  script path to page 0xb.

**Place names.**
- `map_names` holds the banner each map shows on entry: ext 0xb9's string, cleaned
  (`clean_name`). There are 228 maps with one.
- `map_areas` gives 731 maps the name of their nearest world-map entrance over doors, or
  the nearest named map.
- Names for maps without a banner, which the nearest-name search misses or gets wrong. The
  tool sets them (`GUIDE_AREAS`), ahead of that search:
  - maps 229 / 230: "El Beach", the beach of the shipwreck, by the Eluria Colony (p. 74).
    The search had said Port Hilton.
  - map 548: "Ship to El", the ship out of Port Hilton (chapter 29; the scripts).
  - maps 340-380 (with the shared rooms 340...): the Mountain Palace, entered at map 355
    (p. 50);
  - maps 381-385: Lasguss Mountain (p. 56);
  - maps 647-685: Fienal (p. 114). Map 671 is its ninth floor, the Final Save Point (p. 127),
    three staircase rooms below the final boss on map 685.
  - maps 698-825: the Cave of Trials (it has banners).
- Areas the search got wrong by crossing into another cluster over a story transfer, or by
  taking a building's own banner for its area (`AREA_FIXES`, 2026-10-23, docs/handoff/
  apareas.md). Each is settled from the door table and agrees with
  docs/modding/reference/map_names.csv; they take precedence over the search and the banners:
  - 27, 29, 32, 38 (Rena's house and room) -> Arlia Village; 81 (Bar Mikado) and 83 (an Inn
    Kingdom Hotel room) -> Town of Cross: their doors only reach those towns;
  - 143 -> Zand's Mansion: the gate between Herlie's 142 and the mansion's 159;
  - 398, 399, 401 -> Heraldry Weapons Laboratory (they were Armlock): chapter-53 rooms between
    the laboratory's 400 / 395 and the Portal of Sealing 313;
  - 426, 427, 434, 467-477 -> Lacour Castle (they were Port Hilton): the castle corridor and
    the tournament's coliseum; Hilton is only the chapter-28 scene 422 -> 169 away;
  - 593-595 -> Field of Love (they were North City): the switch platforms between 591 / 592
    and the temple gate 595 -> 596;
  - 617-619 -> The Outer Wall (they were L'Aqua): the arrival 625 -> 618 -> 617 / 619 -> 248;
  - 698-825 -> Cave of Trials (they were "Cave of Trials Level 1", the first floor's banner).

  Not taken from the CSV: map 231 (the CSV says Eluria Colony) is its own world-map entrance,
  the beach where the Hilton ship lands for Hoffmann Ruins (chapters 23-27); map 28 has no
  doors. The APWorld's logic never reads an area (its entrance groups come from the doors);
  its location names keep the labels they were released with (tools/ap_world_tables.py
  `RELEASED_AREAS`), and doors.json's `key_group` is named by the target's banner first, so
  the groups keep their names.
- Names starting with `?` (`?JEWELRY`, `?GUARD`) are real: unappraised items. Prima gives the
  item in brackets, for example "?GUARD (Core Plate)".

**Virtual Expel (checked headless, 2026-10-23).** Expel's maps can be visited again before
the final boss, and their chests keep the opened flags from disc 1.
- **How it opens.** Saving at Fienal's Final Save Point (map 671) sets flag 730. The old man
  in Fun City's Fighting Arena (map 303, actor 25) then offers the trip; yes transfers to
  Arlia (map 18, 0x4dfc, under `flag[730] == 1 && flag[2] == 0`). Prima p. 130 describes
  the same: the Fun City old man, Virtual Arlia, then the Synard to fly anywhere on Expel.
  - It is not through map 625. Map 625's door to map 44 is a Private Action link in
    Claude's game (flags 2 and 1, flag 152 clear).
  - The headless runs:
    - `--map 303`, chapter 60, flag 730: talk K25 → map 18.
    - `--map 17` (Arlia's exit), chapter 60: map 1, the Expel world map.
    - Chests at chapter 60 with an AP seed on maps 18, 355 and 495: every chest opens and
      records its check. A chest whose flag was already set (2401, 2435, as if opened on
      disc 1) stays shut and records nothing.
  - Script: `scratchpad/enemy/ap2/run.sh` with `SS_SCRIPT_GLOBALS="a0=60,a4=0" SS_EVENT_SWEEP=1
    SS_SWEEP_FLAGS=730[,flags]`.
- **Which maps stay open.** `late_maps` (tools/ap_extract.py `late_maps`) lists the maps the
  party can still enter then: 708 maps.
  - The search starts from the world maps' entrances and from map 18, and follows every
    transfer whose conditions can hold at chapter 60 with flag 730 set, flag 753 clear (it
    is set after the final boss) and PA mode off.
  - Unknown flags and steps count as possible, so the search is optimistic about puzzles.
  - Every location record has `late`. Late locations number 308 of 313 chests and 105 of
    109 gift events.
  - The chests that are not late:
    - Port of Clik before its destruction: maps 117 / 119, four chests. The Expel world map
      enters Clik's old maps only while chapter <= 14.
    - The Heraldry Weapons Laboratory (map 387): one chest.
  - The gift events that are not late: Fun City map 301 (two), map 448 and map 556.
- **For the logic.** The disc switch is no longer a point of no return for Expel's
  locations. Only the locations that are not `late` (and those missable by their own
  conditions) can be lost.

**What the story needs from the inventory: nothing outside the key-item flags.** The scripts'
item tests (has_item, item_count, set_item_count with a constant item, and the library's
check routine) fall in four groups, none of them on the main story's path:
- the Cross tournament's sponsor gear, maps 468 / 469 (chapter 19): what the sponsor lent is
  taken back;
- a Herlie Private Action (map 145) testing the Seventh Ray;
- the Cave of Trials puzzles (Red Lotus Gem, Cracked Gem; maps 751 / 764);
- the key-item flags (10000 + n).

So shuffling regular items never blocks the story. One side effect to note: an item the
player receives that matches the chosen sponsor's gear (for example a Walloon Sword with
sponsor flag 279) is taken at the tournament's end, as the lent one would be.

**Chests by opened flag** (done 2026-10-23).
- The 338 chest handlers hold 313 distinct opened flags. 25 flags appear on two maps: the
  versions of one place, such as maps 18 / 21 (Arlia), 38 / 40 (Rena's room) and 507 / 508
  (Hoffmann Ruins).
- **One location per flag.** `chests` has one record per flag. The lowest map's record
  stands for it and keeps its id, and `sites` lists every version's handler push. A seed
  patches the placeholder into every site, so the chest pays the same item whichever version
  is opened. The other 25 ids are retired (`retired_location_ids`).
- **Flag 2459 (Hoffmann Ruins).** Map 507 pays Peep Half (item 107) and map 508 the Walloon
  Sword (493).
  - Map 506 and map 510 lead to 507 while flag 149 is clear, and to 508 once it is set.
  - Flag 149 is set on map 509, the room beyond 507, by its switch routine (0x3dd4, called
    with 0x1fc and 0x95). The other Hoffmann switches set flags 145-151 the same way.
  - So the vanilla game pays Peep Half when the chest is opened on the way in, and the
    Walloon Sword when it is first opened after the switch. The flag is shared, so only one
    of them is ever given.
  - The location takes map 507's item (Peep Half) for the item pool: it is the version the
    player meets first. The record has `contents_differ`.

**Guide treasure lists.** A pass over Prima's treasure boxes finds no chest of ours missing
from the guide, apart from OCR noise. Arlia (p. 26) matches item for item. The items the
guide lists that no chest, gift or Private Action of ours gives are shop stock or drops
(Reverse Doll, Core Plate) rather than chests.

**Private Action flags that feed the story.** Of the 99 flags PAs set, 17 gate a join, a
gift, a shop or a chapter store elsewhere. For example:
- flag 409 (a Linga PA, map 183) gates Precis's join, the Meteorite gift (map 209) and shop
  47;
- flags 447 / 448 (a Linga PA with Bowman) open shop 47;
- flag 364 (a PA with Bowman, map 189) gates key item flag 73 (map 381);
- flag 430 (a Celine PA, map 167) gates the General's Ring (map 174).

Flag 247 (map 95) is registered in and out of PA mode, so it is not PA-only.

## Open items

1. ~~Chests and gifts per location.~~ Done: one chest per opened flag; one id per gift site
   (Location ids).
2. **The 129 computed reward calls.** For example, map 305's contest prizes.
3. **Map 463 and the hub map 625.** Check them in play. Virtual Expel is done (above).
4. ~~The Prima names.~~ Done (`GUIDE_AREAS`).
