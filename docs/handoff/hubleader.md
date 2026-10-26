# Handoff: hubleader (the leader stand-in in hub mode)

Agent "hubleader", 2026-10-25. Build dir `engine/build-hubleader` (configured like build-hubcore:
RelWithDebInfo, SS_BUILD_AP_NET / SS_BUILD_SETUP on). Scratch: scratchpad/hubleader/.

## The gap and the ruling

Found by "wiki" (docs/handoff/wiki.md "0.02.50 pass", "Couldn't confirm"): field/leader.cpp
`candidate()` applied the leader stand-in (docs/handoff/leader.md: the walking actor drawn as the
Formation leader, the hero's name token read as the leader's) only to `world_mode: open_world`
seeds, while the option's docstring (apworld/so2/options.py StartingCharacter), the player guide
and D7 (docs/handoff/hub.md: "the open-world party v2 rules and the starting character apply")
say hub mode too. **The user's ruling (2026-10-25): fix the port; the stand-in applies in hub
mode as well.**

## Rules given

- Headless only; never a window. Own build dir, -j8. SS_DISC1 = disc 1's .cue (disc 2 beside it).
- Every game process under the 4 GB cap (tools/memcap.py or prlimit / ulimit), at most 2 at once.
- Scratch XDG_CONFIG_HOME; never a config-saving flag. SDL_AUDIO_DRIVER=dummy. SS_AP_ANY_SAVE=1
  only for headless loads of ordinary saves with a seed.
- Scratch in scratchpad/hubleader/; cap output. No commits (the user reviews). Wiki checkout
  scratchpad/wiki: edit only, no commit, no push. Enhance, never degrade.
- A hub scene that breaks with the stand-in: stop and report with options.

## Done

- **Code** (one gate): engine/src/game/field/leader.cpp `candidate()`: `open_world || hub`
  (story mode, no seed, Archipelago off: unchanged). Comments: leader.h / leader.cpp /
  script_ext.cpp / actor_field.cpp say "open world and hub". Nothing else gated the stand-in on
  open world (the starting character's join and lead already ran in hub mode: party_v2 /
  give_start; hubcore's proof "Ashton joins and leads").
- **Unit test**: engine/tests/test_leader.cpp `leader_standin_follows_the_formation_leader`: a
  hub seed (`Seed::hub`) gives the stand-in (Ashton, Claude's game).
- **Runner**: tools/ap_hub_chambers.py `--starting-character NAME` (seed option; SS_LEADER_TRACE
  on; asserts "NAME leads" and the stand-in drawn on the run's maps; lists the messages
  rewritten per map and what showed the hero).
- **Smoke** (tools/smoke_field_events.sh, hub section): `ap_hub_ng_cs` also checks Celine walks
  Fun City; new `ap_hub_leader_ng` (named Ashton: confirm, leads, drawn on 279, out to the world
  map) and `ap_hub_leader_alentax` (SS_HUB_STATE --map 68 -> 61: the view, Ashton drawn, "message
  rewritten 1 6", the scene-animation latch, 191 won). Seed `hubashton`.
- **Docs**: seed_format.md ("starting_character" contract: open world and hub; Hub's party line;
  "The leader stand-in": `open_world` or `hub`, a hub bullet, the debug line), engine/README.md
  (the hub bullet), the wiki checkout scratchpad/wiki/Archipelago.md (the leader paragraph and
  the known issue: open world and hub mode).

## Proof (headless, build-hubleader)

- **Hub New Game** (scratchpad/hubleader/run.py; seeds scratchpad/hubleader/seeds/ from
  mkseeds.py, Fun City start, Claude's game):
  - `hub_ashton` (named): "character select: Ashton Anchors", "Ashton leads (Formation leader,
    slot 1)", `[standin] ... map 279 shows 6 (stand-in, own pack)`; SS_ROUTE=279:down walks out
    to map 2 at vblank 3106 (the Synard parked by Fun City); world map shows Claude.
  - `hub_any` (the character select's pick, Bowman / Opera excluded): Opera refused, Celine
    picked, "Celine leads", `shows 2 (stand-in, own pack)`, the same walk to map 2.
  - Shots: shots/ng_grid.png (Ashton / Celine walking Fun City; the world map with Claude),
    shots/crop_ashton_3400.png.
- **Save and load**: New Game (Ashton) -> camp menu save in Fun City (SS_SAVE_ANYWHERE=1) ->
  card_ashton.mcd + sidecar (save_hash, start_given); the load (SS_LOAD_SAVE=0, no
  SS_AP_ANY_SAVE: the guard's own path) shows Ashton from vblank 2 (config +0x41 saved) and walks
  to map 2 (shots/load_grid.png).
- **Chambers** (tools/ap_hub_chambers.py --starting-character Ashton, work
  scratchpad/hubleader/chambers): every check of the chamber runner as before plus the stand-in.
  - alentax (61, Claude's game): Ashton walks in; 5 messages name Ashton (speaker labels "Ashton"
    and Rena's "Ashton!"; shots/alentax_grid.png); at vblank 1985 Claude's own scene animation
    latches Claude for the rest (Rena's later "... Claude!" and Claude's lines; the leader design);
    battle 191 won, check, view end map 53, flags / globals unchanged.
  - bark (454): Ashton walks 450 -> 453 -> 454; the scene creates Ashton's own actor at 727: Claude
    for the scene (no names rewritten); battle 531, check, key item 89.
  - Subset, all PASS: alentax_r, bark_r, crosscave, varmillion (18 messages), azamgil, shin447,
    magichand, lover_r, berle306, marsilio, tower (Shin 569 + Berle 575), fienal1b, synard_r,
    harfainx_r (see "Results" below for the line per chamber).
  - **Presentation only:** alentax, bark and varmillion (18 rewritten lines) with
    SS_LEADER_STANDIN=0 (work chambers_off; the runner then fails only its own "stand-in drawn"
    check, as it should): the pre / post state dumps byte-identical to the stand-in runs, the play
    ends at the same vblank (6974 / 2328 / 10089; "Ashton" and "Claude" have the same length).
  - Runner artefact (not a port issue): a Rena run's --map start walks as Claude until the first
    transfer (the sweep clears flag 1 after the set-up), so the approach map shows the hero
    ("walking actor's model changed").
- **Suites** (build-hubleader, final code): ss_tests 99/0, ss_game_tests 182/0 (the leader test
  gained its hub case; no new test), smoke 282/0 (272 + 10 new checks: ap_hub_ng_cs's stand-in
  line, ap_hub_leader_ng x4, ap_hub_leader_alentax x5); scratchpad/hubleader/{ss_tests,
  ss_game_tests,smoke}.log.

## Results (tools/ap_hub_chambers.py --starting-character Ashton, 16 runs, all PASS)

| Key | Stand-in drawn on | Lines naming Ashton | Hero shown for the scene (latch) |
|---|---|---|---|
| alentax | 68, 61, 53 | 5 (61) | 61 / 53: scene animation |
| alentax_r | 61, 53 | 2 (61) | 61 / 53: scene animation |
| bark | 450, 453, 454, 455 | 0 | 454 / 455: own actor |
| bark_r | 453, 454, 455 | 0 | 454 / 455: own actor |
| crosscave | 546, 547 | 4 (547) | - |
| varmillion | 464, 462, 135 | 18 (462) | - |
| azamgil | 462, 463 | 5 (463) | - |
| shin447 | 444, 447, 439 | 3 (447) | - |
| magichand | 553, 554, 556 | 0 | 554-556 / 628: own actor |
| lover_r | 595, 596 | 0 | 596 / 310: own actor |
| berle306 | 310, 306, 314, 315 | 7 (306) | 314: own actor |
| marsilio | 304, 305, 296 | 0 | 305 / 296: own actor |
| tower | 568-575, 557 | 2 (569) | 575 / 557: own actor |
| fienal1b | 687, 657, 655 | 0 | 657: own actor |
| synard_r | 264, 269, 272 | 1 (269) | 269-272: scene animation |
| harfainx_r | 515, 523 | 0 | 523: own actor |

Each: view on, the battle won once, the check, goal_won, the view's end where it was, story
60/4000, flags / globals as without the stand-in, re-fire sweeps clean. No hub scene broke.

## Open items

- None from this assignment. The leader's known limits (leader.md): afterimages show the hero; a
  latch lasts until free play, so a chamber scene with the hero's own gesture (Alen-Tax) shows the
  hero from there to its end.
