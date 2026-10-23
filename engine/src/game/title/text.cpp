#include "game/title/text.h"

#include <cstring>

#include "game/common/screen.h"
#include "game/common/text_rules.h"
#include "game/title/title_program.h"
#include "game/title/window.h"
#include "runtime/libgpu.h"

namespace ss::game {
namespace {

TitleText g_text;

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}

u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

// Reads one glyph code and advances; codes are stored plus one.
u32 next_code(const u8*& s) {
  u32 c = *s++;
  if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*s++) << 7;
  return c;
}

void add_prim(Window* w, void* prim) { psyq::addPrim(w->ctx->ot_head, prim); }

// Queues the sprite that shows texels [start, end) of the current line at (sx, sy).
void queue_line_sprite(Window* w, int sx, int sy, int start, int end, int height, u32 palette, int width) {
  auto* p = screen_alloc_prims<psyq::SPRT>(w->ctx->arena, 1);
  psyq::setlen(p, 4);
  p->code = 0x65;
  p->x0 = static_cast<s16>(sx);
  p->y0 = static_cast<s16>(sy);
  p->u0 = static_cast<u8>(start);
  p->v0 = static_cast<u8>(g_text.line);
  p->clut = static_cast<u16>(palette + 0x7E40);
  p->w = static_cast<s16>(width);
  p->h = static_cast<s16>(height);
  add_prim(w, p);
  (void)end;
}

// Zeroes the byte at column x / 2 on the twelve rows of the current line (the shadow
// the previous string may have left there).
void clear_column(int x) {
  for (int row = 11; row >= 0; --row) g_text.pixels[static_cast<size_t>(x / 2 + (g_text.line + row) * 0x80)] = 0;
}

}  // namespace

TitleText& title_text() { return g_text; }

void text_frame_begin() {
  if (g_text.line != 0 || g_text.x != 0) {
    g_text.x = 0;
    g_text.line = 0;
  }
}

void text_frame_upload() {
  if (g_text.x != 0 || g_text.line != 0) {
    psyq::RECT r{0x3C0, 0x100, 0x40, static_cast<s16>(g_text.line + 0x0C)};
    if (r.h > 0xFF) r.h = 0x100;
    psyq::LoadImage(&r, g_text.pixels.data());
  }
}

void text_from_ascii(Window*, const char* in, u8* out) {
  // The ASCII table (0x8004eafc; its first 0x20 entries overlap the keyboard's column map):
  // read entry by entry through the region map, which may place the overlapped words apart
  // from the table in a release whose column map differs (France, Germany).
  const auto table = [](u32 c) { return *title_data(0x8004EAFC + c); };
  const int high_bias = text::ascii_high_bias(text::lang(), text::Program::kTitle);
  const auto* s = reinterpret_cast<const u8*>(in);
  int kana = 0;
  for (;;) {
    u32 c = *s;
    const u8* next = s + 1;
    if (c == 0) {
      *out = 0;
      return;
    }
    if (c == '@') {
      kana = 1 - kana;
      s = next;
      continue;
    }
    u32 glyph;
    if ((kana == 0 || c - 0xA6 > 0x37) && high_bias >= 0 && c >= 0x81) {
      glyph = table(c) + static_cast<u32>(high_bias);  // France: Latin-1 letters (text_rules.h)
    } else if (kana == 0 || c - 0xA6 > 0x37) {
      glyph = c - 0x61 <= 0x19 ? c - 0x20 : table(c);
    } else {
      glyph = table(c) + 0x38u;
    }
    // Voiced (0xde) and semi-voiced (0xdf) marks fold into the kana before them.
    const bool row_ka_ta = c - 0xB6 < 5 || c - 0xBB < 5 || c - 0xC0 < 5;
    if (*next == 0xDE) {
      if (kana == 0) {
        if (c == 0xB3) {
          glyph = 0xB3;
        } else if (row_ka_ta) {
          glyph = c - 2;
        } else if (c - 0xCA < 5) {
          glyph = c - 7;
        }
      } else if (row_ka_ta) {
        glyph = c + 0x17;
      } else if (c - 0xCA < 5) {
        glyph = c + 0x12;
      }
      next = s + 2;
    } else if (*next == 0xDF) {
      glyph = kana == 0 ? c - 2 : c + 0x17;
      next = s + 2;
    }
    const u32 code = (glyph + 1) & 0xFF;
    if (code < 0x80) {
      *out++ = static_cast<u8>(code);
    } else {
      *out++ = static_cast<u8>(code | 0x80);
      *out++ = static_cast<u8>(code >> 7);
    }
    s = next;
  }
}

const u8* text_find(Window*, int id) {
  const u8* table = g_text.strings.data();
  for (const u8* p = table;; p += 4) {
    const u16 key = rd16(p);
    if (key == 0xFFFF) return nullptr;
    if (key == static_cast<u32>(id)) return table + rd16(p + 2);
  }
}

int text_width(Window*, const u8* s) {
  const u8* widths = g_text.font.data() + 8;
  int width = 0;
  for (u32 c; (c = next_code(s)) != 0;) width += widths[(c - 1) & 0xFFFF];
  return width;
}

int text_glyph_offsets(Window*, const u8* s, int* xs) {
  const u8* widths = g_text.font.data() + 8;
  int x = 0, count = 0;
  for (u32 c; (c = next_code(s)) != 0;) {
    *xs++ = x;
    ++count;
    x += widths[(c - 1) & 0xFFFF];
  }
  *xs = x;
  return count;
}

void text_underline(Window* w, int x, int y, const u8* s, u32 colour) {
  const int lx = x + 8 + w->x;
  const int ly = y + 0x14 + w->y;
  const int width = text_width(w, s);
  auto* a = screen_alloc_prims<psyq::LINE_F2>(w->ctx->arena, 1);
  psyq::setlen(a, 3);
  std::memcpy(&a->r0, &colour, 3);
  a->code = 0x40;
  a->x0 = static_cast<s16>(lx);
  a->y0 = static_cast<s16>(ly);
  a->x1 = static_cast<s16>(lx + width);
  a->y1 = static_cast<s16>(ly);
  add_prim(w, a);
  auto* b = screen_alloc_prims<psyq::LINE_F2>(w->ctx->arena, 1);
  psyq::setlen(b, 3);
  b->code = 0x40;
  b->x0 = static_cast<s16>(lx + 1);
  b->y0 = static_cast<s16>(ly + 1);
  b->x1 = static_cast<s16>(lx + width + 1);
  b->y1 = static_cast<s16>(ly + 1);
  add_prim(w, b);
}

u32 text_draw(Window* w, int x, int y, const u8* s, u32 palette) {
  const u8* font = g_text.font.data();
  const int glyph_count = rd32(font), height = rd32(font + 4);
  if (g_text.line >= 0x100) return 0;
  if (palette > 0x7FFF) {
    palette &= 0x7FFF;
    u32 colour;
    std::memcpy(&colour, title_data(0x8004D578 + palette * 4), 4);
    text_underline(w, x, y, s, colour);
  }
  // Four glyph bits (most significant = leftmost pixel) to four 4-bit texels of
  // colour 1, and the same shifted one texel right as colour 2 (the shadow); the
  // shadow's fifth texel lands in bit 1 for the next group (scratchpad 0x1f800100/180).
  u16 glyph_bits[16], shadow_bits[16];
  for (u32 i = 0; i < 16; ++i) {
    const u32 v = (i & 1) << 12 | (i & 2) << 7 | (i & 4) << 2 | (i & 8) >> 3;
    glyph_bits[i] = static_cast<u16>(v);
    shadow_bits[i] = static_cast<u16>(static_cast<u16>(v << 5) | (i & 1) << 1);
  }
  int sx = x + 8 + w->x;
  const int sy = y + 8 + w->y;
  if (g_text.x & 1) ++g_text.x;
  int start = g_text.x;
  clear_column(start);
  for (u32 c; (c = next_code(s)) != 0;) {
    c = (c - 1) & 0xFFFF;
    const u8* rows = font + height * c * 2 + ((glyph_count + 3) & ~3) + 8;
    const u8 advance = font[8 + c];
    if (static_cast<u32>(g_text.x + 0x10) > 0xFD) {
      // The line is full: show what this string put on it and continue on the next.
      if (g_text.x != start) {
        const int drawn = g_text.x - start;
        const int width = (drawn + 1) & 1 ? drawn + 2 : drawn + 1;
        queue_line_sprite(w, sx, sy, start, g_text.x, height, palette, width);
        sx += drawn;
      }
      g_text.x = 0;
      g_text.line = static_cast<s16>(g_text.line + height);
      start = 0;
      if (g_text.line > 0xFF) break;
      clear_column(0);
    }
    u16 scratch[13][6] = {};  // twelve rows of 24 texels (+ the shadow's row below)
    u8* dst = g_text.pixels.data() + g_text.x / 2 + g_text.line * 0x80;
    for (int r = 0; r < 12; ++r) reinterpret_cast<u8*>(scratch[r])[0] = dst[r * 0x80];
    const int odd = g_text.x & 1;
    const int groups = (advance + odd + 3) >> 2;
    for (int r = 0; r < height; ++r) {
      u32 bits = rd16(rows + r * 2);
      if (odd) bits >>= 1;
      for (int g = 0; g < groups; ++g) {
        const u32 index = (bits >> 12) & 0xF;
        bits <<= 4;
        scratch[r][g] |= glyph_bits[index];
        const u16 shadow = shadow_bits[index];
        scratch[r + 1][g] |= shadow & 0xFFF0;
        reinterpret_cast<u8*>(&scratch[r + 1][g + 1])[0] = static_cast<u8>(shadow & 0xF);
      }
    }
    for (int r = 0; r < 12; ++r) std::memcpy(dst + r * 0x80, scratch[r], 8);
    g_text.x = static_cast<s16>(g_text.x + advance);
  }
  ++g_text.x;
  if (start != g_text.x) {
    const int drawn = g_text.x - start;
    queue_line_sprite(w, sx, sy, start, g_text.x, height, palette, drawn & 1 ? drawn + 1 : drawn);
    sx += drawn;
  }
  return static_cast<u32>((sy - w->y - 8) << 16) | static_cast<u32>((sx - w->x) - 8);
}

void text_draw_id(Window* w, int x, int y, int id, u32 palette) { text_draw(w, x, y, text_find(w, id), palette); }

void window_text_begin(Window* w) {
  auto* p = screen_alloc_prims<psyq::DR_AREA>(w->ctx->arena, 1);
  const psyq::RECT area{0, static_cast<s16>(screen_draw_y()), 0x140, 0xF0};
  psyq::SetDrawArea(p, &area);
  add_prim(w, p);
}

void window_text_end(Window* w) {
  int left = w->x + 8;
  if (left < 0) left = 0;
  int top = w->y + 4;
  if (top < 0) top = 0;
  int width = w->w - 0x10;
  if (width + w->x + 8 > 0x140) width = 0x138 - w->x;
  if (width < 0) width = 0;
  int height = w->h - 8;
  if (height + w->y + 4 > 0xF0) height = 0xEC - w->y;
  if (height < 0) height = 0;
  const psyq::RECT area{static_cast<s16>(left), static_cast<s16>(screen_draw_y() + top), static_cast<s16>(width),
                        static_cast<s16>(height)};
  auto* p = screen_alloc_prims<psyq::DR_AREA>(w->ctx->arena, 1);
  psyq::SetDrawArea(p, &area);
  add_prim(w, p);
  auto* mode = screen_alloc_prims<psyq::DR_MODE>(w->ctx->arena, 1);
  psyq::SetDrawMode(mode, 0, 0, 0x1F, nullptr);
  add_prim(w, mode);
}

}  // namespace ss::game
