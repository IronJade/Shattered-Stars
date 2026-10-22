#pragma once

// Shared helpers of the effects code (effects*.cpp): the inline GTE sequences the
// overlay uses (lwc2 / cop2 / mfc2), run on the port's GTE so that every register,
// FIFO and stale value (OTZ) behaves as on the console.

#include <cstring>

#include "game/field/effects.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"

namespace ss::game::field::fx {

using G = psx::Gte;
inline psx::Gte& gte() { return psyq::gte(); }

// lwc2 VXYn / VZn from an SVECTOR.
inline void ldv(int i, s16 x, s16 y, s16 z) {
  gte().write_data(G::VXY0 + 2 * i, static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16);
  gte().write_data(G::VZ0 + 2 * i, static_cast<u32>(static_cast<u16>(z)));
}
inline void ldv(int i, const psyq::SVECTOR& v) { ldv(i, v.vx, v.vy, v.vz); }
inline void run(u32 command) { gte().execute(command); }
inline u32 sxy(int i) { return gte().read_data(G::SXY0 + i); }
inline u32 otz() { return gte().read_data(G::OTZ); }
inline u32 rgb(int i) { return gte().read_data(G::RGB0 + i); }
inline s32 mac(int i) { return static_cast<s32>(gte().read_data(G::MAC0 + i)); }
inline s16 ir(int i) { return static_cast<s16>(gte().read_data(G::IR0 + i)); }
inline void ld_ir(s32 a, s32 b, s32 c) {
  gte().write_data(G::IR1, static_cast<u32>(a));
  gte().write_data(G::IR2, static_cast<u32>(b));
  gte().write_data(G::IR3, static_cast<u32>(c));
}

// A projected point's coordinates moved to the screen (sh of the low / high halves
// minus the view origin), and the overlay's off-screen tests: x within [-40, 380),
// y within [-60, 240), done on the 16-bit results.
inline s16 screen_x(u32 sxy, s16 ox) { return static_cast<s16>(static_cast<u16>(sxy) - static_cast<u16>(ox)); }
inline s16 screen_y(u32 sxy, s16 oy) { return static_cast<s16>(static_cast<u16>(sxy >> 16) - static_cast<u16>(oy)); }
inline bool x_visible(s16 x) { return static_cast<u16>(x + 0x28) < 0x1A4; }
inline bool y_visible(s16 y) { return static_cast<u16>(y + 0x3C) < 0x12C; }

// Raw GP0 words of a primitive (after the host tag).
inline u32* words(void* prim) { return psyq::prim_words(static_cast<psyq::P_TAG*>(prim)); }
inline void put_xy(void* prim, int word, s16 x, s16 y) {
  words(prim)[word] = static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16;
}

// A primitive from the screen's arena; null when it is full.
void* alloc_prim(size_t bytes);
// Links 'prim' (len GP0 words) into the particle's layer at 'index' (current half),
// then, unless 'mode_tpage' < 0, a DR_MODE (SetDrawMode(dfe 0, dtd 1, mode_tpage)) in
// front of it: 0x20 = additive (abr 1).
constexpr int kNoMode = -1, kAdditive = 0x20;
void link(Particle* p, int index, void* prim, u32 len, int mode_tpage);

}  // namespace ss::game::field::fx
