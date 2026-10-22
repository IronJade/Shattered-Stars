#include "game/battle/enemy_support.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game/battle/battle.h"
#include "game/battle/camera.h"
#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/common/screen.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

void todo(const char* what) {
  runtime::Runtime::get().log_once(std::string("battle-enemy-support-") + what,
                                   std::string("battle: not ported yet: ") + what);
}

u32 svec_xy(s32 x, s32 y) { return static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16; }


}  // namespace

s32 bobj_front_x(const BObj* o, s32 dist) {
  const s32 d = o->facing[0] < 0 ? dist * -0x1000 : dist * 0x1000;
  return o->pos[0] + d;
}

Sprite* unit_play_anim_ex(const Unit* u, int anim, int loops) { return sprite_spawn_from(anim, loops, u, nullptr); }

void unit_throw_weapons(Unit* u, s32 variant) {
  const int free = shot_count_free();
  if (free < 0x20) return;
  const int n = free > 0x30 ? 8 : 4;
  for (int i = 0; i < n; ++i) {
    Shot* s = shot_spawn(0x2C);
    if (s == nullptr) continue;
    shot_place_at_unit(s, u, 0x20, 0x60, 0);
    s->vel[0] = bobj_facing_fx(u, rng_between(8, 0x14));
    s->vel[1] = rng_between_fixed(-12, 3);
    s->vel[2] = rng_spread_fixed(0xC);
    s->owner = u;
    s->p130 = variant;
    bobj_share_sprite(u, s);
  }
}

// (The spell stand-ins went: spells.h / spellcast.h / spell_fx.h have the real ones.)

// ---- Other stand-ins (the battle agent's).
void bload_request(int, void*, s16, int) { todo("bload_request 0x800475c0 (enemy module)"); }
u32 voice_play_entry(int, Unit*) {
  todo("voice_play_entry 0x8007f818 (enemy-module voices)");
  return 0;
}

void draw_textured_patch(const s32 uv[4], const s32 corners[4][4], u16 clut, u16 tpage, s32 ot_bias, u32 code_rgb,
                         s32 n) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  if (pool == nullptr || n <= 0 || !arena_has_room(pool, n * n * 0x28)) return;
  // The vertex grid (9 x 9, rows of 9: scratchpad 0x1f800000) and its UVs (0x1f800288).
  psyq::SVECTOR vert[81]{};
  u16 tex[81]{};
  const s32 u0 = uv[0], v0 = static_cast<s16>(uv[1]), u1 = static_cast<s16>(uv[2]), v1 = static_cast<s16>(uv[3]);
  const s32 dv = v1 - v0;
  s32 vacc = 0;
  for (s32 row = 0, base = 0; row <= n; ++row, base += 9, vacc += dv) {
    const s32* c0 = corners[0];
    const s32* c1 = corners[1];
    const s32* c2 = corners[2];
    const s32* c3 = corners[3];
    const s32 lx = (c2[0] - c0[0]) * row / n + c0[0], ly = (c2[1] - c0[1]) * row / n + c0[1];
    const s32 lz = (c2[2] - c0[2]) * row / n + c0[2];
    const s32 rx = (c3[0] - c1[0]) * row / n + c1[0], ry = (c3[1] - c1[1]) * row / n + c1[1];
    const s32 rz = (c3[2] - c1[2]) * row / n + c1[2];
    const s32 vv = (vacc / n + v0) << 8;
    const s32 u_lo = static_cast<s16>(u0);  // (the console keeps u0 << 16 and shifts it back)
    s32 ax = 0, ay = 0, az = 0, au = 0;
    for (s32 col = 0; col <= n; ++col) {
      const s32 i = base + col;
      vert[i].vx = static_cast<s16>((ax / n + lx) >> 12);
      vert[i].vy = static_cast<s16>((ay / n + ly) >> 12);
      vert[i].vz = static_cast<s16>((az / n + lz) >> 12);
      tex[i] = static_cast<u16>(vv | (au / n + u_lo));
      au += u1 - u_lo;
      az += rz - lz;
      ay += ry - ly;
      ax += rx - lx;
    }
  }
  gte_set_view_matrix();  // gte_set_view_matrix_sp 0x8007d1d4: the same, its matrix in the scratchpad
  // (A 4-byte allocation marks the start; the linked quads follow it in the arena.)
  pool_a_bytes_used() += 4;
  psx::Gte& q = psyq::gte();
  const s32 far = 0x600 - ot_bias;
  for (s32 row = 0; row < n; ++row) {
    const s32 r0 = row * 9, r1 = r0 + 9;
    for (s32 col = 0; col < n; ++col) {
      const s32 a = r0 + col;
      q.write_data(psx::Gte::VXY0, svec_xy(vert[a].vx, vert[a].vy));
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(vert[a].vz)));
      q.execute(psx::Gte::kRtps);
      const u32 sxy0 = q.read_data(psx::Gte::SXY2);
      const u32 flag0 = q.read_data(psx::Gte::FLAG);
      const psyq::SVECTOR& b = vert[a + 1];
      const psyq::SVECTOR& c = vert[r1 + col];
      const psyq::SVECTOR& d = vert[r1 + col + 1];
      q.write_data(psx::Gte::VXY0, svec_xy(b.vx, b.vy));
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(b.vz)));
      q.write_data(psx::Gte::VXY1, svec_xy(c.vx, c.vy));
      q.write_data(psx::Gte::VZ1, static_cast<u32>(static_cast<s32>(c.vz)));
      q.write_data(psx::Gte::VXY2, svec_xy(d.vx, d.vy));
      q.write_data(psx::Gte::VZ2, static_cast<u32>(static_cast<s32>(d.vz)));
      q.execute(psx::Gte::kRtpt);
      const u32 sxy1 = q.read_data(psx::Gte::SXY0), sxy2 = q.read_data(psx::Gte::SXY1), sxy3 = q.read_data(psx::Gte::SXY2);
      const u32 flag1 = q.read_data(psx::Gte::FLAG);
      if ((flag1 & 0x80000000u) != 0 || (flag0 & 0x80000000u) != 0) continue;
      q.execute(psx::Gte::kAvsz4);
      const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ)) >> 3;
      if (otz >= far) continue;
      auto* p = screen_alloc_prims<psyq::POLY_FT4>(pool->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyFT4(p);
      std::memcpy(&p->r0, &code_rgb, 4);
      p->x0 = static_cast<s16>(sxy0), p->y0 = static_cast<s16>(sxy0 >> 16);
      p->x1 = static_cast<s16>(sxy1), p->y1 = static_cast<s16>(sxy1 >> 16);
      p->x2 = static_cast<s16>(sxy2), p->y2 = static_cast<s16>(sxy2 >> 16);
      p->x3 = static_cast<s16>(sxy3), p->y3 = static_cast<s16>(sxy3 >> 16);
      p->u0 = static_cast<u8>(tex[a]), p->v0 = static_cast<u8>(tex[a] >> 8);
      p->clut = clut;
      p->u1 = static_cast<u8>(tex[a + 1]), p->v1 = static_cast<u8>(tex[a + 1] >> 8);
      p->tpage = tpage;
      p->u2 = static_cast<u8>(tex[a + 9]), p->v2 = static_cast<u8>(tex[a + 9] >> 8);
      p->u3 = static_cast<u8>(tex[a + 10]), p->v3 = static_cast<u8>(tex[a + 10] >> 8);
      psyq::addPrim(&pool->ot_head[ot_bias + 0x610 - otz], p);
      pool_a_bytes_used() += 0x28;
    }
  }
}

void draw_quad_gt4(const s32 uv[4], const psyq::SVECTOR v[4], u16 clut, s32 tpage, s32 ot_bias, const u32 rgb[4]) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  gte_set_view_matrix();  // gte_set_view_matrix_sp 0x8007d1d4
  if (pool == nullptr || !arena_has_room(pool, 0x34)) return;
  pool_a_bytes_used() += 0x34;  // (allocated whether or not it is linked)
  psx::Gte& q = psyq::gte();
  q.write_data(psx::Gte::VXY0, svec_xy(v[0].vx, v[0].vy));
  q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(v[0].vz)));
  q.execute(psx::Gte::kRtps);
  const u32 sxy0 = q.read_data(psx::Gte::SXY2);
  const u32 flag0 = q.read_data(psx::Gte::FLAG);
  q.write_data(psx::Gte::VXY0, svec_xy(v[1].vx, v[1].vy));
  q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(v[1].vz)));
  q.write_data(psx::Gte::VXY1, svec_xy(v[2].vx, v[2].vy));
  q.write_data(psx::Gte::VZ1, static_cast<u32>(static_cast<s32>(v[2].vz)));
  q.write_data(psx::Gte::VXY2, svec_xy(v[3].vx, v[3].vy));
  q.write_data(psx::Gte::VZ2, static_cast<u32>(static_cast<s32>(v[3].vz)));
  q.execute(psx::Gte::kRtpt);
  const u32 sxy1 = q.read_data(psx::Gte::SXY0), sxy2 = q.read_data(psx::Gte::SXY1), sxy3 = q.read_data(psx::Gte::SXY2);
  const u32 flag1 = q.read_data(psx::Gte::FLAG);
  if ((flag1 & 0x80000000u) != 0 || (flag0 & 0x80000000u) != 0) return;
  q.execute(psx::Gte::kAvsz4);
  const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ)) >> 3;
  if (otz >= 0x600 - ot_bias) return;
  auto* p = screen_alloc_prims<psyq::POLY_GT4>(pool->arena, 1);
  if (p == nullptr) return;
  psyq::setPolyGT4(p);
  std::memcpy(&p->r0, &rgb[0], 4);
  std::memcpy(&p->r1, &rgb[1], 4);
  std::memcpy(&p->r2, &rgb[2], 4);
  std::memcpy(&p->r3, &rgb[3], 4);
  p->x0 = static_cast<s16>(sxy0), p->y0 = static_cast<s16>(sxy0 >> 16);
  p->x1 = static_cast<s16>(sxy1), p->y1 = static_cast<s16>(sxy1 >> 16);
  p->x2 = static_cast<s16>(sxy2), p->y2 = static_cast<s16>(sxy2 >> 16);
  p->x3 = static_cast<s16>(sxy3), p->y3 = static_cast<s16>(sxy3 >> 16);
  const u8 ua = static_cast<u8>(uv[0]), va = static_cast<u8>(uv[1]), ub = static_cast<u8>(uv[2]), vb = static_cast<u8>(uv[3]);
  p->u0 = ua, p->v0 = va, p->clut = clut;
  p->u1 = ub, p->v1 = va, p->tpage = static_cast<u16>(tpage);
  p->u2 = ua, p->v2 = vb;
  p->u3 = ub, p->v3 = vb;
  psyq::addPrim(&pool->ot_head[ot_bias + 0x610 - otz], p);
}

Vec4 vec_random_in_disc_xz(const s32 centre[3], s32 radius) {
  const u32 a = rng_range(0x1000);
  const s32 d = static_cast<s32>(rng_range(static_cast<u32>(radius)));
  Vec4 out;
  out.x = centre[0] + bcos(a) * d;
  out.y = 0;
  out.z = centre[2] + bsin(a) * d;
  return out;
}

u32 bobj_poly_tag(const BObj* o, u16 flags) {
  u32 rgb = 0;  // (the console's register holds garbage for an unknown mode)
  switch (o->colour_mode) {
    case 0: rgb = 0x808080; break;
    case 1: rgb = static_cast<u32>(o->rgb[0]) | static_cast<u32>(o->rgb[1]) << 8 | static_cast<u32>(o->rgb[2]) << 16; break;
    case 2: rgb = static_cast<u32>(o->rgb[0]) * 0x10101; break;
    default: break;
  }
  return rgb | ((flags & 0x60) == 0 ? 0x2C000000u : 0x2E000000u);
}

void collider_set_attack_box(Collider* c, s32 hx, s32 hy, s32 hz) {
  c->box_mask |= 4;
  c->attack_min[0][0] = c->pos[0] - hx * 0x1000;
  c->attack_max[0][0] = c->pos[0] + hx * 0x1000;
  c->attack_min[0][1] = c->pos[1] - hy * 0x1000;
  c->attack_min[0][2] = c->pos[2] - hz * 0x1000;
  c->attack_max[0][2] = c->pos[2] + hz * 0x1000;
  c->attack_max[0][1] = c->pos[1] + hy * 0x1000;
}

void sound_handle_release(u32 handle) {
  if (sound_handle_active(handle)) sound_release_clip(bg().sound, static_cast<int>(handle >> 16) & 0xFF);
}

void shot_skill_camera_end(Shot* s) {
  if (s->owner->camera_focus != 0) camera_schedule_end(7, s->owner, 0x3C);
}

int unit_is_disabled(const Unit* u) {
  return ((u->st()->status & 0xE6) != 0 || (u->status_bits & 0x48) != 0 || u->super_armour != 0) ? 1 : 0;
}

void gte_set_view_matrix() {
  const ViewState& v = view();
  psx::Gte& g = psyq::gte();
  const s16 x = static_cast<s16>(-v.view_eye[0] >> 12);
  const s16 y = static_cast<s16>(-v.view_eye[1] >> 12);
  const s16 z = static_cast<s16>(-v.view_eye[2] >> 12);
  psyq::SetRotMatrix(&v.render_view);
  g.write_data(psx::Gte::VXY0, svec_xy(x, y));
  g.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(z)));
  g.execute(psx::Gte::kRtv0);
  psyq::MATRIX m{};
  m.t[0] = static_cast<s32>(g.read_data(psx::Gte::MAC1));
  m.t[1] = static_cast<s32>(g.read_data(psx::Gte::MAC2));
  m.t[2] = static_cast<s32>(g.read_data(psx::Gte::MAC3));
  psyq::SetTransMatrix(&m);
  psyq::SetRotMatrix(&v.render_view);
}

void draw_tri_g3(const psyq::SVECTOR v[3], const u32 code_rgb[3], int front) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  if (pool == nullptr || !arena_has_room(pool, 0x1C)) return;
  auto* p = screen_alloc_prims<psyq::POLY_G3>(pool->arena, 1);
  if (p == nullptr) return;
  psx::Gte& q = psyq::gte();
  q.write_data(psx::Gte::VXY0, svec_xy(v[0].vx, v[0].vy));
  q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(v[0].vz)));
  q.write_data(psx::Gte::VXY1, svec_xy(v[1].vx, v[1].vy));
  q.write_data(psx::Gte::VZ1, static_cast<u32>(static_cast<s32>(v[1].vz)));
  q.write_data(psx::Gte::VXY2, svec_xy(v[2].vx, v[2].vy));
  q.write_data(psx::Gte::VZ2, static_cast<u32>(static_cast<s32>(v[2].vz)));
  q.execute(psx::Gte::kRtpt);
  psyq::setPolyG3(p);
  std::memcpy(&p->r0, &code_rgb[0], 4);
  std::memcpy(&p->r1, &code_rgb[1], 4);
  std::memcpy(&p->r2, &code_rgb[2], 4);
  const u32 s0 = q.read_data(psx::Gte::SXY0), s1 = q.read_data(psx::Gte::SXY1), s2 = q.read_data(psx::Gte::SXY2);
  p->x0 = static_cast<s16>(s0), p->y0 = static_cast<s16>(s0 >> 16);
  p->x1 = static_cast<s16>(s1), p->y1 = static_cast<s16>(s1 >> 16);
  p->x2 = static_cast<s16>(s2), p->y2 = static_cast<s16>(s2 >> 16);
  q.execute(psx::Gte::kAvsz3);
  s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ)) >> 3;
  // (The console leaves the allocated primitive unlinked when it is too far.)
  if (otz >= 0x600) return;
  otz -= 8;
  if (otz < 0 || front != 0) otz = 0;
  psyq::addPrim(&pool->ot_head[0x610 - otz], p);
  pool_a_bytes_used() += 0x1C;
  if ((code_rgb[0] & 0x2000000) != 0 && arena_has_room(pool, 0xC)) {
    auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
    if (dm == nullptr) return;
    psyq::SetDrawMode(dm, 0, 1, 0x20, nullptr);
    psyq::addPrim(&pool->ot_head[0x610 - otz], dm);
    pool_a_bytes_used() += 0xC;
  }
}

void draw_beam(const s32 pos[3], const s32 dir[3], u32 rgb_a, u32 rgb_b, s32 length, s32 r0, s32 r1, s32 sides) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  if (pool == nullptr) return;
  s32 slices = length / 0x140 + 1;
  if (8 < slices) slices = 8;
  // The console allocates every quad and every DR_MODE up front (and gives up when either
  // does not fit); the unused tail stays allocated.
  if (!arena_has_room(pool, sides * slices * 0x24)) return;
  pool_a_bytes_used() += sides * slices * 0x24;
  if (!arena_has_room(pool, sides * slices * 0xC)) return;
  pool_a_bytes_used() += sides * slices * 0xC;
  if (r1 == 0) r1 = r0;
  // The beam's frame: yaw about y to the direction, then its pitch about the horizontal
  // axis across it, under the camera rotation; the origin through the render view.
  // (SQR on IR1..3: the components truncated to 16 bits, squared unshifted.)
  const s32 dx = static_cast<s16>(dir[0]), dz = static_cast<s16>(dir[2]);
  const s32 yaw = ratan2(dir[0], dir[2]);
  const s32 pitch = ratan2(dir[1], square_root0(static_cast<s32>(static_cast<u32>(dx * dx) + static_cast<u32>(dz * dz))));
  const s32 axis[3] = {bcos(static_cast<u32>(yaw)), 0, -bsin(static_cast<u32>(yaw))};
  psyq::MATRIX m_yaw{}, m_axis{}, m{};
  rot_matrix_y(yaw, &m_yaw);
  rot_matrix_axis(axis, pitch, &m_axis);
  const ViewState& v = view();
  psyq::MulMatrix0(&v.view_matrix, &m_axis, &m);
  psyq::MulMatrix0(&m, &m_yaw, &m);
  psx::Gte& q = psyq::gte();
  psyq::SetRotMatrix(&v.render_view);
  q.write_data(psx::Gte::VXY0, svec_xy((pos[0] - v.view_eye[0]) >> 12, (pos[1] - v.view_eye[1]) >> 12));
  q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(static_cast<s16>((pos[2] - v.view_eye[2]) >> 12))));
  q.execute(psx::Gte::kRtv0);
  m.t[0] = static_cast<s32>(q.read_data(psx::Gte::MAC1));
  m.t[1] = static_cast<s32>(q.read_data(psx::Gte::MAC2));
  m.t[2] = static_cast<s32>(q.read_data(psx::Gte::MAC3));
  psyq::SetRotMatrix(&m);
  psyq::SetTransMatrix(&m);
  for (s32 s = 0; s < slices; ++s) {
    const s32 seg = length / slices;
    const s16 z0 = static_cast<s16>(seg * s), z1 = static_cast<s16>(seg * (s + 1));
    const u32 col_a = colour_lerp(rgb_a, rgb_b, (s << 12) / slices);
    const u32 col_b = colour_lerp(rgb_a, rgb_b, ((s + 1) << 12) / slices);
    const u32 code = col_a | 0x3A000000;
    const s32 step = 0x1000 / sides;
    s32 c_prev = 0x1000, s_prev = 0;
    for (s32 k = 0; k < sides; ++k) {
      const s32 c = bcos(static_cast<u32>((k + 1) * step)), sn = bsin(static_cast<u32>((k + 1) * step));
      // Two edges: the previous angle at radius r0, this one at r1; along z0 -> z1.
      const s16 x0 = static_cast<s16>(static_cast<u32>(c_prev * r0) >> 12);
      const s16 y0 = static_cast<s16>(static_cast<u32>(s_prev * r0) >> 12);
      const s16 x1 = static_cast<s16>(static_cast<u32>(c * r1) >> 12);
      const s16 y1 = static_cast<s16>(static_cast<u32>(sn * r1) >> 12);
      q.write_data(psx::Gte::VXY0, svec_xy(x0, y0));
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(z0)));
      q.execute(psx::Gte::kRtps);
      const u32 sxy0 = q.read_data(psx::Gte::SXY2);
      q.write_data(psx::Gte::VXY0, svec_xy(x1, y1));
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(z0)));
      q.write_data(psx::Gte::VXY1, svec_xy(x0, y0));
      q.write_data(psx::Gte::VZ1, static_cast<u32>(static_cast<s32>(z1)));
      q.write_data(psx::Gte::VXY2, svec_xy(x1, y1));
      q.write_data(psx::Gte::VZ2, static_cast<u32>(static_cast<s32>(z1)));
      q.execute(psx::Gte::kRtpt);
      const u32 sxy1 = q.read_data(psx::Gte::SXY0), sxy2 = q.read_data(psx::Gte::SXY1), sxy3 = q.read_data(psx::Gte::SXY2);
      q.execute(psx::Gte::kAvsz4);
      const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ)) >> 3;
      c_prev = c;
      s_prev = sn;
      if (otz >= 0x5F8) continue;
      auto* p = screen_alloc_prims<psyq::POLY_G4>(pool->arena, 1);
      auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
      if (p == nullptr || dm == nullptr) return;
      psyq::setPolyG4(p);
      std::memcpy(&p->r0, &code, 4);
      std::memcpy(&p->r1, &col_a, 4);
      std::memcpy(&p->r2, &col_b, 4);
      std::memcpy(&p->r3, &col_b, 4);
      p->x0 = static_cast<s16>(sxy0), p->y0 = static_cast<s16>(sxy0 >> 16);
      p->x1 = static_cast<s16>(sxy1), p->y1 = static_cast<s16>(sxy1 >> 16);
      p->x2 = static_cast<s16>(sxy2), p->y2 = static_cast<s16>(sxy2 >> 16);
      p->x3 = static_cast<s16>(sxy3), p->y3 = static_cast<s16>(sxy3 >> 16);
      psyq::addPrim(&pool->ot_head[0x618 - otz], p);
      psyq::SetDrawMode(dm, 0, 1, 0x20, nullptr);
      psyq::addPrim(&pool->ot_head[0x618 - otz], dm);
    }
  }
}


void enemy_debug_trace(const EnemyUnit* e) {
  static const bool on = std::getenv("SS_ENEMY_TRACE") != nullptr;
  if (!on) return;
  const BattleGlobals& g = bg();
  for (const EnemyUnit* u : g.enemy_units) {
    if (u == nullptr || u->removed != 0) continue;
    if (u != e) return;
    break;
  }
  std::string line;
  char buf[160];
  for (int i = 0; i < 8; ++i) {
    const EnemyUnit* u = g.enemy_units[i];
    if (u == nullptr) continue;
    std::snprintf(buf, sizeof(buf), "%se%d t%x s%d k%d r%d d%d x%d y%d z%d hp%d c%d,%d,%d,%d", line.empty() ? "" : " | ", i,
                  u->st()->id, u->action, u->action_kind, u->removed, u->death, u->pos[0] >> 12, u->pos[1] >> 12,
                  u->pos[2] >> 12, u->st()->hp, u->counters[0], u->counters[1], u->counters[2], u->counters[3]);
    line += buf;
  }
  for (int i = 0; i < 4; ++i) {
    const PartyUnit* u = g.party_units[i];
    if (u == nullptr) continue;
    std::snprintf(buf, sizeof(buf), " | p%d s%d x%d z%d hp%d", i, u->action, u->pos[0] >> 12, u->pos[2] >> 12, u->st()->hp);
    line += buf;
  }
  static const bool ticks = std::getenv("SS_ENEMY_TRACE")[0] == '2';
  if (ticks) {
    // SS_ENEMY_TRACE=2: also the AI tick accumulators (+0x239) and the battle state / flags.
    char t[64];
    std::snprintf(t, sizeof(t), " | st%d fl%d ai", g.state, g.flags);
    line += t;
    for (const EnemyUnit* u : g.enemy_units)
      if (u != nullptr) line += " " + std::to_string(static_cast<s8>(u->ai_ticks));
    if (const PartyUnit* p = g.party_units[0]) {
      std::snprintf(t, sizeof(t), " pc %d pa %d gate %d vset %d frames %d", p->counters[0], p->appear,
                    g.intro_voice_gate, g.special_voice_set, g.battle_frames);
      line += t;
      char q[96];
      std::snprintf(q, sizeof(q), " e %d %d %d p %d %d", e->action, e->pos[0], e->pos[2], p->pos[0], p->pos[2]);
      line += q;
      if (const Camera* c = g.camera) {
        std::snprintf(q, sizeof(q), " cam %d %d %d", c->mode, c->step, c->hold);
        line += q;
      }
    }
  }
  static const bool shots = std::getenv("SS_ENEMY_TRACE")[0] == '3';
  if (shots) {
    // SS_ENEMY_TRACE=3: also every live Shot (kind, owner's unit slot, position, anim, hit).
    line += " | shots";
    for (int i = 0; i < 0xA0; ++i) {
      const Shot* s = shot_at(i);
      if (s == nullptr || s->kind == 0) continue;
      int owner = -1;
      for (int k = 0; k < 8; ++k)
        if (s->owner != nullptr && s->owner == g.enemy_units[k]) owner = k;
      char t[96];
      std::snprintf(t, sizeof(t), " k%x/e%d/%d,%d,%d/a%d/h%d", s->kind, owner, s->pos[0] >> 12, s->pos[1] >> 12,
                    s->pos[2] >> 12, s->anim, s->hit_landed);
      line += t;
    }
  }
  std::fprintf(stderr, "[enemy] vblank %llu rng %u %s\n", static_cast<unsigned long long>(runtime::Runtime::get().vblanks()),
               rng_draws(), line.c_str());
}


}  // namespace ss::game::battle
