#include "game/ap/hub.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include "game/ap/ap.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/message.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"
#include "runtime/region.h"
#include "runtime/runtime.h"

// The hub's chapter lock and story views (hub.h; docs/ap/seed_format.md "Hub") [hubvm agent].
namespace ss::game::ap {

bool g_hub_vm = false;
int g_hub_sites = 0;

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
  int settle = 0;                         // free-play frames in a row after the win (hub_free_play)
  u64 settle_vblank = 0;                  // ... the last one's vblank
};

// Free-play frames in a row that end a won view (a sixth of a second at 60 Hz): well past the
// battle's hand-back frame, and short enough that a walk straight back into a trigger region meets
// the real state (the event sweep fires its next step after 20 idle frames).
constexpr int kSettleFrames = 10;

// A row's question at a transfer into its chamber (hub_ask_transfer's phases) [hubask agent].
struct Ask {
  int phase = 0;                          // 0 none; 1 the question window printing; 2 the choice up; 3 the windows closing
  size_t boss = 0;
  int64_t door = -1;                      // the transfer's door id ...
  int map = -1;                           // ... on this map
  bool yes = false;
  s32 sent[6]{};                          // the transfer as the door table sent it (map .. dir)
  int x = 8, w = 28;                      // the question window (cells): its answers go under it
};
// The answer the next map load takes (a yes at a question or an extra option chosen).
struct Decision {
  bool set = false;
  size_t boss = 0;
  bool yes = false;
};
// An `ask.options` entry on the loaded map: its words checked, at the loaded release's offsets.
struct Site {
  size_t boss = 0, option = 0;
  u32 window = 0, choice = 0, act = 0;    // word indices in the code: the options window, the choice, after its store
  int count = 0;                          // the choice's own option count (the extra option is this index)
  bool logged = false;
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
  // [hubask agent] the questions
  Ask ask;
  Decision decision;
  std::vector<Site> sites;
  int text_index = -1;                    // the script string hub_script_text extends now ...
  const Site* text_site = nullptr;        // ... for this site
  std::vector<u8> question_text, answers_text, options_text;  // (alive while their windows show)
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

// Why the field is not in free play by its own rule (field/leader.cpp free_play, the camp menu's
// test field_menu_check plus the choice), or null: an event or talk running, the map's thread
// waiting on the music (1), a fade (3), a transfer / load (8) or a choice (9), the controls locked
// (ext 0x7b), a fade or a window up. (ap's in_free_play already has the controls on and depth 0.)
const char* scene_busy() {
  if (const field::ScriptCtx* c = field::g_script_vm) {
    const s32 w = c->wait_state;
    if (c->active_trigger != -1 || c->talk_active != 0) return "an event or talk";
    if (c->depth > 0) return "a routine";
    if (w == 1 || w == 3 || w == 8 || w == 9) return "the thread waits";
  }
  if (const u8* ff = field::globals().field_flags; ff != nullptr && ff[0x26] != 0) return "the controls locked";
  if (field::ScreenFade* f = field::field_fade(); f != nullptr && field::fade_is_active(f, -1)) return "a fade";
  if (field::MsgSystem* m = field::msg_system()) {
    const int status = field::msg_window_status(m, -1);
    if (status != 1 && status != -1) return "a window";
  }
  return nullptr;
}

// ---- The questions [hubask agent] ----

// The map font's codes: one byte below 0x80, else two.
void put_code(std::vector<u8>& out, u32 c) {
  if (c < 0x80) {
    out.push_back(static_cast<u8>(c));
  } else {
    out.push_back(static_cast<u8>((c & 0x7F) | 0x80));
    out.push_back(static_cast<u8>(c >> 7));
  }
}
// A text's code at 'p' and its size with the control codes' parameters (ap.cpp text_code).
u32 text_code(const u8* p, size_t* size) {
  u32 c = p[0];
  size_t n = 1;
  if (c >= 0x80) {
    c = (c & 0x7F) | static_cast<u32>(p[1]) << 7;
    n = 2;
  }
  switch (c) {
    case 0x4003: case 0x4004: case 0x4007: case 0x4008: case 0x400C: case 0x400D: case 0x400E: case 0x400F: n += 1; break;
    case 0x4006: case 0x4009: n += 2; break;
    default: break;
  }
  *size = n;
  return c;
}
// The cursor's room before each option line, as the game's own choices write it: glyphs 0xfe, 0xfe,
// 0xea on NA and Europe (maps 297 / 305 strings 156 / 111); France and Germany, with fonts of their
// own, 0x72 (their space) four times and 0x82 (every option string of maps 297 / 305).
void put_lead_in(std::vector<u8>& out) {
  const region::Id id = region::current().id;
  if (id == region::Id::kFrance || id == region::Id::kGermany) {
    for (const u32 c : {0x72u, 0x72u, 0x72u, 0x72u, 0x82u}) put_code(out, c);
  } else {
    for (const u32 c : {0xFEu, 0xFEu, 0xEAu}) put_code(out, c);
  }
}

bool row_open(const HubBoss& b) { return b.ask.on && game_matches(b) && !goal_won(b.encounter); }

std::string join_lines(const std::vector<std::string>& lines) {
  std::string out;
  for (const std::string& l : lines) out += (out.empty() ? "" : " / ") + l;
  return out;
}

// An options site may show its extra option: the boss unbeaten and, on a chamber map, its view on.
bool site_open(const Hub& h, const Site& s) {
  const HubBoss& b = h.bosses[s.boss];
  if (!row_open(b)) return false;
  if (in_maps(b, current_map())) return h.view.on && h.view.boss == s.boss && !h.view.won;
  return true;
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
  g_hub_sites = 0;
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
  h.ask = Ask{};            // [hubask agent] (a question left up; no answer kept)
  h.decision = Decision{};
}

void hub_map_loaded(int map) {
  Hub& h = hub();
  if (!h.on) return;
  // [hubask agent] An answer is for the load it led to; a question left up ends with any load.
  const Decision decision = h.decision;
  h.decision = Decision{};
  h.ask = Ask{};
  if (h.view.on && (h.view.cut || !in_view_maps(h.bosses[h.view.boss], map)))
    view_end(h, "map " + std::to_string(map) + " loaded");
  if (h.view.on) return;
  for (size_t i = 0; i < h.bosses.size(); ++i) {
    const HubBoss& b = h.bosses[i];
    if (!in_maps(b, map) || !game_matches(b)) continue;
    // A won boss: no view, unless the scene's end is still to come by a walk (view.done_flag clear:
    // the Field of Power's 604 vision after the Guardian at 603) [hubchambers].
    const bool won = goal_won(b.encounter);
    if (won && (!b.has_view || b.view.done_flag < 0 || real_flag(b.view.done_flag))) continue;
    if (!b.has_view) {
      say("chamber map " + std::to_string(map) + " (" + boss_label(b) + "): no story view, the scene plays at the hub's state");
      continue;
    }
    // [hubask agent] A chamber that asks: its view only after a yes (at its doors or an extra
    // option); a no, a load or any other way in leaves the chamber at the hub's own state.
    if (b.ask.on && !won && !(decision.set && decision.boss == i && decision.yes)) {
      say("chamber map " + std::to_string(map) + " (" + boss_label(b) + "): no story view (" +
          (decision.set && decision.boss == i ? "the answer was no" : "not asked: a load or another way in") +
          "), the map at the hub's state");
      continue;
    }
    view_start(h, i, map);
    if (won) {
      h.view.won = true;
      say("story view: " + boss_label(b) + " won before; on until flag " + std::to_string(b.view.done_flag) + " is set");
    }
    return;
  }
}

void hub_free_play() {
  Hub& h = hub();
  View& v = h.view;
  if (!h.on || !v.on || !v.won || h.bosses[v.boss].view.until_transfer) return;
  // The scene must be over, not just handing the controls back for a frame: ap's free play (the
  // controls on, the script at depth 0) holds for a frame or two when a battle returns to the field
  // mid-scene, the scene's thread then waiting on the map's load (Shin 447: the view ended there and
  // the aftermath's step store ran outside it). So the field's own free-play rule (field/leader.cpp
  // free_play) for kSettleFrames frames in a row.
  const u64 now = runtime::Runtime::get().vblanks();
  const char* busy = scene_busy();
  if (busy != nullptr) {
    if (v.settle > 0 && tracing())
      std::fprintf(stderr, "[ap] hub trace: free play at vblank %llu map %d not settled (%s)\n",
                   static_cast<unsigned long long>(now), current_map(), busy);
    v.settle = 0;
    return;
  }
  v.settle = v.settle > 0 && now <= v.settle_vblank + 4 ? v.settle + 1 : 1;  // (field frames: 1-2 vblanks)
  v.settle_vblank = now;
  if (v.settle >= kSettleFrames) view_end(h, "free play after the battle");
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

// ---- The questions [hubask agent] ----

bool hub_ask_busy(int64_t door) {
  const Hub& h = hub();
  return h.on && h.ask.phase != 0 && h.ask.door == door && h.ask.map == current_map();
}

bool hub_ask_transfer(int64_t door, const s32* own, s32* args) {
  Hub& h = hub();
  if (!h.on) return false;
  field::ScriptCtx* ctx = field::g_script_vm;
  Ask& a = h.ask;
  const int map = current_map();
  if (a.phase != 0 && (a.door != door || a.map != map || ctx == nullptr)) a = Ask{};  // (another transfer: that one ended)
  if (a.phase == 0) {
    if (ctx == nullptr || ctx->sync != 0) return false;  // (a synchronous call can't wait for an answer)
    for (size_t i = 0; i < h.bosses.size(); ++i) {
      const HubBoss& b = h.bosses[i];
      if (!row_open(b) || !in_maps(b, args[0]) || in_maps(b, map)) continue;
      if (h.view.on && h.view.boss == i) continue;
      if (!b.ask.all_doors && std::find(b.ask.doors.begin(), b.ask.doors.end(), door) == b.ask.doors.end()) continue;
      if (h.decision.set && h.decision.boss == i) return false;  // (answered already: an extra option)
      a = Ask{};
      a.phase = 1;
      a.boss = i;
      a.door = door;
      a.map = map;
      std::memcpy(a.sent, args, sizeof a.sent);
      // The question as the game's own yes / no questions are laid out (map 297's fee: ext 0x69 at
      // (8, 2), the hold window 1; then ext 0x7f(1) waits for it to print).
      h.question_text.clear();
      for (size_t k = 0; k < b.ask.text.size(); ++k) {
        if (k > 0) put_code(h.question_text, 0x4000);
        const std::vector<u8> g = glyph_text(b.ask.text[k]);
        h.question_text.insert(h.question_text.end(), g.begin(), g.end());
      }
      h.question_text.push_back(0);
      // Wide enough for its longest line in this map's font: a line wraps past the window's width
      // less 16 pixels (field/message_text.cpp text_layout_line); a cell to spare, the screen is 40.
      int widest = 0;
      for (const std::string& line : b.ask.text) widest = std::max(widest, glyph_text_width(line));
      a.w = std::clamp((widest + 0x10 + 7) / 8 + 1, 20, 38);
      a.x = (40 - a.w) / 2;
      const int lines = static_cast<int>(b.ask.text.size());
      field::vm_svc_msg_window_open(a.x, 2, a.w, lines, h.question_text.data(), 1, 1);
      field::vm_svc_msg_window_set_hold(1, 1);
      field::vm_svc_msg_window_set_frame(1, 1, 1);
      ctx->wait_state = 2;
      ctx->wait_param = 1;
      char buf[96];
      std::snprintf(buf, sizeof buf, "door %lld (map %d -> map %d)", static_cast<long long>(door), map, args[0]);
      say("ask: " + boss_label(b) + ": " + buf + ": \"" + join_lines(b.ask.text) + "\" (window " + std::to_string(a.w) +
          " cells, its widest line " + std::to_string(widest) + " px" + (widest > a.w * 8 - 0x10 ? ": too wide, it wraps)" : ")"));
      return true;
    }
    return false;
  }
  const HubBoss& b = h.bosses[a.boss];
  switch (a.phase) {
    case 1: {
      // The answers (ext 0x68, window 0, the cursor's room before each line; ext 0xaa 0 0) and the
      // choice (ext 0xdc 2: the first option first, cancel allowed, which answers no).
      const int lines = static_cast<int>(b.ask.text.size());
      h.answers_text.clear();
      put_lead_in(h.answers_text);
      std::vector<u8> g = glyph_text(b.ask.yes);
      h.answers_text.insert(h.answers_text.end(), g.begin(), g.end());
      put_code(h.answers_text, 0x4000);
      put_lead_in(h.answers_text);
      g = glyph_text(b.ask.no);
      h.answers_text.insert(h.answers_text.end(), g.begin(), g.end());
      h.answers_text.push_back(0);
      field::vm_svc_msg_window_open(a.x + 1, 2 + (lines * 13 + 24) / 8, std::min(24, a.w - 2), 2, h.answers_text.data(), 0, 1);
      field::vm_svc_msg_window_set_hold(0, 1);
      field::vm_svc_msg_window_set_frame(0, 1, 1);
      field::vm_svc_msg_window_set_24(0, 0);
      ctx->choice_count = 2;
      ctx->choice_first_line = 0;
      ctx->choice_selection = 0;
      ctx->choice_cancel = 1;
      ctx->wait_param = 0;
      if (!field::vm_svc_msg_window_exists(0)) {
        field::vm_set_r(0xFFFFFFFFu);
      } else {
        field::vm_svc_msg_window_set_cancel(0, 1);
        field::vm_svc_msg_window_set_close_flag(0, 0);
        ctx->wait_state = 9;
        ctx->choice_close = 0;
        ctx->choice_cursor[0].x = -1;
        ctx->choice_slide = 0;
      }
      a.phase = 2;
      return true;
    }
    case 2:
      // The answer (R: 0 yes, 1 no, -1 cancelled), then both windows close (ext 0x81 0 / 1, 0x7f -1).
      a.yes = field::vm_r() == 0;
      field::vm_svc_msg_window_close(0);
      field::vm_svc_msg_window_close(1);
      ctx->wait_state = 2;
      ctx->wait_param = -1;
      a.phase = 3;
      return true;
    default: {
      const Ask done = a;
      a = Ask{};
      s32 mine[6];  // (the script's own target; 'own' may be 'args' itself)
      std::memcpy(mine, own, sizeof mine);
      own = mine;
      std::memcpy(args, done.sent, sizeof done.sent);  // (as the door table sent it)
      if (done.yes) {
        h.decision = Decision{true, done.boss, true};
        say("ask: " + boss_label(b) + ": yes: map " + std::to_string(args[0]) + " with its story view");
        return false;
      }
      if (!in_maps(b, own[0])) {
        // A seed door sent the transfer into the chamber (to.while_unbeaten): no is its own target.
        args[0] = own[0];
        args[2] = own[2];
        args[3] = own[3];
        args[4] = own[4];
        args[5] = own[5];
        say("ask: " + boss_label(b) + ": no: door " + std::to_string(done.door) + " goes to its own target (map " +
            std::to_string(own[0]) + ")");
        return false;
      }
      h.decision = Decision{true, done.boss, false};
      say("ask: " + boss_label(b) + ": no: map " + std::to_string(args[0]) + " at the hub's state");
      return false;
    }
  }
}

void hub_script_loaded(int map, const std::vector<u8>& script) {
  Hub& h = hub();
  h.sites.clear();
  g_hub_sites = 0;
  if (!h.on) return;
  auto word = [&](s64 at) -> u32 {
    u32 w = 0;
    if (at >= 0 && static_cast<size_t>(0x1C + at + 4) <= script.size()) std::memcpy(&w, script.data() + 0x1C + at, 4);
    return w;
  };
  for (size_t i = 0; i < h.bosses.size(); ++i) {
    const HubBoss& b = h.bosses[i];
    for (size_t k = 0; k < b.ask.options.size(); ++k) {
      const HubAsk::Option& o = b.ask.options[k];
      if (o.map != map) continue;
      // The game's choices take this shape: ext 0x68 (inline: the options window 0), ..., ext 0xdc N
      // (inline), push R, pop_store L[n] (0x22 typed, R kept), then the routine's own use of it.
      const s64 w = region::script_offset(map, static_cast<u32>(o.window));
      const s64 c = region::script_offset(map, static_cast<u32>(o.choice));
      const u32 ww = word(w), cw = word(c), store = word(c + 8);
      const bool ok = w >= 0 && c > w && (ww >> 24) == 0x68 && (ww & 0x800000) == 0 && (cw >> 24) == 0xDC &&
                      (cw & 0x800000) == 0 && (cw & 0xFFFF) >= 1 && (cw & 0xFFFF) < 8 && word(c + 4) == 0x0B000000u &&
                      ((store >> 24) == 0x22 || (store >> 24) == 0x01 || (store >> 24) == 0x02 || (store >> 24) == 0x08) &&
                      (store & 0x800000) != 0;
      if (!ok) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "ask: %s: map %d's choice at %#x (window %#x) is not the shape the seed names (%#010x, %#010x); no extra option",
                      boss_label(b).c_str(), map, o.choice, o.window, cw, ww);
        say(buf);
        continue;
      }
      Site s;
      s.boss = i;
      s.option = k;
      s.window = static_cast<u32>(w / 4);
      s.choice = static_cast<u32>(c / 4);
      s.act = s.choice + 3;
      s.count = static_cast<int>(cw & 0xFFFF);
      h.sites.push_back(s);
    }
  }
  g_hub_sites = static_cast<int>(h.sites.size());
}

bool hub_fetch(field::ScriptCtx* ctx) {
  Hub& h = hub();
  if (!h.on || ctx == nullptr || h.sites.empty()) return false;
  const u32 ip = static_cast<u32>(ctx->ip - ctx->code);
  for (Site& s : h.sites) {
    if (ip != s.window && ip != s.choice && ip != s.act) continue;
    if (!site_open(h, s)) return false;
    const HubBoss& b = h.bosses[s.boss];
    const HubAsk::Option& o = b.ask.options[s.option];
    auto push = [&](s32 v) { *ctx->value_sp++ = static_cast<u32>(v); };
    if (ip == s.window) {
      // ext 0x68 (x, y, w, h, -, string; window 0) one line taller, from the stack, its text extended.
      const u32* p = ctx->ip;
      ctx->ip += 4;
      push(static_cast<s16>(p[0]));
      push(static_cast<s16>(p[1]));
      push(static_cast<s16>(p[1] >> 16));
      push(static_cast<s16>(p[2]) + 1);
      push(static_cast<s32>(p[3]));
      h.text_index = static_cast<int>(p[3]);
      h.text_site = &s;
      field::vm_exec_ext(ctx, 0x68800000u);
      h.text_index = -1;
      h.text_site = nullptr;
      if (!s.logged) {
        s.logged = true;
        char buf[96];
        std::snprintf(buf, sizeof buf, "map %d's choice at %#x: option %d", current_map(), o.choice, s.count);
        say("ask: " + boss_label(b) + ": " + buf + " \"" + o.text + "\" added");
      }
      return true;
    }
    if (ip == s.choice) {
      ++ctx->ip;
      field::vm_exec_ext(ctx, 0xDC000000u | static_cast<u32>(s.count + 1));
      return true;
    }
    if (field::vm_r() != s.count) return false;  // (one of the choice's own options)
    // The extra option: the choice's windows close (ext 0x81 0 / 1), the controls come back (ext
    // 0x84) and the party goes to `to` (ext 0xda from the stack).
    field::vm_svc_msg_window_close(0);
    field::vm_svc_msg_window_close(1);
    field::vm_exec_ext(ctx, 0x84000000u);
    const bool into = in_maps(b, o.to.map);
    if (into) h.decision = Decision{true, s.boss, true};
    char buf[128];
    std::snprintf(buf, sizeof buf, ": to map %d (%d, %d, %d)", o.to.map, o.to.x, o.to.y, o.to.z);
    say("ask: " + boss_label(b) + ": \"" + o.text + "\" chosen" + buf + (into ? ", with its story view" : ", out of the chamber"));
    push(o.to.map);
    push(0);
    push(o.to.x);
    push(o.to.y);
    push(o.to.z);
    push(o.to.dir >= 0 ? o.to.dir : 0);
    push(128);
    field::vm_exec_ext(ctx, 0xDA800000u);
    return true;
  }
  return false;
}

const u8* hub_script_text(int index, const u8* original) {
  Hub& h = hub();
  if (!h.on || h.text_site == nullptr || index != h.text_index || original == nullptr) return nullptr;
  const HubAsk::Option& o = h.bosses[h.text_site->boss].ask.options[h.text_site->option];
  // The original's lines (up to its end, or a closing wait), then one more with the lines' own
  // lead-in (the codes its first two lines begin with: the cursor's room).
  std::vector<std::pair<size_t, size_t>> lines;
  size_t at = 0, start = 0, n = 0, end = 0;
  for (int i = 0; i < 400; ++i) {
    const u32 c = text_code(original + at, &n);
    if (c == 0 || c == 0x4000 || c == 0x4002) {
      lines.push_back({start, at});
      if (c != 0x4000) {
        end = at;
        break;
      }
      start = at + n;
    }
    at += n;
  }
  size_t tail = end;  // the closing wait (if any) and the end
  for (int i = 0; i < 8; ++i) {
    const u32 c = text_code(original + tail, &n);
    tail += n;
    if (c == 0) break;
  }
  std::vector<u8> lead;
  if (lines.size() >= 2) {
    size_t p = lines[0].first, q = lines[1].first;
    for (int i = 0; i < 8 && p < lines[0].second && q < lines[1].second; ++i) {
      size_t np = 0, nq = 0;
      const u32 cp = text_code(original + p, &np);
      if (cp != text_code(original + q, &nq) || np != nq) break;
      p += np;
      q += nq;
    }
    lead.assign(original + lines[0].first, original + p);
  }
  h.options_text.assign(original, original + end);
  put_code(h.options_text, 0x4000);
  h.options_text.insert(h.options_text.end(), lead.begin(), lead.end());
  const std::vector<u8> g = glyph_text(o.text);
  h.options_text.insert(h.options_text.end(), g.begin(), g.end());
  h.options_text.insert(h.options_text.end(), original + end, original + tail);
  return h.options_text.data();
}

int hub_asked_view() {
  const Hub& h = hub();
  if (!h.on || !h.view.on || h.view.won) return -1;
  const HubBoss& b = h.bosses[h.view.boss];
  return b.ask.on ? b.encounter : -1;
}

void hub_resume_view(int encounter) {
  Hub& h = hub();
  if (!h.on || encounter < 0) return;
  for (size_t i = 0; i < h.bosses.size(); ++i) {
    if (h.bosses[i].encounter != encounter || !h.bosses[i].ask.on) continue;
    h.decision = Decision{true, i, true};
    say("ask: " + boss_label(h.bosses[i]) + ": the save was made in its story view (a yes): on again with the map's load");
    return;
  }
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
