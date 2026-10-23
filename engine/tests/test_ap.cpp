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
