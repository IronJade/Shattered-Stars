# Handoff: leader stand-in (field sprite follows the Formation leader + name swap)

Agent "leader", started 2026-10-25 (session fa70766e). Build dir `engine/build-leader`.

## The request (verbatim) and decisions

"Add option for Selecting Any playable character as the lead. Add code to codify that whatever
character is the 'Leader' is the sprite used by the player (in lieu of the protag)."
Decisions (2026-10-25): open world only; the hero stays in the party; "Yes, swap names": the
leader's name replaces the hero's in dialogue and speaker labels.

Contract: docs/ap/seed_format.md "v2 fields" -> "Party v2" (`starting_character`).
Research: scratchpad/openworld/party_ships.md section B.

## Status

**Built and verified (2026-10-25); only open items below remain.**

- Code: `engine/src/game/field/leader.{h,cpp}` (new: the rule, the scene latch, the display-only
  sprite swap, the own-pack fallback, the message token rewrite, SS_LEADER_* debug env), hooks:
  `actor_field.cpp` fieldactor_draw (one `LeaderSpriteSwap` guard after actor_resolve_anim),
  `script_ext.cpp` (message_text, the two open_default ops and the notice op pass script strings
  through `leader_message_text`), `engine/CMakeLists.txt` (leader.cpp; tests/test_leader.cpp),
  `engine/tests/test_leader.cpp` (7 tests), `tools/smoke_field_events.sh` (ap_leader_ow /
  ap_leader_story at the end).
- Docs: docs/ap/seed_format.md "The leader stand-in (what the port does)" (under Party v2),
  engine/README.md Archipelago "Open world" bullet, docs/catalog/globals.csv field `config`
  row (+0x40 / +0x41), docs/catalog/formats.md "Field party sprite packs".
- **Same-state proofs (final code, all byte-identical dumps):**
  1. Claude's opening with the party agent's `starting_character: Ashton`
     (scratchpad/leader/ow_ashton.apso2, `--map 7`, SS_SWEEP_PLAY=8:K20,4:T1,9999:T0) vs the
     same with SS_LEADER_STANDIN=0, to the first free play on map 12 (ch 4 step 100):
     instant + auto 0.5 s (vblank 18386, rng 24 23add229 draws 2994) and typewriter + presses
     (SS_SWEEP_PRESS=all; vblank 19576, rng 30 38802fd6 draws 3055). Ashton joins at vblank 6
     (own pack until map 8), 14 messages rewritten, the map-5 scene animation latches the hero
     through maps 5/6/14/12 until free play.
  2. The same with `starting_character: Noel` (4 letters), typewriter: dumps and rng identical;
     the vblank count differs (19576 vs 19543): name length changes typing time (presentation).
  3. Late save, SS_ROUTE=671:up,684:up into the final scene on 685 (own-actor latch at 4780):
     127 route lines (state changes + rng signatures) identical, dump at 7000 identical.
  4. Map 9 ch 4/2000 (Rena joins; SS_LEADER_STANDIN=1 SS_LEADER_SLOT=1) to control on map 17.
  5. Rena's game (scratchpad/leader/ow_rena.apso2: story_route rena, starting_character Ashton;
     New Game through the title with the smoke script's NEWGAME inputs), event sweep of Arlia's
     36 entries: identical [sweep] logs on vs off; 2 rewritten lines ("Old woman: Hi there,
     Ashton." = rpair_07752.png).
- Captures (scratchpad/leader/shots/; pair_*.png = off | on): pair_01395 / pair_09190 (Ronixis
  says "Ashton", Calnus / ruins), pair_06250 (speaker label "Ashton"), pair_04100 / pair_05480
  (ruins, dungeon), pair_18380 (forest free play), pair_11300 (scene-animation fallback: Claude
  in both), p685_04550 + f685b_grid.png (final scene: Ashton walks in, then the scene's own
  Ashton -> Claude walks, "Claude: How can that..."), cmp_400.png (Fienal, Claude | Ashton),
  world_ash (world map keeps Claude with Ashton leading), cc_ash (Central City, Ashton),
  run_strip.png (running shows the walk), menu_triangle_grid.png / menu_noel_pair.png (camp
  menu Choose Leader -> Claude / Noel at once on closing).
- **Suites (final code, 2026-10-25):** ss_tests 99/0, ss_game_tests 155/0 (7 leader_*),
  smoke 120 PASS / 0 FAIL (incl. ap_leader_ow x3, ap_leader_story).

## Open items

- Afterimage ghosts of the walking actor (afterimage.cpp copies the Actor) show the hero.
- The rewritten copy makes SS_TRACE's msg_window_open "text=+0x..." offset meaningless (debug).
- A scene latch spans maps until free play (the opening's maps 5-12 show Claude).

## Design (as built)

- Display-only: the walking actor (slot G[0xc4]) keeps the hero's model and its logical sprite
  resolution (anim_data / frame / timer / loops); in fieldactor_draw a guard swaps the drawn
  sprite fields (cel_header, cels, frames, pixels, palettes, cel) to the stand-in's record for
  the same direction animation, and restores them after the draw. Walk/stand frames follow the
  hero's frame (all twelve have 3-frame ping-pong walks / 1-frame stands at 8 vblanks); run
  (heroes only) -> the stand-in's walk on its own clock at the run's pace (6).
- Stand-in = party slot config +0x41 (or SS_LEADER_SLOT) holding an active member (id 1-12),
  not the hero (flag 1 -> Claude, else Rena); open-world seed (or SS_LEADER_STANDIN=1); field
  map only; the walking actor's model must be the hero's (bunny / disguise keep theirs).
- Scene latch ("the hero for this scene"; sprite AND names): set by the stand-in's own actor
  existing (any slot < 0x82 with model == stand-in), a script message naming the stand-in
  (0x400c literal or 0x4007 variable), a scene animation (sprite anim id > 20); cleared when
  free play is observed (control == G[0xc4], unlocked, no trigger/talk/wait/sync, no fade, no
  window).
- Names: script strings with the hero's literal 0x400c token get a copy with the stand-in's
  index (cached per map; the map load destroys all windows first). AP override texts and the
  harbour menu are not touched. 0x4007 (variable) names are never rewritten.
- Debug env: SS_LEADER_STANDIN=0/1, SS_LEADER_SLOT=N (presentation only; never config),
  SS_LEADER_TRACE=1 ("[standin] vblank V map M shows N (why)", message rewrites / latches).
- Free play (latch clear): control == G[0xc4], unlocked, no trigger / talk, VM wait not 1 / 3 /
  8 / 9 (the camp menu's test + choice), no fade, no busy window.
- Own pack: entry 0xc27 + 2 * (1 << (c - 3)) (c 3-7) or 0xc67 + (1 << (c - 8)) (c 8-12), read
  with Loader::archive()->read_from_entry and unpacked privately; only when the 16 slots lack the
  record and field_flags +0x27 (palette tint) is clear.

## How to verify

    export SS_DISC1=".../Star Ocean - The Second Story (Disc 1).cue"
    cmake --build engine/build-leader -j4
    SS_TEST=leader engine/build-leader/ss_game_tests          # 7/0
    engine/build-leader/ss_tests; engine/build-leader/ss_game_tests
    tools/smoke_field_events.sh engine/build-leader/shattered-stars   # ap_leader_* at the end
    cd scratchpad/leader   # run.py LABEL KEY=VAL ... (memcap, scratch config); pair.py
    python3 run.py s_on SS_LEADER_TRACE=1 SS_EVENT_SWEEP=1 "SS_SWEEP_PLAY=8:K20,4:T1,9999:T0" \
      SS_SWEEP_PLAY_STOP=1 SS_SWEEP_PLAY_DUMP=runs/s_on.state SS_SWEEP_PRESS=choices \
      SS_INSTANT_TEXT=1 SS_AUTO_ADVANCE=500 "ARGS=--ap-seed ow_ashton.apso2 --map 7 --frames 300000"
    (same with SS_LEADER_STANDIN=0 -> s_off; cmp runs/s_on.state runs/s_off.state)


## Rules given

- Open-world seeds only (`world_mode: open_world`); story mode and non-AP games unchanged.
- Presentation only: never change script state (flags, chapter, party, RNG). Prove it headless.
- Build in engine/build-leader; tests ss_tests (SS_DISC1), ss_game_tests, smoke script.
- Headless only, every game under tools/memcap.py (SS_MEM_CAP_GB 4), at most 2 at once; never
  the GUI, never a config-saving flag.
- Edit only field sprite/actor/name-token code, engine/tests/, own doc sections; re-read shared
  files right before editing; don't fix other agents' code.
- Keep this log current every ~20 min. Don't commit. Scratch in scratchpad/leader/.
- On a permission refusal stop and report the exact command. Stop at ~400k context.
