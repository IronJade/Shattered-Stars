#include "game/battle/fx_draw.h"

#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/loaders.h"
#include "game/battle/render.h"
#include "game/battle/stage.h"
#include "game/battle/unit_move.h"
#include "game/common/screen.h"
#include "game/title/new_game.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"

namespace ss::game::battle {
namespace {

// 32-bit products as the R3000 computes them (mflo).
s32 mul32(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }

u32 svec_xy(s32 x, s32 y) { return static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16; }

// The colour phase folds back at 0x1000 (0..0x1fff -> 0..0xfff..0).
u32 fold(s32 v) {
  v &= 0x1FFF;
  return static_cast<u32>(0xFFF < v ? 0x1FFF - v : v);
}

struct SVec {
  s16 x = 0, y = 0, z = 0;
};

void load(psx::Gte& q, int n, const SVec& v) {
  static const psx::Gte::Data xy[3] = {psx::Gte::VXY0, psx::Gte::VXY1, psx::Gte::VXY2};
  static const psx::Gte::Data z[3] = {psx::Gte::VZ0, psx::Gte::VZ1, psx::Gte::VZ2};
  q.write_data(xy[n], svec_xy(v.x, v.y));
  q.write_data(z[n], static_cast<u32>(static_cast<s32>(v.z)));
}

}  // namespace

void draw_vortex(const VortexParams& p) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  const s32 per_quad = p.blend != 0 ? 0x30 : 0x24;
  if (!arena_has_room(pool, mul32(mul32(per_quad, p.rings), p.segments))) return;
  const u32 code = p.blend != 0 ? 0x3A000000u : 0x38000000u;
  psx::Gte& q = psyq::gte();
  SVec a, b, c, d;  // scratchpad 0x1f8003e0 / 0x3f0 (this segment), 0x3e8 / 0x3f8 (the previous)
  s32 lo = 0, hi = 0x1000;  // ring << 12 and (ring + 1) << 12
  for (s32 ring = 0; ring < p.rings; ++ring, lo += 0x1000, hi += 0x1000) {
    const s32 r0 = square_root0(mul32(lo / p.rings, p.radius) >> 12);
    const s32 r1 = square_root0(mul32(hi / p.rings, p.radius) >> 12);
    a.x = c.x = static_cast<s16>(ring * p.step + p.base);
    b.x = d.x = static_cast<s16>((ring + 1) * p.step + p.base);
    a.y = static_cast<s16>(mul32(r0, p.width << 12) >> 19);
    a.z = 0;
    b.y = static_cast<s16>(mul32(r1, p.width << 12) >> 19);
    b.z = 0;
    const s32 f_in = ((p.rings - ring) << 8) / p.rings;
    const s32 f_out = ((p.rings - ring - 1) << 8) / p.rings;
    u32 c_in = scale_colour(colour_lerp(p.colour_a, p.colour_b, static_cast<s32>(fold(mul32(p.phase_span, lo) / p.rings + p.phase))), f_in);
    u32 c_out = scale_colour(colour_lerp(p.colour_a, p.colour_b, static_cast<s32>(fold(mul32(p.phase_span, hi) / p.rings + p.phase))), f_out);
    for (s32 seg = 0; seg < p.segments; ++seg) {
      c.y = a.y, c.z = a.z;
      d.y = b.y, d.z = b.z;
      const u32 prev_in = c_in, prev_out = c_out;
      const s32 t = ((seg + 1) << 12) / p.segments;
      const s32 twist = p.phase + (mul32(bsin(static_cast<u32>(mul32(p.twist_freq, t))), p.twist_amp) >> 12);
      // Ring 0 keeps the untwisted phase on its inner edge.
      const s32 k_in = mul32(p.phase_span, lo) / p.rings + (ring != 0 ? twist : p.phase);
      const s32 k_out = mul32(p.phase_span, hi) / p.rings + twist;
      c_in = scale_colour(colour_lerp(p.colour_a, p.colour_b, static_cast<s32>(fold(k_in))), f_in);
      c_out = scale_colour(colour_lerp(p.colour_a, p.colour_b, static_cast<s32>(fold(k_out))), f_out);
      const s32 co = bcos(static_cast<u32>(t)), si = bsin(static_cast<u32>(t));
      a.y = static_cast<s16>(mul32(mul32(co, r0), p.width) >> 19);
      a.z = static_cast<s16>(mul32(mul32(si, r0), p.width) >> 19);
      b.y = static_cast<s16>(mul32(mul32(co, r1), p.width) >> 19);
      b.z = static_cast<s16>(mul32(mul32(si, r1), p.width) >> 19);
      load(q, 0, a);
      q.execute(psx::Gte::kRtps);
      const u32 v0 = q.read_data(psx::Gte::SXY2);
      load(q, 0, c);
      load(q, 1, b);
      load(q, 2, d);
      q.execute(psx::Gte::kRtpt);
      const u32 v1 = q.read_data(psx::Gte::SXY0), v2 = q.read_data(psx::Gte::SXY1), v3 = q.read_data(psx::Gte::SXY2);
      q.execute(psx::Gte::kAvsz4);
      const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ)) >> 3;
      if (0x600 - p.ot_bias <= otz) continue;
      auto* quad = screen_alloc_prims<psyq::POLY_G4>(pool->arena, 1);
      if (quad == nullptr) return;
      psyq::setPolyG4(quad);
      const u32 rgb0 = c_in | code;
      std::memcpy(&quad->r0, &rgb0, 4);
      std::memcpy(&quad->r1, &prev_in, 3);
      std::memcpy(&quad->r2, &c_out, 3);
      std::memcpy(&quad->r3, &prev_out, 3);
      quad->x0 = static_cast<s16>(v0), quad->y0 = static_cast<s16>(v0 >> 16);
      quad->x1 = static_cast<s16>(v1), quad->y1 = static_cast<s16>(v1 >> 16);
      quad->x2 = static_cast<s16>(v2), quad->y2 = static_cast<s16>(v2 >> 16);
      quad->x3 = static_cast<s16>(v3), quad->y3 = static_cast<s16>(v3 >> 16);
      psyq::P_TAG* ot = &pool->ot_head[p.ot_bias + 0x610 - otz];
      psyq::addPrim(ot, quad);
      pool_a_bytes_used() += 0x24;
      if (p.blend != 0) {
        auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
        if (dm == nullptr) return;
        psyq::SetDrawMode(dm, 0, 1, (p.blend - 1) << 5, nullptr);
        psyq::addPrim(ot, dm);
        pool_a_bytes_used() += 0xC;
      }
    }
  }
}

u32 colour_modulate(u32 a, u32 b) {
  u32 r = (a & 0xFF) * (b & 0xFF) >> 7;
  if (0xFF < r) r = 0xFF;
  u32 g = (a & 0xFF00) * (b >> 8 & 0xFF) >> 7;
  if (0xFFFF < g) g = 0xFFFF;
  u32 bl = (a & 0xFF0000) * (b >> 16 & 0xFF) >> 7;
  if (0xFFFFFF < bl) bl = 0xFFFFFF;
  return r | (g & 0xFF00) | (bl & 0xFF0000);
}

void project_to_screen(const s32 world[3], s16 out[2]) {
  ViewState& v = view();
  psx::Gte& q = psyq::gte();
  const s16 dx = static_cast<s16>((world[0] - v.view_eye[0]) >> 12);
  const s16 dy = static_cast<s16>((world[1] - v.view_eye[1]) >> 12);
  const s16 dz = static_cast<s16>((world[2] - v.view_eye[2]) >> 12);
  psyq::SetRotMatrix(&v.render_view);
  q.write_data(psx::Gte::VXY0, svec_xy(dx, dy));
  q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(dz)));
  q.execute(psx::Gte::kRtv0);
  psyq::MATRIX m = v.render_view;
  m.t[0] = static_cast<s32>(q.read_data(psx::Gte::MAC1));
  m.t[1] = static_cast<s32>(q.read_data(psx::Gte::MAC2));
  m.t[2] = static_cast<s32>(q.read_data(psx::Gte::MAC3));
  psyq::SetTransMatrix(&m);
  psyq::SetRotMatrix(&v.render_view);
  q.write_data(psx::Gte::VXY0, 0);
  q.write_data(psx::Gte::VZ0, 0);
  q.execute(psx::Gte::kRtps);
  const u32 sxy = q.read_data(psx::Gte::SXY2);
  out[0] = static_cast<s16>(sxy);
  out[1] = static_cast<s16>(sxy >> 16);
}

namespace {

bool flare_on_screen(const s16 s[2]) {
  return static_cast<u16>(static_cast<u16>(s[0]) + 0xF0) < 0x321 && static_cast<u16>(static_cast<u16>(s[1]) + 0xF0) < 0x2D1;
}

void add_additive_mode(RenderContext* pool, psyq::P_TAG* ot) {
  auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
  if (dm == nullptr) return;
  psyq::SetDrawMode(dm, 0, 1, 0x20, nullptr);
  psyq::addPrim(ot, dm);
  if (pool == bg().prim_pool_a) pool_a_bytes_used() += 0xC;
}

void set_xy(s16* xy, s32 x, s32 y) {
  xy[0] = static_cast<s16>(x);
  xy[1] = static_cast<s16>(y);
}

}  // namespace

void draw_flare_ring(const s32 pos[3], s32 r_inner, s32 r_outer, int bands, const u32* colours, const s32* pcts, int ot) {
  s16 c[2];
  project_to_screen(pos, c);
  if (!flare_on_screen(c)) return;
  RenderContext* pool = bg().prim_pool_a;
  psyq::P_TAG* otp = &pool->ot_head[ot];
  s32 segs = (r_outer < 0 ? r_outer + 3 : r_outer) / 4 + 8;
  if (0x40 < segs) segs = 0x40;
  s32 radius = r_inner;  // the outer radius of the previous band
  for (int b = 0; b < bands; ++b) {
    const s32 in_r = radius;
    radius = mul32(r_outer - r_inner, pcts[b]) / 100 + r_inner;
    const u32 c_in = colours[b * 2], c_out = colours[b * 2 + 1];
    s32 ix = c[0] + in_r, iy = c[1];   // the inner edge at angle 0
    s32 ox = c[0] + radius, oy = c[1];  // the outer edge
    if (in_r == 0) {
      auto* tri = screen_alloc_prims<psyq::POLY_G3>(pool->arena, static_cast<size_t>(segs));
      if (tri == nullptr) return;
      for (s32 k = 0; k < segs; ++k, ++tri) {
        const s32 px = ox, py = oy;
        const s32 a = ((k + 1) << 12) / segs;
        ix = c[0] + (mul32(bcos(static_cast<u32>(a)), in_r) >> 12);
        iy = c[1] + (mul32(bsin(static_cast<u32>(a)), in_r) >> 12);
        ox = c[0] + (mul32(bcos(static_cast<u32>(a)), radius) >> 12);
        oy = c[1] + (mul32(bsin(static_cast<u32>(a)), radius) >> 12);
        psyq::setPolyG3(tri);
        const u32 rgb0 = c_in | 0x32000000u;
        std::memcpy(&tri->r0, &rgb0, 4);
        std::memcpy(&tri->r1, &c_out, 3);
        std::memcpy(&tri->r2, &c_out, 3);
        set_xy(&tri->x0, c[0], c[1]);
        set_xy(&tri->x1, ox, oy);
        set_xy(&tri->x2, px, py);
        psyq::addPrim(otp, tri);
        if (pool == bg().prim_pool_a) pool_a_bytes_used() += 0x1C;
      }
    } else {
      auto* quad = screen_alloc_prims<psyq::POLY_G4>(pool->arena, static_cast<size_t>(segs));
      if (quad == nullptr) return;
      for (s32 k = 0; k < segs; ++k, ++quad) {
        const s32 pix = ix, piy = iy, pox = ox, poy = oy;
        const s32 a = ((k + 1) << 12) / segs;
        ix = c[0] + (mul32(bcos(static_cast<u32>(a)), in_r) >> 12);
        iy = c[1] + (mul32(bsin(static_cast<u32>(a)), in_r) >> 12);
        ox = c[0] + (mul32(bcos(static_cast<u32>(a)), radius) >> 12);
        oy = c[1] + (mul32(bsin(static_cast<u32>(a)), radius) >> 12);
        psyq::setPolyG4(quad);
        const u32 rgb0 = c_in | 0x3A000000u;
        std::memcpy(&quad->r0, &rgb0, 4);
        std::memcpy(&quad->r1, &c_out, 3);
        std::memcpy(&quad->r2, &c_in, 3);
        std::memcpy(&quad->r3, &c_out, 3);
        set_xy(&quad->x0, ix, iy);
        set_xy(&quad->x1, ox, oy);
        set_xy(&quad->x2, pix, piy);
        set_xy(&quad->x3, pox, poy);
        psyq::addPrim(otp, quad);
        if (pool == bg().prim_pool_a) pool_a_bytes_used() += 0x24;
      }
    }
  }
  add_additive_mode(pool, otp);
}

void draw_flare_polygon(const s32 pos[3], int sides, s32 radius, u32 rgb, int ot) {
  s16 c[2] = {0, 0};
  project_to_screen(pos, c);
  if (!flare_on_screen(c)) return;
  RenderContext* pool = bg().prim_pool_a;
  auto* tri = screen_alloc_prims<psyq::POLY_F3>(pool->arena, static_cast<size_t>(sides));
  if (tri == nullptr) return;
  psyq::P_TAG* otp = &pool->ot_head[ot];
  const u32 code = rgb | 0x22000000u;
  s32 x = c[0], y = c[1] + radius;
  for (int i = 0; i < sides; ++i, ++tri) {
    const s32 px = x, py = y;
    const s32 a = ((i + 1) << 12) / sides + 0x400 + view().camera_roll;
    x = c[0] + (mul32(bcos(static_cast<u32>(a)), radius) >> 12);
    y = c[1] + (mul32(bsin(static_cast<u32>(a)), radius) >> 12);
    psyq::setPolyF3(tri);
    std::memcpy(&tri->r0, &code, 4);
    set_xy(&tri->x0, c[0], c[1]);
    set_xy(&tri->x1, x, y);
    set_xy(&tri->x2, px, py);
    psyq::addPrim(otp, tri);
    if (pool == bg().prim_pool_a) pool_a_bytes_used() += 0x14;
  }
  add_additive_mode(pool, otp);
}

namespace {

// A flare's element tables (#1984 data): positions along the axis (s16, 0x1000 = the
// source, 0 = the screen centre side), intensities (s16), types (u8: 0 disc, 1 ring,
// 2 hexagon, 3 rainbow ring, 4 soft disc, 5 tinted disc), sizes (u8) and ring widths (u8).
struct FlareSet {
  int count = 0;
  s16 pos[11] = {};
  s16 intensity[11] = {};
  u8 type[11] = {};
  u8 size[11] = {};
  u8 width[11] = {};
};

FlareSet load_set(int count, u32 pos, u32 intensity, u32 type, u32 size, u32 width) {
  FlareSet f;
  f.count = count;
  for (int i = 0; i < count; ++i) {
    auto s16_at = [](u32 a) {
      const u8* p = ovl1984_data(a);
      return p != nullptr ? static_cast<s16>(p[0] | p[1] << 8) : static_cast<s16>(0);
    };
    auto u8_at = [](u32 a) {
      const u8* p = ovl1984_data(a);
      return p != nullptr ? *p : static_cast<u8>(0);
    };
    f.pos[i] = s16_at(pos + static_cast<u32>(i) * 2);
    f.intensity[i] = s16_at(intensity + static_cast<u32>(i) * 2);
    f.type[i] = u8_at(type + static_cast<u32>(i));
    f.size[i] = u8_at(size + static_cast<u32>(i));
    f.width[i] = u8_at(width + static_cast<u32>(i));
  }
  return f;
}

FlareSet& stage_set() {
  static FlareSet s = load_set(11, 0x800810C0, 0x800810D8, 0x800810F0, 0x800810FC, 0x80081108);
  return s;
}

FlareSet& small_set() {
  static FlareSet s = load_set(4, 0x80081114, 0x8008111C, 0x80081124, 0x80081128, 0x8008112C);
  return s;
}

// 0x80074558 lens_flare_render.
void lens_flare_render(FlareSet& set, const s32 src[3], s32 dist, u32 rgb, s32 size, s32 threshold, bool patch) {
  ViewState& v = view();
  auto channel = [](u32 c, s16 k) {
    u32 x = static_cast<u32>(mul32(static_cast<s32>(c), k)) >> 7;
    if (0x100 <= static_cast<s32>(x)) x = 0xFF;
    return x;
  };
  const u32 col = channel(rgb & 0xFF, v.stage_colour[0]) | channel((rgb & 0xFF00) >> 8, v.stage_colour[1]) << 8 |
                  channel(rgb >> 16 & 0xFF, v.stage_colour[2]) << 16;
  const s32 cp = bcos(static_cast<u32>(v.pitch));
  const s32 dx = mul32(bcos(static_cast<u32>(v.yaw + 0x400)), cp) >> 12;
  const s32 dy = bsin(static_cast<u32>(v.pitch));
  const s32 dz = mul32(bsin(static_cast<u32>(v.yaw + 0x400)), cp) >> 12;
  // From the source to the point 'dist' ahead of the eye along the view direction.
  s32 d[3];
  d[0] = v.view_eye[0] + mul32(dx, dist) - src[0];
  d[1] = v.view_eye[1] + mul32(dy, dist) - src[1];
  d[2] = v.view_eye[2] + mul32(dz, dist) - src[2];
  const s32 dot = mul32(-d[0] >> 12, dx) + mul32(-d[1] >> 12, dy) + mul32(-d[2] >> 12, dz);
  static const s32 kZero[3] = {0, 0, 0};  // 0x8006e33c
  const s32 facing = dot / (vec_distance_fx(d, kZero) >> 12);
  if (facing < 0x400) return;
  if (patch) {
    set.size[0] = static_cast<u8>(size);
    set.size[1] = static_cast<u8>(size + 4);
    set.intensity[0] = static_cast<s16>(size * 7);
  }
  const s32 off_axis = 0x1000 - facing;
  for (int i = set.count - 1; 0 <= i; --i) {
    auto toward = [](s32 v0) { return (v0 < 0 ? v0 + 0xFFF : v0) >> 12; };
    const s32 at[3] = {mul32(toward(d[0]), set.pos[i]) + src[0], mul32(toward(d[1]), set.pos[i]) + src[1],
                       mul32(toward(d[2]), set.pos[i]) + src[2]};
    s32 over = facing - threshold;
    if (over < 0) over = 0;
    const s32 level = mul32(set.intensity[i], over) >> 12;
    if (level == 0) continue;
    const u32 c = i != 0 ? scale_colour(col, level) : col;
    const s32 r = mul32(mul32((mul32(off_axis, set.pos[i]) >> 12) + 0x1000, set.size[i]) >> 12, v.zoom) >> 12;
    u32 colours[4] = {c, scale_colour(c, 0x40), 0, 0};
    s32 pcts[2] = {100, 0};
    switch (set.type[i]) {
      case 0:
        draw_flare_ring(at, 0, r, 1, colours, pcts, 0x60F);
        break;
      case 1:
        draw_flare_ring(at, r - set.width[i], r, 1, colours, pcts, 0x60F);
        break;
      case 2:
        draw_flare_polygon(at, 6, r, c, 0x60F);
        break;
      case 3:
        colours[0] = scale_colour(0xFF0000, level);
        colours[1] = colours[2] = scale_colour(0xFF00, level);
        colours[3] = scale_colour(0xFF, level);
        pcts[0] = 0x32, pcts[1] = 100;
        draw_flare_ring(at, r - set.width[i], r, 2, colours, pcts, 0x60F);
        break;
      case 4:
        colours[1] = scale_colour(c, 0xA0);
        draw_flare_ring(at, 0, r, 1, colours, pcts, 0x60F);
        break;
      case 5:
        colours[0] = colour_modulate(c, 0x805858);
        colours[1] = colours[2] = colour_modulate(c, 0x408040);
        colours[3] = colour_modulate(c, 0x202080);
        pcts[0] = 0x60, pcts[1] = 100;
        draw_flare_ring(at, 0, r, 2, colours, pcts, 0x60F);
        break;
      default:
        break;
    }
  }
  // Looking almost straight at the source: an additive full-screen wash.
  const s32 near = 0x1000 - size * 4;
  if (near < facing) {
    s32 peak = size * 6;
    if (0xC0 < peak) peak = 0xC0;
    const s32 level = mul32(peak, facing - near) / (size * 4);
    RenderContext* pool = bg().prim_pool_a;
    psyq::P_TAG* otp = &pool->ot_head[0x60F];
    if (auto* tile = screen_alloc_prims<psyq::TILE>(pool->arena, 1)) {
      psyq::setTile(tile);
      const u32 rgbc = scale_colour(col, level) | 0x62000000u;
      std::memcpy(&tile->r0, &rgbc, 4);
      tile->x0 = 0, tile->y0 = 0, tile->w = 0x140, tile->h = 0xE8;
      psyq::addPrim(otp, tile);
      pool_a_bytes_used() += 0x10;
    }
    add_additive_mode(pool, otp);
  }
}

}  // namespace

void lens_flare_draw(const s32 sun[3], s32 dist, u32 rgb, s32 size, s32 threshold) {
  lens_flare_render(stage_set(), sun, dist, rgb, size, threshold, true);
}

void lens_flare_draw_small(const s32 src[3], s32 dist, u32 rgb, s32 size, s32 threshold) {
  lens_flare_render(small_set(), src, dist, rgb, size, threshold, false);
}

}  // namespace ss::game::battle
