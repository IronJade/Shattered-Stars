// Ghost's enemy-code module: block d4cbc8c13d8e (1708 bytes, slot A at 0x80088110-0x800887bc),
// vtable 0x80088694, ctor 0x80088110 (0x26c-byte objects); monster type 0xaa Ghost (encounter
// 211). A touch with a flash on the victim (attack 1), a volley of 16 slow spirit balls
// (attack 2) and a slash conjured on the target, after which it fades away for a second
// and comes back (attack 3).

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"

namespace ss::game::battle {
namespace {

struct EnemyGhost : EnemyUnit {
  // 0x80088110: enemy_create, the module's vtable, hit sounds 0x4c / 0x4e.
  EnemyGhost() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4E;
  }

  // 0x80088158 [26]: the volley from afar, else close in for the slash (+0x17c cleared),
  // else the usual choice.
  void choose_target() override {
    if (enemy_try_approach(this, 600, 7000, 2, 400) != 0) return;
    if (enemy_try_approach(this, 200, 0x2000, 3, 0x15E) != 0) {
      counters[2] = 0;
      return;
    }
    EnemyUnit::choose_target();
  }

  // 0x800881cc [27]: the touch (once through anim 4); a hit that dealt damage flashes
  // (anim 7) on the first victim.
  void attack1() override {
    loop_limit = 1;
    anim = 4;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0) return;
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n == 0 || hit_landed != 0) return;
    if (unit_attack_targets(this, n, units, points, 0x100, 9999, 0) == 0) return;
    if (Sprite* sp = unit_play_anim_ex(this, 7, 1)) {
      sprite_set_pos_v(sp, units[0]->pos);
      sp->layer = 2;
    }
  }

  // 0x8008831c [28]: melee frames (enemy_attack_step) and once past frame 16 a volley of 16
  // slow balls toward 0x180 ahead, scattered +-0xc high and +-0x140 deep (life 0x60).
  void attack2() override {
    anim = 5;
    enemy_attack_step(this);
    if (bg().flags != 0 || counters[1] != 0 || frame < 0x11) return;
    counters[1] = 1;
    sound_handle_pitch_up(bsfx_play_at(0x34, this), 0x24);
    for (int i = 0; i < 0x10; ++i) {
      ShotDesc* d = shot_desc_init(0xB);
      const s32 from[3] = {bobj_front_x(this, 0x10), pos[1] - 0xA0000, pos[2]};
      d->speed = 0x10;
      d->point[0] = bobj_front_x(this, 0x180);
      d->point[1] = from[1] + rng_between(-0xC000, 0xC000);
      const s32 dz = rng_between(-0x140000, 0x140000);
      d->life = 0x60;
      d->flags = 6;
      d->power = 0x100;
      d->hit_effect = 0xD;
      d->hits = 2;
      d->hit_bits[2] = 0x1000;
      d->hit_bits[1] = 0;
      d->hit_bits[0] = 1;
      d->point[2] = from[2] + dz;
      shot_spawn_projectile(this, from, d, this);
    }
  }

  // 0x800884a8 [29]: by +0x178: 0 anim 7 once, past frame 4 a slash (Shot kind 0x2d) on
  // the target's upper body (once, +0x17c); 1 gone (not animated, so not drawn) for 60
  // ticks of +0x174 (counted even while the battle is paused); 2 anim 8 once; then idle.
  void attack3() override {
    anim = counters[1] != 0 ? 8 : 7;
    loop_limit = 1;
    if (counters[1] != 1) {
      if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
        ++counters[1];
        bobj_anim_reset(this);
        if (counters[1] != 3) return;
        action_kind = 0;
        begin_action();
        return;
      }
      if (counters[1] != 1) {
        if (bg().flags != 0 || counters[1] != 0 || counters[2] != 0 || frame < 5) return;
        counters[2] = 1;
        sound_handle_pitch_down(bsfx_play_at(0x34, this), 0xF);
        if (target == nullptr) return;
        // Kind 0x2d (0x8005b9d0, the owner's attack as a one-shot slash) is the battle
        // agent's: until it is ported the port's Shot update logs and removes it.
        Shot* s = shot_spawn(0x2D);
        if (s == nullptr) return;
        const s32 at[3] = {target->pos[0], target->pos[1] - (static_cast<s32>(target->size[1]) << 14), target->pos[2]};
        bobj_set_pos(s, at);
        s->owner = this;
        bobj_share_sprite(this, s);
        return;
      }
    }
    counters[0] += bg().frame_ticks;
    if (0x3B < counters[0]) counters[1] = 2;
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_ghost_create() { return std::make_unique<EnemyGhost>(); }

}  // namespace ss::game::battle
