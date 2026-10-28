// The rabbits' enemy-code module: block 49e1e6da065c (1816 bytes, slot B linked at 0x80089f18),
// vtable 0x8008a508 (the factory stores it over the EnemyUnit one; no constructor), monster
// types 0x0a Vopalbunny, 0x0b Killerrabi, 0x0c Saberbunny, 0x80 Punkponk (0x26c-byte
// objects), encounters 14 15 85 86 95 96 ... (21). They hop toward their target instead of
// walking (squashing on each landing, the Punkponk higher and hurting whoever it lands
// on), and spit from afar.

#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/unit_move.h"

namespace ss::game::battle {
namespace {

struct EnemyBunny : EnemyUnit {
  // 0x80089f18 [23]: the approach as hops (+0x121 in the air). On the ground: next to the
  // approach point (within one step of run speed in x and z) the attack starts (slot 12);
  // otherwise, once the landing squash (+0x78 vertical scale, see pre_think) is over, it
  // stretches (0x1400), takes the walking velocity from unit_move_toward_goal (no move;
  // no reachable point: back to idle) and jumps (y -0x10, the Punkponk -0x18). In the air:
  // x / z and y move separately where free (a blocked y ends the jump), gravity 0x800 a
  // tick (Punkponk 0x1000); on landing a squash (0xc00), and the attack when it stands
  // still. The Punkponk's hops hit (0x100, angle 9999). Near the point the horizontal
  // velocity stops. Anim 1 frame 0 rising, frame 1 falling (not animated).
  void state_approach() override {
    BattleGlobals& g = bg();
    const s16 type = static_cast<s16>(st()->id);
    if (g.flags == 0 && hop(g, type)) return;
    anim = 1;
    frame = static_cast<s8>(vel[1] >= 0 ? 1 : 0);
    animate(1);
  }
  // The approach's movement; true when it handed over to slot 12 (nothing more this frame).
  bool hop(BattleGlobals& g, s16 type) {
    // (When no point is found the original reads its stack slot uninitialised; 0 here.)
    Vec4 goal;
    unit_find_approach_point(this, &goal);
    const s32 step = run_speed * g.frame_ticks;
    const s32 dx = goal.x - pos[0];
    const s32 dz = goal.z - pos[2];
    const bool near = (dx < 0 ? -dx : dx) <= step && (dz < 0 ? -dz : dz) <= step;
    if (jumping == 0) {
      if (near) {
        begin_action();
        return true;
      }
      if (scale_y != 0x1000) return false;
      vel[1] = 0;
      scale_y = 0x1400;
      if (unit_move_toward_goal(this, 0) == 2) {
        action_kind = 0;
        begin_action();
        return true;
      }
      vel[1] = type == 0x80 ? -0x18000 : -0x10000;
      hit_landed = 0;
      jumping = 1;
      airborne = 1;
      return false;
    }
    const s32 dxz[3] = {vel[0] * g.frame_ticks, 0, vel[2] * g.frame_ticks};
    if (unit_check_move(this, dxz, 7) == nullptr) {
      const s32 v[3] = {vel[0], 0, vel[2]};
      bobj_move(this, v);
    }
    const s32 dy[3] = {0, vel[1] * g.frame_ticks, 0};
    if (unit_check_move(this, dy, 7) == nullptr) {
      const s32 v[3] = {0, vel[1], 0};
      bobj_move(this, v);
    } else {
      jumping = 0;
    }
    vel[1] += type == 0x80 ? g.frame_ticks << 12 : g.frame_ticks << 11;
    const s32 ground = bobj_ground_height(this);
    if (ground <= pos[1]) {
      pos[1] = ground;
      jumping = 0;
      airborne = 0;
      scale_y = 0xC00;
      if (vel[0] == 0 && vel[2] == 0) {
        begin_action();
        return true;
      }
    }
    if (type == 0x80) {
      Unit* units[8] = {};
      Vec4 points[8];
      const int n = collider_attack_overlaps(this, units, points, 1);
      if (n != 0 && hit_landed == 0) unit_attack_targets(this, n, units, points, 0x100, 9999, 0);
    }
    if (near) {
      vel[0] = 0;
      vel[2] = 0;
    }
    return false;
  }

  // 0x8008a2f8 [32]: the squash / stretch (+0x78) eases back to 0x1000 by 0x80 a tick.
  int pre_think() override {
    const s32 s = scale_y;
    if (s > 0x1000) {
      scale_y = s - bg().frame_ticks * 0x80;
      if (scale_y < 0x1000) scale_y = 0x1000;
    } else if (s < 0x1000) {
      scale_y = s + bg().frame_ticks * 0x80;
      if (scale_y > 0x1000) scale_y = 0x1000;
    }
    return 0;
  }

  // 0x8008a374 [26]: the spit (attack 2) from 1000 away, else the default choice.
  void choose_target() override {
    if (enemy_try_approach(this, 1000, 0x2000, 2, 0x200) == 0) EnemyUnit::choose_target();
  }

  // 0x8008a3c0 [28]: one homing spit (anim 6, speed 0x14, 30 ticks) on the first frame,
  // from 0xb4 up (no sound).
  void attack2() override {
    anim = 5;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0) return;
    ShotDesc* d = shot_desc_init(6);
    d->speed = 0x14;
    d->life = 0x1E;
    d->flags = 0x100006;
    d->target = target;
    const s32 from[3] = {bobj_front_x(this, 0x20), pos[1] - 0xB4000, pos[2]};
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_bunny_create() { return std::make_unique<EnemyBunny>(); }

}  // namespace ss::game::battle
