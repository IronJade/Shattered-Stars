#pragma once

#include "core/types.h"
#include "game/battle/battle.h"

// The battle's window system (base class vtable 0x8006d160, 0x80067874..0x80069194) and
// the text renderers: the 8x8 UI font drawn as SPRT8 cells of the #1979 atlas, and the
// proportional #1980 font rendered into a 4-bit line buffer (64 rows of 256 texels)
// uploaded to VRAM (0x3c0, 0x1c0) every frame. A window owns a RenderContext (an arena
// and a one-entry OT) in one of the 8 OT layers while it is open.
namespace ss::game::battle {

struct BWindow : Task {
  s16 x = 0, y = 0, w = 0, h = 0;   // +0x08 rect
  s16 saved[4] = {};                // +0x10 window_save_rect
  u8 line_height = 0xC;             // +0x18
  RenderContext ctx;                // +0x1c (the console allocates it)
  bool opened = false;

  explicit BWindow(int prims);      // window_create 0x80067874: arena prims * 0x28 bytes, 1 OT entry
  ~BWindow() override;              // window_destroy 0x800678e8
  void update() override { draw(); }  // [1]
  virtual void draw() {}
  virtual void open(s16 nx, s16 ny, s16 nw, s16 nh);  // [3] window_open 0x80067960
  virtual void release();           // [4] window_release 0x800679ac (closes; the owner deletes)
  virtual void draw_contents() {}   // [5] 0x80069170
};

void window_draw_frame(BWindow* w);         // 0x80067a04
void window_save_rect(BWindow* w);          // 0x80069128
void window_restore_rect(BWindow* w);       // 0x8006914c
// 0x800681b8: ASCII in the 8x8 UI font at window-relative (x, y), CLUT 0x7f80 + palette;
// '\n' moves down a line; returns the width drawn.
int window_draw_small_text(BWindow* w, int x, int y, const char* s, int palette);
void text_from_ascii(const char* in, u8* out);  // 0x800683a8
const u8* text_find(int id);                    // 0x80068468: #1980 block 0 by id, or null
int text_width(const u8* s);                    // 0x800684f0
int text_width_id(int id);                      // 0x800684bc
void text_underline(BWindow* w, int x, int y, const u8* s, u32 colour);  // 0x80068568
// 0x80068a0c: a glyph-code string at window-relative (x, y) with palette (CLUT row 0x1f9);
// bit 15 also underlines it (colour table 0x8006d198). Returns (y << 16 | x) of the end.
u32 text_draw(BWindow* w, int x, int y, const u8* s, u32 palette);
u32 text_draw_id(BWindow* w, int x, int y, int id, u32 palette);  // 0x800686f8 (text_draw's end position)
void window_text_begin(BWindow* w);  // 0x80068760: full-screen draw area, runs after the text
void window_text_end(BWindow* w);    // 0x80068834: the window's clip area and the font tpage
void text_frame_begin();             // 0x80069060
void text_frame_upload();            // 0x800690a0
void text_reset();                   // load_battle_text: the 0x2000-byte line buffer

}  // namespace ss::game::battle
