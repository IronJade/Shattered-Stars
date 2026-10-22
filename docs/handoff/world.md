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
- Phase 3 steps 3-4 (Voice Collection, readability renames): 6ddcb4d.
- Phase 4 step 1 (saves byte-compatible: field save icon, buffer leftovers, snapshot
  refresh; SS_STATE_DUMP; test_save): 4b86443.

## Phase 4 plan (coordinator, 2026-10-22)

1. Save / load compatible with the original memory card data, on scratch cards only:
   (a) a port save loads in PCSX-Redux with the same state, (b) a Redux save loads in the
   port with the same state, (c) the port's save block matches the original's byte for
   byte apart from random / time fields (each difference explained). Fix mismatches
   (menu_save.cpp is the menu agent's: minimal edits, flagged).
2. World edits part 2: mods data overrides (item records, shop inventories, enemy stats
   per type via a hook in the battle encounter loader), checked against the original
   value and logged; an in-window overlay with the debug console's status / output (only
   with --debug); smoke cases; engine/README.md.

(Phase 3 plan, done: triage, field TODOs, Voice Collection, readability.)

## In progress (uncommitted)

Phase 4 step 2 is done and tested; waiting for the commit:

- Step 2, data overrides: `engine/src/game/field/mods.{h,cpp}` rewritten (kinds script /
  item / shop / enemy, field names or OFFSET[:SIZE], EXPECTED checked, applied / skipped
  logged once). Hooks (one or two lines each): item records in map.cpp (section 11),
  menu_shop.cpp / menu_camp.cpp / menu_status.cpp (entry 3002, menu agent's), battle
  loaders.cpp load_item_data (battle agent's); shop stock in menu_shop.cpp (shop
  lists); enemy stats in battle/encounter.cpp's record loop (enemy agent's file).
  `tools/so2_mod.py`: item / items / shop / shops / enemy / enemies (EXPECTED read from
  entry 3002, overlay 0xbaa, the encounter packs 0x57d + N).
- Step 2, overlay: `engine/src/runtime/debug_overlay.cpp/.h` (5x7 font; status line +
  last six console lines on a darkened band, drawn into the displayed and captured
  image, only with --debug); runtime.{h,cpp} (debug_overlay flag, debug_say, the typed
  command echoed, SS_DEBUG_INPUT="VBLANK:cmd;..." for tests); debug_console.cpp's say()
  goes through debug_say; main.cpp sets debug_overlay with --debug; CMake +1.
- Smoke: mod_shop (shop 1 entry 0 -> Revival Card at 4321 Fol: the buy list shows it),
  mod_enemy (--battle 19: Landworms with 4321 HP), debug_console (scripted commands).
  engine/README.md: world edits table / tool commands, the overlay.
- Tests after step 2: ss_tests 76/0, ss_game_tests 74/0, smoke 21/21.

## Next steps

- Phase 4 complete once committed; wait for the coordinator.

## Findings not yet in the catalog

- (none: the step-1 findings are in the catalog now)
