// Succubus's enemy-code module: block af0f75db5522 (1960 bytes, slot A at 0x80088110-0x800888b8),
// vtable 0x80088790, ctor 0x80088110 (0x26c-byte objects); monster types 0xac Succubus, 0xad
// Nightmare, 0xae D-feather (encounters 197 266 267 503). The dive (attack 2: up for 16
// ticks, then down at where the target will be, bouncing off what it hits) and a homing
// ball (attack 3).

#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/unit_move.h"

namespace ss::game::battle {
namespace {

struct EnemySuccubus : EnemyUnit {
  // 0x80088110: enemy_create, the module's vtable, hit sounds 0x4c / 0x4d.
  EnemySuccubus() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4D;
  }

  // 0x80088158 [26]: the dive from afar, the ball from further; else the usual choice.
  void choose_target() override {
    if (enemy_try_approach(this, 400, 0x2000, 2, 600) != 0) return;
    if (enemy_try_approach(this, 600, 12000, 3, 0x200) != 0) return;
    EnemyUnit::choose_target();
  }

  // 0x800881c4 [28]: the dive, by +0x174: 0 rises 16 a tick (anim 6, airborne, +0x121) for
  // 16 ticks (+0x178), then aims at the target's middle 24 ticks ahead of its motion (no
  // higher than y -0x15e) at 32 a tick (straight down at 2 without a target); 1 dives (anim
  // 5) until a wall, the ground, the animation's end or a hit (once per landed flag), then
  // springs back (vel -8 / -16 away from its facing, anim 0xb); 2 flies back with gravity
  // until it meets a wall or lands on a unit, or reaches the ground.
  void attack2() override {
    BattleGlobals& g = bg();
    if (counters[0] == 1) {
      bool done = false;
      if (g.flags == 0) {
        if (unit_try_move_scaled(this, vel, 7) != kWallBlock) {
          bobj_move(this, vel);
        } else {
          done = true;
        }
        anim = 5;
        loop_limit = 1;
        const s32 ground = bobj_ground_height(this);
        if (ground < pos[1]) {
          pos[1] = ground;
          done = true;
        }
      }
      if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) done = true;
      if (g.flags == 0) {
        Unit* units[8] = {};
        Vec4 points[8];
        const int n = collider_attack_overlaps(this, units, points, 1);
        if (n != 0 && hit_landed == 0) {
          unit_attack_targets(this, n, units, points, 0x100, 9999, 0);
          done = true;
        }
      }
      if (!done) return;
      ++counters[0];
      vel[0] = 0 < facing[0] ? -0x8000 : 0x8000;
      vel[1] = -0x10000;
      bobj_anim_reset(this);
      anim = 0xB;
      animate(static_cast<u32>(attack_recovery));
      return;
    }
    if (counters[0] == 0) {
      airborne = 1;
      jumping = 1;
      anim = 6;
      animate(static_cast<u32>(attack_recovery));
      if (g.flags != 0) return;
      vel[0] = 0;
      vel[1] = -0x10000;
      vel[2] = 0;
      loop_limit = 0;
      bobj_move(this, vel);
      counters[1] += g.frame_ticks;
      if (counters[1] < 0x11) return;
      counters[1] = 0;
      ++counters[0];
      if (target == nullptr) {
        vel[1] = 0x20000;
      } else {
        s32 aim[3] = {target->pos[0] + target->vel[0] * 0x18,
                      target->pos[1] + target->size[1] * -0x2000 + target->vel[1] * 0x18,
                      target->pos[2] + target->vel[2] * 0x18};
        if (aim[1] < -0x15E000) aim[1] = -0x15E000;
        const Vec4 v = vec_direction(pos, aim);
        vel[0] = v.x << 5;
        vel[1] = v.y << 5;
        vel[2] = v.z << 5;
        extra34 = v.w;
      }
      bobj_anim_reset(this);
      return;
    }
    if (counters[0] != 2) return;
    Unit* b = unit_try_move_scaled(this, vel, 7);
    if (b == kWallBlock || (b != nullptr && 0 < vel[1])) {
      // (Airborne stays set on this way out.)
      action_kind = 0;
    } else {
      loop_limit = 0;
      vel[1] += g.frame_ticks * 0x1000;
      animate(static_cast<u32>(attack_recovery));
      if (g.flags != 0) return;
      bobj_move(this, vel);
      if (pos[1] <= bobj_ground_height(this)) return;
      action_kind = 0;
      airborne = 0;
    }
    jumping = 0;
    begin_action();
  }

  // 0x80088640 [29]: one homing ball (hit anim 9) at once.
  void attack3() override {
    anim = 6;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0) return;
    ShotDesc* d = shot_desc_init(7);
    d->speed = 0x18;
    d->life = 0x1E;
    d->flags = 0x100006;
    d->hit_effect = 9;
    d->target = target;
    const s32 from[3] = {bobj_front_x(this, 0x20), pos[1] - 0x64000, pos[2]};
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_succubus_create() { return std::make_unique<EnemySuccubus>(); }

}  // namespace ss::game::battle
