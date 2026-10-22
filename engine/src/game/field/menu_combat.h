#pragma once

#include "core/types.h"

namespace ss::game::menu {
class MenuPageLoader;
}

// The camp menu's Combat submenu: pages 8-10 of the field's menus, all in code overlay
// 0xbca (data 0xbcb).
// - Page 8, Tactics: each member's battle orders (record +0x38, one of six per class).
// - Page 9, Party Order: two members picked in turn change places (slot and record). The
//   front four must keep someone who can fight.
// - Page 10, Formation: the battle formation (config +0x40, one of 13) with a 3D preview
//   of where the front four stand, and the member the player controls (config +0x41).
namespace ss::game::field {

void menu_page08_update(menu::MenuPageLoader* p);   // field 0x80035994
void menu_page09_update(menu::MenuPageLoader* p);   // field 0x80035a28
void menu_page0a_update(menu::MenuPageLoader* p);   // field 0x80035abc

}  // namespace ss::game::field
