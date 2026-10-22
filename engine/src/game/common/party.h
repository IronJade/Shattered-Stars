#pragma once

#include "core/types.h"

// The party / stat library the title and field programs both link (title
// 0x800340b0-0x80034ff8, field 0x800331b8-0x80034100, plus the field's StatChange
// helpers 0x800397a4-0x80039f6c and 0x800323dc-0x800324c8). Everything works on the
// "selected member": party_select(slot) points the accessors at party slot 'slot'
// (handoff 3, 0x60 bytes) and record 'slot' (handoff 4, 0xd0 bytes).
//
// The block pointers come from party_bind: the title's menu session and the field's
// field_bind_system bind the handoff blocks (config 2, inventory 6, party 3, records 4).
namespace ss::game {

void party_bind(u8* config, u8* inventory, u8* party, u8* records);
u8* party_config();     // title 0x8004ebfc / field 0x80075270: handoff 2 (0x2a0 bytes)
u8* party_inventory();  // title 0x8004dda4 / field 0x80075278: handoff 6 (0xc28 bytes)
u8* party_slots();      // title 0x8004dda8 / field 0x8007406c..: handoff 3 (8 x 0x60)
u8* party_records();    // title 0x8004ddac: handoff 4 (8 x 0xd0)

int party_get_selected();          // title 0x800340b0 / field 0x800331b8
void party_select(int slot);       // title 0x800340c0 / field 0x800331c8
u8* party_selected_slot();         // title 0x8004ddcc / field 0x8007407c
u8* party_selected_record();       // title 0x8004ddd0 / field 0x80074080

// Stat ids: bits 0-4 group, bits 8-11 index. The kind table (title 0x8004b5d0 / field
// 0x80071968) says whether a group lives in the record (bit 0, else the party slot)
// and whether it is 32-bit (bit 1).
s32 stat_get(u32 id);                       // title 0x80034110 / field 0x80033218
void stat_set(u32 id, s32 value);           // title 0x800341f0 / field 0x800332f8
int stat_group_offset(int group);           // title 0x800342d8 / field 0x800333e0

int party_selected_char();                  // title 0x8003469c / field 0x800337a4: character id, 0 when <= 0
int party_slot_active(int slot);            // field 0x80033758: party_selected_char of 'slot' (selection kept)
int party_find_char_slot(int character);    // field 0x80033708: the slot holding 'character', -1 if none
int party_selected_alive();                 // field 0x800337cc: party_selected_char when status & 7 is clear, else 0
u32 char_equip_mask();                      // field 0x8003381c: 1 << (character - 1), 0 for none
u16 config_get_button(int index);           // title 0x800349d8 / field 0x80033ae0
void config_set_button(int index, u16 button);   // field 0x80033bc8: config + index * 2 (0-7)
s32 char_get_equip(int slot);               // title 0x80034b6c / field 0x80033c74: stat_get(slot + 0xb)
void char_set_equip(int slot, s32 item);    // field 0x80033c94: stat_set(slot + 0xb)
int char_skill_level(int skill);            // field 0x80033cb4: |record + 0x5c + skill|
void char_set_skill_level(int skill, int level);  // field 0x80033ce0: record + 0x5c + skill
// field 0x80033d1c: out[0..2] the specialty's skills (table title 0x8004b5e4 / field
// 0x8007197c), out[3] skills learnt, out[4] skills needed, out[5] level (average of
// the skill levels once all are learnt, -1 when none, +1 per accessory 0x1b4, max 10).
void char_specialty_info(int specialty, u16 out[6]);
int char_specialty_level(int specialty);    // field 0x80033cf8: out[5]
// field 0x80033ea0: a super specialty from two specialties (field table 0x800719ae):
// out[0..1] the specialties, out[2] 0 / 1 / 2 (two members past level 3, anyone with
// the second), out[3] 2, out[4] level (sum / 3, max 10; -1 when out[2] is 0).
void party_super_specialty_info(int id, u16 out[5]);
int party_super_specialty_level(int id);    // field 0x80033e7c: out[4]
// field 0x800341e4: Link Combos. The front four's pending second killer-move set
// (record +0xce/+0xcf) moves to +0xcc/+0xcd (voice_reload_member); a member keeps a
// Link Combo partner (+0x3b, a character id; 0x10 the extra partner of script flag
// 0x5d) only while wearing accessory 0x15c with the partner in the front row.
void party_update_link_combos();
int party_find_link_partner(int character); // field 0x8003446c: the front member whose partner (+0x3b) is 'character
int inventory_find(const u8* inventory, int item);  // field 0x8003c820: index of the item (low 10 bits), -1
int inventory_count(int item);              // field 0x800335d8: count bits 10-14 of its entry
u8 party_selected_status();                 // field 0x80039e08: slot +2 (& 7: cannot be healed)
// title 0x80034ef8 / field 0x80034000: experience needed to finish 'level': the sum over
// i = 1..level of (i * i + 1) * ((i - 1) / 10 + 5) (15 from level 100); 0 at level 0,
// 1000000000 from level 255.
s32 exp_for_level(int level);
// title 0x80034fb4 / field 0x800340bc: exp_for_level(level) minus the selected member's
// experience (slot +0x10), at least 0.
s32 exp_to_next_level(int level);

// ---- StatChange (6 bytes; field 0x800397a4-0x80039f6c) --------------------------------
// Values of the selected member: 1 HP base (+0x14), 2 HP max (+0x18), 3 HP (+0x1c),
// 4 MP base (+0x20), 5 MP max (+0x22), 6 MP (+0x24), 7 money (config + 0x18). Writes
// are clamped (HP 9999, MP 999, money 999999999), flag what changed and only take
// effect when 'apply' is set.
struct StatChange {
  u8 apply = 0;          // +0
  u8 hp_changed = 0;     // +1
  u8 mp_changed = 0;     // +2
  u8 money_changed = 0;  // +3
  u8 status_changed = 0; // +4
  u8 attr_changed = 0;   // +5
};
void stat_change_clear(StatChange* sc);                         // field 0x800397d0 (create 0x800397a4)
StatChange* stat_change_begin(StatChange* sc);                  // field 0x800323dc: clear, apply = 1
s32 stat_change_get(StatChange* sc, int which);                 // field 0x80039b58
void stat_change_set(StatChange* sc, int which, s32 value);     // field 0x80039c24
void stat_change_heal_pct(StatChange* sc, int is_mp, int pct);  // field 0x80039834: + pct% of max, up to max
void stat_change_heal_hp_pct(StatChange* sc, int pct);          // field 0x800397ec (wrapper 0x800324a8)
void stat_change_heal(StatChange* sc, int is_mp, int amount);   // field 0x8003993c: + amount, 1..max
void stat_change_add_money(StatChange* sc, int delta);          // field 0x80039ac0 (wrapper 0x800324c8)
s32 stat_change_get_attr(StatChange* sc, int i);                // field 0x80039e7c: stat_get(i + 3)
void party_heal_all(StatChange* sc, int is_mp, int amount);     // field 0x80032410: stat_change_heal on every member
void stat_change_heal_mp_pct(StatChange* sc, int pct);          // field 0x80039810
// field 0x80039a30 (wrappers 0x800399e8 HP / 0x80039a0c MP): base, max and current
// all + amount.
void stat_change_add_hp_mp(StatChange* sc, int is_mp, s32 amount);
void stat_change_set_status(StatChange* sc, u8 status);         // field 0x80039e20: slot +2
// field 0x80039e9c: attribute i (stat i + 3), clamped to 9999 (255 for 5 and 7, 0 past 7).
void stat_change_set_attr(StatChange* sc, int i, s32 value);
void stat_change_add_attr(StatChange* sc, int i, s32 amount);   // field 0x80039b08
bool stat_change_chance(StatChange* sc, int pct);               // field 0x80039f94: rng_range(100) < pct
s32 clamp_max(s32 value, s32 max);                              // field 0x80039f48: 0..max
s32 percent_of(s32 value, s32 pct);                             // field 0x80039f6c: value * pct / 100

// Hook for the voice-bank reload a bond change asks for (field 0x80038dfc); the field
// installs it, unset = nothing to reload.
extern void (*g_voice_reload_member)(int slot);

}  // namespace ss::game
