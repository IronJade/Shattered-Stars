#pragma once

#include <string>
#include <vector>

#include "core/types.h"
#include "game/ap/seed.h"

// Archipelago, the hub (port feature; `world_mode: "hub"`, docs/ap/seed_format.md "Hub",
// docs/handoff/hub.md) [hubvm agent]: the two script-VM pieces of the hub open world.
//
// - The chapter lock (P2). The hub plays at chapter 60 for good. Outside a story view, a script
//   store to the chapter (G[0xa0], game_state +0) is dropped, and one to the step (G[0xa4]) too,
//   except on the maps of hub.lock.step_maps (default: 685, the goal fight's aftermath on the way
//   to the endings). Each dropped site is logged once. With the chapter held, the disc switch
//   (map 625 at chapter 34) never comes; script B 0x6a is dropped in hub mode as well.
// - The story views (P6). While the party is on a boss chamber's map and the boss is not won yet
//   (its encounter not in the sidecar's goal_won), the seed's view for it shows the chamber's
//   scripts the boss's own story state: the chapter / step bytes (G[0xa0]-G[0xa7]), the view's
//   flags and globals read and write a view copy, so the original scene (dialogue, fight,
//   aftermath) plays. The view ends at the first map load outside the chamber's maps (the
//   scene's transfer out, or the player walking out) or at the first free play after the boss was
//   won; what was stored in it is dropped then, except the `keep` flags, which are written to the
//   real flags. Party changes (ext 0xae / 0xaf) inside a view are dropped (characters are items in
//   the hub). New Game and a load turn a view off (nothing written back); a save holds the real
//   state, never the view's.
// Without a hub seed none of this runs: the VM tests g_hub_vm first and takes its own path.
namespace ss::game::ap {

// A hub seed is loaded: the VM hands its global variable and flag accesses to the hooks below.
extern bool g_hub_vm;

// ---- The script VM's hooks (field/script_vm.cpp, only while g_hub_vm) ----

// A read of 'size' (1, 2 or 4) bytes of the script globals at byte offset 'addr' ('real': their
// own memory): true when a story view supplies some of them (*out: the bytes, little-endian,
// zero-extended; the others from 'real').
bool hub_var_read(u32 addr, int size, const u8* real, u32* out);
// A store of the low 'size' bytes of 'value' at 'addr' ('real': their memory) by the instruction
// at byte offset 'at' of the loaded script: true when the hub took it, into the view (the bytes
// it redirects; the others written to 'real') or dropped (the lock, outside a view).
bool hub_var_write(u32 addr, int size, u32 value, u8* real, int at);
// A global script flag a story view lists: true, *on = its view value.
bool hub_flag_read(int flag, bool* on);
// A store to a global flag a story view lists: true (the view's value takes it).
bool hub_flag_write(int flag, bool on);
// Script ext 0xae / 0xaf (party_add / party_remove; 'character' = the script's argument, the
// party id - 1): true when a story view drops it.
bool hub_swallows_party(bool add, int character);
// Script B 0x6a (the disc switch): true in hub mode (the hub stays on disc 2).
bool hub_swallows_disc_change();

// ---- game/ap's hooks ----

void hub_start(const Seed* seed);   // the seed loaded (start / start_with), null: stopped
void hub_reset();                   // New Game, a load: any view off, nothing written back
void hub_map_loaded(int map);       // a map's script was loaded (a view starts or ends)
void hub_free_play();               // free play: a view whose boss was won ends (until "free_play")
// A map transfer about to start (script ext 0x7d / 0xa4 / 0xda; args = map, transition, x, y, z,
// dir): after the win, a view's cut_to replaces the first transfer out of the view (true: args
// rewritten; the view ends at the next map load).
bool hub_map_transfer(s32* args);
bool hub_boss_encounter(int encounter);  // a chamber's boss (its victory goes in goal_won)
void hub_battle_won(int encounter); // a victory (outcome 0)

// ---- Tests and the status line ----

bool hub_view_on();
int hub_view_encounter();           // the view's boss encounter, -1 without a view
std::string hub_status();           // "" without a hub seed
// The lock's and the views' log lines so far (since hub_start), oldest first.
const std::vector<std::string>& hub_log();

}  // namespace ss::game::ap
