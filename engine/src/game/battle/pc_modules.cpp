#include "game/battle/pc_modules.h"

#include <array>
#include <vector>

#include "disc/slz.h"
#include "game/battle/battle.h"
#include "runtime/runtime.h"

namespace ss::game::battle {

std::unique_ptr<PartyUnit> pc_claude_create();  // pc_claude.cpp
std::unique_ptr<PartyUnit> pc_rena_create();    // pc_casters.cpp
std::unique_ptr<PartyUnit> pc_celine_create();
std::unique_ptr<PartyUnit> pc_leon_create();
std::unique_ptr<PartyUnit> pc_noel_create();
std::unique_ptr<PartyUnit> pc_ernest_create();  // pc_ernest.cpp

namespace {

// The code entry of each character (g_char_battle_entries 0x80084ea8 [character][variant]
// + 1; the weapon variants' code is identical).
constexpr std::array<int, 13> kCodeEntry = {0, 2131, 2135, 2137, 2139, 2143, 2155, 2147, 2141, 2153, 2151, 2157, 2159};
constexpr u32 kModuleBase = 0x801EE000;

std::array<std::vector<u8>, 13> g_images;  // block 0 (slot-0 link) by character
std::array<bool, 13> g_loaded{};

const std::vector<u8>* module_image(int character) {
  if (character < 1 || character > 12) return nullptr;
  if (!g_loaded[character]) {
    g_loaded[character] = true;
    BattleGlobals& g = bg();
    const int entry = kCodeEntry[character];
    std::vector<u8> packed(static_cast<size_t>(g.loader->entry_size(entry)));
    if (!packed.empty()) {
      g.loader->load_sync(packed.data(), entry, 0);
      if (auto un = disc::slz::unpack_block(packed, 0, 0, nullptr)) g_images[character] = std::move(*un);
    }
  }
  return &g_images[character];
}

}  // namespace

std::unique_ptr<PartyUnit> pc_module_create(int character) {
  switch (character) {
    case 1: return pc_claude_create();
    case 2: return pc_rena_create();
    case 3: return pc_celine_create();
    case 8: return pc_leon_create();
    case 10: return pc_ernest_create();
    case 11: return pc_noel_create();
    default:
      runtime::Runtime::get().log_once("battle-module-" + std::to_string(character),
                                       "battle: the party-character module of character " +
                                           std::to_string(character) + " is not ported (no killer moves)");
      return std::make_unique<PartyUnit>();
  }
}

u8 pc_module_u8(int character, u32 address) {
  const std::vector<u8>* img = module_image(character);
  const u32 off = address - kModuleBase;
  return img != nullptr && off < img->size() ? (*img)[off] : 0;
}

void pc_modules_reset() {
  for (auto& i : g_images) i.clear();
  g_loaded.fill(false);
}

}  // namespace ss::game::battle
