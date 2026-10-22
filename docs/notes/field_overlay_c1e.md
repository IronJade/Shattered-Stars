# Field code overlay 0xc1e (#3102) at 0x8007e000: the world map

Function rows: `overlay_c1e.csv` (module `field_ovl_c1e`, 82 functions: 77 confirmed, 3 likely,
2 guess). ELF with the field image and this overlay: `analysis/elf/SO2_boot+field+ovl3102.elf`
(named copy `..._named.elf`); Ghidra project `analysis/ghidra_world/` (PSX:LE:32:default);
decompiles `analysis/decomp/ovl3102/` (67 functions + `index.csv`); full disassembly
`analysis/decomp/ovl3102_asm/ovl3102.s`. Code 0x8007e000-0x8008f41c, data (vtables, jump
tables, place-marker lists) 0x8008f41c-0x8008f6xx. Port: `engine/src/game/field/worldmap*.{h,cpp}`.

Names of field-program functions are the catalog names (`docs/catalog/functions.csv`).

## 1. When it is loaded

`field_load_map` sets `field_flags+0x68 = (map - 1) < 3`: maps 1-3 (Expel, Energy Nede, the
small map 3) use this overlay; every other map uses 0xc1d (#3101, `overlay_c1d.md`). The field
calls into 0x8007e000+ only behind that flag. Field-side world branches:

- `field_load_map`: release the previous WorldBg's blocks (0x800888e0) when leaving a world map;
  world spot (default (0x15026, -0x102, 0x6485), yaw 0xe98; vehicle spots game_state +0x26c..),
  delete WorldBg / WorldCam after the map's objects, the party sprite pack (#0x10e1) instead of
  the map's packs (`field_register_party_sprites` from the pack's own table), music mode 2,
  `new WorldBg` (0x80088814, 0x5a0) + `new WorldCam` (0x8007e000, 0x88), section 3 through
  0x80088a3c, BG angle / centre from game_state +0x1e / leader, VSync mode 2, the leader's
  smoothed-turning flag 0x1000000.
- `actor_create` / `actor_create_or_place` / `actor_sys_restore_all` / `actor_create_attachment`
  construct WorldActors (0x80082e5c / 0x80082dd4 / 0x80082d8c) instead of FieldActors.
- `field_update`: `worldbg_cancel_block_load` before the camp menu and battles;
  `worldmap_trigger_test` for the Private Action / landing prompts.
- `field_start_battle`: WorldBg vtable[3] (suspend: yaw into game_state +0x1e, lighting regions
  into handoff 0x10); `field_unload_map`: 0x800888e0.
- Script ext 0x76 / 0x77 (`worldmap_trigger_add`), table A 0x24 (`worldmap_add_entry` -> a
  lighting region).

## 2. Objects (g++ 2.x vtables: entry n = `{s16 delta, s16 0, u32 fn}` at vptr + 8n)

| Object | Size | Global | vptr (at) | Ctor | Task prio | Entries |
|---|---|---|---|---|---|---|
| WorldCam | 0x88 | 0x800752c4 | 0x8008f41c (+4) | `worldcam_construct` 0x8007e000 | none (called by the BG) | [1] `worldcam_update` 0x8007e188 |
| WorldActor | 0x44c | field_actors[] | 0x8008f460 (**+0x3f8**) | 0x80082d8c / 0x80082dd4 / 0x80082e5c | (ActorSystem) | [1] `worldactor_draw` 0x80082f44, [2] `worldactor_player_control` 0x80085868, [3] `worldactor_camera_follow` 0x80084da8, [4] `worldactor_move_step` 0x80084e04 |
| WorldBg | 0x5a0 | 0x8007528c | 0x8008f4a8 (+4) | `worldbg_construct` 0x80088814 | **3** (FieldTask 0x80057450) | [1] `worldbg_update` 0x80088948, [2] `worldbg_destroy` 0x80088894, [3] `worldbg_suspend` 0x8008ae48, [4] `worldbg_resume` 0x8008af54, [5] `worldbg_enable_tint` 0x8008b03c (empty), [6] `worldbg_set_light` 0x8008b044 |

WorldBg sits where the field BgRenderer does (0x8007528c), so the field's generic BG calls
(vtable[3..6]) reach it unchanged. `worldactor_destroy` 0x80082f1c is the class dtor.

### WorldCam (0x88)

| Off | Meaning |
|---|---|
| +0x08 | move mode (0 idle; 10 = orbit move); +0x84 its priority (byte table 0x8008f42b + mode) |
| +0x10 / +0x14 / +0x18 / +0x1c / +0x20 / +0x24 | speed ramp: speed (8.8), accel, remaining (frames << 8), half, max, min |
| +0x30 / +0x32 / +0x34 | brake mark, unused short, ticks run |
| +0x38 / +0x3c | zoom target / ramp speed (0x80075ab4 follows) |
| +0x40 / +0x50 | eye destination / per-frame step |
| +0x60 / +0x70 | target destination / per-frame step |
| +0x80 / +0x85 | mode argument / braking from the start |

It moves the camera block's interpolated eye / target (0x80075a9c / 0x80075aa8) and copies them
into the camera block proper (0x80075a84 / 0x80075a90) every update. All camera points are kept
relative to the focus block's row / column; `worldcam_shift` moves them by +-0x3000000 when the
leader crosses into the next block (the torus seam included).

### WorldBg (0x5a0)

| Off | Meaning |
|---|---|
| +0x08 | recentre request (section 3 tag 5 -> WorldCam mode 10 next update) |
| +0x0c / +0x10 | focus position >> 12 (leader), -1 = none yet |
| +0x18 / +0x1c | local point of the last height query |
| +0x20..+0x2c | focus (20.12, x / z modulo one block) |
| +0x30 / +0x34 | camera yaw / pitch << 12 (pitch default 0xee000 = 21 degrees) |
| +0x38 / +0x3c / +0x40 / +0x44 / +0x48 | turn input, turn brake, pitch step, turn speed, pitch speed |
| +0x4c | 13 x 13 grid of sub-cell corners (s16 x, y, z, pad) for the visibility pass |
| +0x59c | actor light colour (lighting regions) |

### WorldActor (0x44c = Actor 0x3f8 + vptr + 0x50)

| Off | Meaning |
|---|---|
| +0x3fc | projected depth (SZ3 >> 3; < 9 not drawn) |
| +0x400 / +0x404 | sprite scale x / y before the zoom (0xe80) |
| +0x408 / +0x40c | sprite scale of the last draw |
| +0x410 | s8 OT bias |
| +0x414..+0x420 | floor normal (shadow plane) |
| +0x424 | wading depth 0..0x20 (sprite bottom clipped) |
| +0x425 | scripted moves turn to the heading (1) |
| +0x428 / +0x42c / +0x430 | vehicle velocity |
| +0x434 / +0x438 | vehicle turn rate / spare |
| +0x43c | terrain height under the actor (-0x8000 unknown) |

## 3. The world: a torus drawn as a planet

- 9 x 7 blocks of 0x3000 units (0x1b000 x 0x15000); x / z wrap. Block index = bx + 9 bz; block
  entry = index + 0xfd4 (map 1), 0x1060 (map 2), 0x10de / 0x10df (map 3, two blocks only).
  Map 1 has one alternate block: index 0x30 is entry 0x1013 instead of 0x1004 while game_state
  word 0 is in 0xf..0x3c.
- The nine blocks around the focus live in one buffer (0x80076178, 9 slots of 0xd000); slot
  entries 0x80076164, wanted blocks (nearest first) 0x80076150. `worldbg_stream_blocks` reads at
  most one block at a time (load queue, 0x80076180 pending entry / 0x80076182 slot).
- The chunk container of the leader's square (map container entries 0x1014+ (map 1), 0x109f+
  (map 2), 0x10e0 (map 3)) holds section 3 (below) and section 1 (the script) like any map.
- Drawing: each loaded block at offsets (-0x1800, 0x1800, 0x4800) in x / z from the focus
  block. `world_set_object_matrix` builds two matrices: 0x80075ae8 = Ry(yaw2) +
  (pos - eye) with **absolute y** (the bend pass) and 0x80075b08 = Rx(pitch2) Rz(roll) +
  (0, -eye.y, 0) (the projection pass). A vertex goes through RTPS with the first, is bent onto
  a sphere of radius 0x4000 with the curvature table (`(sin x, -(cos x cos z) >> 12, sin z)`
  pairs at table + 0x800, indexed by x = IR1 >> 2 and z = (IR3 - curve_offset - 0x4b0) >> 2,
  scaled with GPF12 by IR0 = 0x4000 - y), then through RTPS / RTPT with the second.
  `curve_offset` (0x80076188) compensates the camera pitch.
- Visibility (`worldbg_visible_cells`): the 13 x 13 corner grid is bent and projected; a 0xc00
  sub-cell is drawn unless all of it is off one screen side and far; the 3 x 3 around the focus
  always; at most 18 kept. Result: one u16 mask per block, bit 15 - (col & 3) - 4 (row & 3).

### Terrain block (archive entries above, SLZ-free, up to 0xd000 bytes)

| Off | Meaning |
|---|---|
| +0x00 | s32 vertex count N |
| +0x04 | offset of the coarse height grid (33 x 33 bytes, 0x180 per cell, height = byte * -10) |
| +0x08 | 16 x u16 vertex count of each sub-cell |
| +0x28 | 16 x u32 offset (from +0x68) of each sub-cell's vertices |
| +0x68 | vertices: N x {s16 x, y, z, x} (the word at +4 is x << 16 \| z for one load) |
| H = +0x68 + 8N | 16 sub-cell headers of 0x20: u16 attribute-area size (& ~3), 11 x u16 polygon counts (types A..K) |
| H + 0x200 | 16 x u32 polygon area offset (from H + 0x280) |
| H + 0x240 | 16 x {s16 half-x, half-z} sub-cell extents (height queries) |
| H + 0x280 + off | per sub-cell: attribute bytes (one per walkable polygon: bit 0 walkable, bit 1 / 2 water, bits 4-7 terrain type), then the polygon records |

Polygon records (vertex fields are vertex index * 4, relative to the sub-cell's vertices):

| Type | Size | Primitive | Layout |
|---|---|---|---|
| A | 0x18 | GT3 | +0 uv0\|clut, +4 uv1 \| c << 16, +8/+0xc/+0x10 colours whose top bytes are u2, v2, tpage, +0x14 a \| b << 16 |
| B | 0x14 | GT3 | +0 uv0\|clut, +4 uv1\|tpage, +8 uv2 \| b << 16, +0xc grey bytes, +0x10 a \| c << 16 |
| C | 0x10 | GT3 | +0 uv0\|clut, +4 uv1\|tpage \| b << 24, +8 uv2 \| a << 16 \| c << 24, +0xc grey bytes |
| D | 0x10 | FT3 | +0 uv0\|clut, +4 uv1\|tpage \| grey << 24, +8 uv2 \| b << 16, +0xc a \| c << 16 |
| E | 0x20 | GT4 | +0 uv0\|clut, +4 uv1\|tpage, +8..+0x14 colours (top bytes u2 v2 u3 v3), +0x18 a \| b << 16, +0x1c c \| d << 16 |
| F | 0x18 | (none) | collision only, not drawn, not walkable |
| G | 0x14 | GT4 | +0 uv0\|clut, +4 uv1\|tpage, +8 uv2 \| uv3 << 16, +0xc grey bytes, +0x10 vertex bytes |
| H | 0x14 | (none) | collision only |
| I | 0x10 | FT4 | +0 uv0\|clut, +4 uv1\|tpage \| grey << 24, +8 uv2 \| uv3 << 16, +0xc vertex bytes |
| J | 0x18 | GT3 semi-transparent | layout A, no fog |
| K | 0x20 | GT4 semi-transparent | layout E, no fog |

Walkable (height-query) types: A B C D J (triangles), G I K (quads, a b d c order). Lighting:
NCCS with the light matrix L12 = 0x1000 (lit by the normal's y), colour matrix = terrain light
<< 5. Culling: NCLIP (triangles >= 0, quads < 14), GTE FLAG errors, screen bounds, OTZ / 2 <
0x800. OT index 0x820 - OTZ / 2 (the field OT has 0x830 entries; sky at 0x10 / 0x11, actors at
0x83a + bias - depth).

Depth fog (types A-E, G, I): OTZ / 2 below 0x149 none; below 0x446 a grey (0x680)
subtractive G3 / G4 under the polygon plus a DR_MODE (dither, additive) and an additive G3 / G4
in the fog colour from DPCS of each vertex's depth factor; beyond 0x446 the polygon switches to
the darker CLUT (+12 rows, `worldbg_darken_clut`), its tpage blend one step down, and only the
fog G3 / G4 is added (quads E / G have no far path).

### Section 3 of a world chunk container (`worldbg_parse_section3`)

Tagged stream of u32 tags until 0 (jump table 0x8008f488):

| Tag | Payload | Effect |
|---|---|---|
| 1 | u16 x, y; u32 off -> SLZ image | {u16 flags (depth & 3), u16 CLUT count, CLUTs, u16 w, h, pixels}: CLUT to row 0x1e0 + n and a darkened copy 12 rows down; pixels to (x, y) |
| 2 | same | CLUT to row 0x1fb (the sky / clouds) |
| 7 | same | CLUT to row 0x1fa |
| 3 | s16 x, y; {u32 duration; s16 x0, y0, x1, y1}*; u32 0 | a texture-animation group (up to 24 groups, 0x83 frames): MoveImage of each frame's rectangle onto (x, y) |
| 4 | u32 off -> SLZ | the curvature table (0x8007617c) |
| 5 | u32 off | the first nine blocks {s32 index (-1 ends), u32 off} and the defaults (`worldbg_init_blocks`) |
| 6 | u16 x, y; u32 off | a CLUT at off + 0x14 and its darkened copy 5 rows down |

When returning from another program (field_flags +0x24) the field restores the actors after
section 3 (`actor_sys_restore_all`).

## 4. Camera

- Pitch: degrees in 0x8007627c (21 on foot, 18 in vehicles); the follow camera raises it over high
  ground ahead (three probe lines of `world_camera_floor` along the view, 16 steps, + 0x22, max
  0x2aa) with a 0x800-per-tick ramp.
- Yaw: `world_camera_manual` integrates a turn speed (+0x44) from the turn input (+0x38: L1 / R1
  0x2000 per tick, or the walk's input-direction turn) with braking (+0x3c or 0x600), max
  0x19000 (vehicles 0x1000 / 0x14000 / 0x380); the orbit is distance 0xaf0 around the focus.
  The input is multiplied by frame_ticks **and** the speed accumulates once per frame, so the
  turn acceleration depends on the frame rate (the console runs this map at about 20 fps:
  PCSX-Redux shows frame_ticks 3 throughout the walk reference).
- Walking turns the camera with the pad direction (`worldactor_walk` end, only when the leader
  moved): for the 8 directions d = 0, 0x200 .. 0xe00 relative to the camera the turn input is
  0, -0x400, -0x800, +0x555, 0, -0x555, +0x800, +0x400 (piecewise linear in d), times
  speed / (walk * 8); straight up / down (d within 0x180 of 0 or 0x800) add up to 0x400 to the
  brake instead.

## 5. Walking and terrain queries

- `world_terrain_query(bg, x, z, hit)`: wraps the torus, picks the block (unloaded = `off_map`),
  tries the point's sub-cell and its neighbours across x, z and both, returns the height of the
  first polygon containing the point (GTE OP edge tests). Hit record (0x1c): u8 attr, u8 off_map,
  u16 corners, 4 vertex pointers, block origin x / z. Map 2: outside the disc r 0x5cf8 around
  (67000, 40000) (the whirlwind) and, in vehicles, inside r 8000 around (0x13023, 0xd4b5) the
  attribute becomes (attr & 0xfe) | 0xf0 (terrain type 0xf, not walkable).
- `worldactor_walk`: probes 90 units ahead (and +-ratan2(0x3c, 0x5a) on the first try); blocked
  probes are collected and `worldactor_slide_turn` searches the smallest turn that clears them
  (up to 4 tries); unblocked, `worldactor_avoid_polygons` keeps 0x3c clearance; then both
  shoulders (+-0x200, 0x3c out) and the feet must stand on walkable polygons or the move is
  undone. Encounter distance: field_flags +0x18 += speed * ticks.
- Encounters: group from the field tables 0x80074ef0 (map 1) / 0x80074f70 (map 2) (7 rows x 9
  blocks, s8; negative = 3 x 3 sub-table through 0x80074f5c / 0x80074fd0), +1 on terrain 5, +2
  on 10-12 (map 2); stage / kind from the terrain type (attr >> 4) and the lighting region ->
  field_flags +0x12 (stage) / +0x14 (group) / +0x31 (kind); map 1 also +0x30.

## 6. Lighting regions (script table A 0x24; saved through handoff 0x10)

Up to 16: {s32 x, z (0x8007470c); s16 params[12] (0x80076284)}: params[0] radius, [1..3] light
rings, [5..7] / [9..11] fog / sky rings blended by the camera facing (cos-like weight of
yaw + 0x400). Outside ring 1 the colours fade towards dusk (light (0xa0, 0x32, 0)), between rings
1 and 2 dusk, then towards night (light / sky (0x1e, 0, 0x80), fog black). 0x800762fd = 1 within
90% of the radius, 2 in the core. Everything is then multiplied by the BG tint (0x80075aba /
128). Suspend blob: {u32 size, u32 count, {s32 x, z; s16 params[12]}[count]}.

## 7. Port status (`engine/src/game/field/worldmap*.cpp`)

Ported: WorldCam, WorldBg (section 3, streaming through the load queue, lighting regions,
suspend / resume through handoff 0x10, terrain queries), the terrain renderer with depth fog,
the visibility pass, sky and clouds, texture animations, map 2's whirlwind walls, WorldActor
(sprite draw with the sprite caches, wading, blob shadow, flat actors, auto animation, scripted
moves, walking with the slide / avoid searches, encounter settings), the follow / manual camera,
and the field-side world branches (section 1). Map 2 from the user's save renders like
PCSX-Redux; with the console's pacing (`SS_WORLD_VSYNC=3`, see 4.) a 45-frame walk with camera
turns matches the original's leader position, facing, camera yaw and turn speed frame for frame
(the original then takes one 2-vblank frame and the runs drift apart). Remaining pixel
differences are GPU-level (PCSX-Redux applies dithering after semi-transparent blending, so its
fog / wall overlays come out about 0.5-1 level darker).

Not ported:

- Vehicles: `worldactor_vehicle_control` 0x80085944, the vehicle model draw through field
  0x8006053c (tables 0x80075190 / 0x800751a4), vehicle packs 0x10e2-0x10e4, vehicle actors
  0xd / 0xe, landing tests.
- Place markers / corner minimap (`worldbg_draw_markers` 0x8008b064, lists 0x8008f4ec map 1 /
  0x8008f580 map 2; Start cycles game_state +0x20 = 0..2).
- `worldcam_target_from_angle`, `worldcam_set_zoom`, the leaf accessors 0x800887d4-0x8008880c
  and 0x80082900 have no caller in the field or the overlay (embedded map code API).
