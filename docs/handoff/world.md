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
- Phase 3 step 2 (afterimages, encounter voice streaming, field_load_map /
  field_unload_map teardown, voice budget floor): 1879d21.

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

Steps 3 and 4 are done and tested; waiting for the coordinator's commit:

- Step 3, Voice Collection: new `engine/src/game/title/voice_collection.cpp/.h` (+1 line
  in engine/CMakeLists.txt; `title_program.cpp` calls it for choice 2). voice_collection
  0x8003fb78, the grid 0x80041100 / 0x8004137c / 0x800413ec, draw_number 0x800411a4.
  Verified with PCSX-Redux (ref/vc3: the save card; ref/vc5: a scratch card with every
  voice heard, `scratchpad/world/cards/allvoices.mcd`, BGM track 0): menu, labels and
  numbers match exactly; the grid's picture, wire frames and the music-driven heights
  match up to GPU rasterisation (lines, GT4 texture interpolation). Exit returns to the
  title. The stale sound TODOs in intro.cpp / title_screen.cpp are gone.
- Step 4, readability: renames in the catalog and the port (word-boundary, engine/src
  except battle/, docs/catalog, docs/notes/field_*): mem_alloc_c/_b/mem_free_b ->
  mem_alloc_low/_high/mem_free, pad_set_menu_hook -> mem_set_alloc_fail_hook (g_menu_hook
  -> g_alloc_fail_hook), field_rumble_stub -> field_error_stub (vm_svc_field_error_stub,
  comment fixed), sound_fade_out_all -> sound_fade_out_effects, sprite_anim_set_byte2 ->
  sprite_anim_set_mode, actor_set_special -> actor_set_patrol_emote,
  obj_alloc_ptr_table -> render_ctx_create_ot, field_reset_effect_tables ->
  field_static_init_effect_tables, field_update_screen_clamp_flag ->
  field_update_screen_steering, sound_bank_holder_new -> sound_holder_create,
  world_transform_vertices -> world_morph_vertices, entry_cache_free_pair ->
  entry_cache_free_party_packs. New rows: field_alloc_retry 0x8004b760,
  preload_evict_lowest 0x8005691c, render_ctx_create_arena 0x8006f89c; globals
  g_pad_option 0x8002b63c (write-only), g_screen_steering 0x8007627e. 21 of the 23
  'guess' rows are now likely / confirmed; the two left (party_update_bonds summary
  improved, party_find_bonded) are the menu agent's (+0x3b pair semantics unknown).
  Comment fixes in other agents' files: menu_camp.cpp (two 0x800134d8 TODOs: the
  alloc-fail hook, not needed) and battle/battle_program.cpp (same). Not touched:
  battle/loaders.cpp still says mem_alloc_b in a comment, docs/notes/battle.md too.

Tests after steps 3-4: ss_tests 76/0, ss_game_tests 73/0, smoke 16/16.

## Next steps

- Phase 3 is complete once steps 3-4 are committed. Known deviation left: title movie
  start latency (movie.cpp note: the studio movie starts about 18 vblanks earlier than
  in PCSX-Redux because every movie uses the opening's seek time; ref/mov2-3).
- The two 'guess' party-bond rows (0x800341e4, 0x8003446c) belong to the menu agent.

## Findings not yet in the catalog

- (none: the step-1 findings are in the catalog now)
