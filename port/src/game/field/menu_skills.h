#pragma once

#include "core/types.h"

namespace ss::game::menu {
class MenuPageLoader;
}

// The camp menu's Skills page: page 5 of the field's menus (code overlay 0xbc6 at 0x8007e000,
// data 0xbc7).
// - Every skill a member knows or may learn (its book read: script flag 700 + group) is
//   listed with its level and the SP the next level costs. Confirming raises it; the
//   level's bonus follows, and a notice tells of new or higher specialties and super
//   specialties.
// - Square switches a battle skill (0x21-0x2e) off or on (its level's sign).
// - Triangle lists the specialties and super specialties with their progress.
namespace ss::game::field {

void menu_page05_update(menu::MenuPageLoader* p);   // field 0x8003571c

}  // namespace ss::game::field
