#include "game/battle/unit.h"

#include <cstring>

#include "game/battle/render.h"
#include "game/battle/stage.h"
#include "game/boot/rng.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

Unit g_wall_marker;  // what unit_check_move's -1 (wall / obstacle) is in the port

}  // namespace

Unit* const kWallBlock = &g_wall_marker;

UnitGlobals& unit_globals() {
  static UnitGlobals g;
  return g;
}

s32* Collider::get_box(int code) {
  const int box = code & 0xF;
  const bool attack = (code & 0xF0) != 0;
  const bool max = (code & 0xF00) != 0;
  if (!attack) return max ? body_max[box & 1] : body_min[box & 1];
  return max ? attack_max[box & 1] : attack_min[box & 1];
}

Unit::Unit() {
  // unit_create 0x8004dd30: the Collider boxes are zeroed and the fields set as above.
}

bool unit_is_down(const Unit* u) {
  if (u == nullptr) return true;
  return (u->st()->status & 7) != 0 || u->removed != 0;
}

void unit_apply_global_blend(Unit* u) {
  const UnitGlobals& ug = unit_globals();
  if (ug.blend_mode == 2) {
    u->colour_mode = 0;
  } else if (ug.blend_mode == 1) {
    u->colour_mode = 2;
    u->rgb[0] = ug.blend_alpha;
  }
}

Unit* unit_check_move(const Unit* u, const s32 d[3], u32 flags) {
  const StageData& s = stage();
  const s32 nz = u->pos[2] + d[2];
  const s32 nx = u->pos[0] + d[0];
  if ((flags & 8) == 0) {
    if (nx + u->size[0] * -0x2000 < s.bounds[0] + 0x4000) return kWallBlock;
    if (s.bounds[2] - 0x4000 <= nx + u->size[0] * 0x2000) return kWallBlock;
    if (nz < s.bounds[1]) return kWallBlock;
    if (s.bounds[3] <= nz) return kWallBlock;
  }
  s32 dy = 0;
  if (u->airborne == 0 && (d[0] != 0 || d[1] != 0 || d[2] != 0)) {
    dy = stage_ground_height(nx >> 12, nz >> 12) - u->pos[1];
    if (dy > 0) dy = 0;
  }
  const s32 a0[3] = {u->pos[0] + u->size[0] * -0x2000 + d[0], u->pos[1] + u->size[1] * -0x4000 + dy + d[1],
                     u->pos[2] + u->size[2] * -0x2000 + d[2]};
  const s32 a1[3] = {u->pos[0] + u->size[0] * 0x2000 + d[0], u->pos[1] + dy + d[1],
                     u->pos[2] + u->size[2] * 0x2000 + d[2]};
  auto overlap = [](s32 b0, s32 b1, s32 x0, s32 x1) {
    return (b0 <= x0 && x0 < b1) || (b0 < x1 && x1 < b1) || (x0 <= b0 && b1 <= x1);
  };
  const BattleGlobals& g = bg();
  const int n = (flags & 4) != 0 ? 0x24 : 0xC;
  for (int i = 0; i < n; ++i) {
    s32 b0[3], b1[3];
    Unit* hit;
    if (i < 12) {
      const Unit* other = i < 4 ? static_cast<const Unit*>(g.party_units[i]) : g.enemy_units[i - 4];
      if (i < 4 ? (flags & 1) == 0 : (flags & 2) == 0) continue;
      if (other == u || unit_is_down(other)) continue;
      b0[0] = other->pos[0] + other->size[0] * -0x2000;
      b1[0] = other->pos[0] + other->size[0] * 0x2000;
      b0[2] = other->pos[2] + other->size[2] * -0x2000;
      b1[2] = other->pos[2] + other->size[2] * 0x2000;
      b0[1] = other->pos[1] + other->size[1] * -0x4000;
      b1[1] = other->pos[1];
      hit = const_cast<Unit*>(other);
    } else {
      const auto& o = s.obstacles[i - 12];
      if (o.kind == 0) continue;
      for (int k = 0; k < 3; ++k) b0[k] = o.min[k], b1[k] = o.max[k];
      hit = kWallBlock;
    }
    if (overlap(b0[0], b1[0], a0[0], a1[0]) && overlap(b0[2], b1[2], a0[2], a1[2])) {
      if ((b0[1] <= a0[1] && a0[1] < b1[1]) || (b0[1] < a1[1] && a1[1] < b1[1]) || (a0[1] <= b0[1] && b1[1] <= a1[1]))
        return hit;
    }
  }
  return nullptr;
}

Unit* unit_try_move_scaled(const Unit* u, const s32 v[3], u32 flags) {
  const s32 t = bg().frame_ticks;
  const s32 d[3] = {v[0] * t, v[1] * t, v[2] * t};
  return unit_check_move(u, d, flags);
}

s32 unit_ground_follow(Unit* u) {
  const BattleGlobals& g = bg();
  const s32 ground = bobj_ground_height(u);
  if (g.flags == 0) {
    bool falling = u->airborne != 0;
    if (!falling) {
      if (u->flying == 0) u->pos[1] = ground;
      falling = u->airborne != 0;
    }
    if (falling && (((static_cast<u32>(u->command) | static_cast<u32>(u->jumping) << 8 | static_cast<u32>(u->flying) << 16) &
                     0xFFFF00) == 0 ||
                    unit_is_down(u))) {
      // Falling: gravity half a unit per tick, at most 0x40000 per tick.
      s32 v[3] = {0, u->vel[1] == 0 ? 0x1000 : u->vel[1], 0};
      if (unit_try_move_scaled(u, v, 7) == nullptr) {
        bobj_move(u, v);
        u->vel[1] += (g.frame_ticks / 2) * 0x1000;
        if (u->vel[1] > 0x40000) u->vel[1] = 0x40000;
        if (u->pos[1] < ground) {
          if (u->vel[1] > 0) u->airborne = 1;
        } else {
          u->pos[1] = ground;
          u->airborne = 0;
          u->jumping = 0;
          u->vel[1] = 0;
        }
      } else {
        u->vel[1] = 0;
        u->airborne = 2;
        u->jumping = 0;
      }
    } else {
      if (u->flying == 0 && u->pos[1] != ground) u->airborne = 1;
      if (u->flying != 0 && u->action == 0) {
        // Hovering toward the hover height at a quarter of the run speed (at least 0x8000).
        s32 step = u->run_speed < 0x8000 ? 0x8000 : u->run_speed;
        step *= g.frame_ticks;
        if (step < 0) step += 3;
        bool past;
        if (u->pos[1] < u->hover_height) {
          u->pos[1] += step >> 2;
          past = u->hover_height < u->pos[1];
        } else {
          u->pos[1] -= step >> 2;
          past = u->pos[1] < u->hover_height;
        }
        if (past) {
          u->pos[1] = u->hover_height;
          u->airborne = 0;
        }
      }
    }
  }
  s32 d = ground - u->pos[1];
  if (d < 0) d += 0x7FFF;
  return d >> 15;
}

void unit_draw_model(Unit* u) {
  const s32 x = u->pos[0], y = u->pos[1];
  if (u->shake_frames != 0) {
    u->shake_frames = static_cast<s16>(u->shake_frames - bg().frame_ticks);
    if (u->shake_frames < 0) u->shake_frames = 0;
    u->pos[0] += u->shake_amp * 0x1000;
    u->shake_amp = static_cast<s16>(-u->shake_amp);
  }
  // TODO(port): g_stage_height_fn (stage code blocks) lifts grounded units.
  bobj_project(u);
  u->pos[1] = y;
  u->pos[0] = x;
}

void unit_update_base(Unit* u) {
  const BattleGlobals& g = bg();
  if (g.flags != 0) return;
  if (u->recovery_gauge != 0) {
    u->recovery_gauge = static_cast<s16>(u->recovery_gauge - g.frame_ticks);
    if (u->recovery_gauge < 0) u->recovery_gauge = 0;
  }
  if (u->special_timer != 0) {
    u->special_timer = static_cast<s16>(u->special_timer - g.frame_ticks);
    if (u->special_timer < 0) u->special_timer = 0;
  }
  // TODO(port): footprint sprites on stages 6 and 0xc (sprite_spawn_c).
}

void unit_draw_shadow(Unit* u, s32 height) {
  s32 a = u->ext_x1 < 0 ? -u->ext_x1 : u->ext_x1;
  const s32 b = u->ext_x0 < 0 ? -u->ext_x0 : u->ext_x0;
  if (a < b) a = b;
  s32 r = a * 4 + 0x20;
  const s32 k = u->size[0];
  if (r < k * 4 + 0x20) r = k * 4 + 0x20;
  if (r < 0x40) r = 0x40;
  const s32 k3 = k * 3;
  if (k3 * 2 < r) r = k3 * 2;
  r -= height;
  if (r < 4) r = 4;
  if (u->action == 1 && k3 < r) r = k3;
  s32 t = r * u->shadow_scale;
  r = (t < 0 ? t + 0xFF : t) >> 8;
  t = r * u->shadow_scale2;
  r = (t < 0 ? t + 0xFF : t) >> 8;
  bobj_draw_shadow(u, -r, r, -r, r, 0xFFFFFFFFu);
}

// ---- EnemyUnit ----------------------------------------------------------------------

EnemyUnit::EnemyUnit() {
  is_enemy = 1;
  sfx_hit = 0x4A;
  sfx_crit = 0x48;
  sfx_guard = 0x4F;
}

void EnemyUnit::update() {
  if (removed != 0) return;
  BattleGlobals& g = bg();
  pool = g.prim_pool_a;
  // TODO(port): drop the camera focus (+0x132) when the camera's target is another unit.
  if (ignore_units != 0) {
    const s32 zero[3] = {0, 0, 0};
    if (unit_check_move(this, zero, 7) == nullptr) ignore_units = 0;
  }
  unit_apply_global_blend(this);
  enemy_think(this);
  unit_update_base(this);
}

int EnemyUnit::animate(u32 anim_flags) {
  const s32 shadow = unit_ground_follow(this);
  const s32 y = pos[1];
  pos[1] += bsin(static_cast<u32>((hover_phase & 0x1F) << 7)) * hover_amp;
  if (bg().flags == 0) hover_phase = static_cast<s16>(hover_phase + bg().frame_ticks);
  unit_draw_model(this);
  pos[1] = y;
  const int f = static_cast<s16>(bobj_anim_draw(this, anim_flags | (bg().flags & 0xFE)));
  if (f != -1) unit_draw_shadow(this, shadow);
  return f;
}

void enemy_think(EnemyUnit* e) {
  BattleGlobals& g = bg();
  if (g.effects_suppressed != 0) return;
  // TODO(port): the intro voice (+0x242, voice_play_entry).
  if (e->st()->id == 0x94 && e->animset) e->animset->layout = e->model_layout + 0x44;
  if (e->st()->id == 0xCF && e->animset) e->animset->layout = e->model_layout + 0xC0;
  if (g.state < kStateFighting) {
    e->anim = 0;
    e->animate(0);
    return;
  }
  // TODO(port): the fighting states (AI, approach, attacks, reactions, dying). Until then
  // the monster idles.
  runtime::Runtime::get().log_once("battle-enemy-ai", "battle: monster AI not ported yet (monsters idle)");
  e->anim = 0;
  e->animate(0);
}

int enemy_count_active(const std::array<EnemyUnit*, 8>& units) {
  int n = 0;
  for (const EnemyUnit* u : units) {
    if (!unit_is_down(u) && u->removed == 0 && static_cast<u8>(u->action - 5) > 1) ++n;
  }
  return n;
}

}  // namespace ss::game::battle
