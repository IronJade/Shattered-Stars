#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/types.h"
#include "game/ap/json.h"
#include "game/ap/seed.h"

namespace ss::ap_net {
class Link;
}

// Archipelago (port feature; docs/archipelago_plan.md, the seed format in
// docs/ap/seed_format.md). A seed (`.apso2`, --ap-seed FILE or the config key ap_seed) is
// loaded at start-up; its patch lines join the world edits (field/mods.cpp). Checks are
// recorded by location id (chests by their opened flag, chests and gifts by the placeholder
// their patch puts in place of the item, bosses by encounter); received items wait in a queue
// and are given in free play with a notice; the state goes in a sidecar file next to the
// memory card (<card>.slotNN.ap.json), so the card itself stays byte-compatible. With a
// server (main.cpp hands over an ap_net::Link), checks, the goal and DeathLink go out and
// received items come in. Everything is off without a seed.
namespace ss::game::ap {

// The save sidecar: the Archipelago state of one save.
struct SaveState {
  std::string seed;
  std::string slot_name;
  int64_t received_index = 0;      // the server's items given in this save (its item index)
  std::vector<int64_t> checked;    // location ids, in the order they were checked
  bool start_given = false;        // the seed's starting items are in the inventory
  bool goal_done = false;          // the goal battle was won (sent again on connect)
  // Phase 3.
  std::vector<int> characters;          // character items received (party ids 1-12)
  std::vector<int> characters_waiting;  // received, not joined yet (no room, a Private Action...)
  std::vector<int> first_drops;         // enemy types whose first drop was taken as a check
  int ambushes = 0;                     // ambush traps received, not fought yet
  // Phase 4.
  std::vector<int> keys;                // area keys received (their numbers, in the order they came)
  // v2.
  std::vector<int> goal_won;            // goal.all's encounters won so far (in order)
  // v2 party [party agent] (docs/ap/seed_format.md "Party v2").
  std::vector<int> temporary;           // members a split window let in for a while (party ids)
  struct Stash {
    int slot = 0;                       // the party slot a temporary member borrowed (a full party)
    std::vector<u8> bytes;              // its member's slot (0x60) and record (0xd0), put back later
  };
  std::vector<Stash> stash;
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

// Checks: recorded once each (offline too, sent on connect).
const Location* find_location(int64_t id);
bool location_checked(int64_t id);                  // records it; false for an unknown id
bool is_checked(int64_t id);
void on_flag_set(int flag);                         // script flags: chests by opened flag

// Received items: queued, then given in free play (inventory, Fol) with a notice.
void receive(const ItemDef& item, const std::string& from);
bool receive_by_name_or_id(const std::string& what, const std::string& from, std::string* error);
size_t pending_count();                             // local and server items still to give
void free_play_update();                            // the field's play state calls this

// Saves: the sidecar goes with the save; a load reads it (a new game starts fresh).
void on_new_game();
void on_saved(const std::string& card_image, int save_index);
void on_loaded(const std::string& card_image, int save_index);

// ---- The server (phase 2) ----

// main.cpp, after start(): the link to the server (null: offline). game/ap polls it once per
// vblank (Runtime::net_poll) and sends checks only once the room's seed matches the seed.
void connect(std::unique_ptr<ap_net::Link> link);
bool deathlink_enabled();                           // the seed's DeathLink option
void vblank();                                      // the link's events (every vblank)
bool connected();                                   // slot connected and the seed matches

// ---- Game hooks (phase 2) ----

// Field give_item (script B 0x60) / add money (ext 0xd4): true when the value is one of the
// seed's placeholders, which was handled (the location's item given or sent, the check
// recorded); the original op is then skipped.
bool on_give_item(s32 value);
bool on_give_money(s32 amount);
// A message window opening (script ext 0x64 family): the text to show instead, set by the
// placeholders reached in the same vblank ("Got Blueberries." / "Sent Hookshot to Alice.", a
// page each), else null. Messages no window takes become banners. A placeholder reached
// again in the same vblank (a loop giving copies) is ignored.
const u8* take_message_override();
// battle_exit_to_field: boss checks and the goal. A boss location is checked on a victory
// (outcome 0), on a time-up (4: a timed survival lasted, 516 / 520) and, when the script let
// the party lose (the flag battle_frame latched), on the wipe (2: 204 / 517 / 530); never on an
// escape (1, 3). The goal needs a victory.
void on_battle_exit(int encounter, int outcome);
// The battle's frame: latches the lose-allowed flag (field_flags +0x7E = 0) for
// on_battle_exit; a received DeathLink KOs the party (the console's own defeat follows),
// except in a fight the script lets the party lose, where it waits for the field.
void battle_frame();
// The lose-allowed flag as battle_frame latches it (tests; on_battle_exit clears it).
void note_battle_lose_allowed(bool lose_allowed);
// field_begin_game_over: DeathLink out (not for a death a DeathLink caused).
void on_game_over();
// A DeathLink as if received (console `ap_deathlink`; the link's kDeathLink).
void deathlink_receive(const std::string& source, const std::string& cause);
bool deathlink_pending();

// ---- Phase 3: characters, drops, traps, multipliers, DeathLink's safe points ----

// The program running (boot run_program): received DeathLinks are dropped on the title and in
// the credits, and after the final boss (the ending).
enum class Scene { kTitle, kField, kBattle, kCredits };
void on_program(Scene scene);
// Script ext 0xae party_add (character = the script's argument, the party id - 1): true when
// Archipelago takes the join (options.character_items: a join site the seed lists records its
// check, and the member joins only if their item was received). 'at' is the instruction's
// own byte offset, 'call_at' the innermost call into the running routine (-1: none).
bool on_party_add(int character, int at, int call_at);
// Script ext 0xaf party_remove, before the game's remove ('character', 'at', 'call_at' as above).
// options.character_items: a received member is recruited for good. Where the story parts with an
// active member for good (a parting choice: Celine at Cross Cave, Leon at the Eluria colony, Opera
// at the Hoffmann Ruins; Mars sending an unrecruited Celine home), a received one rejoins at the
// next free play. The story's other leaves are temporary and its own scenes bring the member back,
// so a join still waiting (the item's banner had not gone yet) is dropped: the story's comes.
void on_party_remove(int character, int at, int call_at);
// A global script flag the scripts are clearing (the VM's flag store): true when it stays set.
// A received Celine keeps flag 112 ("Celine travels with the party", map 131 stores it from
// party_has): Mars then sends her home only for a while (map 462 / 126) and map 125 (chapter
// 15, step 3300) brings her back, as for a Celine the player kept at Cross Cave.
bool keeps_flag(int flag);

// ---- v2 party (docs/ap/seed_format.md "Party v2") [party agent] ----
// party_rules "open" (open world with character_items): only the story members (the hero; both
// heroes under story_route "any"), received members and the starting character join, at any site
// (a listed one still records its check); no lockouts; a slot of a story member or of a member
// recruited for good (received, the starting character) is never overwritten: with every slot
// taken a join takes a reserve slot of another member, else it is skipped. The story's split
// windows (ap.cpp kSplitWindows) let the members a fight needs in for a while (Claude in Shingo
// Forest in Rena's game; Dias, and Claude in Rena's game, at the tournament) and take them out
// once over; a member recruited for good whom a split set aside rejoins once no window keeps them
// away. The starting character (also without character_items) joins at the first free play
// before the starting items and becomes the Formation leader (config +0x41).
// The starting character in play: 0 without one or when it names the picked game's hero.
int starting_member();
// The split window on now that keeps member 'id' away (id 0: one keeping everyone away), or "";
// the one that lets member 'id' in for a while, or "".
std::string split_window_keeping(int id);
std::string split_window_needing(int id);
// Free play's party step at once (the start's join, the split windows, temporary members, the
// join queue): the banner it would show ("" none). Tests.
std::string party_free_play_step();
// Result screen #1981: award_exp_money's totals (exp / fol multipliers) and roll_drops (false:
// the drop is not given, it was the enemy type's first-drop check). Both leave the game's
// values alone without a seed or with the options off.
void scale_battle_rewards(s32* exp, s32* fol);
bool on_enemy_drop(int enemy_type, int item);
// The field's random encounters (field_update_encounters): options.encounter_rate.
bool random_encounters_off();             // encounter_rate 0
s32 scale_encounter_distance(s32 distance);  // the distance to the next one, * 100 / rate
bool party_has_character(int id);         // in a party slot, active or in reserve
// New Game's choice of story_route (title new_game.cpp): 0 free, 1 Claude, 2 Rena (the
// seed's options.story_route; the locations the seed holds are that story_route's game's).
int required_story_route();

// ---- Phase 4: the open world (doors and keys, the Expel teleporter), shop slots ----

// The seed names script places (doors, routines, join sites) by NA's code offsets; these
// hooks take the loaded release's offsets and translate them (runtime/region.h
// script_offset / script_na_offset), so one seed plays on every release.
// field/map.cpp: map 'map''s script was loaded (on the world maps, a chunk's). The seed's doors
// on it are checked: a door whose word is not a map transfer is ignored (and logged).
void on_script_loaded(int map, const std::vector<u8>& script);
// A trigger region about to start the routine at byte offset 'routine' of the loaded script:
// true when it is a door routine the open world keeps shut (every seed door in it needs a key
// not received; a door with `keys` needs all of them: the El voyage's trigger). Nothing of it
// runs; a banner names each missing key.
bool door_blocked_at_trigger(int routine);
// A map transfer (script ext 0x7d / 0xa4 / 0xda) at byte offset 'offset'; args = map,
// transition, x, y, z, dir (as fetched). True: a locked door, the transfer is skipped. A seed
// door with a target rewrites args (map, x, y, z, dir).
bool on_map_transfer(int offset, s32* args);
bool has_key(int key);
// Shops (menu_shop.cpp settle): 'count' copies of entry 'index' (item 'item') of shop 'shop'
// bought or ordered. Returns the copies the game itself gives: count - 1 when the first copy
// was a shop slot's check (its item given, or sent), else count.
int on_shop_buy(int shop, int index, int item, int count);
bool shop_slot_open(int shop, int index, int item);  // an unchecked shop-slot location there

// ---- Port Hilton's harbour (open world; docs/ap/seed_format.md "Phase 4", "The harbour") ----
// An open-world seed gets port-added patch lines on map 169 (the Hilton sailor, actor 17, routine
// 0x714c; the Hoffmann ship's soldier, actor 24, routine 0x79c0) and map 144 (Herlie's sailor,
// routine 0x7298) that call three port-only script ops (table A subs 0x70-0x72, which do nothing
// in the original); field/script_table_a.cpp runs them with these.
enum class Voyage { kNotNow, kCross, kHoffmann, kEl };
const char* voyage_name(Voyage v);   // "Cross", "Hoffmann Ruins", "El", "Not now"
// The captain's destinations now, Not now not included: Cross; in open world also Hoffmann Ruins
// at chapters 23-59 (the story's first trip, at 23, stores chapter 24; map 231's soldier sails
// back until 59) and El at the story's voyage point (chapter 27 step 7000, disc 1: what room
// 444's trigger waits for). Outside open world: Cross only.
std::vector<Voyage> harbour_menu();
// Op 0x70, in place of the sailor's `push 2` (the choice library's option count): the count, Not
// now included. With more than Cross on offer the choice's two strings (the script's string
// indices 'question' and 'options') show the port's menu (script_text) until harbour_choose.
int harbour_open(int question, int options);
// A message window's text in place of the script's string 'index' ('original'); null: the
// script's own. Only the harbour's menu, while it is open.
const u8* script_text(int index, const u8* original);
// Op 0x71, after the choice: option 'choice' (-1: cancelled) of the menu harbour_open built.
// kStay: nothing (Not now, cancelled, or refused: a banner names the missing key); kCross: the
// sailor's own fare and voyage go on; kJump: go on at byte offset 'offset' of the loaded script
// (the Hoffmann soldier's sail, NA 0x7cf8, in this release's layout); kEl: every key of the El
// voyage's gate (seed door 29106503) is held: the caller plays room 444's ending
// (el_voyage_transfer).
struct HarbourChoice {
  enum Kind { kStay, kCross, kJump, kEl } kind = kStay;
  int offset = -1;
};
HarbourChoice harbour_choose(int choice);
// Room 444's ending of the El voyage's scene (routine 0x7c28) after its fade: stores chapter 28
// step 0 and gives its transfer (map, transition, x, y, z, dir, extra: 422, 130, -215, 0, 0, 180,
// 128) for script ext 0xda.
void el_voyage_transfer(s32 args[7]);
// Op 0x72: a voyage the script is about to start (its landing's script flag: 17 Hilton -> Herlie,
// 18 Herlie -> Hilton, 21 map 231 -> Hilton, 22 Hilton -> map 231) can land: false, with the
// banner naming the key ("Map 231 Pass needed to sail"), when the seed's door there needs a key
// not received. Before the fare, so a refused voyage costs nothing.
bool voyage_landing_open(int flag);

// ---- v2 (docs/ap/seed_format.md "v2 fields"): goals, crafting, killer moves ----
// Goals: on_battle_exit records goal.all's victories (outcome 0) in the sidecar's goal_won and
// sends the goal once all are won; goal.flags are set at the first field frame after New Game
// and after every load (free_play_update). Door conditions (a door's flags / won, any world
// mode) lock it like a missing key: door_blocked_at_trigger / on_map_transfer.
bool goal_won(int encounter);                 // in the sidecar's goal_won
// Shops' sell lists (menu_shop.cpp add_inventory): true for an item left out. The Silver
// Trumpet (item 48) while The Evil Melody (song 7, script flag 53) is not composed, in a seed
// whose goal.all holds Iseria Queen (561): she comes only from that song.
bool sell_locked(int item);
// CreationPage::produce() (field/menu_creation.cpp), the item-making specialties: 'item' was
// made (its common kind; success or failure alike). The seed's craft location for it is
// checked once; its item comes on top (given in free play, or sent); the made item is the
// player's as usual.
void on_craft(int item, bool success);
// options.no_fail_crafting: CreationPage::decide() makes every attempt that picked a result
// succeed.
bool no_fail_crafting();
// Killer-move checks: each unchecked killer_move location whose member (in a party slot) has
// used the move at least `uses` times (record + 0x8a + 2 * move) is checked. Run in free play
// after a battle and after a load (and by tests); returns the checks recorded.
int scan_killer_moves();

std::string status_text();
std::vector<std::string> waiting_notices();   // the banners not shown yet (tests)

}  // namespace ss::game::ap
