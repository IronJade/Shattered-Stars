#pragma once

#include "core/types.h"

// What the field's save page (overlay 0xbb6) adds to a save file. Its icon comes from
// entry #3027 (0xbd3): sheet 0 or 1 of sixteen icons each (a 16x16 4-bit image in three
// animation frames and its palette), picked by story progress (0x8007f648). The page
// unpacks the sheet into the heap and frees it before the file buffer is allocated over
// it, so the bytes save_build never writes (summary +0x60..+0x80, and the end of the
// last 128-byte frame after the packed data) are the sheet's from offset 0x454 on. The
// title cannot save, and its copy of the module has no icon (zeros).
namespace ss::game::menu {

// The icon of the current game state: 0x00-0x0f by game_state +0 (story progress),
// 0x10-0x1f by game_state +0x24c when that is further along (0x8007f648).
int field_save_icon_index(const u8* game_state);

// field 0x80055fc8, the first thing the field's save_build does: the load-game
// snapshot (handoff 0xe) takes the state as it is now (game_state 0x2a0, script flags
// 0x170). It was copied when the menu opened, before game_state +0x1c (the music) was
// turned into its index in the music table.
void field_state_to_snapshot();

// field_state_to_snapshot, then fills the file buffer (0x2000 bytes) with the leftover
// sheet bytes and returns the 0x1a0-byte icon (palette 0x20, three 0x80-byte frames)
// for file + 0x60, or nullptr when the entry cannot be read.
const u8* field_save_prefill(u8* file);

}  // namespace ss::game::menu
