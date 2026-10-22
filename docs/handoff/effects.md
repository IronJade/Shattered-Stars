# Effects / embedded map code: continuation log

Worker: field effects agent (phase 3: embedded map code, container sections 5-12).
Build dir: `engine/build-fx`. Scratch (on disk): `scratchpad/fx_worker/`. Nothing here is
committed by the worker; the coordinator commits.

## Done and verified (all of phase 3 except #3107)

- Step 1, the shadow overflow (`fieldactor_build_shadow`, 4 points read every 8 bytes).
  Committed.
- Step 2, the field's own RotMatrix (0x8006e52c) at every site that calls it.
  Committed.
- Step 3, #3103 (section 5, the movie player): `game/field/field_movie.{h,cpp}`. The map
  548 FMV matches PCSX-Redux. Committed in b906414.
- #3108 (section 8, scripted paths, maps 384 and 598-602) in
  `game/field/field_sub_overlays.{h,cpp}`:
  - `path_start` (ext 0x9c), `path_add_point` (ext 0x9e), `path_control`;
  - the A 0x2a / 0x2b / ext 0xea globals;
  - no encounters while on a path.

  On map 384 (ladder climb, path chaining, back down) the leader's position equals
  Redux at every 40th vblank through 1360. Input `400:up,403:none,450:cross,456:none,
  700:up,1000:none,1050:down,1300:none` (Redux: relative to the map load).
- #3106 (section 9, mirror images, map 139): `mirror_add` (ext 0xbf) and
  `mirror_update` (the `fieldactor_draw` hook).
  - Checked against Redux with `--map-pos -157,0,11` and `-60,0,-370` at map+150. The
    images match; 15-28 near-black pixels differ in the shadow at the image's feet. Not
    investigated.
- #3104 (section 10, the rainbow on map 143): `rainbow_create` (special 0) and the
  Rainbow task. Aligned on the first update; at +2 to +400 no pixel differs by more
  than one 5-bit step. The 1-step differences are GPU dithering of additive gouraud
  quads, outside this code.
  - Test: `SS_SCRIPT_GLOBALS="1d0=0x20000,740=4" --map 143`. Redux:
    `POKE="1d0=0x20000,740=6"`, spawn 0x801f9894 / word 0x3c028007.
  - The ray step is 0x400/24 (multiply by 0x2aaaaaab, then `sra 2`).
- #3109 (section 7, the Cooking Master TIME / PRESS gauge, map 688): `gauge_create`,
  `gauge_destroy`, `gauge_update` (main loop, after `task_run_all`), A 0x33 / 0x35 /
  0x37. Section 11 (entry #2) holds the item records A 0x33 reads (byte 0x19).
  - Aligned on the first update (0x801f999c, word 0x27bdff88). The TIME row is
    pixel-identical to Redux at +200 and +800 (5:00, 4:51).
  - PRESS shows 53 vs 56: the script's own value differs (another dish), so that row
    differs only in the digits and the bar length.
  - Test: `SS_SCRIPT_GLOBALS="860=0x400000" --map 688` with the Cross mash input; the
    gauge appears at about vblank 3791.
- `ss_tests` 76/76 and `ss_game_tests` 71/71 pass, with the disc and without it.
  `fx_streak_reaches_target` now skips without `SS_DISC1`.

- #3105 (section 12, map 581): `the_end` (A 0x40), the final screen: caption typed
  in, then the logo and "fin." at 640x480 interlaced, with music; it never returns.
  - Against Redux (aligned on the A 0x40 call, Redux 62 vblanks later because of its
    CD reads): the caption frames are within one step (10-20 edge pixels).
  - The logo comes out 1 px left of Redux. The quads and texture upload follow the
    asm, so this is likely GPU rasterisation of 15-bit textured quads; not investigated.
  - Test: `SS_SCRIPT_GLOBALS="79c=0x3c000" --map 581`; Redux spawn 0x801f9a64 / word
    0x27bdff18.
- #3107 (section 6): the program A 0x2d runs. Assessed only: an 85 KB program at
  0x8002f810 with its own libpress, the credits between the private endings and the
  final screen. Porting it is a separate program port; A 0x2d is still a stub.
- B_16 / B_70 preload: on map 576 the console preloads the whole cue list (#4511,
  555008 bytes; the Redux RAM dump shows slot bytes 0; 897444 bytes free before).
  - `field_movie.cpp` now models the console heap as that figure minus the preload
    table's buffers. Map 576 stays whole.
  - Map 685's B_16 (music #2682 + cue list #4517, disc 2) goes partial, so the
    sequence loads its music bank as on the console. Not run: the ending state is hard
    to reach.
- Main-loop rate during movies. Redux on map 548 runs 28 loop passes a vblank before the
  first frame, then 18 / 0 / 1 / 5 per 4-vblank frame (compose in the first vblank,
  DecDCTvlc in the third). The port ran 1-2.
  - Now `MovieGlobals::loop_vblanks` = 1/28 per pass (set by `field_movie_start`) and
    the compose spends 0.86 vblank per ~990 pixels (`movie_console_spend`). The port
    gives 28, then 20 / 0 / 1 / 6.
  - Movie frames still match Redux (same about 10-vblank start offset).
- `ss_tests` 76/76; `ss_game_tests` 71/71 with and without the disc; the smoke test has
  16 PASS.

## Uncommitted files

- New: `engine/src/game/field/field_sub_overlays.{h,cpp}` (#3108, #3106, #3104, #3109,
  #3105).
- `engine/CMakeLists.txt`.
- `actor_field.cpp`: hooks.
- `map.cpp`: reset, section 11, `[[maybe_unused]]` todo.
- `vm_services.cpp`: services.
- `special.cpp`: special 0.
- `field_program.cpp`: `gauge_update` in the main loop.
- `field_movie.cpp`: heap model, loop pass cost, compose cost.
- `title/movie.{h,cpp}`: `loop_vblanks`, `movie_console_spend`.
- `tests/test_field_effects.cpp`: the streak test skips without the disc.
- Catalog: `analysis/field_catalog/subcode_updates.csv` (69 rows) and
  `subcode_globals_updates.csv` (30).

## Next steps

1. #3107, the credits program, if wanted: a program port of its own.
2. Optional: the 1-px logo offset of the end screen (GPU); the mirror image's
   shadow (15-28 pixels); the movie start latency (seek time from the head position,
   TODO in movie.cpp).

## Tooling

- `scratchpad/fx_worker/rea.sh`: the REA CLI with Java 21 and TMPDIR on disk. It
  avoids the shared MCP session.
- `scratchpad/fx_worker/dump_overlay.py N` writes `analysis/decomp/subN/listing.s`.
- `scratchpad/fx_worker/run_ref.sh NAME MAP SPAWN WORD OFFSETS DUMPS [POKE]` with env:
  - `INPUT=` (relative to the map load);
  - `LEADER=N` (leader position every N vblanks);
  - `END=N` (quit);
  - `POS=x,y,z` (leader spot, like `--map-pos`).

  With SPAWN 0, capture offsets count from the map load.

## Findings not yet in the catalog

None. Everything above is in `analysis/field_catalog/subcode_updates.csv` /
`subcode_globals_updates.csv`: actor_create_child 0x8004185c, SetDrawMode / ResetGraph /
SetDispMask, the field_load_map / field_update / A 0x2d additions, and the path, mirror,
rainbow, gauge and item-record globals.
