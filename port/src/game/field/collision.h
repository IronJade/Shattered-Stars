#pragma once

#include "core/types.h"

// Field-map walking and collision (code overlay #3101). The scene's cell grid
// (formats.md "Cell grid"; MapScene sub-block 2) holds, in cell 0, the world-space
// floor zones (list 14: triangles with a floor plane, control angles and an OT layer)
// and walls (list 15: polygons the actor's radius may not touch); the screen-space
// cells hold depth layers (list 2), reflections (9), tint regions (8) and shading
// regions (4 / 6), tested against the actor's projected position.
//
// Item layout (cell 0 items have no 4-byte prefix):
//   [u8 type; u8 r, g, b]  s16 y_min, y_max, x_min, z_min, x_max, z_max;
//   u8 used; u8 tag; u8 count; u8; then vertices: world {s16 x, y, z, pad},
//   screen {s16 x, y}; then the list's own fields.
namespace ss::game::field {

struct Actor;

// Cell grid access (g_scene_cells 0x80075334 / g_scene_grid 0x80075338).
int scene_cell_count(int cell, int list);          // items in a cell's list
const u8* scene_cell_list(int cell, int list);     // the first item

int zone_cell_at_xy(const Actor* a, int sx, int sy);    // 0x80085cf0: cell of a screen point (relative to the grid origin), or -1
int zone_cell_at(const Actor* a, const s16* xy);  // 0x80085cc4
// 0x80083944: a screen-space item contains (sx, sy) at height y (type 0 triangle,
// 1 quad, 2 box, 3 ellipse).
bool zone_test_point(const Actor* a, const u8* item, const s16* xy, s32 y);
// 0x80083c3c: a world-space item's triangle contains the position (x, y, z; 20.12).
bool walk_point_in_tri(const Actor* a, const u8* item, const s32* pos);
void walk_snap_to_floor(Actor* a);                 // 0x80083628: floor zone, height on its plane, zone layer
void walk_update_layer(Actor* a);                 // 0x80083840: OT layer from cell list 2
// 0x80082cfc: one step of player walking: 'dir' is a pad direction (0-7, mapped through
// scene +0xcc / the zone's angles) or, with the analog stick, an angle; 'move' = a
// direction is held. Walls deflect the step (fieldactor_wall_slide), solid actors stop it.
void fieldactor_walk_step(Actor* a, int dir, int move);
// 0x800863fc: the position touches a wall; *hit = its index in list 15.
bool walk_test_obstacles(const Actor* a, const s32* pos, u32* hit);
// 0x80085f30: the smallest turn (up to +-range, binary search) that clears the walls
// for a step of 'step' (20.12); 0 when both ways are blocked.
s32 fieldactor_wall_slide(const Actor* a, s32 step, s32 range);
// 0x8008df74 (script B 0x06): switches the floor triangles and walls tagged 'tag' (byte
// +0xd) on or off, and records it in field_flags +0x89.. (8 entries, bit 7 = off, count
// +0x91) for field_load_map to replay.
void walk_set_group_enabled(int tag, int enabled);

}  // namespace ss::game::field
