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

bool parse_item(const Json& j, ItemDef* out, std::string* error, const std::string& where) {
  if (!j.is_object()) return fail(error, where + ": an item must be an object");
  ItemDef it;
  int64_t v = 0;
  if (get_int(j["ap_id"], &v)) it.ap_id = v;
  it.name = j["name"].as_string();
  it.kind = j["kind"].is_string() ? j["kind"].as_string() : "item";
  if (it.kind == "item") {
    if (!get_int(j["game_id"], &v) || v < 1 || v >= 0x400) return fail(error, where + ": game_id 1-1023 expected");
    it.game_id = static_cast<int>(v);
    it.count = get_int_or(j["count"], 1);
    if (it.count < 1 || it.count > 20) return fail(error, where + ": count 1-20 expected");
  } else if (it.kind == "fol") {
    if (!get_int(j["amount"], &v) || v < 0) return fail(error, where + ": amount expected");
    it.amount = static_cast<int>(v);
  } else if (it.kind != "foreign") {
    return fail(error, where + ": unknown kind '" + it.kind + "'");
  }
  *out = it;
  return true;
}

struct Runtime {
  bool on = false;
  Seed seed;
  std::map<int64_t, size_t> by_id;      // location id -> index
  std::map<int, size_t> by_flag;        // chest opened flag -> index
  SaveState state;
  std::set<int64_t> checked;
  struct Pending {
    ItemDef item;
    std::string from;
  };
  std::deque<Pending> pending;
  std::vector<u8> notice_text;          // the notice's map-font text (kept while it shows)
  int notice_frames = 0;                // frames the current notice has shown
  std::deque<std::string> notices;      // notices waiting for the banner
};
Runtime& rt() {
  static Runtime r;
  return r;
}

void index_seed(Runtime& r) {
  r.by_id.clear();
  r.by_flag.clear();
  for (size_t i = 0; i < r.seed.locations.size(); ++i) {
    const Location& l = r.seed.locations[i];
    r.by_id[l.id] = i;
    if (l.type == "chest" && l.flag >= 0) r.by_flag[l.flag] = i;
  }
}

void fresh_state(Runtime& r) {
  r.state = SaveState{};
  r.state.seed = r.seed.seed;
  r.state.slot_name = r.seed.slot_name;
  r.checked.clear();
  r.pending.clear();
}

// ASCII -> the field's map-font text (glyph codes: one byte below 0x80, else two; 0 ends).
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

// The banner: one notice at a time, each shown ~3 seconds; new ones only in free play, and
// one showing is closed early when an event or a menu starts.
void notice_update(bool play) {
  Runtime& r = rt();
  field::Notice* n = field::g_notice_object();
  if (n == nullptr) return;
  if (!r.notice_text.empty()) {
    r.notice_frames += field::globals().frame_ticks;
    if ((r.notice_frames > 180 || !play) && n->state == 3) field::notice_dismiss(n);
    if (n->state == 0 && r.notice_frames > 30) {
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

// ---- The seed file ----

bool load_seed_text(const std::string& text, Seed* out, std::string* error) {
  Json j;
  std::string perr;
  if (!Json::parse(text, &j, &perr)) return fail(error, "not JSON: " + perr);
  if (!j.is_object()) return fail(error, "the seed must be a JSON object");
  if (j["format"].as_string() != "apso2") return fail(error, "format must be \"apso2\"");
  Seed s;
  s.version = get_int_or(j["version"], 0);
  if (s.version != 1) return fail(error, "unsupported version " + std::to_string(s.version) + " (this build reads 1)");
  s.seed = j["seed"].as_string();
  if (s.seed.empty()) return fail(error, "seed (the multiworld's seed id) missing");
  s.slot_name = j["slot"]["name"].as_string();
  s.slot = get_int_or(j["slot"]["id"], 0);
  s.game = j["game"].as_string();
  s.options = j["options"].is_object() ? j["options"] : Json::object();
  for (const auto& [key, v] : j["items"].items()) {
    ItemDef it;
    if (!parse_item(v, &it, error, "items." + key)) return false;
    int64_t id = 0;
    if (!get_int(Json::string(key), &id)) return fail(error, "items: key '" + key + "' is not an AP item id");
    it.ap_id = id;
    s.items[id] = it;
  }
  std::set<int64_t> seen;
  const Json& locs = j["locations"];
  if (!locs.is_array()) return fail(error, "locations must be an array");
  for (size_t i = 0; i < locs.size(); ++i) {
    const Json& lj = locs[i];
    const std::string where = "locations[" + std::to_string(i) + "]";
    Location l;
    int64_t v = 0;
    if (!get_int(lj["id"], &v)) return fail(error, where + ": id missing");
    l.id = v;
    if (!seen.insert(l.id).second) return fail(error, where + ": id " + std::to_string(l.id) + " repeated");
    l.name = lj["name"].as_string();
    const Json& c = lj["check"];
    l.type = c["type"].as_string();
    l.map = get_int_or(c["map"], -1);
    if (l.type == "chest") {
      l.flag = get_int_or(c["flag"], -1);
      if (l.flag < 0 || l.flag >= 0x170 * 8) return fail(error, where + ": a chest needs its opened flag (0-2943)");
    } else if (l.type == "gift") {
      l.offset = get_int_or(c["offset"], -1);
      if (l.map < 0 || l.offset < 0) return fail(error, where + ": a gift needs map and offset");
    } else if (l.type == "boss") {
      l.encounter = get_int_or(c["encounter"], -1);
      if (l.encounter < 0) return fail(error, where + ": a boss needs its encounter");
    } else if (l.type.empty()) {
      return fail(error, where + ": check.type missing");
    }
    const Json& pj = lj["item"];
    if (!pj.is_object()) return fail(error, where + ": item missing");
    Placement p;
    p.player = get_int_or(pj["player"], s.slot);
    p.player_name = pj["player_name"].as_string();
    p.classification = pj["classification"].as_string();
    p.own = p.player == s.slot;
    if (p.own) {
      if (!parse_item(pj, &p.item, error, where + ".item")) return false;
    } else {
      p.item.kind = "foreign";
      p.item.name = pj["name"].as_string();
      if (get_int(pj["ap_id"], &v)) p.item.ap_id = v;
    }
    l.item = p;
    s.locations.push_back(l);
  }
  for (size_t i = 0; i < j["patches"].size(); ++i) {
    const std::string& line = j["patches"][i].as_string();
    if (!line.empty()) s.patches.push_back(line);
  }
  const Json& st = j["start"];
  for (size_t i = 0; i < st["items"].size(); ++i) {
    ItemDef it;
    if (!parse_item(st["items"][i], &it, error, "start.items[" + std::to_string(i) + "]")) return false;
    s.start_items.push_back(it);
  }
  s.start_fol = get_int_or(st["fol"], 0);
  for (size_t i = 0; i < st["flags"].size(); ++i) s.start_flags.push_back(get_int_or(st["flags"][i], -1));
  *out = std::move(s);
  return true;
}

bool load_seed_file(const std::string& path, Seed* out, std::string* error) {
  std::ifstream in(upath(path), std::ios::binary);
  if (!in) return fail(error, "cannot open " + path);
  std::stringstream ss;
  ss << in.rdbuf();
  std::string err;
  if (!load_seed_text(ss.str(), out, &err)) return fail(error, path + ": " + err);
  return true;
}

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
}

const Location* find_location(int64_t id) {
  Runtime& r = rt();
  const auto it = r.by_id.find(id);
  return it != r.by_id.end() ? &r.seed.locations[it->second] : nullptr;
}

bool is_checked(int64_t id) { return rt().checked.count(id) != 0; }

bool location_checked(int64_t id) {
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
  log("check " + std::to_string(id) + " (" + l->name + "): " + what +
      (p.own ? "" : " for " + (p.player_name.empty() ? "player " + std::to_string(p.player) : p.player_name)));
  if (!p.own) queue_notice("Sent " + what + " to " + (p.player_name.empty() ? "player " + std::to_string(p.player) : p.player_name));
  return true;
}

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
  r.pending.push_back({item, from});
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

size_t pending_count() { return rt().pending.size(); }

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
  if (!r.pending.empty() && r.notice_text.empty()) {
    const Runtime::Pending p = r.pending.front();
    if (give(p.item)) {
      r.pending.pop_front();
      ++r.state.received_index;
      log("given " + describe(p.item) + " (received index " + std::to_string(r.state.received_index) + ")");
      queue_notice("Received " + describe(p.item) + (p.from.empty() ? "" : " from " + p.from));
    } else {
      runtime::Runtime::get().log_once("ap-inventory-full", "[ap] the inventory is full; received items wait");
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
  r.state = s;
  r.checked = std::set<int64_t>(s.checked.begin(), s.checked.end());
  // Items still waiting stay queued and are given in the loaded game. (Phase 2 resyncs
  // from the server instead: it sends everything past the save's received index.)
  log("loaded " + path + ": " + std::to_string(s.checked.size()) + " checks, received index " +
      std::to_string(s.received_index));
}

std::string status_text() {
  Runtime& r = rt();
  if (!r.on) return "Archipelago off (no seed)";
  char buf[256];
  std::snprintf(buf, sizeof buf, "seed %s | slot %s | checks %zu/%zu | received %lld | waiting %zu",
                r.seed.seed.c_str(), r.seed.slot_name.c_str(), r.checked.size(), r.seed.locations.size(),
                static_cast<long long>(r.state.received_index), r.pending.size());
  return buf;
}

}  // namespace ss::game::ap
