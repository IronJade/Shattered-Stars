#pragma once

#include <memory>

#include "core/types.h"
#include "game/battle/unit.h"

// The party-character code modules (#2131 Claude, #2135 Rena, #2137 Celine, #2139 Bowman,
// #2143 Dias, #2155 Precis, #2147 Ashton, #2141 Leon, #2153 Opera, #2151 Ernest, #2157 Noel,
// #2159 Chisato; the even entry before each is the data). The console unpacks the code to
// 0x801ee000 + 0x4000 * slot and constructs the PartyUnit subclass at its offset 0. The
// port's subclasses are native; their data tables are read from block 0 (the slot-0 link
// at 0x801ee000).
namespace ss::game::battle {

// pc_construct_slot: the character's PartyUnit subclass (the base class for characters
// whose module is not ported yet).
std::unique_ptr<PartyUnit> pc_module_create(int character);
// A byte of the character's module image (slot-0 link: address 0x801ee000..), or 0.
u8 pc_module_u8(int character, u32 address);
void pc_modules_reset();

}  // namespace ss::game::battle
