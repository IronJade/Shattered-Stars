#pragma once

#include "core/types.h"

namespace ss::game::menu {
class MenuPageLoader;
}

// The camp menu's Options page: page 7 of the field's menus (code overlay 0xbc8 at 0x8007e000,
// data 0xbc9). It has two screens. Moving the cursor between their option groups scrolls
// the boxes by 0xd8.
// - Screen 1: message speed (with a sample that runs at it), sound mode, the four window
//   colours (a dialog of RGB sliders), and three switches (config +0x49 / +0x4a / +0x4b).
// - Screen 2: the eight button assignments (pressing a button assigns it) and vibration.
// The values go back into the config (and game_state +0x118) when the page closes.
namespace ss::game::field {

void menu_page07_update(menu::MenuPageLoader* p);   // field 0x80035854

}  // namespace ss::game::field
