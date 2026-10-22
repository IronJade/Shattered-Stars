#pragma once

#include "core/types.h"
#include "game/battle/window.h"

// #1981, the result screen (0x80085518-0x80088054; native in the port, its tables read from
// the image): EXP and Fol, drops, level-ups with the skills learned, the defeat / escape
// messages, and the fade that ends the battle (battle_fade_task, state 5). The awards are
// written straight into the handoff blocks the field reads back: config (2) Fol, the party
// slots (3) EXP, HP, MP, level and attributes, the character records (4) SP, learned skills
// and attribute 9, the inventory (6) the drops.
namespace ss::game::battle {

struct Sprite;

struct ResultScreen : BWindow {
  u8 state = 0;              // +0x20
  u8 unused21 = 0;           // +0x21
  u8 confirmed = 0;          // +0x22: the page was confirmed; the text scrolls away
  u8 drop_count = 0;         // +0x23
  u8 drop_row = 0;           // +0x24: first drop on the page
  u8 member = 0;             // +0x25: level-up pages: the member shown
  u8 line = 0;               // +0x26: level-up pages: first line on the page
  u8 voice_done[4] = {};     // +0x27: the member's result line was spoken
  u16 drops[8] = {};         // +0x2c: dropped items
  s32 scroll = 0;            // +0x3c
  s32 exp = 0;               // +0x40: EXP per member (after the last member's bonus)
  s32 fol = 0;               // +0x44
  s32 levels[4] = {};        // +0x48: levels gained
  s32 learned[4][24] = {};   // +0x58: message ids of the skills learned (0x60 per member)
  s16 sp[4] = {};            // +0x1d8: SP gained
  s16 target_w = 0;          // +0x1e0
  s16 target_h = 0;          // +0x1e2
  u16 timer = 0;             // +0x1e4: vblanks on the page (auto-advance past 0x168)
  Sprite* prompt = nullptr;  // +0x1e8: the button prompt

  ResultScreen();             // result_screen_ctor 0x80085f4c (window_create(0x80))
  void draw() override;       // [1] result_screen_update 0x80086000
  void open_screen();         // [6] result_screen_open 0x80085f94
};

// The level-up context (0x404 bytes): the cumulative EXP table, [0] reused as the SP
// gained; the slot and record being raised.
struct LevelUpCtx {
  s32 table[255] = {};       // [1..254] EXP to reach the next level
  u8* slot = nullptr;        // +0x3fc
  u8* rec = nullptr;         // +0x400
};

s32* exp_table_build(s32* table);                         // 0x80085518
int levelup_apply(LevelUpCtx* ctx, u8* slot, u8* rec);    // 0x800855a4: levels gained
void levelup_gain_hp_mp_sp(LevelUpCtx* ctx);              // 0x80085ab8
const u16* levelup_learn_skills(LevelUpCtx* ctx, u8* slot, u8* rec);  // 0x80085764: 0-terminated ids

ResultScreen* battle_result_screen();  // g_result_screen 0x8006fab8 (null when none)
void result_screen_start();            // battle_main_loop at state 4: new + vtable[6]
void result_screen_free();             // battle_free_all (a screen still open)
void battle_fade_start();              // battle_fade_task_ctor 0x800872f8 (deletes itself)

}  // namespace ss::game::battle
