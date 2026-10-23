// The party members' AI (0x80038114-0x8003a9cf, 0x8003b6d8, 0x8003bc9c): what a member
// the player does not control does each frame, by class (fighters < 9, healer 9, casters
// 10-12) and its record's tactic (+0x38: 0 attack freely, 1 protect / support, 2 / 3
// cautious, 4 stay away from magic, 5 do nothing; 0 whenever the AI drives the leader).
// The decisions draw from the RNG in the same order as the console.
#include "game/battle/party_ai.h"

#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/combat.h"
#include "game/battle/input.h"
#include "game/battle/killer.h"
#include "game/battle/spells.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next()) & 0x7FFF; }
s16 rds16(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
u8 data_u8(u32 address) {
  const u8* p = battle_data(address);
  return p != nullptr ? *p : 0;
}
s16 data_s16(u32 address) {
  const u8* p = battle_data(address);
  return p != nullptr ? rds16(p) : 0;
}

// The tactic the AI follows: the record's (+0x38) unless the whole party is AI-driven
// (config +0x181 == 1) or the member is under status 3 / 0x2000 (berserk).
int ai_tactic(PartyUnit* u) {
  const BattleGlobals& g = bg();
  if (g.config[0x181] == 1 || u->status_test(3, 0x2000) != 0) return 0;
  return u->record[0x38];
}

// The run toward a random point unit_random_run picked (+0x12b 1, state 1, slot 29).
void start_random_run(PartyUnit* u) {
  u->move_mode = 1;
  u->action = 1;
  u->start_approach();
  u->action_kind = 0;
}

}  // namespace

// 0x8003b6d8: 0 when another standing member (or this one) already chose 'spell' (+0x207)
// on 'target' (any target for 0x33); else 1.
int pc_spell_not_taken(PartyUnit* u, int spell, const Unit* target) {
  (void)u;
  for (PartyUnit* m : bg().party_units) {
    if (unit_is_down(m) || m->ai_spell != spell) continue;
    if (spell == 0x33) return 0;
    if (m->target == target) return 0;
  }
  return 1;
}

// 0x8003bc9c: the four party units sorted by u's affinity (self 8; others the average of the
// two config affinity tables (+0x58 / +0xe8, 12 x 12); down members 0), highest first.
void pc_sort_allies_by_affinity(PartyUnit* u, PartyUnit* out[4]) {
  BattleGlobals& g = bg();
  s32 score[4];
  for (int i = 0; i < 4; ++i) {
    out[i] = g.party_units[i];
    score[i] = 0;
    if (unit_is_down(g.party_units[i])) continue;
    if (g.party_units[i] == u) {
      score[i] = 8;
    } else {
      const u8* row = g.config + (u->st()->id - 1) * 0xC;
      const int col = rds16(g.party_slot[i]) - 1;
      score[i] = (row[col + 0x58] + row[col + 0xE8]) >> 1;
    }
  }
  for (int i = 0; i < 4; ++i) {
    for (int j = i + 1; j < 4; ++j) {
      if (score[i] < score[j]) {
        std::swap(score[i], score[j]);
        std::swap(out[i], out[j]);
      }
    }
  }
}

// 0x80038810: when the leader asked for assistance (0x8006fb29 == 1) and has a target, the
// standing member closest to it (if u) runs to a random point near itself and the request
// is answered.
int pc_ai_assist_leader(PartyUnit* u) {
  BattleGlobals& g = bg();
  if (g.assist_request != 1) return 0;
  s32 best_d = 0x1869F000;
  int best = -1;
  const Unit* goal = g.party_units[g.controlled_slot]->target;
  if (goal != nullptr) {
    for (int i = 0; i < 4; ++i) {
      if (unit_is_down(g.party_units[i])) continue;
      const s32 d = vec_distance_fx(g.party_units[i]->pos, goal->pos);
      if (d < best_d) {
        best = i;
        best_d = d;
      }
    }
  }
  if (best < 0) return 0;
  if (g.party_units[best] != u) return 0;
  if (unit_random_run(u, 0x140, 0)) return 0;
  start_random_run(u);
  g.assist_request = 0;
  return 1;
}

// 0x80038990: the enemy to go for. Mode 0 keeps a standing target 22000 / 32768 of the time;
// else (and in mode 1, which first skips enemies another attacking member is on) the
// nearest reachable standing enemy, never the leader's while an assist runs. Sets +0x1d4
// as it scans (the last enemy looked at when none fits) and returns the pick or null.
Unit* pc_ai_pick_enemy(PartyUnit* u, int mode) {
  BattleGlobals& g = bg();
  u->reach = u->base_reach;
  bool keep = false;
  if (u->target != nullptr && !unit_is_down(u->target) && mode == 0) keep = rng15() < 22000;
  if (keep) return u->target;
  const Unit* avoid = nullptr;
  if (g.assist_request != 0 && g.assist_timer != 0) avoid = g.party_units[g.controlled_slot]->target;
  int best = -1;
  s32 best_d = 0x9C40000;
  do {
    if (mode < 0) break;
    for (int i = 0; i < 8; ++i) {
      EnemyUnit* e = g.enemy_units[i];
      if (unit_is_down(e)) continue;
      u->target = e;
      if (unit_can_reach_target(u, e, 1) == 0) continue;
      if (mode == 1) {
        bool taken = false;
        for (PartyUnit* m : g.party_units) {
          if (!unit_is_down(m) && 8 < m->action && m->target == e) {
            taken = true;
            break;
          }
        }
        if (taken) continue;
      }
      const s32 d = vec_distance_fx(u->pos, e->pos);
      if (d < best_d && avoid != e) {
        best = i;
        best_d = d;
      }
    }
    --mode;
  } while (best < 0);
  if (best != -1 && g.enemy_units[best] != avoid) {
    u->target = g.enemy_units[best];
    return g.enemy_units[best];
  }
  return nullptr;
}

// 0x80038bdc: -1 nothing, 0 / 1 the killer move on L / R, 2 a normal attack. Scores each
// button's assigned moves (expected damage with their charge, -99999 without the MP) and the
// normal attack against the target's HP (6/5 of it, or a quarter when cautious); a module's
// forced choice (slot 45) and status 2 / 0x80000000 (normal attacks only) come first.
int pc_ai_choose_attack(PartyUnit* u, Unit* target, int cautious) {
  if (target == nullptr) return -1;
  if (u->status_test(2, 0x80000000u) != 0) return 2;
  int choice = u->forced_attack_choice();
  if (choice != -1) return choice;
  s32 score[3] = {0, 0, 0};
  u->status_enabled = 0;
  for (int which = 0; which < 2; ++which) {
    if (u->st()->mp < u->killer_move_cost(which)) {
      score[which] = -99999;
      continue;
    }
    int far = unit_target_is_far(u, target);
    score[which] = 0;
    for (int c = 0; c < 2 && u->record[0xCC + c * 2 + which] != 0; ++c) {
      const int k = c * 2 + which;
      const u8* ad = u->attack_data[static_cast<size_t>(k)][static_cast<size_t>(far)];
      if (ad == nullptr) break;
      const u8* md = u->move_data[static_cast<size_t>(k)];
      if (md == nullptr) break;
      const int lvl = pc_move_charge_level(u, k);
      if (target->flying != 0 && (ad[2] == 0 || (ad[2] == 1 && target->pos[1] <= u->size[1] * -0x1000))) break;
      s32 v = rds16(ad + 4);
      if (v == 0x2000) {
        v = 700;
      } else {
        if (ad[1] != 0) v = u->st()->atk[2] * v;
        v = unit_calc_damage(u, target, v, ad[1] != 0, static_cast<u16>(rds16(ad + 6)));
      }
      v = static_cast<s32>(v * static_cast<s32>(ad[0])) / 2;
      const s16 mode = rds16(md + 6);
      if (mode == 0) {
        s32 t = v * rds16(md) * lvl;
        if (t < 0) t += 0xFF;
        v += t >> 8;
      } else if (mode == 1) {
        v += v * rds16(md) * lvl;
      }
      if (9999 < v) v = 9999;
      if (v < -9999) v = -9999;
      score[which] += v;
      if (ad[3] == 1) far = 0;
    }
  }
  score[2] = unit_calc_damage(u, target, 0x100, 0, 0);
  u->status_enabled = 1;
  const u32 odds = cautious == 0 ? 0x1000 : 0x400;
  s32 thr = target->st()->hp;
  thr = cautious == 0 ? thr * 6 / 5 : thr / 4;
  if (thr < score[2]) {
    choice = 2;
  } else if (cautious == 0 || rng15() < 24000) {
    s32 best = 0;
    for (int k = 0; k < 3; ++k) {
      const s32 s = score[k];
      if (s < thr) {
        if (best < s) {
          choice = k;
          best = s;
        }
      } else if (best < thr || s < best) {
        choice = k;
        best = s;
      }
    }
  }
  if (choice != -1) return choice;
  int result = 2;
  if (rng15() < odds) {
    if (0 < score[0] && rng15() < 0x4000) choice = 0;
    result = choice;
    if (0 < score[1] && rng15() < 0x4000) result = 1;
  }
  return result;
}

// 0x800390d8: the command for kind 0 (L: command 2, reach +0x2de[far]), 1 (R: command 3,
// +0x2e2[far]) or 2 (a normal attack: command 1, the base reach).
void pc_ai_issue_attack(PartyUnit* u, Unit* target, int kind) {
  if (kind == -1) return;
  const int far = unit_target_is_far(u, target);
  if (kind == 1) {
    u->command = 3;
    u->combo = 0;
    u->reach = u->killer_range[static_cast<size_t>(2 + far)];
  } else if (kind == 0) {
    u->command = 2;
    u->combo = 0;
    u->reach = u->killer_range[static_cast<size_t>(far)];
  } else if (kind == 2) {
    u->command = 1;
    u->reach = u->base_reach;
  }
}

// 0x8003926c: a healing spell and its target. The most hurt standing ally (sorted by
// affinity) at or under 2/3 of its max HP (1/2 with 'half'): Fairy Light (0x33) or 0x32 when
// several are hurt, else 0x31 / 0x30 / Heal (0x2f) by how much is missing; then the cures
// (0x34 poison / 0x36 / 0x35) and 0x3f / 0x36 for bad statuses. *spell = 0 when none.
Unit* pc_ai_choose_heal(PartyUnit* u, int* spell, int half) {
  *spell = 0;
  if (u->recovery_gauge != 0 || u->wait_after_spell != 0) return nullptr;
  int hurt = 0;
  s32 lowest = 10000;
  PartyUnit* worst = nullptr;
  PartyUnit* list[4];
  pc_sort_allies_by_affinity(u, list);
  for (PartyUnit* m : list) {
    if (unit_is_down(m)) continue;
    const s32 thr = half == 0 ? (m->st()->hp_max << 1) / 3 : m->st()->hp_max / 2;
    const s32 hp = m->st()->hp;
    if (hp <= thr) {
      ++hurt;
      if (hp < lowest) {
        worst = m;
        lowest = hp;
      }
    }
  }
  auto pick = [&](int s, int mark, PartyUnit* who) -> Unit* {
    *spell = s;
    u->ai_spell = static_cast<u8>(mark);
    return who;
  };
  if (hurt != 0) {
    s32 need = worst->st()->hp_max;
    need += need / 2;
    if (1 < hurt) {
      if (0x898 < need && pc_can_cast_spell(u, 0x33) != 0 && pc_spell_not_taken(u, 0x32, worst) != 0)
        return pick(0x33, 0x32, worst);
      if (0x4B0 < need && pc_can_cast_spell(u, 0x32) != 0 && pc_spell_not_taken(u, 0x32, worst) != 0)
        return pick(0x32, 0x32, worst);
    }
    if (0xAF0 < need && pc_can_cast_spell(u, 0x31) != 0 && pc_spell_not_taken(u, 0x2F, worst) != 0)
      return pick(0x31, 0x2F, worst);
    if (800 < need && pc_can_cast_spell(u, 0x30) != 0 && pc_spell_not_taken(u, 0x2F, worst) != 0)
      return pick(0x30, 0x2F, worst);
    if (pc_can_cast_spell(u, 0x2F) != 0 && pc_spell_not_taken(u, 0x2F, worst) != 0) return pick(0x2F, 0x2F, worst);
  }
  for (PartyUnit* m : list) {
    if (m == nullptr) continue;
    const u8 status = m->st()->status;
    if ((status & 1) != 0 && pc_can_cast_spell(u, 0x34) != 0 && pc_spell_not_taken(u, 0x34, m) != 0)
      return pick(0x34, 0x34, m);
    if (m->removed == 0 && (status & 6) != 0 && pc_can_cast_spell(u, 0x36) != 0 &&
        pc_spell_not_taken(u, 0x36, m) != 0)
      return pick(0x36, 0x36, m);
  }
  for (PartyUnit* m : list) {
    if (unit_is_down(m)) continue;
    if ((m->st()->status & 8) != 0 && pc_can_cast_spell(u, 0x35) != 0 && pc_spell_not_taken(u, 0x35, m) != 0)
      return pick(0x35, 0x35, m);
  }
  for (PartyUnit* m : list) {
    if (unit_is_down(m) || (m->status_bits & 0x2A7) == 0 || pc_spell_not_taken(u, 0x3F, m) == 0) continue;
    if (pc_can_cast_spell(u, 0x3F) != 0) return pick(0x3F, 0x3F, m);
    if (pc_can_cast_spell(u, 0x36) != 0 && pc_spell_not_taken(u, 0x36, m) != 0) return pick(0x36, 0x3F, m);
  }
  return nullptr;
}

// 0x80039788: a buff for an ally or a debuff for a random standing enemy, by comparing
// their stats (and the party's average level against the enemy's); *spell = 0 when none.
Unit* pc_ai_choose_support(PartyUnit* u, int* spell) {
  BattleGlobals& g = bg();
  *spell = 0;
  if (u->recovery_gauge != 0 || u->wait_after_spell != 0) return nullptr;
  PartyUnit* list[4];
  pc_sort_allies_by_affinity(u, list);
  u->ai_spell = 0;
  EnemyUnit* e = nullptr;
  int tries = 0;
  for (; tries < 100; ++tries) {
    e = g.enemy_units[rng_range(8)];
    if (!unit_is_down(e)) break;
  }
  if (tries == 100) return nullptr;
  const Stats* es = e->st();
  int n = 0;
  s32 levels = 0;
  for (int i = 0; i < 4; ++i) {
    if (g.party_units[i] != nullptr) {
      ++n;
      levels += rds16(g.party_slot[i] + 0x26);
    }
  }
  if (n == 0) n = 1;
  const s32 avg = levels / n;
  auto on_enemy = [&](int s) -> Unit* {
    *spell = s;
    return e;
  };
  auto on_ally = [&](int s, PartyUnit* m) -> Unit* {
    *spell = s;
    u->ai_spell = static_cast<u8>(s);
    return m;
  };
  if (rng15() < 0x2000 && 0xF < es->mp && u->st()->mp < 0x28 && pc_can_cast_spell(u, 0x2E) != 0 &&
      es->resist[1] < 0x60)
    return on_enemy(0x2E);
  if (e->odds[1] != 0 && avg + 3 <= es->battle_level) {
    if ((u->status_bits & 0x8000) == 0 && pc_can_cast_spell(u, 0x2A) != 0 && pc_spell_not_taken(u, 0x2A, u) != 0)
      return on_ally(0x2A, u);
    if (e->boss == 0) {
      const u32 r = rng15();
      if (r <= 0xFFF && (e->status_bits & 6) == 0) {
        if (pc_can_cast_spell(u, 0x11) != 0) return on_enemy(0x11);
        if (pc_can_cast_spell(u, 0x3C) != 0) return on_enemy(0x3C);
      }
    }
  }
  for (PartyUnit* m : list) {
    if (unit_is_down(m)) continue;
    const Stats* ms = m->st();
    if (e->boss != 0 && avg + 6 <= es->battle_level) {
      const u32 r = rng15();
      if (r < 20000 && (m->status_bits & 0x2000) == 0 && pc_can_cast_spell(u, 0x3B) != 0 &&
          pc_spell_not_taken(u, 0x3B, m) != 0)
        return on_ally(0x3B, m);
    }
    if (ms->hp_max / 6 < es->atk[2] - (ms->def[2] * 3) / 2 && (m->status_bits & 0x500) == 0) {
      if (pc_can_cast_spell(u, 0x3A) != 0 && pc_spell_not_taken(u, 0x3A, m) != 0) return on_ally(0x3A, m);
      if (pc_can_cast_spell(u, 0xF) != 0 && pc_spell_not_taken(u, 0xF, m) != 0) return on_ally(0xF, m);
    }
    if (9 <= ms->klass) continue;
    if (ms->atk[2] - (es->def[2] * 3) / 2 < es->hp_max / 16) {
      if ((m->status_bits & 0x1000) == 0 && pc_can_cast_spell(u, 0x39) != 0 && pc_spell_not_taken(u, 0x39, m) != 0)
        return on_ally(0x39, m);
      const u32 r = rng15();
      if (r < 0x2000 && es->resist[1] < 0x50 && (e->status_bits & 1) == 0 && pc_can_cast_spell(u, 6) != 0)
        return on_enemy(6);
    }
    if (m->run_speed - 2 < e->run_speed) {
      if ((m->status_bits & 0x800) == 0 && pc_can_cast_spell(u, 0x37) != 0 && pc_spell_not_taken(u, 0x37, m) != 0)
        return on_ally(0x37, m);
      const u32 r = rng15();
      if (r < 0x2000 && es->resist[1] < 0x40 && (e->status_bits & 0x20) == 0 && pc_can_cast_spell(u, 0x38) != 0)
        return on_enemy(0x38);
    }
    if (0x80 < es->eva[2] - ms->acc[2]) {
      if ((m->status_bits & 0x4000) == 0 && pc_can_cast_spell(u, 0x3E) != 0 && pc_spell_not_taken(u, 0x3E, m) != 0)
        return on_ally(0x3E, m);
      const u32 r = rng15();
      if (r < 0x2000 && es->resist[1] < 0x40 && (e->status_bits & 0x200) == 0 && pc_can_cast_spell(u, 0x22) != 0)
        return on_enemy(0x22);
    }
  }
  if (rng15() < 0x1000 && e->boss == 0 && avg + 4 <= es->battle_level && es->resist[1] < 0x40 &&
      pc_can_cast_spell(u, 0x29) != 0)
    return on_enemy(0x29);
  if ((e->status_bits & 0xFFFFFD58u) == 0) return nullptr;
  if (0x1FFF < rng15()) return nullptr;
  if (0x3F < es->resist[1]) return nullptr;
  if (pc_can_cast_spell(u, 0x3D) == 0) return nullptr;
  return on_enemy(0x3D);
}

// 0x8003a20c: the attack spell (index, spell - 1) to cast at 'target', or -1. Each learned,
// affordable spell of the class list (< 0x2e) scores (base 0x8006b5bc + INT * 0x8006b5be /
// 256 + its proficiency * 0x8006b5c0 / 256) times the target's element affinity factor
// (0x8006cdc0) / 2; 0 against an absorbing enemy for the bigger ones. The highest score
// below 'hp_pct' % of the target's HP wins, else the lowest above it.
int pc_ai_choose_attack_spell(PartyUnit* u, Unit* target, int hp_pct) {
  BattleGlobals& g = bg();
  if (target == nullptr || u->recovery_gauge != 0) return -1;
  const u8* list = pc_spell_list(u);
  if (list == nullptr) return -1;
  const int count = data_u8(0x8006B1AB + u->st()->klass);
  s32 score[64] = {};
  const Stats* ts = target->st();
  for (int i = 0; i < count && i < 64; ++i) {
    score[i] = 0;
    if (static_cast<s8>(u->record[0x3C + i]) <= 0) continue;
    const u8 sp = list[i];
    const int idx = sp - 1;
    if (u->st()->mp < unit_spell_mp_cost(u, idx) || 0x2D < idx) continue;
    s32 base = data_s16(0x8006B5BC + static_cast<u32>(idx) * 6);
    if (base == 0) continue;
    if (base == -1 && ts->resist[0] < 0x40) base = 1000;
    if (base == -2 && ts->resist[0] < 0x60 && ts->hp < ts->hp_max / 4) base = 2000;
    s32 a = data_s16(0x8006B5BE + static_cast<u32>(idx) * 6) * u->st()->intel[2];
    if (a < 0) a += 0xFF;
    s32 b = data_s16(0x8006B5C0 + static_cast<u32>(idx) * 6) * rds16(u->record + 0x8C + i * 2);
    if (b < 0) b += 0xFF;
    const u8 elem = data_u8(0x8006B173 + sp);
    const s8 factor = static_cast<s8>(data_u8(0x8006CDC0 + ts->element[elem]));
    s32 s = ((base + (a >> 8) + (b >> 8)) * factor) / 2;
    if (target->special_timer != 0) s = 0;
    if (0x20 < data_s16(0x8006B5BE + static_cast<u32>(idx) * 6)) {
      for (EnemyUnit* e : g.enemy_units) {
        if (!unit_is_down(e) && e->st()->element[elem] == 4) s = 0;
      }
    }
    score[i] = s;
  }
  s32 best = 0;
  int pick = -1;
  for (int i = 0; i < count && i < 64; ++i) {
    const s32 s = score[i];
    if (s == 0) continue;
    bool better = best < s;
    if ((ts->hp * hp_pct) / 100 <= best) better = s < best;
    if (better) {
      best = s;
      pick = i;
    }
  }
  return pick != -1 ? list[pick] - 1 : -1;
}

// 0x8003a658 (slot 44): with a standing enemy within 32.0 of its body edge, the member
// runs to a random point within 0x300 (at least as far as that enemy).
void pc_ai_engage_nearby(PartyUnit* u) {
  BattleGlobals& g = bg();
  s32 d = 0;
  int i = 0;
  for (; i < 8; ++i) {
    EnemyUnit* e = g.enemy_units[i];
    if (unit_is_down(e)) continue;
    d = vec_distance_fx(e->pos, u->pos);
    if (d + u->size[0] * -0x2000 < 0x200000) break;
  }
  if (i != 8 && !unit_random_run(u, 0x300, d)) start_random_run(u);
}

// 0x8003a740: for another standing member that is a caster or under a quarter of its HP,
// its nearest enemy when that is within 30.0 of u's body edge (also u's new target).
Unit* pc_ai_find_protect_target(PartyUnit* u) {
  BattleGlobals& g = bg();
  for (int i = 0; i < 4; ++i) {
    PartyUnit* m = g.party_units[i];
    if (unit_is_down(m) || m == u) continue;
    const Stats* ms = stats_of(g.party_slot[i]);
    if (ms->klass < 9 && ms->hp_max / 4 <= ms->hp) continue;
    const int idx = pc_nearest_enemy_index(m);
    if (idx < 0) continue;
    EnemyUnit* e = g.enemy_units[static_cast<size_t>(idx)];
    if (vec_distance_fx(e->pos, m->pos) + u->size[0] * -0x2000 < 0x1E0000) {
      u->target = e;
      return e;
    }
  }
  return nullptr;
}

// 0x80038664: the fighters (classes < 9): 3/32 of frames, by tactic, an enemy and the
// attack for it (tactic 4 keeps away instead).
void pc_ai_fighter(PartyUnit* u) {
  BattleGlobals& g = bg();
  if (g.flags != 0 || g.state != kStateFighting) return;
  const int tactic = ai_tactic(u);
  if (tactic == 5 || pc_ai_assist_leader(u) != 0) return;
  if (0xC00 < rng15()) return;
  Unit* e = nullptr;
  int cautious = 0;
  switch (tactic) {
    case 0:
      e = pc_ai_pick_enemy(u, 0);
      break;
    case 1:
      e = pc_ai_find_protect_target(u);
      if (e == nullptr) e = pc_ai_pick_enemy(u, 0);
      break;
    case 2:
      e = pc_ai_pick_enemy(u, 0);
      cautious = 1;
      break;
    case 3:
      e = pc_ai_pick_enemy(u, 1);
      cautious = 1;
      break;
    case 4:
      pc_ai_engage_nearby(u);
      return;
    default:
      return;
  }
  pc_ai_issue_attack(u, e, pc_ai_choose_attack(u, e, cautious));
}

// 0x80038114: the healer (class 9, Rena): 1/8 of frames a heal (tactic 0 / 2), or heal /
// support / now and then an attack spell (tactic 1), or a normal attack (tactic 4); with
// nothing to do it keeps away from nearby enemies.
void pc_ai_healer(PartyUnit* u) {
  BattleGlobals& g = bg();
  if (g.flags != 0 || g.state != kStateFighting) return;
  const int tactic = ai_tactic(u);
  if (tactic == 5 || pc_ai_assist_leader(u) != 0) return;
  if (0x1000 < rng15()) return;
  int spell = 0;
  Unit* target = nullptr;  // (the console leaves a register's old value for tactic 3)
  switch (tactic) {
    case 0:
      target = pc_ai_choose_heal(u, &spell, 0);
      break;
    case 1:
      target = pc_ai_choose_heal(u, &spell, 0);
      if (target == nullptr) {
        target = pc_ai_choose_support(u, &spell);
        if (target == nullptr && rng15() < 0x400) {
          target = pc_ai_pick_enemy(u, 0);
          spell = pc_ai_choose_attack_spell(u, target, 100) + 1;
        }
      }
      break;
    case 2:
      target = pc_ai_choose_heal(u, &spell, 1);
      break;
    case 4:
      if (Unit* e = pc_ai_pick_enemy(u, 0)) pc_ai_issue_attack(u, e, 2);
      break;
    default:
      break;
  }
  if (spell != 0) {
    u->command = 4;
    u->target = target;
    u->wait_after_spell = 0;
    u->spell = static_cast<u8>(spell);
  }
  if (u->command == 0 && tactic != 4) pc_ai_engage_nearby(u);
}

// 0x80038328: the casters (classes 10-12): with odds the share of standing enemies, now
// and then (always for class 12) a heal / support (tactics < 3), else by tactic an attack
// spell (HP share 100 / 15 / 50) or a normal attack (tactic 4); with nothing to do they keep
// away from nearby enemies.
void pc_ai_caster(PartyUnit* u) {
  BattleGlobals& g = bg();
  if (g.flags != 0 || g.state != kStateFighting) return;
  const int tactic = ai_tactic(u);
  if (tactic == 5 || pc_ai_assist_leader(u) != 0) return;
  int present = 0, down = 0;
  for (EnemyUnit* e : g.enemy_units) {
    if (e == nullptr) continue;
    ++present;
    if (unit_is_down(e)) ++down;
  }
  const u32 odds = present != 0 ? static_cast<u32>(((present - down) * 0x1000) / present) : 0x8000;
  if (odds < rng15()) return;
  int spell = -1;
  Unit* target = nullptr;
  if (tactic < 3 && (rng15() < 0x400 || u->st()->klass == 0xC)) {
    target = pc_ai_choose_heal(u, &spell, 1);
    if (target == nullptr && (target = pc_ai_choose_support(u, &spell)) == nullptr) {
      spell = -1;
    } else {
      spell -= 1;
    }
  }
  if (target == nullptr) {
    int pct = -1;
    switch (tactic) {
      case 0:
        if (rng15() <= 0x1000) {
          target = pc_ai_pick_enemy(u, 0);
          pct = 100;
        }
        break;
      case 1:
        if (0x600 < rng15()) {
          target = pc_ai_choose_heal(u, &spell, 1);
          spell -= 1;
        } else {
          target = pc_ai_pick_enemy(u, 0);
          pct = 0xF;
        }
        break;
      case 2:
        if (rng15() <= 0xC00) {
          target = pc_ai_pick_enemy(u, 1);
          pct = 0x32;
        }
        break;
      case 4:
        if (Unit* e = pc_ai_pick_enemy(u, 0)) pc_ai_issue_attack(u, e, 2);
        break;
      default:
        break;
    }
    if (pct != -1) spell = pc_ai_choose_attack_spell(u, target, pct);
  }
  if (-1 < spell) {
    u->command = 4;
    u->target = target;
    u->spell = static_cast<u8>(spell + 1);
  }
  if (u->command == 0 && tactic != 4) pc_ai_engage_nearby(u);
}

}  // namespace ss::game::battle
