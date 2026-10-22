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

- Step 1 triage: starting.

## Next steps

- Build the triage list (`scratchpad/world/triage.csv`).

## Findings not yet in the catalog

- (none yet)
