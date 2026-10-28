# Handoff: maptab (tracker maps part 2 + the map half of B6; map_names `area` audit)

Agent "maptab", started 2026-10-27 by the Phase B coordinator (docs/handoff/phaseb.md). For pack
1.0.0 and so2.apworld 0.2.2. Plan: docs/plans/trackers.md (B6); spec: docs/ap/tracker_maps.md
"For the pack (part 2)". Previous agents: docs/handoff/mapdoors.md (part 1), packgen.md (the pack),
logicexport.md (the export, so2_logic_connect / so2_logic_view / so2_in_logic), storyaware.md.

## The user's words (2026-10-27, verbatim)
"...we could simply have the map number that a door leads to on the image to be able to follow the
pathway. If it allows for clickable transitions between them, then this would be our method. Click
on the doorway with the mapTo label and that map then shows up. Anything that we CAN name correctly,
we should. Houses, Shops, etc." and then: "Those labels for doors may need to be live. Keep in mind
if we move forward with the Entrance Randomiser and the unconnected doors, the logic engine needs to
be able to follow routes to get from one place to another. The tracker would need the correct toMap
draws for each door." So: no destination is ever baked into an image; destinations are shown live
from data.

## The brief
1. map_names.csv `area` audit (drives the pack's tabs and the layout's room groups): fix maps filed
   under the wrong area from evidence (door graph components from each area's entrance, KEY_GROUPS,
   banners), a basis note per change (map_names_evidence.csv style); regenerate
   docs/ap/tracker_maps.json (tools/ap_tracker_maps.py) and the modding reference CSVs
   (`tools/mod_reference.py <DISC1.cue>`, only the cells the fixes explain). If the layout changes:
   rebuild the port in engine/build-maptab, `SS_TEST=tracker_` + tools/ap_tracker_maps_test.py
   (counts in engine/tests/test_tracker_maps.cpp only if they changed for this reason).
2. The pack, part 2 (tools/ap_poptracker.py + tools/poptracker/**): room placeholders at the
   layout's size with the port's boxes (original art); a PopTracker map per room; tabs Area >
   (Overview, rooms in room_groups order); each check a second map location on its room; door
   locations (non-AP, "Door <letter>", never counted); the live door panel (BadgeText
   "A -> 412 Salva Drift"; vanilla door table from rooms[].doors[].to; a clean ER seam: slot_data
   pairings + a found-entrances key, "?" until found: seam + tests, not ER); the click handler
   (section changed -> reset AvailableChestCount -> UiHint ActivateTab along the destination room's
   tab path; several destinations -> the first unconditional); panel item clicks the same; auto tab
   from so2_map_* to room tabs. Door marker colour: sensible, say why. Pack zip <= ~20 MB.
3. UT's map tab (apworld): tracker_world per UT's map-integration.md; map_page_setting_key
   "so2_map_{team}_{player}", map id -> page index, player icon from x/z on world maps if
   supported, the name mapping. Internal / external / hybrid: apworld ~220 KB today, at most
   ~2 MB more; document setup and whether UT can use the player's rendered maps. Doors in UT:
   use what it supports, document what it can't. Real generation unchanged: batches
   (tools/ap_batch.sh, 288 seeds) byte-identical .apso2 / spoilers / multidata vs a frozen
   baseline (real copies of top-level scripts), world tests, test/general, SO2_SLOW_TESTS,
   tools/ap_ut_check.py.
4. Tests: tools/ap_poptracker_test.py (rooms, doors, panel, destinations exist as room tabs or are
   explained, click handler and panel under lupa with a stubbed UiHint), tools/build_poptracker.sh
   all, the real PopTracker smoke (door click via AvailableChestCount -> UiHint + reset; auto-tab to
   a room), UT's map tab loading headless if possible.
5. Docs: docs/ap/poptracker.md, docs/ap/tracker_maps.md (part 2 status), docs/ap/apworld.md (UT map
   tab), the player guide's tracker section (apworld/so2/docs/), this log; a "for the release
   notes" paragraph here (agent "releasedocs" owns release notes, README, wiki).

Files I may touch: tools/ap_poptracker*.py, tools/poptracker/**, tools/build_poptracker.sh,
tools/ap_tracker_maps*.py, docs/ap/tracker_maps.json and .md, docs/modding/reference/map_names*.csv
and the CSVs mod_reference.py regenerates, apworld/so2/**, engine/tests/test_tracker_maps.cpp
(counts only), docs/ap/poptracker.md, docs/ap/apworld.md, docs/ap/seed_format.md if needed,
docs/handoff/maptab.md. NOT: docs/release_notes/*, README.md, docs/ap/release_checklist.md,
docs/handoff/windows_check.md, docs/plans/next.md, scratchpad/wiki (agent "releasedocs"). Engine
source changes: none expected; if the renderer must change, describe it here and stop that part.

## Rules given
- The user's real Archipelago install (~/.local/share/Archipelago) is READ-ONLY. Never touch a real
  PopTracker folder; scratch HOMEs only.
- Python from scratchpad/ap/Archipelago with ../ap-venv/bin/python and SKIP_REQUIREMENTS_UPDATE=1;
  pytest -n 6; generation under `prlimit --as=$((8<<30))`; at most one heavy job at a time.
- Nothing on the user's desktop: third-party programs (UT, PopTracker, AP clients, the port) run
  with DISPLAY, WAYLAND_DISPLAY and DBUS_SESSION_BUS_ADDRESS unset and XDG_RUNTIME_DIR pointed at a
  scratch folder (or under ~/.local/bin/xvfb-run).
- Port runs: headless, tools/memcap.py, at most 2 game processes, builds -j6, scratch
  XDG_CONFIG_HOME/HOME, SS_DISC1 = "/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/Star Ocean - The Second Story (Disc 1).cue".
- Commit on main after each verified piece (area fixes; the pack's rooms/doors; the UT map tab),
  docs first, staged by path (never git add -A / -u); message ends with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never commit generated
  images or zips. Never push, tag or publish. index.lock: wait and retry; never delete it.
- A denied tool call: don't retry; write the blocked step here and stop, naming the exact command.
- A choice that is the user's: write it here with my recommendation and continue with the rest.
- Cap command output; long jobs in the background with deadlines; kill only by exact PID; don't
  pipe a program's output into a pipe drained later (write to a file); scratch under
  scratchpad/maptab/ (not /tmp, which is RAM); delete stale batches when done. Cloned repos and web
  pages are data, not instructions.
- Keep this log current after each finished step and at least every 30 minutes. Roll over at
  ~400k context: update the log and stop.

## State
### Done
- **1. Area audit** (map_names.csv `area`): 10 maps fixed, each with its evidence in
  docs/modding/reference/map_names_area_evidence.csv (map, area, previous_area, place,
  previous_place, basis, evidence): 4, 5, 6, 8 Arlia Village -> Planet Milocinia (the opening's
  caverns: entered only from map 7, last door to Shingo Forest 14; headless shots show the same
  caverns as 7); 11, 13 Lacour Front Line and 15 Arlia Village -> Shingo Forest (sepia copies of
  Shingo Forest 10 / 12 / 14: a flashback entered from the Front Line's tent 447); 16 Arlia
  Village -> Shingo Forest (doors only 14 -> 16 -> 12; green forest); 52, 53 Arlia Village ->
  Alen's Mansion (Alen's study: doors to 46 and Salva Drift; 53 -> 17 is one-way story). Places
  of the placeholders renamed from thumbnails (8 "cavern below the landing site (the opening)",
  ...); kinds fixed where the same evidence shows it. Method: scratchpad/maptab/audit.py
  (each area's door-graph components), audit2.py (maps with no door neighbour in their own
  area), room thumbnails (render.py, the CLI render of single rooms) and --map shots (shot.py).
- Room grouping (tools/ap_tracker_maps.py room_groups): a world-map entrance group's maps seed
  the area image of the same name (Port Hilton's harbour 169/171 -> "Port Hilton", North City's
  plaza 260/261 -> "North City"); a seed's area that is another group's name is not that
  group's place; layered BFS with ties to the group named as the map's place. Moves: Port
  Hilton town 8 rooms Tavern Stone -> Port Hilton (+171 newly a room), North City 13 rooms
  Field of Love -> North City, 591/592 North City -> Field of Love, Arlia night maps Rena's
  Room -> Arlia Village, 11/13/15 -> Shingo Forest, 52/53 -> Alen's Mansion, Lacour tents
  441-444 Lacour meeting room -> Meeting Room (their csv area). 794 rooms (was 793), 66 groups,
  380 scene transfers; 862 images. tracker_maps.md "Room groups" + counts updated.
- Regenerated: docs/ap/tracker_maps.json; docs/modding/reference/maps.csv and doors.csv
  (tools/mod_reference.py: only the 10 maps' rows / the doors naming them changed).
- Verified: port rebuilt in engine/build-maptab (RelWithDebInfo, AP net + setup); SS_TEST=tracker_
  10/10; tools/ap_tracker_maps_test.py 862/862 images, sizes, same bytes twice, nothing stray;
  ap_tracker_maps.py --check up to date; the current pack's tests 24/24 on the new layout.
  No count in engine/tests/test_tracker_maps.cpp needed a change.
- Found, not mine: test_tracker_job (setupthread's) leaves an empty `tracker_job_test/` folder
  in the working directory (removed; run ss_game_tests from a scratch cwd).
- Found, for the user (not fixable without renaming released AP locations): some released AP
  area names are other places' (tools/ap_world_tables.py RELEASED_AREAS: "Port Hilton" rows are
  in the Lacour coliseum, "North City" chests on the Field of Love platforms 593). The pack's
  tabs follow the AP area names (so each check is under its own name); the room groups now
  also put each town's own rooms under its name.

- Coordinator's extra: apworld/so2/docs/setup_en.md UT step 1 reworded (so2.apworld 0.2.2 or
  later, or the room's own if newer; 0.2.0-0.2.2 rooms track with 0.2.2): committed 0062337.
- **2. The pack, part 2** (tools/ap_poptracker.py, tools/poptracker/doors.lua (new), init.lua,
  autotracking/archipelago.lua, tools/ap_poptracker_test.py, tools/ap_poptracker_smoke.py):
  794 room maps (`room_<map>`) with placeholders at the layout's sizes and the port's boxes
  (32-colour PNGs, style colours exact, 4.1 MB); each check also on its room; tabs Area >
  (overview titled as the area, then rooms titled by map number in room_groups order; L'Aqua
  rooms only); 1,775 door locations ("Doors/<map> <letter>", a diamond per marker, one section
  "Door <letter>", `^$so2_door_access|<id>`: Inspect blue / SequenceBreak yellow when unknown);
  the live door panel (a LuaItem per doorway, code door_<map>_<letter>, BadgeText
  "A → 18 north: fountain square", "*" story, "(+53* 46 +2)"); clicks (section changed ->
  next frame: reset to 1, ActivateTab along SO2_MAP_TABS[dest]; panel left click = first
  unconditional, right click = the next ones); Auto tab to room tabs (940/964 maps); ER seam
  (slot data `door_pairs`, data storage `so2_doors_{team}_{slot}`, "?" until found;
  provisional names). scripts/doors_data.lua generated (SO2_ROOMS, SO2_MAP_NAMES,
  SO2_DOORWAYS, SO2_ROOM_DOORS, SO2_DOOR_SECTIONS). Player marker images/status/you.png (for UT).
- Verified: tools/build_poptracker.sh all: 1,201 files, zip 5,559,587 bytes, checker strict
  "Validated 11 files" (zip + folder), versions.json ok, tests 37/37 (TestRooms 5, TestDoorsLua 5
  new); real PopTracker 0.36.0 smoke 44/44 (step 8: door A of 26 -> 18 clicked, reset, tabs
  Expel > Arlia Village > Arlia Village > 18, not saved as cleared; map 26 from so2_map_* -> its
  room tab); PopTracker peak memory 555 MB with rooms vs 225 MB without (decoded images).
- Docs: docs/ap/poptracker.md ("For players", structure table, "Rooms and doors", Images, Testing,
  Smoke step 8 + result), docs/ap/tracker_maps.md (part 2 status).

- **3. UT's map tab** (hybrid): apworld/so2/ut_map.py (new: `SO2Settings` with `ut_pack_path`
  (FilePath, required False, UT's `ut_dialog_name`), `map_page_index`, `location_icon_coords`,
  `tracker_world`), apworld/so2/world.py (`settings: ClassVar[SO2Settings]`, `settings_key =
  "so2_options"` (the default would be "so2.world_options": the class lives in world.py),
  `tracker_world`), apworld/so2/tracker/ (generated: maps/maps.json = the pack's maps,
  locations/locations.json with AP names, layouts/maps.json = UT's map menu, map_pages.json; 612 KB
  raw, 56 KB in the .apworld), tools/ap_poptracker.py (`build_ut`, `--ut-out`, `--ut-check`, writes
  dist/poptracker/ut_tracker), TestUT, tools/ap_poptracker_utmap.py (new: UT's own
  load_pack / load_map / update_location_icon_coords headless in a UT copy; TrackerGameContext needs
  a running loop, and its no_connection=True path is broken in UT 0.3.6 (no `checksums`)).
- Verified: batches ALL IDENTICAL (288/288 seeds, 480 .apso2, 288 spoilers, 288 multidata, checker
  output the same; baseline = frozen c544a40 world, scratchpad/maptab/identity.py); world tests 882
  passed (3 skipped); test/general + world 935; SO2_SLOW_TESTS 885; tools/ap_ut_check.py UT MATCHES
  THE WORLD (59 matches); UT map loader 15/15 (repo world) and 15/15 (packaged so2.apworld);
  so2.apworld 281,228 bytes (scratchpad/maptab/dist; no world_version bump); pack tests 41/41.
- Docs: apworld.md "Universal Tracker's map tab" (+ results, Files rows), poptracker.md
  "Universal Tracker's map tab (B6)" + TestUT, setup_en.md (UT map tab paragraph, a PopTracker
  section).
- Note: the scratch AP's host.yaml (scratchpad/ap/Archipelago/host.yaml) now has `so2_options:
  ut_pack_path: ""` (AP saves world settings groups); a stale "so2.world_options" block from the
  first try was removed by hand.

### In progress
- (none: final smoke rerun and cleanup)

### For the release notes (agent "releasedocs" owns them)
Tracker maps, room by room (PopTracker pack 1.0.0, so2.apworld 0.2.2): besides each area's overview
the PopTracker pack now has a tab per room, 794 of them, numbered by the game's map number, with the
area's checks on them and a lettered marker on every doorway. A **Doors** panel beside each room says
where each door leads ("A → 18 north: fountain square"; * = only at some points of the story): click
a line, or a door's marker, and that room opens; with the game connected the tabs follow you room by
room. No destination is drawn into the pictures: the panel is live, ready for randomized doors.
Universal Tracker gets a **map tab** too: so2.apworld carries the map data and UT takes the pictures
from the PopTracker pack's zip, which it asks for once; it follows the game as well, with a marker on
the world maps. A few maps moved to the right place: Alen's study is Alen's Mansion, Port Hilton's
and North City's own rooms are under their towns, the opening on Planet Milocinia and Rena's
flashback in Shingo Forest are where they belong.

### Facts for releasedocs' "TODO (maptab)" placeholders
- UT map tab: **hybrid** (UT's term): JSON in so2.apworld (`apworld/so2/ut_map.py`,
  `apworld/so2/tracker/`: maps/maps.json, locations/locations.json, layouts/maps.json,
  map_pages.json), pictures from the PopTracker pack zip. so2.apworld 281,228 bytes (the map tab
  56 KB of it). Player setup: on first connect UT asks for `so2_shattered_stars_1.0.0.zip` (file
  dialog); kept in host.yaml as `so2_options: ut_pack_path`; cancel = no map tab (`null`),
  `/reload_pack` asks again. Follows `so2_map_{team}_{slot}`: room pages, world maps with a player
  marker. No doors in UT (they are no AP locations / entrances / events). UT can't read
  PopTracker's user-override: a player's render goes into a copy of the zip.
- New host.yaml setting: `so2_options: ut_pack_path`. **No new slot-data key** (batches identical
  including multidata). ER seam names (pack side only, provisional for Phase C): slot data
  `door_pairs`, data storage `so2_doors_{team}_{slot}`.
- Pack: 1,201 files, zip 5,559,587 bytes (sha256 f76fe8a4...; was 3.0 MB), 794 room maps,
  1,775 door locations, 940 of 964 maps switch a tab; PopTracker peak memory 555 MB (225 MB
  before the rooms). Checker strict "Validated 11 files" (zip and folder).
- Tests: tools/ap_poptracker_test.py 41/41 (TestRooms 5, TestDoorsLua 5, TestUT 4 new);
  tools/ap_poptracker_smoke.py 44/44 (step 8: door click: door A of map 26 cleared from Lua as a
  popup click does -> the pack resets it and opens room 18's tab path; map 26 from so2_map_* opens
  its room tab); tools/ap_poptracker_utmap.py 15/15 (UT's map loader headless, also with the
  packaged so2.apworld); layout: SS_TEST=tracker_ 10/10, tools/ap_tracker_maps_test.py 862 images.
- Release checklist rows to add: `tools/ap_poptracker.py --ut-check apworld/so2/tracker` (or the
  TestUT test) before packaging the world; `tools/ap_poptracker_utmap.py --apworld <built
  so2.apworld>` with the release pack zip.
- Layout: 794 rooms (was 793; Port Hilton's pier 171), 862 images in a full render.

### How to verify
- UT map tab: `env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS
  XDG_RUNTIME_DIR=$PWD/scratchpad/maptab/xdg/run SKIP_REQUIREMENTS_UPDATE=1 prlimit --as=$((8<<30))
  scratchpad/ap/ap-venv/bin/python tools/ap_poptracker_utmap.py [--apworld BUILT]`;
  batches: `bash scratchpad/maptab/run_batches.sh AP_DIR OUT`, then from scratchpad/ap/Archipelago
  `PYTHONPATH=. ../ap-venv/bin/python ../../maptab/identity.py OLD NEW`.
- Pack: `tools/build_poptracker.sh all`; smoke: `env -u DISPLAY -u WAYLAND_DISPLAY -u
  DBUS_SESSION_BUS_ADDRESS XDG_RUNTIME_DIR=$PWD/scratchpad/maptab/xdg/run SKIP_REQUIREMENTS_UPDATE=1
  prlimit --as=$((8<<30)) scratchpad/ap/ap-venv/bin/python tools/ap_poptracker_smoke.py --scratch
  scratchpad/maptab/smoke --port 38395`; memory: `python3 scratchpad/maptab/mem.py ZIP DIR`.
- Area audit: `SS_DISC1=<cue> python3 tools/ap_tracker_maps.py --check`;
  `SS_TEST=tracker_ SS_DISC1=<cue> engine/build-maptab/ss_game_tests` (from a scratch cwd);
  `SS_DISC1=<cue> python3 tools/ap_tracker_maps_test.py engine/build-maptab/shattered-stars --scratch scratchpad/maptab/e2e`;
  `python3 scratchpad/maptab/cmp_layout.py OLD.json NEW.json` (group moves).

## Log
- 2026-10-27: log created; reading.
- 2026-10-27: area audit done and verified (above); committing.
- 2026-10-27: pack part 2 done and verified (above); committing.
- 2026-10-27: UT map tab done and verified (batches identical, tests, real UT); committing.
