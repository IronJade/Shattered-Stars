// Resident effect helpers of the spells. See spell_fx.h.

#include "game/battle/spell_fx.h"

#include <cstring>
#include <vector>

#include "game/battle/hud.h"
#include "game/battle/render.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/common/screen.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"

namespace ss::game::battle {
namespace {

// ScreenFade (0x1c bytes, vtable 0x8006b878).
struct ScreenFade : Task {
  u32 rgb = 0;          // +0x08
  s32 tpage = 0;        // +0x0c
  s32 step = 0;         // +0x10
  s32 hold = 0;         // +0x14
  RenderContext layer;  // +0x18 (a one-entry OT, a 0x40-byte arena)

  ~ScreenFade() override;
  void update() override;  // [1] screen_fade_update 0x8003c4f4
};
std::vector<ScreenFade*> g_fades;  // (port) the live fades, freed at the battle's end

ScreenFade::~ScreenFade() {
    // screen_fade_destroy 0x8003c464.
    for (size_t i = 0; i < g_fades.size(); ++i)
      if (g_fades[i] == this) g_fades.erase(g_fades.begin() + static_cast<long>(i));
    task_unregister(this);
    ot_layer_remove(&layer);
    if (layer.arena >= 0) screen_destroy_arena(layer.arena);
    if (layer.ot >= 0) screen_destroy_ot(layer.ot);
}

void ScreenFade::update() {
  BattleGlobals& g = bg();
  auto* t = screen_alloc_prims<psyq::TILE>(layer.arena, 1);
  if (t == nullptr) return;
  psyq::setTile(t);
  t->code = 0x62;
  t->r0 = static_cast<u8>(rgb);
  t->g0 = static_cast<u8>(rgb >> 8);
  t->b0 = static_cast<u8>(rgb >> 16);
  t->x0 = 0;
  t->y0 = 0;
  t->w = 0x140;
  t->h = 0xE8;
  psyq::addPrim(layer.ot_head, t);
  auto* m = screen_alloc_prims<psyq::DR_MODE>(layer.arena, 1);
  if (m == nullptr) return;
  psyq::SetDrawMode(m, 0, 1, tpage, nullptr);
  psyq::addPrim(layer.ot_head, m);
  if ((g.flags & 4) != 0) return;
  if (hold != 0 && 0 <= step) {
    hold -= g.frame_ticks;
    if (hold < 0) hold = 0;
    return;
  }
  const s32 d = step * g.frame_ticks;
  auto chan = [d](u32 v) {
    s32 c = static_cast<s32>(v & 0xFF) - d;
    if (c < 0) c = 0;
    if (0xFF < c) c = 0xFF;
    return static_cast<u32>(c);
  };
  rgb = chan(rgb) | chan(rgb >> 8) << 8 | chan(rgb >> 16) << 16;
  if (step < 0 && rgb == 0xFFFFFF) step = -step;
  if (rgb == 0) delete this;  // (out of the task list first: safe from its own update)
}

}  // namespace

Task* screen_fade_create(u32 rgb, int tpage, int step, int hold) {
  BattleGlobals& g = bg();
  ScreenFade* f = new ScreenFade();
  f->rgb = rgb;
  f->tpage = tpage;
  f->step = step;
  f->hold = hold;
  g_fades.push_back(f);
  task_register(f, 0);
  render_ctx_create_ot(&f->layer, 1);
  render_ctx_create_arena(&f->layer, 0x40);
  ot_layer_add(&f->layer);
  ot_layer_to_front(&f->layer);
  if (BattleHud* h = battle_hud()) ot_layer_to_front(&h->ctx);
  ot_layer_to_front(g.prim_pool_b);
  return f;
}

Shot* effect_spawn_t31_at(const s32 pos[3], s32 rings, s32 segments, s32 grow, s32 life, u32 rgb_a, u32 rgb_b,
                          s32 width, s32 step, s16 flag) {
  Shot* s = shot_spawn(0x31);
  if (s == nullptr) return nullptr;
  s->pos[0] = pos[0], s->pos[1] = pos[1], s->pos[2] = pos[2];
  s->p128 = rings;
  s->p12c = segments;
  s->p130 = grow;
  s->p134 = life;
  s->p138 = width;
  s->vel[0] = step;
  s->point[0] = static_cast<s32>(rgb_a);
  s->point[1] = static_cast<s32>(rgb_b);
  s->power = flag;
  return s;
}

void spell_fx_reset() {
  while (!g_fades.empty()) delete g_fades.back();
}

void stage_random_point(s32 out[4], int shrink) {
  const StageData& st = stage();
  out[0] = rng_between(st.bounds[0] / shrink, st.bounds[2] / shrink);
  out[2] = rng_between(st.bounds[1] / shrink, st.bounds[3] / shrink);
  // (The original passes the 20.12 coordinates where whole units belong.)
  out[1] = stage_ground_height(out[0], out[2]);
}

void model_draw(const u8* model, u16 clut, u16 tpage, s32 ot_bias) {
  ViewState& v = view();
  const u16 saved = v.cull_threshold;
  v.cull_threshold = 0xFFFF;
  model_render(bg().prim_pool_a, model, clut, tpage, false, ot_bias);
  v.cull_threshold = saved;
}

void model_draw_culled(const u8* model, u16 clut, u16 tpage, s32 ot_bias) {
  ViewState& v = view();
  const u16 saved = v.cull_threshold;
  v.cull_threshold = 0;
  model_render(bg().prim_pool_a, model, clut, tpage, false, ot_bias);
  v.cull_threshold = saved;
}

void draw_star(s32 r_a, s32 r_b, s16 h, u32 rgb_a, u32 rgb_b) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  if (!arena_has_room(pool, 0x280)) return;
  psx::Gte& q = psyq::gte();
  auto xy = [](s32 x, s32 y) { return static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16; };
  s32 pc = 0x1000, ps = 0;
  s32 deg = 0x24000;
  for (int i = 0; i < 10; ++i) {
    const s32 c = bcos(static_cast<u32>(deg / 0x168)), sn = bsin(static_cast<u32>(deg / 0x168));
    const s32 ra = (i & 1) == 0 ? r_a : r_b;
    const s32 rb = (i & 1) == 0 ? r_b : r_a;
    const u32 v1 = xy((pc * ra) >> 12, (ps * ra) >> 12);
    const u32 v2 = xy((c * rb) >> 12, (sn * rb) >> 12);
    for (int k = 0; k < 2; ++k) {
      q.write_data(psx::Gte::VXY0, 0);
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(static_cast<s16>(k == 0 ? h : -h))));
      q.write_data(psx::Gte::VXY1, v1);
      q.write_data(psx::Gte::VZ1, 0);
      q.write_data(psx::Gte::VXY2, v2);
      q.write_data(psx::Gte::VZ2, 0);
      q.execute(psx::Gte::kRtpt);
      const u32 s0 = q.read_data(psx::Gte::SXY0), s1 = q.read_data(psx::Gte::SXY1), s2 = q.read_data(psx::Gte::SXY2);
      q.execute(psx::Gte::kAvsz3);
      const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ)) >> 3;
      if (0x5F9 < otz) continue;
      auto* t = screen_alloc_prims<psyq::POLY_F3>(pool->arena, 1);
      auto* m = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
      if (t == nullptr || m == nullptr) return;
      psyq::setPolyF3(t);
      const u32 rgb = (i & 1) == 0 ? rgb_a : rgb_b;
      t->r0 = static_cast<u8>(rgb), t->g0 = static_cast<u8>(rgb >> 8), t->b0 = static_cast<u8>(rgb >> 16);
      t->code = 0x22;
      t->x0 = static_cast<s16>(s0 & 0xFFFF), t->y0 = static_cast<s16>(s0 >> 16);
      t->x1 = static_cast<s16>(s1 & 0xFFFF), t->y1 = static_cast<s16>(s1 >> 16);
      t->x2 = static_cast<s16>(s2 & 0xFFFF), t->y2 = static_cast<s16>(s2 >> 16);
      psyq::P_TAG* ot = &pool->ot_head[0x616 - otz];
      psyq::addPrim(ot, t);
      psyq::SetDrawMode(m, 0, 1, 0x20, nullptr);
      psyq::addPrim(ot, m);
      pool_a_bytes_used() += 0x20;
    }
    pc = c;
    ps = sn;
    deg += 0x24000;
  }
}

void draw_quad_g4(const s16 pts[4][4], const u32 colours[4], int flat) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  auto* p = screen_alloc_prims<psyq::POLY_G4>(pool->arena, 1);
  if (p == nullptr) return;
  psx::Gte& q = psyq::gte();
  auto xy = [](const s16* v) { return static_cast<u32>(static_cast<u16>(v[0])) | static_cast<u32>(static_cast<u16>(v[1])) << 16; };
  q.write_data(psx::Gte::VXY0, xy(pts[0]));
  q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(pts[0][2])));
  q.execute(psx::Gte::kRtps);
  const u32 s0 = q.read_data(psx::Gte::SXY2);
  q.write_data(psx::Gte::VXY0, xy(pts[1]));
  q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(pts[1][2])));
  q.write_data(psx::Gte::VXY1, xy(pts[2]));
  q.write_data(psx::Gte::VZ1, static_cast<u32>(static_cast<s32>(pts[2][2])));
  q.write_data(psx::Gte::VXY2, xy(pts[3]));
  q.write_data(psx::Gte::VZ2, static_cast<u32>(static_cast<s32>(pts[3][2])));
  q.execute(psx::Gte::kRtpt);
  const u32 s1 = q.read_data(psx::Gte::SXY0), s2 = q.read_data(psx::Gte::SXY1), s3 = q.read_data(psx::Gte::SXY2);
  q.execute(psx::Gte::kAvsz4);
  s32 otz = (static_cast<s32>(q.read_data(psx::Gte::OTZ)) >> 3) - 5;
  psyq::setPolyG4(p);
  std::memcpy(&p->r0, &colours[0], 4);
  std::memcpy(&p->r1, &colours[1], 3);
  std::memcpy(&p->r2, &colours[2], 3);
  std::memcpy(&p->r3, &colours[3], 3);
  p->x0 = static_cast<s16>(s0 & 0xFFFF), p->y0 = static_cast<s16>(s0 >> 16);
  p->x1 = static_cast<s16>(s1 & 0xFFFF), p->y1 = static_cast<s16>(s1 >> 16);
  p->x2 = static_cast<s16>(s2 & 0xFFFF), p->y2 = static_cast<s16>(s2 >> 16);
  p->x3 = static_cast<s16>(s3 & 0xFFFF), p->y3 = static_cast<s16>(s3 >> 16);
  pool_a_bytes_used() += 0x24;
  if (0x5FF < otz) return;
  if (otz < 0 || flat != 0) otz = 0;
  psyq::P_TAG* ot = &pool->ot_head[0x610 - otz];
  psyq::addPrim(ot, p);
  if ((colours[0] & 0x2000000) == 0) return;
  if (auto* m = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1)) {
    psyq::SetDrawMode(m, 0, 1, 0x20, nullptr);
    psyq::addPrim(ot, m);
    pool_a_bytes_used() += 0xC;
  }
}

}  // namespace ss::game::battle
