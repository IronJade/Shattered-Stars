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

### Chisato (#2159): ported, verifying

- `engine/src/game/battle/pc_chisato.cpp` (new): ChisatoUnit (vtable 0x801f0904), 8 moves,
  6 Shot callbacks, the normal attack (slot 30), slots 4 / 6 / 33 / 35 / 36 / 39.
- Small hunks in the battle agent's files (to flag): `pc_modules.cpp` (case 12),
  `unit.h` (`Unit::jump_time`, vtable slot 6, virtual), `unit_move.cpp` (its body:
  unit_jump_time), `party_states.cpp` (the leap calls `jump_time`), CMakeLists.
- Verified against Redux (moves 0-3 at charge level 0): RNG draws equal at every sample
  through the battle and the result screen (to the field's 1024 draws after the exit);
  frames under 0.8% through the fights (b160-b900).
- Running: Redux `chis_b5..b8` (moves 4-7, level 0); then all 8 at USES=999 (max level).
- Tools: `scratchpad/fx_worker/battle/redux/run_pc.sh NAME CHAR L1BYTE [R1] [USES]` (Redux,
  scratch HOME) and `port_pc.sh NAME CHAR L1BYTE [USES]` (port on the same field_exit dump);
  compare with `scratchpad/battle/rngcmp.py REFLOG PORTLOG` and
  `FRAMECMP_D=7 scratchpad/battle/framecmp.py REFDIR PORTDIR/cap 160 1100 10`.
- Disassembly: `scratchpad/fx_worker/battle/char{06,07,09,12}.elf`; decompiles through the
  MCP REA session (batch_decompile, open_binary with the CLI snapshot
  `battle/<name>/snapshot`); listings with `dump_asm.py` (one CLI process).

### Original bugs F1, F3, F5, F7, F8 (done, on disk, uncommitted): one commit point

- F1 `field/effects_draw.cpp` (the line and burst depth: `otz_for_depth`), F3 / F7
  `field/effects.cpp` (glow replaced; clamp), F5 `field/field_movie.cpp` (per-row clipped
  write-back), F8 a-e `field/actor_field.cpp` (`fieldactor_build_light_split`).
- Tests adjusted: `test_field_actors.cpp` (light-split Redux trace: cache-B UVs skipped for
  F8b, 95 dup46 calls accepted as subsets for F8d; the list-4 triangle test expects the
  corrected UVs), `test_field_effects.cpp` (the SS_FX_REF dump tests find lines / burst quads
  in any slot, F1). ss_game_tests 74/0 with the disc; with SS_FX_REF all dump tests pass.
- F4 (lit sphere): recommendation "intended" (only caller passes hemi 1, the other path is
  dead). Docs: original_bugs.md rows, field_effects.md, field_overlay_c1d.md.

Next: finish Chisato (b5-b8, max-level runs), report Chisato + the bug commit point; then
Precis (char06), Opera (char09), Ashton (char07).

## Uncommitted files

- `docs/notes/gpu_accuracy.md` (Lines section), this log.

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
