// The spiders' enemy-code module: block 7605d41faf78 (1708 bytes, slot A linked at 0x80088110),
// vtable 0x80088694 (the factory stores it over the EnemyUnit one; no constructor), monster
// types 0x23 Arcmene, 0x24 Scewer, 0x25 Darthwidow, 0x8f Ladyquimira (0x26c-byte objects),
// encounters 50-52 116 117 123 ... (13). A double-hit bite, a web shot, and a blink: it
// fades out in a burst of green sparks and fades in again at a random free spot.

#include "game/battle/bsfx.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/particle.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/enemy_support.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

const s32 kZero[3] = {0, 0, 0};  // g_zero_vector 0x8006e33c

struct EnemyArcmene : EnemyUnit {
  // 0x80088110 [26]: 1/8 of the time the blink (attack 3) when the nearest member is within
  // 1000 units, else the web from afar (attack 2) or the default choice.
  void choose_target() override {
    bool blink = false;
    // ORIGINAL BUG (original_bugs.md E1): the member's index (enemy_nearest_party_index) is
    // compared with the distance 0x3e8000, so the test always passes:
    //   if (rng15() < 0x1000) blink = enemy_nearest_party_index(this) < 0x3E8000;
    if (rng15() < 0x1000) {
      const int i = enemy_nearest_party_index(this);
      blink = i >= 0 && vec_distance_fx(pos, bg().party_units[static_cast<size_t>(i)]->pos) < 0x3E8000;
    }
    if (blink) {
      bsfx_play_at(0x21, this);
      action_kind = 3;
      begin_action();
      return;
    }
    if (enemy_try_approach(this, 800, 0x2000, 2, 0x280) == 0) EnemyUnit::choose_target();
  }

  // 0x800881cc [27]: the bite: the melee step, with the landed flag cleared once past
  // frame 2 so it can hit twice.
  void attack1() override {
    anim = 4;
    enemy_attack_step(this);
    if (counters[1] != 0 || frame <= 2) return;
    hit_landed = 0;
    counters[1] = 1;
  }

  // 0x80088228 [28]: one homing web shot (anim 8, speed 0x1e, 30 ticks) past frame 3.
  void attack2() override {
    anim = 5;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame <= 3) return;
    bsfx_play_at(0x1F, this);
    ShotDesc* d = shot_desc_init(8);
    d->speed = 0x1E;
    d->life = 0x1E;
    d->flags = 0x100006;
    d->target = target;
    const s32 from[3] = {bobj_front_x(this, 0x20), pos[1] - 0x64000, pos[2]};
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }

  // 0x80088360 [29]: the blink (+0x178 step, +0x174 grey level): anim 6, then 32 green
  // sparks and a fade to black (4 a tick); at 0 it moves to a random spot of the arena
  // (up to 200 tries for one where nothing blocks it), fades back in on frame 0 of anim 7,
  // and plays anim 7 once.
  void attack3() override {
    BattleGlobals& g = bg();
    switch (counters[1]) {
      case 0:
        anim = 6;
        if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) != -1) return;
        counters[1] = 1;
        counters[0] = 0x80;
        loop_limit = 0;
        animate(static_cast<u32>(attack_recovery));
        for (int i = 0; i < 0x20; ++i) {
          const Vec4 v = vec_random_around(kZero, 8);
          const s32 from[4] = {pos[0], pos[1] + size[1] * -0x4000, pos[2], 0};
          particle_spawn_spark_line(from, v.data(), 0x204020, 0xD0FFD0, 0x30, 0x5A);
        }
        bsfx_play_at(0x29, this);
        return;
      case 1: {
        colour_mode = 2;
        rgb[0] = static_cast<u8>(counters[0]);
        animate(static_cast<u32>(attack_recovery));
        if (g.flags != 0) return;
        counters[0] -= g.frame_ticks * 4;
        if (counters[0] >= 0) return;
        counters[0] = 0;
        counters[1] = 2;
        bobj_anim_reset(this);
        const StageData& sd = stage();
        for (int tries = 0; tries < 200; ++tries) {
          pos[0] = rng_between(sd.bounds[0], sd.bounds[2]);
          pos[2] = rng_between(sd.bounds[1], sd.bounds[3]);
          pos[1] = bobj_ground_height(this);
          if (unit_check_move(this, kZero, 7) == nullptr) return;
        }
        return;
      }
      case 2:
        anim = 7;
        colour_mode = 2;
        rgb[0] = static_cast<u8>(counters[0]);
        animate(1);  // (held on its first frame while it fades in)
        if (g.flags != 0) return;
        counters[0] += g.frame_ticks * 4;
        if (counters[0] < 0x80) return;
        rgb[0] = 0x80;
        colour_mode = 0;
        counters[1] = 3;
        return;
      case 3:
        loop_limit = 1;
        if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) != -1) return;
        action_kind = 0;
        begin_action();
        return;
      default:
        return;
    }
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_arcmene_create() { return std::make_unique<EnemyArcmene>(); }

}  // namespace ss::game::battle
