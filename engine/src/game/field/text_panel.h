#pragma once

#include "core/types.h"
#include "game/field/tasks.h"

// The field's simple window class (Window base, vtable 0x80074038, 0x8006ef10-
// 0x8006f8d4) and its TextPanel subclass (0x28 bytes, vtable 0x80072e5c,
// 0x800459c0-0x80045c78): a skinned frame around a gradient, with 8x8 ASCII text
// from the #3110 font strip ('@' toggles kana). The field keeps three panels
// (0x8007529c..0x800752a4) and four post-battle stat panels (0x800752e4); a panel
// is a priority-4 task while open: text printed during the frame sizes the frame
// drawn by its update.
namespace ss::game::field {

struct FieldWindow : Task {
  s16 x = 0, y = 0, w = 0, h = 0;  // +0x08
  u8 line_height = 0xC;            // +0x10
  s32 visible = 1;                 // +0x14
  s32 frame_visible = 1;           // +0x18
  void update() override {}
};

struct TextPanel : FieldWindow {
  s32 text_y = 0;        // +0x1c
  s32 text_x = 0;        // +0x20
  s32 max_width = 0x10;  // +0x24
  TextPanel();           // inline in field_create_subsystems
  ~TextPanel() override; // text_panel_destruct
  void update() override;  // text_panel_draw
};

void window_set_rect(FieldWindow* w, s16 x, s16 y, s16 width, s16 height);  // 0x8006ef10
void window_draw_frame(FieldWindow* w);                                     // 0x8006ef48 (OT 0x818)
// Draws ASCII text at window-relative (x, y) with CLUT 0x7f80 + palette (OT 0x819);
// '\n' moves down a line. Returns the end x.
int window_draw_ascii(FieldWindow* w, int x, int y, const char* s, int palette);  // 0x8006f5b4

void text_panel_open(TextPanel* p, int x, int y, int w, int h);   // 0x800459c0
void text_panel_destruct(TextPanel* p);                           // 0x80045a3c
void text_panel_draw(TextPanel* p);                               // 0x80045a90
void text_panel_print(TextPanel* p, const char* text, int palette);  // 0x80045b5c
void text_panel_clear(TextPanel* p);                              // 0x80045c78

}  // namespace ss::game::field
