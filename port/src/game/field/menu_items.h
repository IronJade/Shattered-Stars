#pragma once

#include "core/types.h"

namespace ss::game::menu {
class MenuPageLoader;
}

// The camp menu's Items page: page 3 of the field's menus (code overlay 0xbbe at 0x8007e000,
// data 0xbbf). The inventory is a list of two columns, with the item's picture and
// description (or its figures) above it.
// - Confirming an item uses it: on a member, on everyone, or with no target. An item
//   that can be worn opens a member picker that equips it.
// - The second button opens the commands: sorting (8 orders), Specialty (item creation,
//   page 0xf), Super Specialty (page 0x10) and Valuables.
namespace ss::game::field {

void menu_page03_update(menu::MenuPageLoader* p);   // field 0x80035580

}  // namespace ss::game::field
