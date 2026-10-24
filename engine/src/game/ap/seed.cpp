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
  s.deathlink = s.options["deathlink"].as_bool(false);
  // The protagonist the seed's logic assumes (the APWorld's option: any / claude / rena).
  if (const Json& p = s.options["protagonist"]; p.is_string()) {
    const std::string& v = p.as_string();
    if (v == "claude") s.protagonist = 1;
    else if (v == "rena") s.protagonist = 2;
    else if (v != "any" && v != "random") return fail(error, "options.protagonist: any, claude or rena expected");
  } else if (!p.is_null() && !option_int(s.options, "protagonist", 0, 2, &s.protagonist, error)) {
    return false;
  }
  // Phase 3: the randomizer's play options. Without them (or at their defaults) the game
  // plays as the original.
  s.character_items = s.options["character_items"].as_bool(false);
  const Json& drops = s.options["drops_as_checks"];
  if (drops.is_string()) {
    if (drops.as_string() != "off" && drops.as_string() != "first_drop")
      return fail(error, "options.drops_as_checks: off or first_drop expected");
    s.first_drops = drops.as_string() == "first_drop";
  } else {
    s.first_drops = drops.as_bool(false);
  }
  if (!option_int(s.options, "exp_multiplier", 1, 10, &s.exp_multiplier, error)) return false;
  if (!option_int(s.options, "fol_multiplier", 1, 10, &s.fol_multiplier, error)) return false;
  if (!option_int(s.options, "encounter_rate", 0, 200, &s.encounter_rate, error)) return false;
  // Phase 4: the open world (doors need keys, the Expel teleporter), shop slots, the door table.
  if (const Json& wm = s.options["world_mode"]; wm.is_string()) {
    if (wm.as_string() != "story" && wm.as_string() != "open_world")
      return fail(error, "options.world_mode: story or open_world expected");
    s.open_world = wm.as_string() == "open_world";
  } else if (!wm.is_null()) {
    int v = 0;
    if (!option_int(s.options, "world_mode", 0, 1, &v, error)) return false;
    s.open_world = v == 1;
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
    }
    s.doors.push_back(door);
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
