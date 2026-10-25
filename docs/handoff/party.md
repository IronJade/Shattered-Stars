# Handoff: port party v2 (open-world party rules, starting character)

Agent "party", started 2026-10-25. Plan item of docs/handoff/apv2.md ("port party v2").

## The assignment

Implement docs/ap/seed_format.md "v2 fields" -> "Party v2 (`party_rules`, `starting_character`)"
in the port: parse the two options (+ accept seed version 2, coordinating with "hooks"); joins
under `party_rules: "open"` (only story members, received members and the starting character
join; no lockouts; received/starting members never overwritten; new join sites Rena 9 0x6b94,
Claude 12 0x6e08, Noel 277 0x6fe0); a verified table of the story's split windows (temporary
members let through and removed at the window's end, received members rejoin after it);
the starting character (joins at the first free play before start.items, Formation leader
config +0x41, counts as received; naming the picked game's hero -> nobody joins); tests, demos,
smoke, docs. NOT mine: the leader's field sprite and the dialogue name swap ("leader" agent).

## Rules given

- Build in engine/build-party (`cmake --build engine/build-party -j4`). Tests:
  `SS_DISC1=<cue> engine/build-party/ss_tests`, `engine/build-party/ss_game_tests`,
  `tools/smoke_field_events.sh engine/build-party/shattered-stars`. Run them before reporting.
- Other agents edit the tree at the same time ("leader": field sprite / map.cpp / message name
  tokens; "hooks": seed.cpp/seed.h v2 goals/doors/craft/killer, ap.cpp goal & check hooks,
  menu_creation.cpp, battle/result.cpp, shop sell lists; an APWorld agent: apworld/). Keep to the
  join/party code, my seed fields, engine/tests/, my doc sections. Re-read a shared file right
  before editing; keep the tree compiling; don't fix others' code (wait or report).
- Headless runs only (`--headless`), never a GUI or PCSX-Redux's GUI (`-cli` only), never a flag
  that saves ~/.config/shattered-stars/config.ini; every game process under tools/memcap.py
  (SS_MEM_CAP_GB 4); at most 2 game processes at once.
- The port enhances, never degrades; a normal game and story-mode seeds play as the original;
  AP behaviour behind the seed's options.
- Keep this file current at least every 20 minutes. Don't commit. Scratch in scratchpad/party/.
  Cap command output. On a permission refusal stop and report the exact command. Roll over at
  ~400k tokens (update this file, end the turn saying so).

## Done

- engine/build-party configured and built (2026-10-25).
- Research (scratchpad/party/splits.md, party_sites.csv, site_conds.txt; tools scan_party.py,
  site_conds.py, ctx.py, mapdis.py): every ext 0xae / 0xaf site in the 820 field scripts with its
  conditions; the split/restore table; restores are by the split's own flags (exact), so only
  members a split doesn't list stay in (extras), and temporary members are needed only where a
  non-hero fights alone (Shingo Forest map 12 in Rena's game; the tournament 477).
- Code (07:30-07:50; builds; ss_game_tests 155/0 incl. 6 new ap_v2_party_* tests):
  - seed.h/.cpp: `Seed::open_party` (options.party_rules story/open), `Seed::starting_character`
    (name / 1-12 / null / "hero" / "" / 0). Version 2 was already accepted by "hooks".
  - ap.h: SaveState `temporary`, `stash` (sidecar keys `temporary`, `stash` [{slot, bytes hex}]);
    `starting_member()`, `split_window_keeping(id)`, `split_window_needing(id)`,
    `party_free_play_step()` (tests).
  - ap.cpp: v2 helpers after character_received (hero_id, start_member, party_v2, story_member,
    protected_member, permitted, story_position, kSplitWindows table, window_keeping /
    window_needing, keeps_slot, free_reserve_slot, borrow_slot / restore_stash, party_v2_update,
    party_line); keep_story_flags / keeps_flag for a protected Celine; join_waiting (no lockouts
    for open rules / the start, waits inside windows, a non-kept reserve slot when full, the
    start becomes Formation leader config +0x41); give_start() (start's join pushed first, then
    start items); free_play_update calls party_v2_update; on_party_add rewritten (phase 3 path
    unchanged; v2: refusal / temporary join / never overwrite); kPartings + 234 0x74C8 (Leon,
    Rena's game) and 417 0x7DB8 (Noel choice 1); on_party_remove for protected members.

- Later the same morning (08:00-08:40):
  - Windows: `kEveryone` vs member-only masks (Shingo Forest keeps nobody away); join_waiting
    holds a member the story set aside (in reserve) for any window keeping them away, a new join
    only for an "everyone" window; v2 party changes wait while the party is set aside for the
    hero (flags 2 / 4, and flag 746 = the map library's isolate routine B 0x29 .. B 0x2a).
  - Join logs carry the story position and game; status_text has a party line.
  - event_sweep.cpp: `SS_SWEEP_PARTY_DROP=c,...` (empties a member's slot before SS_SWEEP_PARTY:
    Rena's game alone from a --map start, which begins as Claude).
  - Tests: ss_game_tests 156/0 (7 new: ap_v2_party_seed / _open_joins / _slots / _windows /
    _borrow / _isolated, ap_v2_starting_character); ss_tests 99/0; smoke 116/0 incl. new
    ap_v2_m12 / ap_v2_t477 / ap_v2_start / ap_v2_linga (run before the last two code changes;
    the four demos re-run after them).
  - Headless demos (scratchpad/party/runs/, runner scratchpad/party/run.py under memcap; seeds
    scratchpad/party/seeds/): m12_rena (Claude let in for the fight, Rena back), every
    tournament fight step at 477 (Claude's game 4400 / 6200 / 10200: Dias; Rena's 6500 / 10200 /
    11500 / 16500: Dias, 8500 / 12600 / 19500: Claude; 19500 loops the must-lose Dias fight under
    SS_BATTLE_STUB with or without a seed: not ours), t468_c / t468_c3 (468's split takes Rena out
    in Claude's game, she stays away for the tournament, Celine / Leon stay; chapter 20 -> "Rena
    joins the party"), linga_c (193's split: received Rena / Celine wait, step 5100 -> both join),
    start_c7 / start_r18 (Ashton / Leon start and lead), lock_c7 (Dias joins Claude's game;
    plain seed: "cannot join"). Later (with SS_AUTO_ADVANCE=1 SS_INSTANT_TEXT=1 and a warp from
    map 422 after the items arrive, so the scene meets received members): linga_r (Rena's game),
    mars_r (Rena's game: Claude / Celine out at 126, held, released at step 3400), mars_c
    (Claude's game: Rena out at 124, Celine's 124 join refused, released), front_r (Rena's game:
    444 takes Claude / Celine / Bowman out, released at 26/3100), ship_c (Claude's game: 548 takes
    Rena / Celine out, Noel stays, Shin's fight, Leon's 230 join refused, released at 31/100).
    front_c (Claude's game: 444 takes Rena out) ends in a walk-around with the scene still running
    (depth > 0), so the release isn't reached there (not a free play).
  - Last code change: on_party_add returns at once for a non-permitted member already active (a
    temporary member joined again). Final: ss_game_tests 156/0, ss_tests 99/0, smoke 120/0
    (scratchpad/party/smoke3.log).
  - Docs: seed_format.md "The party rules (what the port does ...)" (incl. contract
    corrections), engine/README.md Archipelago bullet, docs/catalog/formats.md "The story's party
    splits" (flags 490-496, 586, 2318, 2326, the other hero's G[0xc5] leaves, partings 234 0x74c8
    and 417 0x7db8, the battle-subset routine).

## In progress

- Nothing; final report.

## Open items / for the coordinator

- Contract correction (written in seed_format.md): only Shingo Forest (Rena's game) and the
  tournament need a non-received member; Linga / Front Line / ship / Nede Fields keep the hero,
  so their story-only joins are refused. In Rena's game Rena goes to the Front Line (439) and
  lands on El Beach (229) alone when Dias / Leon weren't received (no encounters or scripted
  battles there). An excluded member can still appear for the two fights.
- The full tournament restore (469 0x6870 at chapter 19 step 21500 / 10400) was not reached
  headless (a warp doesn't start it); it restores by the split's flags (static), and the window's
  end (chapter 20) re-adds anyone still out (demo t468_c3).
- tools/ap_e2e_so2.sh not run: the client path (ap_net) is untouched.
- Party members a split doesn't list stay active during it (e.g. a received Leon at Linga or in
  the tournament's 1-on-1 fights): enhancement-neutral, not "completed" by the port.

## Next steps

- None for this assignment. If the coordinator wants: complete splits (set aside the extras),
  or let Dias / Leon in for a while on El Beach in Rena's game.

## How to verify

- `cmake --build engine/build-party -j4`; `engine/build-party/ss_game_tests` (the ap_v2_party_*
  and ap_v2_starting_character tests); `SS_DISC1=<cue> engine/build-party/ss_tests`;
  `SS_DISC1=<cue> tools/smoke_field_events.sh engine/build-party/shattered-stars` (ap_v2_*).
- A demo: `. scratchpad/party/env.sh; python3 scratchpad/party/run.py NAME SEED FRAMES MAP
  KEY=VALUE...` (memcap'd, headless, logs in scratchpad/party/runs/), e.g. Rena's game map 12:
  `python3 scratchpad/party/run.py m12 rena_open.apso2 2500 12 SS_SWEEP_FLAGS='!1'
  SS_SWEEP_PARTY_DROP=0 SS_SWEEP_PARTY=1 SS_SWEEP_CONFIG=45=1 SS_SCRIPT_GLOBALS='a0=4,a4=150'`,
  then `grep '\[ap\] join' scratchpad/party/runs/m12.log`.
- Research tools: scratchpad/party/scan_party.py (sites), site_conds.py (conditions), via.py
  MAP OFFSET (how a site is reached), ctx.py MAP FROM TO (filtered disassembly), mapdis.py.
