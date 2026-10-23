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
// battle_exit_to_field: boss checks and the goal (outcome 0 = victory).
void on_battle_exit(int encounter, int outcome);
// The battle's frame: a received DeathLink KOs the party (the console's own defeat follows).
void battle_frame();
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
// Result screen #1981: award_exp_money's totals (exp / fol multipliers) and roll_drops (false:
// the drop is not given, it was the enemy type's first-drop check). Both leave the game's
// values alone without a seed or with the options off.
void scale_battle_rewards(s32* exp, s32* fol);
bool on_enemy_drop(int enemy_type, int item);
// The field's random encounters (field_update_encounters): options.encounter_rate.
bool random_encounters_off();             // encounter_rate 0
s32 scale_encounter_distance(s32 distance);  // the distance to the next one, * 100 / rate
bool party_has_character(int id);         // in a party slot, active or in reserve
// New Game's choice of protagonist (title new_game.cpp): 0 free, 1 Claude, 2 Rena (the
// seed's options.protagonist; the locations the seed holds are that protagonist's game's).
int required_protagonist();

std::string status_text();

}  // namespace ss::game::ap
