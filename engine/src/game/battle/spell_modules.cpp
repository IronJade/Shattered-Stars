// The spell code module registry. See spell_modules.h.

#include "game/battle/spell_modules.h"

#include <iterator>

namespace ss::game::battle {
namespace {

struct ModuleDef {
  const char* name;  // the spell(s) it serves
  u64 hash;          // FNV-1a 64 of block 0
  u32 size;          // block 0's unpacked size
  SpellModuleFn fn;
};

// (Filled as the modules are ported; scratchpad/world/spell/modules/index.txt lists all 55.)
const ModuleDef kModules[] = {
    {"foehn", 0xaaf512758b30accfull, 2236, spell_mod_foehn},          // 2a9c041ec7: spell 0xa (0x915 / 0x916)
    {"starlight", 0x8b58fab70d17cb84ull, 3168, spell_mod_starlight},  // af4ef66c53: spell 0x12 (0x8e7-0x8e9)
    {"magnum_tornado", 0x6bfd24af2c836819ull, 3904, spell_mod_magnum_tornado},  // 53455d80ad: spell 0xb (0x906 / 0x907)
    {"cure_all", 0x68588d2f05fc34b2ull, 2372, spell_mod_cure_all},              // 95b9fdccfa: spell 0x32 (0x933-0x936)
    {"thundercloud", 0xc552bc746b7733b9ull, 4172, spell_mod_thundercloud},     // 569e24f878: spell 0x10 (0x920 / 0x921)
    {"black_saber", 0xb274234e0882f723ull, 6420, spell_mod_black_saber},  // 67a7a8d842: spell 0x1f (0x928 / 0x929)
    {"earthquake", 0x534692ddbecc9245ull, 6128, spell_mod_earthquake},  // 43d5518828: spell 0x19 (0x974-0x97b)
    {"explode", 0xb38498bb32aedd91ull, 5660, spell_mod_explode},  // a639d47ca2: spell 3 (0x917-0x91b)
    {"fairy_light", 0xfd437a2ddfa786d7ull, 4468, spell_mod_fairy_light},  // 4d978c241f: spell 0x33 (0x90e-0x910)
    {"angel_feather", 0x06058cdfa7ee6cf8ull, 3292, spell_mod_angel_feather},  // cfbae89db2: spell 0x3b (0x954-0x958)
    {"dark_circle", 0xb531304fcd5d8c7bull, 3728, spell_mod_dark_circle},  // fa606854ed: spell 0x26 (0x93e / 0x93f)
    {"energy_arrow", 0x408da17ecaee754cull, 2648, spell_mod_energy_arrow},  // 00b1337668: spell 0x2c (0x930-0x932)
    {"dispel", 0x0b0a16b531f1d7daull, 2608, spell_mod_dispel},  // e37e03cd9f: spell 0x36 (0x94c-0x950)
    {"earth_grave", 0xcf1ecaa4897a0fc1ull, 2360, spell_mod_earth_grave},  // fb43fdecea: spell 0x18 (0x91e / 0x91f)
    {"tetanus_wind", 0x4229ee7bd07ed834ull, 2264, spell_mod_tetanus_wind},  // 03874d77f8: spell 0xc (0x901 / 0x902)
    {"growth", 0xa0f03884c79e4261ull, 2880, spell_mod_growth},  // 44645f977c: spell 0x39 (0x943-0x945)
    {"shadow_bolt", 0xc3896a6e34950e25ull, 2052, spell_mod_shadow_bolt},  // d00581e30f: spell 0x20 (0x8ea / 0x8eb)
    {"protection", 0xf54f2123a20b0725ull, 1848, spell_mod_protection},  // 2acd939273: spell 0x3a (0x949-0x94b)
    {"silence", 0x7ed5fbccc2432983ull, 1524, spell_mod_silence},  // 2ecc4bc085: spell 0x3c (0x8f3-0x8f5)
    {"divine_comedy", 0x7bb9338340ef8dbeull, 8480, spell_mod_divine_comedy},  // 476afdf4ce: spell 0x46 (0x981)
    {"gremlin_lair", 0xf668249439c936b0ull, 6576, spell_mod_gremlin_lair},  // fc13393fe3: spell 0x23 (0x90c / 0x90d)
    {"tractor_beam", 0x194df347f19f9c99ull, 5112, spell_mod_tractor_beam},  // 3d76980103: spell 0x16 (0x903-0x905)
    {"anti", 0x923b134f9fbf7b2dull, 3820, spell_mod_anti},  // b02674cbb8: spell 0x2a (0x964-0x966)
    {"raise_dead", 0x25e931c96cf8e120ull, 3916, spell_mod_raise_dead},  // 96440a6c9b: spell 0x34 (0x937 / 0x938)
    {"lunar_light", 0xb64cb9bcf0081599ull, 3140, spell_mod_lunar_light},  // e794431858: spell 0x1d (0x926 / 0x927)
    {"extinction", 0xd8f9aa517013e37full, 2884, spell_mod_extinction},  // 02571c0499: spell 0x25 (0x97c / 0x97d)
    {"press", 0xc01be6f5285d052dull, 2628, spell_mod_press},  // 6d0c9bf130: spell 0x27 (0x908 / 0x909)
    {"energy_net", 0x13d96fc281732aa9ull, 2600, spell_mod_energy_net},  // 32406ffe6a: spell 0x29 (0x946 / 0x947)
    {"neutral", 0x705d96c1ae6a4409ull, 2056, spell_mod_neutral},  // d43a9b4c0f: spell 0x3f (0x961-0x963)
    {"deep_mist", 0x55abe9218229d391ull, 1808, spell_mod_deep_mist},  // 60d0314477: spell 0x22 (0x92d-0x92f)
    {"bless", 0x296a02e0f0ad2cbeull, 1524, spell_mod_bless},  // 2a4716a80a: spell 0x3e (0x95e-0x960)
    {"forget", 0xe3e611f4f52ca8e6ull, 1160, spell_mod_forget},  // 68317e194a: spell 0x11 (0x8e5 / 0x8e6)
    {"sonic_saber", 0xd7bc50cb964fc346ull, 6132, spell_mod_sonic_saber},  // b82e4cf842: spell 0x9 (0x8ff / 0x900)
    {"meteor_swarm", 0x593f079fa0ca78c0ull, 4764, spell_mod_meteor_swarm},  // ac59fc1380: spell 0x13 (0x967-0x96a)
    {"eruption", 0xf4212c3e324257e1ull, 3856, spell_mod_eruption},  // 7f6a6e02c6: spell 0x2 (0x8fa-0x8fc)
    {"southern_cross", 0xf6009e34a2ff2387ull, 4116, spell_mod_southern_cross},  // dfd43383ca: spell 0x14 (0x96b-0x970)
    {"gravity_press", 0x5ae012f2edb00919ull, 3356, spell_mod_gravity_press},  // bf76cb13f3: spell 0x28 (0x90a / 0x90b)
    {"blood_sucker", 0x59f5b2c6eeaeecedull, 2928, spell_mod_blood_sucker},  // 6d3d24cf0b: spell 0x2d (0x8ec-0x8f2)
    {"fairy_heal", 0x94396ec4c9b9e767ull, 2904, spell_mod_fairy_heal},  // f8c3b94995: spell 0x31 (0x939-0x93d)
    {"shadow_flare", 0x812113bb6520f203ull, 2796, spell_mod_shadow_flare},  // ae70313b32: spell 0x21 (0x92a-0x92c)
    {"reflection", 0x6133cad9ba5f04e1ull, 2856, spell_mod_reflection},  // fb4ba65cff: spell 0xf (0x91c / 0x91d)
    {"delay", 0x14da3108c92485fbull, 2312, spell_mod_delay},  // 8e03dba0da: spell 0x38 (0x951-0x953)
    {"thunderstorm", 0x29a83656e7b5e3d5ull, 2008, spell_mod_thunderstorm},  // 9f010f460d: spell 0xe (0x8f6 / 0x8f7)
    {"haste", 0x93e30289ecad8ac0ull, 2248, spell_mod_haste},  // 83a78870f2: spell 0x37 (0x940-0x942)
    {"light_cross", 0x4dc6320b9d8fc461ull, 2184, spell_mod_light_cross},  // 440856a00a: spell 0x1b (0x8f8 / 0x8f9)
    {"spell40", 0x5011d64c919f9d1aull, 1880, spell_mod_spell40},  // e75959284f: spell 0x40 (0x948)
    {"acid_rain", 0xd7a14d91b10a789bull, 1632, spell_mod_acid_rain},  // 63f77d1822: spell 0x6 (0x8e3 / 0x8e4)
    {"curse", 0x3a1b08115878ff5cull, 1636, spell_mod_curse},  // efdb20dc3f: spell 0x3d (0x959-0x95d)
    // The monsters' spells (enemy agent).
    {"deep_freeze", 0x09e3adccc144b7f3ull, 4096, spell_mod_deep_freeze},  // 6673c9f485: spell 5 (0x8fd / 0x8fe)
    {"noah", 0xf392d11840affa9dull, 2436, spell_mod_noah},  // 6563c70f92: spell 7 (0x911-0x914)
    {"mind_blast", 0x1928033caaf50d2eull, 2908, spell_mod_mind_blast},  // c5209066cb: spell 0x43 (0x97e)
    {"spicule", 0x20cbe172629bc635ull, 5136, spell_mod_spicule},  // c325548547: spell 0x44 (0x97f)
    {"wind_of_destruction", 0xf32d2833195b97afull, 7771, spell_mod_wind_of_destruction},  // 155b3550d0: spell 0x45 (0x980)
    {"star_flare", 0xd4d293087c50a747ull, 2908, spell_mod_star_flare},  // cbdc2116f3: spell 0x1c (0x922-0x925)
    {"daemons_gate", 0x688cd2aa8e7fb3abull, 3493, spell_mod_daemons_gate},  // a6a642e880: spells 0x24 / 0x41 (0x971-0x973)
};

u64 fnv1a64(const std::vector<u8>& b) {
  u64 h = 0xcbf29ce484222325ull;
  for (u8 x : b) {
    h ^= x;
    h *= 0x100000001b3ull;
  }
  return h;
}

}  // namespace

int spell_module_find(const std::vector<u8>& block) {
  const u64 h = fnv1a64(block);
  for (size_t i = 0; i < std::size(kModules); ++i)
    if (kModules[i].fn != nullptr && kModules[i].hash == h && kModules[i].size == block.size()) return static_cast<int>(i);
  return -1;
}

SpellModuleFn spell_module_fn(int index) {
  if (index < 0 || index >= static_cast<int>(std::size(kModules))) return nullptr;
  return kModules[index].fn;
}

void spell_module_missing(SpellCast* sc) { sc->finished = 1; }

}  // namespace ss::game::battle
