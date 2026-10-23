#pragma once

#include <memory>
#include <vector>

#include "core/types.h"
#include "game/battle/battle.h"

// Battle spells (heraldry): the spell tables of the #2129 image, the spell-data cache
// (spell_data_* 0x800482a8-0x80048c5c: the archive entries 0x8e1-0x981 that carry each
// spell's effect data, loaded ahead while the caster chants), spell_cast 0x80062c24 with
// the ten spells it runs itself as Shot kinds (spell_shots.cpp), and the casting side of
// a unit (0x8005287c-0x80053054: chanting, the cast release, the spell clash). The other
// spells run as a SpellCast (spellcast.h). parts/p3.md, p5.md, p7.md; docs/notes/battle.md
// 6.6 and 7 "Spells".
namespace ss::game::battle {

struct Unit;
struct PartyUnit;
struct EnemyUnit;
struct Shot;
struct SpellCast;

constexpr int kSpellCount = 0x48;  // spell ids 0..0x47 (0x40.. enemy-only, no MP cost)

// A spell's effect data (the whole archive entry, spell_data_take hands it over).
using SpellData = std::unique_ptr<std::vector<u8>>;

// ---- Tables (the #2129 image; the entry table is a copy a module may change) ----
int spell_mp_cost_base(int spell);    // g_spell_mp_cost 0x8006b053[spell]
int spell_cast_ticks(int spell);      // g_spell_cast_ticks 0x8006b09b[spell]
int spell_target_class(int spell);    // 0x8006b0e3[spell]: 0 enemy, 1 / 2 party member, 3 none
int spell_needs_data(int spell);      // g_spell_needs_data 0x8006b12b[spell]
int spell_element(int spell);         // g_spell_element 0x8006b173[spell]: 0..11
int spell_effect_kind(int spell);     // g_spell_effect_kind 0x8007002f[spell]: SpellCast kind (0 none)
// g_spell_data_entry 0x8006d1f2: the archive entry of the spell's data for caster variant
// 0 generic, 1 Rena, 2 Leon, 3 Celine, 4 Noel (0 none). Indalecio's second form rewrites
// some (spell_data_entry_set).
s16 spell_data_entry(int spell, int variant);
void spell_data_entry_set(int spell, int variant, s16 entry);
// #1984 block 1's data (0x80081728-0x80085518: the clash and item tables), read from the
// archive the first time.
const u8* ovl1984b1_data(u32 address);

// ---- The spell-data cache (16 pending requests, 16 slots, at most 3 loaded) ----
int unit_caster_variant(const Unit* u);              // 0x800482a8
void spell_data_request(int spell, int priority, Unit* caster);  // 0x800482e8
void spell_data_update();                            // 0x80048464 (battle_main_loop)
// 0x800487c8: 0 not requested (or the spell has no data), 1 requested / loading, 2 loaded.
int spell_data_status(int spell, Unit* caster);
SpellData spell_data_take(int spell, Unit* caster);  // 0x800488e8 (null when not loaded)
void spell_data_clear_all();                         // 0x80048994
void spell_data_cancel_owner(Unit* owner);           // 0x80048a20

// ---- Casting ----
// 0x80062c24: casts 'spell' (0..0x47) from caster at target with power (the caster's
// stats +0x26 or a module's value): takes the preloaded data (dropped when it should have
// some and has none), pays the MP (spells below 0x40; "Not enough MP" 0xb / "Forgot
// spell!" 0xe / "Can't cast spells!" 0xf instead), then runs the spell.
void spell_cast(int spell, Unit* caster, Unit* target, int power);
// The ten spells spell_cast runs itself (Shot kinds; spell_shots.cpp).
void spell_firebolt(Unit* caster, Unit* target, int power);      // 0x80062f20
void spell_ice_needle(Unit* caster, Unit* target, int power);    // 0x80063038
void spell_thunderbolt(Unit* caster, Unit* target, int power);   // 0x800630d4
void spell_grave(Unit* caster, Unit* target, int power);         // 0x8006314c
void spell_wounds(Unit* caster, Unit* target, int power);        // 0x800631ac
void spell_word_of_death(Unit* caster, Unit* target, int power); // 0x8006320c
void spell_heal(Unit* caster, Unit* target, int power);          // 0x8006326c
void spell_cure_light(Unit* caster, Unit* target, int power);    // 0x80063370
void spell_antidote(Unit* caster, Unit* target);                 // 0x800634ec
void spell_wind_blade(Unit* caster, Unit* target, int power);    // 0x800635b0
void spell_camera_focus(Unit* caster, Unit* target);             // 0x80063690

// The unit side.
int unit_can_cast(Unit* u);                       // 0x8005287c (Unit [3])
int unit_mp_cost(Unit* u, int cost);              // 0x80053f6c (killer.cpp): halved / 2/3 by statuses, min 1
int unit_spell_mp_cost(Unit* u, int index);       // 0x80052900: index = spell - 1
void unit_request_spell_data(Unit* u, int priority);  // 0x80052b6c
void unit_begin_cast(Unit* u);                    // 0x80052bac: chanting (state 0xe)
void unit_cast_effects(Unit* u);                  // 0x80052cbc
int unit_update_casting(Unit* u);                 // 0x80052f04: 1 when the cast is over
Unit* unit_find_spell_clash(Unit* caster);        // 0x800539f4
u32 spell_clash_check(Unit* a, Unit* b);          // 0x80081d74 (#1984.1)

// The monsters' and the party's spell helpers (spell_units.cpp).
void enemy_state_chant(EnemyUnit* e);             // 0x8003e540 (state 0xe)
const u8* pc_spell_list(const Unit* u);           // 0x8003a5d4: the class's spell list (null: fighters)
int pc_can_cast_spell(PartyUnit* u, int spell);   // 0x8003b618: learned and affordable

// Spell globals.
u8& spell_landed_flag();           // g_spell_landed 0x8006fb2f: a spell's finishing hit resolved
u8& spell_effect_started_flag();   // g_spell_effect_started 0x8006f6d1

// The Shot kinds of the spells (spell_shots.cpp): 3 cast aura, 4 / 5 cast particles and
// ring, 6 Antidote, 7 / 8 Firebolt, 9 Ice Needle, 0xa Thunderbolt, 0xb Grave, 0xc Wounds,
// 0xd Word of Death, 0xe Heal, 0xf Cure Light, 0x12 line streak, 0x13 meteor, 0x15 heal
// orb, 0x1d Wind Blade. Returns false for other kinds.
bool spell_shot_update(Shot* s);
// 0x80057384: the sparks a spell Shot leaves on its target (level 0..2: more kinds).
void shot_hit_burst(Shot* s, int level);
// 0x8005774c: the command sent to a spell Shot (+0x10e: 1 end, 2 power up by +0x10c,
// 3 turn back; a stationary Shot ends instead), then cleared.
void shot_handle_command(Shot* s, int stationary);

// Battle start / end.
void spells_reset();

// Debugging: SS_BATTLE_CAST="FRAMES:SLOT:SPELL:p0|e0;..." forced party casts (input_update).
void debug_forced_casts();
}  // namespace ss::game::battle
