#pragma once

#include <vector>

#include "core/types.h"
#include "game/common/party.h"

// A list of offered items (0x14 bytes; every page overlay with item lists has a copy, e.g.
// 0xbbc 0x8007e1c8, 0xbc0 0x8007e33c): +0 count, +4 capacity, +8 the next position, +0xc
// the inventory, +0x10 entries of 8 bytes (position, item, count).
namespace ss::game::field {

struct ItemList {
  struct Entry {
    s16 index = 0;   // +0: the position the entry was offered at
    s16 item = 0;    // +2
    s16 count = 0;   // +4
  };
  std::vector<Entry> entries;
  s32 next = 0;

  int count() const { return static_cast<int>(entries.size()); }
  const Entry& at(int i) const { return entries[static_cast<size_t>(i)]; }

  // 0xbc0 0x8007e418: 'item' if the inventory has it (its position counts either way).
  void add_owned(int item) {
    const int n = inventory_count(item);
    const s16 at = static_cast<s16>(next++);
    if (n != 0) entries.push_back({at, static_cast<s16>(item), static_cast<s16>(n)});
  }
  // 0xbc0 0x8007e4bc
  void add_owned_range(int first, int last) {
    for (int i = first; i <= last; ++i) add_owned(i);
  }
  // 0xbc0 0x8007e51c: a 0-terminated list.
  void add_owned_list(const s16* items) {
    if (!items) return;
    for (; *items != 0; ++items) add_owned(*items);
  }
  // 0xbc0 0x8007e574: an entry with the item's inventory count.
  void add(int item) {
    entries.push_back({static_cast<s16>(next++), static_cast<s16>(item), static_cast<s16>(inventory_count(item))});
  }
  // 0xbae 0x8007e654
  bool contains(int item) const {
    for (const Entry& e : entries) {
      if (e.item == item) return true;
    }
    return false;
  }
};

}  // namespace ss::game::field
