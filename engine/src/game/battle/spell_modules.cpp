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
    {"feehn", 0xaaf512758b30accfull, 2236, spell_mod_feehn},  // 2a9c041ec7: spell 0xa (0x915 / 0x916)
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
