#pragma once

#include <vector>

#include "core/types.h"

// World edits (port feature, in the spirit of Severed Chains' mods): patch files change
// map scripts as they load — chest contents, door targets — without touching the discs.
//
// Patch files are `*.patch` in the mods directory (Runtime::mods_dir: `mods/` next to
// config.ini, or SS_MODS_DIR), applied in name order. One patch per line:
//
//   script MAP WORD EXPECTED VALUE     # numbers in decimal or 0x hex
//
// WORD indexes the map's script words from offset 0x1c (the offset tools/so2_script.py
// prints, divided by 4). The word is replaced only if it still holds EXPECTED, so a patch
// written for other data is skipped (and logged) instead of corrupting the script.
// tools/so2_mod.py writes these lines for chests and doors.
namespace ss::game::field {

// Applies every patch for 'map' to its freshly unpacked script (section 1 block 0).
// Returns the number of words changed.
int mods_apply_script(int map, std::vector<u8>& script);

}  // namespace ss::game::field
