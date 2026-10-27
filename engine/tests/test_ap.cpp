// Archipelago, phase 1 (game/ap; docs/ap/seed_format.md): the JSON reader, the seed loader,
// checks and the receive queue, and the save sidecar. Files go in test_ap_scratch/ next to
// the test program. No disc needed.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "ap_net/link.h"
#include "game/ap/ap.h"
#include "game/ap/json.h"
#include "game/ap/skills.h"
#include "game/battle/battle.h"
#include "game/field/field.h"
#include "game/field/item_effect.h"
#include "game/field/script_vm.h"
#include "game/field/special.h"
#include "test.h"
#include "util/sha256.h"

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
  "options": {"story_route": "claude", "goal": "indalecio"},
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
  CHECK(seed_error(replace(kSeed, "\"version\": 1", "\"version\": 4"), "unsupported version 4 (this build reads 1 to 3)"));
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

// The save guard [hubcore agent] (docs/ap/seed_format.md "The save sidecar"): with a seed, a
// load needs this seed's sidecar holding the hash of the save as it is on the card.
TEST(ap_save_guard) {
  // SHA-256 (FIPS 180-2 examples: one block, two blocks, empty).
  const std::string abc = "abc", two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  CHECK(util::sha256_hex(reinterpret_cast<const u8*>(abc.data()), abc.size()) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(util::sha256_hex(reinterpret_cast<const u8*>(two.data()), two.size()) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(util::sha256_hex(nullptr, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  ap::stop();
  std::string error;
  // A save as the card holds it: its length (0x1000) at +0x21a, the summary's +0x1a.
  std::vector<u8> save(0x2000);
  for (size_t i = 0; i < save.size(); ++i) save[i] = static_cast<u8>(i * 7 + 3);
  save[0x21A] = 0x00;
  save[0x21B] = 0x10;
  const std::string digest = util::save_digest(save.data(), save.size());
  CHECK(digest == "sha256:" + util::sha256_hex(save.data(), 0x1000));
  const fs::path dir = scratch("guard");
  const std::string card = (dir / "memcard1.mcd").string();
  CHECK(ap::load_allowed(card, 0, save.data(), save.size()));  // no seed: any save

  ap::Seed s;
  CHECK(ap::load_seed_text(kSeed, &s, &error));
  ap::start_with(s);
  CHECK(!ap::load_allowed(card, 0, save.data(), save.size()));  // an ordinary save (no sidecar)
  // This seed's save: the sidecar (version 2) holds its hash, and it loads.
  ap::on_saved(card, 0, save.data(), save.size());
  ap::SaveState back;
  CHECK(ap::read_sidecar(ap::sidecar_path(card, 0), &back, &error));
  CHECK(back.save_hash == digest);
  {
    std::ifstream in(ap::sidecar_path(card, 0));
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("\"version\": 2") != std::string::npos && text.find(digest) != std::string::npos);
  }
  CHECK(ap::load_allowed(card, 0, save.data(), save.size()));
  CHECK(ap::state().save_hash.empty());  // (never in the running state)
  // An edited byte in the save: refused; a byte past its length is not the save's.
  save[0x400] ^= 1;
  CHECK(!ap::load_allowed(card, 0, save.data(), save.size()));
  save[0x400] ^= 1;
  save[0x1800] ^= 1;
  CHECK(ap::load_allowed(card, 0, save.data(), save.size()));
  save[0x1800] ^= 1;
  // Another seed's save, another slot's: refused.
  ap::SaveState other = back;
  other.seed = "99999";
  CHECK(ap::write_sidecar(ap::sidecar_path(card, 1), other, &error));
  CHECK(!ap::load_allowed(card, 1, save.data(), save.size()));
  other = back;
  other.slot_name = "Rena";
  CHECK(ap::write_sidecar(ap::sidecar_path(card, 2), other, &error));
  CHECK(!ap::load_allowed(card, 2, save.data(), save.size()));
  // No hash (an earlier version's sidecar), or the field stripped from this one: refused.
  other = back;
  other.save_hash.clear();
  CHECK(ap::write_sidecar(ap::sidecar_path(card, 3), other, &error));
  CHECK(!ap::load_allowed(card, 3, save.data(), save.size()));
  ap::Json stripped = ap::sidecar_json(back);
  ap::Json copy = ap::Json::object();
  for (const auto& [k, v] : stripped.items())
    if (k != "save_hash") copy.set(k, v);
  {
    std::ofstream out(ap::sidecar_path(card, 4));
    out << copy.dump();
  }
  CHECK(ap::read_sidecar(ap::sidecar_path(card, 4), &back, &error) && back.save_hash.empty());
  CHECK(!ap::load_allowed(card, 4, save.data(), save.size()));
  // A wrong hash (the sidecar edited to another save's): refused.
  other = back;
  other.save_hash = "sha256:" + util::sha256_hex(save.data(), 0x0FFF);
  CHECK(ap::write_sidecar(ap::sidecar_path(card, 5), other, &error));
  CHECK(!ap::load_allowed(card, 5, save.data(), save.size()));
  ap::stop();
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// Items still waiting across a save and a load [pendingsave agent] (docs/ap/seed_format.md "The
// save sidecar", `pending`): a check's items queue up for free play; a save keeps the queue in the
// sidecar (version 3, guarded by pending_hash), a load in a new session gives them again, and
// nothing is given twice (a second load, a reconnect). A load of a save from before a check owes
// its own item unless the check is a chest or a gift (the loaded game opens it again).
namespace {
const char* kPendingSeed = R"seed({
  "format": "apso2", "version": 1, "seed": "waiting-1", "slot": {"name": "Claude", "id": 1},
  "items": {"5000021": {"name": "Revival Card", "kind": "item", "game_id": 21},
            "5300003": {"name": "Hilton Pass", "kind": "key", "key": 3}},
  "locations": [
    {"id": 2001, "name": "reward 1", "check": {"type": "boss", "encounter": 19},
     "item": {"player": 1, "kind": "item", "game_id": 21, "name": "Revival Card"}},
    {"id": 2002, "name": "reward 2", "check": {"type": "boss", "encounter": 19},
     "item": {"player": 1, "kind": "key", "key": 3, "name": "Hilton Pass"}},
    {"id": 2003, "name": "reward 3", "check": {"type": "boss", "encounter": 19},
     "item": {"player": 1, "kind": "fol", "amount": 300}},
    {"id": 2004, "name": "reward 4", "check": {"type": "boss", "encounter": 19},
     "item": {"player": 2, "player_name": "Alice", "name": "Hookshot", "ap_id": 77}},
    {"id": 2005, "name": "a crate", "check": {"type": "chest", "map": 18, "flag": 2401},
     "item": {"player": 1, "kind": "item", "game_id": 23}}
  ]})seed";

std::string read_text(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
void write_text(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}
}  // namespace

TEST(ap_pending_save) {
  ap::stop();
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(kPendingSeed, &s, &error));
  std::vector<u8> save(0x2000);  // a save as the card holds it (length 0x1000 at +0x21a)
  for (size_t i = 0; i < save.size(); ++i) save[i] = static_cast<u8>(i * 5 + 1);
  save[0x21A] = 0x00;
  save[0x21B] = 0x10;
  const fs::path dir = scratch("pending");
  const std::string card = (dir / "memcard1.mcd").string();

  // Nothing waiting: version 2 and no new key (the sidecar as before).
  ap::start_with(s);
  ap::on_saved(card, 0, save.data(), save.size());
  const std::string empty_text = read_text(ap::sidecar_path(card, 0));
  CHECK(empty_text.find("\"version\": 2") != std::string::npos && empty_text.find("\"pending") == std::string::npos);

  // The boss's four checks: three own items queue up (the fourth is Alice's), a save keeps them.
  ap::on_battle_exit(19, 0);
  CHECK(ap::is_checked(2001) && ap::is_checked(2004) && ap::pending_count() == 3);
  ap::on_saved(card, 1, save.data(), save.size());
  const std::string text = read_text(ap::sidecar_path(card, 1));
  CHECK(text.find("\"version\": 3") != std::string::npos);
  CHECK(text.find("\"pending_hash\": \"sha256:") != std::string::npos);
  ap::SaveState back;
  CHECK(ap::read_sidecar(ap::sidecar_path(card, 1), &back, &error));
  CHECK(back.pending.size() == 3 && back.pending[0].location == 2001 && back.pending[2].location == 2003);
  CHECK(ap::state().pending.empty() && ap::state().pending_hash.empty());  // (never in the running state)
  CHECK(ap::load_allowed(card, 1, save.data(), save.size()));
  ap::on_saved(card, 2, save.data(), save.size());  // (the same queue: the same file)
  CHECK(read_text(ap::sidecar_path(card, 2)) == read_text(ap::sidecar_path(card, 1)));

  // A new session loads it: the three wait again; a second load doesn't double them.
  ap::stop();
  ap::start_with(s);
  CHECK(ap::pending_count() == 0);
  ap::on_loaded(card, 1);
  CHECK(ap::is_checked(2001) && ap::is_checked(2004) && ap::pending_count() == 3);
  ap::on_loaded(card, 1);
  CHECK(ap::pending_count() == 3);
  // The save from before the boss: its game lacks the three, the checks stay checked: still three
  // (owed, not doubled); the crate opened since is not owed (that game opens it again).
  ap::on_flag_set(2401);
  CHECK(ap::is_checked(2005));
  ap::on_loaded(card, 0);
  CHECK(ap::is_checked(2001) && ap::is_checked(2005) && ap::pending_count() == 3);
  ap::on_loaded(card, 0);
  CHECK(ap::pending_count() == 3);
  ap::on_loaded(card, 1);  // (back to the later save: its own three, nothing else)
  CHECK(ap::pending_count() == 3);

  // Items that are no location's own (the console's, the courier's) keep their item and sender.
  ap::stop();
  ap::start_with(s);
  CHECK(ap::receive_by_name_or_id("Revival Card", "Bob", &error));
  CHECK(ap::receive_by_name_or_id("0x30", "", &error));
  CHECK(ap::receive_by_name_or_id("trap:fol_loss:25", "Bob", &error));
  CHECK(ap::receive_by_name_or_id("key:3", "Bob", &error));
  CHECK(ap::receive_by_name_or_id("Celine", "Alice", &error));
  ap::on_saved(card, 3, save.data(), save.size());
  CHECK(ap::read_sidecar(ap::sidecar_path(card, 3), &back, &error) && back.pending.size() == 5);
  CHECK(back.pending[0].location == -1 && back.pending[0].item.kind == "item" && back.pending[0].item.game_id == 21 &&
        back.pending[0].item.ap_id == 5000021 && back.pending[0].from == "Bob" && !back.pending[0].found);
  CHECK(back.pending[1].item.game_id == 0x30 && back.pending[1].from.empty());
  CHECK(back.pending[2].item.kind == "trap" && back.pending[2].item.trap == "fol_loss" && back.pending[2].item.percent == 25);
  CHECK(back.pending[3].item.kind == "key" && back.pending[3].item.key == 3 && back.pending[3].item.name == "Hilton Pass");
  CHECK(back.pending[4].item.kind == "character" && back.pending[4].item.character == 3 && back.pending[4].from == "Alice");
  CHECK(ap::load_allowed(card, 3, save.data(), save.size()));
  ap::stop();
  ap::start_with(s);
  ap::on_loaded(card, 3);
  CHECK(ap::pending_count() == 5);
  ap::SaveState again = back;
  again.pending_hash.clear();
  CHECK(ap::sidecar_from_json(ap::sidecar_json(again), &again, &error) && again.pending.size() == 5);

  // The guard: a list edited, extended, stripped of its hash, or the hash edited: refused like an
  // edited save. Spacing and key order are not edits. Both keys stripped: only the items are lost
  // (a sidecar with nothing waiting), as for the bench.
  const std::string path1 = ap::sidecar_path(card, 1), path4 = ap::sidecar_path(card, 4);
  auto variant = [&](const std::string& from, const std::string& to) {
    std::string t = text;
    const size_t at = t.find(from);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) t.replace(at, from.size(), to);
    write_text(path4, t);
    return ap::load_allowed(card, 4, save.data(), save.size());
  };
  CHECK(variant("\"location\": 2002", "\"location\": 2005") == false);
  CHECK(variant("\"pending\": [", "\"pending\": [{\"location\": 2002}, ") == false);
  CHECK(variant("\"pending_hash\"", "\"pending_hash_x\"") == false);
  CHECK(variant("\"pending_hash\": \"sha256:", "\"pending_hash\": \"sha256:0") == false);
  CHECK(variant("\"pending\": [", "\"pending\":[ ") == true);
  ap::Json j;
  CHECK(ap::Json::parse(text, &j, &error));
  ap::Json stripped = ap::Json::object();
  for (const auto& [k, v] : j.items())
    if (k != "pending") stripped.set(k, v);
  write_text(path4, stripped.dump());
  CHECK(!ap::load_allowed(card, 4, save.data(), save.size()));
  ap::Json both = ap::Json::object();
  for (const auto& [k, v] : j.items())
    if (k != "pending" && k != "pending_hash") both.set(k, v);
  write_text(path4, both.dump());
  CHECK(ap::load_allowed(card, 4, save.data(), save.size()));
  // Tied to its save: the same sidecar beside another save's bytes fails (save_hash first).
  std::vector<u8> other = save;
  other[0x300] ^= 1;
  CHECK(!ap::load_allowed(card, 1, other.data(), other.size()));
  CHECK(ap::load_allowed(card, 1, save.data(), save.size()));
  CHECK(read_text(path1) == text);

  // The server: checks of this slot made in a game this one lacks give their own items here once
  // (boss 2001 and 2002; not the crate 2005, not Alice's 2004); a reconnect adds nothing.
  ap::stop();
  ap::start_with(s);
  std::vector<int64_t> sent;
  int goals = 0;
  std::vector<std::string> deaths;
  std::vector<ap_net::Event> events;
  ap::connect(std::make_unique<FakeLink>(&sent, &goals, &deaths, &events));
  ap_net::Event c;
  c.kind = ap_net::Event::kConnected;
  c.seed_name = "waiting-1";
  c.checked = {2001, 2002, 2004, 2005};
  events.push_back(c);
  ap::vblank();
  CHECK(ap::connected() && ap::is_checked(2002) && ap::pending_count() == 2);
  events.push_back(c);
  ap::vblank();
  CHECK(ap::pending_count() == 2);
  // A load of the later save (it has all four boss checks, three waiting): its three, no more.
  ap::on_loaded(card, 1);
  CHECK(ap::pending_count() == 3);
  ap::stop();
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(ap_server_link) {
  ap::stop();
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(replace(kSeed, "\"options\": {\"story_route\": \"claude\", \"goal\": \"indalecio\"}",
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
  std::string t = replace(kSeed, "\"options\": {\"story_route\": \"claude\", \"goal\": \"indalecio\"}", "\"options\": " + options);
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
      "{\"character_items\": true, \"enemy_dropsanity\": \"first_drop\", \"exp_multiplier\": 3, \"fol_multiplier\": \"10\", "
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
  // enemy_dropsanity also as AP's option value (0 / 1).
  CHECK(ap::load_seed_text(phase3_seed("{\"enemy_dropsanity\": 1}"), &s, &error) && s.first_drops);
  // so2.apworld 0.1.0 / 0.1.1 seeds: the old option names still count.
  CHECK(ap::load_seed_text(phase3_seed("{\"drops_as_checks\": \"first_drop\", \"protagonist\": \"rena\"}"), &s, &error) &&
        s.first_drops && s.story_route == 2);
  CHECK(seed_error(phase3_seed("{\"protagonist\": \"noel\"}"), "options.protagonist: any, claude or rena"));
  // Out of range: the seed does not load.
  CHECK(seed_error(phase3_seed("{\"exp_multiplier\": 11}"), "options.exp_multiplier: 1-10"));
  CHECK(seed_error(phase3_seed("{\"fol_multiplier\": 0}"), "options.fol_multiplier: 1-10"));
  CHECK(seed_error(phase3_seed("{\"encounter_rate\": 201}"), "options.encounter_rate: 0-200"));
  CHECK(seed_error(phase3_seed("{\"enemy_dropsanity\": \"all\"}"), "enemy_dropsanity"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"character\": \"Bowman\"", "\"character\": 13"), "character 1-12"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"offset\": \"0x6bc4\", ", ""), "a join needs map and offset"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"enemy\": 17", "\"enemy\": -1"), "a drop needs its enemy"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"percent\": 25", "\"percent\": 0"), "percent 1-100"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"status\": \"stone\"", "\"status\": \"sleep\""), "status poison"));
  CHECK(seed_error(replace(phase3_seed("{}"), "\"trap\": \"ambush\"", "\"trap\": \"meteor\""), "trap ambush"));
}

// [difficulty agent] options.difficulty: New Game's Battle Rank (title new_game.cpp).
TEST(ap_difficulty_seed) {
  ap::Seed s;
  std::string error;
  // A seed without the key (before the option): -1, the original's rule.
  CHECK(ap::load_seed_text(kSeed, &s, &error) && s.difficulty == -1);
  const char* const names[] = {"earth", "galaxy", "universe", "any"};
  for (int i = 0; i < 4; ++i) {
    const bool by_name = ap::load_seed_text(phase3_seed(std::string("{\"difficulty\": \"") + names[i] + "\"}"), &s, &error);
    if (!by_name) std::fprintf(stderr, "  %s\n", error.c_str());
    CHECK(by_name && s.difficulty == i);
    CHECK(ap::load_seed_text(phase3_seed("{\"difficulty\": " + std::to_string(i) + "}"), &s, &error) && s.difficulty == i);
  }
  CHECK(seed_error(phase3_seed("{\"difficulty\": \"hard\"}"), "options.difficulty: earth, galaxy, universe or any"));
  CHECK(seed_error(phase3_seed("{\"difficulty\": 4}"), "options.difficulty: 0-3"));
  // The runtime: the seed's value while on; -1 off (no seed, or Archipelago off).
  CHECK(ap::load_seed_text(phase3_seed("{\"difficulty\": \"universe\"}"), &s, &error));
  ap::start_with(s);
  CHECK(ap::seed_difficulty() == 2);
  ap::stop();
  CHECK(ap::seed_difficulty() == -1);
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
  CHECK(ap::load_seed_text(phase3_seed("{\"enemy_dropsanity\": \"first_drop\", \"exp_multiplier\": 3, \"fol_multiplier\": 10, "
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
  CHECK(ap::load_seed_text(replace(kSeed, "\"options\": {\"story_route\": \"claude\", \"goal\": \"indalecio\"}",
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
  std::string t = replace(kSeed, "\"options\": {\"story_route\": \"claude\", \"goal\": \"indalecio\"}",
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
  std::string t = replace(kSeed, "\"options\": {\"story_route\": \"claude\", \"goal\": \"indalecio\"}", "\"options\": " + options);
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
  std::vector<u8> s(0x1C + 0x10000, 0);
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
  // [salvadrift agent] The field's per-frame question (no banner): the same answer.
  CHECK(ap::door_shut_at_trigger(0x3C6C) && !ap::door_shut_at_trigger(0x3C18));
  s32 args[7] = {43, 128, -1960, 0, 906, 315, 128};
  CHECK(ap::on_map_transfer(0x3C78, args));     // (reached another way: skipped too)
  // The key (a chest's placeholder): the door opens; its arguments stay the game's.
  CHECK(ap::on_give_item(1105) && ap::has_key(3) && ap::is_checked(9101));
  CHECK(!ap::door_blocked_at_trigger(0x3C6C) && !ap::door_shut_at_trigger(0x3C6C));
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
  CHECK(!ap::door_blocked_at_trigger(0x3C6C) && !ap::door_shut_at_trigger(0x3C6C));
  s32 d[7] = {43, 128, -1960, 0, 906, 315, 128};
  CHECK(!ap::on_map_transfer(0x3C78, d) && d[0] == 43);
  ap::stop();
  // No seed: nothing changes.
  CHECK(!ap::door_blocked_at_trigger(0x3C6C) && !ap::door_shut_at_trigger(0x3C6C) && !ap::on_map_transfer(0x3C78, d) &&
        !ap::on_shop_buy(1, 2, 224, 3));
}

namespace {

// phase4_seed with the El passes (keys 17-19) in three chests (placeholders 1117-1119) and the
// El voyage's gate: the Lacour Front Line meeting room's trigger (map 444, routine 0x7c28; the
// transfer to Lacour Castle at 0x851c, door 29106503) needing all three.
std::string gate_seed(const std::string& options) {
  std::string t = phase4_seed(options);
  t = replace(t, "\"5009000\":",
              "\"5300017\": {\"name\": \"Eluria Tower Pass\", \"kind\": \"key\", \"key\": 17},\n"
              "    \"5300018\": {\"name\": \"El Beach Pass\", \"kind\": \"key\", \"key\": 18},\n"
              "    \"5300019\": {\"name\": \"Eluria Pass\", \"kind\": \"key\", \"key\": 19},\n    \"5009000\":");
  std::string chests;
  for (const int k : {17, 18, 19})
    chests += "    {\"id\": " + std::to_string(9100 + k) + ", \"name\": \"pass " + std::to_string(k) +
              "\", \"placeholder\": " + std::to_string(1100 + k) + ", \"check\": {\"type\": \"chest\", \"map\": 444, \"flag\": " +
              std::to_string(100 + k) + "},\n     \"item\": {\"player\": 1, \"kind\": \"key\", \"key\": " + std::to_string(k) + "}},\n";
  t = replace(t, "  \"locations\": [\n", "  \"locations\": [\n" + chests);
  t = replace(t, "    {\"map\": 17, \"offset\": \"0x2060\", \"key\": 3}\n",
              "    {\"map\": 17, \"offset\": \"0x2060\", \"key\": 3},\n"
              "    {\"id\": 29106503, \"routine\": \"0x7c28\", \"keys\": [18, 19, 17], \"name\": \"El voyage\", \"purpose\": \"to sail\"},\n"
              "    {\"id\": 15407589, \"routine\": \"0x670c\", \"keys\": [28], \"until\": 38, \"name\": \"Central City exit\", \"purpose\": \"to leave\"}\n");
  return t;
}

}  // namespace

TEST(ap_gate_door) {
  ap::stop();
  ScratchGame sg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(gate_seed("{\"world_mode\": \"open_world\"}"), &s, &error));
  if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
  CHECK(s.doors.size() == 5 && s.doors[3].map == 444 && s.doors[3].offset == 0x851C && s.doors[3].routine == 0x7C28);
  CHECK(s.doors[4].map == 235 && s.doors[4].offset == 0x6794 && s.doors[4].until == 38 && s.doors[3].until == 0);
  CHECK(seed_error(replace(gate_seed("{}"), "\"until\": 38", "\"until\": 300"), "until: a chapter"));
  CHECK(s.doors[3].keys == (std::vector<int>{18, 19, 17}) && s.doors[3].key == 18 && s.doors[3].purpose == "to sail");
  CHECK(s.doors[0].keys == std::vector<int>{3} && s.doors[0].purpose.empty());
  CHECK(seed_error(replace(gate_seed("{}"), "\"keys\": [18, 19, 17]", "\"keys\": [18, 0]"), "keys: 1-255"));
  CHECK(seed_error(replace(gate_seed("{}"), "\"keys\": [18, 19, 17]", "\"keys\": 18"), "keys must be an array"));
  ap::start_with(s);
  ap::on_script_loaded(444, script_with_transfers({0x851C}));
  // Without the passes the trigger does not start; the banner names each one, once.
  CHECK(ap::door_blocked_at_trigger(0x7C28));
  CHECK(ap::door_blocked_at_trigger(0x7C28));
  CHECK(ap::waiting_notices() == (std::vector<std::string>{"El Beach Pass needed to sail",
                                                           "Eluria Pass needed to sail",
                                                           "Eluria Tower Pass needed to sail"}));
  CHECK(!ap::door_blocked_at_trigger(0x7C24));  // (another routine)
  // Two of the three: still shut.
  CHECK(ap::on_give_item(1118) && ap::on_give_item(1119) && ap::has_key(18) && ap::has_key(19));
  CHECK(ap::door_blocked_at_trigger(0x7C28));
  s32 args[7] = {422, 130, -215, 0, 0, 180, 128};
  CHECK(ap::on_map_transfer(0x851C, args));
  // All three: the voyage goes on, its transfer untouched.
  CHECK(ap::on_give_item(1117) && ap::has_key(17));
  CHECK(!ap::door_blocked_at_trigger(0x7C28));
  CHECK(!ap::on_map_transfer(0x851C, args) && args[0] == 422 && args[2] == -215);
  ap::stop();
  // Only the missing ones are named.
  CHECK(ap::load_seed_text(gate_seed("{\"world_mode\": \"open_world\"}"), &s, &error));
  ap::start_with(s);
  ap::on_script_loaded(444, script_with_transfers({0x851C}));
  CHECK(ap::on_give_item(1118) && ap::on_give_item(1117));
  CHECK(ap::door_blocked_at_trigger(0x7C28));
  CHECK(ap::waiting_notices() == std::vector<std::string>{"Eluria Pass needed to sail"});
  ap::stop();
  // An exit kept shut until a chapter (Central City's before chapter 38), without its key.
  CHECK(ap::load_seed_text(gate_seed("{\"world_mode\": \"open_world\"}"), &s, &error));
  ap::start_with(s);
  ap::on_script_loaded(235, script_with_transfers({0x6794}));
  const s32 chapter37 = 37, chapter38 = 38;
  std::memcpy(sg.state.data(), &chapter37, 4);
  CHECK(ap::door_blocked_at_trigger(0x670C));
  CHECK(ap::waiting_notices() == std::vector<std::string>{"Key 28 needed to leave"});
  std::memcpy(sg.state.data(), &chapter38, 4);
  CHECK(!ap::door_blocked_at_trigger(0x670C));
  std::memset(sg.state.data(), 0, 4);
  ap::stop();
  // Story mode: no gate.
  CHECK(ap::load_seed_text(gate_seed("{}"), &s, &error));
  ap::start_with(s);
  ap::on_script_loaded(444, script_with_transfers({0x851C}));
  CHECK(!ap::door_blocked_at_trigger(0x7C28) && ap::waiting_notices().empty());
  ap::stop();
  // No seed: nothing changes.
  CHECK(!ap::door_blocked_at_trigger(0x7C28));
}

namespace {

// gate_seed with two ship landings' doors (the world-map script's transfers: Port Hilton -> map
// 231, 909 0x5f08, key 9 "Map 231 Pass"; Herlie -> Port Hilton, 909 0x5ad0, key 7) and those two
// keys in chests (placeholders 1107, 1109).
std::string harbour_seed(const std::string& options) {
  std::string t = gate_seed(options);
  t = replace(t, "\"5009000\":",
              "\"5300007\": {\"name\": \"Port Hilton Pass\", \"kind\": \"key\", \"key\": 7},\n"
              "    \"5300009\": {\"name\": \"Map 231 Pass\", \"kind\": \"key\", \"key\": 9},\n    \"5009000\":");
  std::string chests;
  for (const int k : {7, 9})
    chests += "    {\"id\": " + std::to_string(9100 + k) + ", \"name\": \"pass " + std::to_string(k) +
              "\", \"placeholder\": " + std::to_string(1100 + k) + ", \"check\": {\"type\": \"chest\", \"map\": 169, \"flag\": " +
              std::to_string(100 + k) + "},\n     \"item\": {\"player\": 1, \"kind\": \"key\", \"key\": " + std::to_string(k) + "}},\n";
  t = replace(t, "  \"locations\": [\n", "  \"locations\": [\n" + chests);
  t = replace(t, "    {\"id\": 29106503,",
              "    {\"id\": 59578306, \"routine\": \"0x57d8\", \"key\": 9, \"name\": \"Port Hilton -> map 231\"},\n"
              "    {\"id\": 59578036, \"routine\": \"0x57d8\", \"key\": 7, \"name\": \"Herlie -> Port Hilton\"},\n"
              "    {\"id\": 29106503,");
  return t;
}

void set_story(ScratchGame& sg, s32 chapter, s32 step) {
  std::memcpy(sg.state.data(), &chapter, 4);
  std::memcpy(sg.state.data() + 4, &step, 4);
}

std::vector<ap::Voyage> voyages(std::initializer_list<ap::Voyage> v) { return v; }

// A map-font text's codes (one byte below 0x80, else two; control codes' parameters as codes).
std::vector<u32> text_codes(const u8* p) {
  std::vector<u32> out;
  for (size_t i = 0; p[i] != 0 && out.size() < 400;) {
    u32 c = p[i++];
    if (c >= 0x80) c = (c & 0x7F) | static_cast<u32>(p[i++]) << 7;
    out.push_back(c);
    if (c == 0x400E) out.push_back(p[i++]);
  }
  return out;
}

}  // namespace

TEST(ap_harbour) {
  ap::stop();
  ScratchGame sg;
  ap::Seed s;
  std::string error;
  using V = ap::Voyage;
  CHECK(ap::load_seed_text(harbour_seed("{\"world_mode\": \"open_world\"}"), &s, &error));
  if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
  ap::start_with(s);
  // The open world's harbour lines join the seed's: the refusals lifted, the menu's ops, the
  // Hoffmann soldier's chapters, the landing checks.
  int harbour = 0;
  for (const std::string& l : ap::seed_patch_lines())
    harbour += l.rfind("script 169 ", 0) == 0 || l.rfind("script 144 ", 0) == 0;
  CHECK(harbour == 12);
  bool lifted23 = false, menu_op = false;
  for (const std::string& l : ap::seed_patch_lines()) {
    lifted23 |= l.rfind("script 169 0x1c79 0x00000017 0x000003e7", 0) == 0;
    menu_op |= l.rfind("script 169 0x1c99 0x12000002 0xfe700000", 0) == 0;
  }
  CHECK(lifted23 && menu_op);
  ap::on_script_loaded(169, script_with_transfers({}));

  // The menu by chapter: Cross alone before Lacour's chapter 23 (the sailor's own strings).
  set_story(sg, 16, 0);
  CHECK(ap::harbour_menu() == voyages({V::kCross}));
  CHECK(ap::harbour_open(108, 109) == 2);
  const u8 own_question[] = {0x5C, 0x01, 0x57, 0};  // (a label glyph, code 1, a glyph)
  CHECK(ap::script_text(108, own_question) == nullptr && ap::script_text(109, own_question) == nullptr);
  CHECK(ap::harbour_choose(0).kind == ap::HarbourChoice::kCross);  // (his own fare and voyage)
  CHECK(ap::harbour_open(108, 109) == 2 && ap::harbour_choose(1).kind == ap::HarbourChoice::kStay);
  // Chapter 23 (the soft-lock's chapter): Cross and Hoffmann Ruins, until chapter 59.
  set_story(sg, 23, 0);
  CHECK(ap::harbour_menu() == voyages({V::kCross, V::kHoffmann}));
  set_story(sg, 59, 9999);
  CHECK(ap::harbour_menu() == voyages({V::kCross, V::kHoffmann}));
  set_story(sg, 60, 0);
  CHECK(ap::harbour_menu() == voyages({V::kCross}));
  // El only at the story's voyage point, chapter 27 step 7000, on disc 1.
  set_story(sg, 27, 6999);
  CHECK(ap::harbour_menu() == voyages({V::kCross, V::kHoffmann}));
  set_story(sg, 27, 7000);
  CHECK(ap::harbour_menu() == voyages({V::kCross, V::kHoffmann, V::kEl}));
  sg.config[0x4C] = 1;
  CHECK(ap::harbour_menu() == voyages({V::kCross, V::kHoffmann}));
  sg.config[0x4C] = 0;

  // The menu's text: the script's two strings (by the indices the op found) become the port's.
  CHECK(ap::harbour_open(108, 109) == 4);
  CHECK(ap::script_text(107, own_question) == nullptr);
  const u8* q = ap::script_text(108, own_question);
  CHECK(q != nullptr);
  if (q != nullptr) {
    const std::vector<u32> c = text_codes(q);
    // The script's own speaker label (up to code 1), a new line, then the fare as {number_var:00}.
    CHECK(c.size() > 6 && c[0] == 0x5C && c[1] == 1 && c[2] != 0x57);
    CHECK(std::count(c.begin(), c.end(), 0x4000u) == 1);
    CHECK(std::find(c.begin(), c.end(), 0x400Eu) != c.end());
  }
  // "{fe}{fe}{ea}Cross\n{fe}{fe}{ea}Don't cross{wait}": each option line keeps the lead-in.
  const u8 own_options[] = {0xFE, 0x01, 0xFE, 0x01, 0xEA, 0x01, 0xB8, 0x01, 0x80, 0x80,
                            0xFE, 0x01, 0xFE, 0x01, 0xEA, 0x01, 0xB9, 0x01, 0x82, 0x80, 0};
  const u8* o = ap::script_text(109, own_options);
  CHECK(o != nullptr);
  if (o != nullptr) {
    const std::vector<u32> c = text_codes(o);
    CHECK(std::count(c.begin(), c.end(), 0x4000u) == 3 && c.back() == 0x4002);  // (4 lines, then wait)
    CHECK(std::count(c.begin(), c.end(), 0xEAu) == 4 && c[0] == 0xFE && c[1] == 0xFE && c[2] == 0xEA);
  }

  // El chosen without the El passes: refused before anything is stored; the banner names each.
  ap::HarbourChoice h = ap::harbour_choose(2);
  CHECK(h.kind == ap::HarbourChoice::kStay);
  CHECK(sg.state[0] == 27 && sg.state[4] == 0x58);  // (still chapter 27 step 7000)
  CHECK(ap::waiting_notices() == (std::vector<std::string>{"El Beach Pass needed to sail", "Eluria Pass needed to sail",
                                                           "Eluria Tower Pass needed to sail"}));
  CHECK(ap::script_text(108, own_question) == nullptr);  // (the menu has closed)
  // Hoffmann Ruins without the Map 231 Pass: refused at the sailor (no fare, no voyage).
  CHECK(ap::harbour_open(108, 109) == 4 && ap::harbour_choose(1).kind == ap::HarbourChoice::kStay);
  CHECK(ap::waiting_notices().back() == "Map 231 Pass needed to sail");
  CHECK(!ap::voyage_landing_open(22) && !ap::voyage_landing_open(18));
  CHECK(ap::voyage_landing_open(17) && ap::voyage_landing_open(21));  // (no key on those doors here)
  // With it: the Hoffmann soldier's sail (map 169, 0x7cf8).
  CHECK(ap::on_give_item(1109) && ap::has_key(9) && ap::voyage_landing_open(22));
  CHECK(ap::harbour_open(108, 109) == 4);
  h = ap::harbour_choose(1);
  CHECK(h.kind == ap::HarbourChoice::kJump && h.offset == 0x7CF8);
  // Not now, a cancel, an option past the list: nothing.
  CHECK(ap::harbour_open(108, 109) == 4 && ap::harbour_choose(3).kind == ap::HarbourChoice::kStay);
  CHECK(ap::harbour_open(108, 109) == 4 && ap::harbour_choose(-1).kind == ap::HarbourChoice::kStay);
  CHECK(ap::harbour_open(108, 109) == 4 && ap::harbour_choose(7).kind == ap::HarbourChoice::kStay);
  // Every El pass: El sails as from the Front Line (chapter 28 step 0, Lacour Castle).
  CHECK(ap::on_give_item(1117) && ap::on_give_item(1118) && ap::on_give_item(1119));
  CHECK(ap::harbour_open(108, 109) == 4 && ap::harbour_choose(2).kind == ap::HarbourChoice::kEl);
  CHECK(sg.state[0] == 27);  // (stored only after 444's fade)
  s32 args[7] = {};
  ap::el_voyage_transfer(args);
  CHECK(sg.state[0] == 28 && sg.state[4] == 0);
  CHECK(args[0] == 422 && args[1] == 130 && args[2] == -215 && args[3] == 0 && args[4] == 0 && args[5] == 180 && args[6] == 128);
  // Herlie's sailor: the Port Hilton landing's key first.
  CHECK(!ap::voyage_landing_open(18) && ap::on_give_item(1107) && ap::voyage_landing_open(18));
  // The Hoffmann sail only on Port Hilton's script.
  set_story(sg, 24, 0);
  ap::on_script_loaded(144, script_with_transfers({}));
  CHECK(ap::harbour_open(108, 109) == 3 && ap::harbour_choose(1).kind == ap::HarbourChoice::kStay);
  ap::stop();

  // Story mode: no harbour lines, Cross alone, landings never refused (the original's choice).
  CHECK(ap::load_seed_text(harbour_seed("{}"), &s, &error));
  ap::start_with(s);
  CHECK(ap::seed_patch_lines().size() == s.patches.size());
  ap::on_script_loaded(169, script_with_transfers({}));
  set_story(sg, 27, 7000);
  CHECK(ap::harbour_menu() == voyages({V::kCross}));
  CHECK(ap::harbour_open(108, 109) == 2 && ap::script_text(108, own_question) == nullptr);
  CHECK(ap::harbour_choose(0).kind == ap::HarbourChoice::kCross && ap::harbour_choose(1).kind == ap::HarbourChoice::kStay);
  CHECK(ap::voyage_landing_open(22) && ap::waiting_notices().empty());
  ap::stop();
  // No seed: the same.
  CHECK(ap::harbour_menu() == voyages({V::kCross}) && ap::harbour_open(108, 109) == 2);
  CHECK(ap::script_text(108, own_question) == nullptr && ap::voyage_landing_open(22));
  CHECK(ap::harbour_choose(0).kind == ap::HarbourChoice::kCross && ap::harbour_choose(-1).kind == ap::HarbourChoice::kStay);
  set_story(sg, 0, 0);
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
  CHECK(!ap::on_shop_buy(1, 2, 225, 2) && !ap::is_checked(1365246466));
  // [shopboth agent] Three Lavender (the settlement has added all three): the first purchase is
  // also the check, our Revival Card given; the Lavender stay the player's.
  const auto held = [&](int item) {
    const int i = field::inventory_find(sg.g.inventory, item);
    return i < 0 ? 0 : sg.inv[static_cast<size_t>(i) * 2 + 1] >> 2 & 0x1F;
  };
  field::inventory_add(sg.g.inventory, 224, 3, 1);
  CHECK(ap::on_shop_buy(1, 2, 224, 3) && ap::is_checked(1365246466));
  CHECK(held(224) == 3 && held(21) == 1 && ap::pending_count() == 0);
  // A second purchase: just the item.
  CHECK(!ap::shop_slot_open(1, 2, 224) && !ap::on_shop_buy(1, 2, 224, 1) && held(21) == 1);
  // Alice's slot: the check sent, nothing of ours given (the Sandals are the player's).
  CHECK(ap::on_shop_buy(1, 0, 728, 1) && ap::is_checked(1365246464) && held(21) == 1);
  // A full stack (20 Revival Cards): the check's own item waits, given once there is room.
  ap::stop();
  ap::start_with(s);
  field::inventory_add(sg.g.inventory, 21, 19, 1);
  CHECK(held(21) == 20 && ap::on_shop_buy(1, 2, 224, 1) && ap::is_checked(1365246466));
  CHECK(held(21) == 20 && ap::pending_count() == 1);
  // The courier: ordered copies the inventory refused (20 of a kind) go in as far as they fit,
  // the rest waits; with Archipelago off nothing (the original: they vanish).
  field::inventory_add(sg.g.inventory, 757, 18, 1);
  CHECK(ap::order_overflow(757, 5) && held(757) == 20 && ap::pending_count() == 4);
  ap::stop();
  CHECK(!ap::order_overflow(757, 5) && held(757) == 20 && ap::pending_count() == 0);
}

// ---- v2 (docs/ap/seed_format.md "v2 fields") [hooks agent] ----

namespace {

// kSeed as a version-2 seed: the Limiter Off + Iseria Queen goal (goal.all 561 / 540, goal.flags
// 445 / 446, the gate door 43978493 on map 671 needing flag 556 and 561 won), two craft
// locations (Fried Rice 311: Alice's; 0x1a5: ours) and three killer-move levels (Claude's move
// 3 at 100 and 200 uses, Ashton's move 1 at 50).
const char* kV2Goal = "{\"all\": [561, 540], \"encounters\": [540], \"flags\": [445, 446]}";
std::string v2_seed(const std::string& options, const std::string& goal = kV2Goal) {
  std::string t = replace(kSeed, "\"version\": 1", "\"version\": 2");
  t = replace(t, "\"options\": {\"story_route\": \"claude\", \"goal\": \"indalecio\"}", "\"options\": " + options);
  t = replace(t, "  \"locations\": [\n",
              "  \"locations\": [\n"
              "    {\"id\": 1368523063, \"name\": \"Cooking: Fried Rice\", \"check\": {\"type\": \"craft\", \"item\": 311},\n"
              "     \"item\": {\"player\": 2, \"player_name\": \"Alice\", \"name\": \"Map Width\", \"ap_id\": 81001, \"classification\": \"useful\"}},\n"
              "    {\"id\": 1368523173, \"name\": \"Alchemy: Silver\", \"check\": {\"type\": \"craft\", \"item\": \"0x1a5\"},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"item\", \"game_id\": 21, \"name\": \"Revival Card\"}},\n"
              "    {\"id\": 1375751169, \"name\": \"Claude: move 3 L1\", \"check\": {\"type\": \"killer_move\", \"character\": 1, \"move\": 3, \"level\": 1, \"uses\": 100},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"fol\", \"amount\": 300}},\n"
              "    {\"id\": 1375751170, \"name\": \"Claude: move 3 L2\", \"check\": {\"type\": \"killer_move\", \"character\": \"Claude\", \"move\": 3, \"level\": 2, \"uses\": 200},\n"
              "     \"item\": {\"player\": 2, \"player_name\": \"Alice\", \"name\": \"Hookshot\"}},\n"
              "    {\"id\": 1375847425, \"name\": \"Ashton: move 1 L1\", \"check\": {\"type\": \"killer_move\", \"character\": 7, \"move\": 1, \"level\": 1, \"uses\": 50},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"fol\", \"amount\": 7}},\n");
  t = replace(t, "  \"patches\":",
              "  \"goal\": " + goal + ",\n"
              "  \"doors\": [{\"id\": 43978493, \"routine\": \"0x3bec\", \"flags\": [556], \"won\": [561],\n"
              "             \"banners\": [\"Talk to Filia in Central City first\", \"Defeat the Iseria Queen first\"],\n"
              "             \"name\": \"Indalecio (Limiter Off)\"}],\n"
              "  \"patches\":");
  return t;
}

bool flag_on(const ScratchGame& sg, int n) { return ((sg.flags[static_cast<size_t>(n >> 3)] >> (n & 7)) & 1) != 0; }
void set_flag(ScratchGame& sg, int n, bool on) {
  u8& b = sg.flags[static_cast<size_t>(n >> 3)];
  b = static_cast<u8>(on ? b | 1 << (n & 7) : b & ~(1 << (n & 7)));
}

// The party's records (8 x 0xd0, by party slot) for the killer-move use counts.
struct Records {
  u8*& ptr = field::globals().records;
  u8* saved = ptr;
  std::vector<u8> data = std::vector<u8>(0x680, 0);
  Records() { ptr = data.data(); }
  ~Records() { ptr = saved; }
  void uses(int slot, int move, int n) {
    const s16 v = static_cast<s16>(n);
    std::memcpy(data.data() + slot * 0xD0 + 0x8A + 2 * move, &v, 2);
  }
};

}  // namespace

TEST(ap_v2_seed) {
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(v2_seed("{\"no_fail_crafting\": true}"), &s, &error));
  if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
  CHECK(s.version == 2 && s.no_fail_crafting);
  CHECK(s.goal_all == (std::vector<int>{561, 540}) && s.goal_encounters == std::vector<int>{540});
  CHECK(s.goal_flags == (std::vector<int>{445, 446}));
  CHECK(s.locations[0].type == "craft" && s.locations[0].craft_item == 311 && s.locations[1].craft_item == 0x1A5);
  const ap::Location& k = s.locations[2];
  CHECK(k.type == "killer_move" && k.character == 1 && k.move == 3 && k.level == 1 && k.uses == 100);
  CHECK(s.locations[3].character == 1 && s.locations[3].uses == 200 && s.locations[4].character == 7);
  CHECK(s.doors.size() == 1);
  const ap::Door& d = s.doors[0];
  CHECK(d.map == 671 && d.offset == 0x3BF4 && d.routine == 0x3BEC && d.keys.empty() && d.has_conditions());
  CHECK(d.flags == std::vector<int>{556} && d.won == std::vector<int>{561} && d.banners.size() == 2);
  CHECK(ap::load_seed_text(v2_seed("{}"), &s, &error) && !s.no_fail_crafting);
  // A version-1 seed reads as before.
  CHECK(ap::load_seed_text(kSeed, &s, &error) && s.version == 1 && s.goal_all.empty() && s.goal_flags.empty());
  CHECK(s.goal_encounters == (std::vector<int>{539, 540}) && !s.no_fail_crafting);
  CHECK(seed_error(v2_seed("{}", "{\"all\": [-1]}"), "goal.all: encounter ids"));
  CHECK(seed_error(v2_seed("{}", "{\"all\": 561}"), "goal.all must be an array"));
  CHECK(seed_error(v2_seed("{}", "{\"flags\": [3000]}"), "goal.flags: script flags 0-2943"));
  CHECK(seed_error(replace(v2_seed("{}"), "\"item\": 311", "\"item\": 0"), "a craft needs its item"));
  CHECK(seed_error(replace(v2_seed("{}"), "\"move\": 3, \"level\": 1", "\"move\": 16, \"level\": 1"), "its move (1-15)"));
  CHECK(ap::load_seed_text(replace(v2_seed("{}"), "\"move\": 3, \"level\": 1", "\"move\": 11, \"level\": 1"), &s, &error) &&
        s.locations[2].move == 11);  // (Claude's Mirror Slice)
  CHECK(seed_error(replace(v2_seed("{}"), "\"uses\": 100", "\"uses\": 1000"), "uses (1-999)"));
  CHECK(seed_error(replace(v2_seed("{}"), "\"character\": 7", "\"character\": \"Nobody\""), "its character"));
  CHECK(seed_error(replace(v2_seed("{}"), "\"routine\": \"0x3bec\", ", ""), "flags / won needs its routine"));
  CHECK(seed_error(replace(v2_seed("{}"), "\"flags\": [556]", "\"flags\": [9999]"), "flags: script flags"));
  CHECK(seed_error(replace(v2_seed("{}"), "\"won\": [561]", "\"won\": 561"), "won must be an array"));
}

TEST(ap_v2_goal_all) {
  ap::stop();
  ScratchGame sg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(v2_seed("{}"), &s, &error));
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
  // goal.all: 540 alone (any-of would have ended it) is not the goal; an escape from 561 counts
  // nothing; its victory completes goal.all and the goal goes out once.
  ap::on_battle_exit(540, battle::kVictory);
  CHECK(!ap::state().goal_done && goals == 0 && ap::goal_won(540) && !ap::goal_won(561));
  ap::on_battle_exit(561, battle::kEscaped);
  ap::on_battle_exit(561, battle::kDefeat);
  CHECK(!ap::goal_won(561) && !ap::state().goal_done);
  ap::on_battle_exit(561, battle::kVictory);
  CHECK(ap::state().goal_done && goals == 1 && ap::state().goal_won == (std::vector<int>{540, 561}));
  ap::on_battle_exit(561, battle::kVictory);
  CHECK(goals == 1 && ap::state().goal_won.size() == 2);
  CHECK(ap::status_text().find("goal.all 561 540 | won 540 561") != std::string::npos);
  // The sidecar keeps goal_won; an older sidecar reads it as empty.
  ap::SaveState back;
  CHECK(ap::sidecar_from_json(ap::sidecar_json(ap::state()), &back, &error) && back.goal_won == (std::vector<int>{540, 561}));
  ap::Json old = ap::sidecar_json(ap::SaveState{});
  old.set("goal_won", ap::Json());
  CHECK(ap::sidecar_from_json(old, &back, &error) && back.goal_won.empty());
  ap::stop();

  // Loads merge like checked: a save with 561 won, this session's 540: both, and the goal.
  const fs::path dir = scratch("v2goal");
  const std::string card = (dir / "memcard1.mcd").string();
  CHECK(ap::load_seed_text(v2_seed("{}"), &s, &error));
  ap::start_with(s);
  ap::on_battle_exit(561, battle::kVictory);
  ap::on_saved(card, 0);
  ap::on_new_game();
  CHECK(ap::state().goal_won.empty());
  ap::on_battle_exit(540, battle::kVictory);
  CHECK(!ap::state().goal_done);
  ap::on_loaded(card, 0);
  CHECK(ap::state().goal_won == (std::vector<int>{561, 540}) && ap::state().goal_done);
  // A sidecar-less save: a fresh state.
  ap::on_loaded(card, 4);
  CHECK(ap::state().goal_won.empty() && !ap::state().goal_done);
  ap::stop();
  // Any-of seeds (no goal.all) keep today's rule and record nothing in goal_won.
  CHECK(ap::load_seed_text(v2_seed("{}", "{\"encounters\": [540], \"flags\": [445, 446]}"), &s, &error));
  ap::start_with(s);
  ap::on_battle_exit(540, battle::kVictory);
  CHECK(ap::state().goal_done && ap::state().goal_won.empty());
  ap::stop();
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(ap_v2_goal_flags) {
  ap::stop();
  ScratchGame sg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(v2_seed("{}"), &s, &error));
  ap::start_with(s);
  // The first field frame sets them (a start straight into the field), once.
  ap::free_play_update();
  CHECK(flag_on(sg, 445) && flag_on(sg, 446) && !flag_on(sg, 556));
  set_flag(sg, 445, false);
  set_flag(sg, 446, false);
  ap::free_play_update();
  CHECK(!flag_on(sg, 445));
  // New Game: again.
  ap::on_new_game();
  ap::free_play_update();
  CHECK(flag_on(sg, 445) && flag_on(sg, 446));
  // Every load, a save without a sidecar included (start.flags would not apply there).
  const fs::path dir = scratch("v2flags");
  set_flag(sg, 445, false);
  set_flag(sg, 446, false);
  ap::on_loaded((dir / "memcard1.mcd").string(), 3);
  ap::free_play_update();
  CHECK(flag_on(sg, 445) && flag_on(sg, 446));
  ap::stop();
  // A seed without goal.flags sets nothing; no seed, nothing.
  set_flag(sg, 445, false);
  CHECK(ap::load_seed_text(kSeed, &s, &error));
  ap::start_with(s);
  ap::free_play_update();
  CHECK(!flag_on(sg, 445));
  ap::stop();
  ap::free_play_update();
  CHECK(!flag_on(sg, 445));
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(ap_v2_door_conditions) {
  ap::stop();
  ScratchGame sg;
  ap::Seed s;
  std::string error;
  // A story-mode seed: the conditions apply anyway.
  CHECK(ap::load_seed_text(v2_seed("{}"), &s, &error));
  CHECK(!s.open_world);
  ap::start_with(s);
  ap::on_script_loaded(671, script_with_transfers({0x3BF4}));
  // Neither condition: the trigger does not start; the first missing one's banner, once.
  CHECK(ap::door_blocked_at_trigger(0x3BEC));
  CHECK(ap::door_blocked_at_trigger(0x3BEC));
  CHECK(ap::waiting_notices() == std::vector<std::string>{"Talk to Filia in Central City first"});
  CHECK(!ap::door_blocked_at_trigger(0x3BE0));  // (another routine)
  s32 args[7] = {684, 0, 0, 0, 0, 0, 128};
  CHECK(ap::on_map_transfer(0x3BF4, args));     // (reached another way: skipped too)
  // Flag 556 (Filia's scene): the Iseria Queen is still missing.
  set_flag(sg, 556, true);
  CHECK(ap::door_blocked_at_trigger(0x3BEC));
  CHECK(ap::waiting_notices().back() == "Defeat the Iseria Queen first");
  // A loss or an escape does not count; her victory opens it.
  ap::on_battle_exit(561, battle::kEscaped);
  CHECK(ap::door_blocked_at_trigger(0x3BEC));
  ap::on_battle_exit(561, battle::kVictory);
  CHECK(!ap::door_blocked_at_trigger(0x3BEC));
  CHECK(!ap::on_map_transfer(0x3BF4, args) && args[0] == 684);
  // The flag cleared again: shut again (the flag is read live).
  set_flag(sg, 556, false);
  CHECK(ap::door_blocked_at_trigger(0x3BEC));
  ap::stop();
  // Open world: conditions and keys together; all must hold.
  CHECK(ap::load_seed_text(replace(v2_seed("{\"world_mode\": \"open_world\"}"), "\"routine\": \"0x3bec\",",
                                   "\"routine\": \"0x3bec\", \"key\": 3,"), &s, &error));
  CHECK(s.doors[0].keys == std::vector<int>{3});
  ap::start_with(s);
  ap::on_script_loaded(671, script_with_transfers({0x3BF4}));
  set_flag(sg, 556, true);
  ap::on_battle_exit(561, battle::kVictory);
  CHECK(ap::door_blocked_at_trigger(0x3BEC));
  CHECK(ap::waiting_notices() == std::vector<std::string>{"Key 3 needed"});
  CHECK(ap::receive_by_name_or_id("key:3", "Bob", &error));
  ap::stop();
  // A banner left out: a plain one.
  CHECK(ap::load_seed_text(replace(v2_seed("{}"), "\"banners\": [\"Talk to Filia in Central City first\", ", "\"banners\": [\"\", "), &s,
                           &error));
  ap::start_with(s);
  ap::on_script_loaded(671, script_with_transfers({0x3BF4}));
  set_flag(sg, 556, false);
  CHECK(ap::door_blocked_at_trigger(0x3BEC));
  CHECK(ap::waiting_notices() == std::vector<std::string>{"The way is closed for now"});
  ap::stop();
  // No seed: nothing changes.
  CHECK(!ap::door_blocked_at_trigger(0x3BEC) && !ap::on_map_transfer(0x3BF4, args));
}

TEST(ap_v2_trumpet) {
  ap::stop();
  ScratchGame sg;
  ap::Seed s;
  std::string error;
  CHECK(!ap::sell_locked(48));  // (no seed)
  CHECK(ap::load_seed_text(v2_seed("{}"), &s, &error));
  ap::start_with(s);
  // The Iseria Queen goal: the Silver Trumpet stays off the sell list until The Evil Melody
  // (song 7, flag 53) is composed; nothing else is held back.
  CHECK(ap::sell_locked(48) && !ap::sell_locked(47) && !ap::sell_locked(49) && !ap::sell_locked(21));
  set_flag(sg, 53, true);
  CHECK(!ap::sell_locked(48));
  set_flag(sg, 53, false);
  ap::stop();
  // Other goals: sold as usual.
  CHECK(ap::load_seed_text(v2_seed("{}", "{\"encounters\": [540], \"flags\": [445, 446]}"), &s, &error));
  ap::start_with(s);
  CHECK(!ap::sell_locked(48));
  ap::stop();
  CHECK(ap::load_seed_text(kSeed, &s, &error));
  ap::start_with(s);
  CHECK(!ap::sell_locked(48));
  ap::stop();
}

TEST(ap_v2_craft) {
  ap::stop();
  ScratchGame sg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(v2_seed("{\"no_fail_crafting\": true}"), &s, &error));
  ap::start_with(s);
  CHECK(ap::no_fail_crafting());
  // Alice's item at Fried Rice: sent (banner), recorded once.
  ap::on_craft(311, true);
  CHECK(ap::is_checked(1368523063) && ap::waiting_notices() == std::vector<std::string>{"Sent Map Width to Alice"});
  ap::on_craft(311, true);
  CHECK(ap::state().checked.size() == 1 && ap::waiting_notices().size() == 1);
  // Our own item at 0x1a5: checked; the item comes in free play ("Got X"), on top of the craft.
  ap::on_craft(0x1A5, true);
  CHECK(ap::is_checked(1368523173) && ap::pending_count() == 1);
  // An item the seed does not list: nothing.
  ap::on_craft(312, true);
  CHECK(ap::state().checked.size() == 2);
  ap::stop();
  CHECK(ap::load_seed_text(v2_seed("{}"), &s, &error));
  ap::start_with(s);
  CHECK(!ap::no_fail_crafting());
  ap::stop();
  CHECK(!ap::no_fail_crafting());
  ap::on_craft(311, true);  // (no seed: nothing)
}

TEST(ap_v2_killer_moves) {
  ap::stop();
  ScratchGame sg;
  Records rec;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(v2_seed("{}"), &s, &error));
  ap::start_with(s);
  sg.member(0, 1, 100);   // Claude in slot 0
  sg.member(1, -2, 100);  // Rena in reserve
  rec.uses(0, 3, 99);
  CHECK(ap::scan_killer_moves() == 0);
  rec.uses(0, 3, 100);    // level 1 reached
  CHECK(ap::scan_killer_moves() == 1 && ap::is_checked(1375751169) && !ap::is_checked(1375751170));
  CHECK(ap::scan_killer_moves() == 0);
  rec.uses(0, 2, 999);    // (another move)
  CHECK(ap::scan_killer_moves() == 0);
  // Ashton isn't in the party: slot 1's record belongs to Rena.
  rec.uses(1, 1, 60);
  CHECK(ap::scan_killer_moves() == 0);
  // He joins in slot 2 with a count already past (a save, say): checked at once.
  sg.member(2, 7, 100);
  rec.uses(2, 1, 50);
  CHECK(ap::scan_killer_moves() == 1 && ap::is_checked(1375847425));
  // A count past both levels: the rest at once (the cap is 999).
  rec.uses(0, 3, 999);
  CHECK(ap::scan_killer_moves() == 1 && ap::is_checked(1375751170));
  CHECK(ap::waiting_notices() == std::vector<std::string>{"Sent Hookshot to Alice"});
  CHECK(ap::pending_count() == 2);  // (the two own Fol items, given in free play)
  ap::stop();
  CHECK(ap::scan_killer_moves() == 0);  // (no seed)
}

// ---- v2 party (docs/ap/seed_format.md "Party v2") [party agent] ----

namespace {

// A version-2 seed with the given options and three join sites: Rena's first join in Claude's
// game (map 9, 0x6b94), Noel's (map 277, 0x6fe0) and Bowman's (map 208, 0x6bc4).
std::string party_seed(const std::string& options) {
  std::string t = replace(kSeed, "\"version\": 1", "\"version\": 2");
  t = replace(t, "\"options\": {\"story_route\": \"claude\", \"goal\": \"indalecio\"}", "\"options\": " + options);
  t = replace(t, "  \"locations\": [\n",
              "  \"locations\": [\n"
              "    {\"id\": 1353851904, \"name\": \"Shingo Forest: Rena joins\", \"check\": {\"type\": \"join\", \"map\": 9, \"offset\": \"0x6b94\", \"character\": \"Rena\"},\n"
              "     \"item\": {\"player\": 2, \"player_name\": \"Alice\", \"name\": \"Map Width\"}},\n"
              "    {\"id\": 1354126336, \"name\": \"Noel's House: Noel joins\", \"check\": {\"type\": \"join\", \"map\": 277, \"offset\": \"0x6fe0\", \"character\": \"Noel\"},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"fol\", \"amount\": 10}},\n"
              "    {\"id\": 1354055680, \"name\": \"Bowman joins\", \"check\": {\"type\": \"join\", \"map\": 208, \"offset\": \"0x6bc4\", \"character\": \"Bowman\"},\n"
              "     \"item\": {\"player\": 2, \"player_name\": \"Alice\", \"name\": \"Hookshot\"}},\n");
  return t;
}

std::string open_options(const std::string& route, const std::string& extra = "") {
  return "{\"world_mode\": \"open_world\", \"character_items\": true, \"party_rules\": \"open\", \"story_route\": \"" + route +
         "\"" + extra + "}";
}

// The field's party with its records, the map, the story's position and the game.
struct PartyGame {
  ScratchGame sg;
  Records rec;
  fs::path dir = scratch("v2party");
  int loads = 0;
  ~PartyGame() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void at(int map, s32 chapter, s32 step) {
    const s16 m = static_cast<s16>(map);
    std::memcpy(sg.state.data() + 0x1A, &m, 2);
    set_story(sg, chapter, step);
  }
  void claude_game(bool on) { set_flag(sg, 1, on); }
  void members(std::initializer_list<int> ids) {
    std::fill(sg.party.begin(), sg.party.end(), 0);
    int slot = 0;
    for (const int id : ids) sg.member(slot++, id, 100);
  }
  // The party as "1,2,-3" (slots in order, empty ones left out).
  std::string party() const {
    std::string out;
    for (int i = 0; i < 8; ++i) {
      s16 v;
      std::memcpy(&v, sg.party.data() + i * 0x60, 2);
      if (v == 0) continue;
      out += (out.empty() ? "" : ",") + std::to_string(v);
    }
    return out;
  }
  // These members received (through a save's sidecar, as a load gives them).
  void received(std::vector<int> ids) {
    ap::SaveState st = ap::state();
    st.characters = std::move(ids);
    st.characters_waiting.clear();
    st.start_given = true;
    const std::string card = (dir / "memcard1.mcd").string();
    std::string error;
    CHECK(ap::write_sidecar(ap::sidecar_path(card, loads), st, &error));
    ap::on_loaded(card, loads++);
  }
  // A join as script ext 0xae does it: the game's own join unless Archipelago took it.
  bool join(int id, int at, int call_at = -1) {
    if (ap::on_party_add(id - 1, at, call_at)) return false;
    field::party_add_member(nullptr, id - 1);
    return true;
  }
  void leave(int id) {
    ap::on_party_remove(id - 1, 0x100, -1);
    field::party_remove_member(nullptr, id - 1);
  }
};

}  // namespace

TEST(ap_v2_party_seed) {
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(party_seed(open_options("claude")), &s, &error));
  CHECK(s.version == 2 && s.open_party && s.starting_character == 0);
  CHECK(ap::load_seed_text(party_seed(open_options("claude", ", \"starting_character\": \"Ashton\"")), &s, &error));
  CHECK(s.starting_character == 7);
  CHECK(ap::load_seed_text(party_seed(open_options("any", ", \"starting_character\": 9")), &s, &error) && s.starting_character == 9);
  for (const char* none : {"null", "\"hero\"", "\"\"", "0"}) {
    CHECK(ap::load_seed_text(party_seed(open_options("claude", std::string(", \"starting_character\": ") + none)), &s, &error));
    CHECK(s.starting_character == 0);
  }
  // party_rules "story" (or none): phase 3's rules; a starting character works without it too.
  CHECK(ap::load_seed_text(party_seed("{\"party_rules\": \"story\", \"starting_character\": \"Leon\"}"), &s, &error));
  CHECK(!s.open_party && s.starting_character == 8);
  CHECK(ap::load_seed_text(kSeed, &s, &error) && !s.open_party && s.starting_character == 0);
  CHECK(seed_error(party_seed("{\"party_rules\": \"closed\"}"), "options.party_rules: story or open"));
  CHECK(seed_error(party_seed("{\"party_rules\": true}"), "options.party_rules: story or open"));
  CHECK(seed_error(party_seed("{\"starting_character\": \"Zand\"}"), "options.starting_character"));
  CHECK(seed_error(party_seed("{\"starting_character\": 13}"), "options.starting_character"));
}

// party_rules "open": only story members, received members and the starting character join,
// at a listed site (its check recorded) or anywhere else; phase 3's seeds unchanged.
TEST(ap_v2_party_open_joins) {
  ap::stop();
  PartyGame pg;
  pg.claude_game(true);
  pg.members({1});
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(party_seed(open_options("claude")), &s, &error));
  ap::start_with(s);
  // Rena's listed site (map 9, the call at 0x6b94): the check, but no Rena (not received).
  pg.at(9, 4, 2000);
  CHECK(!pg.join(2, 0x63d0, 0x6b94));
  CHECK(ap::is_checked(1353851904) && pg.party() == "1");
  // Unlisted sites (map 27 re-adds Rena; Mars adds Celine): nobody either.
  pg.at(27, 5, 1000);
  CHECK(!pg.join(2, 0x63d0, 0x580));
  pg.at(124, 15, 1000);
  CHECK(!pg.join(3, 0x63d0, 0x7058));
  CHECK(pg.party() == "1" && ap::state().checked.size() == 1);
  // The hero: the game's join.
  CHECK(pg.join(1, 0x63d0, 0x1234));
  // Received: anywhere, the game's join (Leon in Claude's game, Rena at map 27).
  pg.received({2, 8});
  pg.at(27, 5, 1000);
  CHECK(pg.join(2, 0x63d0, 0x580));
  pg.at(230, 30, 0);
  CHECK(pg.join(8, 0x63d0, 0x6c58));
  CHECK(pg.party() == "1,2,8");
  // A listed site of a received member: checked, and the game's join.
  pg.received({2, 8, 11});
  pg.at(277, 39, 1000);
  CHECK(pg.join(11, 0x63d0, 0x6fe0));
  CHECK(ap::is_checked(1354126336) && pg.party() == "1,2,8,11");
  ap::stop();
  // story_route "any": both heroes are story members.
  pg.members({1});
  CHECK(ap::load_seed_text(party_seed(open_options("any")), &s, &error));
  ap::start_with(s);
  pg.at(27, 5, 1000);
  CHECK(pg.join(2, 0x63d0, 0x580));
  CHECK(!pg.join(3, 0x63d0, 0x7058));
  CHECK(pg.party() == "1,2");
  ap::stop();
  // Phase 3 (party_rules "story", character_items): unlisted sites are the game's; a listed one
  // holds back a member not received.
  pg.members({1});
  CHECK(ap::load_seed_text(party_seed("{\"character_items\": true, \"story_route\": \"claude\"}"), &s, &error));
  ap::start_with(s);
  pg.at(124, 15, 1000);
  CHECK(pg.join(3, 0x63d0, 0x7058));
  pg.at(208, 22, 4000);
  CHECK(!pg.join(4, 0x63d0, 0x6bc4));
  CHECK(pg.party() == "1,3" && ap::is_checked(1354055680));
  ap::stop();
}

// A slot of a story member or of a member recruited for good is never overwritten.
TEST(ap_v2_party_slots) {
  ap::stop();
  PartyGame pg;
  pg.claude_game(true);
  // Eight slots: Claude, Rena, Celine (in reserve), Bowman, Dias (in reserve: a story member for a
  // while, not received), Precis, Ashton, Opera.
  pg.members({1, 2, -3, 4, -5, 6, 7, 9});
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(party_seed(open_options("claude")), &s, &error));
  ap::start_with(s);
  pg.at(230, 30, 0);
  pg.received({2, 3, 4, 6, 7, 8, 9, 11});
  // Leon: Dias's reserve slot (not kept), not Celine's.
  CHECK(!pg.join(8, 0x63d0, 0x6c58));
  CHECK(pg.party() == "1,2,-3,4,8,6,7,9");
  // Noel: no slot left but kept ones: skipped, his join waits.
  CHECK(!pg.join(11, 0x63d0, 0x6fe0));
  CHECK(pg.party() == "1,2,-3,4,8,6,7,9");
  CHECK(ap::state().characters_waiting == std::vector<int>{11});
  CHECK(ap::party_free_play_step().empty());  // (still no room)
  ap::stop();
}

// The split windows: temporary members, the split's members kept away, back afterwards.
TEST(ap_v2_party_windows) {
  ap::stop();
  PartyGame pg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(party_seed(open_options("rena")), &s, &error));
  ap::start_with(s);
  // Shingo Forest (map 12, chapter 4 step 150): Rena's game only.
  pg.claude_game(false);
  pg.at(12, 4, 150);
  CHECK(ap::split_window_needing(1) == "Shingo Forest (map 12)" && ap::split_window_needing(5).empty());
  pg.claude_game(true);
  CHECK(ap::split_window_needing(1).empty());
  // The tournament: Dias (and Claude) for a while, everyone else kept apart; over at chapter 20.
  pg.at(477, 19, 4400);
  CHECK(!ap::split_window_needing(5).empty() && !ap::split_window_keeping(0).empty());
  pg.at(1, 20, 0);
  CHECK(ap::split_window_needing(5).empty() && ap::split_window_keeping(0).empty());
  // Linga (chapter 22, steps 3000-5000); Dias away in Rena's game from Mars to the tournament.
  pg.at(193, 22, 4000);
  CHECK(ap::split_window_keeping(0) == "Linga (193 -> 195)");
  pg.at(193, 22, 6000);
  CHECK(ap::split_window_keeping(0).empty());
  pg.claude_game(false);
  pg.at(1, 17, 0);
  CHECK(ap::split_window_keeping(5) == "Dias away (462 -> 434)" && ap::split_window_keeping(4).empty() &&
        ap::split_window_keeping(0).empty());
  set_flag(pg.sg, 2318, true);  // (the Cave of Trials' party choice)
  CHECK(ap::split_window_keeping(0) == "the Cave of Trials (flag 2318)");
  set_flag(pg.sg, 2318, false);
  // Rena's game, Claude not received: map 12 takes Rena out and adds Claude for the fight (let in
  // for a while), then swaps them back; free play after the scene drops him for good.
  pg.members({2});
  pg.at(12, 4, 150);
  pg.leave(2);
  CHECK(!pg.join(1, 0x63d0, 0x6e08));
  CHECK(pg.party() == "1,-2" && ap::state().temporary == std::vector<int>{1});
  pg.leave(1);
  CHECK(pg.join(2, 0x63d0, 0x70b4));
  CHECK(pg.party() == "-1,2");
  pg.at(12, 4, 200);
  CHECK(ap::party_free_play_step().empty());
  CHECK(pg.party() == "2" && ap::state().temporary.empty());
  // Outside the window he is refused (map 9's site, chapter 4 step 2000).
  pg.at(9, 4, 2000);
  CHECK(!pg.join(1, 0x63d0, 0x6ba0));
  CHECK(pg.party() == "2");
  ap::stop();
  // Claude's game, Celine received: Linga takes her out; she stays out during the split (no
  // banner), and rejoins in free play once it is over (her restore is not needed).
  CHECK(ap::load_seed_text(party_seed(open_options("claude")), &s, &error));
  ap::start_with(s);
  pg.claude_game(true);
  pg.members({1, 2, 3});
  pg.received({3});
  pg.at(193, 22, 3000);
  pg.leave(3);
  CHECK(pg.party() == "1,2,-3");
  CHECK(ap::party_free_play_step().empty() && pg.party() == "1,2,-3");
  pg.at(195, 22, 5100);
  CHECK(ap::party_free_play_step() == "Celine joined the party");
  CHECK(pg.party() == "1,2,3" && ap::state().characters_waiting.empty());
  ap::stop();
  // A window for one member (Rena's game, Dias away from the tournament to the Front Line) holds
  // the Dias the story set aside, not one received meanwhile.
  CHECK(ap::load_seed_text(party_seed(open_options("rena")), &s, &error));
  ap::start_with(s);
  pg.claude_game(false);
  pg.members({2, -5});
  pg.received({5});
  pg.at(1, 20, 0);
  CHECK(ap::party_free_play_step().empty() && pg.party() == "2,-5");
  pg.at(447, 26, 3100);  // (past the Front Line too)
  CHECK(ap::party_free_play_step() == "Dias joined the party" && pg.party() == "2,5");
  pg.members({2});
  pg.at(1, 20, 0);
  CHECK(ap::receive_by_name_or_id("Dias", "Bob", &error));
  pg.received({5});
  ap::SaveState st = ap::state();
  CHECK(ap::party_free_play_step().empty());  // (received through a load: not waiting)
  st.characters_waiting = {5};
  {
    const std::string card = (pg.dir / "memcard1.mcd").string();
    CHECK(ap::write_sidecar(ap::sidecar_path(card, 9), st, &error));
    ap::on_loaded(card, 9);
  }
  CHECK(ap::party_free_play_step() == "Dias joined the party" && pg.party() == "2,5");
  ap::stop();
}

// A temporary member with every slot kept borrows one; its member comes back (the sidecar keeps
// the borrowed data).
TEST(ap_v2_party_borrow) {
  ap::stop();
  PartyGame pg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(party_seed(open_options("claude")), &s, &error));
  ap::start_with(s);
  pg.claude_game(true);
  pg.members({1, 2, 3, 4, 6, 7, 8, 9});
  pg.received({2, 3, 4, 6, 7, 8, 9});
  pg.rec.data[0x10] = 0x5A;  // (Claude's record: a byte to see it come back)
  pg.at(477, 19, 4400);
  // Dias's fight: the scene takes Claude out, adds Dias.
  pg.leave(1);
  CHECK(!pg.join(5, 0x63d0, 0x6704));
  CHECK(pg.party() == "5,2,3,4,6,7,8,9" && ap::state().stash.size() == 1 && ap::state().temporary == std::vector<int>{5});
  // The sidecar keeps the stash.
  ap::SaveState back;
  CHECK(ap::sidecar_from_json(ap::sidecar_json(ap::state()), &back, &error));
  CHECK(back.stash.size() == 1 && back.stash[0].slot == 0 && back.stash[0].bytes == ap::state().stash[0].bytes &&
        back.temporary == std::vector<int>{5} && back.stash[0].by == 5);
  ap::Json bad;
  CHECK(ap::Json::parse("{\"format\": \"apso2-save\", \"version\": 1, \"stash\": [{\"slot\": 9, \"bytes\": \"00\"}]}", &bad, &error));
  CHECK(!ap::sidecar_from_json(bad, &back, &error));
  // The fight over: Dias out, Claude back in his own slot with his record.
  pg.leave(5);
  CHECK(pg.join(1, 0x63d0, 0x6b78));
  CHECK(pg.party() == "1,2,3,4,6,7,8,9" && pg.rec.data[0x10] == 0x5A);
  CHECK(ap::state().stash.empty() && ap::state().temporary.empty());
  // [partybuild agent] A scene moving the borrower (as map 12's specials 1 / 2 do): the member
  // moved into the borrowed slot is never overwritten; the borrower goes back to it first.
  pg.leave(1);
  CHECK(!pg.join(5, 0x63d0, 0x6704) && pg.party() == "5,2,3,4,6,7,8,9" && ap::state().stash.size() == 1);
  field::party_swap_slots(nullptr, 0, 1);
  CHECK(pg.party() == "2,5,3,4,6,7,8,9");
  pg.leave(5);
  CHECK(pg.join(1, 0x63d0, 0x6b78));
  CHECK(pg.party() == "1,2,3,4,6,7,8,9" && pg.rec.data[0x10] == 0x5A && ap::state().stash.empty() && ap::state().temporary.empty());
  ap::stop();
}

// [shingofix agent] Map 12's scene with every slot kept (Rena's game, Ashton leading, Claude not
// received): Claude borrows Rena's reserve slot (1); special 1 moves him to slot 0 (Ashton into the
// borrowed slot); a free play meanwhile keeps the stash (he is in, active and needed, wherever he
// stands); Rena's join puts him back in the slot he borrowed before her data, so Ashton stays;
// special 2 then puts Rena in slot 0. A sidecar from before ("by" absent) finds the temporary
// member the same way; "by" is written only when known, and out of range reads as unknown.
TEST(ap_v2_party_borrow_moved) {
  ap::stop();
  PartyGame pg;
  ap::Seed s;
  std::string error;
  for (const bool old_sidecar : {false, true}) {
    CHECK(ap::load_seed_text(party_seed(open_options("rena")), &s, &error));
    ap::start_with(s);
    pg.claude_game(false);
    pg.members({7, 2, 3, 4, 6, 8, 9, 11});
    pg.received({3, 4, 6, 7, 8, 9, 11});
    pg.rec.data[1 * 0xD0 + 0x10] = 0x5B;  // (Rena's record: a byte to see it come back)
    pg.at(12, 4, 150);
    pg.leave(2);  // (the scene takes Rena out)
    CHECK(!pg.join(1, 0x63d0, 0x6e08));
    CHECK(pg.party() == "7,1,3,4,6,8,9,11" && ap::state().stash.size() == 1 && ap::state().stash[0].slot == 1 &&
          ap::state().stash[0].by == 1 && ap::state().temporary == std::vector<int>{1});
    {
      const ap::Json j = ap::sidecar_json(ap::state());
      CHECK(j["stash"][0]["by"].as_int(0) == 1);
    }
    if (old_sidecar) {
      ap::SaveState st = ap::state();
      st.stash[0].by = 0;
      CHECK(ap::sidecar_json(st)["stash"][0]["by"].is_null());
      const std::string card = (pg.dir / "memcard1.mcd").string();
      CHECK(ap::write_sidecar(ap::sidecar_path(card, pg.loads), st, &error));
      ap::on_loaded(card, pg.loads++);
      CHECK(ap::state().stash.size() == 1 && ap::state().stash[0].by == 0 && ap::state().temporary == std::vector<int>{1});
    }
    field::party_swap_slots(nullptr, 0, 1);  // (special 1: Claude to slot 0)
    CHECK(pg.party() == "1,7,3,4,6,8,9,11");
    if (!old_sidecar) {
      CHECK(ap::party_free_play_step().empty());
      CHECK(pg.party() == "1,7,3,4,6,8,9,11" && ap::state().stash.size() == 1);
    }
    CHECK(pg.join(2, 0x63d0, 0x70ac));  // (Rena's join: her data back, then the game's join)
    CHECK(pg.party() == "7,2,3,4,6,8,9,11" && pg.rec.data[1 * 0xD0 + 0x10] == 0x5B);
    CHECK(ap::state().stash.empty() && ap::state().temporary.empty());
    field::party_swap_slots(nullptr, 0, 1);  // (special 2: Rena to slot 0; Claude is gone)
    CHECK(pg.party() == "2,7,3,4,6,8,9,11");
    pg.at(9, 4, 2000);
    CHECK(ap::party_free_play_step().empty() && pg.party() == "2,7,3,4,6,8,9,11");
    ap::stop();
  }
  // The tournament (477, Claude's game, step 4400) with every reserve slot kept: the starting
  // member (Ashton) set aside in slot 0, before the hero. Dias borrows slot 0; the scene's A 0x15
  // before the fight and after Claude's join moves him to slot 7 and Rena into slot 0; the free
  // play after it puts Dias back first, so Ashton's data lands on him, not on Rena.
  auto compact = [&] {  // (script A 0x15, script_table_a.cpp)
    auto id = [&](int i) {
      s16 v;
      std::memcpy(&v, pg.sg.party.data() + i * 0x60, 2);
      return v;
    };
    for (int i = 0; i < 8; ++i) {
      if (id(i) >= 1) continue;
      int j = i + 1;
      while (j < 8 && id(j) <= 0) ++j;
      if (j == 8) return;
      field::party_swap_slots(nullptr, i, j);
    }
  };
  CHECK(ap::load_seed_text(party_seed(open_options("claude", ", \"starting_character\": \"Ashton\"")), &s, &error));
  ap::start_with(s);
  pg.claude_game(true);
  pg.members({7, 1, 2, 3, 4, 6, 8, 9});
  pg.received({});
  pg.at(1, 19, 0);
  CHECK(ap::party_free_play_step().empty());  // (Ashton is in: the start given)
  field::party_remove_member(nullptr, 6);     // (468 / 469 set him aside)
  pg.at(477, 19, 4400);
  pg.leave(1);
  CHECK(!pg.join(5, 0x63d0, 0x6704));
  CHECK(pg.party() == "5,-1,2,3,4,6,8,9" && ap::state().stash.size() == 1 && ap::state().stash[0].by == 5);
  compact();
  CHECK(pg.party() == "5,2,3,4,6,8,9,-1");
  pg.leave(5);
  CHECK(pg.join(1, 0x63d0, 0x6b78));
  compact();
  CHECK(pg.party() == "2,3,4,6,8,9,1,-5" && ap::state().stash.size() == 1);
  CHECK(ap::party_free_play_step().empty());
  CHECK(pg.party() == "-7,3,4,6,8,9,1,2" && ap::state().stash.empty() && ap::state().temporary.empty());
  ap::stop();
  // "by": absent or out of range reads as 0 (unknown).
  ap::Json j;
  const std::string bytes(0x130 * 2, '0');
  for (const char* by : {"", ", \"by\": 13", ", \"by\": -1", ", \"by\": 5"}) {
    CHECK(ap::Json::parse(std::string("{\"format\": \"apso2-save\", \"version\": 2, \"stash\": [{\"slot\": 1, \"bytes\": \"") +
                              bytes + "\"" + by + "}]}",
                          &j, &error));
    ap::SaveState back;
    CHECK(ap::sidecar_from_json(j, &back, &error) && back.stash.size() == 1);
    CHECK(back.stash[0].by == (std::string(by) == ", \"by\": 5" ? 5 : 0));
  }
}

// [shingofix agent] The tournament's kept Dias (the user's option b): a received or starting Dias
// keeps his own data through map 477's specials 0xc / 0xd: his slot saved before 0xc, his record
// before the first 0xd (his own until then), both back after his last fight there (Claude's game
// 10200, Rena's 16500), with a save and load in between (the sidecar's `tournament_dias`, version
// 3, under the save guard). The story's Dias and a game without a seed: as before.
TEST(ap_tournament_dias) {
  ap::stop();
  PartyGame pg;
  ap::Seed s;
  std::string error;
  auto slot_bytes = [&](int i) { return std::vector<u8>(pg.sg.party.begin() + i * 0x60, pg.sg.party.begin() + (i + 1) * 0x60); };
  auto rec_bytes = [&](int i) { return std::vector<u8>(pg.rec.data.begin() + i * 0xD0, pg.rec.data.begin() + (i + 1) * 0xD0); };
  auto put16 = [](u8* p, int v) { const s16 x = static_cast<s16>(v); std::memcpy(p, &x, 2); };
  auto put32 = [](u8* p, s32 v) { std::memcpy(p, &v, 4); };
  auto level = [&](int i) { s16 v; std::memcpy(&v, pg.sg.party.data() + i * 0x60 + 0x28, 2); return static_cast<int>(v); };
  // Dias in slot 3: level 50, EXP 123456, HP 3000, a weapon (record +0xc) and a name.
  auto dias = [&] {
    pg.members({1, 2, 3, 5});
    u8* p = pg.sg.party.data() + 3 * 0x60;
    put16(p + 0x28, 50);
    put32(p + 0x10, 123456);
    put32(p + 0x14, 3000);
    put32(p + 0x18, 3000);
    put16(pg.rec.data.data() + 3 * 0xD0 + 0xC, 0x1234);
    std::memcpy(pg.rec.data.data() + 3 * 0xD0 + 0x24, "Dias", 5);
  };
  const fs::path dir = pg.dir / "td";
  fs::create_directories(dir);
  const std::string card = (dir / "memcard1.mcd").string();
  std::vector<u8> save(0x2000);
  for (size_t i = 0; i < save.size(); ++i) save[i] = static_cast<u8>(i * 5 + 1);
  save[0x21A] = 0x00;
  save[0x21B] = 0x10;

  // Claude's game, Dias received.
  CHECK(ap::load_seed_text(party_seed(open_options("claude")), &s, &error));
  ap::start_with(s);
  pg.claude_game(true);
  dias();
  pg.received({5});
  const std::vector<u8> slot_in = slot_bytes(3);
  pg.at(477, 19, 4400);
  field::field_special_command(nullptr, 0xC);  // (after his first fight: his fixed stats)
  CHECK(level(3) == 35 && ap::state().tournament_dias == slot_in);
  CHECK(ap::sidecar_json(ap::state())["version"].as_int(0) == 3);
  // His record is still his own: the player's change to it counts.
  put16(pg.rec.data.data() + 3 * 0xD0 + 0xC, 0x2222);
  const std::vector<u8> rec_in = rec_bytes(3);
  pg.at(477, 19, 6200);
  field::field_special_command(nullptr, 0xD);  // (after his second: his starting data again)
  CHECK(ap::state().tournament_dias.size() == 0x130);
  CHECK(std::vector<u8>(ap::state().tournament_dias.begin() + 0x60, ap::state().tournament_dias.end()) == rec_in);
  CHECK(rec_bytes(3) != rec_in);  // (0xd wrote his record: at least the name in game_state +0x78)
  // A save between fights, and a load of it into a fresh game: the copy comes from the sidecar.
  ap::on_saved(card, 0, save.data(), save.size());
  ap::SaveState back;
  CHECK(ap::read_sidecar(ap::sidecar_path(card, 0), &back, &error));
  CHECK(back.tournament_dias == ap::state().tournament_dias && !back.tournament_dias_hash.empty());
  CHECK(ap::load_allowed(card, 0, save.data(), save.size()));
  {
    // Edited, stripped of its hash, or moved to another save: refused.
    ap::SaveState other = back;
    other.tournament_dias[0x28] ^= 1;
    CHECK(ap::write_sidecar(ap::sidecar_path(card, 1), other, &error));
    CHECK(!ap::load_allowed(card, 1, save.data(), save.size()));
    other = back;
    other.tournament_dias_hash.clear();
    CHECK(ap::write_sidecar(ap::sidecar_path(card, 1), other, &error));
    CHECK(!ap::load_allowed(card, 1, save.data(), save.size()));
    std::vector<u8> moved = save;
    moved[0x400] ^= 1;
    other = back;
    other.save_hash = util::save_digest(moved.data(), moved.size());
    CHECK(ap::write_sidecar(ap::sidecar_path(card, 1), other, &error));
    CHECK(!ap::load_allowed(card, 1, moved.data(), moved.size()));
  }
  ap::stop();
  ap::start_with(s);
  pg.claude_game(true);
  CHECK(ap::state().tournament_dias.empty());
  ap::on_loaded(card, 0);
  CHECK(ap::state().tournament_dias == back.tournament_dias && ap::state().tournament_dias_hash.empty());
  // Free play between fights keeps it; the fight at 10200, his last: his own data back.
  CHECK(ap::party_free_play_step().empty() && ap::state().tournament_dias.size() == 0x130);
  pg.at(477, 19, 10200);
  field::field_special_command(nullptr, 0xD);
  CHECK(slot_bytes(3) == slot_in && rec_bytes(3) == rec_in && ap::state().tournament_dias.empty());
  CHECK(ap::sidecar_json(ap::state())["tournament_dias"].is_null() && ap::sidecar_json(ap::state())["version"].as_int(0) == 2);
  ap::stop();

  // Rena's game, Dias the starting character: 10200's 0xd is not his last (16500 is); set aside
  // after it, he comes back as he was, in reserve.
  CHECK(ap::load_seed_text(party_seed(open_options("rena", ", \"starting_character\": \"Dias\"")), &s, &error));
  ap::start_with(s);
  pg.claude_game(false);
  dias();
  pg.received({});
  const std::vector<u8> slot_r = slot_bytes(3), rec_r = rec_bytes(3);
  pg.at(477, 19, 6500);
  field::field_special_command(nullptr, 0xC);
  pg.at(477, 19, 10200);
  field::field_special_command(nullptr, 0xD);
  CHECK(ap::state().tournament_dias.size() == 0x130 && level(3) != 50);
  pg.at(477, 19, 16500);
  field::field_special_command(nullptr, 0xD);
  CHECK(slot_bytes(3) == slot_r && rec_bytes(3) == rec_r && ap::state().tournament_dias.empty());
  // Held past the tournament (not seen): the next free play puts him back, set aside as he is.
  pg.at(477, 19, 6500);
  field::field_special_command(nullptr, 0xC);
  CHECK(!ap::state().tournament_dias.empty());
  field::party_remove_member(nullptr, 4);
  pg.at(1, 20, 0);
  ap::party_free_play_step();
  std::vector<u8> aside = slot_r;
  aside[0] = 0xFB;  // (-5: set aside)
  aside[1] = 0xFF;
  CHECK(slot_bytes(3) == aside && ap::state().tournament_dias.empty());
  ap::stop();

  // The story's Dias (not received, no starting Dias): rewritten as before, nothing held.
  CHECK(ap::load_seed_text(party_seed(open_options("claude")), &s, &error));
  ap::start_with(s);
  pg.claude_game(true);
  dias();
  pg.received({});
  pg.at(477, 19, 4400);
  field::field_special_command(nullptr, 0xC);
  CHECK(level(3) == 35 && ap::state().tournament_dias.empty());
  pg.at(477, 19, 10200);
  field::field_special_command(nullptr, 0xD);
  CHECK(ap::state().tournament_dias.empty() && level(3) != 50);
  ap::stop();
  // No seed: as before.
  dias();
  pg.at(477, 19, 4400);
  field::field_special_command(nullptr, 0xC);
  CHECK(level(3) == 35);

  // The sidecar: absent reads as none; a wrong size, bad hex or another member is unreadable.
  ap::Json j;
  const std::string head = "{\"format\": \"apso2-save\", \"version\": 3, \"tournament_dias\": \"";
  const std::string ok = "0500" + std::string(0x60 * 2 - 4, '0');  // (Dias's slot alone)
  for (const auto& [text, good] : std::vector<std::pair<std::string, bool>>{
           {ok, true}, {"fbff" + std::string(0x130 * 2 - 4, '0'), true}, {ok.substr(0, 0x5F * 2), false},
           {"0700" + ok.substr(4), false}, {"zz" + ok.substr(2), false}, {ok + "00", false}}) {
    CHECK(ap::Json::parse(head + text + "\"}", &j, &error));
    ap::SaveState st;
    CHECK(ap::sidecar_from_json(j, &st, &error) == good);
  }
  CHECK(ap::Json::parse("{\"format\": \"apso2-save\", \"version\": 2}", &j, &error));
  ap::SaveState none;
  CHECK(ap::sidecar_from_json(j, &none, &error) && none.tournament_dias.empty());
}

// The starting character: joins at the first free play, before the starting items, and leads;
// the picked game's hero means nobody; no lockouts, also without character_items.
TEST(ap_v2_starting_character) {
  ap::stop();
  PartyGame pg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(party_seed(open_options("claude", ", \"starting_character\": \"Ashton\"")), &s, &error));
  ap::start_with(s);
  pg.claude_game(true);
  pg.members({1});
  pg.at(7, 3, 0);
  ap::on_new_game();
  CHECK(ap::starting_member() == 7);
  CHECK(ap::party_free_play_step() == "Ashton joined the party");
  CHECK(pg.party() == "1,7" && pg.sg.config[0x41] == 1 && ap::state().start_given);
  CHECK(ap::party_free_play_step().empty());  // (once)
  // Counts as received: a listed or story join of hers while present is a no-op, a split's
  // leave brings her back afterwards.
  pg.at(71, 10, 0);
  CHECK(pg.join(7, 0x63d0, 0x7348) && pg.party() == "1,7");
  pg.at(193, 22, 3000);
  pg.leave(7);
  CHECK(ap::party_free_play_step().empty() && pg.party() == "1,-7");
  pg.at(193, 22, 5100);
  CHECK(ap::party_free_play_step() == "Ashton joined the party" && pg.party() == "1,7");
  ap::stop();
  // story_route "any", starting Rena: she joins Claude's game; Rena's game: nobody (the hero).
  CHECK(ap::load_seed_text(party_seed(open_options("any", ", \"starting_character\": \"Rena\"")), &s, &error));
  ap::start_with(s);
  pg.members({2});
  pg.claude_game(false);
  ap::on_new_game();
  CHECK(ap::starting_member() == 0 && ap::party_free_play_step().empty() && pg.party() == "2");
  pg.members({1});
  pg.claude_game(true);
  ap::on_new_game();
  CHECK(ap::starting_member() == 2 && ap::party_free_play_step() == "Rena joined the party" && pg.party() == "1,2");
  ap::stop();
  // Without character_items (party_rules "story"): Leon starts Rena's game (no lockout); the
  // story's recruits join as in the original.
  CHECK(ap::load_seed_text(party_seed("{\"world_mode\": \"open_world\", \"party_rules\": \"story\", \"story_route\": \"rena\", "
                                      "\"starting_character\": \"Leon\"}"),
                           &s, &error));
  ap::start_with(s);
  pg.members({2});
  pg.claude_game(false);
  ap::on_new_game();
  CHECK(ap::party_free_play_step() == "Leon joined the party" && pg.party() == "2,8");
  pg.at(124, 15, 1000);
  CHECK(pg.join(3, 0x63d0, 0x7058) && pg.party() == "2,8,3");
  ap::stop();
}

// [charselect agent] starting_character "any" or a list: the player picks at New Game's character
// select. The seed's choices ("any" = all 12 but exclude_characters), the pick into the state and
// the sidecar, and the pick as the starting character (the hero: nobody extra).
TEST(ap_v2_starting_character_choice) {
  ap::stop();
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(
      party_seed(open_options("claude", ", \"starting_character\": \"any\", \"exclude_characters\": [\"Bowman\", \"Opera\"]")), &s,
      &error));
  CHECK(s.starting_character == 0 && s.starting_choices == std::vector<int>({1, 2, 3, 5, 6, 7, 8, 10, 11, 12}));
  CHECK(ap::load_seed_text(party_seed(open_options("claude", ", \"starting_character\": [\"Celine\", \"Ashton\", 5, \"Celine\"]")), &s,
                           &error));
  CHECK(s.starting_character == 0 && s.starting_choices == std::vector<int>({3, 7, 5}));
  CHECK(!ap::load_seed_text(party_seed(open_options("claude", ", \"starting_character\": [\"Celine\", \"Nobody\"]")), &s, &error));
  CHECK(error.find("a list of characters' names expected") != std::string::npos);
  CHECK(ap::load_seed_text(party_seed(open_options("claude", ", \"starting_character\": \"Celine\"")), &s, &error));
  CHECK(s.starting_character == 3 && s.starting_choices.empty());

  PartyGame pg;
  CHECK(ap::load_seed_text(
      party_seed(open_options("claude", ", \"starting_character\": \"any\", \"exclude_characters\": [\"Bowman\", \"Opera\"]")), &s,
      &error));
  ap::start_with(s);
  CHECK(ap::seed_starting_character() == 0 && ap::starting_choices().size() == 10);
  pg.claude_game(true);
  pg.members({1});
  pg.at(7, 3, 0);
  // Celine picked: she starts (joins at the first free play and leads), as a named one would.
  ap::set_chosen_start(3);
  ap::on_new_game();
  CHECK(ap::state().chosen_start == 3 && ap::starting_member() == 3);
  CHECK(ap::party_free_play_step() == "Celine joined the party");
  CHECK(pg.party() == "1,3" && pg.sg.config[0x41] == 1 && ap::state().start_given);
  // The sidecar keeps the pick (key "starting_character"); absent or out of range: none.
  ap::SaveState back;
  CHECK(ap::sidecar_from_json(ap::sidecar_json(ap::state()), &back, &error) && back.chosen_start == 3);
  CHECK(ap::sidecar_json(ap::state())["starting_character"].as_int(0) == 3);
  ap::Json j;
  CHECK(ap::Json::parse("{\"format\": \"apso2-save\", \"version\": 1}", &j, &error));
  CHECK(ap::sidecar_from_json(j, &back, &error) && back.chosen_start == 0);
  CHECK(ap::Json::parse("{\"format\": \"apso2-save\", \"version\": 1, \"starting_character\": 13}", &j, &error));
  CHECK(ap::sidecar_from_json(j, &back, &error) && back.chosen_start == 0);
  // The hero picked (the screen passes 0; the hero's own id means the same): nobody extra, and
  // nothing in the sidecar.
  pg.members({1});
  ap::set_chosen_start(0);
  ap::on_new_game();
  CHECK(ap::state().chosen_start == 0 && ap::starting_member() == 0 && ap::party_free_play_step().empty() && pg.party() == "1");
  CHECK(ap::sidecar_json(ap::state())["starting_character"].is_null());
  ap::set_chosen_start(1);
  ap::on_new_game();
  CHECK(ap::starting_member() == 0 && ap::party_free_play_step().empty() && pg.party() == "1");
  // A pick is for one New Game only: the next starts without it.
  ap::on_new_game();
  CHECK(ap::state().chosen_start == 0 && ap::starting_member() == 0);
  ap::stop();
  // A seed without choices ignores a pick (its own named member, or none).
  CHECK(ap::load_seed_text(party_seed(open_options("claude", ", \"starting_character\": \"Leon\"")), &s, &error));
  ap::start_with(s);
  pg.members({1});
  ap::set_chosen_start(3);
  ap::on_new_game();
  CHECK(ap::state().chosen_start == 0 && ap::starting_member() == 8);
  ap::stop();
}

// While the party is set aside for the hero alone (a Private Action's flags 2 / 4, or the map
// library's isolate routine's flag 746), the party rules leave it alone.
TEST(ap_v2_party_isolated) {
  ap::stop();
  PartyGame pg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(party_seed(open_options("claude")), &s, &error));
  ap::start_with(s);
  pg.claude_game(true);
  pg.members({1, -3});
  pg.received({3});
  pg.at(18, 10, 0);
  for (const int flag : {2, 4, 746}) {
    set_flag(pg.sg, flag, true);
    CHECK(ap::party_free_play_step().empty() && pg.party() == "1,-3");
    set_flag(pg.sg, flag, false);
  }
  CHECK(ap::party_free_play_step() == "Celine joined the party" && pg.party() == "1,3");
  ap::stop();
}

// ---- The join level and skillsanity [skillsanity2] --------------------------------------------

namespace {

void put_level(std::vector<u8>& party, int slot, int id, int level) {
  const s16 v = static_cast<s16>(id), l = static_cast<s16>(level);
  std::memcpy(party.data() + slot * 0x60, &v, 2);
  std::memcpy(party.data() + slot * 0x60 + 0x28, &l, 2);
}
s16 get16(const u8* p, int off) {
  s16 v;
  std::memcpy(&v, p + off, 2);
  return v;
}
s32 get32(const u8* p, int off) {
  s32 v;
  std::memcpy(&v, p + off, 4);
  return v;
}

std::string skills_seed(const std::string& mode) {
  std::string t = replace(kSeed, "\"version\": 1", "\"version\": 2");
  t = replace(t, "\"options\": {\"story_route\": \"claude\", \"goal\": \"indalecio\"}",
              "\"options\": {\"story_route\": \"claude\", \"skillsanity\": \"" + mode + "\"}");
  t = replace(t, "  \"locations\": [\n",
              "  \"locations\": [\n"
              "    {\"id\": 1373634816, \"name\": \"Skill Guild: Sensibility 2 (1 of 4)\", \"check\": {\"type\": \"guild\", \"set\": 5, \"index\": 0},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"skill\", \"skill\": 11, \"name\": \"Perseverance\"}},\n"
              "    {\"id\": 1373634817, \"name\": \"Skill Guild: Sensibility 2 (2 of 4)\", \"check\": {\"type\": \"guild\", \"set\": 5, \"index\": 1},\n"
              "     \"item\": {\"player\": 2, \"player_name\": \"Alice\", \"name\": \"Hookshot\"}},\n"
              "    {\"id\": 1375731969, \"name\": \"Claude Lv 7: Shooting Stars\", \"check\": {\"type\": \"learn\", \"character\": \"Claude\", \"index\": 1, \"level\": 7},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"move\", \"character\": 3, \"index\": 3, \"name\": \"Celine: Thunderbolt\"}},\n"
              "    {\"id\": 1375733768, \"name\": \"Precis: Holoprojector (Holo-holograph)\", \"check\": {\"type\": \"learn\", \"character\": 6, \"index\": 8},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"fol\", \"amount\": 10}},\n"
              "    {\"id\": 1373637632, \"name\": \"Cave of Trials Level 11: Float\", \"check\": {\"type\": \"learn\", \"skill\": 46},\n"
              "     \"item\": {\"player\": 1, \"kind\": \"skill\", \"skill\": 46, \"name\": \"Float\"}},\n");
  return t;
}

}  // namespace

TEST(ap_join_target_level) {
  std::vector<u8> party(0x300, 0);
  CHECK(ap::join_target_level(party.data(), 0) == 0);  // nobody else
  put_level(party, 0, 1, 12);
  put_level(party, 3, 9, 40);  // the joiner
  CHECK(ap::join_target_level(party.data(), 3) == 12);
  put_level(party, 1, 2, 20);
  CHECK(ap::join_target_level(party.data(), 3) == 16);  // an even count: the middle two, rounded down
  put_level(party, 2, 3, 7);
  CHECK(ap::join_target_level(party.data(), 3) == 12);  // 7, 12, 20
  put_level(party, 4, -4, 90);                          // set aside: not counted
  CHECK(ap::join_target_level(party.data(), 3) == 12);
  put_level(party, 4, 4, 90);
  put_level(party, 5, 5, 91);
  CHECK(ap::join_target_level(party.data(), 3) == 20);  // 7 12 20 90 91
  put_level(party, 0, 1, 300);
  put_level(party, 1, 2, 300);
  put_level(party, 2, 3, 300);
  CHECK(ap::join_target_level(party.data(), 3) == 255);  // clamped
}

// Noel at 40 (his starting data) scaled down to 10: the growth table's mean gains taken off,
// floored at the heroes' level-1 values (docs/handoff/skillsanity.md, the user's option 1).
TEST(ap_join_scale_down) {
  std::vector<u8> slot(0x60, 0), rec(0xD0, 0);
  auto p32 = [&](int off, s32 v) { std::memcpy(slot.data() + off, &v, 4); };
  auto p16 = [&](u8* b, int off, s16 v) { std::memcpy(b + off, &v, 2); };
  p32(0x14, 2700), p32(0x18, 2700), p32(0x1C, 2700);
  for (int off : {0x20, 0x22, 0x24}) p16(slot.data(), off, 340);
  p16(slot.data(), 0x2A, 61), p16(slot.data(), 0x30, 55), p16(slot.data(), 0x3C, 50), p16(slot.data(), 0x48, 28);
  p16(slot.data(), 0x28, 40), p16(rec.data(), 6, 30), p16(rec.data(), 0x1A, 290);
  p32(0x10, 151862);
  ap::Growth noel;  // {range, base}: STR (2,1), INT (3,1), DEX (2,1), CON (3,0), +0x48 (0,0), record +6 (1,0)
  const u8 range[6] = {2, 3, 2, 3, 0, 1}, base[6] = {1, 1, 1, 0, 0, 0};
  std::memcpy(noel.range, range, 6), std::memcpy(noel.base, base, 6);
  ap::scale_down_stats(slot.data(), rec.data(), 10, noel);
  CHECK(get16(slot.data(), 0x28) == 10 && get16(slot.data(), 0x26) == 10);
  CHECK(get32(slot.data(), 0x10) == 1470);  // the EXP level 10 starts at
  CHECK(get32(slot.data(), 0x14) == 645 && get32(slot.data(), 0x1C) == 645);
  CHECK(get16(slot.data(), 0x20) == 190 && get16(slot.data(), 0x24) == 190);
  CHECK(get16(slot.data(), 0x2A) == 16);  // STR 61 - 30 * 1.5
  CHECK(get16(slot.data(), 0x3C) == 10);  // DEX floored (50 - 45)
  CHECK(get16(slot.data(), 0x42) == 0);   // INT: 0 at every join, never raised
  CHECK(get16(slot.data(), 0x30) == 25);  // CON 55 - 30
  CHECK(get16(slot.data(), 0x48) == 28 && get16(rec.data(), 6) == 30);  // no gain in the table
  CHECK(get16(rec.data(), 0x1A) == 290);  // SP as he joins
  // Down to 1: the floors.
  ap::scale_down_stats(slot.data(), rec.data(), 1, noel);
  CHECK(get32(slot.data(), 0x10) == 0 && get16(slot.data(), 0x28) == 1);
  CHECK(get32(slot.data(), 0x14) == 292 && get16(slot.data(), 0x2A) == 5);
}

TEST(ap_skillsanity_seed) {
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(skills_seed("insanity"), &s, &error));
  CHECK(s.skillsanity == 3);
  const ap::Location* guild = nullptr;
  for (const ap::Location& l : s.locations)
    if (l.id == 1373634816) guild = &l;
  CHECK(guild != nullptr && guild->type == "guild" && guild->skill_set == 5 && guild->item.item.kind == "skill" &&
        guild->item.item.skill == 11);
  CHECK(ap::load_seed_text(skills_seed("none"), &s, &error) && s.skillsanity == 0);
  CHECK(ap::load_seed_text(skills_seed("skills_only"), &s, &error) && s.skillsanity == 1);
  CHECK(!ap::load_seed_text(skills_seed("all"), &s, &error));
  CHECK(error.find("options.skillsanity") != std::string::npos);
  CHECK(!ap::load_seed_text(replace(skills_seed("insanity"), "\"index\": 3, \"name\": \"Celine", "\"index\": 40, \"name\": \"Celine"), &s, &error));
  CHECK(!ap::load_seed_text(replace(skills_seed("insanity"), "\"skill\": 46}", "\"skill\": 45}"), &s, &error));
}

TEST(ap_skillsanity_checks) {
  ap::stop();
  ScratchGame sg;
  Records rec;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(skills_seed("insanity"), &s, &error));
  ap::start_with(s);
  CHECK(ap::skills_shuffled() && ap::specials_shuffled());
  // A guild set bought: its checks (ours queued, Alice's sent); its skills are not opened by it.
  set_flag(sg, 705, true);
  ap::on_flag_set(705);
  CHECK(ap::is_checked(1373634816) && ap::is_checked(1373634817));
  CHECK(!field::skill_book_allowed(0xB));
  ap::receive_skill(0xB);
  CHECK(field::skill_book_allowed(0xB) && !field::skill_book_allowed(0xC));
  // Learn points: a location is its check (once), no move taught; others as the original.
  CHECK(ap::on_learn_point(1, 1) && ap::is_checked(1375731969));
  CHECK(ap::on_learn_point(1, 1));
  CHECK(!ap::on_learn_point(1, 3));
  CHECK(ap::learn_point_state(6, 8) == 1);
  ap::on_item_learn_point(6, 8);
  CHECK(ap::learn_point_state(6, 8) == 0 && ap::is_checked(1375733768));
  CHECK(ap::on_float_prize() && ap::is_checked(1373637632));
  // Received moves: known at once in the party, else at the join.
  sg.member(0, 1, 100);
  ap::receive_move(1, 4);
  CHECK(rec.data[0x3C + 4] == 1);
  ap::receive_move(3, 3);
  CHECK(ap::state().moves == (std::vector<int>{1 * 256 + 4, 3 * 256 + 3}));
  sg.member(1, 3, 100);
  const s16 level = 8;
  std::memcpy(sg.party.data() + 0x60 + 0x28, &level, 2);
  ap::on_member_init(1);
  CHECK(rec.data[0xD0 + 0x3C + 3] == 1);
  // The sidecar keeps them.
  ap::SaveState back;
  CHECK(ap::sidecar_from_json(ap::sidecar_json(ap::state()), &back, &error));
  CHECK(back.skills == std::vector<int>{0xB} && back.moves == ap::state().moves);
  ap::stop();
  // skillsanity none: the guild flags open the skills (the original's table: a disc test), no
  // learn point is a check.
  CHECK(ap::load_seed_text(skills_seed("none"), &s, &error));
  ap::start_with(s);
  CHECK(!ap::skills_shuffled() && !ap::specials_shuffled());
  CHECK(!ap::on_learn_point(1, 1) && !ap::on_float_prize() && ap::learn_point_state(6, 8) == -1);
  ap::on_flag_set(705);
  CHECK(!ap::is_checked(1373634816));
  set_flag(sg, 705, false);
  ap::stop();
}

// ---- A full inventory never holds up the give queue [queuebypass agent] -----------------------
// (docs/ap/seed_format.md "The save sidecar"; the user, 2026-10-27: "The ability to bypass a "full"
// item slot and continue to recieve items is a mandatory fix.") An item whose kind is full (20 at
// most) is set aside with one banner; the items behind it (the server's too) go on; it is given
// once there is room, the set-aside ones in the order they came; a save and a load keep it owed
// (the sidecar's `pending`), never given twice; a count that fits in part gives what fits.

namespace {
const char* kFullSeed = R"seed({
  "format": "apso2", "version": 1, "seed": "full-1", "slot": {"name": "Claude", "id": 1},
  "items": {"80001": {"name": "Blackberries", "kind": "item", "game_id": 756},
            "80002": {"name": "100 Fol", "kind": "fol", "amount": 100},
            "5300003": {"name": "Hilton Pass", "kind": "key", "key": 3}},
  "locations": [
    {"id": 3001, "name": "reward 1", "check": {"type": "boss", "encounter": 19},
     "item": {"player": 1, "kind": "item", "game_id": 756, "name": "Blackberries"}},
    {"id": 3002, "name": "reward 2", "check": {"type": "boss", "encounter": 19},
     "item": {"player": 1, "kind": "item", "game_id": 224, "count": 5, "name": "Lavender"}}
  ]})seed";

int held(const ScratchGame& sg, int item) {
  const int i = field::inventory_find(sg.g.inventory, item);
  return i < 0 ? 0 : sg.inv[static_cast<size_t>(i) * 2 + 1] >> 2 & 0x1F;
}
void use_one(ScratchGame& sg, int item) {
  const int i = field::inventory_find(sg.g.inventory, item);
  CHECK(i >= 0);
  if (i >= 0) field::inventory_remove_at(sg.g.inventory, i, 1);
}
// Free play's next give: its banners as expected (else both printed).
bool step_is(const std::string& want) {
  const std::string got = ap::give_free_play_step();
  if (got != want) std::fprintf(stderr, "  give step: \"%s\", expected \"%s\"\n", got.c_str(), want.c_str());
  return got == want;
}
ap_net::Event server_items(std::vector<int64_t> ids) {
  ap_net::Event e;
  e.kind = ap_net::Event::kItems;
  for (size_t i = 0; i < ids.size(); ++i) e.items.push_back({ids[i], 100 + static_cast<int64_t>(i), 2, 0, static_cast<int>(i)});
  return e;
}
}  // namespace

TEST(ap_full_inventory) {
  ap::stop();
  ScratchGame sg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(kFullSeed, &s, &error));
  ap::start_with(s);
  std::vector<u8> save(0x2000);
  for (size_t i = 0; i < save.size(); ++i) save[i] = static_cast<u8>(i * 7 + 3);
  save[0x21A] = 0x00;
  save[0x21B] = 0x10;
  const fs::path dir = scratch("full");
  const std::string card = (dir / "memcard1.mcd").string();
  std::vector<int64_t> sent;
  int goals = 0;
  std::vector<std::string> deaths;
  std::vector<ap_net::Event> events;
  auto connect = [&] {
    ap::connect(std::make_unique<FakeLink>(&sent, &goals, &deaths, &events));
    ap_net::Event c;
    c.kind = ap_net::Event::kConnected;
    c.seed_name = "full-1";
    events.push_back(c);
    events.push_back(server_items({80002, 80001}));  // the server: 100 Fol, then Blackberries
    ap::vblank();
  };

  // 20 Blackberries held. Received: a Blackberry and a key from Bob (the console's queue), then
  // the server's 100 Fol and a Blackberry from Alice. The key and the Fol arrive; both Blackberries
  // wait, one banner for them.
  field::inventory_add(sg.g.inventory, 756, 20, 1);
  CHECK(ap::receive_by_name_or_id("Blackberries", "Bob", &error));
  CHECK(ap::receive_by_name_or_id("key:3", "Bob", &error));
  connect();
  CHECK(ap::connected() && ap::pending_count() == 4);
  CHECK(step_is("Inventory full: Blackberries waiting"));
  CHECK(step_is("Received Hilton Pass from Bob"));
  CHECK(ap::has_key(3));
  CHECK(step_is("Received 100 Fol from Alice"));
  CHECK(sg.fol() == 100 && ap::state().received_index == 1);
  CHECK(step_is(""));  // (the server's Blackberry: set aside, no second banner)
  CHECK(ap::state().received_index == 2 && ap::pending_count() == 2 && held(sg, 756) == 20);
  CHECK(step_is(""));  // (nothing more: no retry banner)
  CHECK(ap::status_text().find("inventory full, set aside: Blackberries, Blackberries") != std::string::npos);

  // A save keeps both owed (the sidecar's `pending`, its usual guard); a new session loads them:
  // set aside again at the first try (one banner), the server's list again gives nothing twice.
  ap::on_saved(card, 0, save.data(), save.size());
  ap::SaveState back;
  CHECK(ap::read_sidecar(ap::sidecar_path(card, 0), &back, &error) && back.pending.size() == 2 && back.received_index == 2);
  CHECK(back.pending[0].item.game_id == 756 && back.pending[0].from == "Bob" && back.pending[0].location == -1);
  CHECK(back.pending[1].item.game_id == 756 && back.pending[1].from == "Alice" && !back.pending[1].aside);
  CHECK(ap::load_allowed(card, 0, save.data(), save.size()));
  ap::stop();
  ap::start_with(s);
  ap::on_loaded(card, 0);
  connect();
  CHECK(ap::pending_count() == 2 && ap::state().received_index == 2);
  CHECK(step_is("Inventory full: Blackberries waiting"));
  CHECK(step_is(""));
  CHECK(step_is(""));
  CHECK(sg.fol() == 100 && held(sg, 756) == 20);

  // One Blackberry used: the first owed one arrives (its usual banner), the other waits on; another
  // used: it arrives too. Nothing more.
  use_one(sg, 756);
  CHECK(step_is("Received Blackberries from Bob"));
  CHECK(held(sg, 756) == 20 && ap::pending_count() == 1);
  CHECK(step_is(""));
  use_one(sg, 756);
  CHECK(step_is("Received Blackberries from Alice"));
  CHECK(held(sg, 756) == 20 && ap::pending_count() == 0 && sg.fol() == 100 && ap::state().received_index == 2);
  CHECK(ap::status_text().find("set aside") == std::string::npos);
  CHECK(step_is(""));

  // A count that fits in part (a boss's 5 Lavender, 17 held): 3 go in, 2 wait (one banner each);
  // the boss's Blackberry in front of it waits too. A save keeps the rest as the item itself.
  ap::stop();
  ap::start_with(s);
  field::inventory_add(sg.g.inventory, 224, 17, 1);
  ap::on_battle_exit(19, 0);
  CHECK(ap::is_checked(3001) && ap::is_checked(3002) && ap::pending_count() == 2);
  CHECK(step_is("Inventory full: Blackberries waiting"));
  CHECK(step_is("Got 3 x Lavender / Inventory full: 2 x Lavender waiting"));
  CHECK(held(sg, 224) == 20 && ap::pending_count() == 2);
  ap::on_saved(card, 1, save.data(), save.size());
  CHECK(ap::read_sidecar(ap::sidecar_path(card, 1), &back, &error) && back.pending.size() == 2);
  CHECK(back.pending[0].location == 3001);
  CHECK(back.pending[1].location == -1 && back.pending[1].item.game_id == 224 && back.pending[1].item.count == 2 &&
        back.pending[1].found);
  ap::stop();
  ap::start_with(s);
  ap::on_loaded(card, 1);
  CHECK(ap::pending_count() == 2);
  CHECK(step_is("Inventory full: Blackberries waiting"));
  CHECK(step_is("Inventory full: 2 x Lavender waiting"));
  use_one(sg, 224);
  CHECK(step_is("Got Lavender"));  // (one fits; the other waits on, quietly)
  CHECK(held(sg, 224) == 20 && ap::pending_count() == 2);
  use_one(sg, 224);
  use_one(sg, 756);
  CHECK(step_is("Got Blackberries"));  // (the set-aside ones in order)
  CHECK(step_is("Got Lavender"));
  CHECK(held(sg, 224) == 20 && held(sg, 756) == 20 && ap::pending_count() == 0);

  // Everything given: the sidecar as before (version 2, no `pending`).
  ap::on_saved(card, 2, save.data(), save.size());
  const std::string text = read_text(ap::sidecar_path(card, 2));
  CHECK(text.find("\"version\": 2") != std::string::npos && text.find("\"pending") == std::string::npos);
  ap::stop();
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// A character item behind an item set aside: it is given and the member joins; a member waiting
// to join (a Private Action) holds up no item either.
TEST(ap_full_inventory_join) {
  ap::stop();
  PartyGame pg;
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(party_seed(open_options("claude")), &s, &error));
  ap::start_with(s);
  pg.claude_game(true);
  pg.members({1});
  pg.at(18, 10, 0);
  CHECK(ap::party_free_play_step().empty());  // (the start's items and Fol)
  field::inventory_add(pg.sg.g.inventory, 21, 20, 1);
  CHECK(ap::receive_by_name_or_id("Revival Card", "Bob", &error));
  CHECK(ap::receive_by_name_or_id("Celine", "Alice", &error));
  CHECK(step_is("Inventory full: Revival Card waiting"));
  CHECK(step_is("Received Celine from Alice"));
  CHECK(step_is("Celine joined the party"));
  CHECK(pg.party() == "1,3" && ap::pending_count() == 1);
  // Bowman received in a Private Action (script flag 2): his join waits, the Fol behind him comes.
  CHECK(ap::receive_by_name_or_id("Bowman", "Alice", &error));
  CHECK(ap::receive_by_name_or_id("5009000", "Bob", &error));
  set_flag(pg.sg, 2, true);
  CHECK(step_is("Received Bowman from Alice"));
  const s32 fol = pg.sg.fol();
  CHECK(step_is("Received 500 Fol from Bob"));
  CHECK(pg.sg.fol() == fol + 500 && pg.party() == "1,3" && ap::state().characters_waiting.size() == 1);
  set_flag(pg.sg, 2, false);
  CHECK(step_is("Bowman joined the party"));
  CHECK(pg.party() == "1,3,4" && ap::pending_count() == 1);
  ap::stop();
}
