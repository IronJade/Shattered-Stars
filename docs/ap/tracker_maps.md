# Tracker maps: the shared layout (`docs/ap/tracker_maps.json`)

The PopTracker pack ships only original placeholder maps. The port renders the real maps from
the player's own disc into PopTracker's user-override folder (decision D5,
docs/plans/trackers.md; "This keeps us trouble free from soliciting any IP"). Both sides must
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
  `format` changes only when the meaning of a field changes.

Regenerate it when place checks change (new rows in data.py), when map names change, or when
the generator's rules change, then rebuild the port and the pack.

## Top level

| Key | Meaning |
|---|---|
| `format` | 1 |
| `source` | `tools/ap_tracker_maps.py` |
| `pack_uid` | `so2_shattered_stars` (the override folder is `user-override/<pack_uid>/`) |
| `style` | `margin`, `gap` (px), `label_height` (px), `background` / `label_colour` (`#rrggbb`), `dot` {`size`, `colour`, `outline`}: what the port uses; the pack may use them too |
| `images` | the images, in order: area images (by area name), then the world maps |
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
| `world` | [x, y, z]: the world position projected (absent for `map`) |
| `clamped` | true when that position projects outside the background (the dot is moved to the nearest edge pixel) |

`how`:
- `chest`: the chest's own position, from the constant arguments of `chest_create` (script
  B 0x6c) with this opened flag.
- `actor`: the actor the routine giving the check belongs to (ext 0x70 actor_create, 0xf3
  actor_create with a pickpocket entry); for gifts and joins handed over by someone.
- `region`: the trigger region that starts that routine (ext 0x74 / 0x75 thread_start, 0xd8 /
  0xd9 trigger_add_button), on the scene's floor. If the region's centre is off the background,
  the dot goes to the middle of the part of the region that is on it; if none of it is,
  the dot is clamped.
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

Coverage (layout of 2026-10-27): 472 place checks (308 chests, 101 gifts, 42 bosses, 21
joins), all with a dot. By `how`: chest 308, actor 65, region 48, spawn 50, map 1; 11 clamped
(trigger regions entirely off screen: map-entry triggers). `area_only` is empty. 66 area images
(242 map rectangles; the largest is Salva Drift, 2024x1720 at 1/1) and 2 world maps (35 group
dots).

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

## The port's command

`shattered-stars --render-tracker-maps [DIR]` (tools/tracker_maps.h) reads disc 1 (as the
game finds it: SS_DISC1, `--disc1`, config.ini or the `discs` folders) and writes every image
above as a PNG under DIR. It writes nothing else: no config.ini, no log, no saves, no cards. It
needs no window and no seed.

Without DIR it uses PopTracker's override folder for the pack: the first existing
`<home>/PopTracker/user-override/so2_shattered_stars` or
`<Documents>/PopTracker/user-override/so2_shattered_stars`. Failing that, the same under
`<home>/PopTracker` when that folder exists (PopTracker's default user folder), else under
`<Documents>/PopTracker`. Home and Documents are what PopTracker uses: the profile and
Documents folders on Windows; elsewhere `$HOME`, and `$XDG_DOCUMENTS_DIR`, else
`~/Documents` when it exists, else `$HOME`.

Testing switches: `SS_TRACKER_DOTS=1` also draws each check's dot (to check the layout:
PopTracker draws its own location squares, so players don't want these), and
`SS_TRACKER_ONLY=a,b` renders only the images whose path contains one of these.

### The setup button

The setup program's **Archipelago** page has a **Render tracker maps for PopTracker** button
(engine/src/setup/setup_ui.cpp, `App::start_tracker_render` / `App::run_tracker_render`), so a
Windows player never needs a terminal for the command above. It runs the same code
(`tools::render_tracker_maps`, linked straight into `shattered-stars-setup` alongside
`tools::poptracker_override_dir`; not the whole `ss_game`, which that code doesn't need) against
whichever Disc 1 is on screen (saved or not) and the default folder, same as
`--render-tracker-maps` with no DIR. The button is disabled with "Choose a valid Disc 1 image
first." until a disc is picked, same tooltip as the bottom bar's Save / Save & Play; while it
runs the page says "Rendering tracker maps..."; afterwards it says how many images and where, with
an **Open folder** button next to it, or the error if the disc couldn't be read. Nothing on the
page is saved by this button, and no save or memory card is touched. Scriptable the same way as
every other setup action: `--do render-tracker-maps` / `SS_SETUP_SCRIPT=...,render-tracker-maps`
(the render itself runs on the frame after the action, so give at least one more frame).
