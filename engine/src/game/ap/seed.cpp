#include "game/ap/seed.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace ss::game::ap {
namespace {

std::filesystem::path upath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

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

// A character as a party id (1-12) or a name ("Celine").
int parse_character(const Json& j) {
  if (j.is_string()) {
    if (const int by_name = character_by_name(j.as_string()); by_name != 0) return by_name;
  }
  int64_t v = 0;
  return get_int(j, &v) && v >= 1 && v <= kCharacterCount ? static_cast<int>(v) : 0;
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
  } else if (it.kind == "character") {
    it.character = parse_character(j["character"]);
    if (it.character == 0) return fail(error, where + ": character 1-12 or a name expected");
    if (it.name.empty()) it.name = character_name(it.character);
  } else if (it.kind == "trap") {
    it.trap = j["trap"].as_string();
    if (it.trap == "fol_loss") {
      it.percent = j.has("percent") ? get_int_or(j["percent"], -1) : 10;
      if (it.percent < 1 || it.percent > 100) return fail(error, where + ": percent 1-100 expected");
    } else if (it.trap == "status") {
      it.status = j.has("status") ? j["status"].as_string() : "poison";
      if (it.status != "poison" && it.status != "paralysis" && it.status != "stone")
        return fail(error, where + ": status poison, paralysis or stone expected");
    } else if (it.trap != "ambush") {
      return fail(error, where + ": trap ambush, fol_loss or status expected");
    }
    if (it.name.empty()) it.name = it.trap == "ambush" ? "Ambush" : it.trap == "fol_loss" ? "Fol loss" : "Status ailment";
  } else if (it.kind == "key") {
    it.key = get_int_or(j["key"], 0);
    if (it.key < 1 || it.key > 255) return fail(error, where + ": key 1-255 expected");
    if (it.name.empty()) it.name = "Key " + std::to_string(it.key);
  } else if (it.kind != "foreign") {
    return fail(error, where + ": unknown kind '" + it.kind + "'");
  }
  *out = it;
  return true;
}

// An option given as a number in a range (a missing one keeps the default).
bool option_int(const Json& options, const char* key, int lo, int hi, int* out, std::string* error) {
  if (!options.has(key)) return true;
  const Json& v = options[key];
  int64_t n = 0;
  if (v.type() == Json::Type::Bool) n = v.as_bool() ? 1 : 0;
  else if (!get_int(v, &n)) n = INT64_MIN;
  if (n < lo || n > hi)
    return fail(error, std::string("options.") + key + ": " + std::to_string(lo) + "-" + std::to_string(hi) + " expected");
  *out = static_cast<int>(n);
  return true;
}

// A field position {map, x, y, z, dir?} (dir: degrees, -1 when missing).
bool parse_target(const Json& c, DoorTarget* out) {
  int64_t x = 0, y = 0, z = 0;
  out->map = get_int_or(c["map"], -1);
  if (!c.is_object() || out->map < 0 || out->map >= 1200 || !get_int(c["x"], &x) || !get_int(c["y"], &y) ||
      !get_int(c["z"], &z))
    return false;
  out->x = static_cast<int>(x);
  out->y = static_cast<int>(y);
  out->z = static_cast<int>(z);
  out->dir = get_int_or(c["dir"], -1);
  return true;
}

// A row's `ask` [hubask agent]: {text, answers?: [yes, no], doors?: [id], again?: "visit" | "always",
// options?: [{map, window, choice, text, to}]}. Lines of at most 34 characters (the windows' width), at most 3 of them.
bool parse_ask(const Json& a, HubAsk* out, const std::string& where, std::string* error) {
  if (a.is_null()) return true;
  if (!a.is_object()) return fail(error, where + ": ask must be an object");
  auto line_ok = [](const std::string& s) { return !s.empty() && s.size() <= 34 && s.find('\n') == std::string::npos; };
  const std::string text = a["text"].as_string();
  for (size_t at = 0; at <= text.size();) {
    const size_t nl = std::min(text.find('\n', at), text.size());
    out->text.push_back(text.substr(at, nl - at));
    at = nl + 1;
  }
  if (out->text.size() > 3 || !std::all_of(out->text.begin(), out->text.end(), line_ok))
    return fail(error, where + ": ask.text: 1-3 lines of 1-34 characters expected");
  if (const Json& an = a["answers"]; !an.is_null()) {
    if (!an.is_array() || an.size() != 2 || !line_ok(an[0].as_string()) || !line_ok(an[1].as_string()))
      return fail(error, where + ": ask.answers: [yes, no] expected");
    out->yes = an[0].as_string();
    out->no = an[1].as_string();
  }
  if (const Json& d = a["doors"]; !d.is_null()) {
    if (!d.is_array()) return fail(error, where + ": ask.doors must be an array");
    out->all_doors = false;
    for (size_t k = 0; k < d.size(); ++k) {
      int64_t id = 0;
      if (!get_int(d[k], &id) || id < 0) return fail(error, where + ": ask.doors: door ids expected");
      out->doors.push_back(id);
    }
  }
  if (const Json& g = a["again"]; !g.is_null()) {  // [hubask2 agent]
    if (g.as_string() != "visit" && g.as_string() != "always") return fail(error, where + ": ask.again: \"visit\" or \"always\" expected");
    out->once_per_visit = g.as_string() == "visit";
  }
  if (const Json& o = a["options"]; !o.is_null()) {
    if (!o.is_array()) return fail(error, where + ": ask.options must be an array");
    for (size_t k = 0; k < o.size(); ++k) {
      HubAsk::Option opt;
      opt.map = get_int_or(o[k]["map"], -1);
      opt.window = get_int_or(o[k]["window"], -1);
      opt.choice = get_int_or(o[k]["choice"], -1);
      opt.text = o[k]["text"].as_string();
      if (opt.map < 0 || opt.map >= 1200 || opt.window < 0 || (opt.window & 3) != 0 || opt.choice <= opt.window ||
          (opt.choice & 3) != 0 || !line_ok(opt.text) || !parse_target(o[k]["to"], &opt.to))
        return fail(error, where + ": ask.options[" + std::to_string(k) +
                               "]: map, window and choice (code offsets), text and to {map, x, y, z} expected");
      out->options.push_back(opt);
    }
  }
  out->on = true;
  return true;
}

// The hub's `bosses` (docs/ap/seed_format.md "Hub") [hubvm agent]: one chamber per row,
// {encounter, name, map, maps?, view?: {chapter, step, flags?: {"n": 0|1}, globals?: {"0xNNN": v},
// keep?: [n]}}.
bool parse_bosses(const Json& bj, std::vector<HubBoss>* out, std::string* error) {
  if (bj.is_null()) return true;
  if (!bj.is_array()) return fail(error, "bosses must be an array");
  constexpr int kFlags = 0x170 * 8;
  for (size_t i = 0; i < bj.size(); ++i) {
    const Json& b = bj[i];
    const std::string where = "bosses[" + std::to_string(i) + "]";
    if (!b.is_object()) return fail(error, where + ": a boss must be an object");
    HubBoss boss;
    boss.encounter = get_int_or(b["encounter"], -1);
    if (boss.encounter < 0) return fail(error, where + ": encounter missing");
    boss.name = b["name"].as_string();
    boss.map = get_int_or(b["map"], -1);
    if (boss.map < 0 || boss.map >= 1200) return fail(error, where + ": map (0-1199) missing");
    if (!b["maps"].is_null() && !b["maps"].is_array()) return fail(error, where + ": maps must be an array");
    for (size_t k = 0; k < b["maps"].size(); ++k) {
      const int m = get_int_or(b["maps"][k], -1);
      if (m < 0 || m >= 1200) return fail(error, where + ": maps: 0-1199 expected");
      boss.maps.push_back(m);
    }
    if (std::find(boss.maps.begin(), boss.maps.end(), boss.map) == boss.maps.end()) boss.maps.insert(boss.maps.begin(), boss.map);
    const Json& v = b["view"];
    if (!v.is_null()) {
      if (!v.is_object()) return fail(error, where + ": view must be an object");
      boss.has_view = true;
      int64_t n = 0;
      if (!get_int(v["chapter"], &n) || n < 0 || n > 255) return fail(error, where + ": view.chapter (0-255) missing");
      boss.view.chapter = static_cast<int>(n);
      if (!get_int(v["step"], &n) || n < 0 || n > 0xFFFFFF) return fail(error, where + ": view.step missing");
      boss.view.step = static_cast<int>(n);
      if (!v["flags"].is_null() && !v["flags"].is_object()) return fail(error, where + ": view.flags must be an object");
      for (const auto& [key, val] : v["flags"].items()) {
        if (!get_int(Json::string(key), &n) || n < 0 || n >= kFlags)
          return fail(error, where + ": view.flags: script flags 0-2943 expected ('" + key + "')");
        int64_t on = 0;
        if (val.type() == Json::Type::Bool) on = val.as_bool() ? 1 : 0;
        else if (!get_int(val, &on) || (on != 0 && on != 1)) return fail(error, where + ": view.flags: 0 or 1 expected");
        boss.view.flags[static_cast<int>(n)] = on != 0;
      }
      if (!v["globals"].is_null() && !v["globals"].is_object()) return fail(error, where + ": view.globals must be an object");
      for (const auto& [key, val] : v["globals"].items()) {
        // {"0xADDR": word} or {"0xADDR": {"size": 1|2|4, "value": v}} (a byte or half global on
        // its own): past the chapter and step, inside the game state (G[0xa8] .. G[0x33f]).
        StoryView::Global g;
        int64_t value = 0;
        bool ok = get_int(Json::string(key), &n);
        if (val.is_object()) {
          g.size = get_int_or(val["size"], 4);
          ok = ok && get_int(val["value"], &value) && (g.size == 1 || g.size == 2 || g.size == 4);
        } else {
          ok = ok && get_int(val, &value);
        }
        if (!ok || n < 0xA8 || n + g.size > 0x340 || (n & (g.size - 1)) != 0)
          return fail(error, where + ": view.globals: offsets 0xa8-0x33f (aligned) with values expected ('" + key + "')");
        g.addr = static_cast<int>(n);
        g.value = static_cast<int32_t>(value);
        boss.view.globals.push_back(g);
      }
      if (!v["after_maps"].is_null() && !v["after_maps"].is_array()) return fail(error, where + ": view.after_maps must be an array");
      for (size_t k = 0; k < v["after_maps"].size(); ++k) {
        const int m = get_int_or(v["after_maps"][k], -1);
        if (m < 0 || m >= 1200) return fail(error, where + ": view.after_maps: 0-1199 expected");
        boss.view.after_maps.push_back(m);
      }
      if (const Json& u = v["until"]; !u.is_null()) {
        if (u.as_string() != "free_play" && u.as_string() != "transfer")
          return fail(error, where + ": view.until: free_play or transfer expected");
        boss.view.until_transfer = u.as_string() == "transfer";
      }
      if (!v["done_flag"].is_null()) {  // [hubchambers]
        boss.view.done_flag = get_int_or(v["done_flag"], -1);
        if (boss.view.done_flag < 0 || boss.view.done_flag >= 0x170 * 8)
          return fail(error, where + ": view.done_flag: a flag 0-2943 expected");
      }
      if (const Json& c = v["cut_to"]; !c.is_null()) {
        int64_t x = 0, y = 0, z = 0;
        boss.view.cut_to.map = get_int_or(c["map"], -1);
        if (!c.is_object() || boss.view.cut_to.map < 0 || boss.view.cut_to.map >= 1200 || !get_int(c["x"], &x) ||
            !get_int(c["y"], &y) || !get_int(c["z"], &z))
          return fail(error, where + ": view.cut_to needs map (0-1199), x, y and z");
        boss.view.has_cut_to = true;
        boss.view.cut_to.x = static_cast<int>(x);
        boss.view.cut_to.y = static_cast<int>(y);
        boss.view.cut_to.z = static_cast<int>(z);
        boss.view.cut_to.dir = get_int_or(c["dir"], -1);
      }
      if (!v["keep"].is_null() && !v["keep"].is_array()) return fail(error, where + ": view.keep must be an array");
      for (size_t k = 0; k < v["keep"].size(); ++k) {
        const int flag = get_int_or(v["keep"][k], -1);
        if (boss.view.flags.count(flag) == 0) return fail(error, where + ": view.keep: a flag of view.flags expected");
        boss.view.keep.push_back(flag);
      }
    }
    if (const Json& game = b["game"]; !game.is_null()) {
      if (game.as_string() == "claude") boss.game = 1;
      else if (game.as_string() == "rena") boss.game = 2;
      else return fail(error, where + ": game: claude or rena expected");
    }
    if (!parse_ask(b["ask"], &boss.ask, where, error)) return false;  // [hubask agent]
    out->push_back(boss);
  }
  return true;
}

// The hub block's `lock` (the rest of the block is New Game's, hubcore's) [hubvm agent].
bool parse_hub_lock(const Json& lj, Seed* s, std::string* error) {
  if (lj.is_null()) return true;
  if (!lj.is_object()) return fail(error, "hub.lock must be an object");
  s->hub_lock_chapter = lj["chapter"].as_bool(true);
  s->hub_lock_step = lj["step"].as_bool(true);
  if (const Json& sm = lj["step_maps"]; !sm.is_null()) {
    if (!sm.is_array()) return fail(error, "hub.lock.step_maps must be an array");
    s->hub_step_maps.clear();
    for (size_t k = 0; k < sm.size(); ++k) {
      const int m = get_int_or(sm[k], -1);
      if (m < 0 || m >= 1200) return fail(error, "hub.lock.step_maps: maps 0-1199 expected");
      s->hub_step_maps.push_back(m);
    }
  }
  return true;
}

// The hub block's New Game state [hubcore agent] (docs/ap/seed_format.md "Hub"): `flags`,
// `globals` {"0xADDR": word}, `games` {claude / rena: {flags, globals}}, `chapter`, `step`,
// `disc`, `start` {name, map, x, y, z, dir, globals?}. Script global words go in the game state
// past the route bytes (G[0xc4] .. G[0x33f], 4-aligned), never over the names or the message
// speed (G[0xc8] .. G[0x1bb]: New Game's own and a setting); the map / position / music words
// (G[0xa8] .. G[0xc3]) are the start's.
bool parse_hub_words(const Json& gj, std::vector<HubWord>* out, std::string* error, const std::string& where) {
  if (gj.is_null()) return true;
  if (!gj.is_object()) return fail(error, where + " must be an object");
  for (const auto& [key, val] : gj.items()) {
    int64_t addr = 0, value = 0;
    if (!get_int(Json::string(key), &addr) || !get_int(val, &value) || addr < 0xC4 || addr + 4 > 0x340 || (addr & 3) != 0 ||
        (addr >= 0xC8 && addr < 0x1BC) || value < INT32_MIN || value > UINT32_MAX)
      return fail(error, where + ": words at 0xc4-0x33c (4-aligned, not the names 0xc8-0x1bb) expected ('" + key + "')");
    out->push_back({static_cast<int>(addr), static_cast<uint32_t>(value)});
  }
  return true;
}
bool parse_hub_flags(const Json& fj, std::vector<int>* out, std::string* error, const std::string& where) {
  if (fj.is_null()) return true;
  if (!fj.is_array()) return fail(error, where + " must be an array");
  for (size_t k = 0; k < fj.size(); ++k) {
    const int flag = get_int_or(fj[k], -1);
    if (flag < 0 || flag >= 0x170 * 8) return fail(error, where + ": script flags 0-2943 expected");
    out->push_back(flag);
  }
  return true;
}
bool parse_hub_state(const Json& hj, Seed* s, std::string* error) {
  HubState& h = s->hub_state;
  if (!s->hub || hj.is_null() || (!hj.has("flags") && !hj.has("start"))) return true;
  h.on = true;
  int64_t n = 0;
  if (hj.has("chapter") && (!get_int(hj["chapter"], &n) || n < 0 || n > 255)) return fail(error, "hub.chapter: 0-255 expected");
  if (hj.has("chapter")) h.chapter = static_cast<int>(n);
  if (hj.has("step") && (!get_int(hj["step"], &n) || n < 0 || n > 0xFFFFFF)) return fail(error, "hub.step: 0-0xffffff expected");
  if (hj.has("step")) h.step = static_cast<int>(n);
  if (hj.has("disc") && (!get_int(hj["disc"], &n) || n < 1 || n > 2)) return fail(error, "hub.disc: 1 or 2 expected");
  if (hj.has("disc")) h.disc = static_cast<int>(n);
  if (!parse_hub_flags(hj["flags"], &h.flags, error, "hub.flags")) return false;
  if (!parse_hub_words(hj["globals"], &h.globals, error, "hub.globals")) return false;
  const Json& games = hj["games"];
  if (!games.is_null() && !games.is_object()) return fail(error, "hub.games must be an object");
  for (const auto& [key, gj] : games.items()) {
    if (key != "claude" && key != "rena") return fail(error, "hub.games: claude or rena expected ('" + key + "')");
    HubGame& game = h.games[key == "claude" ? 0 : 1];
    if (!gj.is_object()) return fail(error, "hub.games." + key + " must be an object");
    if (!parse_hub_flags(gj["flags"], &game.flags, error, "hub.games." + key + ".flags")) return false;
    if (!parse_hub_words(gj["globals"], &game.globals, error, "hub.games." + key + ".globals")) return false;
  }
  const Json& st = hj["start"];
  int64_t x = 0, y = 0, z = 0;
  h.start.map = get_int_or(st["map"], -1);
  if (!st.is_object() || h.start.map < 4 || h.start.map > 826 || !get_int(st["x"], &x) || !get_int(st["y"], &y) ||
      !get_int(st["z"], &z))
    return fail(error, "hub.start needs map (a field map, 4-826), x, y and z");
  h.start.x = static_cast<int>(x);
  h.start.y = static_cast<int>(y);
  h.start.z = static_cast<int>(z);
  h.start.dir = get_int_or(st["dir"], 90);
  h.start_name = st["name"].as_string();
  return parse_hub_words(st["globals"], &h.start_globals, error, "hub.start.globals");
}

}  // namespace

const char* character_name(int id) {
  static const char* const kNames[kCharacterCount] = {"Claude", "Rena",  "Celine", "Bowman", "Dias", "Precis",
                                                      "Ashton", "Leon",  "Opera",  "Ernest", "Noel", "Chisato"};
  return id >= 1 && id <= kCharacterCount ? kNames[id - 1] : "";
}

int character_by_name(const std::string& name) {
  for (int id = 1; id <= kCharacterCount; ++id)
    if (name == character_name(id)) return id;
  return 0;
}

// ---- The seed file ----

bool load_seed_text(const std::string& text, Seed* out, std::string* error) {
  Json j;
  std::string perr;
  if (!Json::parse(text, &j, &perr)) return fail(error, "not JSON: " + perr);
  if (!j.is_object()) return fail(error, "the seed must be a JSON object");
  if (j["format"].as_string() != "apso2") return fail(error, "format must be \"apso2\"");
  Seed s;
  s.version = get_int_or(j["version"], 0);
  // 1: phases 1-4; 2: a seed using any v2 field (docs/ap/seed_format.md "The version rule");
  // 3: a hub seed (docs/ap/seed_format.md "Hub") [hubvm agent].
  if (s.version < 1 || s.version > 3)
    return fail(error, "unsupported version " + std::to_string(s.version) + " (this build reads 1 to 3)");
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
  std::set<int64_t> seen, placeholders;
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
    } else if (l.type == "join") {
      l.offset = get_int_or(c["offset"], -1);
      if (l.map < 0 || l.offset < 0) return fail(error, where + ": a join needs map and offset (the site's call)");
      if (c.has("character")) {
        l.character = parse_character(c["character"]);
        if (l.character == 0) return fail(error, where + ": character 1-12 or a name expected");
      }
    } else if (l.type == "drop") {
      l.enemy = get_int_or(c["enemy"], -1);
      if (l.enemy < 0) return fail(error, where + ": a drop needs its enemy type");
    } else if (l.type == "shop") {
      l.shop = get_int_or(c["shop"], -1);
      l.index = get_int_or(c["index"], -1);
      l.shop_item = get_int_or(c["item"], -1);
      if (l.shop < 0 || l.shop > 0xFF || l.index < 0 || l.index > 0xFF)
        return fail(error, where + ": a shop slot needs shop (0-255) and index (0-255)");
    } else if (l.type == "craft") {  // v2: the creation that makes this item
      l.craft_item = get_int_or(c["item"], -1);
      if (l.craft_item < 1 || l.craft_item >= 0x400) return fail(error, where + ": a craft needs its item (1-1023)");
    } else if (l.type == "killer_move") {  // v2: a member's use count of a move reaching a level
      l.character = parse_character(c["character"]);
      l.move = get_int_or(c["move"], -1);
      l.level = get_int_or(c["level"], 0);
      l.uses = get_int_or(c["uses"], -1);
      if (l.character == 0) return fail(error, where + ": a killer move needs its character (1-12 or a name)");
      // 1-15: the id's 4 bits (0.2.0 uses 1-11: Claude's Mirror Slice is move 11); the record has
      // room for more (32 counters, +0x8c + 2 * (move - 1) up to +0xca).
      if (l.move < 1 || l.move > 15) return fail(error, where + ": a killer move needs its move (1-15)");
      if (l.uses < 1 || l.uses > 999) return fail(error, where + ": a killer move needs uses (1-999)");
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
    if (lj.has("placeholder")) {
      if (!get_int(lj["placeholder"], &v) ||
          !((v >= kItemPlaceholderFirst && v <= kItemPlaceholderLast) || is_fol_placeholder(v)))
        return fail(error, where + ": placeholder 0x400-0x270f (item), 0xf00000-0xffffff or 0x7f000000-0x7fffffff (Fol) expected");
      if (!placeholders.insert(v).second) return fail(error, where + ": placeholder " + std::to_string(v) + " repeated");
      l.placeholder = v;
    }
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
  // The goal: a victory in one of these encounters (default: the final fight, 539 or 540: two
  // versions of the one fight, 540 the harder one after Filia's event, flag 0x22c; map 685).
  const Json& goal = j["goal"]["encounters"];
  for (size_t i = 0; i < goal.size(); ++i) s.goal_encounters.push_back(get_int_or(goal[i], -1));
  if (!j["goal"].has("encounters")) s.goal_encounters = {539, 540};
  // v2: goal.all (every one must be won, any order) and goal.flags (set at New Game and after
  // every load: a gate's preset flags, which start.flags would miss on a save without a sidecar).
  const Json& goal_all = j["goal"]["all"];
  if (!goal_all.is_null() && !goal_all.is_array()) return fail(error, "goal.all must be an array");
  for (size_t i = 0; i < goal_all.size(); ++i) {
    const int enc = get_int_or(goal_all[i], -1);
    if (enc < 0) return fail(error, "goal.all: encounter ids expected");
    if (std::find(s.goal_all.begin(), s.goal_all.end(), enc) == s.goal_all.end()) s.goal_all.push_back(enc);
  }
  const Json& goal_flags = j["goal"]["flags"];
  if (!goal_flags.is_null() && !goal_flags.is_array()) return fail(error, "goal.flags must be an array");
  for (size_t i = 0; i < goal_flags.size(); ++i) {
    const int flag = get_int_or(goal_flags[i], -1);
    if (flag < 0 || flag >= 0x170 * 8) return fail(error, "goal.flags: script flags 0-2943 expected");
    s.goal_flags.push_back(flag);
  }
  s.deathlink = s.options["deathlink"].as_bool(false);
  // so2.apworld 0.1.0 / 0.1.1 seeds name two options the old way (protagonist,
  // drops_as_checks); read whichever name the seed has.
  const char* route_key = s.options.has("story_route") || !s.options.has("protagonist") ? "story_route" : "protagonist";
  const char* drops_key =
      s.options.has("enemy_dropsanity") || !s.options.has("drops_as_checks") ? "enemy_dropsanity" : "drops_as_checks";
  // The story_route the seed's logic assumes (the APWorld's option: any / claude / rena).
  if (const Json& p = s.options[route_key]; p.is_string()) {
    const std::string& v = p.as_string();
    if (v == "claude") s.story_route = 1;
    else if (v == "rena") s.story_route = 2;
    else if (v != "any" && v != "random")
      return fail(error, std::string("options.") + route_key + ": any, claude or rena expected");
  } else if (!p.is_null() && !option_int(s.options, route_key, 0, 2, &s.story_route, error)) {
    return false;
  }
  // Phase 3: the randomizer's play options. Without them (or at their defaults) the game
  // plays as the original.
  s.character_items = s.options["character_items"].as_bool(false);
  const Json& drops = s.options[drops_key];
  if (drops.is_string()) {
    if (drops.as_string() != "off" && drops.as_string() != "first_drop")
      return fail(error, std::string("options.") + drops_key + ": off or first_drop expected");
    s.first_drops = drops.as_string() == "first_drop";
  } else {
    s.first_drops = drops.as_bool(false);
  }
  if (!option_int(s.options, "exp_multiplier", 1, 10, &s.exp_multiplier, error)) return false;
  if (!option_int(s.options, "fol_multiplier", 1, 10, &s.fol_multiplier, error)) return false;
  if (!option_int(s.options, "encounter_rate", 0, 200, &s.encounter_rate, error)) return false;
  // Phase 4: the open world (doors need keys, the Expel teleporter), shop slots, the door table.
  if (const Json& wm = s.options["world_mode"]; wm.is_string()) {
    if (wm.as_string() != "story" && wm.as_string() != "open_world" && wm.as_string() != "hub")
      return fail(error, "options.world_mode: story, open_world or hub expected");
    s.open_world = wm.as_string() == "open_world";
    s.hub = wm.as_string() == "hub";  // [hubvm agent]
  } else if (!wm.is_null()) {
    int v = 0;
    if (!option_int(s.options, "world_mode", 0, 2, &v, error)) return false;
    s.open_world = v == 1;
    s.hub = v == 2;
  }
  s.expel_teleporter = s.options["expel_teleporter"].as_bool(s.open_world);
  if (const Json& sh = s.options["shuffle_shops"]; sh.is_string()) {
    const std::string& v = sh.as_string();
    if (v == "off") s.shuffle_shops = 0;
    else if (v == "specials") s.shuffle_shops = 1;
    else if (v == "full") s.shuffle_shops = 2;
    else return fail(error, "options.shuffle_shops: off, specials or full expected");
  } else if (!sh.is_null() && !option_int(s.options, "shuffle_shops", 0, 2, &s.shuffle_shops, error)) {
    return false;
  }
  s.no_fail_crafting = s.options["no_fail_crafting"].as_bool(false);  // v2
  // v2 party [party agent]: party_rules "story" (phase 3's rules, the default) or "open"; the
  // starting character by name (as options.characters spells them) or party id, null for none.
  if (const Json& pr = s.options["party_rules"]; pr.is_string()) {
    if (pr.as_string() != "story" && pr.as_string() != "open") return fail(error, "options.party_rules: story or open expected");
    s.open_party = pr.as_string() == "open";
  } else if (!pr.is_null()) {
    return fail(error, "options.party_rules: story or open expected");
  }
  // [charselect agent] "any" or a list: the player picks at New Game (the character select).
  const Json& sc = s.options["starting_character"];
  if ((sc.is_string() && sc.as_string() == "any") || sc.is_array()) {
    for (int id = 1; sc.is_string() && id <= kCharacterCount; ++id) s.starting_choices.push_back(id);
    const Json& ex = s.options["exclude_characters"];
    for (size_t i = 0; sc.is_string() && i < ex.size(); ++i)
      std::erase(s.starting_choices, parse_character(ex[i]));
    for (size_t i = 0; sc.is_array() && i < sc.size(); ++i) {
      const int id = parse_character(sc[i]);
      if (id == 0) return fail(error, "options.starting_character: a list of characters' names expected");
      if (std::find(s.starting_choices.begin(), s.starting_choices.end(), id) == s.starting_choices.end())
        s.starting_choices.push_back(id);
    }
  } else if (!sc.is_null()) {
    const bool none = (sc.is_string() && (sc.as_string().empty() || sc.as_string() == "hero")) ||
                      (sc.is_number() && sc.as_int(-1) == 0);
    if (!none) {
      s.starting_character = parse_character(sc);
      if (s.starting_character == 0) return fail(error, "options.starting_character: a character's name or null expected");
    }
  }
  const Json& dj = j["doors"];
  if (!dj.is_null() && !dj.is_array()) return fail(error, "doors must be an array");
  std::set<int64_t> door_ids;
  for (size_t i = 0; i < dj.size(); ++i) {
    const Json& d = dj[i];
    const std::string where = "doors[" + std::to_string(i) + "]";
    if (!d.is_object()) return fail(error, where + ": a door must be an object");
    Door door;
    int64_t v = 0;
    const bool has_id = get_int(d["id"], &v);
    door.id = v;
    door.map = get_int_or(d["map"], has_id ? static_cast<int>(v >> 16) : -1);
    door.offset = get_int_or(d["offset"], has_id ? static_cast<int>(v & 0xFFFF) * 4 : -1);
    if (door.map < 0 || door.map > 0xFFFF || door.offset < 0 || door.offset >= 0x40000 || (door.offset & 3) != 0)
      return fail(error, where + ": a door needs its id, or map and offset (a word's byte offset)");
    if (!has_id) door.id = door_id(door.map, door.offset);
    if (door.id != door_id(door.map, door.offset))
      return fail(error, where + ": id " + std::to_string(door.id) + " is not map * 65536 + offset / 4");
    if (!door_ids.insert(door.id).second) return fail(error, where + ": door " + std::to_string(door.id) + " repeated");
    for (size_t k = 0; k < d["maps"].size(); ++k) door.maps.push_back(get_int_or(d["maps"][k], -1));
    if (door.maps.empty()) door.maps.push_back(door.map);
    door.routine = get_int_or(d["routine"], -1);
    door.key = get_int_or(d["key"], 0);
    if (door.key < 0 || door.key > 255) return fail(error, where + ": key 1-255 expected");
    if (door.key != 0) door.keys.push_back(door.key);
    const Json& kj = d["keys"];
    if (!kj.is_null() && !kj.is_array()) return fail(error, where + ": keys must be an array");
    for (size_t k = 0; k < kj.size(); ++k) {
      const int key = get_int_or(kj[k], -1);
      if (key < 1 || key > 255) return fail(error, where + ": keys: 1-255 expected");
      if (std::find(door.keys.begin(), door.keys.end(), key) == door.keys.end()) door.keys.push_back(key);
    }
    if (door.key == 0 && !door.keys.empty()) door.key = door.keys.front();
    door.name = d["name"].as_string();
    door.purpose = d["purpose"].as_string();
    door.until = get_int_or(d["until"], 0);
    if (door.until < 0 || door.until > 255) return fail(error, where + ": until: a chapter 1-255 expected");
    // v2 conditions: flags that must be set, encounters that must be won (goal_won); they apply
    // in every world mode, and the gate is the trigger, so the door names its routine.
    for (const char* key : {"flags", "won", "banners"})
      if (!d[key].is_null() && !d[key].is_array()) return fail(error, where + ": " + key + " must be an array");
    for (size_t k = 0; k < d["flags"].size(); ++k) {
      const int flag = get_int_or(d["flags"][k], -1);
      if (flag < 0 || flag >= 0x170 * 8) return fail(error, where + ": flags: script flags 0-2943 expected");
      door.flags.push_back(flag);
    }
    for (size_t k = 0; k < d["won"].size(); ++k) {
      const int enc = get_int_or(d["won"][k], -1);
      if (enc < 0) return fail(error, where + ": won: encounter ids expected");
      door.won.push_back(enc);
    }
    for (size_t k = 0; k < d["banners"].size(); ++k) door.banners.push_back(d["banners"][k].as_string());
    if (door.has_conditions() && door.routine < 0)
      return fail(error, where + ": a door with flags / won needs its routine");
    if (d.has("to")) {
      const Json& t = d["to"];
      door.has_target = true;
      door.to.map = get_int_or(t["map"], -1);
      int64_t x = 0, y = 0, z = 0;
      if (door.to.map < 0 || door.to.map >= 1200 || !get_int(t["x"], &x) || !get_int(t["y"], &y) || !get_int(t["z"], &z))
        return fail(error, where + ": to needs map (0-1199), x, y and z");
      door.to.x = static_cast<int>(x);
      door.to.y = static_cast<int>(y);
      door.to.z = static_cast<int>(z);
      door.to.dir = get_int_or(t["dir"], -1);
      // [hubcore agent] P5: a redirect that holds only while a boss is unbeaten.
      door.to_while_unbeaten = get_int_or(t["while_unbeaten"], -1);
      if (!t["while_unbeaten"].is_null() && door.to_while_unbeaten < 0)
        return fail(error, where + ": to.while_unbeaten: an encounter id expected");
    }
    s.doors.push_back(door);
  }
  // The hub's chambers and its lock [hubvm agent].
  if (!parse_bosses(j["bosses"], &s.bosses, error)) return false;
  if (!j["hub"].is_null() && !j["hub"].is_object()) return fail(error, "hub must be an object");
  if (!parse_hub_lock(j["hub"]["lock"], &s, error)) return false;
  if (!parse_hub_state(j["hub"], &s, error)) return false;  // [hubcore agent]
  // D7 (docs/handoff/hub.md): characters are items in hub mode, under the open-world party rules.
  if (s.hub) {
    s.character_items = true;
    s.open_party = true;
  }
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

}  // namespace ss::game::ap
