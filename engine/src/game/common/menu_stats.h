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
  StatCalc() : StatCalc(true) {}  // title 0x8003a250 / field 0x8003b1ac stat_calc_create
  // adjust_levels false skips the constructor's party pass (+0x26 from +0x28): what the
  // destructor's recomputation sees after a load replaced the party.
  explicit StatCalc(bool adjust_levels);
  ~StatCalc();                // title 0x8003a368 / field 0x8003b2c4 stat_calc_destroy
  void load_items();          // title 0x8003a3c4 / field 0x8003b320: unpacks the 0x338-item table
  void all();                 // title 0x8003a534 / field 0x8003b490 stat_calc_all
  void member();              // title 0x8003a5ac / field 0x8003b508 stat_calc_member
  void item_bonuses();        // title 0x8003a738 / field 0x8003b694 stat_calc_item_bonuses
  u32 item_stat(int item, int field) const;  // title 0x8003abd0 / field 0x8003bb2c item_stat_get
  void equip_effects();       // title 0x8003ad68 / field 0x8003bcc4 stat_calc_equip_effects
  // title 0x8003a8d8 / field 0x8003b834: attribute 'which' (0 ATK, 1 AC, 2 HIT, 3 AVD, 4 MAG)
  // of the selected member: its equipment's item fields 3-7 plus the slot's base value.
  s32 attr_total(int which) const;
  void snapshot_attrs(s16 out[5]) const;      // title 0x8003a85c / field 0x8003b7b8
  void snapshot_equip(s16 out[7]) const;      // title 0x8003aaa0 / field 0x8003b9fc
  // title 0x8003ab18 / field 0x8003ba74: nonzero when the selected member can wear 'item'.
  u32 can_equip(int item) const;
  // title 0x8003ab4c / field 0x8003baa8: the equipment slot of 'item' (0 weapon, 1-4 armour,
  // 5 accessory; by item field 1).
  int equip_slot(int item) const;
  // field 0x8003c4bc: whether one of the selected member's equipment pieces has effect
  // 'effect' (item field 2).
  bool wears_effect(int effect) const;
  // field 0x8003c3d8: an MP cost after the member's equipment: halved with effect 0xa5,
  // 0xdc or 0xdd, two thirds with 0xa1 or 0xd2; at least 1 (0 stays 0).
  int mp_cost(int cost) const;

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
