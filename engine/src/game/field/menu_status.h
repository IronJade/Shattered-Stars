#pragma once

#include "core/types.h"

namespace ss::game::menu {
class MenuPageLoader;
class Widget;
}

// The camp menu's Status page: page 6 of the field's menus (code overlay 0xbb2 at 0x8007e000,
// data 0xbb3). It is also the member page script menus open (menu_run_member, a single
// member).
// - It shows one member: the portrait (entry 0x1170 + character, 160x160 16-bit, streamed
//   into VRAM (0x200, 0) and slid up), class, HP / MP, level and experience, the eight
//   abilities, the favourite food, the equipment and ATK / AC / HIT / AVD / MAG.
// - Triangle lists the talents; square shows the elements and status resistances of the
//   equipment; confirm goes on to page 0xe.
namespace ss::game::field {

void menu_page06_update(menu::MenuPageLoader* p);   // field 0x800357b0
// The talents list (Status 0x8007e000; the Skills page has its own copy at the same
// address of 0xbc6).
menu::Widget* create_talents_dialog();

}  // namespace ss::game::field
