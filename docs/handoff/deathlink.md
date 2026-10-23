# Handoff: DeathLink and the losable / timed boss fights (deathlink agent)

Started 2026-10-23 ~08:50. Resume from this file.

## Assignment

Archipelago only; the original game's play is untouched (bookkeeping only).

1. Verify, from the real story scenes (event sweep `--map N` with `SS_SCRIPT_GLOBALS`, a seed,
   `[ap]` log lines), what the four fights do: Dias at the tournament (204, map 477), Shin at
   the Lacour Front Line (516, map 447), Shin on the ship to El (517, map 548), Marsilio's
   first Fienal fight (530, map 657).
2. Port: in `ap::on_battle_exit`, a boss location whose battle was lose-allowed records its
   check on the scripted defeat as well as on a victory (not on a flee); the lose-allowed flag
   is read as `battle_frame` reads it and kept for the battle's exit. The goal still needs a
   victory.
3. APWorld logic unchanged (the losable rows stay filler-only; `tools/ap_late_proof.py`).
   Docs: `docs/ap/apworld.md` (losable row, a DeathLink paragraph), `docs/ap/seed_format.md`.
   Not `apworld/so2/docs/` (the release agent's).
4. Tests in `engine/tests/test_ap.cpp`; suites in `engine/build-deathlink`.

### The user's DeathLink rules for these fights (confirmed, requirements)

- Shin at the Lacour Front Line (516) is the endurance test and stays DeathLinked both ways.
  A wipe is a real game over and sends a DeathLink. A received DeathLink makes the party fall
  there.
- Shin on the ship (517) and Dias at the tournament (204) must not DeathLink in either
  direction. A loss there sends nothing. A DeathLink received during the fight must not make
  the party fall; it waits until free play, as battle_frame does now.
- Marsilio's first Fienal fight (530) is the same kind of scripted loss: treated like 517 and
  204.

### Rules (mandatory)

- Fixes never change the play state (hit timing, hits, damage, difficulty, pacing); if
  anything would, stop and write it down.
- Archipelago servers on 127.0.0.1 only; never archipelago.gg.
- No game code or assets in git. Headless only (`--headless`, `SDL_VIDEO_DRIVER=offscreen`,
  `SDL_AUDIO_DRIVER=dummy`).
- Never touch `~/.config/shattered-stars` (scratch `XDG_CONFIG_HOME` under
  `scratchpad/deathlink/`, `--no-memcard`) or `~/.config/pcsx-redux`; never the user's .gme.
- Kill processes only by exact PID; every wait loop has a deadline and waits on a PID, a
  done-marker or `pgrep -x`. Long runs in the background. At most 6 game processes.
- Scratch under `scratchpad/deathlink/` (/tmp is RAM). Cap command output.
- Don't commit or push; when done append ONE line `deathlink: ...` to
  `scratchpad/checkpoint_notes.txt`.
- Keep this file current after each step and at least every 30 minutes; past ~400k tokens
  update it and end the turn.

## Findings so far (static, from the scripts and the port's code)

- Outcome codes (battle.h, `g_battle_outcome` -> config +0x178): 0 victory, 1 escaped,
  2 wiped out, 3 ran off (the escape race), 4 time up. The field ends the game only on
  outcome 2 with field_flags +0x7E != 0 (map.cpp); every other outcome goes to the script's R.
- Script ext 0x72(rule, b) sets config +0x181 / +0x182 before each scripted fight:
  rule 1 the whole party AI-driven, 2 the escape race, 3 a 60 s time limit (0xE11 ticks, then
  outcome 4); b = 1 makes the enemy copy the party's best stats (enemy_copy_party_stats).
  Every map scanned (`scratchpad/deathlink/scan72.py`):
  - 516 (map 447 0x74cc): rule 3, b 1, param 1 (lose -> game over). The survival "win" is
    outcome **4**, not 0.
  - 520 Berle (map 575 0x7950, story boss, chapter 33): rule 3, b 1, param 1: the same timed
    survival (Prima: "you're not meant to win it ... survive for a minute").
  - 517 (548 0x84b4), 530 (657 0x86ec), 204 (477 0x6dcc / 0x80ac): rule 0, b 1, param 0.
  - 204: after the fight the script tests R: a win (0) gives Dias's "you're still too soft"
    line and starts the fight again (loop at 0x6db4); any other outcome goes on with the story.
- Every ext 0x72 call in every map (`scan72b.py`, constant or not): rule 3 only at 447 0x74bc
  (516) and 575 0x7940 (520); rule 2 (escape race, outcome 3) on maps 607-615; rule 1 on map
  12 and on the tournament's map 477 (0x809c is the 204 site of Rena's game, 0x80ac: the party
  AI-driven). So a time-up (4) is always one of the two survivals' win.
- So, before any change: 516 and 520 (story bosses, which the APWorld lets hold progression:
  rules.may_hold_progression) never record their checks, because on_battle_exit only takes
  outcome 0. That is a worse gap than the losable one.

## Plan / status

- [x] Build dir `engine/build-deathlink` (RelWithDebInfo, ninja, SS_BUILD_AP_NET=ON). Binaries
      for runs are copied to `scratchpad/deathlink/` (`ss_before3` = HEAD's ap.cpp + the debug
      filter below; `ss_after` = the change), so rebuilds never hit "text file busy".
- [x] Private multiworld `scratchpad/deathlink/mw` (Claude: SO2, death_link true, seed 777;
      Bob: ChecksFinder). `serve.sh start|stop` (127.0.0.1:38297, PID in server.pid);
      `bob.py SECONDS ws://localhost:38297` logs DeathLink bounces; `run.sh NAME BIN FRAMES MAP
      VAR=VAL ...` = a paced headless event-sweep play run connected to it (`SS_AP_SERVER=` for
      offline), log `runs/NAME.log`, marker `runs/NAME.done`.
  - Careful: every connected instance gets every DeathLink (same slot, other connections), so
    a run that sends one must run alone (or the others offline).
- [x] The change in ap.cpp / ap.h (below); unit tests ap_boss_checks_scripted_loss and
      ap_deathlink_lose_allowed_battle pass (ss_game_tests 110/0 without the disc).
- [x] Debug aid (battle_debug.cpp/.h, debug only): `SS_BATTLE_DEBUG_ENC=E,...` limits
      SS_BATTLE_GOD / SS_BATTLE_HP1 to those encounters; `SS_BATTLE_GOD=HP` (above 1) also holds
      max HP at HP (a level-1 party otherwise dies to one hit even with the refill).
- [x] Before / after scene runs (table below), docs, suites. **Done 2026-10-23 ~09:45.**
      (The run binaries were deleted afterwards; a "before" binary is HEAD's ap.cpp swapped
      into the build dir for one `ninja shattered-stars`, then put back.)

## The change (port, Archipelago only)

`ap::on_battle_exit` checks a boss location on outcome 0 (victory), 4 (time up: the timed
survivals 516 / 520, only reachable with a member standing) and 2 (wipe) when the battle was
lose-allowed; never on 1 / 3 (escapes). The goal and the final boss's ending need 0.
`battle_frame` latches the lose-allowed flag (`read_battle_lose_allowed`: handoff 9 +0x180
+0x7E == 0) every frame while the state block is there and uses the latch for its DeathLink
wait; `on_battle_exit` reads and clears the latch (also cleared at `on_program(kBattle)`).
A lose-allowed exit clears `dying_from_link` (no game over follows; can't be set there
anyway). New log lines: "a battle at vblank N", "encounter N ended: outcome X, the script lets
the party lose / a loss is a game over" (boss and goal encounters only).
`ap::note_battle_lose_allowed(bool)` (ap.h) sets the latch for tests.

## Verification table

Runs in `scratchpad/deathlink/runs/` (b* = `ss_before3`, HEAD's ap.cpp; a* = `ss_after`).
Starts: `run.sh NAME BIN FRAMES MAP SS_SCRIPT_GLOBALS=... SS_SWEEP_PLAY=9999:T0 ...`:
- 517: map 548, `a0=29,a4=30`, `SS_SWEEP_PARTY=1` (battle at vblank 409);
- 204: map 477, `a0=19,a4=7000` (Claude's game; Claude alone; battle at 692);
- 516: map 447, `a0=27,a4=1000`, `SS_SWEEP_PARTY=1` (battle at 517); survival with
  `SS_BATTLE_GOD=9999 SS_BATTLE_DEBUG_ENC=516`;
- 530: map 657, `a0=50,a4=0`, `SS_EVENT_SWEEP_DISC=2`, `SS_SWEEP_PARTY=1,4,6`, and
  `SS_BATTLE_GOD=9999 SS_BATTLE_HP1=1 SS_BATTLE_DEBUG_ENC=529`: the scene first fights 529
  twice (param 1; won at vblanks ~8500 / ~12500 with the aids), then 530 at vblank 12560;
- 520: map 575, `a0=33,a4=0`, `SS_SWEEP_PARTY=1`, GOD limited to 520.

| Fight | lose-allowed | outcome on a wipe | game over | DeathLink sent | boss check before -> after |
|---|---|---|---|---|---|
| 204 Dias, tournament | yes (+0x7E = 0) | 2 (fight frame 428) | no: maps 474, 469 follow | no | never (wipe) -> on the wipe (a204) |
| 517 Shin, ship | yes | 2 (fight frame 1324) | no: the scene goes on, map 229 | no | never -> on the wipe (a517) |
| 530 Marsilio, Fienal | yes | 2 (fight frame 440) | no: maps 686, 247 follow | no | never -> on the wipe (a530) |
| 516 Shin, Front Line | no (+0x7E = 1) | 2 (fight frame 944) | yes | yes: "[ap] game over: DeathLink sent", Bob received it (b516w, a516w) | survival = outcome **4** at fight frame 3602, story to map 439: never (b516s) -> on the time-up (a516s: "encounter 516 ended: outcome 4", check, "given Blueberry"); a wipe checks nothing |

DeathLink received during the fight (console `ap_deathlink Alice`):
- 517 (a517dl, at vblank 900, in the fight): the party did not fall (same RNG count at the
  exit as without it, 3327); scripted loss, check recorded; the DeathLink fell the party 30
  frames after the field got the controls back (vblank 2624, the world agent's rule) -> game
  over, nothing sent.
- 204 (a204dl, at 1100 / 1500 / 1900): no fall in the fight (RNG 2600 as without), check on
  the scripted loss; fell in the field at vblank 2011.
- 516 (a516dl, connected, at 1400): "DeathLink from Alice: the party falls in battle",
  outcome 2 at fight frame 622, game over, no DeathLink sent back (Bob got none), no check.
- 530 (a530dl, at 13300 / 13700, the fight at 12560): no fall (RNG 8123 as without), check on
  the scripted loss; fell in the field at vblank 13891; a third one (14100) was dropped on the
  title.
- 204 won (a204win, `--battle-end 300:0`): "encounter 204 ended: outcome 0", check; the script
  starts the fight again (a battle at vblank 3291), that one lost (2), the story goes on (maps
  474, 469).
- 520 Berle (a520s, GOD): "encounter 520 ended: outcome 4, a loss is a game over", check
  recorded, story to map 576.

Before / after play identical: the RNG draw counts at each battle exit match between the
before and after binaries (517 3327, 204 2600, 516 wipe 3363, 516 survival 5302, 529 / 529 /
530 4886 / 6775 / 8123).

## Suites (engine/build-deathlink, 2026-10-23 ~09:40)

ss_tests 78/0; ss_game_tests 110/0 (the two new cases included); smoke_field_events.sh 86 PASS
/ 0 FAIL; `ref_regress.py --quick --build engine/build-deathlink` 28/28 pass; ap_e2e_so2.sh
25/25 (its own server on 127.0.0.1:38292). Runner: `scratchpad/deathlink/suite.sh` (logs and
`summary.txt` in `scratchpad/deathlink/suite/`).

## Files touched

- `engine/src/game/ap/ap.cpp`, `engine/src/game/ap/ap.h`: the change.
- `engine/tests/test_ap.cpp`: ap_boss_checks_scripted_loss, ap_deathlink_lose_allowed_battle.
- `engine/src/game/battle/battle_debug.cpp`, `.h`: debug only, SS_BATTLE_DEBUG_ENC and
  SS_BATTLE_GOD=HP.
- `docs/ap/apworld.md` (Bosses bullet, losable row, "DeathLink and the fights you can't win"),
  `docs/ap/seed_format.md` ("Bosses and the goal" bullets, two DeathLink safe-point rows).
- Not touched: the APWorld (`apworld/so2`, its logic and `docs/`), `tools/ap_late_proof.py`.

## For the release agent (player docs)

DeathLink: sent only on a game over, never for a death a DeathLink caused. Received in a fight,
the party falls, except in the three scripted losses (Dias at the tournament, Shin on the ship
to El, Marsilio's first Fienal fight), where it waits until the field. Shin at the Lacour Front
Line is a real endurance fight: a wipe is a game over (and a DeathLink), a received DeathLink
fells the party. Boss checks: the scripted losses count when lost (or won); the two timed
fights (Shin at the Front Line, Berle in Eluria Tower) count when the minute runs out.

## How to verify again

1. Unit: `engine/build-deathlink/ss_game_tests` (the two cases).
2. Scenes: `scratchpad/deathlink/serve.sh start`, `bob.py 900 ws://localhost:38297 &`, then
   the run.sh lines above with a fresh binary copy; grep the logs for `\[ap\]` and
   `\[battle\] exit`. A run expected to send a DeathLink runs alone. `serve.sh stop` after.
