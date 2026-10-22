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
  s16 angles[4] = {};    // 0x80075b34..0x80075b3a: yaw / pitch
  s16 distance2 = 0;     // 0x80075b40
};
CameraBlock& camera_block();
void camera_reset();            // 0x8005f8d8
void camera_update_matrices();  // 0x800603a4 (light matrices: not ported; sprites don't use them)

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
