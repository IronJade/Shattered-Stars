#pragma once

#include "core/types.h"
#include "game/field/tasks.h"
#include "runtime/libgte.h"

// The field camera: the camera block (0x80075a80..0x80075c44, the look-at camera and
// lights the 3D effects use; field 0x8005f8d8-0x800603a4) and the camera script object
// of field maps (overlay #3101: 0x17c bytes, vtable 0x8008ecf8, task priority 1, global
// 0x800752b4), which pans the background: a ring of 8 moves (script ext 0x85-0x93) of
// the BG focus (BgRenderer +8 / +0xa) to a background point, a world point or an
// actor, with speed, acceleration and a speed range. Field maps have a fixed camera;
// "moving the camera" scrolls the pre-rendered background.
namespace ss::game::field {

struct CameraBlock {
  u8 look_at = 0;        // 0x80075a80: 1 = eye / target (actor_sys_setup_view), 0 = angles
  s32 eye[3] = {};       // 0x80075a84: 20.12
  s32 target[3] = {};    // 0x80075a90: 20.12 (or angles)
  s32 zoom = 0x1000;     // 0x80075ab4 (0xccc on the world map)
  s16 distance = 0;      // 0x80075ab8
  u8 ambient[3] = {};    // 0x80075b30
  s16 offset[3] = {};    // 0x80075b28: camera offset vector
  s16 angles[4] = {};    // 0x80075b34..0x80075b3a: pitch, yaw, roll (0)
  s16 pitch2 = 0;        // 0x80075b3c
  s16 yaw2 = 0;          // 0x80075b3e
  s16 distance2 = 0;     // 0x80075b40
  // The matrices of the 3D effects (effects.h; camera_update_matrices in effects_camera.cpp).
  s16 light_rgb[3] = {};          // 0x80075ac0..0x80075ac4: light colour (world map terrain light)
  psyq::MATRIX view{};            // 0x80075ac8: camera rotation (camera_compute_view)
  psyq::MATRIX rot{};             // 0x80075b44: view * Rot(offset): what the particles draw with
  psyq::MATRIX fx_colour{};       // 0x80075b64: particles' GTE colour matrix (fx_setup_light)
  psyq::MATRIX fx_light{};        // 0x80075b84: particles' GTE light matrix
  psyq::MATRIX colour{};          // 0x80075ba4: light colour << 5 in column 0
  psyq::MATRIX light{};           // 0x80075bc4: L12 = 0x1000
  s16 light_dir[3][4] = {};       // 0x80075be4: three light directions (nothing on disc sets them)
  s16 light_col[3][4] = {};       // 0x80075bfc: their colours
  u8 light_mask = 0;              // 0x80075c14: lights in use (bit i)
  s32 light_angles[3] = {};       // 0x80075c3c..0x80075c44
  s32 fx_light_params[3] = {};    // 0x800746ec: particle light rotation {flag, yaw, angle}; 0 at reset
  s32 fx_scale[3] = {0x1000, 0x1000, 0x1000};  // 0x800746fc: object scale (camera_set_object_matrix)
};
CameraBlock& camera_block();
void camera_reset();            // 0x8005f8d8
void camera_update_matrices();  // 0x800603a4: camera_compute_view, light matrices, rot (effects_camera.cpp)
void camera_init_light_matrices(const s32 angles[3]);  // 0x8005fa54
s32 camera_compute_view();      // 0x8005fb2c: view from the eye / target (or the angles)
// 0x8005ff84: GTE RT / TR for an object at 'pos' (20.12): view * Raxis(rot[2]) * Ry(rot[1])
// (rot[0] != 0 picks the other axis and becomes the angle) and view * (pos - eye); scaled;
// H from the zoom. rot[2] is written when rot[0] != 0.
void camera_set_object_matrix(const s32 pos[3], s32 rot[3], const s32 scale[3]);
void gte_set_back_colour();     // 0x800617d8: BK = ambient * BG tint / 128
// The field program's own matrix helpers.
void field_rot_matrix(const psyq::SVECTOR* r, psyq::MATRIX* m);         // 0x8006e52c (C RotMatrix; m21 differs from libgte's)
void rot_matrix_y(s32 angle, psyq::MATRIX* m);                         // 0x8006ecf0
void rot_matrix_axis(const s32 axis[3], s32 angle, psyq::MATRIX* m);  // 0x8006ed5c (Rodrigues)

struct CamMove {             // 0x24 bytes at +0xc
  s32 x = 0, y = 0, z = 0;      // BG point (mode 0) or world point 20.12 (mode 1)
  s32 frames = 0;               // +0x0c
  s32 speed = 0;                // +0x10: 0x100 = one frame's share of the move per vsync
  s32 accel = 0;                // +0x14: negative = brakes from the start
  s32 min_speed = 0;            // +0x18 (0 = speed)
  s32 max_speed = 0;            // +0x1c
  s32 mode = 0;                 // +0x20: 1 = world point
};

struct CamScript final : Task {
  void update() override;       // vtable +0xc: camscript_update

  u8 read = 0;                  // +0x08
  u8 write = 0;                 // +0x09
  CamMove moves[8];             // +0x0c
  s32 cur[3] = {};              // +0x12c: BG focus << 8
  s32 step[3] = {};             // +0x13c: per-frame share of the move << 8
  s32 target[3] = {};           // +0x14c
  s32 speed = 0;                // +0x15c
  s32 min_speed = 0;            // +0x160
  s32 accel = 0;                // +0x164
  s32 braking = 0;              // +0x168
  s32 remaining = 0;            // +0x16c: frames << 8 left
  s32 half = 0;                 // +0x170: frames << 7
  s32 max_speed = 0;            // +0x174
  s32 brake_at = -1;            // +0x178: remaining when the top speed was reached
};

CamScript* camscript();                                      // 0x800752b4
// bg_helper_create (field_load_map): new 0x17c (0x8008e100) + 0x8008e1c0. Replaces the
// previous map's object.
CamScript* camscript_create();
CamScript* camscript_construct(CamScript* c);             // 0x8008e100
void camscript_destroy(CamScript* c, int flags);             // 0x8008e16c
void camscript_reset(CamScript* c);                          // 0x8008e1c0
// 0x8008e1d0 (ext 0x8a-0x8e): to world point (x, y, z) (map units).
void camscript_push_point(CamScript* c, s32 x, s32 y, s32 z, s32 frames, s32 speed, s32 accel, s32 min_speed,
                              s32 max_speed);
// 0x8008e228 (ext 0x85-0x89): to background point (x, y).
void camscript_push_bg(CamScript* c, s32 x, s32 y, s32 frames, s32 speed, s32 accel, s32 min_speed,
                           s32 max_speed);
// 0x8008e270 (ext 0x8f-0x93): to actor 'slot'.
void camscript_push_actor(CamScript* c, int slot, s32 frames, s32 speed, s32 accel, s32 min_speed,
                              s32 max_speed);
void camscript_push(CamScript* c, s32 x, s32 y, s32 z, s32 frames, s32 speed, s32 accel, s32 min_speed,
                        s32 max_speed, s32 mode);                   // 0x8008e308
void camscript_start_move(CamScript* c);                          // 0x8008e388
void camscript_update(CamScript* c);                         // 0x8008e7c8
int camscript_busy(const CamScript* c);                      // 0x8008e9f4: moves queued (VM wait state 4)

}  // namespace ss::game::field
