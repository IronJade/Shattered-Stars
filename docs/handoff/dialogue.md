# Handoff: dialogue settings (instant text, auto-advance)

Started 2026-10-25 by the "dialogue" agent (wave 1 of docs/handoff/apv2.md).

## The user's request (verbatim)

> ...we need a method to auto progress the dialogue (to any choice text) and an option for
> instant dialogue box completion, similar to severed chains. The start of the story is
> nearly an hour of exposition as it sits.

## Rules given

- Two port settings, both off by default, every game mode (normal, story and open-world AP).
  With both off the game plays exactly as the original; with them on only the waiting
  changes, never the script's outcome.
- Edit only the message / menu / settings code, engine/tests/, and the settings docs. Other
  agents: "ships" (game/ap/, field/script_ext.cpp), "killer_prof" (battle/), "apworld2"
  (apworld/, docs/ap/). Keep the tree compiling; don't fix others' files.
- Build dir engine/build-dialogue (-j4). Headless runs only, under tools/memcap.py
  (SS_MEM_CAP_GB 4), at most 2 game processes. Never a GUI, never a flag or code path that
  saves ~/.config/shattered-stars/config.ini. Scratch in scratchpad/dialogue/.
- Don't commit. Log here at least every 20 minutes. On a permission refusal stop and report
  the exact command. Roll over at ~400k context.

## Done and verified

- **Engine (field/message.*):** `TextOptions {instant, auto_advance_frames}` (message.h,
  `text_options()`), MsgWindow gains a port field `button_wait` (vblanks in the current
  confirm wait, counted always). Instant text: state 4 prints the page with
  `kMsgInstantChars` until it stops; state 5 scrolls at once; after a wait ends (6/7/8) the
  next page prints in the same frame. Pauses (0x4003 n>=5), timed waits (0x4006), the
  open/close animations stay. Auto-advance: state 6/7 with button_advance, after
  auto_advance_frames of waiting, unless `msg_vm_choice_or_menu_wait()` (message_draw.cpp:
  the VM in wait state 9 or 0xe); same code path as the confirm press, only that window.
  `SS_MSG_TRACE=1` logs every confirm wait's end (button / auto / forced) with its length.
- **Settings:** `launcher::TextSettings` (settings.h/.cpp: read_text / write_text /
  auto_advance_frames; keys instant_text, auto_advance, auto_advance_delay in ms 100-10000,
  default 1000; left out at defaults); config.h key list; main.cpp applies them (headless
  runs ignore config.ini; `SS_INSTANT_TEXT=0|1`, `SS_AUTO_ADVANCE=0|1|MS` override, also on
  windowed runs). Setup: new **Gameplay** page (after Controls; page id `gameplay`) with
  Instant text, Auto-advance and the wait (0.5/1/1.5/2/3 s). tools/setup_screenshots.sh:
  `gameplay`, `gameplay-on` shots.
- **Unit tests (ss_game_tests, map 7's real text):** field_message_instant_text_same_pages_sooner
  (same waits and layouts as the typewriter, 85 vs 284 frames, no frame ends scrolling),
  field_message_auto_advance_is_a_press (frame-for-frame identical to a press at the same
  wait frame, with and without instant), field_message_auto_advance_waits_at_choices (wait
  states 9 / 0xe block it for 600 frames; otherwise 30 frames), field_message_settings_default_off,
  launcher_text_settings_defaults_and_round_trip. The Ronixis/original comparison still passes.

- **Sweep driver (field/event_sweep.cpp, debugging only, defaults unchanged):**
  `SS_SWEEP_PRESS=all|choices|wait:N|none` (choices answered 30 frames after they come up and
  logged "choice on window W ..."; wait:N presses confirm so it lands in the wait's N-th frame,
  the frame auto-advance at N acts in; none = no field input), `SS_SWEEP_PLAY_DUMP=PATH` (the
  SS_STATE_DUMP blocks when a play reaches free play), `SS_SWEEP_PLAY_STOP=1` (end the run
  there), and a "play: story chapter C step S; rng ..." line at the end of every play.
- **Opening measurements (2026-10-25)**, `scratchpad/dialogue/run_opening.py LABEL KEY=VALUE..`
  (logs and dumps in scratchpad/dialogue/runs/), Claude's New Game (`--map 7`, chapter 3) along
  the story route `SS_SWEEP_PLAY=8:K20,4:T1,9999:T0`: first free play on map 8, then map 4
  (after the Calnus), then map 12 at chapter 4 step 100 (after the landing, the forest, Rena
  and Arlia: maps 5, 6, 14, 12). Vblanks (60 a second):

  | run | input | map 8 (first free play) | map 4 | map 12 |
  |---|---|---|---|---|
  | settings off | masher (confirm ~3 frames into every wait, also ends timed waits) | 5764 (1:36) | 9932 | 19584 (5:26) |
  | instant | masher | 4604 (1:17) | 8128 | 16438 (4:34) |
  | settings off | reader: confirm 1 s after each page is complete | 7019 (1:57) | 12194 | 24075 (6:41) |
  | auto-advance 1 s | **none** (78 boxes auto-advanced, 0 presses) | 7019 | 12194 | 24075 |
  | instant | reader 1 s | 5787 (1:36) | 10287 | 20763 (5:46) |
  | instant + auto 1 s | none | 5787 | 10287 | 20763 |
  | instant + auto 0.5 s | none | 5157 (1:26) | 9087 | 18393 (5:07) |

  Instant text saves 17-20 % to the first free play and 14-16 % to map 12 (55 s at a 1 s
  reading pace); with auto-advance the whole stretch plays with no input. The rest (~4 min of
  the 5:46) is scripted movement, camera, fades and the windows' open/close animations.
- **Same story state:** auto-advance runs are byte-identical to the reader pressing confirm at
  the same wait frame (same vblanks, same rng signature, identical dumps), with and without
  instant text. Across all seven runs chapter/step (4/100), script flags, party, records,
  inventory and config are identical; the only differing bytes are map-local values the
  scripts draw from the random generator (map 12's B_0a(n) NPC wander targets G[0x3a8..0x3bc],
  G[0x444..0x450]) and stale VM call-stack words (block +0x5c/+0x6c); the same kind of bytes
  differ between two settings-off runs that only press at different moments (masher vs reader).
- **Choices:** Ashton's join scene (`MAP=71 SS_SCRIPT_GLOBALS=a0=17,1bc=100 SS_SWEEP_FLAGS=113
  SS_SWEEP_PLAY=71:T2,9999:T0`, instant + auto 1 s): with `SS_SWEEP_PRESS=none` 45 boxes
  auto-advance, the choice comes up at vblank 9681 and is still up (VM wait state 9) when the
  run ends at 12000; with `choices` it is answered 30 frames in and the scene reaches free
  play. The state-6 case (a prompt that waits for the button while the choice is up) is the
  unit test with map 7's shop prompt.
- **Suites (build-dialogue):** ss_tests 99/0, ss_game_tests 135/0, smoke 85 pass / 1 fail
  (`ap_p4_tele_open: the teleporter's edits apply`: expects "3 script ... patch(es) from the
  seed", the open-world seed now gives 15 because of the ships agent's uncommitted open-world
  patch lines in game/ap/ap.cpp; not this work), ref_regress --quick 28/28.
- **Docs:** engine/README.md (setup pages, a Dialogue paragraph), README.md (feature list),
  config.h key list, main.cpp header; the wiki clone scratchpad/wiki (Installing-and-Setup:
  Gameplay section, eight pages; Playing: settings table, "what's different" row), edited
  locally, not committed or pushed. Setup page checked offscreen (scratchpad/dialogue/shots/).

## Open items / next steps

- **Battle result screen** (battle/result.cpp `wait_button`, owned by killer_prof): its pages
  wait for confirm or 0x168 frames. Not changed (not my file). The hook, if wanted:
  `|| (field::text_options().auto_advance_frames > 0 && static_cast<s16>(rs->timer) >=
  field::text_options().auto_advance_frames)` in the confirm test (same path as a press:
  sfx 2, confirmed). Battle talk (party_talk) never waits for confirm: nothing to do.
- Field post-battle equipment report (post_battle.cpp, only when auto-equip changed gear)
  waits for confirm in its own code, not a window wait: not covered.
- Timed waits (0x4006, and 0x4002 with the auto-text flag) are left alone: they already move
  on by themselves; a press could end them early, auto-advance doesn't.
- No in-game toggle (the port has no in-game port menu); settings are in setup.
- The smoke expectation above belongs to the ships agent.

## Files touched (uncommitted)

engine/src/game/field/message.h, message.cpp, message_draw.cpp, message_internal.h,
event_sweep.cpp; engine/src/launcher/settings.h, settings.cpp, config.h; engine/src/main.cpp;
engine/src/setup/setup_ui.h, setup_ui.cpp, setup_main.cpp (comment); engine/tests/
test_field_message.cpp, test_launcher.cpp; tools/setup_screenshots.sh; engine/README.md,
README.md; docs/handoff/dialogue.md; scratchpad/wiki/{Installing-and-Setup,Playing}.md.

## How to verify

    . scratchpad/dialogue/env.sh   # SS_DISC1
    cmake --build engine/build-dialogue -j4
    SS_TEST=field_message,launcher_text engine/build-dialogue/ss_game_tests
    engine/build-dialogue/ss_tests; engine/build-dialogue/ss_game_tests
    tools/smoke_field_events.sh engine/build-dialogue/shattered-stars
    cd scratchpad/dialogue && python3 run_opening.py r_inst_auto60 SS_SWEEP_PLAY=8:K20,4:T1,9999:T0 \
        SS_SWEEP_PRESS=choices SS_AUTO_ADVANCE=1000 SS_INSTANT_TEXT=1
    python3 state_diff.py runs/A.state runs/B.state

## Findings

- Message window states (message.h): 2 opening (10 frames), 4 printing, 3 delay, 5 scroll
  (2 px/frame), 6/7 waiting for confirm (0x4002 / 0x4001 / page full), 8 timed wait
  (0x4006, or 0x4002 with the auto-text flag: 300 frames), 9 closing (10 frames), 1 idle.
- Text control codes over every disc 1 map (scratchpad/dialogue/text_stats.py): 0x4002 80758,
  0x4003 109289 (speed 0: 53962, speed 1: 54807; pauses n>=5: 6 x253, 15 x248, 60-160 x17),
  0x4006 timed waits 453 (mostly 120 frames), 0x4009 choice blocks 5.
- Choices: one VM context (g_script_vm); ext 0xdc-0xe0 put it in wait state 9 on a window;
  the choice reads confirm while the window is in state -1/1/6/7, so a real confirm press
  there answers the choice. Field menus (shop, rename, camp) run their own loops (no task
  updates); script menu requests wait in state 0xE.
- Battle: the only confirm wait is the result screen's wait_button (battle/result.cpp,
  confirm or 360 frames).
</content>
</invoke>
