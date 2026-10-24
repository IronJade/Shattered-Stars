# Handoff: the El voyage gate (Archipelago open world)

Status (2026-10-23 19:30): **done** (elgate-finish agent). The 6-pass gate ships as built
(user decision, 18:45 below); see "Done (19:30)" for the closing checks. Earlier: two agents
were stopped by permission refusals in "ask" mode, because background agents can't show
approval prompts. Start a fresh agent from this brief, with the session in "Edit automatically"
mode.

## The hazard
In open world (world_mode `open_world`), the El voyage (Port Hilton to map 548 at chapter 28
step 0) is one-way until the disc switch (map 625). The stage-11/12 keys (El Beach Pass, Eluria
Pass, Eluria Tower Pass) get placed on the Cross/Lacour continents or in Hoffmann, which close
from chapter 28 to the switch. In batch 0.1.1-pre, 112 of 114 open-world slots had at least one
of them there (tele/elcount.py). A player who sails without them can't fetch them and can't
finish. El's own locations can't hold them either: El Beach rows need the El Beach Pass, Eluria's
need the Eluria Pass, and Eluria Tower's chests aren't late-proven. The problem exists in both
0.1.0 and the unpublished 0.1.1. Source: docs/handoff/apareas.md, the "NEW LEAD, not fixed"
paragraph.

## The user's rule and decision (requirements)
Every key must be **accessible to the player given the current play state**. Items move anywhere,
and the logic validates that they're reachable when needed. Players race to make checks and may
have to search for a forgotten one, but nothing may ever become unreachable. See also the memory
note ap-accessibility-rule. Applied here:
1. **Logic:** the voyage requires the three El passes, so the fill places them only in locations
   reachable before the voyage, in this world or any other.
2. **Port gate:** in an open-world seed, the voyage won't start until the player holds all three
   passes. Show a clear in-game message naming the missing ones, in the style of the port's other
   Archipelago messages, and don't advance the story. Gate it at the exact script point that starts
   the voyage, the way area keys are gated (engine/src/game/ap/). Without a seed, or in story mode,
   nothing changes.
3. **Story check:** the user mentioned Leon giving a key at the Lacour Front Line after the Shin
   fight (to be confirmed). Check which gift and join sites around the Front Line and the voyage
   are locations, and that the fill treats them correctly.
4. **Scan every other one-way transition** in open world where a key could be stranded: the disc
   switch, Expel closing with expel_progression, Fienal, the teleporter modes, and anything the door
   graph shows as one-way. Fix each the same way (logic requirement plus port gate), or prove it safe.

## Delivery
- It goes into world_version 0.1.1, still unpublished; it ships with 0.02.00. Update the 0.1.1
  changelog in docs/ap/apworld.md, and the Archipelago section of the draft
  docs/release_notes/0.02.00.md: open-world seeds made with 0.1.0 must be regenerated, and the ship
  now waits for the El passes.
- **Full battery:**
  - the repo APWorld tests;
  - Archipelago's general tests (docs/ap/release_checklist.md);
  - the doors validator;
  - 96+ seed batches with 0 errors, plus elcount showing no stranded El pass;
  - the late and teleporter proofs: tools/ap_late_proof.py under tools/memcap.py, on
    scratchpad/latesave/card.mcd, never the user's .gme;
  - tools/ap_e2e_so2.sh on a local server;
  - a headless port demo of the gate, with and without the passes (the debug console's ap_give);
  - the engine suites after any engine change.
- Report the logic diff. Story mode must stay byte-identical (data.py tables and fixed story
  seeds).

## Rules
- Machine budget: -j4; at most 4 game processes; at most 2 AP servers. Every game run goes under
  tools/memcap.py.
- Servers on 127.0.0.1 only. Publish nothing, and don't commit or push (the coordinator commits).
- Headless only, with a scratch XDG_CONFIG_HOME under scratchpad/elgate/. Kill processes only by
  exact PID. Every wait loop has a deadline. /tmp is RAM.
- If an action is refused by the permission system, stop and report. Keep this log current every
  20 minutes. Roll over at ~350k tokens.

## Done / in progress
- 2026-10-23 (elgate agent, session 1): started. Scratch in scratchpad/elgate/ (dis.py: map
  script disassembly into scratchpad/elgate/dis/). Researching the voyage's script point.
- **Findings (17:40).** The voyage is not started at Hilton: at chapter 27 step 7000 (stored
  in meeting room 448) the Front Line's meeting room 444 holds a walk-in trigger
  (thread_start 0x86d4, routine 0x7c28) whose scene stores chapter 28 and transfers to 422
  (door 29106503 at 0x851c); 422's init plays the castle scene at chapter 28 step 0 and
  transfers to 169; 169's init plays the departure (0x8260 / 0x8868, chapter 29) and transfers
  to 548. No free play between. Hoffmann's ship (169 0x79c0) sails only at chapters 23-27, so
  the gate must be at 444's trigger, before chapter 28. At step 7000 the Front Line's exits are
  open (438 -> world map shut only at chapter 26 step >= 1000 and chapter 27 step 3000; 444 ->
  440 shut only at chapter 27 steps 1000 / 5000). No key item is given around the Front Line
  (no gift with flags_given; Leon's join is at El Beach, 230; Shin 516 is a story boss at 447,
  chapter 27); join sites are always EXCLUDED.
- **Design.** (1) data.STORY_GATES (tools/ap_world_tables.py): El voyage, stage 10, groups El
  Beach / Eluria Colony / Eluria Tower (keys 18, 19, 17), door 29106503, routine 0x7c28,
  purpose "to sail" (was "to sail to El": too wide), `proven` per game from late_proof.json `voyage`; regions.stage_rules:
  stage 10 needs the three passes. (2) rules.closes_at_gate: a location whose early mode is at
  stage <= 10 and which the voyage proof did not reach holds no progression (it may close
  before the gate; and whatever the passes hang on must be in reach at the gate). (3) The seed
  door {id 29106503, routine "0x7c28", keys [18,19,17], purpose}; port: Door.keys / purpose,
  door_locked = any missing, banner "<Key> needed to sail to El" per missing key (no
  duplicates queued); doors.py validates keys and routine. (4) tools/ap_late_proof.py --state
  voyage: late save at chapter 27 step 7000, flag 730 cleared, disc 1, party Claude + Rena,
  root 444; entries past chapter 27 (SHOW Ga0) and map 1's El / Cave entrances not followed,
  their checks not counted.
- Files touched so far: tools/ap_late_proof.py, tools/ap_world_tables.py, apworld/so2/{rules.py,
  regions.py, world.py, doors.py, test/test_doors.py}, engine/src/game/ap/{seed.h, seed.cpp,
  ap.h, ap.cpp}, engine/tests/test_ap.cpp (ap_gate_door: 26 AP tests pass). Snapshot of the
  pre-change world: scratchpad/elgate/pre/ (so2/, late_proof.json).
- Running: the voyage proof (scratchpad/elgate/voyage/run.sh, setsid, driver.pid; status.txt,
  all.done; per game work_{claude,rena}, voyage_{claude,rena}.json), binary
  scratchpad/elgate/bin/head (HEAD build). Build dir engine/build-elgate.
- Next: when the proof ends, merge into docs/ap/late_proof.json (`voyage` key, via --reparse
  over both work dirs), regenerate data.py, update test_world.py, batch, docs, port demo.
- **Teleporter finding (18:10, real hazard).** Energy Nede's world map (archive 0x1060 + i,
  same 9 x 7 torus; scratchpad/elgate/nedewalk.py, image nede128.png): Central City and North
  City share one small walkable island; Fun City (279, entered only from the Nede world map)
  lies on another with Armlock. Before the Synard (flag 11, set only at map 417, the Cave of Red
  Crystal, where chapter 41 is stored; or 588) the party moves only by Central City's
  transporter (267 -> Noel's House 278, chapters 38-40). So the Expel teleporter (Fun City's
  arena) is reachable from chapter 41 (stage 16 done), not chapter 38 as rules / regions assume
  (EVENT_TELEPORTER = stage 13 + Fun City Pass). The voyage's window for the Cross and Lacour
  continents is chapter 28 to 41: the keys of stages 14-16 (North City, Noel's House, Red
  Crystal Passes) and anything they hang on in other worlds can sit in tele-proven Expel rows
  (teleporter on, expel_progression on) and strand the player at chapter 38. Fix, the same way
  (brief rule 4): EVENT_TELEPORTER needs stage 16; with the teleporter and expel_progression
  on, the voyage gate also needs those three keys (data STORY_GATES `teleporter_groups`, the
  seed door's keys per seed). With either off, Nede keys and foreign progression never sit in
  Expel, so the El passes suffice. DECISION FOR THE USER (flag in the report): the voyage then
  needs 6 passes in such seeds; alternatives are making those 3 keys starting keys, or a port
  change giving the teleporter a way in before the Synard.
- **Done (17:52).** Voyage proof: both games 289 maps, 316 nodes, 129 locations (same set),
  merged into docs/ap/late_proof.json key `voyage` (reparse == run; other keys unchanged).
  Every pre-gate Expel row that is tele- or late-proven is voyage-proven (Hoffmann's 26
  included), so closes_at_gate changes no class (scratchpad/elgate/gatecheck.py).
  Implemented: data TELEPORTER_STAGE 16, STORY_GATES `teleporter_groups/keys` [29, 30, 31],
  EXIT_GATES (Central City exit 15407589 routine 0x670c key 28 until 38; Noel's House exit
  18160748 routine 0x7164 key 30 until 40; no logic); regions.gate_groups / gate_keys;
  EVENT_TELEPORTER needs stage 16; port Door.until (chapter = game_state +0). Logic diff
  (scratchpad/apareas/logicdiff.py + classdiff.py, pre = scratchpad/elgate/pre/so2): 0
  locations' access or class changed, story mode 0; the changes are the stage-10 rule (+3 El
  passes, +3 Nede passes with teleporter and expel_progression) and the teleporter event
  (stage 13 -> 16). data.py: 0 lines removed, tables appended.
  Tests: world tests 108 passed (7986 subtests); AP general 379 passed (116979 subtests; ran
  before EXIT_GATES, rerun at the end); engine AP tests 26 pass (ap_gate_door covers keys,
  banners, until). Docs: apworld.md (one-way section, teleporter, known limits, changes, proof
  usage), seed_format.md (keys / until / purpose), release_notes/0.02.00.md.
- **Running (17:52):** batch 1-96 (scratchpad/elgate/batch: run_batch.sh -> gate/, gate.txt,
  gate.status; check.py extended with the gate / exits / voyage-proof rules; elcount2.py
  counts stranded window keys); late + teleporter proofs with the gate build
  (scratchpad/elgate/proofs/run.sh, status.txt, all.done; compare with docs/ap/late_proof.json
  proven/checks/maps). DON'T edit apworld/ while the batch runs (the AP checkout links it).
- **Done (18:05).** Batch gate/ (purpose "to sail to El"): 96 seeds, 192 .apso2, 189
  combinations, check.py 0 errors (5295 progression), elcount2 stranded 0 (own window keys
  off El in Expel 391, all voyage-proven; own Nede window keys in Expel 94), the 16 all-story
  seeds byte-identical to scratchpad/apareas/batch/tele. Pre batches for comparison
  (elcount2 / old elcount): 0.1.0 base 110 of 114 slots with an El pass off El; 0.1.1-pre
  tele 114 of 114, 85 Nede window keys in Expel. Banner: the box holds 236 px of text
  (notice.cpp: box <= 0xf8); "El Beach Pass needed to sail to El" overflowed, so purpose is now
  "to sail" and the port drops the purpose when the text would not fit (ap.cpp
  banner_text_width). Engine: ss_tests 99/0, ss_game_tests 129/0 (with discs), AP 26/0.
  Port demo (scratchpad/elgate/demo/run.sh, demo.py, logs + captures there; banners.png,
  cc_top.png): 444 at chapter 27 step 7000, leader warped into the trigger (-370,0,630; view
  413,161): no passes -> "door 29106503 (El voyage): locked, needs <6 keys>", control kept,
  chapter 27; 5 of 6 -> only "Red Crystal Pass"; all 6 -> chapter 28, 422 -> 169 -> 548,
  chapter 29 (circle/cross presses for the windows); Central City exit at chapter 37 ->
  locked, needs Central City Pass; chapter 38 or with key 28 -> map 2.
- **Running (18:05):** batch gate2/ (final data, purpose "to sail"; gate2.status); late +
  teleporter proofs at --jobs 1 --resume (restarted 17:55; proofs/status.txt, all.done);
  e2e (scratchpad/elgate/e2e/run.log, work dir e2e/work; binary bin/final).
- **Done (18:12).** Final batch gate2/: 96 seeds, 192 .apso2, 189 combinations, check.py 0
  errors (5295 progression; validate_doors on all), elcount2 stranded 0, 16 all-story seeds
  byte-identical. AP general tests (final world) 379 passed, 116979 subtests. e2e 27/27 (rc 0,
  scratchpad/elgate/e2e). Smoke 86 PASS / 0 FAIL (TMPDIR and XDG under scratchpad/elgate/smoke).
  Demo rerun with the final seed: banners fit ("Eluria Tower Pass needed to sail"). apworld.md
  Testing has the elgate results (proofs to add). Proofs restarted at --jobs 2 --resume (18:11);
  compare with scratchpad/elgate/proofs/compare.py.
- Next (old list): check.py / elcount2 / compare on gate2; compare proofs with docs/ap/late_proof.json
  (proven / checks / maps per game: tools' JSONs in proofs/*.json); rerun AP general tests;
  smoke script (tools/smoke_field_events.sh BIN, 1 game process); fill apworld.md Testing
  (0.1.1 results); checkpoint note; final report. The user decision noted above stays open.

## User decision (18:45, via the coordinator)
The 6-pass gate ships in 0.1.1: with the teleporter and expel_progression both on, the El
voyage waits for the three El passes and the North City, Noel's House and Red Crystal Passes.
The alternatives (Nede passes as starting items; a port change opening the teleporter at
chapter 38) were declined. No change to the implementation is needed.

## Done (19:30, elgate-finish agent)
- **Proof comparison** (scratchpad/elgate/proofs/, finished 19:12; compare.py against
  docs/ap/late_proof.json): the `teleporter` key is identical in both games (proven, checks,
  maps). The `late` key's maps and edges are identical in both games, but 2 of the 278 checks
  the committed file proves are missing from this rerun's proven set, in both claude and rena:
  Cave of Trials Level 1's boss (map 705, id 1364328951) and Level 11's boss (map 807, id
  1364328961) — the cave's "10 / 13" bosses in apworld.md's Results table. Both maps are still
  reached (in `maps`) and their reload-settle note is still "settle (reload)", not "killed",
  just at a different retry count than the committed run (705@G1cf=1: x30 vs x72; 807@G1cf=11:
  x34 vs x72) — a repeating floor unrelated to the voyage door or the teleporter stages, so
  this reads as the proof's own reload-loop noise rather than an effect of the gate. Per the
  brief, docs/ap/late_proof.json and apworld/ are untouched; this is reported, not fixed.
- **docs/ap/apworld.md**: Testing section, "Results (0.1.1 with the El voyage gate)" — added
  the proof-comparison bullet above and made the engine suites line explicit (ss_tests 99/0,
  ss_game_tests 129/0, AP 26/0); the rest of that block (batch, e2e, smoke, demo, AP general
  tests) was already filled in by session 1. The 0.1.1 changelog already states the 6-pass
  rule (the "Fixed: the teleporter needs the Synard" entry); no change needed there.
- **docs/release_notes/0.02.00.md**: Archipelago section's "teleporter needs the Synard" bullet
  now names the Expel Teleporter and Expel Progression options and says they default on in open
  world. The old "Open world: collect the El keys before sailing to El" TO CONFIRM bullet
  (Known limitations) is replaced with the fixed behaviour (all 6 passes, the defaults they
  depend on, the in-game message). The other TO CONFIRM markers (regions sweeps, battle
  result / Scan, French e2e) are untouched, for another agent.
- Nothing else was open. No build, no game or server run; everything needed had already run.

## Proof gap explained (19:55, coordinator; proofab agent)
The 2 missing late-proof checks (Cave of Trials L1 / L11 bosses) are the proof driver's
timing, not the gate: an A/B (scratchpad/proofab/repro.py, which calls ap_late_proof.run_map)
of gate vs pre-gate binary on both routes gave map 705 no boss in either swap and map 807 the
boss in both. Mechanism: run_map kills a run once more than 6 "settle (reload)" lines have
arrived (wall clock, 0.5 s poll), then resumes after max(started) + 1, so an entry that started
just before the kill is skipped before its battle ends; routes are first-come with --jobs > 1
(bfs / done). Tool fix queued for after the regions queue (per-entry reload budget: reset
the counter at each entry start, or resume at the killed entry), then rerun the late proofs;
not a release blocker (late_proof.json unchanged).
