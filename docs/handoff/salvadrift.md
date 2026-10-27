# Handoff: salvadrift (hub mode: Salva Drift from Alen's mansion leaves the hero off the floor)

Agent "salvadrift", started 2026-10-26. Spec: the coordinator's brief (user report below).

## The report
"I found a bug in hub mode. I tried to enter Salva Drift from the mansion (because the bookcase
was still moved), and it said Salva Drift pass needed, but my sprite walked off the map plane and
into nowhere, and got stuck. I think this is the only dungeon entrance like that though."

## Rules given
- Headless only (--headless, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy). Every game
  process under 4 GB (tools/memcap.py / prlimit), at most 2 at once; builds -j4. Scratch
  XDG_CONFIG_HOME; never a config-saving flag; never the user's config or saves.
  SS_AP_ANY_SAVE=1 only for headless loads of ordinary saves with a seed. Kill only by exact PID.
- Enhance, never degrade; AP off = the disc's game untouched; with the key identical to before.
- Scratch in scratchpad/salvadrift/. Own build dir engine/build-salvadrift.
- No commits, no pushes. Past ~400k tokens: update this file and stop.

## Status (2026-10-26)
- Done: diagnosis, reproduction, port fix, unit check, smoke case, door sweep, docs. All
  uncommitted for review. Final full smoke rerun with the new case: 285/0
  (scratchpad/salvadrift/smoke2.log).
- Suites (engine/build-salvadrift, final code): ss_tests 103/0, ss_game_tests 193/0 (with the new
  ap_doors_and_keys checks), smoke 285/0 (smoke2.log; ap_hub_mansion fails on the pre-fix build:
  the hero is at -26,-98,-274 at vblank 420 and the key opens nothing). APWorld tests not run:
  the APWorld is unchanged.

## Diagnosis
- Map 53 = Alen's room in Salva with the bookcase moved (map 46 -> 53 when flag 111, -> 52
  otherwise; the hub state has 111 set, flag 2 clear, chapter 60). Its init (0x6db8) registers
  four regions; region 2 = `thread_start_ex(159,120, 167,133, 198,138, 191,127, 0, 6914, 0, -1000,
  -1)` at 0x6e0c: view quad, y -1000..-1, code word 6914 = routine **0x6c08** (door 3480401's
  routine, so the seed row's `routine` matches). 0x6c08: flag 2 -> a message and a scripted walk
  back (call 0x0628, -27,-48); chapter 7 -> a scene and the same walk back; else (the hub)
  ext_ba(60) + map_transfer_c(62, 128, ...) at 0x6d44. No scripted walk before the transfer.
  Region 3 (code 6998 = 0x6d58) is the stairs' approach: music_preload 32 + map_preload 62.
- The geometry (tools/map_zones.py 53; walls: scratchpad/salvadrift/walls.py 53): floor zones 2/3
  are the passage's ramp (z -71 .. -169, y 0 .. -100); walls 14/15 line only its two sides; nothing
  closes its far end. A field actor off every floor zone keeps its height and walks freely
  (collision.cpp walk_snap_to_floor: no zone, y unchanged) -> an endless plane at y -98.
- So the routine-level lock worked as designed (ap.cpp door_blocked_at_trigger: no "trigger 2
  fires" trace, the banner, the routine never runs) but left the hero in control **inside** a
  region the original never lets anyone stand in (the transfer starts at once; its own refusals
  walk the hero back). Holding Up: down the ramp, past its end, into the void (probe4/probe5:
  z -745 and on; drawn over the bookcase or hidden behind the room). Not a scripted walk, not a
  missing `routine`, not a state-dependent routine.
- Same at every field-map door refused at its trigger (sweep below): the hero walks on past the
  exit. The mansion is the most visible one (the hero vanishes).

## The fix (port only; no seed / APWorld change, existing seeds keep working)
- engine/src/game/field/script_triggers.cpp `ShutDoor` (+ trigger_check_regions): once
  ap::door_blocked_at_trigger refuses a region on a field map, the leader gets no further in than
  the step that entered it (a step ending deeper along that step's line is put back to the last
  spot via fieldactor_place; stepping back / aside is free; 2-step tolerance for ramp drift).
  Banner once per approach (the approach lasts while the leader is within 8 px of the region's
  bbox). The door opening meanwhile (key received) fires at the next step in, not under a
  standing hero. World maps (field_flags[0x68]) and a refusal without a known previous spot
  (first scan on a map / after a scene) keep the old behaviour. SS_TRACE line "shut door region
  N: leader x,y,z, entered along dx,dz (1/256)". Button regions: each press refuses as before.
  (Tried first and dropped: putting the hero back outside the region each frame: the view point
  is a whole pixel, so near a slanted edge a step back out still touched the region and froze the
  hero (map 43 "down"); and a pure dot-product rule ping-ponged with region 3 on map 53.)
- engine/src/game/ap/ap.{h,cpp}: door_shut_by (shared lookup), door_shut_at_trigger (no banner).
- engine/tests/test_ap.cpp ap_doors_and_keys: door_shut_at_trigger checks.
- tools/smoke_field_events.sh: ap_p4_door was "the leader walks on" (through the refused
  region: the old behaviour); now "the leader stays at the door", and after the key the hero walks
  right (into the door) again instead of left: trigger 9 fires at 602, map 18 loads. New case
  ap_hub_mansion (map 53 hub seed: held on the ramp at vblank 420; key at 450 -> map 62 at 467).
- docs/ap/seed_format.md "Locked." bullet + the v2 conditions paragraph.

## Verification (scratchpad/salvadrift/runs, cap)
- fix53b / fix43e: held at the edge while pushing; down / left / right free; one banner.
- key53 (key in start items): trigger 2 fires at the same spot and vblank (239) as before, map 62.
- midkey53: key given while pushing: the region fires at the next step, map 62.
- p4door2: map 17 open-world door (the smoke case's new expectations).
- Identity (fixed vs pre-fix build engine/build-hubleader): AP off and with the key, map 53 and
  map 43, Up for 500 frames: identical [leader] traces (139 lines) and an identical frame at 650.
- Frames: cap/compare53.png (top: pre-fix vblank 300 / 500, the hero gone, then drawn over the
  bookcase; bottom: fixed, the hero on the ramp at the region's edge, head showing, banner).

## The door sweep (scratchpad/salvadrift/sweep.py, sweep/run1.out)
From a spot near each door (its dungeon-side arrival), 8 directions, Up etc. for 500 frames,
fixed vs pre-fix build. Every field-map door refused at its trigger let the pre-fix hero walk on:
- hub: Salva town 43 (2824679, T8): onto the cliff picture, ~550 units on; Alen's room 53
  (3480401): off the ramp into the void; Mars village 124 (8134506 strip, after 8134658's
  approach region): off the top of the village, ~1650 on; Zand's 142 (9312652, upper arrival):
  ~300 on.
- open world: Central City exit 235 (15407589): ~1400 on; Noel's House exit 277 (18160748): ~770
  on. Any mode with the v2 goal gate: 671 (43978493): ~130 on (then a wall).
- Fixed: held at the region's edge in every one.
- Not found from the spots tried: Synard 264 (17308906/938; chapter <= 38 doors), Zand 142 lower
  arrival, El voyage 444 (registered only at chapter 27 step 7000; a strip region, so the hold
  matches the original, which starts the voyage at any entry).
- The other pattern (the transfer refused after a script moved the hero): rows reached by a call,
  not a region: 8134067 (124, 0x7190, chapter-15 scene), 9314034 (142, no routine; flag 2 and
  G[0x1d2]), 17832691 (272, 0x677c, chapter 38 step 1000). None runs at the hub's chapter 60
  outside story views; not covered by the hold (nothing to hold: the scene has moved the hero).
- One row is not a doorway in the hub: Mars 8134658 (124, 0x7dbc) transfers only at chapter 15
  step 1950; at chapter 60 its region (the path between the trees up to the exit strip) only
  preloads, yet the port refuses it (a pre-existing early banner) and now holds the hero at its
  lower edge. Benign (that path leads only to the dungeon exit; checked on a capture,
  runs/mars_plaza), but it is the one place the hold stops the hero short of the real exit.

## For the user / coordinator to decide
- The resting spot at the mansion is partway down the ramp (head showing above the floor), where
  the original's region begins. Walking back works. A nicer spot would need per-door data (e.g.
  the dungeon's return arrival as a seed `back` spot: an APWorld + seed change), not proposed.
- Mars 8134658 (above): the APWorld could leave `routine` off rows whose transfer can't happen at
  the hub's state, unless a story view needs it (that is why the row is there: chapter 15).
- Changed behaviour beyond the hub: the same hold applies to open-world exit gates and the v2 goal
  gate (any mode with a seed). Documented in seed_format.md.

## Notes / tools
- Disassembly: `SS_DISC1=<cue> python3 scratchpad/salvadrift/mapdis.py 52 53` ->
  scratchpad/salvadrift/dis/mapN.dis. Walls: scratchpad/salvadrift/walls.py MAP.
- Runs: `python3 scratchpad/salvadrift/run.py NAME SEED FRAMES MAP=53 POS=x,y,z,f SS_HUB_STATE=1
  INPUT=... CAP=...` (BIN=... for another build). Seeds: scratchpad/salvadrift/seeds (nokey =
  hubask's gen18, a real apworld 0.2.0 hub seed; key1 = with the Salva Drift Key in start items;
  p4door, ow_exits, hubmansion).
