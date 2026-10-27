# Handoff: mapdoors (doorways and room names on the tracker maps, part 1)

Agent "mapdoors", started 2026-10-27 by the Phase B coordinator (docs/handoff/phaseb.md), for
port 0.03.00 / pack 1.0.0. Part 1 = layout data + the port's renderer + names. Part 2 (the pack
side: door locations, nested Area > Room tabs, the click handler, the PopTracker smoke) is a
later agent's, after "logicexport" finishes with the pack generator.

## The user's request (2026-10-27, verbatim)
"The strategy guide doesn't add room names either for dungeons and the like. What it does is draws
lines from one door to the other across the page. I think if we could, we could simply have the map
number that a door leads to on the image to be able to follow the pathway. If it allows for
clickable transitions between them, then this would be our method. Click on the doorway with the
mapTo label and that map then shows up. Anything that we CAN name correctly, we should. Houses,
Shops, etc."

The coordinator's PopTracker findings (scratchpad/trackers/poptracker/doc/PACKS.md):
`ScriptHost:AddOnLocationSectionChangedHandler(name, f(section))` (since 0.26.2) fires when a
player clicks a section (`section.FullID`, read/write `AvailableChestCount`);
`Tracker:UiHint("ActivateTab", tab name)` switches tabs; layouts can nest `tabbed` items. Planned
method: each doorway is a map location with one non-AP "door" section labelled with the
destination map; clicking it makes the pack's Lua activate the destination room's tab and reset
the section (a door never counts as checked).

## The brief
1. Names (map_names.csv): a correct place name for every map where the game's own data or text
   supports one (houses by resident, shops from the shop data, inns, pubs, guilds, churches,
   castle rooms...); evidence per renamed map (column or companion CSV); the guide
   (scratchpad/guide) for confirmation only, never its prose. Rooms without a solid name stay as
   they are (identified by number). Short factual names only.
2. Doors in the layout (docs/ap/tracker_maps.json, additive): per door of every drawn map its pixel
   position (trigger region centre, clamped as today) and destination map id (+ name). Rooms: every
   map of an area the door graph reaches from the area's entrance is drawable; per-room images
   (images/rooms/<map>.png, background at a readable scale) grouped by area; area images stay the
   overview; each check also gets its pixel on its room image. Measure the cost (rooms, placeholder
   pack size, rendered set size; budget: pack zip <= ~20 MB, rendered <= ~400 MB; if over, propose
   cuts in this log). Keep every existing key and meaning; bump a format minor version; document
   the new keys in docs/ap/tracker_maps.md with a "For the pack (part 2)" section.
3. Labels on the images: each door marked with its destination map number (+ short name); each
   room's own number and name in its label strip. Label boxes' positions and texts in the layout
   (one source of truth for the port and the placeholder generator); readable, off the check dots.
4. The port's renderer (`--render-tracker-maps` and the setup button: same function): room images
   too, with door labels; deterministic; same bytes NA/EU/FR/DE where today they match; tests in
   engine/tests/test_tracker_maps.cpp and tools/ap_tracker_maps_test.py; A/B normal play unchanged.
5. Docs: docs/ap/tracker_maps.md, docs/release_notes/0.03.00.md, this log.

Files I may touch: docs/modding/reference/map_names.csv (+ a companion evidence file beside it),
tools/ap_tracker_maps*.py, docs/ap/tracker_maps.json and .md, engine/src/tools/tracker_maps.*,
engine/src/util/** only if needed, engine/tests/test_tracker_maps.cpp, engine/CMakeLists.txt only if
needed, docs/release_notes/0.03.00.md, docs/catalog rows I add, docs/handoff/mapdoors.md.
NOT: apworld/, tools/ap_poptracker*.py, tools/poptracker/**, tools/build_poptracker.sh,
dist/poptracker, docs/ap/poptracker.md, docs/ap/seed_format.md, docs/ap/apworld.md,
docs/release_notes/0.2.2.md (agent "logicexport" edits those). Checking today's pack generator:
`tools/ap_poptracker.py` with an output dir under scratchpad/mapdoors/ only.

## Rules given
- Headless only (--headless, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy); nothing may open
  a window or dialog on the user's desktop; third-party programs run without DISPLAY /
  WAYLAND_DISPLAY / DBUS_SESSION_BUS_ADDRESS.
- Every game process under tools/memcap.py (4 GB); at most 2 game processes at once; own build dir
  engine/build-mapdoors, builds -j6; scratch XDG_CONFIG_HOME and HOME; never a config-saving flag,
  never ~/.config/shattered-stars/config.ini, the user's saves, a real PopTracker folder or
  ~/.local/share/Archipelago; kill only by exact PID.
- Disc 1: SS_DISC1 = "/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/Star Ocean - The Second Story (Disc 1).cue"
  (disc 2 beside it, "(Disc 2)"; other regions under .../Emulation/Roms/Star Ocean 2/).
- Commit on main after each verified piece (names; the layout; the renderer), docs first, staged by
  path (never git add -A / -u); message ends with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never commit a rendered
  image. Never push, tag or publish. index.lock: wait and retry, never delete it.
- A denied tool call: don't retry; write the blocked step here and stop, naming the exact command.
- A choice that is the user's (what gets published, a big scope change, a size cut): write it here
  with a recommendation and continue with the rest.
- Cap command output; long jobs in the background with deadlines; scratch under scratchpad/mapdoors/
  (not /tmp, which is RAM); delete big scratch when done.
- Prefer the port's own code; if disassembling, use the REA MCP tools and catalog anything new.
- Keep this log current after each finished step and at least every 30 minutes (done / in progress
  with files / next / how to verify / rules given). Roll over at ~400k context: update and stop.

## State
- Done (committed ae78aa2): names: 33 maps renamed in docs/modding/reference/map_names.csv
  (place / notes / basis only; `area` and `kind` untouched), each with its evidence in
  map_names_evidence.csv (map, place, previous, basis, evidence: banner, script text described
  not quoted, shop stock, doors, thumbnail). tools/test_mod_reference.py 21 OK. Not regenerated
  (not my files): docs/modding/reference/maps.csv / doors.csv (`tools/mod_reference.py DISC1.cue`
  picks the new names up).
- **Assignment complete** (2026-10-27). Done, committed (0163425 docs, 389d650 layout, 6430541
  notes, 3e7a2d1 renderer + tests): layout 1.1 (tools/ap_tracker_maps.py, docs/ap/tracker_maps.json,
  docs/ap/tracker_maps.md with "Rooms", "Door ids" contract and "For the pack (part 2)"), the
  port's renderer (engine/src/tools/tracker_maps.cpp: rooms 1:1 under a label strip, a letter
  box per doorway; draw_text / draw_text_box; 1:1 blit), tests (engine/tests/test_tracker_maps.cpp
  new tracker_layout_rooms + two rooms in tracker_disc_command; tools/ap_tracker_maps_test.py
  covers rooms, prints time and sizes), release notes (docs/release_notes/0.03.00.md).
- Measured: 793 rooms in 66 groups (105.0 M px), 1775 doorways (1822 markers, 6 clamped), 2011
  doors (1836 with a way back), 378 scene transfers; 31 maps with a background not reached
  (opening/story scenes, door-less Mountain Palace maps, unused slots). Full render 20.8 s,
  861 images (68 + 793), 134.6 MB (rooms 93.1 MB); placeholder rooms ~2.8 MB (2.5 MB zipped,
  PIL default font) on top of today's 3.1 MB pack zip. Both within budget: no cut needed.
- Results: ss_game_tests SS_TEST=tracker_ 7/7 (with SS_DISC1); e2e 861/861 images, sizes,
  same bytes twice, nothing stray, XDG untouched; NA/EU/FR/DE disc 1 renders byte-identical
  (861 each); A/B vs the baseline (worktree of ae78aa2 in scratchpad/mapdoors/base): 12 headless
  runs, 20 files, 0 differ; today's pack generator builds from the 1.1 layout (scratchpad output)
  and tools/ap_poptracker_test.py 23 OK; smoke (tools/smoke_field_events.sh) 373 PASS / 0 FAIL;
  full ss_game_tests 223/0 (with SS_DISC1).
- How to verify: `SS_DISC1=... python3 tools/ap_tracker_maps.py --check`;
  `SS_TEST=tracker_ SS_DISC1=... prlimit --as=$((4<<30)) engine/build-mapdoors/ss_game_tests`;
  `SS_DISC1=... python3 tools/ap_tracker_maps_test.py engine/build-mapdoors/shattered-stars --scratch scratchpad/mapdoors/e2e`;
  spot checks: `SS_TRACKER_DOTS=1 SS_TRACKER_ONLY=rooms/18. python3 scratchpad/mapdoors/run.py
  engine/build-mapdoors/shattered-stars LOG 300 --render-tracker-maps scratchpad/mapdoors/outN`;
  regions: scratchpad/mapdoors/regions.sh; A/B: scratchpad/mapdoors/ab.sh.

## Findings
- **Trigger regions are in screen coordinates, not world ones** (trigger_check_regions tests the
  leader's actor +0x74/+0x76 while field_state +0x68 == 0): a region's background pixels are its
  corners minus MapScene +0x28/+0x2c. Checked over all field maps: the reverse door's arrival point
  lands within 40 px of the region for 1636 of 1846 doors this way, 86 the old way (projecting
  (x, floor, z)). maprender's layout placed its 48 `region` checks the old way (11 of them
  "clamped"); fixed here (0 clamped now), and those checks no longer carry `world`.
- map_names.csv `area` feeds the pack generator (map -> tab, scripts/map_tabs.lua); nothing in
  apworld/ reads map_names.csv (tools/ap_extract.py / ap_world_tables.py mention it in comments
  only). So only `place` / `notes` / `basis` change here, never `area`.
- Some `area` values look wrong (52/53 "Arlia Village" are Salva: Alen's mansion, the drift
  guard) - left for a later pass since `area` drives the pack's tabs.

## Log
- 2026-10-27: log created; design change recorded; room set + door extraction + renderer drafted;
  region coordinate fix found and applied.
- 2026-10-27: names committed (ae78aa2); layout 1.1 committed (0163425, 389d650); renderer verified
  (unit 7/7 tracker_, 223/0 all, e2e, regions, A/B, smoke 373/0); notes + renderer committed
  (6430541, 3e7a2d1). Scratch cleaned (baseline worktree removed, renders and decoded text deleted).
- Open, for the coordinator / the user: (1) the setup button now takes ~20 s (was ~7 s) and runs
  on the setup window's thread, so the window does not redraw meanwhile: a worker thread in
  engine/src/setup/setup_ui.cpp (not my file) would fix that; the CLI is fine. (2) map_names.csv
  `area` errors seen (52/53 are Salva, filed under Arlia Village): `area` drives the pack's tabs,
  so left alone. (3) docs/modding/reference/maps.csv / doors.csv want a regeneration
  (`tools/mod_reference.py DISC1.cue`) for the 33 new names (not my files). (4) Phase C: the
  door id contract (doorway id = lowest doors.json id; ER sends a doorway's doors together) is a
  recommendation in tracker_maps.md, to be confirmed when ER is designed.

## Design change from the user (2026-10-27, via the coordinator), verbatim
"Those labels for doors may need to be live. Keep in mind if we move forward with the Entrance
Randomiser and the unconnected doors, the logic engine needs to be able to follow routes to get from
one place to another. The tracker would need the correct toMap draws for each door."

The coordinator's revision of part 1 (rules from here on):
1. No destination text baked into any image (port render or placeholders): under ER (Phase C) a
   door's destination differs per seed and may be unknown until found. Images carry only static
   things: each room's own number + name in its label strip, and a door marker at each door's
   position with a **letter** (A, B, C... in a stable order per room, e.g. by door id).
2. Live destinations come from data, drawn by the tracker (pack part 2): items' BadgeText /
   SetOverlay can change at run time, so part 2 gives each room tab a door panel ("A -> 412 Salva
   Drift"), filled in Lua from (vanilla) a door table shipped in the pack, or (ER) the seed's
   pairings in slot_data plus the found-entrances data-storage key; clicking a door marker or its
   panel item switches to the live destination's tab.
3. A stable door id for every door, the same id Phase C's entrance randomiser will use as the
   entrance name, derived from docs/ap/doors.json's own door identity (most stable form;
   documented in docs/ap/tracker_maps.md as a contract for Phase C and the world's logic export).
   Per door in the layout: id, map, letter, pixel position, label-box position for the
   panel/legend, and the **vanilla** destination (map id, and the destination door id if known)
   marked as the default only.
4. The rest of the brief stays (names, per-room images, the renderer, sizes, tests). "For the pack
   (part 2)" describes the live door panel and click switching on these ids.
