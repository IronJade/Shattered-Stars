# Handoff: v2 port hooks (goals, door conditions, trumpet, craftsanity, killer moves, auto-advance)

Agent "hooks", wave 2 of docs/handoff/apv2.md. Started 2026-10-25.

## The assignment

The port side of the seed contract "v2 fields" (docs/ap/seed_format.md):
1. Seed version 2 (read 1 and 2). The "party" agent parses `party_rules` / `starting_character`
   in the same seed.cpp / seed.h: small separate insertions, re-read before each edit.
2. Goals: `goal.all` (wins in the sidecar's `goal_won`, merged on load; goal sent once all won;
   outcome 0 only), `goal.flags` (New Game and after every load), door conditions `flags` /
   `won` + `banners` (locked like keyed doors, also in story mode), the Silver Trumpet (item 48)
   out of shop sell lists while flag 53 is clear in seeds whose goal.all holds 561.
3. Craftsanity: `craft` check in CreationPage::produce() (listed specialties, common_kind(made_),
   success or failure); `no_fail_crafting` in decide() after compute().
4. Killer-move checks: use count (record + 0x8a + 2*move) >= `uses`, after battles and on load.
5. Auto-advance (dialogue agent's setting) on battle/result.cpp wait_button and the post-battle
   equipment report's confirm wait.
6. Tests (test_ap.cpp), headless demos, smoke, tools/ap_e2e_so2.sh (v2 seed if practical), docs.

## Rules

- Build dir engine/build-hooks (-j4). Tests: `SS_DISC1=<cue> engine/build-hooks/ss_tests`,
  `engine/build-hooks/ss_game_tests`, `tools/smoke_field_events.sh engine/build-hooks/shattered-stars`.
- Other agents edit at the same time: party (join/party code, seed party fields), leader (field
  sprite/actor, message name tokens), apworld agent (apworld/). Only my hooks, my seed fields,
  engine/tests/, my doc sections. Re-read shared files before editing; keep the tree compiling;
  don't fix others' code.
- Headless only, every game process under tools/memcap.py (SS_MEM_CAP_GB 4), at most 2 at once;
  never a GUI, never PCSX-Redux's GUI, never a config-saving flag.
- Enhance, never degrade; AP behaviour behind seed options (door conditions apply in AP story
  seeds, never in non-AP games).
- Log here at least every 20 minutes. Don't commit. Scratch in scratchpad/hooks/. Cap output.
- On a permission refusal stop and report the exact command. Roll over at ~400k context.

## Done and verified

- **Code (07:20-07:40), builds; unit tests pass** (ss_game_tests 142/0 with 7 new ap_v2_* tests,
  ss_tests 99/0):
  - seed.h / seed.cpp: version 1 and 2 ("unsupported version N (this build reads 1 and 2)");
    `goal.all`, `goal.flags` (0-2943), `options.no_fail_crafting`; locations `craft` {item}
    (Location::craft_item) and `killer_move` {character, move 1-10, level, uses 1-999}; door
    `flags` / `won` / `banners` (a door with conditions must name its routine).
  - ap.h / ap.cpp: SaveState::goal_won (sidecar `goal_won`, merged on load like checked, then
    goal.all re-tested); on_battle_exit records goal.all's victories (and any door `won`
    encounter) and sends the goal once all are won; goal.flags pending at start_with / New Game /
    every load, applied at the next field frame (free_play_update's top); missing_condition()
    in door_locked / door_refused (first missing condition's banner, plain "The way is closed
    for now" without one; log `locked, needs flag 556 ("...")`); door_blocked_at_trigger no
    longer requires open world (keys still only lock there: missing_keys). New:
    goal_won(), sell_locked(), on_craft(), no_fail_crafting(), scan_killer_moves() (pending after
    each battle exit and load, run at the next free play); status_text's goal.all line.
  - field/menu_shop.cpp add_inventory: skips ap::sell_locked items.
  - field/menu_creation.cpp: include ap.h; `no_result_` port field (Customize result == input,
    Alchemy div 0, Survival scale 0); decide(): no-fail after compute(); produce(): craft check
    for the 12 item-making specialties (common_kind(made_)).
  - battle/result.cpp wait_button and field/post_battle.cpp's report: auto-advance
    (text_options().auto_advance_frames) as a confirm press.

- **Headless demos (07:30-08:00; scratchpad/hooks/runs, helper run.py under tools/memcap.py):**
  - Gate, story-mode v2 seed `scratchpad/hooks/gate.apso2`, `--map 671` chapter 60
    (`gate_full`): goal flags set at the first field frame; walking into region 5 (routine
    0x3bec) refused with "Talk to Filia in Central City first"; console `flag 556 1`: refused
    with "Defeat the Iseria Queen first"; console `battle 561` + `--battle-end 60:0`: `goal:
    encounter 561 won (goal_won 1, goal.all 2)`; walking in again loads map 684.
  - goal_won across save / load (`gate_save` -> `gate_load` / `gate_nosc`): the sidecar holds
    `"goal_won": [561]`; loading it: status `won 561`, goal flags set again, the door opens (map
    684); the same card without its sidecar: fresh state, flags still set, the door refuses
    ("Defeat the Iseria Queen first").
  - Silver Trumpet (`shop_locked` / `shop_open`, map 18 -> Hearn's shop 26, console `item 48`):
    locked: log line and Sell has nothing to list; with `flag 53 1`: Sell lists "Silver Trumpet
    30000000" (captures cap_shop_*/frame01890.png).
  - Creation pages (`craft_all.sh`, the menu agent's plans p_*.txt on card spec2, all-items craft
    seed, no-fail off / on): craft checks for alch, art, comp, cust, metal, auth, mach, cook, s1,
    s5, s7, s8 (none for ident, surv, repro); no-fail turns every failure into a success (alch
    428 fail -> 421, metal 92 -> 93, auth, s5, s8, comp / mach nothing -> 264 / 441). **Art**
    picks its result only on success (good_item_ 0 on a failure), so decide()'s rule missed it:
    Creation1::compute forces the roll under the option (now art nf: item 9, success).
    Regression: `craft_ref.sh` (no seed and the seed with no-fail off) vs the Redux refs: alch,
    cust identical; art differs in 4 bytes, the same with build-dialogue's binary (before my
    edits): pre-existing, not from these hooks (the menu agent's artp of 10-22 matched).
  - Killer moves (`km_battle3`, spec2 + modsave, seed killer.apso2): on load, Claude move 3 (190
    >= 100) and Ashton move 4 (154 >= 150) checked at the first free play, Opera (not in the
    party) not; after `battle 2`, Rena's slot count 9 -> 10 in the fight and her location (uses
    10) is checked at the next free play. (The player's L1 presses don't reach the battle in a
    field-started fight here; the AI's use did the job.)
  - Auto-advance: result screen of encounter 2 (`res_off` / `res_on`, no input): battle frames
    1664 -> 1410; equipment report (Arlia's Leather Armor, `wiz_off` / `wiz_on`, no closing
    press): state 15 to the end vs back to state 0.
  - Banner widths: "Speak with Filia in Central City first" is 248 px (> 236); reported; the
    coordinator chose "Talk to Filia in Central City first" (225 px); tests/demos use it.

- **Real 0.2.0 seed loads:** scratchpad/apworld2/bannercheck (open world, Iseria goal,
  craftsanity): `912 locations, 519 item types, 371 patch lines`.
- **Smoke:** 13 new cases (ap_v2_gate, ap_v2_nogate, ap_v2_trumpet_locked / _open,
  auto_wizard); first full run 104 PASS / 1 FAIL (my ap_v2_nogate regex expected the load
  before vblank 200; it comes at 203; fixed).
- **Docs:** seed_format.md (version row; "The version rule" note; new subsection "The port:
  goals, doors, the trumpet, crafting, killer moves (implemented 2026-10-25)"), engine/README.md
  (auto-advance sentence; a so2.apworld 0.2.0 bullet in Archipelago).

- **e2e (tools/ap_e2e_so2.sh extended):** a third player Dias (SO2, goal
  indalecio_limiter_off_and_iseria_queen, craftsanity, killer_move_checks, no_fail_crafting,
  story mode) and a session 3: `battle 540` (goal_won 1 of 2, status without goal), `battle 561`
  (goal), server "Dias (Team #1) has completed their goal", tracker client_status 30. The first
  run found a contract error: 0.2.0's moves.py has Claude's Mirror Slice as move 11; the port
  refused the seed ("a killer move needs its move (1-10)"). Reported; the coordinator fixed the
  table row to 1-15 (the id's 4 bits); the port now accepts 1-15 (test: 16 refused, 11 loads).
  Second run: 36 PASS, 1 FAIL (my seed regex vs pretty-printed JSON; now a Python check).
- **Smoke** second run (parallel with the e2e): ap_load / ap_p4_world (the North City save on
  disc 2) failed once; ap_load passes when rerun alone; final sequential run below.
- **Unrelated:** ss_game_tests `ap_v2_party_windows` (the party agent's in-progress test)
  failed once at test_ap.cpp:1882 on the current tree; not hooks code.

- **Final (08:3x):** ss_tests 99/0, ss_game_tests 149/0 (the party agent's tests pass again),
  smoke 120 PASS / 0 FAIL (sequential), ap_e2e_so2.sh 37/37 (exit 0; AP_E2E_WORK
  scratchpad/hooks/e2e, port 38297). The gate banner "Talk to Filia in Central City first"
  fits on screen (scratchpad/hooks/cap_gate/frame00280.png).

## In progress

- Nothing. Assignment done; report sent.

## Files touched (uncommitted)

engine/src/game/ap/seed.h, seed.cpp, ap.h, ap.cpp (shared with the party agent: separate
insertions); engine/src/game/field/menu_shop.cpp (add_inventory), menu_creation.cpp (include,
no_result_, decide, produce, Customize / Creation13 / Creation14 / Creation1 compute);
engine/src/game/battle/result.cpp (wait_button); engine/src/game/field/post_battle.cpp (report
wait); engine/tests/test_ap.cpp (version test -> 3; ap_v2_* tests appended);
tools/smoke_field_events.sh (cases appended); tools/ap_e2e_so2.sh (Dias session);
docs/ap/seed_format.md, engine/README.md, docs/handoff/hooks.md. Scratch: scratchpad/hooks/
(run.py runner, dis.py, recdump.py, craft_all.sh, craft_ref.sh, seeds, runs/).

## Next steps

- Open items for the coordinator: the Art plan's 4-byte difference
  from its Redux reference (pre-existing, not hooks); the player's L1 presses did not use a
  killer move in a console-started field battle (not investigated; the AI's use verified the
  after-battle scan).

## How to verify

    . scratchpad/hooks/env.sh
    cmake --build engine/build-hooks -j4
    engine/build-hooks/ss_tests; engine/build-hooks/ss_game_tests     # SS_TEST=ap_v2 for mine
    tools/smoke_field_events.sh engine/build-hooks/shattered-stars    # ap_v2_*, auto_wizard
    AP_E2E_WORK=$PWD/scratchpad/hooks/e2e AP_E2E_PORT=38297 tools/ap_e2e_so2.sh $PWD/engine/build-hooks/shattered-stars
    scratchpad/hooks/craft_all.sh alch art cook      # creation plans, no-fail off / on
    python3 scratchpad/hooks/run.py NAME [ENV=V ...] -- GAME ARGS   # one capped headless run
