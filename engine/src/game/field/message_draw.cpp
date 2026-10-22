#include <cstring>

#include "game/field/field.h"
#include "game/field/message.h"
#include "game/field/message_internal.h"
#include "game/field/screen.h"
#include "runtime/libgpu.h"

// Drawing of the message windows (msg_window_draw 0x8005a7bc and its clip helpers)
// and of the choice cursor (choice_draw_cursor 0x8006afd8, the script VM's wait
// state 9).
namespace ss::game::field {
namespace {

// Draw y of the buffer being built (0x80075730 / 0x80075732).
s16 draw_y() {
  Screen* s = field_screen();
  return s && s->current ? s->current->draw.clip.y : 0;
}

void add(int ot, void* prim) {
  if (ot >= 0 && ot < kOtEntries) psyq::addPrim(field_screen()->ot_entry(ot), prim);
}

void add_mode(int ot, u32 code) {
  auto* m = field_screen()->alloc_prims<psyq::DR_MODE>();
  if (!m) return;
  psyq::setlen(m, 2);
  m->code[0] = code;
  m->code[1] = 0;
  add(ot, m);
}

// Colour of a sprite drawn with the window tint, or raw.
template <typename P>
void tint_or_raw(P* p, const u8* tint) {
  p->code = static_cast<u8>(p->code & ~2);
  if (tint == nullptr) {
    p->code |= 1;
  } else {
    p->code = static_cast<u8>(p->code & ~3);
    p->r0 = tint[0];
    p->g0 = tint[1];
    p->b0 = tint[2];
  }
}

// Brightness 'level' of a fading glyph, tinted.
void fade_colour(u8* rgb, int level, const u8* tint) {
  if (tint == nullptr) {
    rgb[0] = rgb[1] = rgb[2] = static_cast<u8>(level);
  } else {
    for (int i = 0; i < 3; ++i) rgb[i] = static_cast<u8>((level * tint[i]) >> 7);
  }
}

void rect_clip_to_screen(psyq::RECT* r, int top) {  // 0x80048734
  int x = r->x;
  if (x < 0) {
    r->x = 0;
    r->w = static_cast<s16>(r->w + x);
    x = 0;
  }
  if (x + r->w > 0x140) r->w = static_cast<s16>(0x140 - x);
  const s16 t = static_cast<s16>(top);
  if (r->y < top) {
    r->h = static_cast<s16>(r->h - (t - r->y));
    r->y = t;
  }
  if (top + 0xE0 < r->y + r->h) r->h = static_cast<s16>(r->h + (t - (r->y + r->h - 0xE0)));
}

}  // namespace

int pad_button_icon_offset(u32 button) {
  switch (button) {
    case 0x01: return 0x60;  // L2
    case 0x02: return 0x70;  // R2
    case 0x04: return 0x40;  // L1
    case 0x08: return 0x50;  // R1
    case 0x10: return 0x10;  // triangle
    case 0x40: return 0x30;  // cross
    case 0x80: return 0x20;  // square
    default: return 0;       // circle (0x20) and anything else
  }
}

void msg_window_get_clip(MsgWindow* w, psyq::RECT* r) {
  if (w->custom_clip != 0) {
    *r = w->clip;
    return;
  }
  const s16 top = draw_y();
  r->x = static_cast<s16>(w->draw_x + 4);
  r->y = static_cast<s16>(w->draw_y + top + 4);
  r->w = static_cast<s16>(w->draw_w - 8);
  r->h = static_cast<s16>(w->draw_h - 8);
  rect_clip_to_screen(r, top);
}

void msg_window_set_clip(MsgWindow* w, const psyq::RECT* r) {
  w->custom_clip = 1;
  w->clip = *r;
}

void msg_window_draw(MsgWindow* w) {
  if (w->state == 10) return;
  Screen* screen = field_screen();
  if (screen == nullptr || screen->current == nullptr) return;
  FieldGlobals& g = globals();
  const u8* icons = field_sys_icons();
  const int ot = w->ot_offset + 0x810;
  MsgSystem* msg = msg_system();
  const u8* tint = msg && msg->tint_enabled != 0 ? msg->tint : nullptr;

  if (w->draw_frame != 0 && icons != nullptr) {
    // Four corners (8x8 or smaller while opening)...
    int ox = w->draw_w - 8;
    if (ox < 0) ox = 0;
    int oy = w->draw_h - 8;
    if (oy < 0) oy = 0;
    const u32* corner = reinterpret_cast<const u32*>(field_data(0x80076364));
    for (int i = 0; i < 4; ++i) {
      auto* s = screen->alloc_prims<psyq::SPRT>();
      if (!s) break;
      psyq::setSprt(s);
      s->x0 = static_cast<s16>(w->draw_x + ((i & 1) ? ox : 0));
      s->y0 = static_cast<s16>(w->draw_y + ((i & 2) ? oy : 0));
      s->w = static_cast<s16>(w->draw_w < 9 ? w->draw_w : 8);
      s->h = static_cast<s16>(w->draw_h < 9 ? w->draw_h : 8);
      if ((i == 1 || i == 3) && w->draw_h == 8) {
        s->u0 = icons[0x52];
        s->v0 = icons[0x53];
      } else {
        u32 idx;
        std::memcpy(&idx, corner + i, 4);
        s->u0 = icons[idx * 8 + 2];
        s->v0 = icons[idx * 8 + 3];
      }
      tint_or_raw(s, tint);
      s->clut = 0x7FC1;
      add(ot + 1, s);
      add_mode(ot + 1, 0xE1000216);
    }
    // ...and four edges stretched between them.
    const u32* edge = reinterpret_cast<const u32*>(field_data(0x80076374));
    for (int i = 0; i < 4; ++i) {
      u32 idx;
      std::memcpy(&idx, edge + i, 4);
      const u8* e = icons + idx * 8;
      const bool horizontal = i < 2;
      if (horizontal ? w->draw_w <= 0x10 : w->draw_h <= 0x10) continue;
      auto* p = screen->alloc_prims<psyq::POLY_FT4>();
      if (!p) break;
      psyq::setPolyFT4(p);
      p->u0 = e[2];
      p->v0 = e[3];
      p->u1 = e[4];
      p->v1 = e[3];
      p->u2 = e[2];
      p->v2 = e[5];
      p->u3 = e[4];
      p->v3 = e[5];
      if (horizontal) {
        p->u1 = static_cast<u8>(p->u1 - 1);
        p->u3 = static_cast<u8>(p->u3 - 1);
      } else {
        p->v2 = static_cast<u8>(p->v2 - 1);
        p->v3 = static_cast<u8>(p->v3 - 1);
      }
      p->code = 0x2C;
      if (tint == nullptr) {
        p->code = 0x2D;
      } else {
        p->r0 = tint[0];
        p->g0 = tint[1];
        p->b0 = tint[2];
      }
      p->code = static_cast<u8>(p->code & ~2);
      const s16 th = static_cast<s16>(e[5] - e[3]);
      const s16 tw = static_cast<s16>(e[4] - e[2]);
      s16 l = 0, r = 0, t = 0, b = 0;
      switch (i) {
        case 0:  // top
          t = w->draw_y;
          l = static_cast<s16>(w->draw_x + 8);
          r = static_cast<s16>(w->draw_x + w->draw_w - 8);
          b = static_cast<s16>(t + th);
          break;
        case 1:  // bottom
          l = static_cast<s16>(w->draw_x + 8);
          r = static_cast<s16>(w->draw_x + w->draw_w - 8);
          b = static_cast<s16>(w->draw_y + w->draw_h);
          t = static_cast<s16>(b - th);
          break;
        case 2:  // left
          l = w->draw_x;
          r = static_cast<s16>(l + tw);
          t = static_cast<s16>(w->draw_y + 8);
          b = static_cast<s16>(w->draw_y + w->draw_h - 8);
          break;
        default:  // right
          r = static_cast<s16>(w->draw_x + w->draw_w);
          l = static_cast<s16>(r - tw);
          t = static_cast<s16>(w->draw_y + 8);
          b = static_cast<s16>(w->draw_y + w->draw_h - 8);
          break;
      }
      p->tpage = 0x16;
      p->x0 = l;
      p->y0 = t;
      p->x1 = r;
      p->y1 = t;
      p->x2 = l;
      p->y2 = b;
      p->x3 = r;
      p->y3 = b;
      p->clut = 0x7FC1;
      add(ot + 1, p);
    }
  }

  if (w->draw_body != 0) {
    // The gradient body, semi-transparent (average) over the scene.
    auto* p = screen->alloc_prims<psyq::POLY_G4>();
    if (p) {
      std::memcpy(&p->r0, &w->colour[0], 3);
      std::memcpy(&p->r1, &w->colour[1], 3);
      std::memcpy(&p->r2, &w->colour[2], 3);
      std::memcpy(&p->r3, &w->colour[3], 3);
      psyq::setPolyG4(p);
      const s16 l = static_cast<s16>(w->draw_x + 3), t = static_cast<s16>(w->draw_y + 3);
      const s16 r = static_cast<s16>(w->draw_x + w->draw_w - 3), b = static_cast<s16>(w->draw_y + w->draw_h - 3);
      p->x0 = l;
      p->y0 = t;
      p->x1 = r;
      p->y1 = t;
      p->x2 = l;
      p->y2 = b;
      p->x3 = r;
      p->y3 = b;
      p->code = w->body_semi != 0 ? static_cast<u8>(p->code | 2) : static_cast<u8>(p->code & ~2);
      if (tint == nullptr) {
        p->code |= 1;
      } else {
        p->code = static_cast<u8>(p->code & ~1);
        u8* c[4] = {&p->r0, &p->r1, &p->r2, &p->r3};
        for (u8* rgb : c) {
          for (int k = 0; k < 3; ++k) {
            u32 v = (static_cast<u32>(rgb[k]) * tint[k]) >> 7;
            rgb[k] = static_cast<u8>(v > 0xFF ? 0xFF : v);
          }
        }
      }
      add(ot, p);
      add_mode(ot, 0xE1000217);
    }
  }

  if ((static_cast<u8>(w->state - 6) < 2 || w->state == 8) && w->show_prompt != 0 && icons != nullptr) {
    // The blinking button prompt in the bottom right corner.
    auto* s = screen->alloc_prims<psyq::SPRT>();
    if (s) {
      psyq::setSprt(s);
      s->x0 = static_cast<s16>(w->draw_x + w->draw_w - 0x16);
      s->w = 0x10;
      s->h = 0x10;
      s->y0 = static_cast<s16>(w->draw_y + w->draw_h - 8);
      s->u0 = static_cast<u8>(icons[0x7A] + (w->blink & 0x1C) * 4);
      s->v0 = icons[0x7B];
      tint_or_raw(s, tint);
      s->clut = 0x7FC5;
      add(ot + 2, s);
      add_mode(ot + 2, 0xE1000216);
    }
  }

  // Text: reset the draw area for the frame (drawn after the text), then each line.
  {
    auto* a = screen->alloc_prims<psyq::DR_AREA>();
    if (a) {
      const psyq::RECT full{0, draw_y(), 0x140, 0xE0};
      psyq::SetDrawArea(a, &full);
      add(ot + 1, a);
    }
  }
  int line_y = 0;
  MsgCells* cells = w->cells;
  for (int li = 0; li < w->line_count; ++li) {
    const int line = (w->first_line + li) % w->line_count;
    MsgLine& L = w->lines[line];
    if (cells != nullptr && L.shown != 0) {
      u32 solid = L.shown;  // glyphs drawn as plain sprites; the rest are still fading in
      if (w->fade_in != 0 && L.all_faded == 0) {
        for (int i = 0; i < L.shown && i < 64; ++i) {
          if (L.fade[i] < 8) {
            const u32 f = (L.fade[i] + g.frame_ticks) & 0xFF;
            L.fade[i] = static_cast<u8>(f > 8 ? 8 : f);
          }
        }
        u32 done = 0;
        while (static_cast<int>(done) < L.shown && done < 64 && L.fade[done] == 8) ++done;
        if (static_cast<int>(done) == L.shown) {
          L.all_faded = 1;
          solid = L.shown;
        } else {
          solid = done == 0 ? 0 : done - 1;
        }
      }
      const int y = w->y + line_y + w->scroll_offset + 8;
      if (L.shown != solid) {
        for (int k = 0; k < 3; ++k) {
          int run_start = 0;
          for (int j = 0; j < k; ++j) run_start += cells->run_len[line][j] * 8;
          const int run_end = run_start + cells->run_len[line][k] * 8;
          if (static_cast<int>(L.glyph_x[solid]) >= run_end) continue;
          if (static_cast<int>(L.glyph_x[L.shown]) - run_start < 1) break;
          int i0 = 0;
          if (static_cast<int>(L.glyph_x[solid]) < run_start) {
            int n = 1;
            do {
              i0 = n++;
            } while (static_cast<int>(L.glyph_x[solid + i0]) < run_start);
          }
          if (i0 != 0) --i0;
          for (int i = i0 + static_cast<int>(solid); static_cast<int>(L.glyph_x[i]) < run_end && i < L.shown; ++i) {
            auto* p = screen->alloc_prims<psyq::POLY_FT4>();
            if (!p) break;
            psyq::setPolyFT4(p);
            p->code = 0x2E;
            fade_colour(&p->r0, L.fade[i] * 0xC, tint);
            const int gx = static_cast<int>(L.glyph_x[i]) < run_start ? run_start : L.glyph_x[i];
            int gw = L.glyph_x[i + 1] - gx;
            if (run_end < static_cast<int>(L.glyph_x[i + 1])) gw = run_end - gx;
            const s16 sx = static_cast<s16>(gx + w->x + 8);
            const s16 sy = static_cast<s16>(y + L.y_offset);
            p->x0 = p->x2 = sx;
            p->y0 = p->y1 = sy;
            p->x1 = p->x3 = static_cast<s16>(sx + gw);
            p->y2 = p->y3 = static_cast<s16>(sy + w->line_height);
            const u8 u = static_cast<u8>(cells->pos[line][k][0] * 8 + gx - run_start);
            const u8 v = static_cast<u8>(cells->pos[line][k][1] * w->line_height);
            p->u0 = p->u2 = u;
            p->u1 = p->u3 = static_cast<u8>(u + gw);
            p->v0 = p->v1 = v;
            p->v2 = p->v3 = static_cast<u8>(v + w->line_height);
            p->tpage = 0x37;
            p->clut = 0x7FC2;
            add(ot + 1, p);
          }
        }
        // Ruby glyphs over the fading glyphs.
        for (int e = 0; e < L.extra_count && e < 32; ++e) {
          const MsgExtra& x = L.extra[e];
          if (x.glyph < static_cast<int>(solid)) continue;
          if (x.glyph >= L.shown) break;
          if (x.code < 0) continue;
          auto* s = screen->alloc_prims<psyq::SPRT>();
          if (!s) break;
          psyq::setSprt(s);
          s->x0 = static_cast<s16>(w->x + x.x + 8);
          s->w = 6;
          s->h = 6;
          s->y0 = static_cast<s16>(w->scroll_offset + w->y + line_y + 7);
          s->u0 = static_cast<u8>((x.code % 0x2A) * 6);
          s->code = static_cast<u8>((s->code & ~1) | 2);
          s->v0 = static_cast<u8>((x.code / 0x2A) * 6 - 0x48);
          fade_colour(&s->r0, (x.glyph < 64 ? L.fade[x.glyph] : 8) * 0xC, tint);
          s->clut = 0x7FCF;
          add(ot + 1, s);
          add_mode(ot + 1, 0xE1000236);
        }
      }
      // The revealed glyphs, one sprite per cell run.
      for (int k = 0; k < 3; ++k) {
        int start = 0;
        for (int j = 0; j < k; ++j) start += cells->run_len[line][j];
        int width = L.glyph_x[solid] - start * 8;
        if (width < 1) break;
        const int run_px = cells->run_len[line][k] << 3;
        if (run_px < width) width = run_px;
        auto* s = screen->alloc_prims<psyq::SPRT>();
        if (!s) break;
        psyq::setSprt(s);
        s->x0 = static_cast<s16>(w->x + start * 8 + 8);
        s->w = static_cast<s16>(width);
        s->y0 = static_cast<s16>(L.y_offset + w->scroll_offset + w->y + line_y + 8);
        s->h = static_cast<s16>(w->line_height);
        s->u0 = static_cast<u8>(cells->pos[line][k][0] << 3);
        s->v0 = static_cast<u8>(cells->pos[line][k][1] * w->line_height);
        tint_or_raw(s, tint);
        s->clut = 0x7FC2;
        add(ot + 1, s);
        add_mode(ot + 1, 0xE1000217);
      }
      // Icons and ruby glyphs of the revealed part.
      for (int e = 0; e < L.extra_count && e < 32 && L.extra[e].glyph <= static_cast<int>(solid) - 1; ++e) {
        const MsgExtra& x = L.extra[e];
        auto* s = screen->alloc_prims<psyq::SPRT>();
        if (!s) break;
        psyq::setSprt(s);
        if (x.code < 0) {
          const u32 idx = static_cast<u32>(~x.code);
          s->x0 = static_cast<s16>(w->x + x.x + 8);
          s->y0 = static_cast<s16>(w->scroll_offset + field_data(0x80076324)[idx] + w->y + line_y - 1);
          const u8* icon = icons ? icons + field_data(0x80076330)[idx] * 8 : nullptr;
          if (icon != nullptr) {
            if (idx < 6) {
              // A configured button: the pad icon for config button idx.
              const u8* cfg = g.config;
              const u16 button = cfg ? static_cast<u16>(cfg[idx * 2] | cfg[idx * 2 + 1] << 8) : 0;
              s->u0 = static_cast<u8>(icon[2] + pad_button_icon_offset(button));
              s->w = 0x10;
              s->h = 0x10;
              s->v0 = icon[3];
            } else {
              s->u0 = icon[2];
              s->v0 = icon[3];
              s->w = static_cast<s16>(icon[4] - icon[2]);
              s->h = static_cast<s16>(icon[5] - icon[3]);
            }
          }
          u32 clut;
          std::memcpy(&clut, field_data(0x8007633C + idx * 4), 4);
          s->clut = static_cast<u16>(clut);
          s->code = static_cast<u8>(s->code & ~1);
          if (tint == nullptr) {
            s->code |= 1;
          } else {
            s->r0 = tint[0];
            s->g0 = tint[1];
            s->b0 = tint[2];
          }
        } else {
          s->x0 = static_cast<s16>(w->x + x.x + 8);
          s->w = 6;
          s->h = 6;
          s->y0 = static_cast<s16>(w->scroll_offset + w->y + line_y + 7);
          s->u0 = static_cast<u8>((x.code % 0x2A) * 6);
          s->v0 = static_cast<u8>((x.code / 0x2A) * 6 - 0x48);
          if (tint == nullptr) {
            s->code |= 1;
          } else {
            s->code = static_cast<u8>(s->code & ~1);
            s->r0 = tint[0];
            s->g0 = tint[1];
            s->b0 = tint[2];
          }
          s->clut = 0x7FCF;
        }
        add(ot + 1, s);
        add_mode(ot + 1, 0xE1000216);
      }
    }
    line_y += L.height;
  }
  if (w->draw_callback != nullptr && w->state != 2) w->draw_callback(w->x, w->y);
  // The text is clipped to the window (this area is set before the text draws).
  auto* a = screen->alloc_prims<psyq::DR_AREA>();
  if (a) {
    psyq::RECT clip;
    msg_window_get_clip(w, &clip);
    psyq::SetDrawArea(a, &clip);
    add(ot + 1, a);
  }
}

void choice_draw_cursor(ChoiceState* c) {
  MsgWindow* w = msg_window(c->window);
  if (w == nullptr) return;
  Screen* screen = field_screen();
  if (screen == nullptr || screen->current == nullptr) return;
  const u8* icons = field_sys_icons();
  int first = 0;
  for (int i = 0; i < c->selection; ++i) first += c->lines_per_option[i];
  const s16 adjust = static_cast<s16>(w->line_height - 0xD);
  s32 cursor_x = 0, cursor_y = 0;
  const int lines = c->lines_per_option[c->selection];
  for (int j = 0; j < lines; ++j) {
    s32 xy[2] = {0, 0};
    u32 end_x = 0;
    s32 height = 0;
    msg_window_line_pos(w, first + j + c->first_line, xy, &end_x, &height);
    bool store = true;
    if (j == 0) {
      cursor_x = xy[0];
      cursor_y = xy[1];
      if (c->slide == 0) {
        if (c->cursor[0][0] != -1 && c->cursor[0][1] != xy[1]) c->slide = 4;
      }
      if (c->slide != 0) {
        // The selection moved: the underline follows after a 4-frame slide.
        const int s = c->slide;
        const int step = s < globals().frame_ticks ? s : globals().frame_ticks;
        c->slide = static_cast<s16>(s - step);
        int d = (xy[1] - c->cursor[0][1]) * (4 - c->slide);
        if (d < 0) d += 3;
        xy[1] = c->cursor[0][1] + (d >> 2);
        store = c->slide == 0;
      }
      if (store) c->option_lines_drawn = static_cast<s16>(lines);
    } else {
      store = c->slide == 0;
    }
    if (store) {
      c->line_height[j] = static_cast<s16>(height);
      c->line_width[j] = static_cast<s16>(end_x);
      c->cursor[j][0] = xy[0];
      c->cursor[j][1] = xy[1];
    }
  }
  // The underline of each line of the selection: gold, with a black line under it.
  for (int i = 0; i < c->option_lines_drawn; ++i) {
    auto* a = screen->alloc_prims<psyq::LINE_F2>();
    auto* b = screen->alloc_prims<psyq::LINE_F2>();
    if (!a || !b) break;
    psyq::setLineF2(a);
    a->r0 = 0xF0;
    a->g0 = 0xD8;
    a->b0 = 0x18;
    const s16 y = static_cast<s16>(c->line_height[i] + c->cursor[i][1] - adjust);
    a->x0 = static_cast<s16>(c->cursor[i][0] + 0xE);
    a->y0 = static_cast<s16>(y - 1);
    a->x1 = static_cast<s16>(c->line_width[i] + c->cursor[i][0]);
    a->y1 = static_cast<s16>(y - 1);
    add(0x817, a);
    psyq::setLineF2(b);
    b->r0 = b->g0 = b->b0 = 0;
    b->x0 = static_cast<s16>(c->cursor[i][0] + 0xF);
    b->y0 = y;
    b->x1 = static_cast<s16>(c->line_width[i] + c->cursor[i][0] + 1);
    b->y1 = y;
    add(0x810, b);
  }
  // The cursor sprite at the selection's first line.
  auto* s = screen->alloc_prims<psyq::SPRT>();
  if (!s || icons == nullptr) return;
  psyq::setSprt(s);
  s->x0 = static_cast<s16>(cursor_x - 2);
  s->w = 0x10;
  s->h = 0x10;
  s->y0 = static_cast<s16>(cursor_y + 3);
  s->u0 = icons[0x5A];
  const u8 v = icons[0x5B];
  s->code = static_cast<u8>((s->code & ~2) | 1);
  s->clut = 0x7FC4;
  s->v0 = v == 1 ? 0 : v;
  add(0x817, s);
  add_mode(0x817, 0xE1000216);
}

}  // namespace ss::game::field
