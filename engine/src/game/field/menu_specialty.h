#pragma once

#include "core/types.h"

namespace ss::game::menu {
class MenuPageLoader;
}

// The camp menu's Specialty page: page 2 of the field's menus (code overlay 0xbbc at
// 0x8007e000, data 0xbbd). After a member is picked it lists what that member can do
// outside battle:
// - Killer Move Setting (fighters): the two killer moves (record +0xcc / +0xcd), or four
//   with a Link Combo partner (record +0x3b; +0xce / +0xcf for the second set). The
//   partner dialog needs accessory 0x15c.
// - The spell list (Rena, Celine, Leon and Noel): each spell can be switched off for
//   battle, and the healing and curing spells can be cast on a member.
// - The field specialties: Oracle (2), Musical Talent (3: compose with a Feather Pen
//   and an instrument, play a song with item 0x35), Practice (8), Scout (9) and
//   Familiar (12: orders from a shop, which goes on to page 0xb).
// Using a specialty can wake one of the member's talents (record +0x20).
namespace ss::game::field {

void menu_page02_update(menu::MenuPageLoader* p);   // field 0x800354dc

}  // namespace ss::game::field
