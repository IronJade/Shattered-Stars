#include "runtime/libcard.h"

#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "psx/memory_card.h"

namespace ss::psyq {
namespace {

struct Slot {
  std::filesystem::path path;
  psx::MemoryCard card;
  bool inserted = true;
  bool loaded = false;  // image opened
  bool new_card = true; // the BIOS reports a card as new until _new_card / a write to frame 0
};
Slot g_slots[2];

Slot* slot_for_chan(long chan) {
  const long port = (chan >> 4) & 1;
  return &g_slots[port];
}

psx::MemoryCard* card(Slot& s) {
  if (!s.inserted || s.path.empty()) return nullptr;
  if (!s.loaded) s.loaded = s.card.open(s.path, true);
  return s.loaded ? &s.card : nullptr;
}

// Events: handle -> (class, spec, fired).
struct Event {
  u32 desc;
  long spec;
  bool enabled = false;
  bool fired = false;
};
std::map<long, Event> g_events;
long g_next_event = 0xF1000000;

void fire(u32 desc, long spec) {
  for (auto& [handle, e] : g_events) {
    if (e.desc == desc && e.spec == spec && e.enabled) e.fired = true;
  }
}

// Open files.
struct OpenFile {
  int slot;
  psx::MemoryCard::File file;
  long position = 0;
  bool async = false;
};
std::map<long, OpenFile> g_files;
long g_next_fd = 3;

// "buXY:NAME" -> slot (X) and name.
bool parse_path(const char* path, int& slot, std::string& name) {
  if (std::strncmp(path, "bu", 2) != 0 || path[2] < '0' || path[2] > '1' || path[4] != ':') return false;
  slot = path[2] - '0';
  name = path + 5;
  return true;
}

bool matches(const std::string& pattern, const std::string& name) {
  size_t i = 0;
  for (; i < pattern.size(); ++i) {
    if (pattern[i] == '*') return true;
    if (i >= name.size() || (pattern[i] != '?' && pattern[i] != name[i])) return false;
  }
  return i == name.size();
}

struct Listing {
  int slot = 0;
  std::string pattern;
  std::vector<psx::MemoryCard::File> files;
  size_t next = 0;
};
Listing g_listing;

DIRENTRY* fill(DIRENTRY* dir, const psx::MemoryCard::File& f) {
  std::memset(dir, 0, sizeof *dir);
  std::strncpy(dir->name, f.name.c_str(), sizeof dir->name - 1);
  dir->attr = 0x51;
  dir->size = static_cast<long>(f.size);
  dir->head = f.first_block;
  return dir;
}

}  // namespace

void card_set_image(int slot, const std::filesystem::path& path) {
  g_slots[slot].path = path;
  g_slots[slot].loaded = false;
}

void card_set_inserted(int slot, bool inserted) { g_slots[slot].inserted = inserted; }

long OpenEvent(u32 desc, long spec, long mode, void (*)()) {
  const long handle = g_next_event++;
  g_events[handle] = Event{desc, spec};
  (void)mode;
  return handle;
}

long CloseEvent(long event) { return g_events.erase(event) ? 1 : 0; }

long EnableEvent(long event) {
  auto it = g_events.find(event);
  if (it == g_events.end()) return 0;
  it->second.enabled = true;
  return 1;
}

long DisableEvent(long event) {
  auto it = g_events.find(event);
  if (it == g_events.end()) return 0;
  it->second.enabled = false;
  return 1;
}

long TestEvent(long event) {
  auto it = g_events.find(event);
  if (it == g_events.end() || !it->second.fired) return 0;
  it->second.fired = false;
  return 1;
}

void EnterCriticalSection() {}
void ExitCriticalSection() {}

long _card_info(long chan) {
  Slot* s = slot_for_chan(chan);
  if (!card(*s)) {
    fire(kSwCard, kEvSpTimeout);
  } else if (s->new_card) {
    fire(kSwCard, kEvSpNew);
  } else {
    fire(kSwCard, kEvSpIoe);
  }
  return 1;
}

long _card_load(long chan) {
  Slot* s = slot_for_chan(chan);
  psx::MemoryCard* c = card(*s);
  if (!c) {
    fire(kSwCard, kEvSpTimeout);
  } else if (!c->formatted()) {
    fire(kSwCard, kEvSpNew);  // unformatted
  } else {
    fire(kSwCard, kEvSpIoe);
  }
  return 1;
}

long _card_clear(long chan) {
  Slot* s = slot_for_chan(chan);
  if (card(*s)) s->new_card = false;
  fire(kHwCard, card(*s) ? kEvSpIoe : kEvSpTimeout);
  return 1;
}

long _card_write(long chan, long frame, const u8* buf) {
  Slot* s = slot_for_chan(chan);
  psx::MemoryCard* c = card(*s);
  if (!c) {
    fire(kHwCard, kEvSpTimeout);
    return 0;
  }
  // The game writes frame 0 only to clear the "new card" flag; keep the image intact.
  if (frame != 0) c->write_frame(static_cast<int>(frame), buf);
  s->new_card = false;
  fire(kHwCard, kEvSpIoe);
  return 1;
}

long _card_read(long chan, long frame, u8* buf) {
  psx::MemoryCard* c = card(*slot_for_chan(chan));
  if (!c) {
    fire(kHwCard, kEvSpTimeout);
    return 0;
  }
  c->read_frame(static_cast<int>(frame), buf);
  fire(kHwCard, kEvSpIoe);
  return 1;
}

void _new_card() {}

long open(const char* path, unsigned long mode) {
  int slot = 0;
  std::string name;
  if (!parse_path(path, slot, name)) return -1;
  psx::MemoryCard* c = card(g_slots[slot]);
  if (!c) return -1;
  if (mode & 0x200) {
    const int blocks = static_cast<int>(mode >> 16) > 0 ? static_cast<int>(mode >> 16) : 1;
    if (!c->create(name, blocks)) return -1;
  }
  auto f = c->find(name);
  if (!f) return -1;
  const long fd = g_next_fd++;
  g_files[fd] = OpenFile{slot, *f, 0, (mode & 0x8000) != 0};
  return fd;
}

long lseek(long fd, long offset, long whence) {
  auto it = g_files.find(fd);
  if (it == g_files.end()) return -1;
  long pos = whence == 0 ? offset : whence == 1 ? it->second.position + offset : static_cast<long>(it->second.file.size) + offset;
  if (pos < 0 || pos > static_cast<long>(it->second.file.size)) return -1;
  it->second.position = pos;
  return pos;
}

long read(long fd, void* buf, long n) {
  auto it = g_files.find(fd);
  if (it == g_files.end()) return -1;
  OpenFile& f = it->second;
  n &= ~0x7FL;  // whole 128-byte frames
  if (f.position + n > static_cast<long>(f.file.size)) n = static_cast<long>(f.file.size) - f.position;
  psx::MemoryCard* c = card(g_slots[f.slot]);
  if (!c || !c->read(f.file, static_cast<u32>(f.position), static_cast<u8*>(buf), static_cast<u32>(n))) return -1;
  f.position += n;
  if (f.async) fire(kSwCard, kEvSpIoe);
  return n;
}

long write(long fd, const void* buf, long n) {
  auto it = g_files.find(fd);
  if (it == g_files.end()) return -1;
  OpenFile& f = it->second;
  n &= ~0x7FL;
  if (f.position + n > static_cast<long>(f.file.size)) n = static_cast<long>(f.file.size) - f.position;
  psx::MemoryCard* c = card(g_slots[f.slot]);
  if (!c || !c->write(f.file, static_cast<u32>(f.position), static_cast<const u8*>(buf), static_cast<u32>(n))) return -1;
  f.position += n;
  if (f.async) fire(kSwCard, kEvSpIoe);
  return n;
}

long close(long fd) { return g_files.erase(fd) ? fd : -1; }

DIRENTRY* firstfile(const char* pattern, DIRENTRY* dir) {
  int slot = 0;
  std::string name;
  g_listing = Listing{};
  if (!parse_path(pattern, slot, name)) return nullptr;
  psx::MemoryCard* c = card(g_slots[slot]);
  if (!c) return nullptr;
  g_listing.slot = slot;
  g_listing.pattern = name;
  for (auto& f : c->files())
    if (matches(name, f.name)) g_listing.files.push_back(f);
  return nextfile(dir);
}

DIRENTRY* nextfile(DIRENTRY* dir) {
  if (g_listing.next >= g_listing.files.size()) return nullptr;
  return fill(dir, g_listing.files[g_listing.next++]);
}

long erase(const char* path) {
  int slot = 0;
  std::string name;
  if (!parse_path(path, slot, name)) return 0;
  psx::MemoryCard* c = card(g_slots[slot]);
  return c && c->erase(name) ? 1 : 0;
}

long format(const char* device) {
  int slot = 0;
  std::string name;
  if (!parse_path(device, slot, name)) return 0;
  psx::MemoryCard* c = card(g_slots[slot]);
  if (!c) return 0;
  c->format();
  g_slots[slot].new_card = false;
  return 1;
}

}  // namespace ss::psyq
