#pragma once

#include <vector>

#include "core/types.h"

// The title program's text renderer (0x80049f40-0x8004ad4c), used by the New Game
// option windows. Glyphs are 1-bit bitmaps (archive entry #14, block 1) drawn with a
// one-pixel drop shadow into a 4-bit, 256x256-texel line buffer in RAM; each string
// becomes one sprite into that buffer, and the buffer is uploaded to VRAM (0x3c0,
// 0x100) once per frame. Strings are looked up by id in entry #14, block 0.
namespace ss::game {

class Window;

struct TitleText {
  s16 x = 0;                // 0x8004ec18: next free texel column on the current line
  s16 line = 0;             // 0x8004ec1a: first texel row of the current line
  std::vector<u8> strings;  // 0x8004ec1c: {u16 id, u16 offset} pairs ending in 0xffff, then strings
  std::vector<u8> font;     // 0x8004ec20: s32 glyph count, s32 height, widths[count], u16 rows
  std::vector<u8> pixels;   // 0x8004ec24: 0x8000 bytes, 128 bytes per texel row
};
TitleText& title_text();

void text_frame_begin();   // 0x8004ad0c: empty the line buffer (start of a frame)
void text_frame_upload();  // 0x8004ad4c: upload the used rows to VRAM (0x3c0, 0x100)

// 0x80049f40: ASCII (with '@' toggling the Japanese kana mode) to glyph codes:
// code = glyph index + 1, codes above 0x7f take two bytes (low 7 bits, then the rest).
void text_from_ascii(Window* w, const char* in, u8* out);
const u8* text_find(Window* w, int id);                    // 0x8004a234: string by id, or null
int text_width(Window* w, const u8* s);                    // 0x8004a2bc: in pixels
int text_glyph_offsets(Window* w, const u8* s, int* xs);   // 0x8004a358: x of each glyph and the end; returns the glyph count
// 0x8004a0b0: two-line underline (the colour, then black one pixel down-right) for a highlighted string.
void text_underline(Window* w, int x, int y, const u8* s, u32 colour);
// 0x8004a6d4: draws a string at window-relative (x, y) with palette 'palette' (CLUT row
// 0x1f9); bit 15 also underlines it. Returns the end position as (y << 16 | x).
u32 text_draw(Window* w, int x, int y, const u8* s, u32 palette);
void text_draw_id(Window* w, int x, int y, int id, u32 palette);  // 0x8004a3d0

// Primitives run in reverse order of queuing: window_text_begin (0x8004a438) queues
// the full-screen draw area that runs after the window's text, window_text_end
// (0x8004a500) the window's clip area and the font's texture page that run before it.
void window_text_begin(Window* w);
void window_text_end(Window* w);

}  // namespace ss::game
