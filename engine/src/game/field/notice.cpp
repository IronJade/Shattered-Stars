#include "game/field/notice.h"

#include <cstring>

#include "game/field/field.h"
#include "game/field/message.h"
#include "game/field/message_internal.h"
#include "game/field/screen.h"
#include "runtime/libgpu.h"

namespace ss::game::field {
namespace {

Notice* g_notice = nullptr;  // 0x800752c0

void add(int ot, void* prim) { psyq::addPrim(field_screen()->ot_entry(ot), prim); }

void add_mode(int ot, u32 code) {
  auto* m = field_screen()->alloc_prims<psyq::DR_MODE>();
  if (!m) return;
  psyq::setlen(m, 2);
  m->code[0] = code;
  m->code[1] = 0;
  add(ot, m);
}

s16 draw_y() {
  Screen* s = field_screen();
  return s && s->current ? s->current->draw.clip.y : 0;
}

}  // namespace

Notice* g_notice_object() { return g_notice; }

Notice* notice_create(Notice* self) {
  self->state = 0;
  task_unregister(self);
  task_register(self, 0);
  g_notice = self;
  return self;
}

Notice::~Notice() { notice_destroy(this); }

void notice_destroy(Notice* self) {
  task_unregister(self);
  if (g_notice == self) g_notice = nullptr;
}

void Notice::update() { notice_update(this); }

void notice_show(Notice* n, const u8* text, int icon, int window, int at_top) {
  n->close_request = 0;
  n->window = window;
  n->state = 1;
  n->icon = icon;
  n->timer = 0;
  n->effect = 0;
  n->at_top = at_top;
  msg_window_destroy(msg_system(), window);
  n->text_width = 0;
  n->text = text;
  // Measure the text in the map font.
  const u8* p = text;
  for (;;) {
    u32 c = *p;
    if (c == 0) break;
    ++p;
    if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*p++) << 7;
    if (c < 0x4000) {
      n->text_width += 1 + msg_glyph_width(c);
      continue;
    }
    switch (c) {
      case 0x4003:
      case 0x4004:
      case 0x4005:
      case 0x4007:
      case 0x4008:
        ++p;
        break;
      case 0x4006:
        p += 2;
        break;
      case 0x400A:
        for (;;) {
          u32 b = *p;
          if (b == 0) goto measured;
          ++p;
          if (b - 0xA6 > 0x39) b = (b & 0x7F) | static_cast<u32>(*p++) << 7;
          if (b == 0x400B) break;
          if (b == 0x400C) ++p;
        }
        break;
      case 0x400C:
        n->text_width += text_char_name_width(*p, nullptr);
        ++p;
        break;
      default:
        break;
    }
  }
measured:
  if (n->at_top == 0) {
    n->x = 0x122 - n->text_width;
    if (n->icon == 0) return;
    n->x = 0x112 - n->text_width;
  } else {
    n->x = 8;
  }
}

void notice_dismiss(Notice* n) {
  if (n->state == 2) {
    n->state = 4;
  } else if (n->state == 1) {
    n->state = 0;
  } else if (n->state == 3) {
    n->close_request = 1;
  }
}

void notice_update(Notice* n) {
  const int ticks = globals().frame_ticks;
  n->shown_frames += ticks;
  n->effect_timer += ticks;
  MsgSystem* msg = msg_system();
  switch (n->state) {
    case 0:
      return;
    case 1:  // waiting 16 frames
      n->timer += ticks;
      if (n->timer > 0xF) {
        n->timer = 0;
        n->state = 2;
        return;
      }
      break;
    case 2:  // the backing slides in; then the text window opens
      n->timer += ticks;
      if (n->timer > 9) {
        n->timer = 10;
        n->shown_frames = 0;
        n->state = 3;
        msg_window_destroy(msg, n->window);
        int wx, wy;
        if (n->at_top == 0) {
          wx = n->text_width - 0x122;
          wy = -0xBF;
        } else {
          wx = n->icon != 0 ? -0x18 : -8;
          wy = -8;
        }
        msg_window_create(msg, wx, wy, -(n->text_width + 0x12), 1, n->text, n->window);
        if (MsgWindow* w = msg_window(n->window)) {
          w->fade_in = 1;
          w->show_prompt = 0;
          w->draw_frame = 0;
          w->draw_body = 0;
          w->hold = 1;
          if (n->icon == 0) w->timer = 0;
        }
      }
      if (n->at_top == 0) {
        n->y = n->timer != 0 ? 0xE0 - (n->timer * 0x21) / 10 : 0xBF;
      } else {
        n->y = n->timer == 0 ? -0x21 : (n->timer * 0x21) / 10 - 0x19;
      }
      n->y += 2;
      break;
    case 3: {  // shown until the text ends (or a window 0-4 opens)
      const int status = msg_window_status(msg, n->window);
      if (n->effect == 0 && status == 8) {
        n->effect_timer = 0;
        n->effect = 1;
      }
      if (n->close_request != 0 || status == 1 || status == -1) {
        n->state = 4;
        // ORIGINAL BUG (original_bugs.md V1): with status -1 the window is gone, and the
        // console queues the move through a null pointer (into low RAM):
        // msg_window_push_move(msg_window(n->window), ...);
        if (MsgWindow* w = msg_window(n->window)) {
          if (n->at_top == 0) {
            msg_window_push_move(w, 0x122 - n->text_width, 0xE0, 10);
          } else {
            msg_window_push_move(w, n->icon != 0 ? 0x18 : 8, -0x19, 10);
          }
        }
      }
      break;
    }
    case 4:  // sliding out
      n->timer -= ticks;
      if (n->timer < 1) {
        msg_window_destroy(msg, n->window);
        n->state = 0;
        return;
      }
      if (n->at_top == 0) {
        n->y = 0xE0 - (n->timer * 0x21) / 10;
      } else {
        n->y = (n->timer * 0x21) / 10 - 0x19;
      }
      n->y += 2;
      break;
    default:
      break;
  }
  notice_draw(n);
}

void notice_draw(Notice* n) {
  // (The original also skips drawing while the byte 0x8007572e is set; nothing sets it.)
  if (n->state < 2 || globals().disc_error != 0) return;
  Screen* s = field_screen();
  const u8* icons = field_sys_icons();
  if (s == nullptr || s->current == nullptr || icons == nullptr) return;
  if (n->icon != 0 && n->state > 2) {
    // The icon, spinning open over its first 16 frames.
    const u8* e = icons + (n->icon + 0x13) * 8;
    auto* p = s->alloc_prims<psyq::POLY_FT4>();
    if (!p) return;
    psyq::setlen(p, 9);
    p->r0 = p->g0 = p->b0 = 0;
    p->code = 0x2D;
    p->tpage = 0x16;
    p->clut = 0x7FCB;
    u8 du = static_cast<u8>(e[4] - e[2]);
    u8 dv = static_cast<u8>(e[5] - e[3]);
    const s16 sx = static_cast<s16>(n->x + 6);
    const s16 sy = static_cast<s16>(n->y + 4);
    s16 t = static_cast<s16>(n->shown_frames - 1);
    if (n->shown_frames < 0x10) {
      du = static_cast<u8>(du - 1);
      dv = static_cast<u8>(dv - 1);
    } else {
      t = 0x10;
    }
    p->u0 = e[2];
    p->v0 = e[3];
    p->u1 = static_cast<u8>(e[2] + du);
    p->v1 = e[3];
    p->u2 = e[2];
    p->v2 = static_cast<u8>(e[3] + dv);
    p->u3 = static_cast<u8>(e[2] + du);
    p->v3 = static_cast<u8>(e[3] + dv);
    p->x0 = sx;
    p->y0 = sy;
    p->x1 = static_cast<s16>(sx + t);
    p->y1 = static_cast<s16>(sy - (t - 0x10));
    p->x2 = static_cast<s16>(sx - (t - 0x10));
    p->y2 = static_cast<s16>(sy + t);
    p->x3 = static_cast<s16>(n->x + 0x16);
    p->y3 = static_cast<s16>(n->y + 0x14);
    add(0x810, p);
  }
  int box = n->text_width + (n->icon != 0 ? 0x1C : 0xC);
  if (box < 0xF9) {
    if (box & 1) --box;
  } else {
    box = 0xF8;
  }
  const s16 bw = static_cast<s16>(box);
  if (n->effect == 1) {
    if (n->effect_timer < 0x9C41) {
      if (n->effect_timer >= 0x1F) {
        // A light sweep across the banner, clipped to it.
        auto* full = s->alloc_prims<psyq::DR_AREA>();
        if (!full) return;
        const psyq::RECT screen_area{0, draw_y(), 0x140, 0xE0};
        psyq::SetDrawArea(full, &screen_area);
        add(0x811, full);
        s16 y = static_cast<s16>(n->y);
        const int sweep = (n->effect_timer - 0x1E) * 5;
        int along = sweep;
        if (box < sweep) {
          y = static_cast<s16>(y + (sweep - box));
          along = box;
          if (sweep - box > 0x2F) n->effect_timer = 40000;
        }
        const s16 x = static_cast<s16>(n->x + along);
        auto* g = s->alloc_prims<psyq::POLY_G4>();
        if (!g) return;
        psyq::setlen(g, 8);
        g->code = 0x3A;
        g->r0 = g->g0 = g->b0 = 0;
        g->x0 = x;
        g->y0 = y;
        g->r1 = g->g1 = g->b1 = 0x60;
        g->x1 = static_cast<s16>(x + 0x30);
        g->y1 = y;
        g->r2 = g->g2 = g->b2 = 0;
        g->x2 = static_cast<s16>(x - 0x18);
        g->y2 = static_cast<s16>(y + 0x18);
        g->r3 = g->g3 = g->b3 = 0x20;
        g->x3 = static_cast<s16>(x + 0x18);
        g->y3 = static_cast<s16>(y + 0x18);
        add(0x811, g);
        add_mode(0x811, 0xE1000236);
        psyq::RECT clip{static_cast<s16>(n->x + 2), static_cast<s16>(n->y + draw_y() + 2), bw, 0x14};
        // rect_clip_to_screen (0x80048734)
        const s16 top = draw_y();
        if (clip.x < 0) {
          clip.w = static_cast<s16>(clip.w + clip.x);
          clip.x = 0;
        }
        if (clip.x + clip.w > 0x140) clip.w = static_cast<s16>(0x140 - clip.x);
        if (clip.y < top) {
          clip.h = static_cast<s16>(clip.h - (top - clip.y));
          clip.y = top;
        }
        if (top + 0xE0 < clip.y + clip.h) clip.h = static_cast<s16>(clip.h + (top - (clip.y + clip.h - 0xE0)));
        auto* a = s->alloc_prims<psyq::DR_AREA>();
        if (!a) return;
        psyq::SetDrawArea(a, &clip);
        add(0x811, a);
      }
    } else {
      const int k = n->effect_timer - 0x9C41;
      if (k > 0xB) {
        n->effect = 2;
      } else {
        // A 12-frame sparkle at the banner's right end.
        auto* p = s->alloc_prims<psyq::POLY_FT4>();
        if (!p) return;
        psyq::setlen(p, 9);
        p->r0 = p->g0 = p->b0 = 0x60;
        p->code = 0x2E;
        p->tpage = 0x16;
        p->clut = 0x7FCA;
        const u8 u = static_cast<u8>(icons[0x132] + (k % 4) * 0x10);
        const u8 v = static_cast<u8>(icons[0x133] + (k / 4) * 0x18);
        p->u0 = u;
        p->v0 = v;
        p->u1 = static_cast<u8>(u + 0x10);
        p->v1 = v;
        p->u2 = u;
        p->v2 = static_cast<u8>(v + 0x18);
        p->u3 = static_cast<u8>(u + 0x10);
        p->v3 = static_cast<u8>(v + 0x18);
        p->x0 = static_cast<s16>(n->x + bw - 3);
        p->y0 = static_cast<s16>(n->y + 0xE);
        p->x1 = static_cast<s16>(n->x + bw + 6);
        p->y1 = static_cast<s16>(n->y + 0xE);
        p->x2 = static_cast<s16>(n->x + bw - 3);
        p->y2 = static_cast<s16>(n->y + 0x1B);
        p->x3 = static_cast<s16>(n->x + bw + 6);
        p->y3 = static_cast<s16>(n->y + 0x1B);
        add(0x811, p);
        add_mode(0x811, 0xE1000236);
      }
    }
  }
  // The backing: the banner strip and its right cap.
  const s16 h = static_cast<s16>(icons[0x9D] - icons[0x9B]);
  auto* a = s->alloc_prims<psyq::SPRT>();
  auto* b = s->alloc_prims<psyq::SPRT>();
  if (!a || !b) return;
  psyq::setSprt(a);
  a->x0 = static_cast<s16>(n->x);
  a->y0 = static_cast<s16>(n->y);
  a->w = bw;
  a->h = h;
  a->u0 = icons[0x9A];
  a->v0 = icons[0x9B];
  a->code = static_cast<u8>((a->code & ~2) | 1);
  a->clut = 0x7FC8;
  add(0x810, a);
  psyq::setSprt(b);
  b->x0 = static_cast<s16>(n->x + bw);
  b->y0 = static_cast<s16>(n->y);
  b->w = 4;
  b->h = h;
  b->u0 = static_cast<u8>(icons[0x9A] - 0xC);
  b->v0 = icons[0x9B];
  b->clut = 0x7FC8;
  b->code = static_cast<u8>((b->code & ~2) | 1);
  add(0x810, b);
  add_mode(0x810, 0xE1000216);
}

void notice_reset(Notice* n) { n->state = 0; }

}  // namespace ss::game::field
