// The hounds' enemy-code module: block 944dea2287e7 (1980 bytes, slot A linked at 0x80088110),
// vtable 0x800887a4 (the factory stores it over the EnemyUnit one; no constructor), monster
// types 0x2f Hounddog, 0x30 Blackhound, 0x31 Hellhound, 0x93 Foriger (0x26c-byte objects),
// encounters 62-64 89-91 ... (20). A spit from afar (attack 4) and leaping bites: the
// pounce (2) and, for the Hellhound, the hanging bite (3); the Foriger chains pounce ->
// hanging bite -> spit with an afterimage.
// (The catalog's stale rows 0x8007fa4c-0x80080080 are a different build of this block.)

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/shot.h"

namespace ss::game::battle {
namespace {

struct EnemyHounddog : EnemyUnit {
  s16 type() const { return static_cast<s16>(st()->id); }

  // 0x80088110 [26]: far off the spit (attack 4); the later hounds pounce (2), the
  // Hellhound also bites from the air (3); else the default choice.
  void choose_target() override {
    if (enemy_try_approach(this, 1000, 0x2000, 4, 0x200) != 0) return;
    if (0x2F < type() && enemy_try_approach(this, 500, 0x1800, 2, 0x100) != 0) return;
    if (type() == 0x31 && enemy_try_approach(this, 600, 0x2000, 3, 0x100) != 0) return;
    EnemyUnit::choose_target();
  }

  // 0x800881d0: the leap the bites share. The first call (+0x174 clear) sets off forward
  // at 0xc and up at 0xe; x and y move separately (x only where free; y stops the leap on
  // anything), gravity 0x1000 a tick. 1 when it ended (landed, blocked, or the animation
  // reached its loop limit): back to idle, except the Foriger, whose attacks chain.
  int leap() {
    if (counters[0] == 0) {
      counters[0] = 1;
      vel[1] = -0xE000;
      vel[0] = bobj_facing_fx(this, 0xC);
    }
    s32 d[3] = {vel[0], 0, 0};
    airborne = 1;
    jumping = 1;
    bool ended = false;
    if (bg().flags == 0) {
      if (unit_try_move_scaled(this, d, 7) == nullptr) bobj_move(this, d);
      d[0] = 0;
      d[1] = vel[1];
      if (unit_try_move_scaled(this, d, 7) != nullptr) {
        ended = true;
      } else {
        bobj_move(this, d);
        const s32 ground = bobj_ground_height(this);
        if (ground < pos[1]) {
          pos[1] = ground;
          airborne = 0;
          ended = true;
        } else {
          vel[1] += bg().frame_ticks * 0x1000;
        }
      }
    }
    if (!ended && static_cast<s16>(animate(static_cast<u32>(attack_recovery))) != -1) return 0;
    jumping = 0;
    if (type() != 0x93) {
      action_kind = 0;
      begin_action();
    }
    return 1;
  }

  // The bite's hit check: the frame's attack boxes against the party; 'pin' gives every
  // member hit 8 ticks of freeze (+0x154) first. Returns the count hit (0 when the landed
  // flag holds it back).
  int bite(bool pin) {
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n == 0 || hit_landed != 0) return 0;
    if (pin) {
      counters[1] = 1;
      for (int i = 0; i < n; ++i) units[i]->super_armour = 8;
    }
    unit_attack_targets(this, n, units, points, 0x100, 9999, 0);
    return n;
  }

  // 0x8008834c [28]: the pounce (anim 5, the leap biting all the way). The Foriger leaves
  // an afterimage and, when the leap ends, follows a landed bite with the hanging bite (3).
  void attack2() override {
    anim = 5;
    loop_limit = 0;
    if (effects[0] == nullptr && type() == 0x93) effects[0] = afterimage_create(this, 4, 0x208060, 1);
    if (leap() != 0 && type() == 0x93) {
      action_kind = hit_landed != 0 ? 3 : 0;
      begin_action();
      return;
    }
    if (bg().flags != 0) return;
    bite(false);
  }

  // 0x800884d4 [29]: the hanging bite (anim 6): biting on even frames, the landed flag
  // cleared on odd ones; once it bit (+0x178) it stops in the air (vel y 0 each frame,
  // x / z cleared) and pins whoever it holds, until the animation ends. The Foriger then
  // spits (4).
  void attack3() override {
    anim = 6;
    if (counters[1] != 0) vel[1] = 0;
    if (leap() != 0) {
      if (type() != 0x93) return;
      action_kind = 4;
      begin_action();
      return;
    }
    if (bg().flags == 0) {
      if ((frame & 1) == 0 && bite(true) != 0) {
        vel[0] = 0;
        vel[2] = 0;
      }
    }
    if ((frame & 1) != 0) hit_landed = 0;
  }

  // 0x80088644 [30]: one homing spit (anim 8, speed 0x24, 30 ticks) past frame 0.
  void attack4() override {
    anim = 7;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame <= 0) return;
    bsfx_play_at(0x1F, this);
    ShotDesc* d = shot_desc_init(8);
    d->speed = 0x24;
    d->life = 0x1E;
    d->flags = 0x100006;
    d->target = target;
    const s32 from[3] = {bobj_front_x(this, 0x40), pos[1] - 0x3C000, pos[2]};
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_hounddog_create() { return std::make_unique<EnemyHounddog>(); }

}  // namespace ss::game::battle
