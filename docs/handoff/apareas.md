# Handoff: Archipelago map-to-area audit (apareas agent, 2026-10-23)

## Assignment
Audit field.json `map_areas` (tools/ap_extract.py `areas()`, a nearest-banner search) against
docs/modding/reference/map_names.csv `area`; trace every use in the extraction, the door table,
the APWorld logic and the port; decide whether any location's open-world requirement is wrong;
fix at the source (real hazard: regenerate, full battery, world_version 0.1.1, changelog) or,
if cosmetic, fix the labels and show the logic and seeds are byte-identical.

## Status (14:30)
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
