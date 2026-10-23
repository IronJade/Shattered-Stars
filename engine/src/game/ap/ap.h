#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "game/ap/json.h"

// Archipelago (port feature; docs/archipelago_plan.md, the seed format in
// docs/ap/seed_format.md). Phase 1: no network. A seed (`.apso2`, --ap-seed FILE or the
// config key ap_seed) is loaded at start-up; its patch lines join the world edits
// (field/mods.cpp); checks are recorded by location id; received items wait in a queue and
// are given in free play with a notice; the state goes in a sidecar file next to the memory
// card (<card>.slotNN.ap.json), so the card itself stays byte-compatible. Everything is off
// without a seed.
namespace ss::game::ap {

// What an Archipelago item is in this game.
struct ItemDef {
  int64_t ap_id = 0;
  std::string name;
  std::string kind;  // "item" (game_id, count), "fol" (amount)
  int game_id = 0;
  int count = 1;
  int amount = 0;
};

// The item placed at a location: this player's own (ItemDef) or another player's.
struct Placement {
  bool own = true;
  int player = 0;
  std::string player_name;
  std::string classification;  // progression / useful / filler / trap
  ItemDef item;                // own: what the game gives; foreign: name (+ ap_id)
};

struct Location {
  int64_t id = 0;
  std::string name;
  std::string type;  // chest / gift / boss / ...
  int map = -1;
  int flag = -1;      // chest: its opened flag (global script flag)
  int offset = -1;    // gift: the call site's byte offset in the map's script code
  int encounter = -1; // boss
  Placement item;
};

struct Seed {
  int version = 0;
  std::string seed;          // the multiworld's seed id
  std::string slot_name;
  int slot = 0;
  std::string game;
  Json options;
  std::map<int64_t, ItemDef> items;  // this game's item table by AP item id
  std::vector<Location> locations;
  std::vector<std::string> patches;  // world-edit lines (field/mods.h syntax)
  std::vector<ItemDef> start_items;
  int start_fol = 0;
  std::vector<int> start_flags;
};

// The seed file. load_seed_text is what load_seed_file reads; errors say what and where.
bool load_seed_text(const std::string& text, Seed* out, std::string* error);
bool load_seed_file(const std::string& path, Seed* out, std::string* error);

// The save sidecar: the Archipelago state of one save.
struct SaveState {
  std::string seed;
  std::string slot_name;
  int64_t received_index = 0;      // items received so far (the server's item index)
  std::vector<int64_t> checked;    // location ids, in the order they were checked
  bool start_given = false;        // the seed's starting items are in the inventory
};
std::string sidecar_path(const std::string& card_image, int save_index);  // memcard1.slot01.ap.json
bool write_sidecar(const std::string& path, const SaveState& s, std::string* error);
bool read_sidecar(const std::string& path, SaveState* s, std::string* error);
Json sidecar_json(const SaveState& s);
bool sidecar_from_json(const Json& j, SaveState* s, std::string* error);

// ---- The running game ----

bool enabled();                                     // a seed is loaded
bool start(const std::string& seed_path, std::string* error);  // loads the seed (at start-up)
void start_with(Seed seed);                         // tests: an already parsed seed
void stop();                                        // back to off (tests)
const Seed* seed();
const SaveState& state();
const std::vector<std::string>& seed_patch_lines();  // field/mods.cpp reads these

// Checks: recorded once each (offline too, for sending on reconnect).
const Location* find_location(int64_t id);
bool location_checked(int64_t id);                  // records it; false for an unknown id
bool is_checked(int64_t id);
void on_flag_set(int flag);                         // script flags: chests by opened flag

// Received items: queued, then given in free play (inventory, Fol) with a notice.
void receive(const ItemDef& item, const std::string& from);
bool receive_by_name_or_id(const std::string& what, const std::string& from, std::string* error);
size_t pending_count();
void free_play_update();                            // the field's play state calls this

// Saves: the sidecar goes with the save; a load reads it (a new game starts fresh).
void on_new_game();
void on_saved(const std::string& card_image, int save_index);
void on_loaded(const std::string& card_image, int save_index);

std::string status_text();

}  // namespace ss::game::ap
