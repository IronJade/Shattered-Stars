# Handoff: hubdata (W1 hub state, W3 boss chamber table, dungeon keys, seed `hub` / `bosses` format)

Agent "hubdata", started 2026-10-25. Plan and the user's decisions: docs/handoff/hub.md (they
override scratchpad/openworld/story_flags.md where they differ).

## Rules given
- Don't edit engine/ or apworld/. Edit tools/ (new tools; ap_world_tables.py only by adding),
  docs/ap/ (hub_state.json, the bosses output, seed_format.md's new "Hub" section), this log.
  Don't commit.
- Game runs only if needed (prefer the late proof's state code): headless, under tools/memcap.py
  (SS_MEM_CAP_GB 4), at most 2 at once, never a GUI / PCSX-Redux GUI (`-cli` only), never a flag
  that saves ~/.config/shattered-stars/config.ini. Discs: SS_DISC1 = Emulation/Roms/PS/Star Ocean
  - The Second Story (Disc 1)/...cue. Late save: scratchpad/latesave/card.mcd (frame 13).
- Scratch in scratchpad/hub/ (not /tmp). Cap command output.
- Log current at least every 20 minutes. Permission refusal: stop, report the exact command.
- Past ~400k tokens of context: update this log, end the turn saying so.
- Agree the seed format with "hubvm" through docs/handoff/hubvm.md.

## Done and verified
- seed_format.md "Hub (`world_mode: hub`)" section written (end of file): version 3,
  `hub` block, `bosses` rows (hubvm's proposed shape adopted + `game`, reference keys), dungeon
  keys, sidecar. Example values illustrative until hub_bosses.json exists. Note left in
  docs/handoff/hubvm.md ("From hubdata").

## Findings so far (for the tools)
- Late save (card frame 13): chapter 60 step 4000, 1406 flags on, 318 of them done flags ->
  1088 set in the late state; 57 nonzero globals. Flag 556 (Limiter Off) is SET in the save and
  445 set: the hub must clear 556 (goal). Ashton's quest G[0x1bc]=9999 (done), Opera's
  G[0x1be]=0, Precis's chain G[0x1e4]=99999.
- Synard spots (table A 0x19 set_point): Expel +0x26c/+0x28c/+0x270/+0x27c (save: 0xc729,-150,
  0x43fd,90: the user flew there), Nede +0x274/+0x290/+0x278/+0x27e. Game's own spots: Arlia
  (0,51209,-150,17464,90: maps 17/19 + Virtual Arlia 18 0x5a4), Fun City (1,81546,-258,33880,176:
  map 279 0xa06c). Constant set_point calls list in scratchpad/hub (scene.py helpers).
- Start points: Fun City 279 (470,0,68,dir 180) from door 68686160; Arlia 17 (-126,0,-614,90)
  from door 59576065.
- Relationship values live in config +0x58 / +0xe8 (not flags/globals): neutral at New Game.
  Ending flags 750-753, 427 (0x1ab Celine+Chris), 355 (0x163 Ashton+Eleanor).
- Boss scene summaries: scratchpad/hub/scenes/ENC_MAP.txt (scratchpad/hub/scene.py M 0xROUTINE).

- W1 DONE: tools/ap_hub_state.py -> docs/ap/hub_state.json (needs SS_DISC1; ~13 s). Hub flags 1063
  (late 1088): reset 24 = boss guards 9 (162-165 Fields, 175, 449, 483, 518, 528, 587 minus the
  already-cleared), PA 13 (306 315 320 330 334 366 376 380 401 404 413 420 445), ending 1 (556),
  key item 84 (Field of Courage gives it). Categories: story 104, chest 282, gift_guard 22,
  boss_guard 25, pa 13, ending 7, other 966 (854 "no setter": flags no script stores globally,
  probably local-flag spill / native; kept as the late proof had them). Globals: names, text
  speed, positions not set; statistics (+0x244..+0x24c), familiar goods (+0x250), saved party
  order (+0x280..: A 0x17 would move members!), familiar kind (+0x288), vehicle 4 zeroed;
  Synard spots = Arlia's / start town's own; quest words kept.

## W3 findings (tools/ap_hub_bosses.py, in progress)
- P5 solved by views (approach rooms in `maps`):
  - Bark 454: map 453's door (routine 0x3bd0) goes to 455 when chapter > 55, to 454 otherwise;
    view (55, 0) on maps [453, 454] -> 454's unconditional trigger -> Bark -> step 1000 -> 455.
  - Synard 269: map 264 (Synard Reception Area) doors 0x7390 / 0x7410 go to 269 at chapter
    <= 38, else 270 (same spot, later version); view (38, 4000) on [264, 269]; 264 has scenes
    only at 38/0 and 38/1000 (cosmetic NPCs at 38). Aftermath 269 -> 271 (38/4000 scene ->
    step 5000 -> 272 / 264).
  - Field of Intelligence 554: 553's door picks 554 iff flag 175 clear; the hub clears 175 ->
    reachable without a view of 553. Aftermath 554 -> 555 (42: step 1000 -> 628 / step 1010 ->
    map 14!) -> ... -> 556 (sets 162).
  - Eluria Tower: door 557 -> 558 (0x7808) needs chapter > 32 (holds at 60, yet the sweep never
    fired it): still a P5 headless look. Proposal: Berle 575's view over the tower's maps at
    (33, 0); Shin 569 (flag 587 guard) then plays inside it.
  - Arena 305: doors from 279 (0xa100), 297 (choice 1), 301, 302: P5 headless look.
- Cut targets that are NOT safe free play at chapter 60: Berle 575 -> 576 (576 stores chapter 34
  unconditionally, then 625 = the disc-switch scene map) -> needs a cut redirect; Field of
  Courage 642 -> 625 in Claude's game (625 hosts its 42/10 vision and returns to 642 at step 20)
  -> 625 must be a continuation map; Magichand -> 555 / 628 / 14; Synard -> 271.
- Field of Power: 604's end scene is guarded only by flags (528 == 1 && 163 == 0), no chapter:
  after Guardian 603 it would run outside a view at chapter 60 -> needs the view to last until a
  transfer out of [603, 604] (not until free play).
- Format extensions to propose to hubvm: `after_maps` (continuation-only maps), `until`
  ("free_play" default / "transfer"), `cut_to` (redirect of the cut's transfer).

- W3 DONE (first version): tools/ap_hub_bosses.py -> docs/ap/hub_bosses.json (needs SS_DISC1 and
  docs/ap/hub_state.json; ~13 s). 22 chamber rows: encounter, map, maps, view {chapter, step,
  flags, keep, after_maps / until / cut_to where curated}, view_flags_why, guard, trigger list
  (thread_start with region / setup / talk), events, cut, side_effects, late (both games), p5,
  dungeon, location. Views: 19; no view (flag-guarded / set battle): Visseyer 209, Shin 519,
  Cross Cave 523. p5: Shin 519 / Berle 520 (tower past 557), Marsilio 532 (arena 305). Dungeons:
  20 key groups with doors on the late edges, chambers, chests; town chambers 306 / 281 / 305.
- seed_format.md "Hub" updated with hubvm's answers (step_maps, byte-global object form, 745,
  Arcmene) and three PROPOSED view keys (after_maps, until, cut_to); proposal posted in hubvm.md.

## In progress
- Nothing running. Waiting on hubvm's answer to after_maps / until / cut_to.

## Next steps
1. Read hubvm.md for the answer on after_maps / until / cut_to; rename / drop in the tool's
   CURATED and in seed_format.md accordingly.
2. Headless checks per chamber are hubvm's / hubcore's (SS_HUB_TRACE=1 lists every store a scene
   makes in its view: use it to confirm each row's flag list; rows with `view_check` list flags
   whose story value the scripts leave unclear: mostly 740-744, plus 508 (Lover) and 2327
   (Marsilio)).
3. Open for the main session: random start set (only Fun City / Arlia now); drop the Lasguss and
   Portal of Sealing keys (nothing usable behind them) -> 18 keys?; a Fienal key on top of D6?;
   town chambers keyless (sphere 1)?; the Field of Intelligence aftermath's map 14 vision.

## How to verify
- `SS_DISC1=<disc 1 cue> python3 tools/ap_hub_state.py` prints the per-category counts (hub flags
  1063 of the late 1088; reset 24). `python3 tools/ap_hub_bosses.py` prints one line per chamber
  and per key group. Both are deterministic (no game run).
- Spot checks: hub_state.json `flags["556"]` (ending, hub 0), `globals_table` (Synard spots,
  zeroed party order); hub_bosses.json row 531 (maps [453, 454], view 55/0), row 522 (Arcmene's
  curated flags, keep [11]).
