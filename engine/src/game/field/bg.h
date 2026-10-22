#pragma once

#include "core/types.h"
#include "game/field/tasks.h"

// The field-map background renderer (overlay 0xc1d: 0x128-byte FieldTask built by
// 0x8008672c, vtable 0x8008ec08, task priority 3; global 0x8007528c). The map's
// pre-rendered background lives in VRAM texture pages (map section 0); the scene's BG
// sprites (sub-block 1) redraw pieces of it at OT index layer + 0x11 so actors sort
// between them. Three sprite groups: plain sprites (scene +0x5c), parallax sprites
// (+0x5e, layers at +0x74) and wrap-around scrolling sprites (+0x60, windows at
// +0x76). Animation records (sub-block 4, +0x64) and colour records (sub-block 5,
// +0x72) step every frame.
namespace ss::game::field {

struct BgRenderer : Task {
  s16 focus_x = 0;          // +0x08: camera target screen x (leader +0x74)
  s16 focus_y = 0;          // +0x0a: camera target screen y - 16
  s16 scroll_x = 0;         // +0x0c
  s16 scroll_y = 0;         // +0x0e
  s16 offset_x = 0;         // +0x10: centring when the background is narrower than the screen
  s16 offset_y = 0;         // +0x12
  u32 tpage[4][16]{};       // +0x14: per blend mode (abr) and texture page slot
  s16 proportional_x = 1;   // +0x114: scroll in proportion to the focus (else centre on it)
  s16 proportional_y = 1;   // +0x116
  s32 floor_y = 0;          // +0x118: scene +0x58
  u8 tint_on = 0;           // +0x11c
  u8 tint_mode = 1;         // +0x11d: 0 = tint replaces the colour, 1 = multiplies it
  u8 tint[3]{};             // +0x11e
  u8 shake = 0;             // +0x121: random scroll jitter amplitude
  u8 vertical_scroll = 1;   // +0x122: background taller than 240 (224-line window)
  s16 forced_x = 30000;     // +0x124: fixed scroll position (30000 = follow)
  s16 forced_y = 30000;     // +0x126

  void update() override;   // bg_draw 0x80086f00: animate and draw
};

// Globals the BG code reads (set by field_load_map and scripts).
struct BgGlobals {
  u8 draw_enabled = 1;      // 0x80075354
  u8 animate_all = 1;       // 0x80076195: animated sprites ignore their scroll windows
  u8 animated_on = 1;       // 0x80076194
  u8 windows_on = 1;        // 0x80076198: scrolling windows
  s16 parallax_x = 30000;   // 0x80076224: forced parallax position (30000 = follow)
  s16 parallax_y = 30000;   // 0x80076226
  s32 colour_speed = 100;   // 0x8007535c: colour-record speed, percent
};
BgGlobals& bg_globals();

BgRenderer* bg_renderer();             // 0x8007528c
void bg_construct();                      // overlay 0x8008672c
void bg_destroy();                     // vtable +0x14 (0x800867b0)
void bg_helper_create();               // overlay 0x8008e100 (0x17c) + 0x8008e1c0
void bg_build_tpages(BgRenderer* bg);   // overlay 0x80086ad4: texture page words
void bg_update_scroll(BgRenderer* bg); // overlay 0x800867d8
void bg_suspend(BgRenderer* bg);       // vtable +0x1c (0x800895e0): before another program
void bg_resume(BgRenderer* bg);        // vtable +0x24 (0x800897bc): back from it (handoff 0x10)
u8* bg_take_anim_save();               // 0x80075728: bg_suspend's copy, for handoff slot 0x10
void bg_set_tint(BgRenderer* bg, int r, int g, int b);  // vtable +0x34 (0x80089988)
void bg_enable_tint(BgRenderer* bg, u8 on);             // vtable +0x2c (0x80089a48)
// 0x80086bbc: the view's top-left in scene coordinates (scroll - centring offset +
// scene +0x28 / +0x2c).
void bg_get_view_origin(const BgRenderer* bg, s16 out[2]);
// Script control of the animation records (by tag, scene record +0x0b).
int bg_anim_get_frame(BgRenderer* bg, int tag);             // 0x80086bf8: -1 no such record
void bg_anim_set_frame(BgRenderer* bg, int tag, int frame);  // 0x80086c4c
void bg_anim_play(BgRenderer* bg, int tag, int mode);        // 0x80086d20: 0 ping-pong, 1 loop, 2 once, 3 stop
int bg_anim_get_state(BgRenderer* bg, int tag);             // 0x80086e70

}  // namespace ss::game::field
