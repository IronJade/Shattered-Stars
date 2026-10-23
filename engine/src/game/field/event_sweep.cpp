#include "game/field/event_sweep.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/message.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

enum class Phase { Off, Settle, Fire, Run, Leave, Reload, Done };

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
  int idle = 0;
  int next = 0;      // the entry to fire next
  bool started = false;  // the current entry left free play at least once
  bool have_snapshot = false;
  std::vector<Entry> entries;
  std::vector<u8> block, party, records, inventory;
  u32 standins = 0, unknown = 0;
  const char* standin_name = nullptr;
  int last_map_seen = -1;
};
Sweep g_sweep;

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

// Free play: the player has control and no event, menu, fade or window is running.
bool idle(const Field* f) {
  const ScriptCtx* ctx = g_script_vm;
  if (f->state != 0 || ctx == nullptr) return false;
  if (ctx->depth != 0 || ctx->active_trigger != -1 || ctx->talk_active != 0 || ctx->wait_state != 0 || ctx->sync != 0)
    return false;
  if (field_state_s16(0x10) == -1) return false;
  if (fade_is_active(field_fade(), -1)) return false;
  if (msg_system() != nullptr) {
    const int status = msg_window_status(msg_system(), -1);
    if (status != 1 && status != -1) return false;
  }
  return true;
}

// Input: while something runs, Cross every 8 frames (Circle every fourth press), scheduled
// 600 frames ahead so that menus' own loops keep getting it; none in free play.
void schedule_input(bool press) {
  auto& rt = runtime::Runtime::get();
  const u64 now = rt.vblanks();
  auto& in = rt.scripted_input;
  in.erase(in.upper_bound(now), in.end());
  in[now + 1] = 0;
  if (!press) return;
  // Anchored to the vblank count, so that rescheduling every frame keeps the same presses.
  for (u64 v = now + 1; v <= now + 600; ++v) {
    if (v % 8 == 0) in[v] = ((v / 8) % 4 == 3) ? 0x20 : 0x40;  // Circle : Cross
    if (v % 8 == 2) in[v] = 0;
  }
}

void snapshot() {
  FieldGlobals& g = globals();
  g_sweep.block.assign(g.state_block, g.state_block + kBlockBytes);
  g_sweep.party.assign(g.party, g.party + 0x300);
  g_sweep.records.assign(g.records, g.records + 0x680);
  g_sweep.inventory.assign(g.inventory, g.inventory + 0xC28);
  g_sweep.have_snapshot = true;
}

void restore() {
  if (!g_sweep.have_snapshot) return;
  FieldGlobals& g = globals();
  std::memcpy(g.state_block, g_sweep.block.data(), kBlockBytes);
  std::memcpy(g.party, g_sweep.party.data(), 0x300);
  std::memcpy(g.records, g_sweep.records.data(), 0x680);
  std::memcpy(g.inventory, g_sweep.inventory.data(), 0xC28);
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
    if (i == self || !vm_svc_actor_exists(i)) continue;
    const int talk = vm_svc_actor_get_talk_entry(i);
    if (talk != -1) out.push_back({'K', i, talk & 0xFFFF});
    const int pick = vm_svc_actor_get_pickpocket_entry(i);
    if (pick != -1) out.push_back({'S', i, pick & 0xFFFF});
  }
  std::string list;
  for (const Entry& e : out) list += std::string(" ") + e.kind + std::to_string(e.index) + "@" + std::to_string(e.code);
  say("entries %zu:%s", out.size(), list.c_str());
}

void fire(const Entry& e) {
  ScriptCtx* ctx = g_script_vm;
  FieldGlobals& g = globals();
  g_sweep.standins = g_vm_standins.calls;
  g_sweep.unknown = g_vm_stats.unknown_ops;
  g_sweep.standin_name = nullptr;
  say("entry %d %c%d code %d start", g_sweep.next, e.kind, e.index, e.code);
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

std::string vm_state(const Field* f) {
  const ScriptCtx* ctx = g_script_vm;
  char line[256];
  std::snprintf(line, sizeof line, "field state %d wait_state %d param %d ip %ld depth %d trigger %d talk %d control %d",
                f->state, ctx ? ctx->wait_state : -1, ctx ? ctx->wait_param : -1,
                ctx && ctx->ip && ctx->code ? static_cast<long>(ctx->ip - ctx->code) : -1L, ctx ? ctx->depth : -1,
                ctx ? ctx->active_trigger : -1, ctx ? ctx->talk_active : -1, field_state_s16(0x10));
  return line;
}

void finish_entry(const Field* f, const char* result) {
  const u32 standins = g_vm_standins.calls - g_sweep.standins;
  const u32 unknown = g_vm_stats.unknown_ops - g_sweep.unknown;
  const Entry& e = g_sweep.entries[static_cast<size_t>(g_sweep.next)];
  say("entry %d %c%d end %s frames %d standins %u%s%s unknown_ops %u%s", g_sweep.next, e.kind, e.index, result,
      g_sweep.timer, standins, standins ? " last " : "", standins && g_vm_standins.last ? g_vm_standins.last : "", unknown,
      std::strcmp(result, "timeout") == 0 ? (" | " + vm_state(f)).c_str() : "");
  ++g_sweep.next;
}

// Back to the swept map with the snapshot's state (from free play, or forced). The script
// thread is held (a long wait, no control) while the screen fades; the snapshot goes back
// in just before field_update's state 1 loads the map, since the thread's stacks live in
// the state block.
void reload(Field* f) {
  if (MsgSystem* msg = msg_system()) msg_window_destroy(msg, -1);
  if (ScriptCtx* ctx = g_script_vm) ctx->wait = 1 << 20;
  field_state_set_s16(0x10, -1);
  f->pending_map = g_sweep.map;
  fade_push(field_fade(), 3, 0x10, 0, 0);
  f->state = 1;
  g_sweep.phase = Phase::Reload;
  g_sweep.timer = 0;
  g_sweep.idle = 0;
}

}  // namespace

void event_sweep_menu_frame() {
  if (g_sweep.phase == Phase::Run || g_sweep.phase == Phase::Settle) schedule_input(true);
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
    say("start (frame limit %d, skip %d)", s.frame_limit, s.skip);
  }
  if (s.phase == Phase::Off || s.phase == Phase::Done) return;
  ++s.timer;
  const bool is_idle = idle(f);
  const int map = current_map();
  if (map != s.last_map_seen) {
    if (s.last_map_seen != -1) say("now on map %d", map);
    s.last_map_seen = map;
  }
  switch (s.phase) {
    case Phase::Settle:
      // The map's main script runs until the player has control (or it leaves the map).
      schedule_input(!is_idle);
      if (map != s.map && f->state == 0 && is_idle) {
        say("main script left for map %d; settle %s", map, s.have_snapshot ? "(reload)" : "failed");
        if (!s.have_snapshot) {
          s.phase = Phase::Done;
          say("done (left the map) entries 0");
          runtime::Runtime::get().frame_limit = runtime::Runtime::get().vblanks() + 2;
          return;
        }
        reload(f);
        return;
      }
      s.idle = is_idle && map == s.map ? s.idle + 1 : 0;
      if (s.idle >= 20) {
        if (!s.have_snapshot) {
          say("settled after %d frames", s.timer);
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
        // An entry's reload never settled: count it against the previous entry and go on.
        reload(f);
        s.timer = 0;
      }
      return;
    case Phase::Fire:
      if (s.next >= static_cast<int>(s.entries.size())) {
        s.phase = Phase::Done;
        say("done entries %zu", s.entries.size());
        schedule_input(false);
        runtime::Runtime::get().frame_limit = runtime::Runtime::get().vblanks() + 2;
        return;
      }
      fire(s.entries[static_cast<size_t>(s.next)]);
      s.phase = Phase::Run;
      s.timer = 0;
      s.idle = 0;
      s.started = false;
      schedule_input(true);
      return;
    case Phase::Run: {
      schedule_input(!is_idle);
      if (!is_idle) s.started = true;
      const char* result = nullptr;
      std::string moved;
      if (f->state == 6 || f->state == 7) {
        result = "game over";
      } else if (map != s.map && is_idle) {
        moved = "map " + std::to_string(map);
        result = moved.c_str();
      } else if (is_idle && map == s.map) {
        if (++s.idle >= 20) result = "ok";
      } else {
        s.idle = 0;
      }
      if (result == nullptr && s.timer > s.frame_limit) result = "timeout";
      if (result == nullptr) return;
      finish_entry(f, result);
      if (f->state == 6 || f->state == 7) f->state = 0;  // take the game over back
      reload(f);
      return;
    }
    case Phase::Reload:
      schedule_input(false);
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
