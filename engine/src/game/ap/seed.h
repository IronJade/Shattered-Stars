#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "game/ap/json.h"

// The Archipelago seed file (`.apso2`; docs/ap/seed_format.md): its types and reader. Depends
// on json.h only, so the setup program can check a seed without the game (CMake ss_ap_seed).
namespace ss::game::ap {

// What an Archipelago item is in this game.
struct ItemDef {
  int64_t ap_id = 0;
  std::string name;
  // "item" (game_id, count), "fol" (amount), "character" (character), "trap" (trap, percent,
  // status); "foreign" (another player's) and "unknown" (not in the seed's table) give nothing.
  std::string kind;
  int game_id = 0;
  int count = 1;
  int amount = 0;
  int character = 0;     // character: the party id, 1 Claude .. 12 Chisato
  std::string trap;      // trap: "ambush", "fol_loss" or "status"
  int percent = 0;       // fol_loss: the share of the party's Fol lost (1-100)
  std::string status;    // status: "poison", "paralysis" or "stone"
};

// The party ids (handoff 3 slot +0; a script's party_add argument + 1).
constexpr int kCharacterCount = 12;
const char* character_name(int id);       // "Celine"; "" outside 1-12
int character_by_name(const std::string& name);  // 1-12, 0 when not a character's name

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
  int character = -1; // join: the character the site adds (optional)
  int enemy = -1;     // drop: the enemy type (the stats record's +0 id)
  // The value the seed's patches put where the location's item was (the chest's or gift's
  // item push): 0x400-0x270f for an item routine (give_item), 0x7f000000 and up for a Fol
  // routine (add money). The port gives the location's item there (or sends another
  // player's) and records the check; -1: none (the patch gives the item itself).
  int64_t placeholder = -1;
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
  std::vector<int> goal_encounters;  // a victory in one of these is the goal (default: 539, 540)
  bool deathlink = false;            // options.deathlink
  int protagonist = 0;               // options.protagonist: 0 any, 1 Claude, 2 Rena (New Game's choice)
  // Phase 3 options (docs/ap/seed_format.md); the defaults play as the original.
  bool character_items = false;      // options.character_items: listed join sites become checks
  bool first_drops = false;          // options.drops_as_checks == "first_drop"
  int exp_multiplier = 1;            // options.exp_multiplier, 1-10
  int fol_multiplier = 1;            // options.fol_multiplier, 1-10
  int encounter_rate = 100;          // options.encounter_rate, percent 0-200
};

// The seed file. load_seed_text is what load_seed_file reads; errors say what and where.
bool load_seed_text(const std::string& text, Seed* out, std::string* error);
bool load_seed_file(const std::string& path, Seed* out, std::string* error);

// Placeholder ranges (Location::placeholder). Item routines take 0x400-0x270f. Fol routines
// take 0xf00000-0xffffff, which fits the money routine's 24-bit push (the generator's range),
// or 0x7f000000-0x7fffffff (a full word; version-1 seeds).
constexpr int64_t kItemPlaceholderFirst = 0x400, kItemPlaceholderLast = 0x270F;
constexpr int64_t kFolPlaceholder24First = 0xF00000, kFolPlaceholder24Last = 0xFFFFFF;
constexpr int64_t kFolPlaceholderFirst = 0x7F000000, kFolPlaceholderLast = 0x7FFFFFFF;
constexpr bool is_fol_placeholder(int64_t v) {
  return (v >= kFolPlaceholder24First && v <= kFolPlaceholder24Last) ||
         (v >= kFolPlaceholderFirst && v <= kFolPlaceholderLast);
}

}  // namespace ss::game::ap
