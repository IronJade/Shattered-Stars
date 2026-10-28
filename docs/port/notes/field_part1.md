# Field program, part 1: 0x8002f810–0x80040c2c

Covers the 262 functions of `analysis/decomp/field/index.csv` in this range, plus 33 functions
Ghidra missed. Those 33 are in the gaps between indexed functions and are appended to `part1.csv`
with evidence `GAP (not in index.csv)`. Addresses are field-module addresses (entry #3100 at
0x8002f810). "vtable[n]" means the n-th 8-byte entry `{s16 delta, s16 0, u32 fn}`: entry n is at
`vptr + 8*n`, so vtable[1] is `+0x8/+0xc`, vtable[2] is `+0x10/+0x14`, and so on.

## What this range contains

| Range | Module | Notes |
|---|---|---|
| 0x8002f810–0x8002f9b0 | program entry, Program base, render-context arena helpers | |
| 0x8002f9e8–0x800303d4 | **MenuScene**: the menu's own frame loop | the camp/shop/etc. menus run *inside* the field program |
| 0x80030470–0x80030c64 | **MenuLauncher** (a Program subclass), plus menu entry points per kind | |
| 0x80030ce0–0x800313c0 (gap) | auto-equip / equipment-effect scan | |
| 0x800313c0–0x80032360 | **Callways**: always-running party effects (field songs, walking regen) | class name from the string `Callways::ERROR` |
| 0x800323dc–0x80032aec | StatChange wrappers, **MenuSession**, resident entry cache | |
| 0x80032b6c–0x80032f58 | debug menu window (VIEW VRAM / VIEW MEMORY) | |
| 0x800331b8–0x8003446c | party selection, **stat codes**, specialties, item data access | |
| 0x80034514–0x80034c00 | MenuFieldQuery, play-time/debug text drawing | |
| 0x80034dd0–0x800360a4 | **MenuPageLoader**: menu page state machine and code paging | |
| 0x800360cc–0x8003652c | string utilities and a tiny printf (StrBuf) | |
| 0x80036578–0x80036d1c | **WindowList** (the menu's task list) | |
| 0x80036df0–0x8003863c | **MemCard** manager (BIOS card events and file I/O) | |
| 0x8003872c–0x800387b8 | **Task** base class | |
| 0x80038b48–0x8003979c | **background loader** (load queue) and party voice-bank preload pump | |
| 0x800397a4–0x80039f6c | **StatChange**: HP/MP/money edits with clamping | |
| 0x8003a0ac–0x8003b1a4 | debug info window and Window base class | |
| 0x8003b1ac–0x8003c2c0 | **StatCalc**: derived stats from equipment and equipment effects | |
| 0x8003c538–0x8003c928 | **Inventory**, with an anti-tamper checksum | |
| 0x8003c9d0–0x8003d4d8 | **screen module**: byte-for-byte the title's `screen_*` code at title address + 0xf5c | |
| 0x8003d4d8–0x80040bd0 | **Actor** core: init, update, animation, turning, colour fade, OT linking, command queue | continues past 0x80040c2c (part 2) |

Most menu-related functions here have no caller in the field image. They are an API that the
menu overlays call at fixed addresses: menu code at 0x800d1b28 and page code at 0x8007e000.

---

## Program base, FieldProgram, MenuLauncher

- **Program** (base): vptr at +4, base vtable at **0x8002f8c4**: [1] = 0x8001446c (boot), [2] =
  `program_set_system`. The ctor `program_create` (gap 0x8002f8a4) sets **g_current_program**
  (0x8002f8dc), and `field_entry` uses that pointer. +8 holds the System pointer.
- **FieldProgram** vtable 0x800730a4: [1] run = FUN_80048a84, [2] = `program_set_system`.
- **MenuLauncher**: 0x3c bytes, vtable **0x80071670** ([1] `menu_launcher_run_nop`, [2]
  `program_set_system`), global DAT_800752d4. Its ctor makes the launcher g_current_program. The
  field update (FUN_8004c334) sets it back to the field program when the menu returns.

| Off | Type | Meaning |
|---|---|---|
| +0x04 | vptr | |
| +0x08 | System* | |
| +0x1c | int | base archive entry (kind 1-5 gives 0xba4, 0xba8, 0xbac, 0xbb0, 0xbb4) |
| +0x20..+0x2c | int[4] | sizes of entries base..base+3 |
| +0x30 | int | load state (1/4 wait loader, 2 size+alloc, 3 read, 5 done) |
| +0x34 | int | total bytes |
| +0x38 | void* | read buffer (the 4 entries back to back) |

Sequence (field update state 3): `menu_launcher_start(kind)`, then `menu_launcher_load_step`
every frame until the loader is idle. A `menu_run_*` call then does `menu_launcher_install`
(unpacks the 4 blocks), builds a MenuSession and a MenuScene on the stack, runs the loop, and
destroys both.

| Field mode (0x80076196) | Kind / entries | Function | Session start page |
|---|---|---|---|
| 0 / 1 (menu button; table_a 0x3d) | 1 / 0xba4-0xba7 | `menu_run_camp` | 1 |
| 2 (table_b 0x6d, arg) | 2 / 0xba8-0xbab | `menu_run_select`; result goes to script R | 0xb |
| 3 (menu button with script flag 0x14) | 3 / 0xbac-0xbaf | `menu_run_page0f(slot)` | 0xf, sub-mode 0xb |
| 4 | 4 / 0xbb0-0xbb3 | `menu_run_page06(slot)` | 6 |
| 5 (table_b) | 5 / 0xbb4-0xbb7 | `menu_run_page11` | 0x11 |

## MenuScene (stack object, 0x40 bytes, no vtable)

Global **g_menu_scene** 0x8007405c (`menu_scene_get`).

| Off | Type | Meaning |
|---|---|---|
| +0x04 | RenderContext* (0x10 bytes) | +4 arena slot, +8 OT slot, +0xc current OT head |
| +0x0c | WindowList* | the menu's windows/pages |
| +0x10 | MenuPageLoader* | |
| +0x14 | MemCard* | created unless +0x18 is set |
| +0x18 | s16 | "no memory card" (never set in this range) |
| +0x1a | s16 | input disabled |
| +0x1c / +0x20 / +0x24 | u32 | held / repeat / pressed, each reduced to its lowest set bit |
| +0x28 | int | VSync() return value |
| +0x2c | int | frame counter |
| +0x30 | int | VSync mode 0-4, giving VSync(0/1/2/10/60) |
| +0x34 | u_long* | saved VRAM (0x200,0x100) 64x128 (0x4000 bytes) |
| +0x38 | u_long* | saved VRAM (0,0x1e0) 320x16 (0x2800 bytes, the CLUT rows) |

Frame (`menu_scene_run`):
`menu_scene_frame_begin` → `menu_pages_update` (returns 1 to exit) → `window_list_update` →
`menu_scene_frame_end`. Frame begin: screen_flip, clear the OT, run Callways vtable[1] (it keeps
ticking inside the menu), `bgload_update`, then read the pad. Frame end: MemCard vtable[5],
DrawSync, VSync, screen_put_env, screen_draw_ot.
**DAT_80074058** is a debug progress marker (stages 1-0xe) written through ctor, loop and dtor.

VRAM traffic in the ctor/dtor (the port must preserve the field's VRAM across the menu):
MoveImage (0x180,0x100,64x256) → (0x140,0x100); StoreImage of the two blocks above; upload menu
TIM 0 to (0x180,0x100,64x128) with 16 CLUTs at (0x100,0x1e0..); upload TIM 1 to
(0x180,0x1c8,64x48) with CLUTs at (0x110,0x1e0..); copy CLUT rows (0x100,0x1ec+i) to
((i+1)*16, 0x1e4+j). The dtor moves the 64x256 block back and reloads both saved blocks.

## MenuSession (0x38 bytes, global g_menu_session 0x80074074)

`menu_session_create` reloads handoff pointers 2/6/3/4 (any function that needs the party can
build one; `party_get_specialty_level` does). It allocates **handoff 0xe** (0x440 bytes) if
absent and frees it at the end unless +0x16 is set.

| Off | Meaning |
|---|---|
| +0x00 | heap used at session start (leak check in the debug window) |
| +0x08 | buffer freed at end |
| +0x12 | s16 menu argument (`menu_run_select` arg) |
| +0x14 | current page id (published for overlays) |
| +0x16 | exit code 1, and keep handoff 0xe |
| +0x17 | set by `menu_run_page06` |
| +0x19 / +0x1a | cached entry 0xbd2 / 0xbbb loaded |
| +0x1b | exit code 2 |
| +0x1c | sub-mode (0xb from `menu_run_page0f`; 4 or 0xb select page 0xf code 0xbae instead of 0xbc0) |
| +0x1d | start page |
| +0x1e | s8 result (`menu_run_select`) |
| +0x20..+0x2f | button map: [i] = index of key binding i in the default table 0x800717b0 `{0x20,0x40,0x10,0x80,4,8,1,2}` |
| +0x30 / +0x35 | exit codes 5 / 6 |
| +0x31 | create MenuFieldQuery (g_field_query 0x80074090, vptr at **+0xc** = 0x800719e0) |
| +0x34 | on destroy set inventory+0xc24 (reset the recent-items list) |

## MenuPageLoader (0x70 bytes, MenuScene+0x10)

| Off | Meaning |
|---|---|
| +0x00 | s16 page (state) |
| +0x02 | s16 substate: 1 create page, 0 running, 2 page closed (pop), 3 page returned a choice |
| +0x04 | s16 choice |
| +0x08 | BgLoadClient (Task, vtable 0x80071c78), owner of this loader's loads |
| +0x10 | s16 stack depth |
| +0x14 | current page window |
| +0x18 + 8n | stack: {Window* @+0, s16 page id @+4} |
| +0x58 | code destination = 0x8007e000 |
| +0x5c | packed buffer (code + data entries back to back) |
| +0x60 | currently resident code entry |
| +0x64 / +0x68 | size of code entry / data entry (entry+1) |
| +0x6c | s16 started |

`menu_pages_update`: when no window has focus and all of the owner's loads are done, unpack +0x5c
to 0x8007e000 and the second block to g_menu_data (0x80074064), then flush the cache. Then run the
page handler. A handler, on substate 1, `new`s the page window (size and ctor in the page code at
0x8007e000+) and pushes it. When the focused window is the current page and its +0x2e is nonzero,
+0x2e becomes the substate and +0x30 the choice. Page 0x14 means close.

Page code entries (`menu_pages_request`), each a pair (code, data):

| Page | Entry | Page | Entry |
|---|---|---|---|
| 1 | 0xba6 | 11-13 | 0xbaa |
| 2 | 0xbbc | 14 | 0xbce |
| 3 | 0xbbe | 15 | 0xbc0 (0xbae for sub-mode 4/0xb) |
| 4 | 0xbc4 | 16 | 0xbc2 |
| 5 | 0xbc6 | 17 | 0xbb6 |
| 6 | 0xbb2 | 19 | 0xbcc |
| 7 | 0xbc8 | 20 (close) | **0xc1d / 0xc1e** single block (field_flags+0x68 chooses) |
| 8-10 | 0xbca | | |

Page 20 reloads the **field's own 0x8007e000 overlay** (entry 0xc1d, or 0xc1e when field_flags+0x68 is
set) that the menu pages overwrote. The same flag picks the actor class: FUN_80043890 builds
0x3fc-byte actors with ctor 0x8007e540, or 0x44c-byte actors with ctor 0x80082e5c.

Main menu (page 1) choices 0-12 lead to pages 2, 3, 4, 5, 6, 7, 0x11, 8, 9, 10, 11, 12, 13. Page 3
choices go to 0xf, 0x10, close, 0x13. Page 2 goes to 0xb and page 6 to 0xe. What each page is (item, skill,
equip, ...) is only visible in the page code entries, which are not analysed yet.

## Task / Window / WindowList

- **Task** base vtable **0x80071ca8**: [1] dtor `task_destroy`, [2] `task_type` (returns 0), [3]
  `task_close` (calls [4]), [4] `task_on_close` (empty), [5] `task_update` (empty).
- **Window** base vtable 0x80071e64 (`window_base_create`, gap 0x8003b0d0): [2] returns 0x13,
  [4] `window_close_self` = `window_list_kill(owner->+0x10, self)`, owner at +8.
- **WindowList**: 0x18 bytes, vtable 0x80071b38, Task-derived. +8 entries (12 bytes
  `{Window*, int layer 0-4, u32 flags}`), +0xc count, +0x10 focus, +0x14 capacity (grows by 20).
  Entry flags: 1 active, 4 dying, 8 dead, 0x10 cannot take focus.
  `window_list_update` calls vtable[5] on each active entry, layer by layer from 0 to 4, then
  purges and compacts. The focus is the last active entry without 0x10. The page loader reads the
  focus to know whether a page is still open.

## MemCard (0xfd0 bytes, vtable 0x80071c18, global g_memcard 0x80078850)

Created by `menu_scene_run`. vtable[5] = `memcard_update` runs every menu frame.
It opens SwCARD (0xf4000001) and HwCARD (0xf0000011) events for EvSpIOE (4), EvSpERROR (0x8000),
EvSpTIMOUT (0x100) and EvSpNEW (0x2000) at +0xfb0..+0xfcc. `pad_set_reset_callback`
(0x8001445c) installs `memcard_reset_callback`, which closes them on a soft reset.
+0xfa4 is the port being serviced (alternates each frame), +0xfa6 bit 0 marks a port step done,
+0xfa8/+0xfac hold the save buffer and size.

Port block (2 of them, 0x7cc bytes each, at +0xc):

| Off | Meaning |
|---|---|
| +0x000 | 15 x 0x80 save summaries (bytes 0x200-0x27f of each save file) |
| +0x780 | u16 flags: 1 check card, 2 new card, 4 card info, 8 file op, 0x1000, 0x2000 check save, 0x4000 none, 0x8000 waiting for event |
| +0x782 | u16 result code (0x1140, 0x1141, 0x2281, 0x2282) |
| +0x784 / +0x786 | port 0/1, channel 0x00/0x10 |
| +0x788 | file-op state (0x14 card info, 0x15 format, 0x16-0x1e write) |
| +0x78a / +0x78c / +0x78e | file count / free blocks / save count |
| +0x790 | s16 slot table[15]: save number, or negative if free |
| +0x7b0 | fd; +0x7b4 slot; +0x7b8 open mode (0x8001 read, 0x8002 write, 0x18200 create 1 block) |
| +0x7bc / +0x7c0 / +0x7c2 | buffer / bytes (rounded up to 128) / file offset |
| +0x7c4 | retries (10); +0x7c6 loop index; +0x7c8 data pointer |

File name: `"bu" + port(%1d) + "0:" + "BASCUS-94421S02-S" + "%02d"`. The directory is scanned with
`bu<p>0:*` (free = 15 − Σ size/8192) and `bu<p>0:BASCUS-94421S02-S??`. A save is 1 block. Bytes
0x200 + 0x80 hold the summary, read for every slot; a write puts 0x5555 at summary+0x18. Bytes
0x280 + 0x100 are also read. The save title at 0x800740b0 starts `"STAR OCEAN "`.

## Background loader (globals 0x80078854-0x8007886c)

| Global | Meaning |
|---|---|
| 0x80078860 | LoadRequest array (0x14 bytes each), grows by 10 |
| 0x80078858 | s16 capacity |
| 0x80078854 | s16 pump state (0 idle, 1 start, 2 select, 3 read, 4 poll, 5 cancel, 6 done) |
| 0x8007885c | s16 active index |
| 0x8007886c | idle frames before the voice preload may resume (60) |
| 0x80078864 / 0x80078868 | voice-preload state (0-3) and its parameter (copies of 0x800759a0 / 0x80075998) |

LoadRequest: `+0 u16 flags (1 used, 2 pending, 4 reading, 8 owns buffer, 0x20 cancel)`,
`+2 s16 entry`, `+4 void *dst`, `+0xc s16 sector offset (0x80011988 seek inside the entry)`,
`+0xe s16 sectors`, `+0x10 owner`. Handles are index+1.
`bgload_update` serves one request at a time through the boot Loader. When the loader has been
idle for 60 frames and field_flags+0x25 is set, it steps the **party voice-bank preloader**
(0x80078e0c..0x80079a7c, part N), which streams battle voice banks of the party into SPU RAM. It
aborts that preload before each read.

## Callways (0x24 bytes, vtable 0x80071780, global DAT_800752e0)

Registered with the field task registry (`FUN_80056270(obj, 1)`), so vtable[1] `callways_update`
runs every field frame, and inside the menu too.

| Off | Meaning |
|---|---|
| +0x08 / +0x09 / +0x0a | song modifiers 13 / 12 (bool) / 14 |
| +0x0b + i | per-party-member equipment flags: bit0 regenerate HP while walking (effect 0x84), bit1 EXP while walking (effect 0x8b), set by `stat_calc_scan_effects` |
| +0x14 | holder of the saved sound bank (0xb14 bytes) while a song plays |
| +0x18 | s16 song level |
| +0x1a | s8 song id (0 = none) |
| +0x1b | step tick, set by `callways_step_tick` from the field update |
| +0x1c | step counter 0-99 |
| +0x20 | last clock value (`FUN_80014434`) |

**Field songs**: `callways_play_song` is called by menu overlay code. It saves the sound bank,
plays a song from its own SLZ data at field-BGM buffer (DAT_80075734, 0x4800 bytes) + 0x4000, and
applies modifiers by song id (jump table 0x80071708). When the music stops, `callways_update`
restores the bank and resumes field BGM (`0x8001a65c(sound, bgm, 0x3c, 0)`). BGM changes, map
loads and exits stop the song first (`callways_stop_song`).
Modifiers (`callways_set_mod`) write the config block (handoff 2): 1 HP%/MP% (+0x184/+0x186,
used by `stat_calc_member`), 2-9 battle values +0x188..+0x196, 10/11 encounter modifier
(field_flags+0x84, ±50; script flags 0x24/0x26 add ±10), 12-14 Callways +9/+8/+0xa.

Song id → modifiers (`callways_song_mods`, gap 0x80031eb0, table index = id − 2; every
modifier value is level×5, from `callways_set_mod_scaled`):

| Song | Effect | Song | Effect |
|---|---|---|---|
| 1 | heal HP+MP by level per step (`callways_song_heal`) | 9 / 10 | mod 10 / 11 (encounter + / −) |
| 2 | mods 4, 5, 6, 2, 3 | 11 / 12 | mod 2 / 3 |
| 3 / 4 / 5 | mod 4 / 5 / 6 | 13 | `callways_song_summon(2)` (random battle from table) |
| 6 | mod 1 (HP/MP max %) | 14 | mod 13 |
| 7 | `callways_song_summon(1)` | 15 / 16 | heal HP / MP per step |
| 8 | mod 12 | 17 | mods 12, 13, 14 |

Per frame it also applies inventory effects: each clock tick, money −= count of item 0x1da; every
60 ticks, money += count of item 0x1e0. On a step tick, members with flag 1 heal 3% HP every 5
steps and members with flag 2 gain 1 EXP (slot+0x10, capped at 999,999,999).

## Party, stats, specialties

Selection: `party_select(slot)` sets **g_sel_slot** 0x8007407c (party slot, 0x60 bytes),
**g_sel_record** 0x80074080 (character record, 0xd0 bytes) and **g_sel_index** 0x80074078.
Almost every stat helper works on the selected member and restores the selection.

**Stat code** (`stat_get` / `stat_set`): bits 0-4 group, bits 8-11 index. The group table
0x80071968 holds per-group flags (bit0 = in record, not slot; bit1 = 32-bit). Offsets come from
`stat_group_offset`:

| Group | Where | Index 0/1/2 |
|---|---|---|
| 1 | slot+0x14, u32 | HP base / max / current (+0x14/+0x18/+0x1c) |
| 2 | slot+0x20, s16 | MP base / max / current (+0x20/+0x22/+0x24) |
| 3-8 | slot+0x2a, +0x30, +0x36, +0x3c, +0x42, +0x48 | attribute base / with equipment / final |
| 9, 10 | record+6, record+0 | two more attributes (base / modified / final) |
| 11-17 | record+0xc..+0x18 | equipment slots 0-6 (`char_get_equip(n)` = code n+0xb) |

Other slot/record fields seen: slot+0 s16 character id (≤0 means empty), slot+2 u8 status
(&7 means it cannot be healed), slot+0x10 u32 EXP, slot+0x4c attribute capped at 255 by item
bonus. Record+0x3a flags (bit0 no auto-equip, bit2 counts for specialty 8 battle bonus), record+0x3b
partner id, record+0x5c s8 skill levels (sign = ?), record+0xcc..+0xcf bond/voice bytes (guess).

Specialties (`char_specialty_info`): table **0x8007197c**, 3 skill ids per specialty, from specialty
1. Level = average of the required skills (0 unless all are learned, −1 if none), +1 per accessory
0x1b4 in equipment slot 5/6, capped at 10. Super specialties (`party_super_specialty_info`): table
**0x800719ae**, pairs (A, B) per id 1-8:
`(0xb,0xa) (3,1) (8,0xe) (0xc,9) (7,0x11) (5,6) (4,0xd) (0xf,0x10)`. Ready when 2+ members have
A > 3 and somebody has B; level = Σ A / 3, capped at 10.

**StatChange** (6 bytes): `[0] apply`, `[1] HP dirty`, `[2] MP dirty`, `[3] money dirty`. Clamps
are HP 9999, MP 999 and money 999,999,999. **Money is config+0x18 (s32)**.

**StatCalc** (vtable 0x80072124, +0 item stat table, 16 bytes per item, loaded by gap 0x8003b320 when
item data is not resident). `stat_calc_member` order: copy base→modified for groups 3-10 · HP/MP max
with the config % · equipment effects · modified→final · item bonuses (`stat_calc_item_bonuses`) ·
clamp current HP/MP. Its destructor recalculates every member, so creating and destroying one
refreshes party stats.

Equipment effect ids (item data +0x28) in `stat_calc_equip_effects` (percent unless noted; a
negative argument to the HP/MP functions adds it as a flat amount):

| Id | Effect | Id | Effect |
|---|---|---|---|
| 0x6f | HP +20, MP +20 | 0x96 | A +40, D +20 |
| 0x72, 0xd1 | MP +30 | 0xa7 | C +10 |
| 0x78 | A +30 | 0xad | A +30, D −10 |
| 0x7a, 0x7f | B +30 | 0xb2 | A +100, D −50 |
| 0x80, 0x9e | B +10 | 0xbc | A +10, D +30 |
| 0x81, 0xbd | HP +20 | 0xbe | E +10 |
| 0x83 | MP +100 flat | 0xcd, 0xd4 | HP +30 |
| 0x86 | counted; each gives HP +20, MP +20 at the end | 0xdb | HP +10 flat |
| 0x87 | A +20, D −5 | 0xec | MP +10 |
| 0x88 | A +100 | 0xee | MP +100 |
| 0x91 | B +30, A −30 | 0xef | HP +1000 flat |

A = slot+0x2c (group 3 modified), B = +0x32 (group 4), C = +0x38 (group 5), D = +0x3e (group
6), E = +0x44 (group 7). `stat_mod_attr_3e` clamps +0x40 instead of +0x3e, a bug in the
original. Effects that `stat_calc_scan_effects` reports to the field: 0x79 bit 1, 0x8d bit 2,
0xb0 bit 4, 0xb1 bit 8, 0xce bit 0x10, plus the walking flags 0x84/0x8b above.

## Inventory (handoff 6)

| Off | Meaning |
|---|---|
| +0x000 | u16 entry[0x400]: bits 0-9 item, 10-14 count (≤ 20 enforced by `inventory_add`), **15 new** |
| +0x800 | u16 recent[16], most recent first (`inventory_push_recent`) |
| +0x820 | ItemData* (0x30-byte records, item-1). `inventory_item_data` reads offset 0 as u32, offsets 4..0x12 as s16, others as u8. +4 equip mask (bit = char id − 1), +8 attack, +0xc/+0xe/+0x12 (unknown), +0x10 defence, +0x14/+0x16/+0x2c bonuses (u8), +0x28 effect id, +0x2a type. `item_stat_get` index 0-7 = offsets 4, 0x2a, 0x28, 8, 0x10, 0xc, 0xe, 0x12. Auto-equip maps type to slot through 0x80071688: types 1, 2, 7 → slot 0 (weapon, compared on attack); 8/9/10/11 → slots 1/2/3/4 (compared on defence); 3-6 are never auto-equipped |
| +0x824 | u8 check[0x400] |
| +0xc24 | u8 clear the recent list on the next add |

Check byte = `((~i & 0xf ^ salt) << 4) | ((v & 0xf) ^ ((v & 0xff) >> 4) ^ salt)` with
`v = e ^ (e >> 8)`, `e = entry & 0x7fff`, and `salt = rng_range(16)` on every write. Verify by
recovering `salt = (~i & 0xf) ^ (check >> 4)`. **`inventory_add` deletes an entry whose check
byte doesn't match.** A port that edits inventory must rewrite the check byte, or saves will lose
items. It must also consume `rng_range` exactly as the original does where RNG parity matters.

## Screen module (shared with the title program)

Every function at 0x8003c9d0–0x8003d4c8 is the title's code at title address + 0xf5c, with the same
globals laid out the same way: buffers 0x800740d0 (DRAWENV 0x5c bytes + DISPENV, 0x70 per buffer),
buffer index 0x800740cc, arenas 0x800741b0/0x80074210/0x80074270, OTs 0x800742d0/0x80074330,
draw-y 0x80074360, overflow callback 0x80074368, deferred delete 0x8007436c/0x8007442c.
Port: **reuse `port/src/game/title/screen.cpp` unchanged**. Field-only additions:
`render_ctx_create_arena_in` (splits a caller buffer), `render_ctx_release_arena`, `deferred_delete`,
`screen_set_overflow_callback`. Title rows that map here: screen_init, deferred_delete_update,
screen_setup_buffer0/1, screen_set_display_window, screen_drawenv/dispenv,
screen_create/destroy_arena, screen_create/destroy_ot, screen_clear_ot, screen_draw_ot,
screen_put_env, screen_flip, screen_alloc, screen_draw_y.

## Actor (field actors, `field_actors[]` 0x80075360)

Concrete actors are 0x3fc bytes (or 0x44c in the 0xc1e overlay mode). Their ctors are in the
0x8007e000 overlay. The base data is 0x3f8 bytes with the **vptr at +0x3f8** (abstract base vtable
0x80072dd4, entries = `__pure_virtual` 0x8006fe00). Virtuals used by `actor_update`: vtable[2]
(+0x10) custom update(arg), vtable[3] (+0x18) draw/link, vtable[4] (+0x20) move step (runs while
+0x27 != 0). Dtor `actor_destroy`.

| Off | Type | Meaning |
|---|---|---|
| +0x00/+0x04/+0x08 | s32 | position x/y/z, 20.12 fixed point (map units << 12) |
| +0x10 | s16 | animation id (−1 none) |
| +0x12 / +0x14 | s16 | frame timer / frame index |
| +0x16 | s16 | model / sprite id (init arg) |
| +0x18 | s16 | wait counter (cmd 1 frames, cmd 4 loops) |
| +0x1a / +0x1c / +0x1e | s16 | turn target / per-frame step / frames left |
| +0x20 | s16 | facing, 0-4095 |
| +0x22 | s16 | palette (map info +0x5a when −1) |
| +0x24 | u8 | radius (0x18) |
| +0x25 | u8 | turning |
| +0x26 | u8 | animation direction (ping-pong) |
| +0x27 | u8 | move mode: 0 none, 1 xyz, 2 xz, 3/4 jump |
| +0x28..+0x34 | s32[4] | move target (x, y, z, w) |
| +0x38..+0x44 | s32[4] | move start |
| +0x4c | u32 | flags: 0x80 animation paused, 0x20000 screen-space coordinates, 0x2000000 ping-pong path, default 0x1000f |
| +0x50 | s16 | jump height |
| +0x54 | s32 | speed (<0 means frame count before `actor_calc_speed`) |
| +0x58 | s16 | emote timeout |
| +0x5c / +0x5e | s16 | move frames |
| +0x60 / +0x62 | s16 | jump angles |
| +0x64 / +0x68 | s32 | scale 0x1000 |
| +0x6c / +0x70 | s32 | per-frame velocity x / z (jump / screen moves) |
| +0x74 / +0x76 | s16 | screen x / y or base y |
| +0x78..+0x7a | u8 | colour RGB (0x80 neutral) |
| +0x7b | u8 | fading |
| +0x7c..+0x7e / +0x80..+0x82 | u8 | fade start / target RGB |
| +0x7f | u8 | value set by cmd 0x1b, copied to the child (semi-transparency? guess) |
| +0x83 | u8 | apply global tint |
| +0x84 | u8 | wait state: 1 timer, 2/3 move, 4 turn, 5 anim loops, 6 fade |
| +0x86 | u8 | moved this frame |
| +0x88 / +0x8a | s16 | fade duration / timer |
| +0x8e | s16 | move heading (ratan2) |
| +0x94 | s32 | speed factor (global% * 0x3000 / 100) |
| +0x98 | s16 | previous facing |
| +0xa2 | u16[256] | **command ring** |
| +0x2a2 / +0x2a4 | u16 | ring read / write (mod 256) |
| +0x2a6 | u16 | animation loop counter |
| +0x30c | AnimData* | current animation (below) |
| +0x310 | s16 | current cel |
| +0x314 | s32 | depth for the alternate OT |
| +0x318 | void*[] | primitives; +0x378 u16[] OT index per prim; +0x3a8 prim count |
| +0x3c5 | u8 | frozen (keeps its pose) |
| +0x3d0 | Actor* | child (emote icon); +0x3d4 Actor*[2] more children |
| +0x3dc | void* | owned buffer |
| +0x3f8 | vptr | |

**Command stream** (u16 words queued by `actor_cmd_*`, also by part-2 pushers; `actor_update`
decodes them when the actor isn't waiting). Angles are in degrees, converted with ×4096/360.
Positions are map units (<<12 internally). "speed" is a speed or −frames.

| Cmd | Args | Effect |
|---|---|---|
| 1 | frames | wait |
| 2 / 0x11 | – | wait for move end (state 2 / 3) |
| 3 | – | wait for turn end |
| 4 | loops, absolute | wait for animation loops |
| 5 | – | wait for colour fade |
| 6 | deg, speed_deg, mode | turn to absolute |
| 7 | deg, speed_deg, mode | turn relative |
| 8 | actor, speed_deg, mode | face actor |
| 9 | x, y, z, speed | move to (mode 1) |
| 0xa | x, z, speed | move to xz (mode 2; screen-space if flag 0x20000) |
| 0xb | dx, dy, dz, speed | move by |
| 0xc | dx, dz, speed | move by xz |
| 0xd | deg, dist, speed | move along heading |
| 0xe | deg, dist, speed | move along facing + deg |
| 0xf | anim | set animation |
| 0x10 | – | remove actor (FUN_800454c0(actor manager, self)) |
| 0x12 | x, z, speed_deg, mode | face point |
| 0x13 | icon, frames, flag | emote child (icons 0x24/0x27/0x2a/0x2e/0x35 map to anims 0x39/0x36/0x37/0x38/0x3a) |
| 0x14 | – | delete emote |
| 0x15 | value | 0x8006a6d0(script ctx, value): wakes a script waiting on the actor |
| 0x19 / 0x1a | x, y, z, height, frames | jump to absolute / relative |
| 0x1b | r, g, b, frames, semi | colour fade |
| 0x1c | x(32), y(32), z(32), speed | cmd 9 with 32-bit coordinates |

**AnimData** (`actor_anim_advance`): `u16 frame_count, u8 mode (1 = ping-pong), u8 loop_start`,
then `frame_count` × `{s16 cel, u8 ?, u8 duration}`. Duration 0 with count ≤ 1 means static.
Frame time counts down in vsync units (DAT_8007531c = vsyncs this frame, also used to scale every
timer).

**Draw** (`actor_link_prims`): each primitive's RGB (bytes 4-6) is multiplied by the global tint
/128 (0x80075294+0xc..0xe, enabled by +0xf) and by the actor colour /128. The prim is then
linked with addPrim at `OT[16 + depth]`, where OT = `*(*(0x80075290+0xf8)+0x70)` (the field render
context's current OT). Prims whose depth equals `depth_sel` go to a second table
`alt[0x820 − z/2]` (sorted against background layers). Children are drawn recursively.

---

## Globals in this range

| Address | Name | Type | Meaning |
|---|---|---|---|
| 0x8002f8c4 | Program_vtable | vtable | Program base vtable |
| 0x8002f8dc | g_current_program | Program* | object `field_entry` runs; swapped while a menu runs |
| 0x80074058 | g_menu_debug_stage | int | progress marker 1-0xe |
| 0x8007405c | g_menu_scene | MenuScene* | |
| 0x80074060 | g_menu_res | u8* | menu resource pack (TIM table), freed after upload |
| 0x80074064 | g_menu_data | u8* | menu / page data block |
| 0x80074068 | g_item_data | void* | item data (needed by `item_data_get`) |
| 0x8007406c | (cleared by menu_scene_create) | int | |
| 0x80074074 | g_menu_session | MenuSession* | |
| 0x80074078 / 0x8007407c / 0x80074080 | g_sel_index / g_sel_slot / g_sel_record | int / PartySlot* / CharRecord* | selected party member |
| 0x8007408c / 0x80074090 | g_field_query_on / g_field_query | int / MenuFieldQuery* | |
| 0x80074094 | g_page_load_stage | int | debug marker in `menu_pages_update` |
| 0x80074098 | "BASCUS-94421S02-S" | char[] | save file prefix |
| 0x800740b0 | save title | char[] | "STAR OCEAN ..." |
| 0x800740cc..0x8007445b | screen module state | | see screen section |
| 0x80078850 | g_memcard | MemCard* | |
| 0x8007884c | g_menu_input_debounce | u8 | frames of ignored input after a press |
| 0x80078854..0x8007886c | background loader | | see section |
| 0x8007585c | g_entry_cache | {ptr, entry, ?}[16] | resident archive-entry cache |
| 0x800752d4 | g_menu_launcher | MenuLauncher* | |
| 0x800752e0 | g_callways | Callways* | |
| 0x80075360 | field_actors | Actor*[0xc2] | (existing) |
| 0x80075766 | g_move_speed_pct | s16 | global actor speed percent |
| 0x8007531c | g_frame_vsyncs | int | vsyncs elapsed this frame (0 = paused) |
| 0x80075330 | map info | struct* | +0x5a default palette, +0x68 u16 encounter step value |
| 0x80075294 | actor manager (0x490) | struct* | +0xc..+0xe global tint RGB, +0xf tint on |
| 0x80075290 | field render context (0x10c) | struct* | +0xf8 → current frame, +0x70 OT |
| 0x80075734 | field BGM buffer | u8[0x4800] | songs unpack at +0x4000 |
| 0x80075a78 / 0x80075a7c | free memory size / base | int / u8* | menu arena = base, size − 0x14000 |
| 0x80076196 / 0x8007619a | menu mode / its party slot | u8 / s16 | |
| 0x800717a0 | song summon table | s16[9] | 0x09,0x18,0x46,0x4f,0x67,0x6d,0xed,0x80,... |
| 0x800717b0 | default button table | u16[8] | 0x20,0x40,0x10,0x80,4,8,1,2 |
| 0x80071968 | stat group flags | u8[32] | |
| 0x8007197c | specialty → skills | u8[17][3] | |
| 0x800719ae | super specialty pairs | u8[9][2] | |
| 0x80071688 | item type → slot jump table | | auto-equip |
| 0x80071708 | song → modifier jump table | | |

Field-state offsets used here: field_flags (0x8007570c) +0x25 encounters / voice preload enabled
(guess), +0x38 u32 (read by MenuFieldQuery), +0x3c leader char (menu mode 3), **+0x68 field mode
(overlay 0xc1d vs 0xc1e, actor class)**, +0x84 s8 encounter modifier. game_state (0x80075710) +0x264
s16 pending battle id, +0x294 s32. Config (handoff 2): +0x00 u16 keys[8], +0x14 program-swap
counter, **+0x18 money**, +0x4c/+0x4d flags, +0x181, +0x184/+0x186 HP/MP %, +0x188..+0x196
battle values.

## Data formats read from disc

1. **Menu programs** (entries 0xba4 + 4·(kind−1), 4 consecutive entries read as one buffer): each is
   one SLZ block. Block 0 is the **resource pack**: `u32 offset[n]` from the pack start, each pointing at
   a 4-bit **TIM** (+0 TIM header, +8 CLUT block header, +0x14 256 colours = 16 CLUT rows,
   +0x214 pixel block header, +0x220 pixels). Block 1 is menu code (→ 0x800d1b28), block 2 page code
   (→ 0x8007e000), block 3 data (→ g_menu_data).
2. **Menu pages** (table above): code entry and data entry read back to back, both SLZ
   (code → 0x8007e000, data → g_menu_data).
3. **Field overlay** 0xc1d / 0xc1e → 0x8007e000 (single SLZ block). It contains actor class ctors
   (0x8007e540 / 0x80082e5c).
4. **Field song data** (from menu code): `+4` SLZ sequence, unpacked to BGM buffer + 0x4000.
5. **Item stat table** (gap 0x8003b320): SLZ, 16 bytes per item, u16 each: +0 equip mask, +2 attack,
   +4 defence, +6/+8/+0xa = item fields 0xc/0xe/0x12, +0xc effect id, +0xe type (`item_stat_get`).
6. Entry cache keys 0xbd2, 0xbbb, 0xc27 + n, 0xc67 + n (per-character resources; unknown content).

## Porting notes

- **Screen**: reuse the title's screen module. The field adds 12 arena slots and the
  caller-buffer arena; OT index offset 16 is reserved before actor depths.
- **Actor core** is self-contained apart from: the concrete actor classes (move step, sprite
  primitive builder FUN_80042ce4, emote child FUN_80041748, remove FUN_800454c0, spawn
  FUN_80043890), sin/cos 4096 tables (FUN_8006e498 / FUN_8006e404, same as title sin4096/cos4096),
  script wake FUN_8006a6d0. libgte: `ratan2`, `SquareRoot0`, `gte_Square0` (Square0 of a
  VECTOR, used for the distance). Keep the integer maths exact: angle conversion
  `(deg % 360) * 0x1000 / 360` (magic 0xb60b60b7), timers scaled by g_frame_vsyncs, 20.12 positions.
- GPU primitives in this range: POLY_FT4 (debug VRAM view), text sprites with SetDrawMode
  (debug/time), and RGB modulation of actor prims (the prim layout must keep r,g,b at bytes 4-6 and
  the OT tag in word 0). VRAM ops: MoveImage, StoreImage, LoadImage, LoadClut (boot 0x80012f28),
  ClearImage.
- **Menu**: this range is only the host side. Pages, windows and drawing are in overlay entries
  0xba4-0xbce that nobody has analysed yet. A native port needs either those overlays reversed or
  a native menu that respects the MenuSession contract (exit codes, handoff 0xe, start page,
  sub-mode, `menu_run_select` result) and the VRAM save/restore.
- **Memory card**: BIOS `open/read/write/lseek/close/firstfile/nextfile`, `_card_info`,
  `_new_card`, `_card_write`, card events. Replace with host files named
  `BASCUS-94421S02-Snn`. Keep the 0x200 header / 0x80 summary / data layout if save compatibility
  is wanted.
- **Background loader** sits on the boot Loader (`loader_select_entry/start_read/poll/cancel`,
  0x80011988 sector seek). A port can load synchronously but must keep the voice-preload pause
  semantics, or drop the voice preload.
- **Sound** calls: sound_stop, sound_play_sequence, sound_state, 0x8001a65c (resume a sequence with
  fade), 0x8001a910, sound bank swap through System+0x5ac0/+0x5aa8.
- **Pad**: pad_current/pressed/pressed_repeat/clear, pad_set_repeat_delay(12),
  pad_set_port_mask(3), clock read 0x80014434, reset callback 0x8001445c.
- Heap: `mem_alloc`, `mem_alloc_high`, `mem_free`, `__builtin_new/delete`, mem stats 0x80013428
  (debug only). Cache flush 0x8001054c after loading code (no-op natively).

## Corrections and additions to the existing catalog

- `inventory_set_count` (0x8003c798) **subtracts** `count` from the entry (no-op if the result
  would go negative, clears the entry at 0, re-checksums). Proposed name `inventory_remove_at`.
- Script `table_a 0x01 add_money` changes game_state+0x248 (s16). Real money (Fol, ≤ 999,999,999) is
  **config+0x18**, the value globals.csv calls "counter clamped 0..999999999". The 0x248 opcode is
  something else.
- `overlay0_start` → `field_entry`. Ghidra's `FUN_8002f890` is `program_set_system`, shared by
  every Program vtable.
- formats.md: the field's 0x8007e000 overlay is entry **0xc1d** or **0xc1e** (field_flags+0x68).
  Menus overwrite it and reload it on close.
- Inventory entry bit 15 = "new" (set by pickups, cleared by auto-equip). Inventory has check bytes
  at +0x824 and the recent list at +0x800.

## Gap code not in index.csv (not listed in the CSV)

Uncalled in this image, probably called by overlays; read only far enough to classify:
0x80030ce0 (copy of `party_member_clear_bond`), 0x80031744 (calls `callways_end_song`),
0x8003284c (loop over member status), 0x800329c8 (entry cache SLZ unpack), 0x800334a8-0x8003359c
(accessors on 0x80074084), 0x8003363c (`inventory_add` wrapper), 0x80033708, 0x800337cc,
0x80033bc8 (config key binding writes), 0x800340bc, 0x80034154 (random party member),
0x80034a8c (clock to h:m), 0x80034ba0 (MenuFieldQuery update → 0x80034594), 0x800368a8,
0x80036b68 (WindowList helpers), 0x80038538 (wait SwCARD event), 0x80038690, 0x800387c0,
0x80038e64/0x80038e8c/0x80038eb8/0x80038f38 (`bgload_queue` variants), 0x80039268,
0x800393c4 (prints "error StructScdque"), 0x80039810-0x80039a30, 0x80039b08, 0x80039e20,
0x80039e9c (StatChange variants), 0x80039f94 (rng helper), 0x8003b320 (item stat table load),
0x8003b7b8/0x8003b834/0x8003b9fc (equipment stat snapshots for auto-equip), 0x8003ba74/0x8003baa8
(can-equip checks), 0x8003c0ec, 0x8003c360/0x8003c3d8/0x8003c4bc (equipment stat sums),
0x8003c974 (inventory verify loop), 0x8003d118/0x8003d1b8 (ClearOTagR)/0x8003d274
(DrawOTag)/0x8003d418 (screen variants = title +0xf5c gaps), 0x8003f518 (actor helper calling
0x80043018 / FUN_80042e4c).
Data mistaken for functions: 0x80030944-0x8003095c, 0x800310f8-0x8003111c, 0x80031ee8-0x80031fec,
0x80033bec-0x80033c5c, 0x80039ee4/0x80039eec, 0x8003b874-0x8003b994, 0x8003bae8-0x8003bb14 are switch
targets.
