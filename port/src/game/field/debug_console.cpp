#include "game/field/debug_console.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "game/ap/ap.h"
#include "game/field/actor_base.h"
#include "game/field/actors.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
s16 rd16(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
void wr32(u8* p, s32 v) { std::memcpy(p, &v, 4); }
void wr16(u8* p, s32 v) {
  const s16 x = static_cast<s16>(v);
  std::memcpy(p, &x, 2);
}

bool number(const std::string& s, long long* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  *out = std::strtoll(s.c_str(), &end, 0);
  return end != nullptr && *end == 0;
}

void say(const std::string& text) { runtime::Runtime::get().debug_say(text); }

const char* kHelp =
    "commands (numbers in decimal or 0x hex):\n"
    "  where                     map, field state, leader position, Fol, story globals\n"
    "  warp MAP [X Y Z [DIR]]    go to field map MAP (its default spot, or X Y Z facing DIR)\n"
    "  warp MAP force            the same from inside a scene (debugging: the scene is dropped)\n"
    "  fol N                     set the party's Fol\n"
    "  item ID [COUNT]           add an item (new, as from a chest)\n"
    "  heal                      every member's HP and MP to full\n"
    "  flag N [0|1]              read / set script flag N\n"
    "  global OFF [VALUE]        read / set script global G[OFF] (s32)\n"
    "  battle ENC [STAGE]        start encounter ENC (stage 3 by default)\n"
    "  noclip                    the leader walks through walls (toggle)\n"
    "  ap_status                 Archipelago: seed, checks, received items (needs --ap-seed)\n"
    "  ap_check ID               Archipelago: record location ID as checked\n"
    "  ap_give ITEM [FROM]       Archipelago: receive ITEM (AP id / name of the seed, or a game\n"
    "                            item id); given in free play with a notice\n"
    "  ap_deathlink [FROM]       Archipelago: a DeathLink as if received (the party falls)";

// The Archipelago commands (game/ap): they work outside the field too (the queue waits).
bool ap_command(const std::vector<std::string>& w) {
  const std::string& cmd = w[0];
  if (cmd == "ap_status" && w.size() == 1) {
    say(ap::status_text());
  } else if (cmd == "ap_check" && w.size() == 2) {
    if (!ap::enabled()) {
      say("Archipelago is off (start with --ap-seed FILE)");
      return true;
    }
    long long id = 0;
    if (!number(w[1], &id)) {
      say("not a number: " + w[1]);
    } else if (ap::is_checked(id)) {
      say("location " + w[1] + " was already checked");
    } else if (ap::location_checked(id)) {
      const ap::Location* l = ap::find_location(id);
      say("checked " + w[1] + (l != nullptr && !l->name.empty() ? " (" + l->name + ")" : ""));
    } else {
      say("location " + w[1] + " is not in this seed");
    }
  } else if (cmd == "ap_deathlink" && (w.size() == 1 || w.size() == 2)) {
    if (!ap::enabled()) {
      say("Archipelago is off (start with --ap-seed FILE)");
      return true;
    }
    ap::deathlink_receive(w.size() == 2 ? w[1] : "the console", "");
    say("DeathLink pending: the party falls at the next safe point");
  } else if (cmd == "ap_give" && (w.size() == 2 || w.size() == 3)) {
    std::string error;
    if (ap::receive_by_name_or_id(w[1], w.size() == 3 ? w[2] : "", &error)) {
      say("queued " + w[1] + " (" + std::to_string(ap::pending_count()) + " waiting for free play)");
    } else {
      say(error);
    }
  } else {
    return false;
  }
  return true;
}

Actor* leader() { return field_actor(globals().game_state[0x24]); }

}  // namespace

std::string debug_console_status() {
  Field* f = field_program();
  if (f == nullptr) return "not in the field";
  FieldGlobals& g = globals();
  char line[160];
  const Actor* a = leader();
  std::snprintf(line, sizeof line, "map %d | state %d | %d,%d,%d | Fol %d", rd16(g.game_state + 0x1A), f->state,
                a ? a->x >> 12 : 0, a ? a->y >> 12 : 0, a ? a->z >> 12 : 0, rd32(g.config + 0x18));
  return line;
}

void debug_console_command(const std::string& text) {
  std::istringstream in(text);
  std::vector<std::string> w;
  for (std::string t; in >> t;) w.push_back(t);
  if (w.empty()) return;
  Field* f = field_program();
  if (w[0] == "help") {
    say(kHelp);
    return;
  }
  if (w[0].rfind("ap_", 0) == 0 && ap_command(w)) return;
  if (f == nullptr) {
    say("not in the field (commands work once a map is loaded)");
    return;
  }
  FieldGlobals& g = globals();
  std::vector<long long> n(w.size(), 0);
  for (size_t i = 1; i < w.size(); ++i) {
    if (i == 2 && w[0] == "warp" && w[i] == "force") continue;  // (warp MAP force) [shingofix agent]
    if (!number(w[i], &n[i])) {
      say("not a number: " + w[i]);
      return;
    }
  }
  const std::string& cmd = w[0];
  if (cmd == "where") {
    const Actor* a = leader();
    char line[256];
    std::snprintf(line, sizeof line, "%s | view %d,%d | control %d | G[0xa0]=%d G[0xa4]=%d",
                  debug_console_status().c_str(), a ? a->screen_x : 0, a ? a->screen_y : 0,
                  rd16(g.field_flags + 0x10), rd32(g.field_flags + 0xA0), rd32(g.field_flags + 0xA4));
    say(line);
  } else if (cmd == "warp" && (w.size() == 2 || w.size() >= 5 || (w.size() == 3 && w[2] == "force"))) {
    const int map = static_cast<int>(n[1]);
    if (map < 0 || map > 826) {
      say("field maps are 0-826");
      return;
    }
    // "force" [shingofix agent]: also while a scene runs (a map whose main routine never returns,
    // as the tournament's 476); not from a menu or a map change.
    const bool force = w.size() == 3;
    if (f->state != 0 || (!force && g_script_vm != nullptr && g_script_vm->depth != 0)) {
      say("busy (an event or a menu is running); try again in free play");
      return;
    }
    if (w.size() <= 3) {
      // The map's own entry spot: field_update state 1 loads it like a scripted change.
      f->pending_map = map;
      fade_push(field_fade(), 3, 0x10, 0, 0);
      f->state = 1;
    } else {
      // As script map_transfer (ext 0x7d): the VM's wait state 1 loads the map and
      // places the leader at X Y Z.
      ScriptCtx* ctx = g_script_vm;
      ctx->arrival_param = 0x80;
      vm_svc_preload_release(1);
      wr32(g.game_state + 8, static_cast<s32>(n[2]));
      wr32(g.game_state + 0xC, static_cast<s32>(n[3]));
      wr32(g.game_state + 0x10, static_cast<s32>(n[4]));
      ctx->arrival_dir = w.size() > 5 ? static_cast<s32>(n[5]) : 0;
      ctx->transition = 0;
      ctx->transfer_flag = 0;
      ctx->wait_param = map;
      ctx->wait_state = 1;
      fade_push(field_fade(), 3, 0x10, 0, 0);
      wr16(g.field_flags + 0x10, -1);
    }
    say("warping to map " + std::to_string(map));
  } else if (cmd == "fol" && w.size() == 2) {
    const s32 v = static_cast<s32>(n[1] < 0 ? 0 : n[1] > 999999999 ? 999999999 : n[1]);
    wr32(g.config + 0x18, v);
    say("Fol " + std::to_string(v));
  } else if (cmd == "item" && (w.size() == 2 || w.size() == 3)) {
    const int count = w.size() == 3 ? static_cast<int>(n[2]) : 1;
    int added = 0;
    for (int i = 0; i < count; ++i) added += inventory_add(g.inventory, static_cast<int>(n[1]), 1, 1) != 0;
    say("item " + w[1] + ": added " + std::to_string(added));
  } else if (cmd == "heal" && w.size() == 1) {
    for (int slot = 0; slot < 8; ++slot) {
      u8* p = g.party + slot * 0x60;
      if (rd16(p) <= 0) continue;
      wr32(p + 0x1C, rd32(p + 0x18));
      wr16(p + 0x24, rd16(p + 0x22));
    }
    say("party healed");
  } else if (cmd == "flag" && (w.size() == 2 || w.size() == 3)) {
    const long long bit = n[1];
    if (bit < 0 || bit >= 0x170 * 8) {
      say("flag out of range");
      return;
    }
    u8& byte = g.script_flags[bit >> 3];
    if (w.size() == 3) byte = static_cast<u8>(n[2] ? byte | 1 << (bit & 7) : byte & ~(1 << (bit & 7)));
    say("flag " + w[1] + " = " + std::to_string(byte >> (bit & 7) & 1));
  } else if (cmd == "global" && (w.size() == 2 || w.size() == 3)) {
    const long long off = n[1];
    if (off < 0 || off + 4 > 0x8C0 - 0x180) {
      say("global offset out of range");
      return;
    }
    if (w.size() == 3) wr32(g.field_flags + off, static_cast<s32>(n[2]));
    say("G[" + w[1] + "] = " + std::to_string(rd32(g.field_flags + off)));
  } else if (cmd == "battle" && (w.size() == 2 || w.size() == 3)) {
    if (f->state != 0) {
      say("busy; try again in free play");
      return;
    }
    wr16(g.game_state + 0x264, static_cast<s32>(n[1]));
    wr16(g.field_flags + 0x12, w.size() == 3 ? static_cast<s32>(n[2]) : 3);
    f->state = 0x21;
    say("starting encounter " + w[1]);
  } else if (cmd == "noclip" && w.size() == 1) {
    Actor* a = leader();
    if (a == nullptr) {
      say("no leader");
      return;
    }
    a->flags ^= kActorWalls;
    say((a->flags & kActorWalls) ? "walls on" : "noclip on");
  } else {
    say("unknown command or wrong arguments: " + text + " (try 'help')");
  }
}

void debug_console_install() {
  auto& rt = runtime::Runtime::get();
  rt.debug_command = debug_console_command;
  rt.debug_status = debug_console_status;
}

}  // namespace ss::game::field
