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

// Indalecio's second form (+0x2f8; enemy_mod_indalecio.cpp), for Divine Comedy's module.
bool enemy_indalecio_transformed(const Unit* u);

// The modules (spell_mod_<name>.cpp).
void spell_mod_foehn(SpellCast* sc);
void spell_mod_starlight(SpellCast* sc);
void spell_mod_magnum_tornado(SpellCast* sc);
void spell_mod_cure_all(SpellCast* sc);
void spell_mod_black_saber(SpellCast* sc);
void spell_mod_earthquake(SpellCast* sc);
void spell_mod_explode(SpellCast* sc);
void spell_mod_fairy_light(SpellCast* sc);
void spell_mod_angel_feather(SpellCast* sc);
void spell_mod_dark_circle(SpellCast* sc);
void spell_mod_energy_arrow(SpellCast* sc);
void spell_mod_dispel(SpellCast* sc);
void spell_mod_earth_grave(SpellCast* sc);
void spell_mod_tetanus_wind(SpellCast* sc);
void spell_mod_growth(SpellCast* sc);
void spell_mod_shadow_bolt(SpellCast* sc);
void spell_mod_protection(SpellCast* sc);
void spell_mod_silence(SpellCast* sc);
void spell_mod_divine_comedy(SpellCast* sc);
void spell_mod_gremlin_lair(SpellCast* sc);
void spell_mod_tractor_beam(SpellCast* sc);
void spell_mod_anti(SpellCast* sc);
void spell_mod_raise_dead(SpellCast* sc);
void spell_mod_lunar_light(SpellCast* sc);
void spell_mod_extinction(SpellCast* sc);
void spell_mod_press(SpellCast* sc);
void spell_mod_energy_net(SpellCast* sc);
void spell_mod_neutral(SpellCast* sc);
void spell_mod_deep_mist(SpellCast* sc);
void spell_mod_bless(SpellCast* sc);
void spell_mod_forget(SpellCast* sc);
void spell_mod_sonic_saber(SpellCast* sc);
void spell_mod_meteor_swarm(SpellCast* sc);
void spell_mod_eruption(SpellCast* sc);
void spell_mod_southern_cross(SpellCast* sc);
void spell_mod_gravity_press(SpellCast* sc);
void spell_mod_blood_sucker(SpellCast* sc);
void spell_mod_fairy_heal(SpellCast* sc);
void spell_mod_shadow_flare(SpellCast* sc);
void spell_mod_reflection(SpellCast* sc);
void spell_mod_delay(SpellCast* sc);
void spell_mod_thunderstorm(SpellCast* sc);
void spell_mod_haste(SpellCast* sc);
void spell_mod_light_cross(SpellCast* sc);
void spell_mod_spell40(SpellCast* sc);
void spell_mod_acid_rain(SpellCast* sc);
void spell_mod_curse(SpellCast* sc);
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
