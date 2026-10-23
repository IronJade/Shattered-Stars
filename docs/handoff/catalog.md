# Catalog agent: continuation log

Worker: catalog agent (new, started after the Phase 6 rollover at 01:10, docs/handoff/effects.md).
Scratch (on disk): `scratchpad/catalog/`. Nothing here is committed by this agent; the
coordinator commits hourly.

## Step 1: bookkeeping (done)

The 11 "(unported: ...)" rows the effects worker left for the talk layer and the field
menu voice streaming now have real ports (verified by reading the port files and grepping
for the addresses):

- battle `pc_start_talk` 0x8003acac, `pc_start_fury` 0x8003acbc, `party_banter_voice_table`
  0x80074980 -> `engine/src/game/battle/party_talk.cpp`.
- battle_ovl1984_0 `voice_play_extra` 0x80080878, `voice_play_battle_line` 0x800808e8,
  `voice_ally_reaction` 0x80080974 -> `engine/src/game/battle/party_talk.cpp`.
- field `bgload_init` 0x80038bcc, `bgload_shutdown` 0x80038c70, `bgload_update` 0x80039418
  -> `engine/src/game/field/menu_voice.cpp` (the field part; the title's share stays
  `engine/src/game/common/menu_core.cpp`, noted in the cell).
- field `voice_reload_member` 0x80038dfc, `voice_preload_abort` 0x80038e44 ->
  `engine/src/game/field/menu_voice.cpp`.

Fixed the 4 stale `[port partial: ...]` notes (the voice player is ported, `voice_player.cpp`,
and `battle_frame_director` covers the stage code blocks and opening voices):

- battle_ovl1984_0 `sound_handle_active` 0x8007fc54: now notes voice_handle_active
  (voice_player.cpp) instead of "the voice player is not ported".
- battle_ovl1984_0 `voice_pick_victory` 0x800804d4: notes voice_preload_entry
  (voice_player.cpp) instead of "not ported".
- battle_ovl1984_0 `voice_play_result` 0x800807c8: notes voice_play_entry (voice_player.cpp)
  instead of "not ported".
- battle `battle_program_create` 0x800715d0: the `[port partial: ...]` bracket removed; the
  summary now says battle_frame_director covers the stage code blocks, the opening voices
  and the rest, all ported (director.cpp calls `stage_code_update()` and
  `opening_voices_update()`, confirmed by reading the file).

Regenerated `docs/catalog/COVERAGE.md` with `tools/catalog_coverage.py`: 4683 rows; unported
0 (was 11), empty 0, bad path 0; partial 6 (was 10: the 4 above fixed, the other 6 in the
"Partial ports" list are untouched -- effect_skill_popup, enemy_goat_chant,
battle_main_loop, battle_free_party_models, input_dispatch, hud_draw -- those still have
real missing parts per their notes, not stale ones).

Rows touched: 11 port cells + 4 summary cells in `docs/catalog/functions.csv` (no new rows,
no reordering). `docs/catalog/COVERAGE.md` regenerated.

## Step 2: gap scan (starting)

Plan: write `tools/catalog_gaps.py` to scan, per program, the jal targets reached from the
code plus REA's `list_procedures` over the program's image, and diff against functions.csv's
address range for that module. Known example to check first: boot 0x8001446c-0x800144e4.
