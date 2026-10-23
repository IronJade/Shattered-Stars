// EnemyUnit behaviour (vtable 0x8006b908): enemy_think's state machine (0x8003cac0),
// the idle AI that rolls the five behaviour odds every 8 ticks, target choice, approach,
// the attack slots with melee hit detection, reactions and dying. The spell states are in
// spell_units.cpp, the encounter packs' classes in enemy_mod_*.cpp.
// parts/p2.md "EnemyUnit".

#include <cstring>

#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/spells.h"
#include "game/battle/render.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

void todo(const char* what) {
  runtime::Runtime::get().log_once(std::string("battle-enemy-") + what, std::string("battle: not ported yet: ") + what);
}

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }

// config +0x182 == 1: the monster takes the party's best numbers (a test rule).
void enemy_copy_party_stats(EnemyUnit* e) {
  BattleGlobals& g = bg();
  s16 atk = 0, eva = 0, acc = 0, def = 0;
  s32 hp = 0;
  for (const PartyUnit* u : g.party_units) {
    if (unit_is_down(u)) continue;
    const Stats* s = u->st();
    if (atk < s->atk[2]) atk = s->atk[2];
    if (eva < s->eva[2]) eva = s->eva[2];
    if (acc < s->acc[2]) acc = s->acc[2];
    if (def < s->def[2]) def = s->def[2];
    if (hp < s->hp_max) hp = s->hp_max;
  }
  Stats* s = e->st();
  s->hp = s->hp_max;
  s->acc[1] = s->acc[2] = static_cast<s16>(acc + 0x100);
  s->eva[1] = s->eva[2] = static_cast<s16>(eva + 0xE0);
  if (hp < 0) hp += 3;
  const s16 at = static_cast<s16>((hp >> 2) + def * 2);
  s->atk[1] = s->atk[2] = at;
  const s16 df = static_cast<s16>(atk * 2 - 8);
  s->def[1] = s->def[2] = df;
  for (int i = 0; i < 10; ++i) s->element[i] = 4;
  s->resist[0] = s->resist[1] = 0xFF;
}

}  // namespace

void Unit::update_status_effects() { unit_update_status_effects(this); }

void enemy_think(EnemyUnit* e) {
  BattleGlobals& g = bg();
  enemy_debug_trace(e);  // SS_ENEMY_TRACE (enemy_support.h)
  if (g.effects_suppressed != 0) return;
  if (e->intro_voice != 0 && g.state != kStateLoading) {
    g.intro_voice_gate = 8;
    --g.special_voice_set;
    const int variant = static_cast<int>(rng_range(e->voice_variants));
    e->voice_handle = static_cast<s32>(voice_play_entry(e->intro_voice + variant, nullptr, 0));
    e->intro_voice = 0;
  }
  if (e->st()->id == 0x94 && e->animset != nullptr) e->animset->layout = e->model_layout + 0x44;
  if (e->st()->id == 0xCF && e->animset != nullptr) e->animset->layout = e->model_layout + 0xC0;
  if (g.state < kStateFighting) {
    e->anim = 0;
    e->animate(0);
    return;
  }
  if (e->voice_handle != 0) {
    e->special_timer = 8;
    if (!sound_handle_active(static_cast<u32>(e->voice_handle))) e->voice_handle = 0;
    e->anim = 0;
    e->animate(0);
    return;
  }
  if (g.config[0x182] == 1) enemy_copy_party_stats(e);
  if (e->death != 0 || e->reaction != 0) unit_release_spell_data(e);
  if (e->death == 0) {
    if (e->reaction == 1) e->on_attack_start(0);
  } else {
    // A defeat: the kill counter, and the last one ends the battle.
    u32 kills;
    std::memcpy(&kills, g.config + 0x1C, 4);
    ++kills;
    std::memcpy(g.config + 0x1C, &kills, 4);
    if (g.state == kStateFighting && enemy_count_active(g.enemy_units) < 2 && (g.flags & 0x40) == 0)
      battle_victory_begin(e->last_attacker);
    if (e->death == 2) {
      e->removed = 1;
      --g.enemy_count;
      e->start_reaction();
      return;
    }
  }
  e->start_reaction();
  if (e->action == 6) {
    e->update_vanish();
    e->animate(0);
    return;
  }
  if (e->action == 5) {
    e->state_dying();
    return;
  }
  e->update_status_effects();
  if (e->pre_think() != 0) return;
  if (static_cast<u8>(e->action - 3) < 2) {
    e->counter_window = 6;
    e->attack_recovery = 0;
    e->anim = 2;
    e->state_hit();
    return;
  }
  const u8 status = e->st()->status;
  if (status & 0x40) {
    e->anim = 2;
    e->loop_limit = 0;
    e->animate(0);
    return;
  }
  if ((status & 0xE6) != 0 || (e->status_bits & 0x48) != 0 || e->super_armour != 0) {
    if (e->super_armour != 0) {
      const s32 t = e->super_armour - g.frame_ticks;
      e->super_armour = static_cast<s16>(t);
      if (static_cast<s16>(t) < 0) e->super_armour = 0;
    }
    e->animate(1);
    return;
  }
  switch (e->action) {
    case 1: e->state_approach(); return;
    case 2: {
      // enemy_state_hop 0x8003d470.
      if (g.flags == 0) {
        bobj_move(e, e->vel);
        e->leap_ticks -= g.frame_ticks;
        if (e->leap_ticks < 1) {
          std::memcpy(e->pos, e->step_dest, 16);
          e->begin_action();
          return;
        }
      }
      e->anim = 1;
      e->animate(0);
      return;
    }
    case 9: e->attack1(); return;
    case 10: e->attack2(); return;
    case 11: e->attack3(); return;
    case 12: e->attack4(); return;
    case 13: e->attack5(); return;
    case 14: enemy_state_chant(e); return;  // (spell_units.cpp)
    case 15: e->state_cast(); return;
    default: break;
  }
  // Idle (and unknown states).
  if (e->action == 0) spell_data_cancel_owner(e);
  const u8 cmd = e->command;
  if (cmd != 0) {
    if (cmd == 1) {
      e->action = 1;
      e->action_kind = 1;
      e->begin_move();
    } else if (cmd == 4) {
      unit_begin_cast(e);
      e->camera_focus = 0;
    }
    e->command = 0;
  }
  e->anim = 0;
  if (e->action == 0) {
    static const s32 kZero[3] = {0, 0, 0};
    Unit* blocker = unit_check_move(e, kZero, 7);
    if (blocker == nullptr) {
      e->ignore_units = 0;
      e->stuck_ticks = 0;
      if (g.flags == 0) enemy_ai_tick(e);
    } else {
      const s32 t = static_cast<u16>(e->stuck_ticks) + g.frame_ticks;
      e->stuck_ticks = static_cast<s16>(t);
      if (0x1D < static_cast<s16>(t)) {
        e->vel[1] = 0;
        e->vel[2] = 0;
        if (unit_start_sidestep(e, blocker) != 0) {
          stage_ground_height(e->step_dest[0] >> 12, e->step_dest[2] >> 12);
          e->vel[1] = 0;
          e->vel[0] /= 0x12;
          e->vel[2] /= 0x12;
        }
        e->ignore_units = 1;
      }
      if (g.flags == 0 && e->ignore_units != 0) enemy_ai_tick(e);
    }
  }
  e->animate(0);
}

int EnemyUnit::start_reaction() {
  if (family == EnemyFamily::Axe && reaction == 2 && st()->id == 0x89)
    todo("enemy_axe_knockback: Weirdaxe's debris Shots");
  return unit_start_reaction(this);
}

// EnemyUnit::release_spell [11] 0x8003e720: spell_units.cpp.

void EnemyUnit::begin_action() {
  if (target != nullptr) {
    facing[0] = pos[0] < target->pos[0] ? 0x64000 : static_cast<s32>(0xFFF9C000);
    facing[1] = 0;
    facing[2] = 0;
  }
  u8 next;
  switch (action_kind) {
    case 1: next = 9; break;
    case 2: next = 10; break;
    case 3: next = 11; break;
    case 4: next = 12; break;
    case 5: next = 13; break;
    case 6:
      battle_message_show(spell + 999);
      action = 0xF;
      counters[0] = 0;
      counters[3] = 0;
      animate(0);
      bobj_anim_reset(this);
      return;
    default:
      unit_release_spell_data(this);
      release_effects();
      action = 0;
      bobj_anim_reset(this);
      anim = 0;
      animate(0);
      return;
  }
  action = next;
  on_attack_start(action - 8);
  animate(0);
  bobj_anim_reset(this);
  loop_limit = 1;
  layer = 1;
  counters[0] = 0;
  counters[1] = 0;
}

int EnemyUnit::resist_roll(int kind, int chance) {
  if (boss != 0 && kind == 0) return 0;
  return unit_resist_roll(this, kind, chance);
}

void EnemyUnit::state_approach() {
  BattleGlobals& g = bg();
  if (airborne == 1 && flying == 0) {
    animate(0);
    return;
  }
  jumping = 0;
  if (g.flags == 0) {
    const int r = unit_move_toward_goal(this, 1);
    if (r != 0) {
      if (r == 2) action_kind = 0;
      if (action_kind != 0) {
        begin_action();
        return;
      }
      begin_action();
      if (odds[1] == 0) return;
      if (odds[1] <= rng_range(0x100)) return;
      enemy_try_cast(this);
      return;
    }
  }
  anim = 1;
  animate(0);
}

void EnemyUnit::state_hit() {
  anim = static_cast<u16>(action + anim - 3);
  if (bg().flags == 0 && unit_knockback_slide(this)) action = 0;
  animate(0);
}

void EnemyUnit::state_dying() {
  BattleGlobals& g = bg();
  animate(1);
  if ((g.flags & 0xBF) != 0) return;
  blend = 2;
  colour_mode = 1;
  if (counters[0] < 0x61) {
    rgb[0] = static_cast<u8>('`' - static_cast<u8>(counters[0]));
    rgb[1] = static_cast<u8>('`' - static_cast<u8>(counters[0]));
  } else {
    rgb[0] = 0;
    rgb[1] = 0;
  }
  rgb[2] = static_cast<u8>('`' - static_cast<u8>(counters[0] / 2));
  counters[0] += g.frame_ticks;
  if (0xC0 < counters[0] || (2 < g.state && g.end_countdown < 8)) {
    removed = 1;
    --g.enemy_count;
  }
  scale_x += 0x30;
  scale_y -= 0x18;
}

void EnemyUnit::choose_target() {
  Unit* p = enemy_find_provoker();
  Vec4 ap;
  if (p != nullptr) {
    target = p;
    if (unit_find_approach_point(this, &ap)) enemy_start_attack(this);
    return;
  }
  int idx = enemy_nearest_party_index(this);
  move_mode = 0;
  if (idx < 0) return;
  for (int k = 0; k < 4; ++k) {
    target = bg().party_units[idx & 3];
    if (!unit_is_down(target) && target->status_test(3, 4) == 0 && unit_find_approach_point(this, &ap)) {
      enemy_start_attack(this);
      return;
    }
    idx = static_cast<int>(rng_range(4));
  }
}

void EnemyUnit::attack1() {
  loop_limit = 1;
  anim = 4;
  enemy_attack_step(this);
}
void EnemyUnit::attack2() {
  anim = 5;
  enemy_attack_step(this);
}
void EnemyUnit::attack3() {
  anim = 6;
  enemy_attack_step(this);
}
void EnemyUnit::attack4() {
  anim = 7;
  enemy_attack_step(this);
}
void EnemyUnit::attack5() {
  anim = 8;
  enemy_attack_step(this);
}

void EnemyUnit::begin_move() {
  if (unit_can_reach_target(this, target, 0) == 1) {
    unit_begin_approach(this);
  } else {
    action = 0;
    action_kind = 0;
  }
}

// EnemyUnit::state_cast [35] 0x8003e5dc: spell_units.cpp.

void enemy_ai_tick(EnemyUnit* e) {
  BattleGlobals& g = bg();
  e->reach = e->base_reach;
  if (e->airborne == 1 || g.state != kStateFighting) return;
  if (e->status_bits & 0x80) {
    // Afraid: wanders, and gets over it now and then.
    enemy_wander(e);
    if (rng15() < 0x200) e->status_bits &= ~0x80u;
    return;
  }
  if (g.config[0x181] == 2) {
    // The escape race: run for the goal.
    int tries = 0;
    for (; tries < 10; ++tries) {
      s32 d[3] = {0x640000 - e->pos[0], 0, 0x12C000 - e->pos[2]};
      if (e->flying == 0) d[1] = stage_ground_height((e->pos[0] + d[0]) >> 12, (e->pos[2] + d[2]) >> 12) - e->pos[1];
      e->walk_dest[0] = e->pos[0] + d[0];
      e->walk_dest[1] = e->pos[1] + d[1];
      e->walk_dest[2] = e->pos[2] + d[2];
      if (unit_check_move(e, d, 7) == nullptr) break;
    }
    if (tries < 10) {
      e->move_mode = 1;
      e->action = 1;
      e->action_kind = 0;
      e->begin_move();
      return;
    }
  }
  if (7 < static_cast<s8>(e->ai_ticks)) {
    e->ai_ticks = 0;
    for (int i = 0; i < 5; ++i) {
      if (!(static_cast<s32>(rng_range(0x400)) < static_cast<s32>(e->odds[i]))) continue;
      switch (i) {
        case 0: e->choose_target(); break;
        case 1:
          if (rng15() >= 12001) enemy_try_cast(e);
          break;
        case 2:
          if (e->counter_window == 0) {
            e->ai_ticks = static_cast<u8>(e->ai_ticks + g.frame_ticks);
            return;
          }
          e->counter_window = 0;
          e->target = e->last_attacker;
          e->last_attacker = nullptr;
          enemy_start_attack(e);
          break;
        case 3: enemy_aggro_check(e); break;
        default: enemy_wander(e); break;
      }
      break;
    }
    if (e->counter_window != 0) --e->counter_window;
  }
  e->ai_ticks = static_cast<u8>(e->ai_ticks + g.frame_ticks);
}

void enemy_wander(EnemyUnit* e) {
  if (!unit_random_run(e, 0x200, 0)) {
    e->move_mode = 1;
    e->action = 1;
    e->action_kind = 0;
    e->begin_move();
  }
}

Unit* enemy_find_provoker() {
  BattleGlobals& g = bg();
  if (g.provoker != nullptr) return g.provoker;
  for (PartyUnit* u : g.party_units)
    if (!unit_is_down(u) && u->status_test(3, 0x800) != 0) return u;
  return nullptr;
}

void enemy_aggro_check(EnemyUnit* e) {
  for (PartyUnit* u : bg().party_units) {
    if (unit_is_down(u)) continue;
    if (vec_distance_fx(e->pos, u->pos) < static_cast<s32>(static_cast<u32>(e->aggro_radius) << 12)) e->choose_target();
  }
}

void enemy_start_attack(EnemyUnit* e) {
  e->action = 1;
  e->action_kind = 1;
  e->begin_move();
}

int enemy_attack_step(EnemyUnit* e) {
  if (static_cast<s16>(e->animate(static_cast<u32>(e->attack_recovery))) == -1) {
    e->action_kind = 0;
    e->begin_action();
    return 0;
  }
  if (bg().flags != 0) return 0;
  Unit* units[8] = {};
  Vec4 points[8];
  const int n = collider_attack_overlaps(e, units, points, 1);
  if (n == 0 || e->hit_landed != 0) return 0;
  return unit_attack_targets(e, n, units, points, 0x100, 9999, 0) + 1;
}

int enemy_nearest_party_index(const Unit* e) {
  const BattleGlobals& g = bg();
  s32 best = 0x9C40000;
  int idx = -1;
  for (int pass = 0; pass < 2 && idx < 0; ++pass) {
    for (int i = 0; i < 4; ++i) {
      PartyUnit* u = g.party_units[i];
      if (unit_is_down(u)) continue;
      if (pass == 0 && u->status_test(3, 4) != 0) continue;
      const s32 d = vec_distance_fx(e->pos, u->pos);
      if (d < best) {
        idx = i;
        best = d;
      }
    }
  }
  return idx;
}

// enemy_try_cast 0x8003e3b0: spell_units.cpp.

Unit* enemy_random_party_member(const Unit* e) {
  const BattleGlobals& g = bg();
  int tries = 0;
  Unit* u = nullptr;
  for (; tries < 200; ++tries) {
    u = g.party_units[rng_range(4)];
    if (!unit_is_down(u) && u->status_test(3, 4) == 0) break;
  }
  if (tries == 200) u = g.party_units[enemy_nearest_party_index(e) & 3];
  return u;
}

bool unit_random_run(Unit* u, s32 range, s32 min_dist) {
  int tries = 0;
  for (;;) {
    s32 d[3];
    d[0] = rng_spread_fixed(range);
    d[2] = rng_spread_fixed(range);
    d[1] = u->flying == 0 ? stage_ground_height((u->pos[0] + d[0]) >> 12, (u->pos[2] + d[2]) >> 12) - u->pos[1] : 0;
    u->walk_dest[0] = u->pos[0] + d[0];
    u->walk_dest[1] = u->pos[1] + d[1];
    u->walk_dest[2] = u->pos[2] + d[2];
    const bool bad = vec_distance_fx(u->walk_dest, u->pos) < min_dist || unit_check_move(u, d, 7) != nullptr;
    if (!bad) break;
    if (++tries >= 10) break;
  }
  return tries == 10;
}

}  // namespace ss::game::battle
