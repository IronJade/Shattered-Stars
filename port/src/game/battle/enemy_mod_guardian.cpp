// The #2129 image's default enemy-code module in slot A (0x80088110-0x80089f18, vtable
// 0x80089df0): Guardian 0xbe, Geo-guardian 0xbf and Owlbear 0xc0 (0x270-byte objects;
// encounters 457 458 501 512 525 527 558 load no code and use it). A three-phase boss:
// phase 0 melee and two kinds of energy balls, phase 1 (after the double ball) stronger
// melee, phase 2 (below a third of its HP) it roots itself, beams and quakes.

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }

// The target's centre (its body's mid height).
Vec4 centre_of(const Unit* u) { return {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0}; }

// Debris sprites (anims 0xf-0x11 of the unit's set) scattered round a point, growing to
// a random size; 'fade' / 'delay' as the dying and quake bursts use them.
void scatter_debris(Unit* u, int count, const s32 at[3], s32 radius, bool disc, s32 lo, s32 hi, bool fade) {
  for (int i = 0; i < count; ++i) {
    const int anim = static_cast<int>(rng_range(3)) + 0xF;
    Sprite* sp = fade ? unit_spawn_sprite(u, anim) : unit_spawn_sprite_ex(u, anim, 1);
    if (sp == nullptr) continue;
    const s32 g[3] = {at[0], bobj_ground_height(u), at[2]};
    const Vec4 p = disc ? vec_random_in_disc_xz(g, radius) : vec_random_around(g, radius);
    sprite_set_pos_v(sp, p.data());
    const s32 sc = rng_between(lo, hi);
    sp->scale_y = sc;
    sp->scale_x = sc;
    if (fade) {
      sprite_set_fade(sp, 0x80, -4, 0x10);
      sp->size[2] = static_cast<s16>(rng_range(0xC));
    }
    sp->layer = 3;
  }
}

void shot_move(Shot* s);
void shot_hit(Shot* s, s32 power);
void shot_ball(Shot* s);
void shot_twin_ball(Shot* s);
void shot_beam(Shot* s);

struct EnemyGuardian : EnemyUnit {
  u8 phase = 0;  // +0x26c: 0 normal, 1 after the twin balls, 2 rooted (below 1/3 HP)

  // 0x80088110: enemy_create, the module's vtable, phase 0.

  // 0x8008814c [26]
  void choose_target() override {
    const Stats* s = st();
    if (phase == 2) {
      if (enemy_try_approach(this, 600, 0x2000, 2, 0x50) != 0) {
        action_kind = 2;
        battle_message_show(0x271A);
      } else if (enemy_try_approach(this, 600, 0x8000, 1, 0x50) != 0) {
        action_kind = 1;
      } else {
        return;
      }
      attack_now();
      return;
    }
    if (phase == 1) {
      if (s->hp < s->hp_max / 3) {
        action_kind = 5;
        battle_message_show(0x2719);
        phase = 2;
        attack_now();
        return;
      }
      if (blocked_in_front() && enemy_try_approach(this, 300, 28000, 2, 0x30) != 0) return;
    } else if (phase == 0) {
      if (s->hp < (s->hp_max << 1) / 3 && enemy_try_approach(this, 600, 0x8000, 5, 0xF0) != 0) {
        battle_message_show(0x2718);
        return;
      }
      if (blocked_in_front() && enemy_try_approach(this, 300, 28000, 2, 0x30) != 0) return;
      if (enemy_try_approach(this, 600, 0x2000, 4, 800) != 0) return;
      if (enemy_try_approach(this, 300, 0x2000, 3, 0x78) != 0) {
        battle_message_show(0x2717);
        return;
      }
    } else {
      return;
    }
    enemy_try_approach(this, 200, 0x8000, 1, 0x50);
  }
  // A unit (not a wall) 0xf0 units in front.
  bool blocked_in_front() {
    const s32 d[3] = {bobj_facing_fx(this, -0xF0), 0, 0};
    Unit* b = unit_check_move(this, d, 7);
    return b != nullptr && b != kWallBlock;
  }
  // The attack starts where it stands (the approach enemy_try_approach began is dropped).
  void attack_now() {
    counters[0] = counters[1] = counters[2] = 0;
    begin_action();
  }

  // 0x80088650: the melee resolution the attacks share: sparks and flash sprites at each
  // point hit (phase 1 adds element bit 4 to its weapon, kept until a hit lands).
  void melee_hit() {
    if (bg().flags != 0) return;
    Stats* s = st();
    const u16 saved = s->weapon_element;
    if (phase == 1) s->weapon_element = saved | 0x10;
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n == 0 || hit_landed != 0) return;
    const int dmg = unit_attack_targets(this, n, units, points, 0x100, 9999, 0);
    for (int i = 0; i < n; ++i) {
      const int flash = dmg == 0 ? 0x12 : (phase == 1 ? 7 : 0xC);
      if (Sprite* sp = unit_spawn_sprite(this, flash)) {
        sprite_set_pos_v(sp, points[i].data());
        sprite_set_fade(sp, 0x80, -4, 8);
        sp->layer = 3;
        sp->scale_vel[1] = 0x100;
        sp->scale_vel[0] = 0x100;
      }
      if (dmg != 0) {
        if (Sprite* sp = unit_spawn_sprite_ex(this, 0xF, 1)) {
          sprite_set_pos_v(sp, points[i].data());
          sp->layer = 3;
        }
      }
      for (int k = 0; k < 12; ++k) {
        static const s32 kZero[3] = {0, 0, 0};  // g_zero_vector
        const Vec4 v = vec_random_around(kZero, 0xC);
        particle_spawn_spark_line(points[i].data(), v.data(), 0x3080, 0x80C0FF, 8, 0x3C);
      }
    }
    s->weapon_element = saved;
  }
  // The landed flag resets while no attack box is on, so the next swing can hit again.
  void rearm_hit() {
    if (bg().flags == 0 && (box_mask & 0xC) == 0 && hit_landed != 0) hit_landed = 0;
  }

  // 0x8008840c [27]: a swing; rooted, a beam ball at the target.
  void attack1() override {
    loop_limit = 1;
    anim = 4;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (phase != 2) {
      melee_hit();
      return;
    }
    animset->layout = model_layout + 0xD8;
    if (frame <= 1 || counters[1] != 0 || target == nullptr) return;
    counters[1] = 1;
    bsfx_play_at(0x34, this);
    bsfx_play_at(0x3C, this);
    Shot* s = shot_spawn_callback(shot_beam);
    if (s == nullptr) return;
    shot_place_at_unit(s, this, 0x88, 0xE0, 0);
    const Vec4 aim = centre_of(target);
    const Vec4 v = vec_velocity_toward(s->pos, aim.data(), 0x28);
    std::memcpy(s->vel, &v, sizeof(s->vel));
    s->owner = this;
    s->target = target;
    bobj_share_sprite(this, s);
    particle_spawn_ring(s->pos, 0x10, 0x50, 5, 0x3C, 0x2080, 0x30FF, 0x20, 0x400);
    for (int i = 0; i < 12; ++i) {
      const Vec4 r = vec_random_around(s->pos, 800);
      effect_spawn_beam_at(s->pos, r.data(), 0xE, 0xC0, 0x10, 0x40, 0x400, 1);
    }
  }

  // 0x800888ac [28]: a swing; rooted, a sweeping beam (frames 4-8) then a quake.
  void attack2() override {
    BattleGlobals& g = bg();
    loop_limit = 1;
    anim = 5;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (phase != 2) {
      melee_hit();
      rearm_hit();
      return;
    }
    animset->layout = model_layout + 0xD8;
    if (static_cast<u32>(static_cast<u8>(frame)) - 4 < 5) {
      if (counters[1] == 0) {
        counters[1] = 1;
        bsfx_play_at(0x34, this);
        const u32 h = bsfx_play_at(0x45, this);
        sound_handle_fade_out(h, 10, 0x14);
        sound_handle_pitch_down(h, 0x28);
      }
      s32 at[3] = {pos[0] + bobj_facing_fx(this, 0x60), pos[1] - 0xC8000, pos[2]};
      s32 ang = counters[0];
      if (facing[0] < 0) ang += 0x800;
      const s32 dir[3] = {bcos(static_cast<u32>(ang)), 800, bsin(static_cast<u32>(ang))};
      at[0] += dir[0] * 0x20;
      at[2] += dir[2] * 0x88;
      draw_beam(at, dir, 0x1020, 0x30FF, 0x400, 8, 0, 3);
      if (g.flags == 0) counters[0] -= (g.frame_ticks * 0x28000) / 0x168;
      return;
    }
    if (counters[1] == 0) return;
    counters[1] = 0;
    collider_set_attack_box(this, 800, 0x100, 800);
    melee_hit();
    bsfx_play_at(0x50, this);
    sound_handle_fade_out(bsfx_play_at(0x2D, this), 0x3C, 0x14);
    bsfx_play_at(0x50, target);
    const Vec4 c = centre_of(this);
    effect_spawn_sparks_at(c.data(), 0x14, 0xC0, 0, 0x40, 0x3C, 0xE, 3, 0x100, 1);
    const s32 ground[3] = {pos[0], bobj_ground_height(this), pos[2]};
    effect_spawn_sparks_at(ground, 0x14, 0x80, 0x100, 0x18, 0x5A, 5, 1, 0x80, 1);
    scatter_debris(this, 0x20, pos, 0x400, true, 12000, 0x4000, true);
  }

  // 0x80088d00 [29]: a lunge with an afterimage and +10 knockback power.
  void attack3() override {
    Stats* s = st();
    const s16 saved = s->stat4e[2];
    s->stat4e[2] = static_cast<s16>(saved + 10);
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, 3, 0x3080, 1);
    if (effects[0] != nullptr) effects[0]->next_kind = 1;
    loop_limit = 1;
    anim = 6;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      s->stat4e[2] = saved;
      if (effects[0] != nullptr) effects[0]->ending = 1;
      effects[0] = nullptr;
      return;
    }
    melee_hit();
    rearm_hit();
    s->stat4e[2] = saved;
  }

  // Thrown back by the shot: the recoil slides it back to a stop.
  void recoil() {
    BattleGlobals& g = bg();
    if (unit_try_move_scaled(this, vel, 7) == nullptr) bobj_move(this, vel);
    if (vel[0] < 1) {
      vel[0] += g.frame_ticks * 0x800;
      if (0 < vel[0]) vel[0] = 0;
    } else {
      vel[0] -= g.frame_ticks * 0x800;
      if (vel[0] < 0) vel[0] = 0;
    }
  }
  // An energy ball's launch flash: 8 sparks round the ball.
  void launch_sparks(const Shot* s) {
    for (int k = 0; k < 8; ++k) {
      Sprite* sp = unit_spawn_sprite_ex(this, 0xE, 1);
      if (sp == nullptr) continue;
      const Vec4 r = vec_random_around(s->pos, 0x3C);
      sprite_set_pos_v(sp, r.data());
      const s32 sc = rng_between(0x1000, 0x1800);
      sp->scale_y = sc;
      sp->scale_x = sc;
      sp->layer = 3;
    }
  }

  // 0x80088e68 [30]: an energy ball past frame 5 (then a recoil).
  void attack4() override {
    anim = 7;
    enemy_attack_step(this);
    if (bg().flags != 0) return;
    vel[1] = 0;
    vel[2] = 0;
    if (frame <= 5) return;
    if (counters[1] != 0) {
      recoil();
      return;
    }
    counters[1] = 1;
    bsfx_play_at(0x34, this);
    bsfx_play_at(0x23, this);
    if (target != nullptr) {
      if (Shot* s = shot_spawn_callback(shot_ball)) {
        shot_place_at_unit(s, this, 0x15E, 200, 0);
        const Vec4 aim = centre_of(target);
        const Vec4 v = vec_velocity_toward(s->pos, aim.data(), 0x18);
        std::memcpy(s->vel, &v, sizeof(s->vel));
        s->owner = this;
        s->target = target;
        bobj_share_sprite(this, s);
        launch_sparks(s);
        const s32 at[3] = {bobj_front_x(this, 0x168), pos[1] - 0xC8000, pos[2]};
        if (Shot* ring = effect_spawn_sparks_at(at, 0x10, 0x60, 0, 4, 0x3C, 0xE, 3, 0, 1)) ring->vel[2] = 0x400;
      }
    }
    vel[0] = bobj_facing_fx(this, -6);
  }

  // 0x80089108 [31]: two balls past frame 15 (then phase 1); rooted, the 3-step
  // transformation: rumble 60 ticks then immobile, 300 ticks of anim 6, 150 of anim 7.
  void attack5() override {
    BattleGlobals& g = bg();
    if (phase == 2) {
      if (counters[0] == 0 && counters[1] == 0) {
        animset->layout = model_layout + 0xD8;
        loop_limit = 0;
        run_speed = 0;
        base_speed = 0;
        bobj_anim_reset(this);
      }
      special_timer = 8;
      if (g.flags == 0) counters[0] += g.frame_ticks;
      if (counters[1] == 1) {
        anim = 6;
        if (300 < counters[0]) {
          counters[1] = 2;
          counters[0] = 0;
          bsfx_play_at(0xF, this);
          bobj_anim_reset(this);
        }
      } else if (counters[1] == 0) {
        anim = 9;
        if (rng15() < 0x2000) bsfx_play_at(0x50, this);
        scatter_debris(this, 2, pos, 100, false, 0x1000, 0x2000, false);
        if (0x3C < counters[0]) {
          airborne = 1;
          counters[1] = 1;
          counters[0] = 0;
          bobj_anim_reset(this);
        }
      } else if (counters[1] == 2) {
        anim = 7;
        if (0x96 < counters[0]) {
          action_kind = 0;
          begin_action();
          return;
        }
      }
      animate(0);
      return;
    }
    anim = 8;
    enemy_attack_step(this);
    if (g.flags == 0) {
      vel[1] = 0;
      vel[2] = 0;
      if (0xF < frame) {
        if (counters[1] == 0) {
          counters[1] = 1;
          bsfx_play_at(0x34, this);
          bsfx_play_at(0x23, this);
          if (target != nullptr) {
            s16 ball_anim = 0x13;
            for (int i = 0; i < 2; ++i, ball_anim = static_cast<s16>(ball_anim + 3)) {
              Shot* s = shot_spawn_callback(shot_twin_ball);
              if (s == nullptr) continue;
              shot_place_at_unit(s, this, i != 0 ? 0xA0 : 0x15E, 0x10E, 0);
              s->anim = static_cast<u16>(ball_anim);
              const Vec4 aim = centre_of(target);
              const Vec4 v = vec_velocity_toward(s->pos, aim.data(), 0x1C);
              std::memcpy(s->vel, &v, sizeof(s->vel));
              s->owner = this;
              s->target = target;
              bobj_share_sprite(this, s);
              launch_sparks(s);
            }
          }
          vel[0] = bobj_facing_fx(this, -6);
          phase = 1;
          animset->layout = model_layout + 0x6C;
        } else {
          recoil();
        }
      }
    }
    melee_hit();
    rearm_hit();
  }

  // 0x800895f8 [24]: rooted, a hit only shakes it (super armour 20 ticks).
  void state_hit() override {
    if (phase != 2) {
      EnemyUnit::state_hit();
      return;
    }
    const s32 kx = knockback[0] < 0 ? -knockback[0] : knockback[0];
    shake_amp = static_cast<s16>((kx >> 12) << 3);
    if (8 < static_cast<s16>((kx >> 12) << 3)) shake_amp = 8;
    shake_frames = 0x14;
    super_armour = 0x14;
    action_kind = 0;
    begin_action();
  }

  // 0x80089694 [25]: once: debris, 16 beams, a violet ring; then the usual fade.
  void state_dying() override {
    if (counters[0] == 0) {
      scatter_debris(this, 8, pos, 100, true, 0x1000, 0x2000, true);
      for (int i = 0; i < 0x10; ++i) {
        const Vec4 c = centre_of(this);
        const Vec4 d = vec_random_around(c.data(), 0x500);
        const Vec4 c2 = centre_of(this);
        effect_spawn_beam_at(c2.data(), d.data(), 0xE, 0x140, 0x30, 0xC000, 0x400, 0);
      }
      const Vec4 c = centre_of(this);
      particle_spawn_ring(c.data(), 0x18, 0xE0, 0x38, 0x5A, 0, 0x8000FF, 0x100, 0);
      bsfx_play_at(0x50, this);
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
      counters[0] = 1;
    }
    EnemyUnit::state_dying();
  }
};

// 0x800898b0: the balls' step: faces along its velocity, the owner's depth, moves and ages.
void shot_move(Shot* s) {
  s->layer = 2;
  s->facing[1] = s->vel[1];
  s->facing[2] = s->vel[2];
  s->facing[0] = s->vel[0];
  s->size[2] = s->owner->size[2];
  if (bg().flags != 0) return;
  bobj_move(s, s->vel);
  s->p134 += bg().frame_ticks;
}

// 0x80089930: on its first contact with the party the owner's hit lands (knocked along
// the ball's flight); debris, and a ground ring for the twin balls (power 0x201).
void shot_hit(Shot* s, s32 power) {
  if (s->hit_landed != 0) return;
  Unit* units[8] = {};
  Vec4 points[8];
  const int n = collider_attack_overlaps(s, units, points, 1);
  if (n == 0) return;
  s32 vx = s->vel[0];
  s->hit_landed = 1;
  if (vx == 0) vx = s->owner->facing[0];
  if (unit_attack_targets(s->owner, n, units, points, power, vx < 0 ? 0x800 : 0, 0) != 0) {
    bsfx_play_at_pos(0x50, s->pos);
    bsfx_play_at_pos(0x21, s->pos);
    for (int i = 0; i < 8; ++i) {
      Sprite* sp = shot_spawn_sprite_at(s, static_cast<int>(rng_range(3)) + 0xF, 1);
      if (sp == nullptr) continue;
      const Vec4 r = vec_random_around(points[0].data(), 100);
      sprite_set_pos_v(sp, r.data());
      const s32 sc = rng_between(0x1000, 0x2000);
      sp->scale_y = sc;
      sp->scale_x = sc;
      sp->layer = 3;
    }
    if (power == 0x201) {
      const s32 g[3] = {units[0]->pos[0], bobj_ground_height(units[0]), units[0]->pos[2]};
      effect_spawn_sparks_at(g, 0x10, 0xF0, 0x40, 0x14, 0x5A, 4, 1, 0x40, 1);
    }
  }
  s->owner->attack_recovery = 0;
  shot_kill(s);
}

// 0x80089b28: attack 4's ball (anim 0x18) with a sparkle trail; 24 ticks.
void shot_ball(Shot* s) {
  s->anim = 0x18;
  shot_move(s);
  shot_draw_with_shadow(s, 4, 0xFFFFFFFF);
  if (bg().flags != 0 || shot_fade_out(s) != 0) return;
  if (s->p12c == 0 && 0x18 < s->p134) s->p12c = 1;
  if (Sprite* sp = shot_spawn_sprite(s, 0xD)) {
    sprite_set_pos(sp, bobj_front_x(s, -0x20), s->pos[1], s->pos[2]);
    sprite_set_fade(sp, 0x80, -4, 8);
    sp->scale_vel[1] = 0x40;
    sp->scale_vel[0] = 0x40;
    sp->layer = 3;
  }
  shot_hit(s, 0x180);
}

// 0x80089c28: attack 5's balls; 24 ticks or the ground.
void shot_twin_ball(Shot* s) {
  shot_move(s);
  shot_draw_with_shadow(s, 4, 0xFFFFFFFF);
  if (bg().flags != 0 || shot_fade_out(s) != 0) return;
  if (s->p12c == 0 && (0x18 < s->p134 || bobj_ground_height(s) <= s->pos[1])) s->p12c = 1;
  shot_hit(s, 0x201);
}

// 0x80089cd8: the rooted attack 1's beam ball: a beam along its flight; 128 ticks or the
// ground.
void shot_beam(Shot* s) {
  shot_move(s);
  const Vec4 dir = vec_normalize(s->vel);
  draw_beam(s->pos, dir.data(), 0x1020, 0x30FF, 0xC0, 8, 0, 3);
  if (bg().flags != 0 || shot_fade_out(s) != 0) return;
  if (s->p12c == 0 && (0x80 < s->p134 || bobj_ground_height(s) <= s->pos[1])) s->p12c = 1;
  collider_set_attack_box(s, 0xC0, 0x18, 0x18);
  shot_hit(s, 0x200);
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_guardian_create() { return std::make_unique<EnemyGuardian>(); }

}  // namespace ss::game::battle
