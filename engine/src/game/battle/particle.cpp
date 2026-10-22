#include "game/battle/particle.h"

#include <cstring>
#include <memory>
#include <string>

#include "game/battle/battle.h"
#include "game/battle/combat.h"
#include "game/battle/render.h"
#include "game/battle/stage.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/common/screen.h"
#include "game/title/new_game.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

constexpr int kParticles = 200;
const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c
std::unique_ptr<Particle[]> g_pool;  // g_pool_48 0x8006fac8

psx::Gte& gte() { return psyq::gte(); }

u32 svec_xy(s32 x, s32 y) { return static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16; }

void particle_reset(Particle* p) {  // 0x8005dfcc
  p->in_pause = (bg().flags & 0x40) != 0 ? 1 : 0;
  p->p34 = p->p38 = p->p3c = p->p40 = p->p44 = 0;
  p->vel[0] = p->vel[1] = p->vel[2] = 0;
  p->p24[0] = p->p24[1] = p->p24[2] = 0;
}

// 0x8005e264: the GTE draws relative to the particle (view rotation; translation = the
// rotated offset from the eye).
void particle_set_gte_origin(const Particle* p) {
  ViewState& v = view();
  psx::Gte& g = gte();
  const s16 dx = static_cast<s16>((p->pos[0] - v.view_eye[0]) >> 12);
  const s16 dy = static_cast<s16>((p->pos[1] - v.view_eye[1]) >> 12);
  const s16 dz = static_cast<s16>((p->pos[2] - v.view_eye[2]) >> 12);
  psyq::SetRotMatrix(&v.render_view);
  g.write_data(psx::Gte::VXY0, svec_xy(dx, dy));
  g.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(dz)));
  g.execute(psx::Gte::kRtv0);
  psyq::MATRIX m = v.render_view;
  m.t[0] = static_cast<s32>(g.read_data(psx::Gte::MAC1));
  m.t[1] = static_cast<s32>(g.read_data(psx::Gte::MAC2));
  m.t[2] = static_cast<s32>(g.read_data(psx::Gte::MAC3));
  psyq::SetRotMatrix(&v.render_view);
  psyq::SetTransMatrix(&m);
}

void particle_move(Particle* p) {  // 0x8005e368
  const s32 t = bg().frame_ticks;
  p->pos[0] += p->vel[0] * t;
  p->pos[1] += p->vel[1] * t;
  p->pos[2] += p->vel[2] * t;
}

bool on_screen(u32 sxy) {
  return static_cast<u16>(static_cast<s16>(sxy & 0xFFFF) + 0x28) < 0x1A4 &&
         static_cast<u16>(static_cast<s16>(sxy >> 16) + 0x3C) < 300;
}

// 0x8005e3cc: an additive LINE_G2 from the GTE origin (colour +0x38) to 'tail' (+0x3c),
// sorted by the average depth of its two ends.
void particle_draw_line(const Particle* p, s16 tx, s16 ty, s16 tz) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  if (!arena_has_room(pool, 0x20)) return;
  psx::Gte& q = gte();
  q.write_data(psx::Gte::VXY0, 0);
  q.write_data(psx::Gte::VZ0, 0);
  q.execute(psx::Gte::kRtps);
  const u32 head = q.read_data(psx::Gte::SXY2);
  if (!on_screen(head)) return;
  q.write_data(psx::Gte::VXY0, svec_xy(tx, ty));
  q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(tz)));
  q.execute(psx::Gte::kRtps);
  const s32 flag = static_cast<s32>(q.read_data(psx::Gte::FLAG));
  const u32 tail = q.read_data(psx::Gte::SXY2);
  if (flag < 0 || !on_screen(tail)) return;
  // ORIGINAL BUG (original_bugs.md B3): the console reads the OTZ register here, which RTPS
  // does not set, so the line sorts at the depth of whatever ran the last AVSZ:
  //   const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ));
  // The line's own depth instead: AVSZ4 over its two ends (head SZ2, tail SZ3), twice each.
  const u32 sz_head = q.read_data(psx::Gte::SZ2), sz_tail = q.read_data(psx::Gte::SZ3);
  q.write_data(psx::Gte::SZ0, sz_head);
  q.write_data(psx::Gte::SZ1, sz_head);
  q.write_data(psx::Gte::SZ2, sz_tail);
  q.write_data(psx::Gte::SZ3, sz_tail);
  q.execute(psx::Gte::kAvsz4);
  const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ));
  if (0x5F9 < (otz >> 5)) return;
  auto* line = screen_alloc_prims<psyq::LINE_G2>(pool->arena, 1);
  auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
  if (line == nullptr || dm == nullptr) return;
  psyq::setLineG2(line);
  const u32 c0 = static_cast<u32>(p->p38), c1 = static_cast<u32>(p->p3c);
  std::memcpy(&line->r0, &c0, 3);
  line->code = 0x52;
  std::memcpy(&line->r1, &c1, 3);
  line->x0 = static_cast<s16>(head & 0xFFFF);
  line->y0 = static_cast<s16>(head >> 16);
  line->x1 = static_cast<s16>(tail & 0xFFFF);
  line->y1 = static_cast<s16>(tail >> 16);
  psyq::P_TAG* ot = &pool->ot_head[0x616 - (otz >> 5)];
  psyq::addPrim(ot, line);
  psyq::SetDrawMode(dm, 0, 1, 0x20, nullptr);
  psyq::addPrim(ot, dm);
  pool_a_bytes_used() += 0x20;
}

bool frozen(const Particle* p) {
  u8 f = bg().flags;
  if (p->in_pause != 0) f = static_cast<u8>(f & 0xBF);
  return f != 0;
}

// Kind 1 (0x8005e618): a streak flying to +0x24, accelerating; freed on arrival.
void particle_k1_homing_line(Particle* p) {
  BattleGlobals& g = bg();
  particle_set_gte_origin(p);
  const Vec4 d = vec_velocity_toward(p->pos, p->p24, 1);
  p->vel[0] = d.x, p->vel[1] = d.y, p->vel[2] = d.z, p->vel[3] = d.w;
  particle_draw_line(p, static_cast<s16>((p->vel[0] * p->p44) >> 12), static_cast<s16>((p->vel[1] * p->p44) >> 12),
                     static_cast<s16>((p->vel[2] * p->p44) >> 12));
  if (frozen(p)) return;
  p->vel[0] = (p->vel[0] * p->p40) >> 12;
  p->vel[1] = (p->vel[1] * p->p40) >> 12;
  p->vel[2] = (p->vel[2] * p->p40) >> 12;
  particle_move(p);
  p->p40 += p->p34 * g.frame_ticks;
  s32 dist = vec_distance_fx(p->pos, p->p24);
  if (dist < 0) dist = -dist;
  if (dist <= p->p40 * g.frame_ticks + p->p44) {
    p->pos[0] = p->p24[0], p->pos[1] = p->p24[1], p->pos[2] = p->p24[2];
    particle_free(p);
  }
}

// Kind 2 (0x8005e7d8): a streak along the velocity with gravity 0x600.
void particle_k2_spark_line(Particle* p) {
  BattleGlobals& g = bg();
  particle_set_gte_origin(p);
  const Vec4 n = vec_normalize(p->vel);
  particle_draw_line(p, static_cast<s16>((n.x * p->p44) >> 12), static_cast<s16>((n.y * p->p44) >> 12),
                     static_cast<s16>((n.z * p->p44) >> 12));
  if ((g.flags & 0xC) != 0) return;
  particle_move(p);
  p->vel[1] += g.frame_ticks * 0x600;
  p->p40 -= g.frame_ticks;
  if (p->p40 < 1 || 0 < p->pos[1]) particle_free(p);
}

// Kind 3 (0x8005e8f0): a flat ring of +0x34 additive POLY_G4 from radius +0x44 to +0x44 +
// +0x38 (colours +0x28 inside, +0x24 outside, fading over the last 32 ticks), growing +0x3c a tick.
void particle_k3_ring(Particle* p) {
  BattleGlobals& g = bg();
  s32 orient[3] = {p->p24[2], 0, 0};
  obj_set_matrix(p->pos, orient, kUnitScale);
  RenderContext* pool = g.prim_pool_a;
  if (arena_has_room(pool, p->p34 * 0x30)) {
    psx::Gte& q = gte();
    s32 c0 = bcos(0), s0 = bsin(0);
    u32 in = static_cast<u32>(p->p24[1]);   // +0x28
    u32 out = static_cast<u32>(p->p24[0]);  // +0x24
    if (p->p40 < 0x20) {
      in = scale_colour(in, p->p40 << 2);
      out = scale_colour(out, p->p40 << 2);
    }
    const s32 r = p->p44, r2 = p->p44 + p->p38;
    for (int i = 0; i < p->p34; ++i) {
      const s32 a = ((i + 1) * 0x1000) / p->p34;
      const s32 c1 = bcos(static_cast<u32>(a)), s1 = bsin(static_cast<u32>(a));
      q.write_data(psx::Gte::VXY0, svec_xy((c0 * r) >> 12, 0));
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(static_cast<s16>((s0 * r) >> 12))));
      q.execute(psx::Gte::kRtps);
      const u32 v0 = q.read_data(psx::Gte::SXY2);
      q.write_data(psx::Gte::VXY0, svec_xy((c1 * r) >> 12, 0));
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(static_cast<s16>((s1 * r) >> 12))));
      q.write_data(psx::Gte::VXY1, svec_xy((c0 * r2) >> 12, 0));
      q.write_data(psx::Gte::VZ1, static_cast<u32>(static_cast<s32>(static_cast<s16>((s0 * r2) >> 12))));
      q.write_data(psx::Gte::VXY2, svec_xy((c1 * r2) >> 12, 0));
      q.write_data(psx::Gte::VZ2, static_cast<u32>(static_cast<s32>(static_cast<s16>((s1 * r2) >> 12))));
      q.execute(psx::Gte::kRtpt);
      const u32 v1 = q.read_data(psx::Gte::SXY0), v2 = q.read_data(psx::Gte::SXY1), v3 = q.read_data(psx::Gte::SXY2);
      auto x_in = [](u32 v) { return static_cast<u16>(static_cast<s16>(v & 0xFFFF) + 0x28) < 0x1A4; };
      auto y_in = [](u32 v) { return static_cast<u16>(static_cast<s16>(v >> 16) + 0x3C) < 300; };
      if ((x_in(v0) || x_in(v1) || x_in(v2) || x_in(v3)) && (y_in(v0) || y_in(v1) || y_in(v2) || y_in(v3))) {
        q.execute(psx::Gte::kAvsz4);
        const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ));
        if ((otz >> 3) < 0x5FA) {
          auto* quad = screen_alloc_prims<psyq::POLY_G4>(pool->arena, 1);
          auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
          if (quad != nullptr && dm != nullptr) {
            psyq::setPolyG4(quad);
            std::memcpy(&quad->r0, &in, 3);
            quad->code = 0x3A;
            std::memcpy(&quad->r1, &in, 3);
            std::memcpy(&quad->r2, &out, 3);
            std::memcpy(&quad->r3, &out, 3);
            quad->x0 = static_cast<s16>(v0 & 0xFFFF), quad->y0 = static_cast<s16>(v0 >> 16);
            quad->x1 = static_cast<s16>(v1 & 0xFFFF), quad->y1 = static_cast<s16>(v1 >> 16);
            quad->x2 = static_cast<s16>(v2 & 0xFFFF), quad->y2 = static_cast<s16>(v2 >> 16);
            quad->x3 = static_cast<s16>(v3 & 0xFFFF), quad->y3 = static_cast<s16>(v3 >> 16);
            psyq::P_TAG* ot = &pool->ot_head[0x616 - (otz >> 3)];
            psyq::addPrim(ot, quad);
            psyq::SetDrawMode(dm, 0, 1, 0x20, nullptr);
            psyq::addPrim(ot, dm);
            pool_a_bytes_used() += 0x30;
          }
        }
      }
      c0 = c1;
      s0 = s1;
    }
  }
  if (frozen(p)) return;
  p->p44 += p->p3c;
  p->p40 -= g.frame_ticks;
  if (p->p40 < 1) particle_free(p);
}

void particle_unported(Particle* p) {
  runtime::Runtime::get().log_once("battle-particle-" + std::to_string(p->kind),
                                   "battle: particle kind " + std::to_string(p->kind) + " not drawn yet (removed)");
  particle_free(p);
}

void particle_update(Particle* p) {  // 0x8005e1b0
  if (bg().effects_suppressed != 0 && p->in_pause == 0) return;
  switch (p->kind) {
    case 1: particle_k1_homing_line(p); break;
    case 2: particle_k2_spark_line(p); break;
    case 3: particle_k3_ring(p); break;
    default: particle_unported(p); break;
  }
}

void set_vec(s32 dst[4], const s32 src[4]) {
  dst[0] = src[0], dst[1] = src[1], dst[2] = src[2], dst[3] = src[3];
}

}  // namespace

void particle_pool_create() { g_pool = std::make_unique<Particle[]>(kParticles); }
void particle_pool_destroy() { g_pool.reset(); }

void particle_pool_update() {
  if (!g_pool) return;
  for (int i = 0; i < kParticles; ++i)
    if (g_pool[i].kind != 0) particle_update(&g_pool[i]);
}

Particle* particle_alloc(int kind) {
  if (!g_pool) return nullptr;
  for (int i = 0; i < kParticles; ++i) {
    if (g_pool[i].kind != 0) continue;
    particle_reset(&g_pool[i]);
    g_pool[i].kind = static_cast<u16>(kind);
    return &g_pool[i];
  }
  return nullptr;
}

void particle_free(Particle* p) {
  if (p != nullptr) p->kind = 0;
}

Particle* particle_spawn_homing_line(const s32 pos[4], const s32 target[4], u32 head, u32 tail, s32 length,
                                     s32 speed, s32 accel) {
  Particle* p = particle_alloc(1);
  if (p == nullptr) return nullptr;
  set_vec(p->pos, pos);
  set_vec(p->p24, target);
  p->p34 = accel;
  p->p38 = static_cast<s32>(head);
  p->p3c = static_cast<s32>(tail);
  p->p40 = speed;
  p->p44 = length;
  return p;
}

Particle* particle_spawn_spark_line(const s32 pos[4], const s32 vel[4], u32 head, u32 tail, s32 length, s32 life) {
  Particle* p = particle_alloc(2);
  if (p == nullptr) return nullptr;
  set_vec(p->pos, pos);
  set_vec(p->vel, vel);
  p->p38 = static_cast<s32>(head);
  p->p3c = static_cast<s32>(tail);
  p->p40 = life;
  p->p44 = length;
  return p;
}

Particle* particle_spawn_ring(const s32 pos[4], s32 segments, s32 width, s32 growth, s32 life, u32 colour_out,
                              u32 colour_in, s32 radius, s32 yaw) {
  Particle* p = particle_alloc(3);
  if (p == nullptr) return nullptr;
  set_vec(p->pos, pos);
  p->p34 = segments;
  p->p38 = width;
  p->p3c = growth;
  p->p40 = life;
  p->p44 = radius;
  p->p24[0] = static_cast<s32>(colour_out);  // +0x24
  p->p24[1] = static_cast<s32>(colour_in);   // +0x28
  p->p24[2] = yaw;                           // +0x2c
  return p;
}

Particle* particle_spawn_shard(const s32 pos[4], s32 a34, s32 a38, s32 a3c, s32 a24, s32 a2c, s32 a40, s32 a44) {
  Particle* p = particle_alloc(4);
  if (p == nullptr) return nullptr;
  set_vec(p->pos, pos);
  p->p34 = a34;
  p->p38 = a38;
  p->p3c = a3c;
  p->p40 = a40;
  p->p44 = a44;
  p->p24[0] = a24;
  p->p24[2] = a2c;
  return p;
}

Particle* particle_spawn_confetti(const s32 pos[4], const s32 vel[4], u32 colour, s32 size, s32 life) {
  Particle* p = particle_alloc(5);
  if (p == nullptr) return nullptr;
  set_vec(p->pos, pos);
  set_vec(p->vel, vel);
  p->p38 = static_cast<s32>(colour);
  p->p3c = size;
  p->p40 = life;
  p->p34 = static_cast<s32>(rng_range(0x1000));
  return p;
}

}  // namespace ss::game::battle
