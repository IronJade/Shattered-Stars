#pragma once

#include "core/types.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"

// Combat rules: hit boxes, the physical hit pipeline (accuracy, damage, criticals,
// equipment procs, knockback, reactions), HP / MP changes, the reaction states and the
// damage popups (0x800349b8, 0x80050198..0x8005224c, 0x80053b24, 0x8005d604..).
// docs/port/notes/battle.md §7 "Damage", parts/p3.md.
namespace ss::game::battle {

// 0x800349b8: the units of the other side (vs_party: the party, else the enemies) whose
// body boxes meet atk's enabled attack boxes; fills units[] and the hit points; count.
int collider_attack_overlaps(const Collider* atk, Unit* units[8], Vec4 points[8], int vs_party);
bool unit_attack_hits(Unit* atk, Unit* def);                                     // 0x80050198
int unit_calc_damage(Unit* atk, Unit* def, int power, int magic, u16 elements);   // 0x800503f0
// 0x800509e4: resolves a physical hit on each target (angle 9999: knock along x away
// from atk); returns 1 when damage landed.
int unit_attack_targets(Unit* atk, int count, Unit** targets, const Vec4* points, int power, int angle,
                        u16 elements);
void unit_show_guts_effect(Unit* src, Unit* at, int offensive);  // 0x80051d54
void unit_damage_hp(Unit* u, s32 dmg);                          // 0x80051e1c (negative heals)
void unit_damage_mp(Unit* u, s32 dmg);                          // 0x80051f88
int unit_start_reaction(Unit* u);                               // 0x80051fdc (Unit [5])
bool unit_knockback_slide(Unit* u);                             // 0x8005224c: true when recovered
int unit_resist_roll(Unit* u, int kind, int chance);            // 0x80053b24 (Unit [13])
void unit_update_vanish(Unit* u);                               // 0x80053c04 (Unit [8], state 6)
void unit_apply_stun(Unit* u);                                  // 0x80055588
Vec4 vec_normalize(const s32 v[3]);                             // 0x800675e8 (4.12)
int party_count_standing();                                     // 0x80047f1c
int party_count_fled();                                         // 0x80047f8c

// Damage popups (16, DamagePopup 0x2c bytes, vtable 0x8006cf64): the value in digits
// above the unit, bouncing on its floor for 32 frames.
void damage_popup_spawn(Unit* u, s32 value, s32 vx, int is_mp);              // 0x8005d71c
void damage_popup_spawn_at(const s32 pos[3], s32 value, s32 vx, int is_mp);  // 0x8005d604
void damage_popups_reset();

// Screen shake + pad vibration (#1984.0): y only (0x80072594) or both axes (0x800725d4).
void screen_shake_vertical(int amp_y, int period_y, int frames);
void screen_shake(int amp_x, int amp_y, int period_x, int period_y, int frames);

}  // namespace ss::game::battle
