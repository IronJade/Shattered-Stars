// World-map drawing (overlay #3102): the BG draw (0x800893c0) with its visibility pass
// (0x8008a880), the terrain block renderer (0x8007f8a8), the curved-world projection
// helpers (0x8007f1f4 / 0x8007f3e4 / 0x8007f648), texture animations and the sky.
#include <cstring>
#include <vector>

#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/screen.h"
#include "game/field/worldmap.h"
#include "game/field/worldmap_internal.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
using namespace wm;

namespace {

constexpr int kSlotBytes = 0xd000;
WorldView& view() { return world().view; }

// ---- Primitive helpers: the console builds primitives in the screen's arena with byte
// offsets; host primitives carry a P_TAG, then the same GP0 words.
psyq::P_TAG* alloc_prim(int words) {
  auto* t = static_cast<psyq::P_TAG*>(field_screen()->alloc(sizeof(psyq::P_TAG) + static_cast<size_t>(words) * 4));
  if (t != nullptr) t->len = static_cast<u32>(words);
  return t;
}
inline u8* at(psyq::P_TAG* t, int o) { return reinterpret_cast<u8*>(psyq::prim_words(t)) + (o - 4); }
inline void put32(psyq::P_TAG* t, int o, u32 v) { std::memcpy(at(t, o), &v, 4); }
inline void put16(psyq::P_TAG* t, int o, u16 v) { std::memcpy(at(t, o), &v, 2); }
inline void put8(psyq::P_TAG* t, int o, u8 v) { *at(t, o) = v; }
inline u16 get16(psyq::P_TAG* t, int o) {
  u16 v;
  std::memcpy(&v, at(t, o), 2);
  return v;
}
inline void link(int ot_index, psyq::P_TAG* p) {
  if (p != nullptr) psyq::addPrim(field_screen()->ot_entry(ot_index), p);
}

// ---- Terrain block renderer (0x8007f8a8) ----

struct TerrainCtx {
  std::vector<s16> verts;  // bent vertices: x, y, z, pad
  std::vector<s32> depth;  // IR0 (depth-cue factor) of each vertex from the bend pass
  u32 dr_mode[2]{};        // SetDrawMode(dfe 0, dtd 1, tpage 0x20): additive for the fog overlay
  s32 fog[3]{};            // far colour << 4 (fog overlay colour)
};
TerrainCtx g_tc;

inline void ld_vert(int reg, u32 field) {
  // Vertex fields are index * 4; the array holds 4 halfwords per vertex.
  const size_t i = static_cast<size_t>(field >> 2) * 4;
  if (i + 3 > g_tc.verts.size()) {
    ldv(reg, 0, 0, 0);
    return;
  }
  ldv(reg, g_tc.verts[i], g_tc.verts[i + 1], g_tc.verts[i + 2]);
}
inline s32 dep(u32 field) {
  const size_t i = static_cast<size_t>(field >> 2);
  return i < g_tc.depth.size() ? g_tc.depth[i] : 0;
}

// The tests after RTPT on a triangle: back face, projection error, fully off screen
// (x 0..319, y 0..223); returns OTZ / 2 or -1.
int tri_screen(u32 s[3]) {
  run(G::kNclip);
  if (mac(0) < 0) return -1;
  const s32 f = flag();
  s[0] = sxy(0);
  s[1] = sxy(1);
  s[2] = sxy(2);
  if (f < 0) return -1;
  if (!((s[0] & 0xffff) < 0x140 || (s[1] & 0xffff) < 0x140 || (s[2] & 0xffff) < 0x140)) return -1;
  run(G::kAvsz3);
  if (!((s[0] >> 16) < 0xe0 || (s[1] >> 16) < 0xe0 || (s[2] >> 16) < 0xe0)) return -1;
  const s32 z = otz() >> 1;
  return z < 0x800 ? z : -1;
}

// Quads: RTPS on the first corner, RTPT on the other three; NCLIP on (1, 2, 3) must be
// below 14 (the opposite winding of the triangles, plus near-degenerate faces).
int quad_screen(u32 s[4], s32 first_flag) {
  run(G::kNclip);
  if (mac(0) >= 0xe) return -1;
  s[1] = sxy(0);
  s[2] = sxy(1);
  s[3] = sxy(2);
  if (flag() < 0 || first_flag < 0) return -1;
  if (!((s[0] & 0xffff) < 0x140 || (s[1] & 0xffff) < 0x140 || (s[2] & 0xffff) < 0x140 || (s[3] & 0xffff) < 0x140))
    return -1;
  run(G::kAvsz4);
  if (!((s[0] >> 16) < 0xe0 || (s[1] >> 16) < 0xe0 || (s[2] >> 16) < 0xe0 || (s[3] >> 16) < 0xe0)) return -1;
  const s32 z = otz() >> 1;
  return z < 0x800 ? z : -1;
}

// Lighting: V0 = (0, 0x1000, 0), NCCS on each vertex colour.
u32 nccs(u32 rgbc) {
  ld_rgbc(rgbc);
  run(G::kNccs);
  return rgb2();
}
u32 grey(u32 v) { return v | v << 8 | v << 16; }

// Fog colours: DPCS of black towards FC with each vertex's depth factor.
void dpcs(s32 r, s32 g, s32 b, const s32* d, int n, u32* out) {
  ld_fc(r, g, b);
  ld_rgbc(0);
  for (int i = 0; i < n; ++i) {
    ld_ir0(d[i]);
    run(G::kDpcs);
    out[i] = rgb2();
  }
}

psyq::P_TAG* make_g3(const u32 col[3], const u32 s[3]) {
  psyq::P_TAG* p = alloc_prim(6);
  if (p == nullptr) return nullptr;
  for (int i = 0; i < 3; ++i) {
    put32(p, 4 + i * 8, col[i]);
    put32(p, 8 + i * 8, s[i]);
  }
  put8(p, 7, 0x32);
  return p;
}
psyq::P_TAG* make_g4(const u32 col[4], const u32 s[4]) {
  psyq::P_TAG* p = alloc_prim(8);
  if (p == nullptr) return nullptr;
  for (int i = 0; i < 4; ++i) {
    put32(p, 4 + i * 8, col[i]);
    put32(p, 8 + i * 8, s[i]);
  }
  put8(p, 7, 0x3a);
  return p;
}
psyq::P_TAG* make_dr_mode() {
  psyq::P_TAG* p = alloc_prim(2);
  if (p == nullptr) return nullptr;
  put32(p, 4, g_tc.dr_mode[0]);
  put32(p, 8, g_tc.dr_mode[1]);
  return p;
}

// Depth fog of a triangle: OTZ / 2 below 0x149 nothing; below 0x446 a grey subtractive
// G3 (the polygon's own tpage blend) then an additive fog G3; beyond, the darker CLUT
// (+12 rows), the tpage blend one step down and the additive fog G3 only.
void fog_tri(psyq::P_TAG* main, int z, const s32 mid[3], const s32 far[3], const u32 s[3], int clut_off,
             int tpage_off) {
  const int ot = 0x820 - z;
  if (z < 0x149) {
    link(ot, main);
    return;
  }
  u32 c[3];
  if (z < 0x446) {
    dpcs(0x680, 0x680, 0x680, mid, 3, c);
    psyq::P_TAG* grey3 = make_g3(c, s);
    dpcs(g_tc.fog[0], g_tc.fog[1], g_tc.fog[2], mid, 3, c);
    psyq::P_TAG* fog3 = make_g3(c, s);
    link(ot, fog3);
    link(ot, make_dr_mode());
    link(ot, grey3);
    link(ot, main);
    return;
  }
  dpcs(g_tc.fog[0], g_tc.fog[1], g_tc.fog[2], far, 3, c);
  if (main != nullptr) {
    put16(main, tpage_off, static_cast<u16>(get16(main, tpage_off) - 0x20));
    put16(main, clut_off, static_cast<u16>(get16(main, clut_off) + 0x300));
  }
  link(ot, make_g3(c, s));
  link(ot, main);
}

void fog_quad(psyq::P_TAG* main, int z, const s32 d[4], const u32 s[4], bool has_far, int clut_off, int tpage_off) {
  const int ot = 0x820 - z;
  if (z < 0x149) {
    link(ot, main);
    return;
  }
  u32 c[4];
  if (!has_far || z < 0x446) {
    dpcs(0x680, 0x680, 0x680, d, 4, c);
    psyq::P_TAG* grey4 = make_g4(c, s);
    dpcs(g_tc.fog[0], g_tc.fog[1], g_tc.fog[2], d, 4, c);
    psyq::P_TAG* fog4 = make_g4(c, s);
    link(ot, fog4);
    link(ot, make_dr_mode());
    link(ot, grey4);
    link(ot, main);
    return;
  }
  dpcs(g_tc.fog[0], g_tc.fog[1], g_tc.fog[2], d, 4, c);
  if (main != nullptr) {
    put16(main, clut_off, static_cast<u16>(get16(main, clut_off) + 0x300));
    put16(main, tpage_off, static_cast<u16>(get16(main, tpage_off) - 0x20));
  }
  link(ot, make_g4(c, s));
  link(ot, main);
}

// GT3 from a record of type A / J layout: +0 uv0|clut, +4 uv1 | c << 16, +8/+0xc/+0x10
// colours whose top bytes are u2, v2 and the tpage, +0x14 a | b << 16.
void draw_type_a(const u8* r, bool semi) {
  const u32 ab = ru32(r + 0x14), uvc = ru32(r + 4);
  ld_vert(0, ab & 0xffff);
  ld_vert(1, ab >> 16);
  ld_vert(2, uvc >> 16);
  run(G::kRtpt);
  u32 s[3];
  const int z = tri_screen(s);
  if (z < 0) return;
  psyq::P_TAG* p = alloc_prim(9);
  if (p == nullptr) return;
  put32(p, 0xc, ru32(r));
  put32(p, 0x18, (uvc & 0xffff) | static_cast<u32>(r[0x13]) << 16);
  put16(p, 0x24, static_cast<u16>(r[0xb] | r[0xf] << 8));
  ldv(0, 0, 0x1000, 0);
  for (int k = 0; k < 3; ++k) {
    put32(p, 4 + k * 0xc, nccs(ru32(r + 8 + k * 4)));
    if (k == 0) put8(p, 7, semi ? 0x36 : 0x34);
  }
  put8(p, 7, semi ? 0x36 : 0x34);
  put32(p, 8, s[0]);
  put32(p, 0x14, s[1]);
  put32(p, 0x20, s[2]);
  if (semi) {
    link(0x820 - z, p);
    return;
  }
  const s32 d[3] = {dep(ab & 0xfffc), dep((ab >> 16) & 0xfffc), dep((uvc >> 16) & 0xfffc)};
  fog_tri(p, z, d, d, s, 0xe, 0x1a);
}

// GT3 type B: +0 uv0|clut, +4 uv1|tpage, +8 uv2 | b << 16, +0xc grey bytes, +0x10 a | c << 16.
void draw_type_b(const u8* r) {
  const u32 t0 = ru32(r + 0x10), t1 = ru32(r + 8);
  ld_vert(0, t0 & 0xffff);
  ld_vert(1, t1 >> 16);
  ld_vert(2, t0 >> 16);
  run(G::kRtpt);
  psyq::P_TAG* p = nullptr;
  u32 s[3];
  const int z = tri_screen(s);
  if (z < 0) return;
  p = alloc_prim(9);
  if (p == nullptr) return;
  put32(p, 0xc, ru32(r));
  put16(p, 0x24, static_cast<u16>(t1));
  put32(p, 0x18, ru32(r + 4) & 0xffffff);
  ldv(0, 0, 0x1000, 0);
  const u32 g = ru32(r + 0xc);
  for (int k = 0; k < 3; ++k) {
    put32(p, 4 + k * 0xc, nccs(grey((g >> (8 * k)) & 0xff)));
    if (k == 0) put8(p, 7, 0x34);
  }
  put8(p, 7, 0x34);
  put32(p, 8, s[0]);
  put32(p, 0x14, s[1]);
  put32(p, 0x20, s[2]);
  const s32 a = dep(t0 & 0xfffc), b = dep(t1 >> 18 << 2), c = dep(t0 >> 18 << 2);
  const s32 mid[3] = {a, b, c};
  const s32 far[3] = {a, c, b};  // the original swaps b and c in its far path
  fog_tri(p, z, mid, far, s, 0xe, 0x1a);
}

// GT3 type C: +0 uv0|clut, +4 uv1|tpage | b << 24, +8 uv2 | a << 16 | c << 24, +0xc grey bytes.
void draw_type_c(const u8* r) {
  const u32 t0 = ru32(r + 8), t1 = ru32(r + 4);
  const u32 a = (t0 >> 14) & 0x3fc, b = (t1 >> 22) & 0x3fc, c = (t0 >> 22) & 0x3fc;
  ld_vert(0, a);
  ld_vert(1, b);
  ld_vert(2, c);
  run(G::kRtpt);
  u32 s[3];
  const int z = tri_screen(s);
  if (z < 0) return;
  psyq::P_TAG* p = alloc_prim(9);
  if (p == nullptr) return;
  put32(p, 0x18, t1 & 0xffffff);
  put16(p, 0x24, static_cast<u16>(t0));
  put32(p, 0xc, ru32(r));
  ldv(0, 0, 0x1000, 0);
  const u32 g = ru32(r + 0xc);
  for (int k = 0; k < 3; ++k) {
    put32(p, 4 + k * 0xc, nccs(grey((g >> (8 * k)) & 0xff)));
    if (k == 0) put8(p, 7, 0x34);
  }
  put8(p, 7, 0x34);
  put32(p, 8, s[0]);
  put32(p, 0x14, s[1]);
  put32(p, 0x20, s[2]);
  const s32 d[3] = {dep(a), dep(b), dep(c)};
  fog_tri(p, z, d, d, s, 0xe, 0x1a);
}

// FT3 type D: +0 uv0|clut, +4 uv1|tpage | grey << 24, +8 uv2 | b << 16, +0xc a | c << 16.
void draw_type_d(const u8* r) {
  const u32 t1 = ru32(r + 0xc), t2 = ru32(r + 8), a2 = ru32(r + 4);
  ld_vert(0, t1 & 0xffff);
  ld_vert(1, t2 >> 16);
  ld_vert(2, t1 >> 16);
  run(G::kRtpt);
  u32 s[3];
  const int z = tri_screen(s);
  if (z < 0) return;
  psyq::P_TAG* p = alloc_prim(7);
  if (p == nullptr) return;
  put32(p, 0x14, a2 & 0xffffff);
  put32(p, 0xc, ru32(r));
  put16(p, 0x1c, static_cast<u16>(t2));
  const u32 gv = a2 >> 24;
  ldv(0, 0, 0x1000, 0);
  put32(p, 8, s[0]);
  put32(p, 0x10, s[1]);
  put32(p, 0x18, s[2]);
  put32(p, 4, nccs(gv | 0x24000000u | gv << 8 | gv << 16));
  const s32 d[3] = {dep(t1 & 0xfffc), dep(t2 >> 18 << 2), dep(t1 >> 18 << 2)};
  fog_tri(p, z, d, d, s, 0xe, 0x16);
}

// GT4 from a record of type E / K layout: +0 uv0|clut, +4 uv1|tpage, +8..+0x14 colours
// (top bytes u2, v2, u3, v3), +0x18 a | b << 16, +0x1c c | d << 16.
void draw_type_e(const u8* r, bool semi) {
  const u32 ab = ru32(r + 0x18), cd = ru32(r + 0x1c);
  ld_vert(0, ab & 0xffff);
  run(G::kRtps);
  u32 s[4];
  s[0] = sxy(2);
  const s32 f0 = flag();
  ld_vert(0, ab >> 16);
  ld_vert(1, cd & 0xffff);
  ld_vert(2, cd >> 16);
  run(G::kRtpt);
  const int z = quad_screen(s, f0);
  if (z < 0) return;
  psyq::P_TAG* p = alloc_prim(12);
  if (p == nullptr) return;
  put32(p, 0xc, ru32(r));
  put32(p, 0x18, ru32(r + 4) & 0xffffff);
  put16(p, 0x24, static_cast<u16>(r[0xb] | r[0xf] << 8));
  put16(p, 0x30, static_cast<u16>(r[0x13] | r[0x17] << 8));
  ldv(0, 0, 0x1000, 0);
  for (int k = 0; k < 4; ++k) {
    put32(p, 4 + k * 0xc, nccs(ru32(r + 8 + k * 4)));
    if (k == 0) put8(p, 7, semi ? 0x3e : 0x3c);
  }
  put8(p, 7, semi ? 0x3e : 0x3c);
  for (int k = 0; k < 4; ++k) put32(p, 8 + k * 0xc, s[k]);
  if (semi) {
    link(0x820 - z, p);
    return;
  }
  const s32 d[4] = {dep(ab & 0xfffc), dep(ab >> 18 << 2), dep(cd & 0xfffc), dep(cd >> 18 << 2)};
  fog_quad(p, z, d, s, false, 0xe, 0x1a);
}

// GT4 type G: +0 uv0|clut, +4 uv1|tpage, +8 uv2 | uv3 << 16, +0xc grey bytes, +0x10 vertex bytes.
void draw_type_g(const u8* r) {
  const u32 t1 = ru32(r + 0x10);
  ld_vert(0, (t1 & 0xff) << 2);
  run(G::kRtps);
  u32 s[4];
  s[0] = sxy(2);
  const s32 f0 = flag();
  ld_vert(0, (t1 >> 6) & 0x3fc);
  ld_vert(1, (t1 >> 14) & 0x3fc);
  ld_vert(2, (t1 >> 22) & 0x3fc);
  run(G::kRtpt);
  const u32 uv23 = ru32(r + 8);
  const int z = quad_screen(s, f0);
  if (z < 0) return;
  psyq::P_TAG* p = alloc_prim(12);
  if (p == nullptr) return;
  put32(p, 0xc, ru32(r));
  put32(p, 0x18, ru32(r + 4) & 0xffffff);
  put16(p, 0x24, static_cast<u16>(uv23));
  put16(p, 0x30, static_cast<u16>(uv23 >> 16));
  ldv(0, 0, 0x1000, 0);
  const u32 g = ru32(r + 0xc);
  for (int k = 0; k < 4; ++k) {
    put32(p, 4 + k * 0xc, nccs(grey((g >> (8 * k)) & 0xff)));
    if (k == 0) put8(p, 7, 0x3c);
  }
  put8(p, 7, 0x3c);
  for (int k = 0; k < 4; ++k) put32(p, 8 + k * 0xc, s[k]);
  const s32 d[4] = {dep((t1 & 0xff) << 2), dep((t1 >> 6) & 0x3fc), dep((t1 >> 14) & 0x3fc), dep(t1 >> 24 << 2)};
  fog_quad(p, z, d, s, false, 0xe, 0x1a);
}

// FT4 type I: +0 uv0|clut, +4 uv1|tpage | grey << 24, +8 uv2 | uv3 << 16, +0xc vertex bytes.
void draw_type_i(const u8* r) {
  const u32 t2 = ru32(r + 0xc);
  ld_vert(0, (t2 & 0xff) << 2);
  run(G::kRtps);
  u32 s[4];
  s[0] = sxy(2);
  const s32 f0 = flag();
  ld_vert(0, (t2 >> 6) & 0x3fc);
  ld_vert(1, (t2 >> 14) & 0x3fc);
  ld_vert(2, (t2 >> 22) & 0x3fc);
  run(G::kRtpt);
  const u32 t0 = ru32(r + 4), a2 = ru32(r + 8);
  const int z = quad_screen(s, f0);
  if (z < 0) return;
  psyq::P_TAG* p = alloc_prim(9);
  if (p == nullptr) return;
  put32(p, 0xc, ru32(r));
  put16(p, 0x1c, static_cast<u16>(a2));
  put32(p, 0x14, t0 & 0xffffff);
  put16(p, 0x24, static_cast<u16>(a2 >> 16));
  const u32 gv = t0 >> 24;
  ldv(0, 0, 0x1000, 0);
  put32(p, 8, s[0]);
  put32(p, 0x10, s[1]);
  put32(p, 0x18, s[2]);
  put32(p, 0x20, s[3]);
  put32(p, 4, nccs(gv | 0x2c000000u | gv << 8 | gv << 16));
  const s32 d[4] = {dep((t2 & 0xff) << 2), dep((t2 >> 6) & 0x3fc), dep((t2 >> 14) & 0x3fc), dep(t2 >> 24 << 2)};
  fog_quad(p, z, d, s, true, 0xe, 0x16);
}

}  // namespace

// 0x8007f8a8: draws the sub-cells of one terrain block selected by 'mask' (bit 15 - i
// for sub-cell i, 4 x 4 cells of 0xc00 units). The matrices are world_set_object_matrix's.
void world_draw_block(const u8* blk, u16 mask) {
  WorldView& v = view();
  if (g_fade_targets.bg_tint_active != 0) {
    auto m7 = [](s32 a, s32 b) {
      s32 t = a * b;
      if (t < 0) t += 0x7f;
      return t >> 7;
    };
    const s16* tint = g_fade_targets.bg_tint;
    for (int i = 0; i < 3; ++i) g_tc.fog[i] = m7(v.far_colour[i], tint[i]) << 4;
  } else {
    for (int i = 0; i < 3; ++i) g_tc.fog[i] = v.far_colour[i] << 4;
  }
  {
    psyq::DR_MODE dm{};
    psyq::SetDrawMode(&dm, 0, 1, 0x20, nullptr);
    g_tc.dr_mode[0] = dm.code[0];
    g_tc.dr_mode[1] = dm.code[1];
  }
  psyq::SetLightMatrix(&v.light_m);
  psyq::SetColorMatrix(&v.colour);
  psyq::SetBackColor(0, 0, 0);
  const s32 nverts = rd32(blk);
  const u8* hdr_base = blk + 0x68 + nverts * 8;
  for (int i = 0; i < 16; ++i) {
    if (((mask >> (15 - i)) & 1) == 0) continue;
    // Bend pass: world matrix, then the curvature.
    psyq::SetRotMatrix(&v.world);
    psyq::SetTransMatrix(&v.world);
    const u8* src = blk + 0x68 + rd32(blk + 0x28 + i * 4);
    const int count = rd16(blk + 8 + i * 2);
    g_tc.verts.assign(static_cast<size_t>(count) * 4 + 4, 0);
    g_tc.depth.assign(static_cast<size_t>(count) + 1, 0);
    for (int k = 0; k < count; ++k) {
      ldv_raw(0, src + k * 8);
      run(G::kRtps);
      g_tc.depth[static_cast<size_t>(k)] = ir(0);
      const Bent b = bend(ir(1), ir(2), ir(3));
      g_tc.verts[static_cast<size_t>(k) * 4] = b.x;
      g_tc.verts[static_cast<size_t>(k) * 4 + 1] = b.y;
      g_tc.verts[static_cast<size_t>(k) * 4 + 2] = b.z;
    }
    // Projection pass.
    psyq::SetRotMatrix(&v.proj);
    psyq::SetTransMatrix(&v.proj);
    const u8* hdr = hdr_base + i * 0x20;
    const u8* rec = hdr_base + 0x280 + rd32(hdr_base + 0x200 + i * 4) + (read_u16(hdr) & 0xfffc);
    const int counts[11] = {read_u16(hdr + 2),   read_u16(hdr + 4),   read_u16(hdr + 6),   read_u16(hdr + 8),
                            read_u16(hdr + 0xa), read_u16(hdr + 0xc), read_u16(hdr + 0xe), read_u16(hdr + 0x10),
                            read_u16(hdr + 0x12), read_u16(hdr + 0x14), read_u16(hdr + 0x16)};
    for (int k = 0; k < counts[0]; ++k, rec += 0x18) draw_type_a(rec, false);
    for (int k = 0; k < counts[1]; ++k, rec += 0x14) draw_type_b(rec);
    for (int k = 0; k < counts[2]; ++k, rec += 0x10) draw_type_c(rec);
    for (int k = 0; k < counts[3]; ++k, rec += 0x10) draw_type_d(rec);
    for (int k = 0; k < counts[4]; ++k, rec += 0x20) draw_type_e(rec, false);
    rec += counts[5] * 0x18;  // F: not drawn
    for (int k = 0; k < counts[6]; ++k, rec += 0x14) draw_type_g(rec);
    rec += counts[7] * 0x14;  // H: not drawn
    for (int k = 0; k < counts[8]; ++k, rec += 0x10) draw_type_i(rec);
    for (int k = 0; k < counts[9]; ++k, rec += 0x18) draw_type_a(rec, true);
    for (int k = 0; k < counts[10]; ++k, rec += 0x20) draw_type_e(rec, true);
  }
}

// 0x8007f3e4: bends and projects 'n' points (SVECTOR-like rows of 4 halfwords) with the
// current object matrices; screen xy and GTE FLAG of each.
void world_bend_project(const s16 (*pts)[4], int n, u32* sxy_out, s32* flag_out) {
  WorldView& v = view();
  std::vector<Bent> bent(static_cast<size_t>(n));
  psyq::SetRotMatrix(&v.world);
  psyq::SetTransMatrix(&v.world);
  for (int i = 0; i < n; ++i) {
    ldv(0, pts[i][0], pts[i][1], pts[i][2]);
    run(G::kRtps);
    bent[static_cast<size_t>(i)] = bend(ir(1), ir(2), ir(3));
  }
  psyq::SetRotMatrix(&v.proj);
  psyq::SetTransMatrix(&v.proj);
  for (int i = 0; i < n; ++i) {
    ldv(0, bent[static_cast<size_t>(i)].x, bent[static_cast<size_t>(i)].y, bent[static_cast<size_t>(i)].z);
    run(G::kRtps);
    sxy_out[i] = sxy(2);
    flag_out[i] = flag();
  }
}

// 0x8007f1f4: one point: screen xy, GTE FLAG and SZ3.
u32 world_project_point(const s16 pt[3], s32* sz) {
  WorldView& v = view();
  psyq::SetRotMatrix(&v.world);
  psyq::SetTransMatrix(&v.world);
  ldv(0, pt[0], pt[1], pt[2]);
  run(G::kRtps);
  const Bent b = bend(ir(1), ir(2), ir(3));
  psyq::SetRotMatrix(&v.proj);
  psyq::SetTransMatrix(&v.proj);
  ldv(0, b.x, b.y, b.z);
  run(G::kRtps);
  if (sz != nullptr) *sz = static_cast<s32>(gte().read_data(G::SZ3));
  return sxy(2);
}

// 0x8007f648: the bent position of a point, back in world units (x / z rotated back by
// the camera yaw), for drawing things that sit on the curved ground.
void world_bend_to_world(const s16 pt[3], s16 out[3]) {
  WorldView& v = view();
  psyq::SetRotMatrix(&v.world);
  psyq::SetTransMatrix(&v.world);
  ldv(0, pt[0], pt[1], pt[2]);
  run(G::kRtps);
  const Bent b = bend(ir(1), ir(2), ir(3));
  out[1] = b.y;
  const s32 yaw = world_ratan2((v.target[2] - v.eye[2]) >> 12, (v.target[0] - v.eye[0]) >> 12);
  const s32 c = rcos(-(yaw - 0x400)), s = rsin(-(yaw - 0x400));
  const s32 bx = b.x, bz = b.z;
  out[0] = static_cast<s16>((v.eye[0] >> 12) + ((bx * c + bz * s) >> 12));
  out[2] = static_cast<s16>((v.eye[2] >> 12) + ((bx * -s + bz * c) >> 12));
}

namespace {

// 0x8008a880: which sub-cells of the nine blocks to draw. The 13 x 13 grid of sub-cell
// corners is projected; a sub-cell is a candidate unless all four edges lie outside one
// screen side, and close enough (sum of squared corner distances / 256 below 0x3200);
// the 3 x 3 sub-cells around the camera always are. The 18 nearest are kept.
void visible_cells(WorldBg* bg, u16 mask[9]) {
  WorldView& v = view();
  std::memset(mask, 0, 9 * sizeof(u16));
  const s32 zero[3] = {0, 0, 0}, scale[3] = {0x1000, 0x1000, 0x1000};
  world_set_object_matrix(zero, v.light_angle, scale);
  u32 sxy[0xa9];
  s32 flags[0xa9];
  world_bend_project(bg->block_xz, 0xa9, sxy, flags);
  const WorldCam* cam = world_cam();
  const s32 ex = cam != nullptr ? cam->eye[0] : 0, ez = cam != nullptr ? cam->eye[2] : 0;
  u8 outcode[0xa9];
  s16 dist[0xa9];
  for (int i = 0; i < 0xa9; ++i) {
    const s16 dx = static_cast<s16>(static_cast<u32>((ex >> 12) - bg->block_xz[i][0]) >> 8);
    const s16 dz = static_cast<s16>(static_cast<u32>((ez >> 12) - bg->block_xz[i][2]) >> 8);
    dist[i] = static_cast<s16>(dx * dx + dz * dz);
    if (flags[i] != 0) {
      outcode[i] = 0xf;
      continue;
    }
    const s16 x = static_cast<s16>(sxy[i]), y = static_cast<s16>(sxy[i] >> 16);
    u8 code = x < 0 ? 1 : x > 0x13f ? 2 : 0;
    if (y < 0) {
      code |= 4;
    } else if (y > 0xdf) {
      code |= 8;
    }
    outcode[i] = code;
  }
  u8 taken[0xa9]{};
  s16 cell[0xa9 + 9];
  s32 key[0xa9 + 9];
  int n = 0;
  for (int b = 0; b < 9; ++b) {
    const int bz = b / 3, bx = b % 3;
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        const int g = bx * 4 + col + bz * 52 + row * 13;
        const u8 c0 = outcode[g], c1 = outcode[g + 1], c13 = outcode[g + 13], c14 = outcode[g + 14];
        if ((c1 & c0) == 0 || (c14 & c1) == 0 || (c13 & c14) == 0 || (c0 & c13) == 0) {
          const u32 sum = static_cast<u16>(dist[g]) + static_cast<u16>(dist[g + 1]) + static_cast<u16>(dist[g + 13]) +
                          static_cast<u16>(dist[g + 14]);
          if (sum < 0x3200) {
            taken[g] = 1;
            cell[n] = static_cast<s16>(g);
            key[n++] = static_cast<s32>(sum);
          }
        }
      }
    }
  }
  int g = ((bg->centre_z % 0x3000) / 0xc00 + 3) * 13 + 3 + (bg->centre_x % 0x3000) / 0xc00;
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col, ++g) {
      if (g >= 0 && g < 0xa9 && taken[g] == 0) {
        cell[n] = static_cast<s16>(g);
        key[n++] = 0;
      }
    }
    g += 10;
  }
  const int keep = n < 0x13 ? n : 0x12;
  for (int i = 0; i < keep; ++i) {
    int best = i;
    for (int j = i + 1; j < n; ++j)
      if (key[j] < key[best]) best = j;
    if (best != i) {
      std::swap(cell[i], cell[best]);
      std::swap(key[i], key[best]);
    }
  }
  for (int i = 0; i < keep; ++i) {
    const int c = cell[i];
    const int row = c / 13, col = c % 13;
    const int b = (col >> 2) + (row >> 2) * 3;
    if (b >= 0 && b < 9) mask[b] = static_cast<u16>(mask[b] | 1u << (15 - ((col & 3) + (row & 3) * 4)));
  }
}

// The texture animations (section 3 tag 3): each group's timer counts down by the frame
// ticks; when it runs out the next frame is MoveImage'd onto the group's destination.
void step_texture_anims() {
  WorldGlobals& w = world();
  const s32 ticks = globals().frame_ticks;
  for (int i = 0; i < 24; ++i) {
    if (w.anim_first[i] == 0xff) continue;
    if (w.anim_timer[i] < 1) {
      do {
        ++w.anim_cur[i];
        if (static_cast<s32>(w.anim_end[i]) <= w.anim_cur[i]) w.anim_cur[i] = w.anim_first[i];
        w.anim_timer[i] = static_cast<s8>(w.anim_timer[i] + w.anim_duration[w.anim_cur[i]]);
      } while (w.anim_timer[i] < 1);
      const s16* f = w.anim_frame[w.anim_cur[i]];
      const psyq::RECT r{f[0], f[1], f[2], f[3]};
      psyq::MoveImage(&r, w.anim_dest[i][0], w.anim_dest[i][1]);
    }
    w.anim_timer[i] = static_cast<s8>(w.anim_timer[i] - ticks);
  }
}

// The sky: three gradient G4 bands above the horizon (OT 0x10) and the additive cloud
// strip (texture (960, 256), CLUT row 0x1fb; OT 0x11) spanning the field of view, all
// rotated by the camera roll about the horizon point 9000 units ahead.
void draw_sky(WorldBg* bg) {
  WorldView& v = view();
  const s32 yaw = world_camera_yaw();
  s16 pts[6][4]{};
  const s16 fx = static_cast<s16>(bg->centre_x % 0x3000 + ((rcos(yaw) * 9000) / 4096));
  const s16 fz = static_cast<s16>(bg->centre_z % 0x3000 + ((rsin(yaw) * 9000) / 4096));
  static const s16 ys[6] = {-0x7d0, -0x190, 0xc8, -0x4b0, 0x64, 0};
  for (int i = 0; i < 6; ++i) {
    pts[i][0] = fx;
    pts[i][1] = ys[i];
    pts[i][2] = fz;
  }
  const s32 zero[3] = {0, 0, 0}, scale[3] = {0x1000, 0x1000, 0x1000};
  world_set_object_matrix(zero, v.light_angle, scale);
  u32 sxy[6];
  s32 flags[6];
  world_bend_project(pts, 6, sxy, flags);
  if (flags[5] < 0) return;
  const s32 hx = static_cast<s16>(sxy[5]), hy = static_cast<s16>(sxy[5] >> 16);
  auto sy = [&](int i) { return static_cast<s32>(static_cast<s16>(sxy[i] >> 16)); };
  const s32 s = rsin(-v.roll), c = rcos(-v.roll);
  auto m7 = [](s32 a, s32 b) {
    s32 t = a * b;
    if (t < 0) t += 0x7f;
    return t >> 7;
  };
  for (int band = 0; band < 3; ++band) {
    psyq::P_TAG* p = alloc_prim(8);
    if (p == nullptr) return;
    u8 top[3], bottom[3];
    s32 y0, y1;
    if (band == 0) {
      top[0] = bottom[0] = v.sky_r_now[0];
      top[1] = bottom[1] = v.sky_g_now[0];
      top[2] = bottom[2] = v.sky_b_now[0];
      y0 = sy(0) - (hy + 0x78);
      y1 = sy(0) - hy;
    } else if (band == 1) {
      top[0] = v.sky_r_now[0];
      top[1] = v.sky_g_now[0];
      top[2] = v.sky_b_now[0];
      bottom[0] = v.sky_r_now[1];
      bottom[1] = v.sky_g_now[1];
      bottom[2] = v.sky_b_now[1];
      y0 = sy(0) - hy;
      y1 = sy(1) - hy;
    } else {
      top[0] = v.sky_r_now[1];
      top[1] = v.sky_g_now[1];
      top[2] = v.sky_b_now[1];
      for (int i = 0; i < 3; ++i) bottom[i] = static_cast<u8>(m7(v.far_colour[i], v.light[i]));
      y0 = sy(1) - hy;
      y1 = sy(2) - hy;
    }
    const s32 xl = -0x14 - hx, xr = 0x154 - hx;
    auto px = [&](s32 x, s32 y) { return static_cast<s16>(((x * c + y * s) >> 12) + hx); };
    auto py = [&](s32 x, s32 y) { return static_cast<s16>(((x * -s + y * c) >> 12) + hy); };
    put32(p, 4, static_cast<u32>(top[0] | top[1] << 8 | top[2] << 16) | 0x39000000u);
    put16(p, 8, static_cast<u16>(px(xl, y0)));
    put16(p, 0xa, static_cast<u16>(py(xl, y0)));
    put32(p, 0xc, static_cast<u32>(top[0] | top[1] << 8 | top[2] << 16));
    put16(p, 0x10, static_cast<u16>(px(xr, y0)));
    put16(p, 0x12, static_cast<u16>(py(xr, y0)));
    put32(p, 0x14, static_cast<u32>(bottom[0] | bottom[1] << 8 | bottom[2] << 16));
    put16(p, 0x18, static_cast<u16>(px(xl, y1)));
    put16(p, 0x1a, static_cast<u16>(py(xl, y1)));
    put32(p, 0x1c, static_cast<u32>(bottom[0] | bottom[1] << 8 | bottom[2] << 16));
    put16(p, 0x20, static_cast<u16>(px(xr, y1)));
    put16(p, 0x22, static_cast<u16>(py(xr, y1)));
    link(0x10, p);
  }
  // The cloud strip.
  const s32 half_fov = world_ratan2(0xa0, mul(map_data().projection_h, v.zoom) >> 12);
  s32 q = (half_fov * 2) & 0xfff;
  q = (q < 0 ? q + 3 : q) >> 2;
  const s32 width = (q * 360) / 320;
  const s32 u_start = 0x100 - ((yaw & 0x1ff) >> 1);
  const s32 top_y = sy(3) - hy, bottom_y = sy(4) - hy;
  for (int pass = 0; pass < 2; ++pass) {
    s32 x = 0xa0;
    s32 u = u_start & 0xff;
    s32 left = width;
    while (left > 0) {
      psyq::P_TAG* p = alloc_prim(9);
      if (p == nullptr) return;
      s32 n, step, x2;
      u8 u0, u1;
      if (pass == 0) {
        n = 0x100 - u;
        if (left < n) n = left;
        step = (n * 180) / width;
        left -= n;
        u0 = static_cast<u8>(u);
        u1 = static_cast<u8>(u + n - 1);
        x2 = x + step;
      } else {
        n = u;
        if (left < u) n = left;
        step = (n * 180) / width;
        left -= n;
        u0 = static_cast<u8>(u - 1);
        u1 = static_cast<u8>(u - n);
        x2 = x - step;
      }
      const s32 ax = x - hx, bx = x2 - hx;
      put32(p, 4, static_cast<u32>(static_cast<u8>(v.light[0]) | static_cast<u8>(v.light[1]) << 8 |
                                   static_cast<u8>(v.light[2]) << 16) |
                      0x2e000000u);
      put16(p, 8, static_cast<u16>(((ax * c + top_y * s) >> 12) + hx));
      put16(p, 0xa, static_cast<u16>(((ax * -s + top_y * c) >> 12) + hy));
      put8(p, 0xc, u0);
      put8(p, 0xd, 0);
      put16(p, 0xe, 0x7ec0);
      put16(p, 0x10, static_cast<u16>(((bx * c + top_y * s) >> 12) + hx));
      put16(p, 0x12, static_cast<u16>(((bx * -s + top_y * c) >> 12) + hy));
      put8(p, 0x14, u1);
      put8(p, 0x15, 0);
      put16(p, 0x16, 0x3f);
      put16(p, 0x18, static_cast<u16>(((ax * c + bottom_y * s) >> 12) + hx));
      put16(p, 0x1a, static_cast<u16>(((ax * -s + bottom_y * c) >> 12) + hy));
      put8(p, 0x1c, u0);
      put8(p, 0x1d, 0x40);
      put16(p, 0x20, static_cast<u16>(((bx * c + bottom_y * s) >> 12) + hx));
      put16(p, 0x22, static_cast<u16>(((bx * -s + bottom_y * c) >> 12) + hy));
      put8(p, 0x24, u1);
      put8(p, 0x25, 0x40);
      link(0x11, p);
      if (pass == 0) {
        u = 0;
        x += step;
      } else {
        u = 0x100;
        x -= step;
      }
    }
  }
}

}  // namespace

// 0x800893c0.
void world_bg_draw(WorldBg* bg) {
  WorldGlobals& w = world();
  WorldView& v = w.view;
  u16 mask[9];
  visible_cells(bg, mask);
  static const s32 off[3] = {-0x1800, 0x1800, 0x4800};
  for (int slot = 0; slot < 9; ++slot) {
    if (w.loaded[slot] == -1 || w.blocks.empty()) continue;
    int idx = 0;
    for (; idx < 9; ++idx)
      if (w.loaded[slot] == w.wanted[idx]) break;
    if (idx == 9 || mask[idx] == 0) continue;
    const s32 pos[3] = {off[idx % 3] << 12, 0, off[idx / 3] << 12};
    const s32 scale[3] = {0x1000, 0x1000, 0x1000};
    world_set_object_matrix(pos, v.light_angle, scale);
    world_draw_block(w.blocks.data() + slot * kSlotBytes, mask[idx]);
  }
  if (read_u16(globals().game_state + 0x1a) == 2) {
    runtime::Runtime::get().log_once("world:rings", "world: map 2 whirlwind walls (0x800893c0) not ported yet");
  }
  step_texture_anims();
  draw_sky(bg);
}

// 0x8008b064: the town markers (Start toggles game_state +0x20). Not ported yet.
void world_draw_markers(WorldBg*, int) {
  runtime::Runtime::get().log_once("world:markers", "world: map markers (0x8008b064) not ported yet");
}

}  // namespace ss::game::field
