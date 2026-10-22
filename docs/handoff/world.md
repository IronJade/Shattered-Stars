# World map / field gaps: continuation log

Worker: world agent (phase 3: closing the field program's remaining gaps). Build dir:
`engine/build-world`. Scratch (on disk): `scratchpad/world/` (Redux driver `world.lua` +
`run_redux.sh`, comparison scripts `cmp.py` / `side.py`, Ghidra helpers `ghd.sh` /
`gha.sh` on the project `analysis/ghidra_world`). Nothing is committed by the worker; the
coordinator commits.

## Done and verified (committed)

- Phase 1-2: world map overlay 0xc1e, vehicles, bunny, courier, minimap, insert-disc
  screen (b906414, 69b33dd, 6c88bd4). Notes: `docs/notes/field_overlay_c1e.md`.

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

Step 1 (triage) is done and tested; waiting for the coordinator's commit. My changes:

- `docs/catalog/functions.csv`: port column of 309 of the 317 triage rows (list in
  `scratchpad/world/triage_all.csv`, values in `scratchpad/world/triage_apply.py`).
  The 8 left are steps 2-3: 0x80050dec, 0x80051974, 0x800570e8, 0x800573d0, the
  afterimages 0x8005ee9c / 0x8005f018 / 0x8005f108, title 0x8003fb78.
- `script_party.cpp` / `script_vm.h`: party_swap_characters (0x800534b4, ext 0xb0; no
  map script on either disc uses it); `vm_services.cpp`: vm_svc_party_swap_characters
  calls it, vm_svc_loader_11988 returns 1 (loader_select_entry_at accepts when idle; the
  scripts' B_4f prefetches never read the result).
- `map.cpp`: screen_clamp_flag (field_update_screen_clamp_flag 0x80055c08) at the old
  TODO in field_load_map (analog steering on maps whose camera looks down).
- `field_program.cpp`: the FieldPanel TODO is now a note (0x800752a8 is never opened).
- `common/menu_widget.cpp` (menu agent's file): Widget::draw_list skips rows past the
  item count, as title 0x80073cdc does.

Tests after step 1: ss_tests 76/0, ss_game_tests 71/0, smoke 16/16 (SS_DISC1 set).

## Next steps

- Step 2: field_update_encounter_streaming (0x80050dec), preload_free_saved_table
  (0x800570e8), extra packs, afterimages (0x8005ee9c / 0x8005f018 / 0x8005f108) with
  field_destroy_map_objects (0x80051974), TODO(port) in encounter.cpp and camera.cpp.
- Step 3: Voice Collection (title 0x8003fb78).
- Step 4: renames below, plus the remaining 'guess' rows.

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
