#include "game/battle/window.h"

#include <cstring>
#include <span>
#include <vector>

#include "game/common/program_image.h"
#include "game/common/screen.h"
#include "game/common/text_rules.h"
#include "runtime/libgpu.h"

namespace ss::game::battle {
namespace {

// The #1980 line buffer (g_text_pixels 0x8006fae0) and its cursor (0x8006fb96 / 0x8006fb98).
// The console's buffer is 0x2000 bytes (64 rows), but text_draw starts a line at any row
// up to 0x3f and writes its 12 rows (and a glyph's 8-byte group past the row end) from
// there, so the last lines of a full frame run into the heap after it. The port keeps
// that room (13 more rows); only the 64 rows are uploaded.
constexpr size_t kTextBytes = 0x2000;
std::vector<u8> g_pixels(kTextBytes + 13 * 0x80, 0);
s16 g_text_x = 0;
s16 g_text_line = 0;

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

u32 next_code(const u8*& s) {
  u32 c = *s++;
  if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*s++) << 7;
  return c;
}

// The ASCII table (0x8006e7c0, one byte per character), read from its start in the release's
// own layout: NA's and Europe's hold 0xe0 entries (the program's globals follow, zero in the
// image), France's and Germany's 0x100 (the accented letters from 0xe0). The region map
// places only NA's extent (port/tools/region_map.py TABLES), so the entries past it are indexed
// from the start, never mapped one by one.
u8 ascii_table(u32 c) {
  const std::span<const u8> table = program_run(kBattleProgramEntry, kProgramBase, 0x8006E7C0);
  return c < table.size() ? table[c] : u8{0};
}

void add_prim(BWindow* w, void* prim) { psyq::addPrim(w->ctx.ot_head, prim); }

template <typename T>
T* alloc(BWindow* w, size_t n = 1) {
  return screen_alloc_prims<T>(w->ctx.arena, n);
}

// The SPRT that shows texels [start, start + width) of the current line at (sx, sy).
void queue_line_sprite(BWindow* w, int sx, int sy, int start, int height, u32 palette, int width) {
  auto* p = alloc<psyq::SPRT>(w);
  if (p == nullptr) return;
  psyq::setlen(p, 4);
  p->code = 0x65;
  p->x0 = static_cast<s16>(sx);
  p->y0 = static_cast<s16>(sy);
  p->u0 = static_cast<u8>(start);
  p->v0 = static_cast<u8>(g_text_line + 0xC0);
  p->clut = static_cast<u16>(palette + 0x7E40);
  p->w = static_cast<s16>(width);
  p->h = static_cast<s16>(height);
  add_prim(w, p);
}

// The twelve rows of the current line at texel column x: the byte at x / 2 is zeroed
// (a shadow the previous string may have left there).
void clear_column(int x) {
  for (int row = 11; row >= 0; --row) g_pixels[static_cast<size_t>(x / 2 + (g_text_line + row) * 0x80)] = 0;
}

void queue_sprite(BWindow* w, int x, int y, u8 u, u8 v, u16 clut, int sw, int sh) {
  auto* p = alloc<psyq::SPRT>(w);
  if (p == nullptr) return;
  psyq::setlen(p, 4);
  p->code = 0x65;
  p->x0 = static_cast<s16>(x);
  p->y0 = static_cast<s16>(y);
  p->u0 = u;
  p->v0 = v;
  p->clut = clut;
  p->w = static_cast<s16>(sw);
  p->h = static_cast<s16>(sh);
  add_prim(w, p);
}

void queue_draw_mode(BWindow* w, int dfe, int dtd, int tpage) {
  auto* m = alloc<psyq::DR_MODE>(w);
  if (m == nullptr) return;
  psyq::SetDrawMode(m, dfe, dtd, tpage, nullptr);
  add_prim(w, m);
}

}  // namespace

BWindow::BWindow(int prims) {
  render_ctx_create_arena(&ctx, static_cast<size_t>(prims) * 0x28);  // (the screen manager doubles it)
  render_ctx_create_ot(&ctx, 1);
}

BWindow::~BWindow() {
  if (opened) release();
  if (ctx.arena >= 0) screen_destroy_arena(ctx.arena);
  if (ctx.ot >= 0) screen_destroy_ot(ctx.ot);
}

void BWindow::open(s16 nx, s16 ny, s16 nw, s16 nh) {
  x = nx;
  y = ny;
  w = nw;
  h = nh;
  task_register(this, 0);
  ot_layer_add(&ctx);
  opened = true;
}

void BWindow::release() {
  task_unregister(this);
  ot_layer_remove(&ctx);
  opened = false;
}

void window_save_rect(BWindow* w) {
  w->saved[0] = w->x;
  w->saved[1] = w->y;
  w->saved[2] = w->w;
  w->saved[3] = w->h;
}

void window_restore_rect(BWindow* w) {
  w->x = w->saved[0];
  w->y = w->saved[1];
  w->w = w->saved[2];
  w->h = w->saved[3];
}

void window_draw_frame(BWindow* w) {
  BattleGlobals& g = bg();
  constexpr u16 kClut = 0x7F81;
  if (auto* corners = alloc<psyq::SPRT_8>(w, 4)) {
    for (int i = 0; i < 4; ++i) {
      psyq::SPRT_8* p = &corners[i];
      psyq::setlen(p, 3);
      p->code = 0x75;
      p->x0 = static_cast<s16>((i & 1) == 0 ? w->x : w->x - 8 + w->w);
      p->y0 = static_cast<s16>(i < 2 ? w->y : w->y - 8 + w->h);
      p->clut = kClut;
      p->u0 = *battle_data(0x8006D190 + i);
      p->v0 = *battle_data(0x8006D194 + i);
      add_prim(w, p);
    }
  }
  const s16 ww = w->w;
  for (int i = 0; i < ww - 0x2F; i += 0x20)
    for (int side = 0; side < 2; ++side)
      queue_sprite(w, w->x + i + 8, side == 0 ? w->y : w->y - 8 + w->h, 8, side == 0 ? 0x30 : 0x58, kClut, 0x20, 8);
  if (const int rest = (ww - 0x10) & 0x1F)
    for (int side = 0; side < 2; ++side)
      queue_sprite(w, (w->x + w->w) - (rest + 8), side == 0 ? w->y : w->y - 8 + w->h, 8, side == 0 ? 0x30 : 0x58,
                   kClut, rest, 8);
  const s16 hh = w->h;
  for (int i = 0; i < hh - 0x2F; i += 0x20)
    for (int side = 0; side < 2; ++side)
      queue_sprite(w, side == 0 ? w->x : w->x - 8 + w->w, w->y + i + 8, side == 0 ? 0 : 0x28, 0x38, kClut, 8, 0x20);
  if (const int rest = (hh - 0x10) & 0x1F)
    for (int side = 0; side < 2; ++side)
      queue_sprite(w, side == 0 ? w->x : w->x - 8 + w->w, (w->y + w->h) - (rest + 8), side == 0 ? 0 : 0x28, 0x38,
                   kClut, 8, rest);
  queue_draw_mode(w, 0, 0, 0xF);
  w->draw_contents();
  // The translucent background in the configured window colours (config +0x30..+0x3c).
  if (auto* p = alloc<psyq::POLY_G4>(w)) {
    psyq::setPolyG4(p);
    const u8* c = g.config + 0x30;
    std::memcpy(&p->r0, c, 3);
    std::memcpy(&p->r1, c + 4, 3);
    std::memcpy(&p->r2, c + 8, 3);
    std::memcpy(&p->r3, c + 12, 3);
    p->code = 0x3A;
    const s16 left = static_cast<s16>(w->x + 3), right = static_cast<s16>(w->x + w->w - 2);
    const s16 top = static_cast<s16>(w->y + 2), bottom = static_cast<s16>(w->y + w->h - 2);
    p->x0 = left, p->y0 = top;
    p->x1 = right, p->y1 = top;
    p->x2 = left, p->y2 = bottom;
    p->x3 = right, p->y3 = bottom;
    add_prim(w, p);
  }
  queue_draw_mode(w, 0, 1, 0);
}

int window_draw_small_text(BWindow* w, int x, int y, const char* s, int palette) {
  int sy = y + 8 + w->y;
  const int sx0 = x + 8 + w->x;
  const size_t n = std::strlen(s);
  u32 xy = static_cast<u32>(sx0) + static_cast<u32>(sy) * 0x10000;
  if (n != 0) {
    auto* cells = alloc<psyq::SPRT_8>(w, n);
    if (cells == nullptr) return 0;
    for (size_t i = 0; i < n; ++i) {
      const u8 c = static_cast<u8>(s[i]);
      if (c == '\n') {
        sy += w->line_height;
        xy = static_cast<u32>(sx0) + static_cast<u32>(sy) * 0x10000;
        continue;
      }
      psyq::SPRT_8* p = &cells[i];
      psyq::setlen(p, 3);
      p->code = 0x75;
      p->x0 = static_cast<s16>(xy & 0xFFFF);
      p->y0 = static_cast<s16>(xy >> 16);
      const u8 cell = ascii_table(c);
      p->u0 = static_cast<u8>((cell & 0x1F) << 3);
      p->v0 = static_cast<u8>((cell >> 5) << 3);
      p->clut = static_cast<u16>(palette + 0x7F80);
      xy += 7;
      add_prim(w, p);
    }
  }
  queue_draw_mode(w, 0, 0, 0xF);
  return static_cast<int>((xy & 0xFFFF) - 8) - w->x;
}

void text_from_ascii(const char* in, u8* out) {
  const int high_bias = text::ascii_high_bias(text::lang(), text::Program::kBattle);
  const auto* s = reinterpret_cast<const u8*>(in);
  int kana = 0;
  for (;;) {
    u32 c = *s++;
    if (c == 0) break;
    if (c == '@') {
      kana = 1 - kana;
      continue;
    }
    u32 glyph;
    if ((kana == 0 || 0x37 < c - 0xA6) && high_bias >= 0 && c >= 0x81)
      glyph = ascii_table(c) + static_cast<u32>(high_bias);  // FR / DE (text_rules.h)
    else if (kana == 0 || 0x37 < c - 0xA6) glyph = c - 0x61 < 0x1A ? c - 0x20 : ascii_table(c);
    else glyph = ascii_table(c) + 0x38u;
    const u32 code = (glyph + 1) & 0xFF;
    if (code < 0x80) {
      *out++ = static_cast<u8>(code);
    } else {
      *out++ = static_cast<u8>(code | 0x80);
      *out++ = static_cast<u8>(code >> 7);
    }
  }
  *out = 0;
}

const u8* text_find(int id) {
  const std::vector<u8>& t = bg().text_strings;
  for (size_t p = 0; p + 4 <= t.size(); p += 4) {
    const u16 key = rd16(t.data() + p);
    if (key == 0xFFFF) return nullptr;
    if (key == static_cast<u32>(id)) return t.data() + rd16(t.data() + p + 2);
  }
  return nullptr;
}

int text_width(const u8* s) {
  if (s == nullptr) return 0;
  const u8* widths = bg().text_font.data() + 8;
  int width = 0;
  for (u32 c; (c = next_code(s)) != 0;) width += widths[(c - 1) & 0xFFFF];
  return width;
}

int text_width_id(int id) { return text_width(text_find(id)); }

void text_underline(BWindow* w, int x, int y, const u8* s, u32 colour) {
  const int lx = x + 8 + w->x;
  const int ly = y + 0x14 + w->y;
  const int width = text_width(s);
  if (auto* a = alloc<psyq::LINE_F2>(w)) {
    psyq::setlen(a, 3);
    std::memcpy(&a->r0, &colour, 3);
    a->code = 0x40;
    a->x0 = static_cast<s16>(lx);
    a->y0 = static_cast<s16>(ly);
    a->x1 = static_cast<s16>(lx + width);
    a->y1 = static_cast<s16>(ly);
    add_prim(w, a);
  }
  if (auto* b = alloc<psyq::LINE_F2>(w)) {
    psyq::setlen(b, 3);
    b->r0 = b->g0 = b->b0 = 0;
    b->code = 0x40;
    b->x0 = static_cast<s16>(lx + 1);
    b->y0 = static_cast<s16>(ly + 1);
    b->x1 = static_cast<s16>(lx + width + 1);
    b->y1 = static_cast<s16>(ly + 1);
    add_prim(w, b);
  }
}

u32 text_draw(BWindow* w, int x, int y, const u8* s, u32 palette) {
  if (s == nullptr) return 0;
  const std::vector<u8>& fontv = bg().text_font;
  const u8* font = fontv.data();
  const int glyph_count = rd32(font), height = rd32(font + 4);
  if (0x3F < g_text_line) return 0;
  if (palette > 0x7FFF) {
    palette &= 0x7FFF;
    u32 colour;
    std::memcpy(&colour, battle_data(0x8006D198 + palette * 4), 4);
    text_underline(w, x, y, s, colour);
  }
  // Four glyph bits (MSB = leftmost) to four 4-bit texels of colour 1 and the same shifted
  // one texel right as colour 2 (the shadow) (scratchpad 0x1f800100 / 0x1f800180).
  u16 glyph_bits[16], shadow_bits[16];
  for (u32 i = 0; i < 16; ++i) {
    const u32 v = (i & 1) << 12 | (i & 2) << 7 | (i & 4) << 2 | (i & 8) >> 3;
    glyph_bits[i] = static_cast<u16>(v);
    shadow_bits[i] = static_cast<u16>(static_cast<u16>(v << 5) | (i & 1) << 1);
  }
  int sx = x + 8 + w->x;
  const int sy = y + 8 + w->y;
  if (g_text_x & 1) ++g_text_x;
  int start = g_text_x;
  clear_column(start);
  for (u32 c; (c = next_code(s)) != 0;) {
    c = (c - 1) & 0xFFFF;
    const u8* rows = font + height * c * 2 + ((glyph_count + 3) & ~3) + 8;
    const u8 advance = font[8 + c];
    if (static_cast<u32>(g_text_x + 0x10) > 0xFD) {
      // The line is full: show what this string put on it and go on with the next.
      if (g_text_x != start) {
        const int drawn = g_text_x - start;
        const int width = (drawn + 1) & 1 ? drawn + 2 : drawn + 1;
        queue_line_sprite(w, sx, sy, start, height, palette, width);
        sx += drawn;
      }
      g_text_x = 0;
      g_text_line = static_cast<s16>(g_text_line + height);
      start = 0;
      if (0x3F < g_text_line) break;
      clear_column(0);
    }
    u16 scratch[13][6] = {};  // twelve rows of 24 texels (+ the shadow's row below)
    u8* dst = g_pixels.data() + g_text_x / 2 + g_text_line * 0x80;
    for (int r = 0; r < 12; ++r) reinterpret_cast<u8*>(scratch[r])[0] = dst[r * 0x80];
    const int odd = g_text_x & 1;
    const int groups = (advance + odd + 3) >> 2;
    for (int r = 0; r < height; ++r) {
      u32 bits = rd16(rows + r * 2);
      if (odd) bits >>= 1;
      for (int gi = 0; gi < groups; ++gi) {
        const u32 index = (bits >> 12) & 0xF;
        bits <<= 4;
        scratch[r][gi] |= glyph_bits[index];
        const u16 shadow = shadow_bits[index];
        scratch[r + 1][gi] |= shadow & 0xFFF0;
        reinterpret_cast<u8*>(&scratch[r + 1][gi + 1])[0] = static_cast<u8>(shadow & 0xF);
      }
    }
    for (int r = 0; r < 12; ++r) std::memcpy(dst + r * 0x80, scratch[r], 8);
    g_text_x = static_cast<s16>(g_text_x + advance);
  }
  ++g_text_x;
  if (start != g_text_x) {
    const int drawn = g_text_x - start;
    queue_line_sprite(w, sx, sy, start, height, palette, drawn & 1 ? drawn + 1 : drawn);
    sx += drawn;
  }
  return static_cast<u32>((sy - w->y - 8) << 16) | static_cast<u32>((sx - w->x - 8) & 0xFFFF);
}

u32 text_draw_id(BWindow* w, int x, int y, int id, u32 palette) { return text_draw(w, x, y, text_find(id), palette); }

void window_text_begin(BWindow* w) {
  auto* p = alloc<psyq::DR_AREA>(w);
  if (p == nullptr) return;
  const psyq::RECT area{0, static_cast<s16>(screen_draw_y()), 0x140, 0xE8};
  psyq::SetDrawArea(p, &area);
  add_prim(w, p);
}

void window_text_end(BWindow* w) {
  int left = w->x + 8;
  if (left < 0) left = 0;
  int top = w->y + 4;
  if (top < 0) top = 0;
  int width = w->w - 0x10;
  if (0x140 < width + w->x + 8) width = 0x138 - w->x;
  if (width < 0) width = 0;
  int height = w->h - 8;
  if (0xE8 < height + w->y + 4) height = 0xE4 - w->y;
  if (height < 0) height = 0;
  const psyq::RECT area{static_cast<s16>(left), static_cast<s16>(screen_draw_y() + top), static_cast<s16>(width),
                        static_cast<s16>(height)};
  if (auto* p = alloc<psyq::DR_AREA>(w)) {
    psyq::SetDrawArea(p, &area);
    add_prim(w, p);
  }
  queue_draw_mode(w, 0, 0, 0x1F);
}

void text_frame_begin() {
  if (g_text_line != 0 || g_text_x != 0) {
    g_text_x = 0;
    g_text_line = 0;
  }
}

void text_frame_upload() {
  if (g_text_x != 0 || g_text_line != 0) {
    psyq::RECT r{0x3C0, 0x1C0, 0x40, static_cast<s16>(g_text_line + 0x0C)};
    if (0x3F < r.h) r.h = 0x40;
    psyq::LoadImage(&r, g_pixels.data());
  }
}

void text_reset() {
  std::fill(g_pixels.begin(), g_pixels.end(), 0);  // (the console clears its 0x2000 bytes)
  g_text_x = 0;
  g_text_line = 0;
}

}  // namespace ss::game::battle
