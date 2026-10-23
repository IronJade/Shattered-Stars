#include "game/ap/ap.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "ap_net/link.h"
#include "game/battle/battle.h"
#include "game/battle/unit.h"
#include "game/field/field.h"
#include "game/field/message.h"
#include "game/field/notice.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"
#include "runtime/runtime.h"

namespace ss::game::ap {
namespace {

void log(const std::string& line) { std::fprintf(stderr, "[ap] %s\n", line.c_str()); }

// UTF-8 strings <-> paths (as launcher/config.h does; ss_game does not link the launcher).
std::filesystem::path upath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string ustr(const std::filesystem::path& p) {
  const std::u8string u = p.u8string();
  return std::string(u.begin(), u.end());
}

bool fail(std::string* error, const std::string& what) {
  if (error != nullptr) *error = what;
  return false;
}

// A number given as a JSON number or a string ("0x1eb").
bool get_int(const Json& j, int64_t* out) {
  if (j.is_number() || (j.is_string() && !j.as_string().empty())) {
    const int64_t sentinel = INT64_MIN;
    const int64_t v = j.as_int(sentinel);
    if (v == sentinel) return false;
    *out = v;
    return true;
  }
  return false;
}
int get_int_or(const Json& j, int fallback) {
  int64_t v = 0;
  return get_int(j, &v) ? static_cast<int>(v) : fallback;
}

struct Runtime {
  bool on = false;
  Seed seed;
  std::map<int64_t, size_t> by_id;      // location id -> index
  std::map<int, size_t> by_flag;        // chest opened flag -> index
  std::map<int64_t, size_t> by_placeholder;  // patch placeholder -> index
  SaveState state;
  std::set<int64_t> checked;
  struct Pending {
    ItemDef item;
    std::string from;
    bool found = false;                 // a boss reward ("Got X"), not a received item
  };
  std::deque<Pending> pending;          // local items to give (console, boss rewards)
  std::vector<u8> notice_text;          // the notice's map-font text (kept while it shows)
  int notice_frames = 0;                // frames the current notice has shown
  std::deque<std::string> notices;      // notices waiting for the banner
  // The server.
  std::unique_ptr<ap_net::Link> link;
  bool net_ok = false;                  // slot connected, the room's seed is ours
  bool seed_mismatch = false;
  std::vector<ItemDef> net_items;       // the server's items for us, by index
  std::vector<std::string> net_from;
  // DeathLink.
  bool deathlink_in = false;            // received, not yet applied
  std::string deathlink_source;
  bool dying_from_link = false;         // the next game over was a DeathLink's
  // Placeholders reached in one vblank: their messages for the window opening in that vblank
  // (one page each), else a banner each; a placeholder reached again in that vblank (a loop
  // giving copies, as the prize tables do) is ignored.
  std::vector<std::string> override_lines;
  std::vector<u8> override_text;        // the window's text (kept while the window reads it)
  u64 override_vblank = 0;
  std::set<int64_t> hit_this_vblank;    // placeholder values reached in override_vblank
};
Runtime& rt() {
  static Runtime r;
  return r;
}

void index_seed(Runtime& r) {
  r.by_id.clear();
  r.by_flag.clear();
  r.by_placeholder.clear();
  for (size_t i = 0; i < r.seed.locations.size(); ++i) {
    const Location& l = r.seed.locations[i];
    r.by_id[l.id] = i;
    if (l.type == "chest" && l.flag >= 0) r.by_flag[l.flag] = i;
    if (l.placeholder >= 0) r.by_placeholder[l.placeholder] = i;
  }
}

void fresh_state(Runtime& r) {
  r.state = SaveState{};
  r.state.seed = r.seed.seed;
  r.state.slot_name = r.seed.slot_name;
  r.checked.clear();
  r.pending.clear();
}

// ASCII -> the field's map-font glyph codes (one byte below 0x80, else two).
void put_code(std::vector<u8>& out, u32 g) {
  if (g < 0x80) {
    out.push_back(static_cast<u8>(g));
  } else {
    out.push_back(static_cast<u8>((g & 0x7F) | 0x80));
    out.push_back(static_cast<u8>(g >> 7));
  }
}
void put_glyphs(std::vector<u8>& out, const std::string& ascii) {
  for (const char ch : ascii) {
    s32 code = static_cast<u8>(ch);
    if (code >= 0x80) code = '?';
    field::text_ascii_to_glyph(&code, 0, 0);
    put_code(out, static_cast<u32>(code));
  }
}

// A message window's text as the map scripts write it (the "N Fol acquired." string 0x122):
// speed 1, the line, speed 0, wait for the button, end. One line of at most 34 characters
// per page; several lines are pages (0x4001: wait for the button, then empty the window).
std::vector<u8> window_text(const std::vector<std::string>& lines) {
  std::vector<u8> out;
  put_code(out, 0x4003);
  out.push_back(1);
  for (size_t i = 0; i < lines.size(); ++i) {
    std::string ascii = lines[i];
    if (ascii.size() > 34) ascii = ascii.substr(0, 33) + ".";
    if (i > 0) put_code(out, 0x4001);
    put_glyphs(out, ascii);
  }
  put_code(out, 0x4003);
  out.push_back(0);
  put_code(out, 0x4002);
  out.push_back(0);
  return out;
}

// The banner's text (0 ends).
std::vector<u8> map_text(const std::string& ascii) {
  std::vector<u8> out;
  for (const char ch : ascii) {
    s32 code = static_cast<u8>(ch);
    if (code >= 0x80) code = '?';
    field::text_ascii_to_glyph(&code, 0, 0);
    const u32 g = static_cast<u32>(code);
    if (g < 0x80) {
      out.push_back(static_cast<u8>(g));
    } else {
      out.push_back(static_cast<u8>((g & 0x7F) | 0x80));
      out.push_back(static_cast<u8>(g >> 7));
    }
  }
  // A timed wait (code 0x4006, 257 frames: both count bytes non-zero) holds the banner
  // until notice_update closes it; without one it slides out as soon as the text is shown.
  out.push_back(static_cast<u8>((0x4006 & 0x7F) | 0x80));
  out.push_back(static_cast<u8>(0x4006 >> 7));
  out.push_back(0x01);
  out.push_back(0x01);
  out.push_back(0);
  return out;
}

std::string describe(const ItemDef& it) {
  if (it.kind == "fol") return std::to_string(it.amount) + " Fol";
  std::string name = it.name.empty() ? "item " + std::to_string(it.game_id) : it.name;
  if (it.count > 1) name = std::to_string(it.count) + " x " + name;
  return name;
}

void queue_notice(const std::string& text) { rt().notices.push_back(text); }

// Placeholder messages no window took (the give's message came before it, or much later):
// a banner each.
void flush_override_lines(Runtime& r) {
  for (const std::string& line : r.override_lines) queue_notice(line);
  r.override_lines.clear();
}

// The banner: one notice at a time, each shown ~3 seconds; new ones only in free play, and
// one showing is closed early when an event or a menu starts.
void notice_update(bool play) {
  Runtime& r = rt();
  field::Notice* n = field::g_notice_object();
  if (n == nullptr) return;
  if (!r.notice_text.empty()) {
    // (The game's own notices, the map names, use the same banner: only ours is closed.)
    const bool ours = n->text == r.notice_text.data() && n->state != 0;
    r.notice_frames += field::globals().frame_ticks;
    if (ours && ((r.notice_frames > 180 && n->state == 3) || !play)) field::notice_dismiss(n);
    if (!ours && r.notice_frames > 30) {
      r.notice_text.clear();
      r.notice_frames = 0;
    }
    return;
  }
  if (!play || r.notices.empty() || n->state != 0) return;
  r.notice_text = map_text(r.notices.front());
  r.notices.pop_front();
  r.notice_frames = 0;
  // Where the map name goes (script ext 0xb9): bottom right, or top left when the leader
  // stands low on the screen.
  s16 cam_x = 0, cam_y = 0;
  field::vm_svc_bg_get_view_origin(&cam_x, &cam_y);
  const int leader = field::leader_slot();
  const bool top = field::vm_svc_actor_exists(leader) && field::vm_svc_actor_get_view_y(leader) - cam_y > 0xB3;
  field::notice_show(n, r.notice_text.data(), 0, 7, top ? 1 : 0);
}

bool in_free_play() {
  field::Field* f = field::field_program();
  if (f == nullptr || f->state != 0) return false;
  if (field::g_script_vm != nullptr && field::g_script_vm->depth != 0) return false;
  const field::FieldGlobals& g = field::globals();
  s16 control;
  std::memcpy(&control, g.field_flags + 0x10, 2);
  return control != -1;
}

std::string player_label(const Placement& p) {
  Runtime& r = rt();
  if (!p.player_name.empty()) return p.player_name;
  if (r.link) {
    const std::string n = r.link->player_name(p.player);
    if (!n.empty()) return n;
  }
  return "player " + std::to_string(p.player);
}

bool give(const ItemDef& it) {
  field::FieldGlobals& g = field::globals();
  if (it.kind == "fol") {
    s32 fol;
    std::memcpy(&fol, g.config + 0x18, 4);
    const s64 sum = static_cast<s64>(fol) + it.amount;
    fol = static_cast<s32>(std::min<s64>(sum, 99999999));
    std::memcpy(g.config + 0x18, &fol, 4);
    return true;
  }
  if (it.kind != "item") return true;  // (nothing to give for another player's item)
  int added = 0;
  for (int i = 0; i < it.count; ++i) added += field::inventory_add(g.inventory, it.game_id, 1, 1) != 0;
  return added > 0;
}

}  // namespace

// ---- The sidecar ----

std::string sidecar_path(const std::string& card_image, int save_index) {
  std::filesystem::path p = upath(card_image);
  char tail[32];
  std::snprintf(tail, sizeof tail, ".slot%02d.ap.json", save_index + 1);
  p.replace_extension();
  return ustr(p) + tail;
}

Json sidecar_json(const SaveState& s) {
  Json j = Json::object();
  j.set("format", Json::string("apso2-save"));
  j.set("version", Json::number(static_cast<int64_t>(1)));
  j.set("seed", Json::string(s.seed));
  j.set("slot_name", Json::string(s.slot_name));
  j.set("received_index", Json::number(s.received_index));
  j.set("start_given", Json::boolean(s.start_given));
  j.set("goal", Json::boolean(s.goal_done));
  Json checked = Json::array();
  for (const int64_t id : s.checked) checked.push(Json::number(id));
  j.set("checked", checked);
  return j;
}

bool sidecar_from_json(const Json& j, SaveState* s, std::string* error) {
  if (j["format"].as_string() != "apso2-save") return fail(error, "not an apso2 save sidecar");
  if (get_int_or(j["version"], 0) != 1) return fail(error, "unsupported sidecar version");
  SaveState r;
  r.seed = j["seed"].as_string();
  r.slot_name = j["slot_name"].as_string();
  r.received_index = j["received_index"].as_int(0);
  r.start_given = j["start_given"].as_bool(false);
  r.goal_done = j["goal"].as_bool(false);
  for (size_t i = 0; i < j["checked"].size(); ++i) r.checked.push_back(j["checked"][i].as_int(0));
  *s = std::move(r);
  return true;
}

bool write_sidecar(const std::string& path, const SaveState& s, std::string* error) {
  // Through a temporary file, so a crash never leaves half a sidecar.
  const std::filesystem::path p = upath(path);
  const std::filesystem::path tmp = upath(path + ".tmp");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return fail(error, "cannot write " + path + ".tmp");
    out << sidecar_json(s).dump();
    if (!out) return fail(error, "write failed: " + path + ".tmp");
  }
  std::error_code ec;
  std::filesystem::rename(tmp, p, ec);
  if (ec) return fail(error, "cannot replace " + path + ": " + ec.message());
  return true;
}

bool read_sidecar(const std::string& path, SaveState* s, std::string* error) {
  std::ifstream in(upath(path), std::ios::binary);
  if (!in) return fail(error, "no sidecar " + path);
  std::stringstream ss;
  ss << in.rdbuf();
  Json j;
  std::string err;
  if (!Json::parse(ss.str(), &j, &err)) return fail(error, path + ": " + err);
  if (!sidecar_from_json(j, s, &err)) return fail(error, path + ": " + err);
  return true;
}

// ---- The running game ----

bool enabled() { return rt().on; }
const Seed* seed() { return rt().on ? &rt().seed : nullptr; }
const SaveState& state() { return rt().state; }

const std::vector<std::string>& seed_patch_lines() {
  static const std::vector<std::string> none;
  return rt().on ? rt().seed.patches : none;
}

void start_with(Seed s) {
  Runtime& r = rt();
  r.seed = std::move(s);
  r.on = true;
  index_seed(r);
  fresh_state(r);
  r.notices.clear();
  r.notice_text.clear();
  log("seed " + r.seed.seed + " slot " + r.seed.slot_name + ": " + std::to_string(r.seed.locations.size()) +
      " locations, " + std::to_string(r.seed.items.size()) + " item types, " + std::to_string(r.seed.patches.size()) +
      " patch lines");
}

bool start(const std::string& seed_path, std::string* error) {
  Seed s;
  if (!load_seed_file(seed_path, &s, error)) return false;
  start_with(std::move(s));
  return true;
}

void stop() {
  Runtime& r = rt();
  r = Runtime{};
  runtime::Runtime::get().net_poll = nullptr;
}

const Location* find_location(int64_t id) {
  Runtime& r = rt();
  const auto it = r.by_id.find(id);
  return it != r.by_id.end() ? &r.seed.locations[it->second] : nullptr;
}

bool is_checked(int64_t id) { return rt().checked.count(id) != 0; }

namespace {
// Records a check (once); 'banner': another player's item gets a "Sent" notice (not when a
// placeholder's message window says so already).
bool record_check(int64_t id, bool banner) {
  Runtime& r = rt();
  if (!r.on) return false;
  const Location* l = find_location(id);
  if (l == nullptr) {
    log("check " + std::to_string(id) + ": not a location of this seed");
    return false;
  }
  if (!r.checked.insert(id).second) return true;
  r.state.checked.push_back(id);
  const Placement& p = l->item;
  std::string what = p.own ? describe(p.item) : p.item.name;
  log("check " + std::to_string(id) + " (" + l->name + "): " + what + (p.own ? "" : " for " + player_label(p)));
  if (!p.own && banner) queue_notice("Sent " + what + " to " + player_label(p));
  if (r.link && r.net_ok) r.link->check({id});
  return true;
}
}  // namespace

bool location_checked(int64_t id) { return record_check(id, true); }

void on_flag_set(int flag) {
  Runtime& r = rt();
  if (!r.on) return;
  const auto it = r.by_flag.find(flag);
  if (it == r.by_flag.end()) return;
  location_checked(r.seed.locations[it->second].id);
}

void receive(const ItemDef& item, const std::string& from) {
  Runtime& r = rt();
  if (!r.on) return;
  r.pending.push_back({item, from, false});
  log("received " + describe(item) + (from.empty() ? "" : " from " + from) + " (queued, " +
      std::to_string(r.pending.size()) + " waiting)");
}

bool receive_by_name_or_id(const std::string& what, const std::string& from, std::string* error) {
  Runtime& r = rt();
  if (!r.on) return fail(error, "no seed loaded");
  // An AP item id or name of the seed's table, else a game item id.
  for (const auto& [id, it] : r.seed.items) {
    if (it.name == what || std::to_string(id) == what) {
      receive(it, from);
      return true;
    }
  }
  char* end = nullptr;
  const long v = std::strtol(what.c_str(), &end, 0);
  if (end != nullptr && *end == 0 && v >= 1 && v < 0x400) {
    ItemDef it;
    it.kind = "item";
    it.game_id = static_cast<int>(v);
    receive(it, from);
    return true;
  }
  return fail(error, "unknown item '" + what + "' (an AP item id or name of the seed, or a game item id)");
}

size_t pending_count() {
  Runtime& r = rt();
  const size_t net = r.net_items.size() > static_cast<size_t>(r.state.received_index)
                         ? r.net_items.size() - static_cast<size_t>(r.state.received_index)
                         : 0;
  return r.pending.size() + net;
}

void free_play_update() {
  Runtime& r = rt();
  if (!r.on) return;
  if (!in_free_play()) {
    notice_update(false);
    return;
  }
  if (!r.state.start_given) {
    r.state.start_given = true;
    for (const ItemDef& it : r.seed.start_items) give(it);
    if (r.seed.start_fol > 0) {
      ItemDef f;
      f.kind = "fol";
      f.amount = r.seed.start_fol;
      give(f);
    }
    field::FieldGlobals& g = field::globals();
    for (const int flag : r.seed.start_flags)
      if (flag >= 0 && flag < 0x170 * 8) g.script_flags[flag >> 3] = static_cast<u8>(g.script_flags[flag >> 3] | 1 << (flag & 7));
    if (!r.seed.start_items.empty() || r.seed.start_fol > 0) log("starting inventory given");
  }
  // A received DeathLink: the party falls here (the game over), as in a lost battle.
  if (r.deathlink_in) {
    r.deathlink_in = false;
    r.dying_from_link = true;
    log("DeathLink from " + r.deathlink_source + ": the party falls");
    field::field_begin_game_over(field::field_program());
    return;
  }
  // Placeholder messages that no window showed: a banner each instead.
  if (runtime::Runtime::get().vblanks() != r.override_vblank) flush_override_lines(r);
  // One item per banner: local ones first (the console, boss rewards), then the server's in
  // index order (received_index: the next one this save has not had).
  if (r.notice_text.empty() && r.notices.empty()) {
    if (!r.pending.empty()) {
      const Runtime::Pending p = r.pending.front();
      if (give(p.item)) {
        r.pending.pop_front();
        log("given " + describe(p.item));
        queue_notice((p.found ? "Got " : "Received ") + describe(p.item) + (p.from.empty() ? "" : " from " + p.from));
      } else {
        runtime::Runtime::get().log_once("ap-inventory-full", "[ap] the inventory is full; received items wait");
      }
    } else if (static_cast<size_t>(r.state.received_index) < r.net_items.size()) {
      const size_t i = static_cast<size_t>(r.state.received_index);
      const ItemDef& it = r.net_items[i];
      if (give(it)) {
        ++r.state.received_index;
        log("given " + describe(it) + " (server item " + std::to_string(i) + ")");
        if (it.kind == "item" || it.kind == "fol")
          queue_notice("Received " + describe(it) + (r.net_from[i].empty() ? "" : " from " + r.net_from[i]));
      } else {
        runtime::Runtime::get().log_once("ap-inventory-full", "[ap] the inventory is full; received items wait");
      }
    }
  }
  notice_update(true);
}

void on_new_game() {
  Runtime& r = rt();
  if (!r.on) return;
  fresh_state(r);
  log("new game: a fresh state for seed " + r.seed.seed);
}

void on_saved(const std::string& card_image, int save_index) {
  Runtime& r = rt();
  if (!r.on || card_image.empty()) return;
  const std::string path = sidecar_path(card_image, save_index);
  std::string err;
  if (write_sidecar(path, r.state, &err)) {
    log("saved " + path);
  } else {
    log("could not save the sidecar: " + err);
  }
}

void on_loaded(const std::string& card_image, int save_index) {
  Runtime& r = rt();
  if (!r.on || card_image.empty()) return;
  const std::string path = sidecar_path(card_image, save_index);
  SaveState s;
  std::string err;
  if (!read_sidecar(path, &s, &err)) {
    log("loaded a save without Archipelago state (" + err + "); starting fresh");
    fresh_state(r);
    r.state.start_given = true;  // (an ordinary save: no starting inventory on top of it)
    return;
  }
  if (s.seed != r.seed.seed) {
    log("the save's sidecar is for seed " + s.seed + ", not " + r.seed.seed + "; using it anyway");
  } else {
    // A check stays checked (the multiworld keeps it): this session's checks join the save's.
    std::set<int64_t> have(s.checked.begin(), s.checked.end());
    for (const int64_t id : r.state.checked)
      if (have.insert(id).second) s.checked.push_back(id);
  }
  if (s.seed == r.seed.seed && r.state.goal_done) s.goal_done = true;
  r.state = s;
  r.checked = std::set<int64_t>(s.checked.begin(), s.checked.end());
  // Local items still waiting stay queued; the server's items past the save's received
  // index are given again (this session keeps the server's whole list).
  log("loaded " + path + ": " + std::to_string(s.checked.size()) + " checks, received index " +
      std::to_string(s.received_index));
}

// ---- The server ----

namespace {

// A placeholder's location: the item given (ours) or sent (another player's), the check
// recorded, the message for the window the script opens in this vblank.
void placeholder_hit(int64_t value, size_t index) {
  Runtime& r = rt();
  const u64 now = runtime::Runtime::get().vblanks();
  if (now != r.override_vblank) {
    flush_override_lines(r);
    r.hit_this_vblank.clear();
    r.override_vblank = now;
  }
  const Location& l = r.seed.locations[index];
  if (!r.hit_this_vblank.insert(value).second) {
    // A loop giving copies of one item (prize tables): the location's item once.
    log("placeholder " + std::to_string(value) + " (" + l.name + ") again in this vblank: ignored");
    return;
  }
  const Placement& p = l.item;
  std::string text;
  if (p.own) {
    // Our own item: given whenever the game reaches the give (the game's own flags keep a
    // chest or a gift from repeating, as with its original item).
    if (!give(p.item)) {
      r.pending.push_back({p.item, "", true});  // (inventory full: given later)
      log("the inventory is full; " + describe(p.item) + " waits");
    }
    text = "Got " + describe(p.item) + ".";
  } else {
    text = "Sent " + p.item.name + " to " + player_label(p) + ".";
  }
  record_check(l.id, false);
  r.override_lines.push_back(text);
}

}  // namespace

bool on_give_item(s32 value) {
  Runtime& r = rt();
  if (!r.on || value < kItemPlaceholderFirst || value > kItemPlaceholderLast) return false;
  const auto it = r.by_placeholder.find(value);
  if (it == r.by_placeholder.end()) {
    // (No item has such a number: skip it rather than corrupt the inventory.)
    log("give_item " + std::to_string(value) + ": not a placeholder of this seed; nothing given");
    return true;
  }
  placeholder_hit(value, it->second);
  return true;
}

bool on_give_money(s32 amount) {
  Runtime& r = rt();
  if (!r.on || !is_fol_placeholder(amount)) return false;
  const auto it = r.by_placeholder.find(amount);
  if (it == r.by_placeholder.end()) {
    log("add money " + std::to_string(amount) + ": not a placeholder of this seed; nothing given");
    return true;
  }
  placeholder_hit(amount, it->second);
  return true;
}

const u8* take_message_override() {
  Runtime& r = rt();
  if (r.override_lines.empty() || runtime::Runtime::get().vblanks() != r.override_vblank) return nullptr;
  r.override_text = window_text(r.override_lines);
  r.override_lines.clear();
  return r.override_text.data();
}

void connect(std::unique_ptr<ap_net::Link> link) {
  Runtime& r = rt();
  r.link = std::move(link);
  r.net_ok = false;
  r.seed_mismatch = false;
  runtime::Runtime::get().net_poll = r.link ? &vblank : nullptr;
  if (r.link) log("connecting to the server");
}

bool deathlink_enabled() { return rt().on && rt().seed.deathlink; }
bool connected() { return rt().net_ok; }
bool deathlink_pending() { return rt().deathlink_in; }

void deathlink_receive(const std::string& source, const std::string& cause) {
  Runtime& r = rt();
  if (!r.on) return;
  r.deathlink_in = true;
  r.deathlink_source = source.empty() ? "someone" : source;
  log("DeathLink received from " + r.deathlink_source + (cause.empty() ? "" : ": " + cause));
}

void vblank() {
  Runtime& r = rt();
  if (!r.on || !r.link) return;
  for (ap_net::Event& ev : r.link->poll()) {
    switch (ev.kind) {
      case ap_net::Event::kStatus:
        log("server: " + ev.text);
        break;
      case ap_net::Event::kRefused:
        log("the server refused the slot: " + ev.text);
        queue_notice("Archipelago: refused (" + ev.text + ")");
        break;
      case ap_net::Event::kConnected: {
        if (ev.seed_name != r.seed.seed) {
          // Another room: nothing goes out, nothing comes in (playing on offline).
          r.seed_mismatch = true;
          r.net_ok = false;
          log("the server's seed is " + ev.seed_name + ", the seed file's " + r.seed.seed + ": staying offline");
          queue_notice("Archipelago: the server has another seed");
          break;
        }
        r.seed_mismatch = false;
        r.net_ok = true;
        // The server's checks (another session of this slot) count here too; ours go out.
        int marked = 0;
        for (const int64_t id : ev.checked) {
          if (find_location(id) != nullptr && r.checked.insert(id).second) {
            r.state.checked.push_back(id);
            ++marked;
          }
        }
        if (!r.state.checked.empty()) r.link->check(r.state.checked);
        if (r.state.goal_done) r.link->goal();
        log("connected (slot " + std::to_string(ev.slot) + "): sent " + std::to_string(r.state.checked.size()) +
            " checks, " + std::to_string(marked) + " checked elsewhere");
        queue_notice("Archipelago: connected");
        break;
      }
      case ap_net::Event::kItems:
        if (r.seed_mismatch) break;
        for (const ap_net::NetItem& ni : ev.items) {
          if (ni.index < 0) continue;
          const size_t i = static_cast<size_t>(ni.index);
          if (i < r.net_items.size()) continue;  // (sent again after a reconnect)
          if (i > r.net_items.size()) {
            log("server items " + std::to_string(r.net_items.size()) + ".." + std::to_string(i - 1) + " missing");
            r.net_items.resize(i);  // (empty kinds: nothing to give)
            r.net_from.resize(i);
          }
          ItemDef it;
          const auto def = r.seed.items.find(ni.item);
          if (def != r.seed.items.end()) {
            it = def->second;
          } else {
            it.kind = "unknown";
            it.ap_id = ni.item;
            it.name = r.link->item_name(ni.item, r.seed.slot);
            log("server item " + std::to_string(i) + ": AP item " + std::to_string(ni.item) + " is not in the seed's table");
          }
          r.net_items.push_back(it);
          r.net_from.push_back(ni.player == r.seed.slot ? std::string() : r.link->player_name(ni.player));
          log("server item " + std::to_string(i) + ": " + describe(it) +
              (r.net_from.back().empty() ? "" : " from " + r.net_from.back()));
        }
        break;
      case ap_net::Event::kDeathLink:
        if (r.seed.deathlink && !r.seed_mismatch) deathlink_receive(ev.source, ev.text);
        break;
      case ap_net::Event::kMessage:
        log("server says: " + ev.text);
        break;
    }
  }
}

// ---- Battles ----

void on_battle_exit(int encounter, int outcome) {
  Runtime& r = rt();
  if (!r.on || outcome != 0) return;
  for (const Location& l : r.seed.locations) {
    if (l.type != "boss" || l.encounter != encounter || is_checked(l.id)) continue;
    record_check(l.id, true);
    if (l.item.own) r.pending.push_back({l.item.item, "", true});
  }
  if (!r.state.goal_done &&
      std::find(r.seed.goal_encounters.begin(), r.seed.goal_encounters.end(), encounter) != r.seed.goal_encounters.end()) {
    r.state.goal_done = true;
    log("goal: encounter " + std::to_string(encounter) + " won");
    if (r.link && r.net_ok) r.link->goal();
  }
}

void battle_frame() {
  Runtime& r = rt();
  if (!r.on || !r.deathlink_in) return;
  battle::BattleGlobals& g = battle::bg();
  // In the fight, between special moves: every standing member falls, as from a lethal hit
  // (HP 0, death 1); the battle's own check then loses the fight (outcome 2).
  if (g.state != battle::kStateFighting || (g.flags & 0x40) != 0) return;
  r.deathlink_in = false;
  r.dying_from_link = true;
  for (battle::PartyUnit* u : g.party_units) {
    if (u == nullptr || battle::unit_is_down(u)) continue;
    u->st()->hp = 0;
    u->death = 1;
  }
  log("DeathLink from " + r.deathlink_source + ": the party falls in battle");
}

void on_game_over() {
  Runtime& r = rt();
  if (!r.on) return;
  const bool from_link = r.dying_from_link;
  r.dying_from_link = false;
  if (from_link || !r.seed.deathlink) return;
  if (r.link && r.net_ok) {
    log("game over: DeathLink sent");
    r.link->deathlink(r.seed.slot_name + "'s party fell");
  } else {
    log("game over: DeathLink not sent (offline)");
  }
}

std::string status_text() {
  Runtime& r = rt();
  if (!r.on) return "Archipelago off (no seed)";
  char buf[320];
  const char* net = !r.link ? "offline" : r.seed_mismatch ? "wrong seed" : r.net_ok ? "connected" : "connecting";
  std::snprintf(buf, sizeof buf, "seed %s | slot %s | checks %zu/%zu | received %lld | waiting %zu | %s%s%s",
                r.seed.seed.c_str(), r.seed.slot_name.c_str(), r.checked.size(), r.seed.locations.size(),
                static_cast<long long>(r.state.received_index), pending_count(), net,
                r.seed.deathlink ? " | DeathLink" : "", r.state.goal_done ? " | goal" : "");
  return buf;
}

}  // namespace ss::game::ap
