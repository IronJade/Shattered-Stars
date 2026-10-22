// The field's visual effects, part 1 (overlay #3101): GlowFx, the particle pool, its OT
// layers and the spawners the scripts call. The behaviours and drawing helpers are in
// effects_draw.cpp; the field side (SceneRenderer, splicing) in scene_renderer.cpp.
#include "game/field/effects.h"

#include <algorithm>
#include <vector>

#include "game/common/math.h"
#include "game/field/effects_internal.h"
#include "game/field/field.h"
#include "game/field/scene_renderer.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

FxState g_fx;
GlowFx* g_glow = nullptr;             // 0x800752dc
std::vector<GlowFx*> g_orphan_glows;  // replaced by a second ext 0x79 but still registered

s32 mul(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }

void copy4(s32 dst[4], const s32 src[4]) { std::copy(src, src + 4, dst); }

}  // namespace

FxState& fx_state() { return g_fx; }

// ---- GlowFx ------------------------------------------------------------------------

GlowFx* glow_create_default(GlowFx* self) {
  self->frames = 1;
  self->timer = 0;
  self->rgb[0] = self->rgb[1] = self->rgb[2] = 0x80;
  task_register(self, 1);
  return self;
}

GlowFx* glow_create(GlowFx* self, int r, int g, int b, int frames) {
  if (frames < 0) frames = 1;
  self->rgb[0] = r;
  self->frames = frames;
  self->rgb[1] = g;
  self->rgb[2] = b;
  self->timer = 0;
  task_register(self, 1);
  return self;
}

void glow_destroy(GlowFx* self, int flags) {
  task_unregister(self);
  if ((flags & 1) != 0) delete self;
}

void GlowFx::update() { glow_draw(this); }

void glow_draw(GlowFx* self) {
  Screen* screen = field_screen();
  if (screen == nullptr || screen->current == nullptr) return;
  if (self->frames < self->timer) self->timer = self->frames;
  const s32 t = self->timer;
  // A frames of 0 (ext 0x79 with 0) divides by zero on the console (break 7).
  const s32 frames = self->frames != 0 ? self->frames : 1;
  auto level = [&](s32 c) {
    s32 v = (c * t) / frames;
    if (0x100 < v) v = 0xFF;  // 0x100 itself wraps to 0 in the byte
    return static_cast<u32>(static_cast<u8>(v));
  };
  const u32 colour = level(self->rgb[0]) | level(self->rgb[1]) << 8 | level(self->rgb[2]) << 16;
  self->timer = t + globals().frame_ticks;
  psyq::P_TAG* entry = screen->ot_entry(0x110);
  for (int i = 0; i < 0x18;) {
    const s32 a0 = (i << 12) / 0x18;
    const s32 c0 = cos4096(static_cast<u32>(a0)), s0 = sin4096(static_cast<u32>(a0));
    ++i;
    const s32 a1 = (i * 0x1000) / 0x18;
    const s32 c1 = cos4096(static_cast<u32>(a1)), s1 = sin4096(static_cast<u32>(a1));
    auto* p = screen->alloc_prims<psyq::POLY_G4>();
    if (p == nullptr) return;
    u32* w = fx::words(p);
    w[0] = 0x3A000000u;  // black, POLY_G4 semi-transparent
    fx::put_xy(p, 1, static_cast<s16>(static_cast<s16>(mul(c0, 0x76) >> 12) + 0xA0),
               static_cast<s16>(static_cast<s16>(mul(s0, 0x4B) >> 12) + 0x78));
    w[2] = 0;
    fx::put_xy(p, 3, static_cast<s16>(static_cast<s16>(mul(c1, 0x76) >> 12) + 0xA0),
               static_cast<s16>(static_cast<s16>(mul(s1, 0x4B) >> 12) + 0x78));
    w[4] = colour;
    fx::put_xy(p, 5, static_cast<s16>(static_cast<s16>(mul(c0, 0xEC) >> 12) + 0xA0),
               static_cast<s16>(static_cast<s16>(mul(s0, 0x96) >> 12) + 0x78));
    w[6] = colour;
    fx::put_xy(p, 7, static_cast<s16>(static_cast<s16>(mul(c1, 0xEC) >> 12) + 0xA0),
               static_cast<s16>(static_cast<s16>(mul(s1, 0x96) >> 12) + 0x78));
    psyq::setlen(p, 8);
    psyq::addPrim(entry, p);
  }
  // DR_TPAGE 0xe1000220 (abr 1, dither) plus a NOP word, linked last so it runs first.
  auto* mode = screen->alloc_prims<psyq::DR_MODE>();
  if (mode == nullptr) return;
  mode->code[0] = 0xE1000220u;
  mode->code[1] = 0;
  psyq::setlen(mode, 2);
  psyq::addPrim(entry, mode);
}

GlowFx* field_glow() { return g_glow; }

GlowFx* glow_spawn(int r, int g, int b, int frames) {
  if (g_glow != nullptr) g_orphan_glows.push_back(g_glow);
  g_glow = glow_create(new GlowFx, r, g, b, frames);
  return g_glow;
}

void glow_destroy_current() {
  if (g_glow == nullptr) return;
  glow_destroy(g_glow, 3);
  g_glow = nullptr;
}

void glow_destroy_all() {
  glow_destroy_current();
  for (GlowFx* glow : g_orphan_glows) glow_destroy(glow, 3);
  g_orphan_glows.clear();
}

// ---- Pool and layers ---------------------------------------------------------------

void fx_pool_create() {
  FxState& s = g_fx;
  s.count = kFxPoolSize;
  s.pool.reset();  // object_delete of a previous pool
  s.pool = std::make_unique<Particle[]>(kFxPoolSize);
  for (int i = 0; i < kFxPoolSize; ++i) {
    fx_particle_clear(&s.pool[i]);
    s.pool[i].type = 0;
  }
}

void fx_particle_clear(Particle* p) {
  for (int i = 0; i < 3; ++i) p->pos[i] = p->vel[i] = p->tgt[i] = 0;
  p->type = 0;
}

Particle* fx_particle_alloc(u16 type) {
  FxState& s = g_fx;
  if (s.count == 0) fx_pool_create();
  for (int i = 0; i < kFxPoolSize; ++i) {
    Particle* p = &s.pool[i];
    if (p->type == 0) {
      fx_particle_reset(p);
      p->type = type;
      return p;
    }
  }
  return nullptr;
}

OtLayer* fx_layer_get(u8 id) {
  FxState& s = g_fx;
  for (;;) {
    for (int i = 0; i < 4; ++i)
      if (s.layer[i] && s.layer_id[i] == id) return s.layer[i].get();
    if (fx_layer_alloc(id) != 1) return nullptr;
  }
}

int fx_layer_alloc(u8 id) {
  FxState& s = g_fx;
  for (int i = 0; i < 4; ++i) {
    if (s.layer[i]) continue;
    s.layer[i] = std::make_unique<OtLayer>();
    s.layer[i]->half = 0;
    s.layer_id[i] = id;
    s.layer[i]->dirty = 0;
    ot_layer_clear(s.layer[i]->current());
    return 1;
  }
  return 0;
}

void fx_particle_reset(Particle* p) {
  p->p38 = p->p3c = p->p40 = p->p44 = p->p48 = 0;
  for (int i = 0; i < 3; ++i) p->vel[i] = p->tgt[i] = 0;
}

void fx_particle_update(Particle* p) {
  p->layer->dirty = 1;
  switch (p->type) {
    case 1: fx_streak_update(p); break;
    case 2: fx_spark_update(p); break;
    case 3: fx_ring_update(p); break;
    case 4: fx_spinner_update(p); break;
    case 5: fx_orb_update(p); break;
    case 6: fx_beam_update(p); break;
    case 7: fx_triangle_update(p); break;
    case 8: fx_burst_update(p); break;
    case 9: fx_ring_textured_update(p); break;
    default: break;  // the table has 9 entries
  }
}

void fx_particle_free(Particle* p) {
  if (p != nullptr) p->type = 0;
}

// ---- Spawners ----------------------------------------------------------------------

Particle* fx_spawn_streak(u8 layer, const s32 from[4], s32 to[4], s32 c0, s32 c1, s32 len, s32 speed, s32 accel,
                          s32 no_extend) {
  Particle* p = fx_particle_alloc(1);
  if (p == nullptr) return nullptr;
  if (no_extend == 0) {
    // Push 'to' further along the line by len: dir (from - to) / distance, times
    // (len >> 12) / (distance << 12).
    s32 d[3], sq[3];
    for (int i = 0; i < 3; ++i) d[i] = (to[i] - from[i]) >> 12;
    fx::ld_ir(d[0], d[1], d[2]);
    fx::run(fx::G::kSqr0);
    for (int i = 0; i < 3; ++i) sq[i] = fx::mac(i + 1);
    const s32 dist = static_cast<s32>(static_cast<u32>(SquareRoot0(sq[0] + sq[1] + sq[2])) << 12);
    fx::ld_ir(d[0], d[1], d[2]);
    fx::run(fx::G::kSqr0);
    for (int i = 0; i < 3; ++i) sq[i] = fx::mac(i + 1);
    s32 n = static_cast<s32>(static_cast<u32>(SquareRoot0(sq[0] + sq[1] + sq[2])) << 12) >> 12;
    if (n == 0) n = 1;
    s32 dir[3];
    for (int i = 0; i < 3; ++i) dir[i] = (from[i] - to[i]) / n;
    const s32 k = len >> 12;
    if (dist != 0) {  // the console traps on 0 (break 7)
      for (int i = 0; i < 3; ++i) to[i] += mul(dir[i], k) / dist;
    }
  }
  p->layer = fx_layer_get(layer);
  copy4(p->pos, from);
  copy4(p->tgt, to);
  p->p38 = accel;
  p->p3c = c0;
  p->p40 = c1;
  p->p44 = speed;
  p->p48 = len;
  return p;
}

Particle* fx_spawn_spark(u8 layer, const s32 pos[4], const s32 vel[4], s32 c0, s32 c1, s32 len, s32 life) {
  Particle* p = fx_particle_alloc(2);
  if (p == nullptr) return nullptr;
  p->layer = fx_layer_get(layer);
  copy4(p->pos, pos);
  copy4(p->vel, vel);
  p->p3c = c0;
  p->p40 = c1;
  p->p44 = life;
  p->p48 = len;
  return p;
}

Particle* fx_spawn_beam(u8 layer, const s32 pos[4], const s32 to[4], s32 c0, s32 c1, s32 life, s32 fade) {
  Particle* p = fx_particle_alloc(6);
  if (p == nullptr) return nullptr;
  p->layer = fx_layer_get(layer);
  copy4(p->pos, pos);
  p->tgt[0] = (to[0] - p->pos[0]) >> 12;  // end point relative to pos, map units
  p->tgt[1] = (to[1] - p->pos[1]) >> 12;
  p->p38 = life;
  p->p44 = c0;
  p->p48 = c1;
  p->vel[0] = fade;
  p->tgt[2] = (to[2] - p->pos[2]) >> 12;
  return p;
}

Particle* fx_spawn_ring(u8 layer, const s32 pos[4], s32 segments, s32 width, s32 grow, s32 life, s32 c_outer,
                        s32 c_inner, s32 radius, s32 tilt) {
  Particle* p = fx_particle_alloc(3);
  if (p == nullptr) return nullptr;
  p->layer = fx_layer_get(layer);
  copy4(p->pos, pos);
  p->p38 = segments;
  p->p3c = width;
  p->p40 = grow;
  p->p44 = life;
  p->p48 = radius;
  p->tgt[0] = c_outer;
  p->tgt[1] = c_inner;
  p->tgt[2] = tilt;
  return p;
}

Particle* fx_spawn_orb(u8 layer, const s32 pos[4], s32 lat, s32 lon, s32 grow, s32 life, s32 rgb, s32 radius) {
  Particle* p = fx_particle_alloc(5);
  if (p == nullptr) return nullptr;
  p->layer = fx_layer_get(layer);
  copy4(p->pos, pos);
  p->p38 = lat;
  p->p3c = lon;
  p->p40 = grow;
  p->p44 = life;
  p->p48 = radius;
  p->tgt[0] = rgb;
  return p;
}

Particle* fx_spawn_triangle(u8 layer, const s32 p0[4], const s32 p1[4], const s32 p2[4], s32 c0, s32 c1, s32 c2,
                            s32 life, s16 fade) {
  Particle* p = fx_particle_alloc(7);
  if (p == nullptr) return nullptr;
  p->layer = fx_layer_get(layer);
  copy4(p->pos, p0);
  for (int i = 0; i < 3; ++i) p->vel[i] = (p1[i] - p0[i]) >> 12;  // vertex offsets, map units
  p->tgt[0] = (p2[0] - p0[0]) >> 12;
  p->tgt[1] = (p2[1] - p0[1]) >> 12;
  p->p38 = life;
  p->p3c = c0;
  p->p40 = c1;
  p->p44 = c2;
  p->flag = fade;
  p->tgt[2] = (p2[2] - p0[2]) >> 12;
  return p;
}

Particle* fx_spawn_burst(u8 layer, const s32 pos[4], s32 frames) {
  Particle* p = fx_particle_alloc(8);
  if (p == nullptr) return nullptr;
  OtLayer* l = fx_layer_get(layer);
  if (frames == 0) {  // the console traps (break 7)
    runtime::Runtime::get().log_once("fx:burst0", "fx_spawn_burst: 0 frames (the original divides by zero)");
    frames = 1;
  }
  p->layer = l;
  copy4(p->pos, pos);
  p->p38 = 0;  // spin angle
  p->p3c = 0;  // age
  p->p40 = 0;  // fade
  p->p44 = 0x180000 / frames;
  return p;
}

Particle* fx_spawn_spinner(u8 layer, const s32 pos[4], s32 a38, s32 a3c, s32 a40, s32 a28, s32 a30, s32 rgb,
                           s32 life) {
  Particle* p = fx_particle_alloc(4);
  if (p == nullptr) return nullptr;
  p->layer = fx_layer_get(layer);
  copy4(p->pos, pos);
  p->p38 = a38;
  p->p3c = a3c;
  p->p40 = a40;
  p->p44 = rgb;
  p->p48 = life;
  p->tgt[0] = a28;
  p->tgt[2] = a30;
  return p;
}

Particle* fx_spawn_ring_textured(u8 layer, const s32 pos[4], s32 segments, s32 width, s32 height, s32 grow,
                                 s32 life, s32 clut, s32 uv, s32 radius) {
  Particle* p = fx_particle_alloc(9);
  if (p == nullptr) return nullptr;
  p->layer = fx_layer_get(layer);
  copy4(p->pos, pos);
  p->p38 = segments;
  p->p3c = clut;
  p->p40 = grow;
  p->p44 = life;
  p->p48 = radius;
  p->tgt[0] = width;
  p->tgt[1] = height;
  p->tgt[2] = uv;
  p->p4c = 0;
  return p;
}

}  // namespace ss::game::field
