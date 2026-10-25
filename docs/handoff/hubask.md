# Handoff: hubask (the user's rulings on Shigeo 281 and the arena 305: ask first, an exit)

Agent "hubask", started 2026-10-25. Spec: docs/handoff/hub.md, last entries ("the user's rulings on
hubcore2's two questions"). Background: docs/handoff/hubcore.md "hubcore2" (the arena's
`to.while_unbeaten` door, hub smoke cases), docs/handoff/hubchambers.md (Shigeo 281 / Marsilio 305
rows, tools/ap_hub_chambers.py), docs/handoff/hubvm.md (a view's start / end), seed_format.md "Hub".

## The rulings (the spec)
1. **Shigeo 281, ask first.** Crossing Fun City 281 while Shigeo (533) is unbeaten starts his scene
   and fight. Ask first ("Face Shigeo now?"): yes plays the original scene and fight through the
   story view; no lets the party pass without the scene.
2. **The arena 305, ask plus a port-side exit.** "players will generally use the arena in Fun City
   as a way to level up their characters. They need to be able to do that without being forced to
   go through Marsilio first." Ask at the arena's entrances (the redirected door 19929509 and the
   registration desk): yes plays Marsilio's (532) scene and fight; no gives the arena's normal use
   (its ordinary battles) at the hub state. Inside Marsilio's view the party must have a way back
   out without fighting.
- Coordinator's design: the question at the chamber's entrance (the transfer into it); yes = the
  view on; no = the map at the hub's own chapter 60. A generic seed row key (e.g. `ask: {text}` on
  a bosses row) the APWorld writes. The game's own choice windows. "No" records nothing (the boss
  stays unbeaten, the question comes again). Save / load inside either room stays consistent. The
  arena exit proven headless.
- APWorld follow-ups (coordinator, 2026-10-25): (1) regenerate data.py (hub_bosses.json 13:47; must
  cmp identically); (2) door row 44502760 (679 -> 674 east arrival while Cyril 538 unbeaten,
  `to.while_unbeaten`); (3) the arena gift's rule (needs 532 won? check against the No path);
  (4) `hub.CONFIRMED_OPTIONAL = {199}` (XINE), keep the Lasguss key. Then world tests, AP general,
  hub batch (check.py 0 errors), identity batch (192/192), per docs/handoff/apworld2.md.

## Rules given
- Own build dir engine/build-hubask (configured like engine/build-hubcore). Shared tree: keep it
  compiling after every edit.
- SS_DISC1=".../Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/...(Disc 1).cue".
- Suites before reporting verified, with counts. Baseline (hubcore2's build): ss_tests 99/0,
  ss_game_tests 170/0, smoke 161/0.
- Headless only (`--headless`); never a window / GUI; never a flag that saves
  ~/.config/shattered-stars/config.ini; scratch XDG_CONFIG_HOME; SDL_AUDIO_DRIVER=dummy.
- Every game process under 4 GB (tools/memcap.py run_capped or prlimit --as); at most 2 at once; a
  cap hit is a failure to investigate, never a retry.
- Headless loads of ordinary saves need SS_AP_ANY_SAVE=1.
- Enhance, never degrade: without a hub seed nothing changes; Archipelago off = the original.
- Scratch in scratchpad/hubask/ (not /tmp). Cap output. Kill only by exact PID. Waits have
  deadlines; long jobs in the background.
- Don't commit, don't push.
- Ownership: engine/src/game/ap/hub.{h,cpp}, ap.{h,cpp}, seed.{h,cpp}; the APWorld rows for this
  (apworld/so2/hub.py, tools/ap_world_tables.py, data.py). Others: hubproof
  (docs/handoff/hubproof.md; may edit hub.py / ap_world_tables.py for its proof switch),
  hubchambers2 (docs/handoff/hubchambers.md; tools/ap_hub_bosses.py, hub_bosses.json,
  script_ext.cpp ext 0xCB). Shared files: dated notes in their logs, small hunks.
- Permission refusal: stop and report the exact command. Past ~400k tokens: update this log, end.
- A ruling that can't be met as written: stop and report with options and a recommendation.

## Done and verified
- (headless proofs on engine/build-hubask; suites not yet run on the final code)
- **Shigeo, yes** (run shigeo_yes: 279:T0, 280:T12 answered yes (the sweep's confirm), 281:T0):
  "ask: Shigeo, encounter 533: door 18356813 (map 280 -> map 281): ..." -> "yes: map 281 with
  its story view" -> view on (57/2000) -> the scene, battle 533 (stub) won -> view off at free play.
- **Shigeo, no** (shigeo_no: 280:T12:1): "no: map 281 at the hub's state", "chamber map 281 ...: no
  story view (the answer was no)"; then 281:T3 -> 297 -> 299:T6 -> 303:K25 -> map 18 (Virtual
  Arlia, Expel). A real walk across 281 at the hub's state (walk281b: --map 281 --map-pos
  -3795,0,-3406,2048 --input 60:up,640:right,720:up): the strip T0 (0x855c) fires at view
  (215, 214) and does nothing at chapter 60, T3 -> map 297; no battle.
- **The arena desk** (297 K16, routine 0x5c20): its first menu gets a third option "Face Marsilio."
  (desk_face: 297:K16:2 -> 305 with the view on (57/20); the Clerk's menu gets a fourth option
  "Leave the arena."; 305:K16:2 "Go see Narl" -> battle 532 won -> 296, view off). "Register."
  (desk_reg: 297:K16:1/0/0 = Register, Enter tournament, Duel Battle; seed with start Fol 50000):
  305 with no view, the normal arena: four battles (vblank 5657-6751), then 304.
- **The exit** (door_exit: 304:T4 yes -> 305 view on, 305:K16:3 "Leave the arena." -> 304 (1339, 0,
  -6), view off, "dropped flag 745=0"; then 304:T4 asks again (no -> ... ) ).
- **The redirect door, no** (door_no: 304:T4:1): "no: door 19929509 goes to its own target (map
  296)" -> 296. (Fixed: 'own' aliased 'args' in the last phase.)
- Captures (scratchpad/hubask/cap/q/frame03332.png): the question in the game's windows, sized to
  its widest line (the layout wraps at the window's width less 16 px).

- **Save / load** (save305 / load305; save281 / load281): a save made in Marsilio's view (304 walk
  into the door, yes, camp-menu save with SS_SAVE_ANYWHERE) writes `"hub_view": 532` in its
  sidecar; its load: "the save was made in its story view (a yes): on again with the map's load",
  the view on, the Clerk's "Leave the arena." -> 304. A save in 281 at the hub's state: no
  `hub_view`, the load comes up without the view. (Neither room has a save point: normal play can't
  save there; the debug can.) Without `hub_view` a load in 305 at the hub's state is the empty ring
  with no exit (305's arena routine runs only after the desk's registration): hence `hub_view`.
- **Releases:** the sites' words checked on NA / EU / FR / DE discs (297 alike; 305 through the
  region tables' script map); the answers' lead-in is 0xfe 0xfe 0xea (NA / EU) or 0x72 x4 + 0x82
  (FR / DE); FR capture scratchpad/hubask/cap/fr/frame00200.png.
- **Unit test** ap_hub_ask (test_script_vm.cpp): format + refusals, the phases at a transfer (yes,
  no, cancel), the redirect's no -> own target, a transfer not in `doors`, the options site (window
  one line taller, the choice +1, the extra option -> 305 / -> 304, closed without the view, a
  changed word), saves (hub_asked_view / hub_resume_view), won, no seed. ss_game_tests 173/0.
- **Smoke** (tools/smoke_field_events.sh, hub block): the smoke's hub seeds now carry the two `ask`
  rows (= hub.ASK) and a Fol seed; new cases ap_hub_ask_yes / _no / _walk / _desk / _register /
  _exit / _save / _load (34 checks). Hub block alone (scratchpad/hubask/mini_hub.sh): 56 PASS, 0
  FAIL (the 22 older hub checks unchanged).
- **APWorld:** hub.py `ASK` (+ seed_bosses, BOSS_KEYS "ask"), `P5_DOORS` + Cyril 44502760,
  `CONFIRMED_OPTIONAL = {199}`; data.py regenerated (only the Cross Cave guardians' trigger
  changed; regenerates identically); test_hub.py (seed rows' ask, the two P5 doors, TestHubAsk);
  scratchpad/apworld2/batch/check.py (P5 doors, ask rows). World tests 313 passed (22613 subtests);
  AP general 379 passed. Arena gift: no rule needed (the normal arena is open while Marsilio is
  unbeaten; the gift was never gated in logic).
- Docs: seed_format.md (the `ask` row, "The port: the questions", `hub_view` in both sidecar
  sections, the P5 arena bullet), docs/ap/apworld.md (asks, Cyril's door, XINE), the player guide
  (both chambers ask; training at any time), engine/README.md, docs/catalog/formats.md (the desk,
  the Clerk, the strip, the lead-in glyphs, the wrap width). Notes: hubproof.md "For hubproof (from
  hubask)".

- **Final suites (engine/build-hubask, final code, 2026-10-25):** ss_tests 99/0 (SS_DISC1),
  ss_game_tests 173/0 (incl. ap_hub_ask), smoke 198/0, rc 0 (161 baseline + 3 from another agent's
  hero-select cases + 34 ap_hub_ask_*; scratchpad/hubask/smoke2.log). An earlier full run
  (smoke1.log) failed ap_hub_fienal once ("then Fienal opens"): charselect's New Game change
  (no hero select with story_route claude / rena: the field starts ~550 vblanks earlier) had made
  its console `battle 524` at vblank 3050 too late; charselect moved it to 2760 (their note
  below); it passes since.
- **APWorld final:** world tests 313 passed / 22613 subtests; AP general 379 passed; data.py
  regenerates identically; hub batch (players_hub -> hub_ask, seeds 1-96) 96/96 exit 0, check.py
  0 errors on 192 files (154 hub slots); identity batch (players -> full_hubask) 96/96, compare.py
  vs `full`: 192 identical; check.py catches 3 of 3 mutations (scratchpad/hubask/neg). A generated
  seed (hub_ask/18, Fun City, story_route any) played in the port: Shigeo's question, no, the
  arena door's question, yes, the Clerk's "Leave the arena." -> 304 (run gen18).

## In progress
- Nothing. Final report sent.

## Next steps (for others)
- hubproof: regenerate data.py once more with docs/ap/hub_proof.json; rerun the nodes behind 297's
  registration (the arena's gift is reachable without Marsilio now). Note in hubproof.md.
- charselect: their item 2 (starting character in ap / seed / the sidecar) is theirs to make;
  answered in docs/handoff/charselect.md "For charselect (from hubask)".

## Decisions for the user (reported)
1. The desk and the Clerk ask through an extra option on their own menus, not a yes / no (the
   doors into 281 and the 304 door ask yes / no): reasons in "Design (as built)".
2. The question comes at every crossing of 281 until Shigeo is beaten (as ruled: "the question
   comes again next time"); the Nede <-> Expel route asks both ways.
3. A save made in an asked chamber's view loads back into it (sidecar `hub_view`); neither room
   has a save point in normal play.
4. The texts are APWorld data (hub.ASK), English in every release like the harbour's.

## Design (as built)
- Seed: a bosses row's `ask` {text (1-3 lines of <= 34 chars, "\n"), answers? [yes, no] (default
  "Yes." / "No."), doors? [door ids] (default: every transfer into the chamber's maps from outside
  them asks), options? [{map, window, choice, text, to}]}. While the boss is unbeaten the view comes
  on only after a yes; no, a load or any other way in = the chamber at the hub's own state.
- The question (hub_ask_transfer, from ap::on_map_transfer after the door table): the transfer op is
  the host: ip back on it (stack args back on the stack) while the phases run: window 1 (the
  question, hold, sized to the widest line in the map font), wait for it (wait state 2), window 0
  (the answers with the game's lead-in glyphs 0xfe 0xfe 0xea), the VM's own choice (wait state 9,
  cancel = no), both windows closed (wait 2 / -1), then the transfer: yes as the door table sent it
  (decision yes -> the next map load turns the view on); no: a seed door that had sent it into the
  chamber (to.while_unbeaten) goes to its own target; else the chamber with no view.
- Options (hub_fetch at the VM's fetch, armed per map by hub_script_loaded after checking the words:
  ext 0x68 inline at `window`, ext 0xdc N inline at `choice`, then push R / pop_store L[n] (0x22)):
  the options window opened from the stack one line taller with the extra line (hub_script_text via
  ap::script_text: the original's lines, its lead-in, the text), the choice with N + 1 options, and
  at the instruction after the store, R == N: windows 0 / 1 closed, ext 0x84, ext 0xda to `to`
  (into the chamber: decision yes). On a chamber map only while its view is on.
- Why options at the desk and the Clerk instead of a yes / no there: the desk's own transfer into 305
  comes after the fee, the battle mode and flag 586 / the split flags (a team battle takes members
  out; only 305's arena routine 0x91c0 puts them back), so a question at that transfer would leave a
  yes with a paid fee and a split party; a third option on its first menu ("Hear an explanation /
  Register / Face Marsilio.") asks before any of it and leaves "Register." the arena as it is. 305 is
  the arena ring itself (no door, the Clerk its only entry): "Leave the arena." on the Clerk's menu is
  the way out a player finds (an invisible region at the arrival would not be), and the Clerk's own
  options keep their numbers (the chamber runner's 305:K16:2 still picks "Go see Narl").

## How to verify
- Build: `cmake --build engine/build-hubask -j8` (configured like build-hubcore: deps from
  build-hubvm/_deps). Suites: `SS_DISC1=<cue> engine/build-hubask/ss_tests`,
  `engine/build-hubask/ss_game_tests` (ap_hub_ask), `TMPDIR=$PWD/scratchpad/hubask/smoke
  XDG_CONFIG_HOME=$PWD/scratchpad/hubask/xdg tools/smoke_field_events.sh
  engine/build-hubask/shattered-stars` (the ap_hub_ask_* cases; the hub block alone:
  scratchpad/hubask/mini_hub.sh, regenerated from the smoke by the python in this log's history:
  lines 1-35 + NEWGAME_A/B + the hub block).
- Runs: `python3 scratchpad/hubask/mkseed.py OUT --ask scratchpad/hubask/ask.json` (the smoke's
  hubfc + the `ask` rows), `python3 scratchpad/hubask/run_ng.py NAME SEED FRAMES claude
  KEY=VALUE...` (logs scratchpad/hubask/runs/NAME.log). Routes: Shigeo yes `SS_SWEEP_PLAY=279:T0,
  280:T12,281:T0`; no `279:T0,280:T12:1,281:T3,297:T4,299:T6,303:K25`; walk `MAP=281
  SS_HUB_STATE=1 ARGS+=--map-pos -3795,0,-3406,2048 --input 60:up,640:right,720:up`; desk
  `...,281:T3,297:K16:2,305:K16:2`; Register `...,297:K16:1/0/0` (seed with start Fol:
  askfol.apso2); exit `...,297:T4,299:T2,304:T4,305:K16:3,304:T4:1`; save in the view: `MAP=304
  SS_HUB_STATE=1 SS_SAVE_ANYWHERE=1 SS_MEMCARD1=CARD ARGS+=--map-pos 1339,0,-6,2048 --input
  60:up,330:none,450:cross,...,1000:triangle,...` (as the smoke's ap_hub_ask_save), load
  `NOINPUT=1 SS_LOAD_SAVE=0 SS_MEMCARD1=CARD`.
- Releases: `python3 scratchpad/hubask/regions3.py` (the sites' words on FR / DE / EU through
  engine/data/regions/*.inc); scratchpad/hubask/regions.py (NA words, strings).
- APWorld (docs/handoff/apworld2.md "How to verify"): world tests, AP general, `python3
  tools/ap_world_tables.py -o X && cmp X apworld/so2/data.py`, `./run_batch.sh players_hub hub_ask
  $(seq 1 96)` + check.py, `./run_batch.sh players full_hubask $(seq 1 96)` + `compare.py
  full_hubask full`; check.py mutations: scratchpad/hubask/neg (3 of 3 caught).

## For hubask (from hubproof, 2026-10-25 14:15)
1. **Shared APWorld files.** My switch (hubproof item 5) touches, in apworld/so2/hub.py: the
   `from .data import` line (adds HUB_MAPS, HUB_REACHED), one new line after DUNGEON_OF_MAP
   (`HUB_MAPS_OF`), `reached_games` (chest / gift / boss -> data.HUB_REACHED), `drop_rule` (places
   -> the hub proof's maps), `may_hold_progression` (-> reached_games), the module docstring and
   the CONFIRMED_OPTIONAL comment; in tools/ap_world_tables.py: `hub_tables(..., hub_proof=None)`
   (HUB_REACHED / HUB_MAPS, shops / drops from the hub proof's maps), `write_hub`, main's
   `--hub-proof` (default docs/ap/hub_proof.json); and test_hub.py's `row["late"]` uses + the
   default counts; scratchpad/apworld2/batch/check.py's two hub reach lines. Already in the tool
   (inert until docs/ap/hub_proof.json exists: without it data.py regenerates as before). The
   hub.py hunks go in with data.py's regeneration once the hub proof is final (~15:00). Your
   P5_DOORS / CONFIRMED_OPTIONAL / arena-gift edits don't overlap them; tell me here if you plan
   to regenerate data.py yourself, so we do it once (it must then include docs/ap/hub_proof.json).
2. The hub proof reads `P5_DOORS` from hub.py at run time (no import): add 44502760 there and the
   next proof run takes it.
3. XINE (199): the hub proof's first run checked XINE's boss location in Claude's game at the hub
   state (Lasguss 385), which agrees with your CONFIRMED_OPTIONAL = {199}.
4. Your ask on 281 / 305 changes what a real walk meets there; the proof fires doors directly, so
   its reach doesn't change, except the arena 305 at the hub's own chapter after "no" (the arena's
   ordinary use: its gift / shops). When it lands, tell me (docs/handoff/hubproof.md "For
   hubproof") and I rerun the nodes behind 304 / 297's registration.

## For hubask (from charselect, 2026-10-25 15:30)
1. **New Game skips the hero select and the naming screen** when the seed's `story_route` is
   claude / rena (all modes; title code engine/src/game/title/new_game.cpp only, no ap.cpp /
   seed.cpp change). The game starts ~550 vblanks earlier from the same inputs. In
   tools/smoke_field_events.sh I changed: the `ap_hero_*` block (claude / rena check the skip,
   new `ap_hero_any`), `hub_ng` (no Left / Right press: `--input "$NEWGAME_A,$NEWGAME_B"`), and
   `ap_hub_fienal`'s debug battle 3050 -> 2760 (the walk now reached the Fienal door before the
   win). All other hub cases pass unchanged (smoke 197 PASS / 1 FAIL before that fix). Any new hub
   case with timed SS_DEBUG_INPUT: the field starts at ~2714 with the smoke's NEWGAME inputs.
2. **Coming (after the user's decision on the character select's text):** the starting character
   chosen at New Game (`starting_character: "any"` or a list) has to reach the AP runtime and the
   sidecar. I expect: seed.cpp accepting `"any"` / a list for `options.starting_character` (today
   "any" fails the seed load), an `ap::` setter the title calls before start_new_game, and the
   chosen id in the sidecar so `start_member` survives loads. Small separate hunks in ap.cpp /
   ap.h / seed.cpp / seed.h; I will write the exact hunks here before making them, or you take
   them, whichever you prefer (answer in docs/handoff/charselect.md "For charselect").
