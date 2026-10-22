#include "game/field/bg.h"

#include <cstring>
#include <memory>

#include "game/boot/rng.h"
#include "game/field/camera.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/message.h"
#include "game/field/screen.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

std::unique_ptr<BgRenderer> g_bg;
BgGlobals g_bg_globals;
u8* g_anim_save = nullptr;  // 0x80075728: bg_suspend's copy of the animation state

// Little-endian views into the scene (MapScene, section 0 block 0). The animation,
// colour and window records are updated in place, as on the console.
s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
s16 rd16(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
u16 rdu16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}
void wr16(u8* p, s32 v) {
  const s16 x = static_cast<s16>(v);
  std::memcpy(p, &x, 2);
}

// Sub-block records.
// Tile (sub-block 0, 8 bytes): u, v, w, h, flags (bit 0 semi, bits 1-2 abr, bit 3 flip x,
// bit 4 flip y; 3-4 nonzero = POLY_FT4), clut x / 16, clut y - 0x1e0, prim (0x64 = SPRT;
// for FT4 tiles a scale percentage).
struct Tile {
  u8 u, v, w, h, flags, clut_x, clut_y, prim;
};
const Tile* tile_at(const u8* scene, int index) {
  return reinterpret_cast<const Tile*>(scene + rd32(scene + 0x78) + index * 8);
}
u16 tile_clut(const Tile* t) { return static_cast<u16>((t->clut_y + 0x1E0) << 6 | t->clut_x); }

// BG sprite (sub-block 1, 20 bytes).
struct SpriteRec {
  const u8* p;
  s16 x() const { return rd16(p); }
  s16 y() const { return rd16(p + 2); }
  u16 tile() const { return rdu16(p + 4); }
  u8 page() const { return p[6]; }
  u8 layer() const { return p[7]; }
  s16 anim() const { return rd16(p + 8); }
  s16 colour() const { return rd16(p + 0xA); }
  s16 x_min() const { return rd16(p + 0xC); }
  s16 x_max() const { return rd16(p + 0xE); }
  s16 y_min() const { return rd16(p + 0x10); }
  s16 y_max() const { return rd16(p + 0x12); }
};

// Animation record (sub-block 4, 0xa0 bytes): +0 timer, +2 tile, +4 frame count, +5
// flags (0x80 frozen, 1 ping-pong, 2 stop at the end), +6 page, +8 r, +9 g, +0xa b,
// +0xc dx, +0xe dy; frames of 12 bytes from +0x10: tile, duration, r g, b, page, dx, dy.
u8* anim_at(u8* scene, int index) { return scene + rd32(scene + 0x88) + index * 0xA0; }
// Colour record (sub-block 5, 0x14c bytes): +2 step count, +4 timer, steps of 8 bytes
// from +0x88 {duration, r g, b}, +0x148 current r g, +0x14a b, +0x14b ping-pong.
u8* colour_at(u8* scene, int index) { return scene + rd32(scene + 0x8C) + index * 0x14C; }

void step_animations(u8* scene, s32 ticks) {
  const int count = rdu16(scene + 0x64);
  for (int r = 0; r < count; ++r) {
    u8* a = anim_at(scene, r);
    const u8 flags = a[5];
    if (flags & 0x80) continue;
    const int frames = a[4];
    const u16 timer = rdu16(a);
    int i = 0, sum = 0;
    bool found = false;
    for (i = 0; i < frames; ++i) {
      sum += rdu16(a + 0x12 + i * 12);
      if (timer < sum) {
        found = true;
        break;
      }
    }
    if (!found) {
      if (flags & 1) {
        for (i = frames - 2; i > 0; --i) {
          sum += rdu16(a + 0x12 + i * 12);
          if (timer < sum) {
            found = true;
            break;
          }
        }
      }
      if (!found) {
        if ((flags & 2) == 0) {
          i = 0;
          wr16(a, sum != 0 ? timer % sum : timer);
        } else {
          i = (flags & 1) ? 0 : frames - 1;
          wr16(a, rdu16(a) - ticks);
        }
      }
    }
    const u8* f = a + 0x10 + i * 12;
    std::memcpy(a + 2, f, 2);       // tile
    wr16(a + 6, f[7]);              // page
    std::memcpy(a + 8, f + 4, 2);   // r, g
    a[0xA] = f[6];                  // b
    std::memcpy(a + 0xC, f + 8, 4); // dx, dy
    wr16(a, rdu16(a) + ticks);
  }
}

void step_colours(u8* scene, s32 ticks) {
  const int count = rdu16(scene + 0x72);
  for (int r = 0; r < count; ++r) {
    u8* c = colour_at(scene, r);
    const int steps = rdu16(c + 2);
    const u16 timer = rdu16(c + 4);
    int k = 0, sum = 0;
    bool found = false;
    for (k = 0; k < steps; ++k) {
      sum += rdu16(c + 0x88 + k * 8);
      if (timer < sum) {
        found = true;
        break;
      }
    }
    if (!found && k == steps && c[0x14B] == 1) {
      for (k = steps - 2; k > 0; --k) {
        sum += rdu16(c + 0x88 + k * 8);
        if (timer < sum) {
          found = true;
          break;
        }
      }
    }
    if (!found) {
      k = 0;
      if (sum != 0) wr16(c + 4, timer % sum);
    }
    std::memcpy(c + 0x148, c + 0x8A + k * 8, 2);
    c[0x14A] = c[0x8C + k * 8];
    wr16(c + 4, rdu16(c + 4) + (ticks * g_bg_globals.colour_speed) / 100);
  }
}

// The colour of a sprite: its animation's, its colour record's, or neutral; then the
// renderer's tint.
void sprite_colour(const BgRenderer* bg, u8* scene, const SpriteRec& s, u8 rgb[3], u16& tile, u8& page) {
  tile = s.tile();
  page = s.page();
  if (s.anim() != -1) {
    const u8* a = anim_at(scene, s.anim());
    rgb[0] = a[8];
    rgb[1] = a[9];
    rgb[2] = a[0xA];
    tile = rdu16(a + 2);
    page = static_cast<u8>(rdu16(a + 6));
  } else if (s.colour() != -1) {
    const u8* c = colour_at(scene, s.colour());
    rgb[0] = c[0x148];
    rgb[1] = c[0x149];
    rgb[2] = c[0x14A];
  } else {
    rgb[0] = rgb[1] = rgb[2] = 0x80;
  }
  (void)bg;
}

void apply_tint(const BgRenderer* bg, u8 rgb[3]) {
  for (int i = 0; i < 3; ++i) {
    const u32 v = (static_cast<u32>(rgb[i]) * bg->tint[i]) >> 7;
    rgb[i] = static_cast<u8>(v > 0xFF ? 0xFF : v);
  }
}

void add(int ot_index, void* prim) { psyq::addPrim(field_screen()->ot_entry(ot_index), prim); }

psyq::SPRT* make_sprite(const Tile* t, int x, int y, const u8 rgb[3]) {
  auto* p = field_screen()->alloc_prims<psyq::SPRT>();
  if (!p) return nullptr;
  psyq::setlen(p, 4);
  p->code = 0x64;
  p->r0 = rgb[0];
  p->g0 = rgb[1];
  p->b0 = rgb[2];
  p->x0 = static_cast<s16>(x);
  p->y0 = static_cast<s16>(y);
  p->u0 = t->u;
  p->v0 = t->v;
  p->w = t->w;
  p->h = t->h;
  if (t->flags & 1) p->code |= 2;
  p->clut = tile_clut(t);
  return p;
}

// The texture page command that precedes a sprite (queued after it).
void add_tpage(int ot_index, u32 tpage, bool dither) {
  auto* m = field_screen()->alloc_prims<psyq::DR_MODE>();
  if (!m) return;
  if (dither) {
    psyq::setlen(m, 2);
    m->code[0] = 0xE1000200u | (tpage & 0x1FF);
    m->code[1] = 0;
  } else {
    psyq::SetDrawMode(m, 0, 0, static_cast<int>(tpage), nullptr);
  }
  add(ot_index, m);
}

// 0x8008953c: UVs of an FT4 tile, nudged one texel when the quad is flipped and kept
// inside the 256-texel page.
void set_ft4_uvs(psyq::POLY_FT4* p, int u, int v, int w, int h) {
  if (p->x3 - p->x0 < 0 && --u < 0) {
    u = 0;
    --w;
  }
  if (p->y3 - p->y0 < 0 && --v < 0) {
    v = 0;
    --h;
  }
  if (u + w > 0xFF) --w;
  if (v + h > 0xFF) --h;
  p->u0 = static_cast<u8>(u), p->v0 = static_cast<u8>(v);
  p->u1 = static_cast<u8>(u + w), p->v1 = static_cast<u8>(v);
  p->u2 = static_cast<u8>(u), p->v2 = static_cast<u8>(v + h);
  p->u3 = static_cast<u8>(u + w), p->v3 = static_cast<u8>(v + h);
}

}  // namespace

BgGlobals& bg_globals() { return g_bg_globals; }
BgRenderer* bg_renderer() { return g_bg.get(); }

void bg_construct() {
  g_bg = std::make_unique<BgRenderer>();
  task_register(g_bg.get(), 3);  // FieldTask (0x80057450)
  BgGlobals& bgg = g_bg_globals;
  bgg = BgGlobals{};
  // The fades reach the BG through its vtable (+0x2c enable tint, +0x34 tint colour).
  g_fade_targets.bg_set_tint_enabled = [](int on) {
    if (BgRenderer* bg = bg_renderer()) bg_enable_tint(bg, static_cast<u8>(on));
  };
  g_fade_targets.bg_set_tint = [](int r, int g, int b) {
    if (BgRenderer* bg = bg_renderer()) bg_set_tint(bg, r, g, b);
  };
  // Anchored message windows follow the view.
  g_msg_project_anchor = [](s16* xy) {
    if (BgRenderer* bg = bg_renderer(); bg != nullptr && !map_data().scene.empty()) bg_get_view_origin(bg, xy);
  };
}

void bg_destroy() {
  if (g_bg) task_unregister(g_bg.get());
  g_bg.reset();
}

void bg_helper_create() { camscript_create(); }  // CamScript (camscript_construct 0x8008e100 + camscript_reset)

void bg_build_tpages(BgRenderer* bg) {
  if (!bg) return;
  const u8* scene = map_data().scene.data();
  bg->shake = 0;
  for (int page = 0; page < 16; ++page) {
    const u8* entry = scene + 0xEC + page * 4;
    if (entry[0] == 0) continue;
    const u32 ty = static_cast<u32>(page >> 3) << 8;
    for (u32 abr = 0; abr < 4; ++abr) {
      bg->tpage[abr][page] = (entry[1] & 3u) << 7 | abr << 5 | (ty & 0x100) >> 4 |
                             (((page & 7) * 0x40 + 0x200u) & 0x3FF) >> 6 | (ty & 0x200) << 2;
    }
  }
  bg->floor_y = rd16(scene + 0x58);
}

void bg_update_scroll(BgRenderer* bg) {
  if (!bg) return;
  const u8* scene = map_data().scene.data();
  const u16 width = rdu16(scene + 4), height = rdu16(scene + 6);
  if (width < 0x141) {
    bg->offset_x = static_cast<s16>((0x140 - width) / 2);
    bg->scroll_x = 0;
  } else {
    bg->offset_x = 0;
    if (bg->proportional_x == 0) {
      bg->scroll_x = static_cast<s16>((bg->focus_x - static_cast<s16>(rd32(scene + 0x28))) - 0xA0);
    } else {
      const s32 d = static_cast<u16>(bg->focus_x) - rd32(scene + 0x28);
      bg->scroll_x = static_cast<s16>((static_cast<s16>(d) * static_cast<s32>(width - 0x140)) / width);
    }
    if (bg->scroll_x < 0) bg->scroll_x = 0;
    if (width < bg->scroll_x + 0x140) bg->scroll_x = static_cast<s16>(width - 0x140);
  }
  const u32 window = bg->vertical_scroll ? 0xE0 : 0xF0;
  if (window < height) {
    bg->offset_y = 0;
    if (bg->proportional_y == 0) {
      bg->scroll_y = static_cast<s16>((bg->focus_y - static_cast<s16>(rd32(scene + 0x2C))) - static_cast<s16>(window >> 1));
    } else {
      const s32 d = static_cast<u16>(bg->focus_y) - rd32(scene + 0x2C);
      bg->scroll_y = static_cast<s16>((static_cast<s16>(d) * static_cast<s32>(height - window)) / height);
    }
    if (bg->scroll_y < 0) bg->scroll_y = 0;
    if (static_cast<int>(height) < bg->scroll_y + static_cast<int>(window))
      bg->scroll_y = static_cast<s16>(height - window);
  } else {
    bg->offset_y = static_cast<s16>((0xF0 - height) / 2);
    bg->scroll_y = 0;
  }
  if (bg->forced_x != 30000) bg->scroll_x = bg->forced_x;
  if (bg->forced_y != 30000) bg->scroll_y = bg->forced_y;
  if (globals().frame_ticks != 0 && bg->shake != 0) {
    bg->scroll_x = static_cast<s16>(bg->scroll_x + static_cast<s16>(rng_next() % bg->shake) - bg->shake);
    bg->scroll_y = static_cast<s16>(bg->scroll_y + static_cast<s16>(rng_next() % bg->shake) - bg->shake);
  }
}

void bg_suspend(BgRenderer* bg) {
  // Saved into field_flags so a later bg_resume (after a battle or another program)
  // restores the view; the animation records' running state (first 16 bytes of each
  // 0xa0-byte record) goes into a heap copy that is handed over in slot 0x10.
  u8* flags = globals().field_flags;
  wr16(flags + 0x74, bg->focus_x);
  wr16(flags + 0x76, bg->focus_y);
  std::memcpy(flags + 0x78, &bg->floor_y, 4);
  wr16(flags + 0x0A, bg->forced_x);
  wr16(flags + 0x16, bg->forced_y);
  flags[0x7C] = static_cast<u8>(bg->proportional_x);
  flags[0x7D] = static_cast<u8>(bg->proportional_y);
  flags[0x69] = bg->vertical_scroll;
  flags[0x85] = bg->tint_mode;
  flags[0x86] = bg->tint[0];
  flags[0x87] = bg->tint[1];
  flags[0x88] = bg->tint[2];
  flags[0x94] = g_fade_targets.bg_tint_active;
  delete[] g_anim_save;
  g_anim_save = nullptr;
  MapData& map = map_data();
  if (map.scene.empty()) return;
  const int count = rdu16(map.scene.data() + 0x64);
  if (count == 0) return;
  const u32 bytes = static_cast<u32>(count) * 0x10 + 4;
  g_anim_save = new u8[bytes];
  std::memcpy(g_anim_save, &bytes, 4);
  const u8* anims = map.scene.data() + rd32(map.scene.data() + 0x88);
  for (int i = 0; i < count; ++i) std::memcpy(g_anim_save + 4 + i * 0x10, anims + i * 0xA0, 0x10);
}

u8* bg_take_anim_save() {
  u8* p = g_anim_save;
  g_anim_save = nullptr;
  return p;
}

void bg_resume(BgRenderer* bg) {
  u8* flags = globals().field_flags;
  bg->focus_x = rd16(flags + 0x74);
  bg->focus_y = rd16(flags + 0x76);
  std::memcpy(&bg->floor_y, flags + 0x78, 4);
  bg->forced_x = rd16(flags + 0x0A);
  bg->forced_y = rd16(flags + 0x16);
  bg->proportional_x = flags[0x7C];
  bg->proportional_y = flags[0x7D];
  bg->vertical_scroll = flags[0x69];
  bg->tint_mode = flags[0x85];
  for (int i = 0; i < 3; ++i) {
    bg->tint[i] = flags[0x86 + i];
    g_fade_targets.bg_tint[i] = flags[0x86 + i];  // 0x80075aba..0x80075abe
  }
  g_fade_targets.bg_tint_active = flags[0x94];  // 0x80075356
  bg_update_scroll(bg);
  FieldGlobals& g = globals();
  auto* saved = static_cast<u8*>(handoff_get(g.handoff, 0x10));
  if (saved == nullptr) return;
  MapData& map = map_data();
  if (!map.scene.empty()) {
    const int count = rdu16(map.scene.data() + 0x64);
    u8* anims = map.scene.data() + rd32(map.scene.data() + 0x88);
    for (int i = 0; i < count; ++i) std::memcpy(anims + i * 0xA0, saved + 4 + i * 0x10, 0x10);
  }
  delete[] saved;
  handoff_clear(g.handoff, 0x10);
}

void bg_set_tint(BgRenderer* bg, int r, int g, int b) {
  // The tint scales the map's clear colour (scene +0x6f..+0x71) too.
  bg->tint[0] = static_cast<u8>(r);
  bg->tint[1] = static_cast<u8>(g);
  bg->tint[2] = static_cast<u8>(b);
  const u8* scene = map_data().scene.data();
  auto scale = [](int c, int t) {
    int v = c * t;
    if (v < 0) v += 0x7F;
    v >>= 7;
    return v > 0x100 ? 0xFF : v;
  };
  screen_set_clear_colour(field_screen(), static_cast<u8>(scale(scene[0x6F], r)), static_cast<u8>(scale(scene[0x70], g)),
                          static_cast<u8>(scale(scene[0x71], b)));
}

void bg_enable_tint(BgRenderer* bg, u8 on) { bg->tint_on = on; }

// ---- Animation records driven by scripts (scene sub-block 4: 0xa0 bytes each; +0x0b
// the record's tag, +0x05 state: bit 7 frame set by script, bits 0-1 mode, bit 4
// direction; +0x04 frame count; frames of 0xc bytes from +0x10, duration at +2).
namespace {

int anim_count() { return map_data().scene.empty() ? 0 : rdu16(map_data().scene.data() + 0x64); }
u8* anim_record(int i) {
  u8* scene = map_data().scene.data();
  return scene + rd32(scene + 0x88) + i * 0xA0;
}

}  // namespace

int bg_anim_get_frame(BgRenderer*, int tag) {
  for (int i = 0; i < anim_count(); ++i) {
    const u8* a = anim_record(i);
    if (static_cast<s8>(a[0xB]) == tag) return a[5] & 0x7F;
  }
  return -1;
}

void bg_anim_set_frame(BgRenderer*, int tag, int frame) {
  for (int i = 0; i < anim_count(); ++i) {
    u8* a = anim_record(i);
    if (static_cast<s8>(a[0xB]) != tag) continue;
    a[5] = frame < 1 ? static_cast<u8>(~frame) : static_cast<u8>(frame | 0x80);
    const u8* f = a + frame * 0xC + 0x10;
    std::memcpy(a + 2, f, 2);
    wr16(a + 6, f[7]);
    std::memcpy(a + 8, f + 4, 2);
    a[10] = f[6];
    std::memcpy(a + 0xC, f + 8, 2);
    std::memcpy(a + 0xE, f + 0xA, 2);
  }
}

void bg_anim_play(BgRenderer*, int tag, int mode) {
  for (int i = 0; i < anim_count(); ++i) {
    u8* a = anim_record(i);
    if (static_cast<s8>(a[0xB]) != tag) continue;
    const int frames = a[4];
    s16 total = 0;
    if (mode != 3)
      for (int k = 0; k < frames; ++k) total = static_cast<s16>(total + rd16(a + 0x12 + k * 0xC));
    if (mode == 1) {
      wr16(a, total - 1);
      a[5] = 0x12;
    } else if (mode == 0) {
      for (int k = frames - 2; k > 0; --k) total = static_cast<s16>(total + rd16(a + 0x12 + k * 0xC));
      wr16(a, total - 1);
      a[5] = 0x13;
    } else if (mode == 2) {
      wr16(a, total);
      a[5] = 3;
    } else if (mode == 3) {
      a[5] = 2;
      wr16(a, 0);
    }
  }
}

int bg_anim_get_state(BgRenderer*, int tag) {
  for (int i = 0; i < anim_count(); ++i) {
    const u8 st = anim_record(i)[5];
    if (static_cast<s8>(anim_record(i)[0xB]) != tag) continue;
    if ((st & 3) != 2) return (st & 0x10) == 0 ? 2 : 0;
    return (st & 0x10) == 0 ? 3 : 1;
  }
  return -1;
}

void bg_get_view_origin(const BgRenderer* bg, s16 out[2]) {
  const u8* scene = map_data().scene.data();
  out[0] = static_cast<s16>(bg->scroll_x - bg->offset_x + static_cast<s16>(rd32(scene + 0x28)));
  out[1] = static_cast<s16>(bg->scroll_y - bg->offset_y + static_cast<s16>(rd32(scene + 0x2C)));
}

void BgRenderer::update() {
  BgGlobals& bgg = g_bg_globals;
  MapData& map = map_data();
  if (!bgg.draw_enabled || map.scene.empty()) return;
  // TODO(port): skipped while a sub-program runs (0x800752c8) unless 0x80076279.
  u8* scene = map.scene.data();
  const s32 ticks = globals().frame_ticks;
  step_animations(scene, ticks);
  step_colours(scene, ticks);

  const int plain = rdu16(scene + 0x5C);
  const u8* records = scene + rd32(scene + 0x7C);
  int index = 0;  // running record index through the three sprite groups

  // Plain BG sprites.
  for (int n = 0; n < plain; ++n, ++index) {
    const SpriteRec s{records + index * 20};
    const bool in_window = s.x_min() < scroll_x && scroll_x < s.x_max() && s.y_min() < scroll_y && scroll_y < s.y_max();
    if (!(in_window || (bgg.animate_all == 0 && s.anim() != -1))) continue;
    if (s.anim() != -1 && !bgg.animated_on) continue;
    u8 rgb[3];
    u16 tile_index;
    u8 page;
    sprite_colour(this, scene, s, rgb, tile_index, page);
    if (tint_on) {
      if (tint_mode == 0) {
        tile_index = s.tile();
        page = s.page();
        std::memcpy(rgb, tint, 3);
      } else {
        // Multiplied without the clamp in this path, as the original does.
        for (int i = 0; i < 3; ++i) rgb[i] = static_cast<u8>((static_cast<u32>(rgb[i]) * tint[i]) >> 7);
      }
    }
    const Tile* t = tile_at(scene, tile_index);
    s32 x = offset_x + (s.x() - scroll_x);
    s32 y = offset_y + (s.y() - scroll_y);
    if (s.anim() != -1) {
      const u8* a = anim_at(scene, s.anim());
      x += rd16(a + 0xC);
      y += rd16(a + 0xE);
    }
    const int ot = s.layer() + 0x11;
    if (t->prim == 0x64 && (t->flags & 0x18) == 0) {
      if (auto* p = make_sprite(t, x, y, rgb)) {
        add(ot, p);
        add_tpage(ot, tpage[(t->flags & 6) >> 1][page], true);
      }
    } else {
      auto* p = field_screen()->alloc_prims<psyq::POLY_FT4>();
      if (!p) continue;
      psyq::setlen(p, 9);
      p->code = (tile_at(scene, s.tile())->flags & 1) ? 0x2E : 0x2C;
      p->r0 = rgb[0], p->g0 = rgb[1], p->b0 = rgb[2];
      p->tpage = static_cast<u16>((t->flags & 1) == 0 ? tpage[0][page] : tpage[(t->flags & 6) >> 1][page]);
      p->clut = tile_clut(t);
      const int w = (t->w * t->prim) / 100, h = (t->h * t->prim) / 100;
      p->x0 = static_cast<s16>(x), p->y0 = static_cast<s16>(y);
      p->x1 = static_cast<s16>(x + w), p->y1 = static_cast<s16>(y);
      p->x2 = static_cast<s16>(x), p->y2 = static_cast<s16>(y + h);
      p->x3 = static_cast<s16>(x + w), p->y3 = static_cast<s16>(y + h);
      if (t->flags & 8) {  // flip x
        const s16 x0 = p->x0, x2 = p->x2;
        p->x0 = static_cast<s16>(p->x1 - 1);
        p->x1 = static_cast<s16>(x0 - 1);
        p->x2 = static_cast<s16>(p->x3 - 1);
        p->x3 = static_cast<s16>(x2 - 1);
      }
      if (t->flags & 0x10) {  // flip y
        std::swap(p->y0, p->y2);
        std::swap(p->y1, p->y3);
      }
      set_ft4_uvs(p, t->u, t->v, t->w, t->h);
      add(ot, p);
    }
  }

  // Parallax sprites: layers at +0x74 offset them by a percentage of the scroll.
  const int layers = rdu16(scene + 0x74);
  if (layers != 0) {
    u8* layer_recs = scene + rd32(scene + 0x90);
    const s32 cx = (rdu16(scene + 0x34) - 0x140) / 2, cy = (rdu16(scene + 0x36) - 0xF0) / 2;
    s32 dx = cx - scroll_x, dy = cy - scroll_y;
    if (bgg.parallax_x == 30000) {
      for (int l = 0; l < layers; ++l) {
        u8* L = layer_recs + l * 0x10;
        wr16(L + 0xC, rd16(L + 4) + (dx * rd16(L + 8)) / 100);
        wr16(L + 0xE, rd16(L + 6) + (dy * rd16(L + 0xA)) / 100);
      }
    } else {
      if (bgg.parallax_x > 10000) dx = cx - 20000;
      if (bgg.parallax_y > 10000) dy = cy - 20000;
      for (int l = 0; l < layers; ++l) {
        u8* L = layer_recs + l * 0x10;
        wr16(L + 0xC, bgg.parallax_x + rd16(L + 4) + dx);
        wr16(L + 0xE, bgg.parallax_y + rd16(L + 6) + dy);
      }
    }
    const int count = rdu16(scene + 0x5E);
    for (int n = 0; n < count; ++n, ++index) {
      const SpriteRec s{records + index * 20};
      s16 px = 0, py = 0;
      for (int l = 0; l < layers; ++l) {
        const u8* L = layer_recs + l * 0x10;
        if (L[0] == s.layer()) {
          px = static_cast<s16>(cx - rd16(L + 0xC));
          py = static_cast<s16>(cy - rd16(L + 0xE));
          break;
        }
      }
      if (!(s.x_min() < px && px < s.x_max() && s.y_min() < py && py < s.y_max())) continue;
      u8 rgb[3];
      u16 tile_index;
      u8 page;
      sprite_colour(this, scene, s, rgb, tile_index, page);
      if (tint_on) apply_tint(this, rgb);
      const Tile* t = tile_at(scene, tile_index);
      s32 x = offset_x + (s.x() - px), y;
      if (s.anim() == -1) {
        y = offset_y + (s.y() - py);
      } else {
        const u8* a = anim_at(scene, s.anim());
        x += rd16(a + 0xC);
        y = rd16(a + 0xE) + offset_y + (s.y() - py);
      }
      const int ot = s.layer() + 0x11;
      if (auto* p = make_sprite(t, x, y, rgb)) {
        add(ot, p);
        add_tpage(ot, tpage[(t->flags & 6) >> 1][page], false);
      }
    }
  } else {
    index += rdu16(scene + 0x5E);
  }

  // Wrap-around scrolling windows and their sprites (+0x76 / +0x60).
  const int windows = rdu16(scene + 0x76);
  if (windows != 0 && bgg.windows_on) {
    u8* win = scene + rd32(scene + 0x94);
    Screen* screen = field_screen();
    const s16 draw_y = static_cast<s16>(screen->current == &screen->buffers[0] ? 0 : 0xE0);  // 0x80075730[buffer]
    for (int k = 0; k < windows; ++k) {
      u8* W = win + k * 0x24;
      const s32 w = (rd16(W + 0x10) - rd16(W + 0xC)) + 1;
      const s32 h = (rd16(W + 0x12) - rd16(W + 0xE)) + 1;
      auto advance = [](u8* phase, u8* acc, s32 speed, s32 span, u8 mode) {
        if (mode == 0) {
          wr16(phase, (rd16(phase) + speed) % span);
        } else {
          if (speed != 0) {
            const s32 a = speed < 0 ? -speed : speed;
            if (a <= rd16(acc)) {
              wr16(phase, rd16(phase) + (speed < 0 ? -1 : 1));
              wr16(acc, rd16(acc) - a);
              wr16(phase, rd16(phase) % span);
            }
          }
          wr16(acc, rd16(acc) + 1);
        }
      };
      advance(W + 0x1C, W + 0x20, rd32(W + 4), w, W[1]);
      advance(W + 0x1E, W + 0x22, rd32(W + 8), h, W[2]);
      const s16 sx = static_cast<s16>(scroll_x - rd16(W + 0x1C));
      const s16 sy = static_cast<s16>(scroll_y - rd16(W + 0x1E));
      // The clip rectangle (both coordinates are taken from +0xe, as the original does).
      wr16(W + 0x14, rd16(W + 0xE) - sx);
      wr16(W + 0x18, w);
      wr16(W + 0x1A, h);
      wr16(W + 0x16, (rd16(W + 0xE) + draw_y) - sy);
      if (rd16(W + 0x14) < 0) {
        wr16(W + 0x18, rd16(W + 0x18) + rd16(W + 0x14));
        wr16(W + 0x14, 0);
      }
      if (0x140 < rd16(W + 0x14) + rd16(W + 0x18)) wr16(W + 0x18, 0x140 - rd16(W + 0x14));
      if (rd16(W + 0x16) < draw_y) {
        wr16(W + 0x1A, rd16(W + 0x1A) - (draw_y - rd16(W + 0x16)));
        wr16(W + 0x16, draw_y);
      }
      if (draw_y + 0xE0 < rd16(W + 0x16) + rd16(W + 0x1A))
        wr16(W + 0x1A, rd16(W + 0x1A) + (draw_y - (rd16(W + 0x16) + rd16(W + 0x1A) - 0xE0)));
    }
    const int count = rdu16(scene + 0x60);
    s16 sx = 0, sy = 0;
    for (int n = 0; n < count; ++n, ++index) {
      const SpriteRec s{records + index * 20};
      int k = 0;
      for (k = 0; k < windows; ++k) {
        const u8* W = win + k * 0x24;
        if (W[0] == s.layer()) {
          sx = static_cast<s16>(scroll_x - rd16(W + 0x1C));
          sy = static_cast<s16>(scroll_y - rd16(W + 0x1E));
          break;
        }
      }
      const u8* W = win + (k < windows ? k : windows - 1) * 0x24;
      const s32 x0 = rd16(W + 0xC), y0 = rd16(W + 0xE), x1 = rd16(W + 0x10), y1 = rd16(W + 0x12);
      bool visible = false;
      {
        const s32 mx = (x1 + x0 + 1) / 2 - 0xA0;
        const s32 d = mx - scroll_x < 0 ? scroll_x - mx : mx - scroll_x;
        if (d < ((x1 - x0) + 0x141) >> 1) {
          const s32 half_h = ((y1 - y0) + 0xF1) >> 1;
          const s32 my = (y0 + y1 + 1) / 2 - 0x78;
          const s32 e = my - scroll_y;
          visible = e < 0 ? (scroll_y - my < half_h) : (e < half_h);
        }
      }
      if (!visible) continue;
      const psyq::RECT full{0, draw_y, 0x140, 0xE0};
      if (auto* area = field_screen()->alloc_prims<psyq::DR_AREA>()) {
        psyq::SetDrawArea(area, &full);
        add(s.layer() + 0x11, area);
      }
      u8 rgb[3];
      u16 tile_index;
      u8 page;
      sprite_colour(this, scene, s, rgb, tile_index, page);
      if (tint_on) apply_tint(this, rgb);
      const Tile* t = tile_at(scene, tile_index);
      s32 x = offset_x + (s.x() - sx), y;
      if (s.anim() == -1) {
        y = offset_y + (s.y() - sy);
      } else {
        const u8* a = anim_at(scene, s.anim());
        x += rd16(a + 0xC);
        y = rd16(a + 0xE) + offset_y + (s.y() - sy);
      }
      Tile clipped = *t;
      clipped.clut_x &= 0x3F;
      auto* base = make_sprite(&clipped, x, y, rgb);
      if (!base) continue;
      const int ot = s.layer() + 1;
      add(ot, base);
      const s16 span_x = static_cast<s16>((x1 - x0) + 1), span_y = static_cast<s16>((y1 - y0) + 1);
      auto copy = [&](s32 ox, s32 oy) {
        auto* c = field_screen()->alloc_prims<psyq::SPRT>();
        if (!c) return;
        std::memcpy(reinterpret_cast<u8*>(c) + sizeof(psyq::P_TAG), reinterpret_cast<u8*>(base) + sizeof(psyq::P_TAG),
                    sizeof(psyq::SPRT) - sizeof(psyq::P_TAG));
        psyq::setlen(c, 4);
        c->x0 = static_cast<s16>(base->x0 + ox);
        c->y0 = static_cast<s16>(base->y0 + oy);
        add(ot, c);
      };
      u32 edges = 0;
      if (s.x() + rd16(W + 0x1C) < x0) {
        copy(span_x, 0);
        edges = 0x1000;
      } else if (x1 < s.x() + base->w + rd16(W + 0x1C)) {
        copy(-span_x, 0);
        edges = 0x10;
      }
      if (s.y() + rd16(W + 0x1E) < y0) {
        copy(0, span_y);
        edges |= 0x100;
      } else if (y1 < s.y() + base->h + rd16(W + 0x1E)) {
        copy(0, -span_y);
        edges |= 1;
      }
      if (edges == 0x1100) copy(span_x, span_y);
      if (edges == 0x1001) copy(span_x, -span_y);
      if (edges == 0x110) copy(-span_x, span_y);
      if (edges == 0x11) copy(-span_x, -span_y);
      add_tpage(ot, tpage[(t->flags & 6) >> 1][page], false);
      const psyq::RECT clip{rd16(W + 0x14), rd16(W + 0x16), rd16(W + 0x18), rd16(W + 0x1A)};
      if (auto* area = field_screen()->alloc_prims<psyq::DR_AREA>()) {
        psyq::SetDrawArea(area, &clip);
        add(s.layer() + 0x11, area);
      }
    }
  }
}

}  // namespace ss::game::field
