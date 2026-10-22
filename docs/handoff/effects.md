# Effects / embedded map code: continuation log

Worker: field effects agent (phase 3: embedded map code, container sections 5-12).
Build dir: `engine/build-fx`. Scratch (on disk): `scratchpad/fx_worker/`. Nothing here is
committed by the worker; the coordinator commits.

## Done and verified

- Step 1, the shadow overflow (`fieldactor_build_shadow`, 4 points read every 8 bytes).
  ASan on map 588 is clean. Committed.
- Step 2, RotMatrix: the field's own RotMatrix (0x8006e52c) is used at every site that
  calls it. Map 129 matches PCSX-Redux. Committed.
- Step 3, #3103 (section 5, the movie player): `game/field/field_movie.{h,cpp}` and the
  movie branch of the main loop. The map 548 FMV matches Redux (mean difference
  0.4/255), sprites composited. Committed in b906414.
- #3108 (section 8, scripted paths, maps 384 and 598-602) in
  `game/field/field_sub_overlays.{h,cpp}`:
  - `path_start` (ext 0x9c), `path_add_point` (ext 0x9e), `path_control` (player
    control while 0x800761ea is set);
  - A 0x2a / 0x2b / ext 0xea globals;
  - no encounters while on a path.

  Verified against Redux on map 384 (`--map 384`, input `400:up,403:none,450:cross,
  456:none,700:up,1000:none,1050:down,1300:none`): the leader's trajectory is identical
  at every 40th vblank, through 1360.
- #3106 (section 9, mirror images, map 139): `mirror_add` (ext 0xbf) and
  `mirror_update` (`fieldactor_draw` hook while 0x800761e8 is set).
  - Checked against Redux with `--map-pos -157,0,11` and `-60,0,-370` at map+150. The
    images match; 15-28 dark pixels differ in the shadow at the image's feet. Not
    investigated yet.

- #3104 (section 10, the rainbow on map 143): `rainbow_create` (special 0) and the
  Rainbow task. Compared with Redux, aligned on the first update (0x801f9894), at +2,
  +20, +60, +100, +200 and +400:
  - no pixel differs by more than one 5-bit step on the rainbow;
  - the 1-step differences are GPU dithering of additive gouraud quads, outside this
    code;
  - +400 also has 26 pixels of message text one character apart.

  Note: the ray step is 0x400 / 24 (24 rays make a quarter circle); the asm does a
  multiply by 0x2aaaaaab plus `sra 2`.

## In progress (uncommitted)

- `engine/src/game/field/field_sub_overlays.{h,cpp}` (new): #3108 and #3106 are done.
  #3104 is next.
- `engine/CMakeLists.txt`: adds `field_sub_overlays.cpp`.
- `actor_field.cpp`: `path_control` / `mirror_update` hooks and the include.
- `map.cpp`: `sub_overlays_map_reset()` at map load, and `todo()` marked
  `[[maybe_unused]]`.
- `vm_services.cpp`: `sub_live(section)` gate; ext 0x9c / 0x9e / 0xbf and A 0x2a /
  0x2b / ext 0xea wired.
- Builds; `ss_tests` / `ss_game_tests` not re-run since #3108.

## Next steps

1. (done) #3104 (section 10, map 143): the rainbow task. Special command 0 creates it
   (0xd4 bytes, task priority 1, update 0x801f9894). It draws 24 rays x 8 bands of
   POLY_G4 0x3a at OT entry 0x110, after a DR_MODE 0xe1000220 (additive).
   - Brightness per ray: `clamp(timer*2 - 24*i, 0, 0x68)`; the timer adds frame
     ticks up to 0xc34f.
   - Radii come from widths {5,6,7,3,1,2,1,2,3,0} starting at 250; centre (0xd2, 0xec);
     angles 0xa64 + k*0x400/6.
   - Colour tables R 0x801f9f10, G 0x801f9f34, B 0x801f9f58 (in
     `analysis/subcode/entry_3104.bin`).
   - field_load_map unregisters and frees 0x800752cc.
   - Test: `SS_SCRIPT_GLOBALS="1d0=0x20000,740=4" --map 143` (the scene reaches
     ext_a8(0) at about vblank 300). Redux: `POKE="1d0=0x20000,740=4"`.
2. #3109 (section 7, map 688): the timer / counter panel object 0x800752e8 (0xb0
   bytes: 3 TextPanels at +0xc / +0x34 / +0x5c). Calls in: A 0x35 creates/destroys
   it, A 0x33 = 0x801f9fb8, A 0x37 = 0x801f9f54, and the main loop calls 0x801f999c
   each frame. Entry #2 (section 11) is its data (0x800756c8 / 0x800756cc).
3. #3105 (section 12) + #3107 (section 6, an 85 KB program) on map 581: A 0x40 =
   0x801f9a64.
4. Check against Redux:
   - B_16 / B_70 partial preload: does a B_6f sequence get its music bank? The port
     always preloads whole entries; `field_movie.cpp` preload_bytes.
   - The main-loop rate while a movie plays (script `wait` timing).
5. Catalog: append #3108 / #3106 / #3104 / #3109 rows to
   `analysis/field_catalog/subcode_updates.csv` / `subcode_globals_updates.csv`
   (not done yet for these).

## Tooling

- `scratchpad/fx_worker/rea.sh`: the REA CLI with Java 21 and TMPDIR on disk. It
  avoids the shared MCP session, which other agents switch.
- `scratchpad/fx_worker/dump_overlay.py N`: full listing of overlay #N, written to
  `analysis/decomp/subN/listing.s`. It uses the tiny ELFs in `scratchpad/fx_worker/elf/`
  (built with psxexe2elf `--raw` and the `analysis/subcode/subN.sym` starts).
- `scratchpad/fx_worker/run_ref.sh NAME MAP SPAWN WORD OFFSETS DUMPS [POKE]` with env
  `INPUT=` (relative to the map load), `LEADER=N` (leader position every N vblanks),
  `END=N` (quit), `POS=x,y,z` (leader spot, like `--map-pos`). With SPAWN 0, capture
  offsets count from the map load.

## Findings not yet in the catalog

- 0x8004185c `actor_create_child(actor, slot)`: destroys child[slot] (+0x3d4) and, on
  field maps, makes a new field actor with the parent's model via
  `actor_init(pos 0, layer 0xff, talk 0, pick -1, flags 0x1000f)`.
- `field_load_map` clears the 0x800761d4 mirror slots, 0x800761e8 and 0x800761ea, and
  sets 0x800761a4 = 0x14000 on every map load.
- Section 7-10 overlay list: 0x800744d6[id] = #3109 / #3108 / #3106 / #3104.
