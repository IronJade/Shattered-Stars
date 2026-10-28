# The PopTracker pack (`so2_shattered_stars`, pack 1.0.0)

A [PopTracker](https://github.com/black-sliver/PopTracker) pack for so2.apworld: Star Ocean: The
Second Story played on Shattered Stars with Archipelago. It connects to the room as a tracker,
shows what the slot has received and which of the room's locations are checked, on maps and
boards, and switches map tabs as the port moves between maps. The plan is
docs/plans/trackers.md (B4, decisions D2, D4, D5, D7); the map layout it shares with the port is
docs/ap/tracker_maps.md.

The pack is **generated** in this repo from the APWorld's own tables, so its ids, names and areas
cannot drift from the world: `tools/ap_poptracker.py` writes it, `tools/build_poptracker.sh` zips,
hashes and checks it, `tools/ap_poptracker_test.py` tests it. Its hand-written Lua lives in
`tools/poptracker/`. The build output (`dist/poptracker/`) is never committed.

Status (pack 1.0.0): built by B4a (the pack) and B4b (its logic). Each location's access goes
through one function, `so2_access` (scripts/logic.lua), a generic evaluator of the room's logic
export (`slot_data["tracker"]`, so2.apworld 0.2.2's: docs/ap/seed_format.md "The tracker export"),
so the pack shows exactly what the world's own rules say, for every option and seed, with no game
logic of its own (decision D2). Rooms from 0.2.0 and 0.2.1 have no export: a checklist, said in the
banner. See "Logic".

## For players

1. Install PopTracker 0.36.0 or later, and put `so2_shattered_stars_1.0.0.zip` in its `packs`
   folder (or let PopTracker install and update it once it is listed: D7).
2. Load the pack: variant **Map Tracker** (maps, boards and items) or **Items Only**.
3. Click "AP" in the menu and connect to the room (host:port, the slot name, the password). The
   pack only listens: it sends no checks and no hints.
4. Optional, the real maps: run `shattered-stars --render-tracker-maps` once (docs/ap/tracker_maps.md
   "The port's command"). It draws every map from **your own disc** into PopTracker's override
   folder for this pack (`<home or Documents>/PopTracker/user-override/so2_shattered_stars/`), at
   the same paths and sizes as the pack's placeholders, so the check squares land on the real
   chests, NPCs and doors. Nothing from the disc ships with the pack (decision D5).

What it shows:
- **Maps.** Expel and Energy Nede (their world map, then a tab per area group, then a tab per area
  inside a group of several), Fienal, and the Cave of Trials (a tab per level). A square per spot
  on an area map: checks at the same spot (one NPC's several gifts, the Cave's reused chest rooms)
  share a square, one entry per check. The world maps have a square per area group (an area key's
  entrance) that sums up the place checks behind it.
- **Rooms and doors.** Each area's tab has its overview (the area map, titled as the area) and then
  a tab per room (every field map the area's doors reach: 794 rooms), titled by the map's number, in
  door order from the entrance. A room shows its checks (the same squares as the overview) and a
  **door marker** (a yellow diamond) per doorway, lettered A, B, C... on the image. Beside the map, the
  **door panel** lists where each doorway leads, live: "A → 18 north: fountain square" ("*" marks a
  destination that only happens in some story states; "(+53* 46)" the doorway's other destinations).
  **Clicking a door** (hover its diamond, click "Door A" in the popup) or **its panel line** opens the
  destination room's tab; right-clicking a panel line steps through a doorway's other destinations.
  With Auto tab on, the tabs follow the party room by room. Doors are not checks: never counted,
  never cleared. "Rooms and doors" below.
- **Boards** for the checks that are no place: Shops (a cell per shop), Item Creation (per
  specialty, in cells of up to 25), Killer Moves (per member), Guilds and Learning (skill guilds,
  learn points per member) and Enemy Drops (per area).
- **Only the room's locations.** On connect the pack reads which locations the room has (the
  server's missing and checked ones) and hides the rest; before connecting everything shows.
- **Items** (left panel): area keys (Expel, Energy Nede), hub mode's dungeon keys, characters (a
  member outside the room's pool is badged "out"; so is a dungeon key the hub room doesn't use),
  traps received (counts), skills and moves (skillsanity), and **Room**: the room's options from its
  slot data (mode, goal, route, shops, missables, drops, skillsanity, killer moves, Fienal
  requirement, crafts, bosses, Cave of Trials, teleporter, bench, hell seed).
- **Logic.** With a room from so2.apworld 0.2.2 or later, a check is green (reachable) when the
  world's own rules say it is in logic with what the slot has received (and the items clicked in
  the panel), and red when not: the same as Universal Tracker. A room from 0.2.0 / 0.2.1 has no
  logic in it: every check shows as reachable (a checklist).
- **Story awareness** (story mode, with port 0.03.00 connected to the room): story mode's stages need
  no items, so the logic alone puts nearly every check in reach from the start. The pack follows how
  far the story has got (the port's `so2_story_*` key) and shows a check in logic by the items but
  **later in the story** in blue (PopTracker's Inspect: "seen, not yet to be had"); green is what the
  story has reached, red what needs items. It follows the chapter live, also backwards when an
  earlier save is loaded. Before the port has reported (or with an older port), and in open world
  and hub mode, everything is as the logic alone says. The **Chapter** item shows the chapter;
  clicking it sets one by hand (left +1, right -1; 0 = none), for play without the port connected.
  The **Story** item (on by default) turns story awareness off.
- **Status**: the logic banner at the top ("not connected", "logic unavailable for this room:
  checklist", "logic: from the room", or "logic needs a newer pack: checklist" for a room of a
  later world whose export this pack can't read), the story chapter the port reports, **Story**
  (story awareness, on by default) and **Auto tab** (on by default): with port 0.03.00 connected to
  the room, the map tabs follow the map the party is on.
- **Hints**: hinted locations in this world are highlighted (priority, avoid, ...).
- Checks reach the tracker through the server, so checks made while the port is offline show up
  once it reconnects and sends them.

## Pack structure

| Path | What | Made by |
|---|---|---|
| `manifest.json` | uid `so2_shattered_stars`, `package_version` 1.0.0, `game_name` "Star Ocean: The Second Story" (the AP game name), platform PC, author DreamGlyph Studios, variants `map` ("Map Tracker") and `items_only` ("Items Only"), each with the flag `ap` only (never `apmanual` / `aphintgame`), min and target PopTracker 0.36.0, `versions_url` | generator |
| `settings.json` | smooth scaling for items and maps (the badges and maps are drawn larger than shown) | generator |
| `items/items.json` | 287 items: 35 area keys, 18 dungeon keys, 12 characters, 5 traps (consumables), 46 skills, 148 moves (264 mapped to AP ids); the logic banner (`so2_logic_*`, progressive), the chapter (`so2_chapter`), story awareness (`so2_story_aware`), auto tab (`so2_auto_tab`), the refresh counter (`so2_rev`, in no layout); 18 settings (`opt_<key>` toggles, `opt_<key>_<value>` progressive stages) | generator |
| `locations/places.json` | region (Expel, Energy Nede, Fienal, Cave of Trials) -> area -> a location per dot (472 place checks, 408 dots) | generator |
| `locations/world.json` | "Expel World Map" / "Energy Nede World Map" -> a location per area group, whose sections are `ref`s to the place checks of the group's areas | generator |
| `locations/doors.json` | "Doors" -> a location per doorway ("<map> <letter>", a diamond per marker on `room_<map>`), one section "Door <letter>" (no AP location; access `^$so2_door_access|<doorway id>`): 1,775 | generator |
| `locations/boards.json` | Shops (52 cells, 550 checks), Item Creation (26, 553), Killer Moves (8, 133), Guilds and Learning (25, 194), Enemy Drops (19, 89) | generator |
| `maps/maps.json` | 66 area maps + 2 world maps (one per layout image, named after the image file) + 5 boards + 794 rooms (`room_<map>`, the room's `location_size`) | generator |
| `layouts/items.json` | the banner, the item tabs (Keys, Party, Skills, Moves per member, Room), `tracker_broadcast` (keys and characters) | generator |
| `layouts/tracker.json` | `tracker_default` of the map variant: the item column left, the map tabs right | generator |
| `items_only/layouts/tracker.json` | the Items Only variant's `tracker_default` (no maps) | generator |
| `scripts/init.lua`, `so2.lua`, `logic.lua`, `autotracking/archipelago.lua` | the hand-written Lua (below) | tools/poptracker/ |
| `scripts/pack_data.lua` | `SO2_PACK`, `SO2_SETTINGS` (slot key, code, values, default), `SO2_CHARACTERS`, `SO2_DUNGEON_KEYS`, `SO2_WORLD_MAPS` (world x/z -> pixels) | generator |
| `scripts/map_tabs.lua` | `SO2_MAP_TABS[field map] = {tab, tab, ...}`: 940 of map_names.csv's 964 maps (a room: its own tab) | generator |
| `scripts/doors_data.lua` | `SO2_ROOMS` (map -> name, tab path), `SO2_MAP_NAMES` (destinations that are no room), `SO2_DOORWAYS` (the vanilla door table: doorway id -> room, letter, panel item code, destinations {map, back, cond}), `SO2_ROOM_DOORS`, `SO2_DOOR_SECTIONS` (section full id -> doorway id) | generator |
| `scripts/autotracking/item_mapping.lua` | `ITEM_MAPPING[AP item id] = {{code, type}}` | generator |
| `scripts/autotracking/location_mapping.lua` | `LOCATION_MAPPING[AP location id] = {"@path/section"}`: every one of the data package's 1,991 locations | generator |
| `images/maps/*.png` | placeholders at the layout's paths and sizes (66 areas, 2 worlds) | generator |
| `images/rooms/*.png` | room placeholders at the layout's paths and sizes (794): the label strip and the door letter boxes where the port draws them | generator |
| `images/doors/*.png`, `images/status/you.png` | the door panel's bar, the door sections' icon; the player marker (Universal Tracker's world maps) | generator |
| `images/boards/*.png`, `images/items/*.png`, `images/status/*.png` | boards, item badges, the banner | generator |
| `images/icon.png`, `LICENSE` | the Shattered Stars icon (res/icon, original), the repo's MIT licence | copied |

Beside the pack folder the generator writes `ut_name_mapping.json` and `ut_tracker/` (below,
"Universal Tracker's map tab").

## The generator (`tools/ap_poptracker.py`)

    SKIP_REQUIREMENTS_UPDATE=1 scratchpad/ap/ap-venv/bin/python tools/ap_poptracker.py \
        [--out DIR] [--zip FILE] [--ap-dir DIR] [--versions-url URL] [--no-images]

It imports apworld/so2's table modules (`data`, `items`, `locations`, `skillsanity`, `options`) as
a bare package `so2`, without the package's `__init__` (the world class, which would load every
world). Archipelago's `BaseClasses` / `Options` come from `--ap-dir` (default the scratch 0.6.8 of
tools/ap_test_server.sh). Other inputs: docs/ap/tracker_maps.json (format 1, uid checked) and
docs/modding/reference/map_names.csv. It takes about 4 seconds.

Naming:
- **Item codes**: `key_<slug of the key>`, `dkey_<dungeon>`, `char_<member>`, `trap_<kind>`,
  `skill_<skill>`, `move_<member>_<move>`, `opt_<slot key>[_<value>]`, `so2_*` for the status
  items. The slug is tracker_maps.md's (lower case, apostrophes dropped, other runs -> `_`).
- **Locations and sections**: PopTracker separates a path with `/`, so a `/` in a name (five shop
  names, "Secret Charity/Fake Gallery") becomes `-`. A dot's location is named
  "<area>: <its first check>" (" (+N)" when it holds more), unique across the pack; its sections
  drop the area prefix ("Leather Armor chest"). Board cells: "<area>: shop N" (sections: the item),
  "Item Creation: <specialty> (k of n)", "Killer moves: <member>", "Skill Guild: <name>",
  "Learning: <member>", "Drops: <area>". Every location has exactly one section; section paths are
  in `LOCATION_MAPPING`.
- **Section rules**: access `["^$so2_access|<AP location id>"]`, visibility
  `["$so2_exists|<AP location id>"]`. Nothing else in the pack has rules.
- **Map tabs** (`map_tabs.lua`): a room switches to its own tab (area path + its number); a map drawn in an area image switches to that area's tab (a map in
  two images: the one map_names.csv names); then map_names.csv's area, the area of a location row on
  that map, the area group whose entrance maps hold it, and the world-map tab for the 'Expel' /
  'Energy Nede' maps (the Cave of Trials tab for its own). World maps 1 and 2 switch to their world
  map tab. 94 maps (system maps, cutscene copies, areas with no checks such as L'Aqua) switch
  nothing.

Regenerate when the world's locations, items or options change, when tracker_maps.json changes, or
when map_names.csv changes; the tests fail when the pack and the world disagree.

## Lua

All hand-written files are in `tools/poptracker/`, short and generic; the generator copies them.

- `init.lua`: loads pack_data, map_tabs, so2, logic; in the map variant doors_data and doors, then
  the maps and locations; the layouts; then the autotracker. `SO2_ITEMS_ONLY` is the Items Only
  variant.
- `doors.lua`: rooms and doors (next section).
- `so2.lua`: shared state `SO2` (`connected`, `exists` = the room's location ids, `received` = AP
  item id -> count from the server, `slot_data`, `story`), `so2_exists(id)` (the visibility rule),
  `so2_item_count(ap_item_id)` (the pack's item when mapped, so clicks count too; else what the
  server sent), `so2_refresh()` (bumps `so2_rev` so PopTracker re-runs the rules after a Lua-only
  change), `so2_set_logic_status(key)` (the banner), and a watch on every item code that calls
  `so2_logic_items_changed(code)`.
- `logic.lua`: the evaluator ("Logic", next).
- `autotracking/archipelago.lua`: the AP handlers.
  - **Clear** (each connect): resets every mapped section (count, highlight) and item, the chapter;
    sets the Room settings from the slot data (`SO2_SETTINGS`; a missing key: `absent`, else the
    option's default) and the "out" badges (`characters`, hub `dungeon_keys`); reads which
    locations the room has from `Archipelago.MissingLocations` + `CheckedLocations` into
    `SO2.exists` and clears the checked ones; calls `so2_logic_load(slot_data)`; then
    `SetNotify` + `Get` for `_read_hints_{team}_{slot}`, `so2_map_{team}_{slot}`,
    `so2_story_{team}_{slot}` (docs/ap/data_storage.md).
  - **Item**: by index (repeats ignored); counts every id in `SO2.received`; advances the mapped
    items (toggle on, consumable +1).
  - **Location**: clears its section (count 0, so a repeat changes nothing).
  - **Data storage** (Retrieved and SetReply): hints highlight this world's hinted sections; a new
    `map` in `so2_map_*` activates `SO2_MAP_TABS[map]` tab by tab with `UiHint("ActivateTab")`
    when Auto tab is on (position-only updates on a world map change nothing); `so2_story_*` sets
    `SO2.story` and the Chapter item and calls `so2_story_changed(story)` (logic.lua: the story
    view is redone and the rules re-run, also for a step-only change).

## Logic

**The seam.** Every section's access rule is `^$so2_access|<AP location id>`. The `^` makes PopTracker read the
return value as an **AccessibilityLevel** (`None`, `SequenceBreak`, `Normal`, ...), so an evaluator
can show out-of-logic-but-possible (yellow) as well as reachable and unreachable. The contracts,
all in `tools/poptracker/logic.lua`:

| Function | Called | Contract |
|---|---|---|
| `so2_logic_load(slot_data)` | by the Clear handler, once per connect, after `SO2.exists` and the items are set | read `slot_data.tracker` (B3's export; absent in rooms from 0.2.0 / 0.2.1 or a world without it); set `SO2_LOGIC.mode` and the banner: `so2_set_logic_status("on")` with logic, `"checklist"` without, `"newer"` for an export of a format the pack doesn't read |
| `so2_logic_items_changed(code)` | after any item changes (received, clicked, reset) | drop memoized results (keep it cheap: it runs once per changed item) |
| `so2_access(id)` | by PopTracker for each visible section, whenever an item changed | `id` is the AP location id as a **string**; return an `AccessibilityLevel` |
| `so2_story_changed(story)` | optional; on each `so2_story_*` value, after the Chapter item | story awareness: logic.lua drops its story view and calls `so2_refresh()` |

Inputs an evaluator has: `so2_item_count(ap_item_id)` (items by AP id, as the export names them),
`SO2.slot_data`, `SO2.exists`, `SO2.story`. If what `so2_access` returns changes without an item
change (an export loaded late, a story change), call `so2_refresh()` (the Clear handler calls it
after `so2_logic_load`). Don't change items inside `so2_access` (PopTracker warns about items
toggled during rule evaluation).

**The evaluator** (`tools/poptracker/logic.lua`, about 150 lines, no game knowledge). It is the
reference evaluator's algorithm (`tools/ap_tracker_logic.py`; docs/ap/seed_format.md "The tracker
export", "The evaluation") step for step:
- `so2_logic_load` indexes the export's location groups by AP id (`loc[id] = {region, rule, ...}`)
  when its `format` is one the pack reads (`SO2_LOGIC_FORMATS`: 1).
- On the first `so2_access` after an item change, one **sweep**: region 0 reached, no events; passes
  until one changes nothing (an entrance from a reached region whose rule holds reaches its region;
  an event in a reached region whose rule holds is collected once). Rule results are memoised per
  pass (`holds(v, k)`), and item counts per item state (`so2_item_count(ap_id)`: the pack's mapped
  item, so a click counts, else what the server sent); events count by name.
- `so2_access(id)` is then a lookup: `Normal` when the location's region is reached and its rule
  holds (the last pass's memo), else `None`. A location the export doesn't list, and every location
  without an export, is `Normal` (checklist).
- `so2_logic_items_changed` (every item change: received, clicked, reset) drops the result; the next
  `so2_access` sweeps again. A sweep is about fifty rule evaluations a pass and a few passes.
- Helpers for B6 and tests: `so2_logic_view()` (the current result: `reached` regions, `got`
  events), `so2_in_logic(id)` (true / false / nil), `so2_goal_in_logic()`.
- **Entrance randomizer (Phase C), already read:** entrances are named (`{name, from, to, rule}`),
  the export is the randomized graph, an entrance with `to` -1 (not connected yet: a deferred one)
  reaches nothing, and `so2_logic_connect(name, region)` connects it when the player finds it (from
  the slot data's pairings or the found-entrances data-storage key; it re-runs the rules). The
  "region" body (an indirect condition, CanReachRegion) is evaluated too. So such a room needs no
  new evaluator, only more data (docs/ap/seed_format.md "The tracker export").

It returns `Normal`, `Inspect` (story awareness, next) and `None`: the export knows what is in logic,
not what is possible out of logic, so there is no `SequenceBreak` (yellow).

**Story awareness** (story mode; the export's optional `story` key, docs/ap/seed_format.md "Story
awareness"; docs/ap/apworld.md "Story awareness" for where the points come from):
- **The position** (`so2_story_position()`): the Chapter item's count (the port's `so2_story_*` sets
  it; a click changes it), with the port's step while the port's chapter is the item's (else step
  0); Chapter 0 is no position.
- **The points reached** (`so2_story_stage()`): how many of `story.stages` ({name, chapter, step},
  compared as pairs) are at or before the position, at least 1 (a chapter before 3 is the story's
  start); nil (no story awareness) without the export's
  `story` key (open world, hub, rooms before 0.2.2), without a position, or with the **Story**
  setting item (`so2_story_aware`, a toggle, on by default) off.
- **A second sweep** (`so2_story_view()`, kept until the items or the points reached change): the
  same algorithm, but an event of `story.events` whose point isn't reached is not collected
  (the disc switch at point 13, the Final Save Point at 24, so the regions and rules behind them
  wait too); `so2_story_in_logic(id)`: the location's region reached, its rule holding, and its
  point (`story.locations`; unlisted: 1) reached.
- **`so2_access(id)`**: `None` when not in logic by the items; else `Inspect` when the story view
  says not yet ("later in the story"), else `Normal`. **Why Inspect**: it is PopTracker's level for
  "checkable but not collectible", drawn blue, distinct from red (red says items are missing,
  something to act on; a check later in the story needs nothing but playing on) and from yellow
  (`SequenceBreak` says it can be had now out of logic, which isn't so: the party can't get there
  yet). Hiding such checks (`None`) would lose the "you will get here" view the user asked for.
- A step-only change (chapter 60 step 3000 -> 4000, the Final Save Point) changes no item, so
  `so2_story_changed` re-runs the rules itself (`so2_refresh()`).

## Rooms and doors

docs/ap/tracker_maps.md "For the pack (part 2)" is the spec; the user's direction (2026-10-27): door
labels must be live, so that with the entrance randomizer (Phase C) the tracker draws each door's
real destination. So **no destination is drawn into any image**: a room's image carries only its own
number and name (the label strip) and a letter per doorway; the destinations are text the pack's Lua
sets at run time.

- **Rooms.** One PopTracker map per room of the layout (`room_<map>`, 794), the layout's image at its
  root path (`images/rooms/<map>.png`; the port renders the real background into the same file name
  and size). Each check also has a square on its room (`checks[].room`), so a room shows the same
  checks as its area's overview.
- **Tabs.** Area > Room: an area with rooms is itself tabbed: first its overview, titled as the area
  (so `ActivateTab(<area>)` also shows the overview), then a tab per room in `room_groups[].rooms`
  order (door distance from the entrance), **titled by the map number** ("18"): the label strip on
  the image and the door panel name the room, and number titles keep the busiest areas' tab bars at
  two rows (Hoffmann Ruins has 46 rooms; named titles took six to eight). A room group with no area
  image (L'Aqua) is a tab of rooms only under Energy Nede. Every room is in exactly one group, so a
  map has one tab path (`SO2_MAP_TABS`, `SO2_ROOMS[map].tabs`): door clicks and Auto tab both use it.
- **Door markers.** A location per doorway (`locations/doors.json`, "Doors/<map> <letter>"), a
  diamond at each of its markers (two trigger regions of one routine are one doorway), one section
  "Door <letter>" with a door icon. It is no Archipelago location: not in `LOCATION_MAPPING`, no AP
  id, not in UT's names, always visible. Its access rule `^$so2_door_access|<doorway id>` is its
  colour: **SequenceBreak (yellow)**, for every door (the user, 2026-10-27: "Yellow works for me").
  Why: a door is never a check, so it must not look like one, and PopTracker colours a location
  only by its state, from a fixed palette a pack can't change (there is no white): checks use
  Normal (green: a check to get), None (red: needs items), Inspect (blue: later in the story, story
  awareness), cleared (grey) and, at a square holding several checks, the mixed orange / teal /
  purple. SequenceBreak is the one state no check uses (`so2_access` returns only Normal, None and
  Inspect; `TestDoorsLua.test_door_colour_is_no_checks` keeps it that way), and a door location has
  one section, so it never mixes. Until 2026-10-27 a known door was Inspect (blue), next to story
  awareness's blue squares. **A door whose destination isn't known yet** (the entrance randomizer: a
  paired doorway not found, its panel line "A → ?") **is yellow too for now; its own colour is a
  Phase C decision**, to make with the entrance randomizer's design (`so2_door_destinations(id) ==
  nil` in `so2_door_access` is the one place to change). The logic export doesn't place maps in
  regions yet, so a door can't be coloured by its destination's reachability either; when Phase C
  adds a region -> map hint, `so2_door_access` is the one place to do that too.
- **The door panel.** A Lua item per doorway (`ScriptHost:CreateLuaItem`, code
  `door_<map>_<letter>`, a dark bar `images/doors/line.png`), in letter order beside the room's map
  (a group "Doors (* story)" docked right, items 260 x 22). Its badge text (`BadgeText`, left
  aligned, 12 px) is the line: `so2_door_text(id)` = "<letter> → <map> <name>" for the first
  unconditional destination (else the first), "*" when that one only happens in some story states,
  then up to three other destinations by number ("(+53* 46 +2)"); cut with ".." to about 40
  characters. Destinations that are no room: "Expel (world map)", or map_names.csv's place for the
  one undrawn story map (182, Inn Hilton Island at night).
- **Clicks.** `ScriptHost:AddOnLocationSectionChangedHandler("so2 doors", ...)`: a door section with
  fewer available than its one chest was clicked; the handler notes it, and in the next frame
  (`AddOnFrameHandler`, outside PopTracker's own click handling) sets `AvailableChestCount` back to
  `ChestCount` (a door never counts as checked, and nothing is saved as cleared) and sends
  `Tracker:UiHint("ActivateTab", title)` for each title of the destination's tab path, outermost
  first (`so2_door_go`). Several destinations: the first unconditional one, else the first; a
  destination with no tab (the undrawn map) changes nothing. A panel line's `OnLeftClickFunc` does
  the same; `OnRightClickFunc` steps through the doorway's other destinations.
- **Auto tab.** `so2_map_{team}_{slot}`'s map switches to the room's own tab when it is a room (940
  of map_names.csv's 964 maps switch a tab now: the rooms, then as before).
- **The entrance randomizer's seam (Phase C, not built).** `so2_door_destinations(id)` is the one
  source of a door's destination. Vanilla: `SO2_DOORWAYS[id].to` (from the layout's
  `rooms[].doors[].to`). With a room that randomizes doors, the Clear handler's `so2_doors_load(slot
  data)` reads **`door_pairs`** ({"<doorway id>": <doorway id at the other end>}; the doorway ids are
  tracker_maps.md's "Door ids" contract) and the pack subscribes to **`so2_doors_{team}_{slot}`** (a
  list, or a set, of the doorway ids found); a paired doorway reads "A → ?" until it is
  in that list, then shows (and opens) the room of its pair (yellow either way for now: the colour of
  an unknown door is Phase C's to decide, "Door markers" above). Doorways the room doesn't pair stay
  vanilla. Both names are provisional, for Phase C to confirm (and UT's
  `found_entrances_datastorage_key` can be the same key); the logic side is already there
  (`so2_logic_connect`, "Logic"). Tested with a hand-made pairing (`TestDoorsLua`).
- **Memory.** PopTracker decodes every map image it loads: with the 794 rooms its peak memory went
  from 225 MB to 555 MB (the placeholders; a player's render has the same sizes, so the same memory).

## Images (all original)

Everything in `images/` is drawn by the generator with Pillow from plain shapes and text. The
font is Pillow's bundled **Aileron Regular** (`ImageFont.load_default`; Aileron by Sora Sagano,
dotcolon.net, released as CC0 1.0), so the pack carries no font file and no font licence terms.
Nothing comes from the discs.

- **Area maps** (`images/maps/<area>.png`, 66): exactly the layout's `width` x `height`; each field
  map's rectangle in a muted fill with a 40 px grid, its label in the label strip above
  (tracker_maps.md style), "map N" and a hint to render the real map. The check squares sit at the
  layout's dot pixels.
- **World maps** (`images/maps/world_expel.png` 2048x1592, `world_energy_nede.png` 2000x2000): a
  plain sea with a 128 px grid (Energy Nede: the playable disc, radius 0x5cf8 around
  (67000, 40000)), a north arrow, and a ring and a label at each area group's arrival point
  (tracker_maps.json `dots`, from doors.json's arrivals). No terrain.
- **Boards** (`images/boards/*.png`): a title and a grid of labelled cells (330x46); the cell's
  square sits at its left end.
- **Rooms** (`images/rooms/<map>.png`, 794): exactly the layout's `width` x `height`; the label strip
  (the room's `label` in `style.label_colour` on `style.background`), a flat tone where the port draws
  the background with "map N" and a hint to render the real map, and each doorway's letter in its
  `box` (`style.door` fill, outline and text): the boxes the port draws, so a player's render lines up.
  32-colour palette PNGs with the style colours exact: 4.1 MB for all 794.
- **Doors**: the door panel's bar (`images/doors/line.png`) and the door sections' icon
  (`images/doors/door.png`); the player marker for UT's world maps (`images/status/you.png`).
- **Badges** (`images/items/*.png`, 160x60, shown at 80x30): a rounded tile in the category's
  colour (Expel keys blue, Energy Nede keys purple, dungeon keys bronze, characters green, skills
  gold, moves red, traps grey, settings teal) with the short name; the banner (512x48).

**User overrides.** PopTracker replaces any pack file with the same path under the first existing
`user-override/so2_shattered_stars/` folder: the working directory's, `<home>/PopTracker/`,
`<Documents>/PopTracker/`, then the program's folder (src/poptracker.cpp). The port's
`--render-tracker-maps` writes `images/maps/*.png` and `images/rooms/*.png` there. The pack keeps every image at its root
paths, never in a variant folder (PopTracker would then look for `<override>/<variant>/<file>` and
the player's render would be hidden). Players can override any other image the same way.

## Building

    tools/build_poptracker.sh build [--tag TAG] [--changelog LINE]...
    tools/build_poptracker.sh check
    tools/build_poptracker.sh all

`build` writes under `dist/poptracker/`: the pack folder, `so2_shattered_stars_1.0.0.zip` (the
pack's files at the zip's root, sorted, fixed timestamps: the same inputs give the same bytes, so
the sha256 is reproducible), its `.sha256`, `versions.json` (the committed
`tools/poptracker/versions.json` if there is one, with this version's entry first: `package_version`,
`download_url` = `https://github.com/IronJade/Shattered-Stars/releases/download/<TAG>/<zip>`,
default TAG `poptracker-v<version>`, `sha256`, `changelog`) and `ut_name_mapping.json`. `check` runs the
pack checker, checks versions.json against the zip, and runs the tests. The version is
`PACK_VERSION` in tools/ap_poptracker.py.

## Testing

`tools/ap_poptracker_test.py` (42 tests, about 21 seconds; run by `check`):
- **ids**: every mapped AP item and location id is in the registered world's data package; every
  one of its 1,991 locations has exactly one section, whose rules are the seam's with its own id;
  the UT name mapping's keys are unique and cover every section not named as its AP location.
- **progression**: three generated rooms (story, open world, hub, with character items, killer
  moves `any`, skillsanity `insanity` and the rest that makes items progression: 60, 125 and 109
  progression items): every progression item is mapped, and every item of the groups Keys, Dungeon
  Keys, Characters, Skills and Moves; every location of those rooms has a section.
- **layout**: every place check sits on its area image at the layout's x/y; checks sharing a dot
  share one location; every layout image is at its path with the layout's size, none in a variant
  folder; every map location is inside its image; every referenced image exists.
- **JSON**: every file parses; the manifest; every item code and map named in a layout exists; every
  ref resolves (472, one per place check); every `map_tabs.lua` path is a real tab path of the map
  layout; every map with a place check switches a tab.
- **Rooms** (`TestRooms`): every layout room is a map `room_<map>` with its image at the layout's size
  and the door letter boxes' fill and outline where the layout puts them (the port's boxes); one door
  location per doorway with one "Door <letter>" section, its access rule, a diamond per marker at
  the layout's x/y, never an AP location and not in `LOCATION_MAPPING`; `SO2_DOOR_SECTIONS` is exactly
  those sections; every door's destination is a room (whose tab is its own, titled by its number), a
  world map (its tab) or explained (`UNDRAWN_DESTINATIONS`: map 182); every area's tabs are its
  overview (titled as the area) and then its rooms in `room_groups` order, each room tab's map and
  door panel (the doorways' item codes in letter order), and `SO2_MAP_TABS[room]` is that path; the
  pack's files are under 20 MB. Each check also has its square on its room (`TestLayout`).
- **Doors in Lua** (`TestDoorsLua`, against the stub with `CreateLuaItem`, section-changed and frame
  handlers): every doorway's panel line ("A → 18 north: fountain square", story variants "(+..)",
  "*"); a door section cleared (a click) does nothing inside the change, then in the next frame is set
  back to 1 and `ActivateTab` goes along the destination room's path, once; a check's section change
  is no door; a panel line's left click (the first unconditional destination) and right clicks (the
  next ones); a door to a world map opens its tab, the undrawn map nothing; Auto tab from
  `so2_map_0_1` goes to room tabs; the entrance randomizer's seam: a room with `door_pairs` subscribes
  `so2_doors_0_1`, paired doors read "A → ?" until found, then show and open their pair's room, and
  are SequenceBreak (yellow) either way; a vanilla room again: no key, vanilla lines; every door is
  SequenceBreak and no script but doors.lua names it (no check can be yellow).
- **UT's map tab** (`TestUT`): apworld/so2/tracker/ is this build's JSON (under 1 MB), its sections
  are AP location names covering exactly the pack's locations, its maps are the pack's, every room
  has its page and is named by its label in UT's menu, and the world's `tracker_world` functions
  (`map_page_index`, `location_icon_coords`), keys and `settings_key` behave. UT's own loader runs
  in tools/ap_poptracker_utmap.py (docs/ap/apworld.md).
- **Lua**: the pack's own scripts run in Lua 5.4 (lupa, the Lua version PopTracker bundles) against
  a stub of PopTracker's API (items with codes and states, sections, `FindObjectForCode`,
  `ProviderCountForCode`, `UiHint`, the Archipelago handlers, `SetNotify` / `Get`): both variants
  load; a connect sets visibility, the checked section, the banner, the settings and badges, the
  data-storage keys; items, a repeated index, traps, an unmapped item; a check; hints; auto tab
  switching (and not on position-only updates, and not with Auto tab off); the chapter; a
  reconnect resets. Every section's rules run.
- **Logic** (`TestLuaLogic`): the pack's evaluator against the reference (tools/ap_tracker_logic.py,
  which the world's tests prove equal to the world's logic) on the exports of 16 rooms, one per
  option mix of the world's UT tests (test_ut.MIXES: story, open world, hub, bench on and off, the
  rich hub, the hell seed, ...), each generated and filled (seed 21). For the empty state, the items
  found before each sphere (the precollected ones first), every item and three random states (246
  item states, 243,885 section evaluations in all), the items go through the pack's Item handler, and every section's own access
  rule (`^$so2_access|<id>`, as PopTracker runs it) must be `Normal` exactly for the reference's
  locations in logic and `None` for the rest of the room's; the reached regions, events and goal
  agree. Also: every item a room's rules name is one of the pack's mapped items (so its change
  reaches the evaluator through the item watch, and PopTracker runs the rules again); a received
  item and a click on a mapped item (and its undo) change the result at once (the memo follows the
  items); an entrance left unconnected (`to` -1) reaches nothing until `so2_logic_connect` names
  its region, and then the result is the reference's again; a room without the export, and one with
  an export of a newer format, are a checklist with their banners.
- **Story awareness** (`test_story_is_the_reference`): in the three story rooms, for the empty state,
  the items before the first sphere and every item, at 15 story positions sent as the port sends
  them (a SetReply of `so2_story_0_1`: chapter 0 (no position), 2 (the first point), 3, 5, 12, 24
  step 500, 35 step 9999, 36, 41, 48, 57, 58, 60 steps 3000, 4000 and 5000), every section is `Normal` exactly for the reference's
  locations in logic for the story (`evaluate(counts, story)`), `Inspect` for its `later` ones and
  `None` for the rest; each value re-runs the rules (`so2_rev` moves, the step-only change too); the
  Story item off gives the plain logic and on again the story's; Chapter set by hand counts as a
  position (step 0), and 0 as none; a value that isn't a position (or `null`) changes nothing. The
  13 other rooms (open world, hub) with the same values never show `Inspect`.

**Pack checker.** `check` runs PopTracker/pack-checker v1.7.0 (what PopTracker/pack-checker-action
runs) with `--strict` on the zip and on the folder, against the published schemas
(https://poptracker.github.io/schema/packs/strict/). It validates every JSON file against the
schema, warns about JSON / Lua compatibility and hidden or dead files in zips, unsupported images
and misleading extensions, the minimum PopTracker version, and dead `ref` / `parent` references.
Result for 1.0.0: "Validated 10 files", no warnings, exit 0 (also lax). versions.json also passes
the packlist's `versions.schema.json`. With story awareness (storyaware, 2026-10-27): 401 files, 287
items, strict "Validated 10 files" for the zip and the folder, versions.json ok (sha256 aac3c4dc...),
24 tests OK.

**PopTracker itself**: tools/ap_poptracker_smoke.py ("Smoke test" below) loads the pack in the real
PopTracker 0.36.0, headless, against a local room.

## Distribution (decided by the user, 2026-10-27)

- The zip is the asset of **its own release** on IronJade/Shattered-Stars (D4), tag
  `poptracker-v<version>` (pack 1.0.0: `poptracker-v1.0.0`), not marked Latest, its notes linking the
  port and world releases, like the port's `v0.0X.YY` and the world's `apworld-vX.Y.Z` (the user:
  "It's own release works for me. We are already doing that with the apworld anyway.").
- `versions.json` (what `manifest.versions_url` points to; PopTracker checks it for updates) at a
  stable raw URL: **`tools/poptracker/versions.json` on main** (the user: as recommended), read as
  `https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/tools/poptracker/versions.json`
  (the generator's default `--versions-url`). Committing it after the release is one more docs-style
  commit on main, it lives with the generator, and there is no second branch to keep. The common
  alternative is a `versions` branch (Emerald's pack: a workflow pushes it on each release); it
  keeps main's history free of version bumps but needs an orphan branch and push automation.
  Changing the choice later means a new pack version with the new URL.
- Listing in PopTracker's `community-packs.json` (D7): after the first stable release, a PR to
  black-sliver/PopTracker's `packlist` branch: key `so2_shattered_stars`, name / author / platform
  as in the manifest, https `versions_url`, `homepage` (the repo or its wiki page), `icon_url` (a
  PNG: `res/icon/shattered-stars-128.png` on main). The listing is public under the user's name.

## Universal Tracker's map tab (B6)

UT's map tab is **hybrid** (docs/ap/apworld.md "Universal Tracker's map tab"): its JSON is in
so2.apworld (`apworld/so2/tracker/`), its pictures come from this pack's zip. So the zip is also UT's
picture source: every image a UT page names is at the pack's root paths (`images/maps/`,
`images/rooms/`, `images/boards/`, and `images/status/you.png`, the player marker UT draws on the
world maps), and those paths are tracker_maps.md's, stable across pack versions.

- The generator builds UT's JSON from the same data (`Pack.build_ut`): the pack's maps (same
  `maps.json`, so the page indexes are the pack's), the places, world maps and boards with every
  section named by its AP location name (no rules, no refs, no doors), UT's map menu (the pack's tabs
  with each room titled by its label), and `map_pages.json` (field map -> page for
  `map_page_index`; the world maps' x / z -> pixel mapping for the player marker). It writes them to
  `dist/poptracker/ut_tracker/` with every build, and with `--ut-out apworld/so2/tracker` into the
  APWorld (commit them with the generator's changes); `--ut-check DIR` fails when DIR is out of date,
  and so does `TestUT`.
- `dist/poptracker/ut_name_mapping.json` (`"<location>/<section>"` -> AP location id for every
  section whose name is not its AP location name) is still written: what UT's
  `poptracker_name_mapping` would need to read this pack's own locations (external mode). The world
  doesn't use it (its own JSON names the sections by AP name).

## Smoke test (the real PopTracker, headless)

    SKIP_REQUIREMENTS_UPDATE=1 prlimit --as=$((8<<30)) scratchpad/ap/ap-venv/bin/python \
        tools/ap_poptracker_smoke.py [--zip dist/poptracker/so2_shattered_stars_1.0.0.zip] [--keep]

It runs PopTracker 0.36.0 itself against a local room, with **no window**: PopTracker starts with
a cleared environment (no `DISPLAY`, no `WAYLAND_DISPLAY`, no D-Bus, an empty `XDG_RUNTIME_DIR` and
an empty `PATH`, so neither a display server nor a dialog helper can be reached), SDL's dummy video
and audio drivers, and a scratch HOME (scratchpad/packgen/smoke). PopTracker is the official Linux
AppImage of the v0.36.0 release, downloaded once into scratchpad/packgen/poptracker/, its minisign
signature checked against PopTracker's release key (`key/3B69F3B9A853297F.pub` in its repo), and
extracted; this machine has no SDL2 / SDL2_ttf / SDL2_image development files for a source build.
The AppImage's SDL 2.0.20 has no offscreen driver, so it is the dummy one (`SDL_VIDEODRIVER`, SDL2's
spelling). Needs `websockets` (in the AP venv) and `cryptography` (installed into the AP venv).

Steps: generate an open-world room (character items, crafts, killer moves, drops, shop specials,
skills) with the scratch Archipelago, host it on 127.0.0.1:38391 (MultiServer, `--disable_save`,
stopped at the end).

**The logic, in PopTracker itself.** PopTracker (Map Tracker) connects to the fresh room with a
probe added through its own user-override folder: the override's `scripts/init.lua` is the pack's
plus one `ScriptHost:LoadScript` of `scripts/so2_smoke_probe.lua` (PopTracker reads scripts through
the override folder too); the pack's `logic.lua`, `so2.lua` and autotracking load from the zip
unchanged. The probe, a frame handler that walks the room's sections 100 a frame, prints whenever
it changes the set of unchecked sections that **PopTracker itself** shows as reachable
(`LocationSection.AccessibilityLevel`, PopTracker's own evaluation of each `^$so2_access|<id>`).
A client of the slot then plays: at three points (fresh; after it has checked every location then
in logic, and the server sent the items found there; the same again) that set must equal the
world's own: the room rebuilt from its slot data in Universal Tracker's way (test_ut.py's
`ut_rebuild` / `in_logic`), its unchecked locations in logic. The override is removed afterwards.

Then the port's part as a client of the slot (checks for the three lowest missing locations and a
craft, then `so2_map_0_1` = `{"map": 26}` and `so2_story_0_1` = `{"chapter": 12, "step": 100}`);
start PopTracker with `--load-pack so2_shattered_stars --pack-variant map|items_only --ap-host
localhost:38391 --ap-slot Tester`, and quit it with SIGINT after 40 seconds (it saves its state on
quit). Then it checks the log and the saved state
(`saves/so2_shattered_stars/1.0.0/<variant>/autosave.json`).

**Story awareness, in PopTracker itself** (step 7; `--no-story` skips it). A second room, one
story-mode slot "Storyteller" (killer moves, the Cave of Trials, boss checks; seed 4243) on PORT + 1;
PopTracker (Map Tracker, the same probe, which also reports the sections it shows as Inspect)
connects to it. Fresh, before any story key, its Normal sections must be the world's own in logic and
none Inspect. Then a client of the slot, as the port (items_handling 0b001), Sets
`so2_story_{team}_{slot}` the port's way (one replace, want_reply false) at chapter 3, 12, 36, 60
step 3000 and 60 step 4000 (a step-only change): each time PopTracker's Normal sections must be the
reference evaluator's for the room's export with that position, its Inspect ones the reference's
`later`, and the rest None.

**Rooms and doors, in PopTracker itself** (step 8, after the variants' runs). PopTracker (Map
Tracker) with a door probe in its user override (`scripts/so2_smoke_doors.lua`) connects to the first
room; 60 frames after the Clear handler the probe clears door A of map 26 (Hearn's General Store:
`AvailableChestCount = 0`, what a click on "Door A" in the popup does) and 30 frames later prints the
section's count and its `AccessibilityLevel`. Checks: the door is SequenceBreak (yellow), the
section had 1 of 1, the pack set it back to 1 of 1, no Lua error or warning, and in the state PopTracker saves on quit the active tabs are the destination room's path
(`SO2_MAP_TABS[18]`: Expel > Arlia Village > Arlia Village > 18) and the door section is not saved
as cleared. Step 5's map check now wants map 26's own room tab (Expel > Arlia Village > Arlia
Village > 26) from `so2_map_*`. It also prints the panel line and PopTracker's peak memory.

Result with rooms and doors (2026-10-27, maptab: pack 1.0.0 with layout 1.1's rooms, so2.apworld
0.2.2): **44/44 checks pass** (the 37 below with step 5's room tab, and 7 for step 8). Door A of
map 26 -> 18 (panel line "A → 18 north: fountain square"); the click reset the door and opened
Expel > Arlia Village > Arlia Village > 18; map 26 from `so2_map_*` opened its room tab; peak memory
555 MB (225 MB without the rooms). No Lua error or warning.

Result with yellow doors (2026-10-27, utzip: `so2_door_access` SequenceBreak for every door): **45/45
checks pass** (the 44 above and step 8's colour check: PopTracker reports door A of map 26 at
AccessibilityLevel 5, SequenceBreak); peak memory 558 MB.

Result with story awareness (2026-10-27, pack 1.0.0 as built by storyaware, so2.apworld 0.2.2's
export with `story`): **37/37 checks pass** (the 24 below and 13 for step 7). The
story room, 583 locations in logic by its 3 starting items (Normal sections / Inspect "later in the
story"):

| Position | fresh (no key) | chapter 3 | 12 | 36 | 60, step 3000 | 60, step 4000 |
|---|---|---|---|---|---|---|
| Normal | 583 | 37 | 108 | 291 | 393 | 583 |
| Inspect | 0 | 546 | 475 | 292 | 190 | 0 |

each the reference's exactly, the rest None; the step-only change moved 190 sections from Inspect to
Normal (the Final Save Point: 80 killer-move levels learned above level 30, 91 checks in the Cave of
Trials, Lacour's 18 chapter-60 gifts and the King's Room's chest). No Lua error or warning.

Result with the evaluator (2026-10-27, pack 1.0.0, so2.apworld 0.2.2's export): **24/24 checks
pass**. The logic: fresh, 115 of the room's 1,293 locations in logic (the 2 starting passes);
after the first round of checks (117 items received) 99 of the 1,178 unchecked; after the second
(216 items) 40 of 1,079: each time PopTracker's reachable sections were exactly the world's, and
its unreachable ones the rest. No Lua error or warning.

Result before the evaluator (2026-10-27, pack 1.0.0 as built by B4a): **20/20 checks pass** (both variants). What it showed: the pack loads ("so2_shattered_stars 1.0.0 (map) loaded"), PopTracker connects to the room
as a tracker (the server: "Tester (Team #1) tracking Star Ocean: The Second Story has joined.
Client(0.5.1), ['PopTracker', 'NoText', 'Tracker']"), applies the slot data (the Room settings,
the banner), shows the received items (the starting Arlia Pass and Fun City Pass, and the Salva
Pass one of the checks gave), clears exactly the four checked sections, sets the chapter to 12 from `so2_story_*`, and
switches the map tabs to Expel > Arlia Village > Arlia Village from `so2_map_*` (the saved UI
state's active tabs). PopTracker also fetched the community pack list and the pack's
`versions_url` (not published yet: "Could not fetch", harmless). An earlier run found
PopTracker's "duplicate location section" warning for the world maps' unnamed `ref` sections; they
now carry their AP location names, and the smoke test fails on any warning.

**A GUI check** (for a person, optional): install the zip in PopTracker's `packs` folder, load
"Star Ocean: The Second Story (Shattered Stars)" > Map Tracker, connect to a room with the AP
button, and look at: the banner ("Logic: from the room" with a 0.2.2 room; "Logic unavailable for
this room: checklist" with an older one), the green and red squares, the Room tab, a
world map's squares, an area map's squares and their popups (one entry per check), the boards'
cells, and (with port 0.03.00 connected to the same room) the tabs following the party. With
`shattered-stars --render-tracker-maps` run first, the area maps show the real backgrounds and the
squares sit on the chests.
