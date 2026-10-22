#pragma once

#include <vector>

#include "core/types.h"
#include "game/battle/spellcast.h"

// The spell code modules: the 55 distinct code blocks the spell data entries (0x8e1-0x981)
// carry (data command 6), each a native function here (spell_mod_<name>.cpp, named after
// the spell(s) it serves), found by the bytes of the block.
namespace ss::game::battle {

// The registry index of the module whose block 0 is 'block' (-1: not ported / unknown).
int spell_module_find(const std::vector<u8>& block);
SpellModuleFn spell_module_fn(int index);  // null for -1
// The handler of a cast whose module is not ported: the cast ends at once.
void spell_module_missing(SpellCast* sc);

// The modules (spell_mod_<name>.cpp).
void spell_mod_feehn(SpellCast* sc);

}  // namespace ss::game::battle
