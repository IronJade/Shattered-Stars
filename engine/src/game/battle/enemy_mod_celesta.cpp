// G-Celesta's enemy-code module: block 3edfc7fc63f6 (3632 bytes, slot A linked at 0x80088110),
// vtable 0x80088e18, monster types 0xaf G-Celesta, 0xb2 Iselia-queen (0x26c-byte objects),
// encounters 515 561. A flying swordswoman with an 8-copy afterimage: attack 2 a 150-300 tick
// glide bouncing off walls that cuts whoever it meets (then attack 3, a cut), attack 4 a cut
// that sends a chain of ground waves at the target, attack 5 one that sends a homing blade;
// knocked back, she throws two sprays of weapon pieces. Her skill 0x27 level is 30.

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The homing blade's afterimage lives in the Shot's +0x134; the port keeps p134 as the
// "bound" flag (shot_reset clears it) and the pointer here.
std::unordered_map<const Shot*, Afterimage*>& blade_trails() {
  static std::unordered_map<const Shot*, Afterimage*> trails;
  return trails;
}
void blade_trail_end(const Shot* s) {
  if (s->p134 == 0) return;
  if (Afterimage* a = blade_trails()[s]) a->ending = 1;
}

void shot_wave(Shot* s);
void shot_blade(Shot* s);

struct EnemyCelesta : EnemyUnit {
  // 0x80088110: enemy_create and the module's vtable.

  // 0x800888e4 [5]: knocked back, she throws weapon pieces twice (variants 0 and 2).
  int start_reaction() override {
    if (reaction == 2) {
      unit_throw_weapons(this, 0);
      unit_throw_weapons(this, 2);
    }
    return unit_start_reaction(this);
  }

  // 0x80088ddc [9]: chants spell 0x27 at level 30.
  int skill_level(int skill) override { return skill == 0x27 ? 0x1E : 0; }

  // 0x80088148 [14]: word 0: bit 24 of the mask itself (not a status of hers); else 0.
  int status_test(int word, u32 mask) override { return word == 0 ? static_cast<int>((mask >> 24) & 1) : 0; }

  // 0x80088160 [26]: far off the wave cut (attack 4) or the blade (5), started at once;
  // nearer the glide (2) in a random direction at twice her run speed for 150-300 ticks;
  // else the usual choice.
  void choose_target() override {
    if (enemy_try_approach(this, 900, 0x2000, 4, 300) != 0) {
      action_kind = 4;
      begin_action();
      return;
    }
    if (enemy_try_approach(this, 600, 0x2000, 5, 300) != 0) {
      action_kind = 5;
      begin_action();
      return;
    }
    if (enemy_try_approach(this, 400, 0x2000, 2, 600) == 0) {
      EnemyUnit::choose_target();
      return;
    }
    action_kind = 2;
    begin_action();
    const u32 a = rng_range(0x1000);
    vel[1] = 0;
    vel[0] = ((bcos(a) * run_speed) >> 12) << 1;
    vel[2] = ((bsin(a) * run_speed) >> 12) << 1;
    counters[1] = rng_between(0x96, 300);
  }

  // 0x800882b0 [28]: the glide (anim 5, held 8): bounces off walls and obstacles, hurting
  // whoever it touches (once per new victim, power 0x100); attack 3 when the time (+0x178)
  // is up and the spot is free.
  void attack2() override {
    BattleGlobals& g = bg();
    loop_limit = 0;
    anim = 5;
    special_timer = 8;
    counters[1] -= g.frame_ticks;
    s32 d[3] = {0, 0, 0};
    animate(0);
    if (counters[1] < 1 && unit_check_move(this, d, 7) == nullptr) {
      action_kind = 3;
      begin_action();
      return;
    }
    if (g.flags != 0) return;
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, 8, 0x602010, 1);
    d[0] = vel[0] * g.frame_ticks;
    if (unit_check_move(this, d, 7) == kWallBlock) vel[0] = -vel[0];
    d[0] = 0;
    d[2] = vel[2] * g.frame_ticks;
    if (unit_check_move(this, d, 7) == kWallBlock) vel[2] = -vel[2];
    bobj_move(this, vel);
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    // (+0x174 holds the last victim; slot 12 clears it when the attack starts.)
    const Unit* last = counters[0] != 0 ? last_victim_ : nullptr;
    if (n == 0) {
      counters[0] = 0;
    } else if (last != units[0]) {
      counters[0] = 1;
      last_victim_ = units[0];
      unit_attack_targets(this, 1, units, points, 0x100, 9999, 0);
    }
  }

  // The afterimage ends with the attack.
  void end_attack() {
    if (effects[0] != nullptr) effects[0]->ending = 1;
    effects[0] = nullptr;
    action_kind = 0;
    begin_action();
  }

  // 0x80088504 [29]: the cut after the glide (anim 8, one loop; no hit check of its own).
  void attack3() override {
    anim = 8;
    loop_limit = 1;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) end_attack();
  }

  // 0x800885a0 [30] / 0x80088744 [31]: a cut (anim 4) with a growing (4) / growing and
  // spinning (5) afterimage; once an attack box is on, the projectile at the target.
  void throw_cut(u32 trail_rgb, u8 kind, ShotFn fn, s32 speed) {
    anim = 4;
    loop_limit = 1;
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, 8, trail_rgb, 1);
    if (effects[0] != nullptr) effects[0]->next_kind = kind;
    if (counters[1] == 0 && (box_mask & 0xC) != 0 && target != nullptr) {
      if (Shot* s = shot_spawn_callback(fn)) {
        bobj_set_pos(s, pos);
        const Vec4 v = vec_velocity_toward_xz(pos, target->pos, speed);
        s->vel[0] = v.x;
        s->vel[1] = v.y;
        s->vel[2] = v.z;
        s->owner = this;
        s->layer = 1;
        s->target = target;
        bobj_share_sprite(this, s);
        bsfx_play_at(0x34, s);
      }
      counters[1] = 1;
    }
    loop_limit = 1;
    if (static_cast<s16>(animate(0)) == -1) end_attack();
  }
  void attack4() override { throw_cut(0x602010, 1, shot_wave, 0x18); }
  void attack5() override { throw_cut(0x204080, 3, shot_blade, 0x10); }

  Unit* last_victim_ = nullptr;  // +0x174 (valid while counters[0] is set)

  // 0x80088df0 [36]: the dtor (EnemyUnit vtable, unit_destroy).
};

// 0x80088934: attack 4's ground wave (anim 7, one loop, layer 2, the owner's depth): after 4
// ticks it spawns the next wave 4 steps ahead (passing on its +0x130, fade, power and landed
// flags); outside the arena it starts to fade; until it has landed, the owner's melee hit
// (power 0x100) on the party (the result is not kept). Dies with its animation.
void shot_wave(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 7;
  s->loop_limit = 1;
  s->layer = 2;
  s->size[2] = s->owner->size[2];
  if (static_cast<s16>(shot_draw_with_shadow(s, 4, 0xFFFFFFFF)) == -1) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  shot_fade_out(s);  // (its result is not looked at: a Shot that died here runs on)
  bool to_ground = false;
  if (s->hit_landed == 0) {
    if (s->p12c != 0) {
      to_ground = true;
    } else if (3 < s->p128 && s->p134 == 0) {
      s->p134 = 1;
      if (Shot* n = shot_spawn_callback(shot_wave)) {
        n->pos[0] = s->pos[0] + s->vel[0] * 4;
        n->pos[1] = s->pos[1];
        n->pos[2] = s->pos[2] + s->vel[2] * 4;
        n->vel[0] = s->vel[0];
        n->vel[1] = s->vel[1];
        n->vel[2] = s->vel[2];
        n->p130 = s->p130;
        n->p12c = s->p12c;
        n->power = s->power;
        n->owner = s->owner;
        n->hit_landed = s->hit_landed;
        bobj_share_sprite(s, n);
      }
    }
  }
  if (!to_ground && s->p12c == 0) {
    if (bobj_outside_arena(s)) {
      s->p12c = 1;
      return;
    }
    s->p128 += g.frame_ticks;
  }
  s->pos[1] = bobj_ground_height(s);
  if (s->hit_landed == 0) shot_hit_melee(s, 0x100, 1);
}

// 0x80088b20: attack 5's homing blade (anim 6 looping, layer 2, the owner's depth, an
// 8-copy afterimage 0x208040): accelerates toward its target 1 a tick on each axis for 720
// ticks (then fades); its first melee hit (power 0x180) throws two debris bursts and ends it.
void shot_blade(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 6;
  s->loop_limit = 0;
  s->layer = 2;
  s->size[2] = s->owner->size[2];
  if (static_cast<s16>(shot_draw_with_shadow(s, 4, 0xFFFFFFFF)) != -1) {
    if (g.flags != 0) return;
    if (s->p134 == 0) {
      blade_trails()[s] = afterimage_create(s, 8, 0x208040, 1);
      s->p134 = 1;
    }
    bobj_move(s, s->vel);
    if (shot_fade_out(s) != 0) {
      blade_trail_end(s);
      return;
    }
    if (s->p12c == 0) {
      s->p128 += g.frame_ticks;
      if (0x2D0 < s->p128) s->p12c = 1;
    }
    const s32 step = g.frame_ticks << 12;
    s->vel[0] += s->pos[0] < s->target->pos[0] ? step : -step;
    s->vel[1] += s->pos[1] < s->target->pos[1] ? step : -step;
    s->vel[2] += s->pos[2] < s->target->pos[2] ? step : -step;
    if (s->hit_landed != 0) return;
    if (shot_hit_melee(s, 0x180, 1) == 0) return;
    s32 scale = 0x2000;
    for (int i = 0; i < 2; ++i, scale += 0x1000) {
      const s32 spread[3] = {0x80000, 0x80000, 0x40000};
      effect_debris_burst(s->pos, spread, 3, 2, 1, scale, 4);
    }
  }
  blade_trail_end(s);
  shot_kill(s);
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_celesta_create() { return std::make_unique<EnemyCelesta>(); }

}  // namespace ss::game::battle
