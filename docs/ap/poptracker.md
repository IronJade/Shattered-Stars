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

Status (pack 1.0.0 as built by B4a): everything but the logic. Each location's access goes
through one function, `so2_access` (scripts/logic.lua), which shows every location the room has
as reachable (a checklist) and says so in the banner. B4b replaces that function's body with an
evaluator over the room's logic export (B3, `slot_data["tracker"]`): see "The logic seam".

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
- **Status**: the logic banner at the top ("not connected", "logic unavailable for this room:
  checklist", "logic: from the room"), the story chapter the port reports, and **Auto tab** (on by
  default): with port 0.03.00 connected to the room, the map tabs follow the map the party is on.
- **Hints**: hinted locations in this world are highlighted (priority, avoid, ...).
- Checks reach the tracker through the server, so checks made while the port is offline show up
  once it reconnects and sends them.

## Pack structure

| Path | What | Made by |
|---|---|---|
| `manifest.json` | uid `so2_shattered_stars`, `package_version` 1.0.0, `game_name` "Star Ocean: The Second Story" (the AP game name), platform PC, author DreamGlyph Studios, variants `map` ("Map Tracker") and `items_only` ("Items Only"), each with the flag `ap` only (never `apmanual` / `aphintgame`), min and target PopTracker 0.36.0, `versions_url` | generator |
| `settings.json` | smooth scaling for items and maps (the badges and maps are drawn larger than shown) | generator |
| `items/items.json` | 286 items: 35 area keys, 18 dungeon keys, 12 characters, 5 traps (consumables), 46 skills, 148 moves (264 mapped to AP ids); the logic banner (`so2_logic_*`, progressive), the chapter (`so2_chapter`), auto tab (`so2_auto_tab`), the refresh counter (`so2_rev`, in no layout); 18 settings (`opt_<key>` toggles, `opt_<key>_<value>` progressive stages) | generator |
| `locations/places.json` | region (Expel, Energy Nede, Fienal, Cave of Trials) -> area -> a location per dot (472 place checks, 408 dots) | generator |
| `locations/world.json` | "Expel World Map" / "Energy Nede World Map" -> a location per area group, whose sections are `ref`s to the place checks of the group's areas | generator |
| `locations/boards.json` | Shops (52 cells, 550 checks), Item Creation (26, 553), Killer Moves (8, 133), Guilds and Learning (25, 194), Enemy Drops (19, 89) | generator |
| `maps/maps.json` | 66 area maps + 2 world maps (one per layout image, named after the image file) + 5 boards | generator |
| `layouts/items.json` | the banner, the item tabs (Keys, Party, Skills, Moves per member, Room), `tracker_broadcast` (keys and characters) | generator |
| `layouts/tracker.json` | `tracker_default` of the map variant: the item column left, the map tabs right | generator |
| `items_only/layouts/tracker.json` | the Items Only variant's `tracker_default` (no maps) | generator |
| `scripts/init.lua`, `so2.lua`, `logic.lua`, `autotracking/archipelago.lua` | the hand-written Lua (below) | tools/poptracker/ |
| `scripts/pack_data.lua` | `SO2_PACK`, `SO2_SETTINGS` (slot key, code, values, default), `SO2_CHARACTERS`, `SO2_DUNGEON_KEYS`, `SO2_WORLD_MAPS` (world x/z -> pixels) | generator |
| `scripts/map_tabs.lua` | `SO2_MAP_TABS[field map] = {tab, tab, ...}`: 870 of map_names.csv's 964 maps | generator |
| `scripts/autotracking/item_mapping.lua` | `ITEM_MAPPING[AP item id] = {{code, type}}` | generator |
| `scripts/autotracking/location_mapping.lua` | `LOCATION_MAPPING[AP location id] = {"@path/section"}`: every one of the data package's 1,991 locations | generator |
| `images/maps/*.png` | placeholders at the layout's paths and sizes (66 areas, 2 worlds) | generator |
| `images/boards/*.png`, `images/items/*.png`, `images/status/*.png` | boards, item badges, the banner | generator |
| `images/icon.png`, `LICENSE` | the Shattered Stars icon (res/icon, original), the repo's MIT licence | copied |

Beside the pack folder the generator writes `ut_name_mapping.json` (below, "For B6").

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
- **Map tabs** (`map_tabs.lua`): a map drawn in an area image switches to that area's tab (a map in
  two images: the one map_names.csv names); then map_names.csv's area, the area of a location row on
  that map, the area group whose entrance maps hold it, and the world-map tab for the 'Expel' /
  'Energy Nede' maps (the Cave of Trials tab for its own). World maps 1 and 2 switch to their world
  map tab. 94 maps (system maps, cutscene copies, areas with no checks such as L'Aqua) switch
  nothing.

Regenerate when the world's locations, items or options change, when tracker_maps.json changes, or
when map_names.csv changes; the tests fail when the pack and the world disagree.

## Lua

All hand-written files are in `tools/poptracker/`, short and generic; the generator copies them.

- `init.lua`: loads pack_data, map_tabs, so2, logic; adds items, maps and locations (the map
  variant only), layouts; then the autotracker. `SO2_ITEMS_ONLY` is the Items Only variant.
- `so2.lua`: shared state `SO2` (`connected`, `exists` = the room's location ids, `received` = AP
  item id -> count from the server, `slot_data`, `story`), `so2_exists(id)` (the visibility rule),
  `so2_item_count(ap_item_id)` (the pack's item when mapped, so clicks count too; else what the
  server sent), `so2_refresh()` (bumps `so2_rev` so PopTracker re-runs the rules after a Lua-only
  change), `so2_set_logic_status(key)` (the banner), and a watch on every item code that calls
  `so2_logic_items_changed(code)`.
- `logic.lua`: the logic seam (next section).
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
    `SO2.story` and the Chapter item and calls `so2_story_changed(story)` when it exists (B6).

## The logic seam (for B4b)

Every section's access rule is `^$so2_access|<AP location id>`. The `^` makes PopTracker read the
return value as an **AccessibilityLevel** (`None`, `SequenceBreak`, `Normal`, ...), so an evaluator
can show out-of-logic-but-possible (yellow) as well as reachable and unreachable. The contracts,
all in `tools/poptracker/logic.lua`:

| Function | Called | Contract |
|---|---|---|
| `so2_logic_load(slot_data)` | by the Clear handler, once per connect, after `SO2.exists` and the items are set | read `slot_data.tracker` (B3's export; absent in rooms from 0.2.0 / 0.2.1 or a world without it); set `SO2_LOGIC.mode` and the banner: `so2_set_logic_status("on")` with logic, `"checklist"` without |
| `so2_logic_items_changed(code)` | after any item changes (received, clicked, reset) | drop memoized results (keep it cheap: it runs once per changed item) |
| `so2_access(id)` | by PopTracker for each visible section, whenever an item changed | `id` is the AP location id as a **string**; return an `AccessibilityLevel` |
| `so2_story_changed(story)` | optional; on each `so2_story_*` value | B6's story awareness |

Inputs an evaluator has: `so2_item_count(ap_item_id)` (items by AP id, as the export names them),
`SO2.slot_data`, `SO2.exists`, `SO2.story`. If what `so2_access` returns changes without an item
change (an export loaded late, a story change), call `so2_refresh()`. Don't change items inside
`so2_access` (PopTracker warns about items toggled during rule evaluation). Today
(`B4a`): `so2_logic_load` sets "checklist", `so2_access` returns `AccessibilityLevel.Normal`.
The tests' Lua stub (tools/ap_poptracker_test.py `TestLua`) runs the scripts in Lua 5.4 and is the
place to test the evaluator against B3's Python reference.

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
- **Badges** (`images/items/*.png`, 160x60, shown at 80x30): a rounded tile in the category's
  colour (Expel keys blue, Energy Nede keys purple, dungeon keys bronze, characters green, skills
  gold, moves red, traps grey, settings teal) with the short name; the banner (512x48).

**User overrides.** PopTracker replaces any pack file with the same path under the first existing
`user-override/so2_shattered_stars/` folder: the working directory's, `<home>/PopTracker/`,
`<Documents>/PopTracker/`, then the program's folder (src/poptracker.cpp). The port's
`--render-tracker-maps` writes `images/maps/*.png` there. The pack keeps every image at its root
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
default TAG `apworld-v0.2.2`, `sha256`, `changelog`) and `ut_name_mapping.json`. `check` runs the
pack checker, checks versions.json against the zip, and runs the tests. The version is
`PACK_VERSION` in tools/ap_poptracker.py.

## Testing

`tools/ap_poptracker_test.py` (18 tests, about 6 seconds with the build; run by `check`):
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
- **Lua**: the pack's own scripts run in Lua 5.4 (lupa, the Lua version PopTracker bundles) against
  a stub of PopTracker's API (items with codes and states, sections, `FindObjectForCode`,
  `ProviderCountForCode`, `UiHint`, the Archipelago handlers, `SetNotify` / `Get`): both variants
  load; a connect sets visibility, the checked section, the banner, the settings and badges, the
  data-storage keys; items, a repeated index, traps, an unmapped item; a check; hints; auto tab
  switching (and not on position-only updates, and not with Auto tab off); the chapter; a
  reconnect resets. Every section's rules run.

**Pack checker.** `check` runs PopTracker/pack-checker v1.7.0 (what PopTracker/pack-checker-action
runs) with `--strict` on the zip and on the folder, against the published schemas
(https://poptracker.github.io/schema/packs/strict/). It validates every JSON file against the
schema, warns about JSON / Lua compatibility and hidden or dead files in zips, unsupported images
and misleading extensions, the minimum PopTracker version, and dead `ref` / `parent` references.
Result for 1.0.0: "Validated 10 files", no warnings, exit 0 (also lax). versions.json also passes
the packlist's `versions.schema.json`.

**PopTracker itself**: tools/ap_poptracker_smoke.py ("Smoke test" below) loads the pack in the real
PopTracker 0.36.0, headless, against a local room.

## Distribution (proposal; the user decides what is published and where)

- The zip is a release asset of IronJade/Shattered-Stars (D4), beside so2.apworld: by default the
  `apworld-v0.2.2` release (pack 1.0.0 ships with world 0.2.2).
- `versions.json` (what `manifest.versions_url` points to; PopTracker checks it for updates) at a
  stable raw URL. **Proposed: `tools/poptracker/versions.json` on main**, read as
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

## For B6 (Universal Tracker's map tab)

- `dist/poptracker/ut_name_mapping.json`: `"<location>/<section>"` -> AP location id for every
  section whose name is not its AP location name (all of them today: sections drop the area
  prefix): UT's `poptracker_name_mapping`. Keys are unique by construction (dot names are unique).
- `scripts/map_tabs.lua`'s table is the `map_page_index` UT needs (map -> page), once the pages are
  chosen; `SO2_WORLD_MAPS` holds the world maps' x/z -> pixel mapping for a player icon.
- The pack's maps.json / locations JSON follow PopTracker's format, which UT's map tab reads.

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
stopped at the end); play the port's part as a client of the slot (checks for the three lowest
missing locations and a craft, then `so2_map_0_1` = `{"map": 26}` and `so2_story_0_1` =
`{"chapter": 12, "step": 100}`); start PopTracker with `--load-pack so2_shattered_stars
--pack-variant map|items_only --ap-host localhost:38391 --ap-slot Tester`, and quit it with SIGINT
after 40 seconds (it saves its state on quit). Then it checks the log and the saved state
(`saves/so2_shattered_stars/1.0.0/<variant>/autosave.json`).

Result (2026-10-27, pack 1.0.0): **20/20 checks pass** (both variants). What it showed: the pack loads ("so2_shattered_stars 1.0.0 (map) loaded"), PopTracker connects to the room
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
button, and look at: the banner ("Logic unavailable for this room: checklist"), the Room tab, a
world map's squares, an area map's squares and their popups (one entry per check), the boards'
cells, and (with port 0.03.00 connected to the same room) the tabs following the party. With
`shattered-stars --render-tracker-maps` run first, the area maps show the real backgrounds and the
squares sit on the chests.
