#pragma once

#include <cstring>

#include "core/types.h"

// State shared by the message system's source files (not part of the API).
namespace ss::game::field {

const u8*& msg_font_ref();    // g_map_font 0x80075714 (swapped for windows 10-19)
const u8*& msg_glyphs_ref();  // g_map_glyphs 0x80075718
extern const u8* g_side_font;     // 0x800762f0
extern const u8* g_side_glyphs;   // 0x800762f4
extern const u8* g_side_strings;  // 0x800762f8

inline s32 msg_rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
inline u16 msg_rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

// Advance width of glyph code c (1-based) in the current font.
inline int msg_glyph_width(u32 c) { return msg_font_ref()[c + 7]; }

}  // namespace ss::game::field
