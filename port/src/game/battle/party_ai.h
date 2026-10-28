#pragma once

// The party members' AI (party_ai.cpp): the per-class decisions pc_ai_update (party_states.cpp)
// runs for the members the player does not control, and their helpers.
#include "core/types.h"

namespace ss::game::battle {

struct Unit;
struct PartyUnit;

void pc_ai_fighter(PartyUnit* u);  // 0x80038664 (classes < 9)
void pc_ai_healer(PartyUnit* u);   // 0x80038114 (class 9)
void pc_ai_caster(PartyUnit* u);   // 0x80038328 (classes 10-12)
int pc_ai_assist_leader(PartyUnit* u);                              // 0x80038810
Unit* pc_ai_pick_enemy(PartyUnit* u, int mode);                     // 0x80038990
int pc_ai_choose_attack(PartyUnit* u, Unit* target, int cautious);  // 0x80038bdc
void pc_ai_issue_attack(PartyUnit* u, Unit* target, int kind);      // 0x800390d8
Unit* pc_ai_choose_heal(PartyUnit* u, int* spell, int half);        // 0x8003926c
Unit* pc_ai_choose_support(PartyUnit* u, int* spell);               // 0x80039788
int pc_ai_choose_attack_spell(PartyUnit* u, Unit* target, int hp_pct);  // 0x8003a20c
void pc_ai_engage_nearby(PartyUnit* u);                             // 0x8003a658 (slot 44)
Unit* pc_ai_find_protect_target(PartyUnit* u);                      // 0x8003a740
int pc_spell_not_taken(PartyUnit* u, int spell, const Unit* target);  // 0x8003b6d8
void pc_sort_allies_by_affinity(PartyUnit* u, PartyUnit* out[4]);   // 0x8003bc9c

}  // namespace ss::game::battle
