// The Azamgil module: block 817b29806e3f (728 bytes, slot B linked at 0x80089f18), vtable
// 0x8008a0c8 (the factory stores it over the EnemyUnit one: no constructor), monster types
// 0x69 Azamgil, 0x6b Atulatul (0x26c-byte objects; encounters 148 149 150 151 195 206). The
// base melee, and from afar (or now and then) one homing spit.

#include "game/battle/bsfx.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"

namespace ss::game::battle {
namespace {

struct EnemyAzamgil : EnemyUnit {
  // 0x80089f18 [26]: the spit (2) from 440 units (or 12000 / 32768, speed 0x1b8), else the
  // base choice.
  void choose_target() override {
    if (enemy_try_approach(this, 0x1B8, 12000, 2, 0x1B8) == 0) EnemyUnit::choose_target();
  }

  // 0x80089f64 [28]: anim 5; once past frame 1 (sfx 0x1f) a homing projectile (anim 6,
  // speed 0x1a, life 0x38) from 0x20 in front, 0xb8 up.
  void attack2() override {
    anim = 5;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame < 2) return;
    bsfx_play_at(0x1F, this);
    ShotDesc* d = shot_desc_init(6);
    d->speed = 0x1A;
    d->life = 0x38;
    d->flags = 0x100006;
    d->target = target;
    const s32 from[3] = {bobj_front_x(this, 0x20), pos[1] - 0xB8000, pos[2]};
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_azamgil_create() { return std::make_unique<EnemyAzamgil>(); }

}  // namespace ss::game::battle
