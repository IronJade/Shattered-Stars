// The field's visual effects, part 2 (overlay #3101 0x8008aa70-0x8008d928): the nine
// particle behaviours and their drawing helpers. Everything goes through the GTE in the
// original's order (rtps / rtpt / avsz3 / avsz4 / gpf12 / nccs / ncct) so stale registers
// behave alike: fx_draw_line and the burst read OTZ without an average of their own, so
// their depth is whatever the last avsz left (in practice an earlier particle's).
// Primitives come from the screen's arena and go into the particle's OT layer at
// 0x820 - depth; additive ones get a DR_MODE (abr 1) linked in front.
#include <cstring>

#include "game/common/math.h"
#include "game/common/program_image.h"
#include "game/field/camera.h"
#include "game/field/effects_internal.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "runtime/runtime.h"

namespace ss::game::field {

using fx::G;

namespace fx {

void* alloc_prim(size_t bytes) {
  Screen* s = field_screen();
  return s != nullptr && s->current != nullptr ? s->alloc(bytes) : nullptr;
}

void link(Particle* p, int index, void* prim, u32 len, int mode_tpage) {
  if (index < 0 || index >= kFxLayerEntries) {  // the burst can index outside (no check)
    runtime::Runtime::get().log_once("fx:ot_range", "fx: OT layer index out of range; primitive dropped");
    return;
  }
  psyq::P_TAG* ot = p->layer->current();
  psyq::setlen(prim, len);
  psyq::addPrim(&ot[index], prim);
  if (mode_tpage < 0) return;
  auto* mode = static_cast<psyq::DR_MODE*>(alloc_prim(sizeof(psyq::DR_MODE)));
  if (mode == nullptr) return;
  // SetDrawMode(mode, 0, 1, tpage, NULL): libgpu leaves the texture-window word 0 (a NOP)
  // when there is no window.
  mode->code[0] = 0xE1000000u | (static_cast<u32>(mode_tpage) & 0x9FF) | 1u << 9;
  mode->code[1] = 0;
  psyq::setlen(mode, 2);
  psyq::addPrim(&ot[index], mode);
}

}  // namespace fx

namespace {

constexpr int kFieldOverlayEntry = 0xC1D;  // #3101 at 0x8007e000

s32 mul(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }
s32 cosv(s32 a) { return cos4096(static_cast<u32>(a)); }
s32 sinv(s32 a) { return sin4096(static_cast<u32>(a)); }
s16 origin_x() { return fx_state().origin[0]; }
s16 origin_y() { return fx_state().origin[1]; }

// gte ldlvl / sqr0 / stlvnl, then SquareRoot0 of the sum.
s32 length_of(const s32 v[3]) {
  fx::ld_ir(v[0], v[1], v[2]);
  fx::run(G::kSqr0);
  return SquareRoot0(fx::mac(1) + fx::mac(2) + fx::mac(3));
}

u16 ovl_u16(u32 address) {
  const u8* p = program_data_at(kFieldOverlayEntry, 0x8007E000, address);
  if (p == nullptr) return 0;
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

}  // namespace

void fx_set_local_transform(const Particle* p) {
  const CameraBlock& cam = camera_block();
  const s16 rel[3] = {static_cast<s16>((p->pos[0] - cam.eye[0]) >> 12), static_cast<s16>((p->pos[1] - cam.eye[1]) >> 12),
                      static_cast<s16>((p->pos[2] - cam.eye[2]) >> 12)};
  psyq::SetRotMatrix(&cam.rot);
  fx::ldv(0, rel[0], rel[1], rel[2]);
  fx::run(G::kRtv0);
  psyq::MATRIX m{};
  m.t[0] = fx::mac(1);
  m.t[1] = fx::mac(2);
  m.t[2] = fx::mac(3);
  psyq::SetRotMatrix(&cam.rot);
  psyq::SetTransMatrix(&m);
}

void fx_particle_integrate(Particle* p) {
  const s32 ticks = globals().frame_ticks;
  for (int i = 0; i < 3; ++i) p->pos[i] += mul(p->vel[i], ticks);
}

void fx_draw_line(Particle* p, const psyq::SVECTOR* end) {
  fx::ldv(0, 0, 0, 0);
  fx::run(G::kRtps);
  const u32 xy0 = fx::sxy(2);
  fx::ldv(0, *end);
  fx::run(G::kRtps);
  const u32 xy1 = fx::sxy(2);
  const s32 o = static_cast<s32>(fx::otz()) >> 3;  // stale: no avsz here
  if (o >= 0x800) return;
  auto* line = static_cast<psyq::LINE_G2*>(fx::alloc_prim(sizeof(psyq::LINE_G2)));
  if (line == nullptr) return;
  u32* w = fx::words(line);
  w[0] = static_cast<u32>(p->p3c) | 0x52000000u;  // LINE_G2, semi-transparent
  fx::put_xy(line, 1, fx::screen_x(xy0, origin_x()), fx::screen_y(xy0, origin_y()));
  w[2] = static_cast<u32>(p->p40);
  fx::put_xy(line, 3, fx::screen_x(xy1, origin_x()), fx::screen_y(xy1, origin_y()));
  fx::link(p, 0x820 - o, line, 4, fx::kAdditive);
}

u32 fx_scale_colour(u32 rgb, s32 t) {
  fx::gte().write_data(G::IR0, static_cast<u32>(t) << 4);
  fx::ld_ir(static_cast<s32>((rgb & 0xFF) << 4), static_cast<s32>(rgb >> 4 & 0xFF0), static_cast<s32>(rgb >> 12 & 0xFF0));
  fx::run(G::kGpf12);
  return fx::rgb(2) & 0xFFFFFF;
}

void fx_draw_triangle(Particle* p, const psyq::SVECTOR v[3], const u32 rgb[3], int front) {
  fx::ldv(0, v[0]);
  fx::ldv(1, v[1]);
  fx::ldv(2, v[2]);
  fx::run(G::kRtpt);
  const u32 xy[3] = {fx::sxy(0), fx::sxy(1), fx::sxy(2)};
  fx::run(G::kAvsz3);
  s32 o = static_cast<s32>(fx::otz()) >> 1;
  if (o >= 0x800) return;
  o -= 4;
  if (o < 0 || front != 0) o = 0;
  auto* tri = static_cast<psyq::POLY_G3*>(fx::alloc_prim(sizeof(psyq::POLY_G3)));
  if (tri == nullptr) return;
  u32* w = fx::words(tri);
  for (int i = 0; i < 3; ++i) {
    w[i * 2] = rgb[i];
    fx::put_xy(tri, i * 2 + 1, fx::screen_x(xy[i], origin_x()), fx::screen_y(xy[i], origin_y()));
  }
  fx::link(p, 0x820 - o, tri, 6, (rgb[0] & 0x02000000u) != 0 ? fx::kAdditive : fx::kNoMode);
}

void fx_setup_light(const s32 params[3]) {
  CameraBlock& cam = camera_block();
  cam.fx_colour = psyq::MATRIX{};
  cam.fx_light = psyq::MATRIX{};
  // 0x80075aba..0x80075abe: the BG tint.
  const s32 r = g_fade_targets.bg_tint[0], g = g_fade_targets.bg_tint[1], b = g_fade_targets.bg_tint[2];
  cam.colour.m[0][0] = static_cast<s16>(r << 5);
  cam.colour.m[1][0] = static_cast<s16>(g << 5);
  cam.colour.m[2][0] = static_cast<s16>(b << 5);
  for (int i = 0; i < 3; ++i) {
    if (((cam.light_mask >> i) & 1) == 0) continue;
    for (int k = 0; k < 3; ++k) cam.fx_light.m[i][k] = cam.light_dir[i][k];
    cam.fx_colour.m[0][i] = static_cast<s16>(mul(cam.light_col[i][0], r) >> 7);
    cam.fx_colour.m[1][i] = static_cast<s16>(mul(cam.light_col[i][1], g) >> 7);
    cam.fx_colour.m[2][i] = static_cast<s16>(mul(cam.light_col[i][2], b) >> 7);
  }
  const s32 axis[3] = {cosv(params[1]), 0, -sinv(params[1])};
  psyq::MATRIX ry{}, ra{}, m{};
  rot_matrix_y(params[1], &ry);
  rot_matrix_axis(axis, params[2], &ra);
  psyq::SetRotMatrix(&ra);
  for (int j = 0; j < 3; ++j) {
    fx::ld_ir(ry.m[0][j], ry.m[1][j], ry.m[2][j]);
    fx::run(G::kRtir);
    m.m[0][j] = fx::ir(1);
    m.m[1][j] = fx::ir(2);
    m.m[2][j] = fx::ir(3);
  }
  psyq::MulMatrix(&cam.fx_light, &m);
}

void fx_draw_lit_sphere(Particle* p, s32 lat, s32 lon, s32 radius, u32 rgb, s32 t, s32 hemi, s32 blend) {
  const CameraBlock& cam = camera_block();
  const u32 rgbc = fx_scale_colour(rgb, t) | (blend != 0 ? 0x3A000000u : 0x38000000u);
  psyq::SetLightMatrix(&cam.fx_light);
  psyq::SetColorMatrix(&cam.fx_colour);
  gte_set_back_colour();
  for (s32 i = 0;; ++i) {
    if (hemi == 1) {
      if (i >= lat / 2) return;
    } else if (i >= 0) {
      return;  // nothing but the lower hemisphere is ever drawn
    }
    for (s32 j = 0; j < lon; ++j) {
      const s32 a0 = (i << 11) / lat - 0x400, b0 = (j << 12) / lon;
      const s32 a1 = ((i + 1) << 11) / lat - 0x400, b1 = ((j + 1) << 12) / lon;
      const s32 ca0 = static_cast<s16>(cosv(a0)), sa0 = static_cast<s16>(sinv(a0));
      const s32 cb0 = static_cast<s16>(cosv(b0)), sb0 = static_cast<s16>(sinv(b0));
      const s32 ca1 = static_cast<s16>(cosv(a1)), sa1 = static_cast<s16>(sinv(a1));
      const s32 cb1 = static_cast<s16>(cosv(b1)), sb1 = static_cast<s16>(sinv(b1));
      // Normals at the quad's corners (scratch 0x1f80021c), vertices = normal * radius.
      const s16 n[4][3] = {
          {static_cast<s16>(mul(ca0, cb0) >> 12), static_cast<s16>(-sa0), static_cast<s16>(mul(ca0, sb0) >> 12)},
          {static_cast<s16>(mul(ca0, cb1) >> 12), static_cast<s16>(-sa0), static_cast<s16>(mul(ca0, sb1) >> 12)},
          {static_cast<s16>(mul(ca1, cb0) >> 12), static_cast<s16>(-sa1), static_cast<s16>(mul(ca1, sb0) >> 12)},
          {static_cast<s16>(mul(ca1, cb1) >> 12), static_cast<s16>(-sa1), static_cast<s16>(mul(ca1, sb1) >> 12)}};
      s16 v[4][3];
      for (int k = 0; k < 4; ++k)
        for (int c = 0; c < 3; ++c) v[k][c] = static_cast<s16>(mul(n[k][c], radius) >> 12);
      fx::ldv(0, v[0][0], v[0][1], v[0][2]);
      fx::run(G::kRtps);
      const u32 xy0 = fx::sxy(2);
      fx::ldv(0, v[1][0], v[1][1], v[1][2]);
      fx::ldv(1, v[2][0], v[2][1], v[2][2]);
      fx::ldv(2, v[3][0], v[3][1], v[3][2]);
      fx::run(G::kRtpt);
      const u32 xyr[4] = {xy0, fx::sxy(0), fx::sxy(1), fx::sxy(2)};
      s16 x[4], y[4];
      for (int k = 0; k < 4; ++k) {
        x[k] = fx::screen_x(xyr[k], origin_x());
        y[k] = fx::screen_y(xyr[k], origin_y());
      }
      fx::run(G::kAvsz4);
      if (!fx::x_visible(x[0]) && !fx::x_visible(x[1]) && !fx::x_visible(x[2]) && !fx::x_visible(x[3])) continue;
      if (!fx::y_visible(y[0]) && !fx::y_visible(y[1]) && !fx::y_visible(y[2]) && !fx::y_visible(y[3])) continue;
      const s32 depth = static_cast<s32>(fx::otz());
      fx::ldv(0, n[0][0], n[0][1], n[0][2]);
      fx::gte().write_data(G::RGBC, rgbc);
      fx::run(G::kNccs);
      const s32 o = depth >> 1;
      if (o >= 0x800) continue;
      auto* quad = static_cast<psyq::POLY_G4*>(fx::alloc_prim(sizeof(psyq::POLY_G4)));
      if (quad == nullptr) return;
      u32* w = fx::words(quad);
      w[0] = fx::rgb(2);
      fx::ldv(0, n[1][0], n[1][1], n[1][2]);
      fx::ldv(1, n[2][0], n[2][1], n[2][2]);
      fx::ldv(2, n[3][0], n[3][1], n[3][2]);
      fx::run(G::kNcct);
      w[2] = fx::rgb(0);
      w[4] = fx::rgb(1);
      w[6] = fx::rgb(2);
      for (int k = 0; k < 4; ++k) fx::put_xy(quad, k * 2 + 1, x[k], y[k]);
      fx::link(p, 0x820 - o, quad, 8, blend != 0 ? (blend - 1) * 0x20 : fx::kNoMode);
    }
  }
}

// ---- Behaviours --------------------------------------------------------------------

void fx_streak_update(Particle* p) {
  fx_set_local_transform(p);
  s32 d[3];
  for (int i = 0; i < 3; ++i) d[i] = (p->pos[i] - p->tgt[i]) >> 12;
  s32 n = static_cast<s32>(static_cast<u32>(length_of(d)) << 12) >> 12;
  if (n == 0) n = 1;
  for (int i = 0; i < 3; ++i) p->vel[i] = (p->tgt[i] - p->pos[i]) / n;  // unit direction (x 4096)
  const psyq::SVECTOR end{static_cast<s16>(mul(p->vel[0], p->p48) >> 12), static_cast<s16>(mul(p->vel[1], p->p48) >> 12),
                          static_cast<s16>(mul(p->vel[2], p->p48) >> 12), 0};
  fx_draw_line(p, &end);
  for (int i = 0; i < 3; ++i) p->vel[i] = mul(p->vel[i], p->p44) >> 12;
  fx_particle_integrate(p);
  const s32 ticks = globals().frame_ticks;
  p->p44 += mul(p->p38, ticks);
  for (int i = 0; i < 3; ++i) d[i] = (p->pos[i] - p->tgt[i]) >> 12;
  const s32 len = length_of(d);
  s32 dist = mul(len, 0x1000);
  if (dist < 0) dist = mul(len, -0x1000);
  if (dist <= mul(p->p44, ticks) + p->p48) {
    for (int i = 0; i < 3; ++i) p->pos[i] = p->tgt[i];
    fx_particle_free(p);
  }
}

void fx_spark_update(Particle* p) {
  fx_set_local_transform(p);
  const s32 v[3] = {p->vel[0] >> 12, p->vel[1] >> 12, p->vel[2] >> 12};
  s32 n = length_of(v);
  if (n == 0) n = 1;
  const s32 dir[3] = {p->vel[0] / n, p->vel[1] / n, p->vel[2] / n};
  const psyq::SVECTOR end{static_cast<s16>(mul(dir[0], p->p48) >> 12), static_cast<s16>(mul(dir[1], p->p48) >> 12),
                          static_cast<s16>(mul(dir[2], p->p48) >> 12), 0};
  fx_draw_line(p, &end);
  fx_particle_integrate(p);
  const s32 ticks = globals().frame_ticks;
  const s32 life = p->p44 - ticks;
  p->vel[1] += ticks * 0x600;  // gravity
  p->p44 = life;
  if (life < 1 || 0 < p->pos[1]) fx_particle_free(p);
}

void fx_ring_update(Particle* p) {
  const s32 h = psyq::ReadGeomScreen();
  psyq::PushMatrix();
  s32 rot[3] = {p->tgt[2], 0, 0};
  camera_set_object_matrix(p->pos, rot, camera_block().fx_scale);
  s32 c_prev = cosv(0), s_prev = sinv(0);
  u32 c_inner = static_cast<u32>(p->tgt[1]);
  u32 c_outer = static_cast<u32>(p->tgt[0]);
  if (p->p44 < 0x20) {
    c_inner = fx_scale_colour(c_inner, p->p44 << 2);
    c_outer = fx_scale_colour(c_outer, p->p44 << 2);
  }
  const u32 w0 = c_inner | 0x3A000000u;  // POLY_G4, semi-transparent
  for (s32 i = 0; i < p->p38; ++i) {
    const s32 a = ((i + 1) << 12) / p->p38;
    const s32 c = cosv(a), s = sinv(a);
    const s32 r = p->p48, rw = p->p48 + p->p3c;
    // Flat in the object's xz plane (scratch 0x1f800004.. x / z; y cleared once).
    fx::ldv(0, static_cast<s16>(mul(c_prev, r) >> 12), 0, static_cast<s16>(mul(s_prev, r) >> 12));
    fx::run(G::kRtps);
    const u32 xy0 = fx::sxy(2);
    fx::ldv(0, static_cast<s16>(mul(c, r) >> 12), 0, static_cast<s16>(mul(s, r) >> 12));
    fx::ldv(1, static_cast<s16>(mul(c_prev, rw) >> 12), 0, static_cast<s16>(mul(s_prev, rw) >> 12));
    fx::ldv(2, static_cast<s16>(mul(c, rw) >> 12), 0, static_cast<s16>(mul(s, rw) >> 12));
    fx::run(G::kRtpt);
    c_prev = c;
    s_prev = s;
    const u32 xyr[4] = {xy0, fx::sxy(0), fx::sxy(1), fx::sxy(2)};
    s16 x[4], y[4];
    for (int k = 0; k < 4; ++k) x[k] = fx::screen_x(xyr[k], origin_x());
    if (!fx::x_visible(x[0]) && !fx::x_visible(x[1]) && !fx::x_visible(x[2]) && !fx::x_visible(x[3])) continue;
    fx::run(G::kAvsz4);
    for (int k = 0; k < 4; ++k) y[k] = fx::screen_y(xyr[k], origin_y());
    if (!fx::y_visible(y[0]) && !fx::y_visible(y[1]) && !fx::y_visible(y[2]) && !fx::y_visible(y[3])) continue;
    const s32 o = static_cast<s32>(fx::otz()) >> 1;
    if (o >= 0x800) continue;
    auto* quad = static_cast<psyq::POLY_G4*>(fx::alloc_prim(sizeof(psyq::POLY_G4)));
    if (quad == nullptr) break;
    u32* w = fx::words(quad);
    w[0] = w0;
    w[2] = c_inner;
    w[4] = c_outer;
    w[6] = c_outer;
    for (int k = 0; k < 4; ++k) fx::put_xy(quad, k * 2 + 1, x[k], y[k]);
    fx::link(p, 0x820 - o, quad, 8, fx::kAdditive);
  }
  p->p48 += p->p40;
  p->p44 -= globals().frame_ticks;
  if (p->p44 <= 0) fx_particle_free(p);
  psyq::PopMatrix();
  fx::gte().write_ctrl(G::H, static_cast<u32>(h));
}

void fx_spinner_update(Particle* p) {
  const s32 h = psyq::ReadGeomScreen();
  psyq::PushMatrix();
  CameraBlock& cam = camera_block();
  // The original passes 0x800746ec as both the position and the rotation.
  camera_set_object_matrix(cam.fx_light_params, cam.fx_light_params, cam.fx_scale);
  const s32 r = p->tgt[1];
  const s32 a = p->p38, b = p->p3c;
  const psyq::SVECTOR v[3] = {
      {static_cast<s16>((mul(mul(cosv(a), cosv(b)) >> 12, r) + p->pos[0]) >> 12),
       static_cast<s16>((p->pos[1] + mul(sinv(a), r)) >> 12),
       static_cast<s16>((mul(mul(cosv(a), sinv(b)) >> 12, r) + p->pos[2]) >> 12), 0},
      {static_cast<s16>((mul(mul(cosv(a + 0x38), cosv(b + 0x38)) >> 12, r) + p->pos[0]) >> 12),
       static_cast<s16>((p->pos[1] + mul(sinv(a + 0x38), r)) >> 12),
       static_cast<s16>((mul(mul(cosv(a + 0x38), sinv(b + 0x38)) >> 12, r) + p->pos[2]) >> 12), 0},
      {static_cast<s16>(p->pos[0] >> 12), static_cast<s16>(p->pos[1] >> 12), static_cast<s16>(p->pos[2] >> 12), 0}};
  s32 t0 = 0x100, t2 = 0x80;
  if (p->p48 < 0) {
    t0 = p->p48 * 4 + 0x100;
    t2 = p->p48 * 2 + 0x80;
  }
  const u32 c0 = fx_scale_colour(static_cast<u32>(p->p44), t0) | 0x32000000u;
  const u32 rgb[3] = {c0, c0, fx_scale_colour(static_cast<u32>(p->p44), t2)};
  fx_draw_triangle(p, v, rgb, 1);
  const s32 ticks = globals().frame_ticks;
  s32 nr = r + ticks * 0x20;
  p->p38 = (p->p38 + mul(p->tgt[0], ticks)) & 0xFFF;
  p->p48 -= ticks;
  p->p3c = (p->p3c + mul(p->tgt[2], ticks)) & 0xFFF;
  if (p->p40 < nr) nr = p->p40;
  p->tgt[1] = nr;
  if (p->p48 < -0x40) fx_particle_free(p);
  psyq::PopMatrix();
  fx::gte().write_ctrl(G::H, static_cast<u32>(h));
}

void fx_orb_update(Particle* p) {
  fx_setup_light(camera_block().fx_light_params);
  fx_set_local_transform(p);
  fx_draw_lit_sphere(p, p->p38, p->p3c, p->p48, static_cast<u32>(p->tgt[0]), p->p44 << 2, 1, 2);
  const s32 life = p->p44 - globals().frame_ticks;
  p->p48 += p->p40;
  p->p44 = life;
  if (life < 1) fx_particle_free(p);
}

void fx_beam_update(Particle* p) {
  fx_set_local_transform(p);
  if (p->vel[0] == 0) {
    p->p3c = p->p44;
    p->p40 = p->p48;
  } else {
    p->p3c = static_cast<s32>(fx_scale_colour(static_cast<u32>(p->p44), p->p38));
    p->p40 = static_cast<s32>(fx_scale_colour(static_cast<u32>(p->p48), p->p38));
  }
  const psyq::SVECTOR end{static_cast<s16>(p->tgt[0]), static_cast<s16>(p->tgt[1]), static_cast<s16>(p->tgt[2]), 0};
  fx_draw_line(p, &end);
  p->p38 -= globals().frame_ticks;
  if (p->p38 < 1) fx_particle_free(p);
}

void fx_triangle_update(Particle* p) {
  fx_set_local_transform(p);
  u32 c[3];
  if (p->flag == 0) {
    c[0] = static_cast<u32>(p->p3c);
    c[1] = static_cast<u32>(p->p40);
    c[2] = static_cast<u32>(p->p44);
  } else {
    c[0] = fx_scale_colour(static_cast<u32>(p->p3c), p->p38);
    c[1] = fx_scale_colour(static_cast<u32>(p->p40), p->p38);
    c[2] = fx_scale_colour(static_cast<u32>(p->p44), p->p38);
  }
  const psyq::SVECTOR v[3] = {
      {0, 0, 0, 0},
      {static_cast<s16>(p->vel[0]), static_cast<s16>(p->vel[1]), static_cast<s16>(p->vel[2]), 0},
      {static_cast<s16>(p->tgt[0]), static_cast<s16>(p->tgt[1]), static_cast<s16>(p->tgt[2]), 0}};
  const u32 rgb[3] = {c[0] | 0x32000000u, c[1], c[2]};
  fx_draw_triangle(p, v, rgb, 1);
  p->p38 -= globals().frame_ticks;
  if (p->p38 < 1) fx_particle_free(p);
}

void fx_burst_update(Particle* p) {
  // Projected through the camera alone: the translation is the eye's (pos zeroed for
  // fx_set_local_transform), the centre the particle's position.
  s32 saved[4];
  std::memcpy(saved, p->pos, sizeof saved);
  p->pos[0] = p->pos[1] = p->pos[2] = 0;
  fx_set_local_transform(p);
  std::memcpy(p->pos, saved, sizeof saved);
  fx::ldv(0, static_cast<s16>(p->pos[0] >> 12), static_cast<s16>(p->pos[1] >> 12), static_cast<s16>(p->pos[2] >> 12));
  fx::run(G::kRtps);
  const u32 centre = fx::sxy(2);
  const s16 cx = fx::screen_x(centre, origin_x()), cy = fx::screen_y(centre, origin_y());
  const s32 o = static_cast<s32>(fx::otz()) >> 3;  // stale, as in fx_draw_line
  const s32 r = p->p3c, div = p->p44 != 0 ? p->p44 : 1;
  for (s32 i = 0; i < 12; ++i) {
    const s32 a = (i << 12) / 12 + p->p38;
    auto at = [&](s32 angle, s32 k, s16 base, bool sine) {
      const s32 cs = sine ? sinv(angle) : cosv(angle);
      return static_cast<s16>(static_cast<u16>(base) + static_cast<u16>(mul(mul(cs, r), k) / div));
    };
    auto* quad = static_cast<psyq::POLY_G4*>(fx::alloc_prim(sizeof(psyq::POLY_G4)));
    if (quad == nullptr) break;
    fx::put_xy(quad, 1, at(a, 1, cx, false), at(a, 1, cy, true));
    fx::put_xy(quad, 3, at(a - 0x71, 6, cx, false), at(a - 0x71, 6, cy, true));
    fx::put_xy(quad, 5, at(a + 0x71, 6, cx, false), at(a + 0x71, 6, cy, true));
    fx::put_xy(quad, 7, at(a, 8, cx, false), at(a, 8, cy, true));
    s32 t = 0x100 - p->p40;
    if (t < 0) t += 3;
    u32* w = fx::words(quad);
    w[0] = fx_scale_colour(0xF8A8, t >> 2) | 0x3A000000u;
    w[2] = fx_scale_colour(0xF8A8, (0x100 - p->p40) / 2);
    w[4] = w[0];
    w[6] = fx_scale_colour(0xF8A8, 0x100 - p->p40);
    fx::link(p, 0x824 - o, quad, 8, fx::kAdditive);
  }
  const s32 ticks = globals().frame_ticks;
  p->p38 = (p->p38 + ticks * 0x20) & 0xFFF;
  p->p3c += ticks;
  if (0x20 < p->p3c) {
    p->p40 += ticks * 0x10;
    if (0x100 < p->p40) p->p40 = 0x100;
  }
  if (0x30 < p->p3c) fx_particle_free(p);
}

void fx_ring_textured_update(Particle* p) {
  fx_set_local_transform(p);
  u32 colour = 0x808080;
  s32 c_prev = cosv(p->p4c), s_prev = sinv(p->p4c);
  if (p->p44 < 0x20) colour = fx_scale_colour(0x808080, p->p44 << 2);
  colour |= 0x2E000000u;  // POLY_FT4, semi-transparent
  const u32 uv = 0x8008ED58u + static_cast<u32>(p->tgt[2]) * 8;  // no range check on the console
  for (s32 i = 0; i < p->p38; ++i) {
    const s32 a = ((i + 1) << 12) / p->p38 + p->p4c;
    const s32 c = cosv(a), s = sinv(a);
    const s32 r = p->p48, rw = p->p48 + p->tgt[0];
    const s16 h = static_cast<s16>(p->tgt[1]);
    // Inner edge flat at y 0, outer edge at y 'height' (+0x2c).
    fx::ldv(0, static_cast<s16>(mul(c_prev, r) >> 12), 0, static_cast<s16>(mul(s_prev, r) >> 12));
    fx::run(G::kRtps);
    const u32 xy0 = fx::sxy(2);
    fx::ldv(0, static_cast<s16>(mul(c, r) >> 12), 0, static_cast<s16>(mul(s, r) >> 12));
    fx::ldv(1, static_cast<s16>(mul(c_prev, rw) >> 12), h, static_cast<s16>(mul(s_prev, rw) >> 12));
    fx::ldv(2, static_cast<s16>(mul(c, rw) >> 12), h, static_cast<s16>(mul(s, rw) >> 12));
    fx::run(G::kRtpt);
    c_prev = c;
    s_prev = s;
    const u32 xyr[4] = {xy0, fx::sxy(0), fx::sxy(1), fx::sxy(2)};
    s16 x[4], y[4];
    for (int k = 0; k < 4; ++k) x[k] = fx::screen_x(xyr[k], origin_x());
    if (!fx::x_visible(x[0]) && !fx::x_visible(x[1]) && !fx::x_visible(x[2]) && !fx::x_visible(x[3])) continue;
    fx::run(G::kAvsz4);
    for (int k = 0; k < 4; ++k) y[k] = fx::screen_y(xyr[k], origin_y());
    if (!fx::y_visible(y[0]) && !fx::y_visible(y[1]) && !fx::y_visible(y[2]) && !fx::y_visible(y[3])) continue;
    const s32 o = static_cast<s32>(fx::otz()) >> 1;
    if (o >= 0x7FA) continue;
    auto* quad = static_cast<psyq::POLY_FT4*>(fx::alloc_prim(sizeof(psyq::POLY_FT4)));
    if (quad == nullptr) break;
    u32* w = fx::words(quad);
    w[0] = colour;
    w[2] = ovl_u16(uv + 4) | static_cast<u32>(static_cast<u16>(p->p3c + 0x7F86)) << 16;  // u0v0, CLUT
    w[4] = ovl_u16(uv + 6) | 0x36u << 16;                                               // u1v1, tpage (384, 256) 4-bit abr 1
    w[6] = ovl_u16(uv + 0);                                                             // u2v2
    w[8] = ovl_u16(uv + 2);                                                             // u3v3
    for (int k = 0; k < 4; ++k) fx::put_xy(quad, k * 2 + 1, x[k], y[k]);
    fx::link(p, 0x820 - o, quad, 9, fx::kNoMode);
  }
  const s32 ticks = globals().frame_ticks;
  p->p48 += p->p40;
  p->p44 -= ticks;
  p->p4c = static_cast<s16>((static_cast<u16>(p->p4c) + mul(p->vel[0], ticks)) & 0xFFF);
  if (p->p44 <= 0) fx_particle_free(p);
}

}  // namespace ss::game::field
