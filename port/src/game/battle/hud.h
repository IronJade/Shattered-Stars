#pragma once

#include "core/types.h"
#include "game/battle/window.h"

// The top status bar (BattleHud, 0x88 bytes, vtable 0x8006d130, g_hud 0x8006fa9c), the
// empty debug console window (0x470 bytes, vtable 0x8006b848, 0x8006faa0) and the bottom
// message banner (MessageWindow 0x30 bytes, vtable 0x80080f7c, 0x8006faa8).
namespace ss::game::battle {

struct BattleHud : BWindow {
  Sprite* run_icon = nullptr;      // +0x20: 1 paused / 2 running (anim -1 until then)
  Sprite* cursor = nullptr;        // +0x24: under the controlled member
  Sprite* member_cursor = nullptr; // +0x28: member select (input mode 1)
  Sprite* key_icon[2] = {};        // +0x2c: killer-move buttons (target select)
  Sprite* tactic_icon[4] = {};     // +0x34: "no orders" marks
  Sprite* item_icon = nullptr;     // +0x44
  Sprite* item_ready = nullptr;    // +0x48
  s16 phase = static_cast<s16>(0xFF01);  // +0x4c: highlight pulse
  s16 amp = 0;                     // +0x4e
  s32 hp_shown[4] = {};            // +0x50: displayed HP, moving toward the real value
  s32 mp_shown[4] = {};            // +0x60
  // +0x70 flash per member: BattleGlobals::hud_member_flash.
  u8 visible = 0;                  // +0x80
  s32 clip = 0;                    // +0x84: rows hidden at the top (slide)

  BattleHud() : BWindow(0x50) {}
  void draw() override;            // [1] hud_draw 0x800654f4
};

struct DebugWindow : BWindow {     // 0x8003be6c: drawn never
  DebugWindow() : BWindow(8) {}
};

struct MessageWindow : BWindow {
  s32 text_id = 0;                 // +0x20
  s32 ticks = 0;                   // +0x24
  s32 step = 0;                    // +0x28: 0 rising, 1 holding, 2 sinking
  s32 queued = 0;                  // +0x2c
  MessageWindow() : BWindow(0x14) {}
  void draw() override;            // [1] battle_message_update 0x80074e20
};

void hud_init(BattleHud* h);                        // 0x80065388
void hud_set_visible(BattleHud* h, int on);         // 0x80065464
// 0x80066d84: a 4-pixel bar at window-relative (x, y), (width - 1) * value / max filled.
void hud_draw_gauge(BWindow* w, int x, int y, int width, s32 value, s32 max);
void battle_message_start(MessageWindow* m, int id);  // 0x80074d54
void battle_windows_create();   // battle_init_objects: g_hud and the debug window
void battle_windows_open();     // battle_scene_start
void battle_windows_free();     // battle_free_all
BattleHud* battle_hud();
MessageWindow* battle_message();  // the banner on screen (null when none)
// 0x8003bf18 battle_start_burst_create (battle_scene_start): the radial wipe the battle
// opens with, a task with its own layer that deletes itself once the wipe is over.
void battle_start_burst_create();

}  // namespace ss::game::battle
