# Handoff: Archipelago map-to-area audit (apareas agent, 2026-10-23)

## Assignment
Audit field.json `map_areas` (tools/ap_extract.py `areas()`, a nearest-banner search) against
docs/modding/reference/map_names.csv `area`; trace every use in the extraction, the door table,
the APWorld logic and the port; decide whether any location's open-world requirement is wrong;
fix at the source (real hazard: regenerate, full battery, world_version 0.1.1, changelog) or,
if cosmetic, fix the labels and show the logic and seeds are byte-identical.

## Session 2 (started 13:57; the first session ended at ~13:54 and its work is in checkpoint a123cbd)
- Found: e2e run 2 passed 25/25; late proof Claude finished (proven/checks/maps/nodes same as
  docs, routes/edges order differs); late Rena was killed mid-run (616 nodes) with the session.
- New rule: every game run under tools/memcap.py (4 GB). The proofs and e2e above ran uncapped,
  so they are rerun: first wrap ap_late_proof.py / ap_e2e_so2.sh launches with the cap.
- Done 14:20: tools/ap_late_proof.py runs every game under memcap (capped/Watch/hit_cap; a
  run at the cap raises MemoryCapHit, the proof exits 3, never retried); tools/ap_e2e_so2.sh
  runs both games through run_game (memcap.run_capped; $W/memcap.txt; two new checks, so
  27 checks) and takes AP_E2E_WORK (must be under scratchpad/). e2e run 3 under the cap
  (bin/tele = build-enemy, work scratchpad/apareas/e2e/work3): 27/27, both runs rc 0, not at cap.
- Running since 14:22: scratchpad/apareas/late_proof2/run_all.sh (setsid; pid in driver.pid):
  late claude+rena at --jobs 2 each, then teleporter claude+rena; per-game work dirs and
  JSONs late_proof2/{late,teleporter}_{claude,rena}.json; status.txt, all.done at the end.
  Compare with docs/ap/late_proof.json (proven/checks/maps/nodes).
- Teleporter lead: REAL (14:50). scratchpad/apareas/tele/walkmap.py rasterises the Expel world
  map's walkable polygons (blocks 0xfd4+i, block_query's formats) and flood-fills: Arlia's
  exit is on the Cross continent (Arlia, Salva, Cross, Clik, Mars, Herlie, Mountain Palace,
  Lasguss, Cross Cave); the Lacour continent (Hilton, Linga, Lacour, Sanctuary, Front Line) is
  another, reached by the Herlie ferry (map 144 sailor 0x7298: chapter >= 16, pays Fol; the
  sweep's K23 fails only for lack of Fol); El continent (229, 232, 557, 625), Hoffmann island
  (231, 478) and the Cave of Trials island (704) are islands reached only by the story ships
  (Hilton 0x79c0: chapters 23-27 / 28 step 0; 169 -> 548: chapter 28 step 0) or by flight
  (flag 11, set only on Nede maps 417 / 588; Expel's Synard spot gs+0x26c is set only by
  map 18 0x5a4 under chapter >= 60 && flag 730). The teleporter proof fired map 1's T21/T23/
  T49/T51/T53/... directly, so 26 Hoffmann chests + 2 Eluria Colony gifts are `tele` and may
  hold ANY progression in open world: a Nede-story key there is lost if skipped before the
  switch. Fix plan: ap_late_proof.py teleporter state drops map-1 exits to those 6 maps;
  rerun teleporter proof; regenerate; battery.
- Fix done (~14:40, real clock; earlier times in this section were guesses): tools/ap_walkmap.py
  (the terrain tool; --check agrees with ap_late_proof.py TELEPORTER_ISLANDS = {229, 231, 232,
  478, 557, 704}; same at --step 32 and chapter 60). ap_late_proof.py: teleporter state drops
  map 1's exits to those maps (a note on node 1). The docs' teleporter result is exactly a
  --reparse of scratchpad/late_proof/teleporter's logs (checked with the HEAD tool), so the new
  docs/ap/late_proof.json = the filtered --reparse of copies of those logs
  (scratchpad/apareas/tele_reparse): late part byte-identical; teleporter Claude 418 maps / 153
  locations, Rena 418 / 153 (was 583/265, 470/187); 498 surviving routes unchanged.
  data.py regenerated: 28 rows lose `tele` (Hoffmann 26 chests, Eluria Colony 2 gifts) + cave
  rows (unused). Logic (scratchpad/apareas/classdiff.py + logicdiff.py vs pre_tele/so2):
  story mode unchanged; open world with teleporter + expel_progression: those 28 go "any" ->
  "expel keys" and lose the teleporter access mode. Nothing else.
- Hazard seed (scratchpad/apareas/hazard2, gen.sh WORLD OUT): plando Red Crystal Pass in
  "Hoffmann Ruins: Cure Stone chest": pre world accepts (sphere 10, early mode); fixed world
  refuses (falls back to Eluria Tower: Berle boss). Batch 0.1.1-pre (batch/new): 43 of 45
  open-world slots with teleporter+expel_progression put a Nede-story key (65) or another
  player's progression (78) there (tele/islandcount.py; check.py with new data: 43 errors).
- Test: test_teleporter_islands (OpenWorldChecks, x3): world tests 101 passed, 7986 subtests;
  it fails on the pre-fix world (pre_tele_test).
- 14:29: AP general tests 379 passed (116977 subtests; was 116984 before, 0 failures).
  Running: scratchpad/apareas/battery2.sh (batch seeds 1-96 -> batch/tele; status in
  battery2.status, battery2.done). docs/ap/apworld.md updated (teleporter proof text, known
  limits, Files/proof notes, 0.1.1 changelog bullet); Testing results still to update.
- NEW LEAD, not fixed (design decision): the El voyage (Hilton -> 548 at chapter 28 step 0) is
  one way: El maps' only exits are their own world-map component and the disc switch (625).
  So the Cross/Lacour continents and Hoffmann close from chapter 28 to the switch; our
  stage-11/12 keys (El Beach, Eluria, Eluria Tower Passes) placed there are lost if not
  picked up before the voyage. Batch 0.1.1-pre: 112 of 114 open-world slots have one off El
  (tele/elcount.py; Hoffmann 111, Salva 43, Cross Cave 42...). El's own locations can't hold
  them (El Beach rows need the El Beach Pass, Eluria's the Eluria Pass, Eluria Tower's chests
  are not late-proven), so a placement rule alone would fail solo fills: options are making
  them starting keys, a port-side warning or ferry back, etc. Same in 0.1.0.

## Status (first session, written 12:59)
- **Verdict:** the labels are cosmetic (no logic reads them), fixed anyway with names kept.
  Checking every requirement turned up a **real, unrelated hazard: two ship landings missing
  from the open-world logic**, which leaves about 4 in 10 open-world 0.1.0 seeds
  unfinishable. Fixed in 0.1.1 (not published).
- Done: steps 1-5, world tests, AP general tests, doors validator, 96-seed batch, e2e 25/25,
  port demo of the landings, docs, changelog, version 0.1.1.
- Running: the late + teleporter proofs (scratchpad/apareas/late_proof/run_all.sh; status.txt,
  all.done when finished; ~3 h at --jobs 3, one game at a time); e2e run 2
  (scratchpad/apareas/e2e/run2.log). When the proofs end: compare
  scratchpad/apareas/late_proof/late_proof.json with docs/ap/late_proof.json (games' `proven`,
  `checks`, `maps`; teleporter likewise). Its inputs are unchanged (the proof seed's content is
  identical, the field.json diff is area labels only), so expect the same result. Then append
  the checkpoint line and report.

## Findings
**Mismatches: 160 maps** (scratchpad/apareas/mismatch.py), settled from doors.json:
- 128 Cave of Trials maps 698-825: "Cave of Trials Level 1" vs "Cave of Trials" (one cluster
  entered at 704; "Level 1" is the first floor's banner). CSV right.
- 27, 29, 32 "Rena's House", 38 "Rena's Room" -> Arlia Village; 81 "Bar Mikado", 83 "Inn Kingdom
  Hotel" -> Town of Cross: the building's own banner; doors only reach the town. CSV right.
- 143 Herlie -> Zand's Mansion (Herlie 142 <-> 143 -> 159). CSV right; key Herlie either way.
- 398, 399, 401 Armlock -> Heraldry Weapons Laboratory (chapter-53 rooms between 400 / 395 and
  313). CSV right.
- 426, 427, 434, 467-477 Port Hilton -> Lacour Castle (427 links castle rooms 420/428/431-435;
  434 -> coliseum 468; Hilton only by the chapter-28 scene 422 -> 169). CSV right.
- 593-595 North City -> Field of Love (switch platforms between 591/592, flags 516/517, and the
  gate 595 -> 596). CSV right.
- 617-619 L'Aqua -> The Outer Wall (625 -> 618 -> 617/619 -> 248; no door to L'Aqua). CSV right.
- 231 (no label) vs CSV "Eluria Colony": **CSV wrong** (for the naming agents; the CSV is
  read-only here): 231 is its own world-map entrance, the beach where the Hilton ship lands for
  Hoffmann Ruins (flag 22, chapters 23-27; stores at chapter 24). Not applied.
- 28 (no label) vs CSV "Arlia Village": no doors; not settled; not applied.

**Where map_areas goes** (no logic reads it):
- ap_extract.py `area_of` -> field.json `map_areas` and every record's `area`.
- ap_doors.py -> doors.json `from_area` / `to_area` (informational) and `key_group` (names the
  35 entrance groups; grouping agrees with the CSV for all 66 entrances).
- ap_world_tables.py: record `area` -> location names ("<area>: <item> chest"), rows' `area`,
  locations.py LOCATION_NAME_GROUPS; key_group -> KEY_GROUPS `group`, rows' `group`, seed door
  names. Logic: a row's group = `map_groups()` over doors.json; STAGES hand-written; regions
  from field.json regions / map_worlds; windows from conditions.
- Port: the seed's `doors` (id, key) only; names in logs.
- AP's fill sorts locations by name (Fill.py, Location.__lt__), and .apso2 holds names: a
  renamed location changes fixed-seed output, so names are kept.
- Locations on mismatched maps (99): cave rows (no early mode), Rena's Room chest (group Arlia:
  right key), 593 x3 chests (group Armlock "near"; real key Love Pass; never progression),
  Dias tournament boss 477 (losable). None has a wrong key that matters.

**The hazard (not label-made; the labels had hidden it: apworld.md justified stage 7's Hilton
key by the coliseum's "Hilton banners").**
- Chapter 17 is stored boarding the ship at Herlie (map 144, flag 18); the world map's on-load
  routine 0x57d8 lands it through door 59578036 (Hilton's key group); chapter 18 is stored
  entering Lacour from the world map (909 routine 0x47e8). Prima: Herlie is where you "catch a
  ship to the continent of Lacour" (p. 46), Hilton its only harbour (p. 58). 0.1.0's stage 6 needed only Herlie,
  so Lacour's early mode (stage 6 + Lacour Pass) let the **Hilton Pass sit in Rosalia's Room**.
- Hoffmann Ruins: the Hilton ship (flag 22 at 169, chapters 23-27) lands through door 59578306
  on map 231 (key "Map 231 Pass"); flag 21 sails back. World coordinates: Hoffmann (85,62) and
  231 (93,60) lie north of the Front Line (103,46), "the northernmost tip of the continent"
  (Prima p. 70). 0.1.0's Hoffmann early mode (stage 8 + Hoffmann Pass) let the **Map 231 Pass
  sit in Hoffmann's 26 progression-capable chests**.
- Proof: AP 0.1.0 accepts plando seeds with those placements (scratchpad/apareas/hazard/out,
  playthrough sphere 7 "Story: Herlie", sphere 8 Rosalia -> Hilton Pass); 0.1.1 refuses them
  (new_out: plando falls back). Port (scratchpad/apareas/demo/run.sh): flag 18 at chapter 17
  -> "door 59578036 ... locked, needs Hilton Pass", with key 11 -> map 169; flag 22 at chapter
  23 -> "door 59578306 ... locked, needs Map 231 Pass", with key 7 -> map 231.
- Baseline batch (0.1.0, seeds 1-96, scratchpad/apareas/batch/base): 45 own-key deadlock
  placements in 44 of 114 open-world slots (38 seeds). Fixed batch: 0.

**Fix (0.1.1):** ap_world_tables STAGES stage 6 = Herlie + Hilton + Lacour, stage 7 = Lacour;
SHIP_LANDINGS {Lacour: Hilton, Hoffmann: map 231} -> KEY_GROUPS `landing`; rules.py early mode
adds the landing keys (LANDING_OF_GROUP). Logic diff (scratchpad/apareas/logicdiff.py OLD NEW):
182 locations' open-world access changed, story mode and every progression class unchanged:
- +Hilton Pass: Lacour-group rows (Rosalia's Room chest; 31 gifts, 65 shop slots, 1 boss, all
  never-progression), their stage 6 -> 5 where it was the first stage (equivalent: stage 6
  itself now needs Hilton + Lacour);
- Hilton-group rows: stage 6 -> 5 (1 gift, 47 shop slots, 1 boss; never progression);
- +Map 231 Pass: Hoffmann Ruins' 30 chests, 4 joins, 1 boss.

**Label fix (cosmetic):** ap_extract.py AREA_FIXES (the CSV's areas, door-justified);
ap_doors.py key_group = target banner, else area (same 35 names); ap_world_tables.py
RELEASED_AREAS + area_label() keep the 0.1.0 location names / name groups. With only these,
data.py was byte-identical (scratchpad/apareas/fix1). field.json diff: `area` values only;
doors.json: from_area / to_area only.

## Battery (0.1.1)
- World tests: 98 passed (95 + test_ship_landings x3), 7986 subtests.
- AP general tests: 379 passed, 116984 subtests.
- Batch seeds 1-96 (2 SO2 + ChecksFinder, random options): 192 .apso2, 189 combinations, all
  generate, check.py 0 errors, 5305 progression items; doors validator 192/192; all 16
  all-story seeds byte-identical to the 0.1.0 batch (scratchpad/apareas/compare.py).
- e2e (copy of tools/ap_e2e_so2.sh with work dir scratchpad/apareas/e2e/work, binary = the
  phase-4 build-enemy copy): 25/25 (run 1). Run 2 pending.
- Late + teleporter proofs: running (see Status).

## Leads (not fixed; unverified)
- The proofs fire world-map triggers without walking there. After the Final Save Point the
  Synard flies anywhere (Prima p. 130), so the late proof holds; the teleporter's chapter-38
  trip (flag 730 clear) may have no Synard, and El (ship only at chapter 28), Hoffmann (ships
  chapters 23-27) and the Lacour continent's ferry would then be out of reach: tele-proven
  locations there may hold any progression. Check flight in the teleporter state.
- Central City at chapters 36-37: entered by the 619 -> 248 scene; 248 -> 247 -> 246 -> 243 ...
  -> 235 -> world map are unconditional; if the mayor's scene (247, chapter 37) does not start
  by itself, a player who walks out needs the Central City Pass (first required at stage 17).
- Early mode still assumes an area's entrance is walkable from the stage before its first
  visit (maps that open later, Nede's geography): apworld.md "Known limits".

## Files touched (none committed)
- tools/ap_extract.py (AREA_FIXES), tools/ap_doors.py (key_group naming),
  tools/ap_world_tables.py (RELEASED_AREAS, area_label, STAGES 6/7 + comment, SHIP_LANDINGS,
  `landing`), apworld/so2/{rules.py, data.py (regenerated), archipelago.json (0.1.1),
  test/test_world.py (test_ship_landings)}, docs/ap/{field.json, doors.json (regenerated),
  apworld.md (stage table, early mode, known limits, "Changes"), field.md (Place names)},
  docs/handoff/apareas.md.

## How to verify
- Regenerate (deterministic): `python3 tools/ap_extract.py DISC1.cue DISC2.cue --no-battles -o
  docs/ap/field.json; python3 tools/ap_doors.py DISC1.cue -o docs/ap/doors.json; python3
  tools/ap_world_tables.py` -> no diff.
- `python3 scratchpad/apareas/mismatch.py` (2 left: 28, 231), `python3
  scratchpad/apareas/logicdiff.py scratchpad/apareas/old/apworld/so2 apworld/so2`,
  `python3 scratchpad/apareas/compare.py scratchpad/apareas/batch/base scratchpad/apareas/batch/new`,
  `scratchpad/apareas/demo/run.sh` (1 game process at a time).
- Tests: docs/ap/apworld.md "Testing".

## Rules given
- Logic must not drift unless it fixes a proven error; any change re-proven with the full
  battery (world tests, AP general tests, doors validator, 64+ seed batch 0 errors, late proof,
  port e2e on a local server).
- AP servers only on 127.0.0.1 (tools/ap_test_server.sh); never archipelago.gg. At most 2 AP
  servers.
- No game data in git beyond docs/ap's IDs/offsets/names. Headless only. Scratch
  XDG_CONFIG_HOME under scratchpad/apareas/; never ~/.config/shattered-stars or pcsx-redux.
- Kill only own processes by exact PID; every wait loop has a deadline; no `pgrep -f` of own
  pattern. Scratch in scratchpad/apareas/ (not /tmp). At most 4 game processes. Builds
  engine/build-apareas with -j4.
- Don't touch region-profile / FR-DE text agents' files; docs/modding/reference/*.csv read-only.
- No commit / push. One "apareas: " line in scratchpad/checkpoint_notes.txt when done.
- Don't publish a .apworld; the coordinator asks the user.

## Proofs finished (coordinator, 15:45)
- The late proofs on 0.1.1 match docs/ap/late_proof.json: Claude and Rena each prove 278 locations
  with 156 checks over 610 maps. The state block gains the `expel_teleporter` field, and Claude's
  node count is 731 (it was 732).
- The teleporter proofs prove the same 153 locations and 85 checks in both games. They reach 423
  maps instead of 418 (gained 441-444 and one more: the ship-landing routes the fix added).
- docs/ap/late_proof.json now holds the new results, written in the same JSON format. Running
  tools/ap_world_tables.py on it gives a data.py byte-identical to the committed 0.1.1 data.py,
  so the logic is unchanged.
- 0.1.1 ships with 0.02.00 (the user's decision); there is no separate hotfix release.

## Lead closed: flight on an early teleporter return (coordinator, 2026-10-23 23:40)
Question (the user): does the party get the Synard on Expel when the open-world teleporter
brings it back? Answer: **not before the Final Save Point; 0.1.1's logic stands.**
- Expel's world map spawns the Synard (actor 0xd) whenever flag 11 is set, at its Expel spot
  game_state +0x26c / +0x28c / +0x270 (map.cpp:564-570). Only Virtual Arlia's arrival (map 18
  0x5a4, chapter >= 60 and flag 730) writes that spot; the teleporter's three patch lines change
  only the old man's and the guide's chapter tests (seed_format.md "The Expel teleporter").
- Live run (scratchpad/synard/run_early.log): --map 18 at chapter 41, flag 11 set, flag 730
  clear, then to map 1: the spot stays 0, so the Synard sits at the map's origin; the party is
  at Arlia's exit (86054, 25733).
- Terrain (scratchpad/synard/origin_reach.py, tools/ap_walkmap.py's grid at step 16, both NA
  discs): no walkable cell within the boarding reach (0x105 units, field_world.cpp:310-318) of
  the origin: open sea. So the Synard can't be boarded, and Hoffmann Ruins, El and the Cave of
  Trials island stay out of the teleporter's reach until the Final Save Point, as
  TELEPORTER_ISLANDS assumes.
- After the Final Save Point the Synard is parked at Arlia and reaches the Cave of Trials; the
  apworld puts every Cave of Trials location after the Final Save Point event, so that
  requirement is covered. (A plain debug warp at chapter 60 did not reproduce Arlia's parking:
  it needs the real Virtual Expel arrival; that case rests on the script reading.)
- Possible enhancement (the user's call, not planned): park the Synard on an open-world
  teleporter return; the islands could then hold progression (a 0.1.2 logic change plus proofs).
