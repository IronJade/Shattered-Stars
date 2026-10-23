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

## Phase 5 step 2: party modules (in progress)

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

### Precis (#2155): ported, registered (case 6), level 0 verified; max level queued

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
- Max level: Redux `prec_m1..m10` queued after Opera's (`redux/queue.sh`, 3 at a time).

### Opera (#2153): ported, registered (case 9), smoke-run; Redux references running

- `engine/src/game/battle/pc_opera.cpp` (new): OperaUnit (vtable 0x801f1e10), 10 moves
  (pointer table 0x801f1f88), slots 31 / 35 / 36 / 37 / 45, 14 Shot callbacks including
  #1984 block 1's shot_spiral_ring / shot_falling_rock / shot_vortex_area and
  shot_draw_ring_bands (not ported before; only her moves use them). Move 0 starts
  spellcast kind 2 (sets `s3c` and `orb_set` to her AnimSet).
- Port smoke: all 10 moves run (`port/opsmoke_N`).
- Redux `op_b1..b10` queued (level 0); compare with `port_pc.sh op_bN 9 N 0 1500`.
- New bugs: F13 (burst's last shard velocity written to the burst: fix after the faithful
  check), F14 (shard hit sprites' raw y jitter: decide).
- Catalog: battle_pc2153 rows (33) and port columns for the four block-1 functions.

### Low priority after the modules: GPU check (coordinator)

Result-screen fade: the subtractive gouraud band (POLY_G4, abr 2, dithered, DR_MODE tpage
0x40) drifts from b-7 to b-5 in the best-matching port frame from ~b1040; the whole scene
~2.5% brighter in the port from b850 (ref ernest_58). Check against the hardware model:
dither-then-blend order / Redux brightness. Document in gpu_accuracy.md (change nothing) if
the port follows hardware; fix if the port is wrong. Suggested test: two G4 quads, white to
a colour, DR_MODE tpage 0x40 over a scene. Precis b4 (faint additive vortex) and b10
(sphere) differences may be the same thing.

### Original bugs F1, F3, F5, F7, F8 (done, reported with Chisato); F11 avoided, F12 decide (new)

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

Next: compare Opera (op_b1..b10) as they finish, then Precis / Opera max level; report
Precis, then Opera (run the three suites first); then Ashton (char07: vtable 0x801f1e4c,
object 0x388, killer table 0x801f1fd0 of 10 direct pointers; slots 4, 6, 33, 35, 36, 37,
46); then the GPU check. Redux cap: 3 instances at a time (`redux/queue.sh`), builds -j8.

## Uncommitted files

- Everything so far is in the WIP checkpoints (96aa11d); this log.

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
