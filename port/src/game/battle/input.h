#pragma once

#include "core/types.h"
#include "game/battle/battle.h"

// The player's battle controller (g_input 0x8006fad4, 0xa0 bytes; input_update 0x80049274
// and input_dispatch 0x80049310 with the mode handlers): attack / target select, the
// leader swap, free running, the skill shortcuts and the HUD gauge and range panel it
// draws. parts/p3.md "Battle input block".
namespace ss::game::battle {

struct BattleInput {
  u8 mode = 0;               // +0x00: 0 idle, 1 member select, 2 target select, 3 ring menu, 4 list menu
  s8 repeat_delay = 0;       // +0x01
  s8 camera_return = 0;      // +0x02: camera mode 2 ends when it runs out
  s8 member_t = 0;           // +0x03 / +0x04: member cursor timers
  s8 member_t2 = 0;
  s8 arrow_t = 0;            // +0x05: target arrows closing
  s8 tactic_t = 0;           // +0x06
  u8 bounce = 0;             // +0x07
  u8 octant[8] = {};         // +0x08: enemies by screen direction from the target
  u8 member_purpose = 0;     // +0x10
  u8 target_purpose = 0;     // +0x11: 0 attack, 1 spell, 2 item
  u8 saved_slot = 0;         // +0x12
  u8 ring_sel = 0;           // +0x13
  u8 ring_prev = 0;          // +0x14
  u8 hud_state = 0;          // +0x15: 0 none, 1 sliding in / shown, 2 sliding out
  u8 chain_latch = 0;        // +0x16
  u8 counter_latch = 0;      // +0x17
  s16 major[8] = {};         // +0x18
  s16 minor[8] = {};         // +0x28
  s16 xmark_t = 0;           // +0x38
  u16 target_cam_t = 0;      // +0x3a
  s16 ring_radius = 0;       // +0x3c
  s16 ring_rot = 0;          // +0x3e
  s16 hud_x = 0;             // +0x40
  Sprite* member_cursor = nullptr;  // +0x44
  Sprite* arrow_l = nullptr;        // +0x48
  Sprite* arrow_r = nullptr;        // +0x4c
  Sprite* xmark_l = nullptr;        // +0x58
  Sprite* xmark_r = nullptr;        // +0x5c
  Sprite* hud_panel = nullptr;      // +0x84
  Sprite* range_icon = nullptr;     // +0x88
  Sprite* tactic_icon = nullptr;    // +0x8c
  Sprite* marker = nullptr;         // +0x90: the target marker
  u8 marker_owner = 0;              // the console flags the marker's death in the low byte of +0x90
  u32 held = 0;              // +0x94
  u32 repeat = 0;            // +0x98
  u32 pressed = 0;           // +0x9c
};
BattleInput& battle_input();

void input_reset();                               // battle_init_objects
void input_update(BattleInput* in);               // 0x80049274
void input_dispatch(BattleInput* in);             // 0x80049310
void input_open_target_select(BattleInput* in);   // 0x8004b270
void input_hide_member_cursor(BattleInput* in);   // 0x8004b340
void input_hide_target_arrows(BattleInput* in);   // 0x8004b3a8
void input_target_select_update(BattleInput* in); // 0x8004c014
void input_classify_targets(BattleInput* in, s32 x, s32 y);  // 0x8004cb8c
void input_pick_target(BattleInput* in, u32 a, u32 b, int dir);  // 0x8004ccf4
int pc_nearest_enemy_index(const Unit* u);        // 0x8003b56c

}  // namespace ss::game::battle
