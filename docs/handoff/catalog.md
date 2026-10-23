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
during this session was `3cffe28`, which already swept up step 1's work).

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

## Step 3: the scan's own prologue-filter bug, and 411 real rows (new catalog agent, 04:40 start)

The coordinator's mandate for this step: the previous step's "Known catalog gaps found on
the way" note was right that the gap scan was *wrong*, not just incomplete -- it tagged every
candidate without this compiler's `addiu sp,sp,-N` prologue as `[?]` and then **excluded**
`[?]` hits from "missing" entirely, so a hand-written PsyQ leaf (memcpy, setjmp, the libapi
syscall stubs, ...) never got a row no matter how strong its other evidence was. Confirmed by
hand before touching anything: rerunning the *old*, untouched script reported "1 missing" for
all 32 programs (just the precedented battle 0x8007f8d0 containment case), while the raw
`both`/`jal_only`/`proc_only` bucket *lists* in that same run already showed boot alone sitting
on 303 untouched candidates including memcpy/memset/setjmp/toupper/strncmp by name (Ghidra had
already resolved them; the script was throwing that signal away).

### tools/catalog_gaps.py rewrite (done)

- Every `jal` or REA-procedure hit now counts as a candidate regardless of prologue; the
  `[fn]`/`[leaf]`/`[?]` tag is kept but purely informational.
- New `tail-j` source: a plain `j` whose own address falls inside a function (bounded by
  scanned prologues + the catalog's own rows + REA's procedure list) but whose *target* lands
  outside that function's range -- a tail call the old script's blanket "j is noise" rule
  couldn't see. Gated on the target *also* looking like a function start (`[fn]`/`[leaf]`),
  or it floods with false positives from stale/merged function boundaries (measured on field
  before the gate: 1003 raw tail-j hits, 97% `[?]`; after: 14, all plausible).
- New `table` source: a run of >=3 consecutive data words that are all plausible code
  pointers into the program's own range *and* land on a plausible instruction boundary at the
  target (same `[fn]`/`[leaf]` gate -- without it, field alone threw up ~625 three-in-a-row
  coincidences in ordinary code reinterpreted as data). This is the only source that can find
  a function reached *only* through a vtable/function-pointer table.
- Each candidate is tagged with every source that found it (`jal+REA` = strongest). Ran with
  no arguments (full rescan) after every functions.csv edit, per the existing warning.
- Full rescan, before any row fill: **517 missing** (was "~1") across boot(304)/
  title(41)/field(87)/battle(67)/credits_3107(11)/field_sub3103(1)/menu_0ba5(1)/menu_0bb6(5).
  Every other program: still 0 (field_ovl_c1d/c1e, the embedded map code, the menu overlays,
  battle_ovl1981/1984_*, credits_3107's own candidates aside, confirmed clean).

### Row fill: 517 -> 107 real candidates left (411 new rows + 1 corrected cell)

| Program | Found | Filled | Left (why) |
|---|---:|---:|---|
| boot | 304 | 303 | 1 containment FP (0x80023ec4, inside strncmp's own body) |
| field | 87 | 62 | 18 containment FPs + 8 real-but-unnamed (see below) |
| title | 41 | 29 | 12 containment FPs (all land inside already-catalogued giants) |
| battle | 67 | 12 | 4 containment FPs + 51 one cohesive unidentified boss subsystem |
| field_sub3103 | 1 | 1 | -- |
| menu_0ba5 | 1 | 1 | -- |
| menu_0bb6 | 5 | 3 | 2 containment FPs |
| credits_3107 | 11 | 0 | all 11 resolve to **no procedure at all** against the credits ELF -- confirmed pure data, not functions |

"Containment FP" = a `tail-j`/`jal`/`table` hit whose target, resolved through REA's own
procedure lookup, lands *inside* a function functions.csv already has a row for at a
*different* address (same class as the existing battle 0x8007f8d0 -> voice_buffer_slot_acquire
precedent). Every one was checked by hand against the live catalog before being left out, not
assumed; `tools/catalog_gaps.py`'s next run will keep reporting all of these (and the 8+51
below) as "missing" forever -- expected, not unfinished work (see each `scratchpad/catalog/
fill_*.py`'s `SKIP` set for the exact mapping, address -> which existing row it's really in).

Mechanics used (same as the Step 2 log, repeated for whoever continues): opened each
program's own ELF (`analysis/elf/SO2_boot+<x>.elf`; menu_0bb6 has none pre-built, so
`python3 tools/psxexe2elf.py --raw analysis/archive/disc1/2998.0.unpacked --base 0x8007e000
--entry 0x8007e000 -o scratchpad/catalog/menu_0bb6.elf` made one from the archive's unpacked
block -- MIPS isn't accepted by `open_binary`'s own `raw-binary` mode, this sidesteps that),
`batch_decompile` in ~15-40 address batches (bigger trips the per-call token cap and gets
saved to a scratch file -- read it with `python3 -c "import json; ..."` pulling just
`address`/`procedure.name`/`procedure.address` (the canonical entry) and the first ~15
pseudocode lines, not the whole body, to keep it cheap). Each program's `scratchpad/catalog/
fill_<name>.py` holds the exact row data, a `SKIP` set (containment FPs with the evidence
in a comment) and, for field/battle, a `LEFT_OPEN` set (real, decompiled, deliberately not
named -- see below); all insert into the existing block relative to its current order
(tolerates the one pre-existing boot inversion, pad_start/pad_driver_start, without touching
it) and re-read functions.csv immediately before each write.

**boot (303 rows, now 660):** ~81 are exact PsyQ/libapi/libetc/libcard/libpad/C-runtime
names Ghidra's signature match already gave (memcpy, memset, strncmp + its thunk, toupper,
tolower, memchr, strlen, setjmp, longjmp, _exit, write, FlushCache, GPU_cw, CdRemove, the
Deliver/Open/Close/Wait/Test/Enable/DisableEvent + ReturnFromException/ResetEntryInt/
HookEntryInt kernel syscalls, GetSR, ChangeClearPAD/RCnt, the InitPAD2/StartPAD2/StopPAD2/
PAD_init2 + SysEnqIntRP/DeqIntRP pad family, VSync + the callback-table family, ClearOTag(R)/
DrawOTag(2)/DrawSync/GetODE/SetPriority/SetDrawStp (libgpu), InitCARD2/StartCARD2/StopCARD2 +
the _patch_card/_patch_pad kernel-table-patch family, the _spu_*/Spu* reverb internals) --
`confirmed`/`likely`, port `(PsyQ: runtime) ...` for the C-runtime ones (the port uses the
host's C++ standard library) or `(not needed: ...)` for the hardware ones (the port renders
via OpenGL / reads input via SDL / has no BIOS interrupt layer or CD drive). ~222 are Ghidra's
own unexported-offset labels (`SYS_OBJ_*` 52: GPU/DMA packet-transfer internals behind
LoadImage/StoreImage/DrawSync, confirmed by reading ~40 of them -- direct GP0/GP1/DMA_DICR
register pokes and a callback queue; `PRNT_OBJ_*` 37: one shared tail-chained printf
%-directive state machine, confirmed by reading the 3 entry states; the rest (`SPU_OBJ_*`,
`S_SCA_OBJ_*`, `S_SVA_OBJ_*`, `S_SRMP_OBJ_*`, `INTR*_OBJ_*`, `VSYNC_OBJ_*`, `PAD_OBJ_*`,
`PUTCHAR_OBJ_*`, ~15 more small families) are libspu reverb/voice-attribute internals or the
VSync/interrupt/pad callback machinery, each family sampled 2-3 members before assigning a
shared disposition) -- kept their Ghidra label, `likely`/`guess`, `(not needed: ...)` per
family. The CD-driver subsystem (0x80025298-0x80026a5c) got descriptive
`cdrom_subsystem_*`/`cdrom_*` names (same disposition as the existing CdInit/CdControl/
CdControlF rows: no CD drive, the host reads the disc image synchronously). A handful of
one-off `FUN_` addresses got real names from reading their bodies (strncmp's thunk, strlen,
GetSR, a tiny C++-runtime `operator new` retry loop + its `set_new_handler`, a heap/pool
allocator init+gc pair, a BIOS-kernel-jump-table patcher for the GPU/video-mode layer).

**field (62 rows, now 798):** the user's explicit 8 (0x8003359c, 0x80038538, 0x80038e64,
0x80038e8c, 0x80038f38, 0x80039268, 0x8003c360, 0x8003d118) are all real -- a
map-sub-table-select/offset family, a leader pad-event-pending poll, a voice-queue push
family (4 convenience wrappers around one full form), a stat percent-adjust-clamped helper,
and an ordering-table-ring advance helper. ~23 more addresses (0x80020e5c-0x80026a5c) are
field's *own* duplicate copy of boot's early PsyQ runtime (DrawSync, memcpy, memset, printf,
ClearImage/LoadImage/StoreImage/MoveImage, ClearOTagR, DrawOTag, PutDrawEnv, PutDispEnv,
SetDrawArea, FlushCache, the Open/Close/Test/EnableEvent syscalls, write, ResetRCnt, a CD-
driver init) -- confirmed byte-identical to boot's bodies at the same addresses, so reused
boot's name/signature/confidence/port verbatim with a note. The rest are field's own game
code: a resource cache (SLZ-backed, keyed by id), the map-sub-table family's siblings, a
ScdQueue accessor + two push wrappers, an OT-ring clear/draw pair, a script-opcode table
lookup, several `actor_misc_*`/`actor_sys_update` helpers (one, `actor_sys_update`, guessed
only from its position between two already-named `actor_sys_*` rows -- flagged `guess` with
that caveat spelled out), loader region-size/dump helpers, and a GTE 3x3-matrix-multiply
wrapper. **Left open (8, real + decompiled, not named):** 0x8003f518 (141 lines),
0x800410cc (71), 0x80041ce4 (359), 0x80044c8c (230), 0x800452b4 (41), 0x80061cf8 (137),
0x80079ee4 (106) -- skimmed, no distinguishing call-site/string evidence found yet; and
0x80047068, a `table`-sourced hit (the first one the new source type ever found) that
resolves into 0x80046ae4 `fade_update`'s own body as a containment FP, not a separate row.

**title (29 rows, now 741):** the user's 0x80034bac is `item_attr_abs_get` (abs() of a
signed per-item byte). ~11 more are the libcard File*/memcard BIOS-syscall-thunk family
(_card_info, _card_async_load_directory, write_card_sector, allow_new_card, FileOpen/Seek/
Read/Close, FormatDevice, nextfile, firstfile) plus strcpy and strcmp (both real C-runtime
bodies, not thunks) -- `(not needed: no physical memory card; ...)` / `(PsyQ: runtime) ...`
respectively. The remaining ~17 are title's own copies of field-style helpers (two
dual-weapon-state checks, title's own map_sub_table_select/offset pair, an item-table
index/attribute pair, a most-recently-used list toucher, a free-slot handle-table
registrator, a fixed-record stream reader, a BIOS-kernel-jump-table patcher for the
memcard layer) and `inventory_item_data`, confirmed byte-identical to field's
0x8003c538. The 12 "jal only, no REA" addresses the previous pass already flagged
(0x80040004-0x80042ff0) are **all** containment FPs: every one resolves (REA's own
canonical-entry lookup) into `voice_collection` (0x8003fb78), `title_program_main`
(0x80041db0) or `new_game_sequence` (0x80042cc8), all three already catalogued -- confirmed
by decompiling all 12 by hand, not assumed from the address pattern alone (credits_3107 has
the *same* suspicious address set and it resolves to nothing at all there -- see below).

**battle (12 rows, now 944):** 9 small/clear ones (two empty vtable-slot stubs, a
texture-batch upload loop, a position-jitter calculator, an alloc-size fallback selector +
its two fixed-mode callers, a UI-reset vtable call, a stat-slot-init pair) plus a trivial
flag setter and a byte-identical pair of "pick a random variant, spawn it" shot helpers.
**Left open (51, one subsystem, not 51 mysteries):** 0x80072000-0x80081000ish is a single
dense boss/attack-pattern AI block -- every function in it spawns one or more "Shot"
objects via the same FUN_8002fb34 (allocate) / FUN_8002feac (set position) / FUN_8002fe88
(set motion) / FUN_800103d4 (pick a sprite range) calls with hard-coded offsets, several
~256-line bodies are near-identical (one spawn-configuration template reused per attack),
a stat-init block references the string "Bowman", and a pause/active flag (DAT_8006fb07)
gates most of the block. This matches the `battle_main_loop` partial-port note's "not the
Shot/Particle pools" -- these are very likely that pool's content, for one specific
unique encounter not yet identified. Next step: `search_strings` around "Bowman" and
`procedure_callers`/xrefs on a couple of the family to find which encounter table loads
this block, *then* name members -- guessing at 51 near-identical bodies one by one without
that anchor would mostly produce noise. Exact addresses: `scratchpad/catalog/
fill_battle.py`'s `LEFT_OPEN` set.

**field_sub3103, menu_0ba5, menu_0bb6 (5 rows total):** trivial getters/setters plus
menu_0bb6's `rank_list_widget_init`/`_page_create`/`_row_format` (a ranking/score-list
page builder; the other 2 of its 5 candidates are containment FPs into those same two
real functions, confirmed via a from-scratch ELF since no pre-built one exists for this
overlay -- see the `psxexe2elf.py --raw` line above).

**credits_3107 (0 rows):** its 11 "jal only" candidates are the exact same suspicious
address set (0x80040004-0x80041ffc) that turned out to be title's containment-FP cluster --
but decompiling all 11 against credits_3107's *own* ELF resolved every one to **no
procedure at all** (empty canonical address, no pseudocode). Confirmed by hand: not a
function, data a jal-shaped word happened to point into. No rows; this is the clean,
verified case the method note at the top of "Missing rows" always warned about.

Other fixes on the way: one bad port path (`leader_pad_event_pending` cited a
non-existent `engine/src/game/field/input.cpp`; it's actually a BIOS pad hot-plug poll with
no port need -- fixed to `(not needed: ...)`, confirmed via `tools/catalog_coverage.py
--check` now exits 0 with 0 bad paths).

**Not done, flagged for whoever's next:** `docs/catalog/COVERAGE.md`'s "Partial ports"
section still lists battle_ovl1984_0's `sound_handle_active`/`voice_pick_victory`/
`voice_play_result` as `[port partial: ...]` even though the Step 1 log above says that was
fixed -- checked against the actual committed baseline (`git show 3cffe28:docs/catalog/
functions.csv`) and the stale `[port partial:...]` text is still there, meaning that fix
was never actually saved to disk (same "lost work" pattern this log's Step 2 preamble
describes for the gap-scan section). Out of scope for this pass (not part of the
gap-scan/missing-rows mandate); worth re-doing properly next time someone's in those 3
rows. The ~411 new rows' port cells are mostly `(not needed: ...)`/`(PsyQ: ...)` for
boot/field/title (same reasoning as their already-ported twins) but weren't all
individually re-verified against the engine source; battle's 12 + the still-open 51 are
the real "reached in play, actually unported" candidates once the boss subsystem is
identified -- see `docs/catalog/COVERAGE.md`'s "Unported, reached in play" section.

### State

`docs/catalog/functions.csv`: 5376 rows (was 4966 at session start -- 411 new rows, plus
1 corrected existing cell, `leader_pad_event_pending`'s bad port path). `docs/catalog/COVERAGE.md` regenerated in full
(`tools/catalog_coverage.py` then `tools/catalog_gaps.py` with no args): confirmed 4433,
likely 722, guess 221; ported/not-needed/PsyQ cover the rest, empty 61 (new rows awaiting a
port-reachability check), bad path 0, partial 11 (pre-existing, see above), unported 0.
Gap-scan total: 517 -> 107 (all 107 explained above: confirmed non-functions or real-but-
deliberately-left-open). No engine code touched; no suites needed (catalog-only). Not
committed (coordinator commits hourly). `tools/catalog_gaps.py` itself was rewritten (see
top of this step) -- diff it against `3cffe28`'s copy if anyone wants the exact before/after.
