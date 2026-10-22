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

// Helpers the modules share.
// The camera follows this cast: the first slot, or the second once the first is finished
// (the modules' "slot +0xc == 0 || g_spellcasts[0] +0xe" test).
inline bool spellcast_leads_camera(const SpellCast* sc) {
  const SpellCast* first = spellcast_slot(0);
  return sc->slot == 0 || (first != nullptr && first->finished != 0);
}
// A model block of the cast's data (+0xa8[k]: the command-3 blocks), null when absent.
inline const u8* spellcast_model(const SpellCast* sc, int k) {
  return k >= 0 && k < 8 && !sc->textures[static_cast<size_t>(k)].empty() ? sc->textures[static_cast<size_t>(k)].data() : nullptr;
}

// The modules (spell_mod_<name>.cpp).
void spell_mod_foehn(SpellCast* sc);
void spell_mod_starlight(SpellCast* sc);
void spell_mod_magnum_tornado(SpellCast* sc);
void spell_mod_cure_all(SpellCast* sc);
// The monsters' spells (enemy agent).
void spell_mod_deep_freeze(SpellCast* sc);
void spell_mod_noah(SpellCast* sc);
void spell_mod_mind_blast(SpellCast* sc);
void spell_mod_spicule(SpellCast* sc);
void spell_mod_wind_of_destruction(SpellCast* sc);
void spell_mod_star_flare(SpellCast* sc);
void spell_mod_daemons_gate(SpellCast* sc);
void spell_mod_thundercloud(SpellCast* sc);

}  // namespace ss::game::battle
