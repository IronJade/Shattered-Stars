#include "game/ap/ap.h"
#include "game/ap/hub.h"
#include "game/ap/skills.h"

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
#include "game/boot/loader.h"
#include "game/boot/system.h"
#include "game/common/text_rules.h"
#include "game/field/field.h"
#include "game/field/load_queue.h"
#include "game/field/message.h"
#include "game/field/message_internal.h"
#include "game/field/notice.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"
#include "runtime/region.h"
#include "runtime/runtime.h"
#include "util/log.h"
#include "util/sha256.h"
#include "util/utf8.h"

namespace ss::game::ap {
namespace {

// stderr and the run's log (util/log.h: shattered-stars.log, for bug reports).
void log(const std::string& line) { util::log_line("[ap] " + line); }

// UTF-8 strings <-> paths (util/utf8.h, as launcher/config.h; ss_game does not link the launcher).
std::filesystem::path upath(const std::string& s) { return util::utf8_path(s); }
std::string ustr(const std::filesystem::path& p) { return util::path_utf8(p); }

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
  // The battle in progress is one the script lets the party lose (field_flags +0x7E = 0),
  // latched by battle_frame for battle_frame's DeathLink wait and on_battle_exit's checks.
  bool battle_lose_allowed = false;
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
  int loaded_map = -1;                   // ... and the map itself (for its script offsets)
  std::set<int64_t> bad_doors;           // seed doors whose word is not a map transfer: ignored
  std::map<int, size_t> by_shop_slot;    // (shop << 8 | index) -> shop location index
  // Port Hilton's harbour: the captain's menu while it is open (harbour_open .. harbour_choose),
  // the script's string indices it replaces, and their texts (kept while the windows read them).
  bool harbour_shown = false;
  bool transfer_again = false;           // on_map_transfer: the hub's question is up, the op runs again [hubask agent]
  std::vector<Voyage> harbour;           // the destinations offered (Not now after them)
  int harbour_question = -1, harbour_options = -1;
  std::vector<u8> harbour_question_text, harbour_options_text;
  // v2: goal.flags to set at the next field frame (New Game, a load); killer-move use counts to
  // compare at the next free play (a battle, a load); craft locations by the item they need.
  bool goal_flags_pending = false;
  bool killer_scan_pending = false;
  std::map<int, size_t> by_craft;        // item -> craft location index
  // v2 party [party agent]: the starting character becomes the Formation leader when they join.
  bool lead_start = false;
  int chosen_start_pending = 0;  // [charselect agent] New Game's pick, until on_new_game
  // The hub's New Game [hubcore agent]: the start's position, for the field's first map load.
  bool start_position = false;
  bool banners_measured = false;         // the seed doors' banners measured (first free play)
};
Runtime& rt() {
  static Runtime r;
  return r;
}

// The seed names places in a script by NA's offsets; another release may have laid the map's
// script out otherwise (runtime/region.h script_offset). The loaded script's offset 'offset'
// as NA's (-1: no NA counterpart).
int na_offset(int map, int offset) {
  if (offset < 0 || region::is_na()) return offset;
  return static_cast<int>(region::script_na_offset(map, static_cast<u32>(offset)));
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
  r.by_craft.clear();
  for (size_t i = 0; i < r.seed.locations.size(); ++i) {
    const Location& l = r.seed.locations[i];
    r.by_id[l.id] = i;
    if (l.type == "craft" && r.by_craft.count(l.craft_item) == 0) r.by_craft[l.craft_item] = i;
    if (l.type == "chest" && l.flag >= 0) r.by_flag[l.flag] = i;
    if (l.placeholder >= 0) r.by_placeholder[l.placeholder] = i;
    if (l.type == "join") r.join_sites[static_cast<int64_t>(l.map) << 32 | static_cast<u32>(l.offset)] = i;
    if (l.type == "drop") r.by_enemy[l.enemy] = i;
    if (l.type == "shop") r.by_shop_slot[l.shop << 8 | l.index] = i;
  }
  skills_index(r.seed);  // [skillsanity2]
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
  r.lead_start = false;  // (v2 party)
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
// One ASCII character of an Archipelago message as a map-font glyph: the release's own
// mapping, except the symbols France / Germany's leaves as letters (text_rules.h
// port_symbol_glyph). Bytes past ASCII (UTF-8 names) show '?'.
s32 message_glyph(char ch) {
  s32 code = static_cast<u8>(ch);
  if (code >= 0x80) code = '?';
  const s32 symbol = text::port_symbol_glyph(text::lang(), code);
  if (symbol >= 0) return symbol;
  field::text_ascii_to_glyph(&code, 0, 0);
  return code;
}

void put_glyphs(std::vector<u8>& out, const std::string& ascii) {
  for (const char ch : ascii) put_code(out, static_cast<u32>(message_glyph(ch)));
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
    const u32 g = static_cast<u32>(message_glyph(ch));
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

// The story_route's exclusions (docs/ap/field.md "Who excludes whom"): Leon joins only in
// Claude's game (script flag 1 set), Dias only in Rena's.
const char* story_route_refuses(int id) {
  const bool claude = script_flag(1);
  if (id == 8 && !claude) return "Rena";
  if (id == 5 && claude) return "Claude";
  return nullptr;
}

bool character_received(const Runtime& r, int id) {
  return std::find(r.state.characters.begin(), r.state.characters.end(), id) != r.state.characters.end();
}

// ---- v2 party (docs/ap/seed_format.md "Party v2") [party agent] ----

constexpr int kClaude = 1, kRena = 2, kDias = 5, kLeon = 8;
constexpr u16 member_bit(int id) { return static_cast<u16>(1u << (id - 1)); }

// The picked game's hero: Claude with script flag 1 (Claude's game), else Rena.
int hero_id() { return script_flag(1) ? kClaude : kRena; }

// The starting character in play: 0 without one, or when it names the picked game's hero
// (possible under story_route "any": then nobody joins and the hero leads).
int start_member(const Runtime& r) {
  const int id = r.seed.starting_character != 0 ? r.seed.starting_character : r.state.chosen_start;  // [charselect]
  return id >= 1 && id <= kCharacterCount && id != hero_id() ? id : 0;
}

// The v2 party rules are on: party_rules "open", or a starting character (open world with or
// without character_items).
bool party_v2(const Runtime& r) {
  return r.on && (r.seed.open_party || r.seed.starting_character != 0 || start_member(r) != 0);  // [charselect] a pick
}

// The story members: the hero, and under story_route "any" both heroes (the player picks the
// game at New Game, so neither can be an item).
bool story_member(const Runtime& r, int id) {
  return id == hero_id() || (r.seed.story_route == 0 && (id == kClaude || id == kRena));
}

// Recruited for good: received, or the starting character. Never refused, never overwritten,
// back after the story's partings and splits.
bool protected_member(const Runtime& r, int id) { return character_received(r, id) || id == start_member(r); }

// Who may join under party_rules "open".
bool permitted(const Runtime& r, int id) { return story_member(r, id) || protected_member(r, id); }

// The story's position: chapter G[0xa0] * 0x10000 + step G[0xa4] (steps stay below 0x10000;
// G[0xa0] is game_state +0, the same bytes).
s32 story_position() {
  const u8* gs = field::globals().game_state;
  if (gs == nullptr) return 0;
  return rd32s(gs) * 0x10000 + rd32s(gs + 4);
}

// The story's split windows (scratchpad/party/splits.md: every party leave in the 820 field
// scripts with its return): while one is on, the members it keeps away stay away (a member
// recruited for good whom the split took out does not rejoin, a newly received one waits), and
// the members it needs for a while are let in even under party_rules "open" (taken out again
// once it is over). Outside every window a member recruited for good who is in reserve rejoins
// at the next free play (the restores re-add only the members the story itself took out).
struct SplitWindow {
  int game;                                  // 0 both games, 1 Claude's (script flag 1), 2 Rena's
  int from_ch, from_step, to_ch, to_step;    // the story's position, both ends included
  int flag;                                  // >= 0: on while this script flag is set instead
  u16 away;                                  // members it keeps away (member_bit; kEveryone)
  u16 temps;                                 // members it lets in for a while
  const char* what;
};
constexpr u16 kEveryone = 0x0FFF;
constexpr SplitWindow kSplitWindows[] = {
    // Map 12 (Shingo Forest), chapter 4 step 150, Rena's game: the scene takes Rena out and
    // Claude alone fights the monster (battle_c), then swaps them back.
    {2, 4, 150, 4, 150, -1, 0, member_bit(kClaude), "Shingo Forest (map 12)"},
    {1, 5, 0, 5, 1000, -1, member_bit(kRena), 0, "Rena at home (17 -> 27)"},
    {2, 5, 2000, 5, 2500, -1, member_bit(kClaude), 0, "Claude away (18 -> 17)"},
    {0, 5, 3000, 6, 2100, -1, member_bit(kClaude) | member_bit(kRena), 0, "the other hero away (27 -> 61)"},
    {0, 7, 1000, 8, 0, -1, member_bit(kClaude) | member_bit(kRena), 0, "the other hero away (21 -> 17)"},
    {0, 15, 1000, 15, 3300, -1, kEveryone, 0, "Mars (124 / 126 / 462 -> 125 / 135)"},
    {2, 15, 2700, 19, 1500, -1, member_bit(kDias), 0, "Dias away (462 -> 434)"},
    // The tournament (maps 468-477): the party is set aside and each fight puts one fighter alone
    // into the battle (Dias's fights in both games, Claude's in Rena's game).
    {0, 19, 1000, 19, 0xFFFF, -1, kEveryone, member_bit(kDias) | member_bit(kClaude), "the tournament (468 -> 469 / 474)"},
    {2, 19, 5500, 26, 2500, -1, member_bit(kDias), 0, "Dias away (468 -> 439)"},
    {0, 22, 3000, 22, 5000, -1, kEveryone, 0, "Linga (193 -> 195)"},
    {0, 25, 3000, 30, 0xFFFF, -1, member_bit(kLeon), 0, "Leon away (422 -> 230 / 229)"},
    {0, 26, 1000, 26, 3000, -1, kEveryone, 0, "the Front Line (444 -> 447)"},
    {0, 29, 0, 31, 0, -1, kEveryone, 0, "the ship to El (548 -> 229 / 230 / 232)"},
    {1, 33, 0, 33, 10, -1, kEveryone, 0, "Eluria Tower (567)"},
    {0, 53, 3000, 53, 4000, -1, kEveryone, 0, "the Heraldry Weapons Laboratory (398 -> 401)"},
    {0, 0, 0, 0, 0, 586, kEveryone, 0, "the Fun City arena (flag 586)"},
    {0, 0, 0, 0, 0, 2318, kEveryone, 0, "the Cave of Trials (flag 2318)"},
    {0, 0, 0, 0, 0, 2326, kEveryone, 0, "Fun City's contest (flag 2326)"},
};

bool window_on(const SplitWindow& w) {
  if (w.game == 1 && !script_flag(1)) return false;
  if (w.game == 2 && script_flag(1)) return false;
  if (w.flag >= 0) return script_flag(w.flag);
  const s32 at = story_position();
  return at >= w.from_ch * 0x10000 + w.from_step && at <= w.to_ch * 0x10000 + w.to_step;
}

// The window on now that keeps member 'id' away (0: one that keeps everyone away), or null.
const SplitWindow* window_keeping(int id) {
  for (const SplitWindow& w : kSplitWindows)
    if ((id == 0 ? w.away == kEveryone : (w.away & member_bit(id)) != 0) && window_on(w)) return &w;
  return nullptr;
}
// The window on now that lets member 'id' in for a while, or null.
const SplitWindow* window_needing(int id) {
  for (const SplitWindow& w : kSplitWindows)
    if ((w.temps & member_bit(id)) != 0 && window_on(w)) return &w;
  return nullptr;
}

// A member slot the port never overwrites: a story member's or one recruited for good.
bool keeps_slot(const Runtime& r, int id) { return permitted(r, id); }

bool in_list(const std::vector<int>& v, int id) { return std::find(v.begin(), v.end(), id) != v.end(); }
void drop_from(std::vector<int>& v, int id) { v.erase(std::remove(v.begin(), v.end(), id), v.end()); }

void invalidate_voice(int slot) {
  if (slot < 0 || slot >= 4) return;
  field::vm_svc_voice_slot_invalidate(slot);
  field::vm_svc_load_queue_set_streaming(0);
  field::vm_svc_set_voice_cache_state(0);
}

// The party for the log: "Claude, Rena, (Celine)" (a member in reserve in brackets).
std::string party_line() {
  std::string out;
  for (int i = 0; i < 8; ++i) {
    const u8* p = party_slot(i);
    if (p == nullptr) break;
    const s16 v = rd16s(p);
    if (v == 0) continue;
    if (!out.empty()) out += ", ";
    out += v > 0 ? std::string(character_name(v)) : "(" + std::string(character_name(-v)) + ")";
  }
  return out.empty() ? "nobody" : out;
}

// With every slot taken: a reserve slot the port may give a new member, the first one of a
// member it does not keep (the original overwrites the first reserve slot); -1: none.
int free_reserve_slot(const Runtime& r) {
  for (int i = 0; i < 8; ++i) {
    const u8* p = party_slot(i);
    if (p == nullptr) return -1;
    const s16 v = rd16s(p);
    if (v < 0 && !keeps_slot(r, -v)) return i;
  }
  return -1;
}

// Puts member 'id' (not in the party) in slot 'slot' as the game's join does there.
void join_in_slot(int slot, int id) {
  field::vm_svc_party_slot_init(slot, id);
  invalidate_voice(slot);
}

// The member whose data a stash holds.
int stash_member(const SaveState::Stash& s) {
  if (s.bytes.size() < 2) return 0;
  const s16 v = rd16s(s.bytes.data());
  return v < 0 ? -v : v;
}

// A temporary member with no slot left (a full party of members the port keeps) borrows the
// first reserve slot; its member's slot and record are stashed (and saved in the sidecar) until
// restore_stash puts them back.
bool borrow_slot(Runtime& r, int id) {
  field::FieldGlobals& g = field::globals();
  if (g.records == nullptr) return false;
  for (int i = 0; i < 8; ++i) {
    u8* p = party_slot(i);
    if (p == nullptr || rd16s(p) >= 0) continue;
    SaveState::Stash s;
    s.slot = i;
    s.bytes.assign(p, p + 0x60);
    s.bytes.insert(s.bytes.end(), g.records + i * 0xD0, g.records + (i + 1) * 0xD0);
    log(std::string(character_name(id)) + " borrows slot " + std::to_string(i) + " of " + character_name(stash_member(s)) +
        " (in reserve; put back when the window ends)");
    r.state.stash.push_back(std::move(s));
    join_in_slot(i, id);
    return true;
  }
  return false;
}

// Puts stash 'k' back in its slot; the temporary member there (in reserve or active) is dropped
// from the party: it was only lent the place.
void restore_stash(Runtime& r, size_t k) {
  field::FieldGlobals& g = field::globals();
  const SaveState::Stash s = r.state.stash[k];
  r.state.stash.erase(r.state.stash.begin() + static_cast<long>(k));
  u8* p = party_slot(s.slot);
  if (p == nullptr || g.records == nullptr || s.bytes.size() != 0x60 + 0xD0) return;
  const s16 v = rd16s(p);
  const int lent = v < 0 ? -v : v;
  std::memcpy(p, s.bytes.data(), 0x60);
  std::memcpy(g.records + s.slot * 0xD0, s.bytes.data() + 0x60, 0xD0);
  invalidate_voice(s.slot);
  drop_from(r.state.temporary, lent);
  log(std::string(character_name(stash_member(s))) + " back in slot " + std::to_string(s.slot) +
      (lent != 0 ? std::string(" (") + character_name(lent) + " leaves it)" : std::string()));
}

// The stash holding member 'id', put back before anything joins them.
void restore_stash_of(Runtime& r, int id) {
  for (size_t k = 0; k < r.state.stash.size(); ++k)
    if (stash_member(r.state.stash[k]) == id) {
      restore_stash(r, k);
      return;
    }
}

// The party is set aside for the hero alone: a Private Action (script flags 2 / 4), or the map
// library's isolate routine (B 0x29, then flag 746 until its restore routine, B 0x2a, clears it).
constexpr int kIsolatedFlag = 746;
bool party_isolated() { return script_flag(2) || script_flag(4) || script_flag(kIsolatedFlag); }

// The free-play part of the v2 rules (not while the party is set aside for the hero alone): a
// stash whose temporary member is out of the party goes back; temporary members who joined for
// good meanwhile stay; once no window needs a temporary member it leaves the party; a member
// recruited for good who is in reserve rejoins once no window keeps them away (through
// join_waiting, one banner each).
void party_v2_update(Runtime& r) {
  if (!party_v2(r) || party_isolated()) return;
  for (size_t k = 0; k < r.state.stash.size();) {
    const u8* p = party_slot(r.state.stash[k].slot);
    if (p != nullptr && rd16s(p) > 0 && in_list(r.state.temporary, rd16s(p)) && window_needing(rd16s(p)) != nullptr) {
      ++k;
      continue;
    }
    restore_stash(r, k);
  }
  for (size_t k = 0; k < r.state.temporary.size();) {
    const int id = r.state.temporary[k];
    if (protected_member(r, id) || (r.seed.open_party && story_member(r, id))) {
      log(std::string(character_name(id)) + " stays: joined for good meanwhile");
      r.state.temporary.erase(r.state.temporary.begin() + static_cast<long>(k));
      continue;
    }
    if (window_needing(id) != nullptr) {
      ++k;
      continue;
    }
    r.state.temporary.erase(r.state.temporary.begin() + static_cast<long>(k));
    const int slot = slot_of_character(id);
    if (slot < 0) continue;
    // Out of the party for good: the slot is free again (the original leaves its temporary
    // members in reserve, where the next join overwrites them).
    u8* p = party_slot(slot);
    const s16 none = 0;
    std::memcpy(p, &none, 2);
    invalidate_voice(slot);
    log(std::string(character_name(id)) + " leaves the party: the window that needed them is over (" + party_line() + ")");
  }
  for (int id = 1; id <= kCharacterCount; ++id) {
    const int slot = slot_of_character(id);
    if (slot < 0 || rd16s(party_slot(slot)) > 0 || !protected_member(r, id) || in_list(r.state.characters_waiting, id)) continue;
    if (const SplitWindow* w = window_keeping(id)) {
      runtime::Runtime::get().log_once("ap-split-" + std::to_string(id),
                                       std::string("[ap] ") + character_name(id) + " is away for " + w->what);
      continue;
    }
    r.state.characters_waiting.push_back(id);
    log(std::string(character_name(id)) + " is in reserve and no split keeps them away: rejoins");
  }
}

// A received Celine travels with the party: script flag 112, which map 131 stores from
// party_has(Celine) as chapter 15 begins and Mars (462 / 125 / 126) reads (keeps_flag). So does
// a starting Celine.
constexpr int kCelineKeptFlag = 112;
constexpr int kCeline = 3;
void keep_story_flags(const Runtime& r) {
  field::FieldGlobals& g = field::globals();
  if ((!r.seed.character_items && !party_v2(r)) || g.script_flags == nullptr || !protected_member(r, kCeline)) return;
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
  if (it.kind == "skill") {  // skillsanity [skillsanity2]: the party may learn it
    receive_skill(it.skill);
    return true;
  }
  if (it.kind == "move") {  // skillsanity: the member knows it (now, or from their join)
    receive_move(it.character, it.move_index);
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
  j.set("version", Json::number(static_cast<int64_t>(2)));  // 2: save_hash [hubcore agent]
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
  j.set("goal_won", ints(s.goal_won));  // v2
  // v2 party [party agent]: temporary members, and the slots they borrowed (written only when
  // there are any: a sidecar without them reads as empty).
  if (!s.temporary.empty()) j.set("temporary", ints(s.temporary));
  if (!s.stash.empty()) {
    Json a = Json::array();
    for (const SaveState::Stash& st : s.stash) {
      static const char* const kHex = "0123456789abcdef";
      std::string hex;
      for (const u8 b : st.bytes) {
        hex += kHex[b >> 4];
        hex += kHex[b & 15];
      }
      Json e = Json::object();
      e.set("slot", Json::number(static_cast<int64_t>(st.slot)));
      e.set("bytes", Json::string(hex));
      a.push(e);
    }
    j.set("stash", a);
  }
  if (!s.save_hash.empty()) j.set("save_hash", Json::string(s.save_hash));  // version 2
  if (s.hub_view >= 0) j.set("hub_view", Json::number(static_cast<int64_t>(s.hub_view)));  // [hubask agent]
  if (s.chosen_start > 0) j.set("starting_character", Json::number(static_cast<int64_t>(s.chosen_start)));  // [charselect]
  if (!s.skills.empty()) j.set("skills", ints(s.skills));  // skillsanity [skillsanity2]
  if (!s.moves.empty()) j.set("moves", ints(s.moves));
  return j;
}

bool sidecar_from_json(const Json& j, SaveState* s, std::string* error) {
  if (j["format"].as_string() != "apso2-save") return fail(error, "not an apso2 save sidecar");
  const int version = get_int_or(j["version"], 0);
  if (version != 1 && version != 2) return fail(error, "unsupported sidecar version");
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
  ints("goal_won", &r.goal_won);  // v2 (absent in older sidecars: empty)
  ints("temporary", &r.temporary);  // v2 party
  for (size_t i = 0; i < j["stash"].size(); ++i) {
    const Json& e = j["stash"][i];
    SaveState::Stash st;
    st.slot = static_cast<int>(e["slot"].as_int(-1));
    const std::string& hex = e["bytes"].as_string();
    auto nib = [](char c) { return c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'; };
    for (size_t k = 0; k + 1 < hex.size(); k += 2) st.bytes.push_back(static_cast<u8>(nib(hex[k]) << 4 | nib(hex[k + 1])));
    if (st.slot < 0 || st.slot > 7 || st.bytes.size() != 0x60 + 0xD0) return fail(error, "stash: a slot 0-7 and 0x130 bytes expected");
    r.stash.push_back(std::move(st));
  }
  r.save_hash = j["save_hash"].as_string();  // version 2 (absent: empty, and a seed refuses the load)
  r.hub_view = static_cast<int>(j["hub_view"].as_int(-1));  // [hubask agent] (absent: none)
  r.chosen_start = static_cast<int>(j["starting_character"].as_int(0));  // [charselect] (absent: none)
  if (r.chosen_start < 0 || r.chosen_start > kCharacterCount) r.chosen_start = 0;
  ints("skills", &r.skills);  // skillsanity [skillsanity2] (absent: none)
  ints("moves", &r.moves);
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
  if (r.seed.open_world) {
    // Port Hilton's harbour (docs/ap/seed_format.md "Phase 4", "The harbour"). The Hilton sailor
    // (map 169, actor 17, routine 0x714c) is the only way off the Lacour continent: entering
    // Lacour at chapter 22 step 9999 stores chapter 23, and he refused the passage at chapters 23
    // and 24, so the keys of stage 9 the fill may put on the Cross continent were out of reach
    // (a soft-lock); he no longer refuses it then, nor at chapter 18 for a Tournament Pass. His
    // choice becomes the captain's menu (port op A 0x70: the option count; A 0x71: the voyage
    // chosen): Cross, Hoffmann Ruins, El, Not now. The Hoffmann ship's soldier (actor 24)
    // stands there at chapters 23-59, not 23-27; he and Herlie's sailor (map 144, routine
    // 0x7298) check the landing's key before the voyage (A 0x72). NA words; other releases'
    // through region::script_offset, each line skipped if the disc's word differs.
    for (const char* line : {
             "script 169 0x1c65 0x00000012 0x000003e7  # Hilton sailor: no Tournament Pass refusal (chapter 18)",
             "script 169 0x1c79 0x00000017 0x000003e7  # ... nor the chapter 23 one (Lacour's black day)",
             "script 169 0x1c7c 0x00000018 0x000003e7  # ... nor chapter 24's",
             "script 169 0x1c99 0x12000002 0xfe700000  # his choice: the captain's menu (port op A 0x70, the count)",
             "script 169 0x1ca6 0x19800004 0x0b800004  # push L[4] (the option chosen) ...",
             "script 169 0x1ca7 0x00000000 0xfef10000  # ... port op A 0x71: the voyage",
             "script 169 0x2647 0x0000001b 0x0000003b  # the Hoffmann ship's soldier (actor 24): chapters 23-59",
             "script 169 0x1f23 0x0000001b 0x0000003b  # ... his 'ruins again' choice until chapter 59",
             "script 169 0x1f3b 0x19800000 0x0b800000  # ... push L[0] (yes: 0) ...",
             "script 169 0x1f3c 0x00000000 0xfef20016  # ... port op A 0x72: the Map 231 landing's key (flag 22)",
             "script 144 0x1cdc 0x19800000 0x0b800000  # Herlie's sailor: push L[0] (yes: 0) ...",
             "script 144 0x1cdd 0x00000000 0xfef20012  # ... port op A 0x72: Port Hilton's landing key (flag 18)",
         })
      r.seed.patches.push_back(line);
  }
  r.on = true;
  index_seed(r);
  fresh_state(r);
  r.goal_flags_pending = true;   // (a start straight into the field, --map, has no New Game)
  r.killer_scan_pending = true;
  r.notices.clear();
  r.notice_text.clear();
  r.banners_measured = false;
  hub_start(&r.seed);  // the hub's chapter lock and story views [hubvm agent]
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
  hub_start(nullptr);
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

// [skillsanity2] A check the game reached with no item push of its own (a learn point, a guild
// purchase): our own item comes at the next free play ("Got X"), another player's is "Sent".
void deliver_check(int64_t id) {
  Runtime& r = rt();
  if (!r.on || is_checked(id)) return;
  const Location* l = find_location(id);
  if (l == nullptr || !record_check(id, true)) return;
  if (l->item.own) r.pending.push_back({l->item.item, "", true});
}

SaveState& mutable_state() { return rt().state; }

void on_flag_set(int flag) {
  Runtime& r = rt();
  if (!r.on) return;
  if (flag >= 700 && flag <= 711) on_guild_flag(flag);  // skillsanity: a guild set bought [skillsanity2]
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
//
// v2 [party agent]: no lockouts (party_rules "open", and for the starting character); a member
// a split window keeps away waits for its end; with every slot taken, a reserve slot of a member
// the port does not keep (a story member for a while) is used, never a kept member's. The
// starting character becomes the Formation leader (config +0x41) as they join.
std::string join_waiting(Runtime& r) {
  // The Formation leader: config +0x41, an active slot of the front four (menu_combat.cpp).
  auto lead = [&](int id) {
    if (!r.lead_start || id != start_member(r)) return;
    r.lead_start = false;
    const int slot = field::party_find_slot(nullptr, id - 1);
    if (slot >= 0 && slot < 4 && field::globals().config != nullptr) {
      field::globals().config[0x41] = static_cast<u8>(slot);
      log(std::string(character_name(id)) + " leads (Formation leader, slot " + std::to_string(slot) + ")");
    }
  };
  for (size_t k = 0; k < r.state.characters_waiting.size(); ++k) {
    const int id = r.state.characters_waiting[k];
    const char* name = character_name(id);
    auto done = [&] { r.state.characters_waiting.erase(r.state.characters_waiting.begin() + static_cast<long>(k)); };
    const bool no_lockouts = r.seed.open_party || id == start_member(r);
    if (const char* hero = no_lockouts ? nullptr : story_route_refuses(id)) {
      done();
      log(std::string(name) + " cannot join in " + hero + "'s game: dropped");
      return std::string(name) + " cannot join " + hero + "'s party";
    }
    if (party_v2(r)) restore_stash_of(r, id);
    const int slot = slot_of_character(id);
    if (slot >= 0 && rd16s(party_slot(slot)) > 0) {
      done();  // (already in the party: joined at a site)
      log(std::string(name) + " is in the party already");
      lead(id);
      continue;
    }
    if (script_flag(2) || script_flag(4)) return "";
    if (party_v2(r) && party_isolated()) return "";
    if (party_v2(r)) {
      // A member the story set aside (in reserve) waits for the end of every window keeping them
      // away; a new one only while the party is split (a window keeping everyone away).
      if (const SplitWindow* w = window_keeping(slot >= 0 ? id : 0)) {
        runtime::Runtime::get().log_once("ap-wait-" + std::to_string(id),
                                         std::string("[ap] ") + name + " waits: " + w->what + " keeps the party apart");
        continue;
      }
    }
    int into = -1;  // (v2, a full party: a reserve slot of a member the port does not keep)
    if (slot < 0 && !party_has_room()) {
      into = party_v2(r) ? free_reserve_slot(r) : -1;
      if (into < 0) {
        runtime::Runtime::get().log_once("ap-party-full", "[ap] the party is full (8); received characters wait");
        continue;
      }
    }
    if (into >= 0) {
      log(std::string(name) + " takes the reserve slot " + std::to_string(into) + " of " +
          character_name(-rd16s(party_slot(into))) + " (not kept)");
      join_in_slot(into, id);
    } else {
      field::party_add_member(nullptr, id - 1);
    }
    const int active = field::party_find_slot(nullptr, id - 1);
    if (active >= 0) field::vm_svc_party_member_clear_bond(active);
    done();
    log(std::string(name) + " joins the party (slot " + std::to_string(active) + ")" +
        (party_v2(r) ? ": " + party_line() : std::string()));
    lead(id);
    return std::string(name) + " joined the party";
  }
  return "";
}

void measure_door_banners(Runtime& r);  // (below, with the doors)

// New Game's first free play: the starting character (v2) and the seed's starting items, Fol and
// flags, once per game (an ordinary save loaded without a sidecar counts as given).
void give_start(Runtime& r) {
  if (r.state.start_given) return;
  r.state.start_given = true;
  // v2 [party agent]: the starting character joins first (their banner before anything the
  // starting items bring) and leads; naming the picked game's hero, nobody joins.
  if (const int sc = start_member(r); sc != 0) {
    drop_from(r.state.characters_waiting, sc);
    r.state.characters_waiting.insert(r.state.characters_waiting.begin(), sc);
    r.lead_start = true;
    log(std::string("starting character ") + character_name(sc) + ": joins at the first free play and leads");
  } else if (r.seed.starting_character != 0) {
    log(std::string("starting character ") + character_name(r.seed.starting_character) +
        " is this game's hero: nobody joins, the hero leads");
  }
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
  skills_new_game();  // skillsanity: the heroes' moves [skillsanity2]
}

}  // namespace

void free_play_update() {
  Runtime& r = rt();
  if (!r.on) return;
  // v2: goal.flags (a goal gate's preset flags, such as Clik's Filia meeting 445 / 446), at the
  // first field frame after New Game or a load, the loaded game's flags in place by then; every
  // load, a save without a sidecar included (start.flags would miss that one).
  if (r.goal_flags_pending && field::globals().script_flags != nullptr) {
    r.goal_flags_pending = false;
    u8* flags = field::globals().script_flags;
    for (const int flag : r.seed.goal_flags) flags[flag >> 3] = static_cast<u8>(flags[flag >> 3] | 1 << (flag & 7));
    if (!r.seed.goal_flags.empty()) log("goal flags set (" + std::to_string(r.seed.goal_flags.size()) + ")");
  }
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
  hub_free_play();  // the hub: a story view whose boss was won ends [hubvm agent]
  measure_door_banners(r);  // (once per seed: the font is loaded by now)
  give_start(r);
  keep_story_flags(r);  // (a save from before the item: its flag 112 as the item says)
  party_v2_update(r);   // v2: split windows, temporary members, members back after a split
  if (r.killer_scan_pending) {  // v2: after a battle or a load, the killer moves' use counts
    r.killer_scan_pending = false;
    scan_killer_moves();
  }
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
  // [charselect agent] the member picked at New Game's character select (0: nobody extra).
  r.state.chosen_start = r.seed.starting_choices.empty() ? 0 : r.chosen_start_pending;
  r.chosen_start_pending = 0;
  if (r.state.chosen_start != 0)
    log(std::string("starting character ") + character_name(r.state.chosen_start) + ": picked at New Game");
  hub_reset();  // (a story view left on)
  r.goal_flags_pending = true;  // v2: goal.flags in the new game's first field frame
  r.start_position = false;
  log("new game: a fresh state for seed " + r.seed.seed);
}

void on_new_game_state(u8* block, bool debug_start) {
  Runtime& r = rt();
  const HubState& h = r.seed.hub_state;
  if (!r.on || !r.seed.hub || !h.on || block == nullptr) return;
  const char* hub_state_env = std::getenv("SS_HUB_STATE");
  if (debug_start && hub_state_env == nullptr) {
    log("hub: a debug start (--map): the hub's New Game state not applied (SS_HUB_STATE=1 applies it there)");
    return;
  }
  u8* gs = block + 0x220;     // game_state (script G[0xa0] ..)
  u8* flags = block + 0x8C0;  // the script flags (0x170 bytes)
  field::FieldGlobals& g = field::globals();
  // The original New Game picked the hero (config +0x45: 0 Claude, 1 Rena) and stored its route
  // flag (1, Claude's game) and bytes (+0x24 / +0x25); the names and the party are in place.
  // A debug start is always Claude's branch; SS_HUB_STATE=rena builds Rena's game there (the
  // hub proof's sweeps; the party is the test's business) [hubcore agent].
  const bool claude = debug_start && hub_state_env != nullptr && std::string(hub_state_env) == "rena"
                          ? false
                          : g.config == nullptr || g.config[0x45] == 0;
  auto set_flag = [&](int n) { flags[n >> 3] = static_cast<u8>(flags[n >> 3] | 1 << (n & 7)); };
  auto put_words = [&](const std::vector<HubWord>& words) {
    for (const HubWord& w : words) std::memcpy(gs + (w.addr - 0xA0), &w.value, 4);
  };
  std::memset(flags, 0, 0x170);
  for (const int n : h.flags) set_flag(n);
  put_words(h.globals);
  const HubGame& game = h.games[claude ? 0 : 1];
  for (const int n : game.flags) set_flag(n);
  put_words(game.globals);
  // The route flag stays New Game's (the hero picked): set in Claude's game, clear in Rena's.
  flags[0] = static_cast<u8>(claude ? flags[0] | 2 : flags[0] & ~2);
  const s32 chapter = h.chapter, step = h.step;
  std::memcpy(gs, &chapter, 4);
  std::memcpy(gs + 4, &step, 4);
  if (g.config != nullptr) g.config[0x4C] = static_cast<u8>(h.disc - 1);
  bool inserted = true;
  if (g.loader != nullptr) inserted = game::loader_insert_disc(*g.loader, h.disc);
  put_words(h.start_globals);
  std::string where = "the debug start's map";
  if (!debug_start) {
    // The transfer to the start: the field's first map load places the leader there, facing
    // 'dir' (degrees, as a map transfer's; game_state +0x18 holds 4096ths of a turn).
    const s16 map = static_cast<s16>(h.start.map);
    const s32 pos[3] = {h.start.x, h.start.y, h.start.z};
    const s16 facing = static_cast<s16>(((h.start.dir % 360 + 360) % 360) * 4096 / 360);
    std::memcpy(gs + 0x1A, &map, 2);
    std::memcpy(gs + 8, pos, 12);
    std::memcpy(gs + 0x18, &facing, 2);
    r.start_position = true;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s (map %d at %d, %d, %d, facing %d)", h.start_name.empty() ? "the start" : h.start_name.c_str(),
                  h.start.map, h.start.x, h.start.y, h.start.z, h.start.dir);
    where = buf;
  }
  log(std::string("hub New Game: ") + (claude ? "Claude" : "Rena") + "'s game, " + std::to_string(h.flags.size()) + " flags, " +
      std::to_string(h.globals.size()) + " globals (+" + std::to_string(game.flags.size()) + " / " +
      std::to_string(game.globals.size()) + " the hero's, " + std::to_string(h.start_globals.size()) + " the start's), chapter " +
      std::to_string(chapter) + " step " + std::to_string(step) + ", disc " + std::to_string(h.disc) +
      (inserted ? "" : " (not available yet)") + "; " + where);
}

bool take_start_position() {
  Runtime& r = rt();
  const bool was = r.start_position;
  r.start_position = false;
  return was;
}

bool start_position_pending() { return rt().start_position; }

namespace {
// goal.all: every encounter in goal_won. Sets the goal (and sends it) the first time.
void goal_all_update(Runtime& r) {
  if (r.seed.goal_all.empty() || r.state.goal_done) return;
  for (const int enc : r.seed.goal_all)
    if (std::find(r.state.goal_won.begin(), r.state.goal_won.end(), enc) == r.state.goal_won.end()) return;
  r.state.goal_done = true;
  log("goal: every encounter of goal.all won");
  if (r.link && r.net_ok) r.link->goal();
}
}  // namespace

void on_saved(const std::string& card_image, int save_index, const u8* file, size_t length) {
  Runtime& r = rt();
  if (!r.on || card_image.empty()) return;
  const std::string path = sidecar_path(card_image, save_index);
  std::string err;
  SaveState s = r.state;
  s.save_hash = file != nullptr ? util::save_digest(file, length) : std::string();  // [hubcore agent]
  s.hub_view = hub_asked_view();  // [hubask agent] an asked chamber's view on now
  if (write_sidecar(path, s, &err)) {
    log("saved " + path);
  } else {
    log("could not save the sidecar: " + err);
  }
}

void on_loaded(const std::string& card_image, int save_index) {
  Runtime& r = rt();
  if (!r.on) return;
  // v2: goal.flags again after every load (sidecar or not); the use counts the save brings.
  r.goal_flags_pending = true;
  hub_reset();  // the hub: a load starts with any story view off [hubvm agent]
  r.killer_scan_pending = true;
  if (card_image.empty()) return;
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
    // v2: goal.all's wins too (the save's, then this session's).
    for (const int enc : r.state.goal_won)
      if (std::find(s.goal_won.begin(), s.goal_won.end(), enc) == s.goal_won.end()) s.goal_won.push_back(enc);
  }
  if (s.seed == r.seed.seed && r.state.goal_done) s.goal_done = true;
  s.save_hash.clear();  // (the card's, not the running state's)
  hub_resume_view(s.hub_view);  // [hubask agent] the asked chamber's view the save was made in
  s.hub_view = -1;
  r.state = s;
  goal_all_update(r);  // (the merge may complete goal.all)
  r.deathlink_in = false;  // (a load starts the game again)
  r.dying_from_link = false;
  r.ending = false;
  r.checked = std::set<int64_t>(s.checked.begin(), s.checked.end());
  // Local items still waiting stay queued; the server's items past the save's received
  // index are given again (this session keeps the server's whole list).
  log("loaded " + path + ": " + std::to_string(s.checked.size()) + " checks, received index " +
      std::to_string(s.received_index));
}

bool load_allowed(const std::string& card_image, int save_index, const u8* file, size_t length) {
  const Runtime& r = rt();
  if (!r.on) return true;
  const std::string path = card_image.empty() ? std::string() : sidecar_path(card_image, save_index);
  std::string why;
  SaveState s;
  std::string err;
  if (path.empty() || !read_sidecar(path, &s, &err)) {
    why = "this save isn't from this Archipelago seed (no Archipelago state beside it: " + (path.empty() ? "no card file" : err) + ")";
  } else if (s.seed != r.seed.seed || s.slot_name != r.seed.slot_name) {
    why = "this save isn't from this Archipelago seed (its state is for seed " + s.seed + ", slot " + s.slot_name + ")";
  } else if (s.save_hash.empty()) {
    why = "this save was changed outside the game (its Archipelago state holds no hash of it)";
  } else if (file == nullptr || util::save_digest(file, length) != s.save_hash) {
    why = "this save was changed outside the game (it differs from the one this seed saved)";
  }
  if (why.empty()) return true;
  if (const char* any = std::getenv("SS_AP_ANY_SAVE"); any != nullptr && std::string(any) == "1") {
    log("save " + std::to_string(save_index + 1) + ": " + why + "; loaded anyway (SS_AP_ANY_SAVE=1)");
    return true;
  }
  log("load refused, save " + std::to_string(save_index + 1) + (card_image.empty() ? "" : " of " + card_image) + ": " + why);
  return false;
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

namespace {

// Whether the battle running is one its script lets the party lose: script ext 0xb7's third
// argument, kept in field_flags +0x7E (the state block, handoff 9, +0x180); random and menu
// battles set it to 1 (field state 5). 0: the loss is no game over, the scene goes on.
// -1: no state block (a --battle start, or gone).
int read_battle_lose_allowed() {
  const battle::BattleGlobals& g = battle::bg();
  const u8* block = g.handoff != nullptr ? static_cast<const u8*>(handoff_get(g.handoff, 9)) : nullptr;
  return block == nullptr ? -1 : block[0x180 + 0x7E] == 0 ? 1 : 0;
}

// The battle outcomes (battle.h Outcome; config +0x178) that win a boss location. A victory
// (0) always; a time-up (4) too, which only ext 0x72's rule 3 fights end in and only with a
// member standing (a wipe ends the fight first): the story's timed survivals, Shin at the
// Lacour Front Line (516) and Berle in Eluria Tower (520), where the enemy can't be beaten and
// lasting the minute is the win. A wipe (2) only in a fight the script lets the party lose
// (Dias at the tournament 204, Shin on the ship 517, Marsilio in Fienal 530): the scripted
// defeat, after which the story goes on. Never an escape (1) or the escape race lost (3).
bool outcome_takes_boss_check(int outcome, bool lose_allowed) {
  return outcome == battle::kVictory || outcome == battle::kTimeUp || (outcome == battle::kDefeat && lose_allowed);
}

}  // namespace

void note_battle_lose_allowed(bool lose_allowed) { rt().battle_lose_allowed = lose_allowed; }

void on_battle_exit(int encounter, int outcome) {
  Runtime& r = rt();
  // The flag latched in the fight (the state block may be gone by now); this battle's only.
  const bool lose_allowed = r.battle_lose_allowed;
  r.battle_lose_allowed = false;
  if (!r.on) return;
  if (lose_allowed) {
    // No game over follows a fight the script lets the party lose, so nothing else would
    // clear this; a DeathLink never fells the party there anyway (battle_frame waits).
    r.dying_from_link = false;
  }
  r.killer_scan_pending = true;  // v2: the fight may have raised a killer move's use count
  const bool victory = outcome == battle::kVictory;
  // The goal: a victory in one of goal.encounters, or (v2) in every one of goal.all.
  const bool all = !r.seed.goal_all.empty();
  const std::vector<int>& goal_list = all ? r.seed.goal_all : r.seed.goal_encounters;
  const bool goal = std::find(goal_list.begin(), goal_list.end(), encounter) != goal_list.end();
  const bool boss = std::any_of(r.seed.locations.begin(), r.seed.locations.end(), [&](const Location& l) {
    return l.type == "boss" && l.encounter == encounter;
  });
  if (boss || goal)
    log("encounter " + std::to_string(encounter) + " ended: outcome " + std::to_string(outcome) +
        (lose_allowed ? ", the script lets the party lose" : ", a loss is a game over"));
  if (victory && std::find(std::begin(kFinalBoss), std::end(kFinalBoss), encounter) != std::end(kFinalBoss)) {
    // The ending and the credits follow: no DeathLink ends that.
    r.ending = true;
    if (r.deathlink_in) log("DeathLink from " + r.deathlink_source + " dropped: the final boss is beaten");
    r.deathlink_in = false;
  }
  if (outcome_takes_boss_check(outcome, lose_allowed)) {
    for (const Location& l : r.seed.locations) {
      if (l.type != "boss" || l.encounter != encounter || is_checked(l.id)) continue;
      record_check(l.id, true);
      if (l.item.own) r.pending.push_back({l.item.item, "", true});
    }
  }
  // The goal needs the victory. goal.all: each victory is kept (goal_won; also one a door's
  // `won` names), the goal comes with the last one.
  const bool door_won = std::any_of(r.seed.doors.begin(), r.seed.doors.end(), [&](const Door& d) {
    return std::find(d.won.begin(), d.won.end(), encounter) != d.won.end();
  });
  // The hub [hubvm agent]: a chamber's boss won goes in goal_won too (the boss is beaten: its
  // story view no longer comes; "Defeat X first" doors read it); the view on ends at the next
  // free play.
  const bool hub_boss = hub_boss_encounter(encounter);
  if (victory) hub_battle_won(encounter);
  if (victory && ((all && goal) || door_won || hub_boss) &&
      std::find(r.state.goal_won.begin(), r.state.goal_won.end(), encounter) == r.state.goal_won.end()) {
    r.state.goal_won.push_back(encounter);
    log("goal: encounter " + std::to_string(encounter) + " won (goal_won " + std::to_string(r.state.goal_won.size()) +
        ", goal.all " + std::to_string(r.seed.goal_all.size()) + ")");
  }
  if (all) {
    if (victory && goal) goal_all_update(r);
  } else if (victory && goal && !r.state.goal_done) {
    r.state.goal_done = true;
    log("goal: encounter " + std::to_string(encounter) + " won");
    if (r.link && r.net_ok) r.link->goal();
  }
}

void battle_frame() {
  Runtime& r = rt();
  if (!r.on) return;
  // The lose-allowed flag, for the wait below and for on_battle_exit's boss checks: latched
  // while the state block is there (the exit reads the latched value).
  if (const int lose = read_battle_lose_allowed(); lose >= 0) r.battle_lose_allowed = lose != 0;
  if (!r.deathlink_in) return;
  battle::BattleGlobals& g = battle::bg();
  // In the fight, between special moves: every standing member falls, as from a lethal hit
  // (HP 0, death 1); the battle's own check then loses the fight (outcome 2).
  if (g.state != battle::kStateFighting || (g.flags & 0x40) != 0) return;
  // Not in a story battle the script lets the party lose (script ext 0xb7's third argument,
  // field_flags +0x7e = 0: no game over, the scene goes on): it waits for free play.
  if (r.battle_lose_allowed) return;
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
  if (scene == Scene::kBattle) {
    r.battle_lose_allowed = false;  // (battle_frame latches it)
    if (r.on) log("a battle at vblank " + std::to_string(runtime::Runtime::get().vblanks()));
  }
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
  if (!r.on || (!r.seed.character_items && !party_v2(r))) return false;
  const int id = character + 1;
  if (id < 1 || id > kCharacterCount) return false;
  const field::FieldGlobals& g = field::globals();
  const int map = g.game_state != nullptr ? rd16s(g.game_state + 0x1A) : -1;
  // The site: an inline party_add's own offset, else the call into the library's join routine.
  auto find = [&](int at_offset) -> const Location* {
    const int offset = na_offset(map, at_offset);
    if (offset < 0) return nullptr;
    const auto it = r.join_sites.find(static_cast<int64_t>(map) << 32 | static_cast<u32>(offset));
    return it != r.join_sites.end() ? &r.seed.locations[it->second] : nullptr;
  };
  const Location* l = find(at);
  if (l == nullptr) l = find(call_at);
  const s32 pos = story_position();
  const std::string where = "map " + std::to_string(map) + " " + std::to_string(at) + " (call " + std::to_string(call_at) +
                            ", chapter " + std::to_string(pos >> 16) + " step " + std::to_string(pos & 0xFFFF) +
                            (script_flag(1) ? ", Claude's game" : ", Rena's game") + ")";
  if (l != nullptr && !is_checked(l->id)) {
    record_check(l->id, true);
    if (l->item.own) r.pending.push_back({l->item.item, "", true});
  }
  if (!party_v2(r)) {
    // Phase 3 (party_rules "story", no starting character).
    if (l == nullptr) {
      // A site the seed does not list (a story member for a scene, a party put back together
      // after a split): the original join.
      log(std::string("join ") + character_name(id) + " at " + where + ": not a listed site, the game's join");
      return false;
    }
    if (!character_received(r, id)) {
      log(std::string("join site ") + std::to_string(map) + ":" + std::to_string(l->offset) + ": " + character_name(id) +
          " has not been received: not added");
      return true;
    }
    // Received already: the scene's own join, when there is room (else the item keeps waiting).
    if (slot_of_character(id) >= 0 || party_has_room()) {
      drop_from(r.state.characters_waiting, id);
      log("join site " + std::to_string(map) + ":" + std::to_string(l->offset) + ": " + character_name(id) +
          " was received: the game's join");
      return false;
    }
    return true;
  }
  // v2 [party agent] (docs/ap/seed_format.md "Party v2"). Who may join: under party_rules "open"
  // the story members, received members and the starting character; under "story" (a starting
  // character without character_items) everyone, except that a listed site adds a received
  // member only (phase 3).
  restore_stash_of(r, id);
  const bool allowed = r.seed.open_party ? permitted(r, id) : (l == nullptr || protected_member(r, id));
  if (!allowed) {
    const bool active = slot_of_character(id) >= 0 && rd16s(party_slot(slot_of_character(id))) > 0;
    if (active) return true;  // (a temporary member, in the party already: nothing to do)
    if (r.seed.open_party) {
      if (const SplitWindow* w = window_needing(id)) {
        // The story needs them for a while (a fight they make alone): let in, out again later.
        // With every slot kept they borrow one (its member's data stashed until the window ends).
        if (slot_of_character(id) >= 0 || party_has_room()) {
          field::party_add_member(nullptr, id - 1);
        } else if (const int into = free_reserve_slot(r); into >= 0) {
          join_in_slot(into, id);
        } else if (!borrow_slot(r, id)) {
          log(std::string(character_name(id)) + " is needed for " + w->what + " but there is no slot: not added");
          return true;
        }
        if (!in_list(r.state.temporary, id)) r.state.temporary.push_back(id);
        log(std::string("join ") + character_name(id) + " at " + where + ": for a while (" + w->what + "): " + party_line());
        return true;
      }
    }
    log(std::string("join ") + character_name(id) + " at " + where + (l != nullptr ? " (a listed site)" : "") + ": " +
        (r.seed.open_party ? "neither a story member, received nor the starting character" : "not received") +
        ": not added");
    return true;
  }
  if (protected_member(r, id)) drop_from(r.state.characters_waiting, id);
  if (slot_of_character(id) >= 0 || party_has_room()) {
    log(std::string("join ") + character_name(id) + " at " + where + ": the game's join");
    return false;
  }
  // Every slot taken: a reserve slot of a member the port does not keep, never a kept one's.
  if (const int into = free_reserve_slot(r); into >= 0) {
    log(std::string("join ") + character_name(id) + " at " + where + ": takes the reserve slot " + std::to_string(into) +
        " of " + character_name(-rd16s(party_slot(into))));
    join_in_slot(into, id);
    return true;
  }
  log(std::string("join ") + character_name(id) + " at " + where + ": no free slot (a kept member's would be overwritten): skipped");
  if (protected_member(r, id) && !in_list(r.state.characters_waiting, id)) r.state.characters_waiting.push_back(id);
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
    // v2 [party agent]: members who join in either game now (party_rules "open").
    {234, 0x74C8, 8, "Eluria colony, Leon stays (Rena's game)"},  // chapter 31 step 1000, Rena's game
    {417, 0x7DB8, 11, "Cave of the Red Crystal, \"you are needed here\""},  // chapter <= 40 step 1000, choice 1
};
}  // namespace

void on_party_remove(int character, int at, int call_at) {
  Runtime& r = rt();
  if (!r.on || (!r.seed.character_items && !party_v2(r))) return;
  const int id = character + 1;
  if (id < 1 || id > kCharacterCount || !protected_member(r, id)) return;
  const int slot = slot_of_character(id);
  if (slot < 0 || rd16s(party_slot(slot)) <= 0) return;  // not active: nothing is removed
  const field::FieldGlobals& g = field::globals();
  const int map = g.game_state != nullptr ? rd16s(g.game_state + 0x1A) : -1;
  auto& w = r.state.characters_waiting;
  const bool waiting = std::find(w.begin(), w.end(), id) != w.end();
  for (const Parting& p : kPartings) {
    if (p.map != map || p.id != id || (p.at != call_at && p.at != at)) continue;
    if (!waiting) w.push_back(id);
    log(std::string(character_name(id)) + " leaves at map " + std::to_string(map) + " (" + p.what +
        "): received, so they rejoin at the next free play");
    return;
  }
  if (waiting) {
    w.erase(std::remove(w.begin(), w.end(), id), w.end());
    log(std::string(character_name(id)) + " leaves at map " + std::to_string(map) +
        " for a while: the story brings them back (their join no longer waits)");
  } else if (party_v2(r)) {
    log(std::string(character_name(id)) + " leaves at map " + std::to_string(map) + " (call " + std::to_string(call_at) +
        ") for a while");
  }
}

bool keeps_flag(int flag) {
  const Runtime& r = rt();
  if (flag != kCelineKeptFlag || !r.on || (!r.seed.character_items && !party_v2(r)) || !protected_member(r, kCeline))
    return false;
  log("script flag 112 kept set: Celine was received (she travels with the party)");
  return true;
}

int starting_member() { return rt().on ? start_member(rt()) : 0; }

// [charselect agent] New Game's character select.
int seed_starting_character() { return rt().on ? rt().seed.starting_character : 0; }
std::vector<int> starting_choices() { return rt().on ? rt().seed.starting_choices : std::vector<int>{}; }
void set_chosen_start(int id) { rt().chosen_start_pending = id >= 1 && id <= kCharacterCount ? id : 0; }

std::string split_window_keeping(int id) {
  const SplitWindow* w = party_v2(rt()) ? window_keeping(id) : nullptr;
  return w != nullptr ? w->what : "";
}

std::string split_window_needing(int id) {
  const SplitWindow* w = party_v2(rt()) ? window_needing(id) : nullptr;
  return w != nullptr ? w->what : "";
}

std::string party_free_play_step() {
  Runtime& r = rt();
  if (!r.on) return "";
  give_start(r);
  party_v2_update(r);
  return join_waiting(r);
}

bool party_has_character(int id) { return slot_of_character(id) >= 0; }

int required_story_route() {
  const Runtime& r = rt();
  return r.on ? r.seed.story_route : 0;
}

int seed_difficulty() { return rt().on ? rt().seed.difficulty : -1; }  // [difficulty agent]

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

// A seed door that applies to the loaded script (its word checked at load); 'offset' is NA's.
const Door* door_at(int offset) {
  Runtime& r = rt();
  if (offset < 0) return nullptr;
  const auto it = r.doors_by_script.find(r.script_map);
  if (it == r.doors_by_script.end()) return nullptr;
  for (const size_t i : it->second) {
    const Door& d = r.seed.doors[i];
    if (d.offset == offset && r.bad_doors.count(d.id) == 0) return &d;
  }
  return nullptr;
}

// The story's chapter (script global G[0xa0], game_state +0; 0 without a game state).
int story_chapter() {
  const u8* g = field::globals().game_state;
  return g == nullptr ? 0 : static_cast<int>(rd32s(g));
}

// The keys of a door the player lacks (open world; the hub's dungeon keys [hubcore agent]): its
// `key`, or every one of its `keys` (a one-way story point such as the El voyage needs them all);
// none once the chapter reaches the door's `until` (an exit kept shut only while the story is
// inside the area).
std::vector<int> missing_keys(const Door& d) {
  std::vector<int> out;
  if (!rt().seed.open_world && !rt().seed.hub) return out;
  if (d.until > 0 && story_chapter() >= d.until) return out;
  for (const int k : d.keys)
    if (!has_key(k)) out.push_back(k);
  return out;
}

// v2: a door's first condition not met, -1 when all are: its flags (script flags set) first,
// then its won (encounters in goal_won), in the banners' order. Any world mode.
int missing_condition(const Door& d) {
  const Runtime& r = rt();
  for (size_t i = 0; i < d.flags.size(); ++i)
    if (!script_flag(d.flags[i])) return static_cast<int>(i);
  for (size_t i = 0; i < d.won.size(); ++i)
    if (std::find(r.state.goal_won.begin(), r.state.goal_won.end(), d.won[i]) == r.state.goal_won.end())
      return static_cast<int>(d.flags.size() + i);
  return -1;
}

bool door_locked(const Door& d) { return !missing_keys(d).empty() || missing_condition(d) >= 0; }

// A banner text's width in the map font (notice_show's measure; 0 before the font is loaded).
// The banner's box is at most 0xf8 pixels, the text 0xc less (notice.cpp).
constexpr int kBannerTextMax = 0xF8 - 0xC;
int banner_text_width(const std::string& ascii) {
  const u8* font = field::msg_font_ref();
  if (font == nullptr) return 0;
  int width = 0;
  for (const char ch : ascii) {
    const s32 g = message_glyph(ch);
    if (g >= 0 && g < 0x4000) width += 1 + field::msg_glyph_width(static_cast<u32>(g));
  }
  return width;
}

// The banner names each missing key ("Salva Pass needed", "Eluria Pass needed to sail"), one
// at a time, with the door's purpose (or 'purpose', when given) when it fits the banner; a key's
// notice already waiting is not queued again.
void door_refused(const Door& d, const std::string& purpose_override = "") {
  const std::string& purpose = purpose_override.empty() ? d.purpose : purpose_override;
  std::string names;
  for (const int k : missing_keys(d)) {
    const std::string key = key_name(k);
    names += (names.empty() ? "" : ", ") + key;
    std::string text = key + " needed" + (purpose.empty() ? "" : " " + purpose);
    if (!purpose.empty() && banner_text_width(text) > kBannerTextMax) text = key + " needed";
    auto& q = rt().notices;
    if (std::find(q.begin(), q.end(), text) == q.end()) queue_notice(text);
  }
  // v2: the first condition not met, with its banner (the seed's text; a plain one without).
  if (const int c = missing_condition(d); c >= 0) {
    const size_t i = static_cast<size_t>(c);
    const bool flag = i < d.flags.size();
    const std::string what = flag ? "flag " + std::to_string(d.flags[i]) : "encounter " + std::to_string(d.won[i - d.flags.size()]) + " won";
    const std::string text = i < d.banners.size() && !d.banners[i].empty() ? d.banners[i] : "The way is closed for now";
    names += (names.empty() ? "" : ", ") + what + " (\"" + text + "\")";
    if (const int w = banner_text_width(text); w > kBannerTextMax)
      log("door " + std::to_string(d.id) + ": banner \"" + text + "\" is " + std::to_string(w) + " px, over the banner's " +
          std::to_string(kBannerTextMax));
    auto& q = rt().notices;
    if (std::find(q.begin(), q.end(), text) == q.end()) queue_notice(text);
  }
  log("door " + std::to_string(d.id) + (d.name.empty() ? "" : " (" + d.name + ")") + ": locked, needs " + names);
}

// Every banner the seed's doors can show (each key's "X needed", with the purpose when it fits;
// each condition's text), measured once the map font is loaded (the first free play): one over
// the banner's width is logged; with SS_TRACE every width is [hubcore agent].
void measure_door_banners(Runtime& r) {
  if (r.banners_measured || field::msg_font_ref() == nullptr) return;
  r.banners_measured = true;
  std::set<std::string> seen;
  int over = 0;
  auto measure = [&](const Door& d, const std::string& text) {
    if (text.empty() || !seen.insert(text).second) return;
    const int w = banner_text_width(text);
    runtime::Runtime::get().trace(("ap banner \"" + text + "\" " + std::to_string(w) + " px (door " + std::to_string(d.id) + ")").c_str());
    if (w > kBannerTextMax) {
      ++over;
      log("door " + std::to_string(d.id) + ": banner \"" + text + "\" is " + std::to_string(w) + " px, over the banner's " +
          std::to_string(kBannerTextMax));
    }
  };
  for (const Door& d : r.seed.doors) {
    for (const int k : d.keys) {
      const std::string key = key_name(k);
      const std::string with_purpose = key + " needed" + (d.purpose.empty() ? "" : " " + d.purpose);
      measure(d, banner_text_width(with_purpose) > kBannerTextMax ? key + " needed" : with_purpose);
    }
    for (const std::string& b : d.banners) measure(d, b);
  }
  if (!seen.empty()) log("door banners measured: " + std::to_string(seen.size()) + ", " + std::to_string(over) + " too wide");
}

}  // namespace

bool has_key(int key) {
  const Runtime& r = rt();
  return std::find(r.state.keys.begin(), r.state.keys.end(), key) != r.state.keys.end();
}

void on_script_loaded(int map, const std::vector<u8>& script) {
  Runtime& r = rt();
  if (!r.on) return;
  hub_map_loaded(map);  // the hub: a chamber's story view starts, or the one on ends [hubvm agent]
  hub_script_loaded(map, script);  // ... and the extra options of its `ask` rows [hubask agent]
  r.harbour_shown = false;  // (a menu left open by a transfer)
  r.script_map = script_group(map);
  r.loaded_map = map;
  const auto it = r.doors_by_script.find(r.script_map);
  if (it == r.doors_by_script.end()) return;
  for (const size_t i : it->second) {
    const Door& d = r.seed.doors[i];
    const s64 code_at = region::script_offset(map, static_cast<u32>(d.offset));
    const size_t at = 0x1C + static_cast<size_t>(code_at < 0 ? 0 : code_at);
    u32 word = 0;
    if (code_at >= 0 && at + 4 <= script.size()) std::memcpy(&word, script.data() + at, 4);
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

bool door_blocked_at_trigger(int routine_at) {
  Runtime& r = rt();
  // (Keys lock only in open world and the hub, v2 conditions in every mode: door_locked knows.)
  if (!r.on || routine_at < 0) return false;
  const int routine = na_offset(r.loaded_map, routine_at);
  if (routine < 0) return false;
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
  r.transfer_again = false;
  if (!r.on) return false;
  if (hub_map_transfer(args)) return false;  // the hub: a won story view's cut_to [hubvm agent]
  const int na = na_offset(r.loaded_map, offset);
  // [hubask agent] The hub's question at this transfer is up: its phases run in the op's reruns
  // (the door table ran when it came up).
  const int64_t id = na < 0 ? -1 : door_id(r.script_map, na);
  if (hub_ask_busy(id)) {
    r.transfer_again = hub_ask_transfer(id, args, args);
    return r.transfer_again;
  }
  s32 own[6];
  std::memcpy(own, args, sizeof own);
  if (const Door* d = door_at(na); d != nullptr) {
    if (door_locked(*d)) {
      door_refused(*d);
      return true;
    }
    if (d->has_target && d->to_while_unbeaten >= 0 && goal_won(d->to_while_unbeaten)) {
      // [hubcore agent] to.while_unbeaten: the redirect is off once that boss is won.
      log("door " + std::to_string(d->id) + ": encounter " + std::to_string(d->to_while_unbeaten) +
          " won, the door's own target (map " + std::to_string(args[0]) + ")");
    } else if (d->has_target) {
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
  }
  // [hubask agent] Into a chamber that asks first (its `ask`): the question, then this op again.
  r.transfer_again = hub_ask_transfer(id, own, args);
  return r.transfer_again;
}

bool transfer_again() { return rt().transfer_again; }

// ---- Port Hilton's harbour ----

namespace {

constexpr int64_t kElVoyageDoor = 29106503;  // room 444's transfer to Lacour Castle (0x851c): the El gate
constexpr int kHoffmannSail = 0x7CF8;         // map 169 routine 0x79c0: the soldier's sail (no locals)

// The story's step (script global G[0xa4], game_state +4).
int story_step() {
  const u8* g = field::globals().game_state;
  return g == nullptr ? 0 : static_cast<int>(rd32s(g + 4));
}

const Door* find_door(int64_t id) {
  for (const Door& d : rt().seed.doors)
    if (d.id == id) return &d;
  return nullptr;
}

// The story is at the El voyage: chapter 27 step 7000 (stored in meeting room 448; room 444's
// trigger fires then), on disc 1 (never after the disc switch).
bool el_voyage_due() {
  const u8* config = field::globals().config;
  const bool disc1 = config == nullptr || config[0x4C] == 0;
  return disc1 && story_chapter() == 27 && story_step() == 7000;
}

// A map-font text's code at 'p' (one byte below 0x80, else two) and its size with the control
// codes' parameters (tools/so2_script.py CONTROL).
u32 text_code(const u8* p, size_t* size) {
  u32 code = p[0];
  size_t n = 1;
  if (code >= 0x80) {
    code = (code & 0x7F) | static_cast<u32>(p[1]) << 7;
    n = 2;
  }
  switch (code) {
    case 0x4003: case 0x4004: case 0x4007: case 0x4008: case 0x400C: case 0x400D: case 0x400E: case 0x400F: n += 1; break;
    case 0x4006: case 0x4009: n += 2; break;
    default: break;
  }
  *size = n;
  return code;
}

// The captain's question in place of the sailor's ("Sailor{1}The passage to Cross? It'll be
// {number_var:00} FOL."): the script's own speaker label (the release's: up to the label's end,
// code 1, or a new line and its indent), then "Where to, matey? / Cross is N FOL." (G[0x38],
// the fare the routine has just worked out, as the script's own {number_var:00}).
std::vector<u8> harbour_question(const u8* original) {
  std::vector<u8> out;
  if (original != nullptr) {
    const u32 space = static_cast<u32>(message_glyph(' '));
    size_t at = 0, n = 0;
    for (int i = 0; i < 32; ++i) {
      const u32 c = text_code(original + at, &n);
      if (c == 0 || c == 0x4002) break;
      if (c == 1 || c == 0x4000) {
        at += n;
        while (text_code(original + at, &n) == space) at += n;  // (the line's indent)
        out.assign(original, original + at);
        break;
      }
      at += n;
    }
  }
  put_glyphs(out, "Where to, matey?");
  put_code(out, 0x4000);
  put_glyphs(out, "Cross is ");
  put_code(out, 0x400E);
  out.push_back(0);
  put_glyphs(out, " FOL.");
  out.push_back(0);
  return out;
}

// The menu in place of the sailor's "Cross / Don't cross": each line begins with the script's
// own lead-in (the codes both of its lines begin with: the cursor's room), then the destination.
std::vector<u8> harbour_options(const u8* original, const std::vector<Voyage>& menu) {
  std::vector<u8> lead;
  if (original != nullptr) {
    // The original's lines (up to 0x4000 / 0x4002 / the end), as code byte ranges.
    std::vector<std::pair<size_t, size_t>> lines;
    size_t at = 0, start = 0, n = 0;
    for (int i = 0; i < 200; ++i) {
      const u32 c = text_code(original + at, &n);
      if (c == 0 || c == 0x4000 || c == 0x4002) {
        lines.push_back({start, at});
        if (c != 0x4000) break;
        start = at + n;
      }
      at += n;
    }
    if (lines.size() >= 2) {
      size_t a = lines[0].first, b = lines[1].first;
      for (int i = 0; i < 8 && a < lines[0].second && b < lines[1].second; ++i) {
        size_t na = 0, nb = 0;
        const u32 ca = text_code(original + a, &na);
        if (ca != text_code(original + b, &nb) || na != nb) break;
        a += na;
        b += nb;
      }
      lead.assign(original + lines[0].first, original + a);
    }
  }
  std::vector<u8> out;
  std::vector<Voyage> all = menu;
  all.push_back(Voyage::kNotNow);
  for (size_t i = 0; i < all.size(); ++i) {
    if (i > 0) put_code(out, 0x4000);
    out.insert(out.end(), lead.begin(), lead.end());
    put_glyphs(out, voyage_name(all[i]));
  }
  put_code(out, 0x4002);
  out.push_back(0);
  return out;
}

std::string menu_text(const std::vector<Voyage>& menu) {
  std::string s;
  for (const Voyage v : menu) s += (s.empty() ? "" : ", ") + std::string(voyage_name(v));
  return s + ", Not now";
}

}  // namespace

const char* voyage_name(Voyage v) {
  switch (v) {
    case Voyage::kCross: return "Cross";
    case Voyage::kHoffmann: return "Hoffmann Ruins";
    case Voyage::kEl: return "El";
    default: return "Not now";
  }
}

std::vector<Voyage> harbour_menu() {
  std::vector<Voyage> menu{Voyage::kCross};
  const Runtime& r = rt();
  if (!r.on || !r.seed.open_world) return menu;
  const int chapter = story_chapter();
  if (chapter >= 23 && chapter <= 59) menu.push_back(Voyage::kHoffmann);
  if (el_voyage_due()) menu.push_back(Voyage::kEl);
  return menu;
}

int harbour_open(int question, int options) {
  Runtime& r = rt();
  r.harbour = harbour_menu();
  r.harbour_shown = r.harbour.size() > 1;
  r.harbour_question = question;
  r.harbour_options = options;
  if (r.on) {
    char at[48];
    std::snprintf(at, sizeof at, " (chapter %d step %d)", story_chapter(), story_step());
    log("harbour: " + menu_text(r.harbour) + at + (r.harbour_shown ? "" : ": the sailor's own Cross / Don't cross"));
  }
  return static_cast<int>(r.harbour.size()) + 1;
}

const u8* script_text(int index, const u8* original) {
  Runtime& r = rt();
  if (const u8* hub = r.on ? hub_script_text(index, original) : nullptr) return hub;  // [hubask agent] an extra option
  if (!r.on || !r.harbour_shown || index < 0) return nullptr;
  if (index == r.harbour_question) {
    r.harbour_question_text = harbour_question(original);
    return r.harbour_question_text.data();
  }
  if (index == r.harbour_options) {
    r.harbour_options_text = harbour_options(original, r.harbour);
    return r.harbour_options_text.data();
  }
  return nullptr;
}

HarbourChoice harbour_choose(int choice) {
  Runtime& r = rt();
  HarbourChoice out;
  if (!r.on || !r.seed.open_world) {
    // (Not an open-world seed: the sailor's own test, option 0 = Cross.)
    if (choice == 0) out.kind = HarbourChoice::kCross;
    return out;
  }
  const std::vector<Voyage> menu = r.harbour_shown ? r.harbour : harbour_menu();
  r.harbour_shown = false;
  const Voyage v = choice >= 0 && choice < static_cast<int>(menu.size()) ? menu[static_cast<size_t>(choice)] : Voyage::kNotNow;
  log("harbour: option " + std::to_string(choice) + ", " + voyage_name(v));
  switch (v) {
    case Voyage::kCross:
      if (voyage_landing_open(17)) out.kind = HarbourChoice::kCross;  // (his fare and voyage)
      break;
    case Voyage::kHoffmann:
      // The Hoffmann soldier's own sail: at chapter 23 it stores 24, as the story's first trip.
      if (r.script_map != 169) break;
      if (voyage_landing_open(22)) {
        const s64 at = region::script_offset(r.loaded_map, kHoffmannSail);
        if (at < 0) {
          log("harbour: this release's map 169 has no Hoffmann sail at NA 0x7cf8");
          break;
        }
        out.kind = HarbourChoice::kJump;
        out.offset = static_cast<int>(at);
      }
      break;
    case Voyage::kEl: {
      // Every key of the El voyage's gate first, before anything is stored (a chapter 28 left
      // behind would start the voyage at the next visit of Port Hilton or Lacour Castle).
      if (!el_voyage_due()) break;
      const Door* d = find_door(kElVoyageDoor);
      if (d != nullptr && door_locked(*d)) {
        door_refused(*d);
        break;
      }
      out.kind = HarbourChoice::kEl;
      break;
    }
    default: break;
  }
  return out;
}

void el_voyage_transfer(s32 args[7]) {
  u8* g = field::globals().game_state;
  if (g != nullptr) {
    const s32 chapter = 28, step = 0;
    std::memcpy(g, &chapter, 4);
    std::memcpy(g + 4, &step, 4);
  }
  const s32 room444[7] = {422, 130, -215, 0, 0, 180, 128};  // map_transfer_c at 444 0x851c
  std::memcpy(args, room444, sizeof room444);
  log("harbour: the El voyage (chapter 28 step 0, to Lacour Castle as from the Front Line)");
}

bool voyage_landing_open(int flag) {
  Runtime& r = rt();
  if (!r.on || !r.seed.open_world) return true;
  // The world-map script's landing transfers (909, routine 0x57d8), docs/ap/doors.json.
  const int64_t id = flag == 17   ? door_id(909, 0x5CF8)   // Hilton -> Herlie
                     : flag == 18 ? door_id(909, 0x5AD0)   // Herlie -> Port Hilton
                     : flag == 21 ? door_id(909, 0x6120)   // map 231 -> Port Hilton
                     : flag == 22 ? door_id(909, 0x5F08)   // Port Hilton -> map 231
                                  : -1;
  const Door* d = find_door(id);
  if (d == nullptr || !door_locked(*d)) return true;
  door_refused(*d, "to sail");
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

bool on_shop_buy(int shop, int index, int item, int count) {
  Runtime& r = rt();
  if (!r.on || count <= 0 || !shop_slot_open(shop, index, item)) return false;
  const Location& l = r.seed.locations[r.by_shop_slot.at(shop << 8 | index)];
  record_check(l.id, true);  // (another player's item: "Sent X to Y")
  if (l.item.own) {
    if (give(l.item.item)) {
      queue_notice("Got " + describe(l.item.item));
    } else {
      r.pending.push_back({l.item.item, "", true});  // (the inventory is full: given later)
      log("the inventory is full; " + describe(l.item.item) + " waits");
    }
  }
  log("shop " + std::to_string(shop) + " entry " + std::to_string(index) + " (item " + std::to_string(item) +
      "): " + std::to_string(count) + " bought, and the slot's check");
  return true;
}

bool order_overflow(int item, int count) {
  Runtime& r = rt();
  if (!r.on || item <= 0 || count <= 0) return false;
  int fit = 0;
  while (fit < count && field::inventory_add(field::globals().inventory, item, 1, 1) != 0) ++fit;
  ItemDef it;
  it.kind = "item";
  it.game_id = item;
  for (const auto& [id, def] : r.seed.items)
    if (def.kind == "item" && def.game_id == item) it.name = def.name;
  for (int i = fit; i < count; ++i) r.pending.push_back({it, "", true});  // ("Got X" once there is room)
  log("courier: " + std::to_string(count) + " x item " + std::to_string(item) + " ordered, " + std::to_string(fit) +
      " fit; " + std::to_string(count - fit) + " wait");
  return true;
}

// ---- v2: goals, crafting, killer moves ----

namespace {
constexpr int kSilverTrumpet = 48;   // composes song 7 or 8 (menu_specialty.cpp ComposeList)
constexpr int kEvilMelodyFlag = 53;  // song 7 learnt: script flag 0x2f + 6
constexpr int kIseriaQueen = 561;    // song 7 at Musical level 10 (callways_song_summon)
}  // namespace

bool goal_won(int encounter) {
  const Runtime& r = rt();
  return std::find(r.state.goal_won.begin(), r.state.goal_won.end(), encounter) != r.state.goal_won.end();
}

bool sell_locked(int item) {
  const Runtime& r = rt();
  if (!r.on || item != kSilverTrumpet || script_flag(kEvilMelodyFlag)) return false;
  if (std::find(r.seed.goal_all.begin(), r.seed.goal_all.end(), kIseriaQueen) == r.seed.goal_all.end()) return false;
  log("the Silver Trumpet stays off the sell list (The Evil Melody, flag 53, not composed yet)");
  return true;
}

void on_craft(int item, bool success) {
  Runtime& r = rt();
  if (!r.on || item <= 0) return;
  const auto it = r.by_craft.find(item);
  if (it == r.by_craft.end()) return;
  const Location& l = r.seed.locations[it->second];
  if (is_checked(l.id)) return;
  log("craft: item " + std::to_string(item) + " made (" + (success ? "success" : "failure") + ")");
  record_check(l.id, true);  // (another player's item: "Sent X to Y")
  if (l.item.own) r.pending.push_back({l.item.item, "", true});  // ("Got X" in free play)
}

bool no_fail_crafting() { return rt().on && rt().seed.no_fail_crafting; }

int scan_killer_moves() {
  Runtime& r = rt();
  const field::FieldGlobals& g = field::globals();
  if (!r.on || g.records == nullptr || g.party == nullptr) return 0;
  int n = 0;
  for (const Location& l : r.seed.locations) {
    if (l.type != "killer_move" || is_checked(l.id)) continue;
    const int slot = slot_of_character(l.character);
    if (slot < 0) continue;  // (not in the party: the record is someone else's)
    // The member's record (8 x 0xd0, by party slot): the move's use count, +1 each time the
    // move starts (battle pc_begin_action 0x80037098, store 0x800373b4), at most 999.
    const int count = rd16s(g.records + slot * 0xD0 + 0x8A + 2 * l.move);
    if (count < l.uses) continue;
    log(std::string("killer move: ") + character_name(l.character) + " move " + std::to_string(l.move) + " used " +
        std::to_string(count) + " times (level " + std::to_string(l.level) + " at " + std::to_string(l.uses) + ")");
    record_check(l.id, true);
    if (l.item.own) r.pending.push_back({l.item.item, "", true});
    ++n;
  }
  return n;
}

std::vector<std::string> waiting_notices() { return {rt().notices.begin(), rt().notices.end()}; }

std::vector<u8> glyph_text(const std::string& ascii) {
  std::vector<u8> out;
  put_glyphs(out, ascii);
  return out;
}

int glyph_text_width(const std::string& ascii) { return banner_text_width(ascii); }

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
                  sd.open_world ? "open" : sd.hub ? "hub" : "story", sd.expel_teleporter ? "on" : "off", sd.doors.size(),
                  r.bad_doors.size(),
                  sd.shuffle_shops == 2 ? "full" : sd.shuffle_shops == 1 ? "specials" : "off");
    out += buf;
    if (r.state.keys.empty()) out += " none";
    for (const int k : r.state.keys) out += std::string(" ") + key_name(k);
  }
  if (!sd.goal_all.empty() || !r.state.goal_won.empty()) {  // v2
    out += "\n  goal.all";
    for (const int e : sd.goal_all) out += " " + std::to_string(e);
    out += " | won";
    if (r.state.goal_won.empty()) out += " none";
    for (const int e : r.state.goal_won) out += " " + std::to_string(e);
  }
  if (const std::string h = hub_status(); !h.empty()) out += "\n  " + h;  // [hubvm agent]
  if (party_v2(r)) {  // v2 party [party agent]
    out += std::string("\n  party rules ") + (sd.open_party ? "open" : "story") + " | starting " +
           (sd.starting_character != 0 ? character_name(sd.starting_character) : "nobody") + " | party " + party_line();
    if (const SplitWindow* w = window_keeping(0)) out += std::string(" | split: ") + w->what;
    for (const int id : r.state.temporary) out += std::string(" | ") + character_name(id) + " for a while";
    if (!r.state.stash.empty()) out += " | borrowed slots " + std::to_string(r.state.stash.size());
  }
  return out;
}

}  // namespace ss::game::ap
