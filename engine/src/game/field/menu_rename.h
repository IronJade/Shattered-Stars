#pragma once

#include "core/types.h"

namespace ss::game::menu {
class MenuPageLoader;
}

// Page 0xe of the field's menus (code overlay 0xbce, data 0xbcf): renaming the character
// picked on the Status page. It is the title's New Game name entry again, copied into
// the overlay with its own text renderer: a keyboard window (two character sets of five
// rows of ten, and Capital / Lower / OK / Delete / Return), a name box with seven slots
// and the family name after them, and the character's picture (entry 0xbd3 + character,
// a 15-bit image) on the left. The page runs its own frame loop over a 320 x 240 double
// buffer; afterwards the menu's VRAM and screen come back. The name goes to record +0x24
// and game_state +0x28 + (character - 1) * 0x14.
namespace ss::game::field {

void menu_page0e_update(menu::MenuPageLoader* p);   // field 0x80035e78

}  // namespace ss::game::field
