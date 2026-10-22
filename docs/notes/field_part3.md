# Field program part 3: 0x80053e8c-0x80064f30

Range: `field_load_map`, the preload cache, the field task list, the message system (windows,
text layout/rendering, notice banner), the afterimage effect, the OT-layer/scene-object task,
the camera block, the map-entry resolver, the VM context constructor/initialiser and
`vm_exec_ext`. 110 indexed functions, all in `part3.csv` (59 confirmed, 44 likely, 7 guess).
Field-program addresses (archive entry #3100 at 0x8002f810).

Names used from other parts (part2 owns 0x80040c2c-0x80053e8c): `field_load_entry` 0x8004a654,
`field_require_sprite_pack` 0x8004a830, `field_load_extra_pack` 0x8004a920,
`field_load_map_sections` 0x8004aca0, `sprite_pack_register` 0x80053940,
`field_load_code_overlay` 0x8005299c, `field_load_music_sync` 0x8004b6b0,
`field_register_party_sprites` 0x8004b5cc, `field_prompt_disc_change` 0x80052ce8,
`actor_create` 0x80043890, `actor_sys_setup_view` 0x80044394, `load_queue_*` (LoadQueue
0x800752b0). Struct names: `Field`, `MapScene` (section 0 block 0), `MapFiles`, `ScriptCtx`,
`MsgSystem`, `MsgWindow`, `MsgCells`, `TextLine`, `Notice`, `FieldTask`, `Afterimage`,
`SceneRenderer`, `Actor`.

Verification scripts: `analysis/field_catalog/scratch/` (`so2lib.py` reads entries via
`tools/so2_archive.py`; `secscan.py`, `sec0survey.py`, `grid.py`, `listsurvey.py`,
`subsurvey.py`, `fontscan*.py` ran over every map container). The overlay code quoted below
(0x8007e000+) is entry #3101 (0xc1d, field) / #3102 (0xc1e, world), disassembled from
`analysis/archive/disc1/310x.0.unpacked`; it has no decompilation yet.

---------------------------------------------------------------------------------------------

## 1. Map loading sequence (`field_load_map(field, map, keep_position, music_mode)`)

Callers: `field_main_loop` (first map), field state 1 (after a fade), VM wait state 1
(map_transfer). `keep_position` = 0 means "use the map's default spawn"; `music_mode`: 0 keep,
1 restart game_state music (+0x1c), 2 take the preloaded music / vehicle music. Script flag
byte 1 bit 6 forces music_mode 0. `field_flags` (= script globals, handoff 9 +0x180) +0x24 is
"returning from another program", +0x68 "current map is a world map".

1. `preload_free_saved_table`; stop the jingle task if active (`callways_song_active/stop`).
2. Reset per-map globals (0x80076310 = -1, 0x800762ff, 0x80076282, 0x80076279, 0x80076258,
   0x800761d4[], 0x800761e8, 0x800761a8 ...). Preload memory reserve 0x800761a4 = 0x14000.
   field_flags+0x32 = 2; 0x8007535c = 100; 0x80075348 = -1; frame step 0x8007531c = 1;
   VRAM allocators for extra packs: next CLUT row 0x80076254 = 0x1f8, next x 0x80076256 = 0x280.
3. Leader/partner: script flag byte 0 bit 1 set → game_state+0x24 = 0 (leader slot 0),
   +0x25 = 1; else leader 1, partner 0.
4. Free the previous map's objects: embedded program (section 6 copy 0x800756f4), extra-pack
   SLZ blocks (`field_free_extra_packs`), afterimages (`field_destroy_map_objects`),
   scene objects 0x800761ac, overlay buffers 0x800756b8 (0x801e9000) / 0x800756c0
   (0x801f9800), section-11 data 0x800756cc, the 0x801f9800 overlay object 0x800752e8
   (dtor 0x801f98c0), the 0xfc2 side text (cancel + free), OT layers (`field_free_ot_layers`).
5. `load_queue_set_streaming(q, 0)`; `preload_release(1)` (drop kind-1 = battle preloads).
6. Disc check `loader_is_disc(loader, config+0x4c + 1)`. Right disc: spin the LoadQueue until
   idle, then `preload_drop_incomplete`. Wrong disc: `load_queue_reset`, and later (step 12)
   `field_prompt_disc_change(field, disc, 1)`.
7. field_flags+0x27 (palettes tinted) → `sprite_packs_restore_palettes`. Leaving a world map →
   overlay `0x800888e0(bg)`.
8. field_flags+0x68 = (map-1 < 3); game_state+0x1a = map; `preload_prune_for_map(map)`.
9. `map_resolve_entries(&files, returning ? -1 : map, x, z)` (section 2). World map, fresh
   arrival without position: default position (0x15026, -0x102, 0x6485), angle 0xe98.
   Vehicle flags (script flag byte 1 bits 3+4): positions from game_state +0x26c/+0x28c/+0x270
   (map 1) or +0x274/+0x290/+0x278.
10. `actor_sys_reset_sprite_cache`, `actor_delete_slot(-1)`, `msg_window_destroy(msg, -1)`.
11. Free the previous scene (0x80075330), map font (0x80075714), script (0x800756f8) and the
    sprite-pack slots from 2 up (field) / 0 up (world). Delete the overlay objects: BG
    renderer 0x8007528c (vtable +0x10 dtor), 0x800752dc (0x8007e0b8), 0x800752c4, 0x800752b4.
12. Wrong disc → `field_prompt_disc_change`.
13. Music: when returning, handoff 0xc gives the music state of the previous program
    (0x80075746 = -entry). music_mode 1 → game_state+0x1c; 2 → `preload_find_music` (field) or
    0xa86 (vehicle). `field_load_music_sync(field, entry)` loads the entry and installs the
    bank; 0x80075744 = music pending.
14. Party sprite packs (field maps): files[3] → sprite slot 0, files[4] → slot 1; each is
    reloaded only if its entry changed (`field_load_entry` + spin on `field_require_sprite_pack`,
    which SLZ-unpacks block 0 via `sprite_pack_register`). When slot 0 changes while slot 1 is
    kept, slot 1's data is copied out to mem_alloc_high and back to mem_alloc_low around the load
    (heap ordering). World maps (map < 4): optional vehicle packs 0x10e3 (flag byte 1 bit 3),
    0x10e2 (byte 1 bit 7), 0x10e4 (byte 2 bit 0) via `field_load_extra_pack`; then start
    loading files[3] (world party pack).
15. Code overlay, only when field_flags+0x68 differs from the last map (0x80075358):
    `field_load_code_overlay(field, files[5], src)` SLZ-unpacks entry 0xc1d (field) / 0xc1e
    (world) to 0x8007e000 and calls FlushCache (0x8001054c). Field: src = preloaded files[3]
    buffer + entry_size(files[3]) when the files[3] preload was longer than the entry, else 0
    (synchronous load). Every pack-A entry 0xc27+2m is followed on disc by an identical copy of
    #3101 (entries 0xc28+2m, all 32 checked), so one read fetches pack + overlay. World: src = world party pack +
    its word [2] offset, unless returning.
16. Create the mode objects (all overlay code):
    - field: BG renderer `0x8008672c(new 0x128)` → 0x8007528c; `0x8008e100(new 0x17c)` →
      0x800752b4; `0x8008e1c0()`.
    - world: `field_register_party_sprites(field, pack + pack[0])` (slot ids -2, -3, -4..);
      music = pack + pack[1] (restore: separate files[3] pack and files[0] music); music_mode 2
      plays it (`field_load_music_sync(field, 0)` on that buffer) and records 0xa8a (map 1) /
      0xa89 as game_state+0x1c; BG renderer `0x80088814(new 0x5a0)`, `0x8007e000(new 0x88)` →
      0x800752c4.
17. `field_start_pending_music(0)`.
18. **Map container:** `field_load_entry(files[2])` (sync, or from the preload cache) then
    `field_load_map_sections(field, files[2])` (section 3). Then `preload_release(-1)` and
    `field_start_pending_music(1)`. Returning: BG renderer vtable +0x20 (restore).
19. Embedded code (section 3, ids 5-12):
    - section 5 copy → SLZ-unpacked to exactly 0x801e9000 (loops `mem_alloc_high(0x15000)` until
      the allocator returns that address, calling 0x800131ec otherwise);
    - sections 7-10/12 → 0x801f9800 (0x4800 bytes, same trick);
    - section 11 → new buffer 0x800756cc, pointer stored at inventory+0x820.
20. Leader placement (not returning): clear field_flags +0x80..+0x83, +0x91.
    - Position = game_state +0x08/+0x0c/+0x10, angle = game_state+0x18 (0..4095 → degrees
      `*360>>12`), byte +0x21.
    - keep_position == 0 on a field map: angle 90, brightness 0x80, position = centroid of the
      first three corners of the first direction zone tagged '~' (cell 0 list 14, item +0xd),
      or zone 0, or the scene target (+0x14, (s16)+0x58, +0x1c) when the map has no zones.
    - `actor_create(actor_sys, leader, leader, &pos, angle, b, 0xffff, 0xffff, 0x1000f)`;
      `actor_or_flags(leader, 0, 0x200004)`.
    - Field: BG renderer +8/+10 = leader screen pos (x, y-16); overlay `0x800867d8()` (scroll
      to it); +0x114/+0x116 = 0, +0x124/+0x126 = 30000, +0x122 = scene height > 240.
    - World: vehicle actors 0xd / 0xe at the saved vehicle positions.
    - Returning instead: `fade_push(1, 0x14)` and re-create field_flags+0x89 list entries
      (overlay 0x8008df74).
21. World: BG renderer +0x30 = angle << 12, +0x0c/+0x10 = x/z.
22. field_flags +0x0e = leader (camera target), +0x26 = 0 (input unlocked), +0x24 = 0;
    encounter distance +0x18 clamped to 0..0x7530000.
23. `ResetGraph(1)`; `field_setup_screen` unless a fade hold/cross-fade is active. Field:
    `screen_layout_arena(1)`, `screen_set_clear_colour(scene +0x6f, +0x70, +0x71)`,
    `field_update_screen_steering`, VSync divider 0x8007572d = 0 (60 Hz). World:
    layout 0, black, 0x8007572d = 2 (30 Hz).
24. Two dummy frames (`screen_flip` + `screen_clear_ot` twice), `actor_sys_update_one(leader)`;
    special cases: `field_begin_game_over` (config+0x178 == 2 and field_flags+0x7e) or the
    vehicle music `field_side_load(0xa92)` (flag byte 1 bit 6; Field+0x14 = 0x11; music fade 60).

Archive entries touched by a map load:

| Entry | What | Loaded by |
|---|---|---|
| 0xc87 + map (3207 + map, maps 0-0x381) | map container | step 18 |
| 0x1014 + x/0x3000 + 9·(z/0x3000) (72 chunks), 0x109f + ... (map 2), 0x10e0 (map 3) | world-map chunk containers (sections 3 and 1 only) | step 18 |
| 0xc27 + 2·m, m = party bits 2-6 (Celine, Bowman, Dias, Precis, Ashton) | party sprite pack A (Claude, Rena + that subset); entry 0xc28 + 2·m = byte copy of overlay #3101 | step 14/15 |
| 0xc67 + m, m = party bits 7-11 (Leon, Opera, Ernest, Noel, Chisato), none if m = 0 | party sprite pack B | step 14 |
| 0xc1d (#3101) / 0xc1e (#3102) | field / world code overlay → 0x8007e000 | step 15 |
| 0x105c / 0x10e5 / 0x10e9 | world party packs (maps 1 / 2 / 3) | step 14/16 |
| 0x105d-0x105f / 0x10e6-0x10e8 / 0x10ea-0x10ec | restore set for world maps 1/2/3 (map -1): party pack, music, byte copy of overlay #3102 (checked 0x105f, 0x10e8, 0x10ec) | map_resolve_entries |
| 0x10e2-0x10e4 | world vehicle extra packs (tagged list, part2) | step 14 |
| 0xa51-0xaad | music (`u32 bank_offset`, SLZ sequence at +4, bank; same layout as #2674) | step 13 |
| 0xc26 (#3110) | field system data (UI atlas, window CLUTs, system strings; part2) | field_load_system_data, once |
| 0xfc2 (#4034) | menu strings (block 0, `{u16 id, u16 off}` table, ids-2999) + second font (block 1) | field_install_side_text |
| copies of #3103-#3109, #2 | embedded in containers (sections 5-12) | step 19 |

---------------------------------------------------------------------------------------------

## 2. MapFiles (`map_resolve_entries`, 0x80061888)

`s16 files[17]`, filled for (map, x, z); map -1 = "restore": map, music and position come from
the field state (game_state +0x1a, +0x1c, +0x08, +0x10).

| Idx | Field map | World map (map 1 / 2 / 3) |
|---|---|---|
| 0 | music to restore (game_state+0x1c) or -1 | -1; restore: 0x105e / 0x10e7 / 0x10eb |
| 1 | 0xc26 | 0xc26 |
| 2 | 0xc87 + map | 0x1014 + region / 0x109f + region / 0x10e0; region = x/0x3000 + 9·(z/0x3000); map 1 remaps 0x26-0x28, 0x2f-0x31, 0x38-0x3a to 0x3f-0x47 while handoff9+0x220 (game_state word 0) is 0xf..0x3c |
| 3 | 0xc27 + 2·((mask & 0x7c) >> 2) | 0x105c / 0x10e5 / 0x10e9; restore: 0x105d / 0x10e6 / 0x10ea |
| 4 | 0xc67 + ((mask & 0xf80) >> 7), or -1 | -1 |
| 5 | 0xc1d | 0xc1e; restore: 0x105f / 0x10e8 / 0x10ec |
| 6.. | restore only: s16 list at script globals +0x28 (count = globals byte +0x6a); 0-terminated | same |
| 0x10 | 6 + list count | |

mask = `party_member_mask_all()` (bit c-1 for each character c = 1..12 in the party, plus the
reserve list when script flag byte 0 bit 2 is set). Script globals byte +0x7f (handoff9 +0x1ff)
overrides [3]/[4] with the words at globals +0x60/+0x64. The script opcode ext 0xa5 calls the
same resolver to preload [2] and [3] ahead of a transfer (kind 2 preloads tagged with
ctx+0x109c).

---------------------------------------------------------------------------------------------

## 3. Map container and sections

`u32 count; {u32 id; u32 offset}[count]` (offsets from the container start). Survey of all
entries 3207..4115 (`secscan.py`): 803 maps have sections (0,1,2); 15 add 5; 5 add 8; 1 each
add 9, 10, (12,6), (11,7); entries 4116-4187 (world chunks) have (3,1). Entry 4034 (map 827)
and 4105-4115 / 4188+ are not map containers.

| Id | Contents | Parser action (`field_load_map_sections`) |
|---|---|---|
| 0 | SLZ chain: scene (block 0), texture directory (1), textures, CLUTs | 3.1 |
| 1 | SLZ chain: event script (block 0), map font (block 1) | 3.2; then `vm_init(ctx)` |
| 2 | one SLZ block: sprite pack (NPCs/objects of the map) | `sprite_pack_register(data, -1, 2)` (format: part2 "Sprite pack") |
| 3 | world chunk resource stream (raw) | overlay 0xc1e `0x80088a3c(bg, data)` — 3.4 |
| 5 | byte copy of entry #3103 (0xc1f) | → 0x801e9000 code overlay |
| 6 | byte copy of entry #3107 (0xc23), a whole program | kept in 0x800756f4; vm_exec_table_a runs it with boot_run_main_program |
| 7/8/9/10 | byte copies of #3109/#3108/#3106/#3104 (table 0x800744d6 + 2·id) | → 0x801f9800 overlay |
| 0xb | byte copy of entry #2 (unpacks to 0xbfd0) | → buffer, pointer at inventory+0x820 |
| 0xc | byte copy of #3105 (0xc21); the parser copies `entry_size(map container)` bytes | → 0x801f9800 overlay |

Sections 5-12 were checked byte-identical to the packed archive entries on maps 52, 581, 688,
384, 139, 143 (`scratch` one-liner in this session). Each is copied only if no copy is pending.

### 3.1 Section 0

Block numbers are positions in the SLZ chain (`slz_unpack(ctx, src, dst, n)`).

**Block 1, texture directory** (unpacked first):

| Off | Type | Meaning |
|---|---|---|
| 0x00 | s32 | tex_count (1-8) |
| 0x04 | s32 | clut_count (0-22) |
| 0x08 | u32 | 0x10 (offset of the records) |
| 0x0c | u32 | 0x10 + 8·tex_count (block size) |
| 0x10 | {u8 pos; u8 depth; u8 pad[6]}[tex_count] | VRAM x = 0x200 + 64·(pos & 7), y = 256·(pos >> 3); depth 0/1/2 = 4/8/16-bit |

**Blocks 2 .. tex_count+1:** 256x256 texels each at that depth (0x8000 / 0x10000 / 0x20000
bytes), `upload_texture(p, depth, 0, x, y, 256, 256)`. 16-bit pages use pos 0, 4 (one
256-texel page = 4 VRAM columns); 8-bit pages pos 0, 2, 4, ...

**Block tex_count+2** (when clut_count > 0): clut_count x 0x204 bytes `{u8 mode; u8 pad[3];
u16 colour[256]}` → `upload_clut256(colour, 0, 0x1e0 + i)`. mode 0: colour 0 of each of the 16
banks forced to 0 (transparent); mode 1: only colour 0.

Survey (all 827 field containers): every directory matches its chain exactly (texture block
sizes 0x8000 << depth, CLUT block = 0x204·clut_count, nothing else in the chain).

**Block 0 = MapScene** (kept in 0x80075330 for the whole map; 0x958..0x7d94 bytes over all maps). Field
readers: field_load_map, `actor_sys_setup_view` (0x80044394), `actor_sys_draw` (0x80043ac4),
`field_try_talk` (0x80069d8c), overlay 0xc1d `0x80086ad4` (texture pages), `0x80087318` (BG
sprites), `0x80085cf0` (cell lookup), `0x80082cfc` (actor movement).

| Off | Type | Meaning | Evidence |
|---|---|---|---|
| 0x00 | u32 | 0 | all samples |
| 0x04 / 0x06 | u16 | background size in pixels (320x240, 640x480, 512x512); +6 > 240 → BG renderer +0x122 (vertical scroll) | field_load_map; samples |
| 0x08/0x0c/0x10 | s32 | camera eye x,y,z (world units) | actor_sys_setup_view |
| 0x14/0x18/0x1c | s32 | camera target x,y,z; also default spawn x/z | same; field_load_map |
| 0x20 / 0x24 | s32 | projection centre = (+0x34/2, +0x36/2) in all 827 maps | survey |
| 0x28 / 0x2c | s32 | cell-grid origin in screen pixels (0 in samples) | overlay 0x80085cf0 |
| 0x30 | s32 | camera roll (low 16 bits) | actor_sys_setup_view |
| 0x34 / 0x36 | u16 | projection plane size → GeomOffset (w/2, h/2) | same |
| 0x38 | MATRIX | precomputed view rotation (rows orthonormal, 4.12) + t = eye (+0x4c..+0x57) | checked maps 8, 93 |
| 0x58 | s16 | floor y: default spawn y; BG renderer +0x118 | field_load_map; 0x80086ad4 |
| 0x5c | u16 | BG sprite count (records in sub-block 1) | 790/827 maps: sub1 size = 20·count |
| 0x64 | u32 | 0/1 (unknown) | |
| 0x68 | u16 | actor scale (25..200): talk probe distance = scale·0.9 | field_try_talk |
| 0x6a | u16 | projection distance H | actor_sys_setup_view, field_try_talk |
| 0x6c..0x6e | u8[3] | actor light colour r,g,b (0x80 neutral) | samples |
| 0x6f..0x71 | u8[3] | screen clear colour | field_load_map |
| 0x78 | u32[8] | sub-block offsets from block start; [7] = block size (790/827 maps) | 3.1.1 |
| 0x98 | u8 | colour-cycle count; +0x99 u8 mode (1 = ping-pong) | actor_sys_draw |
| 0x9c | {u8 r,g,b; u8 frames}[n] | colour-cycle steps (actor light) | actor_sys_draw |
| 0xcc | u16[8] | world angle for each of the 8 pad directions (camera-relative control) | overlay 0x80082d8c |
| 0xdc / 0xde | s16 | camera rotation x / y (billboard matrix) | actor_sys_setup_view |
| 0xe8 | u32 | 16 (page count) | samples |
| 0xec | {u8 used; u8 depth; u16 pad}[16] | texture page table: page i at VRAM (0x200 + 64·(i&7), 256·(i>>3)); the BG renderer precomputes 4 tpage words per page (abr 0-3) | overlay 0x80086ad4 |
| 0x12c | | sub-block 0 (offsets are absolute; first is always 0x12c) | |

#### 3.1.1 MapScene sub-blocks (offsets at +0x78)

| # | Size | Contents |
|---|---|---|
| 0 | 8·n | **BG tiles** `{u8 u, v, w, h; u8 flags; u8 clut_x/16; u8 clut_y-0x1e0; u8 prim}`. prim 0x64 = SPRT. flags bit 0 semi-transparent (abr = bits 1-2 → page variant), bits 3-4 nonzero = draw as POLY_FT4 (0x2c/0x2e) instead of SPRT. CLUT id = (0x1e0 + b6) << 6 \| b5 (ignored for 16-bit pages) |
| 1 | 20·count(+0x5c) | **BG sprites** (see below) |
| 2 | var | **cell grid** (3.1.2) |
| 3 | 44·n (9 maps) | unknown |
| 4 | 160·n | BG sprite animation records, indexed by BG sprite +8 (fields used: +2 alternate tile, +6 alternate page, +8..+0xa colour, +0xc/+0xe x/y offset) — likely |
| 5 | 332·n | BG sprite colour records, indexed by BG sprite +0xa (colour at +0x148..+0x14a) — guess |
| 6 | 16·n (25 maps) | unknown |

**BG sprite record** (sub-block 1, read by overlay 0x80087318 for i < scene+0x5c):

| Off | Type | Meaning |
|---|---|---|
| 0x00 / 0x02 | s16 | x, y in background pixels (screen = x - scroll + offset; scroll at BG +0xc/+0xe, offset +0x10/+0x12) |
| 0x04 | u16 | tile index (sub-block 0) |
| 0x06 | u8 | texture page slot 0-15 (MapScene +0xec) |
| 0x07 | u8 | layer: inserted at OT index layer + 0x11 (forward OT → higher layer draws on top) |
| 0x08 | s16 | animation record (sub-block 4) or -1 |
| 0x0a | s16 | colour record (sub-block 5) or -1 |
| 0x0c..0x12 | s16[4] | scroll window x_min, x_max, y_min, y_max: drawn only while the scroll position is inside |

Map 93 example: tile 1 = (u 0x38, v 0, 72x96) and its sprite sits at (0x38, 0) on layer 100:
the backgrounds are pre-rendered images in the texture pages, and the sprites redraw parts of
them; actors get layer ids from cell list 2 (below) and sort between them.

#### 3.1.2 Cell grid (sub-block 2; DAT_80075338 = start, DAT_80075334 = start + 4)

```
u8 gx; u8 gy; u16 0;                         // grid size (5x4 for 320x240, 7x6 for 640x480 ...)
Cell cell[gx*gy];                            // 0x60 bytes each
Cell: u16 count[16]; u32 offset[16];         // list k: count[k] items at grid_start + offset[k]
```

- **Cells are screen space**, 128x128 px. `cell_index(sx, sy)` (overlay 0x80085cf0):
  x' = sx - scene+0x28, y' = sy - scene+0x2c; valid when -128 ≤ x' < (gx-1)·128 and the same
  for y; index = (trunc(y'/128) + 1)·gx + trunc(x'/128) + 1; else -1. Checked on map 93: the
  two list-2 quads (x 61-256 / 37-278, y 70-96 / 97-203) are listed exactly in cells 6-8 and
  6-8, 11-13.
- **Cell 0** (row 0, col 0, never produced by the lookup) holds the map-global lists 14 and 15
  (and rarely 0, 1, 3, 5, 10). Items may be shared by several cells (same offset).
- Survey over 827 maps (`listsurvey.py`): list → item size: 2 → 40, 4/6 → 76, 8 → 36, 9 → 40,
  13 → 36, 14 → 88, 15 → 48; lists 0/1/3/5/10 rare.

All items contain a common polygon header. Screen-space items (cells ≥ 1) start with a 4-byte
prefix `{u8 type (1); u8 r, g, b}`; world-space items (cell 0) do not:

```
[prefix 4 bytes]
s16 y_min, y_max;            // height range (-32768..32767 = any)
s16 x_min, z_min, x_max, z_max;   // bounding box (screen x/y for screen items, world x/z for world items)
u8 1; u8 tag (0x7f; '~' (0x7e) = default entry zone, '2' = special wall); u16 n (0 or vertex count 4)
vertices: screen items 4 x {s16 x, s16 y}; world items 4 x {s32 x, s32 z}
[type-specific tail]
```

| List | Space | Size | Tail | Use (evidence) |
|---|---|---|---|---|
| 2 | screen | 40 | u8 layer at +0x24 (+pad) | **actor depth layer**: actor screen point (actor+0x74) inside → actor+0x22 = layer (overlay 0x80083840) — confirmed |
| 4 / 6 | screen | 76 | 4 x {s16,s16} vectors, then 4 x s32 (plane coefficients?) | actor shading regions with a gradient; prefix rgb = 0x40..0x88 grey — guess |
| 8 | screen | 36 | none | flat actor tint regions (prefix rgb) — guess (read by overlay 0x80085b6c) |
| 9 | screen | 40 | s16, s16 | read by the actor draw code 0x8007e704 for the actor's screen point — unknown effect |
| 13 | screen | 36 | none | **talk-through regions**: `field_try_talk` extends the talk probe while it lands inside one (talking across counters) — likely |
| 14 | world (cell 0) | 88 | +0x30..+0x43 unknown; +0x44 u16[8] direction angles; +0x54 u32 | **control-direction zones**: when the actor is inside and +0xe != 0, pad direction d uses angle +0x44[d] instead of scene+0xcc[d] (overlay 0x80082d74-0x80082dcc); '~' zone = default spawn — confirmed |
| 15 | world (cell 0) | 48 | none (n = 4) | **collision walls**: tested against the new position (overlay 0x800863fc); hit → wall slide 0x80085f30 with range 0x380, or 0x180 for tag '2' — likely |
| 0/1/10 | world | 88 / 48 | as 14 / 15 | rare variants |

The event-trigger question from the memory notes: none of these lists carries a script entry.
NPC talk binds through actor fields: `field_try_talk(ctx, 1)` uses actor +0x52 (talk entry),
`field_try_talk(ctx, 0)` (pickpocket: needs a party member with item 0x1b5/0x1bc equipped)
uses +0x90; ctx IP = code base + 4·entry. Map exits are presumably script-polled regions (not resolved here).

### 3.2 Section 1

**Block 0 = event script.** Header (all 899 scripts: hdr[1] = 0, hdr[6] = 0x170, hdr[2] inside
the code; `vm_init`):

| Off | Meaning |
|---|---|
| 0x00 | code size in bytes |
| 0x04 | code start in words after the header (0) → code base = block + 0x1c + 4·hdr[1] |
| 0x08 | init entry (word index): `vm_init` pushes a frame at this IP (vm_push_frame) on a fresh entry, then starts at word 0 |
| 0x0c | string count |
| 0x10 | text size |
| 0x14 | script variable area size (836 in 623 maps, up to 868): ctx+0x18 = globals + align4(hdr5) |
| 0x18 | flag area size (0x170): ctx+0x1c = script flags + hdr6 |
| 0x1c | code; string table (u16 offsets) at 0x1c + hdr[0]; text after it |

**Block 1 = map font** (all 899: height 12; 825 maps share one 276-glyph font, others 280 /
288 / 400 glyphs = the same base plus extras):

```
u32 count; u32 height (12); u8 width[count];      // width of glyph code c (1-based) at +7+c
u16 rows[count][12];                               // at block + count + 8 (= DAT_80075718); bit 15 = leftmost
```

**Text encoding (correction to formats.md "Text").** Strings are *byte* streams: b < 0x80 is a
one-byte code; otherwise code = (b & 0x7f) | next << 7. Codes < 0x4000 are 1-based glyph
indexes into the map font; 0 ends the string. formats.md's u16 values are the raw byte pairs:
glyph = (u16 & 0x7f) | (u16 >> 8) << 7, so 'A' is glyph 0xb6 (stored b6 01), space 0x105
(85 02). Glyph map (`text_ascii_to_glyph`, `text_format_number`): '0'-'9' 0xaa-0xb3, '-'
0xb4, '.' 0xb5, 'A'-'Z' 0xb6-0xcf, 'a'-'z' 0xd0-0xe9, '?' 0xed, '!' 0xef, '=' 0xff, ':' 0x100,
';' 0x101, ',' 0x103, '/' 0x104, space 0x105, unknown 0xfe; 0x55-0xa9 are kana.

Control codes (`msg_window_print`, `text_layout_line`, `text_render_line`):

| Code | Params | Effect |
|---|---|---|
| 0x4000 | - | new line |
| 0x4001 | - | wait for button, then clear the window (new page) |
| 0x4002 | - | wait for button (or 300 frames when script flag byte 0x5e bit 1 = auto text) |
| 0x4003 | u8 n | text speed: n < 5 → speed table entry; n ≥ 5 → pause n-5 frames |
| 0x4004 | u8 n | text colour n·2+3 (nibble LUT rebuilt) |
| 0x4005 | - | centre the line |
| 0x4006 | u16 | timed wait (frames) |
| 0x4007 | u8 v | insert character name, character = message variable v |
| 0x4008 | u8 v | insert 0xfc2 string id (variable v) + 2999 (item names), drawn with the 0xfc2 font |
| 0x4009 | 2 bytes | choice marker (layout) |
| 0x400a ... 0x400b | | centred block (choice / ruby) |
| 0x400c | u8 c | insert character name c |
| 0x400d | u8 i | fixed-width space from table 0x80076318 |
| 0x400e | u8 v | insert message variable v as a decimal number |
| 0x400f | u8 x | set x position |

Message variables are 16 s32 at script globals +0x38 (field_flags+0x38). Character names are
ASCII at game_state +0x28 + 0x14·c ('@' toggles kana).

### 3.3 Section 2

One SLZ block, a sprite pack (part2 "Sprite pack" has the layout; checked here on map 93: 2
groups, keys 0x58 and 0x59, id range 0..0xd). Registered with id -1 in sprite slots ≥ 2.

### 3.4 Section 3 (world chunks only)

Tagged stream parsed by overlay 0xc1e `0x80088a3c`: `u32 tag` then arguments, tag 0 ends
(jump table 0x8008f488):

| Tag | Args | Action |
|---|---|---|
| 1, 2, 7 | u16 vram_x, u16 vram_y, u32 offset | SLZ texture pack `{u16 flags (depth & 3); u16 clut_count; u16 clut[]; u16 w; u16 h; pixels}`; image at (x, y); CLUT rows: tag 1 → 0x1e0 + n plus a copy processed by 0x80088e64 at 0x1ec + n, tag 2 → 0x1fb, tag 7 → 0x1fa |
| 3 | ? | handler 0x80088d2c, not read |
| 4 | u32 offset | SLZ block → 0x8007617c (terrain data, guess) |
| 5 | u32 offset | `field_start_pending_music(1)` unless script flag byte 1 bit 4 (vehicle), then overlay 0x80088eec(bg, base, data) |
| 6 | u16, u16, u32 offset | image data at offset + 0x14 (TIM-like) |

Chunk 4116 begins `{1, 0x200, 0x71c}, {1, 0x240, 0x5880}, {1, 0x280, 0xb384} ...`
(textures in VRAM columns 0x200+). Detailed terrain format: not decoded.

### 3.5 Other files read in this range

- **0xfc2** (`field_install_side_text`): `u32 off_strings; u32 off_font;` → block at +off_font
  unpacks to the alternative font (0x800762f0, glyphs 0x800762f4) used by windows 10-19;
  block at +off_strings unpacks to `{u16 id; u16 off}` pairs ending in 0xffff, strings in the
  map text encoding (0x800762f8). Ids are looked up as variable + 2999.
- **World party pack** (0x105c / 0x10e5 / 0x10e9; checked 0x105c): `u32 off_sprites; u32
  off_music; u32 off_overlay;` sprites = `u32 off[14]` relative to that area, each an SLZ
  sprite pack: [0]/[1] the leader (Claude / Rena leading), [2 + c-1] party member c (the
  partner uses [2 + (leader ^ 1)]), registered with ids -2, -3, -4...; music = `u32
  bank_offset` + SLZ sequence (as #2674); overlay = byte copy of #3102.
- **Preload table entries**: see 4.2.

---------------------------------------------------------------------------------------------

## 4. Subsystems

### 4.1 Field task list (0x80056178-0x80056304)

- `FieldTask` objects: vptr at +4; vtable entry 1 (+8 delta, +0xc fn) = per-frame update.
  Base ctor 0x80057450 (unindexed; vtable 0x8007310c: update = pure virtual 0x8006fe00,
  entry 2 = `field_task_destroy`), registers at priority 3. Used as base by the overlay BG
  renderers (0x80086738 / 0x80088820 call it).
- Table: `FieldTask *task[24]` 0x8007591c, `u8 prio[24]` 0x8007597c, `u16 count` 0x80075994.
  `task_register` uses the first free slot (count grows only when appending);
  `task_unregister` nulls the slot. `task_run_all` runs priorities 0..4 in table order.
- Priorities: 0 VM, notice; 1 LoadQueue, ScreenFade, BattleTransition, jingle, SceneRenderer;
  2 ActorSystem, afterimages; 3 FieldTask default (BG renderers); 4 MsgSystem, TextPanels.

### 4.2 Preload cache (0x80075768, 12 x 20 bytes; debug printf at unindexed 0x80057328)

| Off | Type | Name in the debug string |
|---|---|---|
| +0x00 | u8 | uType (0 free; 1 battle/side preloads, tag -1; 2 map-transfer preloads, tag = ctx+0x109c) |
| +0x01 | u8 | ReadFlag (set by the LoadQueue when the read is done) |
| +0x02 | u8 | owns buffer |
| +0x04 | s16 | tag |
| +0x08 | int | nFileNo (archive entry) |
| +0x0c | int | nLen (0 = whole entry; > entry size = read runs into the following entries) |
| +0x10 | void* | pBuffer |

`preload_start` refuses when free heap (0x80013428) < bytes + reserve (0x800761a4 = 0x14000,
/3 on the world map unless 0x8007599c). `field_load_entry` checks this table first, so a
preloaded map container / party pack / music costs no seek at load time. Saved/restored around
program swaps (`preload_save_table` / `preload_restore_table`, handoff 10/0xc in part2).
`preload_defragment` and part2's `field_restore_preloads` move buffers through VRAM
(x 0x140, 0x2c0 halfwords wide).

### 4.3 MsgSystem (0x20c bytes, vtable 0x800731a4, task prio 4, global 0x80075288)

| Off | Type | Meaning |
|---|---|---|
| +0x04 | vtable* | 0x800731a4 (+0xc = msg_system_update) |
| +0x08 | MsgCells | free-list head (sentinel) |
| +0x1f4 | MsgCells* | → +0x08 |
| +0x1f8..+0x204 | s32[4] | default window x, y, w, h (VM sets 2, 1, 0x18, 4) |
| +0x208..+0x20a | u8[3] | window tint r,g,b; +0x20b u8 tint enabled (accessors 0x80058f58-0x80058fbc) |

Windows: `MsgWindow *message_windows[20]` (0x80075668). 0-9 script windows (7 = notice), 10-19
use the 0xfc2 font. Coordinates in msg_window_create: positive = cells (8 px) / lines (13 px),
negative = pixels; width clamped 6..40 cells.

**Text cells (VRAM).** 31 x 19 cells of 8x13 px, 4-bit: cell i at VRAM
(0x1c0 + 2·(i % 31), 0x100 + 13·(i / 31)). A window owns w·lines cells (`msg_cells_alloc`,
first fit, ≤ 3 runs per line). `text_render_line` renders a line into a 4-bpp bitmap (scratchpad
0x1f800000 when ≤ 0x700 bytes) and LoadImages each run; the window draws one SPRT per run
(CLUT 0x7fc2 / 0x7fcf = row 0x1ff from #3110).

**MsgCells** (0x1ec): +0 u16 cells, +2 u8 used, +4 {s32 col, s32 row}[16 lines][3 runs],
+0x184 u16 run_len[16][3], +0x1e4 next, +0x1e8 prev.

**MsgWindow** (0x168, `msg_window_construct`):

| Off | Type | Meaning |
|---|---|---|
| +0x00..+0x06 | s16 | x, y, w, h (pixels) |
| +0x08/+0x0a | s16 | move start x, y |
| +0x0c..+0x12 | s16 | drawn rect x, y, w, h (animated) |
| +0x14..+0x20 | u32[4] | body gradient colours (config +0x30..+0x3c) |
| +0x25 | u8 | text speed index |
| +0x27 | u8 | state: 1 idle, 2 opening, 3 delay, 4 printing, 5 scrolling, 6/7 wait button (7 = then clear), 8 timed wait, 9 closing, 10 closed |
| +0x28 | u8*[16] | page ring; +0x68 read, +0x69 write, +0x6a u16 offset in page |
| +0x6c | {s32 x, s32 y, u16 frames, u16 t}[8] | move ring; +0xcc read, +0xcd write |
| +0xd0 | s16 | state timer |
| +0xd4 | MsgCells* | text cells |
| +0xd8 / +0xf8 | u16[16] | 1bpp→4bpp LUTs: text colour / shadow |
| +0x120 | MsgLine* | line records (0x198 each; ring of +0x134 lines, first +0x124, used +0x126) |
| +0x12c / +0x12e | s16 | scroll remaining / offset |
| +0x130 / +0x131 | u8 | draw frame / body (0 for ext 0x6c/0x6d) |
| +0x134 | s16 | line count (ceil(h/13), ≤ 16) |
| +0x136 | u8 | OT slot offset (draw at 0x810 + it) |
| +0x139 | u8 | hold (stay open after the last page) |
| +0x140 | u8 | button advance allowed |
| +0x14a / +0x14e | u8 / s16[2] | anchored to a 3D point / its projected position (overlay 0x80086bbc) |
| +0x14b | u8 | word wrap |
| +0x152 / +0x160 | u8 / RECT | custom clip |
| +0x154 | s32 | line height 13 |
| +0x15c | fn* | called when the window finishes closing |

### 4.4 Notice banner (0x3c, vtable 0x80073194, prio 0, global 0x800752c0)

One-line banner in window 7 sliding in from the right (ext 0x84/0xb9): +0x14 state (0 idle,
1 slide in 16 frames, 2 open window, 3 shown, 4 slide out), +0x18 window, +0x1c/+0x20 x/y,
+0x24 hold, +0x28 text width, +0x2c text, +0x30 timer flag, +0x34 at_top, +0x38 close request.
Opening any of windows 0-4 dismisses it.

### 4.5 Afterimage (0x88, vtable 0x80073528, prio 2; list 0x800752ec[12])

+0x08 count (≤ 12), +0x09 ring index, +0x0b colour mode, +0x0c finished, +0x0d effect for the next ghost
(stored +1 per ghost: 1 fade only, 2 grow (sprite scale +0x64/+0x68), 3 spin (+0x8c), 4 grow + spin), +0x0e u8[12] ghost effect, +0x1c owner back-pointer,
+0x20 rgb, +0x24 s32[12] ghost brightness, +0x54 source Actor*, +0x58 s32[12] actor slots
(0x82-0xc1). Setters for +0x0d / +0x0c / +0x1c are unindexed (0x8005f58c-0x8005f5c4).

### 4.6 SceneRenderer / OT layers (8 bytes, vtable 0x80073538, prio 1, global 0x800752d8)

Four layer OTs (0x800761c4[i], each `{u8 buf; u8 dirty; pad; OT[2][0x830 words]}` = 0x20c0 per
buffer) spliced into the main OT at their layer id + 0x11 by `actor_sys_draw`. The update flips
and clears them, then draws DAT_800761a8 scene objects (0x800761ac, 0x50 bytes: +0 u16 type,
+4 layer) through the overlay dispatcher 0x8008a9cc (handlers at 0x8008ed10 by type). The objects
are created by overlay 0xc1d code at 0x80089b40.

### 4.7 Camera block (0x80075a80-0x80075c44)

| Addr | Meaning |
|---|---|
| 0x80075a80 | u8 mode: 0 = angles at a90/a94/a98, 1 = look-at (set by actor_sys_setup_view) |
| 0x80075a84..a8c | eye (20.12) |
| 0x80075a90..a98 | target (20.12) or angles |
| 0x80075ab4 | zoom 0x1000 (0xccc world) |
| 0x80075ab8 | s16 distance (look-at mode) |
| 0x80075aba..abe | light colour (0x80) |
| 0x80075ac0..ac4 | colour-matrix light colour |
| 0x80075ac8 | MATRIX view rotation |
| 0x80075b28..b2c | camera offset vector |
| 0x80075b30..b32 | ambient (0x1e) |
| 0x80075b34..b40 | yaw/pitch angles, distance |
| 0x80075b44..b48 | lights rotated into view space |
| 0x80075b64 / ba4 / bc4 | light / colour / extra matrices |
| 0x80075c15..c17 | world far colour; 0x80075c34/c38 world geom offset adjust; 0x80075c3c..c44 light angles |
| 0x800746ec..f4 / 0x800746fc..704 | offset / scale vectors (0, 0x1000) |

### 4.8 ScriptCtx (0x11cc, vtable 0x80074028, prio 0, global 0x800752ac)

`vm_ctx_create` clears 58 thread slots (+0x4c, 0x48 each → +0x109c) and 16 pairs at +0x10c4.
`vm_init` (after section 1 is unpacked): +0x10 IP, +0x14 code base, +0x18 variables end,
+0x1c flags end, +0x20 call-stack area (handoff9 +0), +0x24 value-stack area (handoff9 +0x100),
+0x28 string table, +0x2c text, +0x109c/+0x10a0 = -1 (transfer tag), +0x10a8/+0x1198 = -1,
+0x10b0 = 1, +0x11a4 align4(hdr5), +0x11a8 hdr6, +0x11ac.. 8 words = 0x10001. Also sets the
default message rect (2, 1, 0x18, 4).

### 4.9 Field state block (handoff 9, 0xa40 bytes; `field_state_create`)

| Off | Contents |
|---|---|
| +0x000 | VM call-stack area (0x800756fc) |
| +0x100 | VM value-stack area (0x80075700) |
| +0x180 | script variables (0x80075708 = field_flags 0x8007570c); the GameState at +0x220 lies inside the variable area (script byte address 0xa0 = GameState+0) |
| +0x220 | GameState (0x2a0): +0x08/+0x0c/+0x10 position, +0x18 angle, +0x1a map, +0x1c music, +0x24/+0x25 leader/partner, +0x28 names (20 bytes each), +0x118 text speed, +0x26c.. vehicle positions |
| +0x8c0 | script flags (0x170 bytes used; 0x80075704) |

New Game defaults: map/position zero, music -1, leader = config+0x45 (Claude 0 / Rena 1),
game_state+0x1a (map) = 7 (Claude) or 0x12 (Rena), game_state+0 = 3 / 4, text speed +0x118 = 3.
Name slots 0-11 (character id - 1): Claude, Rena (the entered name replaces the chosen hero's),
Celine, Bowman, Dias, Precis, Ashton, Leon, Opera, Ernest, Noel, Chisato.

---------------------------------------------------------------------------------------------

## 5. Globals (new in this range)

| Address | Name | Type | Meaning |
|---|---|---|---|
| 0x80075330 | g_scene | MapScene* | section 0 block 0 |
| 0x80075334 | g_scene_cells | Cell* | grid + 4 (cell 0) |
| 0x80075338 | g_scene_grid | u8* | scene + [0x80] |
| 0x80075714 | g_map_font | u8* | section 1 block 1 (swapped with 0x800762f0 for windows 10-19) |
| 0x80075718 | g_map_glyphs | u16* | font + count + 8 |
| 0x800756f8 | g_map_script | u8* | section 1 block 0 |
| 0x80075764 / 0x80075766 | g_scene_h / g_scene_scale | u16 | scene +0x6a / +0x68 |
| 0x80075768 | g_preload | PreloadEntry[12] | 4.2 |
| 0x80075858 | g_preload_saved | PreloadEntry* | 0xf0-byte copy |
| 0x8007585c | g_sprite_slots | {void*; int id; int size}[16] | sprite packs (part2) |
| 0x8007591c / 0x8007597c / 0x80075994 | g_tasks / g_task_prio / g_task_count | | 4.1 |
| 0x80075668 | message_windows | MsgWindow*[20] | (in globals.csv) |
| 0x800751b8 | g_text_speed | int[5] | current speed table (from 0x800751cc[game_state+0x118]) |
| 0x800752c0 | g_notice | Notice* | |
| 0x800752d8 | g_scene_renderer | SceneRenderer* | |
| 0x800752ec | g_afterimages | Afterimage*[12] | |
| 0x800752a8 | g_field_panel | FieldPanel* | 0x6c object (unknown role) |
| 0x8007528c | g_bg | BgRenderer* | overlay object (field 0x128 / world 0x5a0) |
| 0x800756b8 / 0x800756bc | g_ovl_a / g_ovl_a_packed | | 0x801e9000 overlay buffer / section 5 copy |
| 0x800756c0 / 0x800756c4 | g_ovl_b / g_ovl_b_packed | | 0x801f9800 overlay buffer / sections 7-10, 12 copy |
| 0x800756c8 / 0x800756cc | g_sec11_packed / g_sec11 | | entry #2 copy / unpacked (inventory+0x820) |
| 0x800756f4 | g_embedded_program | u8* | section 6 copy |
| 0x800762e8 / 0x800762f0 / 0x800762f4 / 0x800762f8 | g_side_text_raw / g_menu_font / g_menu_glyphs / g_menu_strings | | entry 0xfc2 |
| 0x80076230 / 0x80076240 / 0x80076244 / 0x8007624c | g_extra_pack_data / _id / _tpage / _clut | [4] | extra packs (field_load_extra_pack) |
| 0x80076254 / 0x80076256 | g_extra_clut_row / g_extra_vram_x | s16 | 0x1f8 / 0x280 at map load |
| 0x80075358 | g_overlay_is_world | int | overlay currently loaded (field_flags+0x68 of last map) |
| 0x80075744 / 0x80075746 | g_music_pending / g_music_entry | u8 / s16 | |
| 0x800761a4 | g_preload_reserve | int | 0x14000 |
| 0x800761a8 / 0x800761ac / 0x800761c4 | g_scene_obj_count / g_scene_objs / g_ot_layers[4] | | 4.6 |
| 0x8007627e | g_screen_clamp | s16 | field_update_screen_steering (guess) |
| 0x80074628 / 0x8007462a | g_option_a / g_option_b | u16 | accessors only |
| 0x80075998 / 0x800759a0 | g_battle_stream_timer / g_battle_stream_state | int | battle-data streaming (part2 LoadQueue); accessors 0x80055e80-0x80055eb0 |
| 0x8007533c / 0x8007533d | g_preload_release_pending | u8[2] | |
| 0x80075a80.. | camera block | | 4.7 |
| 0x8007531c | g_frame_ticks | int | vblanks per frame (part2) |

---------------------------------------------------------------------------------------------

## 6. vm_exec_ext review (opcode identifications from this range)

Already in script_opcodes.csv as args-only; semantics established here:

| Opcode | Args | Meaning |
|---|---|---|
| 0x64, 0x68, 0x6c (7 inline / 5 stack) and 0x65, 0x69, 0x6d (8 / 6) | x, y, w, h, string (lo, hi inline), [window] | `msg_window_open`; the short forms use window 0. 0x68/0x69: +0x139 = 1 (hold); 0x6c/0x6d: +0x130/+0x131 = 0 (no frame), others 1 |
| 0x66 0x67 0x6a 0x6b 0x6e 0x80 | - | `msg_window_close(msg, 0)` |
| 0x6f | string, window, ... | `msg_window_open_default` |
| 0x81 | window | `msg_window_close` |
| 0x83 | window | R = window exists |
| 0x84 / 0xb9 | (unused), string lo, string hi, hold | notice banner (`notice_show`, window 7, top when the leader is low on screen) |
| 0xa5 | map, x, z | preload map container and party pack A for a coming transfer |
| 0xaa | value, window | window +0x24 |
| 0xba / 0xd2 | x, y, frames | move window 0; 0xf5 (x, y, frames, window) moves window n |
| 0xd3 / 0xd6 | - | R = party_member_mask() |
| 0xda | (map_transfer_c) | prunes preloads (`preload_release(1)`, `preload_prune_for_map`) |

---------------------------------------------------------------------------------------------

## 7. Functions in this range missing from index.csv

| Address | Notes |
|---|---|
| 0x80055da8 | empty (`jr ra`) |
| 0x80055db0, 0x80055dbc | varargs stubs (store a1/a2 to the stack, return): debug print stubs |
| 0x80055dc8 | `obj+8 = a1` setter |
| 0x80055e58 | `*out = 0x80075a78; return 0x80075a7c` |
| 0x80055efc | **script_flag_set(n)** |
| 0x80055f78 | char_name_ptr(n) = game_state + 0x28 + 0x14·n |
| 0x80055f98 / 0x80055fb0 | return field_flags byte +0x30 / +0x31 |
| 0x80055fc8 | **game_state_save**: copies GameState (0x2a0) and script flags (0x170) into handoff 0xe |
| 0x8005691c | preload_evict_lowest: frees the owned entry with the lowest buffer address (caller 0x8004b76c) |
| 0x80056b44 | preload_forget(entry): clears the entry without freeing |
| 0x80056bec | sum of entry sizes for a 0-terminated s16 list of negative entries |
| 0x80057328 | preload debug dump (printf 0x80023f40, format 0x800730cc) |
| 0x80057450 | **field_task_create** (vtable 0x8007310c, prio 3); called by overlay ctors |
| 0x80057808-0x80057b28 | switch cases of notice_update |
| 0x80058354 | notice +0x14 = 0 |
| 0x80058f58-0x80058fbc | MsgSystem accessors: tint ptr/enabled/set, default w/h, x, y |
| 0x8005f58c-0x8005f5c4 | Afterimage setters (+0x0d, +0x0c = 1, +0x1c) |
| 0x8005ff84 (1056 B) | GTE helper (rsin/rcos, 0x8006ecf0/0x8006ed5c) called by both overlays (0x8008b480, 0x8008ba08; 0x800831e4, 0x800833b0, 0x8008990c) |
| 0x8006053c (4940 B) | GTE renderer writing to the screen primitive cursor, called by the world overlay (0x80083264): world terrain draw (guess) |
| 0x80061cf8 (1256 B) | sprite pixel resampler for scale > 0x1000 (scratchpad 0x1f800040), called by the sprite cache (0x80044bd8) |

---------------------------------------------------------------------------------------------

## 8. Porting notes

- **Data path, no emulation needed:** map container → sections → (scene, textures, CLUTs,
  script, font, sprite pack) is plain parsing plus SLZ; every structure above is position
  independent (offsets are relative). Port `map_resolve_entries` as a pure function.
- **VRAM layout must be kept** (or emulated as a 1024x512 16-bit surface): map texture pages
  at x ≥ 0x200 (page slot = MapScene +0xec index), map CLUTs rows 0x1e0+, extra packs rows
  0x1f8+ / x 0x280+, text cells (0x1c0-0x1fd, 0x100-0x1f6), #3110 atlas (0x180-0x1bf,
  0x100-0x1ff) and CLUT rows 0x1fe/0x1ff. The BG sprites, actors and windows reference
  textures by tpage/CLUT words.
- **GPU primitives built here:** SPRT 0x64/0x66 (+DR_MODE 0xe1000216/0217/0236), POLY_FT4
  0x2c/0x2d/0x2e, POLY_G4 0x38, DR_AREA (SetDrawArea) for window clipping; LoadImage /
  StoreImage / DrawSync / ResetGraph / SetDispMask. Window text goes through VRAM uploads every
  time a line is printed.
- **OT model:** forward OT of 0x830 entries; BG sprites at layer + 0x11, actors via cell
  list 2 layer ids, message windows at 0x810+, notice at OT words +0x2040/+0x2044 (indexes
  0x810/0x811). A port can sort by (OT index, insertion order reversed).
- **GTE:** gte_ldH, SetGeomOffset, SetFarColor, SetFogNearFar, RotMatrix (0x8006e52c),
  RotMatrixZ (0x8006eab8), MulMatrix, SetRotMatrix, SetTransMatrix, TransRot_32, rtv0/rtir,
  sqr, ratan2, SquareRoot0, Push/PopMatrix. The screen-space collision cells depend on the exact
  projection (`field_try_talk` projects with H = scene+0x6a and offset = scene +0x34/+0x36 / 2),
  so the port's projection must match the PSX integer math.
- **Overlays:** the BG renderer, actor movement/collision, actor drawing and scene objects are
  in overlay 0xc1d (#3101, 0x10d70 bytes); the world map in 0xc1e (#3102). Both call back into
  this range (task base ctor 0x80057450, 0x8005ff84, 0x8006053c). They need their own catalog
  pass before the first map can be walked; the functions cited here are the entry points.
- **Dependencies on other ranges:** part2 (field_load_entry, map-section parser, sprite packs,
  music, LoadQueue, ActorSystem, Screen), part4 (VM: vm_push_frame, vm_thread_update), boot
  (loader, SLZ, upload_texture / upload_clut256, mem_alloc_high/c, handoffs, sound).
- **Heap tricks to drop:** fixed-address overlay allocation loops (0x801e9000 / 0x801f9800),
  `preload_defragment` and the slot-1 copy dance are fragmentation workarounds.
- **Text:** implement the byte-varint decoder and the control codes in 3.2; the glyph
  rendering is 12-px 1-bpp with a 1-px shadow into 4-bpp with per-colour LUTs; widths come from
  the map font (+1 px spacing). Word wrap breaks at class changes (letters / digits / kana /
  codes ≥ 0x107).

## 9. Open questions

- MapScene sub-blocks 3, 5, 6 and list types 4/6/8/9 (shading regions?) need the overlay
  0xc1d code (0x80085b6c, 0x8007e704) read in full.
- Map exits/doors: not in the scene lists; likely script-side position checks (part4 / VM
  wait states) — still open.
- World chunk terrain (section 3 tags 3/4) format.
- Meaning of scene +0x64, +0x72..+0x77, sub-block 4 record layout beyond the fields used.
