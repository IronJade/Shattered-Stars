// Zand's enemy-code module: block 7956332dbbd1 (2264 bytes, slot A linked at 0x80088110),
// vtable 0x800888c0, monster type 0xb8 Zand (0x26c-byte objects), encounter 265. Attack 1
// is the base swing, attack 2 a strike that sends a shock wave along the ground at the
// target (each wave spawns the next one two steps ahead), attack 3 a leaping cut with an
// afterimage.

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

void shot_shockwave(Shot* s);

struct EnemyZand : EnemyUnit {
  // 0x80088110: enemy_create and the module's vtable.

  // 0x80088148 [26]: near, the leap (attack 3); farther, the shock wave (2); else the usual
  // choice.
  void choose_target() override {
    if (enemy_try_approach(this, 300, 0x2000, 3, 0x60) != 0) {
      counters[2] = 0;
      return;
    }
    if (enemy_try_approach(this, 700, 0x1000, 2, 0x280) != 0) {
      counters[3] = 0;
      return;
    }
    EnemyUnit::choose_target();
  }

  // 0x800881c4 [27]: the base swing (enemy_attack1).
  void attack1() override { EnemyUnit::attack1(); }

  // 0x800881e4 [28]: a strike (anim 5); past frame 4, while fighting, one shock wave from
  // its feet toward the target (0x30 a tick on the ground plane).
  void attack2() override {
    BattleGlobals& g = bg();
    anim = 5;
    loop_limit = 1;
    enemy_attack_step(this);
    if (g.flags != 0 || counters[1] != 0 || frame < 5 || g.state != kStateFighting || target == nullptr) return;
    if (Shot* s = shot_spawn_callback(shot_shockwave)) {
      bobj_set_pos(s, pos);
      const Vec4 v = vec_velocity_toward_xz(pos, target->pos, 0x30);
      s->vel[0] = v.x;
      s->vel[1] = v.y;
      s->vel[2] = v.z;
      s->owner = this;
      bobj_share_sprite(this, s);
      bsfx_play_at(0x34, s);
    }
    counters[1] = 1;
  }

  // 0x80088760: the leap's step (x by vel x, then y by vel y with 'gravity' a tick): 0 in
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
      if (!(bobj_ground_height(this) < pos[1])) return 0;  // (lands once below the ground)
      pos[1] = bobj_ground_height(this);
      vel[1] = 0;
      bsfx_play_at(0x1E, this);
      r = 2;
      airborne = 0;
    }
    jumping = 0;
    return r;
  }

  // 0x800884c4 [29]: the leaping cut (anim 6, afterimage 0x602008): from frame 1 it jumps
  // at the target (vel y -6, x 6 a tick toward it); hits before frame 9 send the units up
  // and away; landing raises 8 dust puffs.
  void attack3() override {
    if (counters[2] == 0 && 0 < frame) {
      if (target != nullptr) facing[0] = pos[0] < target->pos[0] ? 0x40000 : static_cast<s32>(0xFFFC0000);
      counters[2] = 1;
      vel[1] = -0x6000;
      jumping = 1;
      airborne = 1;
    }
    loop_limit = 1;
    vel[2] = 0;
    anim = 6;
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, 3, 0x602008, 1);
    if (bg().flags == 0) {
      Unit* units[8] = {};
      Vec4 points[8];
      const int n = collider_attack_overlaps(this, units, points, 1);
      if (n != 0 && hit_landed == 0) {
        s32 angle = 9999;
        if (frame < 9) angle = facing[0] < 0 ? 0x9C7 : -0x1C7;
        unit_attack_targets(this, n, units, points, 0x100, angle, 0);
      }
      // The landed flag resets while no attack box is on, so the next frame can hit again.
      if (hit_landed != 0 && (box_mask & 0xC) == 0) hit_landed = 0;
    }
    if (jumping == 0) {
      vel[0] = 0;
    } else {
      vel[0] = 0 < facing[0] ? 0x6000 : -0x6000;
    }
    const int r = leap_step(0x400);
    if (r != 0) {
      bsfx_play_at(0x1E, this);
      if (r == 2)
        for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
    }
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      if (effects[0] != nullptr) effects[0]->ending = 1;
      effects[0] = nullptr;
    }
  }

  // 0x80088898 [36]: the dtor (EnemyUnit vtable, unit_destroy).
};

// 0x800882ec: attack 2's shock wave (anim 8, one loop, the owner's depth) following the
// ground: after 4 ticks it spawns the next wave twice its velocity ahead (passing on its
// fade and landed flags); outside the arena it starts to fade; until it has hit, the owner's
// melee hit (power 0x100) on the party. Dies with its animation.
void shot_shockwave(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 8;
  s->size[2] = s->owner->size[2];
  s->loop_limit = 1;
  if (static_cast<s16>(shot_draw_with_shadow(s, 6, 0xFFFFFFFF)) == -1) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  shot_fade_out(s);  // (its result is not looked at: a Shot that died here runs on)
  bool to_ground = false;
  if (s->hit_landed == 0) {
    if (s->p12c != 0) {
      to_ground = true;
    } else if (3 < s->p128 && s->p134 == 0) {
      s->p134 = 1;
      if (Shot* n = shot_spawn_callback(shot_shockwave)) {
        n->pos[0] = s->pos[0] + s->vel[0] * 2;
        n->pos[1] = s->pos[1];
        n->pos[2] = s->pos[2] + s->vel[2] * 2;
        n->vel[0] = s->vel[0];
        n->vel[1] = s->vel[1];
        n->vel[2] = s->vel[2];
        n->p12c = s->p12c;
        n->owner = s->owner;
        n->hit_landed = s->hit_landed;
        bobj_share_sprite(s, n);
      }
    }
  }
  if (!to_ground && s->p12c == 0) {
    if (bobj_outside_arena(s)) {
      s->p12c = 1;
      return;
    }
    s->p128 += g.frame_ticks;
  }
  s->pos[1] = bobj_ground_height(s);
  if (s->hit_landed == 0 && shot_hit_melee(s, 0x100, 1) != 0) s->hit_landed = 1;
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_zand_create() { return std::make_unique<EnemyZand>(); }

}  // namespace ss::game::battle
