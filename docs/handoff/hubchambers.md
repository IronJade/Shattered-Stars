# Handoff: hubchambers (the 22 boss chambers proven headless from the hub state)

Agent "hubchambers", started 2026-10-25. Plan and the user's decisions: docs/handoff/hub.md (D2:
each story boss plays its ORIGINAL scene in its chamber through a story view; a restaged battle
is the user's call, only for a scene that can't be made self-contained). The view as built:
docs/handoff/hubvm.md ("What a view can't handle" 1-7). Rows: docs/ap/hub_bosses.json
(tools/ap_hub_bosses.py, CURATED). Format: docs/ap/seed_format.md "Hub".

## Rules given
- Build in engine/build-hubchambers (configured like build-hubvm), `cmake --build
  engine/build-hubchambers -j8`. The source tree is shared: keep it compiling after every edit.
- Tests (baseline build-hubvm: ss_tests 99/0, ss_game_tests 161/0, smoke 132/0), with
  SS_DISC1=<disc 1 .cue>: ss_tests, ss_game_tests, tools/smoke_field_events.sh
  engine/build-hubchambers/shattered-stars. All three after any engine change before reporting it
  verified, with counts.
- Headless only (`--headless`), never a GUI; PCSX-Redux only `-cli` with a scratch HOME on
  scratch card copies. Never a flag that saves ~/.config/shattered-stars/config.ini (no --audio /
  --no-audio). SDL_AUDIO_DRIVER=dummy.
- Every game process under tools/memcap.py (SS_MEM_CAP_GB 4); at most 2 at once. A run hitting
  the cap is a failure to investigate, never a retry.
- Late save scratchpad/latesave/card.mcd (frame 13). Scene summaries scratchpad/hub/scenes/.
  Disassembly: REA MCP tools.
- Enhance, never degrade: without a hub seed nothing changes.
- Scratch in scratchpad/hubchambers/ (not /tmp); delete bulky logs. Cap output. Kill only by
  exact PID. Every wait loop has a deadline.
- Don't commit, don't push.
- Engine edits only in engine/src/game/ap/hub.{h,cpp} (+ unit tests in engine/tests); seed.{h,cpp}
  only after a note in hubcore's log (they own it). ap.cpp / seed are hubcore's.
- Other agents: hubcore (docs/handoff/hubcore.md), hubworld (docs/handoff/hubworld.md): notes in
  THEIR logs ("For hubcore" / "For hubworld", dated). Tell hubworld when hub_bosses.json rows change.
- Permission refusal: stop, report the exact command. Past ~400k tokens: update this log, end turn.
- This log current after each chamber and at least every 20 minutes.

## Per-chamber results

All runs: tools/ap_hub_chambers.py from the hub state (hub_state.json, both games), a seed with
all 22 rows, SS_BATTLE_STUB=1, SS_HUB_TRACE=1. Key = the runner's chamber key. "both" = Claude's
and Rena's game (`KEY` and `KEY_r`). Re-fire "sweep": after the scene the sweep lists the room's
ACTIVE entries and fires each (reloading at the real state): no second battle.

| Enc | Boss | Map | Game | Key | Route (approach -> trigger) | View | Battle / check | View end | After-state diff | Re-fire | Result / notes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 191 | Alen-Tax | 61 | both | alentax(_r) | 68:T6 (0x667c) -> set-up (Claude) / 61:T2 region 0x5354 (Rena) | on 6/2100 | ok / 1364328639 | map 53 loaded | none | - | PASS |
| 209 | Visseyer | 412 | both | visseyer(_r) | 409:T6 -> 412:T2 (0x66c0) | none (guard 483) | ok / 1364328657 | - | +483 | - | PASS |
| 522 | Arcmene | 417 | both | arcmene(_r) | 418:T2 (0x6660 lower door) -> set-up | on 40/1000 | ok / 1364328970 | map 1088 loaded | Nede Synard spot (set_point) | - | PASS |
| 523 | Cross Cave guardians | 547 | both | crosscave(_r) | 546:T3 -> 547:K64 (actor 64's talk 0x6af8, not the chest) | none (guard 449) | ok / 1364328971 | - | +449; chest K60: +2569 +68, G[0x2e8] | sweep: K64 no battle | PASS; row trigger curated |
| 194 | Varmillion | 462 | claude | varmillion | 464:T4 -> 462:T7 (0x66cc) | on 15/2400 | ok / 1364328642 | map 135 loaded (no step-3000 scene in 135) | none | - | PASS (Claude's game only) |
| 195 | Azamgil | 463 | rena | azamgil | 464:T4 -> 462:T2 -> 463:T2 (0x66c8) | on 15/2500 | ok / 1364328643 | free play | +70 (key item) | sweep: 0x66c8 / 0x6b1c re-test their steps | PASS (Rena's only); the 15/2600 beat (flag 154, step 2700, no reward) is cut |
| 210 | Harfainx | 523 | both | harfainx(_r) | 515:T4 -> 523:T2 (0x6750) | on 24/2000 | ok / 1364328658 | free play | +77 (key item) | sweep: none | PASS |
| 516 | Shin | 447 | both | shin447(_r) | 444:T6 (0x6730) -> set-up | on 27/1000 | ok / 1364328964 | map 439 loaded | none | - | PASS after the hub.cpp settle fix (before: the view ended at the battle's hand-back frame) |
| 519 + 520 | Shin + Berle (Eluria Tower) | 569 / 575 | both | tower(_r) | 568:T1 -> 569 set-up (Shin, guard 587) -> 570:T3 -> 573:T4 (0x4804) -> 574 -> 575 set-up (Berle) | Berle's 33/20 over 557-575 | ok / 1364328967 + 1364328968 | cut_to 557 (576 -> 557) | +587; G[0x215] (574's came-from byte) | - | PASS (P5: hubcore walks 557 -> 558 -> 559; started on 568) |
| 532 | Marsilio | 305 | both | marsilio(_r) | 304:T4 (P5 door 19929509 -> 305 while unbeaten) -> 305:K16:2 (Clerk, "Go see Narl") | on 57/20 | ok / 1364328980 | map 296 loaded (296 at 57/2000: music only) | none | - | PASS; 2327 settled (stored only, dropped) |
| 536 | Ruprecht/Nicolus/Jibril | 657 | both | fienal1b(_r) | 687:T2 (0x3c8c, east, Fienal's entrance) -> set-up | on 60/1000 | ok / 1364328984 | free play | G[0x1cf] floor byte | sweep: none | PASS; from the west (655) the hero walk stalls, but 655 lies behind 657 |
| 537 | Vesper/Decus | 669 | both | fienal2(_r), fienal2w | 670:T4 (0x3bd0, east) -> set-up; also the west arrival (-980, 0, 18) | on 60/2000 | ok / 1364328985 | free play | none | sweep: none | PASS from both sides |
| 538 | Cyril | 674 | both | fienal3(_r), fienal3b(_r) | 676:T4 (east) -> set-up; 679:T0 (west) with the proposed P5 door 44502760 -> east arrival | on 60/3000 | ok / 1364328986 | free play | G[0x1cf] | sweep: none | PASS; without the redirect the west arrival soft-locks (hero walk never ends) |
| (539) | walk to Indalecio | 685 | both | fienal_walk(_r) | 671:T5 (0x3bec) -> 684 corridor (T2 loops, G[0x1ce]) -> 685 set-up | - | battle 539 starts, won | - | - | - | PASS: the route on to 685 is not blocked at step 4000 |
| 524 | Magichand | 554 | both | magichand(_r) | 553:T7 (0x4a80) -> set-up | on 42/0 | ok / 1364328972 | free play in 556 | +175 +162 (82 already in hub) | - | PASS; chain 554 -> 555 -> 628 (Claude) / 14 (Rena, vision on Expel) -> 556 end scene; row: after_maps + 14 |
| 525 | Guardian (Power) | 603 | both | power(_r), power_out | 602:T2 -> 603 set-up; 603:T2 -> 604:T3 (0x6628 vision) | on 42/0 until transfer | ok / 1364328973 | map 602 loaded | +528 +163 (83 in hub) | 604 region again: nothing | PASS; vision 622 (Claude) / 27 -> 30 (Rena) -> 604 step 2020; row: after_maps [622, 27, 30], done_flag 163 (walk-out case proven) |
| 528 | Lover | 596 | both | lover(_r) | 595:T0 (0x3ba4) -> set-up | on 42/0 | ok / 1364328976 | free play in 596 | +518 +165 | sweep: none | PASS; set-up chains 0x7868 vision 262 (Claude) / 310 (Rena) -> 596 step 20 reward; row: after_maps [262, 310]; 508 settled (view 0) |
| 527 | Guardian (Courage) | 642 | both | courage(_r) | 641:T2 (0x47d4) -> 642:T1:1 (0x6914, option 1 "Put statue on altar") | on 42/0 | ok / 1364328975 | free play in 642 | +164 +84 | sweep: region removed by the scene (ext 0xa3) | PASS (Rena); Claude's run aborts at exit: engine bug ext 0xCB (below); row: 523/524 unlisted |
| 531 | Bark | 454 | both | bark(_r) | 450:T4 -> 453:T2 (0x3bd0, view room) -> 454:T2 (0x661c) | on 55/0 on [453, 454] | ok / 1364328979 | free play in 455 | +89 (key item, 455's scene) | sweep: none | PASS; row: after_maps [455] |
| 521 | Synard | 269 | both | synard(_r) | 260:T28 (0x69e4) -> 264:T0 (0x7390, view room) -> 269 set-up | on 38/4000 on [264, 269] | ok / 1364328969 | map 272 loaded | +501 +745 (272's own chapter>40 Noel scene, any visit) | - | PASS |
| 533 | Shigeo | 281 | both | shigeo(_r) | 280:T12 (0x692c) -> 281:T0 (0x855c strip) | on 57/2000 | ok / 1364328981 | free play | none | sweep: region re-tests 57/2000 | PASS; DECISION: the strip spans 281 between the 280 and 297 doors: crossing Fun City 281 (the Nede -> Expel route) forces the fight while unbeaten |
| 535 | Berle (Armlock) | 306 | both | berle306(_r) | 310:T0 (0x7bc) -> 306:T21 (0x40) | on 58/0 | ok / 1364328983 | free play in 314 | none | sweep: none | PASS; row: after_maps [314] (conclusion dialogue) |

Optional bosses (not chambers): XINE 199 plays at the hub state in both games (385's region 0x6df0:
flag 754 && !113 && !388; 113 can't change at chapter 60); Nightmare 197, Flarelizard 196, Ghost 211
can't arm at the hub state (Ashton's / Opera's quest states, chapter 24); the Cave of Trials bosses
503-510, 512, 513 were proven by the late proof with the same guards (950-962 clear).
Town chambers: 281 in the view has its 8 regions (every door) but none of its 7 chapter-60 NPCs; 306
has 26 entries in the view vs 31 at chapter 60; no shops on either map (no B 0x6d); the arena 305's
gift (1349960704) needs Marsilio won (told hubworld). The start town 279 is not a chamber map.

## Findings
- A `--map` start runs the first map's set-up before the sweep applies SS_SWEEP_FLAGS (flag 1 is
  still set): a Rena run started ON a game-filtered chamber turns on Claude's row. Real arrivals
  (walk in from an approach map) avoid it; the runner judges words against the hub state itself.
- ap's in_free_play() (controls on, script depth 0) is true for a frame when a battle hands the
  field back mid-scene (VM wait 8, ip inside the scene). Fixed in hub.cpp: a won view ends at free
  play only after the field's own rule (field/leader.cpp free_play) held 10 frames in a row.
- Fienal pair 3 (Cyril 674) entered from the west (679) while unbeaten: the scene walks the hero
  into the enemies and never ends (soft lock); fixed by a proposed P5 door row (44502760 -> the east
  arrival while 538 is unbeaten; hubworld told). Pair 1's west side is behind it; pair 2 plays from
  both sides.
- Shigeo's strip in Fun City 281 spans the street between the 280 doors and the 297 / 292 doors:
  crossing 281 (the Nede -> Expel route from the default start) forces the Shigeo fight while he is
  unbeaten. A user decision (hubcore reported the same; options in the final report).
- Marsilio's arena 305 in the view has only the Clerk: a party entering it must win to leave
  (hubcore's finding, reported); its gift location needs 532 won.
- Field of Power: a walk between the Guardian (603) and the end scene (604's region, flags only):
  leaving in between left the vision to run at chapter 60 (party stranded on 622, 163 never set).
  New optional view key `done_flag` (seed.{h,cpp}: note in hubcore's log; hub.cpp): after the win
  the view comes back on in its maps until that real flag is set.
- Visions on Expel maps inside Nede chambers (14, 27, 30, 310, 262): followed as after_maps; the
  map 14 ruling: FOLLOWED (cutting strands Rena's game on Expel without 556's end scene).
- Flags 740-744 are recomputed from the inventory at every map's set-up (642 0x5698: item_query
  742/743/771/772/773): not story state; removed from every view (tool `ITEM_FLAGS`).
- **ENGINE BUG (not mine, field VM): script ext 0xCB (sound_start_clip, script_ext.cpp:643) fetches
  5 stack args; the game's only two call sites (642 0x6e3c Courage's Guardian aftermath, 575 0x761c
  Berle's tower scene) push 4.** The value stack underflows (ASan, engine/build-hubchambers-asan:
  heap-buffer-overflow WRITE in script_vm.cpp:92 push, from vm_thread_update :595), with or without
  a seed (story state too). Shows as "free(): invalid pointer" / "double free" at exit depending on
  the heap layout. Fix (field owner): fetch 4, after checking the original handler's argument use.
- The sweep's play step fires a trigger slot even when the scene removed it (fire() ignores
  `active`), so explicit re-fire steps can lie; the runner's `sweep_after` (SS_SWEEP_PLAY_SWEEP)
  lists only active entries and fires each (reloading between them at the real state).
- hubcore's sweep option "MAP:ENTRY:N" (choices take option N, 0 = first) drives choices.

## Done and verified
- Runner tools/ap_hub_chambers.py (specs, seed from hub_bosses.json, hub-state env, log + dump
  checks, `--list MAP [--route ...]` for entry lists). Chambers above PASS.
- hub.cpp: settle rule for the free-play end (unit test in ap_hub_story_view: a waiting thread and
  a choice restart the count); `view.done_flag` re-arm. ss_game_tests 163/0 (161 + hubcore's 2).
  Full suites (ss_tests, smoke) still to run on the final code.
- Rows changed (regenerated hub_bosses.json): 524 after_maps + 14; 525 after_maps [622, 27, 30],
  done_flag 163; 528 after_maps [262, 310] (+ view flags 542 / 546 / 549 from 262, hub values).

- More rows (2026-10-25 ~15:30): 527 unlist 523/524; 531 after_maps [455]; 535 after_maps [314];
  520 step 20 (hubcore's tower BFS); 740-744 out of every view; dungeons: Synard Plant + 3 doors,
  Portal of Sealing `"hub": false`. Notes sent: hubworld (rows), hubcore (done, Shigeo, ext 0xCB).
- hub.cpp kSettleFrames 10 (was 20: the sweep fires its next step after 20 idle frames).
- Build dirs: engine/build-hubchambers (main), engine/build-hubchambers-asan (gcc ASan, libasan.so.8
  copied from the GNOME flatpak runtime into scratchpad/hubchambers/asan; target shattered-stars only).

- All 22 rows run (2026-10-25 ~17:00), both heroes where the chamber exists in both games; the
  view_check flags settled (740-744 out; 508 view 0; 2327 stored only). Optional bosses checked;
  XINE answered to hubworld. Docs: seed_format.md (view keys no longer "proposed", done_flag, the
  10-frame free-play end, the flag-list rules), catalog (script_opcodes ext 0xcb conflict / 0xa3;
  globals script_flags 521 / 523 / 524 / 740-744, game_state +0x12e / +0x175).

- Suites on the final build (2026-10-25 ~17:30): ss_tests 99/0, ss_game_tests 170/0 (incl. the
  new done_flag checks in ap_hub_view_keys), smoke 161 pass / 0 fail (rc 0; logs
  scratchpad/hubchambers/{ss_tests1,game_tests1,smoke1}.log).
- XINE answered (plays at the hub state, both games; keep the Lasguss key) and the Synard Plant
  doors added (17308906 / 17308938 / 17832691) - both DONE, notes in hubworld's / hubcore's logs.

- Final full runner pass (2026-10-25, one job, the final code): **46/46 chambers pass, rc 0**
  (scratchpad/hubchambers/final1.txt). `courage` passed this time, but it can still abort at exit
  because of the ext 0xCB engine bug, depending on the heap layout.

## In progress
- Nothing. STOPPED at the coordinator's request (context past 600k); a fresh agent continues
  from this log.
- Files touched: tools/ap_hub_chambers.py (new), tools/ap_hub_bosses.py (CURATED, ITEM_FLAGS,
  unlist, trigger, EXTRA_DOORS, NOT_IN_HUB, done_flag copied), docs/ap/hub_bosses.json,
  engine/src/game/ap/hub.cpp, engine/src/game/ap/seed.{h,cpp} (done_flag: 2 small hunks),
  engine/tests/test_script_vm.cpp (ap_hub_story_view), docs/ap/seed_format.md,
  docs/catalog/{script_opcodes,globals}.csv, docs/handoff/hubchambers.md, notes in
  docs/handoff/{hubcore,hubworld}.md.

## Next steps
1. User decisions: Shigeo's forced crossing of 281; Marsilio's no-exit arena (hubcore's).
2. Field owner: ext 0xCB argument count (both call sites push 4) - then `courage` passes reliably.
3. hubworld: copy the regenerated rows, add the Fienal P5 door row (44502760), the arena gift's logic.
4. Optional polish: CURATED 532 `flags {2327: 0}` (settled: stored only, never read) so the row
   loses its last `view_check`; the seed_format.md `bosses` example rows are illustrative.
5. hubproof: `--bosses` can call tools/ap_hub_chambers.py (exit code non-zero on a failure).
6. Recheck the tower through hubcore's real 557 -> 558 -> 559 walk once it is in (my run starts on
   568, a view map).

## How to verify
- `SS_DISC1=<disc 1 .cue> python3 tools/ap_hub_chambers.py` (all; `--only KEY,...`, `-v` for every
  assertion). Logs / dumps / per-chamber JSON (trace: first reads, stores) in
  scratchpad/hubchambers/runs/. `--list MAP [--game rena] [--route 464:T4,462:T2 --frames 900]`
  prints a map's sweep entries (T slots with codes = routine / 4).
- Build: engine/build-hubchambers (configured like build-hubvm, deps from build-hubvm/_deps).
  Baseline there: ss_tests 99/0, ss_game_tests 161/0 (168 with hubcore's later tests).
- `SS_DISC1=... python3 tools/ap_hub_bosses.py` regenerates docs/ap/hub_bosses.json (deterministic).
- ASan: engine/build-hubchambers-asan (BIN=engine/build-hubchambers-asan/shattered-stars with
  scratchpad/hubchambers/story_run.py; ASAN_OPTIONS=detect_leaks=0) shows the ext 0xCB underflow
  on `641:T2,642:T1:1` with or without a seed.

## For hubchambers (from hubworld, 2026-10-25)
1. **XINE (199, Lasguss Mountain map 385):** the late proof proved its check in both games
   (data.py row "Lasguss Mountain: XINE boss", late [claude, rena]), while hub_state.json's note
   says no optional boss is armed at chapter 60. The APWorld makes XINE a hub location (filler
   only) and keeps the Lasguss Mountain key for it. Please confirm whether XINE plays at the hub
   state; if it does, I let it hold progression (`hub.CONFIRMED_OPTIONAL`); if not, it leaves hub
   mode and so does the Lasguss key. (Flarelizard 196, Nightmare 197, Ghost 211: the late proof
   did not reach them; not hub locations.)
2. The APWorld copies hub_bosses.json through tools/ap_world_tables.py into apworld/so2/data.py:
   tell me (in docs/handoff/hubworld.md, "For hubworld") when rows change, and especially if a
   chamber can't be made to work (it would have to leave the boss locations and the Fienal
   requirement's all_bosses list).
3. FYI (a door matter, sent to hubcore): map 264's doors 17308906 / 17308938 go to 269 when
   `chapter <= 38`, which the Synard view makes true; hub_bosses.json lists only the 264 -> 270
   doors for the Synard Plant key.

## For hubchambers (from hubcore, 2026-10-25)
1. **seed.h / seed.cpp (hubcore's):** I added `HubState` / `HubWord` / `HubGame` and
   `Seed::hub_state` (the hub block's New Game state: flags, globals, games, start + start.globals,
   chapter / step / disc). Nothing in `StoryView` / `HubBoss` changed. Add your view keys freely;
   just append (don't reorder) and tell me here. Hub mode also forces `character_items` and
   `open_party` (D7).
2. **Hub New Game is in** (ap.cpp on_new_game_state, from field_create_state_block): a hub seed
   whose `hub` block has `flags` / `start` builds the hub state at New Game. A `--map` debug start
   is left alone (your late-state runs with run.py are unaffected) unless `SS_HUB_STATE=1`, which
   applies the seed's hub state on the --map map (handy: the real hub state, not the late one).
3. **Dungeon doors (tools/ap_hub_bosses.py `dungeons`, yours):** please add to the Synard Plant
   group 17308906 and 17308938 (264 -> 269, routine 0x7390 / 0x7410: the transfers the Synard view
   at chapter 38 actually takes; the listed 17308911 / 17308943 are the chapter-60 ones to 270)
   and 17832691 (272 -> 269, routine 0x677c). For Zand's Mansion 9314034 (routine 0x77cc, which
   also leads to 157): see hubworld's note (no routine on that row, or drop it).
4. **Portal of Sealing:** out of hub mode (no cheap way in at chapter 60); hubworld drops key 19
   and its chest. Your table can mark the group excluded if you like.
5. **XINE / Lasguss:** the Lasguss key exists only if XINE (199) is a hub location: hubworld asked
   you whether XINE plays at the hub state.

## For hubchambers (from hubcore, 2026-10-25, later): Eluria Tower (Berle 520 / Shin 519)
1. **Berle 520's view should be at chapter 33 step 20, not step 0** (your CURATED row). Headless
   BFS of the tower in the hub state (scratchpad/hubcore/bfs.py from 559; logs
   scratchpad/hubcore/bfs/tower0 and tower20):
   - at 33/0: 573 -> 574 is shut (`not (chapter == 33 && step < 20)`), so Berle 575 can't be
     reached; and **566:T2 -> 567 plays the 33/0 split scene, which transfers to 627 (outside the
     view's maps): the view ends and the party is stranded on 627 at chapter 60 with no exit**;
   - at 33/20: 567 only leads back to 566; the climb works 559 -> 560 -> 562 -> 563 -> 566 -> 568
     -> 569 (Shin 519 fought, won) -> 570 -> 573 -> 574 -> 575 (Berle's scene, battle 520 won) ->
     cut_to 557, view off (bfs/tower20/map_573.log).
2. 557 -> 558 needs no override: a real walk into the door region works (the late sweep only stalled
   on the routine's scripted walk). 558 -> 559 is a height check (leader y > 400 in the region);
   I'm still walking it.
3. **Shigeo 281:** the only Nede -> Expel route crosses 281 (279 -> 280 -> 281 -> 297 -> 299 ->
   303's old man), where Shigeo's view (57/2000) is on and the scene starts from a trigger strip.
   Please check with a walk whether crossing 281 to the arena door starts the fight (a fresh party
   from a Fun City start would be forced into it).
4. Arena 305 (Marsilio): any arrival works for the scene (305's set-up registers actor 16's talk at
   57/20); my override candidate is the antechamber 304's door to 296 (19929509) redirected to 305.
   Note the hub state has G[0x217] = 1 (arena state), so 305's set-up also calls 0x8220.


## For hubchambers (from hubcore2, 2026-10-25)
1. **558 -> 559 walked** (no override): the stairs are floor-zone ramps (y 0 -> 450); arrival ->
   left -> up down the ramp -> left (or the mirror on the right) fires 0x3c00 / 0x3c60 at y 490 ->
   559 (runs scratchpad/hubcore/keep/w558_s3/s4.log, Berle's step-20 seed: the view stays on into
   559). So the whole tower is walkable once Berle 520's view is at **step 20**: I'll recheck the
   climb from your row as soon as hub_bosses.json has it (still 33/0 at 13:10). Please note here
   when it lands.
2. **Arena 305 (Marsilio 532) redirect is in** (port `to.while_unbeaten`; APWorld hub.P5_DOORS):
   door 19929509 (304 -> 296) goes to 305 (-13, 0, 276, 271) while 532 is unbeaten. Proven from a
   hub New Game (route 279:T0, 280:T12, 281:T3, 297:T4, 299:T2, 304:T4, then 305:K16 option 2,
   SS_BATTLE_STUB): the view comes on, the Clerk's "Go see Narl" plays Marsilio's scene, 532 won,
   the scene's transfer to 296 ends the view (dropped step 2000, 745, 2327); after the win
   304:T4 -> 296 again. For your chamber spec: K16 is the Clerk (options: training / rest / "Go
   see Narl" = 2).
3. FYI, **305 inside the view has one entry only (K16)**: no walk-out exit; the Clerk's training
   and rest keep the party in 305, "Go see Narl" starts the fight. So an unready party that enters
   305 must win Marsilio to leave (it can train and rest there). That holds for ANY way into 305
   while 532 is unbeaten, the paid arena registration at 297 included (the view turns on at every
   load of 305). I report it to the coordinator as a user decision.

## For hubchambers (from hubproof, 2026-10-25 13:50)
1. **`--bosses` builds on your runner.** tools/ap_late_proof.py `--state hub --bosses` imports
   tools/ap_hub_chambers.py and uses `CHAMBERS` (your specs: approach map, steps, code, end, flags),
   `parse_log`, `check_chamber` and `dump_diff` as they are; it does not touch your file. Per spec it
   runs from the hub's New Game (SS_HUB_STATE on the start map 279, the hero alone), replays the hub
   proof's route to your approach map through the dungeon's keyed door (a seed with every dungeon
   door locked as the APWorld writes them, the chamber's key in start.items), then your steps.
   Please keep those four names and the spec keys stable (adding keys is fine), and add specs for
   the remaining chambers as you prove them: every hub_bosses.json row without one shows as
   "pending" in docs/ap/hub_proof.json `bosses_pending`, and I rerun `--only KEY` as they land.
2. FYI, the hub proof (the late proof from the hub start) uses the rows of hub_bosses.json as they
   are when it runs (md5 in hub_proof.json `state.hub_bosses_md5`). When you change a view that
   opens a way (Berle 520's step 20 for the tower climb, the Synard, Bark, the Fields' after_maps),
   tell me here or in docs/handoff/hubproof.md "For hubproof" and I rerun the nodes behind it.

## For hubchambers (from hubask, 2026-10-25 ~16:30)
FYI, nothing to do for your rows: the port now reads an optional `ask` on a bosses row (the user's
rulings on Shigeo 281 / Marsilio 305; seed_format.md "Hub", "The port: the questions"). The APWorld
adds it from apworld/so2/hub.py `ASK` (not hub_bosses.json), so tools/ap_hub_chambers.py's seeds
(rows straight from hub_bosses.json) play as before. If you copy `ask` into your seeds: a door into
281 / the 304 door shows a yes / no choice, which the sweep confirms (= yes), so `shigeo` and
`marsilio` still pass; the Clerk's menu gets a 4th option after "Go see Narl", so `305:K16:2` is
unchanged. New sweep syntax: `MAP:ENTRY:1/0/0` answers a step's choices one by one.
