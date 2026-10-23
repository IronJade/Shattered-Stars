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
#include "game/boot/system.h"
#include "game/field/field.h"
#include "game/field/load_queue.h"
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
  // Phase 3.
  std::map<int64_t, size_t> join_sites;  // (map << 32 | offset) -> join location index
  std::map<int, size_t> by_enemy;        // enemy type -> drop location index
  Scene scene = Scene::kField;           // (tests run without programs: as in the field)
  bool ending = false;                   // the final boss is beaten: the ending and credits
  int control_frames = 0;                // field frames the player has had the controls in a row
  // Phase 4.
  std::map<int, std::vector<size_t>> doors_by_script;  // the script's map (canonical) -> seed doors
  int script_map = -1;                   // the loaded script's map (canonical), -1 none yet
  std::set<int64_t> bad_doors;           // seed doors whose word is not a map transfer: ignored
  std::map<int, size_t> by_shop_slot;    // (shop << 8 | index) -> shop location index
};
Runtime& rt() {
  static Runtime r;
  return r;
}

// Maps that run one shared script (docs/ap/field.md "How it reads the scripts"): a door is
// listed under the lowest map (above 0), as tools/ap_doors.py writes it.
int script_group(int map) {
  if (map == 0 || map == 2 || map == 3 || map == 826) return 1;             // the world script
  if (map >= 909 && map <= 980) return 909;                                 // Expel's world-map chunks
  if (map >= 1048 && map <= 1110) return 1048;                              // Energy Nede's
  if (map == 1114) return 1113;                                             // world map 3's
  if (map == 345 || map == 346 || (map >= 352 && map <= 354) || map == 356) return 340;  // one dungeon's rooms
  return map;
}

void index_seed(Runtime& r) {
  r.by_id.clear();
  r.by_flag.clear();
  r.by_placeholder.clear();
  r.join_sites.clear();
  r.by_enemy.clear();
  r.doors_by_script.clear();
  r.by_shop_slot.clear();
  r.bad_doors.clear();
  for (size_t i = 0; i < r.seed.locations.size(); ++i) {
    const Location& l = r.seed.locations[i];
    r.by_id[l.id] = i;
    if (l.type == "chest" && l.flag >= 0) r.by_flag[l.flag] = i;
    if (l.placeholder >= 0) r.by_placeholder[l.placeholder] = i;
    if (l.type == "join") r.join_sites[static_cast<int64_t>(l.map) << 32 | static_cast<u32>(l.offset)] = i;
    if (l.type == "drop") r.by_enemy[l.enemy] = i;
    if (l.type == "shop") r.by_shop_slot[l.shop << 8 | l.index] = i;
  }
  for (size_t i = 0; i < r.seed.doors.size(); ++i) {
    std::set<int> scripts;
    for (const int m : r.seed.doors[i].maps) scripts.insert(script_group(m));
    for (const int m : scripts) r.doors_by_script[m].push_back(i);
  }
}

void fresh_state(Runtime& r) {
  r.state = SaveState{};
  r.state.seed = r.seed.seed;
  r.state.slot_name = r.seed.slot_name;
  r.checked.clear();
  r.pending.clear();
  r.deathlink_in = false;
  r.dying_from_link = false;
  r.ending = false;
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
  if (it.kind == "character") return character_name(it.character);
  if (it.kind == "trap") return it.name.empty() ? "a trap" : it.name;
  if (it.kind == "key") return it.name.empty() ? "Key " + std::to_string(it.key) : it.name;
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

// The player moves the leader on the field (field state 0, field_flags +0x10 not -1): no
// menu, vehicle ride, battle transition or game over, and no scene holding the controls.
bool player_in_control() {
  field::Field* f = field::field_program();
  if (f == nullptr || f->state != 0) return false;
  const field::FieldGlobals& g = field::globals();
  s16 control;
  std::memcpy(&control, g.field_flags + 0x10, 2);
  return control != -1;
}

// Free play: the player in control and no event script running (items, joins, traps).
bool in_free_play() {
  if (!player_in_control()) return false;
  return field::g_script_vm == nullptr || field::g_script_vm->depth == 0;
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

// ---- Phase 3 helpers ----

constexpr int kFinalBoss[] = {539, 540};  // Indalecio (docs/ap/battle.md): the ending follows

s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
s32 rd32s(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
bool script_flag(int n) {
  const field::FieldGlobals& g = field::globals();
  return g.script_flags != nullptr && ((g.script_flags[n >> 3] >> (n & 7)) & 1) != 0;
}

// Party slots (handoff 3: 8 x 0x60, +0 the id, negative for a member in reserve).
u8* party_slot(int i) {
  u8* party = field::globals().party;
  return party != nullptr ? party + i * 0x60 : nullptr;
}
int slot_of_character(int id) {
  for (int i = 0; i < 8; ++i) {
    const u8* p = party_slot(i);
    if (p == nullptr) return -1;
    const s16 v = rd16s(p);
    if (v == id || v == -id) return i;
  }
  return -1;
}
bool party_has_room() {
  for (int i = 0; i < 8; ++i)
    if (const u8* p = party_slot(i); p != nullptr && rd16s(p) == 0) return true;
  return false;
}

// The protagonist's exclusions (docs/ap/field.md "Who excludes whom"): Leon joins only in
// Claude's game (script flag 1 set), Dias only in Rena's.
const char* protagonist_refuses(int id) {
  const bool claude = script_flag(1);
  if (id == 8 && !claude) return "Rena";
  if (id == 5 && claude) return "Claude";
  return nullptr;
}

bool character_received(const Runtime& r, int id) {
  return std::find(r.state.characters.begin(), r.state.characters.end(), id) != r.state.characters.end();
}

// A received Celine travels with the party: script flag 112, which map 131 stores from
// party_has(Celine) as chapter 15 begins and Mars (462 / 125 / 126) reads (keeps_flag).
constexpr int kCelineKeptFlag = 112;
constexpr int kCeline = 3;
void keep_story_flags(const Runtime& r) {
  field::FieldGlobals& g = field::globals();
  if (!r.seed.character_items || g.script_flags == nullptr || !character_received(r, kCeline)) return;
  if (!script_flag(kCelineKeptFlag)) log("Celine received: script flag 112 set (she travels with the party)");
  g.script_flags[kCelineKeptFlag >> 3] = static_cast<u8>(g.script_flags[kCelineKeptFlag >> 3] | 1 << (kCelineKeptFlag & 7));
}

// The endings: after the final boss (539 / 540), or the endings flow itself (script flag
// 0x2f1: map 581 plays the endings and the credits while it is set; docs/notes/endings.md).
bool in_ending(const Runtime& r) { return r.ending || script_flag(0x2F1); }

// A trap's effect, now ('note': what the banner says). The ambush waits for the field.
void spring_trap(Runtime& r, const ItemDef& it, std::string* note) {
  field::FieldGlobals& g = field::globals();
  if (it.trap == "ambush") {
    ++r.state.ambushes;
    *note = "an ambush!";
  } else if (it.trap == "fol_loss") {
    s32 fol = g.config != nullptr ? rd32s(g.config + 0x18) : 0;
    const s32 lost = static_cast<s32>(static_cast<s64>(fol) * it.percent / 100);
    fol -= lost;
    if (g.config != nullptr) std::memcpy(g.config + 0x18, &fol, 4);
    *note = "lost " + std::to_string(lost) + " Fol";
  } else if (it.trap == "status") {
    // Field statuses (slot +2): 1 down, 2 paralysis, 4 stone, 8 poison (item effects 0x41 /
    // 0x42 / 0x62 cure them). Poison takes every active member who is up; paralysis and
    // stone take one (the last active one), never the last member standing.
    const u8 bit = it.status == "paralysis" ? 2 : it.status == "stone" ? 4 : 8;
    int up[4], n = 0;
    for (int i = 0; i < 4; ++i) {
      const u8* p = party_slot(i);
      if (p != nullptr && rd16s(p) > 0 && (p[2] & 7) == 0 && rd32s(p + 0x1C) > 0) up[n++] = i;
    }
    if (bit == 8 || n < 2) {
      for (int k = 0; k < n; ++k) party_slot(up[k])[2] |= 8;
      *note = n > 0 ? "the party is poisoned" : "nothing happened";
    } else {
      u8* p = party_slot(up[n - 1]);
      p[2] |= bit;
      *note = std::string(character_name(rd16s(p))) + (bit == 2 ? " is paralysed" : " is turned to stone");
    }
  }
  log("trap " + it.trap + ": " + *note);
}

// Gives an item of this game: false when it has to wait (a full inventory). 'note' (when
// given) receives a trap's effect for its banner.
bool give(const ItemDef& it, std::string* note = nullptr) {
  Runtime& r = rt();
  field::FieldGlobals& g = field::globals();
  if (it.kind == "fol") {
    s32 fol;
    std::memcpy(&fol, g.config + 0x18, 4);
    const s64 sum = static_cast<s64>(fol) + it.amount;
    fol = static_cast<s32>(std::min<s64>(sum, 99999999));
    std::memcpy(g.config + 0x18, &fol, 4);
    return true;
  }
  if (it.kind == "character") {
    // Received: the member joins at the next safe point (free_play_update).
    if (std::find(r.state.characters.begin(), r.state.characters.end(), it.character) == r.state.characters.end())
      r.state.characters.push_back(it.character);
    if (std::find(r.state.characters_waiting.begin(), r.state.characters_waiting.end(), it.character) ==
        r.state.characters_waiting.end())
      r.state.characters_waiting.push_back(it.character);
    keep_story_flags(r);
    return true;
  }
  if (it.kind == "trap") {
    std::string what;
    spring_trap(r, it, &what);
    if (note != nullptr) *note = what;
    return true;
  }
  if (it.kind == "key") {
    // An area key (open world): the doors that name it open from now on.
    if (std::find(r.state.keys.begin(), r.state.keys.end(), it.key) == r.state.keys.end()) r.state.keys.push_back(it.key);
    log("key " + std::to_string(it.key) + " (" + describe(it) + ") received");
    return true;
  }
  if (it.kind != "item") return true;  // (nothing to give for another player's item)
  int added = 0;
  for (int i = 0; i < it.count; ++i) added += field::inventory_add(g.inventory, it.game_id, 1, 1) != 0;
  return added > 0;
}

// The received item's banner.
std::string received_text(const ItemDef& it, const std::string& from, bool found, const std::string& note) {
  if (it.kind == "trap") return "Trap" + (from.empty() ? std::string() : " from " + from) + ": " + note;
  return (found ? "Got " : "Received ") + describe(it) + (from.empty() ? "" : " from " + from);
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
  // Phase 3 (absent in older sidecars: empty).
  auto ints = [](const std::vector<int>& v) {
    Json a = Json::array();
    for (const int n : v) a.push(Json::number(static_cast<int64_t>(n)));
    return a;
  };
  j.set("characters", ints(s.characters));
  j.set("characters_waiting", ints(s.characters_waiting));
  j.set("first_drops", ints(s.first_drops));
  j.set("ambushes", Json::number(static_cast<int64_t>(s.ambushes)));
  j.set("keys", ints(s.keys));  // phase 4
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
  auto ints = [&](const char* key, std::vector<int>* out) {
    for (size_t i = 0; i < j[key].size(); ++i) out->push_back(static_cast<int>(j[key][i].as_int(0)));
  };
  ints("characters", &r.characters);
  ints("characters_waiting", &r.characters_waiting);
  ints("first_drops", &r.first_drops);
  r.ambushes = static_cast<int>(j["ambushes"].as_int(0));
  ints("keys", &r.keys);
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
  if (r.seed.expel_teleporter) {
    // The Expel teleporter (open world; docs/ap/seed_format.md "Phase 4"): Virtual Expel's two
    // ways from chapter 36 on. The Fun City arena's old man (map 303, actor 25) offers the trip
    // to Arlia when flag 730 is set (the Final Save Point): his test becomes chapter >= 36. Arlia's
    // guide back to the arena (map 18, actor 26) stands there from chapter 60: from 36.
    r.seed.patches.push_back("script 303 0x1307 0x3b0302da 0x430000a0  # old man: flag[730] == 1 -> chapter >= 36");
    r.seed.patches.push_back("script 303 0x1308 0x00000001 0x00000024");
    r.seed.patches.push_back("script 18 0x996 0x0000003c 0x00000024  # Arlia's guide: chapter >= 60 -> 36");
  }
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
  // (Testing phase 3: a character's name, or trap:ambush / trap:fol_loss[:PERCENT] /
  // trap:status[:poison|paralysis|stone].)
  ItemDef it;
  if (const int id = character_by_name(what); id != 0) {
    it.kind = "character";
    it.character = id;
    it.name = what;
    receive(it, from);
    return true;
  }
  if (what.rfind("key:", 0) == 0) {  // (phase 4: key:N, an area key)
    it.kind = "key";
    it.key = std::atoi(what.c_str() + 4);
    if (it.key >= 1 && it.key <= 255) {
      for (const auto& [id, def] : r.seed.items)
        if (def.kind == "key" && def.key == it.key) it = def;
      receive(it, from);
      return true;
    }
  }
  if (what.rfind("trap:", 0) == 0) {
    std::string rest = what.substr(5), arg;
    if (const size_t colon = rest.find(':'); colon != std::string::npos) {
      arg = rest.substr(colon + 1);
      rest.resize(colon);
    }
    it.kind = "trap";
    it.trap = rest;
    if (rest == "fol_loss") it.percent = arg.empty() ? 10 : std::clamp(std::atoi(arg.c_str()), 1, 100);
    if (rest == "status") it.status = arg.empty() ? "poison" : arg;
    if ((rest == "ambush" || rest == "fol_loss" || rest == "status") &&
        (rest != "status" || it.status == "poison" || it.status == "paralysis" || it.status == "stone")) {
      it.name = rest;
      receive(it, from);
      return true;
    }
  }
  return fail(error, "unknown item '" + what + "' (an AP item id or name of the seed, a game item id, a character, or trap:KIND)");
}

size_t pending_count() {
  Runtime& r = rt();
  const size_t net = r.net_items.size() > static_cast<size_t>(r.state.received_index)
                         ? r.net_items.size() - static_cast<size_t>(r.state.received_index)
                         : 0;
  return r.pending.size() + net + r.state.characters_waiting.size() + static_cast<size_t>(r.state.ambushes);
}

namespace {

// The ambush trap: in free play on a map with random encounters (field_flags +0x25), not
// riding (script flags 12 / 14), not inside a map transfer's preload trigger: the field's own
// forced battle (state 0x21, as the camp menu starts one) on the area's encounter group
// (+0x14) and stage (+0x12). State 0x21 takes encounter -(game_state +0x264); minus the group
// makes field_prepare_encounter pick a random member of the group, as a random encounter does.
bool start_ambush() {
  field::FieldGlobals& g = field::globals();
  field::Field* f = field::field_program();
  if (f == nullptr || g.field_flags[0x25] == 0 || g.field_flags[0x26] != 0) return false;
  if (script_flag(12) || script_flag(14)) return false;
  const int tag = field::preload_get_kind2_tag();
  if (tag != -1 && field::g_script_vm != nullptr && field::trigger_contains_leader(field::g_script_vm, tag)) return false;
  const s16 group = rd16s(g.field_flags + 0x14);
  const s16 encounter = static_cast<s16>(-group);
  std::memcpy(g.game_state + 0x264, &encounter, 2);
  f->saved_control = rd16s(g.field_flags + 0x10);
  const s16 none = -1;
  std::memcpy(g.field_flags + 0x10, &none, 2);
  f->state = 0x21;
  log("ambush: the area's encounter group " + std::to_string(group) + ", stage " +
      std::to_string(rd16s(g.field_flags + 0x12)));
  return true;
}

// A received character joins through the game's own join (the library routine: ext 0xae
// party_add_member, then A 0x3f party_member_clear_bond), outside Private Actions (script
// flags 2 / 4: the party is set aside) and with a free party slot (8, members in reserve
// included) or their own reserve slot. Returns the banner, or "" while it has to wait.
std::string join_waiting(Runtime& r) {
  for (size_t k = 0; k < r.state.characters_waiting.size(); ++k) {
    const int id = r.state.characters_waiting[k];
    const char* name = character_name(id);
    auto done = [&] { r.state.characters_waiting.erase(r.state.characters_waiting.begin() + static_cast<long>(k)); };
    if (const char* hero = protagonist_refuses(id)) {
      done();
      log(std::string(name) + " cannot join in " + hero + "'s game: dropped");
      return std::string(name) + " cannot join " + hero + "'s party";
    }
    const int slot = slot_of_character(id);
    if (slot >= 0 && rd16s(party_slot(slot)) > 0) {
      done();  // (already in the party: joined at a site)
      log(std::string(name) + " is in the party already");
      continue;
    }
    if (script_flag(2) || script_flag(4)) return "";
    if (slot < 0 && !party_has_room()) {
      runtime::Runtime::get().log_once("ap-party-full", "[ap] the party is full (8); received characters wait");
      continue;
    }
    field::party_add_member(nullptr, id - 1);
    const int active = field::party_find_slot(nullptr, id - 1);
    if (active >= 0) field::vm_svc_party_member_clear_bond(active);
    done();
    log(std::string(name) + " joins the party (slot " + std::to_string(active) + ")");
    return std::string(name) + " joined the party";
  }
  return "";
}

}  // namespace

void free_play_update() {
  Runtime& r = rt();
  if (!r.on) return;
  // A received DeathLink: the party falls (the game over) in free play, or once a scene has
  // left the controls to the player for half a second (a scene's walk-around: a lost
  // scripted battle's game over starts inside a routine too; the frame a battle hands the
  // field back mid-scene does not count). Menus, rides, transitions, battles the script lets
  // the party lose and scenes holding the controls keep it waiting.
  r.control_frames = player_in_control() ? r.control_frames + 1 : 0;
  if (r.deathlink_in && in_ending(r)) {
    log("DeathLink from " + r.deathlink_source + " dropped: the ending");
    r.deathlink_in = false;
  }
  if (r.deathlink_in && (in_free_play() || r.control_frames >= 30)) {
    r.deathlink_in = false;
    r.dying_from_link = true;
    log("DeathLink from " + r.deathlink_source + ": the party falls (vblank " +
        std::to_string(runtime::Runtime::get().vblanks()) + ", script depth " +
        std::to_string(field::g_script_vm != nullptr ? field::g_script_vm->depth : -1) + ", wait " +
        std::to_string(field::g_script_vm != nullptr ? field::g_script_vm->wait_state : -1) + ")");
    field::field_begin_game_over(field::field_program());
    return;
  }
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
  keep_story_flags(r);  // (a save from before the item: its flag 112 as the item says)
  // Placeholder messages that no window showed: a banner each instead.
  if (runtime::Runtime::get().vblanks() != r.override_vblank) flush_override_lines(r);
  // One thing per banner: an ambush (once its banner has gone), a received character
  // joining, then items: local ones first (the console, boss rewards), then the server's in
  // index order (received_index: the next one this save has not had).
  if (r.notice_text.empty() && r.notices.empty()) {
    if (r.state.ambushes > 0 && start_ambush()) {
      --r.state.ambushes;
      return;
    }
    if (const std::string joined = join_waiting(r); !joined.empty()) {
      queue_notice(joined);
    } else if (!r.pending.empty()) {
      const Runtime::Pending p = r.pending.front();
      std::string note;
      if (give(p.item, &note)) {
        r.pending.pop_front();
        log("given " + describe(p.item));
        queue_notice(received_text(p.item, p.from, p.found, note));
      } else {
        runtime::Runtime::get().log_once("ap-inventory-full", "[ap] the inventory is full; received items wait");
      }
    } else if (static_cast<size_t>(r.state.received_index) < r.net_items.size()) {
      const size_t i = static_cast<size_t>(r.state.received_index);
      const ItemDef& it = r.net_items[i];
      std::string note;
      if (give(it, &note)) {
        ++r.state.received_index;
        log("given " + describe(it) + " (server item " + std::to_string(i) + ")");
        if (it.kind == "item" || it.kind == "fol" || it.kind == "character" || it.kind == "trap" || it.kind == "key")
          queue_notice(received_text(it, r.net_from[i], false, note));
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
    fresh_state(r);  // (DeathLink and the ending cleared too)
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
  r.deathlink_in = false;  // (a load starts the game again)
  r.dying_from_link = false;
  r.ending = false;
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
  const std::string from = source.empty() ? "someone" : source;
  // No game to end: the title (nothing loaded yet), the ending after the final boss, the
  // credits. Everywhere else it waits for its safe point (free_play_update, battle_frame).
  const char* drop = r.scene == Scene::kTitle ? "on the title" : r.scene == Scene::kCredits ? "in the credits"
                     : in_ending(r) ? "in the ending" : nullptr;
  if (drop != nullptr) {
    log("DeathLink from " + from + " dropped: " + drop);
    return;
  }
  r.deathlink_in = true;
  r.deathlink_source = from;
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
  if (std::find(std::begin(kFinalBoss), std::end(kFinalBoss), encounter) != std::end(kFinalBoss)) {
    // The ending and the credits follow: no DeathLink ends that.
    r.ending = true;
    if (r.deathlink_in) log("DeathLink from " + r.deathlink_source + " dropped: the final boss is beaten");
    r.deathlink_in = false;
  }
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
  // Not in a story battle the script lets the party lose (script ext 0xb7's third argument,
  // field_flags +0x7e = 0: no game over, the scene goes on): it waits for free play.
  if (const u8* block = g.handoff != nullptr ? static_cast<const u8*>(handoff_get(g.handoff, 9)) : nullptr;
      block != nullptr && block[0x180 + 0x7E] == 0)
    return;
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
  if (r.deathlink_in) log("DeathLink from " + r.deathlink_source + " dropped: the party fell anyway");
  r.deathlink_in = false;
  if (from_link || !r.seed.deathlink) return;
  if (r.link && r.net_ok) {
    log("game over: DeathLink sent");
    r.link->deathlink(r.seed.slot_name + "'s party fell");
  } else {
    log("game over: DeathLink not sent (offline)");
  }
}

// ---- Phase 3 ----

void on_program(Scene scene) {
  Runtime& r = rt();
  r.scene = scene;
  r.control_frames = 0;  // (counted again in the new program's field frames)
  if (r.on && (scene == Scene::kTitle || scene == Scene::kCredits))
    log(std::string(scene == Scene::kTitle ? "the title" : "the credits") + " at vblank " +
        std::to_string(runtime::Runtime::get().vblanks()));
  if (!r.on || (scene != Scene::kTitle && scene != Scene::kCredits)) return;
  // No game in play: a DeathLink waiting is dropped (the next one ends the next game).
  if (r.deathlink_in) log("DeathLink from " + r.deathlink_source + std::string(" dropped: ") +
                          (scene == Scene::kTitle ? "back on the title" : "the credits"));
  r.deathlink_in = false;
  r.dying_from_link = false;
}

bool on_party_add(int character, int at, int call_at) {
  Runtime& r = rt();
  if (!r.on || !r.seed.character_items) return false;
  const int id = character + 1;
  if (id < 1 || id > kCharacterCount) return false;
  const field::FieldGlobals& g = field::globals();
  const int map = g.game_state != nullptr ? rd16s(g.game_state + 0x1A) : -1;
  // The site: an inline party_add's own offset, else the call into the library's join routine.
  auto find = [&](int offset) -> const Location* {
    if (offset < 0) return nullptr;
    const auto it = r.join_sites.find(static_cast<int64_t>(map) << 32 | static_cast<u32>(offset));
    return it != r.join_sites.end() ? &r.seed.locations[it->second] : nullptr;
  };
  const Location* l = find(at);
  if (l == nullptr) l = find(call_at);
  if (l == nullptr) {
    // A site the seed does not list (a story member for a scene, a party put back together
    // after a split): the original join.
    log(std::string("join ") + character_name(id) + " at map " + std::to_string(map) + " " + std::to_string(at) +
        " (call " + std::to_string(call_at) + "): not a listed site, the game's join");
    return false;
  }
  if (!is_checked(l->id)) {
    record_check(l->id, true);
    if (l->item.own) r.pending.push_back({l->item.item, "", true});
  }
  const bool received =
      std::find(r.state.characters.begin(), r.state.characters.end(), id) != r.state.characters.end();
  if (!received) {
    log(std::string("join site ") + std::to_string(map) + ":" + std::to_string(l->offset) + ": " + character_name(id) +
        " has not been received: not added");
    return true;
  }
  // Received already: the scene's own join, when there is room (else the item keeps waiting).
  if (slot_of_character(id) >= 0 || party_has_room()) {
    auto& w = r.state.characters_waiting;
    w.erase(std::remove(w.begin(), w.end(), id), w.end());
    return false;
  }
  return true;
}

namespace {

// The story's partings for good (docs/ap/field.md "Received members and the story's leaves"):
// each is a call of the map's leave routine (pop member, pop "unequip"; with "unequip" set the
// member's accessories, slots 5 / 6, come off first; then ext 0xaf) after which no scene adds the
// member back. The site is the call (the innermost frame's return - 4), as for join sites.
struct Parting {
  int map;
  int at;
  int id;  // party id
  const char* what;
};
constexpr Parting kPartings[] = {
    {524, 0x72BC, 3, "Cross Cave, \"Decide against it\""},       // chapter 13 step 0, choice 1
    {462, 0x7030, 3, "Mars sends Celine home (flag 112 clear)"},  // chapter 15 step 2400, Claude's game
    {234, 0x6EB0, 8, "Eluria colony, \"Waiting here is better\""},  // chapter 31 step 1000, choice 0
    {479, 0x8250, 9, "Hoffmann Ruins, \"This is where we part\""},  // chapter 24 step 3000, choice 1
};
}  // namespace

void on_party_remove(int character, int at, int call_at) {
  Runtime& r = rt();
  if (!r.on || !r.seed.character_items) return;
  const int id = character + 1;
  if (id < 1 || id > kCharacterCount || !character_received(r, id)) return;
  const field::FieldGlobals& g = field::globals();
  const int map = g.game_state != nullptr ? rd16s(g.game_state + 0x1A) : -1;
  for (const Parting& p : kPartings) {
    if (p.map != map || p.id != id || (p.at != call_at && p.at != at)) continue;
    auto& w = r.state.characters_waiting;
    if (std::find(w.begin(), w.end(), id) == w.end()) w.push_back(id);
    log(std::string(character_name(id)) + " leaves at map " + std::to_string(map) + " (" + p.what +
        "): received, so they rejoin at the next free play");
    return;
  }
}

bool keeps_flag(int flag) {
  const Runtime& r = rt();
  return flag == kCelineKeptFlag && r.on && r.seed.character_items && character_received(r, kCeline);
}

bool party_has_character(int id) { return slot_of_character(id) >= 0; }

int required_protagonist() {
  const Runtime& r = rt();
  return r.on ? r.seed.protagonist : 0;
}

void scale_battle_rewards(s32* exp, s32* fol) {
  Runtime& r = rt();
  if (!r.on || (r.seed.exp_multiplier == 1 && r.seed.fol_multiplier == 1)) return;
  const s32 exp0 = *exp, fol0 = *fol;
  *exp = static_cast<s32>(std::min<s64>(static_cast<s64>(*exp) * r.seed.exp_multiplier, 999999999));
  *fol = static_cast<s32>(std::min<s64>(static_cast<s64>(*fol) * r.seed.fol_multiplier, 999999999));
  log("battle rewards: exp " + std::to_string(exp0) + " -> " + std::to_string(*exp) + ", fol " + std::to_string(fol0) +
      " -> " + std::to_string(*fol));
}

bool on_enemy_drop(int enemy_type, int item) {
  Runtime& r = rt();
  if (!r.on || !r.seed.first_drops) return true;
  const auto it = r.by_enemy.find(enemy_type);
  if (it == r.by_enemy.end()) return true;
  const Location& l = r.seed.locations[it->second];
  if (is_checked(l.id)) return true;  // (its first drop was the check: the game's drops from now on)
  record_check(l.id, true);
  if (l.item.own) r.pending.push_back({l.item.item, "", true});
  if (std::find(r.state.first_drops.begin(), r.state.first_drops.end(), enemy_type) == r.state.first_drops.end())
    r.state.first_drops.push_back(enemy_type);
  log("first drop of enemy type " + std::to_string(enemy_type) + " (item " + std::to_string(item) + "): the check instead");
  return false;
}

bool random_encounters_off() {
  const Runtime& r = rt();
  return r.on && r.seed.encounter_rate == 0;
}

s32 scale_encounter_distance(s32 distance) {
  const Runtime& r = rt();
  if (!r.on || r.seed.encounter_rate == 100 || r.seed.encounter_rate <= 0) return distance;
  return static_cast<s32>(static_cast<s64>(distance) * 100 / r.seed.encounter_rate);
}

// ---- Phase 4 ----

namespace {

const char* key_name(int key) {
  static std::string name;
  for (const auto& [id, it] : rt().seed.items) {
    if (it.kind == "key" && it.key == key) {
      name = describe(it);
      return name.c_str();
    }
  }
  name = "Key " + std::to_string(key);
  return name.c_str();
}

// A seed door that applies to the loaded script (its word checked at load).
const Door* door_at(int offset) {
  Runtime& r = rt();
  const auto it = r.doors_by_script.find(r.script_map);
  if (it == r.doors_by_script.end()) return nullptr;
  for (const size_t i : it->second) {
    const Door& d = r.seed.doors[i];
    if (d.offset == offset && r.bad_doors.count(d.id) == 0) return &d;
  }
  return nullptr;
}

bool door_locked(const Door& d) { return rt().seed.open_world && d.key != 0 && !has_key(d.key); }

void door_refused(const Door& d) {
  const char* key = key_name(d.key);
  log("door " + std::to_string(d.id) + (d.name.empty() ? "" : " (" + d.name + ")") + ": locked, needs " + key);
  queue_notice(std::string(key) + " needed");
}

}  // namespace

bool has_key(int key) {
  const Runtime& r = rt();
  return std::find(r.state.keys.begin(), r.state.keys.end(), key) != r.state.keys.end();
}

void on_script_loaded(int map, const std::vector<u8>& script) {
  Runtime& r = rt();
  if (!r.on) return;
  r.script_map = script_group(map);
  const auto it = r.doors_by_script.find(r.script_map);
  if (it == r.doors_by_script.end()) return;
  for (const size_t i : it->second) {
    const Door& d = r.seed.doors[i];
    const size_t at = 0x1C + static_cast<size_t>(d.offset);
    u32 word = 0;
    if (at + 4 <= script.size()) std::memcpy(&word, script.data() + at, 4);
    const u32 op = word >> 24;
    if (op == 0x7D || op == 0xA4 || op == 0xDA) {
      r.bad_doors.erase(d.id);
      continue;
    }
    if (r.bad_doors.insert(d.id).second) {
      char msg[160];
      std::snprintf(msg, sizeof msg, "door %lld: map %d offset %#x holds %#010x, not a map transfer; ignored",
                    static_cast<long long>(d.id), map, d.offset, word);
      log(msg);
    }
  }
}

bool door_blocked_at_trigger(int routine) {
  Runtime& r = rt();
  if (!r.on || !r.seed.open_world || routine < 0) return false;
  const auto it = r.doors_by_script.find(r.script_map);
  if (it == r.doors_by_script.end()) return false;
  const Door* locked = nullptr;
  for (const size_t i : it->second) {
    const Door& d = r.seed.doors[i];
    if (d.routine != routine || r.bad_doors.count(d.id) != 0) continue;
    if (!door_locked(d)) return false;  // (a door of this routine is open: the transfer decides)
    locked = &d;
  }
  if (locked == nullptr) return false;
  door_refused(*locked);
  return true;
}

bool on_map_transfer(int offset, s32* args) {
  Runtime& r = rt();
  if (!r.on) return false;
  const Door* d = door_at(offset);
  if (d == nullptr) return false;
  if (door_locked(*d)) {
    door_refused(*d);
    return true;
  }
  if (d->has_target) {
    char msg[160];
    std::snprintf(msg, sizeof msg, "door %lld: map %d (%d, %d, %d) -> map %d (%d, %d, %d)", static_cast<long long>(d->id),
                  args[0], args[2], args[3], args[4], d->to.map, d->to.x, d->to.y, d->to.z);
    log(msg);
    args[0] = d->to.map;
    args[2] = d->to.x;
    args[3] = d->to.y;
    args[4] = d->to.z;
    if (d->to.dir >= 0) args[5] = d->to.dir;
  }
  return false;
}

bool shop_slot_open(int shop, int index, int item) {
  const Runtime& r = rt();
  if (!r.on) return false;
  const auto it = r.by_shop_slot.find(shop << 8 | index);
  if (it == r.by_shop_slot.end()) return false;
  const Location& l = r.seed.locations[it->second];
  return !is_checked(l.id) && (l.shop_item < 0 || l.shop_item == item);
}

int on_shop_buy(int shop, int index, int item, int count) {
  Runtime& r = rt();
  if (!r.on || count <= 0 || !shop_slot_open(shop, index, item)) return count;
  const Location& l = r.seed.locations[r.by_shop_slot.at(shop << 8 | index)];
  record_check(l.id, true);  // (another player's item: "Sent X to Y")
  if (l.item.own) {
    if (give(l.item.item)) {
      queue_notice("Got " + describe(l.item.item));
    } else {
      r.pending.push_back({l.item.item, "", true});  // (the inventory is full: given later)
    }
  }
  log("shop " + std::to_string(shop) + " entry " + std::to_string(index) + " (item " + std::to_string(item) +
      "): the slot's check took the first copy of " + std::to_string(count));
  return count - 1;
}

std::string status_text() {
  Runtime& r = rt();
  if (!r.on) return "Archipelago off (no seed)";
  char buf[320];
  const char* net = !r.link ? "offline" : r.seed_mismatch ? "wrong seed" : r.net_ok ? "connected" : "connecting";
  std::snprintf(buf, sizeof buf, "seed %s | slot %s | checks %zu/%zu | received %lld | waiting %zu | %s%s%s%s",
                r.seed.seed.c_str(), r.seed.slot_name.c_str(), r.checked.size(), r.seed.locations.size(),
                static_cast<long long>(r.state.received_index), pending_count(), net,
                r.seed.deathlink ? " | DeathLink" : "", r.deathlink_in ? " (pending)" : "",
                r.state.goal_done ? " | goal" : "");
  std::string out = buf;
  const Seed& sd = r.seed;
  if (sd.character_items || sd.first_drops || sd.exp_multiplier != 1 || sd.fol_multiplier != 1 || sd.encounter_rate != 100) {
    std::snprintf(buf, sizeof buf, "\n  characters %s | first drops %s (%zu) | exp x%d | fol x%d | encounters %d%%",
                  sd.character_items ? "on" : "off", sd.first_drops ? "on" : "off", r.state.first_drops.size(),
                  sd.exp_multiplier, sd.fol_multiplier, sd.encounter_rate);
    out += buf;
  }
  if (!r.state.characters.empty()) {
    out += "\n  received characters:";
    for (const int id : r.state.characters) {
      out += std::string(" ") + character_name(id);
      if (std::find(r.state.characters_waiting.begin(), r.state.characters_waiting.end(), id) != r.state.characters_waiting.end())
        out += " (waiting)";
    }
  }
  if (r.state.ambushes > 0) out += "\n  ambushes waiting " + std::to_string(r.state.ambushes);
  if (sd.open_world || !sd.doors.empty() || sd.shuffle_shops != 0) {
    std::snprintf(buf, sizeof buf, "\n  world %s | Expel teleporter %s | doors %zu (%zu ignored) | shops %s | keys",
                  sd.open_world ? "open" : "story", sd.expel_teleporter ? "on" : "off", sd.doors.size(), r.bad_doors.size(),
                  sd.shuffle_shops == 2 ? "full" : sd.shuffle_shops == 1 ? "specials" : "off");
    out += buf;
    if (r.state.keys.empty()) out += " none";
    for (const int k : r.state.keys) out += std::string(" ") + key_name(k);
  }
  return out;
}

}  // namespace ss::game::ap
