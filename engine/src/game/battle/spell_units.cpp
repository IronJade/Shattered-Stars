// The units' spell states: the party's chant / release / cast (PartyUnit slots 3, 11, 40,
// 41) and the monsters' choice of a spell, chant, release and cast (enemy_try_cast,
// enemy_state_chant, EnemyUnit slots 11 and 35). parts/p1.md, p2.md.

#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/camera.h"
#include "game/battle/enemy_support.h"
#include "game/battle/spells.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }

// The spell camera on a caster whose big spell (one with effect data) is about to go off,
// unless another of its side is already releasing one.
void spell_camera_on(Unit* u) {
  BattleGlobals& g = bg();
  camera_end_scheduled();
  camera_request_mode(g.camera, 7, u);
  g.camera_special = u;
  u->camera_focus = 1;
}

}  // namespace

// ---- The party ----

const u8* pc_spell_list(const Unit* u) {
  // 0x8003a5d4 (fighters: no list; the original returns a small number there).
  switch (u->st()->klass) {
    case 9: return battle_data(0x8006B1B8);   // Rena
    case 10: return battle_data(0x8006B1D0);  // Leon
    case 11: return battle_data(0x8006B1E4);  // Celine
    case 12: return battle_data(0x8006B1FC);  // Noel
    default: return nullptr;
  }
}

int pc_can_cast_spell(PartyUnit* u, int spell) {
  // 0x8003b618: the spell is in the member's class list, learned (record +0x3c + index
  // above 0) and affordable.
  const u8* list = pc_spell_list(u);
  if (list == nullptr || u->record == nullptr) return 0;
  const int count = battle_data(0x8006B1ABu + u->st()->klass) != nullptr ? *battle_data(0x8006B1ABu + u->st()->klass) : 0;
  for (int i = 0; i < count; ++i) {
    if (static_cast<s8>(u->record[0x3C + i]) > 0 && list[i] == spell)
      return unit_spell_mp_cost(u, list[i] - 1) <= u->st()->mp ? 1 : 0;
  }
  return 0;
}

int PartyUnit::can_cast() {
  // 0x80037a80 pc_can_cast [3]: casters (class above 8), not busy, not silenced (status
  // 2 / 0x80000000), then unit_can_cast.
  if (st()->klass < 9 || busy != 0) return 0;
  if (status_test(2, static_cast<u32>(0x80000000)) != 0) return 0;
  return unit_can_cast(this);
}

void PartyUnit::state_cast() {
  // 0x80039198 pc_state_cast [40] (state 0xe): the chant animation until the cast time is
  // over, then slot 12 (action kind 6: the spell's name and state 0xf).
  BattleGlobals& g = bg();
  model_default();
  anim = 9;
  animate(0);
  camera_follow_start();
  if (g.flags != 0) return;
  const s32 t = static_cast<u16>(cast_time) - g.frame_ticks;
  cast_time = static_cast<s16>(t);
  if (static_cast<s32>(static_cast<u32>(t) << 16) < 1) begin_action();
}

void PartyUnit::state_spell_release() {
  // 0x80039ef4 pc_state_spell_release [41] (state 0xf).
  BattleGlobals& g = bg();
  if (unit_update_casting(this) != 0) return;
  if (g.flags == 0 && (g.controlled_slot == slot || (spell_needs_data(spell) != 0 && counters[0] < -0x18))) {
    int i = 0;
    for (; i < 4; ++i) {
      PartyUnit* o = g.party_units[static_cast<size_t>(i)];
      if (!unit_is_down(o) && o->action == 0xF && i != slot) break;
    }
    if (i == 4 && g.config[0x4A] == 0) spell_camera_on(this);
  }
  model_default();
  anim = 10;
  animate(0);
}

void PartyUnit::release_spell() {
  // 0x8003a088 pc_cast_spell [11]: the spell's proficiency (record +0x8c + 2 * its place in
  // the class list; +100 with status 2 / 0x80000; max 999) is the power; the hook; the
  // proficiency grows; 120 ticks before the AI casts again; recovery 60.
  const u8* list = pc_spell_list(this);
  int i = 0;
  for (; i < 0x20; ++i)
    if (list != nullptr && list[i] == spell) break;
  s16 prof = 0;
  if (record != nullptr) std::memcpy(&prof, record + 0x8C + i * 2, 2);
  s32 power = prof;
  if (status_test(2, 0x80000) != 0) power += 100;
  if (999 < power) power = 999;
  spell_cast(spell, this, target, power);
  spell_hook(spell);
  if (i < 0x20 && record != nullptr) {
    s16 p;
    std::memcpy(&p, record + 0x8C + i * 2, 2);
    if (p < 999) {
      ++p;
      std::memcpy(record + 0x8C + i * 2, &p, 2);
    }
  }
  wait_after_spell = 0x78;
  action_kind = 0;
  recovery_gauge = 0x3C;
  begin_action();
  target = nullptr;
  spell = 0;
}

// ---- The monsters ----

void enemy_try_cast(EnemyUnit* e) {
  // 0x8003e3b0: when it can cast and the battle is on: the target (the last attacker in
  // the counter window, else a random member), one of its four spells (each a 1/2 roll in
  // turn); a spell for its own side goes to a random monster (none below half HP: no cast).
  BattleGlobals& g = bg();
  if (e->can_cast() != 1 || g.state != kStateFighting) return;
  if (e->counter_window == 0) {
    e->target = enemy_random_party_member(e);
  } else {
    e->target = e->last_attacker;
    e->counter_window = 0;
    e->last_attacker = nullptr;
  }
  int spell = 0;
  for (int i = 0; i < 4; ++i) {
    if (rng15() < 0x4000) {
      if (e->spells[i] != 0) spell = e->spells[i];
      break;
    }
  }
  if (spell == 0) return;
  if (spell_target_class(spell) != 0) {
    EnemyUnit* ally = nullptr;
    int tries = 0;
    for (; tries < 200; ++tries) {
      ally = g.enemy_units[rng_range(8)];
      if (!unit_is_down(ally)) break;
    }
    if (tries == 200) return;
    e->target = ally;
    const Stats* s = ally->st();
    if (s->hp_max / 2 < s->hp) return;
  }
  e->command = 4;
  e->spell = static_cast<u8>(spell);
}

void enemy_state_chant(EnemyUnit* e) {
  // 0x8003e540 (state 0xe): anim 9 while the cast time runs down, then slot 12.
  BattleGlobals& g = bg();
  e->anim = 9;
  e->animate(0);
  if (g.flags != 0) return;
  const s32 t = static_cast<u16>(e->cast_time) - g.frame_ticks;
  e->cast_time = static_cast<s16>(t);
  if (static_cast<s32>(static_cast<u32>(t) << 16) < 1) e->begin_action();
}

void EnemyUnit::state_cast() {
  // 0x8003e5dc [35] (state 0xf): unit_update_casting; the spell camera for a big spell
  // when no other monster is releasing one.
  BattleGlobals& g = bg();
  if (unit_update_casting(this) != 0) return;
  if (g.flags == 0 && spell_needs_data(spell) != 0 && counters[0] < -0x18) {
    int i = 0;
    for (; i < 8; ++i) {
      EnemyUnit* o = g.enemy_units[static_cast<size_t>(i)];
      if (!unit_is_down(o) && o->action == 0xF && i != slot) break;
    }
    if (i == 8) spell_camera_on(this);
  }
  anim = 10;
  animate(0);
}

void EnemyUnit::release_spell() {
  // 0x8003e720 [11]: spell_cast at its target with power stats +0x26; recovery 60.
  spell_cast(spell, this, target, st()->battle_level);
  action_kind = 0;
  recovery_gauge = 0x3C;
  begin_action();
  spell = 0;
}

}  // namespace ss::game::battle
