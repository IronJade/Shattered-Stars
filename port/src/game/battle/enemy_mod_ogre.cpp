// The ogres' enemy-code module: block b8893ccaab98 (500 bytes, slot A linked at 0x80088110),
// vtable 0x800881dc (the factory stores it over the EnemyUnit one; no constructor), monster
// types 0x77 Alen-Tax, 0x78 Varmillion, 0xa7 Ogre, 0xa8 Ghast, 0xa9 Brigandogre (0x26c-byte
// objects), encounters 151 152 191 194 203 208 ... (10). Up close, half the time a smash
// with double ATK (attack 2); otherwise the default behaviour.

#include "game/battle/enemy_classes.h"

namespace ss::game::battle {
namespace {

struct EnemyOgre : EnemyUnit {
  // 0x80088110 [26]: the smash (attack 2) from 0xf0 away (or half the time nearer), else
  // the default choice.
  void choose_target() override {
    if (enemy_try_approach(this, 0xF0, 0x4000, 2, 0x78) == 0) EnemyUnit::choose_target();
  }

  // 0x8008815c [28]: the smash: anim 5, the melee step with double battle ATK.
  void attack2() override {
    anim = 5;
    Stats* s = st();
    const s16 atk = s->atk[2];
    s->atk[2] = static_cast<s16>(atk << 1);
    enemy_attack_step(this);
    s->atk[2] = atk;
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_ogre_create() { return std::make_unique<EnemyOgre>(); }

}  // namespace ss::game::battle
