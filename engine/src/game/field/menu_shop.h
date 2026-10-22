#pragma once

#include "core/types.h"

namespace ss::game::menu {
class MenuPageLoader;
}

// Shops: page 0xb of the field's menus (code overlay 0xbaa at 0x8007e000, data 0xbab; the
// launcher kind 2 entries 0xba8-0xbab). The shop id is the session argument (script B 0x6d's
// parameter): it picks the stock list, the help bar's shop name and, for ids 0x5a-0x5e,
// the order shops that deliver later. Buying and selling fill a cart; "Settle" (or
// leaving) books it: money, the inventory, order deliveries.
namespace ss::game::field {

void menu_page0b_update(menu::MenuPageLoader* p);   // field 0x80035b50

}  // namespace ss::game::field
