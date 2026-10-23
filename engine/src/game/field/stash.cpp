#include "game/field/stash.h"

#include <cstring>

#include "game/field/field.h"
#include "game/field/script_vm.h"

namespace ss::game::field {
namespace {

constexpr int kInventoryBytes = 0xC28;
u8* g_stash = nullptr;     // 0x80076228
bool g_stashed = false;    // 0x8007622c

// Every entry empty, with fresh check bytes.
void clear_entries(u8* inv) {
  for (int i = 0; i < 0x400; ++i) {
    std::memset(inv + i * 2, 0, 2);
    inv[0x824 + i] = inventory_check_byte(inv, static_cast<u32>(i), 0xFFFF);
  }
}

}  // namespace

u8* stash_table() { return g_stash; }
bool stash_holds_items() { return g_stashed; }
void stash_set_holds_items(bool on) { g_stashed = on; }

void stash_init() {
  FieldGlobals& g = globals();
  if (auto* kept = static_cast<u8*>(handoff_get(g.handoff, 0x11))) {
    handoff_clear(g.handoff, 0x11);
    g_stash = kept;
    g_stashed = true;
    return;
  }
  g_stash = new u8[kInventoryBytes]();
  clear_entries(g_stash);
  std::memset(g_stash + 0x800, 0, 0x20);  // the 16 recent-item words
  g_stash[0xC24] = 0;
  g_stashed = false;
}

void inventory_stash() {
  FieldGlobals& g = globals();
  std::memcpy(g_stash, g.inventory, kInventoryBytes);
  clear_entries(g.inventory);
  g_stashed = true;
}

void inventory_unstash() {
  if (!g_stashed) return;
  FieldGlobals& g = globals();
  for (int i = 0; i < 0x400; ++i) {
    u16 e;
    std::memcpy(&e, g_stash + i * 2, 2);
    if ((e & 0x3FF) != 0) inventory_add(g.inventory, e & 0x3FF, e >> 10 & 0x1F, e >> 15);
  }
  g_stashed = false;
}

void inventory_clear_all() { clear_entries(globals().inventory); }

}  // namespace ss::game::field
