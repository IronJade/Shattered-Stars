// The battle program's load queue. See bload.h.

#include "game/battle/bload.h"

#include <cstdlib>

#include "game/battle/battle.h"
#include "game/boot/loader.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

constexpr int kSlots = 16;

// The queue (parallel arrays on the console: entry 0x8006fbb8, owner 0x8006fbf8, group
// 0x8006fc3c, priority 0x8006fc5c, dest 0x8006fdb8).
struct Request {
  s32 entry = 0;
  u32 owner = 0;
  s16 group = 0;
  s16 priority = 0;
  void* dest = nullptr;
};
Request g_queue[kSlots];
// The read in progress: entry 0x8006fbb4 (0 = idle), owner 0x8006fbb0, group 0x8006fc38,
// dest 0x8006fdb4.
Request g_cur;
u32 g_owner = 0;  // g_bload_owner 0x8006fbac

// (host) The console time (in vblanks) when the drive delivers the read's last sector.
double g_busy_until = 0;

constexpr double kVblankHz = 60000.0 / 1001.0;
constexpr double kSeekSecondsPerSector = 3e-6;  // PCSX-Redux cdrom.cc computeSeekDelay
constexpr double kSeekSeconds = 0.001 + 0.030;  // ... plus the read command's round trip
constexpr double kSectorVblanks = kVblankHz / 75.0;  // single speed

double now() { return static_cast<double>(runtime::Runtime::get().vblanks()); }

// loader_poll: nonzero while the drive is still reading (only with SS_BLOAD_DRIVE_TIME).
bool loader_busy() {
  static const bool timed = std::getenv("SS_BLOAD_DRIVE_TIME") != nullptr;
  return timed && now() < g_busy_until;
}

void loader_cancel() { g_busy_until = 0; }

// loader_start_read (and the sector read 0x80011988): the host reads at once; the drive's
// time runs from the head to the first sector and over every sector read.
void start_read(s32 entry, s16 group, void* dest) {
  BattleGlobals& g = bg();
  if (g.loader == nullptr || g.loader->archive() == nullptr || dest == nullptr) return;
  Loader& ld = *g.loader;
  disc::Archive& a = *ld.archive();
  u32 lba = 0;
  u32 sectors = 0;
  if (entry < 0x10000) {
    int bytes = 0;
    if (group == 6) bytes = ld.entry_size(entry) + ld.entry_size(entry + 1);
    if (!a.has_entry(static_cast<u32>(entry))) return;
    lba = a.entry(static_cast<u32>(entry)).lba;
    sectors = static_cast<u32>((bytes > 0 ? bytes : ld.entry_size(entry)) + 0x7FF) / 0x800;
    const u32 head = ld.head();
    ld.select_entry(entry);
    ld.start_read(dest, bytes);
    const double distance = head > lba ? head - lba : lba - head;
    g_busy_until = now() + (distance * kSeekSecondsPerSector + kSeekSeconds) * kVblankHz + sectors * kSectorVblanks;
    return;
  }
  const u32 file = static_cast<u32>((entry >> 16) - 1);
  const u32 sector = static_cast<u32>(entry & 0xFFFF);
  if (!a.has_entry(file)) return;
  lba = a.entry(file).lba + sector;
  const u32 head = ld.head();
  a.read_from_entry(file, 0x800, static_cast<u8*>(dest), sector);
  ld.set_head(lba + 1);
  const double distance = head > lba ? head - lba : lba - head;
  g_busy_until = now() + (distance * kSeekSecondsPerSector + kSeekSeconds) * kVblankHz + kSectorVblanks;
}

// 0x800479c4 bload_compact: the requests left move up in order; the tail is cleared.
void compact() {
  int n = 0;
  for (int i = 0; i < kSlots; ++i)
    if (g_queue[i].entry != 0) g_queue[n++] = g_queue[i];
  for (; n < kSlots; ++n) {
    g_queue[n].entry = 0;
    g_queue[n].group = 0;
  }
}

// 0x80047c98 bload_start_next: once the drive is idle the read in progress is over; the
// head of the queue starts.
void start_next() {
  if (loader_busy()) return;
  g_cur.entry = 0;
  if (g_queue[0].entry == 0) return;
  g_cur = g_queue[0];
  start_read(g_cur.entry, g_cur.group, g_cur.dest);
  for (int i = 1; i < kSlots; ++i) {
    g_queue[i - 1] = g_queue[i];
    if (g_queue[i - 1].entry == 0) break;
  }
  g_queue[kSlots - 1] = Request{};
}

}  // namespace

void bload_request(int entry, void* dest, s16 group, int priority) {
  int at = -1;
  if (-1 < priority) {
    for (int i = 0; i < kSlots; ++i) {
      if (g_queue[i].entry != 0 && (priority < g_queue[i].priority || g_queue[i].priority == -1)) {
        at = i;
        break;
      }
    }
  }
  if (at == -1) {
    for (int i = 0; i < kSlots; ++i) {
      if (g_queue[i].entry == 0) {
        at = i;
        break;
      }
    }
    if (at == -1) return;
  }
  if (g_queue[at].entry != 0)
    for (int i = kSlots - 2; at <= i; --i) g_queue[i + 1] = g_queue[i];
  g_queue[at].entry = entry;
  g_queue[at].dest = dest;
  g_queue[at].group = group;
  g_queue[at].priority = static_cast<s16>(priority);
  g_queue[at].owner = g_owner;
}

void bload_cancel_group(int group) {
  if (group == 0xFF) {
    for (Request& r : g_queue) r.entry = 0;
    return;
  }
  for (Request& r : g_queue)
    if (r.group == group) r.entry = 0;
  compact();
  if (g_cur.group == group && g_cur.entry != 0) {
    g_cur.entry = 0;
    loader_cancel();
  }
}

void bload_cancel(int entry, void* dest) {
  if (g_cur.entry == entry && (dest == nullptr || dest == g_cur.dest)) {
    loader_cancel();
    g_cur.entry = 0;
    return;
  }
  for (Request& r : g_queue) {
    if (r.entry == entry && (dest == nullptr || dest == r.dest)) {
      r.entry = 0;
      compact();
      return;
    }
  }
}

void bload_cancel_dest(void* dest) {
  if (dest == g_cur.dest) {
    loader_cancel();
    g_cur.entry = 0;
    return;
  }
  for (Request& r : g_queue) {
    if (r.dest == dest) {
      r.entry = 0;
      compact();
      return;
    }
  }
}

bool bload_done(int entry) {
  if (entry == 0) return g_cur.entry == 0 && g_queue[0].entry == 0;
  if (g_cur.entry == entry) {
    if (g_owner == 0 || g_owner == g_cur.owner) return false;
  }
  for (const Request& r : g_queue) {
    if (r.entry != entry) continue;
    if (g_owner == 0 || g_owner == r.owner) return false;
  }
  return true;
}

int bload_busy_except_group(int group) {
  if (g_cur.entry != 0 && g_cur.group != group) return 1;
  for (const Request& r : g_queue)
    if (r.entry != 0 && r.group != group) return 1;
  return 0;
}

void bload_update() {
  const s32 ticks = bg().frame_ticks;
  for (Request& r : g_queue) {
    if (r.entry == 0 || r.priority <= 0) continue;
    r.priority = static_cast<s16>(r.priority - ticks);
    if (r.priority < 0) r.priority = 0;
  }
  start_next();
}

u32& bload_owner() { return g_owner; }

void bload_reset() {
  for (Request& r : g_queue) r = Request{};
  g_cur = Request{};
  g_owner = 0;
  g_busy_until = 0;
}

}  // namespace ss::game::battle
