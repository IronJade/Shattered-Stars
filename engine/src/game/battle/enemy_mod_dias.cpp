// Dias's enemy-code module: block a2f3b4380260 (3220 bytes, slot A linked at 0x80088110),
// vtable 0x80088c7c, monster type 0xb0 Dias (0x26c-byte objects), encounters 204 and 263.
// A swordsman: attack 2 a leaping cut (a slash mark where it lands, two sounds on landing),
// attack 3 a cut with a flash, attack 4 a sword wave (one projectile with an afterimage);
// attack 1 is the base swing. Each attack starts with a voice clip from the pack's extra
// data.

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"

namespace ss::game::battle {
namespace {

// The wave's afterimage lives in the Shot's +0x130; the port keeps p130 as the "bound" flag
// (shot_reset clears it) and the pointer here.
std::unordered_map<const Shot*, Afterimage*>& wave_trails() {
  static std::unordered_map<const Shot*, Afterimage*> trails;
  return trails;
}
void wave_trail_end(const Shot* s) {
  if (s->p130 == 0) return;
  if (Afterimage* a = wave_trails()[s]) a->ending = 1;
}

void shot_slash(Shot* s);
void shot_flash(Shot* s);
void shot_wave(Shot* s);

struct EnemyDias : EnemyUnit {
  // 0x80088110: enemy_create and the module's vtable.

  // 0x80088148 [34]: a voice clip from the pack's extra data: hit (0) the second, attack 1
  // the first, attack 2 the third, attacks 3 / 4 the fourth (0x80088178..0x800881a8 are
  // the switch's cases, jump table 0x80088c64).
  void on_attack_start(int attack) override {
    const void* clip;
    switch (static_cast<u32>(attack)) {
      case 0: clip = enemy_extra_data(1); break;
      case 1: clip = enemy_extra_data(0); break;
      case 2: clip = enemy_extra_data(2); break;
      case 3:
      case 4: clip = enemy_extra_data(3); break;
      default: return;
    }
    // (The console plays unconditionally; the port has no clip until the table is exposed.)
    if (clip != nullptr) bsfx_play(0, clip, this, 1);
  }

  // 0x800881cc [26]: far off the wave (attack 4), near the flash cut (3), else the leap (2),
  // else the usual choice; an attack clears the scratch words.
  void choose_target() override {
    if (enemy_try_approach(this, 600, 4000, 4, 0x240) != 0 || enemy_try_approach(this, 300, 12000, 3, 0x60) != 0 ||
        enemy_try_approach(this, 400, 0x2000, 2, 0xC0) != 0) {
      counters[3] = 0;
      counters[2] = 0;
      counters[1] = 0;
      counters[0] = 0;
      return;
    }
    EnemyUnit::choose_target();
  }

  // 0x80088744: the leap's step (x by vel x, then y by vel y with 'gravity' a tick): 0 in
  // the air (or not leaping), 1 stopped by something on the way down (+0x6b = 2), 2 landed
  // (+0x6b = 0); sfx 0x1e either way, and the leap ends.
  int leap_step(s32 gravity) {
    BattleGlobals& g = bg();
    if (g.flags != 0 || jumping == 0) return 0;
    s32 d[3] = {vel[0], 0, 0};
    if (unit_try_move_scaled(this, d, 7) == nullptr) bobj_move(this, d);
    d[0] = 0;
    d[1] = vel[1];
    int r;
    if (unit_try_move_scaled(this, d, 7) != nullptr) {
      bsfx_play_at(0x1E, this);
      r = 1;
      airborne = 2;
    } else {
      bobj_move(this, d);
      vel[1] += gravity * g.frame_ticks;
      if (!(pos[1] < bobj_ground_height(this))) return 0;
      pos[1] = bobj_ground_height(this);
      vel[1] = 0;
      bsfx_play_at(0x1E, this);
      r = 2;
      airborne = 0;
    }
    jumping = 0;
    return r;
  }

  // 0x8008826c [28]: the leaping cut (anim 6): past frame 2 it jumps at the target (vel y
  // -5, x 7 a tick toward it); its first hit sends the units up and away (past frame 8) and
  // leaves a slash mark on the ground; landing raises 8 dust puffs.
  void attack2() override {
    anim = 6;
    if (counters[2] == 0 && 2 < frame) {
      if (target != nullptr) facing[0] = pos[0] < target->pos[0] ? 0x40000 : static_cast<s32>(0xFFFC0000);
      counters[2] = 1;
      vel[1] = -0x5000;
      jumping = 1;
      airborne = 1;
    }
    loop_limit = 1;
    vel[2] = 0;
    if (bg().flags == 0) {
      Unit* units[8] = {};
      Vec4 points[8];
      const int n = collider_attack_overlaps(this, units, points, 1);
      if (n != 0 && hit_landed == 0) {
        s32 angle = 9999;
        if (8 < frame) angle = facing[0] < 0 ? 0x9C7 : -0x1C7;
        unit_attack_targets(this, n, units, points, 0x100, angle, 0);
        if (Shot* s = shot_spawn_callback(shot_slash)) {
          s->p128 = frame < 9 ? 0 : 1;
          s->pos[0] = points[0].x;
          s->pos[2] = points[0].z;
          s->pos[1] = bobj_ground_height(s);
          s->owner = this;
          s->facing[0] = facing[0];
          s->layer = 1;
          s->target = target;
          bobj_share_sprite(this, s);
        }
      }
      // The landed flag resets while no attack box is on, so the next frame can hit again.
      if (hit_landed != 0 && (box_mask & 0xC) == 0) hit_landed = 0;
    }
    if (jumping == 0) {
      vel[0] = 0;
    } else {
      vel[0] = 0 < facing[0] ? 0x7000 : -0x7000;
    }
    const int r = leap_step(0x300);
    if (r != 0) {
      bsfx_play_at(0x1E, this);
      if (r == 2)
        for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
    }
    if (static_cast<s16>(animate(0)) == -1) {
      action_kind = 0;
      begin_action();
      if (jumping != 0) jumping = 0;
    }
  }

  // 0x80088540 [29]: a cut (anim 5) with a flash Shot on it (spawned on its first frame).
  void attack3() override {
    loop_limit = 1;
    anim = 5;
    enemy_attack_step(this);
    if (bg().flags != 0 || counters[1] != 0) return;
    counters[1] = 1;
    Shot* s = shot_spawn_callback(shot_flash);
    if (s == nullptr) return;
    shot_place_at_unit(s, this, 0, 0, 0);
    s->owner = this;
    s->facing[0] = facing[0];
    s->layer = 1;
    s->target = target;
    bobj_share_sprite(this, s);
  }

  // 0x800885fc [30]: the wave cut (anim 8): a sprite (anim 10) at its feet on the first
  // frame, then past frame 2 one sword wave flying 0x18 a tick ahead (+0x178 counts, +0x17c
  // the 8-tick spacing; the count stops it at one).
  void attack4() override {
    BattleGlobals& g = bg();
    loop_limit = 1;
    anim = 8;
    enemy_attack_step(this);
    if (g.flags != 0) return;
    s32 n = counters[1];
    if (n == 0) {
      counters[1] = 1;
      if (Sprite* sp = unit_play_anim_ex(this, 10, 1)) sprite_set_pos_v(sp, pos);
      n = counters[1];
    }
    if (1 < n || frame < 3) return;
    counters[2] -= g.frame_ticks;
    if (0 < counters[2]) return;
    ++counters[1];
    counters[2] += 8;
    Shot* s = shot_spawn_callback(shot_wave);
    if (s == nullptr) return;
    s->vel[0] = bobj_facing_fx(this, 0x18);
    shot_place_at_unit(s, this, 0, 0, 0);
    s->owner = this;
    s->facing[0] = facing[0];
    s->layer = 1;
    s->target = target;
    bobj_share_sprite(this, s);
  }

  // 0x80088c3c [36]: the dtor (EnemyUnit vtable, unit_destroy).
};

// 0x80088880: the leap's slash mark (anim 0xc, one loop) as a free quad on the ground, 0xf0
// either side of the hit point (mirrored for the late hits, +0x128), 0xc8 deep; dies with
// its animation.
void shot_slash(Shot* s) {
  s->layer = 1;
  s->anim = 0xC;
  s->loop_limit = 1;
  s32 c0[3] = {0, 0, 0};
  s32 c1[3] = {0, 0, 0};
  s32 x1;
  if (s->p128 == 0) {
    c0[0] = bobj_facing_fx(s, 0xF0);
    x1 = -0xF0;
  } else {
    c0[0] = bobj_facing_fx(s, -0xF0);
    x1 = 0xF0;
  }
  c1[0] = bobj_facing_fx(s, x1);
  c0[2] = 0xC8000;
  c0[1] = -0xDC000;
  c1[1] = 0;
  c1[2] = -0xC8000;
  if (shot_draw_frame_quad(s, c0, c1, 5, 3) == -1) shot_kill(s);
}

// 0x80088970: attack 3's flash (anim 7, one loop, layer 2, twice the owner's depth); dies
// with its animation or fades once the battle is over.
void shot_flash(Shot* s) {
  s->anim = 7;
  s->layer = 2;
  s->loop_limit = 1;
  s->size[2] = static_cast<s16>(s->owner->size[2] << 1);
  if (static_cast<s16>(shot_draw_and_box(s, 0, 0xFFFFFFFF)) == -1) {
    shot_kill(s);
    return;
  }
  if (bg().flags == 0) shot_fade_out(s);
}

// 0x80088a04: attack 4's sword wave (anim 10, layer 2, twice the owner's depth, a 3-copy
// afterimage 0x102080): flies along its velocity for 24 ticks (then fades); its first contact
// with the party hits for the owner at power 0x100 (knocked along its flight) with a flash
// (anim 0xc), clears the owner's recovery and ends it.
void shot_wave(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 10;
  s->layer = 2;
  s->size[2] = static_cast<s16>(s->owner->size[2] << 1);
  shot_draw_and_box(s, 2, 0x2080);
  if (g.flags != 0) return;
  if (s->p130 == 0) {
    wave_trails()[s] = afterimage_create(s, 3, 0x102080, 0);
    s->p130 = 1;
  }
  if (shot_fade_out(s) != 0) {
    wave_trail_end(s);
    return;
  }
  if (s->p12c == 0 && 0x18 < s->p134) s->p12c = 1;
  bobj_move(s, s->vel);
  s->p134 += g.frame_ticks;
  // Faces along its velocity (the console copies 4 words, +0x24 = +0x34 too).
  s->facing[0] = s->vel[0];
  s->facing[1] = s->vel[1];
  s->facing[2] = s->vel[2];
  if (s->hit_landed != 0) return;
  Unit* units[8] = {};
  Vec4 points[8];
  const int n = collider_attack_overlaps(s, units, points, 1);
  if (n == 0) return;
  s32 vx = s->vel[0];
  s->hit_landed = 1;
  if (vx == 0) vx = s->owner->facing[0];
  if (unit_attack_targets(s->owner, n, units, points, 0x100, vx < 0 ? 0x800 : 0, 0) != 0) {
    if (Sprite* sp = shot_spawn_sprite_at(s, 0xC, 1)) {
      sprite_set_pos_v(sp, s->pos);
      sp->layer = 3;
    }
  }
  s->owner->attack_recovery = 0;
  wave_trail_end(s);
  shot_kill(s);
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_dias_create() { return std::make_unique<EnemyDias>(); }

}  // namespace ss::game::battle
