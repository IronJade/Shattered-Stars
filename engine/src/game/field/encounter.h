#pragma once

#include "core/types.h"
#include "game/common/battle_setup.h"
#include "game/field/field.h"
#include "game/field/tasks.h"

// Starting a battle from the field: picking the encounter and building the battle's
// load list (field_prepare_encounter 0x80050f64, battle_build_load_list 0x800788b0),
// preloading its entries while the player walks, the screen transition
// (BattleTransition 0x80045c8c), tearing the field down (field_start_battle 0x80051a34)
// and handing over to the battle program #2129 (field_exit_to_program 0x80051c9c).
// Formats: docs/notes/battle.md §4.
namespace ss::game::field {

// The entries the next battle needs, as field_prepare_encounter grouped them for
// preloading (consecutive archive entries are read as one preload).
struct BattlePreloads {
  u8 count = 0;          // 0x8007534c: groups
  s32 entry[13]{};       // 0x800759a8: first archive entry of each group
  s32 bytes[13]{};       // 0x800759dc: total size
  s32 pieces[13]{};      // 0x80075a10: entries in the group
  s32 order[13]{};       // 0x80075a44: load-list slot of each sorted id
  s32 last_group = 0;    // 0x80075340
  s32 last_stage = 0;    // 0x80075344
};
BattlePreloads& battle_preloads();

// 0x800788b0: the load list for 'encounter' on 'stage' (BattleSetup +0 .. +0xbc).
void battle_build_load_list(BattleSetup* b, int encounter, int stage);
u32 battle_pick_intro_voices(BattleSetup* b);       // 0x80078a30: voice entries (low / high 16 bits)
const u8* battle_voice_table(int character);        // 0x80078d18: 12 s16 per character, null outside 1..12

// 0x80050f64: picks an encounter from area group 'group' (negative: encounter -group
// exactly) on 'stage', builds g_battle_setup and the preload groups, starts streaming.
void field_prepare_encounter(Field* f, int group, int stage);
int field_battle_preloads_ready();                  // 0x8005161c: 0 not yet, 1 all but one, 2 all

// The encounter screen effect (0x18 bytes, vtable 0x80072e84, task priority 1).
struct BattleTransition final : Task {
  s32 type = 0;     // +0x08: 0 swirl (0x5f frames), 1 split, 2 long swirl (0xf0 frames)
  s32 timer = 0;    // +0x0c: vblanks since it started
  s32 done = 0;     // +0x10: the effect has ended (display off)
  s32 angle = 0;    // +0x14: accumulated swirl rotation
  void update() override;  // battle_transition_update 0x80045d30
};
BattleTransition* battle_transition();              // 0x800752d0
BattleTransition* battle_transition_construct(BattleTransition* t, int type);  // 0x80045c8c
void battle_transition_destruct(BattleTransition* t);                           // 0x80045cdc

void field_start_battle(Field* f, int transition);  // 0x80051a34
void field_destroy_battle_transition(Field* f);     // 0x80051c68
[[noreturn]] void field_exit_to_program(Field* f, int reason);  // 0x80051c9c
bool field_battle_program_loaded();                 // 0x8007534d: #2129 read into its buffer
// field_begin_game_over: the title program (entry 1) read into the same buffer / flag.
void field_read_title_program();

void field_unload_map(Field* f, bool free_scene);   // 0x8005166c
void field_destroy_subsystems(Field* f);            // 0x80049098
void party_validate_before_battle();                // 0x80052ff4
void party_stash_slot_ids();                        // 0x80052b68

}  // namespace ss::game::field
