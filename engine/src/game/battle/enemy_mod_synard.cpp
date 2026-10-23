// Synard's enemy-code module: block 38d01c574dcc (2640 bytes, slot A linked at 0x80088110),
// vtable 0x80088a38, monster types 0xbc Synard, 0xbd D-tyrant (0x26c-byte objects),
// encounters 513 521 559. A hovering dragon: attack 1 a bite (a flash and beams on what it
// hits), attack 2 fire breath for 100 ticks (three spark rings and a stream of piercing
// projectiles, one every 2 ticks); flapping it blows dust while it flies to its target.

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"

namespace ss::game::battle {
namespace {

// The high halfword of a descriptor word (the console stores an s16 there).
void set_hi16(s32& w, u16 v) { w = static_cast<s32>((static_cast<u32>(w) & 0xFFFFu) | static_cast<u32>(v) << 16); }

struct EnemySynard : EnemyUnit {
  // 0x80088110: enemy_create, the module's vtable, hover amplitude 8.
  EnemySynard() { hover_amp = 8; }

  // 0x80088150 [26]: the breath (attack 2) when the approach roll succeeds, else the usual
  // choice.
  void choose_target() override {
    if (enemy_try_approach(this, 500, 12000, 2, 0x140) != 0) {
      counters[2] = 0;
      return;
    }
    EnemyUnit::choose_target();
  }

  // 0x800881a4 [27]: the bite; when it does damage, a flash (anim 8, 2x) and 5 beams at
  // each point hit.
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
    if (unit_attack_targets(this, n, units, points, 0x100, 9999, 0) != 1) return;
    for (int i = 0; i < n; ++i) {
      if (Sprite* sp = unit_spawn_sprite_ex(this, 8, 1)) {
        sprite_set_pos_v(sp, points[i].data());
        sp->layer = 3;
        sp->scale_y = 0x2000;
        sp->scale_x = 0x2000;
      }
      for (int k = 0; k < 5; ++k) {
        const Vec4 d = vec_random_around(points[i].data(), 0x280);
        effect_spawn_beam_at(points[i].data(), d.data(), 3, 0x1E0, 0x18, 0x30000, 0, 0);
      }
    }
  }

  // 0x800883a8 [23]: the usual approach, and each unpaused frame a puff (anims 9 / 10
  // alternating every 2 frames) placed by its view-space facing (+0x3c / +0x3e): mostly
  // sideways 0xe0 behind and 400 up; otherwise 0x174 up with OT bias 2 (facing y < 0) or -16.
  void state_approach() override {
    EnemyUnit::state_approach();
    BattleGlobals& g = bg();
    if (g.flags != 0) return;
    const s32 vy = view_fy, vx = view_fx;
    const s32 ay = vy < 0 ? -vy : vy;
    const s32 ax = vx < 0 ? -vx : vx;
    u32 dir = 0;
    if (!(ay < ax)) dir = vy < 0 ? 1 : 2;
    Sprite* sp = unit_spawn_sprite(this, ((g.battle_frames >> 1) & 1) + 9);
    if (sp == nullptr) return;
    if (dir == 1) {
      sprite_place_near(sp, this, 0, 0x174, 0);
      sp->layer = 2;
    } else if (dir == 0) {
      sprite_place_near(sp, this, -0xE0, 400, 0);
    } else {
      sprite_place_near(sp, this, 0, 0x174, 0);
      sp->layer = -0x10;
    }
    sprite_set_fade(sp, 0x80, -8, 0x10);
  }

  // 0x800884e8 [28]: fire breath (anim 5 looping) for 100 ticks: from frame 1 a roar (its
  // handle in +0x17c) and three turning spark rings along the breath, then every 2 ticks
  // (+0x178) a piercing projectile (anim 6, power 0x100) at a random point 0x230 ahead,
  // 0x60-0x80 below the mouth, +-0x40 in z; the roar fades (and ducks while paused).
  void attack2() override {
    BattleGlobals& g = bg();
    anim = 5;
    loop_limit = 0;
    animate(static_cast<u32>(attack_recovery));
    if (g.flags == 0) {
      if (0 < frame) {
        if (counters[2] == 0) {
          counters[2] = static_cast<s32>(bsfx_play_at(0x2D, this));
          const s32 a[3] = {bobj_front_x(this, 0x140), pos[1] - 0x14000, pos[2]};
          if (Shot* r = effect_spawn_sparks_at(a, 0x14, 0x80, 0, 3, 0x78, 3, 3, 0, 1)) r->vel[2] = 0x400;
          const s32 b[3] = {bobj_front_x(this, 0x1E0), pos[1] + 0xC000, pos[2]};
          if (Shot* r = effect_spawn_sparks_at(b, 0x14, 0x80, 0, 5, 0x78, 3, 3, 0x1E, 1)) r->vel[2] = 0x400;
          const s32 c[3] = {bobj_front_x(this, 0x280), pos[1] + 0x28000, pos[2]};
          if (Shot* r = effect_spawn_sparks_at(c, 0x14, 0x80, 0, 7, 0x78, 3, 3, 0x3C, 1)) r->vel[2] = 0x400;
        }
        counters[1] += g.frame_ticks;
        while (1 < counters[1]) {
          counters[1] -= 2;
          ShotDesc* d = shot_desc_init(6);
          const s32 from[3] = {bobj_front_x(this, 0x100), pos[1] - 0x18000, pos[2]};
          d->speed = 0x18;
          d->point[0] = bobj_front_x(this, 0x230);
          d->point[1] = from[1] + rng_between(0x60000, 0x80000);
          const s32 dz = rng_between(-0x40000, 0x40000);
          d->life = 0x1A;
          d->flags = 6;
          set_hi16(d->element, 1);  // +0x72: pierces
          d->power = 0x100;
          d->point[2] = from[2] + dz;
          shot_spawn_projectile(this, from, d, this);
        }
      }
      counters[0] += g.frame_ticks;
      if (100 < counters[0]) {
        action_kind = 0;
        begin_action();
        return;
      }
    }
    if (counters[2] == 0) return;
    sound_handle_fade_out(static_cast<u32>(counters[2]), 0x3C, 8);
    if (g.flags != 0) sound_handle_duck(static_cast<u32>(counters[2]), 6);
  }

  // 0x80088848 [25]: once: 20 beams out of the body, a spark ring at its centre and one on
  // the ground, sfx 0x34 / 0x21 / 0x22; then the usual fade.
  void state_dying() override {
    if (counters[0] == 0) {
      for (int i = 0; i < 0x14; ++i) {
        const s32 c[3] = {pos[0], pos[1] + size[1] * -0x2000, pos[2]};
        const Vec4 d = vec_random_around(c, 0x500);
        const s32 from[3] = {pos[0], pos[1] + size[1] * -0x2000, pos[2]};
        effect_spawn_beam_at(from, d.data(), 0xB, 0x140, 0x10, 0xC000, 0x400, 0);
      }
      const s32 c[3] = {pos[0], pos[1] + size[1] * -0x2000, pos[2]};
      effect_spawn_sparks_at(c, 0x18, 0xE0, 0, 0x40, 0x3C, 7, 4, 0x100, 0);
      const s32 ground[3] = {pos[0], bobj_ground_height(this), pos[2]};
      effect_spawn_sparks_at(ground, 0x18, 0x140, 0x30, 0x18, 0x5A, 9, 0, 0x80, 0);
      bsfx_play_at(0x34, this);
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
      counters[0] = 1;
    }
    EnemyUnit::state_dying();
  }

  // 0x80088a10 [36]: the dtor (EnemyUnit vtable, unit_destroy).
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_synard_create() { return std::make_unique<EnemySynard>(); }

}  // namespace ss::game::battle
