# Tracker maps: the shared layout (`docs/ap/tracker_maps.json`)

The PopTracker pack ships only original placeholder maps. The port renders the real maps from
the player's own disc into PopTracker's user-override folder (decision D5,
the tracker plan; "This keeps us trouble free from soliciting any IP"). Both sides must
draw the **same layout**: the same image files and sizes, each field map in the same rectangle,
each check's dot on the same pixel. This file describes that layout. B4 (the pack generator,
`tools/ap_poptracker.py`) draws its placeholders from it, and the port's
`--render-tracker-maps` command draws the disc's backgrounds into it.

- Generator: `tools/ap_tracker_maps.py DISC1.cue` (or `SS_DISC1`) writes the JSON;
  `--check` compares with the committed file. It needs the NA disc 1, because the map scenes
  (field camera, background size) and the scripts (where chests, actors and trigger regions
  are) come from it. The output holds only ids, names and numbers. Its only text is the names
  already in the repo: AP location and area names (apworld/so2/data.py) and place names
  (docs/modding/reference/map_names.csv).
- Inputs: `apworld/so2/data.py` (`LOCATIONS`, `HUB_LOCATIONS`, `KEY_GROUPS`, read as literals
  without importing Archipelago), `docs/ap/field.json` (`via`: what registers the routine that
  gives a check), `docs/ap/doors.json` (the door graph and the world-map arrivals),
  `map_names.csv` (labels).
- The port compiles the JSON in (engine/CMakeLists.txt makes
  `generated/tracker_layout_data.cpp` from it), so no extra file ships. A layout change
  needs a port build. Pack and port agree as long as both were built from the same JSON.
  `format` changes only when the meaning of a field changes; `format_minor` counts additions
  a reader of the same `format` can ignore (1.1 added the rooms and their doors).

Regenerate it when place checks change (new rows in data.py), when map names change, or when
the generator's rules change, then rebuild the port and the pack.

## Top level

| Key | Meaning |
|---|---|
| `format` | 1 |
| `format_minor` | 1 (format 1.1: `rooms`, `room_groups`, `style.door`, checks' `room`; a format 1 reader ignores them) |
| `source` | `tools/ap_tracker_maps.py` |
| `pack_uid` | `so2_shattered_stars` (the override folder is `user-override/<pack_uid>/`) |
| `style` | `margin`, `gap` (px), `label_height` (px), `background` / `label_colour` (`#rrggbb`), `dot` {`size`, `colour`, `outline`}, `door` {`fill`, `text`, `outline`} (a room's door letter boxes): what the port uses; the pack may use them too |
| `images` | the images, in order: area images (by area name), then the world maps |
| `rooms` | (1.1) one image per field map an area's doors reach, by map number: "Rooms" below |
| `room_groups` | (1.1) the rooms of each area, in door order: "Rooms" below |
| `checks` | AP location name -> its dot (every place check: chest, gift, boss, join) |
| `area_only` | AP location name -> reason, for place checks without a dot (empty today) |

Pixel coordinates: origin at the image's top-left, x right, y down. A dot's `x`/`y` is its
centre.

## Area images (`"kind": "area"`)

One per Archipelago area (the `area` of the location rows) that holds a place check:
`images/maps/<slug>.png`, where the slug is the area name in lower case with apostrophes
dropped and every other run of non-alphanumerics turned into `_` (`Rena's Room` ->
`renas_room.png`).

| Key | Meaning |
|---|---|
| `path` | the image's path inside the pack (and under the override folder) |
| `area` | the AP area name |
| `width`, `height` | image size in pixels (both at most 2048) |
| `scale` | `"n/d"`: every field map in this image is drawn at this scale (1/1, 3/4, 2/3 or 1/2: the largest that fits) |
| `maps` | the field maps, in drawing order |

Each map entry:

| Key | Meaning |
|---|---|
| `map` | field map number |
| `label` | text for the label strip: the place name from map_names.csv, or `"<area> <map>"` where that name is only a door-graph placeholder |
| `x`, `y`, `w`, `h` | the map's rectangle; `w`/`h` are `bg_w`/`bg_h` times the scale, rounded |
| `bg_w`, `bg_h` | the background's own size on the disc (320x240 up to 1440x360) |
| `label_y` | top of the label strip (`label_height` px, directly above the rectangle, same `x`, width `w`) |

Which maps: every field map that holds one of the area's place checks (a map can appear in
two areas' images). The order runs outwards through the door graph from the maps the area's
world-map entrance leads to (`KEY_GROUPS` maps), with ties broken by map number. The maps are
packed in rows: margin 8, gap 8, and a 16 px label strip above each map. The row width is
the one that gives the squarest image.

The port draws the whole background into the rectangle. It is box-filtered to `w`x`h`, in a
fixed state: no scroll window, animations at their first frame, colour cycles at their first
step. A map with parallax layers (the Heraldry Forest, Lacour, the Fields of Intelligence and
Power: 9 maps) has no single picture, so it is put together from 320x224 screens. Each screen
is drawn at its own scroll position, as the game shows it there. The label goes in the strip (the 5x7 overlay font at 2x, cut with `..` to fit). The
rest of the image is `style.background`. A placeholder should fill the same rectangles (for
example a flat colour with the map's label), so that a player's override lines up with the
dots.

## World images (`"kind": "world"`)

`images/maps/world_expel.png` (world map 1, 2048x1592: the whole world torus, 0x1b000 x
0x15000 units) and `images/maps/world_energy_nede.png` (2, 2000x2000: the playable disc,
radius 0x5cf8 around (67000, 40000), with a margin), seen from above, north up:

- image x = (world x - `x0`) / `units_per_pixel`
- image y = (`z0` - world z) / `units_per_pixel` (integer division)

Expel: x0 0, z0 0x15000 (86016), 54 units per pixel. Energy Nede: x0 42000, z0 65000, 25.
North is up and east is right: the guide's "Salva is directly north of Arlia" and "the east
coast of the Cross continent" (Herlie) both hold in the render.

| Key | Meaning |
|---|---|
| `world_map`, `name` | 1 Expel, 2 Energy Nede |
| `x0`, `z0`, `units_per_pixel` | the mapping above |
| `dots` | one per world-map entrance group (`KEY_GROUPS`: the open world's area keys) |

The port draws the terrain blocks flat, from above. Each polygon is textured and shaded with
its own vertex colours (the default light), and drawn lowest first, with water (the
semi-transparent polygons) last. It renders at 3x and box-filters down. Map 1 uses the
version of block 0x30 seen from chapter 0xf on (entry 0x1013), which covers most of the game.

Each dot: `group` (the KEY_GROUPS group), `key` (its key item's name), `x`, `y`, `world`
[x, z] (the arrival point: where the party appears on the world map when it leaves the group's
entrance maps, the most used one in doors.json `arrivals`), `via_map` (the map that door
leaves from), `areas` (the AP areas whose rows mostly name this group). Every area with an
area image belongs to exactly one dot.

## Checks

`checks[<AP location name>]`:

| Key | Meaning |
|---|---|
| `image` | the area image (`path`) the dot is on |
| `x`, `y` | the dot's centre in that image |
| `map` | the field map it is in |
| `how` | how the spot was found (below) |
| `world` | [x, y, z]: the world position projected (absent for `map` and `region`: a trigger region is not a world position) |
| `clamped` | true when that position projects outside the background (the dot is moved to the nearest edge pixel) |
| `room` | (1.1) {`image`, `x`, `y`}: the same dot on the map's room image (`images/rooms/<map>.png`) |

`how`:
- `chest`: the chest's own position, from the constant arguments of `chest_create` (script
  B 0x6c) with this opened flag.
- `actor`: the actor the routine giving the check belongs to (ext 0x70 actor_create, 0xf3
  actor_create with a pickpocket entry); for gifts and joins handed over by someone.
- `region`: the trigger region that starts that routine (ext 0x74 / 0x75 thread_start, 0xd8 /
  0xd9 trigger_add_button). A field map's trigger regions are in **screen coordinates**, not
  world ones: trigger_check_regions tests the leader's actor +0x74 / +0x76 (its projected
  screen point) while field_state +0x68 (3D mode) is 0, as on every field map
  (docs/notes/field_part4.md 2). So the region's background pixels are its corners minus the
  view origin (MapScene +0x28 / +0x2c), no projection. If the region's centre is off the
  background, the dot goes to the middle of the part of the region that is on it; if none of
  it is, the dot is clamped. (The first layout projected the corners as floor points; the
  1.1 layout corrects the 48 `region` checks. Checked over every field map's doors: the way
  back's arrival point lands within 40 px of the door's region for 1636 of 1846 doors read
  this way, 86 read the old way.)
- `spawn`: the routine is reached only from the map's own code (a scene on entering, a deeper
  call chain), so the dot is the map's default spawn point (MapScene +0x14).
- `map`: as `spawn`, but the spawn point is off the background, so the dot is the
  background's centre.

The routine is found through field.json's `via` tree (the shallowest registration with
constant arguments wins). Gifts and joins use the `gifts` / `fol_gifts` / `recruitments`
record of their location id; bosses use the `bosses` site on the row's map.

Projection (as the port places an actor; `Scene` in the generator): the GTE matrix
actor_sys_setup_view loads is Rz(roll +0x30) Rx(+0xdc) Ry(+0xde) diag(1, -1, -1), with the
eye at +0x08. The screen point is H x / z + plane / 2 (H +0x6a, plane +0x34 / +0x36), and the
background pixel is that minus +0x28 / +0x2c. In the image: `x + bgx * w / bg_w`, floored.
Checked against the running port on maps 18 and 25: the chest sprites sit on the dots.

Several checks often share a dot: one NPC hands out several gifts, and Fienal's and the Cave
of Trials' chest rooms reuse the same spots. A pack can make one map location with one
section per check at each distinct (image, x, y).

Coverage (layout 1.1 of 2026-10-27): 472 place checks (308 chests, 101 gifts, 42 bosses, 21
joins), all with a dot and a room dot. By `how`: chest 308, actor 65, region 48, spawn 50,
map 1; none clamped. `area_only` is empty. 66 area images (242 map rectangles; the largest is
Salva Drift, 2024x1720 at 1/1) and 2 world maps (35 group dots). 794 rooms in 66 groups
(105.1 M pixels), 1775 doorways (1822 markers, 6 clamped) holding 2011 doors (1836 with a way
back), and 380 transfers started by the maps' own code. (Before the `area` audit of
2026-10-27, the maptab handoff log: 793 rooms and 378 transfers; Port Hilton's pier, map 171,
is a room since.)

## Rooms (format 1.1)

Each field map an area's doors reach also has an image of its own, so a path can be followed
room by room: `images/rooms/<map>.png`. The area images stay as each area's overview. A room
image is the map's whole background at its own size (1:1), under a label strip with the map's
number and name, and a small box with a letter beside each doorway. A door's destination is
**never drawn**: with the entrance randomiser (Phase C) it differs per seed and may be unknown
until found, so the tracker shows it from data (part 2, below). Only static things are on the
image.

`rooms[]` (by map number):

| Key | Meaning |
|---|---|
| `path` | `images/rooms/<map>.png` |
| `map` | field map number |
| `name` | the place name from map_names.csv, or the map's area where that name is only a door-graph placeholder |
| `label` | the label strip's text: `"<map> <name>"` |
| `area` | the room's group (`room_groups[].area`): its area image's AP area, or a world-map entrance group without one |
| `width`, `height` | image size: the background's width, and its height plus the strip |
| `bg_y` | the strip's height: the background is drawn at (0, `bg_y`), 1:1 |
| `font_scale` | the text scale (1 to 4): about 20 px high once PopTracker fits the image into a 1000 x 800 panel (`max(1, min(4, (20 max(4 w, 5 h) + 14000) // 28000))`) |
| `location_size` | 12 x `font_scale`: the location square the boxes are kept clear of (around each check dot and door point); the pack's room maps should use it |
| `label_box` | [x, y, w, h] of the strip: [0, 0, `width`, `bg_y`] |
| `scene_doors` | (when there are any) transfers started by the map's own code (a scene on entering, a story event), not by a trigger: `to` records (below) with no spot. Not drawn |
| `doors` | the door markers: one per doorway trigger spot, by letter |

Each door marker:

| Key | Meaning |
|---|---|
| `letter` | A, B, C... (AA after Z) in doorway id order within the room |
| `id` | the doorway id (below) |
| `x`, `y` | the trigger's spot in the room image (where the pack puts the door's location): a region's centre, as a `region` check's dot is placed (screen coordinates minus the view origin; off the background: the middle of its visible part); an actor's projected position (a door given by talking to someone) |
| `box` | [x, y, w, h] of the letter box, beside the spot and clear of the check dots, the other spots (`location_size` squares) and the other boxes |
| `how` | `region` or `actor` |
| `clamped` | true when no part of the trigger is on the background (moved to the nearest edge pixel) |
| `to` | the doorway's **vanilla** destinations, the default only: [{`door`, `map`, `back`, `conditional`}] |

Each `to` record: `door` (the door id), `map` (the map it sends the party to; 1 / 2 / 3 are the
world maps), `back` (when found: the doorway id in the target map that leads back here, nearest
the arrival point: the other end of this entrance), `conditional` (true when the transfer only
happens in some story states: doors.json `requires`).

Text (the strip, the letters): the overlay's 5x7 font at `font_scale` (advance 6 x scale), at
(x + 2 scale, y + (h - 7 scale) / 2) inside its box, cut with `..` to fit w - 4 scale. The
strip is `style.background` with `style.label_colour` text; a letter box is `style.door.fill`
with a `font_scale`-wide `style.door.outline` border and `style.door.text` letters. A
placeholder may use any font, but the same boxes.

### Door ids (a contract for Phase C and the world's logic export)

- A **door** is one record of docs/ap/doors.json: one map-transfer instruction. Its id is
  doors.json's `id`, `map * 65536 + offset / 4` (the NA script's map, the lowest map of a
  shared script; offset = the instruction's byte offset). It never changes while the discs and
  that rule stay the same, and it is already the id the seed's `doors` overrides use
  (seed_format.md, phase 4).
- A **doorway** is what the player walks into: the doors started by one trigger. Doors of one
  routine (story variants of one exit: 125 doorways send the party to more than one map
  depending on the story), or whose triggers sit on the same pixel, are one doorway. Its id is
  the **lowest door id** in it. A doorway whose routine two trigger regions start has two
  markers with the same id and letter.
- Recommended for Phase C: an entrance is a doorway, named from its id (for example
  `"Door 1180128"`), and the entrance randomiser sends every door of a doorway to the same new
  destination (the seed's `doors` gives each door id its `to`). The vanilla pairing of two
  entrances is `to[].back`. The tracker's live state is keyed by doorway id.
- Letters are for people: stable as long as the room's set of doorways is, but never a key.

### Room groups

`room_groups[]` (by name): {`area`, `image` (the area image's path, or null for a group with
none), `world` (1 Expel, 2 Energy Nede: the world map its entrance is on), `entrances` (the maps
the world-map entrance leads to), `rooms` (its maps, by door distance from the entrances, then
by number)}. Every room is in exactly one group, so a map has one room tab.

Which maps: the groups are the area images (seeded with their maps, and with the entrance maps
of the world-map entrance group of the same name: Port Hilton's harbour 169 / 171 joins "Port
Hilton" although that image shows the Lacour coliseum, where the released "Port Hilton" rows
are; North City's plaza 260 / 261 joins "North City") and the world-map entrance groups
(`KEY_GROUPS`) with no area image, seeded with their entrance maps (an entrance into a place
that has an area image joins that image: map 231 joins Eluria Colony; L'Aqua is the one such
group today). A group's places are the map_names.csv `area`s of its seeds, except a seed's area
that is another group's name (Arlia Village's map 16 is a Shingo Forest map, Rena's Room's 38
an Arlia Village one: neither takes the other's maps). From the seeds, breadth first, in layers:
(1) over any door to a map of one of the group's places, a map two groups reach in the same
layer going to the group named as its place, else the first by name; (2) then over two-way doors
with trigger regions both ways to any map still without a group (buildings that have places of
their own, such as Cross Castle's rooms); (3) then over a one-way door with a trigger region
into a map whose own such doors lead back into the group (in through one door, out through
another: Bowman's House). A map in two area images belongs to the one whose name is its place,
else the first by name. World maps and maps without a background are never rooms. Not reached
(so not drawn): 30 maps with a background: opening and story scenes (Planet Milocinia 4-8,
Inn Hilton Island at night 168-182, the Outer Wall 617-619, the
Transporter Portal of Sealing 313, the fanzine hall 122), Mountain Palace maps without doors
(340-356) and unused slots (0, 28, 643, 697, 826).

## For the pack (B4)

- Put the images at the pack's root paths above (not inside a variant folder). PopTracker
  reads an override as `<override>/<file>` (or `<override>/<variant>/<file>` when the pack has
  a variant copy of that file), so a variant copy would hide the player's render.
- Placeholders must be exactly `width` x `height`.
- A check's `map_locations` entry is `{map: <the image's map name>, x, y}` with the dot's x/y.
- PopTracker uses the first `user-override/so2_shattered_stars` folder that exists, in this
  order: the working directory's `user-override`, `<home>/PopTracker/user-override`,
  `<Documents>/PopTracker/user-override`, then the program's own folder
  (src/poptracker.cpp). The port's default target follows the same order (below).

## For the pack (part 2): rooms, doors and the live door panel

What the pack generator (`tools/ap_poptracker.py`) needs to do with format 1.1. Nothing here
changes the format 1 parts above.

**Status (2026-10-27, agent maptab): built** as below; docs/ap/poptracker.md "Rooms and doors" is
the pack's side (and docs/ap/apworld.md "Universal Tracker's map tab" UT's). Where it differs from
this sketch: a room's tab is titled by its map number (the label strip and the door panel carry the
name; named titles made the busiest tab bars six to eight rows); the overview tab is titled as the
area (so activating the area's tab shows it); the door markers are diamonds coloured SequenceBreak
(yellow, the one colour no check uses; until 2026-10-27 a known door was Inspect blue; the colour of
a door not found yet under the entrance randomizer is Phase C's decision, yellow for now); a click
is acted on in the next frame. Measured: 794 placeholders 4.1 MB (32-colour PNGs), the pack zip 5.6 MB; PopTracker's peak
memory 555 MB (225 MB before the rooms).

- **Room images.** A placeholder per room, exactly `width` x `height`: `style.background`, a flat
  box for the background area (0, `bg_y`, `width`, `height - bg_y`), the `label` in the
  `label_box` strip, and each door's letter in its `box` (`style.door`). The same boxes the
  port draws, so a player's render lines up. Root paths `images/rooms/<map>.png`, not in a
  variant folder (as the area images). Measured by mapdoors: 793 placeholders are about 2.8 MB (2.5 MB
  zipped) drawn with PIL's default font; the pack zip was 3.1 MB.
- **Maps.** One PopTracker map per room (`room_<map>`, `img` = `path`, `location_size` = the
  room's `location_size`).
- **Tabs.** Area > Room: each area tab becomes `tabbed` with the area image first ("Overview")
  and then one tab per room in `room_groups[].rooms` order (title: the room's `label`). A group
  with no area image (`image` null: L'Aqua) gets a tab of rooms only. Each room is in exactly
  one group, so map -> tab path is a function: the door clicks and scripts/map_tabs.lua (the
  player's own map, `so2_map_*`) can both switch to the room's tab rather than the area's.
- **Checks on rooms.** Each check's location gets a second map location `{map: room_<map>, x,
  y}` from `checks[].room`, so a room tab shows the same squares as the overview.
- **Door locations.** One location per door marker, at the marker's `x`, `y` on its room
  (the letter box sits beside it, clear of every square): name `"<map> <letter>"`, one section
  named `"Door <letter>"`. Doors are not Archipelago locations: no UT name, no AP id, never
  counted (see the click below), always visible.
- **The live door panel.** PopTracker cannot write text on a map image, so each room tab
  shows a panel beside the map (a layout of items whose text the pack's Lua sets with
  `BadgeText` / an overlay): one line per letter, `"A -> 412 Salva Drift"`. The destination of
  doorway `id` comes from:
  - vanilla (no entrance randomiser): a door table the pack ships, built from `rooms[].doors[].to`
    (doorway id -> its destinations: map, and the other end's doorway id `back`);
  - Phase C (entrance randomiser): the seed's pairings in slot_data (doorway id -> doorway id,
    which gives the map), and for entrances not found yet the found-entrances data-storage key
    (UT's `found_entrances_datastorage_key`): until then the line reads `"A -> ?"`.
  A doorway with several vanilla destinations (story variants: 125 doorways) lists each map,
  the conditional ones marked.
- **Click switching.** `ScriptHost:AddOnLocationSectionChangedHandler` on the door sections:
  when a door section is clicked, set its `AvailableChestCount` back to 1 (a door never counts
  as checked), look up the doorway's live destination (as the panel), and send
  `Tracker:UiHint("ActivateTab", name)` for each tab title on the destination room's tab path
  (a world map: the world map's tab). Unknown destination: no switch. Several destinations: the
  first unconditional one, else the first.
- **Names.** Room names come from `rooms[].name` (map_names.csv `place`, with its evidence in
  map_names_evidence.csv for the renamed ones); a destination that is not a room (a world map,
  an undrawn map) can use map_names.csv or the world image's `name`.

## The port's command

`shattered-stars --render-tracker-maps [DIR]` (tools/tracker_maps.h) reads disc 1 (as the
game finds it: SS_DISC1, `--disc1`, config.ini or the `discs` folders) and writes every image
above as a PNG under DIR: the area and world images, then the rooms (format 1.1: 862 images in
all, about 135 MB, in about 21 s on the development machine), and then, beside DIR, their zip for
Universal Tracker (`so2_shattered_stars_ut.zip`: "The Universal Tracker zip" below; `--ut-zip FILE`
puts it elsewhere, `--no-ut-zip` leaves it out). It writes nothing else: no config.ini, no log, no saves, no cards. It
needs no window and no seed. Each PNG is written under a temporary name beside it
(`<name>.png.part`) and renamed into place once complete, so a run cut short never leaves a
partly written image under the name PopTracker reads. Besides a line per image, the console
gets the count at the start ("rendering 862 image(s) into DIR") and every 100 images ("300 of
862 done"). Exit code 0, or 1 when an image could not be rendered or written.

For other callers, `tools::render_tracker_maps` takes a progress function
(`TrackerRenderOptions::progress`, tools/tracker_maps.h): it is called on the rendering thread
before each image with the images done so far, the total, the image's path, its kind (area,
world, room) and its name (the area, the world, or the room's label), and once after the last
image (empty path); returning false stops the render before the next image, and the function
then returns `tools::kTrackerRenderStopped` (2). With a zip to write (`TrackerRenderOptions::ut_zip`)
it is also called with kind `zip` before each of the zip's files (done / total count the zip's files,
the path is the zip's, the name the file's) and once when the zip is in place (done == total, no
name), before that last call; returning false during the zip stops it with no zip written. Every
call also says how many images have failed so far (`failed`). The render keeps no global state: it may run on
a thread of its own, given a disc handle nothing else uses meanwhile.

Without DIR it uses PopTracker's override folder for the pack: the first existing
`<home>/PopTracker/user-override/so2_shattered_stars` or
`<Documents>/PopTracker/user-override/so2_shattered_stars`. Failing that, the same under
`<home>/PopTracker` when that folder exists (PopTracker's default user folder), else under
`<Documents>/PopTracker`. Home and Documents are what PopTracker uses: the profile and
Documents folders on Windows; elsewhere `$HOME`, and `$XDG_DOCUMENTS_DIR`, else
`~/Documents` when it exists, else `$HOME`.

Testing switches: `SS_TRACKER_DOTS=1` also draws each check's dot (to check the layout:
PopTracker draws its own location squares, so players don't want these; on a room, each door's
spot too, in the door colour), and `SS_TRACKER_ONLY=a,b` renders only the images whose path
contains one of these (`rooms/18.` for one room, `rooms/` for all of them).

### The setup button

The setup program's **Archipelago** page has a **Render tracker maps for PopTracker** button
(engine/src/setup/setup_ui.cpp, `App::start_tracker_render`), so a
Windows player never needs a terminal for the command above. It runs the same code
(`tools::render_tracker_maps`, linked straight into `shattered-stars-setup` alongside
`tools::poptracker_override_dir`; not the whole `ss_game`, which that code doesn't need) against
whichever Disc 1 is on screen (saved or not) and the default folder, same as
`--render-tracker-maps` with no DIR. The button is disabled with "Choose a valid Disc 1 image
first." until a disc is picked, same tooltip as the bottom bar's Save / Save & Play.

The render runs on a thread of its own (engine/src/setup/tracker_job.h, `TrackerRenderJob`),
so the window goes on drawing (about 20 s with the rooms of format 1.1, on the development
machine). While it runs the button is disabled and the page shows a progress bar ("150 of 862
images") and the image in progress ("Rendering room 92 Armory...", "Rendering the area map of
Arlia Village..."); the rest of setup can be used meanwhile. Then it writes the Universal Tracker
zip beside the folder (the bar counts its 868 files: "Writing the zip for Universal Tracker...").
Afterwards it says how many images were written and where, with an **Open folder** button next
to it, and below that the zip's path ("For Universal Tracker: ...so2_shattered_stars_ut.zip") with
the hint "Choose this file when Universal Tracker asks for the pack." and a **Copy path** button;
or the error (the disc couldn't be read, some images failed, or the zip failed). Closing setup during the render (the window's close
button, Quit, Save & Play) stops it before its next image and waits for it: the images written
by then stay, each one whole, and pressing the button again renders them all. Closed while the zip
is written: no zip (an earlier one stays as it was), and the page says so. Nothing on the
page is saved by this button, and no save or memory card is touched.

Thread safety: the worker opens its own disc image and archive (from a copy of the path; the
window reads the same file only through handles of its own, for the disc check and the logo),
parses its own copy of the layout and shares nothing with the window but its progress (under a
mutex) and a stop flag; the folder is resolved on the window's thread before it starts (it
reads the environment).

Scriptable the same way as every other setup action: `--do render-tracker-maps` /
`SS_SETUP_SCRIPT=...,render-tracker-maps` (this also scrolls the Trackers section into view).
For tests, `wait-tracker-render` holds the script until the render is over and
`wait-tracker-render:N` until N images are done; the frames meanwhile don't count towards
`--frames`, so `--do render-tracker-maps,wait-tracker-render --screenshot done.png` shows the
outcome and `--do render-tracker-maps,wait-tracker-render:150 --screenshot mid.png` the
progress (the program then exits, stopping the render). Setup prints "setup: tracker maps:
rendering into DIR" when it starts and "setup: tracker maps: done (862 of 862 images; F frames
drawn while it ran)" (or stopped, ...) when it ends, then "setup: tracker maps: the Universal
Tracker zip: PATH (868 files)" (or "... not written (K of N files)" when it stopped in the zip).

## The Universal Tracker zip

Universal Tracker's map tab (docs/ap/apworld.md "Universal Tracker's map tab") takes its JSON from
so2.apworld and only the pictures from a zip the player points it at once (host.yaml
`so2_options: ut_pack_path`; UT asks with a file dialog the first time). It reads each picture as
`ap:zip:<zip>/<path>` (UT 0.3.6, worlds/tracker/TrackerKivy.py) and can't read PopTracker's
user-override folder. So the render also writes **`so2_shattered_stars_ut.zip`**: every picture UT's
map tab shows, at the pack's paths, with the player's own maps where the pack has placeholders. In
UT's dialog the player picks this file, or the PopTracker pack's zip for the placeholders
(apworld/so2/ut_map.py names both). Port feature, 0.03.00 (the user, 2026-10-27: "Include it now").

**Where.** Beside the image folder, in its parent. With no DIR (PopTracker's override folder, as
above) that is `<PopTracker>/user-override/so2_shattered_stars_ut.zip`, next to the rendered
`so2_shattered_stars` folder:

| OS | Default zip |
|---|---|
| Windows | `C:\Users\<you>\Documents\PopTracker\user-override\so2_shattered_stars_ut.zip` (`C:\Users\<you>\PopTracker\...` when PopTracker's user folder is there) |
| Linux, macOS | `~/PopTracker/user-override/so2_shattered_stars_ut.zip` when `~/PopTracker` exists, else `~/Documents/PopTracker/user-override/so2_shattered_stars_ut.zip` (`$XDG_DOCUMENTS_DIR` on Linux) |

`--render-tracker-maps DIR` puts it at `<DIR's parent>/so2_shattered_stars_ut.zip`; `--ut-zip FILE`
at FILE (its folder is made); `--no-ut-zip` writes none (of the two, the last one given counts).
The setup button always uses the default. Why there: next to the maps, so it's easy to find (setup
and the console print its path); in no folder PopTracker lists packs from (only the `packs` folders,
src/poptracker.cpp); and not an override: PopTracker takes only a **folder** named as the pack's uid
under `user-override` as the pack's override (src/core/pack.cpp), so a file beside it is ignored.
One rule comes from UT: it splits `ap:zip:<zip>/<path>` at the first `zip/`, so no folder on the
zip's path may end in "zip" (`~/myzip/`, `C:\stuff.zip\`; on Windows too, as UT's file dialog gives
the path with `/`). The default paths never do, unless the home folder's own name does.

**What's in it** (868 files, 134.9 MB): the 862 images of the layout, read back from the image
folder after the render, so the entries are byte for byte the files PopTracker shows (an image
missing from the folder is missing from the zip, and the console says how many; with
`SS_TRACKER_ONLY` the folder's other images, from an earlier run, go in as they are); and what
else UT's pages show that is no disc render, drawn by the port as original art:
- the 5 boards (`images/boards/board_shops.png`, `board_crafts.png`, `board_killer_moves.png`,
  `board_skills.png`, `board_drops.png`) from `tools/poptracker/boards.json`, which
  tools/ap_poptracker.py writes and CMake compiles in with the layout
  (`generated/tracker_layout_data.cpp`, `tools::tracker_boards_json()`): each board's path, size,
  title and hint, each cell's box and label, the colours. The port's board has the pack's size and
  every cell at the pack's box, where UT puts the location's square (the cell's (24, h / 2)); the
  text is the overlay's 5x7 font (title 4x, hint and labels 2x, a long label on two lines or at 1x),
  not the pack's font. Only numbers and text come from the generator, none of its pixels;
- UT's player marker (`images/status/you.png`, 64x64 RGBA: the pack's design, a white disc, a red
  #e0302a disc and a white centre, drawn in the port).

Nothing else from the disc, nothing from the pack. Like the rendered images, the zip holds the
player's disc's pictures: for their own use, never shared or published.

**Format** (engine/src/util/zip_writer.h, `util::ZipWriter`). A plain .zip: entries stored (PNGs
don't compress further), sorted by path, every one dated 1980-01-01 00:00, no extra fields, no
comment, external attributes 0, CRC-32 by util/deflate's `crc32`; no zip64 (at most 65,535 files
and 4 GiB). The same images give the same bytes. It is written as `so2_shattered_stars_ut.zip.part`
and renamed once complete (an existing zip is replaced only then), so a stopped or failed run
never leaves a partial zip and an earlier run's zip stays as it was.

**When.** After the images, also when some of them failed (the exit code is then 1 anyway). The
console says `tracker maps: writing the Universal Tracker zip PATH (868 files)`, then `tracker
maps: the Universal Tracker zip: PATH (868 files, 134.8 MB)`. A zip that can't be written is an
error on the console and exit code 1; the images stay.

**Keeping the boards in step.** The pack generator writes `dist/poptracker/boards.json` with every
build; `tools/ap_poptracker.py --boards-out tools/poptracker/boards.json` updates the committed
copy the port compiles in (then rebuild the port). The pack's tests fail when the committed copy is
not the build's (`TestUT.test_boards_for_the_port`, which also checks every board square of the pack
and of UT's JSON sits on a cell).

**Tests.**
- Unit (engine/tests/test_tracker_maps.cpp, `SS_TEST=tracker_`): `tracker_zip_writer` (the archive
  field by field, CRCs against mods::crc32, read back by mods::ZipReader, the same bytes twice, a
  replaced file, refused names, abandon / an unfinished writer leave nothing), `tracker_ut_zip_path`,
  `tracker_ut_extras` (board sizes and cells, the marker's pixels), and with the disc
  `tracker_disc_ut_zip` (a render's zip: the rendered files byte for byte plus the extras, sorted,
  the same bytes twice, nothing outside DIR and the zip, the progress calls in order, a stop before
  or during the zip writes none and leaves an earlier one, no zip asked: none).
- End to end (tools/ap_tracker_maps_test.py): the default place and DIR's parent, `--ut-zip FILE`,
  `--no-ut-zip`; Python's zipfile reads it (testzip), stored, dated, sorted, exactly the layout's
  images and the extras, the map entries the rendered PNGs byte for byte, both full runs' zips the
  same bytes, nothing stray.
- UT's own loader on the port's zip (tools/ap_poptracker_utmap.py `--zip`): the same checks as with
  the pack's zip, and every page's picture and the marker through UT's own image loader (its
  `ap:zip:` load, decoded by Kivy) at the layout's sizes.

**Results (2026-10-28, agent utzip2; docs/handoff/utzip.md).** `SS_TEST=tracker_` 14/14 (all
ss_game_tests 230/0, ss_tests 103/0); the end-to-end test's 34 checks; the 862 images byte for byte
the render's before the zip existed, with and without it; the zip 868 files, 134,897,214 bytes
(sha256 35e72e8d...), the same from the command line and from setup; UT's own loader 16/16 with
the port's zip and with the pack's, against the repo world and the packaged so2.apworld; setup
headless: Done with the zip's path and Copy path, the zip step on screen ("7 of 868 files"), an
exit during the zip leaving no zip and no `.part`; the field-event smoke 373/0 (normal play
unchanged). Times on the development machine (shared with another job that kept about ten cores
busy, best of three full renders): 22.6 s with the zip, 22.0 s with `--no-ut-zip`, 22.0 s for the
port before the zip (20.7 s on a quiet machine); the zip step alone 0.6 to 1.3 s (reading back the
135 MB and writing them out, drawing the boards).

