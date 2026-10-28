// The battle's model shatter (resident #2129): model_shatter 0x8005f30c breaks a model into
// PolyShards (Pool74, 192 x 0x74, vtable 0x8006cf94), one per polygon: shard_alloc
// 0x800600a0, shard_set_vertices 0x80060210, the lighting 0x800604cc..0x80060680, the task
// shard_update 0x800606f8 and shard_free 0x80061670.

#include "game/battle/shatter.h"

#include <array>
#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/render.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/common/screen.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"

namespace ss::game::battle {
namespace {

using psx::Gte;

// A PolyShard's fields (+0x08..+0x74).
struct ShardData {
  u8 type = 0;          // +0x08: 0 free; the vertex count, then the primitive (1 F3, 2 FT3, 3 G3,
                        //        4 GT3, 5 F4, 6 FT4, 7 G4, 8 GT4)
  u8 delay = 0;         // +0x09: ticks before the fade starts
  u8 ot_bias = 0;       // +0x0a
  u8 abr_tpage = 0;     // +0x0b: the untextured kinds' DR_MODE tpage
  u16 flags = 0;        // +0x0c: 1 gravity, 2 runs in the 0x40 pause, 4 semi-transparent
  s16 fade = 0;         // +0x0e: colour fade per tick
  u16 tpage = 0;        // +0x10
  u16 clut = 0;         // +0x12
  u16 uv[4] = {};       // +0x14
  u32 rgb[4] = {};      // +0x1c: lit colours
  s32 pos[3] = {};      // +0x2c: centroid (20.12)
  s32 vel[3] = {};      // +0x3c
  s16 angle[2] = {};    // +0x4c yaw, +0x4e tilt about the yaw's axis
  s8 spin[2] = {};      // +0x51 / +0x52: their steps per tick
  s16 vert[4][4] = {};  // +0x54: vertices from the centroid (SVECTOR)
};

struct PolyShard : Task, ShardData {
  void update() override;  // [1] shard_update 0x800606f8
};

struct ShatterState {
  std::array<PolyShard, 192> pool;  // Pool74 (g_pool_74 0x8006facc)
  s32 cursor = 0;                   // 0x80070010 g_shard_cursor
  u16 flags = 0;                    // 0x80070014 g_shatter_flags
  u16 abr = 0;                      // 0x80070016 g_shatter_abr
  s16 fade = 0;                     // 0x80070018 g_shatter_fade
  s32 ot_bias = 0;                  // 0x8007001c g_shatter_ot_bias
  s32 centre[3] = {};               // 0x1f80021c (scratchpad): the model's position, whole units
  s32 speed = 0;                    // 0x1f80023c (scratchpad)
};
ShatterState g_shatter;

// The record bytes model_shatter points the lighting at: the scratchpad's 0x808080 colour
// (0x1f800000) and the unlit sections' up normal (0x1f8000ec).
constexpr u8 kGrey[4] = {0x80, 0x80, 0x80, 0x00};
constexpr u8 kUpNormal[8] = {0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00};

u32 rd32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}
u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}
// 32-bit products and sums as the R3000 computes them.
s32 mul32(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }
s32 add32(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) + static_cast<u32>(b)); }

// lwc2 of an SVECTOR from model bytes into V0-V2.
void load_v(Gte& q, int i, const u8* p) {
  q.write_data(Gte::VXY0 + 2 * i, rd32(p));
  q.write_data(Gte::VZ0 + 2 * i, rd32(p + 4));
}
void load_vert(Gte& q, int i, const s16* v) {
  q.write_data(Gte::VXY0 + 2 * i, static_cast<u32>(static_cast<u16>(v[0])) | static_cast<u32>(static_cast<u16>(v[1])) << 16);
  q.write_data(Gte::VZ0 + 2 * i, static_cast<u32>(static_cast<s32>(v[2])));
}
s32 gte_flag(const Gte& q) { return static_cast<s32>(q.read_ctrl(Gte::FLAG)); }

// gte_ldclmv / gte_rtir / gte_stclmv: one column of 'in' through RT, into 'out'.
void rt_column(const psyq::MATRIX& in, psyq::MATRIX* out, int col) {
  Gte& q = psyq::gte();
  q.write_data(Gte::IR1, static_cast<u32>(static_cast<s32>(in.m[0][col])));
  q.write_data(Gte::IR2, static_cast<u32>(static_cast<s32>(in.m[1][col])));
  q.write_data(Gte::IR3, static_cast<u32>(static_cast<s32>(in.m[2][col])));
  q.execute(Gte::kRtir);
  out->m[0][col] = q.ir(1);
  out->m[1][col] = q.ir(2);
  out->m[2][col] = q.ir(3);
}

// 0x80061670 shard_free: type 0, out of the task list (null-safe).
void shard_free(PolyShard* s) {
  if (s == nullptr) return;
  s->type = 0;
  task_unregister(s);
}

// 0x800600a0 shard_alloc: the next free shard after the cursor, set up from the shatter
// globals with two random spins, registered at priority 3; null when the pool is full.
PolyShard* shard_alloc(u8 delay) {
  ShatterState& st = g_shatter;
  st.cursor += 1;
  if (0xBE < st.cursor) st.cursor = 0;
  // (The scan runs cursor + 1 .. cursor - 1, wrapping at 192: the cursor's own slot is
  // never tried, and the cursor itself never reaches 191.)
  s32 i = st.cursor + 1;
  while (i != st.cursor) {
    PolyShard& s = st.pool[static_cast<size_t>(i)];
    if (s.type == 0) {
      s.delay = delay;
      s.fade = st.fade;
      s.angle[0] = 0;
      s.angle[1] = 0;
      s.spin[0] = static_cast<s8>(rng_between(-0x80, 0x7F));
      s.spin[1] = static_cast<s8>(rng_between(-0x80, 0x7F));
      s.flags = st.flags;
      s.ot_bias = static_cast<u8>(st.ot_bias);
      s.abr_tpage = static_cast<u8>(st.abr);
      task_register(&s, 3);
      return &s;
    }
    i = i + 1 < 0xC0 ? i + 1 : 0;
  }
  return nullptr;
}

// 0x80060210 shard_set_vertices: the n vertices through the model matrix; their centroid is
// the shard's position, its direction from the shatter centre (normalised) times the speed
// its velocity, and the vertices are kept relative to it.
void shard_set_vertices(PolyShard* s, int n, const u8* v) {
  ShatterState& st = g_shatter;
  Gte& q = psyq::gte();
  s->type = static_cast<u8>(n);
  s32 xyz[4][3] = {};  // scratchpad 0x1f800004 (stride 16)
  s32 sum[3] = {};
  for (int i = 0; i < n; ++i, v += 8) {
    load_v(q, 0, v);
    q.execute(Gte::kRtv0tr);
    for (int k = 0; k < 3; ++k) {
      xyz[i][k] = q.mac(k + 1);
      sum[k] = add32(sum[k], xyz[i][k]);
    }
  }
  const s32 c[3] = {sum[0] / n, sum[1] / n, sum[2] / n};
  const s32 d[3] = {c[0] - st.centre[0], c[1] - st.centre[1], c[2] - st.centre[2]};  // 0x1f80012c
  // Its length: the squares of the 16-bit (IR) components, SquareRoot0.
  q.write_data(Gte::IR1, static_cast<u32>(d[0]));
  q.write_data(Gte::IR2, static_cast<u32>(d[1]));
  q.write_data(Gte::IR3, static_cast<u32>(d[2]));
  q.execute(Gte::kSqr0);
  for (int k = 0; k < 3; ++k) s->pos[k] = static_cast<s32>(static_cast<u32>(c[k]) << 12);
  s32 len = square_root0(add32(add32(q.mac(1), q.mac(2)), q.mac(3)));
  if (len == 0) len = 1;
  for (int k = 0; k < 3; ++k) s->vel[k] = mul32(static_cast<s32>(static_cast<u32>(d[k]) << 12), st.speed) / len;
  if ((st.flags & 1) != 0) s->vel[1] -= 0x2000;
  for (int i = 0; i < n; ++i)
    for (int k = 0; k < 3; ++k) s->vert[i][k] = static_cast<s16>(xyz[i][k] - c[k]);
}

// 0x800604cc shard_light_flat: NCCS of the normal and colour; F3 or F4.
void shard_light_flat(PolyShard* s, const u8* normal, const u8* colour) {
  Gte& q = psyq::gte();
  load_v(q, 0, normal);
  q.write_data(Gte::RGBC, rd32(colour));
  q.execute(Gte::kNccs);
  s->type = s->type == 3 ? 1 : 5;
  s->rgb[0] = q.read_data(Gte::RGB2);
}

// 0x8006050c shard_light_flat_tex: NCCS of the normal with the RGBC already loaded; FT3 or FT4.
void shard_light_flat_tex(PolyShard* s, const u8* normal) {
  Gte& q = psyq::gte();
  load_v(q, 0, normal);
  q.execute(Gte::kNccs);
  s->type = s->type == 3 ? 2 : 6;
  s->rgb[0] = q.read_data(Gte::RGB2);
}

// 0x80060548 shard_light_gouraud3: NCCT of three normals (stride 8) and a colour; G3.
void shard_light_gouraud3(PolyShard* s, const u8* normals, const u8* colour) {
  Gte& q = psyq::gte();
  load_v(q, 0, normals);
  load_v(q, 1, normals + 8);
  load_v(q, 2, normals + 16);
  q.write_data(Gte::RGBC, rd32(colour));
  q.execute(Gte::kNcct);
  s->type = 3;
  for (int i = 0; i < 3; ++i) s->rgb[i] = q.read_data(Gte::RGB0 + i);
}

// 0x800605a0 shard_light_gouraud4: NCCS of the first normal, NCCT of the other three, with
// the RGBC already loaded; G4.
void shard_light_gouraud4(PolyShard* s, const u8* normals) {
  Gte& q = psyq::gte();
  load_v(q, 0, normals);
  q.execute(Gte::kNccs);
  s->rgb[0] = q.read_data(Gte::RGB2);
  load_v(q, 0, normals + 8);
  load_v(q, 1, normals + 16);
  load_v(q, 2, normals + 24);
  q.execute(Gte::kNcct);
  s->type = 7;
  for (int i = 0; i < 3; ++i) s->rgb[1 + i] = q.read_data(Gte::RGB0 + i);
}

// 0x80060614 shard_light_colours: one normal, NCCS of each of the n colours; G3 or G4.
void shard_light_colours(PolyShard* s, int n, const u8* normal, const u8* colours) {
  Gte& q = psyq::gte();
  load_v(q, 0, normal);
  for (int i = 0; i < n; ++i) {
    q.write_data(Gte::RGBC, rd32(colours + 4 * i));
    q.execute(Gte::kNccs);
    s->type = s->type == 3 ? 3 : 7;
    s->rgb[i] = q.read_data(Gte::RGB2);
  }
}

// 0x80060680 shard_light_normals: NCCS of n normals (stride 8) with n colours; G3 or G4.
void shard_light_normals(PolyShard* s, int n, const u8* normals, const u8* colours) {
  Gte& q = psyq::gte();
  for (int i = 0; i < n; ++i) {
    // ORIGINAL BUG (original_bugs.md S13): the console loads each normal through a pointer
    // read from the normal's own first word (lw t3,0(a2); lwc2 c2r0,0(t3)), but the model
    // holds the SVECTORs inline there (model_render loads them directly), so it reads from the
    // address x | y << 16 (garbage, or an address error when it is not word aligned):
    //   load_v(q, 0, <the address in the normal's first word>);
    // The port reads the inline normal.
    load_v(q, 0, normals + 8 * i);
    q.write_data(Gte::RGBC, rd32(colours + 4 * i));
    q.execute(Gte::kNccs);
    s->type = s->type == 3 ? 3 : 7;
    s->rgb[i] = q.read_data(Gte::RGB2);
  }
}

// model_shatter's textured shards: the polygon's CLUT and tpage (with the abr bits replaced),
// its UVs.
void shard_set_texture(PolyShard* s, const u8* r, bool quad, u16 clut_base, u16 tpage_base, u16 abr) {
  s->tpage = static_cast<u16>(abr | ((tpage_base + rd16(r + 6)) & 0xFF9F));
  s->clut = static_cast<u16>(clut_base + rd16(r + 2));
  s->uv[0] = rd16(r);
  s->uv[1] = rd16(r + 4);
  s->uv[2] = rd16(r + 8);
  if (quad) s->uv[3] = rd16(r + 10);
}

void set_xy(s16& x, s16& y, u32 v) {
  x = static_cast<s16>(v & 0xFFFF);
  y = static_cast<s16>(v >> 16);
}

// shard_update's primitive: kind s.type at OT ot_bias + 0x610 - z8 of pool A, with a
// DR_MODE (tpage +0xb) in front of the untextured kinds.
void shard_emit(PolyShard& s, RenderContext* ctx, const u32 xy[4], s32 z8, u32 semi) {
  const s32 idx = s.ot_bias + 0x610 - z8;
  // (port) Past the 0x621-entry table (an OT bias above 0x10) the console writes beyond it.
  if (ctx->ot_head == nullptr || 0x620 < idx) return;
  psyq::P_TAG* ot = &ctx->ot_head[idx];
  const u32 c0 = (s.rgb[0] & 0xFFFFFF) | semi;
  void* prim = nullptr;
  s32 bytes = 0;
  switch (s.type) {
    case 1: {
      auto* p = screen_alloc_prims<psyq::POLY_F3>(ctx->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyF3(p);
      const u32 w = c0 | 0x20000000;
      std::memcpy(&p->r0, &w, 4);
      set_xy(p->x0, p->y0, xy[0]), set_xy(p->x1, p->y1, xy[1]), set_xy(p->x2, p->y2, xy[2]);
      prim = p, bytes = 0x14;
      break;
    }
    case 2: {
      auto* p = screen_alloc_prims<psyq::POLY_FT3>(ctx->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyFT3(p);
      const u32 w = c0 | 0x24000000;
      std::memcpy(&p->r0, &w, 4);
      set_xy(p->x0, p->y0, xy[0]), set_xy(p->x1, p->y1, xy[1]), set_xy(p->x2, p->y2, xy[2]);
      std::memcpy(&p->u0, &s.uv[0], 2), std::memcpy(&p->u1, &s.uv[1], 2), std::memcpy(&p->u2, &s.uv[2], 2);
      p->clut = s.clut, p->tpage = s.tpage, p->pad1 = 0;
      prim = p, bytes = 0x20;
      break;
    }
    case 3: {
      auto* p = screen_alloc_prims<psyq::POLY_G3>(ctx->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyG3(p);
      const u32 w = c0 | 0x30000000;
      std::memcpy(&p->r0, &w, 4);
      std::memcpy(&p->r1, &s.rgb[1], 4), std::memcpy(&p->r2, &s.rgb[2], 4);
      set_xy(p->x0, p->y0, xy[0]), set_xy(p->x1, p->y1, xy[1]), set_xy(p->x2, p->y2, xy[2]);
      prim = p, bytes = 0x1C;
      break;
    }
    case 4: {
      auto* p = screen_alloc_prims<psyq::POLY_GT3>(ctx->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyGT3(p);
      const u32 w = c0 | 0x34000000;
      std::memcpy(&p->r0, &w, 4);
      std::memcpy(&p->r1, &s.rgb[1], 4), std::memcpy(&p->r2, &s.rgb[2], 4);
      set_xy(p->x0, p->y0, xy[0]), set_xy(p->x1, p->y1, xy[1]), set_xy(p->x2, p->y2, xy[2]);
      std::memcpy(&p->u0, &s.uv[0], 2), std::memcpy(&p->u1, &s.uv[1], 2), std::memcpy(&p->u2, &s.uv[2], 2);
      p->clut = s.clut, p->tpage = s.tpage, p->pad2 = 0;
      prim = p, bytes = 0x28;
      break;
    }
    case 5: {
      auto* p = screen_alloc_prims<psyq::POLY_F4>(ctx->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyF4(p);
      const u32 w = c0 | 0x28000000;
      std::memcpy(&p->r0, &w, 4);
      set_xy(p->x0, p->y0, xy[0]), set_xy(p->x1, p->y1, xy[1]), set_xy(p->x2, p->y2, xy[2]);
      set_xy(p->x3, p->y3, xy[3]);
      prim = p, bytes = 0x18;
      break;
    }
    case 6: {
      auto* p = screen_alloc_prims<psyq::POLY_FT4>(ctx->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyFT4(p);
      const u32 w = c0 | 0x2C000000;
      std::memcpy(&p->r0, &w, 4);
      set_xy(p->x0, p->y0, xy[0]), set_xy(p->x1, p->y1, xy[1]), set_xy(p->x2, p->y2, xy[2]);
      set_xy(p->x3, p->y3, xy[3]);
      std::memcpy(&p->u0, &s.uv[0], 2), std::memcpy(&p->u1, &s.uv[1], 2), std::memcpy(&p->u2, &s.uv[2], 2);
      std::memcpy(&p->u3, &s.uv[3], 2);
      p->clut = s.clut, p->tpage = s.tpage, p->pad1 = 0, p->pad2 = 0;
      prim = p, bytes = 0x28;
      break;
    }
    case 7: {
      auto* p = screen_alloc_prims<psyq::POLY_G4>(ctx->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyG4(p);
      const u32 w = c0 | 0x38000000;
      std::memcpy(&p->r0, &w, 4);
      std::memcpy(&p->r1, &s.rgb[1], 4), std::memcpy(&p->r2, &s.rgb[2], 4), std::memcpy(&p->r3, &s.rgb[3], 4);
      set_xy(p->x0, p->y0, xy[0]), set_xy(p->x1, p->y1, xy[1]), set_xy(p->x2, p->y2, xy[2]);
      set_xy(p->x3, p->y3, xy[3]);
      prim = p, bytes = 0x24;
      break;
    }
    case 8: {
      auto* p = screen_alloc_prims<psyq::POLY_GT4>(ctx->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyGT4(p);
      const u32 w = c0 | 0x3C000000;
      std::memcpy(&p->r0, &w, 4);
      std::memcpy(&p->r1, &s.rgb[1], 4), std::memcpy(&p->r2, &s.rgb[2], 4), std::memcpy(&p->r3, &s.rgb[3], 4);
      set_xy(p->x0, p->y0, xy[0]), set_xy(p->x1, p->y1, xy[1]), set_xy(p->x2, p->y2, xy[2]);
      set_xy(p->x3, p->y3, xy[3]);
      std::memcpy(&p->u0, &s.uv[0], 2), std::memcpy(&p->u1, &s.uv[1], 2), std::memcpy(&p->u2, &s.uv[2], 2);
      std::memcpy(&p->u3, &s.uv[3], 2);
      p->clut = s.clut, p->tpage = s.tpage, p->pad2 = 0, p->pad3 = 0;
      prim = p, bytes = 0x34;
      break;
    }
    default:
      return;
  }
  psyq::addPrim(ot, prim);
  pool_a_bytes_used() += bytes;
  if ((s.type & 1) == 0) return;
  auto* dm = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1);
  if (dm == nullptr) return;
  psyq::SetDrawMode(dm, 0, 1, s.abr_tpage, nullptr);
  psyq::addPrim(ot, dm);
  pool_a_bytes_used() += 0xC;
}

// 0x800606f8 shard_update: draws the shard (its matrix: view * tilt * yaw at its position),
// then, unless paused, moves and spins it (gravity with flag 1), counts its delay down and
// fades its colours; it goes once black or below the ground (y > 0).
void PolyShard::update() {
  BattleGlobals& g = bg();
  ViewState& vw = view();
  RenderContext* ctx = g.prim_pool_a;
  if (ctx == nullptr || !arena_has_room(ctx, 0x34)) return;
  Gte& q = psyq::gte();
  const u32 semi = (flags & 4) != 0 ? 0x02000000u : 0u;
  const s32 axis[3] = {bcos(static_cast<u32>(angle[0])), 0, -bsin(static_cast<u32>(angle[0]))};
  psyq::MATRIX ry{}, ra{}, m{};
  rot_matrix_y(angle[0], &ry);
  rot_matrix_axis(axis, angle[1], &ra);
  psyq::SetRotMatrix(&vw.view_matrix);
  for (int c = 0; c < 3; ++c) rt_column(ra, &m, c);
  psyq::SetRotMatrix(&m);
  for (int c = 0; c < 3; ++c) rt_column(ry, &m, c);
  const s16 dx = static_cast<s16>((pos[0] - vw.view_eye[0]) >> 12);
  const s16 dy = static_cast<s16>((pos[1] - vw.view_eye[1]) >> 12);
  const s16 dz = static_cast<s16>((pos[2] - vw.view_eye[2]) >> 12);
  psyq::SetRotMatrix(&vw.view_matrix);
  q.write_data(Gte::VXY0, static_cast<u32>(static_cast<u16>(dx)) | static_cast<u32>(static_cast<u16>(dy)) << 16);
  q.write_data(Gte::VZ0, static_cast<u32>(static_cast<s32>(dz)));
  q.execute(Gte::kRtv0);
  const psyq::VECTOR tr{q.mac(1), q.mac(2), q.mac(3), 0};
  psyq::TransMatrix(&m, &tr);
  psyq::SetRotMatrix(&m);
  psyq::SetTransMatrix(&m);

  int ncol = 0;  // s7: the colours the fade runs over
  u32 xy[4] = {};
  bool ok = false;
  s32 z8 = 0;
  if (1 <= type && type <= 4) {
    ncol = type < 3 ? 1 : 3;
    load_vert(q, 0, vert[0]), load_vert(q, 1, vert[1]), load_vert(q, 2, vert[2]);
    q.execute(Gte::kRtpt);
    for (int i = 0; i < 3; ++i) xy[i] = q.read_data(Gte::SXY0 + i);
    if (0 <= gte_flag(q)) {
      q.execute(Gte::kAvsz3);
      z8 = static_cast<s32>(q.read_data(Gte::OTZ)) >> 3;
      ok = z8 + ot_bias < 0x600;
    }
  } else if (5 <= type && type <= 8) {
    ncol = type < 7 ? 1 : 4;
    load_vert(q, 0, vert[0]);
    q.execute(Gte::kRtps);
    xy[0] = q.read_data(Gte::SXY2);
    const s32 f0 = gte_flag(q);
    load_vert(q, 0, vert[1]), load_vert(q, 1, vert[2]), load_vert(q, 2, vert[3]);
    q.execute(Gte::kRtpt);
    for (int i = 0; i < 3; ++i) xy[1 + i] = q.read_data(Gte::SXY0 + i);
    if (0 <= f0 && 0 <= gte_flag(q)) {
      q.execute(Gte::kAvsz4);
      z8 = static_cast<s32>(q.read_data(Gte::OTZ)) >> 3;
      ok = z8 + ot_bias < 0x600;
    }
  }
  if (ok) shard_emit(*this, ctx, xy, z8, semi);

  if ((g.flags & 0xBF) != 0) return;
  if ((g.flags & 0x40) != 0 && (flags & 2) == 0) return;
  const s32 t = g.frame_ticks;
  for (int k = 0; k < 3; ++k) pos[k] = add32(pos[k], mul32(vel[k], t));
  for (int k = 0; k < 2; ++k)
    angle[k] = static_cast<s16>((static_cast<u32>(static_cast<u16>(angle[k])) + static_cast<u32>(mul32(spin[k], t))) & 0xFFF);
  if ((flags & 1) != 0) vel[1] += 0x800;
  const s32 left = static_cast<s32>(delay) - t;
  delay = left < 0 ? 0 : static_cast<u8>(left);
  // (Freed below the ground, it still fades this frame and may be freed again.)
  if (0 < pos[1]) shard_free(this);
  if (delay != 0) return;
  const s32 f = mul32(fade, t);
  int black = 0;
  for (int i = 0; i < ncol; ++i) {
    const u32 w = rgb[i];
    s32 r = static_cast<s32>(w & 0xFF) - f;
    if (r < 0) r = 0;
    s32 gr = static_cast<s32>((w & 0xFF00) >> 8) - f;
    if (gr < 0) gr = 0;
    s32 b = static_cast<s32>((w & 0xFF0000) >> 16) - f;
    if (b < 0) b = 0;
    if (r == 0 && gr == 0 && b == 0) ++black;
    rgb[i] = static_cast<u32>(r) | static_cast<u32>(gr) << 8 | static_cast<u32>(b) << 16;
  }
  if (black == ncol) shard_free(this);
}

// Record sizes of the model's 20 primitive sections (lit F3 G3 F3c G3n FT3 GT3 F4 G4 (never
// used) G4n FT4 GT4, unlit F3 G3 FT3 GT3 F4 G4 FT4 GT4).
constexpr u32 kSectionBytes[20] = {36, 52, 44, 60, 44, 60, 44, 68, 48, 80, 52, 76, 28, 36, 40, 48, 36, 48, 48, 60};

}  // namespace

// 0x8005f30c model_shatter: the shatter globals, the model matrix (tilt * yaw, scaled, at
// pos: the shards are in world space), then a shard per polygon of every part.
void model_shatter(const u8* model, const s32 pos[3], const s32 rot[3], const s32 scale[3], u16 clut_base,
                   u16 tpage_base, u16 flags, u16 abr_tpage, s32 ot_bias, s32 delay, s32 speed) {
  if (model == nullptr) return;
  ShatterState& st = g_shatter;
  ViewState& vw = view();
  Gte& q = psyq::gte();
  const s32 parts = static_cast<s32>(rd32(model));
  for (int k = 0; k < 3; ++k) st.centre[k] = pos[k] >> 12;
  st.speed = speed;
  st.flags = flags;
  st.ot_bias = ot_bias;
  st.abr = abr_tpage;
  st.fade = static_cast<s16>(speed / 4);
  if (st.fade < 2) st.fade = 2;
  const s32 axis[3] = {bcos(static_cast<u32>(rot[1])), 0, -bsin(static_cast<u32>(rot[1]))};
  psyq::MATRIX ry{}, ra{}, m{};
  rot_matrix_y(rot[1], &ry);
  rot_matrix_axis(axis, rot[2], &ra);
  psyq::SetRotMatrix(&ra);
  for (int c = 0; c < 3; ++c) rt_column(ry, &m, c);
  const psyq::VECTOR tr{pos[0] >> 12, pos[1] >> 12, pos[2] >> 12, 0};
  psyq::TransMatrix(&m, &tr);
  const psyq::VECTOR sc{scale[0], scale[1], scale[2], 0};
  psyq::ScaleMatrix(&m, &sc);
  psyq::SetRotMatrix(&m);
  psyq::SetTransMatrix(&m);

  const u8 d = static_cast<u8>(delay);
  const u8* r = model + 4;
  for (s32 part = 0; part < parts; ++part) {
    r += 4;  // the part's header word
    psyq::SetLightMatrix(&vw.light_matrix);
    psyq::SetColorMatrix(&vw.colour_matrix);
    gte_set_back_colour();
    for (int sec = 0; sec < 20; ++sec) {
      if (sec == 4 || sec == 10) q.write_data(Gte::RGBC, rd32(kGrey));
      if (sec == 12 || sec == 19) {
        // The unlit sections: the up light and the stage colour as the colour matrix.
        psyq::SetLightMatrix(&vw.unlit_light);
        psyq::SetColorMatrix(&vw.unlit_colour);
      }
      const u32 count = rd32(r);
      r += 4;
      if (sec == 8) {
        r += static_cast<size_t>(kSectionBytes[8]) * count;
        continue;
      }
      for (u32 i = 0; i < count; ++i, r += kSectionBytes[sec]) {
        PolyShard* s = shard_alloc(d);
        if (s == nullptr) return;
        switch (sec) {
          case 0:  // F3: normal, rgbc, v0 v1 v2
            shard_set_vertices(s, 3, r + 12);
            shard_light_flat(s, r, r + 8);
            break;
          case 1:  // G3: rgbc, n0 n1 n2, v0 v1 v2
            shard_set_vertices(s, 3, r + 28);
            shard_light_gouraud3(s, r + 4, r);
            break;
          case 2:  // F3c: c0 c1 c2, normal, v0 v1 v2
            shard_set_vertices(s, 3, r + 20);
            shard_light_colours(s, 3, r + 12, r);
            break;
          case 3:  // G3n: c0 c1 c2, n0 n1 n2, v0 v1 v2
            shard_set_vertices(s, 3, r + 36);
            shard_light_normals(s, 3, r + 12, r);
            break;
          case 4:  // FT3: uv0 clut, uv1 tpage, uv2, normal, v0 v1 v2
            shard_set_vertices(s, 3, r + 20);
            shard_light_flat_tex(s, r + 12);
            shard_set_texture(s, r, false, clut_base, tpage_base, abr_tpage);
            s->type = 2;
            break;
          case 5:  // GT3: uv0 clut, uv1 tpage, uv2, n0 n1 n2, v0 v1 v2
            shard_set_vertices(s, 3, r + 36);
            shard_light_gouraud3(s, r + 12, kGrey);
            shard_set_texture(s, r, false, clut_base, tpage_base, abr_tpage);
            s->type = 4;
            break;
          case 6:  // F4: normal, rgbc, v0 v1 v2 v3
            shard_set_vertices(s, 4, r + 12);
            shard_light_flat(s, r, r + 8);
            break;
          case 7:  // G4: rgbc, n0 n1 n2 n3, v0 v1 v2 v3
            shard_set_vertices(s, 4, r + 36);
            // ORIGINAL BUG (original_bugs.md S13): the record's colour (+0) is passed but never
            // loaded (shard_light_gouraud4 takes none), so these shards are lit with the RGBC
            // left over: the last lit F4's colour of this part, else 0x808080:
            //   shard_light_gouraud4(s, r + 4);
            psyq::gte().write_data(Gte::RGBC, rd32(r));
            shard_light_gouraud4(s, r + 4);
            break;
          case 9:  // G4n: c0 c1 c2 c3, n0 n1 n2 n3, v0 v1 v2 v3
            shard_set_vertices(s, 4, r + 48);
            shard_light_normals(s, 4, r + 16, r);
            break;
          case 10:  // FT4: uv0 clut, uv1 tpage, uv2 uv3, normal, v0 v1 v2 v3
            shard_set_vertices(s, 4, r + 20);
            shard_light_flat_tex(s, r + 12);
            shard_set_texture(s, r, true, clut_base, tpage_base, abr_tpage);
            s->type = 6;
            break;
          case 11:  // GT4: uv0 clut, uv1 tpage, uv2 uv3, n0 n1 n2 n3, v0 v1 v2 v3 (RGBC 0x808080)
            shard_set_vertices(s, 4, r + 44);
            shard_light_gouraud4(s, r + 12);
            shard_set_texture(s, r, true, clut_base, tpage_base, abr_tpage);
            s->type = 8;
            break;
          case 12:  // unlit F3: rgbc, v0 v1 v2
            shard_set_vertices(s, 3, r + 4);
            shard_light_flat(s, kUpNormal, r);
            break;
          case 13:  // unlit G3: c0 c1 c2, v0 v1 v2
            // ORIGINAL BUG (original_bugs.md S13): the vertices are read from +4 (model_render:
            // +12), so the shard is (c1 c2 as a vertex, v0, v1):
            //   shard_set_vertices(s, 3, r + 4);
            shard_set_vertices(s, 3, r + 12);
            shard_light_colours(s, 3, kUpNormal, r);
            break;
          case 14:  // unlit FT3: uv0 clut, uv1 tpage, uv2, rgbc, v0 v1 v2
            shard_set_vertices(s, 3, r + 16);
            shard_light_flat(s, kUpNormal, r + 12);
            shard_set_texture(s, r, false, clut_base, tpage_base, abr_tpage);
            // ORIGINAL BUG (original_bugs.md S13): kind 6 (FT4) for a triangle: drawn as a quad
            // whose fourth vertex (+0x6c) and uv3 (+0x1a) are left from the slot's last use:
            //   s->type = 6;
            s->type = 2;
            break;
          case 15:  // unlit GT3: uv0 clut, uv1 tpage, uv2, c0 c1 c2, v0 v1 v2
            shard_set_vertices(s, 3, r + 24);
            shard_light_colours(s, 3, kUpNormal, r + 12);
            shard_set_texture(s, r, false, clut_base, tpage_base, abr_tpage);
            // ORIGINAL BUG (original_bugs.md S13): kind 8 (GT4) for a triangle: a quad with the
            // fourth vertex, uv3 and colour (+0x28, which the fade also waits on) left from the
            // slot's last use:
            //   s->type = 8;
            s->type = 4;
            break;
          case 16:  // unlit F4: rgbc, v0 v1 v2 v3
            shard_set_vertices(s, 4, r + 4);
            shard_light_flat(s, kUpNormal, r);
            break;
          case 17:  // unlit G4: c0 c1 c2 c3, v0 v1 v2 v3
            shard_set_vertices(s, 4, r + 16);
            shard_light_colours(s, 4, kUpNormal, r);
            break;
          case 18:  // unlit FT4: uv0 clut, uv1 tpage, uv2 uv3, rgbc, v0 v1 v2 v3
            shard_set_vertices(s, 4, r + 16);
            shard_light_flat(s, kUpNormal, r + 12);
            shard_set_texture(s, r, true, clut_base, tpage_base, abr_tpage);
            s->type = 6;
            break;
          case 19:  // unlit GT4: uv0 clut, uv1 tpage, uv2 uv3, c0 c1 c2 c3, v0 v1 v2 v3
            shard_set_vertices(s, 4, r + 28);
            shard_light_colours(s, 4, kUpNormal, r + 12);
            shard_set_texture(s, r, true, clut_base, tpage_base, abr_tpage);
            s->type = 8;
            break;
          default:
            break;
        }
      }
    }
  }
}

s32 shatter_ot_bias() { return g_shatter.ot_bias; }

void shatter_reset() {
  ShatterState& st = g_shatter;
  for (PolyShard& s : st.pool) {
    if (s.type != 0) shard_free(&s);
    static_cast<ShardData&>(s) = ShardData{};
  }
  st.cursor = 0;
  st.flags = 0;
  st.abr = 0;
  st.fade = 0;
  st.ot_bias = 0;
  st.centre[0] = st.centre[1] = st.centre[2] = 0;
  st.speed = 0;
}

}  // namespace ss::game::battle
