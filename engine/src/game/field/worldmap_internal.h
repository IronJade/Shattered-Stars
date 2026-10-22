#pragma once

#include <cstring>

#include "core/types.h"
#include "game/common/math.h"
#include "game/field/script_vm.h"
#include "game/field/worldmap.h"
#include "psx/gte.h"
#include "runtime/libgte.h"

// Helpers shared by the world-map files: register-level GTE access (the overlay uses
// PsyQ's inline GTE macros, so the port issues the same loads and commands) and the
// little-endian accessors for the terrain data.
namespace ss::game::field::wm {

using G = psx::Gte;
inline G& gte() { return psyq::gte(); }

inline s32 rd32(const u8* p) { return static_cast<s32>(read_u32(p)); }
inline s16 rd16(const u8* p) { return static_cast<s16>(read_u16(p)); }
inline u32 ru32(const u8* p) { return read_u32(p); }
inline void wr32(u8* p, u32 v) { std::memcpy(p, &v, 4); }
inline void wr16(u8* p, u16 v) { std::memcpy(p, &v, 2); }
inline s32 mul(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }

inline s32 rsin(s32 a) { return sin4096(static_cast<u32>(a)); }
inline s32 rcos(s32 a) { return cos4096(static_cast<u32>(a)); }

// Vertex registers: lwc2 of an 8-byte SVECTOR (the VZ word is sign-extended from 16 bits).
inline void ldv(int i, s16 x, s16 y, s16 z) {
  gte().write_data(2 * i, static_cast<u16>(x) | static_cast<u32>(static_cast<u16>(y)) << 16);
  gte().write_data(2 * i + 1, static_cast<u32>(static_cast<s32>(z)));
}
inline void ldv_raw(int i, const u8* p) {
  gte().write_data(2 * i, read_u32(p));
  gte().write_data(2 * i + 1, read_u32(p + 4));
}
inline void ldv_raw(int i, const s16* p) {
  ldv(i, p[0], p[1], p[2]);
}
inline void ld_ir0(s32 v) { gte().write_data(G::IR0, static_cast<u32>(v)); }
inline void ld_ir(s32 a, s32 b, s32 c) {
  gte().write_data(G::IR1, static_cast<u32>(a));
  gte().write_data(G::IR2, static_cast<u32>(b));
  gte().write_data(G::IR3, static_cast<u32>(c));
}
inline s32 ir(int i) { return static_cast<s32>(gte().read_data(G::IR0 + i)); }
inline s32 mac(int i) { return static_cast<s32>(gte().read_data(G::MAC0 + i)); }
inline u32 sxy(int i) { return gte().read_data(G::SXY0 + i); }
inline s32 otz() { return static_cast<s32>(gte().read_data(G::OTZ)); }
inline s32 flag() { return static_cast<s32>(gte().read_ctrl(G::FLAG)); }
inline u32 rgb2() { return gte().read_data(G::RGB2); }
inline void ld_rgbc(u32 v) { gte().write_data(G::RGBC, v); }
inline void ld_fc(s32 r, s32 g, s32 b) {
  gte().write_ctrl(G::RFC, static_cast<u32>(r));
  gte().write_ctrl(G::GFC, static_cast<u32>(g));
  gte().write_ctrl(G::BFC, static_cast<u32>(b));
}
inline void run(u32 cmd) { gte().execute(cmd); }

// m = rot * m column by column (ldclmv / rtir / stclmv with rot loaded as RT).
inline void rot_columns(const psyq::MATRIX& rot, psyq::MATRIX* m) {
  psyq::SetRotMatrix(&rot);
  for (int c = 0; c < 3; ++c) {
    ld_ir(m->m[0][c], m->m[1][c], m->m[2][c]);
    run(G::kRtir);
    m->m[0][c] = static_cast<s16>(ir(1));
    m->m[1][c] = static_cast<s16>(ir(2));
    m->m[2][c] = static_cast<s16>(ir(3));
  }
}

// The curved-world bend (0x8007f1f4 / 0x8007f3e4 / 0x8007f648 / the terrain draw): after
// RTPS with the world matrix, (IR1, IR2, IR3) is the point in camera-aligned space;
// the curvature table (+0x800) gives sin / cos pairs for x and for z - (offset +
// 0x4b0), and GPF12 scales them by the height above the sphere's centre (0x4000 - y).
struct Bent {
  s16 x, y, z;
};
inline u32 curve_word(s32 index) {
  const std::vector<u8>& t = world().curve;
  if (t.size() < 0x804) return 0x10000000;  // no table yet: cos 1, sin 0
  // The console reads past the table for points beyond the horizon; the port holds the
  // last entry (the vertex is far outside the screen either way).
  s32 off = 0x800 + index * 4;
  if (off < 0) off = 0;
  if (off + 4 > static_cast<s32>(t.size())) off = static_cast<s32>(t.size() & ~size_t{3}) - 4;
  return read_u32(t.data() + off);
}
inline Bent bend(s32 ir1, s32 ir2, s32 ir3) {
  const s32 ix = ir1 >> 2;
  const s32 iz = (ir3 - (world().curve_offset + 0x4b0)) >> 2;
  u32 wx, wz;
  s32 sx, sz;
  if (ix < 0) {
    wx = curve_word(-ix);
    sx = -static_cast<s32>(wx & 0xffff);
  } else {
    wx = curve_word(ix);
    sx = static_cast<s32>(wx & 0xffff);
  }
  if (iz < 0) {
    wz = curve_word(-iz);
    sz = -static_cast<s32>(wz & 0xffff);
  } else {
    wz = curve_word(iz);
    sz = static_cast<s32>(wz & 0xffff);
  }
  ld_ir0(0x4000 - ir2);
  ld_ir(sx, -mul(static_cast<s32>(wx >> 16), static_cast<s32>(wz >> 16)) >> 12, sz);
  run(G::kGpf12);
  Bent b;
  b.x = static_cast<s16>(ir(1));
  b.y = static_cast<s16>(ir(2) + 0x4000);
  b.z = static_cast<s16>(ir(3) + world().curve_offset + 0x4b0);
  return b;
}

s32 world_ratan2(s32 y, s32 x);
s32 world_sqrt(s32 v);

}  // namespace ss::game::field::wm
