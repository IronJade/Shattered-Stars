#pragma once

#include <array>

#include "core/types.h"
#include "game/boot/system.h"
#include "game/common/battle_setup.h"

// The field program (archive entry #3100, loaded at 0x8002f810): map exploration,
// events and the in-field menus. Names and layouts follow docs/catalog (module
// "field"); the analysis notes behind them are summarised in docs/catalog/formats.md.
namespace ss::game::field {

constexpr int kFieldProgramEntry = 3100;

// Byte at 'address' of the field program image (its constant tables).
const u8* field_data(u32 address);

// The field's core globals (field_bind_system 0x80048fb4, field_bind_state_block
// 0x80049460, and the per-frame pad / timing values the main loop publishes).
struct FieldGlobals {
  System* system = nullptr;            // 0x80075254
  HandoffRegistry* handoff = nullptr;  // 0x80075258: System + 0x5a20
  Loader* loader = nullptr;            // 0x8007525c: System + 0x10
  Pad* pad = nullptr;                  // 0x80075268: System + 4
  Sound* sound = nullptr;              // 0x80075274: System + 0x5aa4
  u8* config = nullptr;                // 0x80075270: handoff 2 (0x2a0 bytes)
  u8* inventory = nullptr;             // 0x80075278: handoff 6
  u8* party = nullptr;                 // 0x8007527c: handoff 3, 8 slots of 0x60
  u8* records = nullptr;               // 0x80075280: handoff 4, 8 records of 0xd0

  // The persistent state block (handoff 9, 0xa40 bytes).
  u8* state_block = nullptr;           // 0x800756fc: script call stack base
  u8* value_stack = nullptr;           // 0x80075700: block + 0x100
  u8* script_flags = nullptr;          // 0x80075704: block + 0x8c0 (0x170 bytes)
  u8* script_globals = nullptr;        // 0x80075708: block + 0x180
  u8* field_flags = nullptr;           // 0x8007570c: block + 0x180 (same bytes, viewed as flags)
  u8* game_state = nullptr;            // 0x80075710: block + 0x220 (0x2a0 bytes; +0x1a map)

  s32 frame_ticks = 1;                 // 0x8007531c: vblanks since the previous frame (1-3)
  s32 gpu_busy = 0;                    // 0x80075320
  u16 held[2]{};                       // 0x80075748 / 0x80075754
  u16 pressed[2]{};                    // 0x8007574c / 0x80075758
  u16 repeat[2]{};                     // 0x80075750 / 0x8007575c
  u8 draw_enabled = 1;                 // 0x8007572c
  u8 vsync_mode = 0;                   // 0x8007572d: VSync argument (0 field, 2 world map)
  u8 disc_error = 0;                   // 0x800759a4
  u8 disc_error_prev = 0;              // 0x800759a5
  u8 returning = 0;                    // 0x80076197: state came from a save or another program
  u8 battle_preloaded = 0;             // 0x8007533c: the next encounter's data is preloaded (kind 1)
  u8 battle_preload_requested = 0;     // 0x8007533d: an encounter's data is being preloaded
  u8 scripted_battle = 0;              // 0x800756d8: a script battle is pending (ext 0xb7 / 0xc2)
  u8 map_preload_pending = 0;          // 0x800756d9
  s32 map_preload_x = 0;               // 0x800756dc: script ext 0xa5's last map-transfer preload spot
  s32 map_preload_z = 0;               // 0x800756e0
  u64 vblank_base = 0;                 // host: runtime vblank at field_construct (g_frame_counter 0x80075324 = 0)
  u8 field_active = 0;                 // 0x80075355: actors update (set by field_load_map, scripts)
  s32 poison_next_step = 0;            // 0x80076314: step count (field_flags +0x18 >> 12) of the next poison tick
  u8 show_battle_results = 0;          // 0x80076278: open the post-battle / item-gained window
  u8 sub_overlay_hook = 0;             // 0x80076279: run the sub-overlay's per-frame hook (0x801ead88)
  BattleSetup battle_setup;            // 0x800752b8: becomes handoff 5 for the battle program
  u8* system_data = nullptr;           // 0x80075720: entry #3110 unpacked (strings, window art)
  u8* system_strings = nullptr;        // 0x8007571c
  const u8* ui_atlas = nullptr;        // 0x80075328: #3110's icon block + 0xc (UV rectangles)
};
FieldGlobals& globals();

// The program object (0x50 bytes, vtable 0x800730a4).
struct Field {
  System* system = nullptr;   // +0x08 (program_set_system)
  s32 unknown0c = 0;          // +0x0c
  s32 unknown10 = 0;          // +0x10
  s32 state = 0;              // +0x14: field_update state
  s32 state_timer = 0;        // +0x18
  s32 vehicle_a = 0;          // +0x1c
  s32 vehicle_b = 0;          // +0x20
  s32 pending_map = 0;        // +0x24
  u32 last_encounter_rate = 0;// +0x28
  u32 present = 0;            // +0x2c: 0 skips one PutDispEnv/PutDrawEnv
  s32 encounter_extra = 0;    // +0x30
  s32 on_save_point = 0;      // +0x38
  s32 saved_control = 0;      // +0x3c
  s32 dismount[3]{};          // +0x40
};

Field* field_program();                       // g_current_program 0x8002f8dc (null outside the field)
[[noreturn]] void field_entry(System* sys);   // 0x8002f810
[[noreturn]] void field_run(Field* f);        // 0x80048a84
void field_bind_system(Field* f);             // 0x80048fb4
void field_bind_state_block(Field* f);        // 0x80049460
void field_create_subsystems(Field* f);       // 0x800494dc
void field_load_system_data();                // 0x8004a428 (entry #3110)
void field_setup_screen(Field* f);            // 0x80049b74
[[noreturn]] void field_main_loop(Field* f);  // 0x80049bfc
void field_update(Field* f);                  // 0x8004c334: the state machine
void field_poison_step(Field* f);              // 0x8004c250
// 0x80052ce8: puts disc 'disc' (1 / 2) in the drive. The original saves VRAM, runs the
// insert-disc screen (0x8006f958) until loader_is_disc agrees and restores the display;
// the port swaps the configured image itself (loader_insert_disc, a no-op when the disc
// is already in) and otherwise stays on disc 1, which holds the same field / battle data.
void field_prompt_disc_change(Field* f, int disc, int keep_vram);
void field_update_encounters(Field* f);       // 0x8004c334 state 0: random encounters
void field_update_encounter_streaming(Field* f);  // 0x80050dec: voice streaming follows the rate
void field_begin_game_over(Field* f);         // 0x8004b418: state 6 (the title program is read)
// 0x8005ea54: creates the state block for a new game (or from the load-game snapshot,
// handoff 0xe): starting map 7 (Claude) or 18 (Rena), names, money.
void field_create_state_block();
void field_restore_preloads(Field* f);        // 0x80049738
void party_restore_slot_ids(Field* f);        // 0x80052bb8

}  // namespace ss::game::field
