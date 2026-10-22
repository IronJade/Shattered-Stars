#include "game/battle/camera.h"

#include <cstring>

#include "game/battle/render.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
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
        battle_message_show(0x11);
      } else if (kind != 0) {
        battle_message_show(kind + 8);
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


// 0x80033d2c / 0x80033e1c: velocity toward the destination scaled by 'k' (the distance
// over 16 as the step count); returns the distance in units.
s32 camera_eye_velocity_by_distance(Camera* c, s32 k) {
  const ViewState& v = view();
  const s32 d = vec_distance_fx(v.cam_eye, c->eye_dest);
  vec_step(c->eye_vel, v.cam_eye, c->eye_dest, d >> 4);
  for (int i = 0; i < 3; ++i) c->eye_vel[i] *= k;
  return d >> 12;
}

s32 camera_lookat_velocity_by_distance(Camera* c, s32 k) {
  const ViewState& v = view();
  const s32 d = vec_distance_fx(v.cam_lookat, c->lookat_dest);
  vec_step(c->lookat_vel, v.cam_lookat, c->lookat_dest, d >> 4);
  for (int i = 0; i < 3; ++i) c->lookat_vel[i] *= k;
  return d >> 12;
}

// 0x800341e0: is the point (x, z) inside the view cone around 'angle'? 0 inside, else
// bit 0 on the near side and 2 when the point is within 0x600 of the eye in z.
u8 camera_point_visibility(Camera* c, s32 x, s32 z, s32 angle) {
  const ViewState& v = view();
  z -= c->eye_dest[2];
  const u8 near = static_cast<u8>((z < 0x600) * 2);
  const s32 half = ratan2(0x82, (v.zoom << 9) >> 12);
  const s32 lo = angle - (half - 0x800);
  const s32 hi = angle + half + 0x800;
  s32 a = ratan2(z, x - c->eye_dest[0]);
  if (lo <= a) {
    if (hi < a) return static_cast<u8>(near + 2);
    if (0x800 < lo) a += 0x1000;
    if (hi < -0x800) a -= 0x1000;
    if (lo <= a) return hi < a ? static_cast<u8>(near + 2) : 0;
  }
  return static_cast<u8>(near | 1);
}

// 0x80031c98 camera_setup_follow_leader (mode 2).
void camera_setup_follow_leader(Camera* c) {
  BattleGlobals& g = bg();
  view().view_mode = 1;
  camera_set_lookat_dest(c, g.party_units[g.controlled_slot]->pos);
  c->focus = nullptr;
  c->focus_timer = 0;
  c->step = 0;
  c->framing = 0;
  const s32 d = camera_lookat_velocity_by_distance(c, 0x3200);
  camera_set_motion(c, d / 0x32, 0x100, 0, 0, 0x10000);
}

// 0x80031d3c camera_setup_party_view (mode 3).
void camera_setup_party_view(Camera* c) {
  BattleGlobals& g = bg();
  view().view_mode = 1;
  const s32 span = camera_units_center_x(c, 0, 0xC) >> 12;
  s32 w = span;
  if (span < 0x29A) w = 0x29A;
  if (0x500 < span) w = 0x500;
  const Unit* u = g.party_units[g.controlled_slot];
  const s32 p[3] = {u->pos[0], 0, u->pos[2]};
  camera_set_eye_orbit(c, p, -0x400, 0xE3, (((w * 3) / 2) << 11) / 1000);
  camera_set_lookat_dest(c, p);
  camera_set_motion(c, 0x3C, 0x40, 10, 0, 0x10000);
  camera_eye_velocity(c, 0x3C);
  camera_lookat_velocity(c, 0x3C);
  c->step = 0;
}

// 0x80032050 camera_setup_track_all (modes 14 and 4).
void camera_setup_track_all(Camera* c) {
  view().view_mode = 1;
  const s32 span = camera_units_center_x(c, 0, 0xC);
  camera_units_center_z(c, 0, 0xC);
  const s32 p[3] = {c->centre_x, 0, c->centre_z};
  camera_set_eye_orbit(c, p, -0x400, 0xE3, ((span >> 12) << 11) / 1000);
  c->lookat_dest[1] = static_cast<s32>(0xFFFD8000);
  c->step = 0;
  c->lookat_dest[0] = c->eye_dest[0];
  c->lookat_dest[2] = c->centre_z;
  camera_lookat_velocity(c, 0x14);
  camera_set_motion(c, 0x14, 0x100, 0, 0, 0x10000);
}

// 0x80031fb4 camera_setup_field_view (mode 4): not with one enemy left on screen.
void camera_setup_field_view(Camera* c) {
  BattleGlobals& g = bg();
  if (g.enemy_count == 1) {
    for (const EnemyUnit* e : g.enemy_units)
      if (!unit_is_down(e) && bobj_on_screen(e)) {
        c->mode = 0;
        return;
      }
  }
  camera_setup_track_all(c);
}

// 0x800322c8 camera_setup_closeup (mode 6, the victory pose).
void camera_setup_closeup(Camera* c) {
  ViewState& v = view();
  const StageData& st = stage();
  v.view_mode = 1;
  camera_set_eye_orbit(c, c->target->pos, -0x400, 0x71, 0x640);
  s32 sweep;
  if (static_cast<u32>(rng_next() & 0x7FFF) < 0x4001) {
    v.cam_eye[0] = c->eye_dest[0] - 0x200000;
    sweep = 0x800000;
    if (v.cam_eye[0] < st.bounds[0]) v.cam_eye[0] = st.bounds[0];
  } else {
    v.cam_eye[0] = c->eye_dest[0] + 0x200000;
    sweep = -0x800000;
    if (st.bounds[2] < v.cam_eye[0]) v.cam_eye[0] = st.bounds[2];
  }
  v.cam_eye[1] = c->eye_dest[1];
  c->eye_dest[0] += sweep;
  v.cam_eye[2] = c->eye_dest[2] + 0x100000;
  c->eye_dest[1] -= 0x400000;
  camera_set_lookat_dest(c, c->target->pos);
  v.cam_lookat[0] = c->target->pos[0];
  v.cam_lookat[2] = c->target->pos[2];
  v.cam_lookat[1] = c->target->pos[1];
  camera_set_motion(c, 600, 0x40, 10, 0, 0x10000);
  camera_eye_velocity(c, 600);
  camera_lookat_velocity(c, 600);
}

// 0x80032b30 camera_update_follow (modes 2 / 5 / 7 / 15): an elevated orbit of 'who'
// (raised while a kind-2 obstacle stands between), the look-at on it (or on the focus
// unit), framing the unit attacking it in mode 7, and a view-cone search that keeps the
// focus unit in sight.
void camera_update_follow(Camera* c, s32 elev, BObj* who) {
  BattleGlobals& g = bg();
  ViewState& v = view();
  const StageData& st = stage();
  s32 look[3] = {who->pos[0] + c->lookat_offset[0], who->pos[1] + c->lookat_offset[1],
                 who->pos[2] + c->lookat_offset[2]};
  const s32 elev0 = elev;
  for (int i = 0; i < 0x18; ++i) {
    const auto& o = st.obstacles[i];
    if (o.kind != 2) continue;
    const s32 mz = (o.max[2] - o.min[2]) / 2 + o.min[2];
    if (!(mz < look[2]) || !(o.min[0] <= look[0]) || !(look[0] <= o.max[0])) continue;
    const s32 dz = look[2] - mz;
    const s32 top = o.min[1];
    for (;;) {
      const s32 a = (elev * 0x1000) / 0x168;
      if ((-top >> 12) + 100 <= ((dz >> 12) * bsin(static_cast<u32>(a))) / bcos(static_cast<u32>(a))) break;
      elev += 4;
      if (!(elev < 0x51)) {
        elev = 0x50;
        break;
      }
    }
  }
  camera_set_eye_orbit(c, who->pos, -0x400, (elev * 0x1000) / 0x168, 0x800);
  if (c->look_straight == 0 || c->focus == nullptr) {
    camera_set_lookat_dest(c, look);
  } else {
    camera_set_lookat_dest(c, c->focus->pos);
    if (c->framing != 0) {
      const s32 d = camera_lookat_velocity_by_distance(c, 0x500);
      camera_set_motion(c, d / 5, 0x100, 0, 0, 0x10000);
      c->step = 0;
      c->framing = 0;
      c->attacker = nullptr;
    }
  }
  bool to_cone = c->focus != nullptr;
  if (c->mode == 7 && (g.flags & 0x40) == 0) {
    // The unit attacking 'who' (allies first when 'who' is an enemy).
    Unit* found = nullptr;
    Unit* party_who = nullptr;
    int first_side = 1;
    for (int i = 0; i < 4; ++i)
      if (g.party_units[i] == who) {
        first_side = 0;
        party_who = g.party_units[i];
      }
    for (int side = 0; side < 2; ++side) {
      if (side == first_side) {
        for (int i = 0; i < 4; ++i) {
          Unit* u = g.party_units[i];
          if (u == nullptr || u == who || u->target != who || u->action <= 8) continue;
          s32 dx = u->pos[0] - who->pos[0];
          if (dx < 0) dx = -dx;
          if (dx < 0x3C0000) found = u;
        }
      } else {
        for (int i = 0; i < 8; ++i) {
          Unit* u = g.enemy_units[i];
          if (u == nullptr || u == who || u->target != who || u->action <= 8) continue;
          s32 dx = u->pos[0] - who->pos[0];
          if (dx < 0) dx = -dx;
          if (dx < 0x3C0000) {
            if (party_who == nullptr || party_who->action < 9) found = u;
            else if (u == party_who->target) found = u;
          }
        }
      }
    }
    if (found != nullptr) {
      s32 dx = who->pos[0] - found->pos[0];
      const bool wide = dx < 0 ? 0x1E0000 < found->pos[0] - who->pos[0] : 0x1E0000 < dx;
      if (wide) {
        const s32 mid[3] = {(look[0] - found->pos[0]) / 2 + found->pos[0], look[1], look[2]};
        if (c->look_straight == 0 || c->focus == nullptr) {
          camera_set_lookat_dest(c, mid);
          if (c->framing == 0 || c->attacker != found) {
            const s32 d = camera_lookat_velocity_by_distance(c, 0x500);
            camera_set_motion(c, d / 5, 0x100, 0, 0, 0x10000);
            c->step = 0;
            c->framing = 1;
            c->attacker = found;
          }
          to_cone = c->focus != nullptr;
        }
      }
    }
  }
  if (to_cone && c->look_straight == 0) {
    s32 yaw = -0x400, dist = 0x800;
    for (int k = 0; k < 0x13; ++k) {
      const u8 vis = camera_point_visibility(c, c->focus->pos[0], c->focus->pos[2], yaw);
      if (vis == 0) break;
      yaw += (vis & 1) == 0 ? 0x71 : -0x71;
      if (2 < vis) dist += 0x80;
      elev -= 4;
      if (elev < elev0) elev = elev0;
      camera_set_eye_orbit(c, look, yaw, (elev * 0x1000) / 0x168, dist);
    }
    if (c->focus_timer != 0) {
      const s32 t = c->focus_timer - g.frame_ticks;
      c->focus_timer = static_cast<s16>(t);
      if (static_cast<s16>(t) < 1) {
        c->focus_timer = 0;
        c->focus = nullptr;
      }
    }
  }
  const s32 e[3] = {v.cam_eye[0], v.cam_eye[1], v.cam_eye[2]};
  const s32 d = vec_distance_fx(e, c->eye_dest);
  if (d < 0x2000) {
    std::memcpy(v.cam_eye, c->eye_dest, 12);
  } else {
    if (d < 0x40000) camera_eye_velocity_by_distance(c, d / 0x50);
    else camera_eye_velocity(c, d < 0x80000 ? 10 : 0x14);
    camera_step_eye(c);
  }
  if (c->step == 0) {
    if (camera_motion_step(c, 2) != 0) ++c->step;
    camera_lookat_velocity(c, c->remaining / 0x180);
  } else {
    std::memcpy(v.cam_lookat, c->lookat_dest, 12);
  }
  camera_step_lookat(c);
}

// 0x80032a64 camera_update_behind_target (mode 15).
void camera_update_behind_target(Camera* c) {
  const s32 centre[3] = {c->target->pos[0], 0, c->target->pos[2]};
  s32 o[4];
  camera_orbit_point(o, centre, -0x400, 0x200, 0x800);
  s32 a = ratan2(c->target->pos[1] - o[1], c->target->pos[2] - o[2]) * 0x168;
  if (a < 0) a += 0xFFF;
  camera_update_follow(c, a >> 12, c->target);
}

// 0x80033390 camera_update_track_all (modes 4 / 14).
void camera_update_track_all(Camera* c) {
  ViewState& v = view();
  const s32 sx = camera_units_center_x(c, 0, 0xC);
  const s32 sz = camera_units_center_z(c, 0, 0xC);
  s32 span = sx >> 12;
  if (sx >> 12 < sz >> 12) span = sz >> 12;
  span += 0x40;
  if (span < 0x400) span = 0x400;
  const s32 p[3] = {c->centre_x, 0, c->centre_z};
  camera_set_eye_orbit(c, p, -0x400, 0xE3, (span << 11) / 1000);
  const s32 e[3] = {v.cam_eye[0], v.cam_eye[1], v.cam_eye[2]};
  c->lookat_dest[1] = static_cast<s32>(0xFFFD8000);
  c->lookat_dest[0] = c->eye_dest[0];
  c->lookat_dest[2] = c->centre_z;
  const s32 d = vec_distance_fx(e, c->eye_dest);
  if (d < 0x2000) {
    std::memcpy(v.cam_eye, c->eye_dest, 12);
  } else {
    if (d < 0x40000) camera_eye_velocity_by_distance(c, d / 0x50);
    else camera_eye_velocity(c, d < 0x80000 ? 5 : 10);
    camera_step_eye(c);
  }
  if (c->step == 0) {
    if (camera_motion_step(c, 2) != 0) ++c->step;
    camera_lookat_velocity(c, c->remaining / 0x180);
  } else {
    std::memcpy(v.cam_lookat, c->lookat_dest, 12);
  }
  camera_step_lookat(c);
}

// 0x80033660 camera_update_orbit_unit (mode 12).
void camera_update_orbit_unit(Camera* c) {
  ViewState& v = view();
  camera_set_lookat_dest(c, c->target->pos);
  std::memcpy(v.cam_lookat, c->lookat_dest, 12);
  if (c->step == 0) {
    camera_step_eye(c);
    if (camera_motion_step(c, 0) != 0) ++c->step;
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
    case 2: camera_update_follow(this, 0x23, g.party_units[g.controlled_slot]); break;
    case 3:
    case 11: {
      // camera_update_move_idle 0x80033348: step eye and look-at; mode 0 on arrival.
      camera_step_eye(this);
      camera_step_lookat(this);
      if (camera_motion_step(this, 0) != 0) mode = 0;
      break;
    }
    case 4:
    case 14: camera_update_track_all(this); break;
    case 5:
    case 7: camera_update_follow(this, 0xD, target); break;
    case 6:
    case 10: {
      // camera_update_mode6 0x800335d0 / camera_update_mode10 0x80033618.
      camera_step_eye(this);
      camera_step_lookat(this);
      if (camera_motion_step(this, 0) != 0) mode = 0;
      break;
    }
    case 12: camera_update_orbit_unit(this); break;
    case 15: camera_update_behind_target(this); break;
    default: break;
  }
  ticks += g.frame_ticks;
}

int camera_set_mode(Camera* c, int mode, BObj* target) {
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
    case 2: camera_setup_follow_leader(c); break;
    case 3: camera_setup_party_view(c); break;
    case 4: camera_setup_field_view(c); break;
    case 5:
    case 7:
    case 15: camera_setup_follow_target(c); break;
    case 6: camera_setup_closeup(c); break;
    case 11: camera_setup_overview(c, 0x50, 0); break;
    case 14: camera_setup_track_all(c); break;
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

int camera_request_mode(Camera* c, int mode, BObj* target) {
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


void camera_setup_follow_target(Camera* c) {
  view().view_mode = 1;
  const BObj* t = c->target;
  c->lookat_dest[0] = t->pos[0] + c->lookat_offset[0];
  c->lookat_dest[1] = t->pos[1] + c->lookat_offset[1];
  c->lookat_dest[2] = t->pos[2] + c->lookat_offset[2];
  c->focus = nullptr;
  c->step = 0;
  c->framing = 0;
  u32 k = 0x32;
  do {
    const s32 d = camera_lookat_velocity_by_distance(c, static_cast<s32>(k << 8));
    camera_set_motion(c, d / static_cast<s32>(k), 0x100, 0, 0, 0x10000);
    if (0xB < d / static_cast<s32>(k)) return;
    k /= 2;
  } while (9 < k);
}

void camera_setup_overview(Camera* c, s32 ticks, int all_units) {
  const s32 sx = camera_units_center_x(c, all_units != 0 ? 4 : 0, all_units != 0 ? 0xC : 4);
  const s32 p[3] = {c->centre_x, 0, static_cast<s32>(0xFFC18000)};
  const s32 sz = camera_units_center_z(c, 0, 0xC);
  const s32 wz = (sz >> 12) + 0x80;
  s32 w = sx >> 12;
  if (sx >> 12 < wz) w = wz;
  w = (w << 11) / 1000;
  if (w < 0x800) w = 0x800;
  camera_set_eye_orbit(c, p, -0x400, 0xE3, w);
  c->lookat_dest[1] = static_cast<s32>(0xFFFA6000);
  c->lookat_dest[2] = static_cast<s32>(0xFFC18000);
  c->lookat_dest[0] = c->eye_dest[0];
  camera_set_motion(c, ticks, 0x40, 10, 0, 0x10000);
  camera_eye_velocity(c, ticks);
  camera_lookat_velocity(c, ticks);
  c->mode = 0xB;
  c->step = 0;
}

void camera_set_lookat_offset(Camera* c, const s32 ofs[3]) {
  if ((c->lookat_offset[0] != ofs[0] || c->lookat_offset[1] != ofs[1] || c->lookat_offset[2] != ofs[2]) &&
      c->mode == 7) {
    c->lookat_offset[0] = ofs[0];
    c->lookat_offset[1] = ofs[1];
    c->lookat_offset[2] = ofs[2];
    camera_setup_follow_target(c);
  }
}

void camera_move_to(Camera* c, const s32 centre[3], s32 yaw, s32 pitch, s32 dist, s32 ticks, s32 speed, s32 accel) {
  ViewState& v = view();
  v.view_mode = 1;
  camera_set_eye_orbit(c, centre, yaw, pitch, dist);
  camera_set_lookat_dest(c, centre);
  if (ticks == 0) {
    std::memcpy(v.cam_eye, c->eye_dest, 12);
    std::memcpy(v.cam_lookat, c->lookat_dest, 12);
    c->mode = 0;
  } else {
    camera_request_mode(c, 10, nullptr);
    c->step = 0;
    camera_set_motion(c, ticks, speed, accel, 0, 0x10000);
    camera_eye_velocity(c, ticks);
    camera_lookat_velocity(c, ticks);
  }
}

void camera_orbit_unit(Camera* c, Unit* u, s32 yaw, s32 pitch, s32 dist, s32 ticks, s32 speed, s32 accel) {
  ViewState& v = view();
  v.view_mode = 1;
  c->target = u;
  camera_set_eye_orbit(c, u->pos, yaw, pitch, dist);
  camera_set_lookat_dest(c, u->pos);
  camera_request_mode(c, 0xC, nullptr);
  c->step = 0;
  if (ticks == 0) {
    std::memcpy(v.cam_eye, c->eye_dest, 12);
    std::memcpy(v.cam_lookat, c->lookat_dest, 12);
  } else {
    camera_set_motion(c, ticks, speed, accel, 0, 0x10000);
    camera_eye_velocity(c, ticks);
    camera_lookat_velocity(c, ticks);
  }
}

void camera_set_focus_unit(Camera* c, BObj* who, BObj* focus) {
  if (who == c->target) {
    c->focus = focus;
    c->look_straight = 0;
  }
}

void camera_set_lookat_unit(Camera* c, BObj* who, BObj* focus) {
  if (who == c->target) {
    c->focus = focus;
    c->look_straight = 1;
  }
}

void camera_set_focus_timer(Camera* c, BObj* focus, s16 ticks) {
  if (c->focus == focus) c->focus_timer = ticks;
}

void camera_set_hold(Camera* c, s16 ticks) { c->hold = ticks; }

void camera_set_zoom(Camera* c, s32 zoom, s32 speed) {
  c->zoom_target = zoom;
  c->zoom_speed = speed;
}

void camera_schedule_end(int mode, Unit* u, s16 ticks) {
  BattleGlobals& g = bg();
  if (g.camera_end_mode == 0 || (g.camera != nullptr && mode == g.camera->mode)) {
    g.camera_end_mode = static_cast<s16>(mode);
    g.camera_end_timer = ticks;
    g.camera_end_unit = u;
  }
}

void camera_end_scheduled() {
  BattleGlobals& g = bg();
  if (g.state < 3) {
    camera_end_mode(g.camera, g.camera_end_mode, 0, g.camera_end_unit);
    g.camera_end_mode = 0;
  }
}

const s32 kZeroVector[4] = {0, 0, 0, 0};

}  // namespace ss::game::battle
