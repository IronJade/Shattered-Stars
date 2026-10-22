#include "game/common/save_icon.h"

#include <cstring>
#include <string>
#include <vector>

#include "disc/slz.h"
#include "game/boot/system.h"
#include "game/common/menu_core.h"

namespace ss::game::menu {
namespace {

constexpr int kIconEntry = 0xBD3;
constexpr size_t kLeftoverShift = 0x454;  // the file buffer starts this far into the freed sheet

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
s16 rd16(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}

}  // namespace

int field_save_icon_index(const u8* gs) {
  const s32 progress = rd32(gs);
  const s32 a = (static_cast<s32>(rd16(gs + 0x24C)) * 0x68) / 0x4EC;
  if (progress - 3 < a) return ((a * 0xF) / 0x68 & 0xF) + 0x10;
  return ((progress - 3) * 0xF) / 0x39 & 0xF;
}

void field_state_to_snapshot() {
  auto* block = static_cast<const u8*>(handoff_get(menu_handoff(), 9));
  auto* snapshot = static_cast<u8*>(handoff_get(menu_handoff(), 0xE));
  if (block == nullptr || snapshot == nullptr) return;
  std::memcpy(snapshot, block + 0x220, 0x2A0);          // game_state
  std::memcpy(snapshot + 0x2A0, block + 0x8C0, 0x170);  // script flags
}

const u8* field_save_prefill(u8* file) {
  static u8 icon[0x1A0];
  field_state_to_snapshot();
  auto* block = static_cast<const u8*>(handoff_get(menu_handoff(), 9));
  Loader* loader = menu_loader();
  const int size = loader != nullptr ? loader->entry_size(kIconEntry) : 0;
  if (block == nullptr || size <= 0) return nullptr;
  const int index = field_save_icon_index(block + 0x220);
  std::vector<u8> packed(static_cast<size_t>(size));
  if (!loader->load_sync(packed.data(), kIconEntry, 0)) return nullptr;
  // The entry is a stored block holding two offsets, each to an SLZ sheet of sixteen.
  auto table = disc::slz::unpack_block(packed, 0, 0, nullptr);
  if (!table || table->size() < 8) return nullptr;
  const size_t offset = static_cast<size_t>(rd32(table->data() + (index >> 4) * 4));
  auto sheet = disc::slz::unpack_block(*table, offset, 0, nullptr);
  const size_t k = static_cast<size_t>(index & 0xF);
  if (!sheet || sheet->size() < 0x220 + 16 * 0x180) return nullptr;
  for (size_t i = 0; i + kLeftoverShift < sheet->size() && i < 0x2000; ++i) file[i] = (*sheet)[i + kLeftoverShift];
  // Palette (0x20 bytes from +0x14), then the 24-byte rows of the three frames side by
  // side (from +0x220) split into three 16x16 frames.
  std::memcpy(icon, sheet->data() + 0x14 + k * 0x20, 0x20);
  for (size_t f = 0; f < 3; ++f)
    for (size_t r = 0; r < 16; ++r)
      std::memcpy(icon + 0x20 + f * 0x80 + r * 8, sheet->data() + 0x220 + k * 0x180 + f * 8 + r * 0x18, 8);
  return icon;
}

}  // namespace ss::game::menu
