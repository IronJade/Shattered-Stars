#pragma once

#include <string>

#include "game/boot/system.h"

// Debug entry to the battle program without the field (main.cpp --battle).
namespace ss::game::battle {

// --battle ENC[:STAGE]: builds what the title and the field would have handed over for
// a new game's party (Claude, level 1; handoffs 2 / 3 / 4 / 6) and the battle setup of
// encounter ENC on stage STAGE (default 3, Shingo Forest) as the field's
// battle_build_load_list does (handoff 5, nothing preloaded), then returns
// Program::Battle. 'end' is --battle-end FRAMES[:OUTCOME]: end the fight after FRAMES
// vblanks in state 2 with OUTCOME (default 0, victory), since the combat and the result
// screen are not ported yet. With SS_BATTLE_RAM=<2 MB RAM dump of the original> the
// party, config and inventory (and, when the dump was taken at field_exit_to_program,
// the encounter and stage of the field's setup) come from the dump instead.
Program battle_debug_start(const std::string& spec, const std::string& end);
// --battle-end alone: every battle of the run (the field's too) ends that way.
void battle_debug_set_end(const std::string& end);
// SS_BATTLE_RNG=1: traces the RNG draw count each frame (to line up with a PCSX-Redux
// count of rng_next calls). SS_BATTLE_DUMP=<file>: at battle_exit_to_field writes the
// handoff blocks the field reads back (2 config 0x2a0, 3 party 0x300, 4 records 0x680,
// 6 inventory 0xc28, in that order) for a comparison with a RAM dump of the original.
void battle_debug_frame();
void battle_debug_exit();

}  // namespace ss::game::battle
