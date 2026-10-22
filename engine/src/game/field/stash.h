#pragma once

#include "core/types.h"

// The inventory stash (0x80076228, 0xc28 bytes in the inventory's own format: 0x400
// u16 entries id | count << 10 | new << 15, then the check bytes at +0x824). Scripts
// empty the inventory into it for story stretches where the party loses its items
// (table A 0x10 / 0x11 / 0x34, overlay 0xc1d). It survives other programs in handoff
// 0x11.
namespace ss::game::field {

u8* stash_table();          // 0x80076228
bool stash_holds_items();   // 0x8007622c: the stash holds the inventory
// field_run: take the stash from handoff 0x11, or make an empty one.
void stash_init();
void inventory_stash();     // overlay 0x8008ea0c
void inventory_unstash();   // overlay 0x8008ead8
void inventory_clear_all(); // overlay 0x8008eb60

}  // namespace ss::game::field
