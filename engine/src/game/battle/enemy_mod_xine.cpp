// The XINE module: block 8ac9dffedd2a (2940 bytes, slot A linked at 0x80088110), vtable
// 0x80088b64, monster types 0x22 XINE, 0x8c Hell-servant, 0x8d Weirdavia, 0x8e Phoenix
// (0x26c-byte objects; 9 encounters, 186 187 199 342 361 465 ...). A bird: from afar a
// flaming dash at the target (attack 2, double ATK, each victim hit once), near a double-ATK
// peck (attack 3); feathers fall from its wing beats; it dies in a burst of red streaks.

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

const s32 kZero[3] = {0, 0, 0};  // g_zero_vector 0x8006e33c

// (v * speed * 2) >> 12 on the low 32 bits of the product (mult / mflo, sll 1, sra 12).
s32 scale_speed(s32 v, s32 speed) {
  return static_cast<s32>(static_cast<u32>(v) * static_cast<u32>(speed) * 2u) >> 12;
}

struct EnemyXine : EnemyUnit {
  // 0x80088110: enemy_create, the module's vtable, a double-size shadow (+0x152 = 0x200),
  // hit sounds 0x4c / 0x4d.
  EnemyXine() {
    shadow_scale2 = 0x200;
    sfx_hit = 0x4C;
    sfx_crit = 0x4D;
  }

  // +0x17c holds the dash's last victim (a Unit pointer); it is zero (null) or that unit.
  Unit* last_victim_ = nullptr;
  Unit* last_victim() const { return counters[2] != 0 ? last_victim_ : nullptr; }
  void set_last_victim(Unit* u) {
    last_victim_ = u;
    counters[2] = u != nullptr ? 1 : 0;
  }

  // 0x80088160 [32]: while approaching, on frame 3 of the wing beat (and not disabled), 1-3
  // feathers (anim 8) fall round it on the ground, drifting outwards.
  int pre_think() override {
    if (action != 1 || bg().flags != 0 || frame != 3 || unit_is_disabled(this) == 1) return 0;
    const s32 n = rng_between(1, 3);
    for (s32 i = 0; i < n; ++i) {
      Sprite* sp = unit_play_anim(this, 8);
      if (sp == nullptr) continue;
      const u32 a = rng_range(0x1000);
      s32 at[3] = {0, 0, 0};
      at[0] = pos[0] + bcos(a) * 0x20;
      at[1] = bobj_ground_height(this);
      at[2] = pos[2] + bsin(a) * 0x20;
      sprite_set_pos_v(sp, at);
      sp->vel[0] = bcos(a) << 3;
      sp->vel[2] = bsin(a) << 3;
      sprite_set_fade(sp, 0x80, -4, 0x1E);
      sp->scale_vel[0] = 0x80;
      sp->scale_vel[1] = 0x80;
    }
    return 0;
  }

  // 0x800882d8 [26]: from 1000 units (or 12000 / 32768) the dash (attack 2, speed 0x300,
  // its banner 10000, all scratch words cleared); else the base choice, whose attack-1
  // approach becomes the peck (attack 3, reach 0x20) an eighth of the time (reach 2
  // otherwise).
  void choose_target() override {
    if (enemy_try_approach(this, 1000, 12000, 2, 0x300) != 0) {
      counters[0] = 0;
      counters[1] = 0;
      counters[2] = 0;
      counters[3] = 0;
      battle_message_show(10000);
      return;
    }
    EnemyUnit::choose_target();
    if (action == 1 && action_kind == 1 && rng15() < 0x2000) {
      reach = 0x20;
      action_kind = 3;
    } else {
      reach = 2;
    }
  }

  // The dash's per-victim hit (double ATK): each unit is hit once until the boxes leave it.
  void dash_hit() {
    Unit* units[8] = {};
    Vec4 points[8];
    Stats* s = st();
    const s16 atk = s->atk[2];
    s->atk[2] = static_cast<s16>(atk << 1);
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n == 0) {
      set_last_victim(nullptr);
    } else if (last_victim() != units[0]) {
      set_last_victim(units[0]);
      unit_attack_targets(this, 1, units, points, 0x100, 9999, 0);
      if (Sprite* sp = unit_play_anim(this, 9)) {
        sprite_set_pos_v(sp, points[0].data());
        sprite_set_fade(sp, 0x80, -4, 10);
      }
    }
    s->atk[2] = atk;
  }

  // 0x8008838c [28]: the dash. A flash and a rising whoosh at the beak once; shards there for
  // frames 0-1; from frame 3 it flies at the target at twice its run speed for the distance +
  // 192 units, trailing flames and dust, until the budget runs out, a wall stops it or it
  // dips below the ground. It animates and hits every frame except the one the dash ends.
  void attack2() override {
    BattleGlobals& g = bg();
    loop_limit = 0;
    anim = 5;
    if (g.flags == 0) {
      const s32 beak[4] = {pos[0] + (facing[0] > 0 ? 0x70000 : -0x70000), pos[1] - 0x138000, pos[2], 0};
      if (counters[0] == 0) {
        counters[0] = 1;
        if (Sprite* sp = unit_play_anim(this, 0xD)) {
          sprite_set_pos_v(sp, beak);
          sprite_set_fade(sp, 0x80, -4, 0x14);
          sp->scale_vel[0] = 0x100;
          sp->scale_vel[1] = 0x100;
        }
        const u32 h = bsfx_play_at(0x44, this);
        sound_handle_pitch_up(h, 0x3C);
        sound_handle_fade_out(h, 0x3C, 0);
      }
      if (frame < 2) {
        const s32 yaw = static_cast<s32>(rng_range(0x1000));
        const s32 pitch = rng_between(-0x400, 0x400);
        particle_spawn_shard(beak, yaw, pitch, 0x3C0, 0x16, -0x16, 0x1080FF, 0);
      }
      if (counters[1] == 0 && frame >= 3 && target != nullptr) {
        const Vec4 dir = vec_direction(pos, target->pos);
        vel[0] = dir.x;
        vel[1] = dir.y;
        vel[2] = dir.z;
        extra34 = dir.w;
        counters[1] = 1;
        vel[0] = scale_speed(vel[0], run_speed);
        vel[1] = scale_speed(vel[1], run_speed);
        vel[2] = scale_speed(vel[2], run_speed);
        counters[3] = vec_distance_fx(target->pos, pos) + 0xC0000;
        const s32 ground[3] = {pos[0], bobj_ground_height(this), pos[2]};
        effect_spawn_sparks_at(ground, 0xC, 0x40, 0xC0, 0x20, 0x3C, 0, 1, 0x30, 0);
        bsfx_play_at(0x21, this);
      }
      if (counters[1] != 0) {
        counters[3] -= vec_distance_fx(kZero, vel) * 2;
        if (unit_try_move_scaled(this, vel, 7) == kWallBlock || counters[3] < 0) {
          action_kind = 0;
          begin_action();
          return;
        }
        bobj_move(this, vel);
        const s32 at[3] = {pos[0], bobj_ground_height(this), pos[2]};
        if (Sprite* sp = unit_play_anim(this, 8)) {
          sprite_set_pos_v(sp, at);
          sprite_set_fade(sp, 0x80, -4, 0x14);
          sp->scale_vel[0] = 0x100;
          sp->scale_vel[1] = 0x100;
          sp->vel[0] = (-sp->vel[0]) / 4;
          sp->vel[2] = (-sp->vel[2]) / 4;
        }
        if (Sprite* sp = unit_play_anim(this, 0xC)) {
          sprite_set_pos_v(sp, at);
          sprite_set_fade(sp, 0x80, -2, 0);
        }
        if (bobj_ground_height(this) < pos[1]) {
          pos[1] = bobj_ground_height(this);
          action_kind = 0;
          begin_action();
          return;
        }
      }
    }
    animate(static_cast<u32>(attack_recovery));
    if (g.flags != 0) return;
    dash_hit();
  }

  // 0x8008890c [29]: the peck: enemy_attack_step (anim 6) with double ATK.
  void attack3() override {
    anim = 6;
    Stats* s = st();
    const s16 atk = s->atk[2];
    s->atk[2] = static_cast<s16>(atk << 1);
    enemy_attack_step(this);
    s->atk[2] = atk;
  }

  // 0x80088964 [25]: once: two spark rings on the ground, a red / green particle ring at
  // its centre and 48 red streaks flying out from its feet; then the usual fade.
  void state_dying() override {
    if (counters[0] == 0) {
      const s32 ground = bobj_ground_height(this);
      const s32 c[4] = {pos[0], ground, pos[2], 0};
      effect_spawn_sparks_at(c, 0x10, 0x80, 0xC0, 0x20, 0x50, 7, 0, 0x30, 0);
      effect_spawn_sparks_at(c, 0x10, 0x40, 0x80, 0x18, 0x5F, 9, 0, 0x20, 0);
      const s32 mid[4] = {pos[0], pos[1] + size[1] * -0x2000, pos[2], 0};
      particle_spawn_ring(mid, 0x10, 0x80, 0x30, 0x3C, 0xFF0000, 0xFF00, 0x40, 0);
      for (int i = 0; i < 0x30; ++i) {
        s32 to[4] = {0, 0, 0, 0};
        to[0] = c[0] + rng_between(-0x400000, 0x400000);
        to[1] = c[1] + rng_between(-0xC0000, 0xC0000);
        to[2] = c[2] + rng_between(-0x400000, 0x400000);
        particle_spawn_homing_line(c, to, 0, 0xFF4060, 0x8C, 0x14000, 0);
      }
      counters[0] = 1;
    }
    EnemyUnit::state_dying();
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_xine_create() { return std::make_unique<EnemyXine>(); }

}  // namespace ss::game::battle
