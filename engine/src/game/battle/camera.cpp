#include "game/battle/camera.h"

#include <cstring>

#include "game/battle/render.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

void todo(const char* what) {
  runtime::Runtime::get().log_once(std::string("battle-camera-") + what,
                                   std::string("battle: camera not ported yet: ") + what);
}

Unit* unit_at(int i) {
  const BattleGlobals& g = bg();
  return i < 4 ? static_cast<Unit*>(g.party_units[i]) : g.enemy_units[i - 4];
}

// 0x80067548 vec_step: (to - from) / steps.
void vec_step(s32 out[4], const s32 from[3], const s32 to[3], s32 steps) {
  if (steps == 0) steps = 1;
  for (int i = 0; i < 3; ++i) out[i] = (to[i] - from[i]) / steps;
}

// 0x80074ca4 (#1984.0): the battle-start banner (#1980 string 'id').
void battle_banner_show(int id) {
  runtime::Runtime::get().log_once("battle-banner", "battle: message banner not ported yet (string " +
                                                        std::to_string(id) + ")");
}

void camera_setup_intro(Camera* c) {
  ViewState& v = view();
  const EnemyUnit* e0 = bg().enemy_units[0];
  v.cam_eye[1] = static_cast<s32>(0xFFC18000);
  v.view_mode = 1;
  v.cam_eye[2] = 0;
  v.cam_eye[0] = stage().bounds[2] - 0x1F4000;
  static const s32 kOrigin[3] = {0, 0, 0};
  const s32* p = e0 != nullptr ? e0->pos : kOrigin;
  camera_set_eye_orbit(c, p, -0x2AA, 0xE3, 0x800);
  camera_set_lookat_dest(c, p);
  camera_set_lookat_dir(c, 0x38E, 0x800);
  v.cam_lookat[1] = v.cam_eye[1];
  c->step = 0;
  camera_set_motion(c, 0xBE, 0x80, 5, 0, 0x10000);
  camera_eye_velocity(c, 0xBE);
  camera_lookat_velocity(c, 0xBE);
}

// The other intro openings (camera_set_mode 8 / 9 / 13 / 16 / 17). Each starts the eye at
// a fixed place, orbits a centre for the eye's destination and flies there.
// 0x800316b8 camera_setup_enemy_group (mode 8): the enemies' centroid from behind.
void camera_setup_enemy_group(Camera* c) {
  ViewState& v = view();
  const BattleGlobals& g = bg();
  s32 centre[3] = {0, 0, 0};
  int n = 0;
  for (const EnemyUnit* e : g.enemy_units) {
    if (e == nullptr) continue;
    ++n;
    for (int k = 0; k < 3; ++k) centre[k] += e->pos[k];
  }
  if (n != 0)
    for (int k = 0; k < 3; ++k) centre[k] /= n;
  v.cam_eye[2] = static_cast<s32>(0xFF418000);
  v.view_mode = 1;
  v.cam_eye[1] = static_cast<s32>(0xFFF88000);
  v.cam_eye[0] = stage().bounds[0] + 0x1F4000;
  camera_set_eye_orbit(c, centre, 0x800, 0xE3, 0x800);
  v.cam_lookat[2] = static_cast<s32>(0xFFBE8000);
  v.cam_lookat[0] = v.cam_eye[0];
  v.cam_lookat[1] = v.cam_eye[1];
  camera_set_lookat_dest(c, centre);
  c->step = 0;
  camera_set_motion(c, 0xBE, 0x80, 5, 0, 0x10000);
  camera_eye_velocity(c, 0xBE);
  camera_lookat_velocity(c, 0xBE);
}

// 0x80031858 camera_setup_stage_pan (mode 9): along the stage from its low-x end.
void camera_setup_stage_pan(Camera* c) {
  ViewState& v = view();
  v.cam_eye[1] = static_cast<s32>(0xFF830000);
  v.cam_eye[2] = 0;
  v.view_mode = 1;
  const s32 span = (stage().bounds[2] - stage().bounds[0]) * 5;
  v.cam_eye[0] = stage().bounds[0] + 0x1F4000;
  const s32 centre[3] = {v.cam_eye[0] + span / 4, static_cast<s32>(0xFFF9C000), 0};
  camera_set_eye_orbit(c, centre, 0, 0x71, 0x800);
  v.cam_lookat[1] = static_cast<s32>(0xFF768000);
  v.cam_lookat[2] = 0;
  v.cam_lookat[0] = v.cam_eye[0] - 0x400000;
  camera_set_lookat_dest(c, centre);
  c->step = 0;
  camera_set_motion(c, 0xBE, 0x80, 5, 0, 0x10000);
  camera_eye_velocity(c, 0xBE);
  camera_lookat_velocity(c, 0xBE);
}

// 0x80031990 camera_setup_fixed_view (mode 13): the point (1000, 100, 400).
void camera_setup_fixed_view(Camera* c) {
  ViewState& v = view();
  v.cam_eye[1] = static_cast<s32>(0xFF830000);
  v.view_mode = 1;
  const s32 centre[3] = {0x3E8000, 0x64000, 0x190000};
  v.cam_eye[2] = 0xBB8000;
  v.cam_eye[0] = stage().bounds[0] + 0x1F4000;
  camera_set_eye_orbit(c, centre, 0, 0x71, 0x800);
  v.cam_lookat[1] = static_cast<s32>(0xFFA24000);
  v.cam_lookat[2] = 0;
  v.cam_lookat[0] = v.cam_eye[0];
  camera_set_lookat_dest(c, centre);
  c->step = 0;
  camera_set_motion(c, 0xBE, 0x80, 5, 0, 0x10000);
  camera_eye_velocity(c, 0xBE);
  camera_lookat_velocity(c, 0xBE);
}

// 0x80031aac camera_setup_enemy_ground (mode 16): enemy 0 at ground level.
void camera_setup_enemy_ground(Camera* c) {
  ViewState& v = view();
  const EnemyUnit* e0 = bg().enemy_units[0];
  v.cam_eye[1] = static_cast<s32>(0xFFCE0000);
  v.view_mode = 1;
  v.cam_eye[2] = 0;
  const s32 centre[3] = {e0 != nullptr ? e0->pos[0] : 0, 0, e0 != nullptr ? e0->pos[2] : 0};
  v.cam_eye[0] = stage().bounds[0] + 0x1F4000;
  camera_set_eye_orbit(c, centre, -0x2AA, 0xE3, 0x800);
  camera_set_lookat_dest(c, centre);
  camera_set_lookat_dir(c, 0x38E, 0x800);
  v.cam_lookat[1] = v.cam_eye[1];
  c->step = 0;
  camera_set_motion(c, 0xBE, 0x80, 5, 0, 0x10000);
  camera_eye_velocity(c, 0xBE);
  camera_lookat_velocity(c, 0xBE);
}

// 0x80031bb4 camera_setup_origin (mode 17): a slow 600-tick circle onto the stage origin.
// It leaves the intro step as it was.
void camera_setup_origin(Camera* c) {
  ViewState& v = view();
  v.cam_eye[1] = static_cast<s32>(0xFF254000);
  v.view_mode = 1;
  v.cam_eye[0] = 0;
  v.cam_eye[2] = 0;
  static const s32 kOrigin[3] = {0, 0, 0};
  camera_set_eye_orbit(c, kOrigin, 0xC00, 0xE3, 0xC00);
  camera_set_lookat_dest(c, kOrigin);
  camera_set_lookat_dir(c, 0x400, 0x800);
  v.cam_lookat[1] = v.cam_eye[1];
  camera_set_motion(c, 600, 0x80, 5, 0, 0x10000);
  camera_eye_velocity(c, 600);
  camera_lookat_velocity(c, 600);
}

// 0x80032650: the intro sequence of modes 1, 8, 9, 13, 16 and 17.
void camera_update_intro(Camera* c) {
  ViewState& v = view();
  BattleGlobals& g = bg();
  switch (c->step) {
    case 0: {
      camera_step_eye(c);
      camera_step_lookat(c);
      if (camera_motion_step(c, 0) == 0) break;
      ++c->step;
      int n = 0;
      while (n < 8 && g.enemy_units[n] != nullptr) ++n;
      const EnemyUnit* last = n > 0 ? g.enemy_units[n - 1] : nullptr;
      s32 p[3] = {0, 0, 0};
      if (last != nullptr) std::memcpy(p, last->pos, sizeof p);
      if (c->mode == 0x10) p[1] = 0;
      camera_set_eye_orbit(c, p, -0x2AA, 0xE3, 0x800);
      camera_set_lookat_dest(c, p);
      camera_set_motion(c, 0x50, 0x200, -3, 0xC0, 0x200);
      camera_eye_velocity(c, 0x50);
      camera_lookat_velocity(c, 0x50);
      break;
    }
    case 1: {
      camera_step_eye(c);
      camera_step_lookat(c);
      if (camera_motion_step(c, 0) == 0) break;
      if (g.intro_type == 2) {
        c->step = static_cast<s16>(c->step + 2);
        c->hold = 0;
      } else {
        ++c->step;
        c->hold = 0x14;
      }
      break;
    }
    case 2:
      if (camera_hold_tick(c)) ++c->step;
      break;
    case 3: {
      if (!camera_hold_tick(c) || battle_party_start_appear() == 0) break;
      s32 dist = ((camera_units_center_x(c, 0, 4) >> 12) << 11) / 1000;
      if (dist < 0x800) dist = 0x800;
      const s32 centre[3] = {c->centre_x, 0, 0};
      s32 o[4];
      camera_orbit_point(o, centre, -0x400, 0xE3, dist);
      const u8 kind = g.intro_type;
      if (kind == 0) {
        v.cam_lookat[1] = static_cast<s32>(0xFFFA6000);
        v.cam_lookat[2] = 0;
        v.cam_eye[0] = o[0];
        v.cam_eye[1] = o[1];
        v.cam_eye[2] = o[2];
        v.cam_lookat[0] = o[0];
      }
      ++c->step;
      if (kind == 3) {
        battle_banner_show(0x11);
      } else if (kind != 0) {
        battle_banner_show(kind + 8);
      }
      s32 span = (camera_units_center_x(c, 0, 0xC) >> 12) + 0x80;
      const s32 span_z = (camera_units_center_z(c, 0, 0xC) >> 12) + 0x80;
      if (span < span_z) span = span_z;
      const s32 c2[3] = {c->centre_x, 0, static_cast<s32>(0xFFC18000)};
      camera_set_eye_orbit(c, c2, -0x400, 0xE3, (span << 11) / 1000);
      c->lookat_dest[1] = static_cast<s32>(0xFFFA6000);
      c->lookat_dest[2] = static_cast<s32>(0xFFC18000);
      c->lookat_dest[0] = c->eye_dest[0];
      camera_set_motion(c, 0x23, 0x40, 10, 0, 0x10000);
      camera_eye_velocity(c, 0x23);
      camera_lookat_velocity(c, 0x23);
      break;
    }
    case 4:
      camera_step_eye(c);
      camera_step_lookat(c);
      if (camera_motion_step(c, 0) != 0) c->mode = 0;
      break;
    default:
      break;
  }
}

}  // namespace

Camera::Camera() {
  ViewState& v = view();
  priority = 0;
  look_straight = 0;
  v.view_mode = 1;
  v.cam_eye[0] = 0;
  v.cam_eye[1] = static_cast<s32>(0xFFBE6000);
  v.cam_eye[2] = static_cast<s32>(0xFEDA4000);
  v.cam_lookat[0] = 0;
  v.cam_lookat[1] = static_cast<s32>(0xFFC18000);
  v.cam_lookat[2] = 0;
  v.zoom = 0x1000;
  zoom_target = 0x1000;
  zoom_speed = 0;
}

void Camera::update() {
  ViewState& v = view();
  const BattleGlobals& g = bg();
  for (int i = 0; i < 3; ++i) {
    v.view_eye[i] = v.cam_eye[i];
    v.view_target[i] = v.cam_lookat[i];
  }
  if (zoom_speed != 0) {
    bool done;
    if (v.zoom < zoom_target) {
      v.zoom += zoom_speed * g.frame_ticks;
      done = zoom_target < v.zoom;
    } else {
      v.zoom -= zoom_speed * g.frame_ticks;
      done = v.zoom < zoom_target;
    }
    if (done) {
      v.zoom = zoom_target;
      zoom_speed = 0;
    }
  }
  if (mode == 0 || (g.flags & 0xC) != 0) return;
  switch (mode) {
    case 1:
    case 8:
    case 9:
    case 13:
    case 16:
    case 17:
      camera_update_intro(this);
      break;
    case 2: todo("mode 2 follow leader"); break;
    case 3:
    case 11: {
      // camera_update_move_idle 0x80033348: step eye and look-at; mode 0 on arrival.
      camera_step_eye(this);
      camera_step_lookat(this);
      if (camera_motion_step(this, 0) != 0) mode = 0;
      break;
    }
    case 4:
    case 14: todo("modes 4/14 track all"); break;
    case 5:
    case 7: todo("modes 5/7 follow target"); break;
    case 6:
    case 10: {
      // camera_update_mode6 0x800335d0 / camera_update_mode10 0x80033618.
      camera_step_eye(this);
      camera_step_lookat(this);
      if (camera_motion_step(this, 0) != 0) mode = 0;
      break;
    }
    case 12: todo("mode 12 orbit unit"); break;
    case 15: todo("mode 15 behind target"); break;
    default: break;
  }
  ticks += g.frame_ticks;
}

int camera_set_mode(Camera* c, int mode, Unit* target) {
  if (mode == 0) return 0;
  const u8 prio = battle_data(0x8006B493 + mode) ? *battle_data(0x8006B493 + mode) : 0;
  if (c->mode != 0 && prio < c->priority) return 0;
  if (target != nullptr) {
    c->target = target;
    c->focus = nullptr;
    c->focus_timer = 0;
  }
  c->mode = static_cast<s16>(mode);
  switch (mode) {
    case 1: camera_setup_intro(c); break;
    case 8: camera_setup_enemy_group(c); break;
    case 9: camera_setup_stage_pan(c); break;
    case 10:
    case 12: break;  // no setup
    case 13: camera_setup_fixed_view(c); break;
    case 16: camera_setup_enemy_ground(c); break;
    case 17: camera_setup_origin(c); break;
    default:
      todo(("setup of mode " + std::to_string(mode)).c_str());
      c->mode = 0;
      break;
  }
  c->ticks = 0;
  c->priority = prio;
  return 1;
}

int camera_request_mode(Camera* c, int mode, Unit* target) {
  if (mode == 0) return 0;
  if (c->mode == mode) {
    if (mode != 5 && mode != 7) return 0;
    if (c->target == nullptr) return 0;
    if (target == c->target) return 0;
  }
  return camera_set_mode(c, mode, target);
}

void camera_end_mode(Camera* c, int mode, int next, Unit* u) {
  if (c->mode != mode || (c->mode == 5 && u != c->target)) return;
  std::memset(c->lookat_offset, 0, sizeof c->lookat_offset);  // g_zero_vector
  c->mode = 0;
  if (next == 1) {
    camera_set_mode(c, 3, nullptr);
  } else if (next == 2) {
    camera_set_mode(c, 0xB, nullptr);
  }
}

void camera_reset_mode(Camera* c) {
  c->mode = 0;
  std::memset(c->lookat_offset, 0, sizeof c->lookat_offset);
}

void camera_set_motion(Camera* c, s32 ticks, s32 speed, s32 accel, s32 min, s32 max) {
  c->speed = speed;
  c->min_speed = min == 0 ? speed : min;
  c->decelerating = static_cast<u8>(static_cast<u32>(accel) >> 31);
  c->remaining = ticks << 8;
  c->half = ticks << 7;
  c->accel = accel;
  c->max_speed = max;
  c->ramp_mark = -1;
}

int camera_motion_step(Camera* c, u32 snap) {
  ViewState& v = view();
  const s32 t = bg().frame_ticks;
  const s32 rem = c->remaining - t * c->speed;
  c->remaining = rem;
  const s32 t1 = rem - (c->half - c->ramp_mark);
  if (c->decelerating != 0) {
    c->speed += c->accel * t;
  } else if (c->speed < c->max_speed || rem < t1) {
    if (c->min_speed >= 0) {
      c->speed += (c->half < rem ? c->accel : -c->accel) * t;
    } else if (c->half < rem) {
      c->speed += c->accel * t;
    }
  }
  if (!(c->speed < c->max_speed)) {
    c->speed = c->max_speed;
    if (c->ramp_mark == -1) c->ramp_mark = static_cast<s16>(c->remaining);
  }
  if (c->speed < c->min_speed) c->speed = c->min_speed;
  if (c->remaining >= 1) return 0;
  if (snap & 1) std::memcpy(v.cam_eye, c->eye_dest, 12);
  if (snap & 2) std::memcpy(v.cam_lookat, c->lookat_dest, 12);
  return 1;
}

void camera_step_eye(Camera* c) {
  ViewState& v = view();
  const s32 t = bg().frame_ticks;
  for (int i = 0; i < 3; ++i) v.cam_eye[i] += c->eye_vel[i] * t * c->speed >> 8;
}

void camera_step_lookat(Camera* c) {
  ViewState& v = view();
  const s32 t = bg().frame_ticks;
  for (int i = 0; i < 3; ++i) v.cam_lookat[i] += c->lookat_vel[i] * t * c->speed >> 8;
}

void camera_orbit_point(s32 out[4], const s32 centre[3], s32 yaw, s32 pitch, s32 dist) {
  const s32 cp = bcos(static_cast<u32>(pitch));
  out[0] = (cp * bcos(static_cast<u32>(yaw)) >> 12) * dist + centre[0];
  out[1] = centre[1] - bsin(static_cast<u32>(pitch)) * dist;
  out[2] = (cp * bsin(static_cast<u32>(yaw)) >> 12) * dist + centre[2];
}

void camera_set_eye_orbit(Camera* c, const s32 centre[3], s32 yaw, s32 pitch, s32 dist) {
  s32 o[4] = {};
  camera_orbit_point(o, centre, yaw, pitch, dist);
  std::memcpy(c->eye_dest, o, 12);
}

void camera_set_lookat_dest(Camera* c, const s32 p[3]) { std::memcpy(c->lookat_dest, p, 12); }

void camera_set_lookat_dir(Camera*, s16 angle, s32 dist) {
  ViewState& v = view();
  v.cam_lookat[0] = bcos(static_cast<u32>(static_cast<s32>(angle))) * dist + v.cam_eye[0];
  v.cam_lookat[2] = bsin(static_cast<u32>(static_cast<s32>(angle))) * dist + v.cam_eye[2];
}

void camera_eye_velocity(Camera* c, s32 ticks) { vec_step(c->eye_vel, view().cam_eye, c->eye_dest, ticks); }

void camera_lookat_velocity(Camera* c, s32 ticks) {
  vec_step(c->lookat_vel, view().cam_lookat, c->lookat_dest, ticks);
}

s32 camera_units_center_x(Camera* c, int first, int last) {
  s32 lo = 0xC350000, hi = -0xC350000;
  for (int i = first; i < last; ++i) {
    const Unit* u = unit_at(i);
    if (unit_is_down(u)) continue;
    if (u->pos[0] < lo) lo = u->pos[0];
    if (hi < u->pos[0]) hi = u->pos[0];
  }
  c->centre_x = (hi - lo) / 2 + lo;
  return hi - lo;
}

s32 camera_units_center_z(Camera* c, int first, int last) {
  s32 lo = 0xC350000, hi = -0xC350000;
  for (int i = first; i < last; ++i) {
    const Unit* u = unit_at(i);
    if (unit_is_down(u)) continue;
    if (u->pos[2] < lo) lo = u->pos[2];
    if (hi < u->pos[2]) hi = u->pos[2];
  }
  c->centre_z = (hi - lo) / 3 + lo;
  return hi - lo;
}

bool camera_hold_tick(Camera* c) {
  const s32 h = static_cast<s32>(static_cast<u16>(c->hold)) - bg().frame_ticks;
  c->hold = static_cast<s16>(h);
  const bool done = static_cast<s32>(static_cast<u32>(h) << 16) < 1;
  if (done) c->hold = 0;
  return done;
}

int battle_party_start_appear() {
  BattleGlobals& g = bg();
  if (g.state == kStateLoading) return 0;
  for (PartyUnit* p : g.party_units) {
    if (p == nullptr) continue;
    p->appear = static_cast<u8>((p->appear & 0xFE) | 2);
    p->counters[0] = 0;
  }
  return 1;
}

}  // namespace ss::game::battle
