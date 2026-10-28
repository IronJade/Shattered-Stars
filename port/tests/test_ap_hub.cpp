// Archipelago, the hub open world (`world_mode: hub`; docs/apworld/seed_format.md "Hub") [hubcore
// agent]: the hub block's New Game state (seed parsing, the order New Game builds it in, both
// heroes, the start), the dungeon-key doors and the Fienal gate in hub mode, and the sidecar.
// The chapter lock and the story views have their own tests (test_script_vm.cpp ap_hub_*). No
// disc needed.

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/ap/ap.h"
#include "game/ap/hub.h"
#include "game/ap/seed.h"
#include "game/field/field.h"
#include "test.h"

using namespace ss;
using namespace ss::game;

namespace {

// A hub seed with the New Game state: a few flags (1 among them: Claude's route flag, which a
// Rena game must not get), words, the heroes' own, the start (Fun City) with its Synard words.
const char* kHubStateSeed = R"seed({"format": "apso2", "version": 3, "seed": "hubcore-test", "slot": {"name": "H", "id": 1},
 "options": {"world_mode": "hub", "story_route": "any"},
 "items": {"7000001": {"name": "Salva Drift Key", "kind": "key", "key": 1}},
 "locations": [],
 "hub": {"chapter": 60, "step": 4000, "disc": 2,
         "flags": [1, 11, 35, 730, 2943],
         "globals": {"0x1bc": "0x270f", "0x1e4": "0x1869f", "0x30c": 51209, "0x314": "0x13e8a", "0x32c": -150},
         "games": {"claude": {"flags": [], "globals": {"0xc4": "0x20100"}},
                   "rena": {"flags": [36], "globals": {"0xc4": "0x20001"}}},
         "start": {"name": "Fun City", "map": 279, "x": 470, "y": 0, "z": 68, "dir": 180,
                   "globals": {"0x314": 81546, "0x318": 33880}},
         "lock": {"chapter": true, "step": true, "step_maps": [685]}}})seed";

std::string with(std::string s, const std::string& from, const std::string& to) {
  const size_t at = s.find(from);
  if (at != std::string::npos) s.replace(at, from.size(), to);
  return s;
}

bool refused(const std::string& text, const std::string& part) {
  ap::Seed t;
  std::string e;
  const bool ok = !ap::load_seed_text(text, &t, &e) && e.find(part) != std::string::npos;
  if (!ok) std::fprintf(stderr, "  expected '%s', got '%s'\n", part.c_str(), e.c_str());
  return ok;
}

bool start_seed(const std::string& text) {
  ap::Seed s;
  std::string error;
  if (!ap::load_seed_text(text, &s, &error)) {
    std::fprintf(stderr, "  seed: %s\n", error.c_str());
    return false;
  }
  ap::start_with(s);
  return true;
}

// A state block as the original New Game leaves it (field_create_state_block): Claude's game
// (flag 1, route bytes 0 / 1, chapter 3 on map 7) or Rena's (chapter 4 on map 18), names, the
// message speed; plus a stray flag and word the hub must clear / keep.
struct NewGameBlock {
  field::FieldGlobals& g = field::globals();
  u8* config0 = g.config;
  Loader* loader0 = g.loader;
  std::vector<u8> block = std::vector<u8>(0xA40, 0), config = std::vector<u8>(0x2A0, 0);
  explicit NewGameBlock(bool rena) {
    g.config = config.data();
    g.loader = nullptr;
    config[0x45] = rena ? 1 : 0;
    u8* gs = block.data() + 0x220;
    gs[0] = rena ? 4 : 3;
    gs[0x1A] = rena ? 18 : 7;
    block[0x244] = rena ? 1 : 0;
    block[0x245] = rena ? 0 : 1;
    block[0x338] = 3;  // the message speed (G[0x1b8])
    if (!rena) block[0x8C0] |= 2;
    std::memcpy(block.data() + 0x248, "Claude", 7);
    std::memcpy(block.data() + 0x25C, "Rena", 5);
    flag(900, true);
    word(0x200, 0x55);
  }
  ~NewGameBlock() {
    g.config = config0;
    g.loader = loader0;
  }
  NewGameBlock(const NewGameBlock&) = delete;
  NewGameBlock& operator=(const NewGameBlock&) = delete;
  bool flag(int n) const { return (block[0x8C0 + (n >> 3)] >> (n & 7) & 1) != 0; }
  void flag(int n, bool on) {
    u8& b = block[0x8C0 + (n >> 3)];
    b = on ? static_cast<u8>(b | 1 << (n & 7)) : static_cast<u8>(b & ~(1 << (n & 7)));
  }
  u32 word(int addr) const {
    u32 v;
    std::memcpy(&v, block.data() + 0x180 + addr, 4);
    return v;
  }
  void word(int addr, u32 v) { std::memcpy(block.data() + 0x180 + addr, &v, 4); }
  int flag_count() const {
    int n = 0;
    for (int i = 0; i < 0x170 * 8; ++i) n += flag(i);
    return n;
  }
  s16 half(int gs_off) const {
    s16 v;
    std::memcpy(&v, block.data() + 0x220 + gs_off, 2);
    return v;
  }
};

}  // namespace

TEST(ap_hub_state_seed) {
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(kHubStateSeed, &s, &error));
  if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
  const ap::HubState& h = s.hub_state;
  CHECK(s.hub && h.on && h.chapter == 60 && h.step == 4000 && h.disc == 2);
  CHECK(h.flags == (std::vector<int>{1, 11, 35, 730, 2943}));
  CHECK(h.globals.size() == 5 && h.globals[0].addr == 0x1BC && h.globals[0].value == 0x270F);
  CHECK(h.globals[4].addr == 0x32C && h.globals[4].value == static_cast<uint32_t>(-150));
  CHECK(h.games[0].flags.empty() && h.games[0].globals.size() == 1 && h.games[0].globals[0].value == 0x20100);
  CHECK(h.games[1].flags == std::vector<int>{36} && h.games[1].globals[0].value == 0x20001);
  CHECK(h.start_name == "Fun City" && h.start.map == 279 && h.start.x == 470 && h.start.z == 68 && h.start.dir == 180);
  CHECK(h.start_globals.size() == 2 && h.start_globals[0].addr == 0x314 && h.start_globals[0].value == 81546);
  // D7: characters are items under the open party rules in hub mode, whatever the options say.
  CHECK(s.character_items && s.open_party);
  // Defaults: chapter 60, step 4000, disc 2, facing 90.
  CHECK(ap::load_seed_text(with(with(kHubStateSeed, "\"chapter\": 60, \"step\": 4000, \"disc\": 2,", ""), ", \"dir\": 180", ""), &s,
                           &error));
  CHECK(s.hub_state.chapter == 60 && s.hub_state.step == 4000 && s.hub_state.disc == 2 && s.hub_state.start.dir == 90);
  // A hub block with only the lock (the VM's tests, hubvm's runs): no New Game state.
  const std::string lock_only = R"({"format": "apso2", "version": 3, "seed": "x", "options": {"world_mode": "hub"},
    "items": {}, "locations": [], "hub": {"chapter": 60, "step": 4000, "lock": {"chapter": true}}})";
  CHECK(ap::load_seed_text(lock_only, &s, &error) && s.hub && !s.hub_state.on);
  // Not a hub seed: the block's state is not read, and the party options stay the seed's.
  CHECK(ap::load_seed_text(with(with(kHubStateSeed, "\"world_mode\": \"hub\"", "\"world_mode\": \"open_world\""),
                                "\"version\": 3", "\"version\": 2"),
                           &s, &error));
  CHECK(!s.hub && !s.hub_state.on && !s.character_items && !s.open_party);
  // Refused: words outside the game state's script words or over the names, unaligned, bad
  // flags, a start that is no field map, a disc 3, an unknown game.
  CHECK(refused(with(kHubStateSeed, "\"0x1bc\": \"0x270f\"", "\"0x100\": 1"), "hub.globals: words at 0xc4-0x33c"));
  CHECK(refused(with(kHubStateSeed, "\"0x1bc\": \"0x270f\"", "\"0x1b8\": 1"), "hub.globals"));
  CHECK(refused(with(kHubStateSeed, "\"0x1bc\": \"0x270f\"", "\"0x1bd\": 1"), "hub.globals"));
  CHECK(refused(with(kHubStateSeed, "\"0x1bc\": \"0x270f\"", "\"0xa8\": 1"), "hub.globals"));
  CHECK(refused(with(kHubStateSeed, "\"0x1bc\": \"0x270f\"", "\"0x340\": 1"), "hub.globals"));
  CHECK(refused(with(kHubStateSeed, "\"flags\": [1, 11,", "\"flags\": [2944, 11,"), "hub.flags: script flags"));
  CHECK(refused(with(kHubStateSeed, "\"rena\": {\"flags\": [36]", "\"rena\": {\"flags\": [-1]"), "hub.games.rena.flags"));
  CHECK(refused(with(kHubStateSeed, "\"games\": {\"claude\"", "\"games\": {\"ashton\""), "hub.games: claude or rena"));
  CHECK(refused(with(kHubStateSeed, "\"map\": 279", "\"map\": 2"), "hub.start needs map"));
  CHECK(refused(with(kHubStateSeed, "\"map\": 279, \"x\": 470,", "\"map\": 279,"), "hub.start needs map"));
  CHECK(refused(with(kHubStateSeed, "\"disc\": 2", "\"disc\": 3"), "hub.disc"));
  CHECK(refused(with(kHubStateSeed, "\"0x318\": 33880", "\"0xc8\": 1"), "hub.start.globals"));
  CHECK(refused(with(kHubStateSeed, "\"start\": {\"name\"", "\"begin\": {\"name\""), "hub.start needs map"));
}

TEST(ap_hub_new_game_order) {
  ap::stop();
  for (const bool rena : {false, true}) {
    NewGameBlock ng(rena);
    CHECK(start_seed(kHubStateSeed));
    ap::on_new_game();
    ap::on_new_game_state(ng.block.data(), false);
    // flags: the hub's (all others cleared: 900), the hero's own (Rena: 36), the route flag New
    // Game's (Claude's game set, Rena's clear, whatever the hub's list says).
    CHECK(ng.flag(1) == !rena);
    CHECK(ng.flag(11) && ng.flag(35) && ng.flag(730) && ng.flag(2943) && !ng.flag(900));
    CHECK(ng.flag(36) == rena);
    CHECK_EQ(ng.flag_count(), rena ? 5 : 5);  // (Claude: 1 11 35 730 2943; Rena: 11 35 36 730 2943)
    // globals, then the hero's, then the start's (its Synard words over the hub's).
    CHECK_EQ(ng.word(0x1BC), 0x270Fu);
    CHECK_EQ(ng.word(0x1E4), 0x1869Fu);
    CHECK_EQ(ng.word(0x30C), 51209u);
    CHECK_EQ(ng.word(0x314), 81546u);
    CHECK_EQ(ng.word(0x318), 33880u);
    CHECK_EQ(ng.word(0x32C), static_cast<u32>(-150));
    CHECK_EQ(ng.word(0xC4), rena ? 0x20001u : 0x20100u);
    CHECK_EQ(ng.word(0x200), 0x55u);  // (a word the hub does not list: New Game's)
    // chapter / step; the names and the message speed stay New Game's.
    CHECK_EQ(ng.word(0xA0), 60u);
    CHECK_EQ(ng.word(0xA4), 4000u);
    CHECK(std::memcmp(ng.block.data() + 0x248, "Claude", 7) == 0 && std::memcmp(ng.block.data() + 0x25C, "Rena", 5) == 0);
    CHECK_EQ(ng.block[0x338], 3);
    // disc 2 (config +0x4c), the start: map 279 at (470, 0, 68) facing 180 degrees (2048 / 4096).
    CHECK_EQ(ng.config[0x4C], 1);
    CHECK_EQ(ng.half(0x1A), 279);
    CHECK_EQ(ng.word(0xA8), 470u);
    CHECK_EQ(ng.word(0xAC), 0u);
    CHECK_EQ(ng.word(0xB0), 68u);
    CHECK_EQ(ng.half(0x18), 2048);
    // The first map load keeps that position, once.
    CHECK(ap::start_position_pending() && ap::take_start_position() && !ap::take_start_position());
    ap::stop();
  }
  // A debug start (--map): the state is not applied (the map's own run) ...
  {
    NewGameBlock ng(false);
    CHECK(start_seed(kHubStateSeed));
    ap::on_new_game_state(ng.block.data(), true);
    CHECK(ng.flag(900) && ng.word(0xA0) == 3 && ng.half(0x1A) == 7 && !ap::start_position_pending());
#ifndef _WIN32
    // ... unless SS_HUB_STATE asks: then it is, on the debug start's map.
    setenv("SS_HUB_STATE", "1", 1);
    ap::on_new_game_state(ng.block.data(), true);
    unsetenv("SS_HUB_STATE");
    CHECK(!ng.flag(900) && ng.flag(730) && ng.word(0xA0) == 60 && ng.half(0x1A) == 7 && ng.config[0x4C] == 1);
    CHECK(!ap::start_position_pending());
    CHECK(ng.flag(1) && !ng.flag(36) && ng.word(0xC4) == 0x20100u);  // (Claude's game)
    // SS_HUB_STATE=rena: Rena's game on the debug start (the hub proof's sweeps).
    setenv("SS_HUB_STATE", "rena", 1);
    ap::on_new_game_state(ng.block.data(), true);
    unsetenv("SS_HUB_STATE");
    CHECK(!ng.flag(1) && ng.flag(36) && ng.word(0xC4) == 0x20001u && ng.word(0xA0) == 60 && ng.half(0x1A) == 7);
#endif
    ap::stop();
  }
  // New Game resets a start left pending (a game over back to the title).
  {
    NewGameBlock ng(false);
    CHECK(start_seed(kHubStateSeed));
    ap::on_new_game_state(ng.block.data(), false);
    ap::on_new_game();
    CHECK(!ap::start_position_pending());
    ap::stop();
  }
  // Without the state (a lock-only hub seed), with a story seed, without a seed: New Game's.
  const std::string lock_only = with(kHubStateSeed, "\"flags\": [1, 11, 35, 730, 2943],", "");
  const std::string no_start = with(lock_only, "\"start\": {", "\"begin\": {");
  const std::string story = with(with(kHubStateSeed, "\"world_mode\": \"hub\"", "\"world_mode\": \"story\""), "\"version\": 3",
                                 "\"version\": 2");
  for (const std::string* text : {&no_start, &story, static_cast<const std::string*>(nullptr)}) {
    NewGameBlock ng(false);
    if (text != nullptr) CHECK(start_seed(*text));
    ap::on_new_game_state(ng.block.data(), false);
    CHECK(ng.flag(900) && ng.flag(1) && !ng.flag(730) && ng.word(0xA0) == 3 && ng.half(0x1A) == 7 && ng.config[0x4C] == 0);
    CHECK(!ap::start_position_pending());
    ap::stop();
  }
}

namespace {

// A hub seed with dungeon doors (hub_bosses.json `dungeons`, as the APWorld writes them): the
// Cave of Red Crystal's world-map entrance (Energy Nede's chunks, script 1048; key 13), Salva's
// field door into Salva Drift (map 43; key 1), Zand's Mansion's door written without its routine
// (the routine also leads elsewhere: only the transfer is refused), and Fienal's entrance with
// the D6 requirement (won: Magichand 524, Berle at Armlock 535; one banner each). Key 13 lies in
// a chest (placeholder 1113); bosses 524 and 535 are chambers.
std::string door_seed(const std::string& world_mode) {
  std::string t = R"seed({"format": "apso2", "version": 3, "seed": "hubcore-doors", "slot": {"name": "H", "id": 1},
 "options": {"world_mode": "WM", "story_route": "claude"},
 "items": {"7000013": {"name": "Cave of Red Crystal Key", "kind": "key", "key": 13},
           "7000001": {"name": "Salva Drift Key", "kind": "key", "key": 1}},
 "locations": [{"id": 9113, "name": "key chest", "placeholder": 1113, "check": {"type": "chest", "map": 2, "flag": 900},
                "item": {"player": 1, "kind": "key", "key": 13, "name": "Cave of Red Crystal Key"}},
               {"id": 9001, "name": "key chest 2", "placeholder": 1101, "check": {"type": "chest", "map": 2, "flag": 901},
                "item": {"player": 1, "kind": "key", "key": 1, "name": "Salva Drift Key"}}],
 "bosses": [{"encounter": 524, "name": "Magichand", "map": 554}, {"encounter": 535, "name": "Berle", "map": 306}],
 "doors": [{"id": 68686044, "key": 13, "routine": "0x4364", "name": "Cave of Red Crystal"},
           {"id": 2824679, "key": 1, "routine": "0x673c", "name": "Salva Drift (map 43 -> 63)"},
           {"id": 9314034, "key": 4, "name": "Zand's Mansion (map 142 -> 143)"},
           {"id": 68685691, "routine": "0x3de0", "won": [524, 535],
            "banners": ["Defeat Magichand first", "Defeat Berle (Armlock) first"], "name": "Fienal"}],
 "hub": {"lock": {"chapter": true, "step": true}}})seed";
  return with(t, "\"WM\"", "\"" + world_mode + "\"");
}

// A map script whose code has a map transfer (ext 0xda) at each byte offset.
std::vector<u8> script_with_transfers(std::initializer_list<int> offsets) {
  std::vector<u8> s(0x1C + 0x10000, 0);
  for (const int at : offsets) {
    const u32 w = 0xDA000011u;
    std::memcpy(s.data() + 0x1C + at, &w, 4);
  }
  return s;
}

// The field globals the door checks read (script flags, the chapter).
struct DoorGame {
  field::FieldGlobals& g = field::globals();
  u8 *state0 = g.game_state, *flags0 = g.script_flags;
  std::vector<u8> state = std::vector<u8>(0x2A0, 0), flags = std::vector<u8>(0x170, 0);
  DoorGame() {
    g.game_state = state.data();
    g.script_flags = flags.data();
    state[0] = 60;   // chapter 60
    flags[0] = 2;    // Claude's game
  }
  ~DoorGame() {
    g.game_state = state0;
    g.script_flags = flags0;
  }
  DoorGame(const DoorGame&) = delete;
  DoorGame& operator=(const DoorGame&) = delete;
};

bool notice_waiting(const std::string& text) {
  for (const std::string& n : ap::waiting_notices())
    if (n == text) return true;
  return false;
}

}  // namespace

TEST(ap_hub_dungeon_doors) {
  ap::stop();
  DoorGame dg;
  CHECK(start_seed(door_seed("hub")));
  // The world map (a Nede chunk runs script 1048): the Cave of Red Crystal is shut without key 13,
  // at its trigger (nothing of the routine runs) and at the transfer; the banner names the key.
  ap::on_script_loaded(1060, script_with_transfers({0x4370, 0x3DEC}));
  CHECK(ap::door_blocked_at_trigger(0x4364));
  CHECK(notice_waiting("Cave of Red Crystal Key needed"));
  s32 args[7] = {419, 128, 49, 0, 6205, 266, 128};
  CHECK(ap::on_map_transfer(0x4370, args));
  // Fienal's entrance: shut until both chamber bosses are won, the first missing one's banner.
  CHECK(ap::door_blocked_at_trigger(0x3DE0));
  CHECK(notice_waiting("Defeat Magichand first"));
  // The key arrives (a chest's placeholder): the cave opens, its arguments the game's own.
  CHECK(ap::on_give_item(1113) && ap::has_key(13));
  CHECK(!ap::door_blocked_at_trigger(0x4364));
  CHECK(!ap::on_map_transfer(0x4370, args) && args[0] == 419 && args[4] == 6205);
  // Magichand won (a chamber's victory goes in goal_won): Berle's banner now; then Berle: open.
  ap::on_battle_exit(524, 0);
  CHECK(ap::goal_won(524));
  CHECK(ap::door_blocked_at_trigger(0x3DE0));
  CHECK(notice_waiting("Defeat Berle (Armlock) first"));
  ap::on_battle_exit(535, 1);  // (an escape is no win)
  CHECK(ap::door_blocked_at_trigger(0x3DE0));
  ap::on_battle_exit(535, 0);
  CHECK(!ap::door_blocked_at_trigger(0x3DE0));
  s32 fienal[7] = {687, 128, 1046, 0, 50, 180, 128};
  CHECK(!ap::on_map_transfer(0x3DEC, fienal) && fienal[0] == 687);
  // A town's field door into a dungeon (Salva 43 -> Salva Drift 63): shut, then open with key 1.
  ap::on_script_loaded(43, script_with_transfers({0x679C}));
  CHECK(ap::door_blocked_at_trigger(0x673C));
  s32 drift[7] = {63, 128, 1301, 0, 23, 179, 128};
  CHECK(ap::on_map_transfer(0x679C, drift));
  CHECK(ap::on_give_item(1101) && !ap::door_blocked_at_trigger(0x673C) && !ap::on_map_transfer(0x679C, drift));
  // A door without its routine (Zand's Mansion's: the routine also leads out of the mansion's
  // way): the trigger runs, only the transfer into the dungeon is refused.
  ap::on_script_loaded(142, script_with_transfers({0x7BC8}));
  CHECK(!ap::door_blocked_at_trigger(0x77CC));
  s32 zand[7] = {143, 2, 427, 0, 11, 180, 128};
  CHECK(ap::on_map_transfer(0x7BC8, zand));
  CHECK(notice_waiting("Zand's Mansion Key needed") || notice_waiting("Key 4 needed"));
  // The sidecar keeps the keys and the wins.
  ap::SaveState back;
  std::string error;
  CHECK(ap::sidecar_from_json(ap::sidecar_json(ap::state()), &back, &error));
  CHECK(back.keys == (std::vector<int>{13, 1}) && back.goal_won == (std::vector<int>{524, 535}));
  CHECK(ap::status_text().find("world hub") != std::string::npos);
  ap::stop();
  // A story seed: the keys lock nothing (the won gate still applies, in every mode).
  CHECK(start_seed(with(with(door_seed("story"), "\"version\": 3", "\"version\": 2"), ", \"bosses\"", ", \"x_bosses\"")));
  ap::on_script_loaded(1060, script_with_transfers({0x4370, 0x3DEC}));
  CHECK(!ap::door_blocked_at_trigger(0x4364));
  CHECK(ap::door_blocked_at_trigger(0x3DE0));
  ap::stop();
}
