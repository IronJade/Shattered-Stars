#include "runtime/libgte.h"

#include <algorithm>
#include <array>

#include "game/common/math.h"

namespace ss::psyq {
namespace {

using G = psx::Gte;

u32 pack(s16 lo, s16 hi) { return static_cast<u16>(lo) | static_cast<u32>(static_cast<u16>(hi)) << 16; }

// lw of the five words of MATRIX.m, then ctc2 to five consecutive registers.
void load_matrix(int first, const MATRIX* m) {
  auto e = [m](int k) { return m->m[k / 3][k % 3]; };
  for (int k = 0; k < 4; ++k) gte().write_ctrl(first + k, pack(e(2 * k), e(2 * k + 1)));
  gte().write_ctrl(first + 4, static_cast<u16>(m->m[2][2]));
}

// lwc2 of an SVECTOR's two words into VXYn/VZn.
void load_vertex(int n, const SVECTOR* v) {
  gte().write_data(G::VXY0 + 2 * n, pack(v->vx, v->vy));
  gte().write_data(G::VZ0 + 2 * n, pack(v->vz, v->pad));
}

void load_color(const CVECTOR* c) {
  gte().write_data(G::RGBC, c->r | c->g << 8 | c->b << 16 | static_cast<u32>(c->cd) << 24);
}

void store_color(int reg, CVECTOR* c) {
  const u32 v = gte().read_data(reg);
  *c = {static_cast<u8>(v), static_cast<u8>(v >> 8), static_cast<u8>(v >> 16), static_cast<u8>(v >> 24)};
}

// lwc2 of a VECTOR's three words into IR1-3 (each keeps its low 16 bits).
void load_ir(const VECTOR* v) {
  gte().write_data(G::IR1, static_cast<u32>(v->vx));
  gte().write_data(G::IR2, static_cast<u32>(v->vy));
  gte().write_data(G::IR3, static_cast<u32>(v->vz));
}

s32 data(int reg) { return static_cast<s32>(gte().read_data(reg)); }

// The rotation part of MulMatrix0/MulMatrix/CompMatrix: RT = m0, then RT times
// each column of m1, saturated to IR (sf = 1, lm = 0). All of m1 is read before
// anything is written, so the result may alias either input.
void rotate_columns(const MATRIX* m0, const MATRIX* m1, MATRIX* out) {
  load_matrix(G::R11R12, m0);
  s16 r[3][3];
  for (int j = 0; j < 3; ++j) {
    gte().set_vertex(0, m1->m[0][j], m1->m[1][j], m1->m[2][j]);
    gte().execute(G::kRtv0);
    for (int i = 0; i < 3; ++i) r[i][j] = gte().ir(i + 1);
  }
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) out->m[i][j] = r[i][j];
}

struct MatrixStack {
  std::array<std::array<u32, 8>, 20> entries{};
  int depth = 0;
};
MatrixStack& matrix_stack() {
  static MatrixStack stack;
  return stack;
}

}  // namespace

psx::Gte& gte() {
  static psx::Gte instance;
  return instance;
}

// InitGeom also patches the BIOS exception handler and enables cop2; neither
// exists here. It leaves every other register as it was.
void InitGeom() {
  gte().write_ctrl(G::ZSF3, 0x155);
  gte().write_ctrl(G::ZSF4, 0x100);
  gte().write_ctrl(G::H, 1000);
  gte().write_ctrl(G::DQA, static_cast<u32>(-0x1062));
  gte().write_ctrl(G::DQB, 0x1400000);
  gte().write_ctrl(G::OFX, 0);
  gte().write_ctrl(G::OFY, 0);
}

void SetRotMatrix(const MATRIX* m) { load_matrix(G::R11R12, m); }
void SetLightMatrix(const MATRIX* m) { load_matrix(G::L11L12, m); }
void SetColorMatrix(const MATRIX* m) { load_matrix(G::LR1LR2, m); }

void SetTransMatrix(const MATRIX* m) {
  for (int i = 0; i < 3; ++i) gte().write_ctrl(G::TRX + i, static_cast<u32>(m->t[i]));
}

void SetBackColor(s32 rbk, s32 gbk, s32 bbk) {
  gte().write_ctrl(G::RBK, static_cast<u32>(rbk) << 4);
  gte().write_ctrl(G::GBK, static_cast<u32>(gbk) << 4);
  gte().write_ctrl(G::BBK, static_cast<u32>(bbk) << 4);
}

void SetFarColor(s32 rfc, s32 gfc, s32 bfc) {
  gte().write_ctrl(G::RFC, static_cast<u32>(rfc) << 4);
  gte().write_ctrl(G::GFC, static_cast<u32>(gfc) << 4);
  gte().write_ctrl(G::BFC, static_cast<u32>(bfc) << 4);
}

void SetGeomOffset(s32 ofx, s32 ofy) {
  gte().write_ctrl(G::OFX, static_cast<u32>(ofx) << 16);
  gte().write_ctrl(G::OFY, static_cast<u32>(ofy) << 16);
}

void SetGeomScreen(s32 h) { gte().write_ctrl(G::H, static_cast<u32>(h)); }
s32 ReadGeomScreen() { return static_cast<s32>(gte().read_ctrl(G::H)); }

// Fog: DQA = -320 * a / h, DQB = 0x1400000 (1.25 in 8.24).
void SetFogNear(s32 a, s32 h) {
  if (h == 0) return;  // the console traps (break 7)
  const s32 num = static_cast<s32>(0u - static_cast<u32>(a) * 320u);  // wrapping, as the shifts and subu do
  if (num == INT32_MIN && h == -1) return;                             // break 6
  gte().write_ctrl(G::DQA, static_cast<u32>(num / h));
  gte().write_ctrl(G::DQB, 0x1400000);
}

// Fog that starts at depth a and is complete at b, for screen distance h. Does
// nothing when b - a < 100.
void SetFogNearFar(s32 a, s32 b, s32 h) {
  const s32 range = static_cast<s32>(static_cast<u32>(b) - static_cast<u32>(a));
  if (range < 100 || h == 0) return;
  const s32 ab = static_cast<s32>((0u - static_cast<u32>(a)) * static_cast<u32>(b));  // mult, low word
  const s32 dqa = static_cast<s32>(static_cast<u32>(ab / range) << 8);
  const s32 dqb = static_cast<s32>(static_cast<u32>(b) << 12) / range;
  if (dqa == INT32_MIN && h == -1) return;
  gte().write_ctrl(G::DQA, static_cast<u32>(std::clamp(dqa / h, -0x8000, 0x7FFF)));
  gte().write_ctrl(G::DQB, static_cast<u32>(dqb) << 12);
}

s32 RotTransPers(const SVECTOR* v0, s32* sxy, s32* p, s32* flag) {
  load_vertex(0, v0);
  gte().execute(G::kRtps);
  *sxy = data(G::SXY2);
  *p = data(G::IR0);
  *flag = static_cast<s32>(gte().flag());
  return data(G::SZ3) >> 2;
}

s32 RotTransPers3(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, s32* sxy0, s32* sxy1, s32* sxy2, s32* p,
                  s32* flag) {
  load_vertex(0, v0);
  load_vertex(1, v1);
  load_vertex(2, v2);
  gte().execute(G::kRtpt);
  *sxy0 = data(G::SXY0);
  *sxy1 = data(G::SXY1);
  *sxy2 = data(G::SXY2);
  *p = data(G::IR0);
  *flag = static_cast<s32>(gte().flag());
  return data(G::SZ3) >> 2;
}

// RTPT for v0-v2, then RTPS for v3; the flag is both commands' FLAGs or'd.
s32 RotTransPers4(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, const SVECTOR* v3, s32* sxy0, s32* sxy1,
                  s32* sxy2, s32* sxy3, s32* p, s32* flag) {
  load_vertex(0, v0);
  load_vertex(1, v1);
  load_vertex(2, v2);
  gte().execute(G::kRtpt);
  *sxy0 = data(G::SXY0);
  *sxy1 = data(G::SXY1);
  *sxy2 = data(G::SXY2);
  const u32 first = gte().flag();
  load_vertex(0, v3);
  gte().execute(G::kRtps);
  *sxy3 = data(G::SXY2);
  *p = data(G::IR0);
  *flag = static_cast<s32>(gte().flag() | first);
  return data(G::SZ3) >> 2;
}

void RotTrans(const SVECTOR* v0, VECTOR* v1, s32* flag) {
  load_vertex(0, v0);
  gte().execute(G::kRtv0tr);
  v1->vx = gte().mac(1);
  v1->vy = gte().mac(2);
  v1->vz = gte().mac(3);
  *flag = static_cast<s32>(gte().flag());
}

// The flag is read after RTPT, before AVSZ3 clears it.
s32 RotAverage3(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, s32* sxy0, s32* sxy1, s32* sxy2, s32* p,
                s32* flag) {
  load_vertex(0, v0);
  load_vertex(1, v1);
  load_vertex(2, v2);
  gte().execute(G::kRtpt);
  *sxy0 = data(G::SXY0);
  *sxy1 = data(G::SXY1);
  *sxy2 = data(G::SXY2);
  *flag = static_cast<s32>(gte().flag());
  *p = data(G::IR0);
  gte().execute(G::kAvsz3);
  return data(G::OTZ);
}

s32 RotAverage4(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, const SVECTOR* v3, s32* sxy0, s32* sxy1,
                s32* sxy2, s32* sxy3, s32* p, s32* flag) {
  RotTransPers4(v0, v1, v2, v3, sxy0, sxy1, sxy2, sxy3, p, flag);
  gte().execute(G::kAvsz4);
  return data(G::OTZ);
}

s32 NormalClip(s32 sxy0, s32 sxy1, s32 sxy2) {
  gte().write_data(G::SXY0, static_cast<u32>(sxy0));
  gte().write_data(G::SXY2, static_cast<u32>(sxy2));
  gte().write_data(G::SXY1, static_cast<u32>(sxy1));
  gte().execute(G::kNclip);
  return data(G::MAC0);
}

s32 AverageZ3(s32 sz0, s32 sz1, s32 sz2) {
  gte().write_data(G::SZ1, static_cast<u32>(sz0));
  gte().write_data(G::SZ2, static_cast<u32>(sz1));
  gte().write_data(G::SZ3, static_cast<u32>(sz2));
  gte().execute(G::kAvsz3);
  return data(G::OTZ);
}

s32 AverageZ4(s32 sz0, s32 sz1, s32 sz2, s32 sz3) {
  gte().write_data(G::SZ0, static_cast<u32>(sz0));
  gte().write_data(G::SZ1, static_cast<u32>(sz1));
  gte().write_data(G::SZ2, static_cast<u32>(sz2));
  gte().write_data(G::SZ3, static_cast<u32>(sz3));
  gte().execute(G::kAvsz4);
  return data(G::OTZ);
}

void LocalLight(const SVECTOR* v0, VECTOR* v1) {
  load_vertex(0, v0);
  gte().execute(G::mvmva(true, G::Mx::Light, G::Vec::V0, G::Tr::None, true));
  v1->vx = gte().ir(1);
  v1->vy = gte().ir(2);
  v1->vz = gte().ir(3);
}

void NormalColor(const SVECTOR* v0, CVECTOR* v1) {
  load_vertex(0, v0);
  gte().execute(G::kNcs);
  store_color(G::RGB2, v1);
}

void NormalColorCol(const SVECTOR* v0, const CVECTOR* v1, CVECTOR* v2) {
  load_vertex(0, v0);
  load_color(v1);
  gte().execute(G::kNccs);
  store_color(G::RGB2, v2);
}

void NormalColorCol3(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, const CVECTOR* v3, CVECTOR* v4,
                     CVECTOR* v5, CVECTOR* v6) {
  load_vertex(0, v0);
  load_vertex(1, v1);
  load_vertex(2, v2);
  load_color(v3);
  gte().execute(G::kNcct);
  store_color(G::RGB0, v4);
  store_color(G::RGB1, v5);
  store_color(G::RGB2, v6);
}

void NormalColorDpq(const SVECTOR* v0, const CVECTOR* v1, s32 p, CVECTOR* v2) {
  load_vertex(0, v0);
  load_color(v1);
  gte().write_data(G::IR0, static_cast<u32>(p));
  gte().execute(G::kNcds);
  store_color(G::RGB2, v2);
}

void NormalColorDpq3(const SVECTOR* v0, const SVECTOR* v1, const SVECTOR* v2, const CVECTOR* v3, s32 p, CVECTOR* v4,
                     CVECTOR* v5, CVECTOR* v6) {
  load_vertex(0, v0);
  load_vertex(1, v1);
  load_vertex(2, v2);
  load_color(v3);
  gte().write_data(G::IR0, static_cast<u32>(p));
  gte().execute(G::kNcdt);
  store_color(G::RGB0, v4);
  store_color(G::RGB1, v5);
  store_color(G::RGB2, v6);
}

void ColorCol(const VECTOR* v0, const CVECTOR* v1, CVECTOR* v2) {
  load_ir(v0);
  load_color(v1);
  gte().execute(G::kCc);
  store_color(G::RGB2, v2);
}

void ColorDpq(const VECTOR* v0, const CVECTOR* v1, s32 p, CVECTOR* v2) {
  load_ir(v0);
  load_color(v1);
  gte().write_data(G::IR0, static_cast<u32>(p));
  gte().execute(G::kCdp);
  store_color(G::RGB2, v2);
}

void DpqColor(const CVECTOR* v0, s32 p, CVECTOR* v1) {
  load_color(v0);
  gte().write_data(G::IR0, static_cast<u32>(p));
  gte().execute(G::kDpcs);
  store_color(G::RGB2, v1);
}

VECTOR* ApplyMatrix(const MATRIX* m, const SVECTOR* v0, VECTOR* v1) {
  load_matrix(G::R11R12, m);
  load_vertex(0, v0);
  gte().execute(G::kRtv0);
  v1->vx = gte().mac(1);
  v1->vy = gte().mac(2);
  v1->vz = gte().mac(3);
  return v1;
}

SVECTOR* ApplyMatrixSV(const MATRIX* m, const SVECTOR* v0, SVECTOR* v1) {
  load_matrix(G::R11R12, m);
  load_vertex(0, v0);
  gte().execute(G::kRtv0);
  v1->vx = gte().ir(1);
  v1->vy = gte().ir(2);
  v1->vz = gte().ir(3);
  return v1;
}

// The GTE only multiplies 16-bit vectors, so each component is split by
// magnitude into x = hi * 0x8000 + lo (both parts carrying x's sign): RT * hi
// unshifted, times 8, plus (RT * lo) >> 12. All in wrapping 32-bit arithmetic.
VECTOR* ApplyMatrixLV(const MATRIX* m, const VECTOR* v0, VECTOR* v1) {
  load_matrix(G::R11R12, m);
  const s32 in[3] = {v0->vx, v0->vy, v0->vz};
  s32 hi[3], lo[3];
  for (int i = 0; i < 3; ++i) {
    const u32 mag = in[i] < 0 ? 0u - static_cast<u32>(in[i]) : static_cast<u32>(in[i]);
    hi[i] = static_cast<s32>(mag) >> 15;
    lo[i] = static_cast<s32>(mag & 0x7FFF);
    if (in[i] < 0) {
      hi[i] = static_cast<s32>(0u - static_cast<u32>(hi[i]));
      lo[i] = -lo[i];
    }
  }
  for (int i = 0; i < 3; ++i) gte().write_data(G::IR1 + i, static_cast<u32>(hi[i]));
  gte().execute(G::kRtirSf0);
  u32 high[3];
  for (int i = 0; i < 3; ++i) high[i] = static_cast<u32>(gte().mac(i + 1)) << 3;
  for (int i = 0; i < 3; ++i) gte().write_data(G::IR1 + i, static_cast<u32>(lo[i]));
  gte().execute(G::kRtir);
  v1->vx = static_cast<s32>(static_cast<u32>(gte().mac(1)) + high[0]);
  v1->vy = static_cast<s32>(static_cast<u32>(gte().mac(2)) + high[1]);
  v1->vz = static_cast<s32>(static_cast<u32>(gte().mac(3)) + high[2]);
  return v1;
}

MATRIX* MulMatrix0(const MATRIX* m0, const MATRIX* m1, MATRIX* m2) {
  rotate_columns(m0, m1, m2);
  return m2;
}

MATRIX* MulMatrix(MATRIX* m0, const MATRIX* m1) {
  rotate_columns(m0, m1, m0);
  return m0;
}

// m2 = m0 * m1 and t2 = m0 * t1 + t0, where t1 is truncated to 16 bits per
// component on its way into V0 (as the library does).
MATRIX* CompMatrix(const MATRIX* m0, const MATRIX* m1, MATRIX* m2) {
  const s32 t0[3] = {m0->t[0], m0->t[1], m0->t[2]};
  rotate_columns(m0, m1, m2);
  gte().set_vertex(0, static_cast<s16>(m1->t[0]), static_cast<s16>(m1->t[1]), static_cast<s16>(m1->t[2]));
  gte().execute(G::kRtv0);
  for (int i = 0; i < 3; ++i) m2->t[i] = static_cast<s32>(static_cast<u32>(gte().mac(i + 1)) + static_cast<u32>(t0[i]));
  return m2;
}

// Rx * Ry * Rz with PsyQ's own order of products and >> 12 roundings, which
// fixes the low bits of the result.
MATRIX* RotMatrix(const SVECTOR* r, MATRIX* m) {
  // rcossin_tbl lookup: a negative angle reads the table at -angle and negates the sine.
  auto sin_cos = [](s16 a, s32& s, s32& c) {
    const u32 i = static_cast<u32>(a < 0 ? -a : a) & 0xFFF;
    s = game::sin4096(i);
    c = game::cos4096(i);
    if (a < 0) s = -s;
  };
  s32 sx, cx, sy, cy, sz, cz;
  sin_cos(r->vx, sx, cx);
  sin_cos(r->vy, sy, cy);
  sin_cos(r->vz, sz, cz);
  auto s = [](s32 v) { return static_cast<s16>(v); };
  m->m[0][2] = s(sy);
  m->m[1][2] = s(-(cy * sx) >> 12);
  m->m[2][2] = s((cy * cx) >> 12);
  m->m[0][0] = s((cz * cy) >> 12);
  m->m[0][1] = s(-(sz * cy) >> 12);
  const s32 a = (cz * -sy) >> 12;
  m->m[1][0] = s(((sz * cx) >> 12) - ((a * sx) >> 12));
  m->m[2][0] = s(((sz * sx) >> 12) + ((a * cx) >> 12));
  const s32 b = (sz * -sy) >> 12;
  m->m[1][1] = s(((cz * cx) >> 12) + ((b * sx) >> 12));
  m->m[2][1] = s(((cz * sx) >> 12) - ((b * cx) >> 12));
  return m;
}

MATRIX* TransMatrix(MATRIX* m, const VECTOR* v) {
  m->t[0] = v->vx;
  m->t[1] = v->vy;
  m->t[2] = v->vz;
  return m;
}

// m[i][j] = (m[i][j] * v[j]) >> 12, the product kept to its low 32 bits (multu/mflo).
MATRIX* ScaleMatrix(MATRIX* m, const VECTOR* v) {
  const s32 scale[3] = {v->vx, v->vy, v->vz};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      const u32 product = static_cast<u32>(static_cast<s32>(m->m[i][j])) * static_cast<u32>(scale[j]);
      m->m[i][j] = static_cast<s16>(static_cast<s32>(product) >> 12);
    }
  return m;
}

// PsyQ keeps a 20-entry stack of RT and TR; overflow and underflow print an
// error there and change nothing, as here.
void PushMatrix() {
  auto& st = matrix_stack();
  if (st.depth >= static_cast<int>(st.entries.size())) return;
  auto& e = st.entries[st.depth++];
  for (int k = 0; k < 8; ++k) e[k] = gte().read_ctrl(k);
}

void PopMatrix() {
  auto& st = matrix_stack();
  if (st.depth <= 0) return;
  const auto& e = st.entries[--st.depth];
  for (int k = 0; k < 8; ++k) gte().write_ctrl(k, e[k]);
}

}  // namespace ss::psyq
