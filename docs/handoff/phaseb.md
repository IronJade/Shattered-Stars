# Handoff: Phase B build (tracker support: port 0.03.00 + so2.apworld 0.2.2 + pack 1.0.0)

Coordinator log (main session), started 2026-10-27. The plan is docs/plans/trackers.md (sections 5
B1-B7, 7 decisions); this file records how the build is split into agents, their state, and the
rules every agent gets. Resume from here plus each agent's own docs/handoff/<name>.md.

## The user's words
- 2026-10-27: "Next: port 0.03.00 + world 0.2.2, tracker support, from docs/plans/trackers.md."
- Decisions already made (trackers.md section 7): D1, D2, D3, D4, D6, D7 as recommended; D5 = the
  port renders maps from the player's own disc into PopTracker's user-override folder, nothing from
  the disc is published ("This keeps us trouble free from soliciting any IP"); D9 = all tracker work
  ships as port 0.03.00 + world 0.2.2 + pack 1.0.0.
- D10 (not asked before): taken as recommended (UT refuses with TrackerException and a clear
  message when the installed world can't reproduce the room's `logic_version`); told to the user.
- 0.2.0 Skillsanity rooms in UT (raised by utworld): **accept with a warning** (the user, 2026-10-27:
  "accept with a warning"), on the condition given with the recommendation: the rebuilt logic must be
  conservative (UT's in-logic set a subset of the room's own at every sphere, over utworld's oldroom.py
  rooms); a seed that breaks it keeps the refusal. 0.2.0 hell seeds stay refused. Assigned to the B3
  agent (it edits ut.py too).
- PopTracker pack distribution (the user, 2026-10-27): versions.json at `tools/poptracker/versions.json`
  on main (raw URL), as recommended; the pack zip in **its own release**, tag `poptracker-vX.Y.Z`
  (1.0.0 first), not Latest, notes linking the port and world releases ("It's own release works for
  me. We are already doing that with the apworld anyway."). build_poptracker.sh defaults to that tag.
- Tracker map doors and names (the user, 2026-10-27, verbatim): "The strategy guide doesn't add room
  names either for dungeons and the like. What it does is draws lines from one door to the other across
  the page. I think if we could, we could simply have the map number that a door leads to on the image
  to be able to follow the pathway. If it allows for clickable transitions between them, then this
  would be our method. Click on the doorway with the mapTo label and that map then shows up. Anything
  that we CAN name correctly, we should. Houses, Shops, etc." PopTracker supports it
  (AddOnLocationSectionChangedHandler since 0.26.2 + UiHint ActivateTab; nested tabbed layouts).
  Part 1 (names, doors + per-room images in the layout, the port's renderer): agent mapdoors.
  Part 2 (pack: door sections, Area > Room tabs, the click handler, smoke): after logicexport,
  together with B6.
  **Then (the user, verbatim): "Those labels for doors may need to be live. Keep in mind if we move
  forward with the Entrance Randomiser and the unconnected doors, the logic engine needs to be able to
  follow routes to get from one place to another. The tracker would need the correct toMap draws for
  each door."** Design (sent to mapdoors and logicexport): no destination baked into any image; images
  carry the room's own number/name and a lettered door marker (A, B, C...) per door; destinations are
  live (PopTracker: item BadgeText in a per-room door panel "A -> 412 ...", filled from a vanilla door
  table in the pack, or under ER from slot_data pairings + the found-entrances key; clicking a marker or
  panel item opens the live destination's tab; marker colour from logic). A stable **door id** per door
  (mapdoors defines it in docs/ap/tracker_maps.md) = Phase C's entrance name; the logic export names
  every entrance and leaves room for per-door / deferred entrances and a region -> map hint, so ER
  needs more data, not a new evaluator.
- 2026-10-27: logicexport done (entrances named; `to` = -1 = unconnected, so2_logic_connect /
  TrackerLogic.connect; region kind read). For B6: so2_logic_view / so2_in_logic / so2_goal_in_logic.
  For B7: checklist rows for the extended smoke and TestTrackerManySeeds. storyaware started.
- 2026-10-27: mapdoors done. For part 2 (with B6): its spec is docs/ap/tracker_maps.md "For the pack
  (part 2)"; also audit map_names.csv's `area` column (it drives the pack's tabs; e.g. maps 52/53 are
  Salva but filed under Arlia Village). The door id is a recommendation to confirm at Phase C's
  design. setupthread started (the setup render froze the window ~20 s).
- 2026-10-27: setupthread done. Found: unsetting DBUS_SESSION_BUS_ADDRESS isn't enough (libdbus uses
  $XDG_RUNTIME_DIR/bus): rule tightened here and in docs/HANDOFF.md. setup.exe itself not run under
  Wine (the job is covered by ss_game_tests.exe): for the user's Windows check at release.
- 2026-10-27: storyaware done (chapter -> 24 story points in apworld/so2/story.py; UT deferred events
  keyed so2_story_*, honour enforce_deferred_entrances; pack: later-in-story = Inspect (blue), Story
  toggle; chapter <= 0 = no position). Open, for the user at the GUI check: whether blue reads well
  (red is a one-line change). Approximations documented (stage granularity, Fienal from chapter 58,
  the Final Save Point = the step before the save). maptab and releasedocs started.
- 2026-10-27: releasedocs done. For B7b: fill the 21 `TODO (maptab)` placeholders from maptab's report
  (`grep -rn 'TODO (maptab)' docs/ap/release_checklist.md docs/handoff/windows_check.md
  docs/release_notes scratchpad/wiki`); bump the versions per checklist 12.1 (README badge and package
  names still 0.02.51 on purpose); re-derive the RC's packaged e2e copies from today's
  tools/ap_e2e_so2.sh (they lack step 9); measure the full Wine suite count; optionally test UT story
  awareness on a 0.2.1 story room (by design it works; untested). The .apso2 differs from 0.2.1's
  only in `apworld_version` after the bump (spoilers stay identical). setup_en.md's install wording
  sent to maptab.
- 2026-10-27: maptab done (608k at the end). For the user (recommendations in docs/handoff/maptab.md):
  GUI look (room tab bar titled by map number, the Doors panel, blue door diamonds vs story's blue);
  UT asks for the pack zip once with a file dialog (keep, recommended); player renders in UT need a
  zip copy (a port option later); released AP area names that mislead (e.g. "Port Hilton" checks in
  the Lacour coliseum) stay; ER names door_pairs / so2_doors_* provisional. Small bug for rc030:
  test_tracker_job leaves an empty tracker_job_test/ in the cwd. rc030 started.
- maptab's open points (the user, 2026-10-27): 1. UT's one-time file dialog for the pack zip: "Keep
  it". 3. Released AP area names that mislead (e.g. "Port Hilton" checks in the Lacour coliseum):
  "Leave them as they are there because they trigger chapter or story mode movements". 2. The
  player's render in UT: the user asked the cost of doing it now (answer: a port-written zip of the
  rendered pictures, below). 4. "Can door diamonds just be a color not used for checks? Is white
  used?" (answer: PopTracker colours by state from a fixed palette, packs can't pick colours, no
  white; checks use green / red / blue / grey and orange / teal / purple when mixed; yellow
  (SequenceBreak) is used by no check). rc030 held before its builds until the user answers 2 and 4.
  Proposed: the port also writes so2_shattered_stars_ut.zip (its renders + an original player marker)
  for UT (~2-3 h + ~1 h RC rerun); known doors yellow, unknown ER doors decided at Phase C.
  Small leftover: test_tracker_maps.cpp has the same cwd temp-folder pattern rc030 fixed in
  test_tracker_job.cpp.
  **The user's answers: "2. Include it now. 4. Yellow works for me."** Agent utzip builds both (the
  port writes a UT zip of its renders; known doors SequenceBreak yellow, unknown ER doors yellow for
  now, their colour a Phase C decision) plus the test_tracker_maps.cpp temp-folder fix; log
  docs/handoff/utzip.md. Then a fresh RC agent resumes at task 4 (rc030 ended at 291k).
- D8 (story awareness in story mode): **yes** (the user, 2026-10-27: "Story awareness in story mode
  would be a fantastic addition, yes."). B6 builds it: UT deferred events from `so2_story_*`, and the
  pack colours areas later in the story from the same key.

## Lanes (at most two agents at once: memory pressure crashed a session before)

| Lane | Order | Agent | Log | State |
|---|---|---|---|---|
| World | B1 UT yaml-less | utworld | docs/handoff/utworld.md | **done** 2026-10-27 (605368a, b35eadf, 12fe31d; world tests 783, test_ut 84, slow 320 rooms, general 379, batches 288/288 identical, real UT 20/20 points) |
| Port | B5 data-storage keys | dskeys | docs/handoff/dskeys.md | **done** 2026-10-27 (3a91e09, 802935b, f3c3b5c; unit 9/9, ss_game_tests 216/0, e2e 49/0, no-seed/offline A/B byte-identical, smoke 373/0) |
| World | B2 Rule Builder | rulebuilder | docs/handoff/rulebuilder.md | **done** 2026-10-27 (0cc0bda 1d879a2 64a2c7d; 288 batch seeds identical incl. multidata, 93M truth values equal, world tests 788, general 379, slow 789, UT 20/20, /explain readable; no slowdown) |
| Port | map render command (D5) | maprender | docs/handoff/maprender.md | **done** 2026-10-27 (4575b00 8cafcf7 e3bcf6b 8cc77d3 abad5e0 1687cc7 a2dac88; layout docs/ap/tracker_maps.json: 472/472 place checks positioned, 66 area + 2 world images; ss_game_tests 222/0, e2e 9/9, A/B 0 differ, smoke 373/0, NA/EU/FR/DE identical) |
| World | B3 logic export + B4b Lua evaluator + 0.2.0 Skillsanity rooms accepted with a warning | logicexport | docs/handoff/logicexport.md | **done** 2026-10-27 (a16af67 6bb6d0a 250a83d fd803cf 812f9f6; export 4-35 KB; reference = world at every sphere, 16 mixes + 320 slow rooms; Lua = reference 243,885 evaluations; PopTracker smoke 24/24; 0.2.0 Skillsanity 45 rooms conservative, the user's room loads with the warning; 288 seeds identical but the new key; world tests 863, general 916, UT 40/40) |
| World | Story awareness (D8): UT deferred story events + pack 'later in the story' | storyaware | docs/handoff/storyaware.md | **done** 2026-10-27 (61eace6 60884fb c326fba c5935c8 bc3cc21 e4d234f 91b10bb; apworld/so2/story.py 24 points; batches identical but tracker.story in 93 story slots; world 882, general 935; real UT 59 checks, PopTracker smoke 37/37) |
| World | B4a PopTracker pack (all but the logic evaluator) | packgen | docs/handoff/packgen.md | **done** 2026-10-27 (65ed8ce 2ad5530 3c5db64 80d1b8d cbd1231; tests 18/18, pack checker strict OK, real PopTracker 0.36.0 headless smoke 20/20 both variants) |
| Port | ASan, Windows build + Wine render, setup button | portcheck (sonnet) | docs/handoff/portcheck.md | **done** 2026-10-27 (c43294f b25fa5b 94e30f4; ASan clean: ap_ds 9/9, tracker_ 6/6, net 5/5, render 68/68, e2e 49/0; MinGW 370/370; Wine render 68/68 byte-identical to Linux, default folder Documents\PopTracker\user-override\so2_shattered_stars; setup button tested headless) |
| Port + data | Map doors, per-room images, names (part 1) | mapdoors | docs/handoff/mapdoors.md | **done** 2026-10-27 (ae78aa2 0163425 389d650 6430541 3e7a2d1 994c8ab; 33 rooms named with evidence, 199 of 793 rooms numbered; layout 1.1: 793 rooms, 1775 doorways lettered, vanilla `to` only as data; door id = map*65536+offset/4, doorway id = its lowest door id; render 861 images 135 MB 20.8 s; tracker_ 7/7, ss_game_tests 223/0, e2e 861/861, regions identical, A/B 0 differ, smoke 373/0; fixed: trigger regions are screen coords) |
| Port | Setup render on a worker thread + progress; regenerate maps.csv / doors.csv | setupthread | docs/handoff/setupthread.md | **done** 2026-10-27 (e4f89b1 5eabbee 1fe705a a5ce549; tracker_ 10/10, ss_game_tests 226/0, CLI 861 sha256 = baseline, setup headless runs A-D, ASan + TSan clean, MinGW 359/359, Wine tracker_ 10/10; chests.csv / shops.csv names regenerated by the coordinator) |
| World | B6 UT map tab + pack doors / Area > Room tabs / live door panel (part 2) + map_names area audit | maptab | docs/handoff/maptab.md | **done** 2026-10-27 (85e8325 fa8b0df 0062337 d6ab3d0 c544a40 814ea5e bccaa3e 980b0b2; 10 area fixes; 794 room maps, 1,775 door diamonds, live Doors panel, click/panel switching, ER seam door_pairs + so2_doors_*; pack zip 5.56 MB; UT map tab hybrid (JSON in so2.apworld, 281 KB; pictures from the pack zip, ut_pack_path); batches 288 identical; world 882, general 935, slow 885, UT 59, pack tests 41/41, checker OK, smoke 44/44, UT map loader 15/15) |
| Docs | B7a checklist, Windows recheck list, the three release notes, README, wiki Trackers page | releasedocs | docs/handoff/releasedocs.md | **done** 2026-10-27 (fd6add9..5a95651; checklist section 12; windows_check 'Recheck for 0.03.00' 13 items; notes 0.03.00 / 0.2.2 / poptracker-1.0.0 (DRAFT, sums TODO); README; wiki 11 files uncommitted in scratchpad/wiki, links checked; 21 'TODO (maptab)' placeholders) |
| Both | B7b version bumps, placeholders, RC builds + full tests | rc030 (sonnet) | docs/handoff/release.md "0.03.00" | **held before builds** (tasks 1-3 done: 249ad9d b82b176 2452b35 d4d8dc6; 21 placeholders filled, test_tracker_job temp dir, port 0.03.00 / world 0.2.2 bumped); resume at task 4 after the UT zip / door colour decision |
| Both | UT zip of the port's renders + yellow doors (the user: include now) | utzip | docs/handoff/utzip.md | started 2026-10-27 |
| Both | RC resume at task 4 (builds, tests, sums) | (after utzip; fresh agent from release.md "0.03.00") | | queued |
| Both | The user's Windows check, then the three releases | (after the RC) | | only with the user's go-ahead |

A fresh agent per assignment (rule in ~/.claude/CLAUDE.md), started from the previous one's log.

### The map render contract (for the port's render agent and B4)
- The shipped pack and a player's rendered override must share **one layout**: the same image file
  names and sizes, check dots at the same pixel positions. So the layout facts (which maps make up an
  area image, each map's rectangle, background size, the check positions as pixels) are computed once
  and committed as data (like docs/ap/doors.json / field.json: coordinates and ids, no pixels). The
  generator (B4) draws original placeholder images from them; the port renders the real backgrounds
  into the same rectangles.
- The port command writes only PNGs (and nothing else that is disc art) into
  `<Documents>/PopTracker/user-override/so2_shattered_stars/` at the pack's image paths, headless,
  reading the player's own disc. Never in git, never in a release.
- AP off / no-seed runs untouched; the command does not save the config or touch saves.

## Rules every agent gets
- Edits only in the files named in its brief; its log docs/handoff/<name>.md kept current after each
  finished step and at least every 30 minutes (done / in progress with files / next / how to verify /
  rules given).
- Commits on main after each verified piece, docs first, staged by path (never `git add -A`); message
  ends with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never push, tag
  or publish. If git reports index.lock, wait and retry; never delete the lock.
- A denied tool call: don't retry; write the blocked step in the log and stop with a message naming
  the exact command.
- Port: headless only (`--headless`, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy), every game
  process under tools/memcap.py (4 GB), at most 2 game processes, own build dir engine/build-<name>,
  builds -j6, scratch XDG_CONFIG_HOME, never a config-saving flag, never the user's saves or
  ~/.config/shattered-stars/config.ini; kill only by exact PID.
- World: Python from scratchpad/ap/Archipelago with ../ap-venv/bin/python and
  SKIP_REQUIREMENTS_UPDATE=1, pytest -n 6, generation under prlimit --as (8 GB); the batches
  (tools/ap_batch.sh) stay byte-identical unless the brief says otherwise.
- Nothing on the user's desktop: third-party programs and the setup program run with DISPLAY,
  WAYLAND_DISPLAY, DBUS_SESSION_BUS_ADDRESS unset and XDG_RUNTIME_DIR at a scratch folder (inside a
  systemd-run scope for sanitizer runs), or under ~/.local/bin/xvfb-run.
- AP off = the disc's game untouched; nothing from the discs in git or in anything published.
- Cap command output; long jobs in the background with deadlines; scratch under scratchpad/<name>/.
- Roll over at ~400k context: bring the log up to date and stop.

Discs: NA disc 1 = `/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/Star Ocean - The Second Story (Disc 1).cue`
(disc 2 beside it, "(Disc 2)"); set SS_DISC1 to the .cue, not the folder.

## Log
- 2026-10-27: coordinator log written; utworld (B1) and dskeys (B5) started.
- 2026-10-27: D8 answered yes (story awareness in story mode, part of B6).
- 2026-10-27: dskeys done (keys `so2_map_{team}_{slot}` {map[,x,z]} and `so2_story_{team}_{slot}`
  {chapter,step}; docs/ap/data_storage.md). Notes for later: ASan not run on the net change; a
  `--map` debug start begins at chapter 3 step 0. maprender started.
- 2026-10-27: utworld done. Open for the user: 0.2.0 rooms with Skillsanity (or a 0.2.0 hell seed)
  can't be reproduced (0.2.1 changed those rules) and are refused (ut.OLD_LOGIC); the user's own room
  (~/.local/share/Archipelago/output, 0.2.0 hub + insanity) is one. Asked: keep refusing, or accept
  with a warning (only if the rebuilt logic is always conservative). For B3: add `tracker` to
  ut.NEW_KEYS; bump ut.LOGIC_VERSION with any logic change. For B7: release_checklist rows for
  tools/ap_ut_check.py and SO2_SLOW_TESTS. Baseline trap: a `cp -as` farm's linked Generate.py imports
  the live world (use real copies of top-level scripts). rulebuilder started.
- 2026-10-27: maprender done (`--render-tracker-maps [DIR]`; format docs/ap/tracker_maps.md, "For the
  pack"). Later, not blocking: a setup button for the command (Windows players); shopkeeper dots if
  wanted; better map labels in map_names.csv; the Windows build itself not run (MinGW syntax only).
  packgen (B4a) started alongside rulebuilder.
- 2026-10-27: rulebuilder committed B2 (0cc0bda, 1d879a2), finishing. packgen done: logic seam
  `^$so2_access|<id>` in tools/poptracker/logic.lua (docs/ap/poptracker.md "The logic seam"); Lua tests
  via lupa in scratchpad/ap/ap-venv; PopTracker 0.36.0 AppImage + pack checker kept in
  scratchpad/packgen/ for B4b/B7. For the user (needed at B7): versions.json location (proposal:
  tools/poptracker/versions.json on main) and which release carries the pack zip. For B7: checklist
  rows `tools/build_poptracker.sh all`, tools/ap_poptracker_smoke.py. portcheck started.
- 2026-10-27: rulebuilder done. For B3: equal resolved rules are one object (intern by identity);
  `logic.as_rule(r).to_dict()` serialises; events have names, no AP id; plain spots carry True_. For
  Phase C: CanReachRegion must be added to logic.RESOLVED_KINDS / as_rule.
- **Incident (2026-10-27): a desktop dialog.** tools/ap_ut_check.py's UT copy had no Players folder,
  so UT opened a kdialog folder picker on the user's desktop (utworld's run; the user answered it,
  and the pick, ~/.local/share/Archipelago/Players, stuck in scratchpad/ap_ut's host.yaml;
  rulebuilder's run hit it again). Fixed by the coordinator: the script makes an empty Players folder
  in its copy, forces UT's player_files_path to it, and runs everything without DISPLAY /
  WAYLAND_DISPLAY / DBUS_SESSION_BUS_ADDRESS so no dialog can open; scratchpad/ap_ut's host.yaml
  repointed. Every agent brief that runs third-party GUIs-with-headless-modes now says: strip the
  display variables (also docs/HANDOFF.md "Conventions to keep"). Rerun after the fix: UT 40/40
  match, exit 0, no dialog (e23ea8f, d9ed4e7). logicexport (B3 + B4b + the 0.2.0 warning) started.
- 2026-10-27: portcheck done; the port lane has nothing left before the release. B6 waits for
  logicexport (both edit world.py and the pack generator).
