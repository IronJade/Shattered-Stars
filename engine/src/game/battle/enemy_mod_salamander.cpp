// The lizards' enemy-code module: block ad1679dd4ef9 (1328 bytes, slot A linked at 0x80088110),
// vtable 0x80088518 (the factory stores it over the EnemyUnit one; no constructor), monster
// types 0x71 Salamander, 0x72 Flarelizard, 0x73 Coldlizard, 0x74 Fenrilbeast,
// 0xa3 Meduslizzard, 0xa4 Orbiterbeast, 0xa5 Weirdbeast (0x26c-byte objects), encounters
// 97-100 108 109 ... (29). Besides the plain swing, a breath: for 100 ticks from frame 10 a
// piercing stream of projectiles (one every 2 ticks) of the type's element.

#include "game/battle/bsfx.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"

namespace ss::game::battle {
namespace {

// The breath's element by type (the switch's jump table 0x80088440 over types 0x71-0xa5):
// Salamander / Flarelizard / Meduslizzard 4, Coldlizard / Fenrilbeast 2, Orbiterbeast /
// Weirdbeast 0x100 (the low half of the descriptor's +0x70; other types keep 0).
s32 breath_element(int type) {
  switch (type) {
    case 0x71: case 0x72: case 0xA3: return 4;
    case 0x73: case 0x74: return 2;
    case 0xA4: case 0xA5: return 0x100;
    default: return -1;
  }
}

struct EnemySalamander : EnemyUnit {
  // 0x80088110 [26]: the breath (attack 2) from 0x1cc away (a new breath sound: +0x17c
  // cleared), else the default choice.
  void choose_target() override {
    if (enemy_try_approach(this, 0x1CC, 11000, 2, 0x1CC) != 0)
      counters[2] = 0;
    else
      EnemyUnit::choose_target();
  }

  // 0x80088164 [27]: the default swing (enemy_attack1).
  void attack1() override { EnemyUnit::attack1(); }

  // 0x80088184 [28]: the breath (anim 5 looping; +0x174 ticks, +0x178 the spawn clock,
  // +0x17c the breath sound's handle, sfx 0x2d started on frame 10). Each projectile
  // (anim 6, speed 0x12, 0x36 ticks, power 0x100, 6 hit sprites of anim 2, piercing) flies
  // from 0x100 in front toward a point 0x230 in front jittered in y (-8..+0x20) and z
  // (+-0x40). After 100 ticks back to idle; meanwhile the sound fades (and ducks while the
  // battle is paused).
  void attack2() override {
    BattleGlobals& g = bg();
    anim = 5;
    loop_limit = 0;
    animate(static_cast<u32>(attack_recovery));
    bobj_texcache_reset(this);
    if (g.flags == 0) {
      if (9 < frame) {
        if (counters[2] == 0) counters[2] = static_cast<s32>(bsfx_play_at(0x2D, this));
        counters[1] += g.frame_ticks;
        while (1 < counters[1]) {
          counters[1] -= 2;
          ShotDesc* d = shot_desc_init(6);
          const s32 from[3] = {bobj_front_x(this, 0x100), pos[1] - 0x40000, pos[2]};
          d->speed = 0x12;
          d->point[0] = bobj_front_x(this, 0x230);
          d->point[1] = from[1] + rng_between(-0x8000, 0x20000);
          d->point[2] = from[2] + rng_between(-0x40000, 0x40000);
          d->life = 0x36;
          set_high_half(d->element, 1);  // +0x72: pierces
          d->hit_bits[1] = 3;
          d->hit_bits[2] = 0x4000;
          d->flags = 6;
          d->power = 0x100;
          d->hit_effect = 2;
          d->hits = 6;
          d->hit_bits[0] = 1;
          const s32 element = breath_element(static_cast<s16>(st()->id));
          if (element >= 0) set_low_half(d->element, element);  // +0x70
          shot_spawn_projectile(this, from, d, this);
        }
      }
      counters[0] += g.frame_ticks;
      if (100 < counters[0]) {
        action_kind = 0;
        begin_action();
        return;
      }
    }
    if (counters[2] == 0) return;
    sound_handle_fade_out(static_cast<u32>(counters[2]), 0x3C, 8);
    if (g.flags != 0) sound_handle_duck(static_cast<u32>(counters[2]), 6);
  }

  // The descriptor's +0x70 word is written as two halfwords.
  static void set_low_half(s32& w, s32 v) {
    w = static_cast<s32>((static_cast<u32>(w) & 0xFFFF0000u) | (static_cast<u32>(v) & 0xFFFF));
  }
  static void set_high_half(s32& w, s32 v) {
    w = static_cast<s32>((static_cast<u32>(w) & 0xFFFF) | (static_cast<u32>(v) << 16));
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_salamander_create() { return std::make_unique<EnemySalamander>(); }

}  // namespace ss::game::battle
