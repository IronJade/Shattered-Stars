# Field code overlay 0xc1d (#3101) at 0x8007e000

Function rows: `overlay_c1d.csv` (module `field_ovl_c1d`, 108 functions: 66 confirmed, 35 likely,
7 guess). Image `analysis/ovl_3101_8007e000.bin` (0x10d70 bytes; code 0x8007e000-0x8008ebd0, data
0x8008ebd0-0x8008ed70). Field names (`bg_update_scroll` etc.) are the CSV names; field-program
functions use the catalog names from `docs/catalog/functions.csv` + `part*.csv`.

Reference images built from the rules below (scroll 0, walk data overlaid; red = obstacles, green =
floor triangles): `scratch/ovl_c1d/bg7_walk.png` (map 7), `bg78_walk.png` (map 78, parallax),
`bg4_walk.png` (map 4, animated pieces). Script: `scratch/ovl_c1d/compose_bg.py MAP`.

## 1. What the two overlays are

- `field_load_map` sets `field_flags+0x68 = (map - 1) < 3`. Maps 1-3 are the **world map** and use
  **0xc1e** (#3102); every other map uses **0xc1d** (#3101). The overlay is reloaded only when the
  flag differs from `0x80075358` (the flag the resident overlay was loaded for). Menu page 20 reloads
  the same entry after a menu overwrote 0x8007e000.
- 0xc1d is the **normal field map** code: prerendered-background renderer, field actor class, walk
  and collision on the map's floor/obstacle data, camera pan script, an edge-glow effect, a particle
  system, and a few script helpers. Section 3 and the world-map hooks are not used by it.
- Every field call into 0x8007e000+ that is specific to one overlay is guarded by
  `field_flags+0x68` (checked: field_load_map, actor_sys_restore_all, field_update, field_side_load,
  field_unload_map). The c1e addresses in older notes (0x80082d8c/dd4/e5c, 0x800829fc, 0x80082b6c,
  0x80088814, 0x800888e0, 0x80088a3c, 0x80089368, 0x8008d478, 0x8007e000 as "world helper") are
  **world-map code in 0xc1e**; in 0xc1d those addresses are inside unrelated functions.

### index.csv entries that are not functions in 0xc1d

Ghidra created functions wherever the field image calls 0x8007e000+, including calls meant for menu
page code (also loaded at 0x8007e000) and for 0xc1e. These 22 entries are mid-function in 0xc1d;
their decompilations are fragments (look for `in_t0`, `unaff_s4`):

| index.csv | Real function | Who really calls that address |
|---|---|---|
| 0x8007e550, 0x8007e594, 0x8007e5c0, 0x8007e5e0 | inside 0x8007e540 | menu page code (pages 6, 9, 3, 16) |
| 0x8007e61c | inside 0x8007e600 | menu page 2 |
| 0x8007e740, 0x8007e790, 0x8007fc1c, 0x80080300, 0x80080510, 0x80080b90 | inside 0x8007e704 | menu pages 5, 15, 10, 12, 13, page-window destroy |
| 0x800829fc, 0x80082b6c | inside 0x80082838 | 0xc1e (world trigger add / test) |
| 0x80082d8c, 0x80082dd4, 0x80082e5c | inside 0x80082cfc | 0xc1e (world actor ctors) |
| 0x800886bc, 0x80088814, 0x800888e0, 0x80088a3c, 0x80089368 | inside 0x80086f00 (bg_draw) | 0xc1e (world BG); 0x800886bc is a block split off by recovery |
| 0x8008d478 | inside 0x8008d3d4 | 0xc1e (terrain query) |

Use `analysis/decomp/ovl3101_fixed/80086f00_FUN_80086f00.c` for bg_draw. 19 real functions were
missing from index.csv: 0x8007e600, the 16 BG accessors 0x80089a50-0x80089b30, 0x8008a8e4,
0x8008e9f4. The decompiler also drops stores in places (e.g. 0x8008d2dc); check with the
disassembler when a field looks unset.

## 2. Objects and vtables (g++ 2.x: entry n = `{s16 delta, s16 0, u32 fn}` at vptr + 8n)

| Object | Size | Global | vptr (at) | Ctor | Task prio | Entries |
|---|---|---|---|---|---|---|
| GlowFx | 0x1c | 0x800752dc | 0x8008ebd0 (+4) | `glow_create` 0x8007e058 | 1 | [1] `glow_draw` |
| FieldActor | 0x3fc | field_actors[] | 0x8008ebe0 (**+0x3f8**) | 0x8007e478 / 0x8007e4b8 / 0x8007e540 | (ActorSystem) | [1] `fieldactor_draw` 0x8007e704, [2] `fieldactor_player_control` 0x80082838, [3] `fieldactor_camera_follow` 0x80081b54, [4] `fieldactor_move_step` 0x80081cc8 |
| BgRenderer | 0x128 | 0x8007528c | 0x8008ec08 (+4) | `bg_construct` 0x8008672c | **3** (FieldTask base 0x80057450) | [1] `bg_draw` 0x80086f00, [2] `bg_destroy` 0x800867b0, [3] `bg_suspend` 0x800895e0 (+0x1c), [4] `bg_resume` 0x800897bc (+0x24), [5] `bg_enable_tint` 0x80089a48 (+0x2c), [6] `bg_set_tint` 0x80089988 (+0x34) |
| CamScript ("helper 0x17c") | 0x17c | 0x800752b4 | 0x8008ecf8 (+4) | `camscript_construct` 0x8008e100 | 1 | [1] `camscript_update` 0x8008e7c8, [2] `camscript_destroy` 0x8008e16c |
| Particle pool | 0x400 x 0x50 | 0x800761ac (count 0x800761a8) | type -> PMF table 0x8008ed10 | `fx_pool_create` | (driven by scene_renderer_update) | 9 behaviours |

FieldActor adds **no members**: 0x3fc = the 0x3f8-byte Actor base + vptr. The world-map class in 0xc1e
is 0x44c. The ctors differ only in which base init they call (none / `actor_init_screen` /
`actor_init` + `fieldactor_place`). `fieldactor_destroy` 0x8007e600 is emitted but never referenced.

Task order per frame (`task_run_all`, prio 0..4): VM -> LoadQueue, fades, Callways, CamScript, GlowFx,
scene renderer (particles) -> ActorSystem (update + draw actors) -> **BgRenderer (bg_draw)** ->
messages. So the camera target is final before bg_draw, and within one OT entry the BG prims (added
later) are drawn **before** the actor prims (addPrim prepends).

## 3. Background renderer

### 3.1 BgRenderer object (0x128)

| Off | Type | Meaning |
|---|---|---|
| +0x04 | vptr | 0x8008ec08 |
| +0x08 / +0x0a | s16 | camera target, BG pixels (actor sx, sy-16; script B 0x42; CamScript) |
| +0x0c / +0x0e | s16 | scroll: BG pixel at the screen's top-left (before +0x10) |
| +0x10 / +0x12 | s16 | screen offset when the BG is narrower than 320 / shorter than the view (centring) |
| +0x14 | u32[4][16] | DR_TPAGE values: [abr j][slot i] at +0x14 + 4i + 0x40j, from `bg_build_tpages` |
| +0x114 / +0x116 | s16 | scroll mode x / y: 0 = centre on target, 1 = proportional (target*(bg-320)/bg). Ctor 1; field_load_map sets 0 after placing the leader; script B 0x49 |
| +0x118 | s32 | reference y (camera target's world y; scene+0x58 initially); no reader found |
| +0x11c | u8 | tint enabled (vtable[5]; script B 0x43/0x44 via fade code) |
| +0x11d | u8 | 1 = multiply piece colours by the tint, 0 = replace them (ctor 1) |
| +0x11e..+0x120 | u8 | tint RGB (128 = neutral) |
| +0x121 | u8 | shake amplitude: scroll += rng%amp - amp each frame (script ext 0xf6) |
| +0x122 | u8 | view height 224 instead of 240 (ctor 1; field_load_map sets it to scene h > 240; script B 0x4a) |
| +0x124 / +0x126 | s16 | fixed scroll x / y, 30000 = off (script B 0x4b / 0x4c) |

`bg_update_scroll` (0x800867d8), called by camera follow, player control, CamScript, scripts, the
ActorSystem (free camera) and field_load_map:

```
ox, oy = scene+0x28, scene+0x2c        (s32; 0 on all maps seen)
if w <= 320: off_x = (320 - w)/2; scroll_x = 0
else: off_x = 0
      scroll_x = mode_x ? (s16)((u16)tx - ox) * (w - 320) / w
                        : tx - ox - 160
      clamp scroll_x to [0, w - 320]          (negative -> 0 first, then the upper bound)
view = tall ? 224 : 240
if h <= view: off_y = (240 - h)/2 (240 even when tall); scroll_y = 0
else: off_y = 0; scroll_y = mode_y ? (s16)((u16)ty - oy) * (h - view) / h : ty - oy - view/2
      clamp scroll_y to [0, h - view]
if fix_x != 30000: scroll_x = fix_x;  if fix_y != 30000: scroll_y = fix_y
if g_frame_ticks && shake: scroll_x += rng()%shake - shake; scroll_y += rng()%shake - shake
```

`bg_get_view_origin` (0x80086bbc) = `scroll - off + (ox, oy)`: the BG pixel at screen (0,0). Every
screen-space drawer subtracts it (actors, message windows, particles via 0x800761b0, script B
0x32/0x33/0x40/0x41).

### 3.2 Map section 0 (SLZ chain) as the renderer sees it

Block 0 = scene header (`g_scene` 0x80075330), block 1 = texture header, blocks 2.. = texture pages,
then the CLUT block if any. `field_load_map_sections` uploads pages and CLUTs, then calls
`bg_build_tpages` and `actor_sys_setup_view`.

**Block 1 (texture header)**: `s32 tex_count, s32 clut_count, u32 0x10, u32 size`, then tex_count x
`{u8 pos, u8 depth, u8 pad[6]}`. Slot `pos` = VRAM (0x200 + (pos & 7)*64, (pos >> 3)*256); depth 0/1/2 =
4/8/16-bit. A 16-bit 256x256 page covers 4 slots (pos, pos+1, pos+2, pos+3). Map 7: 4 pages, depth 2,
pos 0, 4, 8, 12 = (512,0) (768,0) (512,256) (768,256); no CLUTs. CLUT block: clut_count x 0x204
`{u8 mode, pad[3], u16 clut[256]}` -> row 0x1e0+i (mode 0 clears colour 0 of every 16-colour bank,
mode 1 only colour 0).

**Block 0 (scene header).** Size 0x12c, then sub-blocks located by the offset table at +0x78. Sub-block
sizes (counts x record size) match the offset table on 820 of 827 maps; the other 7 have 4 bytes of
padding after the piece records. Map 7 values in the last column.

| Off | Type | Meaning | Map 7 |
|---|---|---|---|
| +0x04 / +0x06 | u16 | BG image width / height | 320 / 480 |
| +0x08..+0x10 | s32[3] | camera eye (map units) | -68, 743, 3097 |
| +0x14..+0x1c | s32[3] | camera target; projects to the BG centre | -269, -213, 514 |
| +0x20 / +0x24 | s32 | BG centre (w/2, h/2) (not read by c1d) | 160 / 240 |
| +0x28 / +0x2c | s32 | BG origin offset (scroll, view origin, walk grid) | 0 / 0 |
| +0x30 | s32 | roll | 0 |
| +0x34 / +0x36 | u16 | projection size: GeomOffset = (w/2, h/2) | 320 / 480 |
| +0x38 | s16[9] | camera rotation, stored **transposed** (column-major) vs the GTE matrix; not read | |
| +0x4c | s32[3] | eye again | |
| +0x58 | s16 | default start y / BG +0x118 | 0 |
| +0x5a | s16 | default actor OT layer (actor_init: +0x22 when -1) | 0x80 |
| +0x5c / +0x5e / +0x60 | u16 | piece counts: static / parallax / scroll-layer | 9 / 0 / 11 |
| +0x64 | u16 | BG animation records (0xa0 each, at +[0x88]) | 0 |
| +0x68 | u16 | world scale % (sprites, shadows, talk reach) | 30 |
| +0x6a | u16 | projection distance H | 386 |
| +0x6c..+0x6e | u8 | ambient actor colour (lighting base) | 0x50 0x78 0x73 |
| +0x6f..+0x71 | u8 | clear colour (shows through 0x0000 texels) | 0 0 0 |
| +0x72 | u16 | colour animations (0x14c each, at +[0x8c]) | 0 |
| +0x74 | u16 | parallax layers (0x10 each, at +[0x90]) | 0 |
| +0x76 | u16 | scroll layers (0x24 each, at +[0x94]) | 3 |
| +0x78..+0x94 | u32[8] | offsets: tex rects, pieces, walk block, (+0x84 unused 0x2c-byte records on 9 maps), BG anims, colour anims, parallax, scroll layers | 0x12c, 0x1cc, 0x35c, 0x11f0 x5 |
| +0x98 / +0x99 / +0x9c | | colour-cycle count / mode / entries (ActorSystem) | 0 |
| +0xcc | s16[8] | facing for d-pad directions Up, Up-Right, ..., Up-Left (4096 = 360) | 0xbf2 0xdf1 0xff1 0x1f1 0x3f1 0x5f1 0x7f1 0x9f1 |
| +0xdc / +0xde | s16 | camera rotation x / y (actor_sys_setup_view) | 231 / 50 |
| +0xe8 | u32 | 16 | 16 |
| +0xec | {u8 used, u8 depth, u8, u8}[16] | texture slot table (only the first slot of a page is marked) | slots 0, 4, 8, 12, depth 2 |

**TexRect** (8 bytes, `+[0x78]`, index from piece/anim frames): `u8 u, u8 v, u8 w, u8 h, u8 flags,
u8 clut_x (x/16), u8 clut_row (+0x1e0), u8 scale%`. Flags: bit 0 semi-transparent, bits 1-2 abr
(selects the tpage variant; the FT4 path uses it only when semi), bit 3 mirror x, bit 4 mirror y.
Scale 100 ('d') with no mirror -> SPRT, otherwise POLY_FT4 of size w*scale/100 x h*scale/100.

**Piece** (0x14, `+[0x7c]`; static first, then parallax, then scroll-layer pieces):

| Off | Type | Meaning |
|---|---|---|
| +0x00 / +0x02 | s16 | position in BG pixels |
| +0x04 | u16 | TexRect index |
| +0x06 | u8 | texture slot (tpage x/y = slot as in block 1) |
| +0x07 | u8 | layer: OT index = layer + 0x11 (static, parallax) or layer + 1 (scroll pieces); for parallax and scroll pieces it is also the id of the parallax / scroll layer they move with |
| +0x08 | s16 | BG animation index (-1 none) |
| +0x0a | s16 | colour animation index (-1 none) |
| +0x0c..+0x12 | s16 x0, x1, y0, y1 | visible only while x0 < scroll_x < x1 and y0 < scroll_y < y1 (precomputed overlap test) |

Map 7 static pieces (all OT layer 100, slots 0/4/8/12) assemble the 320x480 picture from y 84 down;
the three scroll layers (ids 50, 60, 150) carry the clouds/sky band, 498 px wide, drifting left.

**BG animation** (0xa0, `+[0x88]`): `u16 timer, u16 cur_rect, u8 count (<=12), u8 flags, u16 cur_slot,
u8 cur_r, cur_g, cur_b, u8 id, s16 cur_dx, cur_dy`, then frames at +0x10, 12 bytes each:
`u16 rect, u16 duration, u8 r, g, b (128 = neutral), u8 slot, s16 dx, s16 dy`. Flags: 0x80 frozen,
bit 0 ping-pong, bit 1 one-shot (hold at the end), 0x10 set by bg_anim_play modes 0/1. 257 maps use
them (data flags are only 0 or 1). Script access by `id`: B 0x65 `bg_anim_play`, 0x68
`bg_anim_get_state`, 0x6e `bg_anim_get_frame`, 0x77 `bg_anim_set_frame`; ext 0xe1 = 0x80076194 (show
animated pieces), B 0x67 = 0x80076195 (0 = draw animated pieces even outside their window).

**Colour animation** (0x14c, `+[0x8c]`, 102 maps): `u16 n_pieces, u16 count, u16 timer, pad, u16
pieces[n] (+0x08; informative, pieces point back via +0x0a)`, frames at +0x88: `{u16 duration, u8 r,
g, b, pad[3]}` (<= 24), current colour +0x148..+0x14a, +0x14b ping-pong. Timer advances by
`g_frame_ticks * 0x8007535c / 100` (speed %, 100 at map load).

**Parallax layer** (0x10, `+[0x90]`, 25 maps): `u8 id, pad[3], s16 base_x, base_y, s16 rate_x%,
rate_y%, s16 cur_x, cur_y`. cur = base + ((bg - 320)/2 - scroll) * rate / 100, or base + override
when 0x80076224/0x80076226 != 30000 (values > 10000 also subtract 20000). Parallax pieces whose +7
equals the layer id are drawn shifted by ((bg-320)/2 - cur).

**Scroll layer** (0x24, `+[0x94]`, maps 7, 184, 384): `u8 id, u8 mode_x, u8 mode_y, pad, s32 vx, s32
vy, s16 x0, y0, x1, y1 (wrap rect), s16 clip x, y, w, h (computed), s16 off_x, off_y, s16 acc_x,
acc_y`. Mode 0: off = (off + v) % size each frame; mode 1: one pixel every |v| frames (acc counts
frames). Both are per drawn frame, not per tick. Map 7: vx = -8, -16, -24 (mode 1).

### 3.3 `bg_draw` per frame (vtable[1])

Skipped unless 0x80075354 (scene loaded) and no modal sub-program (0x800752c8 == 0 or 0x80076279).

1. Step BG animations (unless frozen): find the frame for `timer` (ping-pong adds the return leg);
   past the end: loop (`timer %= total`) or, one-shot, hold the first (ping-pong) / last frame. Copy
   rect, slot, colour, offset to the header; `timer += g_frame_ticks`.
2. Step colour animations the same way.
3. **Static pieces** (scene+0x5c), visible by window, colour:
   - tint off: animated piece -> anim colour (only if 0x80076194); colour-anim piece -> its colour;
     else 0x80,0x80,0x80;
   - tint on: +0x11d == 0 -> tint replaces the colour (static rect/slot used even for animated
     pieces); else colour * tint >> 7 (plain pieces get the tint directly).
   - Rect/slot from the animation when animated, else from the piece. Position = piece + anim
     offset + off - scroll.
   - Emit **SPRT** (len 4, code 0x64, |2 if semi) with `clut = (row+0x1e0)<<6 | clut_x`, then a
     **DR_TPAGE** word `0xe1000200 | (tpage[abr][slot] & 0x1ff)`, both at **OT[piece+7 + 0x11]**
     (DR_TPAGE inserted second, so it executes first). Scaled or mirrored rects: **POLY_FT4** (0x2c,
     0x2e if semi) with tpage in the prim and UVs from `bg_set_poly_uv`.
4. **Parallax pieces** (scene+0x5e): update layer positions, then SPRT + DR_MODE (SetDrawMode with the
   tpage) at OT[piece+7 + 0x11], window tested against the layer-shifted scroll; tint multiplies.
5. **Scroll layers** (scene+0x76, only while 0x80076198): advance offsets, compute a clip rect.
   **Scroll-layer pieces** (scene+0x60), when the layer rect overlaps the view: DR_AREA(full screen
   0, drawy, 320, 224) at OT[id + 0x11]; the SPRT plus up to 3 wrapped copies (x and/or y) and a
   DR_MODE at **OT[id + 1]**; DR_AREA(clip rect) at OT[id + 0x11].
6. Write back the primitive cursor `g_screen+0xfc`.

Quirks to keep or decide on: (a) both DR_AREAs land in OT[id+0x11], after the sprites in OT[id+1]
(the forward OT draws entry 0 first), so the clip never applies to the sprites; the clip x also uses
the layer's y0 (+0xe) instead of x0 (map 184 has x0 = 240). A port that skips the DR_AREAs matches
the hardware. (b) A tint value of 0x100 wraps to 0 in glow_draw's clamp. (c) With 0x80076194 == 0
(ext 0xe1 0) and the tint off, an animated static piece branches to 0x80087f18, which skips the
piece/record pointer increments: the loop re-tests that same piece for the remaining count, the
later static pieces are not drawn, and the parallax/scroll loops start from the wrong record. Port
it as "skip the piece" unless parity with the bug is needed.

### 3.4 OT indexes (forward-linked 0x830-entry OT, entry 0 drawn first; prims prepended)

| Index | Content |
|---|---|
| piece+7 + 0x11 | static and parallax BG pieces (+ DR_TPAGE / DR_MODE) |
| id + 1 | scroll-layer sprites and their DR_MODE |
| id + 0x11 | scroll-layer DR_AREA pair |
| actor+0x22 + 0x11 | actor sprite, shadow, light-split pieces (`actor_link_prims`: OT[16 + (+0x22 + 1)]) |
| zone+0x24 + 0x10 | actor reflection copy (list-9 zone byte +0x24 is stored as the prim's index) |
| layer id + 0x11 | particle OT layers (0x830 entries, index 0x820 - otz/8) spliced by the scene renderer |
| 0x110 | glow ring + DR_TPAGE (abr 1) |

Depth model: every BG piece and actor has a layer 0-255 and both use OT[layer + 0x11]; higher layers
draw on top, and at equal layers the actor wins (BG prims are added later). Actors start at
scene+0x5a and are moved by list-2 layer zones (and floor triangles with +0xf). Map 7 has no layer
zones: Claude (layer 0x80) draws over the canyon pieces (layer 100) and under the top cloud band
(scroll layer 150 -> OT[151]).

### 3.5 VRAM (field maps)

| Area | Use |
|---|---|
| (0x200-0x3ff, 0-0x1ff) | BG texture slots (16 x 64x256); 16-bit pages take 4 slots |
| rows 0x1e0+ | BG CLUTs (4/8-bit pages) |
| (0x141-0x1ff, 1-0xff) | sprite cache A: prescaled 15-bit sprites (tpage 0x105 = (0x140,0) 15-bit) |
| (0x141-0x1ff, 0x101-0x1ff), CLUT rows 0x1f3+ | sprite cache B: 4/8-bit sprites (tpage 0x15 = (0x140,0x100)) |
| (0x180,0x100) / CLUT row 0x1ff | #3110 icons: blob shadow (tpage 0x56, CLUT 0x7fcd, UV g_sys_icons+0x162) |

### 3.6 Suspend / resume / tint (vtable 3-6)

- `bg_suspend` (+0x1c): before leaving the map for battle: target -> field_flags+0x74/+0x76, +0x118 ->
  +0x78, fixed scroll -> +0x0a/+0x16, modes -> +0x7c/+0x7d, tall -> +0x69, +0x11d..+0x120 ->
  +0x85..+0x88, 0x80075356 -> +0x94; the first 16 bytes of every BG animation -> new blob
  0x80075728 (`u32 size` + 16n bytes; becomes handoff 0x10).
- `bg_resume` (+0x24): field_load_map calls it when field_flags+0x24 (returning); restores the above,
  also 0x80075aba/bc/be (BG tint globals), runs bg_update_scroll, copies handoff 0x10 back and frees it.
- `bg_enable_tint` (+0x2c) sets +0x11c; `bg_set_tint` (+0x34) stores RGB and sets the clear colour to
  scene+0x6f..0x71 * tint / 128. ScreenFade applies the BG tint through these every frame (part2).

### 3.7 Rendering map 7 (container #3214: sections 0, 1, 2 only)

1. Section 0: upload blocks 2-5 (0x20000 bytes each) to (512,0), (768,0), (512,256), (768,256) as
   16-bit 256x256. Scene = block 0 (0x125c bytes). `bg_build_tpages`: slots 0/4/8/12 used, depth 2.
2. Leader placed at the centroid of floor triangle 0 ((-137, 0, 2295) map units), which projects to
   BG (157, 399); target = (157, 383). +0x122 = 1 (h 480 > 240), so the view is 224 lines; with mode
   0 (set after load) scroll_y = clamp(383 - 112, 0, 480 - 224) = 256 and scroll_x = 0 (width 320:
   no horizontal scroll, offset 0).
3. Each frame: the static SPRTs whose window contains the scroll at OT[117] (6 of 9 at scroll
   (0,256); pieces 6-8 need scroll_y < 256), and the 11 cloud-layer SPRTs (+ wrapped copies) at
   OT[51] / OT[61] / OT[151] (above the view at the start). No CLUTs, animations or parallax.

## 4. Camera and projection (needed by actors and walk data)

`actor_sys_setup_view`: GeomOffset (scene+0x34/2, +0x36/2), H = scene+0x6a, rotation
`R = RotX(scene+0xdc) * RotY(scene+0xde)` (roll RotZ(scene+0x30)) times the constant
diag(1, -1, -1) (0x80072e2c), translation -eye<<12. Overlay code projects with `TransRot_32`
(translate first: v = R*(pos - eye)), then `sx = ((v.x>>4)*H + (v.z>>5)) / (v.z>>4) + w/2`, same for y
(the >>5 term rounds). Results are **BG pixels**, not screen pixels. Checked on maps 4, 7, 78: the
camera target projects to (160, 240) and obstacle outlines line up with the walls in the picture.
World y is up; positions are 20.12 fixed point (map units << 12); angles 0-4095 with 0 = +x and
0x400 = +z (cos for x, sin for z).

## 5. Walk and collision data (scene + [0x80])

```
u8 grid_w, u8 grid_h, u16 0           // grid = ceil(bg/128) + 2 in each axis (all 827 maps)
Cell cells[grid_w * grid_h]           // 0x60 each = g_scene_zones (0x80075334); base = g_scene_zone_base
Cell: u16 count[16]; u32 offset[16]   // offsets from the walk block start
```

Cell lookup (`zone_cell_at_xy`): `((y - oy)>>7 + 1) * grid_w + ((x - ox)>>7) + 1` on **BG pixels**,
-1 outside. Cell 0 (the top-left border cell) also carries the two global lists 14 and 15. Lists:

| List | Item | Space | Used by | Effect | Maps |
|---|---|---|---|---|---|
| 2 | ZoneItem 0x28 | BG px + world y range | `walk_update_layer` | actor OT layer +0x22 = item+0x24 | 663 |
| 4, 6 | light polygon 0x4c | BG px | `fieldactor_build_light_split` | tinted sprite pieces (RGB +1..+3) | 83 / 68 |
| 8 | ZoneItem 0x24 | BG px | `fieldactor_apply_light_zone` | actor colour += RGB(+1..+3) - ambient | 389 |
| 9 | ZoneItem 0x28 | BG px | `fieldactor_draw` | reflection: mirror y = item+0x26, abr item+0x25, OT item+0x24 | 16 |
| 13 | ZoneItem 0x24 | BG px | `field_try_talk` (field) | talk probe extends across it (counters) | 70 |
| 14 (cell 0) | FloorTri 0x58 | world | walk step, snap, move step | height, slopes, per-area d-pad table | 390 |
| 15 (cell 0) | Obstacle 0x30 | world | walk step, wall slide, B 0x34 | walls | 792 |

Lists 0, 1, 3, 5, 10 appear only in cell 0 of a few maps and are not read by c1d.

**ZoneItem** (`zone_test_point`): `u8 type; u8 r, g, b (lists 4/6/8); s16 y_min (+4), y_max (+6) (world
y); s16 x0, y0, x1, y1 (+8..+0xe, BG px bbox); u8 enabled (+0x10); ...; s16 points at +0x14`. Type 0
triangle (3 points), 1 quad (4 points), 2 bbox only, 3 ellipse (centre +0x14/+0x16, radius x +0x18,
radius y +0x1a). Points are clockwise: inside when every edge cross product <= 0. List-2/9 items
carry +0x24 (layer), +0x25 (abr+1), +0x26 (mirror world y).

**FloorTri** (0x58, verified on 3486 triangles):

| Off | Type | Meaning |
|---|---|---|
| +0x00 / +0x02 | s16 | y range (usually -0x8000..0x7fff) |
| +0x04..+0x0a | s16 | bbox x_min, z_min, x_max, z_max |
| +0x0c | u8 | enabled |
| +0x0d | u8 | tag: 0x7f normal, '~' start area (field_load_map uses the first '~' triangle's centroid, else triangle 0), 'x', small numbers = script groups (B 0x06) |
| +0x0e | u8 | use the triangle's own d-pad table +0x44 |
| +0x0f | u8 | set actor layer +0x22 = +0x54 while standing on it |
| +0x10 / +0x18 / +0x20 | SVECTOR | vertices v0, v1, v2 (map units); only these are tested |
| +0x28 | SVECTOR | the source quad's 4th corner (not read by c1d; quads are stored as two triangles) |
| +0x30 / +0x34 / +0x38 | s32 | normal nx, ny, nz (length 128; flat = (0, 0x80, 0)) |
| +0x3c | s32 | -(n . v0) |
| +0x40 | u16 | sprite stair step: draw y snapped to steps of this size (0 = off) |
| +0x44 | s16[8] | d-pad facing table (same order as scene+0xcc) |
| +0x54 | u8 | layer for +0x0f |

Height on a triangle: `y = (-nx*(x - x0) - nz*(z - z0) + ny*y0) / ny` (20.12 in, 20.12 out), clamped
to the vertices' y range; ny == 0 gives y0. 437 maps have no floor triangles: actors keep their y.

**Obstacle** (0x30, verified on 17899): same first 0x0c bytes, `+0x0c u8 enabled, +0x0d u8 tag ('2' =
low wall: slide angle limit 0x180 instead of 0x380; small numbers = script groups), +0x0e u8 vertex
count (4 or 3), +0x10 SVECTOR v[4]`. Two tests use them: `walk_test_obstacles` (circle of radius
actor+0x2d8 / 2 against all vertices and edges; bbox grown by the radius) and `walk_point_in_tri`
(inside v0-v2 only).

**Where the walkable area comes from**: everywhere not blocked by an obstacle; the obstacle quads
outline the walls (map 7: 14 quads along both canyon sides). Floor triangles only add height, slope
and per-area input tables. `walk_set_group_enabled` (script B 0x06) switches tagged triangles and
obstacles on/off and records the change in field_flags+0x89.. (8 entries, bit 7 = off, count +0x91),
which field_load_map replays after loading.

**Light polygons** (screen-space cell lists 4 and 6, 0x4c bytes; read by
`fieldactor_build_light_split` 0x80083d70, ported in `game/field/actor_field.cpp` and checked
call by call against PCSX-Redux): the zone header (type, colour r g b at +1..+3, y range, bbox,
enabled +0x10), corners at +0x14 closed by a copy of the first, edge lines `a x + b y + c = 0`
with a[4] at +0x28, b[4] at +0x30, c[4] at +0x38, and a same-colour flag at +0x48. List 6 gives
the flat colour under a shaded actor's feet (found by probing the feet point, then half a sprite
width left and right); list-4 polygons near the sprite (a loose box test) clip the sprite quad
into coloured pieces (FT4 / FT3, at most 22 primitives per actor). 124 maps per disc carry
list-4 or list-6 polygons. Quirks kept: the underfoot colour lives in a stack slot that is never
cleared between calls; unscaled sprites divide the UV base too; vertically flipped pieces get no
UVs; disabled or out-of-range polygons are still drawn in the drawing loop; a corner off the
cell grid ends the function early.

## 6. Field actor (FieldActor, 0x3fc)

### 6.1 Per-frame flow

```
actor_sys_update (ActorSystem, prio 2), for actors 0..0x81:
  actor_update(a, player = (slot == field_flags+0x10 && !field_flags+0x26), camera = (slot == field_flags+0xe))
    if player: vtable[2] fieldactor_player_control(a, camera)  (instead of actor_anim_advance)
    ... command ring / waits (part1) ...
    if a+0x27 (move mode): vtable[4] fieldactor_move_step(a)
    if camera: vtable[3] fieldactor_camera_follow(a)
  if field_flags+0xe == -1: bg_update_scroll (free camera)
actor_sys_draw: for each actor (sorted by depth +0x314 or screen y): vtable[1] fieldactor_draw(a, 1),
  then actor_link_prims -> OT[0x10 + a+0x378[i]]
```

### 6.2 Fields the overlay uses (beyond the part1/part2 tables)

| Off | Type | Meaning |
|---|---|---|
| +0x22 | u16 | OT/depth layer (default scene+0x5a; zones; script B 0x25/0x26) |
| +0x48 | s32 | vertical draw offset (jump arc; sprite only, shadow stays) |
| +0x54 | s32 | current speed per tick; +0x94 walk speed (run = x4) |
| +0x7f | u8 | semi-transparency: 0 off, n = abr n-1 (script B 0x79) |
| +0x8c | s16 | sprite roll, added to the camera roll ActorSystem+0x14 (0x7fff = no rotation) |
| +0x9a | u16 | current auto animation (see 6.4) |
| +0x9c / +0x9e | s16 | blob shadow scale x/y, 0x400 = 1.0 (script ext 0x78) |
| +0x2a8..+0x2b4 | | position cached for prim reuse; +0x3ac cached +0x48, +0x3b0/+0x3b2 cached scroll |
| +0x2b8..+0x2c4 | s16[4][2] | the sprite quad's last screen corners |
| +0x2c8 | s8[4] | sprite cache slots (>= 0x20 = cache B), +0x2cc count |
| +0x2cd | u8 | moved this frame (copied to +0x3e1) |
| +0x2d4 | u8 | analog input active |
| +0x2d8 | s32 | collision diameter (tests use /2) |
| +0x2e0 | FloorTri* | current floor triangle (0 = none) |
| +0x2f4 / +0x2f8 / +0x2fc | ptr | sprite record (mode at +2, part tables at +4/+8/+0xc), anim frames (4 or 8 bytes), frame defs (12 bytes) |
| +0x300 | ptr | frame def drawn last; +0x304 pixel data; +0x308 palettes; +0x310 u16 anim frame index |
| +0x314 | s32 | depth = vz >> 14 (sort key, sprite scale) |
| +0x318 / +0x378 / +0x3a8 | | prim pointers [24] / OT index per prim / prim count |
| +0x3b6 / +0x3b8 | s16 | sprite anchor on screen (BG px - view origin) |
| +0x3c6..+0x3c8 | u8 | lighting colour (ambient scene+0x6c, or colour cycle, + light zones) |
| +0x3d0, +0x3d4[2] | Actor* | attachment (emote) and two extra children |
| +0x3e0 | u8 | prescale arg of the last draw; +0x3ea s16; +0x3ec slide-last-frame |
| +0x3ed / +0x3ee | u8 | last 12-way direction while sliding (0x80 none) |
| +0x3ef / +0x3f0 / +0x3f2 / +0x3f4 | | blocked counter / threshold / idle frames / emote anim |

Flags +0x4c used here: 1 lit + light split, 2 on floor (snap/height), 4 collide with actors, 0x10 keep
layer, 0x20 allow clamped scale, 0x40, 0x80 animation paused, 0x100 no perspective scale, 0x200 flat
2D sprite, 0x10000 shadow, 0x20000 screen-space, 0x80000 / 0x100000 mirror x / y, 0x200000 leader
(obstacle sliding), 0x400000 running, 0x800000 ignore draw arg, 0x2000000 / 0x4000000 path
reversal / blocked emote, 0x8000000 keep colour, 0x10000000 hidden, 0x20000000 ignore scroll.

### 6.3 Drawing (`fieldactor_draw`, 10.6 KB, single epilogue)

1. Hidden -> no prims. Auto animation, moved flag, `0x8003f518` (resolve the sprite animation).
2. Lighting colour; flat 2D actors (flag 0x200) -> `fieldactor_draw_flat` and return.
3. Nothing changed (frame, position, +0x48, global tint, arg) -> copy last frame's FT3/FT4 prims to the
   new arena offset by the scroll delta, refresh colours, keep cache slots alive (0x80045284 /
   0x8004529c), draw the attachment, done.
4. Project (y snapped to the floor's stair step) -> +0x74/+0x76, layer zone, depth +0x314 (<= 0:
   behind the camera, no draw).
5. Shadow (flags & 0x30000 == 0x10000): bbox of the 4 projected corners of a ground square of
   half-size +0x2d8/2 map units (x15/13 while running), moved down the screen by
   `k * scene_scale * H / (depth * 400)` with k = 0..9 from the camera elevation over the actor, or of
   a 3x3 grid conformed to a sloped floor triangle; `fieldactor_build_shadow`, OT layer +0x22+1.
6. Sprite: frame def (12 bytes: u8 palette index (x0x20 into +0x308), u8 w, u8 h, pad, s16 anchor x,
   s16 anchor y, u32 pixel offset from +0x304) times
   `scene_scale% * H / depth` and +0x64/+0x68; FT4 corners rotated by camera roll + +0x8c; drawn
   +0x48 above the ground point (re-projected, layer zone re-tested); mirrored
   for 12-way directions 1-5 (auto anims < 0x24) or frame flags. Texture through the sprite cache:
   prescale arg set -> cache A (CPU-scaled 15-bit, tpage 0x105), else cache B (4/8-bit + CLUT, tpage
   0x15); abr from +0x7f.
7. Reflection (list-9 zone under the actor): second FT4 mirrored about the zone's world y.
8. `fieldactor_build_light_split` for lit actors, then multi-part sprites (frame bytes +4/+5/+6 index
   the part tables; field 0x80041ce4 draws each part).
9. Off-screen sprites get no prims (bbox vs the 320x240 view). Attachment and extra children last.

### 6.4 Auto animation (`fieldactor_select_auto_anim`)

12 sprite directions relative to the camera: `dir = ((facing - cam_yaw) + 0x8aa & 0xfff) / 0x156`
(cam_yaw = ActorSystem+0x38 = ratan2(eye.z - at.z, eye.x - at.x); field_flags+0x83 == 1 adds
(sx - w/2) * half_fov / (w/2)). +0x9a = dir walking, dir+12 standing (frame reset), dir+24 running.

### 6.5 Scripted movement (`fieldactor_move_step`, vtable[4])

Mode 1 (xyz) and 2 (xz on the floor): heading = ratan2 to the target; step = speed * ticks; mode 2
follows the floor triangle's plane and normalises the 3D step; arrives when the remaining distance
<= step; actor-actor collision (flag 4, field 0x800410cc) cancels. Mode 3: linear xz with
y offset +0x48 = base + height * sin; mode 4: arc jump in y. Screen-space actors lerp +0x74/+0x76.
Ping-pong patrols reverse after 120 blocked frames; flag 0x4000000 shows a "blocked" emote.

## 7. How the leader walks (input -> movement -> collision)

1. `field_main_loop` latches pads (0x80075748 held) and analog (0x800762fc flag, x 0x80076308,
   y 0x8007630c). `actor_sys_update` passes player = 1 for field_flags+0x10 unless +0x26 locks input.
2. `fieldactor_player_control` (vtable[2]):
   - digital: `dir = 0x80042d90()` = 0 Up, 1 Up-Right, 2 Right, 3 Down-Right, 4 Down, 5 Down-Left,
     6 Left, 7 Up-Left, -1 none (from bits 0xf000 of the held pad). Running while config+2 (cancel
     button, 0x20 by default) is held and script flag 10 is clear: speed = +0x94 * 4, flag 0x400000;
     else speed = +0x94.
   - analog: mag = max(|x|, |y|); < 30 walk, else run (flag 0x400000) unless flag 10; speed = mag *
     +0x94 / 20; angle = ratan2(y, x) + scene+0xcc[0] + 0x400, or, when 0x8007627e (camera looks down,
     set by field_update_screen_steering), ratan2(y, x) + direction from the actor to the world
     point 30 px above it on screen (0x800452b4 screen->world at the actor's y) + 0x800.
   - `actor_anim_advance`, then `fieldactor_walk_step(a, dir_or_angle, moving)`.
3. `fieldactor_walk_step`:
   - facing = scene+0xcc[dir] (digital), or the current floor triangle's table +0x44[dir] when its
     +0xe is set, or the analog angle. Previous facing saved in +0x98.
   - leader (flag 0x200000): predicted position = pos + (cos, sin)(facing) * step; if
     `walk_test_obstacles` hits: `fieldactor_wall_slide(a, step, tag=='2' ? 0x180 : 0x380)` searches
     both sides for the smallest heading change that clears; 0 -> no move this frame; else facing +=
     delta. Then the final step is tested with `walk_point_in_tri` on every obstacle (inside ->
     restore facing, no move).
   - floor triangle under the target position (list 14): if not flat, the step is re-normalised in 3D
     so slopes are slower.
   - pos += step; `walk_snap_to_floor` sets y and +0x2e0; actor-actor block (flag 4) reverts pos.
   - moved flag; 12-way direction memory against jitter while sliding; encounter distance
     field_flags+0x18 += speed * ticks.
4. Back in player control: project to +0x74/+0x76; if camera target, BG target = (sx, sy-16) and
   `bg_update_scroll`, +0x118 = y; `walk_update_layer`.
5. Later in the frame `fieldactor_draw` builds the sprite and `bg_draw` the background with the new
   scroll. Triggers (VM) use +0x74/+0x76 on normal maps (part4).

## 8. CamScript (the 0x17c "field-map helper")

A queue of camera pans for scripts. Task prio 1, so it moves the BG target before the actors run.

| Off | Meaning |
|---|---|
| +0x08 / +0x09 | ring read / write index (8 entries); script wait state 4 waits for +9 == +8 |
| +0x0c | Move[8], 0x24 each: `s32 x, y, z; s32 frames, speed, accel, min_speed, max_speed, mode` |
| +0x12c / +0x130 / +0x134 | current view centre x / y / z (24.8) |
| +0x13c / +0x140 / +0x144 | per-frame delta (target - current) / frames |
| +0x14c / +0x150 / +0x154 | target (24.8) |
| +0x15c / +0x160 / +0x164 / +0x168 | speed (8.8) / min speed / accel / accel sign |
| +0x16c / +0x170 / +0x174 / +0x178 | remaining (frames<<8) / half / max speed / deceleration mark |

Script ext ops (default speed 0x100, accel 0, min 0, max 0x10000 for the short forms): 0x85-0x89
`camscript_push_bg(x, y, frames, ...)` (mode 0, BG pixels), 0x8a-0x8e `camscript_push_point(x, y, z,
frames, ...)` (mode 1, map units), 0x8f-0x93 `camscript_push_actor(actor, frames, ...)`.
`camscript_update`: when a move starts, field_flags+0xe = -1 (the actor camera stops following);
current = view origin + (160, 120 or 112), target = BG point or the projected world point minus 16,
clamped so the view stays inside the BG; each frame position += delta * speed * ticks >> 8 with the
speed ramp; BG target = position, `bg_update_scroll`; pops the move when the distance runs out.

## 9. Other overlay systems

- **GlowFx** (ext 0x79 r, g, b, frames): additive ring of 24 POLY_G4 around the screen edge whose
  outer colour fades in over `frames`; stays until the map unloads (0x800752dc).
- **Particles** (script B 0x0b/0x4d/0x78/0x7b/0x7c/0x7d, A 0x12/0x13): 0x400 x 0x50-byte pool,
  `{u16 type, u16, OtLayer *layer, VECTOR pos (+8), VECTOR vel (+0x18), VECTOR target (+0x28), s32
  +0x38..+0x48 params}`; types 1 streak, 2 spark (gravity), 3 ring (G4), 4 spinner (G3, unused
  spawner), 5 lit orb, 6 beam, 7 triangle, 8 burst, 9 textured ring. Drawn with inline GTE (rtps,
  rtpt, avsz3, gpf12) into four private OT layers (`fx_layer_get`, ids from the script, 0x4184 bytes
  = two 0x830-entry OTs) that the scene renderer (0x800752d8) clears, fills and splices at
  OT[id + 0x11]. Additive (abr 1) lines/G3/G4.
- **field_special_command** (ext 0xa8): see the CSV row; notably 8/9 toggle scroll layers and 6 sets
  sort-by-screen-y.
- **Inventory stash** (A 0x10 / 0x11 / 0x34): copy the inventory to 0x80076228 and clear it, add it
  back, or clear it (keeping check bytes valid).

## 10. World map overlay 0xc1e compared

- Size 0x11600 (code to 0x8008f420, data 0x8008f420-0x8008f600) vs 0x10d70. The images differ from the
  first word. Function-level comparison with relocations masked (82 functions in 0xc1e by sweep):
  only the actor ctor/dtor template matches (0xc1e 0x80082dd4 / 0x80082e5c / 0x80082f1c = c1d
  0x8007e4b8 / 0x8007e540 / 0x8007e600, 0x80082d8c ~ 0x8007e478; they differ in the vptr and the init
  call). Everything else is different code: 0xc1e is a separate world-map program, not a variant.
- Entry points the field calls when field_flags+0x68 is set:

| Address | Role (0xc1e) | Field caller |
|---|---|---|
| 0x8007e000 | world camera controller ctor (0x88 bytes, vptr 0x8008f41c, [1] 0x8007e188); sets the camera globals 0x80075a80..0x80075ab4 | field_load_map (new 0x88 -> 0x800752c4) |
| 0x80082d8c / 0x80082dd4 / 0x80082e5c | WorldActor ctors (0x44c; vptr 0x8008f460: [1] draw 0x80082f44, [2] player control 0x80085868, [3] camera follow 0x80084da8, [4] move step 0x80084e04); dtor 0x80082f1c | actor_create_attachment, actor_create_or_place, actor_create, actor_sys_restore_all |
| 0x800829fc / 0x80082b6c | world trigger region add (ext 0x76/0x77) / test (vehicle landing, VM) | vm_exec_ext, field_update |
| 0x80088814 | WorldBg ctor (0x5a0; FieldTask prio 3; vptr 0x8008f4a8: [1] update 0x80088948, [2] dtor 0x80088894, [3] suspend 0x8008ae48, [4] resume 0x8008af54, [5] tint flag 0x8008b03c, [6] tint 0x8008b044) | field_load_map (new 0x5a0 -> 0x8007528c) |
| 0x80088a3c | parse map section 3 (terrain) | field_load_map_sections |
| 0x800888e0 | free the terrain stream buffers 0x80076178/0x8007617c (after 0x80089368) | field_unload_map, field_load_map |
| 0x80089368 | cancel the pending terrain chunk load (0x80076180, LoadQueue) | field_update (battle start), field_side_load |
| 0x8008d478 | terrain query (bg, x, z, &attr): height or -0x8000; attr bit 0 = landable | field_update (vehicles) |

- Shared vtable layout means the field drives both through the same slots: BG vtable +0x14 delete,
  +0x24 resume, +0x2c tint flag, +0x34 tint; actor vtable [1]..[4].
- Field calls that only make sense in 0xc1d are guarded by the flag (`fieldactor_construct_full` vs
  0x80082e5c, `walk_snap_to_floor` in actor_sys_restore_all, `bg_get_view_origin` in field_update) or
  by the data: `bg_build_tpages` runs for section 0, which world-map chunk containers (0x1014+,
  0x109f+, 0x10e0) never carry (113 checked: sections 3 and 1 only). `glow_destroy` 0x8007e0b8 on
  0x800752dc is unguarded but that pointer is only non-null on normal maps.

## 11. Globals touched by the overlay

| Address | Name (proposed) | Meaning |
|---|---|---|
| 0x8007528c | g_bg | BgRenderer (WorldBg in 0xc1e) |
| 0x800752b4 | g_camscript | CamScript |
| 0x800752dc | g_glow | GlowFx (ext 0x79) |
| 0x800752cc | | sub-overlay object 0x801f9800 (special command 0) |
| 0x80075330 / 0x80075334 / 0x80075338 | g_scene / g_scene_zones / g_scene_zone_base | section 0 block 0; cells; walk block |
| 0x80075354 | | scene loaded (bg_draw gate) |
| 0x8007535c | g_colour_anim_speed | colour animation speed % (100) |
| 0x80075728 | | BG animation save blob (handoff 0x10) |
| 0x80075730 / 0x80075732 | | draw y of buffer 0 / 1 (DR_AREA) |
| 0x80075a84..0x80075a98 | | camera eye / target << 12 (particles) |
| 0x80075aba / bc / be, 0x80075356 | | BG tint RGB, tint active |
| 0x80075b44 / 0x80075b64 / 0x80075b84 | | view rotation copy, colour matrix, light matrix (particles) |
| 0x80076194 / 0x80076195 / 0x80076198 | | BG animated pieces shown (ext 0xe1) / animated pieces ignore window when 0 (B 0x67) / scroll layers on (special 8/9); all 1 at map load |
| 0x800761a8 / 0x800761ac | | particle count (0x400 once created; also gates the OT-layer path) / pool |
| 0x800761b0 / 0x800761b2 | | particle view origin |
| 0x800761b4 / 0x800761c4 | | OT layer ids [4] / buffers [4] |
| 0x800761e8 | | second OT-layer pass (extra children drawn) |
| 0x800761ea | | alternate input mode: player control delegates to 0x801f9800 |
| 0x80076224 / 0x80076226 | | parallax override x / y (30000 = off) |
| 0x80076228 / 0x8007622c | | inventory stash / stashed flag |
| 0x8007627e | | camera looks down: analog input uses the unprojection path |
| 0x800762fc / 0x80076308 / 0x8007630c | | analog active / stick x / stick y |

field_flags offsets used: +0x0e camera target (-1 free), +0x10 controlled actor, +0x18 encounter
distance, +0x38/+0x3c/+0x40 script scratch (special 3/4), +0x69 and +0x74..+0x7d BG save, +0x80 sort
by y, +0x82, +0x83 direction FOV correction, +0x85..+0x88, +0x94 BG tint save, +0x89..+0x91 walk
group replay list. Script flag 10 (0x80075704[1] bit 2) disables running.

## 12. Porting notes

- **GPU prims**: SPRT (0x64/0x66, 16-bit textures), DR_TPAGE (0xe1000200 | tpage, dither on),
  POLY_FT4 (0x2c/0x2e; pieces, sprites, shadow 0x2f), POLY_FT3 (0x24, light split), POLY_G4 (0x3a,
  glow, particles), POLY_G3 (0x32), LINE_G2 (0x52), DR_MODE (SetDrawMode), DR_AREA (SetDrawArea).
  Prim colours are modulated (0x80 neutral); 0x0000 texels are transparent and show the clear colour.
- **OT**: entries are linked forward; within an entry the last prim added draws first. Insertion
  order across tasks matters (actors before BG). Keep a draw list keyed by (OT index, reverse
  insertion order).
- **GTE**: TransRot_32 (field 0x80070cf4: translate then rotate, 32-bit), RotTrans (in 0x800452b4),
  SquareRoot0, ratan2, sin4096/cos4096, gte_Square0, MulMatrix, PushMatrix/PopMatrix, ReadGeomScreen
  (0x80070984), inline rtps/rtpt/avsz3/stotz/gpf12/ldIR0/SetRotMatrix/SetTransMatrix/SetBackColor
  (particles). The overlay does its own perspective divide, so the actor/BG path needs only
  rotation + translation; integer rounding `(x*H + z/2)/z` matters for pixel parity.
- **Field functions the overlay calls**: actor base (actor_construct_base, actor_set_defaults,
  actor_init, actor_init_screen, actor_destroy, actor_update, actor_anim_advance, actor_turn_start,
  actor_walk_frame_delay, actor_save_anim, actor_cmd_set_anim, actor_mark_dirty,
  actor_create_attachment), 0x8003f518 (resolve sprite anim), 0x800410cc (actor-actor collision),
  0x80041ce4 (sprite part renderer), 0x80042d90 (pad direction), 0x80044754 / 0x80044c8c (sprite cache
  A / B upload), 0x80045284 / 0x8004529c (cache keep-alive), 0x800452b4 (screen -> world at y),
  0x8004872c (empty stub), 0x80057450 / field_task_destroy (FieldTask), task_register /
  task_unregister, screen_set_clear_colour, ot_layer_clear, 0x8005ff84 / 0x800617d8 / 0x8006ecf0 /
  0x8006ed5c (particle matrices and back colour), party_find_slot, party_move_character_to_slot,
  init_dias, inventory_add, inventory_check_byte, callways_song_active / callways_stop_song,
  object_delete; boot: rng_next, handoff_get / handoff_clear, mem_alloc / mem_alloc_high / mem_alloc_low /
  mem_free, __builtin_new / __builtin_delete, SetDrawMode, SetDrawArea, memset, strcpy; overlay
  0x801f9800 (special command 0, alternate input, second OT pass).
- **Order of work for map 7**: section 0 upload + scene parse (done in field), BgRenderer object with
  bg_update_scroll / bg_build_tpages / bg_get_view_origin, bg_draw static + scroll-layer pieces
  (animations and parallax can wait), camera projection as in section 4, FieldActor draw (sprite
  cache B path first, shadow, no light split), player control + walk step + obstacles + floor snap,
  camera follow. CamScript and GlowFx are needed once map 7's opening script runs camera pans.
- **Fixed-point**: positions 20.12; steps `cos(facing) * (step >> 8) >> 4`; CamScript 24.8 with 8.8
  speeds; colour multiplies `>> 7`. Timers use g_frame_ticks except the scroll-layer drift.
