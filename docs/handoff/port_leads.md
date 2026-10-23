# Handoff: small port leads (leads agent, from 2026-10-23 09:20)

Assignment (coordinator): close a list of small leads, in this order. Sources:
`docs/handoff/confidence_battle.md` (port leads 2-10), `docs/handoff/confidence_field.md` (port
lead 1), `docs/handoff/savepage.md` (the Callways blocker), `docs/notes/original_bugs.md`.

1. Battle leads 2 and 9 (crash-avoidance, like S4): original_bugs.md rows, status avoided,
   `// ORIGINAL BUG` comments at the port's arrays, no behaviour change.
2. Battle lead 7: C++ renames screen_shake -> screen_shake_vertical, screen_shake2 ->
   screen_shake, camera_reset_mode -> camera_reset (pure renames).
3. Visual fixes, battle leads 3 (ribbon trail far edge), 4 (flare fan roll), 10 (model_render
   section 0 depth test): original_bugs.md rows (fixed, visual only), RNG / hits identical with
   and without, one before / after frame each in scratchpad/leads/.
4. Field lead 1: field_install_side_text turns voice streaming back on; the courier path
   (field_world.cpp ~712) doesn't. Make the port match; record RNG use before / after.
5. Catalog rows still `likely`: menu_0bb9 0x8008b338, field_sub3109 0x801f9fe0, menu_0bb2
   0x800810a8; added by the coordinator (09:40): boot 0x8001abc0 sound_add_ticks, boot 0x80024a98
   CdRemove, menu_0bc8 0x8007eb14 options_page_on_child_done (boot notes:
   docs/handoff/confidence_boot.md; ~20 min each at most, else note why and move on).
6. The Callways stop (time box ~45 min): MusicDialog orientation (RIGHT vs DOWN), then the
   save page stopping a Callways song like the original.

## Done

- **Lead 1 (battle leads 2, 9): avoided, B16 / B17.** original_bugs.md B16 (shot_hit_magic
  0x800547d4 and shot_k16_projectile 0x8005a4d8: 4-slot buffers, frames 0x98 / 0xd0 checked in
  the REA asm) and B17 (rolling_hits: the stage 0x24 cart 0x8008c47c, the stage 0xd boulder
  0x8008c2ac and, checked here too, the stage 0x1a boulder 0x8008c2d0; all three 4-entry
  lists). Comments: `engine/src/game/battle/shot.cpp` (shot_hit_magic, shot_k16_projectile),
  `engine/src/game/battle/stage_objects.cpp` (rolling_hits). No code change.
- **Lead 2 (battle lead 7): renamed.** 53 files under engine/src/game/battle (sed with word
  boundaries, in that order). camera_reset(Camera*) now overloads render.h's camera_reset()
  (0x80076be8), both catalog names. Catalog summaries that named screen_shake2 /
  camera_reset_mode follow (`scratchpad/leads/u_leads1.py`, 11 rows, applied under the lock).

  Verified: ss_tests 78/0, smoke pass, ref_regress --quick 28/0 (scratchpad/leads/regress_l12),
  --id stage26,stage36,stage36_long,caster35 4/4 (regress_l12_ids). ss_game_tests 109/1:
  game_windowed_run_writes_its_log segfaults in Window::~Window (window.cpp:113 at the time),
  from another agent's in-flight SS_SHOT second-window change in platform/window.cpp /
  main.cpp; the baseline binary (scratchpad/leads/bin/ss_base) passes the same run. Not leads'.

- **Lead 5 (catalog): all six confirmed** (done early, while lead 3's runs were going):
  `scratchpad/leads/u_leads2.py` applied under the lock; `tools/catalog_coverage.py` re-run:
  5452 rows, all confirmed (likely 0, guess 0). menu_0bb9 model_blend (decomp vs
  CreationViewer::model_blend + the Redux byte match in docs/notes/menu.md), field_sub3109
  gauge_set_pos (3 instructions; no reference but its symbol; the field's A 0x35 stores
  +4/+8 itself), menu_0bb2 grid_box_place (only via vtable 0x8008133c, which only the
  destructor builds), menu_0bc8 options_page_on_child_done (slot [10] of vtable 0x80080764;
  summary made exact; the port only logs it), boot sound_add_ticks (read_bytes: the 2nd word
  is 0x8c86338c = lw a2, 0x338c(a0), the "jal" was a misread byte; sound.cpp comment "the
  console traps" corrected: its div has no zero check), boot CdRemove (li t2,0xa0; jr t2;
  li t1,0x72 = BIOS A(72h), the same stub shape as GPU_cw / DeliverEvent beside it).

- **Lead 3 (battle leads 3, 4, 10): fixed, visual only, B18 / B19 / B20.** killer.cpp
  RibbonTrail::update (`drawn` flag: the first quad drawn works out its own near edge),
  fx_draw.cpp draw_flare_polygon (first vertex at 0x400 + roll), stage.cpp ModelRender::f3
  ((z >> 3) + bias). Play-neutral: ref_regress --all with ss_l12 vs ss_l3, 223 pass both and
  all 339 summary lines identical (scratchpad/leads/summaries/regress_all_l12.md /
  regress_all_l3.md); probe-forced runs (scratchpad/leads/probe/, a private
  source copy with SS_PROBE_* switches) give equal RNG / enemy traces with and without each
  fix. Natural cases: trail segment-0 culls in encounter 420 (i420_3 / n420_3) and x11_3,
  but their far edges are ~1000 px below the screen, so the GPU drops the quad either way;
  the flare roll is 0 at every reference / stage-sweep draw; section 0 is used by no stage
  model, only by the eF_3 / i12_3 spell (113 triangles past the old cut, hidden behind
  nearer polygons). So the frames in scratchpad/leads/frames/ are forced:
  B18_trail_op_m4_f424.png (segment 0 skipped), B19_flare_stage17_f190(_crop).png (roll
  341), B20_sec0_eF3_f966.png (section 0 drawn alone). Suites (snapshot snap_l35): ss_tests
  78/0, ss_game_tests 119/0, smoke 86/0, ref_regress --quick 28/0.

- **Lead 4 (field lead 1): fixed toward the original.** `engine/src/game/field/message_text.cpp`
  field_install_side_text ends with `load_queue_set_streaming(load_queue(), true)` as the
  original's 0x80053b44 does (FUN_8004358c(queue, 1)); post_battle.cpp report_draw's own call
  after it dropped (now inside); comments in field_world.cpp / message.h. Redux check
  (`scratchpad/leads/redux/courier.lua`, disc 2, dex_north_city card with a due delivery poked
  at field_load_map; log out/log.txt): install_side_text at rel 314, set_streaming(1) from
  0x80053c20 at rel 315, voice_queue_build_field at rel 372, the delivery's end at ~rel 462.
  Port traces (`scratchpad/leads/courier/run.sh`, before.log / after.log, every RNG draw's
  site via SS_RNG_SITES): before, the voice banks load only at vblank 282 (after the
  delivery, when field_prepare_encounter turns streaming on); after, at 100-120 right after
  the install, the original's order. RNG: same draws through the delivery; then +12 draws by
  vblank 330 (field_program.cpp:583, the per-frame encounter-wait draw, resumes once the
  clips are in, 8 vblanks sooner) and the field clip picks land elsewhere in the sequence
  (banks 287 / 410 instead of 290 / 412); 4854 vs 4842 draws over 3000 vblanks. Suites
  (snap_l4): ss_tests 78/0, ss_game_tests 119/0, smoke 86/0, quick 28/0.

- **Lead 6 (the Callways stop): verified matching, no code change.** The original's
  MusicDialog is vertical (Compose over Play music) and its grid call is the port's exactly
  (music_dialog_build 0x80080dd4: cursor_add_grid(.., 1, 1, 2, 0, 0x28, 0, 0xe, ..); slot
  [11] is the shared Dialog input 0x800e05f0), so no orientation or input bug; RIGHT is not
  needed. A Redux rerun on the early save moves DOWN to Play and wraps back (out3/); the
  earlier block was a harness artifact. The late save confirms with CIRCLE, cancels with CROSS.
  Late save, Ashton > Musical Talent > Play > Song of Salvation, CROSS x 3, Data > Save:
  Redux (`scratchpad/leads/cw/redux/cwref.lua`, out2/log.txt): song 1 from +850; the save
  page's constructor calls callways_stop_song_resume at +1461 (ra 0x8007e168) ->
  callways_end_song(c, 1); song 0 after. Port (probe build with [pcw] traces,
  cw/port/m4/log.txt): play_song at 562, field_save_page_open at 1000 stops song 1 with
  resume. The "secret talent Sense of Rhythm" message after the song also appears in the port
  when it starts from the original's menu RNG (SS_MENU_RNG=scratchpad/leads/cw/late_rng_open.bin,
  cw/port/m5). Details appended to docs/handoff/savepage.md.

## State: all six leads closed (10:25)

Final suites on the current tree (snapshot of engine/build-leads): ss_tests 78/0,
ss_game_tests 119/0, smoke 86/0, ref_regress --quick 28/0 (scratchpad/leads/suites_final.log).

Files changed (engine): battle/shot.cpp, stage_objects.cpp (comments, B16 / B17); 53 battle
files for the renames (list: scratchpad/leads/rename_files.txt; combat.h / camera.h
declarations); battle/killer.cpp (B18), fx_draw.cpp (B19), stage.cpp (B20);
field/message_text.cpp, post_battle.cpp, field_world.cpp (comment), message.h (comment) for
lead 4; boot/sound.cpp (comment). Docs: docs/notes/original_bugs.md (B16-B20),
docs/catalog/functions.csv (u_leads1.py 11 summaries, u_leads2.py 6 rows), COVERAGE.md,
docs/handoff/{savepage,confidence_battle,confidence_field,confidence_boot}.md (closed marks).

## How to verify

- Build: `cmake --build engine/build-leads -j8` (configured from build-conf-battle's _deps via
  FETCHCONTENT_SOURCE_DIR_*; RelWithDebInfo, Ninja).
- `SS_DISC1=<disc 1 .cue> engine/build-leads/ss_tests`, `ss_game_tests`,
  `tools/smoke_field_events.sh`, `tools/ref_regress.py --quick --build engine/build-leads`.
- `scratchpad/leads/suites2.sh TAG` snapshots the build's binaries and runs all four suites
  (result in scratchpad/leads/suites_TAG.log).
- Binaries kept: `scratchpad/leads/bin/ss_l12` (before lead 3) / `ss_l3` (after),
  `ss_l4before` / `ss_l4after`. Probe sources (lead 3 / 6 traces and forcing switches):
  `scratchpad/leads/probe/engine` (a 09:40 copy of engine/ plus probes; its build dir was
  removed: configure it like build-leads to reuse it). Lead 3 frames:
  `scratchpad/leads/frames/`. Lead 4: `scratchpad/leads/courier/run.sh BIN TAG` and
  `scratchpad/leads/redux/courier.lua`. Lead 6: `scratchpad/leads/cw/port_run.sh`,
  `scratchpad/leads/cw/redux/cwref.lua`.

## Rules (mandatory, from the coordinator)

- Context: past ~400k tokens bring this file up to date and end the turn at the next break
  point; past 500k stop as soon as it is written.
- Fixes: original bugs commented `// ORIGINAL BUG (original_bugs.md Id): ...` with the fix
  below, listed in original_bugs.md. A fix never changes the play state (hit timing, hits,
  damage, difficulty, pacing); visual-only is fine; crash avoidance that otherwise plays the
  same is "avoided" (S4). Port divergences are fixed toward the original (Redux where not
  obvious).
- No game code or assets in git. Headless only; never audible audio. Never touch
  ~/.config/shattered-stars (scratch XDG_CONFIG_HOME under scratchpad/leads/, named scratch
  cards or --no-memcard; copy cards first; never the user's .gme) or ~/.config/pcsx-redux
  (Redux only -cli + scratch HOME, max 3).
- Kill by exact PID; wait loops with deadlines on a PID / done-marker / `pgrep -x`; long jobs
  in the background. Scratch under scratchpad/leads/ (/tmp is RAM). Build engine/build-leads
  -j8; max 6 game processes.
- Other agents: mod-manager (setup_ui.cpp, launcher/*, field/mods.*, tools/mod_*, mods/,
  .github/), release (setup_ui.cpp header, main.cpp --version, build scripts, apworld/so2),
  DeathLink (game/ap/*, tests/test_ap.cpp, docs/ap/*), screenshot (small renderer option).
  Re-read a file right before editing; stay out of their areas.
- Before calling anything done: ss_tests, ss_game_tests (SS_DISC1), smoke_field_events.sh,
  ref_regress --quick --build engine/build-leads.
- Catalog writes only under the lock: `flock scratchpad/catalog.lock python3
  scratchpad/fx_worker/cat3/apply3.py <update file>`. REA server shared: re-open the binary
  before each batch and check the subject.
- Cap output. Don't commit or push. One line per finished lead in
  scratchpad/checkpoint_notes.txt, prefixed "leads: ". Keep this file current after each lead.
