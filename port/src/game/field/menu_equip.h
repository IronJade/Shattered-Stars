#pragma once

#include "core/types.h"

namespace ss::game::menu {
class MenuPageLoader;
}

// The camp menu's Equipment page: page 4 of the field's menus (code overlay 0xbc4 at
// 0x8007e000, data 0xbc5).
// - The member's seven slots are listed with their pieces. Confirming a slot opens a list
//   of the inventory's pieces that fit it, and the page previews each one with the
//   attributes before and after.
// - The second button opens the options: the best pieces at once, and the "EW" switch
//   (record +0x3a bit 0) that the field's automatic equipping honours.
namespace ss::game::field {

void menu_page04_update(menu::MenuPageLoader* p);   // field 0x80035688

}  // namespace ss::game::field
