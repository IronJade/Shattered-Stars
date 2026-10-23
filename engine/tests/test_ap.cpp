// Archipelago, phase 1 (game/ap; docs/ap/seed_format.md): the JSON reader, the seed loader,
// checks and the receive queue, and the save sidecar. Files go in test_ap_scratch/ next to
// the test program. No disc needed.

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "ap_net/link.h"
#include "game/ap/ap.h"
#include "game/ap/json.h"
#include "game/battle/battle.h"
#include "game/field/field.h"
#include "game/field/script_vm.h"
#include "test.h"

using namespace ss;
using namespace ss::game;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
  const char* base = SDL_GetBasePath();
  const fs::path dir = fs::path(base ? base : ".") / "test_ap_scratch" / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

// A small seed: a chest of our own (Fol), a chest holding another player's item, a gift and a
// boss; two item types; starting items.
const char* kSeed = R"seed({
  "format": "apso2",
  "version": 1,
  "seed": "12345678901234567890",
  "game": "Star Ocean: The Second Story",
  "slot": {"name": "Claude", "id": 1},
  "options": {"protagonist": "claude", "goal": "indalecio"},
  "items": {
    "5000021": {"name": "Revival Card", "kind": "item", "game_id": "0x15"},
    "5009000": {"name": "500 Fol", "kind": "fol", "amount": 500}
  },
  "locations": [
    {"id": 1347569665, "name": "Arlia: crate by the store (200 Fol)",
     "check": {"type": "chest", "map": 18, "flag": "0x962"},
     "item": {"player": 1, "kind": "fol", "amount": 5000, "name": "5000 Fol"}},
    {"id": 1347569664, "name": "Arlia: crate (item 0x2a5)",
     "check": {"type": "chest", "map": 18, "flag": 2401},
     "item": {"player": 2, "player_name": "Alice", "name": "Hookshot", "ap_id": 77, "classification": "progression"}},
    {"id": 1349650432, "name": "a gift",
     "check": {"type": "gift", "map": 26, "offset": 1234},
     "item": {"player": 1, "kind": "item", "game_id": 21, "count": 2, "name": "Revival Card"}},
    {"id": 1, "name": "Cyril",
     "check": {"type": "boss", "encounter": 0},
     "item": {"player": 1, "kind": "fol", "amount": 1}}
  ],
  "patches": ["script 18 0x82c 0xffffff38 0xffffec78", "", "# a comment"],
  "start": {"items": [{"kind": "item", "game_id": 1, "count": 3}], "fol": 100, "flags": [5, "0x10"]}
})seed";

// A Link that records what the game sends and hands out queued events.
struct FakeLink : ap_net::Link {
  std::vector<int64_t>* sent;
  int* goals;
  std::vector<std::string>* deaths;
  std::vector<ap_net::Event>* events;
  FakeLink(std::vector<int64_t>* s, int* g, std::vector<std::string>* d, std::vector<ap_net::Event>* e)
      : sent(s), goals(g), deaths(d), events(e) {}
  void check(const std::vector<int64_t>& l) override { sent->insert(sent->end(), l.begin(), l.end()); }
  void goal() override { ++*goals; }
  void deathlink(const std::string& cause) override { deaths->push_back(cause); }
  void say(const std::string&) override {}
  std::vector<ap_net::Event> poll() override {
    std::vector<ap_net::Event> out;
    out.swap(*events);
    return out;
  }
  ap_net::State state() const override { return ap_net::State::kSlotConnected; }
  std::string player_name(int slot) const override { return slot == 2 ? "Alice" : "P" + std::to_string(slot); }
  std::string item_name(int64_t item, int) const override { return "Thing " + std::to_string(item); }
  std::string location_name(int64_t, int) const override { return ""; }
};

std::string replace(std::string s, const std::string& from, const std::string& to) {
  const size_t at = s.find(from);
  if (at != std::string::npos) s.replace(at, from.size(), to);
  return s;
}

bool seed_error(const std::string& text, const std::string& want) {
  ap::Seed s;
  std::string error;
  if (ap::load_seed_text(text, &s, &error)) return false;
  if (error.find(want) == std::string::npos) {
    std::fprintf(stderr, "  error '%s' lacks '%s'\n", error.c_str(), want.c_str());
    return false;
  }
  return true;
}

}  // namespace

TEST(ap_json_round_trip) {
  ap::Json j;
  std::string error;
  CHECK(ap::Json::parse(R"({"big": 1347569665123, "neg": -7, "real": 2.5, "s": "a\"b\\c\n\u00e9\ud83d\ude00",
                           "list": [1, [true, false, null], {}], "hex": "0x1eb"})",
                        &j, &error));
  CHECK_EQ(j["big"].as_int(), 1347569665123LL);
  CHECK_EQ(j["neg"].as_int(), -7);
  CHECK(j["real"].is_number() && j["real"].as_int() == 2);
  CHECK(j["s"].as_string() == "a\"b\\c\n\xc3\xa9\xf0\x9f\x98\x80");
  CHECK_EQ(j["list"].size(), 3u);
  CHECK(j["list"][1][0].as_bool() && !j["list"][1][1].as_bool(true) && j["list"][1][2].is_null());
  CHECK(j["list"][2].is_object() && j["list"][2].size() == 0);
  CHECK_EQ(j["hex"].as_int(), 0x1eb);
  CHECK(j["missing"].is_null() && j["missing"]["deeper"].is_null() && j["list"][99].is_null());
  // Written and read again: the same values.
  ap::Json k;
  CHECK(ap::Json::parse(j.dump(), &k, &error));
  CHECK(k.dump() == j.dump());
  CHECK(ap::Json::parse(j.dump(0), &k, &error) && k.dump() == j.dump());
  // Malformed text fails with the line.
  CHECK(!ap::Json::parse("{\n\"a\": 1,\n\"b\" 2}", &k, &error) && error.find("line 3") != std::string::npos);
  CHECK(!ap::Json::parse("[1, 2", &k, &error));
  CHECK(!ap::Json::parse("{} x", &k, &error) && error.find("trailing") != std::string::npos);
  CHECK(!ap::Json::parse("\"open", &k, &error));
}

TEST(ap_seed_loads) {
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(kSeed, &s, &error));
  if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
  CHECK(s.version == 1 && s.seed == "12345678901234567890" && s.slot_name == "Claude" && s.slot == 1);
  CHECK(s.options["goal"].as_string() == "indalecio");
  CHECK_EQ(s.items.size(), 2u);
  CHECK(s.items.count(5000021) && s.items[5000021].game_id == 0x15 && s.items[5000021].kind == "item");
  CHECK(s.items.count(5009000) && s.items[5009000].kind == "fol" && s.items[5009000].amount == 500);
  CHECK_EQ(s.locations.size(), 4u);
  const ap::Location& chest = s.locations[0];
  CHECK(chest.type == "chest" && chest.map == 18 && chest.flag == 0x962 && chest.item.own);
  CHECK(chest.item.item.kind == "fol" && chest.item.item.amount == 5000);
  const ap::Location& foreign = s.locations[1];
  CHECK(!foreign.item.own && foreign.item.player == 2 && foreign.item.player_name == "Alice");
  CHECK(foreign.item.item.kind == "foreign" && foreign.item.item.name == "Hookshot" && foreign.item.item.ap_id == 77);
  CHECK(foreign.item.classification == "progression");
  CHECK(s.locations[2].type == "gift" && s.locations[2].offset == 1234 && s.locations[2].item.item.count == 2);
  CHECK(s.locations[3].type == "boss" && s.locations[3].encounter == 0);
  CHECK_EQ(s.patches.size(), 2u);  // (the empty line is dropped; mods.cpp skips the comment)
  CHECK(s.patches[0] == "script 18 0x82c 0xffffff38 0xffffec78");
  CHECK(s.start_items.size() == 1 && s.start_items[0].game_id == 1 && s.start_items[0].count == 3);
  CHECK(s.start_fol == 100 && s.start_flags.size() == 2 && s.start_flags[1] == 0x10);
}

TEST(ap_seed_errors) {
  CHECK(seed_error("not json", "not JSON"));
  CHECK(seed_error("[]", "JSON object"));
  CHECK(seed_error(replace(kSeed, "\"apso2\"", "\"other\""), "format"));
  CHECK(seed_error(replace(kSeed, "\"version\": 1", "\"version\": 2"), "unsupported version 2"));
  CHECK(seed_error(replace(kSeed, "\"seed\": \"12345678901234567890\",", ""), "seed"));
  CHECK(seed_error(replace(kSeed, "\"flag\": \"0x962\"", "\"flag\": 9999"), "locations[0]: a chest needs"));
  CHECK(seed_error(replace(kSeed, "\"id\": 1347569664", "\"id\": 1347569665"), "locations[1]: id 1347569665 repeated"));
  CHECK(seed_error(replace(kSeed, "\"game_id\": \"0x15\"", "\"game_id\": 4096"), "items.5000021: game_id"));
  CHECK(seed_error(replace(kSeed, "\"kind\": \"fol\", \"amount\": 500", "\"kind\": \"spell\""), "unknown kind"));
  CHECK(seed_error(replace(kSeed, "\"5000021\":", "\"abc\":"), "not an AP item id"));
  CHECK(seed_error(replace(kSeed, "\"type\": \"boss\", \"encounter\": 0", "\"type\": \"boss\""), "a boss needs"));
  CHECK(seed_error(replace(kSeed, "\"type\": \"gift\", ", ""), "check.type missing"));
  ap::Seed s;
  std::string error;
  CHECK(!ap::load_seed_file("no/such/seed.apso2", &s, &error) && error.find("cannot open") != std::string::npos);
}

TEST(ap_checks_and_queue) {
  ap::stop();
  CHECK(!ap::enabled() && ap::seed() == nullptr && ap::seed_patch_lines().empty());
  CHECK(!ap::location_checked(1347569665));  // off: nothing recorded
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(kSeed, &s, &error));
  ap::start_with(s);
  CHECK(ap::enabled() && ap::seed() != nullptr && ap::seed_patch_lines().size() == 2);
  CHECK(ap::find_location(1) != nullptr && ap::find_location(1)->name == "Cyril");
  CHECK(ap::find_location(42) == nullptr);
  // A check is recorded once; an unknown id is refused.
  CHECK(ap::location_checked(1));
  CHECK(ap::location_checked(1));
  CHECK(!ap::location_checked(42));
  CHECK(ap::state().checked.size() == 1 && ap::is_checked(1) && !ap::is_checked(1347569665));
  // A chest's opened flag checks it; other flags do nothing.
  ap::on_flag_set(5);
  ap::on_flag_set(0x962);
  CHECK(ap::is_checked(1347569665) && ap::state().checked.size() == 2 && ap::state().checked[1] == 1347569665);
  // Received items: by the seed's name or AP id, or a game item id.
  CHECK(ap::receive_by_name_or_id("Revival Card", "Alice", &error));
  CHECK(ap::receive_by_name_or_id("5009000", "", &error));
  CHECK(ap::receive_by_name_or_id("0x2a5", "Bob", &error));
  CHECK(!ap::receive_by_name_or_id("Excalibur", "", &error) && error.find("unknown item") != std::string::npos);
  CHECK(!ap::receive_by_name_or_id("0x400", "", &error));
  CHECK_EQ(ap::pending_count(), 3u);
  CHECK(ap::status_text().find("checks 2/4") != std::string::npos);
  CHECK(ap::status_text().find("waiting 3") != std::string::npos);
  // A new game starts fresh.
  ap::on_new_game();
  CHECK(ap::state().checked.empty() && ap::pending_count() == 0 && !ap::is_checked(1));
  ap::stop();
  CHECK(!ap::enabled() && ap::status_text().find("off") != std::string::npos);
}

TEST(ap_sidecar) {
  CHECK(ap::sidecar_path("cards/memcard1.mcd", 0) == "cards/memcard1.slot01.ap.json");
  CHECK(ap::sidecar_path("cards/my.card.mcr", 14) == "cards/my.card.slot15.ap.json");
  ap::SaveState st;
  st.seed = "12345678901234567890";
  st.slot_name = "Claude";
  st.received_index = 7;
  st.checked = {1347569665, 1, 4294967296LL};
  st.start_given = true;
  ap::SaveState back;
  std::string error;
  CHECK(ap::sidecar_from_json(ap::sidecar_json(st), &back, &error));
  CHECK(back.seed == st.seed && back.slot_name == st.slot_name && back.received_index == 7 && back.start_given);
  CHECK(back.checked == st.checked);
  ap::Json other = ap::Json::object();
  other.set("format", ap::Json::string("something"));
  CHECK(!ap::sidecar_from_json(other, &back, &error));

  // Files: written through a temporary, read back; a missing one is an error.
  const fs::path dir = scratch("sidecar");
  const std::string card = (dir / "memcard1.mcd").string();
  const std::string path = ap::sidecar_path(card, 2);
  CHECK(ap::write_sidecar(path, st, &error));
  CHECK(fs::exists(path) && !fs::exists(path + ".tmp"));
  back = ap::SaveState{};
  CHECK(ap::read_sidecar(path, &back, &error) && back.checked == st.checked && back.received_index == 7);
  CHECK(!ap::read_sidecar((dir / "none.ap.json").string(), &back, &error));
  {
    std::ofstream bad(dir / "bad.ap.json");
    bad << "{\"format\": \"apso2-save\", ";
  }
  CHECK(!ap::read_sidecar((dir / "bad.ap.json").string(), &back, &error));

  // Through the running game: a save writes it, a load reads it back (checks made since
  // the save stay checked); a save without a sidecar starts fresh.
  ap::Seed s;
  CHECK(ap::load_seed_text(kSeed, &s, &error));
  ap::start_with(s);
  CHECK(ap::location_checked(1));
  ap::on_saved(card, 0);
  CHECK(fs::exists(ap::sidecar_path(card, 0)));
  CHECK(ap::location_checked(1347569665));
  ap::on_loaded(card, 0);
  CHECK(ap::is_checked(1) && ap::is_checked(1347569665) && ap::state().checked.size() == 2);
  ap::on_new_game();
  ap::on_loaded(card, 5);  // no sidecar for save 6
  CHECK(ap::state().checked.empty() && ap::state().start_given);
  ap::stop();
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(ap_server_link) {
  ap::stop();
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(replace(kSeed, "\"options\": {\"protagonist\": \"claude\", \"goal\": \"indalecio\"}",
                                   "\"options\": {\"deathlink\": true}, \"goal\": {\"encounters\": [7]}"),
                           &s, &error));
  CHECK(s.deathlink && s.goal_encounters.size() == 1 && s.goal_encounters[0] == 7);
  ap::start_with(s);
  CHECK(ap::deathlink_enabled());
  CHECK(ap::location_checked(1));  // offline: recorded, sent on connect
  std::vector<int64_t> sent;
  int goals = 0;
  std::vector<std::string> deaths;
  std::vector<ap_net::Event> events;
  ap::connect(std::make_unique<FakeLink>(&sent, &goals, &deaths, &events));
  CHECK(!ap::connected());
  // Another room's seed: nothing goes out.
  ap_net::Event c;
  c.kind = ap_net::Event::kConnected;
  c.seed_name = "other";
  events.push_back(c);
  ap::vblank();
  CHECK(!ap::connected() && sent.empty());
  CHECK(ap::status_text().find("wrong seed") != std::string::npos);
  // The right one: our checks go out, the server's join ours.
  c.seed_name = "12345678901234567890";
  c.checked = {1349650432, 999};
  events.push_back(c);
  ap::vblank();
  CHECK(ap::connected());
  CHECK(sent.size() == 2 && sent[0] == 1 && sent[1] == 1349650432);
  CHECK(ap::is_checked(1349650432) && !ap::is_checked(999));
  sent.clear();
  CHECK(ap::location_checked(1347569665) && sent.size() == 1 && sent[0] == 1347569665);
  // Items: by index; a resend after a reconnect adds nothing; an unknown AP id still counts.
  ap_net::Event it;
  it.kind = ap_net::Event::kItems;
  it.items = {{5000021, 10, 2, 0, 0}, {5009000, 11, 2, 0, 1}};
  events.push_back(it);
  ap::vblank();
  CHECK_EQ(ap::pending_count(), 2u);
  it.items = {{5000021, 10, 2, 0, 0}, {5009000, 11, 2, 0, 1}, {42, 12, 3, 0, 2}};
  events.push_back(it);
  ap::vblank();
  CHECK_EQ(ap::pending_count(), 3u);
  CHECK(ap::state().received_index == 0);
  // The goal: once, sent at once while connected.
  ap::on_battle_exit(7, 2);
  CHECK(goals == 0 && !ap::state().goal_done);
  ap::on_battle_exit(7, 0);
  CHECK(goals == 1 && ap::state().goal_done);
  ap::on_battle_exit(7, 0);
  CHECK(goals == 1);
  // A boss location: the victory records it, its own item waits for free play.
  const size_t before = ap::pending_count();
  ap::on_battle_exit(0, 0);
  CHECK(ap::is_checked(1) && ap::pending_count() == before);  // (Cyril's check was already made)
  // DeathLink: in from the server; out on a game over, but not for a death it caused.
  ap_net::Event d;
  d.kind = ap_net::Event::kDeathLink;
  d.source = "Alice";
  events.push_back(d);
  ap::vblank();
  CHECK(ap::deathlink_pending());
  ap::on_game_over();
  CHECK(deaths.size() == 1);
  // Placeholders: another player's item at a placeholder is sent, with the window's message.
  CHECK(!ap::on_give_item(0x3FF));  // (a real item number: the game gives it)
  CHECK(ap::on_give_item(0x2000));  // (no location has it: skipped, nothing given)
  CHECK(ap::take_message_override() == nullptr);
  ap::stop();
}

TEST(ap_placeholders) {
  ap::stop();
  ap::Seed s;
  std::string error;
  std::string text = replace(kSeed, "\"id\": 1347569664,", "\"id\": 1347569664, \"placeholder\": 1024,");
  CHECK(ap::load_seed_text(text, &s, &error));
  CHECK(s.locations[1].placeholder == 1024);
  CHECK(seed_error(replace(text, "\"placeholder\": 1024", "\"placeholder\": 5"), "placeholder 0x400"));
  CHECK(seed_error(replace(replace(text, "\"id\": 1349650432,", "\"id\": 1349650432, \"placeholder\": 1024,"), "", ""),
                   "placeholder 1024 repeated"));
  ap::start_with(s);
  std::vector<int64_t> sent;
  int goals = 0;
  std::vector<std::string> deaths;
  std::vector<ap_net::Event> events;
  ap::connect(std::make_unique<FakeLink>(&sent, &goals, &deaths, &events));
  ap_net::Event c;
  c.kind = ap_net::Event::kConnected;
  c.seed_name = "12345678901234567890";
  events.push_back(c);
  ap::vblank();
  // The chest's give_item(1024): Alice's Hookshot is sent, the check goes out, and the
  // message window opening next shows it (once).
  CHECK(ap::on_give_item(1024));
  CHECK(ap::is_checked(1347569664) && sent.size() == 1 && sent[0] == 1347569664);
  const u8* m = ap::take_message_override();
  CHECK(m != nullptr);
  CHECK(ap::take_message_override() == nullptr);
  CHECK(!ap::on_give_money(5000));  // (a real amount: the game adds it)
  ap::stop();
  CHECK(!ap::on_give_item(1024));  // (off: the game's own op)
}

// [world agent] Several placeholders in one vblank (a gift event giving items and Fol, a
// prize table): one window page each; a loop reaching one placeholder again gives once; the
// 24-bit Fol range (the money routine's push imm24).
TEST(ap_placeholders_same_vblank) {
  ap::stop();
  ap::Seed s;
  std::string error;
  std::string text = replace(kSeed, "\"id\": 1347569664,", "\"id\": 1347569664, \"placeholder\": 1024,");
  text = replace(text, "\"id\": 1349650432,", "\"id\": 1349650432, \"placeholder\": \"0xf00000\",");
  CHECK(ap::load_seed_text(text, &s, &error));
  CHECK(s.locations[2].placeholder == 0xF00000);
  CHECK(seed_error(replace(text, "\"0xf00000\"", "\"0xeffff0\""), "placeholder 0x400"));
  // (The own item goes in the inventory: scratch buffers for it.)
  ss::game::field::FieldGlobals& g = ss::game::field::globals();
  u8* const old_inventory = g.inventory;
  std::vector<u8> inventory(0x1000, 0);
  g.inventory = inventory.data();
  ap::start_with(s);
  CHECK(ap::on_give_money(0xF00000));  // the gift's Fol site: its own item (2 Revival Cards)
  CHECK(ss::game::field::inventory_find(g.inventory, 21) != -1);
  CHECK(ap::on_give_item(1024));       // the chest: Alice's Hookshot
  CHECK(ap::on_give_item(1024));       // (a loop: ignored)
  CHECK(ap::on_give_money(0xFFFFFF));  // (in the range, no location: skipped)
  CHECK(ap::is_checked(1349650432) && ap::is_checked(1347569664));
  CHECK(ap::state().checked.size() == 2);
  const u8* m = ap::take_message_override();
  CHECK(m != nullptr);
  int pages = 1;
  for (size_t i = 0; i < 400;) {  // codes: one byte below 0x80, else two; 0x4003 has a parameter
    u32 c = m[i];
    if (c >= 0x80) {
      c = (c & 0x7F) | static_cast<u32>(m[i + 1]) << 7;
      i += 2;
    } else {
      ++i;
    }
    if (c == 0x4003) ++i;
    if (c == 0x4001) ++pages;  // a new page
    if (c == 0x4002) break;    // the end: wait for the button
  }
  CHECK(pages == 2);
  CHECK(ap::take_message_override() == nullptr);
  ap::stop();
  g.inventory = old_inventory;
}

// ---- Phase 3 [world agent]: characters, first drops, traps, multipliers, DeathLink's scenes ----

namespace {

// kSeed with the phase-3 options, a join site (map 208, Bowman's call at 0x6bc4), a drop
// location (enemy type 17), and placeholders holding own traps and a character.
std::string phase3_seed(const std::string& options) {
  std::string t = replace(kSeed, "\"options\": {\"protagonist\": \"claude\", \"goal\": \"indalecio\"}", "\"options\": " + options);
  t = replace(t, "  \"locations\": [\n",
              "  \"locations\": [\n"
              "    {\"id\": 1354055680, \"name\": \"Bowman joins\", \"check\": {\"type\": \"join\", \"map\": 208, \"offset\": \"0x6bc4\", \"character\": \"Bowman\"},\n"
              "     \"item\": {\"player\": 2, \"player_name\": \"Alice\", \"name\": \"Hookshot\"}},\n"
              "    {\"id\": 1362100241, \"name\": \"Goblin's first drop\", \"check\": {\"type\": \"drop\", \"enemy\": 17},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"item\", \"game_id\": 21, \"name\": \"Revival Card\"}},\n"
              "    {\"id\": 9001, \"name\": \"trap chest\", \"placeholder\": 1100, \"check\": {\"type\": \"chest\", \"map\": 18, \"flag\": 100},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"trap\", \"trap\": \"fol_loss\", \"percent\": 25}},\n"
              "    {\"id\": 9002, \"name\": \"poison chest\", \"placeholder\": 1101, \"check\": {\"type\": \"chest\", \"map\": 18, \"flag\": 101},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"trap\", \"trap\": \"status\"}},\n"
              "    {\"id\": 9003, \"name\": \"stone chest\", \"placeholder\": 1102, \"check\": {\"type\": \"chest\", \"map\": 18, \"flag\": 102},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"trap\", \"trap\": \"status\", \"status\": \"stone\"}},\n"
              "    {\"id\": 9004, \"name\": \"ambush chest\", \"placeholder\": 1103, \"check\": {\"type\": \"chest\", \"map\": 18, \"flag\": 103},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"trap\", \"trap\": \"ambush\"}},\n"
              "    {\"id\": 9005, \"name\": \"Celine chest\", \"placeholder\": 1104, \"check\": {\"type\": \"chest\", \"map\": 18, \"flag\": 104},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"character\", \"character\": 3}},\n");
  return t;
}

// Scratch game blocks for the field's globals (restored by the destructor).
struct ScratchGame {
  ss::game::field::FieldGlobals& g = ss::game::field::globals();
  u8 *party0 = g.party, *config0 = g.config, *state0 = g.game_state, *flags0 = g.script_flags, *inv0 = g.inventory;
  std::vector<u8> party = std::vector<u8>(0x300, 0), config = std::vector<u8>(0x2A0, 0),
                  state = std::vector<u8>(0x2A0, 0), flags = std::vector<u8>(0x170, 0), inv = std::vector<u8>(0x1000, 0);
  ScratchGame() {
    g.party = party.data();
    g.config = config.data();
    g.game_state = state.data();
    g.script_flags = flags.data();
    g.inventory = inv.data();
  }
  ~ScratchGame() {
    g.party = party0;
    g.config = config0;
    g.game_state = state0;
    g.script_flags = flags0;
    g.inventory = inv0;
  }
  void member(int slot, int id, int hp, u8 status = 0) {
    u8* p = party.data() + slot * 0x60;
    const s16 v = static_cast<s16>(id);
    std::memcpy(p, &v, 2);
    p[2] = status;
    std::memcpy(p + 0x1C, &hp, 4);
  }
  s32 fol() const {
    s32 v;
    std::memcpy(&v, config.data() + 0x18, 4);
    return v;
  }
};

}  // namespace

TEST(ap_phase3_seed) {
  ap::Seed s;
  std::string error;
  // Without the options: the original game.
  CHECK(ap::load_seed_text(kSeed, &s, &error));
  CHECK(!s.character_items && !s.first_drops && s.exp_multiplier == 1 && s.fol_multiplier == 1 && s.encounter_rate == 100);
  const std::string opts =
      "{\"character_items\": true, \"drops_as_checks\": \"first_drop\", \"exp_multiplier\": 3, \"fol_multiplier\": \"10\", "
      "\"encounter_rate\": 50, \"deathlink\": true}";
  CHECK(ap::load_seed_text(phase3_seed(opts), &s, &error));
  if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
  CHECK(s.character_items && s.first_drops && s.exp_multiplier == 3 && s.fol_multiplier == 10 && s.encounter_rate == 50);
  CHECK(s.locations[0].type == "join" && s.locations[0].map == 208 && s.locations[0].offset == 0x6bc4 && s.locations[0].character == 4);
  CHECK(s.locations[1].type == "drop" && s.locations[1].enemy == 17);
  CHECK(s.locations[2].item.item.kind == "trap" && s.locations[2].item.item.trap == "fol_loss" && s.locations[2].item.item.percent == 25);
  CHECK(s.locations[3].item.item.status == "poison" && s.locations[4].item.item.status == "stone");
  CHECK(s.locations[6].item.item.kind == "character" && s.locations[6].item.item.character == 3 && s.locations[6].item.item.name == "Celine");
  CHECK(std::string(ap::character_name(12)) == "Chisato" && ap::character_by_name("Leon") == 8 && ap::character_by_name("Zand") == 0);
  // drops_as_checks also as AP's option value (0 / 1).
  CHECK(ap::load_seed_text(phase3_seed("{\"drops_as_checks\": 1}"), &s, &error) && s.first_drops);
  // Out of range: the seed does not load.
  CHECK(seed_error(phase3_seed("{\"exp_multiplier\": 11}"), "options.exp_multiplier: 1-10"));
  CHECK(seed_error(phase3_seed("{\"fol_multiplier\": 0}"), "options.fol_multiplier: 1-10"));
  CHECK(seed_error(phase3_seed("{\"encounter_rate\": 201}"), "options.encounter_rate: 0-200"));
  CHECK(seed_error(phase3_seed("{\"drops_as_checks\": \"all\"}"), "drops_as_checks"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"character\": \"Bowman\"", "\"character\": 13"), "character 1-12"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"offset\": \"0x6bc4\", ", ""), "a join needs map and offset"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"enemy\": 17", "\"enemy\": -1"), "a drop needs its enemy"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"percent\": 25", "\"percent\": 0"), "percent 1-100"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"status\": \"stone\"", "\"status\": \"sleep\""), "status poison"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"trap\": \"ambush\"", "\"trap\": \"meteor\""), "trap ambush"));
}

TEST(ap_character_items) {
  ap::stop();
  ScratchGame sg;
  sg.member(0, 1, 100);  // Claude
  sg.member(1, 2, 100);  // Rena
  const s16 map = 208;
  std::memcpy(sg.state.data() + 0x1A, &map, 2);
  ap::Seed s;
  std::string error;
  // Off: every join is the game's.
  CHECK(ap::load_seed_text(phase3_seed("{}"), &s, &error));
  ap::start_with(s);
  CHECK(!ap::on_party_add(3, 0x6bc4, -1));
  CHECK(!ap::is_checked(1354055680));
  // On: the listed site (the call into the join routine, or an inline party_add) records its
  // check and keeps Bowman out until his item comes; other sites and maps join as before.
  CHECK(ap::load_seed_text(phase3_seed("{\"character_items\": true}"), &s, &error));
  ap::start_with(s);
  CHECK(!ap::on_party_add(3, 0x100, 0x6bc8));  // (another site)
  CHECK(!ap::is_checked(1354055680));
  CHECK(ap::on_party_add(3, 0x1234, 0x6bc4));  // the call at 0x6bc4
  CHECK(ap::is_checked(1354055680) && ap::state().checked.size() == 1);
  CHECK(ap::on_party_add(3, 0x6bc4, -1));      // inline at 0x6bc4: checked once, still held back
  CHECK(ap::state().checked.size() == 1);
  const s16 other = 209;
  std::memcpy(sg.state.data() + 0x1A, &other, 2);
  CHECK(!ap::on_party_add(3, 0x1234, 0x6bc4));  // (same offset on another map)
  std::memcpy(sg.state.data() + 0x1A, &map, 2);
  // Received (Celine from a placeholder here: an own item): waiting for free play, kept in
  // the sidecar.
  CHECK(ap::on_give_item(1104));
  CHECK(ap::state().characters.size() == 1 && ap::state().characters[0] == 3);
  CHECK(ap::state().characters_waiting.size() == 1);
  CHECK(ap::status_text().find("Celine (waiting)") != std::string::npos);
  // Bowman received: the site's own join goes ahead (room in the party) and he stops waiting.
  CHECK(ap::receive_by_name_or_id("Bowman", "Alice", &error));
  CHECK(ap::state().characters.size() == 1);  // (queued, not given: no free play in a test)
  ap::SaveState st = ap::state();
  st.characters.push_back(4);
  st.characters_waiting.push_back(4);
  const fs::path dir = scratch("phase3");
  const std::string card = (dir / "memcard1.mcd").string();
  CHECK(ap::write_sidecar(ap::sidecar_path(card, 0), st, &error));
  ap::on_loaded(card, 0);
  CHECK(ap::state().characters.size() == 2 && ap::state().characters_waiting.size() == 2);
  CHECK(!ap::on_party_add(3, 0x1234, 0x6bc4));  // received: the game's join
  CHECK(ap::state().characters_waiting.size() == 1 && ap::state().characters_waiting[0] == 3);
  // A full party (8 slots, members in reserve included): the join waits.
  for (int i = 2; i < 8; ++i) sg.member(i, i == 3 ? -(i + 1) : i + 1, 100);
  ap::SaveState st2 = ap::state();
  st2.characters_waiting.push_back(9);
  st2.characters.push_back(9);
  CHECK(ap::write_sidecar(ap::sidecar_path(card, 1), st2, &error));
  ap::on_loaded(card, 1);
  CHECK(ap::party_has_character(4) && !ap::party_has_character(9));
  // Sidecar round trip of the phase-3 fields.
  st2.first_drops = {17, 3};
  st2.ambushes = 2;
  ap::SaveState back;
  CHECK(ap::sidecar_from_json(ap::sidecar_json(st2), &back, &error));
  CHECK(back.characters == st2.characters && back.characters_waiting == st2.characters_waiting);
  CHECK(back.first_drops == st2.first_drops && back.ambushes == 2);
  ap::stop();
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// A received member is recruited for good (docs/ap/seed_format.md "Character items"): a
// received Celine keeps script flag 112 (Mars then only sends her home for a while, and map 125
// brings her back); the story's partings for good (Cross Cave's choice 1, map 524, the call at
// 0x72bc) put a received member back in the join queue; a temporary leave (map 126's, 0x6710)
// drops a join still waiting. Without a seed, or without the item, nothing changes.
TEST(ap_received_members_stay) {
  ap::stop();
  ScratchGame sg;
  sg.member(0, 1, 100);  // Claude
  sg.member(1, 2, 100);  // Rena
  sg.member(2, 3, 100);  // Celine
  auto at_map = [&](int m) {
    const s16 v = static_cast<s16>(m);
    std::memcpy(sg.state.data() + 0x1A, &v, 2);
  };
  auto flag112 = [&] { return (sg.flags[112 >> 3] >> (112 & 7) & 1) != 0; };
  at_map(524);
  // No seed: the original.
  CHECK(!ap::keeps_flag(112));
  ap::on_party_remove(2, 0x6418, 0x72bc);
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(phase3_seed("{\"character_items\": true}"), &s, &error));
  ap::start_with(s);
  // Celine not received: the parting is the game's, flag 112 is the scripts'.
  CHECK(!ap::keeps_flag(112));
  ap::on_party_remove(2, 0x6418, 0x72bc);
  CHECK(ap::state().characters_waiting.empty() && !flag112());
  // Received (a placeholder of ours): flag 112 set at once and kept.
  CHECK(ap::on_give_item(1104));
  CHECK(flag112() && ap::keeps_flag(112) && !ap::keeps_flag(113));
  // Joined (the sidecar of a save where she is in the party): Cross Cave's choice 1 re-queues her.
  const fs::path dir = scratch("members");
  const std::string card = (dir / "memcard1.mcd").string();
  ap::SaveState st = ap::state();
  st.characters_waiting.clear();
  CHECK(ap::write_sidecar(ap::sidecar_path(card, 0), st, &error));
  ap::on_loaded(card, 0);
  CHECK(ap::state().characters_waiting.empty());
  ap::on_party_remove(2, 0x100, 0x7000);  // (another call on the map: not a parting)
  CHECK(ap::state().characters_waiting.empty());
  ap::on_party_remove(2, 0x6418, 0x72bc);
  CHECK(ap::state().characters_waiting.size() == 1 && ap::state().characters_waiting[0] == 3);
  ap::on_party_remove(2, 0x6418, 0x72bc);  // (once)
  CHECK(ap::state().characters_waiting.size() == 1);
  // A temporary leave (Rena's game, map 126) drops the join still waiting: the story's comes.
  at_map(126);
  ap::on_party_remove(2, 0x6418, 0x6710);
  CHECK(ap::state().characters_waiting.empty());
  // A member already in reserve is not removed again: nothing changes.
  sg.member(2, -3, 100);
  at_map(524);
  ap::on_party_remove(2, 0x6418, 0x72bc);
  CHECK(ap::state().characters_waiting.empty());
  // Another character at Celine's parting site: nothing (the site is per member).
  sg.member(3, 9, 100);  // Opera
  ap::on_party_remove(8, 0x6418, 0x72bc);
  CHECK(ap::state().characters_waiting.empty());
  ap::stop();
  CHECK(!ap::keeps_flag(112));
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(ap_drops_rewards_encounters) {
  ap::stop();
  ScratchGame sg;
  ap::Seed s;
  std::string error;
  // No seed: the game's values.
  s32 exp = 1234, fol = 567;
  ap::scale_battle_rewards(&exp, &fol);
  CHECK(exp == 1234 && fol == 567);
  CHECK(ap::on_enemy_drop(17, 5));
  CHECK(!ap::random_encounters_off() && ap::scale_encounter_distance(4000) == 4000);
  // A seed with the options at their defaults: the same.
  CHECK(ap::load_seed_text(phase3_seed("{}"), &s, &error));
  ap::start_with(s);
  ap::scale_battle_rewards(&exp, &fol);
  CHECK(exp == 1234 && fol == 567);
  CHECK(ap::on_enemy_drop(17, 5) && !ap::is_checked(1362100241));
  CHECK(!ap::random_encounters_off() && ap::scale_encounter_distance(4000) == 4000);
  // On.
  CHECK(ap::load_seed_text(phase3_seed("{\"drops_as_checks\": \"first_drop\", \"exp_multiplier\": 3, \"fol_multiplier\": 10, "
                                       "\"encounter_rate\": 200}"),
                           &s, &error));
  ap::start_with(s);
  ap::scale_battle_rewards(&exp, &fol);
  CHECK(exp == 3702 && fol == 5670);
  s32 big = 900000000, big_fol = 1;
  ap::scale_battle_rewards(&big, &big_fol);
  CHECK(big == 999999999);
  CHECK(ap::on_enemy_drop(16, 5));   // (no location for type 16)
  CHECK(!ap::on_enemy_drop(17, 5));  // the first drop of type 17: the check, not the item
  CHECK(ap::is_checked(1362100241) && ap::state().first_drops.size() == 1 && ap::state().first_drops[0] == 17);
  CHECK(ap::pending_count() == 1);   // (its own Revival Card, given in free play)
  CHECK(ap::on_enemy_drop(17, 5));   // later drops: the game's
  CHECK(ap::scale_encounter_distance(4000) == 2000 && !ap::random_encounters_off());
  CHECK(ap::load_seed_text(phase3_seed("{\"encounter_rate\": 0}"), &s, &error));
  ap::start_with(s);
  CHECK(ap::random_encounters_off());
  CHECK(ap::load_seed_text(phase3_seed("{\"encounter_rate\": 50}"), &s, &error));
  ap::start_with(s);
  CHECK(ap::scale_encounter_distance(4000) == 8000);
  ap::stop();
}

TEST(ap_traps) {
  ap::stop();
  ScratchGame sg;
  sg.member(0, 1, 100);
  sg.member(1, 2, 100);
  sg.member(2, 3, 0, 1);  // Celine is down
  const s32 fol = 1000;
  std::memcpy(sg.config.data() + 0x18, &fol, 4);
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(phase3_seed("{}"), &s, &error));
  ap::start_with(s);
  CHECK(ap::on_give_item(1100));  // Fol loss 25%
  CHECK(sg.fol() == 750 && ap::is_checked(9001));
  CHECK(ap::on_give_item(1101));  // poison: every active member who is up
  CHECK(sg.party[2] == 8 && sg.party[0x60 + 2] == 8 && sg.party[0xC0 + 2] == 1);
  CHECK(ap::on_give_item(1102));  // stone: one member (the last active one up)
  CHECK(sg.party[0x60 + 2] == (8 | 4) && sg.party[2] == 8);
  // Stone again: Claude is the last one up, so poison instead (already poisoned).
  CHECK(ap::receive_by_name_or_id("trap:status:stone", "", &error));
  CHECK(ap::on_give_item(1103));  // an ambush: waits for the field
  CHECK(ap::state().ambushes == 1);
  CHECK(ap::status_text().find("ambushes waiting 1") != std::string::npos);
  CHECK(!ap::receive_by_name_or_id("trap:meteor", "", &error));
  CHECK(!ap::receive_by_name_or_id("trap:status:sleep", "", &error));
  ap::stop();
}

TEST(ap_deathlink_scenes) {
  ap::stop();
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(replace(kSeed, "\"options\": {\"protagonist\": \"claude\", \"goal\": \"indalecio\"}",
                                   "\"options\": {\"deathlink\": true}"),
                           &s, &error));
  ap::start_with(s);
  // On the title and in the credits there is no game to end: dropped (and one waiting goes).
  ap::on_program(ap::Scene::kTitle);
  ap::deathlink_receive("Alice", "");
  CHECK(!ap::deathlink_pending());
  ap::on_program(ap::Scene::kField);
  ap::deathlink_receive("Alice", "");
  CHECK(ap::deathlink_pending());
  ap::on_program(ap::Scene::kBattle);  // (a battle transition: it waits)
  CHECK(ap::deathlink_pending());
  ap::on_program(ap::Scene::kCredits);
  CHECK(!ap::deathlink_pending());
  ap::deathlink_receive("Alice", "");
  CHECK(!ap::deathlink_pending());
  // A game over takes one waiting with it; a load or a new game starts clean.
  ap::on_program(ap::Scene::kField);
  ap::deathlink_receive("Alice", "");
  ap::on_game_over();
  CHECK(!ap::deathlink_pending());
  ap::deathlink_receive("Alice", "");
  ap::on_new_game();
  CHECK(!ap::deathlink_pending());
  // After the final boss (539 / 540, whatever the goal): the ending, nothing more.
  ap::deathlink_receive("Alice", "");
  ap::on_battle_exit(540, 0);
  CHECK(!ap::deathlink_pending());
  ap::deathlink_receive("Alice", "");
  CHECK(!ap::deathlink_pending());
  ap::on_new_game();
  ap::deathlink_receive("Alice", "");
  CHECK(ap::deathlink_pending());
  ap::on_battle_exit(539, 2);  // (a lost final fight is no ending)
  CHECK(ap::deathlink_pending());
  ap::stop();
}

// The boss checks of the fights that can't be won [deathlink agent]: a fight the script lets
// the party lose (field_flags +0x7E = 0; Dias 204, Shin on the ship 517, Marsilio 530) is
// checked on the scripted defeat, a timed survival (Shin 516, Berle 520) on the time-up; the
// goal still needs a victory. Outcomes: 0 victory, 1 escaped, 2 wiped out, 3 ran off, 4 time up.
namespace {

std::string scripted_loss_seed() {
  std::string t = replace(kSeed, "\"options\": {\"protagonist\": \"claude\", \"goal\": \"indalecio\"}",
                          "\"options\": {\"deathlink\": true}, \"goal\": {\"encounters\": [540]}");
  return replace(t, "{\"id\": 1, \"name\": \"Cyril\",",
                 "{\"id\": 204, \"name\": \"Dias\", \"check\": {\"type\": \"boss\", \"encounter\": 204},"
                 " \"item\": {\"player\": 1, \"kind\": \"fol\", \"amount\": 1}},\n"
                 "    {\"id\": 517, \"name\": \"Shin (ship)\", \"check\": {\"type\": \"boss\", \"encounter\": 517},"
                 " \"item\": {\"player\": 1, \"kind\": \"fol\", \"amount\": 1}},\n"
                 "    {\"id\": 516, \"name\": \"Shin (front line)\", \"check\": {\"type\": \"boss\", \"encounter\": 516},"
                 " \"item\": {\"player\": 1, \"kind\": \"fol\", \"amount\": 1}},\n"
                 "    {\"id\": 191, \"name\": \"Alen-Tax\", \"check\": {\"type\": \"boss\", \"encounter\": 191},"
                 " \"item\": {\"player\": 1, \"kind\": \"fol\", \"amount\": 1}},\n"
                 "    {\"id\": 1, \"name\": \"Cyril\",");
}

}  // namespace

TEST(ap_boss_checks_scripted_loss) {
  ap::stop();
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(scripted_loss_seed(), &s, &error));
  if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
  ap::start_with(s);
  // A fight whose loss is a game over: the wipe checks nothing, nor does an escape.
  ap::on_battle_exit(191, 2);
  ap::on_battle_exit(191, 1);
  ap::on_battle_exit(191, 3);
  CHECK(!ap::is_checked(191));
  // A fight the script lets the party lose: an escape or the race lost still checks nothing...
  ap::note_battle_lose_allowed(true);
  ap::on_battle_exit(204, 1);
  ap::note_battle_lose_allowed(true);
  ap::on_battle_exit(204, 3);
  CHECK(!ap::is_checked(204));
  // ... the flag is that battle's only (the exit clears it) ...
  ap::on_battle_exit(204, 2);
  CHECK(!ap::is_checked(204));
  // ... and the scripted defeat checks it, its own item given in free play.
  const size_t before = ap::pending_count();
  ap::note_battle_lose_allowed(true);
  ap::on_battle_exit(204, 2);
  CHECK(ap::is_checked(204) && ap::pending_count() == before + 1);
  // A victory there checks it too (the tournament's Dias: a win starts the fight again).
  ap::on_battle_exit(517, 0);
  CHECK(ap::is_checked(517));
  // A timed survival: the time-up is the win, whatever the flag.
  ap::on_battle_exit(516, 4);
  CHECK(ap::is_checked(516));
  // A victory, as before.
  ap::on_battle_exit(191, 0);
  CHECK(ap::is_checked(191));
  // The goal needs the victory: not a scripted loss, not a time-up.
  ap::note_battle_lose_allowed(true);
  ap::on_battle_exit(540, 2);
  ap::on_battle_exit(540, 4);
  CHECK(!ap::state().goal_done);
  ap::on_battle_exit(540, 0);
  CHECK(ap::state().goal_done);
  ap::stop();
}

TEST(ap_deathlink_lose_allowed_battle) {
  ap::stop();
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(scripted_loss_seed(), &s, &error));
  ap::start_with(s);
  std::vector<int64_t> sent;
  int goals = 0;
  std::vector<std::string> deaths;
  std::vector<ap_net::Event> events;
  ap::connect(std::make_unique<FakeLink>(&sent, &goals, &deaths, &events));
  ap_net::Event c;
  c.kind = ap_net::Event::kConnected;
  c.seed_name = "12345678901234567890";
  events.push_back(c);
  ap::vblank();
  CHECK(ap::connected());
  // A fight in progress, no party units, no state block (the latch keeps what it was told).
  battle::BattleGlobals& g = battle::bg();
  const auto saved_state = g.state;
  const auto saved_flags = g.flags;
  auto* const saved_handoff = g.handoff;
  const auto saved_units = g.party_units;
  g.state = battle::kStateFighting;
  g.flags = 0;
  g.handoff = nullptr;
  g.party_units.fill(nullptr);
  ap::on_program(ap::Scene::kBattle);
  // Shin on the ship (lose-allowed): a DeathLink received in the fight waits for the field;
  // the scripted defeat checks the boss and sends nothing (no game over follows).
  ap::note_battle_lose_allowed(true);
  ap::deathlink_receive("Alice", "");
  ap::battle_frame();
  CHECK(ap::deathlink_pending());
  ap::on_battle_exit(517, 2);
  CHECK(ap::is_checked(517) && ap::deathlink_pending() && deaths.empty());
  // Shin at the front line (a loss is a game over): the DeathLink fells the party there; the
  // loss checks nothing and its game over sends no DeathLink back.
  ap::on_program(ap::Scene::kBattle);
  ap::battle_frame();
  CHECK(!ap::deathlink_pending());
  ap::on_battle_exit(516, 2);
  ap::on_game_over();
  CHECK(!ap::is_checked(516) && deaths.empty());
  // A wipe of its own there is a game over that sends one.
  ap::on_program(ap::Scene::kBattle);
  ap::battle_frame();
  ap::on_battle_exit(516, 2);
  ap::on_game_over();
  CHECK(deaths.size() == 1 && !ap::is_checked(516));
  g.state = saved_state;
  g.flags = saved_flags;
  g.handoff = saved_handoff;
  g.party_units = saved_units;
  ap::stop();
}

// ---- Phase 4: the open world (doors, keys, the Expel teleporter), shop slots ----

namespace {

// kSeed with phase-4 options, a key item (Salva Pass, key 3), a chest placeholder holding it,
// two shop slots (one ours, one Alice's) and three doors: the Expel world map's Salva entrance
// (chunk script 909 0x3c78, routine 0x3c6c) needing key 3; Arlia's door to map 18 (17 0x2054)
// sent to map 23; and a door on a word that is no map transfer.
std::string phase4_seed(const std::string& options) {
  std::string t = replace(kSeed, "\"options\": {\"protagonist\": \"claude\", \"goal\": \"indalecio\"}", "\"options\": " + options);
  t = replace(t, "\"5009000\":", "\"5300003\": {\"name\": \"Salva Pass\", \"kind\": \"key\", \"key\": 3},\n    \"5009000\":");
  t = replace(t, "  \"locations\": [\n",
              "  \"locations\": [\n"
              "    {\"id\": 9101, \"name\": \"key chest\", \"placeholder\": 1105, \"check\": {\"type\": \"chest\", \"map\": 18, \"flag\": 105},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"key\", \"key\": 3, \"name\": \"Salva Pass\"}},\n"
              "    {\"id\": 1365246466, \"name\": \"Arlia shop: Lavender\", \"check\": {\"type\": \"shop\", \"shop\": 1, \"index\": 2, \"item\": 224},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"item\", \"game_id\": 21, \"name\": \"Revival Card\"}},\n"
              "    {\"id\": 1365246464, \"name\": \"Arlia shop: Sandals\", \"check\": {\"type\": \"shop\", \"shop\": 1, \"index\": 0},\n"
              "     \"item\": {\"player\": 2, \"player_name\": \"Alice\", \"name\": \"Hookshot\"}},\n");
  t = replace(t, "  \"patches\":",
              "  \"doors\": [\n"
              "    {\"id\": 59576094, \"routine\": \"0x3c6c\", \"key\": 3, \"name\": \"Expel -> Salva\"},\n"
              "    {\"map\": 17, \"offset\": \"0x2054\", \"routine\": \"0x204c\", \"to\": {\"map\": 23, \"x\": 224, \"y\": 0, \"z\": 106, \"dir\": 198}},\n"
              "    {\"map\": 17, \"offset\": \"0x2060\", \"key\": 3}\n"
              "  ],\n"
              "  \"patches\":");
  return t;
}

// A map script with map_transfer_c words at the given byte offsets of its code.
std::vector<u8> script_with_transfers(std::initializer_list<int> offsets) {
  std::vector<u8> s(0x1C + 0x4000, 0);
  for (const int at : offsets) {
    const u32 w = 0xDA000011u;
    std::memcpy(s.data() + 0x1C + at, &w, 4);
  }
  return s;
}

}  // namespace

TEST(ap_phase4_seed) {
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(kSeed, &s, &error));
  CHECK(!s.open_world && !s.expel_teleporter && s.shuffle_shops == 0 && s.doors.empty());
  CHECK(ap::load_seed_text(phase4_seed("{\"world_mode\": \"open_world\", \"shuffle_shops\": \"specials\"}"), &s, &error));
  if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
  CHECK(s.open_world && s.expel_teleporter && s.shuffle_shops == 1);
  CHECK(s.items[5300003].kind == "key" && s.items[5300003].key == 3);
  CHECK(s.locations[1].type == "shop" && s.locations[1].shop == 1 && s.locations[1].index == 2 && s.locations[1].shop_item == 224);
  CHECK(s.locations[2].shop_item == -1);
  CHECK(s.doors.size() == 3);
  // A door by id: map 909, offset 0x3c78 (909 * 65536 + 0x3c78 / 4); by map and offset: its id.
  CHECK(s.doors[0].map == 909 && s.doors[0].offset == 0x3C78 && s.doors[0].routine == 0x3C6C && s.doors[0].key == 3);
  CHECK(s.doors[1].id == ap::door_id(17, 0x2054) && s.doors[1].has_target && s.doors[1].to.map == 23 && s.doors[1].to.dir == 198);
  CHECK(ap::load_seed_text(phase4_seed("{\"world_mode\": \"open_world\", \"expel_teleporter\": false}"), &s, &error) &&
        s.open_world && !s.expel_teleporter);
  CHECK(ap::load_seed_text(phase4_seed("{\"world_mode\": 1, \"shuffle_shops\": 2}"), &s, &error) && s.open_world && s.shuffle_shops == 2);
  CHECK(seed_error(phase4_seed("{\"world_mode\": \"chaos\"}"), "options.world_mode"));
  CHECK(seed_error(phase4_seed("{\"shuffle_shops\": \"all\"}"), "options.shuffle_shops"));
  CHECK(seed_error(replace(phase4_seed("{}"), "\"kind\": \"key\", \"key\": 3}", "\"kind\": \"key\", \"key\": 256}"), "key 1-255"));
  CHECK(seed_error(replace(phase4_seed("{}"), "\"shop\": 1, \"index\": 2", "\"shop\": 1"), "a shop slot needs"));
  CHECK(seed_error(replace(phase4_seed("{}"), "{\"id\": 59576094,", "{\"id\": 59576094, \"map\": 18,"), "is not map * 65536"));
  CHECK(seed_error(replace(phase4_seed("{}"), "\"offset\": \"0x2060\"", "\"offset\": \"0x2054\""), "repeated"));
  CHECK(seed_error(replace(phase4_seed("{}"), "\"offset\": \"0x2060\"", "\"offset\": \"0x2061\""), "a door needs"));
  CHECK(seed_error(replace(phase4_seed("{}"), "{\"map\": 23, ", "{"), "to needs map"));
}

TEST(ap_doors_and_keys) {
  ap::stop();
  ScratchGame sg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(phase4_seed("{\"world_mode\": \"open_world\"}"), &s, &error));
  ap::start_with(s);
  // The Expel teleporter's edits join the seed's patch lines.
  int teleporter = 0;
  for (const std::string& l : ap::seed_patch_lines()) teleporter += l.rfind("script 303 0x1307", 0) == 0 || l.rfind("script 18 0x996", 0) == 0;
  CHECK(teleporter == 2);
  // The world map: chunk 915 runs script 909. The Salva entrance is locked without key 3.
  ap::on_script_loaded(915, script_with_transfers({0x3C78}));
  CHECK(ap::door_blocked_at_trigger(0x3C6C));
  CHECK(!ap::door_blocked_at_trigger(0x3C18));  // (another routine)
  s32 args[7] = {43, 128, -1960, 0, 906, 315, 128};
  CHECK(ap::on_map_transfer(0x3C78, args));     // (reached another way: skipped too)
  // The key (a chest's placeholder): the door opens; its arguments stay the game's.
  CHECK(ap::on_give_item(1105) && ap::has_key(3) && ap::is_checked(9101));
  CHECK(!ap::door_blocked_at_trigger(0x3C6C));
  CHECK(!ap::on_map_transfer(0x3C78, args) && args[0] == 43 && args[2] == -1960);
  CHECK(ap::status_text().find("keys Salva Pass") != std::string::npos);
  // Map 17: a door with a target is sent there; a door on a word that is no transfer is ignored.
  ap::on_script_loaded(17, script_with_transfers({0x2054}));
  s32 b[7] = {18, 0, 230, 0, -133, 0, 128};
  CHECK(!ap::on_map_transfer(0x2054, b) && b[0] == 23 && b[2] == 224 && b[4] == 106 && b[5] == 198 && b[6] == 128);
  CHECK(!ap::door_blocked_at_trigger(0x204C));
  CHECK(ap::status_text().find("(1 ignored)") != std::string::npos);
  // (A door elsewhere does not apply here.)
  s32 c[7] = {43, 128, -1960, 0, 906, 315, 128};
  CHECK(!ap::on_map_transfer(0x3C78, c) && c[0] == 43);
  // The sidecar keeps the keys.
  ap::SaveState back;
  CHECK(ap::sidecar_from_json(ap::sidecar_json(ap::state()), &back, &error) && back.keys == std::vector<int>{3});
  ap::stop();
  // Story mode: keys never lock a door, and no teleporter edits.
  CHECK(ap::load_seed_text(phase4_seed("{}"), &s, &error));
  ap::start_with(s);
  CHECK(ap::seed_patch_lines().size() == s.patches.size());
  ap::on_script_loaded(909, script_with_transfers({0x3C78}));
  CHECK(!ap::door_blocked_at_trigger(0x3C6C));
  s32 d[7] = {43, 128, -1960, 0, 906, 315, 128};
  CHECK(!ap::on_map_transfer(0x3C78, d) && d[0] == 43);
  ap::stop();
  // No seed: nothing changes.
  CHECK(!ap::door_blocked_at_trigger(0x3C6C) && !ap::on_map_transfer(0x3C78, d) && ap::on_shop_buy(1, 2, 224, 3) == 3);
}

TEST(ap_shop_slots) {
  ap::stop();
  ScratchGame sg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(phase4_seed("{\"shuffle_shops\": \"full\"}"), &s, &error));
  ap::start_with(s);
  CHECK(ap::shop_slot_open(1, 2, 224) && !ap::shop_slot_open(1, 2, 225) && !ap::shop_slot_open(1, 1, 221));
  // Another item at the entry (the seed expected Lavender): not the slot.
  CHECK(ap::on_shop_buy(1, 2, 225, 2) == 2 && !ap::is_checked(1365246466));
  // Three Lavender: the first is the check (our Revival Card), the game gives two.
  CHECK(ap::on_shop_buy(1, 2, 224, 3) == 2 && ap::is_checked(1365246466));
  CHECK(field::inventory_find(sg.g.inventory, 21) >= 0);
  CHECK(!ap::shop_slot_open(1, 2, 224) && ap::on_shop_buy(1, 2, 224, 1) == 1);
  // Alice's slot: one copy bought, none given, the check sent.
  CHECK(ap::on_shop_buy(1, 0, 728, 1) == 0 && ap::is_checked(1365246464));
  ap::stop();
}
