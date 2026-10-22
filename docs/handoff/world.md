# World map / field gaps: continuation log

Worker: world agent (phase 3: closing the field program's remaining gaps). Build dir:
`engine/build-world`. Scratch (on disk): `scratchpad/world/` (Redux driver `world.lua` +
`run_redux.sh`, comparison scripts `cmp.py` / `side.py`, Ghidra helpers `ghd.sh` /
`gha.sh` on the project `analysis/ghidra_world`). Nothing is committed by the worker; the
coordinator commits.

## Done and verified (committed)

- Phase 1-2: world map overlay 0xc1e, vehicles, bunny, courier, minimap, insert-disc
  screen (b906414, 69b33dd, 6c88bd4). Notes: `docs/notes/field_overlay_c1e.md`.
- Phase 3 step 1 (catalog triage, party_swap_characters, screen clamp flag, draw_list
  bounds): b32c1a9.

## Phase 3 plan (coordinator, 2026-10-22)

1. Triage catalog rows with an empty port column (field 269, field_ovl_c1d 49,
   field_ovl_c1e 13, boot 26, title 13): PsyQ -> "(PsyQ: runtime)", not needed ->
   "(not needed: why)", ported elsewhere -> file, real code -> port. Skip the menu
   library range 0x8002ffbc-0x80039f94 (menu agent) and the 0x801f9800 sub-overlays /
   sections 5-12 (effects agent).
2. TODO(port) in field_program.cpp (field_update_encounter_streaming 0x80050dec),
   map.cpp (preload table cleanup 0x800570e8, extra packs, afterimages), encounter.cpp,
   camera.cpp.
3. Voice Collection (title 0x8003fb78, title choice 2) and other title TODO(port).
4. 'guess' rows in field / boot / c1e: real names and summaries.

Catalog rule: merge new rows in address order within their module's block (HEAD's row
order is kept; `scratchpad/catalog_order.py` restores it).

## In progress (uncommitted)

Step 2 (field TODO(port)s) is done and tested; waiting for the coordinator's commit:

- New `engine/src/game/field/afterimage.cpp/.h` (+1 line in engine/CMakeLists.txt):
  ghost trails 0x8005ee9c / 0x8005f018 / 0x8005f108, the list 0x800752ec,
  field_destroy_map_objects 0x80051974. Table A 0x1b / 0x1c (vm_services.cpp) and
  actor_destroy (actor_base.cpp) use it; the unused ActorHooks::destroyed is gone
  (actors.h). Test: ss_game_tests field_afterimage_trails_and_fades. Only map 295 (a
  minigame with four racers) uses it; no Redux reference.
- `map.cpp` field_load_map: preload_free_saved_table (0x800570e8, load_queue.cpp), the
  movie-stop / sub-overlay-hook / map-preload resets, afterimages, the side-loaded entry
  (field_side_free, post_battle.cpp), streaming off + preload_release(1), the disc check
  with the load-queue drain (preload_drop_incomplete) or reset, the CamScript teardown,
  and the disc prompt moved after the teardown as in the original.
- `encounter.cpp` field_unload_map: side entry, equipment report
  (field_free_equip_report), CamScript, afterimages. Bug fix: the random-encounter voice
  budget surplus is at least 3000 (0x80051588), not at most.
- `field_program.cpp`: field_update_encounter_streaming (0x80050dec) after field_update
  (decl in field.h); preload_free_saved_table in field_destroy_subsystems; the stale
  TODOs (state 0, default, teardown) rewritten. `camera.cpp`: the world-branch TODO is a
  note (world_camera_reset does the GTE setup).
- `worldmap_actor.cpp`: SS_WORLD_DBG also prints the voice stream / encounter state.
- Catalog: 7 function rows (0x80050dec, 0x80050f64, 0x80051974, 0x800570e8, afterimages)
  and the global g_afterimages.

Verified: PCSX-Redux world map from the north-city save (ref/voice1): the voice stream
goes 0 -> 1 (t 4) -> 2 -> 3 and the battle preloads start, as in the port (CD timing and
the random distance differ; the budget arithmetic matches with the 3000 floor).
PCSX-Redux camp menu on the world map (ref/menu3): the camera block is unchanged after
it. Tests after step 2: ss_tests 76/0, ss_game_tests 72/0, smoke 16/16 (with disc 2,
and with SS_DISC2=/nonexistent).

## Next steps

- Step 3: Voice Collection (title 0x8003fb78; helpers 0x80041100 / 0x800411a4 /
  0x8004137c / 0x800413ec = a spectrum grid of 32 x 40 quads fed by the sound voices).
- Step 4: renames below, plus the remaining 'guess' rows; rename vm_svc_field_rumble_stub
  to match 0x80055da0's real role (coordinator's request) and fix its comment.

## Findings not yet in the catalog (step 4)

- boot 0x80012fec mem_alloc_c = first-fit allocation from the low end of the heap list
  (0x8002b5d8; with 0x8002b5e0 set it uses the high end); 0x800130ec mem_alloc_b = the
  high end (carves a block's top); 0x80013254 mem_free_b = the general free (merges free
  neighbours). Both allocators call the hook 0x8002b5dc with the size on failure.
- boot 0x800134d8 "pad_set_menu_hook" sets that alloc-fail hook (global 0x8002b5dc,
  "g_menu_hook"). The field installs 0x8004b760 (uncatalogued): if 0x8005691c
  (uncatalogued: evicts the preload slot with the lowest buffer address, cancelling its
  read) frees something it retries mem_alloc_c, else calls 0x80055da0(0x7c).
- field 0x80055da0 "field_rumble_stub" is an empty error-report hook: codes 0x27 / 0x28
  (no party member, 0x80052ff4), 0x7c (out of memory), 0x7f (a battle preload failed).
- field 0x8006f8d4 "obj_alloc_ptr_table" = render_ctx_create_ot (ctx +8 =
  screen_create_ot); 0x8006f89c (uncatalogued) = render_ctx_create_arena.
- field 0x800573d0 "field_reset_effect_tables": only a data word at 0x8002f8e0 (after
  g_current_program) points at it and nothing reads that word; a static initialiser
  that never runs (__main is empty).
