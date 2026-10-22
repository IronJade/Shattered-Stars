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
