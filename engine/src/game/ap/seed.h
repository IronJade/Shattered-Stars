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
  // status), "key" (key: an area key, phase 4); "foreign" (another player's) and "unknown" (not
  // in the seed's table) give nothing.
  std::string kind;
  int game_id = 0;
  int count = 1;
  int amount = 0;
  int character = 0;     // character: the party id, 1 Claude .. 12 Chisato
  std::string trap;      // trap: "ambush", "fol_loss" or "status"
  int percent = 0;       // fol_loss: the share of the party's Fol lost (1-100)
  std::string status;    // status: "poison", "paralysis" or "stone"
  int key = 0;           // key: the area key's number, 1-255 (the doors it opens name it)
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
  int shop = -1;      // shop: the shop id (script B 0x6d's argument)
  int index = -1;     // shop: the entry in the shop's list (0 first)
  int shop_item = -1; // shop: the item the entry sells (optional: checked when given)
  // v2 (docs/ap/seed_format.md "v2 fields"). craft: the item (1-1023) the creation must make;
  // killer_move: the member is `character` (1-12), the learn tables' move (1-15), the level
  // (display), the use count (record + 0x8a + 2 * move) that reaches it.
  int craft_item = -1;
  int move = -1;
  int level = 0;
  int uses = -1;
  // The value the seed's patches put where the location's item was (the chest's or gift's
  // item push): 0x400-0x270f for an item routine (give_item), 0x7f000000 and up for a Fol
  // routine (add money). The port gives the location's item there (or sends another
  // player's) and records the check; -1: none (the patch gives the item itself).
  int64_t placeholder = -1;
  Placement item;
};

// A door (phase 4, docs/ap/seed_format.md): one map-transfer instruction (script ext 0x7d /
// 0xa4 / 0xda), id = map * 65536 + offset / 4 (tools/ap_doors.py, docs/ap/doors.json). In an
// open-world seed a door with a key needs that key, and one with `keys` needs all of them (a
// one-way story point: the El voyage's trigger); a door with a target sends the party there
// instead (entrance shuffle).
struct DoorTarget {
  int map = -1;
  int x = 0, y = 0, z = 0;
  int dir = -1;       // -1: the door's own facing
};
struct Door {
  int64_t id = 0;
  int map = 0;               // the script's map (909 / 1048: every world-map chunk)
  std::vector<int> maps;     // the maps running that script (default: map)
  int offset = 0;            // the transfer instruction's byte offset in the script code
  int routine = -1;          // the byte offset of the routine a trigger starts (-1: unknown)
  int key = 0;               // the key it needs (0: none)
  std::vector<int> keys;     // ... or the keys it needs, all of them (`keys`; `key` is among them)
  bool has_target = false;
  DoorTarget to;
  // to.while_unbeaten (the hub's P5 doors) [hubcore agent]: the target applies only while this
  // encounter is not in the sidecar's goal_won; once it is won the door is the original again
  // (-1: always). The arena antechamber's door 304 -> 296 leads to Marsilio's arena 305 so.
  int to_while_unbeaten = -1;
  std::string name;
  std::string purpose;       // the banner's ending for a missing key ("to sail to El"; "": none)
  int until = 0;             // the keys are needed only while the story's chapter is below this (0: always)
  // v2 conditions (any world mode, story included): script flags that must all be set and
  // encounters that must all be in the sidecar's goal_won; one banner per condition, flags
  // first, then won (the first missing condition's shows).
  std::vector<int> flags;
  std::vector<int> won;
  std::vector<std::string> banners;
  bool has_conditions() const { return !flags.empty() || !won.empty(); }
};

// The hub (`world_mode: "hub"`; docs/ap/seed_format.md "Hub") [hubvm agent]: a story boss's
// chamber. While the party is on one of its maps and the boss is not yet won (the sidecar's
// goal_won), its story view (if any) shows the chamber's scripts the boss's own story state:
// the chapter / step globals (G[0xa0] / G[0xa4]), the listed flags and globals read and write a
// view copy, so the original scene plays; what the scene stores there is dropped when the view
// ends, except the `keep` flags.
struct StoryView {
  int chapter = 0;
  int step = 0;
  std::map<int, bool> flags;      // flag -> the value the view starts with
  struct Global {
    int addr = 0;                 // script global byte offset (0xa8-0x33f), aligned to size
    int size = 4;                 // 4 (a word, the format's default), 2 or 1
    int32_t value = 0;            // the value the view starts with (its low 'size' bytes)
  };
  std::vector<Global> globals;    // more script globals redirected like the chapter and step
  std::vector<int> keep;          // view flags whose value at the view's end is written back
  std::vector<int> after_maps;    // the view goes on into these from a view map (never starts there)
  bool until_transfer = false;    // until "transfer": no end at free play, only at a map outside the view
  bool has_cut_to = false;        // cut_to: after the win, the first transfer out of the view goes here
  DoorTarget cut_to;
  int done_flag = -1;             // done_flag: after the win the view still comes on in its maps until this real flag is set [hubchambers]
};
// A chamber's `ask` (docs/ap/seed_format.md "Hub", the user's rulings on Shigeo 281 and the arena
// 305) [hubask agent]: while the boss is unbeaten the view comes on only when the player says yes.
// The question (text, the game's own windows) comes at a script transfer into the chamber's maps
// from outside them (only the listed `doors`, when given): yes = the view on, no = the chamber at
// the hub's own state (a seed door that sent the transfer into the chamber goes to its own target
// instead). `options` add a last option to an existing choice: chosen, the party goes to `to`
// (into the chamber: the view on; out of it: the view ends, nothing recorded). Any other arrival
// (a load, another transfer) has no view.
struct HubAsk {
  bool on = false;
  std::vector<std::string> text;  // the question's lines
  std::string yes = "Yes.", no = "No.";
  bool all_doors = true;          // no `doors`: every transfer into the chamber asks
  std::vector<int64_t> doors;     // ... else only these seed door ids (map * 65536 + offset / 4)
  struct Option {
    int map = -1;                 // the map whose script holds the choice
    int window = -1;              // the byte offset (NA) of the op opening its options window (ext 0x68)
    int choice = -1;              // ... and of its choice op (ext 0xdc; then `push R`, `pop_store L[n]`)
    std::string text;             // the extra option's line
    DoorTarget to;                // where choosing it sends the party
  };
  std::vector<Option> options;
};
struct HubBoss {
  int encounter = -1;             // the boss battle: a victory records it (goal_won)
  std::string name;
  int map = -1;                   // the chamber
  std::vector<int> maps;          // the maps the view spans (default: map)
  bool has_view = false;          // false: no view (a flag-guarded boss plays at the hub's state)
  StoryView view;
  int game = 0;                   // 0 both games, 1 only Claude's (script flag 1 set), 2 only Rena's
  HubAsk ask;                     // [hubask agent]
};

// The hub's New Game (the `hub` block's state; docs/ap/seed_format.md "Hub") [hubcore agent]:
// what New Game builds after the original's own (hero, names, party, inventory): the flags (all
// others cleared), the script global words, the picked hero's own flags and words, the chapter
// and step, disc 2, and the start (a town's world-map arrival, with its Synard parking words).
struct HubWord {
  int addr = 0;                   // script global byte offset, 4-aligned (0xc4-0x33f; never the names)
  uint32_t value = 0;
};
struct HubGame {
  std::vector<int> flags;         // set after the hub's flags
  std::vector<HubWord> globals;   // written after the hub's globals
};
struct HubState {
  bool on = false;                // the block holds the state (`flags` / `start`): New Game builds it
  int chapter = 60, step = 4000;  // G[0xa0] / G[0xa4]
  int disc = 2;                   // 1 or 2 (config +0x4c)
  std::vector<int> flags;         // every flag set; the others are cleared
  std::vector<HubWord> globals;
  HubGame games[2];               // [0] Claude's game, [1] Rena's
  std::string start_name;
  DoorTarget start;               // the field map and arrival (dir: degrees, a transfer's facing)
  std::vector<HubWord> start_globals;  // start.globals: the start's Synard words, after games[hero]
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
  int story_route = 0;               // options.story_route: 0 any, 1 Claude, 2 Rena (New Game's choice)
  // Phase 3 options (docs/ap/seed_format.md); the defaults play as the original.
  bool character_items = false;      // options.character_items: listed join sites become checks
  bool first_drops = false;          // options.enemy_dropsanity == "first_drop"
  int exp_multiplier = 1;            // options.exp_multiplier, 1-10
  int fol_multiplier = 1;            // options.fol_multiplier, 1-10
  int encounter_rate = 100;          // options.encounter_rate, percent 0-200
  // Phase 4 (docs/ap/seed_format.md "Phase 4").
  bool open_world = false;           // options.world_mode == "open_world": doors need their keys
  bool expel_teleporter = false;     // options.expel_teleporter (default on in open_world)
  int shuffle_shops = 0;             // options.shuffle_shops: 0 off, 1 specials, 2 full
  std::vector<Door> doors;           // the seed's door table
  // v2 goals and crafting (docs/ap/seed_format.md "v2 fields").
  std::vector<int> goal_all;         // goal.all: every one must be won (then goal_encounters is not the goal)
  std::vector<int> goal_flags;       // goal.flags: set at New Game and after every load
  bool no_fail_crafting = false;     // options.no_fail_crafting: item creation never fails by chance
  // v2 party (docs/ap/seed_format.md "Party v2") [party agent].
  bool open_party = false;           // options.party_rules == "open": only story members, received members
                                     // and the starting character join; no lockouts
  int starting_character = 0;        // options.starting_character: party id 1-12 (0: none, the hero leads)
  // The hub [hubvm agent] (docs/ap/seed_format.md "Hub").
  bool hub = false;                  // options.world_mode == "hub": the chapter lock and the story views
  bool hub_lock_chapter = true;      // hub.lock.chapter: outside a view, chapter stores are dropped
  bool hub_lock_step = true;         // hub.lock.step: ... and step stores, except on hub_step_maps
  std::vector<int> hub_step_maps = {685};  // hub.lock.step_maps: maps whose step stores pass the lock
  std::vector<HubBoss> bosses;       // the seed's `bosses` (the chambers)
  HubState hub_state;                // the `hub` block's New Game state [hubcore agent]
};

// The door id rule (tools/ap_doors.py).
constexpr int64_t door_id(int map, int offset) { return static_cast<int64_t>(map) * 65536 + offset / 4; }

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
