#include "game/common/menu_gfx.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "game/common/menu_core.h"
#include "game/common/menu_scene.h"

namespace ss::game::menu {
namespace {

std::vector<VramRect> g_vram;   // title 0x80074bcc (count 0x80074bd4, capacity 0x80074bd0)

struct Upload {                 // 0x18 bytes
  u16 flags = 0;                // +0x00
  psyq::RECT a{};               // +0x04
  psyq::RECT b{};               // +0x0c
  const u8* ref = nullptr;      // +0x14 (flag 2 without 8: caller-owned pixels)
  std::vector<u8> owned;        // +0x14 (flag 8)
};
std::vector<Upload> g_uploads;  // title 0x80074be0 (count 0x80074bd8, capacity 0x80074bdc)

// Text colour tables (title 0x8004ecd0 + c * 0x20 / 0x8004ed90 / 0x8004ee50): for a
// 1-bpp nibble (bit 3 = leftmost pixel), four 4-bit pixels of colour c.
u16 g_tab_fg[7][16];
u16 g_tab_shadow[7][16];
u16 g_tab_mask[7][16];

const u8* g_font = nullptr;          // title 0x8004ece8
const u8* g_font_strings = nullptr;  // title 0x8004ecec

// Message expansion table for code 0x4009 (title 0x8004ef30, 16 bytes per entry) and
// its cursor (0x8004efb0).
struct Expansion {
  const u8* font = nullptr;
  const u8* strings = nullptr;
  s32 group = 0;
  s32 index = 0;
};
Expansion g_expansions[8];
int g_expansion_next = 0;

// title 0x80032eec: the three tables of colour set i + 1.
void build_colour_tables(int i, int fg, int shadow) {
  auto fill = [](u16* out, int c) {
    const u16 rep = static_cast<u16>(c | c << 4 | c << 8);
    for (int p = 0; p < 16; ++p) {
      u16 mask = (p & 8) ? 0xF : 0;
      if (p & 4) mask |= 0xF0;
      if (p & 2) mask |= 0xF00;
      u16 v = rep & mask;
      if (p & 1) v = static_cast<u16>((rep | c << 12) & (mask | 0xF000));
      out[p] = v;
    }
  };
  fill(g_tab_fg[i + 1], fg);
  fill(g_tab_shadow[i + 1], shadow);
  for (int p = 0; p < 16; ++p) {
    u16 mask = (p & 8) ? 0xF : 0;
    if (p & 4) mask |= 0xF0;
    if (p & 2) mask |= 0xF00;
    if (p & 1) mask |= 0xF000;
    g_tab_mask[i + 1][p] = static_cast<u16>(~mask);
  }
}

// ---- Sprite cell cache (title 0x80074bac: 8 blocks of 0x284 bytes) ---------------------
u8 g_clut_loaded[12];        // title 0x80074bc0
struct SharedAnim {           // title 0x80074b78 / 0x80074bb0 / 0x80074bb4 / 0x80074bb8 / 0x80074bbc
  s16 timer = -1;
  s16 frame = 0;
  u8 backwards = 0;
  u8 open = 1;
  s16 anim = 0;
} g_shared;

}  // namespace

struct SpriteCellCache {   // 0x284 bytes
  s16 x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // +0x000: region (VRAM units)
  s16 pen_x = 0, pen_y = 0;            // +0x008
  s16 cells[48][4] = {};               // +0x00c: u0, v0, u1, v1
  u32 keys[48] = {};                   // +0x18c: 0xffff = free
  u8 used[48] = {};                    // +0x24c: used this frame
  u8 row_h = 0;                        // +0x27c
  u8 next = 0;                         // +0x27d
  s32 refs = 0;                        // +0x280
};

namespace {
SpriteCellCache g_cells[8];
}

psyq::POLY_FT4* poly_ft4_from_raw(const RenderContext* ctx, const PolyFT4Raw& raw) {
  auto* p = prim_alloc<psyq::POLY_FT4>(ctx);
  if (!p) return nullptr;
  psyq::setlen(p, 9);
  p->r0 = raw.r0;
  p->g0 = raw.g0;
  p->b0 = raw.b0;
  p->code = raw.code;
  p->x0 = raw.x0, p->y0 = raw.y0, p->u0 = raw.u0, p->v0 = raw.v0, p->clut = raw.clut;
  p->x1 = raw.x1, p->y1 = raw.y1, p->u1 = raw.u1, p->v1 = raw.v1, p->tpage = raw.tpage;
  p->x2 = raw.x2, p->y2 = raw.y2, p->u2 = raw.u2, p->v2 = raw.v2, p->pad1 = raw.pad1;
  p->x3 = raw.x3, p->y3 = raw.y3, p->u3 = raw.u3, p->v3 = raw.v3, p->pad2 = raw.pad2;
  return p;
}

// ---- VRAM allocator ----------------------------------------------------------------------

namespace {
VramRect* rect_of(int handle) { return &g_vram[static_cast<size_t>(handle - 1)]; }

// title 0x8006db24: stores a region in the first unused slot (the table grows by five).
int cvram_store(VramRect r) {
  r.flags |= 1;
  for (size_t i = 0; i < g_vram.size(); ++i) {
    if (!(g_vram[i].flags & 1)) {
      g_vram[i] = r;
      return static_cast<int>(i) + 1;
    }
  }
  g_vram.push_back(r);
  return static_cast<int>(g_vram.size());
}
}  // namespace

void cvram_init() {
  g_vram.clear();
  cvram_add(0x140, 0x40, 0x3F, 0x40);
  cvram_add(0x140, 0x80, 0x3F, 0x7F);
  cvram_add(0x180, 0, 0x3F, 0xFE);
  cvram_add(0x1C0, 0xC0, 0x3F, 0x3F);
}

void cvram_release() { g_vram.clear(); }

void cvram_merge_free() {
  for (size_t i = 0; i < g_vram.size(); ++i) {
    if (g_vram[i].flags & 2) cvram_merge(static_cast<int>(i) + 1);
  }
}

int cvram_add(int x, int y, int w, int h) {
  if (w == 0 || h == 0) return -1;
  VramRect r;
  r.flags = 2;
  r.x = static_cast<s16>(x);
  r.y = static_cast<s16>(y);
  r.w = static_cast<s16>(w);
  r.h = static_cast<s16>(h);
  return static_cast<s16>(cvram_store(r));
}

int cvram_alloc(int w, int h) {
  int best = 0;
  s16 best_h = 0x400;
  for (size_t i = 0; i < g_vram.size(); ++i) {
    const VramRect& r = g_vram[i];
    if ((r.flags & 2) && static_cast<s16>(w) <= r.w && static_cast<s16>(h) <= r.h && r.h < best_h) {
      best = static_cast<int>(i) + 1;
      best_h = r.h;
    }
  }
  if (best == 0) {
    std::fprintf(stderr, "CVram::Valloc Error\n");
    return 0;
  }
  rect_of(best)->flags &= ~2;
  {
    const VramRect r = *rect_of(best);
    cvram_add(r.x, static_cast<s16>(h + r.y), r.w, static_cast<s16>(r.h - h));
  }
  {
    const VramRect r = *rect_of(best);
    cvram_add(static_cast<s16>(w + r.x), r.y, static_cast<s16>(r.w - w), static_cast<s16>(h));
  }
  rect_of(best)->w = static_cast<s16>(w);
  rect_of(best)->h = static_cast<s16>(h);
  return best;
}

int cvram_alloc4(int width_px, int h) {
  const int handle = cvram_alloc((width_px + 3) >> 2, h);
  if (handle == 0) return 0;
  VramRect* r = rect_of(handle);
  r->flags |= 4;
  r->tpage = static_cast<u16>((r->y >> 4 & 0x10) | ((r->x & 0x3FF) >> 6) | ((r->y & 0x200) << 2));
  return handle;
}

s16 cvram_x(int handle) { return handle ? rect_of(handle)->x : 0; }
s16 cvram_y(int handle) { return handle ? rect_of(handle)->y : 0; }
u16 cvram_tpage(int handle) { return handle ? rect_of(handle)->tpage : 0; }

s16 cvram_u(int handle) {
  if (!handle) return 0;
  const VramRect* r = rect_of(handle);
  s16 x = r->x;
  if (r->flags & 4) return static_cast<s16>((x % 64 + (x < 0 && x % 64 ? 64 : 0)) << 2);
  if (r->flags & 8) return static_cast<s16>((x % 128 + (x < 0 && x % 128 ? 128 : 0)) << 1);
  return x;
}

s16 cvram_v(int handle) {
  if (!handle) return 0;
  return static_cast<s16>(rect_of(handle)->y % 256);
}

void cvram_mark_free(int handle) {
  if (handle != 0) rect_of(handle)->flags |= 0x10;
}

void cvram_free_marked() {
  for (size_t i = 0; i < g_vram.size(); ++i) {
    if (g_vram[i].flags & 0x10) {
      g_vram[i].flags = static_cast<u16>((g_vram[i].flags & 0xFFEF) | 2);
      cvram_merge(static_cast<int>(i) + 1);
    }
  }
}

void cvram_merge(int handle) {
  // Horizontal neighbours of the same row and height, then vertical ones.
  for (size_t i = 0; i < g_vram.size(); ++i) {
    VramRect& e = g_vram[i];
    VramRect& r = *rect_of(handle);
    if (!(e.flags & 2) || r.y != e.y || r.h != e.h) continue;
    if (r.x + r.w == e.x) {
      e.flags &= 0xFFFC;
      r.w = static_cast<s16>(r.w + e.w);
    }
    if (e.x + e.w == r.x) {
      e.flags &= 0xFFFC;
      r.x = e.x;
      r.w = static_cast<s16>(r.w + e.w);
    }
  }
  for (size_t i = 0; i < g_vram.size(); ++i) {
    VramRect& e = g_vram[i];
    VramRect& r = *rect_of(handle);
    if (!(e.flags & 2) || r.x != e.x || r.w != e.w) continue;
    if (r.y + r.h == e.y) {
      e.flags &= 0xFFFC;
      r.h = static_cast<s16>(r.h + e.h);
    }
    if (e.y + e.h == r.y) {
      e.flags &= 0xFFFC;
      r.y = e.y;
      r.h = static_cast<s16>(r.h + e.h);
    }
  }
}

// ---- Upload queue ------------------------------------------------------------------------

void upload_queue_init() { g_uploads.clear(); }
void upload_queue_release() { g_uploads.clear(); }

namespace {
void upload_push(Upload u) {
  u.flags |= 1;
  for (auto& slot : g_uploads) {
    if (!(slot.flags & 1)) {
      slot = std::move(u);
      return;
    }
  }
  g_uploads.push_back(std::move(u));
}
}  // namespace

void upload_image(const psyq::RECT& r, std::vector<u16> pixels) {
  Upload u;
  u.flags = 10;
  u.a = r;
  u.owned.resize(pixels.size() * 2);
  std::memcpy(u.owned.data(), pixels.data(), u.owned.size());
  upload_push(std::move(u));
}

void upload_image_ref(const psyq::RECT& r, const u8* pixels) {
  Upload u;
  u.flags = 2;
  u.a = r;
  u.ref = pixels;
  upload_push(std::move(u));
}

void upload_image_split(const psyq::RECT& a, const psyq::RECT& b, std::vector<u16> pixels) {
  Upload u;
  u.flags = 12;
  u.a = a;
  u.b = b;
  u.owned.resize(pixels.size() * 2);
  std::memcpy(u.owned.data(), pixels.data(), u.owned.size());
  upload_push(std::move(u));
}

void upload_free_later(std::vector<u8> data) {
  Upload u;
  u.flags = 8;
  u.owned = std::move(data);
  upload_push(std::move(u));
}

void upload_queue_flush() {
  if (g_uploads.empty()) return;
  bool all_done = true;
  for (auto& u : g_uploads) {
    const u16 f = u.flags;
    if (!(f & 1)) continue;
    all_done = false;
    u.flags = static_cast<u16>(f & 0xFFFE);
    const u8* pixels = !u.owned.empty() ? u.owned.data() : u.ref;
    if (f & 2) psyq::LoadImage(&u.a, pixels);
    if (u.flags & 4) {
      // Each row holds a.w then b.w pixels (16-bit units).
      std::vector<u16> left(static_cast<size_t>(u.a.w) * static_cast<size_t>(u.a.h));
      std::vector<u16> right(static_cast<size_t>(u.b.w) * static_cast<size_t>(u.b.h));
      const auto* src = reinterpret_cast<const u16*>(pixels);
      for (int row = 0; row < u.a.h; ++row) {
        std::memcpy(left.data() + row * u.a.w, src, static_cast<size_t>(u.a.w) * 2);
        std::memcpy(right.data() + row * u.b.w, src + u.a.w, static_cast<size_t>(u.b.w) * 2);
        src += u.a.w + u.b.w;
      }
      psyq::LoadImage(&u.a, left.data());
      psyq::LoadImage(&u.b, right.data());
    }
    if (u.flags & 8) {
      u.owned.clear();
      u.owned.shrink_to_fit();
    }
  }
  if (all_done) g_uploads.clear();
}

// ---- Text renderer -----------------------------------------------------------------------

void TextRenderer::set_default_font(const u8* font, const u8* strings) {
  g_font = font;
  g_font_strings = strings;
}

TextRenderer::TextRenderer() {
  cvram_merge_free();
  set_font(g_font, g_font_strings);
  if (MenuScene* scene = globals().scene) ctx_ = *scene->ctx;
  for (int i = 0; i < 6; ++i) build_colour_tables(i, i * 2 + 3, 1);
  table_ = 1;
}

TextRenderer::~TextRenderer() {
  for (int i = 0; i < 50; ++i) {
    if (lines_[i]) free_line(i);
  }
  cvram_merge_free();
}

void TextRenderer::set_font(const u8* font, const u8* strings) {
  font_ = font;
  if (font) {
    widths_ = font + 8;
    glyphs_ = font + read_u32(font) + 8;
    height_ = static_cast<s16>(read_u32(font + 4));
  }
  strings_ = strings;
  group_ = 0;
}

const u8* TextRenderer::message(int index) const {
  // title 0x80032dec: skips 'index' zero-terminated messages of the current group.
  const u8* p = strings_ + read_u32(strings_ + group_ * 4);
  for (int n = index; n != 0;) {
    if (*p++ == 0) --n;
  }
  return p;
}

void TextRenderer::free_line(int index) {
  TextLine* l = lines_[index];
  cvram_mark_free(l->vram1);
  cvram_mark_free(l->vram2);
  delete l;
  lines_[index] = nullptr;
}

int TextRenderer::add_line(const u8* text) {
  const int index = alloc_line(measure(text), 0);
  render_line(index, text);
  return index;
}

int TextRenderer::alloc_line(int width, u16 flags) {
  auto* l = new TextLine;
  lines_[count_] = l;
  l->width = static_cast<s16>(width);
  l->flags = flags;
  int w = width + 1;
  int pad = w % 4;
  if (pad != 0) pad = 4 - pad;
  w += pad;
  l->height = static_cast<u16>(height_);
  l->y = y_;
  l->x = x_;
  if (w < 200) {
    l->alloc_w = static_cast<u16>(w);
    l->vram1 = static_cast<s16>(cvram_alloc4(w, height_));
    l->u1 = static_cast<u16>(cvram_u(l->vram1));
    l->v1 = static_cast<u16>(cvram_v(l->vram1));
    l->vram2 = 0;
    l->w1 = static_cast<u16>(w);
    l->x2 = 0;
    l->w2 = 0;
    l->u2 = l->v2 = 0;
  } else {
    int rest = w - 0xC5;
    if (rest < 0) rest = w - 0xC2;
    const s16 w2 = static_cast<s16>((rest >> 2) * 4);
    l->alloc_w = static_cast<u16>(w2 + 200);
    l->vram1 = static_cast<s16>(cvram_alloc4(200, height_));
    l->u1 = static_cast<u16>(cvram_u(l->vram1));
    l->v1 = static_cast<u16>(cvram_v(l->vram1));
    l->vram2 = static_cast<s16>(cvram_alloc4(w2, height_));
    l->u2 = static_cast<u16>(cvram_u(l->vram2));
    l->v2 = static_cast<u16>(cvram_v(l->vram2));
    l->w1 = 200;
    l->w2 = static_cast<u16>(w2);
    l->x2 = static_cast<s16>(x_ + 200);
  }
  y_ = static_cast<s16>(y_ + step_);
  return count_++;
}

void TextRenderer::render_line(int index, const u8* text) {
  begin_line(index);
  draw_text(lines_[current_], text);
  upload_line();
}

void TextRenderer::begin_line(int index) {
  current_ = static_cast<s16>(index);
  sub_ = 0;
  const TextLine* l = lines_[index];
  buffer_.assign(static_cast<size_t>((l->alloc_w * height_) >> 2), 0);
  cursor_ = 0;
  set_pen_x(0);
  set_tables(colour_index_);
}

void TextRenderer::set_tables(int colour) { table_ = colour; }

void TextRenderer::set_pen_x(int x) {
  pen_ = static_cast<s16>(x);
  int q = x;
  if (q < 0) q += 3;
  sub_ = static_cast<s16>(x - (q >> 2) * 4);
  cursor_ = static_cast<size_t>(q >> 2);
}

int TextRenderer::pen_x() const { return static_cast<s16>(sub_ + static_cast<int>(cursor_) * 4); }

void TextRenderer::upload_line() {
  TextLine* l = lines_[current_];
  l->clut = 0x7A51;
  auto tpage_of = [](int handle) {
    const int x = cvram_x(handle), y = cvram_y(handle);
    return static_cast<u16>((y >> 4 & 0x10) | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2));
  };
  if (l->w2 == 0) {
    const psyq::RECT r{cvram_x(l->vram1), cvram_y(l->vram1), static_cast<s16>(l->w1 >> 2), static_cast<s16>(l->height)};
    l->tpage1 = tpage_of(l->vram1);
    upload_image(r, std::move(buffer_));
  } else {
    const psyq::RECT a{cvram_x(l->vram1), cvram_y(l->vram1), static_cast<s16>(l->w1 >> 2), static_cast<s16>(l->height)};
    l->tpage1 = tpage_of(l->vram1);
    const psyq::RECT b{cvram_x(l->vram2), cvram_y(l->vram2), static_cast<s16>(l->w2 >> 2), static_cast<s16>(l->height)};
    l->tpage2 = tpage_of(l->vram2);
    upload_image_split(a, b, std::move(buffer_));
  }
  buffer_.clear();
}

void TextRenderer::draw_all(const RenderContext* ctx, int slot, s16 x, s16 y) {
  for (int i = 0; i < 50; ++i) {
    if (lines_[i] && !(lines_[i]->flags & 1)) draw_line(i, ctx, slot, x, y);
  }
  cvram_free_marked();
}

void TextRenderer::draw_line(int index, const RenderContext* ctx, int slot, s16 px, s16 py) {
  const TextLine* l = lines_[index];
  if (!l) return;
  const u32 colour = (colour_ ? *colour_ : 0x808080u) | 0x2C000000u;
  auto quad = [&](s16 x, s16 y, s16 w, s16 h, u8 u, u8 v, u16 tpage, u16 clut) {
    auto* p = prim_alloc<psyq::POLY_FT4>(ctx);
    if (!p) return;
    psyq::setlen(p, 9);
    std::memcpy(&p->r0, &colour, 4);
    p->x0 = x, p->y0 = y;
    p->x1 = static_cast<s16>(x + w), p->y1 = y;
    p->x2 = x, p->y2 = static_cast<s16>(y + h);
    p->x3 = static_cast<s16>(x + w), p->y3 = static_cast<s16>(y + h);
    p->u0 = u, p->v0 = v;
    p->u1 = static_cast<u8>(u + w), p->v1 = v;
    p->u2 = u, p->v2 = static_cast<u8>(v + h);
    p->u3 = static_cast<u8>(u + w), p->v3 = static_cast<u8>(v + h);
    p->tpage = tpage;
    p->clut = clut;
    prim_link(ctx, slot, p);
  };
  const s16 h = static_cast<s16>(l->height);
  quad(static_cast<s16>(l->x + px), static_cast<s16>(l->y + py), static_cast<s16>(l->w1), h, static_cast<u8>(l->u1),
       static_cast<u8>(l->v1), l->tpage1, l->clut);
  if (l->w2 != 0)
    quad(static_cast<s16>(l->x2 + px), static_cast<s16>(l->y + py), static_cast<s16>(l->w2), h,
         static_cast<u8>(l->u2), static_cast<u8>(l->v2), l->tpage2, l->clut);
  // The drop shadow, one pixel down and right with the shadow palette.
  quad(static_cast<s16>(l->x + px + 1), static_cast<s16>(l->y + py + 1), static_cast<s16>(l->w1), h,
       static_cast<u8>(l->u1), static_cast<u8>(l->v1), l->tpage1, 0x7A11);
  if (l->w2 != 0)
    quad(static_cast<s16>(l->x2 + px + 1), static_cast<s16>(l->y + py + 1), static_cast<s16>(l->w2), h,
         static_cast<u8>(l->u2), static_cast<u8>(l->v2), l->tpage2, 0x7A11);
  // Button icons (16x16 from the menu sheet; the session maps buttons to cells).
  const MenuSession* session = globals().session;
  for (const TextIcon& icon : l->icons) {
    const u8 cell = session ? session->button_map[icon.id] : 0;
    auto* p = prim_alloc<psyq::POLY_FT4>(ctx);
    if (!p) break;
    psyq::setlen(p, 9);
    std::memcpy(&p->r0, &colour, 4);
    const s16 x = static_cast<s16>(icon.x + l->x + px), y = static_cast<s16>(l->y + py);
    p->x0 = x, p->y0 = y;
    p->x1 = static_cast<s16>(x + 0x10), p->y1 = y;
    p->x2 = x, p->y2 = static_cast<s16>(y + 0x10);
    p->x3 = static_cast<s16>(x + 0x10), p->y3 = static_cast<s16>(y + 0x10);
    p->u0 = static_cast<u8>(cell << 4), p->v0 = 0;
    p->u1 = static_cast<u8>((cell << 4) + 0x10), p->v1 = 0;
    p->u2 = static_cast<u8>(cell << 4), p->v2 = 0x10;
    p->u3 = static_cast<u8>((cell << 4) + 0x10), p->v3 = 0x10;
    p->clut = 0x7990;
    p->tpage = 0x16;
    prim_link(ctx, slot, p);
  }
}

int TextRenderer::measure(const u8* text) {
  const int saved = g_expansion_next;
  pen_ = 0;
  draw_text(nullptr, text);
  g_expansion_next = saved;
  return pen_;
}

void TextRenderer::draw_text(TextLine* line, const u8* s) {
  for (;;) {
    u32 c = *s++;
    if (c == 0) return;
    if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*s++) << 7;
    if (c < 0x2000) {
      const int glyph = static_cast<int>((c - 1) & 0xFFFF);
      pen_ = static_cast<s16>(pen_ + widths_[glyph]);
      if (line) {
        if (pen_ >= static_cast<s16>(line->alloc_w)) return;
        draw_glyph(glyph, static_cast<s16>(line->alloc_w));
      }
      continue;
    }
    if (c == 0x4006) {
      // Switches to the default font and straight back (no visible effect).
      ++s;
      const u8* font = font_;
      const u8* strings = strings_;
      const s16 group = group_;
      set_font(g_font, g_font_strings);
      set_font(font, strings);
      set_group(group);
      continue;
    }
    if (c == 0x3000) {
      // Pen to an absolute x (the original computes b * 0x101 from one byte and skips two).
      const u8 b = *s;
      s += 2;
      pen_ = static_cast<s16>(b * 0x101);
      int q = pen_;
      if (q < 0) q += 3;
      cursor_ = static_cast<size_t>(q >> 2);
      sub_ = static_cast<s16>(pen_ - (q >> 2) * 4);
      continue;
    }
    if (c == 0x4009) {
      // A message from the expansion table, drawn in place.
      const u8* font = font_;
      const u8* strings = strings_;
      const s16 group = group_;
      const Expansion& e = g_expansions[g_expansion_next & 7];
      set_font(e.font, e.strings);
      set_group(static_cast<s16>(e.group));
      ++g_expansion_next;
      draw_text(line, message(e.index));
      set_font(font, strings);
      set_group(group);
      continue;
    }
    if (c == 0x400C) {
      const u8 id = *s++;
      if (line) line->icons.push_back(TextIcon{pen_, static_cast<s16>(id - 1)});
      pen_ = static_cast<s16>(pen_ + 0x10);
      int q = pen_;
      if (q < 0) q += 3;
      cursor_ = static_cast<size_t>(q >> 2);
      sub_ = static_cast<s16>(pen_ - (q >> 2) * 4);
      continue;
    }
    // Other control codes (0x2000-0x3fff, 0x4000-0x4005, ...) are skipped.
  }
}

void TextRenderer::draw_glyph(int glyph, int stride_px) {
  const int words = height_ < 0x10 ? 1 : 2;
  const u32 width = widths_[glyph];
  const u32 right_bits = sub_ == 0 ? 0x10 : static_cast<u32>((4 - sub_) * 4);
  const u32 left_bits = 0x10 - right_bits;
  const u16* table = g_tab_fg[table_ & 7];
  for (int pass = 0; pass < words; ++pass) {
    const int row_bytes = height_ < 0x10 ? 2 : 4;
    const auto* src = reinterpret_cast<const u8*>(glyphs_ + glyph * height_ * row_bytes);
    size_t dst = cursor_;
    if (words == 2) {
      if (pass == 0) {
        dst += 4;
      } else {
        src += 2;
      }
    }
    for (int row = 0; row < height_; ++row) {
      const u16 v = read_u16(src);
      src += words * 2;
      const u32 n3 = v >> 12, n2 = v >> 8 & 0xF, n1 = v >> 4 & 0xF, n0 = v & 0xF;
      u16* d = buffer_.data() + dst;
      if (dst + 4 < buffer_.size()) {
        d[0] = static_cast<u16>(d[0] | table[n3] << left_bits);
        d[1] = static_cast<u16>(d[1] | table[n2] << left_bits | static_cast<u32>(table[n3]) >> right_bits);
        d[2] = static_cast<u16>(d[2] | table[n1] << left_bits | static_cast<u32>(table[n2]) >> right_bits);
        d[3] = static_cast<u16>(d[3] | table[n0] << left_bits | static_cast<u32>(table[n1]) >> right_bits);
        d[4] = static_cast<u16>(d[4] | static_cast<u32>(table[n0]) >> right_bits);
      } else {
        // Near the end of the image: write what fits (the console writes past it).
        u16 w[5] = {static_cast<u16>(table[n3] << left_bits),
                    static_cast<u16>(table[n2] << left_bits | static_cast<u32>(table[n3]) >> right_bits),
                    static_cast<u16>(table[n1] << left_bits | static_cast<u32>(table[n2]) >> right_bits),
                    static_cast<u16>(table[n0] << left_bits | static_cast<u32>(table[n1]) >> right_bits),
                    static_cast<u16>(static_cast<u32>(table[n0]) >> right_bits)};
        for (int k = 0; k < 5 && dst + static_cast<size_t>(k) < buffer_.size(); ++k) d[k] = static_cast<u16>(d[k] | w[k]);
      }
      int step = stride_px;
      if (step < 0) step += 3;
      dst += static_cast<size_t>(step >> 2);
    }
  }
  u32 w = width;
  const u32 s = static_cast<u32>(sub_) + (w & 3);
  sub_ = static_cast<s16>(s);
  if ((s & 0xFFFF) > 3) {
    sub_ = static_cast<s16>(sub_ - 4);
    w += 4;
  }
  cursor_ += (w >> 1 & 0x7FFE) / 2;
}

// ---- Sprites -------------------------------------------------------------------------------

void sprite_cache_init() {
  for (auto& c : g_cells) c = SpriteCellCache{};
}

void sprite_cache_release() {}

void sprite_cache_clear() {
  for (auto& c : g_cells) c = SpriteCellCache{};
  std::memset(g_clut_loaded, 0, sizeof g_clut_loaded);
}

void sprite_cache_new_frame() {
  g_shared.timer = -1;
  g_shared.open = 1;
  for (auto& c : g_cells) std::memset(c.used, 0, sizeof c.used);
}

SpriteCellCache* sprite_cache_acquire(const s16 region[4]) {
  for (auto& c : g_cells) {
    if (c.refs != 0 && c.x0 == region[0] && c.y0 == region[1] && c.x1 == region[2] && c.y1 == region[3]) {
      ++c.refs;
      return &c;
    }
  }
  for (auto& c : g_cells) {
    if (c.refs != 0) continue;
    c.x0 = region[0];
    c.y0 = region[1];
    c.x1 = region[2];
    c.y1 = region[3];
    c.pen_x = region[0];
    c.pen_y = region[1];
    for (auto& k : c.keys) k = 0xFFFF;
    ++c.refs;
    return &c;
  }
  std::fprintf(stderr, "Cache overflow\n");
  return &g_cells[0];
}

void sprite_cache_release_region(SpriteCellCache* cache) {
  for (auto& c : g_cells) {
    if (&c == cache && c.refs > 0) {
      --c.refs;
      return;
    }
  }
}

void sprite_init(Sprite* s) {
  s->loaded_anim = -1;
  s->gfx = 1;
  s->anim = 3;
  s->pack = nullptr;
  s->cells = nullptr;
  s->timer = 0;
  s->frame = 0;
  s->backwards = 1;
  s->x = 0;
  s->y = 0;
  s->cache = nullptr;
  s->mode = 0;
}

void sprite_destroy(Sprite* s) {
  if (s->cache) sprite_cache_release_region(s->cache);
}

void sprite_sync_shared(Sprite* s) {
  if (g_shared.timer >= 0 && g_shared.anim == s->anim) {
    s->timer = g_shared.timer;
    s->frame = g_shared.frame;
    s->backwards = g_shared.backwards;
  }
}

void sprite_set(Sprite* s, const s16 v[4], s16 scale, u32 colour, s16 mode) {
  s->x = v[0];
  s->y = v[1];
  s->gfx = v[2];
  s->anim = v[3];
  if (s->loaded_anim != v[3]) s->frame = 0;
  s->loaded_anim = -1;
  s->scale = scale;
  s->colour = colour;
  s->mode = mode;
}

const u8* sprite_find_pack(u32 anim, u32 gfx) {
  for (int slot = 0; slot < 16; ++slot) {
    const std::vector<u8>* data = entry_cache_slot(slot);
    if (!data) continue;
    const u8* base = data->data();
    const u32 count = read_u32(base);
    for (u32 i = 0; i < count; ++i) {
      const u8* pack = base + read_u32(base + 4 + i * 4);
      const u32 n = read_u32(pack);
      for (u32 e = 0; e < n; ++e) {
        const u8* entry = pack + 4 + e * 28;
        if (read_u32(entry) == gfx && read_u32(entry + 4) <= anim && anim <= read_u32(entry + 8)) return pack;
      }
    }
  }
  return nullptr;
}

void sprite_step(Sprite* s) {
  u32 mapped = static_cast<u32>(s->anim);
  bool changed = false;
  if (s->loaded_anim != s->anim) {
    if (s->anim < 0x24) {
      const u8* t = table(0x80074B7C, 0);
      mapped = (t ? read_u32(t + (s->anim % 12) * 4) : 0) + static_cast<u32>(s->anim / 12) * 7;
    }
    s->pack = sprite_find_pack(mapped, static_cast<u32>(s->gfx));
    changed = true;
    s->loaded_anim = s->anim;
  }
  if (!s->pack) return;
  const u8* pack = s->pack;
  if (changed) {
    const u32 n = read_u32(pack);
    u32 i = 0, found = 0;
    for (; i < n; ++i) {
      const u8* e = pack + 4 + i * 28;
      if (read_u32(e) != static_cast<u32>(s->gfx)) continue;
      const u8* list = pack + read_u32(e + 12);
      u32 k = 0, limit = 0;
      if (list[0] != 0) {
        for (; k < list[0]; ++k)
          if (mapped == list[k + 1]) break;
        limit = list[0];
      }
      if (k != limit) {
        found = k;
        break;
      }
    }
    if (i == n) {
      std::fprintf(stderr, "GraphicsNo<%d>\nAnimNo<%d>\nPtn<%u>\nerror\n", s->gfx, s->anim, mapped);
      s->pack = nullptr;
      return;
    }
    const u8* e = pack + 4 + i * 28;
    s->frame_info = pack + read_u32(e + 20);
    s->clut_info = pack + read_u32(e + 20) + 0x14;
    const u8* cells = pack + read_u32(e + 24);
    s->cells = cells + (s->frame_info[2] == 1 ? 12 : 16);
    s->cell_rects = cells + read_u32(cells + 4);
    s->cell_data = cells + read_u32(cells);
    const u32 scripts = read_u32(e + 16);
    s->script = pack + read_u32(pack + found * 4 + scripts + 4) + scripts;
  }
  const u8* script = s->script;
  const u8* frames = script + 4;
  for (;;) {
    const u8 delay = frames[s->frame * 4 + 3];
    if (delay == 0 || s->timer <= static_cast<s16>(delay)) break;
    s->timer = static_cast<s16>(s->timer - delay);
    if (script[2] == 1) {
      s16 f = s->frame, next, limit;
      if (s->backwards == 0) {
        next = static_cast<s16>(f - 1);
        limit = static_cast<s16>(script[3] - 1);
      } else {
        next = static_cast<s16>(f + 1);
        limit = static_cast<s16>(read_u16(script));
      }
      s->frame = next;
      if (next == limit) {
        s->frame = f;
        s->backwards ^= 1;
      }
    } else {
      s->frame = static_cast<s16>(s->frame + 1);
      if (s->frame == static_cast<s16>(read_u16(script))) s->frame = script[3];
    }
  }
  s->cell = read_u16(frames + s->frame * 4);
}

namespace {

// title 0x800688d4: finds or uploads the cell image 'rect' of 'data'; returns its
// texture rectangle in u0..v1 (1 = ok, 0 = no room this frame).
int sprite_cache_cell(Sprite* s, int* u0, int* v0, int* u1, int* v1, int bpp8, const u8* rect, const u8* data) {
  SpriteCellCache* c = s->cache;
  if (!c) return 0;
  const u8* pixels = data + read_u32(rect + 8);
  const u32 key = static_cast<u32>(reinterpret_cast<uintptr_t>(pixels) & 0xFFFFFFFFu) | 0x10000u;
  for (int i = 0; i < 48; ++i) {
    if (c->keys[i] == key) {
      *u0 = c->cells[i][0];
      *v0 = c->cells[i][1];
      *u1 = c->cells[i][2];
      *v1 = c->cells[i][3];
      c->used[i] = 1;
      return 1;
    }
  }
  int slot = 0;
  while (slot < 48 && c->keys[slot] != 0xFFFF) ++slot;
  if (slot == 48) {
    slot = c->next;
    int tries = 0;
    for (; tries < 48; ++tries) {
      if (c->used[slot] == 0) break;
      slot = (slot + 1) % 48;
    }
    if (tries == 48) return 0;
  }
  c->next = static_cast<u8>((c->next + 1) % 48);
  const int h = rect[2];
  const int w_px = rect[1];
  const int w = w_px >> (2 - bpp8);
  u32 backup[48];
  std::memcpy(backup, c->keys, sizeof backup);
  for (int attempt = 0; attempt < 16; ++attempt) {
    if (c->x1 <= w + c->pen_x) {
      const u8 row = c->row_h;
      c->row_h = 0;
      c->pen_x = c->x0;
      c->pen_y = static_cast<s16>(c->pen_y + row);
    }
    if (c->y1 <= h + c->pen_y) {
      c->row_h = 0;
      c->pen_x = c->x0;
      c->pen_y = c->y0;
    }
    if (static_cast<s16>(c->y1 - c->y0) < h) return 0;
    *u0 = (c->pen_x - (c->x0 & 0xFFC0)) << (2 - bpp8);
    *v0 = c->pen_y - (c->y0 & 0xFF00);
    *u1 = *u0 + w_px;
    *v1 = *v0 + h;
    c->keys[slot] = key;
    c->cells[slot][0] = static_cast<s16>(*u0);
    c->cells[slot][1] = static_cast<s16>(*v0);
    c->cells[slot][2] = static_cast<s16>(*u1);
    c->cells[slot][3] = static_cast<s16>(*v1);
    bool collided = false;
    for (int j = 0; j < 48; ++j) {
      if (j == slot || c->keys[j] == 0xFFFF) continue;
      const s16* a = c->cells[slot];
      const s16* b = c->cells[j];
      const bool ox = (a[0] <= b[0] && b[0] < a[2]) || (a[0] < b[2] && b[2] < a[2]) || (b[0] <= a[0] && a[2] <= b[2]);
      const bool oy = (a[1] <= b[1] && b[1] < a[3]) || (a[1] < b[3] && b[3] < a[3]) || (b[1] <= a[1] && a[3] <= b[3]);
      if (!(ox && oy)) continue;
      if (c->used[j]) {
        std::memcpy(c->keys, backup, sizeof backup);
        c->keys[slot] = 0xFFFF;
        collided = true;
        break;
      }
      c->keys[j] = 0xFFFF;
    }
    if (!collided) {
      c->used[slot] = 1;
      const psyq::RECT r{c->pen_x, c->pen_y, static_cast<s16>(w), static_cast<s16>(h)};
      psyq::LoadImage(&r, pixels);
      c->pen_x = static_cast<s16>(c->pen_x + w);
      if (c->row_h < h) c->row_h = static_cast<u8>(h);
      return 1;
    }
    c->pen_x = static_cast<s16>(c->pen_x + w);
    if (c->row_h < h) c->row_h = static_cast<u8>(h);
  }
  return 0;
}

}  // namespace

void sprite_draw(Sprite* s, const RenderContext* ctx, int slot) {
  s->timer = static_cast<s16>(s->timer + 1);
  sprite_step(s);
  if (!s->pack) return;
  if (s->anim < 0xC && g_shared.open) {
    g_shared.timer = s->timer;
    g_shared.frame = s->frame;
    g_shared.backwards = s->backwards;
    g_shared.open = 0;
    g_shared.anim = s->anim;
  }
  const u8* cell = s->cells + (s->frame_info[2] == 1 ? s->cell * 4 : s->cell * 8);
  const u8* rect = s->cell_rects + cell[0] * 0xC;
  auto* p = prim_alloc<psyq::POLY_FT4>(ctx);
  if (!p) return;
  psyq::setlen(p, 9);
  const u32 code = s->colour | 0x2C000000u;
  std::memcpy(&p->r0, &code, 4);
  int u0 = 1, v0 = 1, u1 = 10, v1 = 10;
  const u16 clut_xy0 = read_u16(s->clut_info);
  const int bpp8 = clut_xy0 >> 15;
  if (sprite_cache_cell(s, &u0, &v0, &u1, &v1, bpp8, rect, s->cell_data) != 1) return;
  const u16 cy = read_u16(s->clut_info + 2);
  p->tpage = static_cast<u16>(bpp8 << 7 | (cy >> 4 & 0x10) | ((clut_xy0 & 0x3FF) >> 6) | ((cy & 0x200) << 2));
  const u16 gfx = static_cast<u16>(s->gfx);
  int clut_x = 0;
  u32 row = gfx;
  if (gfx > 0xB) {
    clut_x = 0x30;
    row = gfx % 12u;
  }
  if (gfx < 12 && !g_clut_loaded[gfx]) {
    const psyq::RECT r{static_cast<s16>(clut_x), static_cast<s16>(row + 0x1E4), 0x10, 1};
    psyq::LoadImage(&r, s->clut_info + 4);
    g_clut_loaded[gfx] = 1;
  } else if (gfx >= 12) {
    // The original indexes its 12-byte flag table past the end; reload every time.
    const psyq::RECT r{static_cast<s16>(clut_x), static_cast<s16>(row + 0x1E4), 0x10, 1};
    psyq::LoadImage(&r, s->clut_info + 4);
  }
  p->clut = static_cast<u16>((row + 0x1E4) * 0x40 | ((s->mode * 0x10 + clut_x) >> 4 & 0x3F));
  const s16 ox = static_cast<s16>(read_u16(rect + 4)), oy = static_cast<s16>(read_u16(rect + 6));
  int left = -ox;
  int du = u1 - u0;
  int right = left + du;
  int top = -oy;
  int bottom = top + (v1 - v0);
  bool flip = static_cast<u32>((s->anim % 12) - 1) < 5u;
  if (cell[3] & 1) flip = !flip;
  if (flip) {
    left = ox + static_cast<s8>(cell[1]);
    right = static_cast<s8>(cell[1]) - right;
  }
  if (cell[3] & 2) {
    top = oy + static_cast<s8>(cell[2]);
    bottom = static_cast<s8>(cell[2]) - bottom;
  }
  const int scale = static_cast<u16>(s->scale);
  p->x0 = p->x2 = static_cast<s16>((left * scale >> 12) + s->x);
  p->y0 = p->y1 = static_cast<s16>((top * scale >> 12) + s->y);
  p->x1 = p->x3 = static_cast<s16>((right * scale >> 12) + s->x);
  p->y2 = p->y3 = static_cast<s16>((bottom * scale >> 12) + s->y);
  // Texture coordinates one texel in on mirrored or scaled edges.
  int dx = p->x3 - p->x0;
  if (dx < 0) {
    dx = -dx;
    if (du < 0) du = u0 - u1;
    if (dx == du) --u0;
    --u1;
  } else {
    bool adjust = false;
    int span = p->x2 - p->x1;
    if (du < 0) {
      if (dx == u0 - u1) {
        if (span < 0) span = -span;
        if (span != u0 - u1) adjust = true;
      } else {
        adjust = true;
      }
    } else if (dx == du) {
      if (span < 0) span = -span;
      if (span != du) adjust = true;
    } else {
      adjust = true;
    }
    if (adjust) --u1;
  }
  int dy = p->y3 - p->y0;
  const int dv = v1 - v0;
  if (dy < 0 && p->y0 <= p->y1) {
    dy = -dy;
    if (dy == (dv < 0 ? -dv : dv)) --v0;
    --v1;
  } else {
    int span = p->y2 - p->y1;
    if (dy < 0) dy = -dy;
    bool adjust = false;
    if (dv < 0) {
      if (dy == -dv) {
        if (span < 0) span = -span;
        if (span != -dv) adjust = true;
      } else {
        adjust = true;
      }
    } else if (dy == dv) {
      if (span < 0) span = -span;
      if (span != dv) adjust = true;
    } else {
      adjust = true;
    }
    if (adjust) --v1;
  }
  p->u0 = static_cast<u8>(u0), p->v0 = static_cast<u8>(v0);
  p->u1 = static_cast<u8>(u1), p->v1 = static_cast<u8>(v0);
  p->u2 = static_cast<u8>(u0), p->v2 = static_cast<u8>(v1);
  p->u3 = static_cast<u8>(u1), p->v3 = static_cast<u8>(v1);
  prim_link(ctx, slot, p);
}

}  // namespace ss::game::menu
