#include "game/common/menu_widget.h"

#include <cstdio>
#include <cstring>

#include "game/boot/sound.h"
#include "game/common/math.h"
#include "game/common/menu_scene.h"
#include "runtime/runtime.h"

namespace ss::game::menu {
namespace {

MenuScene* scene() { return globals().scene; }
MenuSession* session() { return globals().session; }

void todo(const char* what) { runtime::Runtime::get().log_once(std::string("menu:") + what, std::string("menu: not ported yet: ") + what); }

void set_word(void* prim_colour_field, u32 v) { std::memcpy(prim_colour_field, &v, 4); }

// Line length of the next display line of a message (title 0x80032e18): bytes up to
// and including a 0x4000 code, without the terminator; -1 at the end.
int message_line_length(const u8* p0) {
  u32 c = *p0;
  if (c == 0) return -1;
  const u8* p = p0;
  for (;;) {
    ++p;
    if (c == 0) break;
    if (c > 0x7F) {
      c = (c & 0x7F) | static_cast<u32>(*p) << 7;
      ++p;
    }
    if (c == 0x4000) break;
    c = *p;
  }
  int len = static_cast<int>(p - p0);
  if (c == 0) len = static_cast<int>((p - 1) - p0);
  return len;
}

psyq::DR_AREA* area(const RenderContext* ctx, int slot, s16 x, s16 y, s16 w, s16 h) {
  auto* a = prim_alloc<psyq::DR_AREA>(ctx);
  if (!a) return nullptr;
  const psyq::RECT r{x, y, w, h};
  psyq::SetDrawArea(a, &r);
  prim_link(ctx, slot, a);
  return a;
}

void draw_mode(const RenderContext* ctx, int slot, int dfe, int dtd, int tpage) {
  auto* m = prim_alloc<psyq::DR_MODE>(ctx);
  if (!m) return;
  const psyq::RECT tw{};
  psyq::SetDrawMode(m, dfe, dtd, tpage, &tw);
  prim_link(ctx, slot, m);
}

// A POLY_FT4 of the window art (tpage 0x16) with flat brightness.
void frame_piece(const RenderContext* ctx, int slot, u32 colour, u16 clut, s16 x0, s16 y0, s16 x1, s16 y1,
                 u8 u0, u8 v0, u8 u1, u8 v1) {
  auto* p = prim_alloc<psyq::POLY_FT4>(ctx);
  if (!p) return;
  psyq::setlen(p, 9);
  set_word(&p->r0, colour | 0x2C000000u);
  p->x0 = x0, p->y0 = y0, p->x1 = x1, p->y1 = y0, p->x2 = x0, p->y2 = y1, p->x3 = x1, p->y3 = y1;
  p->u0 = u0, p->v0 = v0, p->u1 = u1, p->v1 = v0, p->u2 = u0, p->v2 = v1, p->u3 = u1, p->v3 = v1;
  p->clut = clut;
  p->tpage = 0x16;
  prim_link(ctx, slot, p);
}

}  // namespace

// ==== TextBox ===============================================================================

TextBox::TextBox() {
  slot = 5;
  reset();
  dy = 0;
  dx = 0;
  owner = static_cast<Widget*>(scene()->windows->focus());
  owner->children->add(this, 2);
  ctx = owner->ctx;
  set_colours(nullptr);
  flags = 0;
  unknown7c = 0;
  highlight = nullptr;
  flags_unused = 0;
}

TextBox::~TextBox() { clear(); }

void TextBox::close() {
  if (anim == 1) {
    anim_count = 4;
    anim = 2;
  } else {
    flags |= 0x100;
  }
}

void TextBox::on_close() { owner->children->kill(this); }

void TextBox::set_colours(const u32* c) {
  for (int i = 0; i < 4; ++i) colours[i] = c ? c[i] : read_u32(globals().config + 0x30 + i * 4);
}

void TextBox::set_flags(u16 f) {
  if (f & 0x8000) {
    flags = static_cast<u16>(flags & ~(f & 0x7FFF));
  } else {
    flags |= f;
  }
}

void TextBox::reset() {
  elements.clear();
  pen_x = pen_y = line_step = 0xC;
  delete scroll;
  scroll = nullptr;
  text = nullptr;
}

void TextBox::clear() {
  for (auto& e : elements) {
    if (e.flags & 2) {
      if (e.kind == 5) {
        auto* s = static_cast<Sprite*>(e.data);
        if (s) {
          sprite_destroy(s);
          delete s;
        }
      } else if (e.kind == 4) {
        delete static_cast<PolyFT4Raw*>(e.data);
      }
      e.data = nullptr;
    }
    if (e.kind == 2 && text) text->free_line(e.state);
  }
  delete text;
  text = nullptr;
  delete scroll;
  scroll = nullptr;
  elements.clear();
  reset();
}

void TextBox::set_rect_centered(int w, int h, int anim_mode) {
  const int x0 = 0x9C - w / 2;
  set_rect(x0, 100 - h / 2, w, h, anim_mode);
}

void TextBox::set_rect(int x0, int y0, int w, int h, int anim_mode) {
  anim = 0;
  base_x = rx = static_cast<s16>(x0);
  base_y = ry = static_cast<s16>(y0);
  base_w = rw = static_cast<s16>(w);
  base_h = rh = static_cast<s16>(h);
  visible = 1;
  font_clut = 0x7851;
  frame_clut = 0x7910;
  anim_count = 4;
  anim = static_cast<s16>(anim_mode);
  x = rx;
  y = ry;
  flags |= 4;
}

void TextBox::update() {
  if (flags & 0x100) return;
  if (visible == 0) return;
  colour = owner->colour;
  const s16 dy_screen = static_cast<s16>(screen_draw_y());
  if (anim != 0 && anim_count != 0) area(ctx, slot, 0, dy_screen, 0x138, 0xD8);
  rx = static_cast<s16>(base_x + dx);
  x = rx;
  ry = static_cast<s16>(base_y + dy);
  y = ry;
  int t = -1;
  if (anim == 1) {
    t = (base_h - 0xC) * (4 - anim_count);
  } else if (anim == 2) {
    t = anim_count * (base_h - 0xC);
  }
  if (t != -1) {
    if (t < 0) t += 3;
    rh = static_cast<s16>((t >> 2) + 0xC);
  }
  draw();
  if (anim_count != 0) {
    if (anim != 0) {
      area(ctx, slot, rx, static_cast<s16>(ry + dy_screen), rw, static_cast<s16>(rh - 4));
      area(ctx, slot - 1, rx, static_cast<s16>(ry + dy_screen), rw, rh);
    }
    const s16 was = anim_count--;
    if (was == 1 && anim == 2) flags |= 0x100;
  }
}

void TextBox::draw() {
  if (static_cast<u16>(ry + 0x1F) < 0xF7) {
    for (int i = static_cast<int>(elements.size()) - 1; i >= 0; --i) {
      BoxElement& e = elements[static_cast<size_t>(i)];
      switch (e.kind) {
        case 1:
          draw_ascii(e);
          draw_mode(ctx, slot, 0, 1, 0x16);
          break;
        case 4: draw_icon(e); break;
        case 5: draw_sprite(e); break;
        case 6: case 7: case 8: todo("text box number elements (kinds 6-8, title 0x80071810/0x80071aa0/0x80071b74)"); break;
        default: break;
      }
    }
    draw_background();
    if (text) text->draw_all(ctx, slot, rx, ry);
    if (scroll) {
      const s16 dys = static_cast<s16>(screen_draw_y());
      area(ctx, slot, 0, dys, 0x138, 0xD8);
      owner->draw_list(this);
      area(ctx, slot, 0, static_cast<s16>(scroll->v[8] + ry + dys), 0x140, static_cast<s16>(scroll->v[3] * scroll->v[6]));
      draw_scrollbar();
    }
    if (highlight) todo("text box highlight underline (title 0x8006fe0c, +0x80)");
    draw_decoration();
    if (!(flags & 0x1000)) {
      if (!(flags & 0x2000)) draw_frame();
      draw_fill();
    }
  } else {
    // Off screen: sprites keep animating.
    for (int i = static_cast<int>(elements.size()) - 1; i >= 0; --i) {
      BoxElement& e = elements[static_cast<size_t>(i)];
      if (e.kind == 5 && e.data) {
        auto* s = static_cast<Sprite*>(e.data);
        s->timer = static_cast<s16>(s->timer + 1);
        sprite_step(s);
      }
    }
  }
}

void TextBox::draw_scrollbar() { todo("list scroll bar (title 0x800703d4)"); }

void TextBox::draw_frame() {
  // Eight pieces of the window art: corners 8x8, edges stretched (title 0x800707fc).
  const int s = slot - 1;
  const s16 x0 = rx, y0 = ry, x1 = static_cast<s16>(rx + rw), y1 = static_cast<s16>(ry + rh);
  const u16 clut = frame_clut;
  // Added in the original's order; addPrim prepends, so the last is drawn first.
  frame_piece(ctx, s, colour, clut, x0, y0, static_cast<s16>(x0 + 8), static_cast<s16>(y0 + 8), 0xC0, 0x20, 0xC8, 0x28);
  frame_piece(ctx, s, colour, clut, static_cast<s16>(x0 + 8), y0, static_cast<s16>(x1 - 8), static_cast<s16>(y0 + 8), 0xC8, 0x20,
              0xCF, 0x28);
  frame_piece(ctx, s, colour, clut, static_cast<s16>(x1 - 8), y0, x1, static_cast<s16>(y0 + 8), 0xD0, 0x20, 0xD8, 0x28);
  frame_piece(ctx, s, colour, clut, x0, static_cast<s16>(y0 + 8), static_cast<s16>(x0 + 8), static_cast<s16>(y1 - 8), 0xC0, 0x28,
              0xC8, 0x2F);
  frame_piece(ctx, s, colour, clut, static_cast<s16>(x1 - 8), static_cast<s16>(y0 + 8), x1, static_cast<s16>(y1 - 8), 0xD0, 0x28,
              0xD8, 0x2F);
  frame_piece(ctx, s, colour, clut, x0, static_cast<s16>(y1 - 8), static_cast<s16>(x0 + 8), y1, 0xC0, 0x30, 0xC8, 0x38);
  frame_piece(ctx, s, colour, clut, static_cast<s16>(x0 + 8), static_cast<s16>(y1 - 8), static_cast<s16>(x1 - 8), y1, 0xC8, 0x30,
              0xCF, 0x38);
  frame_piece(ctx, s, colour, clut, static_cast<s16>(x1 - 8), static_cast<s16>(y1 - 8), x1, y1, 0xD0, 0x30, 0xD8, 0x38);
}

void TextBox::draw_fill() {
  // A gouraud quad with the four corner colours at half intensity (title 0x800710c8).
  auto* g = prim_alloc<psyq::POLY_G4>(ctx);
  if (!g) return;
  psyq::setlen(g, 8);
  const u32 b = colour;
  set_word(&g->r0, menu_colour_code(0x38000000u, b, colours[0] >> 1 & 0x7F7F7F7F));
  set_word(&g->r1, menu_colour(b, colours[1] >> 1 & 0x7F7F7F7F));
  set_word(&g->r2, menu_colour(b, colours[2] >> 1 & 0x7F7F7F7F));
  set_word(&g->r3, menu_colour(b, colours[3] >> 1 & 0x7F7F7F7F));
  g->x0 = static_cast<s16>(rx + 3), g->y0 = static_cast<s16>(ry + 3);
  g->x1 = static_cast<s16>(rx + rw - 3), g->y1 = static_cast<s16>(ry + 3);
  g->x2 = static_cast<s16>(rx + 3), g->y2 = static_cast<s16>(ry + rh - 3);
  g->x3 = static_cast<s16>(rx + rw - 3), g->y3 = static_cast<s16>(ry + rh - 3);
  prim_link(ctx, slot - 1, g);
  draw_mode(ctx, slot - 1, 0, 1, 0x16);
}

int TextBox::add_element(BoxElement e) {
  elements.push_back(std::move(e));
  return static_cast<int>(elements.size()) - 1;
}

int TextBox::add_string(const char* text_) {
  const s16 zero[2] = {0, 0};
  return add_string_at(zero, text_);
}

int TextBox::add_string_at(const s16 offset[2], const char* s) {
  BoxElement e;
  e.kind = 1;
  e.flags = 6;
  const size_t n = std::strlen(s) + 1;
  e.owned.assign(reinterpret_cast<const u8*>(s), reinterpret_cast<const u8*>(s) + n);
  e.x = static_cast<s16>(offset[0] + pen_x);
  e.y = static_cast<s16>(offset[1] + pen_y);
  e.line_step = line_step;
  add_element(std::move(e));
  pen_y = static_cast<s16>(pen_y + line_step);
  return static_cast<int>(elements.size()) - 1;
}

TextRenderer* TextBox::text_renderer() {
  if (!text) {
    text = new TextRenderer;
    text->set_owner_colour(&owner->colour);
  }
  return text;
}

void TextBox::set_group(s16 group) { text_renderer()->set_group(group); }

void TextBox::set_font_group(const u8* font, const u8* strings, s16 group) {
  text_renderer()->set_font(font, strings);
  text->set_group(group);
}

int TextBox::add_message(s16 index) { return add_text_line(text_renderer()->message(index)); }

void TextBox::add_message_lines(int index, int colour_index) {
  TextRenderer* r = text_renderer();
  const u8* p = r->message(index);
  r->set_colour(static_cast<s16>(colour_index));
  bool first = true;
  for (;;) {
    const int n = message_line_length(p);
    if (n == -1) break;
    u8 line[0x100];
    std::memcpy(line, p, static_cast<size_t>(n));
    line[n] = 0;
    add_text_line(line);
    if (first) {
      r->set_colour(1);
      first = false;
    }
    p += n;
  }
}

int TextBox::add_text_line(const u8* t) {
  TextRenderer* r = text_renderer();
  r->set_pen(pen_x, pen_y, 0xC);
  BoxElement e;
  e.kind = 2;
  e.flags = 4;
  e.state = static_cast<u16>(r->add_line(t));
  e.x = pen_x;
  e.y = pen_y;
  add_element(std::move(e));
  pen_y = static_cast<s16>(pen_y + line_step);
  return static_cast<int>(elements.size()) - 1;
}

int TextBox::add_sprite(BoxElement& e) {
  auto* s = new Sprite;
  sprite_init(s);
  const s16 region[4] = {0x1C1, 1, 0x1FF, 0xC0};
  s->cache = sprite_cache_acquire(region);
  e.kind = 5;
  e.flags = 6;
  e.data = s;
  add_element(std::move(e));
  return static_cast<int>(elements.size()) - 1;
}

int TextBox::add_character(s16 id, s16 cflags, s16 anim_, s16 scale) {
  BoxElement e;
  e.state = static_cast<u16>(scale);
  e.x = static_cast<s16>(pen_x + 0x10);
  e.y = static_cast<s16>(pen_y + 0x2A);
  e.a = id;
  e.b = anim_;
  e.c = static_cast<u16>(cflags);
  return add_sprite(e);
}

int TextBox::add_party_character(int character, s16 anim_, s16 scale) {
  u8 status = 0;
  for (int i = 0; i < 8; ++i) {
    const u8* slotp = globals().party + i * 0x60;
    if (static_cast<s16>(read_u16(slotp)) == character) status = slotp[2];
  }
  return add_character(static_cast<s16>(character), status, anim_, scale);
}

int TextBox::add_icon(const PolyFT4Raw& poly) { return add_icon_anim(poly, 0, 0); }

int TextBox::add_icon_anim(const PolyFT4Raw& poly, s16 frames, s16 delay) {
  BoxElement e;
  e.kind = 4;
  e.flags = 6;
  e.data = new PolyFT4Raw(poly);
  e.x = pen_x;
  e.y = pen_y;
  e.state = 0;
  e.a = frames;
  e.b = delay;
  add_element(std::move(e));
  pen_y = static_cast<s16>(pen_y + line_step);
  return static_cast<int>(elements.size()) - 1;
}

void TextBox::draw_ascii(BoxElement& e) {
  const auto* s = e.owned.data();
  int step = e.line_step;
  e.flags &= 0xFFFB;
  u16 px = static_cast<u16>(rx + e.x);
  int py = static_cast<u16>(ry) + static_cast<u16>(e.y);
  int alternate = 0;
  for (;;) {
    u32 c = *s++;
    if (c == 0) return;
    if (c == '@') {
      alternate = 1 - alternate;
      continue;
    }
    if (c < 0x41) {
      if (c == '\n') {
        py += step;
        px = static_cast<u16>(rx + e.x);
        continue;
      }
      if (c == 0xB) {
        step = *s++;
        continue;
      }
      if (c == 8) {
        font_clut = static_cast<u16>((*s++ + 0x1E0) * 0x40 | 0x11);
        continue;
      }
    }
    auto* sp = prim_alloc<psyq::SPRT>(ctx);
    if (!sp) return;
    psyq::setlen(sp, 4);
    set_word(&sp->r0, colour | 0x64000000u);
    sp->x0 = static_cast<s16>(px);
    sp->y0 = static_cast<s16>(py);
    sp->w = 8;
    sp->h = 8;
    u32 g;
    if (c < 0xA0) {
      g = c - 0x20;
    } else if (c < 0xA6 || (c < 0xDE && alternate == 0)) {
      g = c - 99;
    } else if (c >= 0xDE && c < 0xE0) {
      // Voiced marks overlap the previous character.
      g = c - 0x2B;
      px = static_cast<u16>(px - 8);
      sp->x0 = static_cast<s16>(sp->x0 - 7);
      sp->y0 = static_cast<s16>(sp->y0 - 7);
    } else {
      g = c - 0x2B;
    }
    px = static_cast<u16>(px + 8);
    sp->u0 = static_cast<u8>((g & 0x1F) << 3);
    sp->v0 = static_cast<u8>(((g & 0xFF) >> 5) * 8 + 200);
    sp->clut = font_clut;
    prim_link(ctx, slot, sp);
  }
}

void TextBox::draw_icon(BoxElement& e) {
  const auto* raw = static_cast<const PolyFT4Raw*>(e.data);
  PolyFT4Raw q = *raw;
  if (e.a != 0) {
    u32 frame = e.state & 0xFF;
    int counter = (e.state >> 8) + 1;
    u16 hi = static_cast<u16>(counter << 8);
    if (e.b <= counter) {
      ++frame;
      if (static_cast<int>(e.a) <= static_cast<int>(frame)) frame = 0;
      hi = 0;
    }
    e.state = static_cast<u16>(hi | frame);
    const u8 shift = static_cast<u8>(frame * (q.v2 - q.v0));
    q.v0 = static_cast<u8>(q.v0 + shift);
    q.v1 = static_cast<u8>(q.v1 + shift);
    q.v2 = static_cast<u8>(q.v2 + shift);
    q.v3 = static_cast<u8>(q.v3 + shift);
  }
  const s16 ox = static_cast<s16>(rx + e.x), oy = static_cast<s16>(ry + e.y);
  q.x0 = static_cast<s16>(q.x0 + ox), q.y0 = static_cast<s16>(q.y0 + oy);
  q.x1 = static_cast<s16>(q.x1 + ox), q.y1 = static_cast<s16>(q.y1 + oy);
  q.x2 = static_cast<s16>(q.x2 + ox), q.y2 = static_cast<s16>(q.y2 + oy);
  q.x3 = static_cast<s16>(q.x3 + ox), q.y3 = static_cast<s16>(q.y3 + oy);
  const u32 code = colour | 0x2C000000u;
  std::memcpy(&q.r0, &code, 4);
  if (auto* p = poly_ft4_from_raw(ctx, q)) prim_link(ctx, slot, p);
}

void TextBox::draw_sprite(BoxElement& e) {
  auto* s = static_cast<Sprite*>(e.data);
  if (!s) return;
  s16 v[4] = {static_cast<s16>(rx + e.x), static_cast<s16>(ry + e.y), 0, e.b};
  s16 mode = 0;
  if (e.a == 0) {
    v[2] = 0x7FFF;
  } else {
    v[2] = static_cast<s16>(e.a - 1);
    if (e.c & 2) {
      if (v[3] < 0xC) v[3] = static_cast<s16>(v[3] + 0xC);
      frame_piece(ctx, slot, colour, 0x79D0, v[0], static_cast<s16>(v[1] - 0x2C), static_cast<s16>(v[0] + 8),
                  static_cast<s16>(v[1] - 0x1C), 0xE0, 0x10, 0xE8, 0x20);
    }
    if (e.c & 8) mode = 2;
    if (e.c & 4) {
      if (v[3] < 0xC) v[3] = static_cast<s16>(v[3] + 0xC);
      mode = 1;
    }
    if (e.c & 1) v[3] = 0xFF;
  }
  sprite_set(s, v, static_cast<s16>(e.state), colour, mode);
  sprite_sync_shared(s);
  sprite_draw(s, ctx, slot);
}

void box_gradient(TextBox* box, u32 c0, u32 c1, u32 c2, u32 c3, s16 x, s16 y, s16 w, s16 h, int slot) {
  auto* g = prim_alloc<psyq::POLY_G4>(box->ctx);
  if (!g) return;
  psyq::setlen(g, 8);
  set_word(&g->r0, c0 | 0x38000000u);
  set_word(&g->r1, c1);
  set_word(&g->r2, c2);
  set_word(&g->r3, c3);
  g->x0 = x, g->y0 = y;
  g->x1 = static_cast<s16>(x + w), g->y1 = y;
  g->x2 = x, g->y2 = static_cast<s16>(y + h);
  g->x3 = static_cast<s16>(x + w), g->y3 = static_cast<s16>(y + h);
  prim_link(box->ctx, slot, g);
}

void HeaderBox::draw_decoration() {
  const s16 half = static_cast<s16>(static_cast<s16>(rw) / 2);
  box_gradient(this, 0x3F0000, 0x7F0000, 0x3F0000, 0x7F0000, rx, ry, half, static_cast<s16>(rh - 2), slot);
  box_gradient(this, 0x7F0000, 0x8F8F8F, 0x7F0000, 0x8F8F8F, static_cast<s16>(rx + half), ry, half,
               static_cast<s16>(rh - 2), slot);
  box_gradient(this, 0x1F0000, 0x3F0000, 0x1F0000, 0x3F0000, rx, static_cast<s16>(ry + rh - 2), half, 2, slot);
  box_gradient(this, 0x3F0000, 0x474747, 0x3F0000, 0x474747, static_cast<s16>(rx + half), static_cast<s16>(ry + rh - 2),
               half, 2, slot);
}

TitledBox::TitledBox() { flags |= 0x1000; }

void TitledBox::draw_decoration() {
  // The top edge sits 16 pixels down; a red gradient strip fills the top.
  const int s = slot;
  const s16 x0 = rx, y0 = ry, x1 = static_cast<s16>(rx + rw);
  frame_piece(ctx, s, colour, frame_clut, x0, static_cast<s16>(y0 + 0x10), static_cast<s16>(x0 + 8), static_cast<s16>(y0 + 0x18),
              0xC0, 0x38, 0xC8, 0x40);
  frame_piece(ctx, s, colour, frame_clut, static_cast<s16>(x0 + 8), static_cast<s16>(y0 + 0x10), static_cast<s16>(x1 - 8),
              static_cast<s16>(y0 + 0x18), 0xC8, 0x38, 0xCF, 0x40);
  frame_piece(ctx, s, colour, frame_clut, static_cast<s16>(x1 - 8), static_cast<s16>(y0 + 0x10), x1, static_cast<s16>(y0 + 0x18),
              0xD0, 0x38, 0xD8, 0x40);
  draw_frame();
  const s16 half = static_cast<s16>(static_cast<s16>(rw) / 2);
  box_gradient(this, 0x3F, 0x7F, 0x3F, 0x7F, static_cast<s16>(rx + 3), static_cast<s16>(ry + 3), static_cast<s16>(half - 3), 0x10,
               slot - 1);
  box_gradient(this, 0x7F, 0x8F8F8F, 0x7F, 0x8F8F8F, static_cast<s16>(rx + half), static_cast<s16>(ry + 3),
               static_cast<s16>(half - 3), 0x10, slot - 1);
  draw_fill();
}

MessageBox::MessageBox() {
  set_rect_centered(0, 0, 1);
  pen_reset();
  text_renderer();
}

void MessageBox::show(int index) { message_box_show(this, index); }

void message_box_show(TextBox* box, int index) {
  TextRenderer* r = box->text_renderer();
  const u8* p = r->message(index);
  TextRenderer::set_expansion_cursor(0);
  int width = 0, height = 0;
  for (;;) {
    const int n = message_line_length(p);
    if (n == -1) break;
    u8 line[0x100];
    std::memcpy(line, p, static_cast<size_t>(n));
    line[n] = 0;
    const int w = r->measure(line);
    if (width < w) width = w;
    box->add_text_line(line);
    p += n;
    height += box->line_step;
  }
  width += 0x20;
  height += 0xE;
  box->base_x = box->rx = static_cast<s16>(0x9C - width / 2);
  box->base_y = box->ry = static_cast<s16>(0x6C - height / 2);
  box->base_w = box->rw = static_cast<s16>(width);
  box->base_h = box->rh = static_cast<s16>(height);
  box->x = box->rx;
  box->y = box->ry;
  box->pen_at(static_cast<s16>(width), static_cast<s16>(height));
  PolyFT4Raw arrow{};
  arrow.x0 = -0x10, arrow.y0 = -8;
  arrow.x1 = 0, arrow.y1 = -8;
  arrow.x2 = -0x10, arrow.y2 = 8;
  arrow.x3 = 0, arrow.y3 = 8;
  arrow.u0 = 0xE8, arrow.v0 = 0;
  arrow.u1 = 0xF8, arrow.v1 = 0;
  arrow.u2 = 0xE8, arrow.v2 = 0x10;
  arrow.u3 = 0xF8, arrow.v3 = 0x10;
  arrow.clut = 0x7A10;
  arrow.tpage = 0x16;
  box->add_icon_anim(arrow, 8, 4);
}

// ==== Cursor ==================================================================================

void CursorGroup::begin() {
  if (value) index = *value;
}

void CursorGroup::end() {
  if (value) *value = index;
}

void CursorGroup::update(u32 p) {
  Cursor* c = cursor;
  pressed = p;
  flags = c->flags;
  const s16 was_moving = c->anim;
  begin();
  const s16 old = index;
  if (was_moving == 0) input();
  if (old != index || (flags & 4)) {
    flags &= 0xFFFB;
    place();
  }
  end();
  if (was_moving == 0 && c->anim == 0) {
    if (state & 2) {
      state &= 0xFFFC;
      flags |= 0x800;
    }
    if (state & 1) state = static_cast<u16>((state & 0xFFFE) | 2);
  }
  c->flags = flags;
}

void GridGroup::begin() {
  scroll = box ? box->scroll : nullptr;
  CursorGroup::begin();
}

void GridGroup::input() {
  const u32 p = pressed;
  const u8* config = globals().config;
  if (p & 0x1000) {
    move(-cols);
  } else if (p & 0x4000) {
    move(cols);
  } else if (p & 0x8000) {
    if (cols != 1) move(-1);
  } else if (p & 0x2000) {
    if (cols != 1) move(1);
  } else if (mode & 2) {
    if (p & read_u16(config + 10)) {
      flags |= 0x50;
    } else if (p & read_u16(config + 8)) {
      flags |= 0x60;
    }
  }
}

void GridGroup::move(int delta) {
  const s16 total = static_cast<s16>(cols * rows);
  s16 step = static_cast<s16>(delta);
  const s16 n = static_cast<s16>(index + delta);
  s16 next = n;
  if (n < 0) {
    next = static_cast<s16>(n + total);
    if (!(mode & 1)) {
      s16 add;
      if (!(mode & 2) || !scroll || scroll->v[0] < 1) {
        add = static_cast<s16>(-step);
        flags |= 0x100;
      } else {
        flags |= 0x20;
        add = cols;
      }
      next = static_cast<s16>(add + n);
    }
  }
  if (total <= next) {
    if (!(mode & 1)) {
      if (!(mode & 2) || !scroll || limit <= scroll->v[0]) {
        flags |= 0x200;
      } else {
        flags |= 0x10;
        step = cols;
      }
      next = static_cast<s16>(next - step);
    } else {
      next = static_cast<s16>(next - total);
    }
  }
  if (index != next) index = next;
}

void GridGroup::place() {
  const s16 px = static_cast<s16>(x + (index % cols) * dx);
  const s16 py = static_cast<s16>(y + (index / cols) * dy);
  cursor->move_to(px, py);
  if (flags & 0x1000) {
    flags &= 0xEFFF;
    cursor->jump_to(px, py);
  }
  state |= 1;
}

void ListGroup::begin() {
  CursorGroup::begin();
  scroll_pos = *scroll_var;
  int q = scroll_pos;
  if (q < 0) q += 3;
  index = static_cast<s16>(index - (q >> 2));
}

void ListGroup::end() {
  Cursor* c = cursor;
  s16 moving;
  if (edge & 4) {
    edge &= 0xFFFB;
    if ((edge & 2) && scroll_pos < max_scroll) {
      c->sound = 1;
      c->anim = 1;
    }
    if ((edge & 1) && scroll_pos > 0) {
      c->sound = 1;
      c->anim = 1;
    }
    moving = c->anim;
  } else {
    moving = c->anim;
    if (moving == 0) edge &= 0xFFFC;
  }
  if (moving != 0) {
    if (edge & 2) {
      ++scroll_pos;
      if (max_scroll <= scroll_pos) scroll_pos = max_scroll;
    }
    if (edge & 1) {
      --scroll_pos;
      if (scroll_pos < 1) scroll_pos = 0;
    }
  }
  int q = scroll_pos;
  if (q < 0) q += 3;
  index = static_cast<s16>(index + (q >> 2));
  if (value) *value = index;
  *scroll_var = scroll_pos;
}

void ListGroup::input() {
  if (pressed & 0x1000) {
    --index;
    if (index < 0) {
      index = 0;
      edge |= 5;
    }
  } else if (pressed & 0x4000) {
    ++index;
    if (count <= index) {
      index = static_cast<s16>(count - 1);
      edge |= 6;
    }
  }
}

void ListGroup::place() {
  const s16 py = static_cast<s16>(y + index * dy);
  cursor->move_to(x, py);
  if (flags & 0x1000) {
    flags &= 0xEFFF;
    cursor->jump_to(x, py);
  }
}

Cursor::Cursor() {
  owner = static_cast<Widget*>(scene()->windows->focus());
  owner->children->add(this, 2);
  ctx = owner->ctx;
}

Cursor::~Cursor() {
  for (auto*& g : groups) {
    delete g;
    g = nullptr;
  }
}

void Cursor::on_close() { owner->children->kill(this); }

void Cursor::set_flags(u16 f) {
  if (f & 0x8000) {
    flags = static_cast<u16>(flags & ~(f & 0x7FFF));
  } else {
    flags |= f;
  }
}

bool Cursor::take_flags(u16 f) {
  const bool set = (flags & f) != 0;
  if (set) set_flags(static_cast<u16>(f | 0x8000));
  return set;
}

void Cursor::update() {
  const u16 f = flags;
  flags = static_cast<u16>(f & 0xFFFD);
  if (!(f & 1)) return;
  u32 input = 0;
  if (owner->cursor == this) input = owner->repeat;
  if (CursorGroup* g = groups[active]) g->update(input);
  if (anim != 0) {
    flags |= 2;
    int ddx = anim * (to_x - from_x);
    if (ddx < 0) ddx += 3;
    int ddy = anim * (to_y - from_y);
    if (ddy < 0) ddy += 3;
    x = static_cast<s16>(from_x + (ddx >> 2));
    y = static_cast<s16>(from_y + (ddy >> 2));
    if (++anim > 4) {
      anim = 0;
      from_x = to_x;
      from_y = to_y;
    }
  }
  for (int i = 15; i > 0; --i) {
    trail_x[i] = trail_x[i - 1];
    trail_y[i] = trail_y[i - 1];
  }
  trail_x[0] = x;
  trail_y[0] = y;
  draw();
  const s16 before = active;
  if (flags & 0x100) {
    flags &= 0xFEFF;
    prev_group();
  }
  if (flags & 0x200) {
    flags &= 0xFDFF;
    next_group();
  }
  if (before != active) sound = 1;
  if (sound_delay != 0) {
    --sound_delay;
    sound = 0;
  }
  if (sound != 0) {
    owner->sound = sound;
    sound_delay = 1;
    sound = 0;
  }
}

void Cursor::draw() {
  draw_mode(ctx, 8, 0, 1, 0x16);
  clut = 0x79D0;
  const bool focused = owner == scene()->windows->focus() && owner->cursor == this;
  draw_hand(x, y, 0x1D, focused ? 0xFFFFFFFFu : (owner->colour | 0x02000000u));
  clut = 0x7990;
  draw_mode(ctx, 8, 0, 1, 0x16);
}

void Cursor::draw_hand(s16 hx, s16 hy, int cell, u32 colour) {
  if (colour == 0xFFFFFFFFu) colour = owner->colour;
  auto* sp = prim_alloc<psyq::SPRT_16>(ctx);
  if (!sp) return;
  psyq::setlen(sp, 3);
  set_word(&sp->r0, colour | 0x7C000000u);
  sp->x0 = hx;
  sp->y0 = hy;
  sp->u0 = static_cast<u8>((cell % 16) << 4);
  sp->v0 = static_cast<u8>((cell / 16) << 4);
  sp->clut = clut;
  prim_link(ctx, 8, sp);
}

void Cursor::prev_group() {
  for (s16 g = static_cast<s16>(active - 1); g >= 0; --g) {
    if (groups[g]) {
      active = g;
      flags |= 4;
      return;
    }
  }
}

void Cursor::next_group() {
  for (s16 g = static_cast<s16>(active + 1); g < 8; ++g) {
    if (groups[g]) {
      active = g;
      flags |= 4;
      return;
    }
  }
}

void Cursor::add_grid(s16* value, u16 mode, s16 cols, s16 rows, s16 gx, s16 gy, s16 gdx, s16 gdy, TextBox* box) {
  if (mode & 2) todo("scrolling cursor grid (title 0x8006a710)");
  auto* g = new GridGroup;
  for (auto& slotp : groups) {
    if (!slotp) {
      slotp = g;
      break;
    }
  }
  g->value = value;
  g->mode = mode;
  g->cols = cols;
  g->rows = rows;
  if (box) {
    gx = static_cast<s16>(gx + box->x);
    gy = static_cast<s16>(gy + box->y);
  }
  g->x = gx;
  g->y = gy;
  g->dx = gdx;
  g->dy = gdy;
  g->box = box;
  attach(g);
  flags |= 0x1080;
}

void Cursor::add_list(s16* value, s16* scroll_var, s16 count, s16 visible, s16 lx, s16 ly, s16 ldy) {
  auto* g = new ListGroup;
  for (auto& slotp : groups) {
    if (!slotp) {
      slotp = g;
      break;
    }
  }
  g->value = value;
  g->scroll_var = scroll_var;
  const s16 extra = static_cast<s16>(count - visible);
  if (extra < 0) {
    g->max_scroll = 0;
    g->count = static_cast<s16>(visible + extra);
  } else {
    g->max_scroll = static_cast<s16>(extra * 4);
    g->count = visible;
  }
  g->x = lx;
  g->y = ly;
  g->dy = ldy;
  attach(g);
  flags |= 0x1000;
}

void Cursor::move_to(s16 tx, s16 ty) {
  to_x = tx;
  to_y = ty;
  anim = 1;
  sound = 1;
}

void Cursor::jump_to(s16 fx, s16 fy) {
  from_x = fx;
  from_y = fy;
}

void Cursor::attach(CursorGroup* g) {
  g->cursor = this;
  sound_delay = 1;
  x = from_x = to_x;
  y = from_y = to_y;
  flags |= 1;
}

// ==== Widget ====================================================================================

Widget::Widget() {
  MenuScene* s = scene();
  parent = static_cast<Widget*>(s->windows->focus());
  s->windows->add(this, 2);
  pending = -1;
  closing = 0;
  ctx = new RenderContext;
  children = new WindowList;
  cursor = new Cursor;
  ctx->arena = s->ctx->arena;
  render_ctx_create_ot(ctx, 0xF);
  colour = 0x01808080;
}

Widget::~Widget() {
  // A parent may still look at this child's result (the original reads freed memory).
  if (MenuScene* s = scene(); s && s->windows) {
    for (auto& e : s->windows->entries) {
      if (e.flags & 8) continue;  // already deleted
      auto* w = dynamic_cast<Widget*>(e.task);
      if (w && w != this && w->child == this) {
        w->child = nullptr;
        w->child_state_cache = result_state;
        w->child_result_cache = result;
      }
    }
  }
  screen_destroy_ot(ctx->ot);
  delete ctx;
  delete children;
}

void Widget::on_close() {
  MenuScene* s = scene();
  for (size_t i = 0; i < s->windows->entries.size(); ++i) {
    const WindowEntry& e = s->windows->entries[i];
    if (!(e.flags & 1)) continue;
    if (!(e.task->type() & 0x10)) continue;
    auto* w = static_cast<Widget*>(e.task);
    if (w->parent != nullptr && w->parent == this) {
      w->on_close();
      std::fprintf(stderr, "pwnd->destroy\n");
    }
  }
  s->windows->kill(this);
}

void Widget::draw() {
  MenuScene* s = scene();
  depth = static_cast<u16>(s->windows->depth_of(this));
  ctx->ot_head = screen_clear_ot(ctx->ot);
  if (closing) {
    if (pending != -1) {
      set_result(pending);
      pending = -1;
    }
    flags |= 8;
  }
  frame();
  psyq::P_TAG* own = ctx->ot_head;
  psyq::P_TAG* host = s->ctx->ot_head + depth;
  own[14].next = host->next;
  host->next = own;
}

void Widget::draw_list(TextBox*) { todo("list widgets (title 0x80073adc)"); }

void Widget::frame() {
  if (child_id != -1) {
    u16 state = 0;
    s16 res = 0;
    if (child) {
      state = child->result_state;
      res = child->result;
    } else {
      state = child_state_cache;
      res = child_result_cache;
    }
    if (static_cast<u16>(state - 2) < 2) {
      on_child_done(child_id, state, res);
      child_id = -1;
    }
  }
  pressed = 0;
  repeat = 0;
  if (scene()->windows->focus() == this) input();
  children->update();
  if (sound != 0) {
    sound_play_sfx(menu_sound(), sound, 0, 0x7F40);
    sound = 0;
  }
}

void Widget::input() {
  MenuScene* s = scene();
  if (MenuSession* ss = session()) ss->help_flags = help_flags;
  if (flags & 10) return;
  repeat = s->repeat;
  pressed = s->pressed;
  if (input_delay != 0) {
    --input_delay;
    return;
  }
  const u32 held = s->held;
  const u32 r = repeat;
  const u32 p = pressed;
  if (!cursor || (cursor->flags & 2)) return;
  const int n = cursor->active + 1;
  if (cursor->take_flags(0x800)) on_cursor(n);
  const u8* config = globals().config;
  auto button = [config](int i) { return static_cast<u32>(read_u16(config + i * 2)); };
  if (p & button(0)) {
    sound = 7;
    on_confirm(n);
  } else if (p & button(1)) {
    sound = 7;
    on_cancel(n);
  } else if (p & button(2)) {
    sound = 7;
    on_button2(n);
  } else if (p & button(3)) {
    sound = 7;
    on_button3(n);
  } else {
    if (held != 0) on_held(held);
    if (p != 0) on_pressed(p);
    if (r & 0x8000) {
      input_delay = 4;
      sound = 7;
      on_left_right(1);
    } else if (r & 0x2000) {
      input_delay = 4;
      sound = 7;
      on_left_right(2);
    } else if (r & button(6)) {
      sound = 7;
      input_delay = 4;
      on_shoulder(1);
    } else if (r & button(7)) {
      sound = 7;
      input_delay = 4;
      on_shoulder(2);
    }
  }
}

void Widget::on_confirm(int) { sound = 2; }

void Widget::on_cancel(int n) {
  sound = 6;
  if (static_cast<s16>(n) >= 0) cancel();
}

void Widget::on_button2(int n) { sound = static_cast<s16>(n) < 0 ? 2 : 0; }
void Widget::on_button3(int n) { sound = static_cast<s16>(n) < 0 ? 0x1C : 0; }
void Widget::on_left_right(int n) { sound = n < 0 ? 1 : 0; }
void Widget::on_shoulder(int n) { sound = n < 0 ? 1 : 0; }

void Widget::finish(s16 code) {
  if (closing) return;
  pending = code;
  closing = 1;
  set_result(pending);
  pending = -1;
}

void Widget::cancel() {
  if (closing) return;
  pending = -2;
  closing = 1;
  set_result(pending);
  pending = -1;
}

void Widget::set_result(s16 code) {
  if (code == -2) {
    result_state = 2;
  } else {
    result_state = 3;
    result = code;
  }
  close();
}

// ==== Dialog ======================================================================================

Dialog::Dialog() {
  opening = 1;
  open_delay = 4;
  closing_anim = 0;
  flags |= 0x108;
}

void Dialog::close() {
  closing_anim = 1;
  if (flags & 0x100) scene()->windows->set_unfocusable(this);
  close_delay = 6;
  if (parent != nullptr) {
    children->close_type(0x101);
    children->close_type(0x102);
  }
}

void Dialog::frame() {
  if (opening != 0) {
    if (open_delay == 0) {
      opening = 0;
      flags &= 0xFFF7;
    } else {
      --open_delay;
    }
  }
  if (closing_anim != 0) {
    if (close_delay == 0) {
      closing_anim = 0;
      Widget::close();
      // The widget is dying; its children still draw this frame.
    } else {
      --close_delay;
    }
  }
  Widget::frame();
}

void ConfirmDialog::on_confirm(int n) {
  Widget::on_confirm(-n);
  finish(0);
}

void dialog_show_message(Dialog*, s16 message, int arg) {
  auto* box = new MessageBox;
  box->set_group(message);
  box->show(arg);
}

// ==== Panel =========================================================================================

Panel::Panel() {
  fade_count = 0x10;
  fade = 1;
  colour = 0x01808080;
  unknown92 = 0;
  member_box = nullptr;
  info = nullptr;
  unknown54 = 0;
  help = nullptr;
  unknown50 = 0;
  unknown84 = 0;
  unknown58 = 0;
  member_count = 0;
  arrow_frame = 0;
  flags |= 8;
}

Panel::~Panel() = default;

void Panel::close() {
  fade_count = 0x10;
  fade = -1;
}

void Panel::frame() {
  help_anim();
  if (fade != 0) fade_step();
  Widget::frame();
}

void Panel::on_button3(int n) {
  if (unknown58 != 0 && info != nullptr) {
    ++unknown54;
    const bool wrap = session()->unknown1f == 0 ? unknown54 >= 2 : unknown54 >= 3;
    if (wrap) unknown54 = 0;
    unknown84 = 0;
    n = -n;
  }
  Widget::on_button3(n);
}

void Panel::fade_step() {
  int level;
  if (fade == 1) {
    if (fade_count == 0) {
      fade = 0;
      flags &= 0xFFF7;
    } else {
      --fade_count;
    }
    level = static_cast<s16>(0x10 - fade_count);
  } else if (fade == -1) {
    if (fade_count == 0) {
      fade = 0;
      Widget::close();
    } else {
      --fade_count;
    }
    level = fade_count;
  } else {
    return;
  }
  darken(level);
}

void Panel::darken(int level) {
  if (static_cast<u16>(level) > 0xF) level = 0x10;
  const u8 c = static_cast<u8>(-1 - (level * 0xFF >> 4));
  auto* t = prim_alloc<psyq::TILE>(ctx);
  if (!t) return;
  psyq::setlen(t, 3);
  t->r0 = t->g0 = t->b0 = c;
  t->code = 0x62;
  t->x0 = 0;
  t->y0 = 0;
  t->w = 0x138;
  t->h = 0xD8;
  prim_link(ctx, 10, t);
  draw_mode(ctx, 10, 0, 0, 0x56);
}

void Panel::create_help() {
  auto* h = new HeaderBox;
  help = h;
  h->set_rect(0, 200, 0x138, 0x10, 0);
  h->set_flags(0x2000);
  logo_state = 0;
  logo_phase = 0;
  shimmer = 0;
  help_rebuild();
}

void Panel::help_anim() {
  int speed = 0x1000;
  if ((logo_state == 1 || logo_state == 3) && logo_phase == 0) speed = 0x22;
  help_rebuild();
  const u16 hf = session()->help_flags;
  if (logo_state == 1 && logo_phase == 0 && !(hf & 4) && !(hf & 2)) {
    shimmer = 0;
    return;
  }
  const int s = static_cast<u16>(shimmer) + speed;
  shimmer = static_cast<s16>(s);
  if (static_cast<s16>(s) > 0xFFF) {
    shimmer = 0;
    logo_phase = static_cast<s16>(logo_phase + 0x80);
    if (logo_phase > 0xFFF) {
      logo_phase = 0;
      if (++logo_state > 3) logo_state = 0;
    }
  }
}

void Panel::help_rebuild() {
  TextBox* tb = help;
  if (!tb) return;
  tb->clear();
  tb->set_group(2);
  int s2 = 0;
  if (logo_state == 0 || logo_state == 2) s2 = logo_phase;
  if (logo_state == 1 || logo_state == 3) s2 = 0x1000 - logo_phase;
  if (s2 < 0) s2 += 3;
  int sn = sin4096(static_cast<u32>(s2 >> 2));
  const s16 hi = static_cast<s16>(static_cast<u32>(sn) >> 8);
  if (sn < 0) sn += 0xFF;
  const int h8 = sn >> 8;
  const int sign = 0;  // the original subtracts the sign of sin (always >= 0 here)
  const u16 hf = session()->help_flags;
  const s16 half = static_cast<s16>((h8 - sign) >> 1);
  const s16 top = static_cast<s16>(8 - half);
  const s16 bottom = static_cast<s16>(hi + 8 - half);
  if (logo_state >= 0 && logo_state < 2) {
    // The game logo, opening vertically.
    tb->pen_at(0xA4, 0);
    PolyFT4Raw q{};
    q.x0 = 0, q.y0 = top, q.x1 = 0x90, q.y1 = top, q.x2 = 0, q.y2 = bottom, q.x3 = 0x90, q.y3 = bottom;
    q.u0 = 0, q.v0 = 0x70, q.u1 = 0x90, q.v1 = 0x70, q.u2 = 0, q.v2 = 0x80, q.u3 = 0x90, q.v3 = 0x80;
    q.clut = 0x78D0;
    q.tpage = 0x16;
    tb->add_icon(q);
  } else if (logo_state < 4) {
    // Button hints for the shoulder buttons (help flags 2 / 4).
    int base = 0;
    MenuSession* ss = session();
    auto icon16 = [&](s16 px, u8 cell_u) {
      PolyFT4Raw q{};
      q.x0 = 0, q.y0 = top, q.x1 = 0x10, q.y1 = top, q.x2 = 0, q.y2 = bottom, q.x3 = 0x10, q.y3 = bottom;
      q.u0 = cell_u, q.v0 = 0, q.u1 = static_cast<u8>(cell_u + 0x10), q.v1 = 0;
      q.u2 = cell_u, q.v2 = 0x10, q.u3 = static_cast<u8>(cell_u + 0x10), q.v3 = 0x10;
      q.clut = 0x7990;
      q.tpage = 0x16;
      tb->pen_at(px, 0);
      tb->add_icon(q);
    };
    auto label = [&](s16 px, u8 u0, u8 u1, u8 v0, u8 v1, s16 w) {
      PolyFT4Raw q{};
      q.x0 = 0, q.y0 = top, q.x1 = w, q.y1 = top, q.x2 = 0, q.y2 = bottom, q.x3 = w, q.y3 = bottom;
      q.u0 = u0, q.v0 = v0, q.u1 = u1, q.v1 = v0, q.u2 = u0, q.v2 = v1, q.u3 = u1, q.v3 = v1;
      q.clut = 0x7B90;
      q.tpage = 0x16;
      tb->pen_at(px, 0);
      tb->add_icon(q);
    };
    if (hf & 2) {
      icon16(0xA4, static_cast<u8>(ss->button_map[2] << 4));
      u8 u0 = 0, u1 = 0x38;
      if (hf & 0x100) u0 = 0x70, u1 = 0xA0;
      if (hf & 0x200) u0 = 0xA0, u1 = 0xD0;
      label(0xB4, u0, u1, 0x60, 0x70, 0x38);
      base = 0x4A;
    }
    if (hf & 4) {
      icon16(static_cast<s16>(base | 0xA4), static_cast<u8>(ss->button_map[3] << 4));
      u8 u0 = 0x38, u1 = 0x70, v0 = 0x60, v1 = 0x70;
      if (hf & 0x400) u0 = 0, u1 = 0x38, v0 = 0x48, v1 = 0x58;
      label(static_cast<s16>(base | 0xB4), u0, u1, v0, v1, 0x40);
    }
  }
  tb->pen_at(0x10, 1);
  draw_help();
  if ((hf & 1) && member_count > 1) {
    // Shoulder-button arrows for switching party members.
    int x0 = 4, x1 = 0x4C;
    s16 py = static_cast<s16>(-0xC2);
    if (hf & 0x1000) {
      x0 = 8;
      x1 = 0x20;
      py = static_cast<s16>(-0x7A);
    }
    if (arrow_frame > 0xB) arrow_frame = 0;
    int q = arrow_frame;
    if (q < 0) q += 3;
    const u8 vbase = static_cast<u8>((q >> 2) * 0x10);
    ++arrow_frame;
    MenuSession* ss = session();
    auto quad = [&](s16 px, s16 w, u8 u0, u8 u1, u8 v0, u8 v1, u16 clut) {
      PolyFT4Raw r{};
      r.x0 = 0, r.y0 = 0, r.x1 = w, r.y1 = 0, r.x2 = 0, r.y2 = 0x10, r.x3 = w, r.y3 = 0x10;
      r.u0 = u0, r.v0 = v0, r.u1 = u1, r.v1 = v0, r.u2 = u0, r.v2 = v1, r.u3 = u1, r.v3 = v1;
      r.clut = clut;
      r.tpage = 0x16;
      tb->pen_at(px, py);
      tb->add_icon(r);
    };
    quad(static_cast<s16>(x0), 8, 0xD8, 0xE0, static_cast<u8>(vbase + 0x30), static_cast<u8>(vbase + 0x40), 0x7850);
    quad(static_cast<s16>(x0 + 8), 0x10, static_cast<u8>(ss->button_map[6] << 4), static_cast<u8>((ss->button_map[6] << 4) + 0x10),
         0, 0x10, 0x7990);
    quad(static_cast<s16>(x1), 0x10, static_cast<u8>(ss->button_map[7] << 4), static_cast<u8>((ss->button_map[7] << 4) + 0x10), 0,
         0x10, 0x7990);
    quad(static_cast<s16>(x1 | 0x10), 8, 0xE0, 0xE8, static_cast<u8>(vbase + 0x30), static_cast<u8>(vbase + 0x40), 0x7850);
  }
}

void Panel::draw_help() { help->add_message(session()->page_id); }

void Panel::on_member_changed() { draw_member(); }

void Panel::draw_member() {
  TextBox* box = member_box;
  if (!box) return;
  box->clear();
  box->pen_at(8, 0x1E);
  box->add_party_character(static_cast<s16>(read_u16(globals().selected_slot)), 3, 0x1000);
  box->pen(0x2C, 0x2C, 0xC);
  box->add_string(reinterpret_cast<const char*>(globals().selected_record + 0x24));
}

void Panel::build_members() {
  const int selected = party_get_selected();
  member_count = 0;
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    if (party_slot_active() == 0) continue;
    bool ok = true;
    if (!(help_flags & 0x1000)) ok = (globals().selected_slot[2] & 7) == 0;
    if (!ok) continue;
    if (i == selected) member = member_count;
    members[member_count++] = static_cast<s16>(i);
  }
  party_select(selected);
}

void Panel::on_shoulder(int n) {
  u16 enabled = 0;
  if (member_count > 1) enabled = help_flags & 1;
  if (enabled) {
    if (n == 1) {
      if (--member < 0) member = static_cast<s16>(member_count - 1);
    } else {
      if (member_count <= ++member) member = 0;
    }
    party_select(members[member]);
    on_member_changed();
    n = -n;
  }
  Widget::on_shoulder(n);
}

}  // namespace ss::game::menu
