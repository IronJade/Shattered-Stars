// Berle's enemy-code module: block d449d4b12fc8 (1788 bytes, slot A at 0x80088110-0x8008880c),
// vtable 0x800886e4, ctor 0x80088110 (0x26c-byte objects); monster types 0xc4 / 0xc5 Berle
// (encounters 520 535). A swordsman with afterimages: the slash (attack 1), a combo whose
// landed hit chains into a slash half the time (attack 2), and the flame aura (attack 3:
// +0x164 = 480 ticks, a red fire column round it drawn every frame meanwhile).

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/sprite.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

struct EnemyBerle : EnemyUnit {
  // 0x80088110: enemy_create and the module's vtable.

  // 0x80088148 [32]: while the aura runs (+0x164) a 12-sided fire column round the body
  // (radius 5 x its half width, 5 x its half height tall, from that height down), fading
  // over its last 32 ticks.
  int pre_think() override {
    const s32 t = special_timer;
    if (t == 0) return 0;
    const s32 r = size[0] * 5;
    const s32 level = t < 0x21 ? t << 3 : 0x100;
    const s32 from[3] = {pos[0], pos[1] - ((size[1] * 5) << 12), pos[2]};
    const s32 dir[3] = {0, 0x10000, 0};
    const u32 c0 = scale_colour(0xFF4000, level);
    const u32 c1 = scale_colour(0x600000, level);
    draw_beam(from, dir, c0, c1, size[1] * 5, r, r, 0xC);
    return 0;
  }

  // 0x80088240 [26]: from afar the aura (message 0x271d, sfx 10) when it is not up; else
  // (also when the approach started with the aura up) the combo, else the usual choice.
  void choose_target() override {
    if (enemy_try_approach(this, 600, 0x1000, 3, 0x60) != 0 && special_timer == 0) {
      battle_message_show(0x271D);
      bsfx_play_at(10, this);
      action_kind = 3;
      begin_action();
      return;
    }
    if (enemy_try_approach(this, 300, 0x4000, 2, 0x60) == 0) EnemyUnit::choose_target();
  }

  // The swings' dark afterimage (4 copies, colour 0x60).
  void ensure_afterimage() {
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, 4, 0x60, 1);
  }
  void drop_afterimage() {
    if (effects[0] != nullptr) effects[0]->ending = 1;
    effects[0] = nullptr;
  }

  // 0x800882f8 [27]: the slash (once through anim 4); the afterimage ends on a landed hit.
  void attack1() override {
    loop_limit = 1;
    anim = 4;
    ensure_afterimage();
    if (enemy_attack_step(this) != 0) drop_afterimage();
  }

  // 0x8008838c [28]: the combo (once through anim 5) with flashes (anim 8) at the points
  // hit; once a hit has landed, every frame (paused or not) rng_next15 < 0x4000 chains it
  // into the slash (message 0x271c).
  void attack2() override {
    loop_limit = 1;
    anim = 5;
    ensure_afterimage();
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      drop_afterimage();
      return;
    }
    if (bg().flags == 0) {
      Unit* units[8] = {};
      Vec4 points[8];
      const int n = collider_attack_overlaps(this, units, points, 1);
      if (n != 0 && hit_landed == 0 && unit_attack_targets(this, n, units, points, 0x100, 9999, 0) != 0) {
        for (int i = 0; i < n; ++i) {
          if (Sprite* sp = unit_spawn_sprite_ex(this, 8, 1)) sprite_set_pos_v(sp, points[i].data());
        }
      }
    }
    if (hit_landed == 0) return;
    if (rng15() < 0x4000) {
      battle_message_show(0x271C);
      action_kind = 1;
      begin_action();
    }
  }

  // 0x80088588 [29]: the aura: 480 ticks of +0x164, anim 6 three times through.
  void attack3() override {
    special_timer = 0x1E0;
    loop_limit = 3;
    anim = 6;
    enemy_attack_step(this);
  }

  // 0x800885bc [25]: once, 48 beams burst from its middle (sfx 0x21, 0x22); then the usual
  // fade.
  void state_dying() override {
    if (counters[0] == 0) {
      for (int i = 0; i < 0x30; ++i) {
        const s32 c[3] = {pos[0], pos[1] + size[1] * -0x2000, pos[2]};
        const Vec4 r = vec_random_around(c, 0x500);
        const s32 c2[3] = {pos[0], pos[1] + size[1] * -0x2000, pos[2]};
        effect_spawn_beam_at(c2, r.data(), 5, 0x140, 0x18, 0xA000, 0x400, 0);
      }
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
      counters[0] = 1;
    }
    EnemyUnit::state_dying();
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_berle_create() { return std::make_unique<EnemyBerle>(); }

}  // namespace ss::game::battle
