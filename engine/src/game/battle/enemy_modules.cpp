// The monster factory's class choice (0x80075ad8) and the enemy-code slots.
//
// The console unpacks each block of the encounter pack's code list into its slot (kind 0
// A 0x80088110, 1 B 0x80089f18, 2 C 0x8008b1f0, 3 D 0x80088900); slots the pack leaves
// alone keep the #2129 image's default modules. The factory then calls a module's
// constructor (offset 0 of the slot) or stores a vtable at a fixed address inside the
// module. Here every module is a native class: the slot remembers which module the pack
// put there (by the block's bytes), and the factory builds that module's class when the
// type's entry is the one the module provides.

#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "disc/slz.h"
#include "game/battle/battle.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "runtime/region.h"
#include "runtime/runtime.h"

namespace ss::game::battle {

std::unique_ptr<EnemyUnit> enemy_family_create(int type);  // enemy_families.cpp

namespace {

using CreateFn = std::unique_ptr<EnemyUnit> (*)();

// A module: its bytes (FNV-1a 64 of the unpacked block and its size; 0 for the image's
// defaults), the slot it is linked for, the factory entry it serves (the constructor at
// the slot base or a vtable address) and the native class.
struct ModuleInfo {
  const char* name;
  u64 hash;
  u32 size;
  int slot;
  u32 entry;
  CreateFn create;
};

constexpr u32 kCtorA = 0x80088110, kCtorB = 0x80089F18, kCtorC = 0x8008B1F0, kCtorD = 0x80088900;

// The image's defaults (index = slot), then the 40 pack blocks of the two discs.
const ModuleInfo kModules[] = {
    {"guardian (#2129 slot A)", 0, 0, kSlotA, kCtorA, enemy_mod_guardian_create},
    {"nicolus (#2129 slot B)", 0, 0, kSlotB, kCtorB, enemy_mod_nicolus_create},
    {"jibril (#2129 slot C)", 0, 0, kSlotC, kCtorC, enemy_mod_jibril_create},
    {"cyril", 0x5e9f0228d768a083, 0x14E4, kSlotA, kCtorA, enemy_mod_cyril_create},
    {"carlaeagle", 0x3085673ee9f0df6e, 0x95A, kSlotA, kCtorA, enemy_mod_carlaeagle_create},
    {"gelatin", 0x987b55f3986dcda2, 0x8EC, kSlotA, kCtorA, enemy_mod_gelatin_create},
    {"arcmene", 0xd1da51dfd3165ca1, 0x6AC, kSlotA, 0x80088694, enemy_mod_arcmene_create},
    {"hounddog", 0x56bf579329a44fda, 0x7BC, kSlotA, 0x800887A4, enemy_mod_hounddog_create},
    {"salamander", 0xd3c7c17c8ca0531e, 0x530, kSlotA, 0x80088518, enemy_mod_salamander_create},
    {"kitty", 0x27114bae8a0c4b1b, 0x494, kSlotA, kCtorA, enemy_mod_kitty_create},
    {"ricki", 0xfa21a69db8cb84bf, 0x4EC, kSlotA, kCtorA, enemy_mod_ricki_create},
    {"ogre", 0x769815051fb892a4, 0x1F4, kSlotA, 0x800881DC, enemy_mod_ogre_create},
    {"ericodus", 0x848ca7dc10a8ac27, 0xC64, kSlotA, kCtorA, enemy_mod_ericodus_create},
    {"xine", 0x76948307e927c084, 0xB7C, kSlotA, kCtorA, enemy_mod_xine_create},
    {"succubus", 0xf5bd4c5d85c71b51, 0x7A8, kSlotA, kCtorA, enemy_mod_succubus_create},
    {"dias", 0xf457f7ac3a74ec40, 0xC94, kSlotA, kCtorA, enemy_mod_dias_create},
    {"ghost", 0x50bf5ad63036e944, 0x6AC, kSlotA, kCtorA, enemy_mod_ghost_create},
    {"zand", 0x7d247f198e139582, 0x8D8, kSlotA, kCtorA, enemy_mod_zand_create},
    {"gargoyle", 0x7a3f3496c0727922, 0x7E0, kSlotA, 0x800887C8, enemy_mod_gargoyle_create},
    {"bark", 0x4d001fd788cd7a13, 0x1588, kSlotA, kCtorA, enemy_mod_bark_create},
    {"synard", 0xf1de66d9da2c48d1, 0xA50, kSlotA, kCtorA, enemy_mod_synard_create},
    {"celesta", 0x5c0b167bedea25cb, 0xE30, kSlotA, kCtorA, enemy_mod_celesta_create},
    {"berle", 0x09007c82e2184f1d, 0x6FC, kSlotA, kCtorA, enemy_mod_berle_create},
    {"magichand", 0xda6c5e17a13689c5, 0xE98, kSlotA, 0x80088E78, enemy_mod_magichand_create},
    {"lover", 0x71e6ae24d6bc3e4f, 0x674, kSlotA, 0x8008865C, enemy_mod_lover_create},
    {"marsilio", 0xec3ce74cefe14b16, 0xC88, kSlotA, kCtorA, enemy_mod_marsilio_create},
    {"shigeo", 0xee8bf82079eb0a2c, 0x11B4, kSlotA, kCtorA, enemy_mod_shigeo_create},
    {"ruprecht", 0xde78c17af123a10d, 0x10D0, kSlotA, kCtorA, enemy_mod_ruprecht_create},
    {"vesper", 0x5582e1faa52b18ed, 0x12CC, kSlotA, kCtorA, enemy_mod_vesper_create},
    {"snowman", 0x783a6c2ae02aea3c, 0x430, kSlotA, 0x80088418, enemy_mod_snowman_create},
    {"bunny", 0x4e85d23e625f6c89, 0x718, kSlotB, 0x8008A508, enemy_mod_bunny_create},
    {"stingray", 0xed5d889c5d4aca7f, 0x958, kSlotB, kCtorB, enemy_mod_stingray_create},
    {"gerel", 0xc1d01490e0172da9, 0x748, kSlotB, 0x8008A538, enemy_mod_gerel_create},
    {"controller", 0x48f6bbef608e8cb3, 0x3E4, kSlotB, 0x8008A1D4, enemy_mod_controller_create},
    {"niquia", 0xad60263d2f49cbb6, 0x670, kSlotB, kCtorB, enemy_mod_niquia_create},
    {"azamgil", 0xe1ec81d431b62619, 0x2D8, kSlotB, 0x8008A0C8, enemy_mod_azamgil_create},
    {"visseyer", 0x4bd8121ca8a09a13, 0x884, kSlotB, kCtorB, enemy_mod_visseyer_create},
    {"guardbox", 0x2b82ba5af3224e01, 0x418, kSlotB, 0x8008A208, enemy_mod_guardbox_create},
    {"decus", 0x5392aebc6640b16b, 0x9C0, kSlotB, kCtorB, enemy_mod_decus_create},
    {"slime", 0x1e81cff0e1778bc3, 0x800, kSlotC, kCtorC, enemy_mod_slime_create},
    {"miel", 0x317eddc8175a087a, 0x754, kSlotC, kCtorC, enemy_mod_miel_create},
    {"shin", 0xe9bed7a34084fb5a, 0xE3C, kSlotC, kCtorC, enemy_mod_shin_create},
    {"indalecio", 0x9ca74aa5e7c894f8, 0x3304, kSlotD, kCtorD, enemy_mod_indalecio_create},
};

// What each slot holds this battle: a kModules index (-1: a block no table entry knows)
// and the block's bytes (empty for the image's defaults).
struct SlotState {
  int module = 0;
  std::vector<u8> bytes;
  const region::ImageMap* map = nullptr;  // another release's map of the block (runtime/region.h)
  std::string key;                        // its SS_REGION_READ_LOG key, "enemy.<NA FNV-1a 64>"
};
std::array<SlotState, 4> g_slots;

// The factory's switch (0x80075ad8) for the module types: the slot and the entry (the
// constructor at the slot base, or the vtable it stores).
struct FactoryEntry {
  int slot;
  u32 entry;
};
bool factory_entry(int type, FactoryEntry* out) {
  auto set = [&](int slot, u32 entry) {
    *out = {slot, entry};
    return true;
  };
  switch (type) {
    case 0xA: case 0xB: case 0xC: case 0x80: return set(kSlotB, 0x8008A508);
    case 0x10: case 0x11: case 0x12: case 0x81: case 0x82: case 0x83: return set(kSlotB, 0x8008A538);
    case 0x1F: case 0x20: case 0x21: case 0x8A: case 0x8B: return set(kSlotC, kCtorC);
    case 0x22: case 0x8C: case 0x8D: case 0x8E: return set(kSlotA, kCtorA);
    case 0x23: case 0x24: case 0x25: case 0x8F: return set(kSlotA, 0x80088694);
    case 0x26: case 0x27: case 0x28: case 0x90: case 0x91: return set(kSlotA, kCtorA);
    case 0x2C: case 0x2D: case 0x2E: return set(kSlotB, 0x8008A1D4);
    case 0x2F: case 0x30: case 0x31: case 0x93: return set(kSlotA, 0x800887A4);
    case 0x35: case 0x36: case 0x37: return set(kSlotB, kCtorB);
    case 0x38: case 0x39: case 0x3A: case 0x94: return set(kSlotA, kCtorA);
    case 0x3B: case 0x3C: case 0x3D: case 0x97: case 0x98: return set(kSlotB, kCtorB);
    case 0x47: case 0x48: case 0x49: case 0x95: case 0x96: return set(kSlotC, kCtorC);
    case 0x51: case 0x52: case 0x53: case 0x9E: case 0x9F: case 0xA0: return set(kSlotB, kCtorB);
    case 0x54: case 0x55: case 0x56: return set(kSlotA, 0x80088E78);
    case 0x57: case 0x58: case 0x59: return set(kSlotB, 0x8008A208);
    case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: return set(kSlotA, kCtorA);
    case 0x63: case 0x64: case 0x65: return set(kSlotA, kCtorA);
    case 0x66: case 0x67: case 0x68: return set(kSlotA, 0x80088418);
    case 0x69: case 0x6A: case 0x6B: return set(kSlotB, 0x8008A0C8);
    case 0x6C: case 0x6D: return set(kSlotA, 0x8008865C);
    case 0x71: case 0x72: case 0x73: case 0x74: case 0xA3: case 0xA4: case 0xA5: return set(kSlotA, 0x80088518);
    case 0x75: case 0x76: case 0xA6: return set(kSlotA, 0x800887C8);
    case 0x77: case 0x78: case 0xA7: case 0xA8: case 0xA9: return set(kSlotA, 0x800881DC);
    case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: case 0xB0: case 0xB1: case 0xB2:
    case 0xB8: case 0xBC: case 0xBD: case 0xBE: case 0xBF: case 0xC0: case 0xC1: case 0xC2: case 0xC3:
    case 0xC4: case 0xC5: case 0xC6: case 0xC7: case 0xC8: case 0xCB: case 0xCD: return set(kSlotA, kCtorA);
    case 0xB9: case 0xBA: case 0xBB: case 0xCA: return set(kSlotC, kCtorC);
    case 0xC9: case 0xCC: return set(kSlotB, kCtorB);
    case 0xCE: case 0xCF: return set(kSlotD, kCtorD);
    default: return false;
  }
}

void log_once(const std::string& key, const std::string& text) { runtime::Runtime::get().log_once(key, text); }

}  // namespace

void enemy_modules_reset() {
  for (int i = 0; i < 4; ++i) {
    g_slots[i].module = i < 3 ? i : -1;  // slot D has no default (Indalecio's encounters load it)
    g_slots[i].bytes.clear();
    g_slots[i].map = nullptr;
  }
}

void enemy_modules_load(int kind, std::span<const u8> packed) {
  if (kind < 0 || kind > 3) return;
  SlotState& s = g_slots[kind];
  auto un = disc::slz::unpack_block(packed, 0, 0, nullptr);
  s.bytes = un ? std::move(*un) : std::vector<u8>{};
  // The module is known by its normalised instructions (any release's link addresses); the
  // table's key is NA's FNV-1a 64 of the block.
  const u64 h = region::module_identity(region::ImageKind::kEnemy, s.bytes, &s.map);
  s.module = -1;
  for (size_t i = 0; i < std::size(kModules); ++i)
    if (h != 0 && kModules[i].hash == h && kModules[i].slot == kind) s.module = static_cast<int>(i);
  char key[32];
  std::snprintf(key, sizeof key, "enemy.%016llx", static_cast<unsigned long long>(h));
  s.key = key;
  if (!region::is_na() && !(s.map != nullptr && region::prepare(s.map, s.bytes))) s.bytes.clear();
  if (s.module < 0)
    log_once("battle-enemy-module-unknown-" + std::to_string(region::fnv1a64(s.bytes)),
             "battle: unknown enemy code block for slot " + std::to_string(kind) + " (monsters there use the base class)");
}

u8 enemy_module_u8(int slot, u32 address) {
  if (slot < 0 || slot > 3) return 0;
  const SlotState& s = g_slots[slot];
  if (s.module >= 0 && kModules[s.module].hash == 0) {
    const u8* p = battle_data(address);
    return p != nullptr ? *p : 0;
  }
  const u8* p = region::image_byte(s.map, s.bytes, kSlotBase[slot], address, s.key.c_str());
  return p != nullptr ? *p : 0;
}

s16 enemy_module_s16(int slot, u32 address) {
  return static_cast<s16>(enemy_module_u8(slot, address) | enemy_module_u8(slot, address + 1) << 8);
}

s32 enemy_module_s32(int slot, u32 address) {
  return static_cast<s32>(static_cast<u32>(enemy_module_u8(slot, address)) |
                          static_cast<u32>(enemy_module_u8(slot, address + 1)) << 8 |
                          static_cast<u32>(enemy_module_u8(slot, address + 2)) << 16 |
                          static_cast<u32>(enemy_module_u8(slot, address + 3)) << 24);
}

std::unique_ptr<EnemyUnit> enemy_class_create(int type) {
  if (auto e = enemy_family_create(type)) return e;
  FactoryEntry fe{};
  if (!factory_entry(type, &fe)) {
    // The base types (worms 4-6, saucers 0x3e-0x40) and the types the switch lacks.
    switch (type) {
      case 4: case 5: case 6: case 0x3E: case 0x3F: case 0x40: return std::make_unique<EnemyUnit>();
      default: return nullptr;
    }
  }
  const SlotState& s = g_slots[fe.slot];
  const ModuleInfo* m = s.module >= 0 ? &kModules[s.module] : nullptr;
  if (m != nullptr && m->entry == fe.entry && m->create != nullptr) return m->create();
  const std::string why = m == nullptr ? "an unknown module"
                          : m->entry != fe.entry ? std::string("module ") + m->name + ", which lacks its class"
                                                 : std::string("module ") + m->name + " (not ported yet)";
  log_once("battle-enemy-module-" + std::to_string(type),
           "battle: monster type " + std::to_string(type) + " uses " + why + "; base behaviour");
  return std::make_unique<EnemyUnit>();
}

}  // namespace ss::game::battle
