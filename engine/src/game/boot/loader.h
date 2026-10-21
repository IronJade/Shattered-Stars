#pragma once

#include "core/types.h"
#include "disc/archive.h"

namespace ss::game {

// The archive loader the System object owns at +0x10 (boot 0x80011xxx).
// The console streams sectors asynchronously; here reads complete inside
// loader_start_read, so loader_poll always reports idle and the game's
// "while (loader_poll(...))" waits fall straight through.
class Loader {
 public:
  void attach(disc::Archive* archive) { archive_ = archive; }
  disc::Archive* archive() const { return archive_; }

  int entry_size(int entry) const;                       // loader_entry_size
  int select_entry(int entry);                           // loader_select_entry: 1 = accepted
  int start_read(void* dest, int bytes);                 // loader_start_read: 0 = whole entry
  int poll() const { return 0; }                         // loader_poll: nonzero while busy
  void cancel() {}                                       // loader_cancel
  int load_sync(void* dest, int entry, int bytes);       // loader_load_sync

 private:
  disc::Archive* archive_ = nullptr;
  int selected_ = -1;
};

}  // namespace ss::game
