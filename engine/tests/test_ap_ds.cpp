// The tracker keys in the server's data storage [dskeys agent] (game/ap/ap.cpp
// publish_tracker_keys; docs/ap/data_storage.md): so2_map_{team}_{slot} and
// so2_story_{team}_{slot}, written through a fake Link. Each case drives the runtime's own
// vblank (runtime::Runtime::vblank, which runs game/ap's per-vblank poll while a link is in),
// so the throttle sees real vblank counts. A scratch game_state stands in for the game's. No
// disc needed.

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ap_net/link.h"
#include "game/ap/ap.h"
#include "game/field/field.h"
#include "runtime/runtime.h"
#include "test.h"

using namespace ss;
using namespace ss::game;

namespace {

using Sets = std::vector<std::pair<std::string, std::string>>;

// A Link that records the keys set and hands out queued events; its state is the test's.
struct DsLink : ap_net::Link {
  Sets* sets;
  std::vector<ap_net::Event>* events;
  ap_net::State* st;
  DsLink(Sets* s, std::vector<ap_net::Event>* e, ap_net::State* state) : sets(s), events(e), st(state) {}
  void check(const std::vector<int64_t>&) override {}
  void goal() override {}
  void deathlink(const std::string&) override {}
  void say(const std::string&) override {}
  void set(const std::string& key, const std::string& value) override { sets->emplace_back(key, value); }
  std::vector<ap_net::Event> poll() override {
    std::vector<ap_net::Event> out;
    out.swap(*events);
    return out;
  }
  ap_net::State state() const override { return *st; }
  std::string player_name(int) const override { return ""; }
  std::string item_name(int64_t, int) const override { return ""; }
  std::string location_name(int64_t, int) const override { return ""; }
};

const char* kStory = R"({"format": "apso2", "version": 1, "seed": "ds-seed", "slot": {"name": "Claude", "id": 7},
 "locations": []})";
const char* kOpen = R"({"format": "apso2", "version": 1, "seed": "ds-seed", "slot": {"name": "Claude", "id": 7},
 "options": {"world_mode": "open_world"}, "locations": []})";
const char* kHub = R"({"format": "apso2", "version": 3, "seed": "ds-seed", "slot": {"name": "Claude", "id": 7},
 "options": {"world_mode": "hub"}, "locations": [], "hub": {"lock": {"chapter": true, "step": true}}})";

// A scratch game_state (0x2a0 bytes): chapter +0, step +4, position +8 / +0x10, map +0x1a. The
// runtime's other per-vblank hooks are set aside while a case runs (another test's may linger).
struct Scratch {
  field::FieldGlobals& g = field::globals();
  u8* state0 = g.game_state;
  runtime::Runtime& rt = runtime::Runtime::get();
  void (*vsync0)() = rt.vsync_callback;
  void (*audio0)() = rt.audio_tick;
  std::vector<u8> state = std::vector<u8>(0x2A0, 0);
  Sets sets;
  std::vector<ap_net::Event> events;
  ap_net::State st = ap_net::State::kSlotConnected;
  Scratch() {
    g.game_state = state.data();
    rt.vsync_callback = nullptr;
    rt.audio_tick = nullptr;
    ap::stop();
  }
  ~Scratch() {
    ap::stop();
    g.game_state = state0;
    rt.vsync_callback = vsync0;
    rt.audio_tick = audio0;
  }
  void put32(int at, s32 v) { std::memcpy(state.data() + at, &v, 4); }
  void put16(int at, s16 v) { std::memcpy(state.data() + at, &v, 2); }
  void place(int map, int chapter, int step, s32 x = 0, s32 z = 0) {
    put16(0x1A, static_cast<s16>(map));
    put32(0, chapter);
    put32(4, step);
    put32(8, x);
    put32(0x10, z);
  }
  bool start(const char* text) {
    ap::Seed s;
    std::string error;
    if (!ap::load_seed_text(text, &s, &error)) {
      std::fprintf(stderr, "  seed: %s\n", error.c_str());
      return false;
    }
    ap::start_with(s);
    return true;
  }
  void link() { ap::connect(std::make_unique<DsLink>(&sets, &events, &st)); }
  void connected(int team, int slot, const char* seed = "ds-seed") {
    ap_net::Event c;
    c.kind = ap_net::Event::kConnected;
    c.seed_name = seed;
    c.team = team;
    c.slot = slot;
    events.push_back(c);
  }
  void vblanks(int n) {
    for (int i = 0; i < n; ++i) rt.vblank();
  }
  size_t count(const std::string& key) const {
    size_t n = 0;
    for (const auto& [k, v] : sets) n += k == key;
    return n;
  }
  std::string last(const std::string& key) const {
    for (auto it = sets.rbegin(); it != sets.rend(); ++it)
      if (it->first == key) return it->second;
    return "";
  }
};

}  // namespace

// Without a seed nothing is written, a link or not (Archipelago off: no link at all).
TEST(ap_ds_never_without_seed) {
  Scratch t;
  t.place(18, 3, 100);
  t.vblanks(40);  // no link, no seed: game/ap's poll is not even installed
  CHECK(runtime::Runtime::get().net_poll == nullptr);
  t.link();  // a link with no seed (main.cpp never makes one; the poll must still say nothing)
  t.connected(0, 1);
  t.vblanks(40);
  CHECK(t.sets.empty());
}

// Offline: a seed without a link, a link not connected yet, another room's seed, a slot whose
// socket dropped: nothing. Once connected to the seed's room, both keys at once.
TEST(ap_ds_never_offline) {
  Scratch t;
  t.place(18, 3, 100);
  CHECK(t.start(kStory));
  t.vblanks(40);  // a seed played offline: no link
  CHECK(runtime::Runtime::get().net_poll == nullptr);
  t.st = ap_net::State::kSocketOpen;
  t.link();
  t.vblanks(40);  // connecting
  CHECK(t.sets.empty());
  t.connected(0, 1, "another-room");
  t.vblanks(40);  // the server's seed is not ours: offline
  CHECK(t.sets.empty() && !ap::connected());
  t.connected(0, 1);
  t.vblanks(5);  // ours, but the link's state says the slot dropped again meanwhile
  CHECK(ap::connected() && t.sets.empty());
  t.st = ap_net::State::kSlotConnected;
  t.vblanks(1);
  CHECK_EQ(t.sets.size(), 2u);
  CHECK(t.last("so2_map_0_1") == R"({"map":18})");
  CHECK(t.last("so2_story_0_1") == R"({"chapter":3,"step":100})");
  // The socket drops: nothing while it is down, even with changes.
  t.st = ap_net::State::kDisconnected;
  t.place(26, 3, 200);
  t.vblanks(60);
  CHECK_EQ(t.sets.size(), 2u);
}

// The key names are the Connected packet's team and slot; values as docs/ap/data_storage.md.
TEST(ap_ds_key_names) {
  Scratch t;
  t.place(26, 4, 150);
  CHECK(t.start(kStory));
  t.link();
  t.connected(2, 7);
  t.vblanks(1);
  CHECK_EQ(t.sets.size(), 2u);
  CHECK(t.count("so2_map_2_7") == 1 && t.count("so2_story_2_7") == 1);
  CHECK(t.last("so2_map_2_7") == R"({"map":26})");
  CHECK(t.last("so2_story_2_7") == R"({"chapter":4,"step":150})");
}

// Written on a change only: nothing while the values stay; a step, a chapter, a map each
// write their own key once.
TEST(ap_ds_on_change_only) {
  Scratch t;
  t.place(18, 3, 100);
  CHECK(t.start(kStory));
  t.link();
  t.connected(0, 1);
  t.vblanks(200);
  CHECK_EQ(t.sets.size(), 2u);
  t.put32(4, 200);  // a step
  t.vblanks(100);
  CHECK(t.count("so2_story_0_1") == 2 && t.count("so2_map_0_1") == 1);
  CHECK(t.last("so2_story_0_1") == R"({"chapter":3,"step":200})");
  t.put32(0, 4);  // a chapter
  t.vblanks(100);
  CHECK(t.count("so2_story_0_1") == 3 && t.last("so2_story_0_1") == R"({"chapter":4,"step":200})");
  t.put16(0x1A, 26);  // a map
  t.vblanks(100);
  CHECK(t.count("so2_map_0_1") == 2 && t.last("so2_map_0_1") == R"({"map":26})");
  CHECK_EQ(t.sets.size(), 5u);
  // Position bytes off the world maps are not part of the key.
  t.put32(8, 1234);
  t.vblanks(100);
  CHECK_EQ(t.sets.size(), 5u);
}

// Throttled: a key changing every vblank is written at most once every 30 vblanks, and its
// last value still goes out once the interval has passed.
TEST(ap_ds_throttled) {
  Scratch t;
  t.place(1, 10, 0, 50000, 20000);
  CHECK(t.start(kOpen));
  t.link();
  t.connected(0, 1);
  t.vblanks(1);
  CHECK(t.count("so2_map_0_1") == 1 && t.last("so2_map_0_1") == R"({"map":1,"x":50000,"z":20000})");
  for (int i = 1; i <= 300; ++i) {  // the party walks: a new x every vblank for five seconds
    t.put32(8, 50000 + i);
    t.vblanks(1);
  }
  const size_t walked = t.count("so2_map_0_1") - 1;
  CHECK(walked >= 9 && walked <= 10);  // 300 vblanks / 30
  t.vblanks(30);
  CHECK(t.last("so2_map_0_1") == R"({"map":1,"x":50300,"z":20000})");
  const size_t settled = t.count("so2_map_0_1");
  t.vblanks(120);
  CHECK_EQ(t.count("so2_map_0_1"), settled);  // standing still: nothing more
  // A story flurry (steps in quick succession) the same: two writes, the last one current.
  const size_t before = t.count("so2_story_0_1");
  for (int i = 1; i <= 5; ++i) {
    t.put32(4, i * 100);
    t.vblanks(2);
  }
  t.vblanks(30);
  CHECK_EQ(t.count("so2_story_0_1"), before + 2);
  CHECK(t.last("so2_story_0_1") == R"({"chapter":10,"step":500})");
}

// x / z on the world maps (1-3) only; a field map's key has the map alone.
TEST(ap_ds_world_map_position) {
  Scratch t;
  t.place(2, 40, 0, 86054, 25733);
  CHECK(t.start(kOpen));
  t.link();
  t.connected(0, 3);
  t.vblanks(1);
  CHECK(t.last("so2_map_0_3") == R"({"map":2,"x":86054,"z":25733})");
  t.place(279, 40, 0, 86054, 25733);
  t.vblanks(31);
  CHECK(t.last("so2_map_0_3") == R"({"map":279})");
  t.place(1, 40, 0, -5, 7);
  t.vblanks(31);
  CHECK(t.last("so2_map_0_3") == R"({"map":1,"x":-5,"z":7})");
}

// Every (re)connect writes both keys again at once, unchanged values and the throttle
// notwithstanding; a reconnect under another team / slot uses the new names.
TEST(ap_ds_rewritten_on_reconnect) {
  Scratch t;
  t.place(18, 3, 100);
  CHECK(t.start(kStory));
  t.link();
  t.connected(0, 1);
  t.vblanks(1);
  CHECK_EQ(t.sets.size(), 2u);
  t.st = ap_net::State::kDisconnected;  // the server goes away ...
  t.vblanks(50);
  t.st = ap_net::State::kSlotConnected;  // ... and the slot connects again
  t.connected(0, 1);
  t.vblanks(1);
  CHECK_EQ(t.sets.size(), 4u);
  CHECK(t.count("so2_map_0_1") == 2 && t.count("so2_story_0_1") == 2);
  t.connected(0, 1);  // at once, within the throttle's interval too
  t.vblanks(1);
  CHECK_EQ(t.sets.size(), 6u);
  t.connected(1, 4);
  t.vblanks(1);
  CHECK(t.last("so2_map_1_4") == R"({"map":18})" && t.last("so2_story_1_4") == R"({"chapter":3,"step":100})");
}

// No game: on the title, without a game_state, before a map is loaded (map 0): nothing. The
// field's own scene writes as soon as there is a map.
TEST(ap_ds_no_game) {
  Scratch t;
  CHECK(t.start(kStory));
  t.link();
  t.connected(0, 1);
  t.vblanks(10);  // map 0: nothing loaded yet
  CHECK(t.sets.empty());
  ap::on_program(ap::Scene::kTitle);
  t.place(18, 3, 100);
  t.vblanks(10);
  CHECK(t.sets.empty());
  ap::on_program(ap::Scene::kField);
  field::globals().game_state = nullptr;
  t.vblanks(10);
  CHECK(t.sets.empty());
  field::globals().game_state = t.state.data();
  t.vblanks(1);
  CHECK_EQ(t.sets.size(), 2u);
}

// Story mode, open world and the hub write the same keys; the save sidecar holds none of it.
TEST(ap_ds_every_mode) {
  for (const char* seed : {kStory, kOpen, kHub}) {
    Scratch t;
    t.place(18, 3, 100);
    CHECK(t.start(seed));
    const std::string sidecar = ap::sidecar_json(ap::state()).dump();
    t.link();
    t.connected(0, 1);
    t.vblanks(1);
    CHECK(t.last("so2_map_0_1") == R"({"map":18})");
    CHECK(t.last("so2_story_0_1") == R"({"chapter":3,"step":100})");
    t.place(26, 3, 200);
    t.vblanks(31);
    CHECK_EQ(t.sets.size(), 4u);
    CHECK(ap::sidecar_json(ap::state()).dump() == sidecar);
  }
}
