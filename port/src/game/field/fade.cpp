#include "game/field/fade.h"

#include <cstring>

#include "game/common/math.h"
#include "game/field/field.h"
#include "game/field/message.h"
#include "game/field/screen.h"
#include "runtime/libgpu.h"

namespace ss::game::field {
namespace {

ScreenFade* g_fade = nullptr;  // 0x80075298

void add(int ot, void* prim) { psyq::addPrim(field_screen()->ot_entry(ot), prim); }

// DR_MODE carrying only the blend mode (a draw-mode word 0xe1000200 | abr << 5).
void add_blend(int ot, int abr) {
  auto* m = field_screen()->alloc_prims<psyq::DR_MODE>();
  if (!m) return;
  psyq::setlen(m, 2);
  m->code[0] = static_cast<u32>(abr & 3) << 5 | 0xE1000200;
  m->code[1] = 0;
  add(ot, m);
}

void bg_tint_enabled(int on) {
  if (g_fade_targets.bg_set_tint_enabled) g_fade_targets.bg_set_tint_enabled(on);
}
void bg_tint(int r, int g, int b) {
  if (g_fade_targets.bg_set_tint) g_fade_targets.bg_set_tint(r, g, b);
}

// The per-map BG tint, applied whenever no other tint is in force.
void apply_bg_tint() {
  if (g_fade_targets.bg_tint_active != 0) {
    bg_tint_enabled(1);
    bg_tint(g_fade_targets.bg_tint[0], g_fade_targets.bg_tint[1], g_fade_targets.bg_tint[2]);
  } else {
    bg_tint_enabled(0);
  }
}

void free_tiles(ScreenFade* f) {
  for (auto*& t : f->tiles) {
    delete[] t;
    t = nullptr;
  }
}

void set_xy(s16* xy, s16 x, s16 y) {
  xy[0] = x;
  xy[1] = y;
}

}  // namespace

FadeTargets g_fade_targets;

ScreenFade* field_fade() { return g_fade; }

ScreenFade* fade_construct(ScreenFade* f) {
  f->phase = 0;
  f->write = 0;
  f->read = 0;
  f->hold = 2;
  g_fade = f;
  task_register(f, 1);
  for (auto*& t : f->tiles) t = nullptr;
  return f;
}

ScreenFade::~ScreenFade() { fade_destruct(this); }

void fade_destruct(ScreenFade* f) {
  free_tiles(f);
  task_unregister(f);
  if (g_fade == f) g_fade = nullptr;
}

void ScreenFade::update() { fade_update(this); }

void fade_set_scene_brightness(ScreenFade*, int level) {
  bg_tint_enabled(1);
  bg_tint(level, level, level);
  if (g_fade_targets.actors_set_brightness) g_fade_targets.actors_set_brightness(level);
  if (MsgSystem* msg = msg_system()) {
    msg->tint_enabled = 1;
    msg->tint[0] = msg->tint[1] = msg->tint[2] = static_cast<u8>(level);
  }
}

void fade_update(ScreenFade* f) {
  FieldGlobals& g = globals();
  if (f->write == f->read) {
    // Nothing queued: the capture, the hold colour or the BG tint.
    if (f->phase == 1) {
      fade_set_scene_brightness(f, 0);
      f->phase = 2;
      return;
    }
    if (f->phase == 2) {
      fade_draw_capture(f, 0x80, 0, 0);
      return;
    }
    if (f->hold == 1) {
      fade_draw_grey(f, 0xFF, 0, 0, 0x820);
      return;
    }
    if (f->hold == 2) {
      fade_draw_grey(f, 0, 0, 0, 0x820);
      screen_set_clear_colour(field_screen(), 0, 0, 0);
      psyq::SetDispMask(0);
      return;
    }
    apply_bg_tint();
    return;
  }
  apply_bg_tint();
  if (g.disc_error != 0) return;
  if (g_fade_targets.scene_loaded != 0 && g_fade_targets.scene != nullptr) {
    const u8* s = g_fade_targets.scene;
    screen_set_clear_colour(field_screen(), s[0x6F], s[0x70], s[0x71]);
  }
  psyq::SetDispMask(1);
  f->hold = 0;
  FadeCommand& c = f->cmd[f->read];
  c.elapsed = static_cast<s16>(c.elapsed + g.frame_ticks);
  if (c.frames < c.elapsed) c.elapsed = c.frames;
  const int abr = (c.type & 1) != 0 ? 2 : 1;  // odd: subtract (black); even: add (white)
  // (A command of 0 frames would divide by zero on the console; here it ends at once.)
  const int frames = c.frames != 0 ? c.frames : 1;
  switch (c.type) {
    case 0:
      break;
    case 1:
    case 2:
      fade_draw_grey(f, 0xFF - (c.elapsed * 0xFF) / frames, 1, abr, 0x820);
      break;
    case 3:
    case 4:
      if (c.frames == 0) {
        fade_draw_grey(f, 0xFF, 1, abr, 0x820);
      } else {
        fade_draw_grey(f, (c.elapsed * 0xFF) / c.frames, 1, abr, 0x820);
      }
      break;
    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28: {
      int radius, level = 0;
      const int t = (c.elapsed * 0x3C0) / static_cast<u16>(frames);
      if (c.type <= 0x26) {  // closing: the rim fades, then the circle shrinks
        radius = 0x3C0 - t;
        if (radius > 0x1E0) {
          radius = 0x1E0;
          level = ((0x1E0 - t) * 0xFF) / 0x1E0;
        }
      } else {  // opening
        radius = t;
        if (radius > 0x1E0) {
          level = ((radius - 0x1E0) * 0xFF) / 0x1E0;
          radius = 0x1E0;
        }
      }
      const u32 colour = static_cast<u32>(level) | static_cast<u32>(level) << 8 | static_cast<u32>(level) << 16;
      const u32 centre = static_cast<u16>(c.p4) | static_cast<u32>(static_cast<u16>(c.p5)) << 16;
      fade_draw_iris(f, centre, 0x20, radius, colour, abr);
      break;
    }
    case 0x29: {
      const int level = (c.elapsed << 7) / frames;
      fade_set_scene_brightness(f, level);
      if (f->phase == 1) {
        f->phase = 0;
        return;
      }
      int b = 0x80 - level;
      if (b < 0) {
        b = 0;
      } else if (b > 0xFF) {
        b = 0xFF;
      }
      fade_draw_capture(f, static_cast<u8>(b), 1, 1);
      break;
    }
    default: {
      const int k = (c.type - 5) / 2;
      int progress;
      if ((k & 1) == 0) {
        progress = 0x280 - (c.elapsed * 0x280) / frames;
      } else {
        progress = (c.elapsed * 0x280) / frames;
      }
      u32 dir;
      std::memcpy(&dir, field_data(0x8007448C + static_cast<u32>(k) * 4), 4);
      fade_draw_wipe(f, progress, static_cast<int>(dir), abr);
      break;
    }
  }
  if (c.frames <= c.elapsed) {
    if (c.type == 0x29) free_tiles(f);
    g.draw_enabled = 1;
    f->read = static_cast<u8>((f->read + 1) & 7);
    const u32 t = static_cast<u32>(c.type - 1);
    if (f->read == f->write && c.type != 0 && (t & 2) != 0) {
      // A fade out was the last command: hold its colour.
      if ((t & 1) == 0) {
        f->hold = 2;
        psyq::SetDispMask(0);
      } else {
        f->hold = 1;
      }
    }
  }
  f->phase = 0;
}

bool fade_push(ScreenFade* f, s16 type, s16 frames, s16 p4, s16 p5) {
  const bool room = ((f->write + 1) & 7) != f->read;
  if (room) {
    FadeCommand& c = f->cmd[f->write];
    c.type = type;
    c.frames = frames;
    c.elapsed = 0;
    c.active = 1;
    c.p4 = p4;
    c.p5 = p5;
    f->write = static_cast<u8>((f->write + 1) & 7);
  }
  return room;
}

bool fade_is_active(ScreenFade* f, int type) {
  if (type == -1) return f->write != f->read;
  u32 i = f->read;
  if ((i & 7) == f->write) return false;
  u32 slot = i & 7;
  do {
    ++i;
    if (f->cmd[slot].type == type) return true;
    slot = i & 7;
  } while (slot != f->write);
  return false;
}

void fade_draw_grey(ScreenFade* f, int level, int semi, int abr, int ot_slot) {
  fade_draw_tile(f, static_cast<u8>(level), static_cast<u8>(level), static_cast<u8>(level), semi, abr, ot_slot);
}

void fade_draw_tile(ScreenFade*, u8 r, u8 g, u8 b, int semi, int abr, int ot_slot) {
  Screen* s = field_screen();
  auto* t = s->alloc_prims<psyq::TILE>();
  if (!t) return;
  psyq::setTile(t);
  t->w = 0x140;
  t->r0 = r;
  t->g0 = g;
  t->b0 = b;
  t->x0 = 0;
  t->y0 = 0;
  t->h = 0xF0;
  psyq::setSemiTrans(t, semi != 0);
  t->code |= 1;
  add(ot_slot, t);
  add_blend(ot_slot, abr);
}

void fade_draw_wipe(ScreenFade*, int progress, int direction, int abr) {
  Screen* s = field_screen();
  // The covered part: opaque bands of up to 0x78 pixels behind the edge.
  s16 off = 0;
  s16 a = 0x180, b = 0xA0, c = 0x70, d = 0x1B0, e = -0xA0, f2 = 0x70, g = 0xA0, h = -0x70;
  for (int done = 0xF0; progress - done > 0; done += 0x78) {
    auto* p = s->alloc_prims<psyq::POLY_F4>();
    if (!p) return;
    psyq::setlen(p, 5);
    p->code = 0x29;
    const u8 v = abr == 1 ? 0xFF : 0;
    p->r0 = p->g0 = p->b0 = v;
    int band = progress - done;
    if (band > 0x78) band = 0x78;
    const s16 n = static_cast<s16>(band);
    switch (direction) {
      case 0:
        set_xy(&p->x0, 0, off);
        set_xy(&p->x1, 0x140, off);
        set_xy(&p->x2, 0, static_cast<s16>(off + n));
        set_xy(&p->x3, 0x140, static_cast<s16>(off + n));
        break;
      case 1:
        set_xy(&p->x0, 0, static_cast<s16>(0xE0 - off));
        set_xy(&p->x1, 0x140, static_cast<s16>(0xE0 - off));
        set_xy(&p->x2, 0, static_cast<s16>(0xE0 - n - off));
        set_xy(&p->x3, 0x140, static_cast<s16>(0xE0 - n - off));
        break;
      case 2:
        set_xy(&p->x0, off, 0);
        set_xy(&p->x1, off, 0xE0);
        set_xy(&p->x2, static_cast<s16>(off + n), 0);
        set_xy(&p->x3, static_cast<s16>(off + n), 0xE0);
        break;
      case 3:
        set_xy(&p->x0, static_cast<s16>(0x140 - off), 0);
        set_xy(&p->x1, static_cast<s16>(0x140 - off), 0xE0);
        set_xy(&p->x2, static_cast<s16>(0x140 - n - off), 0);
        set_xy(&p->x3, static_cast<s16>(0x140 - n - off), 0xE0);
        break;
      case 4:
        set_xy(&p->x0, h, f2);
        set_xy(&p->x1, g, e);
        set_xy(&p->x2, static_cast<s16>(off + n - 0x70), static_cast<s16>(off + n + 0x70));
        set_xy(&p->x3, static_cast<s16>(off + n + 0xA0), static_cast<s16>(off + n - 0xA0));
        break;
      case 5:
        set_xy(&p->x0, h, c);
        set_xy(&p->x1, g, a);
        set_xy(&p->x2, static_cast<s16>(off + n - 0x70), static_cast<s16>(0x70 - n - off));
        set_xy(&p->x3, static_cast<s16>(off + n + 0xA0), static_cast<s16>(0x180 - n - off));
        break;
      case 6:
        set_xy(&p->x0, d, f2);
        set_xy(&p->x1, b, e);
        set_xy(&p->x2, static_cast<s16>(0x1B0 - n - off), static_cast<s16>(off + n + 0x70));
        set_xy(&p->x3, static_cast<s16>(0xA0 - n - off), static_cast<s16>(off + n - 0xA0));
        break;
      case 7:
        set_xy(&p->x0, d, c);
        set_xy(&p->x1, b, a);
        set_xy(&p->x2, static_cast<s16>(0x1B0 - n - off), static_cast<s16>(0x70 - n - off));
        set_xy(&p->x3, static_cast<s16>(0xA0 - n - off), static_cast<s16>(0x180 - n - off));
        break;
      default:
        break;
    }
    a = static_cast<s16>(a - 0x78);
    b = static_cast<s16>(b - 0x78);
    c = static_cast<s16>(c - 0x78);
    d = static_cast<s16>(d - 0x78);
    e = static_cast<s16>(e + 0x78);
    f2 = static_cast<s16>(f2 + 0x78);
    g = static_cast<s16>(g + 0x78);
    h = static_cast<s16>(h + 0x78);
    off = static_cast<s16>(off + 0x78);
    add(0x820, p);
  }
  // The edge: two semi-transparent gradient strips of 0x78 pixels (0 -> 0x7f -> 0xff).
  const s16 P = static_cast<s16>(progress);
  int edge = progress, edge78 = progress - 0x78, edgeA0 = progress - 0xA0;
  s16 o = 0;
  for (int k = 0; k < 2; ++k) {
    auto* p = s->alloc_prims<psyq::POLY_G4>();
    if (!p) return;
    psyq::setlen(p, 8);
    p->code = 0x3B;
    const s16 e3 = static_cast<s16>(edge), e78 = static_cast<s16>(edge78), eA0 = static_cast<s16>(edgeA0);
    switch (direction) {
      case 0:
        set_xy(&p->x0, 0, e3);
        set_xy(&p->x1, 0x140, e3);
        set_xy(&p->x2, 0, static_cast<s16>(e3 - 0x78));
        set_xy(&p->x3, 0x140, static_cast<s16>(e3 - 0x78));
        break;
      case 1: {
        const s16 y = static_cast<s16>(o - (P - 0xF0));
        set_xy(&p->x0, 0, y);
        set_xy(&p->x1, 0x140, y);
        set_xy(&p->x2, 0, static_cast<s16>(y + 0x78));
        set_xy(&p->x3, 0x140, static_cast<s16>(y + 0x78));
        break;
      }
      case 2:
        set_xy(&p->x0, e3, 0);
        set_xy(&p->x1, e3, 0xF0);
        set_xy(&p->x2, static_cast<s16>(e3 - 0x78), 0);
        set_xy(&p->x3, static_cast<s16>(e3 - 0x78), 0xF0);
        break;
      case 3: {
        const s16 x = static_cast<s16>(o - (P - 0x140));
        set_xy(&p->x0, x, 0);
        set_xy(&p->x1, x, 0xF0);
        set_xy(&p->x2, static_cast<s16>(x + 0x78), 0);
        set_xy(&p->x3, static_cast<s16>(x + 0x78), 0xF0);
        break;
      }
      case 4: {
        const s16 y0 = static_cast<s16>(P + 0x78 - o), x1 = static_cast<s16>(P + 0xA0 - o);
        set_xy(&p->x0, e78, y0);
        set_xy(&p->x1, x1, eA0);
        set_xy(&p->x2, static_cast<s16>(e78 - 0x78), static_cast<s16>(y0 - 0x78));
        set_xy(&p->x3, static_cast<s16>(x1 - 0x78), static_cast<s16>(eA0 - 0x78));
        break;
      }
      case 5: {
        const s16 y0 = static_cast<s16>(o - (P - 0x78)), x1 = static_cast<s16>(P + 0xA0 - o),
                  y1 = static_cast<s16>(o - (P - 400));
        set_xy(&p->x0, e78, y0);
        set_xy(&p->x1, x1, y1);
        set_xy(&p->x2, static_cast<s16>(e78 - 0x78), static_cast<s16>(y0 + 0x78));
        set_xy(&p->x3, static_cast<s16>(x1 - 0x78), static_cast<s16>(y1 + 0x78));
        break;
      }
      case 6: {
        const s16 x0 = static_cast<s16>(o - (P - 0x1B8)), y0 = static_cast<s16>(P + 0x78 - o),
                  x1 = static_cast<s16>(o - (P - 0xA0));
        set_xy(&p->x0, x0, y0);
        set_xy(&p->x1, x1, eA0);
        set_xy(&p->x2, static_cast<s16>(x0 + 0x78), static_cast<s16>(y0 - 0x78));
        set_xy(&p->x3, static_cast<s16>(x1 + 0x78), static_cast<s16>(eA0 - 0x78));
        break;
      }
      case 7: {
        const s16 x0 = static_cast<s16>(o - (P - 0x1B8)), y0 = static_cast<s16>(o - (P - 0x78)),
                  x1 = static_cast<s16>(o - (P - 0xA0)), y1 = static_cast<s16>(o - (P - 400));
        set_xy(&p->x0, x0, y0);
        set_xy(&p->x1, x1, y1);
        set_xy(&p->x2, static_cast<s16>(x0 + 0x78), static_cast<s16>(y0 + 0x78));
        set_xy(&p->x3, static_cast<s16>(x1 + 0x78), static_cast<s16>(y1 + 0x78));
        break;
      }
      default:
        break;
    }
    const u8 c0 = k == 0 ? 0 : 0x7F, c1 = k == 0 ? 0x7F : 0xFF;
    p->r0 = p->g0 = p->b0 = c0;
    p->r1 = p->g1 = p->b1 = c0;
    p->r2 = p->g2 = p->b2 = c1;
    p->r3 = p->g3 = p->b3 = c1;
    edge -= 0x78;
    edge78 -= 0x78;
    edgeA0 -= 0x78;
    o = static_cast<s16>(o + 0x78);
    add(0x820, p);
  }
  add_blend(0x820, abr);
}

void fade_capture_screen(ScreenFade* f) {
  Screen* s = field_screen();
  f->phase = 1;
  // The displayed frame is the buffer not being drawn.
  const ScreenBuffer* shown = s->current == &s->buffers[0] ? &s->buffers[1] : &s->buffers[0];
  const s16 top = shown->draw.clip.y;
  for (u32 i = 0; i < 10; ++i) {
    delete[] f->tiles[i];
    f->tiles[i] = new u16[0x3800 / 2];
    const psyq::RECT r{static_cast<s16>((i % 5) * 0x40), static_cast<s16>(top + (i / 5) * 0x70), 0x40, 0x70};
    psyq::StoreImage(&r, f->tiles[i]);
    psyq::DrawSync(0);
    for (int k = 0; k < 0x3800 / 2; ++k) f->tiles[i][k] |= 0x8000;  // STP on every pixel
  }
}

void fade_draw_capture(ScreenFade* f, u8 brightness, int semi, int abr) {
  Screen* s = field_screen();
  const psyq::RECT scratch{0x1C0, 0x170, 0x40, 0x70};
  for (u32 i = 0; i < 10; ++i) {
    psyq::P_TAG* aux = s->current->aux_ot.data();
    psyq::ClearOTag(aux, 16);
    if (f->tiles[i] != nullptr) psyq::LoadImage(&scratch, f->tiles[i]);
    auto* p = s->alloc_prims<psyq::SPRT>();
    auto* m = s->alloc_prims<psyq::DR_MODE>();
    if (!p || !m) return;
    psyq::setSprt(p);
    p->x0 = static_cast<s16>((i % 5) * 0x40);
    p->y0 = static_cast<s16>((i / 5) * 0x70);
    p->w = scratch.w;
    p->u0 = 0;
    p->h = scratch.h;
    p->v0 = static_cast<u8>(scratch.y);
    psyq::setSemiTrans(p, semi != 0);
    p->r0 = p->g0 = p->b0 = brightness;
    p->code = static_cast<u8>(p->code & ~1);
    psyq::addPrim(&aux[0], p);
    psyq::setlen(m, 2);
    m->code[0] = static_cast<u32>(abr & 3) << 5 | (scratch.y >> 4 & 0x10) | 0x100 | (scratch.x & 0x3FF) >> 6 |
                 0xE1000200;
    m->code[1] = 0;
    psyq::addPrim(&aux[0], m);
    psyq::DrawOTag(aux);  // drawn now, before this frame's ordering table
    if (i != 9) psyq::DrawSync(0);
  }
}

void fade_draw_iris(ScreenFade*, u32 centre, int segments, int radius, u32 colour, int abr) {
  const s16 cx = static_cast<s16>(centre), cy = static_cast<s16>(centre >> 16);
  if (static_cast<u16>(cx + 0xF0) >= 0x321 || static_cast<u16>(cy + 0xF0) >= 0x2D1) return;
  Screen* s = field_screen();
  int prev_y = cy + radius;
  int prev_x = cx;
  for (int i = 1; i <= segments; ++i) {
    const int a = (i * 0x1000) / segments + 0x400;
    const int x = cx + ((cos4096(static_cast<u32>(a)) * radius) >> 12);
    const int y = cy + ((sin4096(static_cast<u32>(a)) * radius) >> 12);
    auto* p = s->alloc_prims<psyq::POLY_G3>();
    if (!p) return;
    psyq::setlen(p, 6);
    p->code = 0x32;
    p->r0 = p->g0 = p->b0 = 0xFF;  // white at the centre
    p->x0 = cx;
    p->y0 = cy;
    std::memcpy(&p->r1, &colour, 3);
    std::memcpy(&p->r2, &colour, 3);
    p->x1 = static_cast<s16>(x);
    p->y1 = static_cast<s16>(y);
    p->x2 = static_cast<s16>(prev_x);
    p->y2 = static_cast<s16>(prev_y);
    add(0x820, p);
    prev_x = x;
    prev_y = y;
  }
  add_blend(0x820, abr);
}

}  // namespace ss::game::field
