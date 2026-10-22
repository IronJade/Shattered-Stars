// The Gerel module: block 205e19e0083c (1864 bytes, slot B linked at 0x80089f18), vtable
// 0x8008a538 (the factory stores it over the EnemyUnit one: no constructor), monster types
// 0x10 Otif, 0x11 Gerel, 0x12 Petrogerell, 0x83 Bloodgerell (0x26c-byte objects; 29
// encounters, 43 44 48 49 52 53 ...). A jelly: it spits a blob, and now and then splits in
// two (attack 3: a copy in a free enemy slot, each with half the HP). The Bloodgerell heals
// half of every hit it takes.

#include <algorithm>
#include <memory>
#include <vector>

#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

// The jelly types whose split blocks another (0x10-0x12, 0x1f-0x21, 0x81-0x83, 0x8a-0x8b).
bool is_jelly(s16 t) {
  if (t < 0x22) return t >= 0x1F || (t < 0x13 && t >= 0x10);
  if (t <= 0x80) return false;
  if (t < 0x84) return true;
  return t < 0x8C && t >= 0x8A;
}

struct EnemyGerel : EnemyUnit {
  // A copy's own stats block (the console's __builtin_new(0x60)); unused by the originals.
  std::array<u8, 0x60> split_stats_{};

  // 0x80089f18 [26]: the spit (2, from 700 units or 12000 / 32768, speed 0x1b8); else with
  // 12000 / 32768 (not type 0x81) the split (3), when an enemy slot is free, no jelly is
  // splitting (state 11) and 0x90 units are clear on both sides; else the base choice.
  void choose_target() override {
    if (enemy_try_approach(this, 700, 12000, 2, 0x1B8) != 0) return;
    if (rng15() < 12000 && static_cast<s16>(st()->id) != 0x81) {
      bool free_slot = false, splitting = false;
      for (EnemyUnit* u : bg().enemy_units) {
        if (u == nullptr) {
          free_slot = true;
          continue;
        }
        if (is_jelly(static_cast<s16>(u->st()->id)) && u->action == 11) splitting = true;
      }
      if (free_slot && !splitting) {
        const s32 left[3] = {-0x90000, 0, 0};
        const s32 right[3] = {0x90000, 0, 0};
        bool clear = false;
        if (unit_check_move(this, left, 7) == nullptr) clear = unit_check_move(this, right, 7) == nullptr;
        if (clear) {
          action_kind = 3;
          begin_action();
          counters[1] = 0;
          return;
        }
      }
    }
    EnemyUnit::choose_target();
  }

  // 0x8008a0d8 [28]: one blob (projectile anim 6, speed 0x14, life 0x24) past frame 5,
  // aimed level 0x80 units further ahead.
  void attack2() override {
    anim = 5;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame < 6) return;
    ShotDesc* d = shot_desc_init(6);
    const s32 from[3] = {bobj_front_x(this, 0x80), pos[1] - 0x90000, pos[2]};
    d->speed = 0x14;
    d->point[0] = from[0] + bobj_facing_fx(this, 0x80);
    d->point[1] = from[1];
    d->flags = 0x100006;
    d->life = 0x24;
    d->point[2] = from[2];
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }

  // 0x8008a228 [29]: the split (anim 7, animated with the battle flags as its argument: the
  // original's quirk). At the end it goes idle and copies itself whole into the first free
  // enemy slot (own stats block, the same sprite set), registers the copy (priority 2),
  // moves both 0x90 units apart and halves both HPs.
  void attack3() override {
    BattleGlobals& g = bg();
    anim = 7;
    if (static_cast<s16>(animate(g.flags)) != -1) return;
    action_kind = 0;
    begin_action();
    for (int i = 0; i < 8; ++i) {
      if (g.enemy_units[i] != nullptr) continue;
      auto copy = std::make_unique<EnemyGerel>();  // enemy_create + this module's vtable
      EnemyGerel* c = copy.get();
      g.enemy_units[i] = c;
      *c = *this;  // the 0x26c-byte copy (the vtable stays the copy's)
      std::copy_n(stats, 0x60, c->split_stats_.begin());
      c->stats = c->split_stats_.data();
      c->slot = static_cast<u8>(i);
      g.enemy_stats[i] = c->stats;
      task_register(c, 2);
      c->pos[0] -= 0x90000;
      pos[0] += 0x90000;
      c->st()->hp /= 2;
      ++g.enemy_count;
      st()->hp /= 2;
      enemy_adopt(std::move(copy));  // (battle_free_all frees it with the other enemies)
      return;
    }
  }

  // 0x8008a48c [32]: the Bloodgerell (0x83) heals half of the last damage it took (+0x190).
  int pre_think() override {
    if (static_cast<s16>(st()->id) == 0x83 && last_damage != 0) {
      const s32 heal = -last_damage / 2;
      damage_popup_spawn(this, heal, -0x2000, 0);
      unit_damage_hp(this, heal);
      last_damage = 0;
    }
    return 0;
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_gerel_create() { return std::make_unique<EnemyGerel>(); }

}  // namespace ss::game::battle
