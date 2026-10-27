#include "game/field/event_sweep.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/boot/loader.h"
#include "game/boot/rng.h"
#include "game/field/actors.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/message.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

enum class Phase { Off, Settle, Fire, Run, Leave, Reload, Done, Play, Route };

struct Entry {
  char kind = 'T';   // T trigger, P Private Action, K talk, S pickpocket
  int index = 0;     // trigger slot or actor slot
  int code = 0;      // script word offset
};

struct Sweep {
  bool checked = false;
  Phase phase = Phase::Off;
  int map = -1;
  int frame_limit = 3600;
  int skip = 0;
  int timer = 0;
  int idle = 0;         // frames of free play in a row
  int idle_top = 0;     // ... with the main thread at the top level (depth 0)
  int next = 0;      // the entry to fire next
  bool started = false;  // the current entry left free play at least once
  bool have_snapshot = false;
  std::vector<Entry> entries;
  std::vector<u8> block, party, records, inventory, config;
  u32 standins = 0, unknown = 0;
  const char* standin_name = nullptr;
  int last_map_seen = -1;
  u32 choices = 0;   // choices answered in the current entry (every third is cancelled)
  u64 signature = 0; // what the script, the field and the windows were doing
  struct Step {
    int map;
    char kind;
    int index;
    int option = 0;            // option: "MAP:K26:1" (below)
    std::vector<int> options;  // "MAP:K16:1/0/0": the step's k-th choice takes the k-th (the last repeats) [hubask agent]
  };
  std::vector<Step> play;  // SS_SWEEP_PLAY
  size_t played = 0;
  int still = 0;     // frames since it last changed
  std::vector<int> scene_models;  // SS_ENDING_LOG: frames each sprite key was drawn on this map
  int goto_map = -1;  // SS_SWEEP_GOTO
  struct Walk { int map; u16 dir; };
  std::vector<Walk> route;  // SS_ROUTE
  bool synced = false;
  u64 sync = 0;             // vblank of the sync (t = 0)
  int control_run = 0;      // frames in a row with field state 0 and a controlled actor
  int streak = 0;           // control streaks since the sync
  int window_wait = 0;      // frames in a row a window waited for the button (state 6 / 7)
  int press_left = 0;       // frames confirm stays held
  int last_state = -1;      // field state last frame
  int reloads = 0;          // reloads in a row whose main script left the map before settling [world agent]
};
// A map whose main script moves on after every reload (map 413 -> 416 in the late proof: the
// reload is no arrival from the map the route came by) would loop in the settle for ever: after
// this many reloads in a row the next entry is reported as "loop" and skipped [world agent].
constexpr int kMaxReloads = 6;
Sweep g_sweep;

// The random generator for comparing with PCSX-Redux (endref.lua): the index and an
// FNV-1a hash over the 56-word table as the console stores it (little-endian words).
std::string rng_signature() {
  u32 table[56];
  s32 index = 0;
  rng_save(table, &index);
  u32 h = 0x811c9dc5u;
  for (u32 w : table)
    for (int b = 0; b < 4; ++b) h = (h ^ (w >> (8 * b) & 0xFF)) * 0x01000193u;
  char buf[48];
  std::snprintf(buf, sizeof buf, "rng %d %08x draws %u", index, h, rng_draws());
  return buf;
}

// Calls fn for each comma-separated item of environment variable var.
template <typename Fn>
void for_each_item(const char* var, Fn fn) {
  const char* v = std::getenv(var);
  if (v == nullptr) return;
  std::string text = v;
  size_t at = 0;
  while (at <= text.size()) {
    const size_t comma = text.find(',', at);
    const std::string item = text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    if (!item.empty()) fn(item.c_str());
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
}

constexpr int kBlockBytes = 0xA40;

int current_map() { return static_cast<s16>(read_u16(globals().game_state + 0x1A)); }

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  std::fprintf(stderr, "[sweep] map %d vblank %llu %s\n", g_sweep.map,
               static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), line);
  std::fflush(stderr);
}

// Free play: the player has control (not locked) and no event, menu, fade or window is running. The
// map's main thread may be inside a function (some maps run a loop while the player walks);
// events start then too, as vm_thread_update only looks at control, the active trigger and
// a running talk.
bool idle(const Field* f) {
  const ScriptCtx* ctx = g_script_vm;
  if (f->state != 0 || ctx == nullptr) return false;
  if (ctx->active_trigger != -1 || ctx->talk_active != 0 || ctx->wait_state != 0 || ctx->sync != 0) return false;
  if (field_state_s16(0x10) == -1 || globals().field_flags[0x26] != 0) return false;  // no control, or locked (ext 0x7b)
  if (fade_is_active(field_fade(), -1)) return false;
  if (msg_system() != nullptr) {
    const int status = msg_window_status(msg_system(), -1);
    if (status != 1 && status != -1) return false;
  }
  return true;
}

// Input. In the field: confirm when a window waits for the button or a choice is up, and
// otherwise every 64 frames while the player has no control (some waits read the pad
// directly); not otherwise while the player has control, where a press as an event ends
// would start a new talk. In a menu's own loop (event_sweep_menu_frame): cancel every 8 frames, to leave
// it (cancel); the presses are scheduled ahead since field_update does not run there.
void press_at(u64 v, u16 button) {
  auto& in = runtime::Runtime::get().scripted_input;
  in[v] = button;
  in[v + 2] = 0;
}

// The pad masks of the confirm and cancel buttons as config +0 / +2 maps them (a new game:
// Cross / Circle; a save may swap them).
u16 pad_button(int i) {
  const u8* cfg = globals().config;
  const u16 v = cfg != nullptr ? static_cast<u16>(cfg[2 * i] | cfg[2 * i + 1] << 8) : 0;
  return v != 0 ? v : (i == 0 ? 0x40 : 0x20);
}

// SS_SWEEP_PRESS (field input; default "all", the presses described above), for measuring
// the dialogue settings (game/field/message.h TextOptions) [dialogue agent]:
//   choices  only choices are answered, each 30 frames after it comes up (confirm, every third
//            cancel); nothing for windows, timed waits or waits that read the pad. With
//            SS_AUTO_ADVANCE the windows move on by themselves.
//   wait:N   choices the same way, and confirm for a window waiting for it (state 6 / 7) so
//            that the press lands in the wait's N-th frame (MsgWindow::button_wait), the frame
//            auto-advance at N frames would act in: the player auto-advance stands in for.
//   none     no field input at all (a choice is never answered).
// Each choice is logged ("choice on window W: ...") when it comes up and when answered.
struct PressMode {
  int kind = 0;  // 0 all, 1 choices, 2 wait, 3 none
  int frames = 0;
};
const PressMode& press_mode() {
  static const PressMode m = [] {
    PressMode r;
    const char* v = std::getenv("SS_SWEEP_PRESS");
    if (v == nullptr || *v == 0 || std::strcmp(v, "all") == 0) return r;
    if (std::strcmp(v, "choices") == 0) r.kind = 1;
    if (std::strcmp(v, "none") == 0) r.kind = 3;
    if (std::strncmp(v, "wait:", 5) == 0) {
      r.kind = 2;
      r.frames = std::max(1, std::atoi(v + 5));
    }
    return r;
  }();
  return m;
}

u16 g_hold_button = 0;  // a play step's option press (schedule_input), held until g_hold_until
u64 g_hold_until = 0;

void schedule_input(int mode) {  // 0 none, 1 field, 2 menu
  auto& rt = runtime::Runtime::get();
  const u64 now = rt.vblanks();
  auto& in = rt.scripted_input;
  in.erase(in.upper_bound(now), in.end());
  in[now + 1] = now + 1 < g_hold_until ? g_hold_button : 0;  // (a play step's option press, held)
  if (mode == 2) {
    for (u64 v = now + 1; v <= now + 600; ++v) {
      if (v % 8 == 0) press_at(v, pad_button(1));  // cancel
    }
    return;
  }
  if (mode != 1) return;
  if (const PressMode& pm = press_mode(); pm.kind != 0) {
    static u64 choice_since = 0, last_window_press = 0;
    ScriptCtx* ctx = g_script_vm;
    const int cw = ctx != nullptr && ctx->wait_state == 9 ? msg_window_get_state(msg_window(ctx->wait_param)) : 0;
    if (ctx != nullptr && ctx->wait_state == 9 && (cw == -1 || cw == 1 || cw == 6 || cw == 7)) {
      if (choice_since == 0) {
        choice_since = now;
        say("choice on window %d: %d options, window state %d", ctx->wait_param, ctx->choice_count, cw);
      }
      if (pm.kind != 3 && now == choice_since + 30) {
        const bool cancel = ++g_sweep.choices % 3 == 0;
        say("choice on window %d: %s after 30 frames", ctx->wait_param, cancel ? "cancel" : "confirm");
        press_at(now + 1, cancel ? pad_button(1) : pad_button(0));
      }
      return;
    }
    if (choice_since != 0) say("choice on window %d over after %llu frames", ctx != nullptr ? ctx->wait_param : -1,
                               static_cast<unsigned long long>(now - choice_since));
    choice_since = 0;
    if (pm.kind != 2 || now < last_window_press + 4) return;
    // The press is read three frames on (scripted input at now + 1, latched by the pad at
    // the next vblank, read at the start of the field frame after), by which time the window
    // has counted three more frames of waiting.
    for (int i = 0; i < kMsgWindowCount; ++i) {
      MsgWindow* w = msg_window(i);
      if (w == nullptr || (w->state != 6 && w->state != 7) || w->button_advance == 0) continue;
      if (w->button_wait + 3 * std::max(1, globals().frame_ticks) >= pm.frames) {
        press_at(now + 1, pad_button(0));
        last_window_press = now;
        break;
      }
    }
    return;
  }
  static u64 last_press = 0;
  static int downs = 0;          // cursor moves made on the choice up now (a play step's option)
  static u64 option_since = 0;   // when that choice came up
  bool waiting = g_script_vm != nullptr && g_script_vm->wait_state == 9;
  // (A step's choices so far: "MAP:K16:1/0/0" gives each its own option.) [hubask agent]
  static size_t step_played = 0;
  static int step_choices = 0;
  static bool was_waiting = false;
  if (g_sweep.played != step_played) step_played = g_sweep.played, step_choices = 0;
  if (was_waiting && !waiting) ++step_choices;
  was_waiting = waiting;
  if (!waiting) downs = 0, option_since = 0;
  // SS_SWEEP_PLAY "MAP:K26:1": while that step plays, a choice takes option 1: 20 frames after it
  // comes up the cursor goes down once (every 10 frames), then confirm (Arlia's guide's "go" is
  // the second option) [hubcore agent].
  const Sweep& sw = g_sweep;
  const Sweep::Step* step = sw.phase == Phase::Play && sw.played > 0 && sw.played <= sw.play.size() ? &sw.play[sw.played - 1] : nullptr;
  const bool listed = step != nullptr && !step->options.empty();
  const int option = step == nullptr ? 0
                     : listed       ? step->options[std::min(static_cast<size_t>(step_choices), step->options.size() - 1)]
                                    : step->option;
  if (waiting && (option > 0 || listed)) {
    if (option_since == 0) option_since = now;
    // (The choice reads the pad only once its window is up: state -1, 1, 6 or 7, as the VM's
    // wait state 9 tests, and not while its cursor slides.)
    const int cw = msg_window_get_state(msg_window(g_script_vm->wait_param));
    const bool ready = (cw == -1 || cw == 1 || cw == 6 || cw == 7) && g_script_vm->choice_slide == 0;
    if (!ready || now < option_since + 20 || now < last_press + 10) return;
    // Held for 4 vblanks (the field reads the pad every other vblank at 30 fps), then released.
    const u16 button = downs < option ? 0x4000 : pad_button(0);  // down, then confirm
    g_hold_button = button;
    g_hold_until = now + 5;
    in[now + 1] = button;
    say("choice on window %d: %s (option %d, cursor at %d)", g_script_vm->wait_param, downs < option ? "down" : "confirm", option,
        g_script_vm->choice_selection);
    if (downs < option && g_script_vm->choice_selection < option) ++downs;
    last_press = now;
    return;
  }
  if (now < last_press + 6) return;
  for (int i = 0; i < kMsgWindowCount && !waiting; ++i) {
    if (MsgWindow* w = msg_window(i)) {
      const int st = msg_window_get_state(w);
      waiting = st == 6 || st == 7 || st == 8;
    }
  }
  if (waiting || (now % 64 == 0 && field_state_s16(0x10) == -1)) {
    // A choice takes confirm (the first option), but every third one cancel, so
    // that topic menus that come back after each answer are left in the end.
    const bool choice = g_script_vm != nullptr && g_script_vm->wait_state == 9;
    press_at(now + 1, choice && ++g_sweep.choices % 3 == 0 ? pad_button(1) : pad_button(0));
    last_press = now;
  }
}

void snapshot() {
  FieldGlobals& g = globals();
  g_sweep.block.assign(g.state_block, g.state_block + kBlockBytes);
  g_sweep.party.assign(g.party, g.party + 0x300);
  g_sweep.records.assign(g.records, g.records + 0x680);
  g_sweep.inventory.assign(g.inventory, g.inventory + 0xC28);
  g_sweep.config.assign(g.config, g.config + 0x2A0);
  g_sweep.have_snapshot = true;
}

void restore() {
  if (!g_sweep.have_snapshot) return;
  FieldGlobals& g = globals();
  std::memcpy(g.state_block, g_sweep.block.data(), kBlockBytes);
  std::memcpy(g.party, g_sweep.party.data(), 0x300);
  std::memcpy(g.records, g_sweep.records.data(), 0x680);
  std::memcpy(g.inventory, g_sweep.inventory.data(), 0xC28);
  std::memcpy(g.config, g_sweep.config.data(), 0x2A0);
}

void collect_entries() {
  ScriptCtx* ctx = g_script_vm;
  auto& out = g_sweep.entries;
  out.clear();
  for (int i = 0; i < kTriggerCount; ++i) {
    const Trigger& t = ctx->triggers[i];
    if (t.active == 0 || t.code == nullptr) continue;
    out.push_back({'T', i, static_cast<int>(t.code - ctx->code)});
    if (t.private_action != -1) out.push_back({'P', i, static_cast<u16>(t.private_action)});
  }
  const int self = field_state_s16(0x10);
  for (int i = 0; i < 0xC2; ++i) {
    // As field_try_talk: not the controlled actor, nor one flagged 0x20000 (no talking).
    if (i == self || !vm_svc_actor_exists(i) || (vm_svc_actor_get_flags(i) & 0x20000) != 0) continue;
    const int talk = vm_svc_actor_get_talk_entry(i);
    if (talk != -1) out.push_back({'K', i, talk & 0xFFFF});
    const int pick = vm_svc_actor_get_pickpocket_entry(i);
    if (pick != -1) out.push_back({'S', i, pick & 0xFFFF});
  }
  std::string list;
  for (const Entry& e : out) {
    list += std::string(" ") + e.kind + std::to_string(e.index) + "@" + std::to_string(e.code);
    if (e.kind == 'T') {  // the region's bounding box, x and z (for walking routes)
      const Trigger& t = ctx->triggers[e.index];
      list += "[" + std::to_string(t.min_x) + ".." + std::to_string(t.max_x) + "," + std::to_string(t.min_z) + ".." +
              std::to_string(t.max_z) + "]";
    }
  }
  say("entries %zu:%s", out.size(), list.c_str());
}

// The entry as the map has it now (the reload can set the map up differently): false when
// it is gone.
bool still_there(const Entry& e) {
  ScriptCtx* ctx = g_script_vm;
  switch (e.kind) {
    case 'T': return ctx->triggers[e.index].active != 0 && ctx->triggers[e.index].code == ctx->code + e.code;
    case 'P': return ctx->triggers[e.index].active != 0 && static_cast<u16>(ctx->triggers[e.index].private_action) == e.code;
    case 'K':
    case 'S':
      if (!vm_svc_actor_exists(e.index) || (vm_svc_actor_get_flags(e.index) & 0x20000) != 0) return false;
      return ((e.kind == 'K' ? vm_svc_actor_get_talk_entry(e.index) : vm_svc_actor_get_pickpocket_entry(e.index)) & 0xFFFF) ==
             e.code;
    default: return false;
  }
}

void fire(const Entry& e) {
  ScriptCtx* ctx = g_script_vm;
  FieldGlobals& g = globals();
  g_sweep.standins = g_vm_standins.calls;
  g_sweep.unknown = g_vm_stats.unknown_ops;
  g_sweep.standin_name = nullptr;
  g_sweep.choices = 0;
  if (e.kind == 'T' || e.kind == 'P') {
    const Trigger& t = ctx->triggers[e.index];
    say("entry %d %c%d code %d start | region x %d..%d z %d..%d y %d..%d", g_sweep.next, e.kind, e.index, e.code, t.min_x,
        t.max_x, t.min_z, t.max_z, t.y_min, t.y_max);
  } else {
    say("entry %d %c%d code %d start", g_sweep.next, e.kind, e.index, e.code);
  }
  switch (e.kind) {
    case 'T':
    case 'P': {
      const Trigger& t = ctx->triggers[e.index];
      vm_push_frame(ctx);
      if (e.kind == 'P') {
        g.script_flags[0] |= 4;
        ctx->ip = ctx->code + static_cast<u16>(t.private_action);
      } else {
        ctx->ip = t.code;
      }
      ctx->last_trigger = e.index;
      ctx->active_trigger = e.index;
      ctx->depth = 1;
      ctx->saved_control = field_state_s16(0x10);
      field_state_set_s16(0x10, -1);
      vm_svc_actor_and_flags(leader_slot(), 0, 0xFFBFFFFF);
      break;
    }
    case 'K':
    case 'S':
      if (e.kind == 'S') {
        ctx->pickpocket_level = 1;
        ctx->pickpocket_bonus = 0;
        ctx->thief_slot = 0;
      }
      field_start_talk(ctx, e.index, e.kind == 'K' ? 1 : 0);
      ctx->talk_active = 1;
      ctx->saved_control = field_state_s16(0x10);
      field_state_set_s16(0x10, -1);
      break;
    default:
      break;
  }
}

// A digest of everything that moves while an event runs: the map, the field state, the
// script thread's position and waits, and every window's state and text position. A
// long scene keeps changing it; a stuck one does not.
u64 progress_signature(const Field* f) {
  u64 h = 1469598103934665603ull;
  auto mix = [&h](u64 v) { h = (h ^ v) * 1099511628211ull; };
  mix(static_cast<u64>(current_map()));
  mix(static_cast<u64>(f->state));
  if (const ScriptCtx* ctx = g_script_vm) {
    mix(static_cast<u64>(ctx->ip - ctx->code));
    mix(static_cast<u64>(ctx->wait_state));
    mix(static_cast<u64>(ctx->depth));
  }
  for (int i = 0; i < kMsgWindowCount; ++i) {
    if (MsgWindow* w = msg_window(i)) {
      mix(static_cast<u64>(i) << 8 | static_cast<u64>(w->state));
      mix(static_cast<u64>(w->page_read) << 16 | static_cast<u64>(w->page_offset));
    }
  }
  return h;
}

std::string vm_state(const Field* f) {
  const ScriptCtx* ctx = g_script_vm;
  char line[256];
  std::snprintf(line, sizeof line, "on map %d field state %d wait_state %d param %d ip %ld depth %d trigger %d talk %d control %d",
                current_map(), f->state, ctx ? ctx->wait_state : -1, ctx ? ctx->wait_param : -1,
                ctx && ctx->ip && ctx->code ? static_cast<long>(ctx->ip - ctx->code) : -1L, ctx ? ctx->depth : -1,
                ctx ? ctx->active_trigger : -1, ctx ? ctx->talk_active : -1, field_state_s16(0x10));
  std::string out = line;
  if (ctx != nullptr && ctx->call_sp != nullptr) {  // return words of the call frames, innermost first
    out += " returns";
    for (int k = 0; k < ctx->depth && k < 8; ++k) out += " w" + std::to_string(ctx->call_sp[-1 - 4 * k] / 4);
  }
  for (int i = 0; i < kMsgWindowCount; ++i) {
    if (MsgWindow* w = msg_window(i)) {
      char d[96];
      std::snprintf(d, sizeof d, " w%d=%d(raw %d timer %d speed %d pages %d/%d off %d)", i, msg_window_get_state(w), w->state,
                    w->timer, w->speed, w->page_read, w->page_write, w->page_offset);
      out += d;
    }
  }
  return out;
}

// SS_SWEEP_SHOW="G1cf:1,G1c0:2,F157": script globals (hex offset : bytes) and flags printed
// as " G1cf=8 G1c0=681 F157=0" on a "state" line when an entry ends and when the map settles,
// so a search can tell the ways into a map apart (tools/ap_late_proof.py)
// [enemy agent, AP late proof]. Empty without the variable.
std::string shown_state() {
  static const char* spec = std::getenv("SS_SWEEP_SHOW");
  if (spec == nullptr) return "";
  std::string out;
  for_each_item("SS_SWEEP_SHOW", [&](const char* v) {
    char* end = nullptr;
    if (*v == 'F') {
      const long n = std::strtol(v + 1, &end, 10);
      if (n < 0 || n >= 0x170 * 8) return;
      out += " F" + std::to_string(n) + "=" + std::to_string(globals().script_flags[n >> 3] >> (n & 7) & 1);
    } else if (*v == 'G') {
      const long off = std::strtol(v + 1, &end, 16);
      const long bytes = end != nullptr && *end == ':' ? std::strtol(end + 1, nullptr, 10) : 4;
      if (off < 0 || off + bytes > 0x8C0 || (bytes != 1 && bytes != 2 && bytes != 4)) return;
      const u8* p = globals().script_globals + off;
      const long value = bytes == 1 ? static_cast<long>(static_cast<s8>(p[0]))
                         : bytes == 2 ? static_cast<long>(static_cast<s16>(p[0] | p[1] << 8))
                                      : static_cast<long>(static_cast<s32>(p[0] | p[1] << 8 | p[2] << 16 | static_cast<u32>(p[3]) << 24));
      char key[16];
      std::snprintf(key, sizeof key, "%lx", off);
      out += std::string(" G") + key + "=" + std::to_string(value);
    }
  });
  return out;
}

void finish_entry(const Field* f, const char* result) {
  const u32 standins = g_vm_standins.calls - g_sweep.standins;
  const u32 unknown = g_vm_stats.unknown_ops - g_sweep.unknown;
  const Entry& e = g_sweep.entries[static_cast<size_t>(g_sweep.next)];
  say("entry %d %c%d end %s frames %d standins %u%s%s unknown_ops %u%s", g_sweep.next, e.kind, e.index, result,
      g_sweep.timer, standins, standins ? " last " : "", standins && g_vm_standins.last ? g_vm_standins.last : "", unknown,
      std::strcmp(result, "timeout") == 0 || std::strcmp(result, "stuck") == 0 ? (" | " + vm_state(f)).c_str() : "");
  if (std::getenv("SS_SWEEP_SHOW") != nullptr) say("entry %d state%s", g_sweep.next, shown_state().c_str());
  ++g_sweep.next;
}

// Back to the swept map with the snapshot's state (from free play, or forced). The script
// thread is held (a long wait, no control) while the screen fades; the snapshot goes back
// in just before field_update's state 1 loads the map, since the thread's stacks live in
// the state block.
void reload(Field* f) {
  // (The map load closes the windows; closing them here, under a notice banner that is
  // showing, reached original_bugs.md V1.)
  if (ScriptCtx* ctx = g_script_vm) ctx->wait = 1 << 20;
  field_state_set_s16(0x10, -1);
  f->pending_map = g_sweep.map;
  fade_push(field_fade(), 3, 0x10, 0, 0);
  f->state = 1;
  g_sweep.phase = Phase::Reload;
  g_sweep.timer = 0;
  g_sweep.idle = g_sweep.idle_top = 0;
}

}  // namespace

void event_sweep_menu_frame() {
  if (g_sweep.phase == Phase::Run || g_sweep.phase == Phase::Settle) schedule_input(2);
}

void event_sweep_frame(Field* f) {
  Sweep& s = g_sweep;
  if (!s.checked) {
    s.checked = true;
    if (std::getenv("SS_EVENT_SWEEP") == nullptr) return;
    if (const char* v = std::getenv("SS_EVENT_SWEEP_FRAMES")) s.frame_limit = std::atoi(v);
    if (const char* v = std::getenv("SS_EVENT_SWEEP_SKIP")) s.skip = std::atoi(v);
    s.map = current_map();
    s.phase = Phase::Settle;
    // SS_EVENT_SWEEP_DISC=2: play as on disc 2 (config +0x4c, as script B 0x6a sets it, and
    // disc 2's archive, which --disc2 / SS_DISC2 registers).
    if (const char* v = std::getenv("SS_EVENT_SWEEP_DISC"); v != nullptr && std::atoi(v) == 2) {
      globals().config[0x4C] = 1;
      if (!loader_insert_disc(*globals().loader, 2)) say("disc 2 is not available (set SS_DISC2)");
    }
    // SS_SWEEP_PARTY="c,c,...": characters (0 Claude .. 11 Chisato) added as the script's
    // party_add_member does; SS_SWEEP_CONFIG="off=v,...": config bytes (hex offsets; the
    // relationship tables are +0x58 and +0xe8, 12 x 12); SS_SWEEP_FLAGS="n,...": script flags.
    // For driving the endings (docs/notes/endings.md) from a --map 581 start.
    FieldGlobals& g = globals();
    // SS_SWEEP_PARTY_DROP="c,...": these characters leave the party for good first (their slot
    // emptied: Rena's game alone from a --map start, which begins as Claude) [party agent].
    for_each_item("SS_SWEEP_PARTY_DROP", [&](const char* v) {
      const int c = std::atoi(v);
      for (int i = 0; i < 8; ++i) {
        u8* p = g.party + i * 0x60;
        const s16 id = static_cast<s16>(p[0] | p[1] << 8);
        if (id == c + 1 || id == -(c + 1)) p[0] = p[1] = 0;
      }
    });
    for_each_item("SS_SWEEP_PARTY", [&](const char* v) { party_add_member(g_script_vm, std::atoi(v)); });
    // SS_SWEEP_PARTY_ASIDE="c,...": then these set aside (to the reserve, as the script's
    // party_remove_member does: a split's party from a --map start) [shingofix agent].
    for_each_item("SS_SWEEP_PARTY_ASIDE", [&](const char* v) { party_remove_member(g_script_vm, std::atoi(v)); });
    for_each_item("SS_SWEEP_CONFIG", [&](const char* v) {
      char* end = nullptr;
      const long off = std::strtol(v, &end, 16);
      if (end != nullptr && *end == '=' && off >= 0 && off < 0x2A0) g.config[off] = static_cast<u8>(std::strtol(end + 1, nullptr, 0));
    });
    // "!n" clears flag n instead ("!1": Rena's game from a --map start) [enemy agent, AP late proof].
    for_each_item("SS_SWEEP_FLAGS", [&](const char* v) {
      const bool clear = *v == '!';
      const long n = std::strtol(clear ? v + 1 : v, nullptr, 0);
      if (n < 0 || n >= 0x170 * 8) return;
      if (clear) g.script_flags[n >> 3] = static_cast<u8>(g.script_flags[n >> 3] & ~(1 << (n & 7)));
      else g.script_flags[n >> 3] = static_cast<u8>(g.script_flags[n >> 3] | 1 << (n & 7));
    });
    say("start (frame limit %d, skip %d, disc %d)", s.frame_limit, s.skip, globals().config[0x4C] + 1);
    // SS_SWEEP_PLAY="MAP:ENTRY,...": play mode. No snapshots or reloads: each time the
    // player has control on the next listed map, its entry (T5, K16, ...) fires once, as
    // walking into that exit or talking would; windows and choices are answered as in the
    // sweep. When the list is used up, or control comes on a map that is not next, it stops.
    for_each_item("SS_SWEEP_PLAY", [&](const char* v) {
      char kind = 0;
      int map = -1, index = -1;
      int option = 0;  // "MAP:ENTRY:N": the step's choices take option N (0 the first) [hubcore agent]
      if (std::sscanf(v, "%d:%c%d:%d", &map, &kind, &index, &option) >= 3) {
        s.play.push_back({map, kind, index, option, {}});
        // "MAP:ENTRY:N/M/...": one option per choice, in order [hubask agent]
        const char* list = std::strchr(v, ':');
        if (list != nullptr) list = std::strchr(list + 1, ':');
        if (list != nullptr && std::strchr(list, '/') != nullptr)
          for (const char* p = list + 1; *p != 0; p = *p == '/' ? p + 1 : p) {
            char* end = nullptr;
            const long n = std::strtol(p, &end, 10);
            if (end == p) break;
            s.play.back().options.push_back(static_cast<int>(n));
            p = end;
          }
      }
    });
    if (!s.play.empty()) s.phase = Phase::Play;
    // SS_SWEEP_GOTO=MAP: once settled, load map MAP as field_update's state 1 does (pending
    // map, no fade) and stop the sweep and its input. For comparing a loaded save's endings
    // with PCSX-Redux, where a Lua script pokes the same two Field members.
    if (const char* v = std::getenv("SS_SWEEP_GOTO")) s.goto_map = std::atoi(v);
    // SS_ROUTE="MAP:DIR,...": walk a route with input simple enough for a PCSX-Redux script
    // to give the same (scratchpad/menu/m8/late/lateref.lua). DIR is up / down / left /
    // right or two joined by '+'. See Phase::Route.
    for_each_item("SS_ROUTE", [&](const char* v) {
      int map = -1;
      char dir[32] = {};
      if (std::sscanf(v, "%d:%31[a-z+]", &map, dir) != 2) return;
      u16 mask = 0;
      const std::string d = dir;
      if (d.find("up") != std::string::npos) mask |= 0x1000;
      if (d.find("right") != std::string::npos) mask |= 0x2000;
      if (d.find("down") != std::string::npos) mask |= 0x4000;
      if (d.find("left") != std::string::npos) mask |= 0x8000;
      s.route.push_back({map, mask});
    });
    if (!s.route.empty()) s.phase = Phase::Route;
  }
  // SS_ENDING_LOG=1: on every map change, the ending list map 581 builds (G[0x388 + k] ids,
  // G[0x348 + 4k] maps, ended by 0xff) and the index G[0x344].
  static const bool ending_log = std::getenv("SS_ENDING_LOG") != nullptr;
  // ... and who each scene shows: the sprite keys (0 Claude .. 11 Chisato for the party)
  // of the actors drawn on the map, with the frames each was drawn, printed as the map ends.
  if (ending_log) {
    if (current_map() != s.last_map_seen) {
      if (s.last_map_seen != -1 && s.last_map_seen != 581) {
        std::string shown;
        for (size_t k = 0; k < s.scene_models.size(); ++k)
          if (s.scene_models[k] >= 30) shown += " " + std::to_string(k) + ":" + std::to_string(s.scene_models[k]);
        say("scene on map %d shows%s", s.last_map_seen, shown.c_str());
      }
      s.scene_models.assign(0x400, 0);
    } else if (current_map() != 581 && !s.scene_models.empty()) {
      for (int slot = 0; slot < 0xC2; ++slot) {
        const Actor* a = field_actor(slot);
        if (a == nullptr || (a->flags & kActorHidden) != 0 || a->layer == 0 || a->prim_count <= 0) continue;
        if (a->model >= 0 && a->model < 0x400) ++s.scene_models[a->model];
      }
    }
  }
  if (ending_log && current_map() != s.last_map_seen) {
    const u8* G = globals().script_globals;
    std::string list;
    for (int k = 0; k < 16; ++k) {
      const u8 id = G[0x388 + k];
      s32 map;
      std::memcpy(&map, G + 0x348 + 4 * k, 4);
      list += " " + std::to_string(id) + "@" + std::to_string(map);
      if (id == 0xFF) break;
    }
    s32 index;
    std::memcpy(&index, G + 0x344, 4);
    say("endings index %d:%s", index, list.c_str());
    // Back on map 581 with the list played through: the credits come next; stop here.
    if (index > 0 && index < 16 && G[0x388 + index] == 0xFF && current_map() == 581) {
      say("endings done");
      runtime::Runtime::get().frame_limit = runtime::Runtime::get().vblanks() + 2;
    }
  }
  // SS_SWEEP_ACTORS=N: every N frames, each actor's slot, sprite key, position, move mode
  // and target, wait state and flags (for stalls that wait on an NPC).
  static const int actors_every = [] {
    const char* v = std::getenv("SS_SWEEP_ACTORS");
    return v != nullptr ? std::atoi(v) : 0;
  }();
  if (actors_every > 0 && runtime::Runtime::get().vblanks() % static_cast<u64>(actors_every) == 0) {
    std::string line;
    for (int slot = 0; slot < 0xC2; ++slot) {
      const Actor* a = field_actor(slot);
      if (a == nullptr) continue;
      char b[128];
      std::snprintf(b, sizeof b, " [%d k%d %d,%d,%d mv%d->%d,%d w%d f%x]", slot, a->model, a->x >> 12, a->y >> 12, a->z >> 12,
                    a->move_mode, a->target[0] >> 12, a->target[2] >> 12, a->wait_state, a->flags);
      line += b;
    }
    say("actors%s", line.c_str());
  }
  if (s.phase == Phase::Off || s.phase == Phase::Done) {
    // After SS_SWEEP_GOTO the ending log still follows the maps.
    if (ending_log && current_map() != s.last_map_seen) {
      say("now on map %d; %s", current_map(), rng_signature().c_str());
      s.last_map_seen = current_map();
    }
    return;
  }
  ++s.timer;
  const bool is_idle = idle(f);
  const int map = current_map();
  // Settled: 20 frames of free play with the main thread at the top level, or 600 with it
  // inside a function (maps whose main thread loops while the player walks). The first
  // keeps the sweep from firing into an intro scene that has not locked the player yet.
  const bool top = g_script_vm != nullptr && g_script_vm->depth == 0;
  auto count = [&](bool on) {
    s.idle = on ? s.idle + 1 : 0;
    s.idle_top = on && top ? s.idle_top + 1 : 0;
    return s.idle_top >= 20 || s.idle >= 600;
  };
  if (map != s.last_map_seen) {
    if (s.last_map_seen != -1) say("now on map %d", map);
    s.last_map_seen = map;
  }
  switch (s.phase) {
    case Phase::Settle:
      // The map's main script runs until the player has control (or it leaves the map).
      schedule_input(is_idle ? 0 : 1);
      if (map != s.map && f->state == 0 && is_idle) {
        say("main script left for map %d; settle %s", map, s.have_snapshot ? "(reload)" : "failed");
        if (!s.have_snapshot) {
          s.phase = Phase::Done;
          say("done (left the map) entries 0");
          runtime::Runtime::get().frame_limit = runtime::Runtime::get().vblanks() + 2;
          return;
        }
        if (++s.reloads >= kMaxReloads) {  // [world agent]
          s.reloads = 0;
          if (s.next < static_cast<int>(s.entries.size())) {
            const Entry& e = s.entries[static_cast<size_t>(s.next)];
            say("entry %d %c%d code %d start", s.next, e.kind, e.index, e.code);
            say("entry %d %c%d end loop frames %d standins 0 unknown_ops 0 | %d reloads in a row left for map %d", s.next,
                e.kind, e.index, s.timer, kMaxReloads, map);
            ++s.next;
          }
          if (s.next >= static_cast<int>(s.entries.size())) {
            s.phase = Phase::Done;
            say("done entries %zu", s.entries.size());
            schedule_input(0);
            runtime::Runtime::get().frame_limit = runtime::Runtime::get().vblanks() + 2;
            return;
          }
        }
        reload(f);
        return;
      }
      if (count(is_idle && map == s.map)) {
        s.reloads = 0;
        if (s.goto_map >= 0) {
          say("settled after %d frames; goto map %d; %s", s.timer, s.goto_map, rng_signature().c_str());
          schedule_input(0);
          f->pending_map = s.goto_map;
          f->state = 1;
          s.phase = Phase::Done;
          return;
        }
        if (!s.have_snapshot) {
          say("settled after %d frames", s.timer);
          if (std::getenv("SS_SWEEP_SHOW") != nullptr) say("settled state%s", shown_state().c_str());
          snapshot();
          collect_entries();
          s.next = s.skip;
        }
        s.phase = Phase::Fire;
        s.timer = 0;
      } else if (s.timer > s.frame_limit) {
        say("settle timeout | %s", vm_state(f).c_str());
        if (!s.have_snapshot) {
          s.phase = Phase::Done;
          say("done (no free play) entries 0");
          runtime::Runtime::get().frame_limit = runtime::Runtime::get().vblanks() + 2;
          return;
        }
        // The reload never reached free play: the next entry is skipped (its own reload follows).
        if (s.next < static_cast<int>(s.entries.size())) {
          const Entry& e = s.entries[static_cast<size_t>(s.next)];
          say("entry %d %c%d code %d start", s.next, e.kind, e.index, e.code);
          say("entry %d %c%d end no free play frames %d standins 0 unknown_ops 0 | %s", s.next, e.kind, e.index, s.timer,
              vm_state(f).c_str());
          ++s.next;
        }
        if (s.next >= static_cast<int>(s.entries.size())) {
          s.phase = Phase::Done;
          say("done entries %zu", s.entries.size());
          runtime::Runtime::get().frame_limit = runtime::Runtime::get().vblanks() + 2;
          return;
        }
        reload(f);
      }
      return;
    case Phase::Fire:
      if (s.next >= static_cast<int>(s.entries.size())) {
        s.phase = Phase::Done;
        say("done entries %zu", s.entries.size());
        schedule_input(0);
        runtime::Runtime::get().frame_limit = runtime::Runtime::get().vblanks() + 2;
        return;
      }
      if (!still_there(s.entries[static_cast<size_t>(s.next)])) {
        const Entry& e = s.entries[static_cast<size_t>(s.next)];
        say("entry %d %c%d code %d start", s.next, e.kind, e.index, e.code);
        const bool actor = e.kind == 'K' || e.kind == 'S';
        say("entry %d %c%d end gone frames 0 standins 0 unknown_ops 0 | now %s talk %d pick %d", s.next, e.kind, e.index,
            actor ? (vm_svc_actor_exists(e.index) ? "exists" : "absent") : "trigger",
            actor ? vm_svc_actor_get_talk_entry(e.index) : -1, actor ? vm_svc_actor_get_pickpocket_entry(e.index) : -1);
        ++s.next;
        return;
      }
      fire(s.entries[static_cast<size_t>(s.next)]);
      s.phase = Phase::Run;
      s.timer = 0;
      s.still = 0;
      s.idle = s.idle_top = 0;
      s.started = false;
      schedule_input(1);
      return;
    case Phase::Run: {
      schedule_input(is_idle ? 0 : 1);
      static const int watch = [] {
        const char* v = std::getenv("SS_EVENT_SWEEP_WATCH");
        return v != nullptr ? std::atoi(v) : 0;
      }();
      if (watch > 0 && s.timer % watch == 0) say("watch | %s", vm_state(f).c_str());
      if (!is_idle) s.started = true;
      const char* result = nullptr;
      std::string moved;
      if (f->state == 6 || f->state == 7) {
        result = "game over";
      } else if (map != s.map && is_idle) {
        moved = "map " + std::to_string(map);
        result = moved.c_str();
      } else if (count(is_idle && map == s.map)) {
        result = "ok";
      }
      // Stuck: nothing moved for frame_limit / 3 frames (at least 1200); a long scene that
      // keeps moving runs to 10 times frame_limit.
      if (const u64 sig = progress_signature(f); sig != s.signature) {
        s.signature = sig;
        s.still = 0;
      } else {
        ++s.still;
      }
      if (result == nullptr && s.still > std::max(1200, s.frame_limit / 3)) result = "stuck";
      if (result == nullptr && s.timer > s.frame_limit * 10) result = "timeout";
      if (result == nullptr) return;
      finish_entry(f, result);
      if (f->state == 6 || f->state == 7) f->state = 0;  // take the game over back
      reload(f);
      return;
    }
    case Phase::Play: {
      schedule_input(is_idle ? 0 : 1);
      if (s.timer % 600 == 0) say("play: %s", vm_state(f).c_str());
      if (!count(is_idle)) return;
      if (s.played >= s.play.size() || s.play[s.played].map != map) {
        say("play: control on map %d after %zu of %zu steps; done", map, s.played, s.play.size());
        {
          // The story state where the play ended (chapter G[0xa0], step G[0xa4]); with
          // SS_SWEEP_PLAY_DUMP=PATH, the blocks SS_STATE_DUMP writes; with
          // SS_SWEEP_PLAY_STOP=1, the run ends there [dialogue agent].
          FieldGlobals& g = globals();
          s32 chapter = 0, step = 0;
          std::memcpy(&chapter, g.script_globals + 0xA0, 4);
          std::memcpy(&step, g.script_globals + 0xA4, 4);
          say("play: story chapter %d step %d; %s", chapter, step, rng_signature().c_str());
          if (const char* path = std::getenv("SS_SWEEP_PLAY_DUMP"); path != nullptr && *path != 0) {
            const u8* block = static_cast<const u8*>(handoff_get(g.handoff, 9));
            if (std::FILE* out = block != nullptr ? std::fopen(path, "wb") : nullptr) {
              std::fwrite(block, 1, 0xA40, out);
              std::fwrite(g.config, 1, 0x2A0, out);
              std::fwrite(g.party, 1, 0x300, out);
              std::fwrite(g.records, 1, 0x680, out);
              std::fwrite(g.inventory, 1, 0xC28, out);
              std::fclose(out);
            }
          }
          if (std::getenv("SS_SWEEP_PLAY_STOP") != nullptr)
            runtime::Runtime::get().frame_limit = runtime::Runtime::get().vblanks() + 2;
        }
        s.phase = Phase::Done;
        // SS_SWEEP_PLAY_SWEEP=1: then sweep the map the play ended on (its entries, as a
        // sweep from a --map start would), e.g. to find the next exit of a route.
        if (std::getenv("SS_SWEEP_PLAY_SWEEP") != nullptr) {
          s.map = map;
          s.phase = Phase::Settle;
          s.timer = 0;
          s.idle = s.idle_top = 0;
        }
        return;
      }
      const auto& step = s.play[s.played++];
      ScriptCtx* ctx = g_script_vm;
      Entry e{step.kind, step.index, 0};
      if (e.kind == 'T' || e.kind == 'P') {
        e.code = static_cast<int>(ctx->triggers[e.index].code - ctx->code);
        if (e.kind == 'P') e.code = static_cast<u16>(ctx->triggers[e.index].private_action);
      } else {
        e.code = (e.kind == 'K' ? vm_svc_actor_get_talk_entry(e.index) : vm_svc_actor_get_pickpocket_entry(e.index)) & 0xFFFF;
      }
      say("play: step %zu on map %d: %c%d", s.played - 1, map, e.kind, e.index);
      fire(e);
      s.idle = s.idle_top = 0;
      return;
    }
    case Phase::Route: {
      // The rules (the same in lateref.lua, decided from the state at the end of the frame):
      // with field state 0, a controlled actor and the leader's actor 20 frames in a row on
      // a listed map, its direction is held; a window waiting for the button (state 6 / 7)
      // or a choice 10 frames in a row gets confirm for 2 frames; nothing else (no input in
      // battles).
      // The first 20-frame streak is the sync point (t = 0); SS_ROUTE_RNG=FILE (57 words:
      // index, table, as endref.lua / lateref.lua dump them) sets the generator there.
      auto& rt = runtime::Runtime::get();
      const u64 now = rt.vblanks();
      const int leader = leader_slot();
      const bool in_control = f->state == 0 && field_state_s16(0x10) != -1 && vm_svc_actor_exists(leader);
      s.control_run = in_control ? s.control_run + 1 : 0;
      auto where = [&]() {
        s32 x = 0, y = 0, z = 0;
        vm_svc_actor_get_position(leader, &x, &y, &z);
        char b[96];
        std::snprintf(b, sizeof b, "leader %d,%d,%d view %d,%d", x >> 12, y >> 12, z >> 12, vm_svc_actor_get_view_x(leader),
                      vm_svc_actor_get_view_y(leader));
        return std::string(b);
      };
      if (!s.synced) {
        if (s.control_run < 20) return;
        if (const char* path = std::getenv("SS_ROUTE_RNG")) {
          u32 w[57];
          if (std::FILE* in = std::fopen(path, "rb")) {
            if (std::fread(w, 4, 57, in) == 57) rng_restore(w + 1, static_cast<s32>(w[0]));
            std::fclose(in);
          }
        }
        s.synced = true;
        s.sync = now;
        s.last_state = f->state;
        say("route: sync t 0 on map %d; %s; %s", map, rng_signature().c_str(), where().c_str());
        // SS_ROUTE_GOTO=MAP: then load MAP at its own entry spot as field state 1 does (the
        // same two Field members lateref.lua pokes with SS_GOTO).
        if (const char* g = std::getenv("SS_ROUTE_GOTO")) {
          f->pending_map = std::atoi(g);
          f->state = 1;
          s.control_run = 0;
          return;
        }
      }
      const long t = static_cast<long>(now - s.sync);
      if (f->state != s.last_state) {
        say("route: t %ld field state %d -> %d on map %d; %s", t, s.last_state, f->state, map, rng_signature().c_str());
        s.last_state = f->state;
      }
      if (s.control_run == 1) ++s.streak;
      u16 dir = 0;
      for (const auto& w : s.route)
        if (w.map == map) dir = w.dir;
      if (in_control && dir != 0 && s.control_run >= 20 && (s.control_run - 20) % 30 == 0)
        say("route: t %ld map %d streak %d c %d %s; %s", t, map, s.streak, s.control_run, where().c_str(), rng_signature().c_str());
      // A choice (VM wait state 9) counts as a window waiting for the button.
      bool waiting = g_script_vm != nullptr && g_script_vm->wait_state == 9;
      for (int i = 0; i < kMsgWindowCount && !waiting; ++i) {
        if (MsgWindow* w = msg_window(i)) waiting = w->state == 6 || w->state == 7;
      }
      s.window_wait = waiting ? s.window_wait + 1 : 0;
      if (s.window_wait >= 10) {
        s.window_wait = 0;
        s.press_left = 2;
        say("route: t %ld confirm on map %d", t, map);
      }
      u16 mask = s.control_run >= 20 ? dir : 0;
      if (s.press_left > 0) {
        mask = static_cast<u16>(mask | pad_button(0));
        --s.press_left;
      }
      // At this vblank: the VSync callback latches the pad before the vblank count moves on
      // (runtime.cpp), so this is what the next frame reads, as a pad override set at this
      // vsync is in PCSX-Redux.
      auto& in = rt.scripted_input;
      in.erase(in.upper_bound(now), in.end());
      in[now] = mask;
      return;
    }
    case Phase::Reload:
      schedule_input(0);
      if (f->state == 1 && !fade_is_active(field_fade(), -1)) {
        restore();  // field_update loads the map right after this
        s.phase = Phase::Settle;
        s.timer = 0;
      } else if (f->state != 1) {
        f->state = 1;  // something (a game over, a menu request) took the state over
      }
      return;
    default:
      return;
  }
}

}  // namespace ss::game::field
