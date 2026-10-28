#pragma once

#include <vector>

#include "core/types.h"

namespace ss::game {
struct MemoryCard;

// title 0x800317e0 / 0x8003186c: the Continue screen. 'data' holds archive entries
// #3024-#3026 (0xbd0 window art, 0xbd1 page data, 0xbd2 party sprites) as one read; the
// menu framework runs page 0x11 in load mode on the title's card manager. On the
// console the field program (#3100) loads in the background meanwhile; the port's
// field is native. Returns nonzero when a game was loaded: the handoff blocks then
// hold the save (config, party, records, inventory, and the field snapshot in 0xe).
int load_game_screen(MemoryCard* card, std::vector<u8>& data);

// Launcher shortcut (SS_LOAD_SAVE / --load-save): loads save position 'index' (0-14,
// as the load screen lists them) of card slot 'slot' (0 / 1) without the menu, as the
// load flow does, and returns nonzero on success; the caller then runs the field.
int load_save_without_menu(MemoryCard* card, int slot, int index);

}  // namespace ss::game
