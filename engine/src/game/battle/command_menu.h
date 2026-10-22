#pragma once

#include "core/types.h"
#include "game/battle/input.h"

// The player's command menus: the ring of four commands round the controlled member
// (spells, items, escape, tactics; input_ring_menu_open / update 0x8004cf5c / 0x8004d178),
// the member select (input mode 1, 0x8004b1ac / 0x8004b65c), the escape command
// (0x8004d914) and the list menus of #1984.0 (list_menu_* 0x8007e644.., the spell menu
// 0x80072c5c.., the item menu 0x80073560.., the tactics menu 0x800801c0..) with their
// description balloon (desc_popup_* 0x80041620..). parts/p3.md "Battle input block".
namespace ss::game::battle {

struct Unit;

void input_ring_menu_open(BattleInput* in);                // 0x8004cf5c
void input_ring_menu_update(BattleInput* in, int closing);  // 0x8004d178
void input_open_member_select(BattleInput* in);            // 0x8004b1ac (mode 1)
void input_member_select_update(BattleInput* in);          // 0x8004b65c
void battle_command_escape();                              // 0x8004d914
// The open menus (g_open_list_menu 0x8006faa4).
void spell_menu_open();                                    // 0x80072c5c
void item_menu_open();                                     // 0x80073560
void tactics_menu_open();                                  // 0x800801c0
// What the menus chose (g_chosen_spell 0x8006fb28, g_chosen_item_index 0x8006fba4).
u8 chosen_spell();
s16 chosen_item_index();
// 0x80082dec (#1984.1) item_use_battle: the item at inventory index 'index' from user on
// target (null: the item's own targets); items.cpp.
void item_use_battle(Unit* user, Unit* target, int index);
// Battle start / end: the menus' memory and the open menu.
void command_menu_reset();

}  // namespace ss::game::battle
