// Marsilio's enemy-code module: block f1ff0f5b5185 (3208 bytes, slot A linked at
// 0x80088110), vtable 0x80088c70, monster type 0xc7 (Marsilio), encounters 530 and 532. A
// swordsman with three slanted cuts and a leaping smash, every swing trailing an
// afterimage; the smash's landing (frames 13+) hits at double power with a shock ring.

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"

namespace ss::game::battle {
namespace {

// The target's centre (its body's mid height).
Vec4 centre_of(const Unit* u) { return {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0}; }

struct EnemyMarsilio : EnemyUnit {
  // 0x80088110: enemy_create and the module's vtable.

  // 0x80088148 [26]: the leap (4) from afar, the cut (3) or the double cut (2) up close.
  void choose_target() override {
    if (enemy_try_approach(this, 400, 0x3830, 4, 0x18) != 0) {
      counters[2] = 0;
      battle_message_show(0x2720);
    } else if (enemy_try_approach(this, 300, 0x2000, 3, 0x40) != 0) {
      battle_message_show(0x271F);
    } else if (enemy_try_approach(this, 300, 24000, 2, 0x5C) == 0) {
      EnemyUnit::choose_target();
    }
  }

  // The swing's afterimage (+0x1f4): 'copies' tinted copies, created on the first frame.
  void bind_trail(int copies) {
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, copies, 0x703000, 1);
  }
  void end_trail() {
    if (effects[0] != nullptr) effects[0]->ending = 1;
    effects[0] = nullptr;
  }
  // The attack slots' end: back to idle (slot 12) and the trail lets go.
  bool anim_done() {
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) != -1) return false;
    action_kind = 0;
    begin_action();
    end_trail();
    return true;
  }
  // The cuts knock the party up and away at a slant (2503 / -455 of 4096: 40 degrees off
  // the x axis, mirrored by the side it faces).
  s32 slant_angle() const { return facing[0] < 0 ? 0x9C7 : -0x1C7; }
  // The landed flag resets while no attack box is on, so the next swing can hit again.
  void rearm_hit() {
    if ((box_mask & 0xC) == 0 && hit_landed != 0) hit_landed = 0;
  }

  // 0x800881f8 [27]: a plain swing (enemy_attack_step) with its afterimage to the end.
  void attack1() override {
    loop_limit = 1;
    anim = 4;
    bind_trail(2);
    // ORIGINAL BUG (original_bugs.md E5): the step's result (hits + 1) is read as "done", so
    // the trail ends on the hit instead of with the swing:
    //   if (enemy_attack_step(this) != 0) end_trail();
    // The swing's end goes idle through begin_action, whose release_effects ends the trail.
    enemy_attack_step(this);
  }

  // 0x80088290 [28]: a slanted cut (one hit per attack).
  void attack2() override {
    loop_limit = 1;
    anim = 5;
    bind_trail(2);
    if (anim_done() || bg().flags != 0) return;
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n != 0 && hit_landed == 0) unit_attack_targets(this, n, units, points, 0x100, slant_angle(), 0);
  }

  // 0x80088408 [29]: the double cut: straight knock up to frame 3, slanted after; the hit
  // re-arms between the two boxes.
  void attack3() override {
    loop_limit = 1;
    anim = 6;
    bind_trail(3);
    if (anim_done() || bg().flags != 0) return;
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n != 0 && hit_landed == 0) {
      const s32 angle = frame > 3 ? slant_angle() : 9999;
      unit_attack_targets(this, n, units, points, 0x100, angle, 0);
    }
    rearm_hit();
  }

  // 0x800885b8 [30]: the leap: past frame 12 it jumps forward (6 a tick, rising 10 falling
  // 0.75 a tick per tick) until a unit stops it or it lands (dust); frames 5-12 cut at a
  // slant, frames 13+ smash at double power: hit sprites, a burst of 24 shock sprites and
  // two rings at the first victim's feet.
  void attack4() override {
    BattleGlobals& g = bg();
    loop_limit = 1;
    anim = 7;
    bind_trail(6);
    if (anim_done()) {
      jumping = 0;
      return;
    }
    if (g.flags != 0) return;
    if (frame > 12 && counters[1] == 0) {
      vel[1] = -0xA000;
      vel[0] = bobj_facing_fx(this, 6);
      counters[1] = 1;
      jumping = 1;
      airborne = 1;
    }
    if (jumping != 0) leap_step();
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n != 0 && hit_landed == 0) {
      s32 angle = 9999;
      int kind = 0;  // 0 before the cut, 1 the cut, 2 the smash
      if (static_cast<u32>(static_cast<u8>(frame)) - 5 < 8) {
        angle = slant_angle();
        kind = 1;
      }
      if (frame > 12) kind = 2;
      if (unit_attack_targets(this, n, units, points, kind == 2 ? 0x200 : 0x100, angle, 0) != 0 && kind != 0) {
        for (int i = 0; i < n; ++i) {
          if (Sprite* sp = unit_play_anim_ex(this, kind + 8, 1)) {
            sprite_set_pos_v(sp, points[i].data());
            sp->scale_y = 0x2000;
            sp->scale_x = 0x2000;
            sp->layer = 3;
          }
        }
        if (kind == 2) smash_burst(units[0]);
      }
    }
    rearm_hit();
  }
  // The leap's step: x then y, each only when free (a unit under it ends the leap with a
  // thud); gravity, and the landing on the ground.
  void leap_step() {
    BattleGlobals& g = bg();
    s32 d[3] = {vel[0], 0, 0};
    if (unit_try_move_scaled(this, d, 7) == nullptr) bobj_move(this, d);
    d[0] = 0;
    d[1] = vel[1];
    if (unit_try_move_scaled(this, d, 7) == nullptr) {
      bobj_move(this, d);
    } else {
      bsfx_play_at(0x1E, this);
      airborne = 2;
      jumping = 0;
    }
    const s32 ground = bobj_ground_height(this);
    vel[1] += g.frame_ticks * 0xC00;
    if (ground < pos[1]) {
      pos[1] = ground;
      vel[1] = 0;
      bsfx_play_at(0x1E, this);
      airborne = 0;
      jumping = 0;
      for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 3);
    }
  }
  // The smash's impact at the victim's feet: 24 fading shock sprites (anim 0xd) flying out
  // in 15-degree steps, a ring of sparks drawn with its own anim 10 and a wider ring.
  void smash_burst(Unit* victim) {
    bsfx_play_at(0x50, this);
    bsfx_play_at(0x21, this);
    s32 deg = 0;
    for (int i = 0; i < 0x18; ++i, deg += 0xF000) {
      Sprite* sp = unit_play_anim(this, 0xD);
      if (sp == nullptr) continue;
      sprite_set_fade(sp, 0x80, -2, 8);
      const s32 at[3] = {victim->pos[0], bobj_ground_height(victim), victim->pos[2]};
      sprite_set_pos_v(sp, at);
      sp->vel[0] = bcos(static_cast<u32>(deg / 0x168)) * 10;
      sp->vel[2] = bsin(static_cast<u32>(deg / 0x168)) * 10;
      sp->layer = 3;
    }
    const s32 feet[3] = {victim->pos[0], bobj_ground_height(victim), victim->pos[2]};
    if (Shot* ring = effect_spawn_sparks_at(feet, 0x14, 0x18, 0x140, 8, 0x3C, 0, 2, 0x60, 1)) {
      bobj_share_sprite(this, ring);
      ring->anim = 10;
      ring->vel[0] = 0xB;
    }
    const s32 feet2[3] = {victim->pos[0], bobj_ground_height(victim), victim->pos[2]};
    effect_spawn_sparks_at(feet2, 0x14, 0x60, 0xE0, 0x18, 0x5A, 0, 1, 0x40, 1);
  }

  // 0x80088b48 [25]: once: 48 beams out of its centre; then the usual fade.
  void state_dying() override {
    if (counters[0] == 0) {
      for (int i = 0; i < 0x30; ++i) {
        const Vec4 c = centre_of(this);
        const Vec4 d = vec_random_around(c.data(), 0x500);
        const Vec4 c2 = centre_of(this);
        effect_spawn_beam_at(c2.data(), d.data(), 7, 0x140, 0x18, 0xA000, 0x400, 0);
      }
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
      counters[0] = 1;
    }
    EnemyUnit::state_dying();
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_marsilio_create() { return std::make_unique<EnemyMarsilio>(); }

}  // namespace ss::game::battle
