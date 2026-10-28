// Shin's enemy-code module: block 570710d436f5 (3644 bytes, slot C at 0x8008b1f0-0x8008c02c),
// vtable 0x8008bf04, ctor 0x8008b1f0 (0x26c-byte objects); monster types 0xb9 Shin, 0xba
// Hellmaster, 0xbb Weirddevil (encounters 430 431 432 434 459 462 ...). A hovering devil:
// a homing energy ball (attack 1) and the drain (attack 2: it flies into the target, holds
// it, takes an eighth of its max HP and MP and throws it off). A hit can knock it out of
// the air; on the ground it lunges at its attacker (attack 4) or rises back (attack 3).

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

// y - size[1] << shift: 0xd the body's centre (as the other modules' centre points), 0xe
// twice as high (the drain's aim and ring).
s32 body_y(const Unit* u, int shift) { return u->pos[1] - (static_cast<s32>(u->size[1]) << shift); }

struct EnemyShin : EnemyUnit {
  // 0x8008b1f0: enemy_create, the module's vtable, hover +0x160 = 8.
  EnemyShin() { hover_amp = 8; }

  // 0x8008b230 [32]: idle, it hovers again (flying, not airborne).
  int pre_think() override {
    if (action == 0) {
      hover_amp = 8;
      flying = 1;
      airborne = 0;
    }
    return 0;
  }

  // 0x8008b25c [26]: at its hover height, the drain (message 0x2716) when the approach
  // starts; else the usual choice.
  void choose_target() override {
    if (pos[1] == hover_height && enemy_try_approach(this, 400, 0x1800, 2, 0) != 0) {
      battle_message_show(0x2716);
      return;
    }
    EnemyUnit::choose_target();
  }

  // 0x8008b2c4 [27]: past frame 2 one homing ball with a teal trail (speed 0x40 for type
  // 0xa2, which is not one of this module's types: the archer family's code reused).
  void attack1() override {
    anim = 4;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame < 3) return;
    bsfx_play_at(0x21, this);
    bsfx_play_at(0x34, this);
    ShotDesc* d = shot_desc_init(6);
    d->trail_rgb = 0x408080;
    d->speed = static_cast<s16>(st()->id) == 0xA2 ? 0x40 : 0x20;
    d->life = 0x38;
    d->hit_effect = 0xD;
    d->flags = 0x100006;
    d->target = target;
    const s32 from[3] = {bobj_front_x(this, 0x40), pos[1] - 0x80000, pos[2]};
    if (Shot* s = shot_spawn_projectile(this, from, d, this)) {
      s->size[0] = 0x18;
      s->size[1] = 0x10;
    }
    counters[1] = 1;
  }

  // 0x8008b44c [28]: the drain, by +0x174: 0 flies at the target's head (4 a tick) until its
  // attack box meets the party (then 1, snapped to the target's x), giving up after 25
  // ticks; 1 holds the target frozen for 31 ticks, then the drain (HP / MP eighths of the
  // target's maxima, at most what it has, popups both ways), the target knocked away with
  // a spark ring and 18 beams, and 2: it springs back and up (vel -9 / -16, gravity 1)
  // until it falls again. Ends with the animation or when the target is down.
  void attack2() override {
    BattleGlobals& g = bg();
    anim = counters[0] != 0 ? 7 : 5;
    loop_limit = 0;
    hover_amp = 0;
    bool done = false;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1 || unit_is_down(target)) done = true;
    if (!done) {
      vel[2] = 0;
      if (g.flags != 0) return;
      if (counters[0] == 1) {
        if (target != nullptr) target->super_armour = 0xC;
        counters[1] += g.frame_ticks;
        if (counters[1] < 0x1F) return;
        counters[0] = 2;
        vel[0] = bobj_facing_fx(this, -9);
        vel[1] = -0x10000;
        sound_handle_pitch_down(bsfx_play_at(0x33, this), 0x3C);
        if (target != nullptr) drain();
        return;
      }
      if (counters[0] == 0) {
        const s32 ty = target != nullptr ? body_y(target, 0xE) : 0;
        vel[1] = ty < pos[1] ? -0x4000 : 0;
        vel[0] = (target != nullptr && target->pos[0] <= pos[0]) ? -0x4000 : 0x4000;
        // (Only the arena edge stops it: a unit in the way does not.)
        if (unit_try_move_scaled(this, vel, 7) != kWallBlock) bobj_move(this, vel);
        Unit* units[8] = {};
        Vec4 points[8];
        if (collider_attack_overlaps(this, units, points, 1) != 0) {
          counters[0] = 1;
          bobj_anim_reset(this);
          counters[1] = 0;
          counters[2] = 0;
          pos[0] = target->pos[0];
          return;
        }
        counters[1] += g.frame_ticks;
        if (counters[1] < 0x19) return;
      } else if (counters[0] == 2) {
        if (unit_try_move_scaled(this, vel, 7) != kWallBlock) bobj_move(this, vel);
        vel[1] += g.frame_ticks * 0x1000;
        if (vel[1] < 0) return;
      } else {
        return;
      }
    }
    action_kind = 0;
    begin_action();
  }
  // (0x8008b44c's drain.)
  void drain() {
    const Stats* ts = target->st();
    s32 hp = ts->hp_max / 8;
    const s32 mp_max8 = ts->mp_max / 8;
    if (ts->hp < hp) hp = ts->hp;
    s32 mp = mp_max8;
    if (ts->mp < mp_max8) mp = ts->mp;
    damage_popup_spawn(this, -hp, -0x2000, 0);
    unit_damage_hp(this, -hp);
    damage_popup_spawn(this, -mp, 0x2000, 1);
    unit_damage_mp(this, -mp);
    damage_popup_spawn(target, hp, -0x2000, 0);
    unit_damage_hp(target, hp);
    damage_popup_spawn(target, mp, 0x2000, 1);
    unit_damage_mp(target, mp);
    target->reaction = 1;
    target->knockback[0] = bobj_facing_fx(this, 8);
    target->knockback[1] = 0;
    target->knockback[2] = 0;
    const s32 top[3] = {target->pos[0], body_y(target, 0xE), target->pos[2]};
    if (Shot* ring = effect_spawn_sparks_at(top, 0x14, 0xF0, -0x40, 0x10, 0x5A, 8, 4, 0x40, 0)) ring->vel[0] = 0x16;
    for (int i = 0; i < 0x12; ++i) {
      const s32 c[3] = {target->pos[0], body_y(target, 0xE), target->pos[2]};
      const Vec4 r = vec_random_around(c, 600);
      const s32 mid[3] = {target->pos[0], body_y(target, 0xD), target->pos[2]};
      effect_spawn_beam_at(mid, r.data(), 9, 0x118, 0x14, 0x8000, 0x400, 0);
    }
  }

  // 0x8008b998 [29]: back up: anim 0xe once (it gets up), then anim 0xb while it rises (or
  // sinks) at half its run speed a tick to the hover height.
  void attack3() override {
    BattleGlobals& g = bg();
    hover_amp = 0;
    anim = counters[0] != 0 ? 0xB : 0xE;
    loop_limit = counters[0] == 0 ? 1 : 0;
    if (static_cast<s16>(animate(0)) == -1 && counters[0] == 0) {
      bobj_anim_reset(this);
      counters[0] = 1;
      anim = 0xB;
      animate(0);
    }
    if (g.flags != 0 || counters[0] == 0) return;
    const s32 step = (g.frame_ticks * run_speed) / 2;
    if (pos[1] < hover_height) {
      pos[1] += step;
      if (pos[1] <= hover_height) return;
    } else {
      pos[1] -= step;
      if (hover_height <= pos[1]) return;
    }
    action_kind = 0;
    pos[1] = hover_height;
    begin_action();
  }

  // 0x8008bb28 [30]: the lunge on the ground (anim 8, a red afterimage, dust) sliding to a
  // stop (0.5 a tick); then it takes off again (attack 3).
  void attack4() override {
    BattleGlobals& g = bg();
    anim = 8;
    loop_limit = 0;
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, 4, 0x802000, 1);
    if (enemy_attack_step(this) != 0) {
      if (effects[0] != nullptr) effects[0]->ending = 1;
      effects[0] = nullptr;
    }
    if (g.flags != 0) return;
    for (int i = 0; i < 4; ++i) effect_dust_puff(pos, 2);
    if (unit_try_move_scaled(this, vel, 7) == nullptr) bobj_move(this, vel);
    if (vel[0] < 0) {
      vel[0] += g.frame_ticks * 0x800;
      if (vel[0] < 0) return;
    } else {
      vel[0] -= g.frame_ticks * 0x800;
      if (0 < vel[0]) return;
    }
    if (effects[0] != nullptr) effects[0]->ending = 1;
    flying = 1;
    effects[0] = nullptr;
    airborne = 0;
    action_kind = 3;
    begin_action();
    counters[0] = 1;
  }

  // 0x8008bcac [4]: a hit (+0x127 == 1) knocks it out of the air 12000 / 32768 of the time.
  void interrupt() override {
    if (reaction == 1 && rng15() < 12000) {
      airborne = 1;
      flying = 0;
    }
  }

  // 0x8008bcfc [24]: knocked back: flying, the hit animation shifted by the state (anim +
  // state - 3), else the fall (anim 0xd). Once the slide ends (and, knocked down, it is on
  // the ground or airborne is 2) the state ends; knocked down, it lunges (attack 4) at its
  // attacker when that one stands within 400, else it takes off (attack 3).
  void state_hit() override {
    if (flying == 0) {
      anim = 0xD;
    } else {
      anim = static_cast<u16>(anim + 0xFFFD + action);
    }
    if (bg().flags == 0) {
      bool done = false;
      if (unit_knockback_slide(this) &&
          (flying != 0 || bobj_ground_height(this) <= pos[1] || airborne == 2))
        done = true;
      if (done) {
        action = 0;
        if (flying == 0) {
          bool near = false;
          if (!unit_is_down(last_attacker)) near = vec_distance_fx(last_attacker->pos, pos) < 0x190000;
          if (near) {
            bsfx_play_at(0x34, this);
            action_kind = 4;
            if (unit_is_down(last_attacker)) {
              // Unreachable (near needs the attacker standing); as the original it would
              // set pos x (+0x8), not the facing, to the signed 0x18.
              const s32 v = bobj_facing_fx(this, 0x18);
              pos[0] = v;
              vel[0] = v;
            } else {
              const s32 v = pos[0] < last_attacker->pos[0] ? 0x18000 : -0x18000;
              vel[0] = v;
              facing[0] = v;
            }
            vel[1] = 0;
            vel[2] = 0;
          } else {
            bsfx_play_at(0x1E, this);
            flying = 1;
            airborne = 0;
            action_kind = 3;
          }
          begin_action();
          counters[0] = 0;
          return;
        }
      }
    }
    animate(0);
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_shin_create() { return std::make_unique<EnemyShin>(); }

}  // namespace ss::game::battle
