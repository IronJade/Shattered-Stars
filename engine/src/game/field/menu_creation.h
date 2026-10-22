#pragma once

#include "core/types.h"

namespace ss::game::menu {
class MenuPageLoader;
}

// The item creation pages the Items page's Specialty / Super Specialty commands open:
// - page 0xf: code overlay 0xbc0 (specialties 1, 5, 6, 7, 10, 13, 14, 16, 17) or 0xbae
//   (specialties 4 and 11);
// - page 0x10: code overlay 0xbc2 (the eight super specialties).
// All of them are one Panel (0x108 bytes and more) that runs a loop of steps: pick a
// material (and a second one), confirm, watch the creation (the item viewer morphs the
// materials into the result), see what came out, maybe wake a talent, and start again.
// Each specialty supplies its lists of materials, how the result and the chance of success
// are worked out, and what is used up.
namespace ss::game::field {

void menu_page0f_update(menu::MenuPageLoader* p);   // field 0x80035bf4
void menu_page10_update(menu::MenuPageLoader* p);   // field 0x80035cb4

}  // namespace ss::game::field
