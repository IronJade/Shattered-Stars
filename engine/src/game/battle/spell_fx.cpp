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

Shot* effect_spawn_streak_at(const s32 pos[3], const s32 target[3], u32 rgb_head, u32 rgb_tail, s32 length,
                             s32 speed, s32 accel, s16 puff) {
  Shot* s = shot_spawn(0x12);
  if (s == nullptr) return nullptr;
  s->pos[0] = pos[0], s->pos[1] = pos[1], s->pos[2] = pos[2];
  s->point[0] = target[0];
  s->point[1] = target[1];
  s->p128 = accel;
  s->p12c = static_cast<s32>(rgb_head);
  s->p130 = static_cast<s32>(rgb_tail);
  s->p134 = speed;
  s->p138 = length;
  s->aux = puff;
  s->point[2] = target[2];
  return s;
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

namespace {  // (draw_disc / draw_wave_grid)
s32 mul_lo(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }
u32 svec_xy(s32 x, s32 y) { return static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16; }
}  // namespace

// 0x8007cc5c draw_disc (#1984.0): a flat fan of 'segments' gouraud triangles (POLY_G3) in
// the x-z plane round the current GTE origin, 'radius' units: rgb_centre at the centre (its
// code 0x30, 0x32 when blend is set), rgb_edge on the rim; blend != 0 adds a DR_MODE (mode
// blend - 1) after each. A triangle with no vertex inside x 0..319 / y -64..279, or with
// otz / 8 >= 0x5fa - ot, is skipped; the others go to OT entry 0x616 + ot - otz / 8 of pool
// A. (The console checks room for all of them, takes the arena's cursor and commits what it
// used at the end.)
void draw_disc(s32 segments, s32 radius, u32 rgb_centre, u32 rgb_edge, s32 blend, s32 ot) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  const s32 size = blend != 0 ? 0x28 : 0x1C;
  if (pool == nullptr || !arena_has_room(pool, mul_lo(size, segments))) return;
  const u32 code = rgb_centre | (blend != 0 ? 0x32000000u : 0x30000000u);
  psx::Gte& q = psyq::gte();
  s32 used = 0;
  s32 x = radius, z = 0;
  for (s32 i = 0; i < segments; ++i) {
    const s32 ang = ((i + 1) << 12) / segments;
    const s32 px = x, pz = z;
    x = mul_lo(bcos(static_cast<u32>(ang)), radius) >> 12;
    z = mul_lo(bsin(static_cast<u32>(ang)), radius) >> 12;
    q.write_data(psx::Gte::VXY0, svec_xy(0, 0));
    q.write_data(psx::Gte::VZ0, 0);
    q.write_data(psx::Gte::VXY1, svec_xy(px, 0));
    q.write_data(psx::Gte::VZ1, static_cast<u32>(static_cast<s32>(static_cast<s16>(pz))));
    q.write_data(psx::Gte::VXY2, svec_xy(x, 0));
    q.write_data(psx::Gte::VZ2, static_cast<u32>(static_cast<s32>(static_cast<s16>(z))));
    q.execute(psx::Gte::kRtpt);
    const u32 s0 = q.read_data(psx::Gte::SXY0), s1 = q.read_data(psx::Gte::SXY1), s2 = q.read_data(psx::Gte::SXY2);
    q.execute(psx::Gte::kAvsz3);
    auto x_in = [](u32 xy) { return static_cast<u16>(xy) < 0x140; };
    auto y_in = [](u32 xy) { return static_cast<u16>(static_cast<u16>(xy >> 16) + 0x40) < 0x158; };
    if (!(x_in(s0) || x_in(s1) || x_in(s2))) continue;
    if (!(y_in(s0) || y_in(s1) || y_in(s2))) continue;
    const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ)) >> 3;
    if (!(otz < 0x5FA - ot)) continue;
    auto* p = screen_alloc_prims<psyq::POLY_G3>(pool->arena, 1);
    if (p == nullptr) return;
    psyq::setPolyG3(p);
    std::memcpy(&p->r0, &code, 4);
    std::memcpy(&p->r1, &rgb_edge, 4);
    std::memcpy(&p->r2, &rgb_edge, 4);
    p->x0 = static_cast<s16>(s0), p->y0 = static_cast<s16>(s0 >> 16);
    p->x1 = static_cast<s16>(s1), p->y1 = static_cast<s16>(s1 >> 16);
    p->x2 = static_cast<s16>(s2), p->y2 = static_cast<s16>(s2 >> 16);
    psyq::P_TAG* entry = &pool->ot_head[0x616 + ot - otz];
    psyq::addPrim(entry, p);
    used += 0x1C;
    if (blend != 0) {
      auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
      if (dm == nullptr) return;
      psyq::SetDrawMode(dm, 0, 1, (blend - 1) << 5, nullptr);
      psyq::addPrim(entry, dm);
      used += 0xC;
    }
  }
  pool_a_bytes_used() += used < 4 ? 4 : used;
}

// 0x8007d9e0 draw_wave_grid (#1984.0): an n x n grid
// of screen-space POLY_FT4 over rect {x0, y0, x1, y1} with the UVs of uv {u0, v0, u1, v1}
// spread over it, every vertex row shifted in x by wave_x {amp, phase, freq} (amp * sin(phase
// + freq * row / n) >> 24) and every vertex column in y by wave_y alike; all at OT entry
// 0x610 - ot of pool A, colour word code_rgb.
void draw_wave_grid(const s32 uv[4], const s32 rect[4], const s32 wave_x[3], const s32 wave_y[3], u16 clut,
                    u16 tpage, s32 ot, u32 code_rgb, s32 n) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  // (screen_alloc 0x80069fd4 of n * n * 0x28 bytes at once; none: nothing is drawn.)
  const s32 bytes = mul_lo(n, n * 0x28);
  if (pool == nullptr || !arena_has_room(pool, bytes)) return;
  pool_a_bytes_used() += bytes;
  const s32 u0 = static_cast<s16>(uv[0]);  // (the console keeps uv[0] << 16 and shifts it back)
  const s32 v0 = static_cast<s16>(uv[1]);
  const s32 du = static_cast<s16>(uv[2]) - u0;
  const s32 dv = static_cast<s16>(uv[3]) - v0;
  const s32 w = rect[2] - rect[0];
  const s32 h = rect[3] - rect[1];
  const u32 clut_hi = static_cast<u32>(clut) << 16;
  const u32 tpage_hi = static_cast<u32>(tpage) << 16;
  s32 xd_low = mul_lo(wave_x[0], bsin(static_cast<u32>(wave_x[1]))) >> 24;
  s32 v_low = v0 << 8;
  for (s32 row = 0; row < n; ++row) {
    const s32 v_high = v_low;
    v_low = (mul_lo(dv, row + 1) / n + v0) << 8;
    s32 yd_next = mul_lo(wave_y[0], bsin(static_cast<u32>(wave_y[1]))) >> 24;
    const s32 xd_high = xd_low;
    xd_low = mul_lo(wave_x[0], bsin(static_cast<u32>(mul_lo(wave_x[2], row + 1) / n + wave_x[1]))) >> 24;
    const s32 y_high = mul_lo(h, row) / n + rect[1];
    const s32 y_low = mul_lo(h, row + 1) / n + rect[1];
    s32 u_next = u0;
    for (s32 col = 0; col < n; ++col) {
      const s32 yd = yd_next;
      yd_next = mul_lo(wave_y[0], bsin(static_cast<u32>(mul_lo(wave_y[2], col + 1) / n + wave_y[1]))) >> 24;
      const s32 u = u_next;
      u_next = mul_lo(du, col + 1) / n + u0;
      const s32 x_a = mul_lo(w, col) / n + rect[0];
      const s32 x_b = mul_lo(w, col + 1) / n + rect[0];
      auto* p = screen_alloc_prims<psyq::POLY_FT4>(pool->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyFT4(p);
      std::memcpy(&p->r0, &code_rgb, 4);
      p->x0 = static_cast<s16>(x_a + xd_high), p->y0 = static_cast<s16>(y_high + yd);
      p->x1 = static_cast<s16>(x_b + xd_high), p->y1 = static_cast<s16>(y_high + yd_next);
      p->x2 = static_cast<s16>(x_a + xd_low), p->y2 = static_cast<s16>(y_low + yd);
      p->x3 = static_cast<s16>(x_b + xd_low), p->y3 = static_cast<s16>(y_low + yd_next);
      const u32 uv0 = static_cast<u32>(u | v_high) | clut_hi;
      const u32 uv1 = static_cast<u32>(u_next | v_high) | tpage_hi;
      const u32 uv2 = static_cast<u32>(u | v_low);
      const u32 uv3 = static_cast<u32>(u_next | v_low);
      p->u0 = static_cast<u8>(uv0), p->v0 = static_cast<u8>(uv0 >> 8), p->clut = static_cast<u16>(uv0 >> 16);
      p->u1 = static_cast<u8>(uv1), p->v1 = static_cast<u8>(uv1 >> 8), p->tpage = static_cast<u16>(uv1 >> 16);
      p->u2 = static_cast<u8>(uv2), p->v2 = static_cast<u8>(uv2 >> 8);
      p->u3 = static_cast<u8>(uv3), p->v3 = static_cast<u8>(uv3 >> 8);
      psyq::addPrim(&pool->ot_head[0x610 - ot], p);
    }
  }
}

s32 (*g_stage_height_fn)(s32 x, s32 y, s32 z) = nullptr;  // 0x8006fe04

namespace {  // (model_find_vertices_near)
u32 rd32_le(const u8* p) {
  return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
         static_cast<u32>(p[3]) << 24;
}
}  // namespace

// 0x800627c0 sv_distance_xz: sqrt(dx^2 + dz^2) of two SVECTORs (GTE SQR: 16-bit inputs).
s32 sv_distance_xz(const u8* a, const psyq::SVECTOR& b) {
  const s32 dx = static_cast<s16>(a[0] | a[1] << 8) - b.vx;
  const s32 dz = static_cast<s16>(a[4] | a[5] << 8) - b.vz;
  const psyq::VECTOR in{dx, 0, dz, 0};
  psyq::VECTOR sq{};
  psyq::Square0(&in, &sq);
  return square_root0(static_cast<s32>(static_cast<u32>(sq.vx) + static_cast<u32>(sq.vy) + static_cast<u32>(sq.vz)));
}

// 0x80061694 model_find_vertices_near: walks every part's 20 primitive sections of a battle
// model (the vertices are inline SVECTORs of each primitive) and lists each vertex whose xz
// distance to points[0] is at most 12000, with its distance to each point. Returns the count.
int model_find_vertices_near(std::vector<NearVertex>& out, const std::vector<u8>& model,
                             const psyq::SVECTOR* points, int npoints) {
  // Per section: the first vertex's offset in a primitive, the vertices tested, the stride
  // (section 9 is skipped; section 18 tests 3 of its quads' 4 vertices).
  struct Section {
    u8 first, verts, stride;
  };
  static constexpr Section kSections[20] = {
      {12, 3, 36}, {28, 3, 52}, {20, 3, 44}, {36, 3, 60}, {20, 3, 44}, {36, 3, 60}, {12, 4, 44},
      {36, 4, 68}, {0, 0, 48},  {48, 4, 80}, {20, 4, 52}, {44, 4, 76}, {4, 3, 28},  {4, 3, 36},
      {16, 3, 40}, {24, 3, 48}, {4, 4, 36},  {16, 3, 48}, {16, 4, 48}, {28, 4, 60}};
  out.clear();
  const u8* base = model.data();
  const u8* end = base + model.size();
  const u8* p = base;
  if (model.size() < 4) return 0;  // (port: bounds checks the console does not make)
  const u32 parts = rd32_le(p);
  p += 4;
  NearVertex slot;
  for (u32 part = 0; part < parts; ++part) {
    p += 4;
    for (const Section& s : kSections) {
      if (end - p < 4) return static_cast<int>(out.size());
      const u32 count = rd32_le(p);
      p += 4;
      if (s.verts == 0) {
        if (static_cast<size_t>(end - p) < static_cast<size_t>(count) * s.stride) return static_cast<int>(out.size());
        p += static_cast<size_t>(count) * s.stride;
        continue;
      }
      for (u32 i = 0; i < count && end - p >= s.stride; ++i, p += s.stride) {
        for (int k = 0; k < s.verts; ++k) {
          const u8* vtx = p + s.first + k * 8;
          for (int j = 0; j < npoints; ++j) slot.dist[j] = static_cast<s16>(sv_distance_xz(vtx, points[j]));
          if (slot.dist[0] < 12001) {
            slot.offset = static_cast<s32>(vtx - base) >> 2;
            out.push_back(slot);
          }
        }
      }
    }
  }
  return static_cast<int>(out.size());
}

}  // namespace ss::game::battle
