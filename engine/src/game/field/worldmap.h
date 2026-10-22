#pragma once

#include <vector>

#include "core/types.h"
#include "game/field/actor_base.h"
#include "game/field/tasks.h"
#include "runtime/libgte.h"

// The world map: code overlay 0xc1e (#3102, loaded at 0x8007e000 instead of the field
// overlay 0xc1d when field_flags +0x68 = (map - 1) < 3). Catalogue: module
// field_ovl_c1e (analysis/field_catalog/overlay_c1e.csv); structure and data formats:
// analysis/field_catalog/overlay_c1e.md.
//
// The world is a torus of 9 x 7 terrain blocks of 0x3000 units (0x1b000 x 0x15000;
// positions wrap), drawn as a curved planet: block vertices go through the view
// rotation, are bent onto a sphere of radius 0x4000 with the curvature table of the
// chunk container (section 3 tag 4), then projected. Nine blocks around the camera are
// kept in memory (0x80076178, 9 x 0xd000) and streamed from archive entries 0xfd4+
// (map 1), 0x1060+ (map 2) or 0x10de / 0x10df (map 3). The chunk container of the
// leader's 0x3000 square (0x1014+ / 0x109f+ / 0x10e0) brings the textures, texture
// animations, the curvature table, the first nine blocks and the script.
//
// Objects (all overlay classes, vtables at 0x8008f41c / 0x8008f460 / 0x8008f4a8):
//   WorldCam   0x88 bytes (0x800752c4): eye / target interpolation (0x8007e000-0x8007ec38)
//   WorldBg    0x5a0 bytes (0x8007528c, FieldTask priority 3): streaming, lighting, sky
//              and terrain draw (0x80088814-0x8008f420)
//   WorldActor 0x44c bytes (field_actors[]): sprite / vehicle draw, walking on the
//              terrain, camera follow (0x80082d58-0x800888e0)
namespace ss::game::field {

struct Field;

// ---- The camera block as the world map uses it (field globals 0x80075a80-0x80075c44) ----
struct WorldView {
  u8 look_at = 0;                  // 0x80075a80
  s32 eye[3]{};                    // 0x80075a84 (20.12, relative to the current block row / column)
  s32 target[3]{};                 // 0x80075a90
  s32 eye_dest[3]{};               // 0x80075a9c: WorldCam's interpolated eye
  s32 target_dest[3]{};            // 0x80075aa8
  s32 zoom = 0x1000;               // 0x80075ab4 (0xccc on the world map)
  s16 roll = 0;                    // 0x80075ab8
  s16 light[3]{0x80, 0x80, 0x80};  // 0x80075ac0..ac4: terrain light colour (lighting regions x BG tint)
  psyq::MATRIX view{};             // 0x80075ac8: camera rotation (camera_compute_view)
  psyq::MATRIX world{};            // 0x80075ae8: view rotation + translation of the object drawn
  psyq::MATRIX proj{};             // 0x80075b08: rotation for the curved -> screen projection
  s16 offset[3]{};                 // 0x80075b28
  u8 ambient[3]{};                 // 0x80075b30
  u8 ambient_pad = 0;              // 0x80075b33: tpage of the model renderer's semi-transparent G3s (never written)
  s16 pitch = 0, yaw = 0;          // 0x80075b34 / 0x80075b36 (camera_compute_view angles)
  s16 b38 = 0;                     // 0x80075b38
  s16 pitch2 = 0, yaw2 = 0;        // 0x80075b3c / 0x80075b3e
  s16 roll2 = 0;                   // 0x80075b40
  psyq::MATRIX rot_b44{};          // 0x80075b44
  psyq::MATRIX colour_b64{};       // 0x80075b64: the particles' colour matrix (cleared by every camera update)
  psyq::MATRIX light_b84{};        // 0x80075b84: the particles' light matrix
  psyq::MATRIX colour{};           // 0x80075ba4: GTE colour matrix (light colour << 5 in column 0)
  psyq::MATRIX light_m{};          // 0x80075bc4: GTE light matrix (L12 = 0x1000: lit by normal.y)
  u8 far_colour[3]{};              // 0x80075c15: fog colour of this frame
  u8 base_far[3]{};                // 0x80075c18: fog colour (0x30, 0x60, 0x60)
  u8 sky_r[2]{}, sky_g[2]{}, sky_b[2]{};            // 0x80075c1c / c20 / c24: sky colours (band 0 top, 1 horizon)
  u8 sky_r_now[2]{}, sky_g_now[2]{}, sky_b_now[2]{};  // 0x80075c28 / c2c / c30: after lighting regions and tint
  s32 geom_ofs_x = 0, geom_ofs_y = 0;  // 0x80075c34 / 0x80075c38: GTE offset adjust
  s32 light_angle[3]{};            // 0x80075c3c..c44: object rotation passed to world_set_object_matrix
};

// ---- WorldCam (0x88, vtable 0x8008f41c, global 0x800752c4) ----
struct WorldCam {
  s16 mode = 0;                    // +0x08: 0 idle, else the move being run
  s16 field_0e = 0;                // +0x0e
  s32 speed = 0;                   // +0x10 (8.8)
  s32 accel = 0;                   // +0x14
  s32 remaining = 0;               // +0x18 (frames << 8)
  s32 half = 0;                    // +0x1c
  s32 max_speed = 0;               // +0x20
  s32 min_speed = 0;               // +0x24
  s16 brake_mark = -1;             // +0x30
  s16 field_32 = 0;                // +0x32
  s32 frames_run = 0;              // +0x34
  s32 zoom_target = 0x1000;        // +0x38
  s32 zoom_speed = 0;              // +0x3c
  s32 eye[4]{};                    // +0x40: eye destination
  s32 eye_step[4]{};               // +0x50
  s32 target[4]{};                 // +0x60: target destination
  s32 target_step[4]{};            // +0x70
  s32 mode_arg = 0;                // +0x80
  u8 mode_prio = 0;                // +0x84
  u8 braking = 0;                  // +0x85
};

// ---- WorldBg (0x5a0, vtable 0x8008f4a8, FieldTask priority 3, global 0x8007528c) ----
struct WorldBg final : Task {
  void update() override;          // vtable[1] 0x80088948

  s32 recentre = 0;                // +0x08: set by section 3 tag 5; asks WorldCam for mode 10
  s32 centre_x = -1;               // +0x0c: the camera's focus (leader x >> 12), world units
  s32 centre_z = 0;                // +0x10
  s32 query_x = 0, query_z = 0;    // +0x18 / +0x1c: local point of the last height query
  s32 focus[4]{};                  // +0x20: focus position (20.12, x / z modulo one block)
  s32 yaw = 0;                     // +0x30: camera yaw << 12 (game_state +0x1e)
  s32 pitch = 0xee000;             // +0x34: camera pitch << 12
  s32 turn = 0;                    // +0x38: turn input (manual camera)
  s32 turn_brake = 0;              // +0x3c
  s32 pitch_step = 0;              // +0x40
  s32 turn_speed = 0;              // +0x44
  s32 pitch_speed = 0;             // +0x48
  s16 block_xz[0xa9][4]{};         // +0x4c: 13 x 13 grid of sub-cell corners (x, y, z) for visibility
  u8 light_rgb[3]{0x80, 0x80, 0x80};  // +0x59c: actor light colour (lighting regions)
};

// ---- WorldActor (0x44c = Actor + 0x50; vtable 0x8008f460) ----
struct WorldActor : Actor {
  s32 depth8 = 0;                  // +0x3fc: projected depth (sz >> 3; < 9 = not drawn)
  s32 sprite_scale_xz[2]{0xe80, 0xe80};  // +0x400 / +0x404: sprite scale x / y (before the zoom)
  s32 sprite_scale_draw[2]{};      // +0x408 / +0x40c: sprite scale of the last draw (x zoom)
  s8 ot_bias = 0;                  // +0x410: OT index bias
  s32 normal[4]{};                 // +0x414: floor normal (shadow)
  u8 wade = 0;                     // +0x424: depth into water (0..0x20, sprite clipped)
  u8 face_heading = 1;             // +0x425: scripted moves turn to the heading
  // Vehicle state (worldactor_vehicle_control 0x80085944).
  s32 throttle = 0;                // +0x428: 0..0x1000 (speed = walk * throttle >> 12 << 5)
  s32 climb = 0;                   // +0x42c: vertical speed (20.12; -0x28000..0x28000, + = down)
  s32 vel_430 = 0;                 // +0x430: cleared with the others, not read
  s32 turn = 0;                    // +0x434: camera turn rate
  s32 bank = 0;                    // +0x438: heading offset from the camera's view direction
  s32 ground = -0x8000;            // +0x43c: terrain height under the actor (-0x8000 unknown)
};

// Result of a terrain query (0x8008d478; 0x1c bytes on the console).
struct TerrainHit {
  u8 attr = 0;                     // +0x00: bit 0 walkable, 2 / 4 water, bits 4-7 terrain type
  u8 off_map = 0;                  // +0x01: the point's block is not loaded
  s16 corners = 0;                 // +0x02: 3 triangle / 4 quad
  const u8* v[4]{};                // +0x04: the polygon's vertices (8-byte records, host pointers)
  s32 base_x = 0, base_z = 0;      // +0x14 / +0x18: block origin (world units)
};

// ---- Globals the overlay owns (0x8007462c-0x80076310) ----
struct WorldGlobals {
  // Texture animations (section 3 tag 3): 24 groups of frames that MoveImage a VRAM
  // rectangle onto the group's destination.
  s32 anim_dest[24][2]{};          // 0x8007462c: destination x, y
  s16 anim_frame[0x83][4]{};       // 0x80075c4c: source x, y, w, h
  u8 anim_duration[0x84]{};        // 0x8007606c
  s8 anim_end[24]{};               // 0x800760f0
  u8 anim_first[24]{};             // 0x80076108 (0xff = unused group)
  u8 anim_cur[24]{};               // 0x80076120
  s8 anim_timer[24]{};             // 0x80076138
  // Terrain blocks.
  s16 wanted[9]{};                 // 0x80076150: blocks around the camera, nearest first
  s16 loaded[9]{};                 // 0x80076164: block entry in each buffer slot (-1 free)
  std::vector<u8> blocks;          // 0x80076178: 9 x 0xd000
  std::vector<u8> curve;           // 0x8007617c: curvature table (tag 4)
  s16 pending = -1;                // 0x80076180: block entry being read
  s16 pending_slot = 0;            // 0x80076182
  u8 pending_done = 0;             // 0x80076184
  u8 streaming = 1;                // 0x80076176
  s32 curve_offset = 0;            // 0x80076188
  s32 pitch_copy = 0;              // 0x8007618c
  s16 cam_pitch_deg = 0x15;        // 0x8007627c: camera pitch, degrees (0x12 in vehicles)
  s16 cam_frames = 0;              // 0x80076280: frames of the follow camera's orbit move (0 = snap)
  // Lighting regions (script table A 0x24 adds them; saved through handoff 0x10).
  s16 region_count = 0;            // 0x80076282
  s32 region_pos[16][2]{};         // 0x8007470c: x, z
  s16 region[16][12]{};            // 0x80076284: radii and colours
  u8 in_region = 0;                // 0x800762fd: 0 none, 1 inside, 2 core
  u8 in_region_prev = 0xff;        // 0x800762fe
  s32 last_terrain = -1;           // 0x80075348: terrain type the encounter settings came from
  TerrainHit leader_hit;           // 0x80076258: the leader's last terrain query
  s32 tri_normal[4]{};             // 0x800756e4: normal of the last polygon a query hit
  WorldView view;
};
WorldGlobals& world();
WorldBg* world_bg();               // 0x8007528c on the world map (null elsewhere)
WorldCam* world_cam();             // 0x800752c4

// Byte at 'address' of overlay #3102 as loaded at 0x8007e000 (its constant tables).
const u8* world_ovl_data(u32 address);

// ---- Map loading (field_load_map's world branches) ----
// 0x80088814 + 0x8007e000: creates WorldBg and WorldCam (replacing the previous ones).
void world_create_objects();
// field_register_party_sprites (field 0x8004b5cc): the world party pack's offset table:
// the leader (-2), the other protagonist (-3), then each party member (-4, -5, ...).
void world_register_party_sprites(const u8* table);
void world_destroy_objects();               // BG vtable delete + __builtin_delete(0x800752c4)
void world_bg_release(WorldBg* bg);         // 0x800888e0: cancel the read, free the blocks
void world_cancel_block_load();             // 0x80089368
void world_parse_section3(WorldBg* bg, const u8* data);  // 0x80088a3c
void world_bg_suspend(WorldBg* bg);         // vtable[3] 0x8008ae48 (handoff 0x10 blob)
void world_bg_resume(WorldBg* bg);          // vtable[4] 0x8008af54
u8* world_take_region_save();               // world_bg_suspend's blob, for handoff 0x10
void world_bg_set_light(int r, int g, int b);  // vtable[6] 0x8008b044
void world_bg_set_angle(WorldBg* bg, s32 angle);       // 0x8008f404: +0x30 = angle << 12
void world_bg_set_centre(WorldBg* bg, s32 x, s32 z);   // 0x8008f410
// Script table A 0x24: a lighting region (x, z, 12 parameters).
void world_add_light_region(const s32 args[14]);

// ---- Vehicles (worldmap_vehicle.cpp) ----
void world_vehicle_models_free();                                   // field 0x800519d4
void world_vehicle_models_reset();                                  // field_load_map: free + VRAM cursors
// field_load_map's resets of the world globals (every map): lighting regions
// (0x80076282), the leader's terrain hit (0x80076258) and the vehicle models.
void world_load_map_reset();
void world_vehicle_load_pack(Field* f, int entry, int vehicle);     // field 0x8004a920 (packs 0x10e2-0x10e4)

// ---- Terrain ----
// 0x8008d478: height of the terrain under (x, z) (20.12), or -0x8000 when no loaded
// polygon covers the point; fills 'hit'.
s32 world_terrain_query(WorldBg* bg, s32 x, s32 z, TerrainHit* hit);
s32 world_terrain_camera_floor(WorldBg* bg, s32 x, s32 z);  // 0x8008e750: coarse height grid
int world_block_slot(WorldBg* bg, int block);              // 0x8008c940: buffer slot or -1
void world_block_origin(const WorldBg* bg, s16 out[2]);     // 0x8008c8e0

// ---- Camera ----
void world_camera_reset();                  // camera_reset 0x8005f8d8 (world part)
void world_camera_update_matrices();        // camera_update_matrices 0x800603a4
s32 world_camera_yaw();                     // 0x8007ebec: ratan2 of the view direction
// 0x8008bed8: the camera follows 'a' (vtable[3] of the camera actor, walking).
void world_camera_follow(WorldBg* bg, Actor* a, s32 turn, s32 turn_brake, int scripted);
void world_set_object_matrix(const s32 pos[3], const s32 angles[3], const s32 scale[3]);  // 0x8007ec38

// ---- Drawing ----
void world_bg_draw(WorldBg* bg);            // 0x800893c0
void world_draw_block(const u8* block, u16 mask);  // 0x8007f8a8

// ---- WorldActor ----
extern const ActorVtable kWorldActorVtable;
Actor* worldactor_new();
Actor* worldactor_construct(Actor* self);                                          // 0x80082d8c
Actor* worldactor_construct_screen(Actor* self, s16 model, s16 x, s16 y, int layer);  // 0x80082dd4
Actor* worldactor_construct_full(Actor* self, s16 model, const s32 pos[3], int angle_deg, int layer, s16 talk_entry,
                                 s16 pickpocket_entry, u32 flags);                 // 0x80082e5c
bool is_world_actor(const Actor* a);

}  // namespace ss::game::field
