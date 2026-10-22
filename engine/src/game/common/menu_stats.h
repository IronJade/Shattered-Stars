#pragma once

#include <vector>

#include "core/types.h"

// Party stat recomputation shared by the title and the field (title 0x8003a250-
// 0x8003b364 / field 0x8003b1ac-0x8003c2c0, "StatCalc"). Menu panels embed one: it
// owns a copy of the item stat table while open and recomputes every member's
// derived stats (maximum HP / MP, attributes after equipment) when it closes.
namespace ss::game::menu {

class StatCalc {   // 8 bytes: +0 item table, +4 vtable (title 0x8004bcb4 / field 0x80072124)
 public:
  StatCalc();                 // title 0x8003a250 / field 0x8003b1ac stat_calc_create
  ~StatCalc();                // title 0x8003a368 / field 0x8003b2c4 stat_calc_destroy
  void load_items();          // title 0x8003a3c4 / field 0x8003b320: unpacks the 0x338-item table
  void all();                 // title 0x8003a534 / field 0x8003b490 stat_calc_all
  void member();              // title 0x8003a5ac / field 0x8003b508 stat_calc_member
  void item_bonuses();        // title 0x8003a738 / field 0x8003b694 stat_calc_item_bonuses
  u32 item_stat(int item, int field) const;  // title 0x8003abd0 / field 0x8003bb2c item_stat_get
  void equip_effects();       // title 0x8003ad68 / field 0x8003bcc4 stat_calc_equip_effects

 private:
  std::vector<u16> items_;    // +0: 0x338 entries of 8 u16 (null in the original when the
                              //     game's item data is resident)
};

// Single-stat modifiers (title 0x8003b024-0x8003b364 / field 0x8003bf80-0x8003c2c0):
// percent changes of a party slot's fields; a negative 'amount' on HP/attr22 is a
// flat increase.
void stat_mod_hp_max(u8* slot, int amount);   // title 0x8003b024: +0x18 (s32, max 9999)
void stat_mod_mp_max(u8* slot, int amount);   // title 0x8003b0dc: +0x22 (max 999)
void stat_mod_attr_2c(u8* slot, int pct);     // title 0x8003b1a4
void stat_mod_attr_32(u8* slot, int pct);     // title 0x8003b214
void stat_mod_attr_3e(u8* slot, int pct);     // title 0x8003b284 (clamps +0x40)
void stat_mod_attr_44(u8* slot, int pct);     // title 0x8003b2f4
void stat_mod_attr_38(u8* slot, int pct);     // title 0x8003b364

// title 0x80034ff8 / field 0x80034100 item_data_get: a field of the game's item data
// (inventory + 0x820, 0x30 bytes per item).
s32 item_data_get(int item, int field);

}  // namespace ss::game::menu
