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
| World | B3 logic export + B4b Lua evaluator + 0.2.0 Skillsanity rooms accepted with a warning | logicexport | docs/handoff/logicexport.md | started 2026-10-27 |
| World | B4a PopTracker pack (all but the logic evaluator) | packgen | docs/handoff/packgen.md | **done** 2026-10-27 (65ed8ce 2ad5530 3c5db64 80d1b8d cbd1231; tests 18/18, pack checker strict OK, real PopTracker 0.36.0 headless smoke 20/20 both variants) |
| Port | ASan, Windows build + Wine render, setup button | portcheck (sonnet) | docs/handoff/portcheck.md | started 2026-10-27 |
| World | B6 UT map tab + story awareness (D8 yes) | (after B4 + B5) | | queued |
| Both | B7 docs, release checklist, release | (last) | | queued; release only with the user's go-ahead |

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
