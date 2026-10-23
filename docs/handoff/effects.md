# Effects / embedded map code: continuation log

Worker: field effects agent. Phase 5 (current): GPU lines, then battle party modules
Precis #2155, Ashton #2147, Opera #2153, Chisato #2159. Phase 4 (credits, GPU) committed
as bc1b341 and 155be10.
Build dir: `engine/build-fx`. Scratch (on disk): `scratchpad/fx_worker/`. Nothing here is
committed by the worker; the coordinator commits.

Phases 1-3 (field effects, RotMatrix sites, #3103 movie player, #3104-#3106 / #3108 /
#3109 / #3105 embedded map code) are committed (d61a763, docs 9e43364). Their notes
are in `docs/notes/field_effects.md`.

## Phase 4 step 1: #3107, the credits (done, reported)

- Analysis: `docs/notes/credits.md` (new, with the Redux results). Catalog: module
  `credits_3107` (65 function rows, 26 global rows, README module list); boot
  `pad_add_vblank_handler` / `pad_remove_vblank_handler` and title
  `g_movie_frames_in_ram` updated. Listings: `analysis/decomp/credits3107/listing.s`,
  `listing2.s`.
- Port:
  - `engine/src/game/credits/credits_program.{h,cpp}` (new): the whole program.
  - `Program::Credits = 3107` and its `run_program` case (`boot/system.{h,cpp}`).
  - `boot/pad.{h,cpp}`: the vblank handler list (`VblankHandler`); `vsync_callback_with`
    runs the handlers first; `pad_init` clears the list.
  - `boot/loader.{h,cpp}`: `Loader::head()` / `set_head()` (the drive head, set by
    `start_read`).
  - `title/movie.{h,cpp}`: the frames-in-RAM paths; `movie_console_now`,
    `movie_console_idle_until`, `movie_next_event`, `MovieGlobals::caller_idles`; the
    seek time from the drive head (Redux's 3 us a sector + 31 ms) instead of one fixed
    latency. Map 548's FMV now starts 2-3 vblanks from Redux's (was about 10).
  - `tests/test_movie.cpp`: the logo movie test sets the head where the title leaves
    it and expects the first flip at 38-44 vblanks (Redux 41; was 20-26).
  - `field/field_section6.cpp` (new): `field_run_section6_program` (A 0x2d), declared in
    `field_sub_overlays.h`, called from `vm_services.cpp`.
  - `engine/CMakeLists.txt`: the two new files.
- Test: `SS_DISC2=<disc 2 cue> SS_SCRIPT_GLOBALS="79c=0x24000" --map 581`
  (`SS_CREDITS_CUES=1`, SS_TRACE for the events).
- Redux: `refcred3` (captures), `refcred4` (counters: compose, DrawSync, DecDCTin,
  movie_close, flip; entry loads; the hand-back). Results in `docs/notes/credits.md`:
  opens within a vblank, first frames within a vblank, ends within 1-3, credits end
  21583 vs 21584, name cards pixel-identical, movie frames within 2-3 LSB, hand-back to
  map 581 / the final screen identical.
- Tests: `ss_tests` 76/0 with and without the disc; `ss_game_tests` 74/0 with the disc;
  smoke 16/16. The last tidy edit (an unused counter in credits_program.cpp) has not
  been linked: `menu_specialty.cpp` (another agent, in progress) fails to link
  (`MusicDialog::on_confirm` undefined), so `ss_game_tests` without the disc is still to
  rerun.

## Phase 4 step 1 was committed as bc1b341.

## Phase 4 step 2: GPU accuracy (done, reported)

Decided against the hardware VRAM dumps of JaCzekanski's ps1-tests (gpu/triangle,
uv-interpolation, quad, transparency, lines), replayed on `psx::Gpu` with
`scratchpad/fx_worker/ps1tests/harness.cpp` (`cmp.py` compares). Findings in
`docs/notes/gpu_accuracy.md` (new).

- a. End-screen logo 1 px left: not rasterisation. Its image sits 10 bytes into its
  buffer and the GPU DMA ignores the address's low two bits, so on the console (and
  Redux: its VRAM dump shows the texture one texel right) it lands one pixel right.
  `psyq::dma_source` (runtime/libgpu) aligns LoadImage's source; `upload_texture` /
  `upload_clut256` (game/common/texture.cpp) use it. Probed: no other upload in the
  title, maps 7/18/139/548/1, battles 1/50/200/500 or the tests is misaligned. End
  screen now identical to Redux (caption, logo, "fin.").
- b. Interpolation: neither side was right. The hardware matches Mednafen's /
  DuckStation's planes (12-bit gradients from the leftmost vertex, half-unit round):
  `draw_triangle` now uses them and reproduces gpu/triangle (plain, big, dithered) and
  gpu/uv-interpolation exactly (before: 3244 / 18733 / 4185 / 15860 pixels off; Redux's
  walker: 310 / 1848 / 162 / 276). Quads are 0-1-2 + 2-1-3 and dropped whole when any of
  their five edges is too long.
  Blending: the hardware dithers and saturates to 5 bits and then blends (gpu/lines'
  transparent dithered polyline); the port already did, Redux blends 8-bit before the
  dither and with an unsaturated modulated colour. That is the remaining ±1 in
  test_field_message's window bodies (1287301 body pixels; with Redux's order they would
  all match its captures) and the New Game portraits' 1-2 LSB. The port is right there.
- c. Mirror foot shadow (map 139): from the interpolation; now 3 / 0 pixels one step off
  at the two positions (was 15-28). (The map-name banner in the bottom-right is typed
  further in the port at map+150: a load-time difference, not the GPU.)
- Tests: `ss_tests` 77/0 (new `gpu_gouraud_and_uv_match_hardware_dumps`, probes from
  the dumps) and `ss_game_tests` 74/0, with and without the disc; smoke 16/16.
  `test_field_message`'s comments and `docs/CHECKLIST.md` (the two GPU items, plus the DMA
  line) updated; `docs/notes/field_effects.md` table updated.

## Phase 5 step 1: GPU lines (done, verified, reported)

Code committed in e37c3a5 (draw_segment, `dither_lines`, `--reference-dither` covering
lines, `gpu_lines_match_hardware_dump`, CHECKLIST line). Verified after the cutoff:
- The ps1-tests harness (`harness lines`) against the whole 1024x512 gpu/lines dump:
  identical, with the test's undefined last polyline colour fitted (0x6400d2; with 0 only
  that segment's 2 x 32 pixels differ). triangle / uv-interpolation / quad: identical over
  the whole dump; transparency identical inside its 320x240 screen.
- Before: 50 px wrong in steep lines, 1 LSB in 129 / 111 gouraud and 528 dithered mono
  pixels, 48 in the polylines.
- `docs/notes/gpu_accuracy.md`: a Lines section and a table row (uncommitted).
- Suites (coordinator, HEAD): ss_tests 78/0, ss_game_tests 74/0 with and without the
  disc, smoke 21/21; mine the same (smoke 16/16 in tools/smoke_field_events.sh here).

## Phase 5 step 2: party modules (done: Chisato, Precis, Opera, Ashton verified and reported)

Precis #2155, Ashton #2147, Opera #2153, Chisato #2159, each in battle/pc_<name>.cpp,
registered in pc_modules.cpp (flag the hunk to the coordinator).

### Chisato (#2159): verified, reported (18:3x)

- `engine/src/game/battle/pc_chisato.cpp` (new): ChisatoUnit (vtable 0x801f0904), 8 moves,
  6 Shot callbacks, the normal attack (slot 30), slots 4 / 6 / 33 / 35 / 36 / 39.
- Small hunks in the battle agent's files (to flag): `pc_modules.cpp` (case 12),
  `unit.h` (`Unit::jump_time`, vtable slot 6, virtual), `unit_move.cpp` (its body:
  unit_jump_time), `party_states.cpp` (the leap calls `jump_time`), CMakeLists.
- Verified against Redux, all 8 moves at charge level 0 (`chis_b1..b8`): RNG draws equal at
  every sample through the battle and the result screen (to the field's 1024 draws after
  the exit); frames under ~1% through the fights (one b780 frame of b7: 10% one-LSB pixels,
  0.3% blocks, a flat brightness step).
- Not the module: in every run (b1-b8) the result screen's last fade (about 180 frames
  after state 4) differs up to 8-10% for ~7 samples; D 5 halves it, so the port's fade runs
  about one 30 Hz frame early. Battle framework (result.cpp), to tell the coordinator.
- Max level (`chis_m1..m8`, USES 999): RNG equal to the exit in all 8. m2 / m6 frames
  differ in the afterimages: the battle agent's B2 fix; a scratch build with the faithful
  order (`scratchpad/fx_worker/faithful/`, killer.cpp line 60 reverted) brings them to
  <= 1.6%. m4 (move 3 level 2) is equal to b479, where the console runs a 3-vblank frame
  (BW_DENSE: f 471 three times, then ticks 3; again at b488): the original's slowdown, not
  modelled; the scene then runs a tick or two apart (RNG totals still meet).
- Tools: `scratchpad/fx_worker/battle/redux/run_pc.sh NAME CHAR L1BYTE [R1] [USES]` (Redux,
  scratch HOME) and `port_pc.sh NAME CHAR L1BYTE [USES] [FRAMES]` (env `OUTNAME`, `CAPS`)
  (port on the same field_exit dump); compare with `scratchpad/battle/rngcmp.py REFLOG
  PORTLOG` and `FRAMECMP_D=7 scratchpad/battle/framecmp.py REFDIR PORTDIR/cap 160 1100 10`;
  `pair.py REFDIR PORTDIR/cap OUT.png N..` (ref | port | diff x4).
- Disassembly: `scratchpad/fx_worker/battle/char{06,07,09,12}.elf`; decompiles through the
  MCP REA session (batch_decompile, open_binary with the CLI snapshot
  `battle/<name>/snapshot` right before each call: other agents switch the shared session);
  listings with `dump_asm.py` (one CLI process).

### Precis (#2155): verified (level 0 and max level), reported 23:xx

- `engine/src/game/battle/pc_precis.cpp` (new): PrecisUnit (vtable 0x801f1754, +0x360 the
  arm), 10 moves (vtable slots 47-56 through the table 0x801f191c), 8 Shot callbacks, slots
  31 (state 10: a projectile; 11: the base jump attack), 33, 35, 36, 37. MP table 0x8006b28b.
- `engine/src/game/battle/pc_module_util.{h,cpp}` (new, mine): the per-Shot afterimage
  table (moved out of pc_chisato.cpp), `pc_camera_frame_target_for` (0x8003aad8, not ported
  before), `gte_origin_at`, `WeaponElementBits`. (draw_disc is the world agent's, spell_fx.)
- Hunk in `pc_modules.cpp` (battle agent's, to flag): case 6 (and case 9 Opera).
- Level 0, all 10 moves (`prec_b1..b10`): RNG equal at every sample to the exit. Frames:
  b2 (move 1) differs in the afterimages (B2; the faithful scratch build matches); b4
  (move 3) had the camera's mode-3 return missing: move 3 calls pc_camera_frame_target
  (0x8003aa30), not _for (fixed; Redux breakpoints `prec_b4_cam` showed it); now only the
  vortex's last faint fade differs (b518-530, up to 4%: faint additive gouraud, see the GPU
  check below); b10 (move 9) the sphere is up to 2 steps redder (b960-980; same question).
- Max level: not run yet (see the resume point). The b4 / b10 frame differences are gone
  with the new reference switches (prec_b4 / b10 rerun: no frame above 1%).

### Opera (#2153): verified (level 0 and max level), reported 23:xx

- `engine/src/game/battle/pc_opera.cpp` (new): OperaUnit (vtable 0x801f1e10), 10 moves
  (pointer table 0x801f1f88), slots 31 / 35 / 36 / 37 / 45, 14 Shot callbacks including
  #1984 block 1's shot_spiral_ring / shot_falling_rock / shot_vortex_area and
  shot_draw_ring_bands (not ported before; only her moves use them). Move 0 starts
  spellcast kind 2 (sets `s3c` and `orb_set` to her AnimSet).
- Level 0 (`op_b1..b10`): RNG equal to the exit in moves 1-8. Moves 0 and 9 are equal
  until the console's first 3-vblank frame (ticks 3: b392 under move 0's orb rain, b441
  as move 9's vortex starts; BW_DENSE in `op_b1_dense` / `op_b10_dense`): the original's
  slowdown, not modelled. Frames: the blend-order differences are gone with the new
  reference switches; move 7 (b8) differs a little in the shards' ribbon trails (b470-530,
  <= 2%; passed on as a B1 question).
- F13 fixed (the last shard gets the upward velocity; with it op_b8's RNG leaves the
  original at b480, as expected). F14 decide.
- Catalog: battle_pc2153 rows (33), port columns for the four block-1 functions, MP table
  0x8006b2f3.

### Ashton (#2147): verified (level 0, max level, weapon variant 1), reported 23:xx

- `engine/src/game/battle/pc_ashton.cpp` (new): AshtonUnit (vtable 0x801f1e4c, 0x388 bytes:
  +0x360 move 4's mark, +0x370 move 7's sparks, +0x384 move 9's glow), 10 moves (pointer
  table 0x801f1fd0), slots 4 / 6 / 33 / 35 / 36 / 37, 9 Shot callbacks. Module data read
  with pc_module_u8(7, ...): 0x801f1fc4, 0x801f1fc8 / 0x801f1fcc, 0x801f1e2c.
- Hunks in the battle agent's files (reported, flagged): `unit.h` (Unit::effect_trail),
  `killer.cpp` (release_effects stops it), `pc_modules.cpp` (case 7).
- Move 4's +0x21e flag is kept in a member (`shaken`): on the console it overwrites the
  low half of PartyUnit +0x21e (item_value_sum in the port).
- Level 0: `as_b1..b5` (moves 0-4): RNG equal to the exit, no frame above 1% in the
  battle (reported). `as_b7..b9` (moves 6-8): RNG equal to the exit; move 7's frames
  differ only in its 2-copy afterimage (B2; the faithful build matches). `as_b6` (move 5):
  see the current state below. `as_b10` (move 9, spell 0x40) queued.
- New bugs: F15 (dart facing from his x velocity: decide), F16 (sound at a null dart:
  avoided), F17 (hit sprites by the first point's height: decide).
- Catalog: battle_pc2147 rows (31), MP table 0x8006b2bf, module tables.

### GPU check (done, reported, switches added and committed hourly by the coordinator)

- `engine/src/psx/gpu.{h,cpp}`: `Gpu::blend_before_dither`, `Gpu::blend_unsaturated_modulation`,
  `Gpu::capture_shift` (all false by default); `engine/src/main.cpp`: `--reference-dither`
  sets all three (hunk flagged). gpu_accuracy.md "What it explains" has the findings: the
  result fade / spheres / vortex fades are Redux's blend order; the "2.5% brighter" is the
  capture expansion; Dias's olive sword-wave copies are the B2 afterimage fix (the user is
  deciding B2 again).
- Scratch: `scratchpad/fx_worker/faithful` (B1 / B2 reverted, `sync.sh [debug]`), the
  Redux-blend env test in it is obsolete now.

### Original bugs: F1, F3, F5, F7, F8, F14, F15, F17 fixed; F4, F12 (held), F13 kept; F11, F16 avoided

- F1 `field/effects_draw.cpp` (the line and burst depth: `otz_for_depth`), F3 / F7
  `field/effects.cpp` (glow replaced; clamp), F5 `field/field_movie.cpp` (per-row clipped
  write-back), F8 a-e `field/actor_field.cpp` (`fieldactor_build_light_split`).
- Tests adjusted: `test_field_actors.cpp` (light-split Redux trace: cache-B UVs skipped for
  F8b, 95 dup46 calls accepted as subsets for F8d; the list-4 triangle test expects the
  corrected UVs), `test_field_effects.cpp` (the SS_FX_REF dump tests find lines / burst quads
  in any slot, F1). ss_game_tests 74/0 with the disc; with SS_FX_REF all dump tests pass.
- F4 (lit sphere): the user agreed it is intended: "keep" (3232b65). Leave the code.

Suites at the report (build-fx, current tree): ss_tests 78/0, ss_game_tests 74/0 with and
without SS_DISC1, smoke 21/21 (TMPDIR=scratchpad/fx_worker/tmp).

## Phase 6: catalog completion (in progress, started 23:50; rolled over 01:10)

Assignment (coordinator): (1) `tools/catalog_coverage.py` writes `docs/catalog/COVERAGE.md`
(per module: rows by confidence and by port status) — done; (2) work the 426 empty port
columns to zero (port file, "(not needed: reason)", "(PsyQ: ...)", or "(unported: ...)"
listed in COVERAGE.md and ported, verified against Redux where play reaches it); battle
rows are the battle agent's area: send it the list of unported functions in its files
before editing them; (3) raise guess / likely rows to confirmed with evidence.
- Tools: `scratchpad/fx_worker/catalog/empty.json` (each empty row with the source files
  naming its address / name), `ctx.py MODULE..` (the source lines), `apply.py UPDATES.csv`
  (in-place row updates, order kept; columns module,address,port[,confidence[,evidence_add
  [,summary[,force]]]]), updates in `scratchpad/fx_worker/catalog/updates_*.csv`.
- Build: build-fx reconfigured 23:48 (ss_game_tests 95, smoke 29: all pass).
- 00:36: empty port columns 426 -> 0 (`updates_battle_stale/misc/ovl1984/voice/field.csv`
  applied). Scans: `refs.py PROGRAM ADDR..` (jal / j / pointer words) and `luiref.py
  PROGRAM ADDR..` (lui + addiu/ori pairs) over the archive's code entries; ELFs for REA in
  `scratchpad/fx_worker/catalog/` (b2129.elf battle init image, f3100.elf field, m2982 /
  m3006 menu overlays at 0x8007e000, t0001.elf title). New rows go through `append.py`
  (appended, order kept). COVERAGE.md now also lists "[port partial: ...]" rows.
- Unported and reached in play (11 rows, "(unported: ...)"), being ported now, approved by
  the coordinator (one owner: me):
  - battle (new file `party_talk.{h,cpp}` + flagged hunks): opening voices
    (battle_frame_director 0x80072228, TODO director.cpp:282), talk pose (pc_start_talk
    0x8003acac; cleared in pc_update 0x800350cc), provoke line (voice_play_battle_line
    0x800808e8, input.cpp provoke: rng_range(4), -1 -> buzzer), ally reaction + fury
    (voice_ally_reaction 0x80080974, pc_start_fury 0x8003acbc, g_member_tables from
    party_banter_voice_table 0x80074980), flee voice (0x80080878), victory voice
    (voice_play_entry(victory_voice, this, -1) at 0x800360a8), equipment weapon throws
    (pc_on_hit 0x8003bb84: status2 0x4000 -> throw(0), 0x2000 -> throw(2);
    pc_attack_hit_check: status1 0x800000 -> throw(0)).
  - field (new file `field/menu_voice.{h,cpp}` + hooks in common/menu_core): the field
    menus' voice streaming (bgload_init / shutdown / update field parts 0x80038bcc /
    0x80038c70 / 0x80039418) and voice_reload_member 0x80038dfc (g_voice_reload_member is
    never installed: a killer-move / party change in a menu keeps the old voice bank).
  - Not needed after all: clash_register / clash_resolve (dead: the only jal is in #2129
    bytes #1984.1 overwrites); pages 0xc / 0xd and the cheat dialog (only behind the
    camp menu's third dialog, whose constructor 0x8007fe6c has no caller).
- 01:00 status. Battle talk port written and wired (`party_talk.{h,cpp}`; hunks in
  director.cpp, party.cpp update, input.cpp provoke, party_states.cpp x5 (ally reaction,
  victory voice, flee voice, both weapon-throw sites), battle_program.cpp (party_talk_init;
  clash TODOs replaced by the reason), CMakeLists). Field part written (`field/menu_voice.
  {h,cpp}`, `menu::BgLoadHooks` in common/menu_core.{h,cpp}, FieldMenuHost sets them).
  Suites on build-fx: ss_tests 78/0, ss_game_tests 99/0 with and without the disc, smoke
  37/0. Debug switches: SS_TALK_TRACE (each talk call with vblank and rng draws),
  SS_TALK_OFF (no opening lines), SS_TALK_FLEE=F (flee at battle frame F),
  SS_MENU_VOICE_TRACE. Harness: explore.lua BW_HIT_BP now prints rng / a0 / a1 / v0 and has
  BW_COPY; port_pc.sh takes EXTRA_IN, PORT_MASH, PORT_STRONG.
- Verified so far (Redux `talk_ko`, Claude + Rena): RNG equal to the battle exit; opening
  line op1 at the same battle frame with the same handle 0x05080000, talk pose 40 vblanks in
  both; victory voices (entries 0x989 / 0x995) and result lines (0xa11 / 0xa16) at the same
  frames (SS_VOICE_TRACE vs BW_HIT_BP vpe). Running: `talk_jobs2.sh` (prov2: provoke + both
  throws, ko2: Claude falls beside Rena, flee).
- (Retracted: the "prec_m1 regression at b570" I reported was my mistake: prec_m1 is the
  max-level reference and needs USES=999, `./port_pc.sh prec_m1 6 1 999 1500`. The
  coordinator checked: with 999 the current tree, talk layer included, is RNG-equal past
  the exit on prec_m1, as_m1, chis_m1, bowman_l1, dias_l1, ernest_2_3.)

### State at the rollover (01:10): what the next agent picks up

`scratchpad/fx_worker/battle/redux/talk_jobs2.sh` finished at 01:12 (refs
`redux/ref/talk_prov2`, `talk_ko2`, `talk_flee`; no Redux running). Their hits, before the
battle ends (later hits at the same addresses are the result / field code):
- talk_flee: `extra` at battle +398, rng 12194 (the flee voice). Compare the port.
- talk_ko2: `ally` at +2100 (a0 0x801ed920, the slot 1 unit: Rena fell first, Claude's
  bond toward her was not poked) and at +2324 (Claude, slot 0); no `fury` (no qualifying
  standing ally the second time; the first: Claude's bond toward Rena is 0). Useful as a
  no-fury RNG check; for the fury, also poke Claude's bond toward Rena
  (`2:0x5a=0x28/1`, Redux `4047@*0x80075270+0x5a=0x28/1`: 0x4b + 1*0xc + 2) or keep Rena
  out of reach.
- talk_prov2: no `line` hit during the fight, so the Select presses did not provoke (busy,
  free run or the camera?): look at the port's input at those frames, or press Select
  repeatedly (BW_MASH select every 90 from 4110; port PORT_MASH adds select:300:90). The
  `throw` hit at +1078 is after the battle (the result code); no equipment throw happened
  either: check that the pokes (record +0x16 accessory, +0x10 armor) took (item ids are
  1-based: item_data_field reads (item - 1) * 0x30; the ids 0x181 / 0x2cf are the 1-based
  ids of records 0x180 / 0x2ce). Then run the port on each and compare
(`SS_TALK_TRACE` [talk] lines against the refs' `[bw] hit` lines, which carry the rng count
and a0 / a1 / v0; `python3 battle/rngcmp.py REF/log.txt PORT/log.txt` from `scratchpad/`;
hits after the battle's end are other programs' code at the same addresses, ignore them):
- prov2 (Claude, Provoke level 3, accessory 0x181 = throws on hits, armor 0x2cf = throws when
  knocked back; Select at 300 and 750):
  `SS_TALK_TRACE=1 EXTRA_POKE="4:0x89=3/1,4:0x16=0x181/2,4:0x10=0x2cf/2" EXTRA_IN="300:select,750:select" PORT_MASH="cross:500:45,l1:590:90" ./port_pc.sh talk_prov2 1 1 0 2400`
  Expect `line` / `lineret` (handle, or -1 = buzzer and no provoke) and `throw` hits.
- ko2 (Claude at 1 HP, idle; Rena copied from slot 0 unstrengthened; her bond 0x28):
  `SS_TALK_TRACE=1 PORT_STRONG=1:1:1 SS_BATTLE_COPY="3:0:0x60:0x60,4:0:0xd0:0xd0" EXTRA_POKE="3:0x14=0x514/4,3:0x18=0x514/4,3:0x1c=0x514/4,3:0x2a=0x50/2,3:0x60=2/2,3:0x1c=1/4,2:0x64=0x28/1" PORT_MASH="none:0:99999" ./port_pc.sh talk_ko2 1 1 0 2400`
  Expect `ally` then `fury` (Rena: action reset, stats +2 bit 0x10, fury_ticks 1200) if
  Claude falls; if he does not, the run needs stronger enemies or a longer idle.
- flee (escape flag + flee timer 0x3c poked at vsync 4432 = battle frame 350):
  `SS_TALK_TRACE=1 SS_TALK_FLEE=350 ./port_pc.sh talk_flee 1 1 0 2400`. Expect `extra`.
- Already verified: talk_ko (first round) RNG equal to the exit, opening line, talk pose,
  victory voices, result lines (above). talk_prov (round 1, Precis) is not usable: its
  Select presses fell on busy frames, and Precis needs USES=999 for that reference anyway.

Not started: the field menu voice against Redux. Plan: the disc-2 world-map save
`scratchpad/world/card1.mcd` (map 2, field_flags +0x25 = 67; copy it, never the user's
cards), a copy of `scratchpad/menu/redux/itemsref.lua` that opens the camp menu soon after
the load (streaming still in progress) and logs 0x80078864 / 0x80078868 / 0x8007886c (menu
state, budget, timer) and 0x800759a0 / 0x80075998 (field) on change, with breakpoints on
0x80038dfc (voice_reload_member), 0x8007972c (voice_slot_invalidate), 0x80079000 /
0x80079308 (queue builds), 0x80079a7c (abort), 0x80038bcc / 0x80038c70 (init / shutdown);
Redux on the disc-2 cue. Port: `SS_LOAD_SAVE=0 SS_MENU_VOICE_TRACE=1` with SS_DISC1 /
SS_DISC2, scratch XDG and card. For the reload hook, a killer-move change: the menu agent's
`kms` plan (`scratchpad/menu/kmsr/plan.txt`, specrun.sh) on its spec cards.

After both verifications: set the 11 "(unported: ...)" rows to their files
(`party_talk.cpp`, `field/menu_voice.cpp` + `common/menu_core.cpp`) with the evidence; fix
the stale "[port partial: ...]" notes on sound_handle_active 0x8007fc54, voice_pick_victory
0x800804d4, voice_play_result 0x800807c8 (the voice player is ported) and on
battle_program_create 0x800715d0 (stage code blocks and opening voices are now ported);
regenerate COVERAGE.md. Then (coordinator's order) the per-program gap scan (jal targets
and REA procedures against the catalog rows; boot has uncatalogued routines around
0x80014444-0x800144e4, e.g. 0x8001446c empty, 0x80014474 / 0x800144ac / 0x800144d0), then
raising guess (43) / likely (412) rows to confirmed.

Files of mine touched this phase (for the checkpoint): new `engine/src/game/battle/
party_talk.{h,cpp}`, `engine/src/game/field/menu_voice.{h,cpp}`, `tools/catalog_coverage.py`,
`docs/catalog/COVERAGE.md`; flagged hunks in battle director.cpp, party.cpp, input.cpp,
party_states.cpp, battle_program.cpp, common/menu_core.{h,cpp}, field/menu_camp.cpp,
engine/CMakeLists.txt (party_talk.cpp, menu_voice.cpp); catalog rows via apply.py /
append.py (3 new rows: menu_0ba6 0x8007fe6c menu3_create, boot 0x80014444 clock_set, boot
0x8001445c pad_set_reset_callback).
- Seen, not mine: Weirdaxe debris (enemy_ai.cpp:210, battle agent). The boot executable
  has uncatalogued functions (e.g. 0x8001446c..0x800144e4); a gap scan (jal targets /
  procedures vs rows) is the next catalog step after the ports.

### Phase 6, second agent (from 01:05): talk checks, field menu voice, M7

**Battle talk: verified (01:50); M7 and "Battle, part 2" ticked in CHECKLIST.md.** Port vblank
p = Redux battle frame p + 5 (vsync p + 4080); compare with `rngcmp.py` (D 7).
- flee (talk_jobs2): `extra` at port 393 / Redux +398, 85 draws after op1 in both, entry 0x991
  in both; RNG equal to the exit (b730).
- ko2 (talk_jobs2): RNG equal to b2400; both ally calls equal (p1 2095 / +2100, p0 2319 /
  +2324, same draw counts); nobody left standing to react, so no fury there.
- prov3 (`redux/talk_jobs3.sh`; port: prov2's command, `talk_prov3`): provoke line at 301 /
  +306 (44 draws after op1 in both), handle 0x40000249 in both; accessory throws at 631 / +636
  and 889 / +894 (283 / 416 draws in both; Redux's status test at 0x8003777c returns 1 there);
  RNG equal to the exit (b1348).
- ko3 (port `talk_ko3c`, command above): line 301 / +306 (handle 0x40000248), Claude falls at
  1015 / +1020 (712 draws in both), Rena's fury with handle 0x4000026c; Rena's unit in the
  Redux dump at 0x80080b40 (`ram_fury_01020.bin`, unit 0x801ed920): +0x124 0, +0x12a 0, +0x201
  1, +0x228 0x4000026c, +0x22c 1200, status 0x10 = the port's fury_state 00001001 / ticks 0x4b0;
  RNG equal to b2400.
- prov2 (talk_jobs2) was unusable: it mapped port p to Redux p + 3810 (the mash phase only).
- Talk pose length: the port's ends 6 frames after a voice line, Redux's 10-16 (the clip's
  disc read + upload: E15 keeps "playing" = the entry still in the voice slot). With
  `SS_BLOAD_DRIVE_TIME=1` the port matches (ko3 +320 / +320 and +1030 / +1030, prov3 +324 /
  +322). Coordinator's decision: CD timing, not modelled; the switch stays comparison-only
  (docs/HANDOFF.md debug options).
- victory_2_3's "temporary differences at b270-b400" (battle agent's lead 2): rngcmp's D
  fallback (2; that log has no frames lines). With CMP_D=6 or 7 the port is RNG-equal to b1250
  (exit b1236), with and without SS_TALK_OFF.
- Regress leads (coordinator): chis_m4 b490, op_m3 b520, op_b10 b500, op_m10 b570 are the
  console's 3-vblank frames (ref frame counters jump at b480 / b420 / b450 / b450); only the
  opening line in SS_TALK_TRACE. refs.csv fixed (also the off-by-one lenient labels on op_b9 /
  op_m2 / op_m9); 9/9 PASS (`scratchpad/fx_worker/regress9/summary.md`).
- Debug lines: SS_TALK_TRACE `throw` at both weapon-throw sites (party_states.cpp, through
  `talk_trace` in party_talk.h) and `fury_state` / `fury_ticks` after pc_start_fury.

Field menu voice (scratch `scratchpad/fx_worker/mv/`):
- `mvref.lua` + `run_mv.sh NAME`: Redux on disc 2 with a copy of `world/card1.mcd` (Claude 56,
  Rena, Ashton; world map 2), camp menu at MV_OPEN vsyncs after the field starts, MV_PLAN
  relative to menu_scene_create; logs the menu / field voice state, queue head and the sound
  slots on change, and the init / shutdown / reload / invalidate / build / abort / reset /
  upload / copy calls with the rng count; dumps rng_open.bin (for SS_MENU_RNG) and ram_end.bin.
- Port: `SS_LOAD_SAVE=0 SS_TRACE=1 SS_MENU_VOICE_TRACE=1`, world map at vblank 2, the field's
  banks in by vblank 26 (28, 29, 145, 146, 280, then clips 265, 290, 412). Killer-move change
  (Claude's first L1 slot, Dragon Howl -> Air Slash): `p2` run, inputs
  `150:triangle,223:cross,263:cross,393:cross,453:cross,493:down,523:cross,640/700/760:circle`
  (each released 4 later): init at 168 (state 3), reload at 524, battle banks 23 / 28 to slot
  0 at 585-588 (budget 226), field clip 257 at 592, done 594, shutdown 779.
- Running (01:20): `run_mv.sh mvA` (MV_OPEN=1500, the kms-style plan 60/100/230/290 cross,
  330 down, 360 cross, circles 700/760/820, MV_AFTER=300).

### Phase 6, third agent (from 03:13): suites after B15, victory_2_3, field menu voice

- Suites on build-fx at a10f166 + tree (B15 and SS_BATTLE_RNG_SKIP in): ss_tests 78/0,
  ss_game_tests 104/0 (SS_DISC1), smoke 62/62, `ref_regress --quick` 23 pass / 3 regress /
  2 review (the same as the 03:12 checkpoint). Runner: `scratchpad/fx_worker/run_suites.sh TAG`
  (logs in `logs/suites_TAG`; ref_regress `--out` must be absolute: the world_spell handler
  runs with cwd = scratchpad/world/spell).
- The 3 regressions are not the talk layer (identical with SS_TALK_OFF=1):
  - e10_3 / f10_3 (b450): suite setup. Those refs were recorded with Cross mash (Claude walks
    from b330), but `h_enemy` sets NO_MASH=1 for every enemy ref. Without NO_MASH
    (`scratchpad/enemy/run_port.sh 10 3 REF OUT`) both are RNG-equal within a frame to the
    exit (b870). For the tools agent (ref_regress.py / refs.csv).
  - m111_3 (b610): the world agent's documented spell-data CD load time (world.md: Goathead's
    Thundercloud, console b602 / port 600); SS_BLOAD_DRIVE_TIME does not move it (not a bload
    read). refs.csv expects rng_equal_to_exit: wrong expectation.
  - m524_3 / m538_3 (review): the battle agent's party-load RNG offset (SS_BATTLE_RNG_SKIP).
- victory_2_3 rerun on this tree (`vic/log2_on.txt`, `log2_off.txt`; port: SS_BATTLE_TICKS=1
  SS_BATTLE_STRONG=1 SS_BATTLE_RNG=1 SS_BATTLE_RAM=ref/ram_field_exit_004046.bin
  --reference-dither --battle 2:3, cross every 45 from 45): CMP_D=6 / 7 first mismatch b1250
  (exit b1236), talk on and off; D 2 / 5 give +10 at b270, +7 at b300, +10 at b400 and equal
  samples between (sample phase). The talk layer draws nothing in the opening (op1 at rng
  1645 both). refs.csv's victory_2_3 row now names CMP_D=7 and the port command.
- CHECKLIST: M7 and "Battle, part 2" were ticked by the second agent (committed a10f166).
  HANDOFF.md: M7 state bullet, Effects row, test counts, debug options (SS_TALK_OFF,
  SS_TALK_FLEE, SS_MENU_VOICE_TRACE, SS_MENU_RNG added next to SS_BLOAD_DRIVE_TIME /
  SS_TALK_TRACE).
- Field menu voice: Redux refs exist from the second agent (not in its log): `mv/mvA` (camp
  menu at MV_OPEN=1500, kms plan 60/100/230/290 cross, 330 down, 360 cross, circles
  700/760/820), `mvB` (the same plus MV_TIMER and a forced battle after the close: pokes
  0x8009c72c=0x7d, 0x8009c43a=2, 0x8009c25c=0x21, L1 mash; `rng_open.bin`), `mvC` (circles
  at 375/390/405: closes while streaming; never quit, exit 1), `mvD` (MV_OPEN=200: the menu
  did not open). Port run `mv/pB` (log only). Redux mvA/mvB: init m+0 (state 3), reload m+361
  (from 0x800802a0) + abort + invalidate slot 0, timer 60 repeats once at 3941 (a 2-vsync
  frame), build_battle m+423 (budget 466 -> 226, entries 23 / 28 to slot 0 at m+436 / m+441),
  build_field m+443 (clip 262, 1 draw), state 3 m+459, shutdown m+841; battle starts with
  banks 1:2900 1:51c0 1:3a40 1:0. Slot 3's flag 1 vs the port's 0 is F18 (avoided).
- **Field menu voice: verified against Redux (03:50).** REA re-read of bgload_init /
  bgload_shutdown / voice_reload_member / bgload_update (#3100) and their helpers
  (voice_queue_busy 0x80078fd4, voice_stream_set_state 0x80055eb0 (< 4 only), set_budget,
  field_get_option_a): the port's menu_voice.cpp / menu_core.cpp do the same.
  New debug: `sound_debug_dump_voice_slots` (boot/sound.{h,cpp}, flagged hunk: slots 0-3 in
  the console layout), written by `SS_TALK_SOUND_DUMP=path` at the battle start
  (party_talk.cpp) and `SS_MENU_SOUND_DUMP=prefix` at each menu voice step
  (menu_voice.cpp; the trace also prints the rng count); `SS_FIELD_CLIP_RNG=file`
  (field/voice_queue.cpp, flagged hunk) loads a Redux generator dump before the field's first
  clip pick. Redux side: `mv/mvref2.lua` (+ driver dumps `snd_*.bin`, `rng_build_field.bin`),
  `run_mv2.sh` (MV_LUA), `queue_efg.sh` / `queue_h.sh` / `queue_i.sh`; port `mv/port_mv.sh NAME`
  (TRI, M, PLAN, BATTLE, MASH, FRAMES; stdin from /dev/null: --debug waits on it at exit);
  compare `mv/cmp_snd.py REDUX.bin 80095ab4 PORT.bin` (slot words, loaded flags and each
  slot's instrument / index / script / sequence / remap bytes).
  - Killer-move reload + battle (Redux mvE / mvH / mvI, port pE3 / pH3, same plan relative to
    menu_scene_create; port M = 1518 for TRI 1500): init m+0 state 3, reload m+361 (both),
    build_battle m+422 port / m+423 Redux (the console's timer repeats once at 3941: a
    2-vsync frame, not modelled), budget 466 -> 226 in both, banks 23 / 28 to slot 0, field
    clip 262 with one draw (SS_MENU_RNG; without it the port picks 266), done, shutdown writes
    field 3 0 in both. Port loads are instant (23 / 28 / 262 at m+424 / 425 / 429 vs Redux
    m+436 / 441 / 456: CD timing). The close takes 19 vblanks vs 21: it waits for a menu
    disc read in Redux (timer back to 60 just before the shutdown).
  - Bytes (pH3 with SS_FIELD_CLIP_RNG=mvH/rng_build_field.bin, so the field picks 269 / 293 /
    417 like Redux): slots 0-3 identical at every step (menu open, reload, battle banks
    queued, clips queued, done) and at the battle start; the one difference is slot 3's
    loaded flag (F18). Redux's slots stay the same at b+100 and b+880. Without the field RNG
    (pE3) slot 0 is identical from the rebuild on; slots 1 / 2 differ in 5-6 effect-index
    bytes (the field-load clips 290 / 412 vs 293 / 417, boot-path RNG).
  - Next battle (encounter 125 stage 2, L1 every 60 from b+100): Claude's killer-move voice
    (slot 0, id 0) plays 3 times in 900 frames on both sides, from the slot that now holds
    bank 23 (Air Slash) instead of 29 (Dragon Howl); no voice loads in either battle.
  - Menu opened while the field still streams (Redux mvF: Triangle at 2298, mvG: 2328; port
    pF9 / pF19): the press aborts the field's read (load_queue_set_streaming 0x80043628 in
    Redux; the port's same path), bgload_init copies state 1 with the budget (Redux 5250,
    port 5416) or state 2 without it, the menu resumes the queue after its idle (145 / 146 /
    280, build_field -678, clips, done) and the shutdown writes the state and budget back
    (mvF field 3 4572 / pF9 3 4738; mvG / pF19 3 0). The field budgets differ because the
    field's budget comes from the distance to the next encounter (field_flags +0x1C, an RNG
    draw: boot path), not from the menu.
  - Backing out during the idle (Redux mvC, port pC): a menu read at m+393 aborts the queue
    and restarts the timer when it ends: Redux m+400 (7-vsync read), the port at once, so
    the build comes at m+454 vs m+461; the steps after are the same.
- **Field load from the data menu (new, 03:50, verified 04:00):** entry 2998 (field save page, menu_0bb6)
  FUN_8007e6e4 (load done: clock, vibration, sound mode, stick centre, session result, then
  voice_reload_member 0-3, finish) was not catalogued; the port's SaveMenu::on_child_done
  (common/menu_save.cpp, the title copy) lacked the reload, so after a load in the field the
  old party's banks stayed marked loaded. Added for Host::Field (flagged hunk, menu agent's
  file). Verified: Redux mvL (camp menu, 7 x Down, Cross, Down, Cross, Cross slot 1, Cross
  save 0, Cross yes) calls voice_reload_member 0-3 from 0x8007e828-0x8007e840 at the load
  (m+693), the shutdown writes field state 0 / budget 0, the reloaded map streams 28 / 29 /
  145 / 146 / 280 and clips 271 / 285 / 417 (field budget 8783 -> 7505). Port pL / pL2 / pL3
  (same PLAN, both RNG switches): the four reloads at the load (m+634: the card read is
  instant), shutdown field 0 0, the same banks and clips; slots byte-identical at the reload
  (all four, slot 3 included) and after the map's streaming (field budget 7505 too; slot 3's
  flag F18 again). Catalog: menu_0bb6 0x8007e6e4 save_menu_child_done (new row); notes:
  docs/notes/menu.md "Voice streaming while a menu is open" + the save page line.
- Catalog this session: 7 field globals 0x80078854-0x8007886c (bgload state / capacity /
  current / requests, g_menu_voice_state, g_menu_voice_budget, g_bgload_timer), the menu_0bb6
  row above; COVERAGE.md regenerated (4962 rows, unported 0, partial 11; the 123 empty port
  columns are the catalog agent's new rows). The "(unported: ...)" and "[port partial]"
  rows from phase 6 were already resolved.
- Suites after these changes (build-fx, 04:0x): ss_tests 78/0, ss_game_tests 107/0
  (SS_DISC1), smoke 73/73, ref_regress --quick 25 pass / 2 regress (e10_3, f10_3: NO_MASH
  setup, above) / 1 review (m538_3, the battle agent's party-load offset).
- Files touched this session: `engine/src/game/boot/sound.{h,cpp}` (debug dump, flagged),
  `engine/src/game/field/voice_queue.cpp` (SS_FIELD_CLIP_RNG, flagged),
  `engine/src/game/common/menu_save.cpp` (field load reload, flagged: menu agent's file),
  `engine/src/game/battle/party_talk.cpp`, `engine/src/game/field/menu_voice.cpp`,
  `docs/HANDOFF.md`, `docs/notes/menu.md`, `docs/catalog/{functions,globals}.csv`,
  `docs/catalog/COVERAGE.md`, `tools/refs/refs.csv` (victory_2_3 row), this log.
- Next (coordinator's order from phase 6): the per-program gap scan (jal targets and REA
  procedures against the catalog rows; boot has uncatalogued routines around
  0x8001446c-0x800144e4), then guess / likely rows to confirmed. No runs active.

## Phase 7: catalog completion, empty port columns (fourth agent, from 04:05)

Assignment (coordinator): the COVERAGE.md "Reached in play" items, the developer screens, the boot
island, the remaining empty port columns, then guess / likely -> confirmed. Scratch and tools:
`scratchpad/fx_worker/cat2/` (README at the top of each script):
- `img.py` program images (boot exe, archive code entries from `code.pickle`, a 78 MB extract of
  disc1.pickle), `normalize` (instruction words with jal targets, lui immediates and lui/gp-based
  offsets masked), `code_end` (control-flow end of a function).
- `match.py` / `classify.py MODULE ROOT`: twins of a module's rows in other programs (same
  normalized words; `=` when the jal targets also pair up), reach from ROOT, refs.
- `reach.py MODULE ROOT[,..] [ADDR..]`: reference reachability (jal / j / lui-built addresses /
  data pointer words owned by the nearest lui-built label); 603 of 741 title functions reached
  from title_entry; spot-checked: every "unreached" ported title row has no live reference.
- `who.py MODULE ADDR..` (refs with the containing row), `xref.py [--prog P] ADDR..` (boot exe +
  all code entries), `jalpairs.py A B` (jal targets of two twins side by side).
- `apply2.py UPDATES.py` (row updates incl. renames, NEW rows inserted in order, contiguity check);
  update sets `upd_title.py`, `upd_boot_menu.py`; backup `functions.before.csv`.
- Redux: `scratchpad/fx_worker/join/` (`run_join.sh NAME`: joinref.lua redirects the CPU at a field
  function entry to call party_slot_init 0x8007a20c for characters 1-12 and dumps the generator
  before / slot and record after each call; `joincmp DIR` runs field::party_slot_init on the
  same generator states; build line in joincmp.cpp's directory: c++ against build-fx's libs).

Done (04:40), all 123 empty port columns resolved (0 left):
- Join (task 1): title 0x80062330-0x80062e34 are the title's copies of field init_celine ..
  init_chisato (instruction-identical, same talent tables); party_slot_init 0x80061f80 reaches
  them only for ids 3-12 and every title call passes 1, 2 or 0. Renamed init_<name>, confirmed,
  port engine/src/game/field/characters.cpp. Redux check (j2): all 12 characters' slot / record
  bytes and the generator after each call identical (field rows got the evidence too).
- menu_stats (task 1): port cells fixed to StatCalc::attr_total / snapshot_attrs /
  snapshot_equip / can_equip / equip_slot (they named a nonexistent MemberStats); title
  0x80034714 is char_equip_mask.
- Debug screens (task 2): title debug_menu_window_create 0x80031aa8 and
  debug_info_window_create 0x80039068 have no reference anywhere in the title image (so their
  vtable functions never run): not needed, unreachable in retail. None reachable by a player.
  The catalog agent's other "debug_screen_create_*" rows were misnamed: 0x80048dcc-0x80048ee4 are
  the New Game windows' destructors (ported: title/window.h implicit destructors), 0x80048f0c /
  0x80035ba0 / 0x80042b88 destructors of vtables no constructor stores, 0x8003a174 / 0x8003a1e0
  / 0x8003a218 the title's Window-over-Task base (window_task_*; only the debug window uses it).
- Boot island (task 3): the six "boot_text_glyph_slot_*" are sound_set_tracks wrappers like
  sound_set_pan (renamed sound_set_track_volume3 / volume4 / transpose2 / coarse / fine2 /
  humanize; no reference in the boot exe or any code entry: not needed); invert_status_flag ->
  interrupts_disabled (dead); boot_resource_lookup / stream_unpack_step / lz_unpack_bytes /
  lz_unpack_words are the time-sliced SLZ unpacker (slz_stream_open / step / lzss / lzss_rle),
  REACHED from battle party_loader_update and spellcast_load_step: port disc/slz.cpp (+ the
  battle's own pacing).
- The other title rows: 30 renamed to their field / battle / menu twins' names (inventory_*,
  party_*, stat_calc_*, RotMatrix, ...), port "(not needed: dead in the title program: ...)" with
  the who.py evidence; reached ones cited (memcard_close_events, bgload_cancel_owner2,
  memcard_start_check_true). 3 new title rows (vtable-only leaf functions the gap scan cannot
  see): 0x8003a208 window_task_type, 0x8003a210 window_task_nop, 0x8003a248 window_task_update_nop.
- menu_0ba5's five: field copies of title panel_fade_step / panel_darken / panel_help_anim /
  panel_help_rebuild / text_draw_glyph (0 words differ), reached, ported in menu_widget.cpp /
  menu_gfx.cpp; renamed *_field.

Task 5 (first pass, 04:50): `raise.py` raised the likely / guess rows whose instruction twin
(>= 8 words, jal targets pairing) is a confirmed row of the same name or the address the row
cites (16 rows: battle's stale stat_mod_* / equip_* copies, title text_cat_measure /
widget_draw_list / _item, field stat_calc_create, credits deferred_delete).
Totals for phase 7 (diff against `cat2/functions.before.csv`): empty port 123 -> 0 (31 cited
to port files, 92 not needed); 85 rows raised to confirmed (57 guess, 28 likely), 20 guess ->
likely; 89 renamed; 3 new rows; 5 port cells corrected (StatCalc); 14 field rows got the Redux
evidence. COVERAGE.md regenerated (4965 rows; likely 481, guess 105; empty 0, unported 0); its
hand-written "Unported, reached in play" section rewritten as the resolution summary.
No engine code changed; no suites needed. No runs active (Redux j2 done; its dumps are small).

Next (whoever continues task 5): the 105 guess / 481 likely rows. Cheapest first: rows whose
twin differs only in jal targets (`classify.py MODULE ROOT --all`, `~` hits: confirm the jal
pairs with `jalpairs.py`), then rows the ported code cites by address (grep the address in
engine/src) read against REA (batch_decompile, ~10 addresses a call: the output carries ~2k
tokens of boilerplate per call, procedure_pseudo_code doubles it). Then the missing-row list
below (add rows in address order with apply2.py NEW).

Found, for the coordinator / catalog agent: the gap scan's `[?]` tag (no `addiu sp` prologue)
hides real leaf functions: boot lacks rows for memcpy 0x80023ed0, memset 0x80023f10,
0x80023e80 (thunk) / 0x80023e9c (strncmp-like, called by slz_unpack), the PRNT_OBJ internals,
toupper / tolower / memchr / setjmp / longjmp / _exit / FlushCache / GPU_cw / CdRemove /
DeliverEvent / OpenEvent / CloseEvent / WaitEvent / TestEvent / EnableEvent / DisableEvent /
ReturnFromException / ResetEntryInt / HookEntryInt / write / ChangeClearPAD / 0x80024b90
(GetSR-like) / ChangeClearRCnt (Ghidra names them; boot_procs.json); field lacks 0x8003359c,
0x80038538, 0x80038e64, 0x80038e8c, 0x80038f38, 0x80039268, 0x8003c360, 0x8003d118 (twins of
title rows); title lacks 0x80034bac; and functions reached only through vtables are invisible
to jal-based scans (the three title rows above).

## Phase 8: the gap scan's open rows, empty ports, confidence (fifth agent, from 05:15)

Assignment (coordinator): (1) battle's 51 "boss subsystem" candidates, (2) field's 8,
(3) the 61 empty port columns, (4) the 3 stale battle_ovl1984_0 `[port partial]` notes,
(5) guess / likely -> confirmed; regenerate COVERAGE.md. Scratch: `scratchpad/fx_worker/cat3/`
(`asm.py ELF ADDR..`: REA CLI instructions saved to `asm/<elf>/<addr>.s`, one private Ghidra
session per call, ~30 s each; `triage.py [MODULE:ADDR..]`: start-of-function words, own /
family references and engine source lines for each empty-port row; `cls_*.txt` the cat2
classify.py twins).

Findings so far (05:40):
- Task 1: the 51 are not a subsystem. 49 are exact entry points of #1984.0 / #1984.1
  functions (camera_end_scheduled_now ... voice_thank_healer, clash_resolve,
  spell_clash_check, item_use_battle), called from resident code; REA split #2129's own
  bytes at those jal targets, inside already-catalogued #2129 rows (load_stage,
  load_formation, battle_init_objects, the dead voice library, init_bowman = the "Bowman"
  string, the stale enemy / party-module copies, pc_calc_equipment_stats,
  party_loader_update). The other 2 are REA splits inside load_stage / party_loader_update
  (at run time inside #1984.0 spell_menu_release and #1984.1 item_use_battle). Redux RAM dump
  `scratchpad/battle/modules/ram_char01.bin`: #1984.0 at 0x800715d0 (16470 words), #1984.1
  at 0x80081728 (3964) and #1981 at 0x80085518 (2767) all equal word for word. No rows added
  (a row there would split load_stage etc. in the #2129 ELF).
- The catalog agent's 12 battle rows (0x8007fe58 ... 0x80089064, 0x80085f4c, 0x800882a4) are
  the same mistake: #2129's bytes at those addresses are mid-function (or data); the calls
  are to #1984.0 sound_handle_pitch_up / _down / bsfx_update, #1981 battle_fade_task_ctor /
  battle_exit_to_field / result_screen_ctor, the slot modules (hounddog leap, gelatin
  attack2, indalecio_create) or the stale Vesper copy's own branch targets. To be deleted.
- `tools/catalog_gaps.py`: (a) the field scan used base 0x80020ae0 (26 field rows at boot
  addresses: boot's own ResetGraph ... cdrom_init_controller mislabelled as a "field copy";
  #3100 loads at 0x8002f810), so its tags / tail-j / table hits were garbage (the 18 field
  "containment" hits were artifacts); menu_0bb6 used 0x8007e6e4 instead of 0x8007e000.
  Programs now carry "base". (b) "coresident" modules (battle: #1984 / #1981 / enemy-slot /
  spell / party modules) cover an address. (c) "ext-jal": jal targets from the code that calls
  the program (battle: overlays, party / enemy / spell modules into 0x8002f810-0x800715d0;
  field: #3101-#3109 but #3107, menus #2981-#3010), gated on prologue / leaf / after a jr ra.
  (d) `docs/catalog/gaps_explained.csv`: hand-checked non-functions with the reason.
- Field (task 2): the port already has them: 0x8003f518 actor_resolve_anim, 0x800410cc
  actor_collides, 0x80041ce4 sprite_draw_part, 0x80044c8c sprite_cache_b_alloc, 0x800452b4
  actor_sys_project_point, 0x80061cf8 sprite_resample (called from #3101 / #3102);
  0x80079ee4 is the field's copy of title create_game_state (no reference anywhere). Rows
  wrong: 0x80044754 "actor_sys_update" is sprite_cache_a_alloc, 0x80043018
  "script_opcode_index_of" is actor_slot (field_actors[194] at 0x80075360). With the fixed
  scan, field has ~26 candidates (functions only overlays / menus call) being named now.

Progress (06:35):
- Applied (`scratchpad/fx_worker/cat3/apply3.py` = cat2 apply2.py + DEL; update sets in
  `cat3/upd/u1..u3*.py`; backup `cat3/functions.before.csv`):
  - u1: 9 stale `[port partial]` notes rewritten (sound_handle_active, voice_pick_victory,
    voice_play_result, battle_program_create, battle_main_loop, input_dispatch,
    effect_skill_popup, enemy_goat_chant, hud_draw; left: enemy_axe_knockback (battle agent's
    Weirdaxe debris), battle_free_party_models (a design difference)); deleted the 12 bogus
    battle rows and the 26 field rows at boot addresses.
  - u2: field +23 rows (the 7 of task 2, 9 functions only overlays / menus call:
    strings_set, window_list_depth, text_to_card_codes, menu_colour_code, menu_colour,
    field_pad_direction, actor_sys_touch_cache_a / _b, script_flag_set, field_flags_get_30 /
    _31, libgte MulMatrix0 / SetLightMatrix / SetColorMatrix / SetBackColor / SetGeomOffset);
    30 field rows renamed / cited (the "map_sub_*" family is title's strings_word / pack_word /
    strings_block / pack_block, window_list_close_type, bgload_request, actor_slot,
    sprite_cache_a_alloc, rot_matrix_x, field_task_create, ...; dead ones "(not needed: dead in
    the field program ...)"); title's 15 empty rows (memcard_status_of /
    memcard_new_game_notice_of = the bodies of memcard_status / _new_game_notice, menu_scene_get,
    deferred_delete, disc_screen_tim_rect, memcard_reset_callback, inventory_* copies ...).
  - u3: field stat_change_heal_mp (dead), menu_0ba5 text_set_expansion_cursor, field_sub3103
    MDEC_get_alarm; field callways_stop_song_resume ported (below).
- Port (reached in play, from the disassembly): the field save page's constructor (#2998
  0x8007e000) stops a playing Callways song first (callways_song_active ->
  callways_stop_song_resume 0x80031744). `common/menu_save.{h,cpp}`: `menu::g_save_page_open_hook`
  called at the top of SaveMenu::SaveMenu for the field host; `field/menu_camp.cpp`:
  `field_save_page_open` installed by FieldMenuHost. Builds (build-fx). Redux check still to do
  (needs a song playing at a save point); suites not run yet.
- In progress: menu_0bb6 (#2998) is the field's copy of the title save menu (0x8007e000-
  0x80082d28, ~71 functions; only 5 had rows, 3 misnamed rank_list_*). `cat3/align_save.py`
  pairs them (58 exact normalized twins, the rest by order); diffing the 4 that differ (create
  144 vs 121 words, child_done 109 vs 102, save_list_create 171 vs 162, save_build 566 vs 564)
  against the port's field paths before adding the rows.

Progress (07:25):
- menu_0bb6 done (u4): 67 new rows + 4 corrected (rank_list_* were save_menu_rebuild_slots /
  save_list_create / save_list_show_entry); names and ports from the title save menu
  (common/menu_save.cpp, the field host); 4 functions differ from the title (create,
  child_done, save_list_create, save_build), diffed with `cat3/adiff.py`.
- Second real gap found and ported: the field save page's constructor also queues the
  character sprite pack 0xbd2 (unless the session holds it; frame() makes it resident), so
  the list shows the party sprites of saves whose characters the field has not loaded. The
  port never queued it. `SaveMenu::SaveMenu` (field host): `load_handle = bgload_queue(&loads,
  0xBD2, 0, nullptr, 0, 0)` after the Callways hook. Port A/B (map 18 Claude-only game,
  card with a 3-member save; `cat3/savepg/cap{,A}/frame00780.png`): without it the third
  member's sprite is missing, with it all three show. Redux check in progress
  (`cat3/redux/saveref.lua`, a copy of the menu agent's itemsref.lua with field captures;
  card `cat3/savepg/card2.mcd`: #1 the 3-member disc-2 save, #2 a Claude-only map-18 save
  written by the port; SS_POKE field_flags +0x68 = 1 opens Data > Save).
- u5: field 0x80038e64 / e8c / eb8 / f38 / 0x80039268 are the bgload wrappers
  (bgload_queue_entry_noflag / _default / bgload_queue_entry / bgload_queue_to /
  bgload_cancel_entry; the step-3 names voice_queue_push_* and their voice_queue.cpp ports were
  wrong); title 0x80038930's duplicate name bgload_queue_entry -> bgload_queue_to.
- u6 / u7 (confidence): 3 twins (cat2/raise.py); 21 rows raised by passing Redux references
  (stage blocks 2-9 by stage13/16c/21/23/26/36/36_long/41, stage 0xe by stage14c, Opera's
  four #1984.1 Shots by op_b/op_m, voice_pick_victory / voice_play_result by talk_ko,
  projectile_arc_velocity by bowman_l* / op_b*).
- Suites after both ports (build-fx): ss_tests 78/0, ss_game_tests 108/0, smoke 86 PASS / 0 FAIL.

### Phase 8 state at the hand-back (07:50): no runs active

Done:
- Tasks 1-4 complete. `tools/catalog_gaps.py` (no arguments): 0 missing across 32 programs
  (95 explained, 4198 weak j-only). COVERAGE.md regenerated (`tools/catalog_coverage.py` then
  `catalog_gaps.py`): 5429 rows, confirmed 4564, likely 699, guess 166; empty 0, unported 0,
  bad path 0, partial 2 (enemy_axe_knockback: the battle agent's Weirdaxe debris;
  battle_free_party_models: a design difference). Its hand-written "Unported, reached in
  play" section now ends with this pass's summary.
- Rows: 5376 -> 5429 (+91 new: field 24, menu_0bb6 67; -38 deleted: 12 bogus battle, 26
  field rows at boot addresses); ~80 renamed / corrected (field 45, title 19, menu_0bb6 4,
  menu_0ba5 1, field_sub3103 1, battle 9 partial notes, ...); confidence: likely 722 -> 699,
  guess 221 -> 166 (most of the drop is corrections / deletions; 24 raised by evidence).
- Code (reached in play, from the disassembly; flag to the coordinator):
  `engine/src/game/common/menu_save.{h,cpp}` (`menu::g_save_page_open_hook`; SaveMenu's
  field-host block: the hook, then `bgload_queue(&loads, 0xBD2, ...)` unless the session holds
  the pack), `engine/src/game/field/menu_camp.cpp` (`field_save_page_open`, installed by
  FieldMenuHost). Port A/B shows the 0xbd2 fix (missing party sprite before, present after).
  Suites (build-fx, final code): ss_tests 78/0, ss_game_tests 108/0, smoke 86/0.

Not verified against Redux (tried, blocked; the next agent can finish it):
- Needs a genuine card where the loaded save lacks a character another save on the card
  has. The port-written map-18 (`--map 18` debug start) save on `cat3/savepg/card2.mcd` makes
  the ORIGINAL halt in Redux when the camp menu opens after loading it ("Unimplemented a0:40
  syscall" = an unresolved BREAK, epc 0x8006fec4, ra 0x8005f970): a debug start is not a valid
  game state (worth a look by whoever owns `--map`: the save itself loads in Redux and the
  field runs until TRIANGLE). Every scratch card checked has saves of one party only.
  Redux harness ready: `cat3/redux/saveref.lua` (SS_POKE "0x8007570c,0x68,1,1,=" enables Data
  > Save; SS_SHOP "100:UP:6,150:CROSS:6,250:CROSS:6,350:CROSS:6" reaches the slot-1 list;
  captures `out/shop*.raw`, `raw2png.py`), scratch HOME / XDG in `cat3/redux/`.
- The Callways stop needs a song playing when a save point opens.

Next (task 5, the confidence pass; 699 likely / 166 guess left):
- Passing references raise rows cheaply: `tools/refs/refs.csv` + the last full run
  (`scratchpad/regress/full_resync_pass/summary.md`: every listed battle ref RNG-equal to the
  battle's end) -> map each ref's subject to the rows it exercises (done: stage blocks, Opera's
  #1984.1 Shots, the victory / result voices, projectile_arc_velocity).
- boot: 56 likely + 20 guess "PsyQ signature" rows and ~200 `_OBJ_` library internals (all
  not needed / PsyQ); low value.
- field 138 likely ("decompilation"): read each against its port (`cat3/asm.py`, `v.py MODULE
  ADDR` names the jal targets; build a symbol ELF like `cat3/field_all.elf` with
  `psxexe2elf.py --symbols LIST` when Ghidra has no function at an address).
- Several step-3 catalog-agent rows were wrong (voice_queue_push_* were bgload wrappers,
  rank_list_* the save menu, "map_sub_*" the strings_ family, equip_dual_weapon_* the memcard
  status bodies): treat that pass's remaining guesses with suspicion; `cat3/twin.py MODULE
  ADDR:END` finds title / field / battle / menu twins by normalized instructions.

Files (this phase): `docs/catalog/functions.csv`, `COVERAGE.md`, `README.md`,
`gaps_explained.csv` (new); `tools/catalog_gaps.py`; `engine/src/game/common/menu_save.{h,cpp}`,
`engine/src/game/field/menu_camp.cpp`; this log. Scratch: `scratchpad/fx_worker/cat3/`
(update sets `upd/u1..u9`, backup `functions.before.csv`).

## State at the end of phase 5 step 2 (23:xx): no runs active, nothing queued

Scratch: the port captures (port/*/cap) were deleted (regenerable with port_pc.sh); the
Redux references (redux/ref, 3.2 GB) are kept for re-comparisons.


- Ashton level 0, all 10 moves compared: RNG equal to the exit in moves 0-4 and 6-9
  (move 9 = spell 0x40, the world agent's `spell_mod_spell40.cpp`: its first check, equal);
  frames: move 7 (b8) and move 9 (b10) differ only in the homing lines (<= 1.9%: the line
  walk, Redux's Bresenham vs the hardware DDA; not B1-B3: a scratch build with all three
  reverted is the same). Move 5 (b6): equal until b536, where the console's frames take 3
  vblanks under the two dragon heads (`as_b6d`, BW_DENSE): the original's slowdown.
- Variant 1 (the dragons; weapon item 307 poked into handoff 4 +0xc): `asv_b3` (move 2's
  flashes): RNG equal to the exit, frames clean. `asv_b4` (move 3's darts) and `asv_b10`
  (move 9 from frame 5) Redux references ran (orphaned by a queue restart, still writing
  their dirs); compare with `EXTRA_POKE="4:0xc=307/2" ./port_pc.sh asv_bN 7 N 0 1500`.
- Original bugs under the play-state rule (a fix must not change hits, damage, timing,
  pacing; measured with `hits.py` over SS_ENEMY_TRACE=1 port logs against the faithful
  version): F12 held (original code back), F13 kept (my fix moved op_b8's hits 8 / 19
  frames earlier: reverted), F14 fixed (neutral), F17 fixed (neutral), F15 held for a
  decision (dart facing feeds the mirrored attack box; console: Ashton's x velocity is 0 at
  the spawn; facing left no change, facing right the fix would move the box behind the
  dart). The scratch build has env toggles SS_F13 / SS_F14 / SS_F15 / SS_F17 for these
  measurements (`scratchpad/fx_worker/faithful`, re-applied by hand after sync.sh).
- F15 fixed play-neutral (only the darts' drawing follows Ashton's facing, +0x128 at the
  spawn; the boxes are mirrored back to facing 0's side): hits identical for the original,
  the fix and a forced opposite drawing; asv_b4 RNG equal to Redux, no frame above 1%.
- Opera max level (`op_m1..m10`): RNG equal to the exit in moves 1, 3-8; slowdown in moves
  0 / 9 (as level 0) and move 2 (Photon Prison: frame counter 2 ticks behind at b420).
  Move 7's shard ribbon trails differ (<= 15% at b480-540, also in a B1-B3-reverted build:
  ribbon_trail_update or a trail reading its dead source Shot; passed to the battle agent).
  Reported.
- Precis max level `prec_m1..m10`: RNG equal to the exit in all 10; frames differ only in
  the afterimages (moves 1, 2, 4, 7: B2; the faithful build matches). Reported.
- Ashton max level `as_m1..m10`: RNG equal to the exit in all 10 (move 5 too at max
  level; move 9 = spell 0x40 at power 2). Frames: moves 0, 2, 6, 8, 9 differ only in the
  afterimages (B2; the faithful build matches); move 7 in the homing lines (line walk).
  asv_m3 / m4 / m10 (variant 1 at max level): RNG equal to the exit; asv_m10's remaining
  <= 4% (b710) is one-pixel block edges in spell 0x40's blue screen mosaic (UV interpolation,
  gpu_accuracy.md). Reported: phase 5 step 2 complete (all four modules).
- Opera op_m8 b510-540 (the battle agent's question): the shard hit explosions sit up to
  192 units higher in the port: that is F14's fix (the user's decision; visual only). With
  F14 off (scratch build) the explosions match Redux; what remains there is the trails
  (my scratch B1 revert used each quad's own angle; the original reuses quad 0's).
- Queue (`redux/queue_max2.log`): done. Compare
  with `./cmp_run.sh NAME CHAR BYTE [USES] [EXTRA_POKE]` (port run + RNG + frames).
- Tools: `port_pc.sh` uses XDG_CONFIG_HOME=scratchpad/fx_worker/xdg; `redux/queue.sh` takes
  "VAR=VAL ...|job"; don't SIGSTOP a queue started from the tool shell (it was killed);
  `hits.py LOG_A LOG_B` compares enemy HP changes of SS_ENEMY_TRACE=1 port logs.

## Uncommitted files

- The coordinator commits hourly; everything of mine is on disk (pc_ashton.cpp,
  pc_opera.cpp, pc_precis.cpp, pc_module_util.*, gpu.{h,cpp}, docs, catalog rows).

## Phase 4 step 2 files (committed in 155be10)

- New: `docs/notes/gpu_accuracy.md`.
- Edited: `engine/src/psx/gpu.cpp` (plane attributes, quad split / drop),
  `engine/src/runtime/libgpu.{h,cpp}` (`dma_source`, LoadImage),
  `engine/src/game/common/texture.cpp` (uploads through `dma_source`),
  `engine/tests/test_gpu.cpp` (new test), `engine/tests/test_field_message.cpp`
  (comments), `docs/CHECKLIST.md`, `docs/notes/{README,field_effects}.md`, this log.

## Tooling

- `scratchpad/fx_worker/rea.sh`: the REA CLI (Java 21, TMPDIR on disk).
- `scratchpad/fx_worker/dump_credits.py` / `dump_credits2.py`: the #3107 listings.
- `scratchpad/fx_worker/run_ref.sh` / `run_ref2.sh` (disc 2) `NAME MAP SPAWN WORD
  OFFSETS DUMPS [POKE]`, env: `INPUT`, `LEADER`, `END`, `POS`, `COUNTS=addr:word,...`
  (calls per vblank after the spawn), `SHOTS` / `SHOTS_QUIT` (absolute vsyncs),
  `FORCE_DISC`, `SKIP_PROLOGUE`, `ENTRIES=1` (every loader_select_entry with its vsync),
  `PC=N` (pc / ra every N vsyncs), `QUIT` (relative to the map), `TMO`.
- `scratchpad/fx_worker/cmpcred.py REFDIR`: Redux credits captures against
  `portcred/cap` frames.
- `run_ref*.sh` env `VRAM=1`: VRAM (1024x1024 halfwords) dumped with the RAM dumps.
- `scratchpad/fx_worker/ps1tests/`: ps1-tests sources and hardware `vram.png`; build
  `g++ -std=c++20 -O2 -I engine/src harness.cpp engine/src/psx/gpu.cpp`, run
  `./harness triangle|uv|quad|transparency out.bin`, compare with `cmp.py`.

## Findings not yet in the catalog

None.
