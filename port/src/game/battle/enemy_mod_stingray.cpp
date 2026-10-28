// The Stingray module: block 99b91f258892 (2392 bytes, slot B linked at 0x80089f18), vtable
// 0x8008a748, monster types 0x3b Stingray, 0x3c Flyingray, 0x3d Raystinger, 0x97 Cavesting,
// 0x98 Gloomsting (0x26c-byte objects; 14 encounters, 34 36 73 74 76 82 ...). A hovering ray:
// a swoop at the target's head (attack 2) and tail stings (attack 3: Shot kind 0x26, one; the
// 0x97+ types three) that petrify (status bit 2) unless resisted or warded, the late types'
// also hurting.
//
// Shot kind 0x26 has its handler in this block (0x8008a30c): the #2129 kind table
// (0x8006fea8) entry 0x26 holds that slot-B link address, so it is valid only while this
// block is loaded. The port spawns it as a callback Shot (kind 0x33) instead.

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/boot/rng.h"

namespace ss::game::battle {

void enemy_mod_stingray_shot_k26(Shot* s);  // 0x8008a30c (Shot kind 0x26 while the block is loaded)

namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

// The sting's afterimage lives in the Shot's +0x130; the port keeps p130 as the "bound"
// flag (nonzero when one was made; shot_reset clears it) and the pointer here.
std::unordered_map<const Shot*, Afterimage*>& sting_trails() {
  static std::unordered_map<const Shot*, Afterimage*> trails;
  return trails;
}

struct EnemyStingray : EnemyUnit {
  // 0x80089f18: enemy_create, the module's vtable, hit sounds 0x4c / 0x4d, hovers (+0x160 = 8).
  EnemyStingray() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4D;
    hover_amp = 8;
  }

  // 0x80089f68 [26]: the swoop (2, from 800 units or a quarter of the time, speed 0x200), the
  // stings (3, from 1000 or an eighth, 0x280), else the base choice.
  void choose_target() override {
    if (enemy_try_approach(this, 800, 0x2000, 2, 0x200) != 0) return;
    if (enemy_try_approach(this, 1000, 0x1000, 3, 0x280) != 0) return;
    EnemyUnit::choose_target();
  }

  // 0x80089fd4 [28]: the swoop: once, a velocity of 0x20 toward the target's head (at most
  // 300 units up; straight down 0x20 without a target); it flies until a wall, 100 units
  // above the ground or a landed hit ends the attack.
  void attack2() override {
    BattleGlobals& g = bg();
    anim = 5;
    if (counters[0] == 0) {
      counters[0] = 1;
      if (target == nullptr) {
        vel[1] = 0x20000;
      } else {
        s32 aim[3] = {target->pos[0], target->pos[1] - (target->size[1] << 14), target->pos[2]};
        if (aim[1] < -0x12C000) aim[1] = -0x12C000;
        const Vec4 v = vec_velocity_toward(pos, aim, 0x20);
        vel[0] = v.x;
        vel[1] = v.y;
        vel[2] = v.z;
        extra34 = v.w;
      }
    }
    bool stop = false;
    if (g.flags == 0) {
      if (unit_try_move_scaled(this, vel, 7) == kWallBlock) {
        stop = true;
      } else {
        bobj_move(this, vel);
      }
      const s32 floor = bobj_ground_height(this) - 0x64000;
      if (floor < pos[1]) {
        pos[1] = floor;
        stop = true;
      }
    }
    loop_limit = 0;
    animate(static_cast<u32>(attack_recovery));
    if (g.flags == 0) {
      Unit* units[8] = {};
      Vec4 points[8];
      const int n = collider_attack_overlaps(this, units, points, 1);
      if (n != 0 && hit_landed == 0) {
        unit_attack_targets(this, n, units, points, 0x100, 9999, 0);
        stop = true;
      }
    }
    if (!stop) return;
    action_kind = 0;
    begin_action();
  }

  // 0x8008a1d0 [29]: anim 6; past frame 3 a sting (Shot kind 0x26, sfx 0x3a) from 3 half
  // widths in front, one per frame up to 1 (types 0x97+: 3).
  void attack3() override {
    anim = 6;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0) return;
    const s32 max = static_cast<s16>(st()->id) < 0x97 ? 1 : 3;
    if (counters[1] >= max || frame < 4) return;
    bsfx_play_at(0x3A, this);
    // The module's own kind 0x26 (handler 0x8008a30c): run as a callback Shot, the port's
    // Shot switch dispatching only the resident kinds.
    if (Shot* s = shot_spawn_callback(enemy_mod_stingray_shot_k26)) {
      shot_place_at_unit(s, this, size[0] * 3, 0, 0);
      s->owner = this;
      s->target = target;
      bobj_share_sprite(this, s);
    }
    ++counters[1];
  }
};

}  // namespace

// 0x8008a30c (Shot kind 0x26): the sting (anim 7 of the owner's set, box 0x24). Once: a
// velocity of 0x20 at the target's centre and a grey afterimage of 6. It flies until it
// leaves the arena (then fades) or meets the party: per unit met the 0x97+ types' owner hits
// all of them (power 0x100), then with the owner's weapon chance (+0x131 / 256) and no
// status-0x200 ward the victim is petrified (status bit 2, sfx 0x31, debris), unless an
// accessory saves it (status 0x20000: 70 %, item 0x5b; status 0x1000: half, item 0x8c);
// otherwise a "miss" icon and sfx 0x4f. Then it ends.
void enemy_mod_stingray_shot_k26(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 7;
  shot_draw_with_shadow(s, 5, 0xFFFFFFFF);
  s->size[2] = 0x24;
  s->size[1] = 0x24;
  s->size[0] = 0x24;
  if (s->p128 == 0) {
    const Unit* t = s->target;
    s->p128 = 1;
    const s32 aim[3] = {t->pos[0], t->pos[1] - (t->size[1] << 13), t->pos[2]};
    const Vec4 v = vec_velocity_toward(s->pos, aim, 0x20);
    s->vel[0] = v.x;
    s->vel[1] = v.y;
    s->vel[2] = v.z;
    s->extra34 = v.w;
    Afterimage* a = afterimage_create(s, 6, 0x808080, 1);
    sting_trails()[s] = a;
    s->p130 = a != nullptr ? 1 : 0;
  }
  // (shot_fade_out has already set +0x12c once the state passes 2, so the second clause
  // never holds; kept as written.)
  if (shot_fade_out(s) != 0 || (kStateOver <= g.state && g.end_countdown < 0x18 && s->p12c == 0)) {
    shot_skill_camera_end(s);
  } else {
    if (g.flags != 0) return;
    if (s->p12c == 0 && bobj_outside_arena(s)) {
      s->p12c = 1;
      return;
    }
    bobj_move(s, s->vel);
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(s, units, points, 1);
    if (n == 0 || s->hit_landed != 0) return;
    Unit* owner = s->owner;
    for (int i = 0; i < n; ++i) {
      Unit* u = units[i];
      if (static_cast<s16>(owner->st()->id) >= 0x97) unit_attack_targets(owner, n, units, points, 0x100, 9999, 0);
      bool petrify = false;
      if (rng_range(0x100) < owner->weapon_chance) petrify = u->status_test(0, 0x200) == 0;
      bool saved = false;
      if (petrify) {
        if (u->status_test(0, 0x20000) != 0 && rng_range(100) < 0x46) {
          u->accessory_proc(0x5B, 0, 0x20000, 0xF);
          saved = true;
        } else if (u->status_test(0, 0x1000) != 0 && rng15() < 0x4000) {
          u->accessory_proc(0x8C, 0, 0x1000, 10);
          saved = true;
        }
      }
      if (petrify && !saved) {
        u->st()->status |= 2;
        u->jumping = 0;
        bsfx_play_at(0x31, u);
        const s32 spread[3] = {0x40000, 0x40000, 0x20000};
        effect_debris_burst(s->pos, spread, 8, 1, 0, 0x1000, 10);
      } else {
        effect_rise_icon(u, 0);
        bsfx_play_at(0x4F, u);
      }
    }
  }
  if (s->p130 != 0) {
    if (Afterimage* a = sting_trails()[s]) a->ending = 1;
  }
  shot_kill(s);
}

std::unique_ptr<EnemyUnit> enemy_mod_stingray_create() { return std::make_unique<EnemyStingray>(); }

}  // namespace ss::game::battle
