// The Lover module: block 253d62d07147 (1652 bytes, slot A linked at 0x80088110), vtable
// 0x8008865c (the factory stores it over the EnemyUnit one: no constructor), monster type
// 0x6d Lover (0x26c-byte objects; encounter 528). (The #2129 image holds a stale copy of this
// block from encounter #1933: catalog rows enemy_lover_*_stale, 0x8007d810-0x8007dd34.) From
// afar it conjures a sparkling burst on the target (a Shot that hits every 4 ticks while its
// animation runs); below half HP it may heal a quarter of its max HP.

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

void shot_burst(Shot* s);

struct EnemyLover : EnemyUnit {
  // 0x80088110 [26]: from 440 units (or half the time) the burst (2) at once (slot 12 right
  // after the approach began); else, below half HP, 20000 / 32768 the heal (3); else the
  // base choice.
  void choose_target() override {
    if (enemy_try_approach(this, 0x1B8, 0x4000, 2, 0x1B8) != 0) {
      action_kind = 2;
    } else {
      const Stats* s = st();
      if (19999 < rng15() || s->hp_max / 2 <= s->hp) {
        EnemyUnit::choose_target();
        return;
      }
      action_kind = 3;
    }
    begin_action();
  }

  // 0x800881cc [28]: anim 5, 3 loops; once past frame 17 the burst on the target's position
  // (sfx 0x26 at the Shot).
  void attack2() override {
    anim = 5;
    loop_limit = 3;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (counters[1] != 0 || bg().flags != 0 || frame < 0x12) return;
    counters[1] = 1;
    if (target == nullptr) return;
    Shot* s = shot_spawn_callback(shot_burst);
    if (s == nullptr) return;
    bobj_set_pos(s, target->pos);
    s->owner = this;
    bsfx_play_at(0x26, s);
    bobj_share_sprite(this, s);
  }

  // 0x800882e4 [29]: anim 6; past frame 8, every frame (no once-flag) 3 rising sparkles
  // (anim 8) in a disc of 0x40 round its feet; at the end it heals a quarter of its max HP
  // (sfx 0x13).
  void attack3() override {
    anim = 6;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      const s32 heal = -st()->hp_max / 4;
      damage_popup_spawn(this, heal, -0x2000, 0);
      unit_damage_hp(this, heal);
      bsfx_play_at(0x13, this);
      action_kind = 0;
      begin_action();
      return;
    }
    if (counters[1] != 0 || bg().flags != 0 || frame < 9) return;
    for (int i = 0; i < 3; ++i) {
      Sprite* sp = unit_play_anim(this, 8);
      if (sp == nullptr) continue;
      const Vec4 p = vec_random_in_disc_xz(pos, 0x40);
      sprite_set_pos_v(sp, p.data());
      sprite_set_fade(sp, 0x80, -4, 0xC);
      sp->accel[1] = -0x800;
      sp->layer = 1;
    }
  }
};

// 0x80088468: the burst (anim 8 once, depth 0x60): dies with its animation or when the battle
// leaves state 2; each frame an orbiting sparkle (anim 10 / 11 / 12); its boxes hit the party
// for the owner (power 0x100), then rest 4 ticks (+0x130).
void shot_burst(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 8;
  s->loop_limit = 1;
  s->size[2] = 0x60;
  s->layer = 2;
  if (static_cast<s16>(shot_draw_and_box(s, 5, 0xFFFFFFFF)) == -1 || g.state != kStateFighting) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  if (Sprite* sp = shot_spawn_sprite(s, rng_between(10, 0xC))) {
    sprite_set_pos_v(sp, s->pos);
    sprite_set_orbit(sp, static_cast<s32>(rng_range(0x1000)), 0, 0x40, 0, 0x40, 0x2D, 0, 4);
    sp->layer = 2;
    sprite_set_fade(sp, 0x80, -4, 0xC);
  }
  if (s->p130 != 0) {
    s->p130 -= g.frame_ticks;
    if (s->p130 < 0) s->p130 = 0;
    return;
  }
  Unit* units[8] = {};
  Vec4 points[8];
  const int n = collider_attack_overlaps(s, units, points, 1);
  if (n == 0) return;
  s->p130 = 4;
  unit_attack_targets(s->owner, n, units, points, 0x100, 9999, 0);
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_lover_create() { return std::make_unique<EnemyLover>(); }

}  // namespace ss::game::battle
