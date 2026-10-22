#pragma once

#include <vector>

#include "core/types.h"
#include "game/field/field.h"

// What the field does once it is back from a battle (or after a script gift; both set
// g_show_battle_results 0x80076278): it rescans the party's equipment for walking
// effects and ability bits, auto-equips better new items for members who allow it, and
// if anything changed shows a report per member (states 0xf / 0x10: equipment before
// and after, ATK / AC / HIT / AVD / MAG before -> after). Field 0x80030d5c-0x800313c0,
// 0x8004b7a8, 0x8004c0cc, field_update states 0 / 0xf / 0x10.
namespace ss::game::field {

// 0x80076190: party_scan_equip_effects' mask. 1: encounters 20% rarer, 2: 50% rarer,
// 4: 20% more often, 8 / 0x10: an encounter whose list index is in 0x80074cf0 /
// 0x80074d70 is re-rolled.
u32 field_ability_bits();

u32 party_scan_equip_effects();      // 0x80030dec (also sets the Callways walk flags)
int party_auto_equip(u8* report);    // 0x80030d5c: report = 8 x 0x30 bytes; 1 when anything was equipped
void inventory_clear_new_flags();    // 0x80030f84
void inventory_give_one(int item);   // 0x8003368c
void inventory_take_one(int item);   // 0x800336c0

// 0x80053a24: stops streaming, empties the load queue and reads 'entry' into the side
// buffer (0x800762e8, done flag 0x800762e4); entry 0xfc2 holds the report's strings and
// font (field_install_side_text).
void field_side_load(s16 entry);
bool field_side_load_done();         // 0x800762e4
std::vector<u8>& field_side_buffer();  // 0x800762e8 (the world map installs music / sprite packs from it)
s16 field_side_entry();              // 0x800762ec
void field_side_clear_done();        // 0x800762e4 = 0
// field_load_map / field_unload_map: frees the side-loaded entry, cancelling its read if
// it has not finished (0x800762e8, 0x800762e4, 0x800762ec).
void field_side_free();
void field_free_equip_report();      // field_unload_map: 0x800761a0

// field_update state 0 (0x8004c334): with g_show_battle_results set and the leader in
// control, the scan and auto-equip; returns true when the report starts (state 0xf).
bool field_post_battle_check(Field* f);
void field_update_equip_report(Field* f);        // state 0xf
void field_update_equip_report_close(Field* f);  // state 0x10
void field_end_overlay_event();                  // 0x8004c0cc (the report window's close callback)

void post_battle_reset();            // field_construct 0x8004880c's part
void post_battle_destroy_panels();   // field_destroy_subsystems 0x80049098's part

// 0x800100ec (boot): 'value' in decimal; width > 0 right-aligns it in 'width' characters
// padded with 'pad' (only the low digits when it is wider).
char* format_decimal(char* out, s32 value, int width, char pad);

}  // namespace ss::game::field
