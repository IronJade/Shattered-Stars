#pragma once

#include "core/types.h"

namespace ss::game::menu {
class MenuPageLoader;
}

// Page 0x13 of the field's menus (code overlay 0xbcc): a picture the Items page opens for
// items 6-0x11 (session +0x12 = the picture, entry 0x117c + n). The picture is 320 x 1200
// pixels, 75 strips of 320 x 16 in 15-bit colour. The page takes over the screen in its own
// frame loop: an interlaced 320 x 480 display shows 28 strips at a time, and up / down (or
// the left stick) scroll it with momentum. Buttons 4 / 5 jump to the top / bottom, and
// cancel fades it out. Strips are read as they come into view, kept in 30 VRAM slots at
// (0x140, 0..0x1d0), and fade in. Afterwards the menu's VRAM and screen come back.
namespace ss::game::field {

void menu_page13_update(menu::MenuPageLoader* p);   // field 0x80035f0c

}  // namespace ss::game::field
