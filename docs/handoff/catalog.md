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

## Step 2: gap scan (done) and fill (boot/battle/menu_0ba5/title done; field family clean)

Worker: a fresh catalog agent picked this up after the previous one was cut off; the gap
scan (`tools/catalog_gaps.py`, already written and already run once) and the "## Missing
rows" section of `docs/catalog/COVERAGE.md` were both already on disk, partially stale
relative to a mid-run edit. Rerunning the full scan (`python3 tools/catalog_gaps.py`, no
args) reproduced the same 273 total and confirmed nothing was lost.

**Caution for the next agent:** `tools/catalog_gaps.py NAME [NAME...]` *replaces* the whole
"## Missing rows" section with only the named programs' results (it doesn't merge). Always
rerun with **no arguments** after touching functions.csv, to regenerate the complete
section for all 32 programs. (I did this once by mistake mid-session; caught it immediately
because the section shrank to "0 missing... None found" and nothing was committed in
between, so a full rerun restored it exactly. Nothing was lost, but it's a sharp edge.)

### Gap-scan coverage (step 1 of my mandate)

Every program named in the task is now scanned and its real ("both"/"jal only"/"proc only",
tag `[fn]`/`[leaf]`) gap count is either filled or explained below. The programs with 0
real gaps (field, field_ovl_c1d/c1e, field_sub3103/3106/3108/3109, field_sub3104/3105,
battle_ovl1981, battle_ovl1984_0..10, menu_0ba6/0baa/0bae/0bb2/0bb6/0bb9, credits_3107) were
confirmed clean this pass too (field_sub3104/3105 and menu_0bb6 specifically had never been
scanned before and come back 0 missing + 0 weak; battle_ovl1984_2..10 likewise). The "j
only, weak" counts (4002 total) are noise per the method note at the top of "Missing rows"
and were not investigated further (matches the existing measured precedent for
battle_ovl1981 / credits_3107).

### Gap fill (step 2 of my mandate): 273 -> 1 real gap left

| Program | Found | Filled | Notes |
|---|---:|---:|---|
| boot | 122 | 122 | all added |
| title | 141 | 141 | 140 new rows + the 1 freebie (titled_box_update, see below) |
| battle | 1 | 1 | added at the function's real entry, see below |
| menu_0ba5 | 9 | 9 | all added |

**boot (122 rows, `docs/catalog/functions.csv` boot block, now 357 rows):**
- ~41 are standard PsyQ SDK functions (ResetGraph, SetDispMask, LoadImage, InitPAD/StartPAD/
  StopPAD, printf/puts/putchar, Load/Store/MoveImage2, Set*Env family, etc.) -- confirmed
  `confirmed`/`likely` by Ghidra's own signature match on the name (boot_procs.json), ported
  where `engine/src/runtime/libgpu.h` etc. already has the SDK name, `(not needed: ...)`
  otherwise (the port renders via OpenGL / reads input via SDL, not GPU packets / BIOS pad
  polling).
- ~37 are unnamed PsyQ library internals Ghidra tags `<LIB>_OBJ_<offset>` (no exported
  symbol recovered): 18 are bare `jr ra` padding (`(not needed: library padding, not
  code)`, same pattern as the existing `PATCHGTE_OBJ_DC` / `STRCMP_OBJ_5C` rows); the other
  ~19 have real bodies (the kernel's interrupt dispatcher, the GPU idle-wait/timeout pair
  behind `ResetGraph`, the pad VSync-read hook behind `InitPAD`/`StartPAD`/`StopPAD`, etc.)
  -- kept the Ghidra `_OBJ_` label as the name (no better one recoverable), `likely`/`guess`.
- **A private CD-ROM driver subsystem** (0x80025190-0x80026d90ish) that duplicates the real
  PsyQ `CdInit`/`CdControl`/`CdControlB`/`CdSync`/`CdReady`/`CdDataSync` behaviour almost
  exactly (same debug strings "CD init"/"CD sync"/"CD ready"/"CD datasync", same retry/
  timeout structure) at *different* addresses from the boot module's existing
  `CdInit`/`CdControl` rows (0x80025190/0x8002558c). Comparing signatures resolved the
  apparent name collision: the existing 0x8002558c row (2-arg, no result buffer, marked
  `likely`/`behaviour` -- i.e. never signature-confirmed) is **renamed `CdControlF`**
  (fire-and-forget), freeing `CdControl`/`CdControlB` for the real 3-arg, result-taking
  pair newly found at 0x800256b8/0x80025454. The existing 0x80025190 `CdInit` row is
  untouched (it calls this new layer transitively through `cdrom_subsystem_control`, so no
  collision there). This is the one existing-row edit in this pass; everything else is pure
  addition. All of this subsystem is `(not needed: no CD drive; the host reads the disc
  image synchronously)`.
- 11 rows (lz_unpack_bytes/words, stream_unpack_step, boot_resource_lookup, the 6
  boot_text_glyph_slot_* wrappers, invert_status_flag) have **zero resolved callers** in
  REA's `procedure_callers` -- checked explicitly, not assumed. Port left empty (not
  "not needed") since an unresolved function-pointer table could still reach them; noted in
  `docs/catalog/COVERAGE.md` under "Unported, reached in play" (the "checked: NOT reached"
  half) for the coordinator to judge.

**battle (1 row):** the gap scan's hit was 0x8007f8d0, but in this session's REA analysis
that address resolves *inside* the body of a function starting at 0x8007f818 (Ghidra's
function-boundary call differed from whatever pass produced the original procs.json dump).
Added the row at the real entry, 0x8007f818 (`voice_buffer_slot_acquire`), with a note
about the 0x8007f8d0 offset. **This means `tools/catalog_gaps.py`'s next run will still
report battle as "1 missing"** (it only checks exact-address hits, not containment) --
that's expected noise, not unfinished work; don't re-add a row at 0x8007f8d0 itself.

**menu_0ba5 (9 rows):** 3 of the 9 turned out to already have confirmed port-side names via
exact-address comments already in the engine: `scroll_grid_group_create` (0x800d4400, see
`engine/src/game/common/menu_widget.h`'s `ScrollGridGroup` comment), `add_message_line`
(0x800d7d74) and `draw_current_text` (0x800d9248), both cited by address in
`engine/src/game/common/menu_gfx.h`. The other 6 are guesses from decompilation alone
(cursor_glow_step/draw, gauge_anim_step/draw, bitmap_scale_blit_row, titled_box_update).
**Freebie:** `titled_box_update` (0x800dfc80, an empty TitledBox vtable slot) has an exact
byte-identical twin at the matching relative offset in **title**, 0x80073698 -- added both
in this pass since the menu_0ba5 investigation found it directly.

**title (140 rows, now 709 total):** mostly guesses from decompilation; a few highlights
with real evidence:
- `character_join_init_03`..`_12` (0x80062330-0x80062e34): matches
  `engine/src/game/title/title_program.cpp`'s own comment "Characters 3-12
  (0x80062330..0x80062e34) join in the field program".
- `get_attribute`/`snapshot_attrs_five`/`snapshot_equip`/`can_wear_item`/`equip_slot_of_item`
  (0x8003a8d8/0x8003a85c/0x8003aaa0/0x8003ab18/0x8003ab4c): matched by exact address to
  `engine/src/game/common/menu_stats.h`'s own comments.
- `title_intro_free` (0x8003cc88): matches `engine/src/game/title/title_program.cpp`'s
  comment "freed here (0x8003cc88, ...)".
- `menu_unused_page_noop_*` (17 rows, 0x80035980-0x80035a94): confirmed empty (bare `jr ra`)
  stubs inside the page-handler table range `engine/src/game/common/menu_scene.h` documents
  as "title links only pages 0 and 0x11" -- `(not needed: unused page handler)`.
- Everything else (the debug_screen_*/debug_menu_*/debug_card_screen_* family, ~20 rows;
  the textbox_*/clut_*/ot_ring_*/sprite_prim_* family; the member_*/bgload_* helpers) is
  `guess` confidence from decompilation alone, port left **empty** (not checked for reach;
  see `docs/catalog/COVERAGE.md` "Unported, reached in play" for which ones look
  developer-only vs. real gameplay code, and which weren't checked either way).

### Mechanics used (for the next agent, or to repeat elsewhere)

- Opened `analysis/elf/SO2_boot+main.elf` (covers **both** boot and title -- same ELF, two
  address ranges) via `open_binary`, then `batch_decompile` in ~35-address batches (bigger
  batches can exceed the tool's output token cap; if that happens, the response is saved to
  a file -- read it with Python/jq, not the Read tool).
- The REA session is shared across agents: re-open the binary before each batch if any other
  tool call reports a different `subject.name` (it happened twice this session, both caught
  by checking the result's subject).
- `scratchpad/catalog/fill_boot.py` and `fill_title.py` hold the exact row data used (kept
  on disk for reference/audit, not committed); both insert into the existing sorted block
  and `assert` contiguity before writing, re-reading functions.csv immediately before each
  write per the rules.
- Verified afterward: `csv.DictReader` over the whole file (4961 rows, 0 bad key sets, 0
  duplicate (module,address) pairs), each touched module's rows still contiguous and address-
  sorted, LF line endings intact.

### State

`docs/catalog/COVERAGE.md` regenerated in full (`tools/catalog_coverage.py` then
`tools/catalog_gaps.py` with no args): 4961 rows; 1 real missing address left across all 32
programs scanned (the battle 0x8007f8d0 containment false-positive, explained above, not a
real gap). Rows touched: 273 new rows (boot 122, title 140 + 1 freebie, battle 1,
menu_0ba5 9) + 1 renamed existing row (boot `CdControl` -> `CdControlF`) + 11 boot rows'
evidence cells appended with a "no caller found" note. No reordering; each module's block
re-sorted into itself only. Not committed (coordinator commits hourly; last checkpoint
during this session was `a10f166`, which already swept up step 1's work).

### Next steps for whoever picks this up

1. `docs/catalog/COVERAGE.md`'s new "## Unported, reached in play" section lists what's
   worth triaging first (confirmed-reached rows needing a port cite, vs. confirmed-dead
   code, vs. likely-developer-only code, vs. genuinely unchecked). Work through it before
   assuming any of the 123 empty-port rows from this pass are real gaps.
2. If `tools/catalog_coverage.py` runs again, it **overwrites the whole file** (not a
   marker-replace like `catalog_gaps.py`) -- the "## Unported, reached in play" section
   (and anything else hand-added after "## Missing rows") will be lost. Re-add it from this
   log, or teach `catalog_coverage.py` to preserve a trailing hand-written section the way
   `catalog_gaps.py` already does (read the old file first, keep anything from "##
   Unported" onward, re-append after writing its own sections).
3. The embedded map code (field_sub3103/3104/3105/3106/3108/3109) and the menu overlays
   0xba5-0xbb9 are now fully scanned with 0 real gaps each -- no further gap-scan work
   needed there, only the normal "fill in missing rows" cycle if functions.csv gains more
   rows later and someone reruns the scan.
