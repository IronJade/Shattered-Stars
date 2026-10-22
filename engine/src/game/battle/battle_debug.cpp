#include "game/battle/battle_debug.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "game/battle/battle.h"
#include "game/common/battle_setup.h"
#include "game/field/encounter.h"
#include "game/field/field.h"
#include "game/title/title_program.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

u32 dump32(const std::vector<u8>& ram, u32 address) {
  const u32 at = address & 0x1FFFFF;
  u32 v = 0;
  if (at + 4 <= ram.size()) std::memcpy(&v, ram.data() + at, 4);
  return v;
}

// Copies handoff 'tag' of the dump's System (0x8002b550 -> registry at +0x5a20) over the
// port's block of the same tag.
bool copy_handoff(const std::vector<u8>& ram, int tag, size_t bytes) {
  const u32 sys = dump32(ram, 0x8002B550);
  const u32 reg = sys + 0x5A20;
  for (int i = 0; i < 16; ++i) {
    if (static_cast<int>(dump32(ram, reg + 64 + 4 * i)) != tag) continue;
    const u32 at = dump32(ram, reg + 4 * i) & 0x1FFFFF;
    auto* dst = static_cast<u8*>(handoff_get(&get_system()->handoff, tag));
    if (dst == nullptr || at + bytes > ram.size()) return false;
    std::memcpy(dst, ram.data() + at, bytes);
    return true;
  }
  return false;
}

}  // namespace

void battle_debug_set_end(const std::string& end) {
  if (end.empty()) return;
  int frames = 0, outcome = 0;
  std::sscanf(end.c_str(), "%i:%i", &frames, &outcome);
  battle_debug().auto_end_frames = frames;
  battle_debug().auto_end_outcome = static_cast<u8>(outcome);
}

Program battle_debug_start(const std::string& spec, const std::string& end) {
  int encounter = 1, stage = 3;
  std::sscanf(spec.c_str(), "%i:%i", &encounter, &stage);
  battle_debug_set_end(end);
  // What the title program leaves for a new game: config, party slots, character
  // records and the inventory (create_game_state 0x80061c58), with Claude in slot 0.
  create_game_state();
  HandoffRegistry* reg = &get_system()->handoff;
  auto* party = static_cast<u8*>(handoff_get(reg, 3));
  auto* records = static_cast<u8*>(handoff_get(reg, 4));
  party_slot_init(party, records, 1);
  auto* setup = new BattleSetup;
  bool have_setup = false;
  if (const char* path = std::getenv("SS_BATTLE_RAM"); path != nullptr && *path != 0) {
    std::ifstream in(path, std::ios::binary);
    std::vector<u8> ram((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (ram.size() >= 0x200000) {
      const bool ok = copy_handoff(ram, 2, 0x2A0) && copy_handoff(ram, 3, 0x300) && copy_handoff(ram, 4, 0x680) &&
                      copy_handoff(ram, 6, 0xC28);
      std::fprintf(stderr, "[battle] SS_BATTLE_RAM %s: handoffs %s\n", path, ok ? "copied" : "not found");
      // field_exit_to_program dumps: the field's setup block (0x800752b8 points at it).
      const u32 p = dump32(ram, 0x800752B8) & 0x1FFFFF;
      const s32 enc = p != 0 ? static_cast<s32>(dump32(ram, 0x80000000u | p)) : 0;
      if (p != 0 && p + 0xC0 <= ram.size() && enc > 0 && enc < 0x240) {
        std::memcpy(setup->raw.data(), ram.data() + p, 0xC0);
        for (int i = 0; i < 13; ++i) setup->set(0x74 + 4 * i, 0);
        have_setup = true;
        std::fprintf(stderr, "[battle] setup from the dump: encounter %d stage %d\n", setup->encounter(),
                     setup->stage());
      }
    }
  }
  if (!have_setup) {
    // As the field builds it (battle_build_load_list reads the party through the field's
    // bound globals).
    field::field_bind_system(nullptr);
    field::battle_build_load_list(setup, encounter, stage);
  }
  handoff_store(reg, setup, 5);
  runtime::Runtime::get().trace("battle debug start");
  return Program::Battle;
}

}  // namespace ss::game::battle
