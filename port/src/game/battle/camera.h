#pragma once

#include "core/types.h"
#include "game/battle/battle.h"

// The battle camera (0xa4 bytes, vtable 0x8006b484, ctor camera_ctor 0x80030fe4,
// g_camera 0x8006f0ac): modes with priorities, an eye / look-at motion model and the
// intro fly-over. It moves g_cam_eye / g_cam_lookat (render.h ViewState); the view uses
// the previous frame's values (camera_update copies them first). Notes:
// analysis/battle_catalog/parts/p1.md "Camera".
namespace ss::game::battle {

struct Unit;

struct Camera : Task {
  s16 mode = 0;               // +0x08
  s16 step = 0;               // +0x0e: step inside the mode
  s32 speed = 0;              // +0x10 (x / 256)
  s32 accel = 0;              // +0x14
  s32 remaining = 0;          // +0x18: ticks << 8
  s32 half = 0;               // +0x1c
  s32 max_speed = 0;          // +0x20
  s32 min_speed = 0;          // +0x24
  s32 centre_x = 0;           // +0x28
  s32 centre_z = 0;           // +0x2c
  s16 ramp_mark = 0;          // +0x30
  s16 hold = 0;               // +0x32
  s16 focus_timer = 0;        // +0x34
  s16 framing = 0;            // +0x36
  s32 lookat_offset[4] = {};  // +0x38
  s32 ticks = 0;              // +0x48: ticks in the mode
  s32 zoom_target = 0x1000;   // +0x4c
  s32 zoom_speed = 0;         // +0x50
  s32 eye_dest[4] = {};       // +0x54
  s32 eye_vel[4] = {};        // +0x64
  s32 lookat_dest[4] = {};    // +0x74
  s32 lookat_vel[4] = {};     // +0x84
  BObj* target = nullptr;     // +0x94 (a unit, or a Shot in mode 0xc)
  BObj* focus = nullptr;      // +0x98 (a unit or a Shot)
  Unit* attacker = nullptr;   // +0x9c
  u8 priority = 0;            // +0xa0
  u8 decelerating = 0;        // +0xa1
  u8 look_straight = 0;       // +0xa2

  Camera();                   // camera_ctor 0x80030fe4
  void update() override;     // [1] camera_update 0x80031390
};

int camera_set_mode(Camera* c, int mode, BObj* target);      // 0x80031118
int camera_request_mode(Camera* c, int mode, BObj* target);  // 0x800310b0
void camera_end_mode(Camera* c, int mode, int next, Unit* u);  // 0x800312b0
void camera_reset(Camera* c);                                // 0x80031358 (0x80076be8 camera_reset() is render.h)
void camera_set_motion(Camera* c, s32 ticks, s32 speed, s32 accel, s32 min, s32 max);  // 0x800338d8
int camera_motion_step(Camera* c, u32 snap);                 // 0x8003391c
void camera_step_eye(Camera* c);                             // 0x80033768
void camera_step_lookat(Camera* c);                          // 0x80033820
void camera_orbit_point(s32 out[4], const s32 centre[3], s32 yaw, s32 pitch, s32 dist);  // 0x80033b18
void camera_set_eye_orbit(Camera* c, const s32 centre[3], s32 yaw, s32 pitch, s32 dist);  // 0x80033aac
void camera_set_lookat_dest(Camera* c, const s32 p[3]);      // 0x80034328
void camera_set_lookat_dir(Camera* c, s16 angle, s32 dist);  // 0x800336ec
void camera_eye_velocity(Camera* c, s32 ticks);              // 0x80033c34
void camera_lookat_velocity(Camera* c, s32 ticks);           // 0x80033cb0
s32 camera_units_center_x(Camera* c, int first, int last);   // 0x80033f0c: returns the x span
s32 camera_units_center_z(Camera* c, int first, int last);   // 0x80034014: returns the z span
bool camera_hold_tick(Camera* c);                            // 0x800341ac
int battle_party_start_appear();                             // 0x80034128
void camera_setup_follow_target(Camera* c);                  // 0x80032140 (modes 5 / 7 / 15)
void camera_setup_overview(Camera* c, s32 ticks, int all_units);  // 0x80031e68 (sets mode 11)
void camera_set_follow_offset(Camera* c, const s32 ofs[3]);  // 0x80032240 (mode 7 only)
void camera_move_to(Camera* c, const s32 centre[3], s32 yaw, s32 pitch, s32 dist, s32 ticks, s32 speed,
                    s32 accel);                              // 0x80032440 (mode 10)
void camera_orbit_unit(Camera* c, BObj* u, s32 yaw, s32 pitch, s32 dist, s32 ticks, s32 speed,
                       s32 accel);                           // 0x80032544 (mode 12)
void camera_set_focus_unit(Camera* c, BObj* who, BObj* focus);   // 0x800342cc
void camera_set_lookat_unit(Camera* c, BObj* who, BObj* focus);  // 0x800342ec
void camera_set_focus_timer(Camera* c, BObj* focus, s16 ticks);  // 0x8003430c
void camera_set_hold(Camera* c, s16 ticks);                  // 0x80034358
void camera_set_zoom(Camera* c, s32 zoom, s32 speed);        // 0x8003434c
// #1984.0: end 'mode' after 'ticks' (battle_frame_director runs it down; 0x80072544) and
// end the scheduled mode now (0x800724f4).
void camera_schedule_end(int mode, Unit* u, s16 ticks);
void camera_end_scheduled_now();
extern const s32 kZeroVector[4];                             // g_zero_vector 0x8006e33c

}  // namespace ss::game::battle
