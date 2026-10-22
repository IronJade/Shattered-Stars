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

Phase 4 step 1 (save round trips) is done and verified; waiting for the commit:

- Fixes: new `engine/src/game/common/save_icon.cpp/.h` (CMake +1): the field save
  page's icon (overlay 0xbb6 0x8007f648, entry 0xbd3, by story progress), the file
  buffer prefilled with the freed icon sheet from +0x454 (the bytes save_build never
  writes), and the snapshot refresh before saving (field 0x80055fc8, so game_state +0x1c
  is saved as the music index like the original). menu_save.cpp (menu agent's): two
  small hunks in save_build (call field_save_prefill, use its icon).
- Test: ss_game_tests save_field_icon_and_leftovers (new tests/test_save.cpp, CMake +1).
- Debug option SS_STATE_DUMP="VBLANK:PATH,..." (field_program.cpp).
- Results (scratch cards only; Redux ref/sv1, ref/rt_a, ref/rt_b; port rt/*):
  (c) port S02 vs Redux S02 saved from the same loaded state: identical except
      inventory +0x820 (RAM address of the item records), game_state +0x14 (the leader's
      position pad word: uninitialised heap in the original's world actor), snapshot
      +0x410..+0x440 (the original's 0x440 snapshot buffer is not cleared), and their
      consequences (packed length / stream, the two checksums).
  (a) the port's save loaded in Redux and (b) Redux's save loaded in the port: config,
      party, records, inventory, game_state, script flags, field flags, map and position
      identical, except the encounter distance (field flags +0x1c, RNG) and the
      inventory check bytes (+0x824.., salted with rng_range on load).
- Tests after step 1: ss_tests 76/0, ss_game_tests 74/0, smoke 16/16.

## Next steps

- Phase 4 step 2: mods data overrides (items, shops, enemy stats hook), the console
  overlay (--debug), smoke cases, engine/README.md.

## Findings not yet in the catalog

- (none: the step-1 findings are in the catalog now)
