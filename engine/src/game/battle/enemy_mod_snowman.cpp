// The snowman's enemy-code module: block 831e90e7722d (1072 bytes, slot A linked at
// 0x80088110), vtable 0x80088418 (the factory stores it over the EnemyUnit one; no
// constructor), monster type 0x66 Snowman (0x26c-byte objects), encounter 543. A two-armed
// flail that knocks its victims off at an angle, alternating sides with the frame, and a
// snowball (anim 7) from afar.

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"

namespace ss::game::battle {
namespace {

struct EnemySnowman : EnemyUnit {
  // 0x80088110 [26]: from afar the snowball (attack 3), close up the flail (2), else the
  // default choice.
  void choose_target() override {
    if (enemy_try_approach(this, 0x200, 0x2000, 3, 0x200) != 0) return;
    if (enemy_try_approach(this, 0x100, 0x2000, 2, 0x60) != 0) return;
    EnemyUnit::choose_target();
  }

  // 0x8008817c [28]: the flail (anim 5): hits knock along angle 0x955 (back and 30 degrees
  // aside) or -0x155 (30 degrees the other way): on even frames 0x955 when it faces left,
  // on odd frames when it faces right.
  void attack2() override {
    anim = 5;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0) return;
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n == 0 || hit_landed != 0) return;
    s32 angle = facing[0] < 0 ? 0x955 : -0x155;
    if ((frame & 1) != 0) angle = facing[0] < 0 ? -0x155 : 0x955;
    unit_attack_targets(this, n, units, points, 0x100, angle, 0);
  }

  // 0x800882b8 [29]: one homing snowball (anim 7, speed 0x18, 0x30 ticks) past frame 2,
  // from 0xc0 up.
  void attack3() override {
    anim = 6;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame <= 2) return;
    bsfx_play_at(0x1F, this);
    ShotDesc* d = shot_desc_init(7);
    d->speed = 0x18;
    d->life = 0x30;
    d->flags = 0x100006;
    d->target = target;
    const s32 from[3] = {bobj_front_x(this, 9), pos[1] - 0xC0000, pos[2]};
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_snowman_create() { return std::make_unique<EnemySnowman>(); }

}  // namespace ss::game::battle
