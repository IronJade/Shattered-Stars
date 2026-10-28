// The Ericodus module: block 066fb2afff60 (3172 bytes, slot A linked at 0x80088110), vtable
// 0x80088c4c, monster types 0x38 Ericodus, 0x39 Takicodos, 0x94 Clubgunner (0x26c-byte
// objects; 24 encounters, 170-177 ...). A club fighter: a swing with a ground ring, a
// sparking two-state combo (attacks 2 / 3), a rock throw (only with a non-default animation
// table) and a leap to a random free spot of the arena.

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/particle.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

const s32 kZero[3] = {0, 0, 0};  // g_zero_vector 0x8006e33c

// Ahead of the unit by 'dist' (20.12): + when it faces right (facing x > 0), else - (so a
// zero facing counts as left, unlike bobj_facing_fx).
s32 ahead(const BObj* o, s32 dist) { return o->facing[0] > 0 ? dist : -dist; }

struct EnemyEricodus : EnemyUnit {
  // 0x80088110: enemy_create, the module's vtable, hit sounds 0x4c / 0x4e.
  EnemyEricodus() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4E;
  }

  // 0x80088158 [26]: far: the combo (3); near: attack 2; with a non-default animation table
  // the throw (4); else an eighth of the time the leap (5) to a random spot of the arena at
  // least 300 units away that it can move to (200 tries), or the base choice.
  void choose_target() override {
    if (enemy_try_approach(this, 700, 0x1000, 3, 0x140) != 0) return;
    if (enemy_try_approach(this, 400, 0x2000, 2, 0x100) != 0) return;
    if (animset->layout != model_layout && enemy_try_approach(this, 600, 0x2000, 4, 0x300) != 0) return;
    if (rng15() < 0x2000) {
      const StageData& sd = stage();
      for (int i = 0; i < 200; ++i) {
        walk_dest[0] = rng_between(sd.bounds[0], sd.bounds[2]);
        walk_dest[2] = rng_between(sd.bounds[1], sd.bounds[3]);
        walk_dest[1] = stage_ground_height(walk_dest[0] >> 12, walk_dest[2] >> 12);
        if (vec_distance_fx(pos, walk_dest) <= 0x12BFFF) continue;
        const s32 d[3] = {walk_dest[0] - pos[0], walk_dest[1] - pos[1], walk_dest[2] - pos[2]};
        if (unit_check_move(this, d, 7) == nullptr) {
          action_kind = 5;
          begin_action();
          return;
        }
        counters[1] = 0;
      }
    }
    EnemyUnit::choose_target();
  }

  // 0x80088314 [27]: the base swing; past frame 1 once, an orange ring on the ground 200
  // units in front.
  void attack1() override {
    EnemyUnit::attack1();
    if (counters[1] != 0 || frame < 2) return;
    counters[1] = 1;
    const s32 x = bobj_front_x(this, 200);
    const s32 at[4] = {x, bobj_ground_height(this), pos[2], 0};
    particle_spawn_ring(at, 0xC, 0x30, 0x10, 0x2D, 0, 0xFF8000, 0x10, 0);
  }

  // 0x800883cc [28]: attacks 2 and 3 (state 10: anim 5, one loop; state 11: anim 6, two
  // loops, triple depth). Every even frame a hit can land again, with a sound and 4 sparks
  // at the club; a landed hit leaves debris (state 11: 8 of CLUT 10, else 6 of CLUT 11).
  void attack2() override {
    BattleGlobals& g = bg();
    loop_limit = static_cast<u8>(action - 9);
    anim = static_cast<u16>(action - 5);
    if (g.flags == 0 && (static_cast<u8>(frame) & 1) == 0) {
      bsfx_play_at(0x25, this);
      hit_landed = 0;
      for (int i = 0; i < 4; ++i) {
        const Vec4 v = vec_random_around(kZero, 8);
        const s32 at[4] = {pos[0] + ahead(this, 0xA0000), pos[1] - 0x80000, pos[2], 0};
        particle_spawn_spark_line(at, v.data(), 0x101020, 0x80C0FF, 8, 0x5A);
      }
    }
    const s16 saved_z = size[2];
    if (action == 11) size[2] = static_cast<s16>(saved_z * 3);
    const s16 r = static_cast<s16>(animate(static_cast<u32>(attack_recovery)));
    size[2] = saved_z;
    if (r == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (g.flags != 0) return;
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n == 0 || hit_landed != 0) return;
    unit_attack_targets(this, n, units, points, 0x100, 9999, 0);
    for (int i = 0; i < n; ++i)
      effect_debris_burst(points[i].data(), kZero, action == 11 ? 8 : 6, 1, 1, 0x1000, action == 11 ? 10 : 11);
  }

  // 0x80088670 [29]: the combo's second part: its banner once, then slot 28.
  void attack3() override {
    if (counters[1] == 0) {
      counters[1] = 1;
      battle_message_show(0x2713);
    }
    attack2();
  }

  // 0x800886cc [30]: a homing rock (projectile anim 0xf, power 0x200, 6 hit sprites) past
  // frame 2.
  void attack4() override {
    anim = 7;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame < 3) return;
    bsfx_play_at(0x1F, this);
    bsfx_play_at(0x34, this);
    ShotDesc* d = shot_desc_init(0xF);
    d->speed = 0x18;
    d->life = 0x28;
    d->flags = 0x100006;
    d->sfx = 0x50;
    d->trail_bits[1] = 5;
    d->power = 0x200;
    d->hit_bits[1] = 3;
    d->hits = 6;
    d->hit_bits[2] = 0x2000;
    d->trail_bits[0] = 2;
    d->trail_bits[2] = 0;
    d->trail_bits[3] = 1;
    d->hit_effect = 2;
    d->hit_bits[0] = 1;
    d->airborne = 0xC;
    d->target = target;
    const s32 from[3] = {pos[0] + ahead(this, 0x40000), pos[1] - 0x28000, pos[2]};
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }

  // The landing (and take-off) dust: a grey ring and 12 puffs.
  void dust(s32 speed) {
    particle_spawn_ring(pos, 0x10, 0x40, 0x12, 0x3C, 0x202020, 0xFFFFFF, 0x20, 0);
    for (int i = 0; i < 12; ++i) effect_dust_puff(pos, speed);
  }

  // 0x80088874 [31]: the leap to walk_dest (chosen by slot 26): crouch (anim 0xb), jump
  // (0xc rising, 0xd falling) under gravity 1 a tick, land (0xe). +0x178 is the step.
  void attack5() override {
    BattleGlobals& g = bg();
    const s32 step = counters[1];
    if (step == 0) {
      anim = 0xB;
      if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) != -1) return;
      dust(2);
      loop_limit = 0;
      animate(static_cast<u32>(attack_recovery));
      bsfx_play_at(0x26, this);
      // Launch speed from half the distance: (d / 2, toward zero; +15 when negative) >> 4.
      s32 h = vec_distance_fx(walk_dest, pos) / 2;
      if (h < 0) h += 0xF;
      const s32 vy = -(h >> 4);
      const Vec4 dir = vec_direction_xz(pos, walk_dest);
      vel[0] = dir.x;
      vel[1] = dir.y;
      vel[2] = dir.z;
      extra34 = dir.w;
      airborne = 1;
      jumping = 1;
      vel[1] = vy;
      vel[0] <<= 4;
      ++counters[1];
      vel[2] <<= 4;
    } else if (step == 1) {
      anim = vel[1] < 0 ? 0xC : 0xD;
      loop_limit = 0;
      animate(static_cast<u32>(attack_recovery));
      if (g.flags != 0) return;
      s32 d[3] = {vel[0], 0, vel[2]};
      if (unit_try_move_scaled(this, d, 7) == nullptr) bobj_move(this, d);
      d[0] = 0;
      d[1] = vel[1];
      d[2] = 0;
      if (unit_try_move_scaled(this, d, 7) == nullptr) {
        bobj_move(this, d);
      } else {
        // Blocked vertically: the jump ends (step 2).
        // ORIGINAL BUG (original_bugs.md E2): both this and the landing below add 1, so a
        // block and a landing in the same frame make step 3, which no branch handles: the
        // monster stays in its attack state for good.
        //   ++counters[1];
        counters[1] = 2;
        bsfx_play_at(0x25, this);
      }
      vel[1] += g.frame_ticks * 0x1000;
      if (bobj_ground_height(this) < pos[1]) {
        pos[1] = bobj_ground_height(this);
        // ORIGINAL BUG (original_bugs.md E2): ++counters[1];
        counters[1] = 2;
        bsfx_play_at(0x25, this);
        dust(4);
        airborne = 0;
      }
    } else if (step == 2) {
      anim = 0xE;
      jumping = 0;
      loop_limit = 1;
      if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
        action_kind = 0;
        begin_action();
      }
    }
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_ericodus_create() { return std::make_unique<EnemyEricodus>(); }

}  // namespace ss::game::battle
