// The Gargoyle module: block be7557dae510 (2016 bytes, slot A linked at 0x80088110), vtable
// 0x800887c8 (the factory stores it over the EnemyUnit one: no constructor), monster types
// 0x75 Gargoyle, 0x76 Stonestatue, 0xa6 Lesserdevil (0x26c-byte objects; encounters 386 391
// 518 519 523). Near: a 4-loop claw flurry whose hits freeze the victims; farther: it rises
// 16 ticks, dives at where the target will be and bounces back off whatever it meets.

#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"

namespace ss::game::battle {
namespace {

struct EnemyGargoyle : EnemyUnit {
  // 0x80088110 [26]: close (within 120 units, or 15000 / 32768) the flurry (2, reach 8);
  // within 200 (or a half) the dive (3, reach 600); else the base choice.
  void choose_target() override {
    if (enemy_try_approach(this, 0x78, 15000, 2, 8) != 0) return;
    if (enemy_try_approach(this, 200, 0x4000, 3, 600) != 0) return;
    EnemyUnit::choose_target();
  }

  // The attack boxes against the party; damage once per landed flag. Returns the hits (0 when
  // nothing was hit or the flag was already set).
  int hit_party(Unit* units[8]) {
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n == 0 || hit_landed != 0) return 0;
    unit_attack_targets(this, n, units, points, 0x100, 9999, 0);
    return n;
  }

  // 0x8008817c [28]: anim 5 four times; the landed flag clears while no attack box is on, so
  // every swing can hit; each victim is frozen 16 ticks (+0x154).
  void attack2() override {
    anim = 5;
    loop_limit = 4;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if ((box_mask & 0xC) == 0) hit_landed = 0;
    if (bg().flags != 0) return;
    Unit* units[8] = {};
    const int n = hit_party(units);
    for (int i = 0; i < n; ++i) units[i]->super_armour = 0x10;
  }

  // 0x800882e4 [29]: the dive, step +0x174: 0 rise (anim 6) 1 unit a tick for more than 16
  // ticks (+0x178), then aim 28 ticks ahead of the target's motion at its centre, speed 0x18
  // (straight down 0x18 without a target); 1 dive (anim 7, 3 loops) until a wall, the ground,
  // the animation's end or a hit, then bounce back (x 8, up 0x14; z keeps the dive's); 2 fall
  // under gravity until the ground (or a wall / a unit while falling) ends the attack.
  void attack3() override {
    BattleGlobals& g = bg();
    const s32 step = counters[0];
    if (step == 1) {
      bool stop = false;
      if (g.flags == 0) {
        // (Blocked by a unit it still moves; only the arena edge / an obstacle stops it.)
        if (unit_try_move_scaled(this, vel, 7) == kWallBlock) {
          stop = true;
        } else {
          bobj_move(this, vel);
        }
        anim = 7;
        loop_limit = 3;
        const s32 ground = bobj_ground_height(this);
        if (ground < pos[1]) {
          pos[1] = ground;
          stop = true;
        }
      }
      if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) stop = true;
      if (g.flags == 0) {
        Unit* units[8] = {};
        if (hit_party(units) != 0) stop = true;
      }
      if (!stop) return;
      ++counters[0];
      vel[0] = facing[0] > 0 ? -0x8000 : 0x8000;
      vel[1] = -0x14000;
      bobj_anim_reset(this);
      anim = 6;
      animate(static_cast<u32>(attack_recovery));
    } else if (step == 0) {
      airborne = 1;
      jumping = 1;
      anim = 6;
      animate(static_cast<u32>(attack_recovery));
      if (g.flags != 0) return;
      vel[0] = 0;
      vel[1] = -0x10000;
      vel[2] = 0;
      loop_limit = 0;
      if (unit_try_move_scaled(this, vel, 7) == nullptr) bobj_move(this, vel);
      counters[1] += g.frame_ticks;
      if (counters[1] <= 0x10) return;
      counters[1] = 0;
      ++counters[0];
      if (target == nullptr) {
        vel[1] = 0x18000;
      } else {
        const Unit* t = target;
        s32 aim[3];
        aim[0] = t->pos[0];
        aim[1] = t->pos[1] - (t->size[1] << 13);
        aim[2] = t->pos[2];
        aim[0] += t->vel[0] * 0x1C;
        aim[1] += t->vel[1] * 0x1C;
        aim[2] += t->vel[2] * 0x1C;
        const Vec4 dir = vec_direction(pos, aim);
        vel[0] = dir.x;
        vel[1] = dir.y;
        vel[2] = dir.z;
        extra34 = dir.w;
        vel[0] *= 0x18;
        vel[1] *= 0x18;
        vel[2] *= 0x18;
      }
      bobj_anim_reset(this);
    } else if (step == 2) {
      Unit* b = unit_try_move_scaled(this, vel, 7);
      if (b == kWallBlock || (b != nullptr && 0 < vel[1])) {
        action_kind = 0;
        jumping = 0;
        begin_action();
        return;
      }
      loop_limit = 0;
      vel[1] += g.frame_ticks * 0x1000;
      animate(static_cast<u32>(attack_recovery));
      if (g.flags != 0) return;
      if (unit_try_move_scaled(this, vel, 7) == nullptr) bobj_move(this, vel);
      if (pos[1] <= bobj_ground_height(this)) return;
      // (Below the ground it ends without snapping back to it.)
      action_kind = 0;
      airborne = 0;
      jumping = 0;
      begin_action();
    }
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_gargoyle_create() { return std::make_unique<EnemyGargoyle>(); }

}  // namespace ss::game::battle
