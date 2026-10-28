// Miel's enemy-code module: block 97f8935752a2 (1876 bytes, slot C at 0x8008b1f0-0x8008b944),
// vtable 0x8008b81c, ctor 0x8008b1f0 (0x26c-byte objects); monster types 0x47 Miel32, 0x48
// Miel64, 0x49 Miel128, 0x95 Liveflayer, 0x96 Mindflayer (encounters 176 177 180 181 184
// 185 ...). Spits one homing ball (the flayers a spread of 7), a close swipe with hit
// flashes and a fan of three bursting balls; Mindflayer's hits drain MP.

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"

namespace ss::game::battle {
namespace {

struct EnemyMiel : EnemyUnit {
  // 0x8008b1f0: enemy_create, the module's vtable, hit sounds 0x4c / 0x4e.
  EnemyMiel() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4E;
  }

  // 0x8008b238 [26]: the Mindflayer's hits take MP (+0x134). While its animation table is
  // not the model's own the close swipe (attack 2) and the fan (attack 3) come first; then
  // the spit (attack 1).
  void choose_target() override {
    if (static_cast<s16>(st()->id) == 0x96) hits_mp = 1;
    if (animset->layout != model_layout) {
      if (enemy_try_approach(this, 0xF0, 0x2000, 2, 0xF0) != 0) return;
      if (enemy_try_approach(this, 500, 0x2000, 3, 0x280) != 0) return;
    }
    enemy_try_approach(this, 300, 24000, 1, 0x140);
  }

  // 0x8008b2f0 [27]: past frame 0, homing balls with a pink trail at the target (types
  // above 0x94: 7, all but the first turned by up to +-0x31c).
  void attack1() override {
    anim = 4;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame < 1) return;
    const int count = static_cast<s16>(st()->id) < 0x95 ? 1 : 7;
    bsfx_play_at(0x1F, this);
    for (int i = 0; i < count; ++i) {
      ShotDesc* d = shot_desc_init(7);
      d->trail_rgb = 0xC08080;
      d->speed = 0x18;
      d->life = 0x28;
      d->flags = 0x100006;
      d->target = target;
      d->angle = i == 0 ? 0 : rng_between(-0x31C, 0x31C);
      const s32 from[3] = {bobj_front_x(this, 0x40), pos[1] - 0x50000, pos[2]};
      if (Shot* s = shot_spawn_projectile(this, from, d, this)) {
        s->size[0] = 0x10;
        s->size[1] = 8;
      }
      counters[1] = 1;
    }
  }

  // 0x8008b4b0 [28]: the swipe (anim 6): a hit flash (6 sparks) at every point hit.
  void attack2() override {
    anim = 6;
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
    unit_attack_targets(this, n, units, points, 0x100, 9999, 0);
    for (int i = 0; i < n; ++i) effect_hit_flash(points[i].data(), 6);
  }

  // 0x8008b5f4 [29]: past frame 0, a fan of three balls (-30, 0, +30 degrees) with impact
  // bursts, one descriptor rewritten for each.
  void attack3() override {
    anim = 5;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame < 1) return;
    bsfx_play_at(0x1F, this);
    bsfx_play_at(0x34, this);
    ShotDesc* d = shot_desc_init(8);
    s32 deg = -0x1E000;  // degrees << 12
    for (int i = 0; i < 3; ++i) {
      d->speed = 0x18;
      d->angle = deg / 0x168;
      d->life = 0x28;
      d->sfx = 0x50;
      d->power = 0x100;
      d->hit_bits[1] = 3;
      d->hits = 6;
      d->hit_bits[2] = 0x2000;
      d->flags = 0x100006;
      d->trail_bits[0] = 1;
      d->trail_bits[1] = 2;
      d->trail_bits[2] = 0;
      d->trail_bits[3] = 1;
      d->hit_effect = 2;
      d->hit_bits[0] = 1;
      d->airborne = 0xC;
      d->target = target;
      const s32 from[3] = {bobj_front_x(this, 0x40), pos[1] - 0x58000, pos[2]};
      shot_spawn_projectile(this, from, d, this);
      deg += 0x1E000;
    }
    counters[1] = 1;
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_miel_create() { return std::make_unique<EnemyMiel>(); }

}  // namespace ss::game::battle
