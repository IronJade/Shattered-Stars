#pragma once

#include "core/types.h"
#include "game/boot/system.h"

// The credits program (#3107: archive entry 3107, 85 KB at 0x8002f810). Map 581
// carries a copy as its section 6, and table A 0x2d runs it after the endings
// (field_run_section6_program). It plays the eleven credit movies (#4594-#4604,
// disc 2) to the ending theme, writes the staff names (#4503) into the frames, and
// then starts the field program (#3100) again.
//
// Its movie player is the title's (game::Movie and libpress, the same code without the
// prologue), used in the mode the title never uses: whole frames are decoded to RAM
// (three 320x240 24-bit frames) instead of VRAM. The main loop writes the names into
// each finished frame with the GTE, and a vblank handler uploads the frame to the
// hidden display buffer and flips to it, at most every fourth vblank. Between movies
// the names go on black frames.
namespace ss::game::credits {

// 0x8002f810: the global constructor creates the program object (0x80042e64), then
// set_system (0x8002f890) and run (0x80042ea0). Never returns (the field comes next).
[[noreturn]] void credits_entry(System* sys);

}  // namespace ss::game::credits
