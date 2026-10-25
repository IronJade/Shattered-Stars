#include "game/ap/hub.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include "game/ap/ap.h"
#include "game/field/field.h"

// The hub's chapter lock and story views (hub.h; docs/ap/seed_format.md "Hub") [hubvm agent].
namespace ss::game::ap {

bool g_hub_vm = false;

namespace {

constexpr u32 kChapter = 0xA0;     // G[0xa0]: the chapter (game_state +0)
constexpr u32 kStep = 0xA4;        // G[0xa4]: the step (game_state +4)
constexpr u32 kViewBytes = 0x340;  // the script globals a view can redirect (to the game state's end)
constexpr int kFlagCount = 0x170 * 8;

struct View {
  bool on = false;
  size_t boss = 0;                        // the row in Hub::bosses
  std::array<bool, kViewBytes> redirected{};
  std::array<u8, kViewBytes> bytes{};     // the view's copy of the redirected bytes
  std::map<int, bool> flags;              // the view's flags (current values)
  std::map<int, bool> start_flags;        // ... as the view started
  std::set<u32> stored;                   // redirected bytes a script stored to
  std::set<int> flags_stored;             // view flags a script stored to
  std::set<int> flags_read;               // (SS_HUB_TRACE: flags whose first read was traced)
  bool won = false;                       // the boss was won in this view
  bool cut = false;                       // cut_to sent the scene's transfer elsewhere: end at the next load
  int party_swallowed = 0;
};

struct Hub {
  bool on = false;
  bool lock_chapter = true;
  bool lock_step = true;
  std::vector<int> step_maps;
  std::vector<HubBoss> bosses;
  View view;
  std::set<long long> logged_sites;       // (map << 32 | offset << 1 | step?) logged once
  bool disc_logged = false;
  std::vector<std::string> log;
};

Hub& hub() {
  static Hub h;
  return h;
}

void say(const std::string& line) {
  std::fprintf(stderr, "[ap] hub: %s\n", line.c_str());
  Hub& h = hub();
  if (h.log.size() < 4096) h.log.push_back(line);
}

int current_map() {
  const u8* gs = field::globals().game_state;
  if (gs == nullptr) return -1;
  s16 m;
  std::memcpy(&m, gs + 0x1A, 2);
  return m;
}

bool real_flag(int flag) {
  const u8* f = field::globals().script_flags;
  return f != nullptr && flag >= 0 && flag < kFlagCount && (f[flag >> 3] >> (flag & 7) & 1) != 0;
}

void set_real_flag(int flag, bool on) {
  u8* f = field::globals().script_flags;
  if (f == nullptr || flag < 0 || flag >= kFlagCount) return;
  const u8 bit = static_cast<u8>(1 << (flag & 7));
  if (on && (f[flag >> 3] & bit) == 0) on_flag_set(flag);  // (a chest's opened flag is a check)
  f[flag >> 3] = on ? static_cast<u8>(f[flag >> 3] | bit) : static_cast<u8>(f[flag >> 3] & ~bit);
}

// The hero's game: Claude's with script flag 1 set (a row's `game`: 1 Claude's, 2 Rena's).
bool game_matches(const HubBoss& b) {
  if (b.game == 0) return true;
  return (b.game == 1) == real_flag(1);
}

bool in_maps(const HubBoss& b, int map) { return std::find(b.maps.begin(), b.maps.end(), map) != b.maps.end(); }
bool in_view_maps(const HubBoss& b, int map) {  // maps or after_maps: where a view on goes on
  return in_maps(b, map) || std::find(b.view.after_maps.begin(), b.view.after_maps.end(), map) != b.view.after_maps.end();
}

std::string boss_label(const HubBoss& b) {
  return (b.name.empty() ? std::string("boss") : b.name) + ", encounter " + std::to_string(b.encounter);
}

s32 view_word(const View& v, u32 addr) {
  s32 w;
  std::memcpy(&w, v.bytes.data() + addr, 4);
  return w;
}

void put_bytes(View& v, u32 addr, int size, u32 value) {
  for (int i = 0; i < size; ++i) {
    v.redirected[addr + i] = true;
    v.bytes[addr + i] = static_cast<u8>(value >> (8 * i));
  }
}

void view_start(Hub& h, size_t index, int map) {
  const HubBoss& b = h.bosses[index];
  View& v = h.view;
  v = View{};
  v.on = true;
  v.boss = index;
  put_bytes(v, kChapter, 4, static_cast<u32>(b.view.chapter));
  put_bytes(v, kStep, 4, static_cast<u32>(b.view.step));
  for (const StoryView::Global& g : b.view.globals)
    if (g.addr >= 0 && static_cast<u32>(g.addr + g.size) <= kViewBytes) put_bytes(v, static_cast<u32>(g.addr), g.size, static_cast<u32>(g.value));
  v.flags = b.view.flags;
  v.start_flags = b.view.flags;
  say("story view on: map " + std::to_string(map) + " (" + boss_label(b) + "): chapter " +
      std::to_string(b.view.chapter) + " step " + std::to_string(b.view.step) + ", " + std::to_string(b.view.flags.size()) +
      " flags, " + std::to_string(b.view.globals.size()) + " globals");
}

// The view ends: the keep flags are written to the real flags; everything else it took is dropped.
void view_end(Hub& h, const std::string& why) {
  View& v = h.view;
  if (!v.on) return;
  const HubBoss& b = h.bosses[v.boss];
  std::string dropped;
  for (u32 a = kChapter; a < kStep + 4; a += 4) {
    bool any = false;
    for (u32 i = 0; i < 4; ++i) any = any || v.stored.count(a + i) != 0;
    if (any) dropped += std::string(dropped.empty() ? "" : ", ") + (a == kChapter ? "chapter " : "step ") + std::to_string(view_word(v, a));
  }
  for (const StoryView::Global& g : b.view.globals) {
    bool any = false;
    for (int i = 0; i < g.size; ++i) any = any || v.stored.count(static_cast<u32>(g.addr + i)) != 0;
    if (!any) continue;
    u32 value = 0;
    for (int i = 0; i < g.size; ++i) value |= static_cast<u32>(v.bytes[static_cast<size_t>(g.addr + i)]) << (8 * i);
    char buf[48];
    std::snprintf(buf, sizeof buf, "G[%#x] %d", g.addr, static_cast<int>(value));
    dropped += std::string(dropped.empty() ? "" : ", ") + buf;
  }
  std::string kept;
  for (const int flag : v.flags_stored) {
    const bool value = v.flags[flag];
    const bool keep = std::find(b.view.keep.begin(), b.view.keep.end(), flag) != b.view.keep.end();
    std::string& out = keep ? kept : dropped;
    out += std::string(out.empty() ? "" : ", ") + "flag " + std::to_string(flag) + "=" + (value ? "1" : "0");
  }
  for (const int flag : b.view.keep)
    if (v.flags_stored.count(flag) != 0) set_real_flag(flag, v.flags[flag]);
  if (v.party_swallowed > 0) dropped += std::string(dropped.empty() ? "" : ", ") + std::to_string(v.party_swallowed) + " party changes";
  say("story view off: map " + std::to_string(current_map()) + " (" + boss_label(b) + "; " + why + "): dropped " +
      (dropped.empty() ? "nothing" : dropped) + (kept.empty() ? "" : "; kept " + kept));
  v = View{};
}

bool overlaps(u32 addr, int size, u32 lo, u32 hi) { return addr < hi && addr + static_cast<u32>(size) > lo; }

// Debugging (SS_HUB_TRACE=1): every global store a script makes while a view is on, the ones the
// view takes and the ones it lets through (for writing a chamber's view spec).
bool tracing() {
  static const bool on = std::getenv("SS_HUB_TRACE") != nullptr;
  return on;
}
void trace_store(const char* what, const std::string& where, long long value, bool taken) {
  std::fprintf(stderr, "[ap] hub trace: %s %s = %lld at map %d (%s)\n", what, where.c_str(), value, current_map(),
               taken ? "view" : "through");
}

}  // namespace

// ---- The VM's hooks ----

bool hub_var_read(u32 addr, int size, const u8* real, u32* out) {
  const View& v = hub().view;
  if (!v.on || addr >= kViewBytes) return false;
  bool any = false;
  u32 value = 0;
  for (int i = 0; i < size; ++i) {
    const u32 a = addr + static_cast<u32>(i);
    const bool mine = a < kViewBytes && v.redirected[a];
    any = any || mine;
    value |= static_cast<u32>(mine ? v.bytes[a] : real[i]) << (8 * i);
  }
  if (any) *out = value;
  return any;
}

bool hub_var_write(u32 addr, int size, u32 value, u8* real, int at) {
  Hub& h = hub();
  if (!h.on) return false;
  View& v = h.view;
  if (v.on && tracing() && addr >= kChapter) {  // (the game state; not the field state block below it)
    char where[48];
    std::snprintf(where, sizeof where, "G[%#x]:%d @%#x", static_cast<unsigned>(addr), size, static_cast<unsigned>(at));
    bool taken = false;
    for (int i = 0; i < size; ++i) taken = taken || (addr + i < kViewBytes && v.redirected[addr + i]);
    trace_store("store", where, static_cast<long long>(static_cast<s32>(value)), taken);
  }
  if (v.on && addr < kViewBytes) {
    bool any = false;
    for (int i = 0; i < size; ++i) any = any || (addr + i < kViewBytes && v.redirected[addr + i]);
    if (any) {
      for (int i = 0; i < size; ++i) {
        const u32 a = addr + static_cast<u32>(i);
        const u8 byte = static_cast<u8>(value >> (8 * i));
        if (a < kViewBytes && v.redirected[a]) {
          v.bytes[a] = byte;
          v.stored.insert(a);
        } else {
          real[i] = byte;
        }
      }
      return true;
    }
  }
  // The lock (outside a view): the chapter always, the step except on the step maps.
  if (addr >= kStep + 4 || addr + static_cast<u32>(size) <= kChapter) return false;  // (most stores)
  const bool chapter = h.lock_chapter && overlaps(addr, size, kChapter, kChapter + 4);
  const int map = current_map();
  const bool step = !chapter && h.lock_step && overlaps(addr, size, kStep, kStep + 4) &&
                    std::find(h.step_maps.begin(), h.step_maps.end(), map) == h.step_maps.end();
  if (!chapter && !step) return false;
  const long long site = static_cast<long long>(map) << 32 | static_cast<long long>(at) << 1 | (step ? 1 : 0);
  if (h.logged_sites.insert(site).second) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s store %d dropped at map %d offset %#x (the lock)", chapter ? "chapter" : "step",
                  static_cast<int>(size == 4 ? value : value & ((1u << (8 * size)) - 1)), map, static_cast<unsigned>(at));
    say(buf);
  }
  return true;
}

bool hub_flag_read(int flag, bool* on) {
  View& v = hub().view;
  if (!v.on) return false;
  const auto it = v.flags.find(flag);
  if (tracing() && v.flags_read.insert(flag).second)  // (each flag's first read in the view)
    std::fprintf(stderr, "[ap] hub trace: read flag %d = %d at map %d (%s)\n", flag,
                 it != v.flags.end() ? (it->second ? 1 : 0) : (real_flag(flag) ? 1 : 0), current_map(),
                 it != v.flags.end() ? "view" : "real");
  if (it == v.flags.end()) return false;
  *on = it->second;
  return true;
}

bool hub_flag_write(int flag, bool on) {
  View& v = hub().view;
  if (!v.on) return false;
  const auto it = v.flags.find(flag);
  if (tracing()) trace_store("flag", std::to_string(flag), on ? 1 : 0, it != v.flags.end());
  if (it == v.flags.end()) return false;
  it->second = on;
  v.flags_stored.insert(flag);
  return true;
}

bool hub_swallows_party(bool add, int character) {
  View& v = hub().view;
  if (!v.on) return false;
  ++v.party_swallowed;
  say(std::string("story view: party_") + (add ? "add " : "remove ") + character_name(character + 1) + " at map " +
      std::to_string(current_map()) + " dropped");
  return true;
}

bool hub_swallows_disc_change() {
  Hub& h = hub();
  if (!h.on) return false;
  if (!h.disc_logged) say("disc switch (B 0x6a) at map " + std::to_string(current_map()) + " dropped: the hub stays on disc 2");
  h.disc_logged = true;
  return true;
}

// ---- game/ap's hooks ----

void hub_start(const Seed* seed) {
  Hub& h = hub();
  h = Hub{};
  if (seed == nullptr || !seed->hub) {
    g_hub_vm = false;
    return;
  }
  h.on = true;
  h.lock_chapter = seed->hub_lock_chapter;
  h.lock_step = seed->hub_lock_step;
  h.step_maps = seed->hub_step_maps;
  h.bosses = seed->bosses;
  g_hub_vm = true;
}

void hub_reset() {
  Hub& h = hub();
  if (h.view.on) say("story view reset (New Game or a load): nothing written back");
  h.view = View{};
}

void hub_map_loaded(int map) {
  Hub& h = hub();
  if (!h.on) return;
  if (h.view.on && (h.view.cut || !in_view_maps(h.bosses[h.view.boss], map)))
    view_end(h, "map " + std::to_string(map) + " loaded");
  if (h.view.on) return;
  for (size_t i = 0; i < h.bosses.size(); ++i) {
    const HubBoss& b = h.bosses[i];
    if (!in_maps(b, map) || !game_matches(b) || goal_won(b.encounter)) continue;
    if (!b.has_view) {
      say("chamber map " + std::to_string(map) + " (" + boss_label(b) + "): no story view, the scene plays at the hub's state");
      continue;
    }
    view_start(h, i, map);
    return;
  }
}

void hub_free_play() {
  Hub& h = hub();
  if (h.on && h.view.on && h.view.won && !h.bosses[h.view.boss].view.until_transfer) view_end(h, "free play after the battle");
}

bool hub_map_transfer(s32* args) {
  Hub& h = hub();
  if (!h.on || !h.view.on || !h.view.won || h.view.cut) return false;
  const HubBoss& b = h.bosses[h.view.boss];
  if (!b.view.has_cut_to || in_view_maps(b, args[0])) return false;
  const DoorTarget& t = b.view.cut_to;
  say("story view: " + boss_label(b) + ": the transfer to map " + std::to_string(args[0]) + " goes to map " +
      std::to_string(t.map) + " (" + std::to_string(t.x) + ", " + std::to_string(t.y) + ", " + std::to_string(t.z) +
      ") instead (cut_to)");
  args[0] = t.map;
  args[2] = t.x;
  args[3] = t.y;
  args[4] = t.z;
  if (t.dir >= 0) args[5] = t.dir;
  h.view.cut = true;
  return true;
}

bool hub_boss_encounter(int encounter) {
  const Hub& h = hub();
  if (!h.on) return false;
  return std::any_of(h.bosses.begin(), h.bosses.end(), [&](const HubBoss& b) { return b.encounter == encounter; });
}

void hub_battle_won(int encounter) {
  Hub& h = hub();
  if (!h.on || !h.view.on || h.bosses[h.view.boss].encounter != encounter) return;
  h.view.won = true;
  say("story view: " + boss_label(h.bosses[h.view.boss]) + " won; the view ends at the next free play or transfer");
}

bool hub_view_on() { return hub().view.on; }
int hub_view_encounter() { return hub().view.on ? hub().bosses[hub().view.boss].encounter : -1; }

std::string hub_status() {
  const Hub& h = hub();
  if (!h.on) return "";
  std::string out = std::string("hub | lock chapter ") + (h.lock_chapter ? "on" : "off") + " step " + (h.lock_step ? "on" : "off") +
                    " | chambers " + std::to_string(h.bosses.size());
  if (h.view.on) {
    const HubBoss& b = h.bosses[h.view.boss];
    out += " | story view " + boss_label(b) + " (chapter " + std::to_string(view_word(h.view, kChapter)) + " step " +
           std::to_string(view_word(h.view, kStep)) + (h.view.won ? ", won" : "") + ")";
  }
  return out;
}

const std::vector<std::string>& hub_log() { return hub().log; }

}  // namespace ss::game::ap
