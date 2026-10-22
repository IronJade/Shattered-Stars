#pragma once

#include <array>
#include <memory>
#include <vector>

#include "core/types.h"
#include "game/boot/system.h"
#include "game/common/battle_setup.h"
#include "game/common/screen.h"

// The battle program (archive entry #2129, loaded at 0x8002f810): one fight, from the
// field's battle setup (handoff 5) to the outcome in config +0x178 and the swap back
// to the field (#3100). Structure and formats: docs/notes/battle.md; names follow
// docs/catalog (modules battle, battle_ovl1984_N, battle_ovl1981).
//
// The original loads code overlays (#1984 blocks, #1981, the party-character code
// #2131.., the encounter packs' enemy code) at fixed addresses. Here all of that is
// native code; only the data those entries carry is read from the disc.
namespace ss::game::battle {

constexpr int kBattleProgramEntry = 2129;

// Byte at 'address' of the #2129 image (its constant tables), or null.
const u8* battle_data(u32 address);
s16 battle_s16(u32 address);
u16 battle_u16(u32 address);
s32 battle_s32(u32 address);

struct Unit;
struct PartyUnit;
struct EnemyUnit;
struct Camera;
struct BObj;
struct Sprite;
struct Shot;

// Battle states (g_battle_state 0x8006efec).
enum BattleState : u8 {
  kStateLoading = 0,   // the party-data loader streams the members
  kStateLoaded = 1,    // intro camera, party appears
  kStateFighting = 2,
  kStateOver = 3,      // music fade, end countdown
  kStateResult = 4,    // victory result screen
  kStateLeave = 5,     // battle_exit_to_field
};

// Outcomes (g_battle_outcome 0x8006fb19 -> config +0x178).
enum Outcome : u8 { kVictory = 0, kEscaped = 1, kDefeat = 2, kRanOff = 3, kTimeUp = 4 };

// A battle task (the original's objects registered with task_register: vtable[1] is
// the per-frame update battle_main_loop calls, priority 0..4).
struct Task {
  virtual ~Task() = default;
  virtual void update() = 0;  // vtable[1]
};

void task_register(Task* task, int prio);  // 0x80046f70 (64 slots)
void task_unregister(Task* task);          // 0x80047004
void task_run_all();                       // battle_main_loop: priorities 0..4

// The eight OT layers (g_ot_layers 0x8006ef20..0x8006ef3c), drawn 7 -> 0.
void ot_layer_add(RenderContext* layer);       // 0x8004707c
void ot_layer_remove(RenderContext* layer);    // 0x800470ec
void ot_layer_to_front(RenderContext* layer);  // 0x80047128

// Archive entry data the battle keeps (load-list buffers and what the loaders unpack).
using Bytes = std::vector<u8>;

// Globals of the battle program (0x8006e8a0..0x80070b2c). Field names follow the
// catalogue; addresses are in the comments.
struct BattleGlobals {
  // System pointers (battle_run binds them).
  System* system = nullptr;           // 0x8006e8a0 b_system
  HandoffRegistry* handoff = nullptr; // 0x8006e8a4
  Loader* loader = nullptr;           // 0x8006e8a8
  Pad* pad = nullptr;                 // 0x8006e8b4
  Sound* sound = nullptr;             // 0x8006e8bc
  u8* config = nullptr;               // 0x8006e8d4 handoff 2 (0x2a0)
  u8* inventory = nullptr;            // 0x8006e8d8 handoff 6 (0xc28)

  // The setup (handoff 5, copied and freed by battle_run).
  BattleSetup setup;
  s16 encounter_id = 0;               // 0x8006ef98 g_encounter_id
  s16 stage_id = 0;                   // 0x8006ef9a g_stage_id
  std::vector<s16> preload_list;      // 0x8006fdf8 g_preload_list (0x22 bytes, handoff 0xc)
  Bytes handoff10_blob;               // 0x8006fdfc g_handoff10_blob (handoff 10)

  // State.
  u8 state = kStateLoading;           // 0x8006efec g_battle_state
  u8 flags = 0;                       // 0x8006fb07 g_battle_flags (1 intro, 4 pause, 8 freeze, 0x40 special move)
  u8 outcome = kVictory;              // 0x8006fb19 g_battle_outcome
  s8 end_countdown = 0;               // 0x8006fb14 g_end_countdown
  s8 hitstop_frames = 0;              // 0x8006fb15
  s8 slowmo_frames = 0;               // 0x8006fb16
  u8 slowmo_phase = 0;                // 0x8006fb37
  u8 party_escaping = 0;              // 0x8006f6cf g_party_escaping
  u8 can_escape = 1;                  // 0x8006efea g_can_escape
  u8 intro_type = 0;                  // 0x8006efeb g_intro_type (1 back, 2 surrounded, 3 raided)
  u8 enemy_count = 0;                 // 0x8006fb0b g_enemy_count
  u8 controlled_slot = 0;             // 0x8006fb06 g_controlled_slot (config +0x41)
  u8 leader_slot = 0;                 // 0x8006fb05 g_leader_slot
  u8 stage_hidden = 0;                // 0x8006f001 g_stage_hidden
  u8 effects_suppressed = 0;          // 0x8006f002
  u8 result_started = 0;              // 0x8006fb18 g_result_started
  u8 victory_music_started = 0;       // 0x8006efe9
  u8 dispmask_delay = 0;              // 0x8006efed g_dispmask_delay (0x8006f6ce = config +0x17e)
  s16 rule_timer = 0;                 // 0x8006f088 g_rule_timer (rule 3)
  s32 frame_ticks = 1;                // 0x8006efd0 g_frame_ticks: vblanks per frame (2 at 30 fps)
  s32 battle_frames = 0;              // 0x8006fb80
  s32 fight_frames = 0;               // 0x8006fb84
  u8 special_voice_set = 0;           // 0x8006f003
  u8 single_victory_voice = 0;        // 0x8006f004
  u8 intro_speakers[2] = {};          // 0x8006fb32 / 0x8006fb33
  Unit* last_attacker = nullptr;      // 0x8006fb00

  // Party and enemies.
  std::array<u8*, 4> party_slot{};    // 0x8006f9e4 g_party_slot: handoff 3 slot (0x60)
  std::array<u8*, 4> party_record{};  // 0x8006f9f4 g_party_record: handoff 4 record (0xd0)
  std::array<PartyUnit*, 4> party_units{};  // 0x8006f9b4 g_party_units
  std::array<EnemyUnit*, 8> enemy_units{};  // 0x8006f9c4 g_enemy_units
  std::array<u8*, 8> enemy_stats{};         // 0x8006fa04 g_enemy_stats (0x60 each)

  // Music.
  Bytes music_pack;                   // load list 0: the bank must outlive the battle's use
  Bytes battle_music;                 // 0x8006fae4 g_battle_music (sequence)
  Bytes victory_music;                // 0x8006fe00 g_victory_music

  // Text (#1980).
  Bytes text_strings;                 // 0x8006fad8 g_text_strings
  Bytes text_font;                    // 0x8006fadc g_text_font

  // Item data (#1983) and the battle item table.
  Bytes item_data;                    // 0x8006faf8 g_item_data (1023 x 0x30)
  std::array<u8, 0x880> item_effects{};  // 0x8006fafc g_item_effects

  // Kept-packed code entries (the port only needs to know they were loaded).
  bool have_battle_code = false;      // 0x8006e8c0 g_ovl_battle_code (#1984)
  bool have_result_code = false;      // 0x8006e8c4 g_ovl_result_code (#1981)
  Bytes battle_code;                  // #1984: its blocks' data tables are read by the port

  // Sprite banks (#1979 / #1982).
  Bytes ui_sprites0, ui_sprites1;     // 0x8006f8d4 / 0x8006f8e8
  Bytes fx_sprites;                   // 0x8006f8fc
  std::array<const u8*, 8> effect_data{};  // 0x8006f900 g_effect_data
  std::array<u16, 256> ui_palette{};  // 0x8006f6d4 g_ui_palette (CLUT row 0x1fe copy)
  u8 palette_cycle_timer = 4;         // 0x8006fb08

  // Render state.
  RenderContext* prim_pool_a = nullptr;  // 0x8006efcc g_prim_pool_a (3-D world)
  RenderContext* prim_pool_b = nullptr;  // 0x8006fad0 g_prim_pool_b (HUD sprites)
  std::array<RenderContext*, 8> ot_layers{};  // 0x8006ef20 g_ot_layers
  Camera* camera = nullptr;           // 0x8006f0ac g_camera
  u8 semi_tpage = 0;                  // 0x8006fb2e: DR_MODE tpage of model_render's semi-transparent untextured polygons

  // Tasks (64 slots).
  std::array<Task*, 64> tasks{};      // 0x8006e8dc g_tasks
  std::array<u8, 64> task_prio{};     // 0x8006eddc g_task_prio
  u16 task_count = 0;                 // 0x8006ef1c g_task_count

  // Owned objects (freed at exit; the console frees them in battle_free_all).
  std::vector<std::unique_ptr<RenderContext>> render_contexts;
  std::unique_ptr<Camera> camera_obj;
  std::unique_ptr<Task> party_loader;    // the party-data loader task (deletes itself at state 1)
  // The shared sprite-set BObjs (battle_init_objects): fx 0x8006f920 (#1982), fx2
  // 0x8006f924, set A 0x8006f928 (#1979 block 0, HUD), set B 0x8006f92c (#1979 block 1),
  // set C 0x8006f930 (the stage's op-13 sprites).
  std::unique_ptr<BObj> sprite_bank_fx, sprite_bank_fx2, sprite_set_a, sprite_set_b, sprite_set_c;

  u8 intro_voice_gate = 0;            // 0x8006fbaa: holds the intro's end while > 0
  u8 intro_voice_state = 0;           // 0x8006fb30 g_intro_voice_state
  u8 party_standing_at_start = 0;     // g_party_standing_at_start
  bool item_data_freed = false;       // the party loader frees g_item_data at state 1
  bool music_started = false;         // battle_run started g_battle_music
  bool battle_code_unpacked = false;  // #1984 block 1 unpacked (state 1)
  u64 last_frame_vblank = 0;          // the VsyncCounter (0x8006cd50) of the port
};
BattleGlobals& bg();

// Program flow (battle_program.cpp).
[[noreturn]] void battle_entry(System* sys);  // 0x8002f810
[[noreturn]] void battle_run();               // 0x8007160c
[[noreturn]] void battle_main_loop();         // 0x800459fc
[[noreturn]] void battle_exit_to_field();     // 0x80087644 (#1981)
void battle_init_state();                     // 0x80074a74
void battle_init_objects();                   // 0x80074168
void battle_scene_start();                    // 0x80046cc0
void battle_free_all();                       // 0x80087884 (#1981)

// Debug: when set, battle_main_loop ends the battle with this outcome after this many
// frames of fighting (the result screen and the units' combat are not ported yet).
struct BattleDebug {
  int auto_end_frames = -1;
  u8 auto_end_outcome = kVictory;
};
BattleDebug& battle_debug();

}  // namespace ss::game::battle
