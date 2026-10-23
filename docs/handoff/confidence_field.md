# Handoff: catalog confidence pass, field-side programs (conf-field agent, from 2026-10-23 08:20)

Assignment (coordinator): raise / correct the `likely` and `guess` rows of the field-side
modules in `docs/catalog/functions.csv`: field (138 likely), field_ovl_c1d (27), field_ovl_c1e
(2), field_sub3103 (1), menu_0bb6 (9), menu_0ba5 (2 likely + 1 guess), menu_0ba6 (1), title
(42 likely + 32 guess), credits_3107 (1 likely + 8 guess): 264 rows at the start. Another agent
does the battle* modules at the same time: never touch those rows.

## Rules (all mandatory)

- For each row read the disassembly / decompilation and compare with the port column's C++.
  Matches in full (or checked against live data / a passing Redux reference): `confirmed` +
  evidence. Name or summary wrong: correct (snake_case subsystem_verb_object; Sony SDK names
  for SDK functions); rename in the C++ only when it is a pure rename (build + tests pass).
  Port behaviour differs: don't rewrite engine code beyond a trivial play-neutral fix; list it
  under "Port leads" below. A newly found original bug goes to docs/notes/original_bugs.md as a
  proposal, not fixed. Can't settle it: leave the confidence and say why in the evidence.
- Catalog edits only through update files `scratchpad/conf_field/uN.py` (format in
  `scratchpad/fx_worker/cat3/apply3.py`'s header), applied under the lock:
  `flock scratchpad/catalog.lock python3 scratchpad/fx_worker/cat3/apply3.py scratchpad/conf_field/uN.py`.
  Never write functions.csv without the lock; never run scratchpad/merge_catalogs.retired.py.
  Apply at least every 30 minutes. At the end: `tools/catalog_coverage.py` under the same lock.
- REA server shared: re-open your binary before each batch of REA calls and check the
  subject. REA for disassembly (no ad-hoc capstone).
- No game code / assets in git; headless only, no audible audio; never touch
  ~/.config/shattered-stars or ~/.config/pcsx-redux (Redux only -cli + scratch HOME, max 3).
  Kill by exact PID; wait loops with deadlines; no `pgrep -f` on own pattern. Scratch under
  scratchpad/conf_field/ (not /tmp). Build dir engine/build-conf-field, -j8; max 6 game
  processes. Engine change: build, ss_tests, ss_game_tests, tools/smoke_field_events.sh
  (SS_DISC1 = disc 1 .cue).
- Don't commit or push. One line per finished batch in scratchpad/checkpoint_notes.txt,
  prefixed "conf-field: ". Keep this file current (every batch, every 30 minutes); past ~400k
  tokens of context, update it and end the turn.

## Tools (scratchpad/conf_field/)

- `todo.csv`: the 264 rows at the start. `twins.txt` (`twins.py`): normalized-instruction twins
  of each todo row in title / field / battle / menus / overlays / credits / boot.
- `d.py MODULE ADDR [N]`: the Ghidra decompilation in analysis/decomp/ with catalog names.
- `asm.py ELF ADDR..`: REA CLI instructions -> `asm/<elf>/<addr>.s` (copy of cat3/asm.py);
  `w1..w4.sh` dumped every todo row (logs w*.log). ELFs with catalog symbols: `title.elf`,
  `field1d.elf` (field + #3101), `field1e.elf` (field + #3102), `menu_0bb6.elf`,
  `menu_0ba6.elf`, `menu_0ba5.elf` (field + menu), `credits.elf`.

## Done (applied batches; scratchpad/conf_field/uN.py)

- u1: title libcard / libapi thunks renamed to the SDK names (_card_load, _card_write, _new_card,
  _card_clear, open, lseek, read, close, format, firstfile2; 0x8004b0d4 firstfile, 0x8004b270
  FIRST_OBJ_19C; noop_stub_b0c4 -> get_sp) + 4 new title FIRST_OBJ rows; MDEC_status (title
  0x800505ac, credits 0x80031808, #3103 0x801ec680: returns *MDEC1 0x1f801824; were
  MDEC_get/set_alarm); title 0x80050230 / credits 0x8003148c DecDCTinCallback (DMACallback(0));
  deleted credits 0x800318c4 DecDCTvlcSize2 (a branch target inside DecDCTvlcSize); field
  callways family, auto-equip family, menu page handlers confirmed.
- u2: field party specialty (party.cpp), debug windows, stat_calc_load_items; title twins;
  title renames rot_matrix_axis (0x8002fc64), party_pack_a_entry / _b_entry, party_packs_load_sync,
  text_render_message, debug_menu_window_draw (+ new 0x80031cbc debug_menu_window_select),
  stat_calc_item_bonuses.
- u3: menu_0bb6 x9 (word diff vs title: only relocations), menu_0ba5 0x800d4400 (summary fixed)
  + new scroll_grid_group_begin / _end rows, message_box_nop (title 0x80073698 / menu_0ba5
  0x800dfc80: empty function called by message_box_show, not a vtable slot), menu_0ba6, credits
  DecDCTGetEnv / PutEnv / inSync / outSync / screen_create_arena, title libpress twins + new title
  0x8004ffec DecDCTGetEnv.
- u4: 39 field rows (actors, text panel, window_draw_ascii, fade, game over, preloads, party ids,
  music, disc prompt, side text).
- u5: 41 field rows (preloads, notices, messages, camera_reset, party_add_member, PA prompt,
  RotMatrixX/Y/Z, the voice queue incl. the Redux-checked build / update, battle load list /
  intro voices, field_prepare_encounter); 0x80057328 -> preload_table_dump.
- u6: field MulMatrix0, 0x800709e4 -> RotTrans, 0x80038538 / title 0x80038034 ->
  memcard_test_hw_events, debug info windows; title screen_clear_ot_r / screen_draw_ot_r,
  title_intro_destroy, set_textured_sprite, draw_vram_screen, draw_plate, window_draw_ascii,
  _patch_gte; #3101 BG anim, fx spawns / updates, glow, fieldactor_destroy.
- u7: title text box / panel guesses (cursor_draw_cell8, cursor_add_grid_rect, cvram_alloc8,
  upload_image_rect_ref / _xywh, text_box_add_string_xy / blank_line / sprite_and_character /
  status_values, text_box_draw_text_line, panel_create_member_box / info_box, panel_show_*;
  title 0x8006d31c = the empty panel_show_item_debug) + 13 uncatalogued menu_0ba5 copies and 4
  title panel getters / setter added; field callways rest, menu_scene_frame_end,
  entry_cache_load_0bd2.
- u8: field sprite_draw_part, sprite_cache_a_alloc / _b_alloc, sprite_resample (read in full
  against sprite_render.cpp), text_panel_create; #3101 BG accessors x15; #3102
  worldcam_target_from_angle, 0x8007ebdc -> worldcam_set_hold; deleted credits 0x80031c04
  DecDCTvlcBuild (the size-limit exit of DecDCTvlc, a branch target at 0x80031ab0).
- u9: field map_preload_entry, #3101 bg_set_target_packed, camscript_busy.

## State at the hand-back (11:55): complete, nothing running

All 264 start rows settled: every row of field, field_ovl_c1d, field_ovl_c1e, field_sub3103,
menu_0bb6, menu_0ba5, menu_0ba6, title, credits_3107 is `confirmed` (COVERAGE.md: field 796,
c1d 108, c1e 82, sub3103 35, menu_0ba5 252, menu_0ba6 20, menu_0bb6 72, title 751, credits 63).
About 45 rows renamed / corrected, 25 rows added (uncatalogued copies inside other rows'
extents), 2 bogus credits rows deleted. `tools/catalog_coverage.py` + `tools/catalog_gaps.py`
re-run under the lock: 0 missing addresses across 32 programs. No engine code changed.
Outside this assignment's module list but field-side, still `likely`: menu_0bb9 0x8008b338
model_blend, field_sub3109 0x801f9fe0 gauge_set_pos, menu_0bb2 0x800810a8 grid_box_place.
(All three confirmed later by the leads agent, docs/handoff/port_leads.md lead 5.)

## Port leads (for a follow-up agent)

1. field 0x80053b44 field_install_side_text: the original ends with load_queue_set_streaming(1)
   (and frees the source). The port's field_install_side_text does neither; post_battle.cpp
   (line ~184) calls set_streaming(true) after it, but engine/src/game/field/field_world.cpp
   (courier state, line ~712, state 0x1d -> 0x1e) does not, so voice-clip streaming stays off
   after a courier delivery until something else turns it on. Fidelity fix (one line), but it
   can change the voice-clip RNG use, so measure before / after.
   DONE (leads agent, docs/handoff/port_leads.md lead 4): field_install_side_text turns streaming
   on itself, as 0x80053b44 does; checked in Redux; RNG before / after recorded there.
2. field 0x80050f64 field_prepare_encounter (design difference, low priority): the console
   preloads only the battle entries that fit its heap (keeping 0x800761a4 free; the rest are
   negated and read by the battle) and computes the voice-clip budget from the read time of
   what it preloaded; the port assumes everything fits. Where the console's heap is short,
   the budget (and so the field clip picks' RNG use) would differ. Check with a Redux trace of
   0x80075998 after an encounter roll on a memory-heavy map before changing anything.

## Next rows (after u7, 11:20; all done by u9)

`python3 scratchpad/conf_field/rem.py MODULE` lists what is left: field sprite_draw_part,
sprite_cache_a_alloc / _b_alloc, sprite_resample (long, sprite_render.cpp), text_panel_create,
map_preload_entry; #3101 the 16 BG accessors 0x80089a50..0x80089b30 and camscript_busy (asm
dumps from w3.sh still coming); #3102 worldcam_target_from_angle / worldcam_set_field_32 (Ghidra
has no function at 0x8007ebdc: read the words); credits DecDCTvlcBuild.

## How to verify

`python3 scratchpad/conf_field/d.py MODULE ADDR` / `asm/<elf>/<addr>.s` against the port file in
the row's port column; twins with `python3 scratchpad/conf_field/twins.py`.
