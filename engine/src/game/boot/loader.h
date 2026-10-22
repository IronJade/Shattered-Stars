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

// The host's disc drive (port). The console asks the player to swap discs until
// loader_is_disc agrees (the field's field_prompt_disc_change 0x80052ce8 / insert-disc
// screen 0x8006f958, the title's 0x8003c5ec); the port registers the archives of the
// configured images (disc 1 always, disc 2 with --disc2) and swaps them itself.
void loader_register_disc(int disc, disc::Archive* archive);
// Attaches the archive of the requested disc (1 or 2: the one holding marker entry
// 5 + disc, as loader_is_disc tests) if it is not already in the drive. False when
// no configured image is that disc; the loader keeps its current archive.
bool loader_insert_disc(Loader& loader, int disc);

}  // namespace ss::game
