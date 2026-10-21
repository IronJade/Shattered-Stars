#include "game/boot/loader.h"

#include <cstdio>

namespace ss::game {

int Loader::entry_size(int entry) const {
  return archive_ ? static_cast<int>(archive_->entry_size(static_cast<u32>(entry))) : 0;
}

int Loader::select_entry(int entry) {
  selected_ = entry;
  return 1;
}

int Loader::start_read(void* dest, int bytes) {
  if (!archive_ || selected_ < 0) return 0;
  const size_t count = bytes > 0 ? static_cast<size_t>(bytes) : archive_->entry_size(static_cast<u32>(selected_));
  if (!archive_->read_from_entry(static_cast<u32>(selected_), count, static_cast<u8*>(dest))) {
    std::fprintf(stderr, "[loader] read of entry %d (%zu bytes) failed\n", selected_, count);
    return 0;
  }
  return 1;
}

int Loader::load_sync(void* dest, int entry, int bytes) {
  select_entry(entry);
  return start_read(dest, bytes);
}

}  // namespace ss::game
