#include "game/title/load_queue.h"

#include "game/title/title_program.h"

namespace ss::game {
namespace {

struct LoadQueue {
  u16 entry[16] = {};      // 0x8004ecb0
  void* dest[16] = {};     // 0x8004ec70
  u16 current = 0;         // 0x8004ecd0: entry being read
  void* current_dest = nullptr;  // 0x8004ec6c
};
LoadQueue g_queue;

Loader* loader() { return title_globals().loader; }

}  // namespace

void load_queue_reset() {
  g_queue.current = 0;
  g_queue.current_dest = nullptr;
  for (int i = 0; i < 16; ++i) {
    g_queue.dest[i] = nullptr;
    g_queue.entry[i] = 0;
  }
}

void async_load(int entry, void* dest, int slot) {
  if (slot == -1) {
    for (int i = 0; i < 16; ++i) {
      if (g_queue.entry[i] == 0) {
        slot = i;
        break;
      }
    }
    if (slot == -1) return;
  }
  if (g_queue.entry[slot] != 0) {
    for (int i = 15; i > slot; --i) {
      g_queue.entry[i] = g_queue.entry[i - 1];
      g_queue.dest[i] = g_queue.dest[i - 1];
    }
  }
  g_queue.entry[slot] = static_cast<u16>(entry);
  g_queue.dest[slot] = dest;
}

void async_load_cancel(int entry, const void* dest) {
  if (g_queue.current == entry && (dest == nullptr || dest == g_queue.current_dest)) {
    loader()->cancel();
    g_queue.current = 0;
    return;
  }
  for (int i = 0; i < 16; ++i) {
    if (g_queue.entry[i] == entry && (dest == nullptr || dest == g_queue.dest[i])) {
      g_queue.entry[i] = 0;
      load_queue_compact();
      return;
    }
  }
}

void load_queue_compact() {
  int kept = 0;
  for (int i = 0; i < 16; ++i) {
    if (g_queue.entry[i] != 0) {
      g_queue.entry[kept] = g_queue.entry[i];
      g_queue.dest[kept] = g_queue.dest[i];
      ++kept;
    }
  }
  for (; kept < 16; ++kept) {
    g_queue.entry[kept] = 0;
    g_queue.dest[kept] = nullptr;
  }
}

bool async_load_done(int entry) {
  if (entry == 0) return g_queue.current == 0 && g_queue.entry[0] == 0;
  if (g_queue.current == entry) return false;
  for (int i = 0; i < 16; ++i) {
    if (g_queue.entry[i] == entry) return false;
  }
  return true;
}

void load_queue_suspend() {
  if (loader()->poll() != 0) {
    loader()->cancel();
    async_load(g_queue.current, g_queue.current_dest, 0);
    g_queue.current = 0;
    g_queue.current_dest = nullptr;
  }
}

void load_queue_update() {
  if (loader()->poll() != 0) return;
  g_queue.current = 0;
  if (g_queue.entry[0] == 0) return;
  g_queue.current = g_queue.entry[0];
  g_queue.current_dest = g_queue.dest[0];
  loader()->select_entry(g_queue.current);
  loader()->start_read(g_queue.current_dest, 0);
  for (int i = 0; i < 15; ++i) {
    g_queue.entry[i] = g_queue.entry[i + 1];
    g_queue.dest[i] = g_queue.dest[i + 1];
    if (g_queue.entry[i] == 0) break;
  }
  g_queue.entry[15] = 0;
  g_queue.dest[15] = nullptr;
}

}  // namespace ss::game
