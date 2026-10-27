#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/types.h"
#include "game/ap/bench.h"
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
    int by = 0;                         // who borrowed it (party id; 0: a sidecar from before) [partybuild agent]
  };
  std::vector<Stash> stash;
  // Sidecar version 2 [hubcore agent]: the save's bytes on the card as util::save_digest gives
  // them ("sha256:..."), written at every save. With a seed a load needs it to match
  // (load_allowed); never kept in the running state.
  std::string save_hash;
  // [hubask agent] An asked chamber's story view (a yes) on when the save was made: its encounter
  // (-1 none). The load turns that view on again with the map's load (the view's values were never
  // saved: the save holds the real state).
  int hub_view = -1;
  // [charselect agent] The starting character the player picked at New Game (options.starting_character
  // "any" or a list; party id, 0 none or the hero). Sidecar key "starting_character".
  int chosen_start = 0;
  // Skillsanity [skillsanity2]: skill items received (skill ids) and move items received
  // (character * 256 + record index), each once, in the order they came.
  std::vector<int> skills;
  std::vector<int> moves;
  // [partybuild agent] heroslot D2 / D3 (open world and hub, a starting member who is not the
  // hero; docs/handoff/heroslot.md): the member the player left leading (config +0x41's) and in
  // slot 0 at the last camp-menu close or save (at New Game: the starting member, the hero in
  // slot 1). Party ids, 0 none. Sidecar keys "leader" / "slot_0" (written only when set).
  int lead = 0;
  int first = 0;
  // [partybuild agent] The bench (game/ap/bench.h; options.bench): places 8-11, a member's slot
  // and record each. Sidecar key "bench" (only with someone benched: then sidecar version 3),
  // guarded by "bench_hash" (bench_digest: the save's bytes and the bench's), which, like
  // save_hash, is never kept in the running state. A load takes the sidecar's bench as it is.
  Bench bench;
  std::string bench_hash;
  // [partybuild agent] B4: benched members lent to the 8 for a fight the story makes them make
  // alone (a split window needing them: Claude in Shingo Forest in Rena's game, Dias / Claude at
  // the tournament). Their bench place stays theirs (empty, never given to a join) until they go
  // back to it. Sidecar key "lent" (only with a loan: then sidecar version 3), in "bench_hash".
  using Loan = BenchLoan;
  std::vector<Loan> lent;
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
void deliver_check(int64_t id);                     // records it (once): our own item given in free play, another's "Sent" [skillsanity2]
SaveState& mutable_state();                         // (game/ap/skills.cpp)
void on_flag_set(int flag);                         // script flags: chests by opened flag

// Received items: queued, then given in free play (inventory, Fol) with a notice.
void receive(const ItemDef& item, const std::string& from);
bool receive_by_name_or_id(const std::string& what, const std::string& from, std::string* error);
size_t pending_count();                             // local and server items still to give
void free_play_update();                            // the field's play state calls this

// Saves: the sidecar goes with the save; a load reads it (a new game starts fresh).
void on_new_game();
// The hub's New Game [hubcore agent] (docs/ap/seed_format.md "Hub", "Order at New Game"):
// field_create_state_block's new game ('block': the state block, 0xa40 bytes; the original's
// state is in it: the hero's route flag and bytes, the names, chapter 3 / 4) becomes the hub
// seed's: its flags (all others cleared; the picked hero's route flag kept), its globals, the
// hero's own flags and words, the chapter and step, the disc (config +0x4c, the disc inserted),
// the start's Synard words and the start (game_state +0x1a map, +0x08 position, +0x18 facing),
// which the first map load keeps (take_start_position). Nothing without a hub seed whose hub
// block holds the state. A debug start (--map, 'debug_start') keeps its map: there the state is
// applied only with SS_HUB_STATE=1 (Claude's game) or SS_HUB_STATE=rena (Rena's).
void on_new_game_state(u8* block, bool debug_start);
// The field's first map load after on_new_game_state: true once (the leader goes to the start's
// position and facing instead of the map's default spot); start_position_pending peeks.
bool take_start_position();
bool start_position_pending();
// 'file' (optional): the save as the card driver read it back after writing it ('length'
// bytes available, a block); the sidecar then holds its hash (save_hash).
void on_saved(const std::string& card_image, int save_index, const u8* file = nullptr, size_t length = 0);
void on_loaded(const std::string& card_image, int save_index);
// The save guard (all world modes; the user, 2026-10-25: "block save editing for the archi
// runs") [hubcore agent]. Before a load reaches the game's memory (menu/menu_save.cpp
// load_finish: the title's load screen and SS_LOAD_SAVE): with a seed on, a save loads only when
// its sidecar names this seed and slot and holds the hash of the save as it is on the card
// ('file', 'length' bytes read); otherwise false (the game's own "load failed") and a log line
// says why. A guard against accidents and casual save editors, not against cheating (the port
// is open source). A sidecar with a bench [partybuild agent] also needs its bench_hash to match
// the save and the bench (an edited bench, or one moved to another save, is refused the same
// way). Without a seed: always true. SS_AP_ANY_SAVE=1 (headless tests; a windowed
// seed run clears every SS_* hook) lets any save load as before, logged.
bool load_allowed(const std::string& card_image, int save_index, const u8* file, size_t length);

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
// [charselect agent] New Game's character select (open world and hub). The seed's named starting
// character (0 none), the members the player may pick (empty: no choice: options.starting_character
// "any" or a list), and the pick (0 or the hero: nobody extra), kept for the game about to start
// (on_new_game puts it in the state and the sidecar).
int seed_starting_character();
std::vector<int> starting_choices();
void set_chosen_start(int id);
// The split window on now that keeps member 'id' away (id 0: one keeping everyone away), or "";
// the one that lets member 'id' in for a while, or "".
std::string split_window_keeping(int id);
std::string split_window_needing(int id);
// Free play's party step at once (the start's join, the split windows, temporary members, the
// join queue): the banner it would show ("" none). Tests.
std::string party_free_play_step();
// [partybuild agent] heroslot (docs/handoff/heroslot.md D1-D3; open world and hub, a starting
// member who is not the hero, else nothing). D1: the starting member's join at New Game puts
// them in slot 0 and the hero in slot 1, and they lead (config +0x41 = 0). D2 / D3: the port
// remembers the leader and the slot-0 member the player left (the camp menu's close, a save);
// at free play a scene's reorder is undone (that member back in slot 0, the leader's slot in
// config +0x41 again), except while the party is set aside or split (a Private Action, a split
// window, temporary members, the endings, a hub story view), when nothing is remembered either.
// field/menu_camp.cpp calls these as the camp menu opens (the button: the order is put right
// first) and after it closes (not after a load).
void on_camp_menu_opening();
void on_camp_menu_closed();
// Result screen #1981: award_exp_money's totals (exp / fol multipliers) and roll_drops (false:
// the drop is not given, it was the enemy type's first-drop check). Both leave the game's
// values alone without a seed or with the options off.
void scale_battle_rewards(s32* exp, s32* fol);
bool on_enemy_drop(int enemy_type, int item);
// The field's random encounters (field_update_encounters): options.encounter_rate.
bool random_encounters_off();             // encounter_rate 0
s32 scale_encounter_distance(s32 distance);  // the distance to the next one, * 100 / rate
bool party_has_character(int id);         // in a party slot, active or in reserve
// [partybuild agent] The bench (game/ap/bench.h): a bench seed is playing (options.bench).
bool bench_enabled();
// The bench's store (places 8-11), null without a bench seed. The camp menu's pages read and
// change it (field/menu_combat.cpp Party Order; docs/handoff/bench.md B1 / B2).
Bench* bench_store();
// Party Order with the bench (docs/handoff/bench.md Design 2). Locked (no member goes to or comes
// from the bench): while the party is set aside or split (a Private Action, a split window, temporary
// members, a borrowed slot, the endings, a hub story view: heroslot's locked states).
bool bench_locked();
// A swap between a game slot holding 'in_slot' (its party id as the slot has it: negative for a
// member set aside, 0 empty) and a bench place holding 'benched' (0 empty): why it is refused (and
// logged), or null. Refused: anything while locked; a member set aside; the hero; a temporary
// member; a benched member whom a split window keeps away now (into the 8). The page's own rule
// (a front member left who can fight) is the page's.
const char* bench_swap_refused(int in_slot, int benched);
// The page closed with the bench on: the party and the bench to the log.
void on_bench_order_closed();
// New Game's choice of story_route (title new_game.cpp): 0 free, 1 Claude, 2 Rena (the
// seed's options.story_route; the locations the seed holds are that story_route's game's).
int required_story_route();
// [difficulty agent] New Game's Battle Rank (title new_game.cpp): the seed's options.difficulty,
// 0 Earth / 1 Galaxy / 2 Universe (set, the screen skipped), 3 any (the screen offered); -1 without
// a seed, with Archipelago off or a seed without the key (the original's rule).
int seed_difficulty();

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
// The same test without the banner or the log [salvadrift agent]: the field asks it every frame
// the hero touches a region door_blocked_at_trigger refused (script_triggers.cpp ShutDoor: the
// hero goes no further in); false once the door has opened.
bool door_shut_at_trigger(int routine);
// A map transfer (script ext 0x7d / 0xa4 / 0xda) at byte offset 'offset'; args = map,
// transition, x, y, z, dir (as fetched). True: a locked door, the transfer is skipped. A seed
// door with a target rewrites args (map, x, y, z, dir).
bool on_map_transfer(int offset, s32* args);
// After on_map_transfer returned true: the transfer waits for the hub's question (a chamber's
// `ask`, game/ap/hub.h hub_ask_transfer): the op must run again (ip back on it, its stack
// arguments back on the stack) [hubask agent].
bool transfer_again();
bool has_key(int key);
// Shops (menu_shop.cpp settle): 'count' copies of entry 'index' (item 'item') of shop 'shop'
// bought or ordered, after the game has booked every copy (all of them the player's). When the
// entry is a shop slot not checked yet, this first purchase is also its check: its item given
// (waiting while there is no room) or sent; true then [shopboth agent: was count - 1 copies].
bool on_shop_buy(int shop, int index, int item, int count);
bool shop_slot_open(int shop, int index, int item);  // an unchecked shop-slot location there
// The courier's delivery of an order (field_world.cpp state 0x1e): 'count' copies of 'item'
// the inventory refused (20 of a kind at most: a slot's check can bring one more of an ordered
// item). Archipelago on: what fits goes in now, the rest waits ("Got X" once there is room)
// instead of vanishing as in the original. Off: false, nothing done [shopboth agent].
bool order_overflow(int item, int count);

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
// ASCII as the map font's codes (no end code; the release's glyphs, '?' past ASCII): the port's own
// message texts (the hub's questions) [hubask agent].
std::vector<u8> glyph_text(const std::string& ascii);
// Its width in pixels in the map font (0 before the font is loaded).
int glyph_text_width(const std::string& ascii);

}  // namespace ss::game::ap
