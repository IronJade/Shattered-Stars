#include "game/field/text_panel.h"

#include <cstring>

#include "game/field/field.h"
#include "game/field/screen.h"
#include "runtime/libgpu.h"

namespace ss::game::field {
namespace {

void add(int ot, void* prim) { psyq::addPrim(field_screen()->ot_entry(ot), prim); }

void add_mode(int ot, u32 code) {
  auto* m = field_screen()->alloc_prims<psyq::DR_MODE>();
  if (!m) return;
  psyq::setlen(m, 2);
  m->code[0] = code;
  m->code[1] = 0;
  add(ot, m);
}

void set_quad(psyq::POLY_FT4* p, int l, int t, int r, int b) {
  p->x0 = static_cast<s16>(l);
  p->y0 = static_cast<s16>(t);
  p->x1 = static_cast<s16>(r);
  p->y1 = static_cast<s16>(t);
  p->x2 = static_cast<s16>(l);
  p->y2 = static_cast<s16>(b);
  p->x3 = static_cast<s16>(r);
  p->y3 = static_cast<s16>(b);
}

}  // namespace

void window_set_rect(FieldWindow* w, s16 x, s16 y, s16 width, s16 height) {
  w->x = x;
  w->y = y;
  w->w = width;
  w->h = height;
}

void window_draw_frame(FieldWindow* w) {
  if (w->visible == 0 || w->frame_visible == 0) return;
  Screen* s = field_screen();
  if (s == nullptr || s->current == nullptr) return;
  // Corners: 8x8 tiles of the skin strip at (0xa0 + 8i, 0xf0).
  for (int i = 0; i < 4; ++i) {
    auto* c = s->alloc_prims<psyq::SPRT_8>();
    if (!c) return;
    psyq::setlen(c, 3);
    c->code = 0x75;
    c->x0 = static_cast<s16>((i & 1) == 0 ? w->x : w->x - 8 + w->w);
    c->y0 = static_cast<s16>(i < 2 ? w->y : w->y - 8 + w->h);
    c->v0 = 0xF0;
    c->u0 = static_cast<u8>(0xA0 + 8 * i);
    c->clut = 0x7F8F;
    add(0x818, c);
    add_mode(0x818, 0xE1000216);
  }
  // Edges.
  for (int i = 0; i < 4; ++i) {
    auto* p = s->alloc_prims<psyq::POLY_FT4>();
    if (!p) return;
    const u8 u = static_cast<u8>(0xC0 + 8 * i);
    p->u0 = u;
    p->v0 = 0xF0;
    p->v1 = 0xF0;
    p->u2 = u;
    if (i < 2) {
      p->u1 = static_cast<u8>(0xC7 + 8 * i);
      p->v2 = 0xF8;
      p->u3 = static_cast<u8>(0xC7 + 8 * i);
      p->v3 = 0xF8;
    } else {
      p->u1 = static_cast<u8>(0xC8 + 8 * i);
      p->v2 = 0xF7;
      p->u3 = static_cast<u8>(0xC8 + 8 * i);
      p->v3 = 0xF7;
    }
    psyq::setlen(p, 9);
    p->code = 0x2D;
    switch (i) {
      case 0: set_quad(p, w->x + 8, w->y, w->x + w->w - 8, w->y + 8); break;
      case 1: set_quad(p, w->x + 8, w->y + w->h - 8, w->x + w->w - 8, w->y + w->h); break;
      case 2: set_quad(p, w->x, w->y + 8, w->x + 8, w->y + w->h - 8); break;
      default: set_quad(p, w->x + w->w - 8, w->y + 8, w->x + w->w, w->y + w->h - 8); break;
    }
    p->tpage = 0x16;
    p->clut = 0x7F8F;
    add(0x818, p);
  }
  // Body: the configured gradient, semi-transparent.
  auto* g = s->alloc_prims<psyq::POLY_G4>();
  if (!g) return;
  const u8* cfg = globals().config;
  std::memcpy(&g->r0, cfg + 0x30, 3);
  std::memcpy(&g->r1, cfg + 0x34, 3);
  std::memcpy(&g->r2, cfg + 0x38, 3);
  std::memcpy(&g->r3, cfg + 0x3C, 3);
  psyq::setlen(g, 8);
  g->code = 0x3B;
  g->x0 = static_cast<s16>(w->x + 3);
  g->y0 = static_cast<s16>(w->y + 3);
  g->x1 = static_cast<s16>(w->x + w->w - 3);
  g->y1 = static_cast<s16>(w->y + 3);
  g->x2 = static_cast<s16>(w->x + 3);
  g->y2 = static_cast<s16>(w->y + w->h - 3);
  g->x3 = static_cast<s16>(w->x + w->w - 3);
  g->y3 = static_cast<s16>(w->y + w->h - 3);
  add(0x818, g);
  add_mode(0x818, 0xE1000216);
}

int window_draw_ascii(FieldWindow* w, int x, int y, const char* str, int palette) {
  if (w->visible == 0) return x + y * 0x10000;
  Screen* s = field_screen();
  const int sx = x + 8 + w->x;
  int sy = y + 8 + w->y;
  u32 pos = static_cast<u32>(sx + sy * 0x10000);
  int kana = 0;
  const u8* table = field_data(0x8007470C);
  const u8* kana_table = field_data(0x80074744);
  for (const u8* p = reinterpret_cast<const u8*>(str); *p != 0; ++p) {
    u32 c = *p;
    auto* sp = s->alloc_prims<psyq::SPRT_8>();
    if (!sp) break;
    psyq::setlen(sp, 3);
    sp->code = 0x75;
    if (c == '@') {
      kana = 1 - kana;
      continue;
    }
    if (c == '\n') {
      sy += w->line_height;
      pos = static_cast<u32>(sx + sy * 0x10000);
      continue;
    }
    u32 xy;
    if (c == 0xDE || c == 0xDF) {
      xy = pos - 0x70008;  // voiced marks sit over the previous kana
      c += 0x38;
    } else {
      if (c > 0xDF) c += 0x38;
      xy = pos;
      pos += 8;
    }
    std::memcpy(&sp->x0, &xy, 4);
    const u8 b = (kana == 0 || c - 0xA6 > 0x37) ? table[c] : kana_table[c];
    sp->u0 = static_cast<u8>((b & 0x1F) << 3);
    sp->v0 = static_cast<u8>(((b >> 5) + 0x19) * 8);
    sp->clut = static_cast<u16>(palette + 0x7F80);
    sp->code = static_cast<u8>(sp->code & ~2);
    add(0x819, sp);
  }
  add_mode(0x819, 0xE1000216);
  return static_cast<int>((pos & 0xFFFF) - 8) - w->x;
}

TextPanel::TextPanel() {
  frame_visible = 1;
  line_height = 0xC;
  visible = 1;
  text_panel_clear(this);
}

TextPanel::~TextPanel() { text_panel_destruct(this); }

void TextPanel::update() { text_panel_draw(this); }

void text_panel_open(TextPanel* p, int x, int y, int w, int h) {
  task_unregister(p);
  task_register(p, 4);
  window_set_rect(p, static_cast<s16>(x), static_cast<s16>(y), static_cast<s16>(w), static_cast<s16>(h));
}

void text_panel_destruct(TextPanel* p) { task_unregister(p); }

void text_panel_draw(TextPanel* p) {
  p->w = static_cast<s16>(p->max_width + 0x20);
  p->h = static_cast<s16>(p->text_y + 0x10);
  u8* cfg = globals().config;
  u8 saved[16];
  std::memcpy(saved, cfg + 0x30, 16);
  const u32 panel_colours[4] = {0x400000, 0x200000, 0x200000, 0};
  std::memcpy(cfg + 0x30, panel_colours, 16);
  window_draw_frame(p);
  std::memcpy(cfg + 0x30, saved, 16);
  p->text_y = 0;
  p->text_x = 0;
  p->max_width = 0x10;
}

void text_panel_print(TextPanel* p, const char* text, int palette) {
  if (p->visible == 0 || p->text_y > 0xC0) return;
  const int end = window_draw_ascii(p, p->text_x, p->text_y, text, palette);
  if (end == p->text_x) {
    p->text_x = 0;
    p->text_y += 8;
  } else {
    p->text_x = end;
  }
  if (p->max_width < end) p->max_width = end;
}

void text_panel_clear(TextPanel* p) {
  p->text_x = 0;
  p->text_y = 0;
  p->max_width = 0x10;
}

}  // namespace ss::game::field
