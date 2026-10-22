#include "game/battle/shot.h"

#include <cstring>
#include <memory>
#include <string>

#include "game/battle/bobj.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/render.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/common/screen.h"
#include "game/title/new_game.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

constexpr int kShots = 160;
std::unique_ptr<Shot[]> g_pool;  // g_pool_13c 0x8006fac4

// Kind 1 (0x8005566c): three stun stars circling the head of +0x114; keeps the target's
// stun bit (stats +2 0x40) set for +0x128 ticks.
void shot_k01_stun_orbit(Shot* s) {
  BattleGlobals& g = bg();
  shot_use_anims_f928(s);
  s->anim = 0x12;
  Unit* t = s->target;
  if (t == nullptr) {
    shot_kill(s);
    return;
  }
  if (s->p128 < 1 || unit_is_down(t)) {
    t->stats[2] = static_cast<u8>(t->stats[2] & 0xBF);
    if (t->is_enemy == 0) static_cast<PartyUnit*>(t)->busy = 0;
    shot_kill(s);
    return;
  }
  t->stats[2] = static_cast<u8>(t->stats[2] | 0x40);
  const s32 cx = t->pos[0], cz = t->pos[2];
  const s32 rx = t->size[0] * 2 + 0x10, rz = t->size[2] * 2;
  s->pos[0] = cx + bcos(static_cast<u32>(s->p12c)) * rx;
  s->pos[2] = cz + bsin(static_cast<u32>(s->p12c)) * rz;
  s->pos[1] = t->pos[1] + t->size[1] * -0x4000;
  // (The console draws the first star, then twice at 120 degrees and once at 240: four
  // draws, each advancing the animation.)
  for (int deg = 0x78; deg < 0xF1; deg += 0x78) {
    bobj_project(s);
    bobj_anim_draw(s, 0);
    const s32 a = (deg * 0x1000) / 0x168;
    s->pos[0] = cx + bcos(static_cast<u32>(s->p12c + a)) * rx;
    s->pos[2] = cz + bsin(static_cast<u32>(s->p12c + a)) * rz;
    bobj_project(s);
    bobj_anim_draw(s, 0);
  }
  if ((g.flags & 8) == 0) {
    const u32 a = static_cast<u32>(s->p12c + (g.frame_ticks * 0xA000) / 0x168) & 0xFFF;
    s->p12c = static_cast<s32>(a);
    s->roll = static_cast<s16>(a);
    if (g.flags == 0) s->p128 -= g.frame_ticks;
  }
}

// The quad UV table of kind 0x11 (0x8006ff78: 8 entries of uv2, uv3, uv0, uv1; entry 2 is
// rewritten from the current frame's UV box).
u16 g_ring_uv[8][4];
bool g_ring_uv_loaded = false;

void ring_uv_load() {
  if (g_ring_uv_loaded) return;
  g_ring_uv_loaded = true;
  for (int k = 0; k < 8; ++k)
    for (int i = 0; i < 4; ++i) {
      const u8* p = battle_data(0x8006FF78 + static_cast<u32>(k * 8 + i * 2));
      g_ring_uv[k][i] = p != nullptr ? static_cast<u16>(p[0] | p[1] << 8) : 0;
    }
}

u32 svec_xy(s32 x, s32 y) { return static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16; }

// Kind 0x11 (0x80058b78): a ring of +0x128 textured quads (radius +0x138, width +0x118,
// height +0x11c; UV variant +0x120, cycling 4..7 when 4 or more; CLUT 0x7d40 + +0x12c)
// turning by +0x28 and growing by +0x130 for +0x134 ticks; following +0x110 when set.
void shot_k11_sprite_ring(Shot* s) {
  BattleGlobals& g = bg();
  static const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c
  ring_uv_load();
  if (s->spark_owner != nullptr) {
    s->pos[0] = s->spark_owner->pos[0];
    s->pos[1] = s->spark_owner->pos[1];
    s->pos[2] = s->spark_owner->pos[2];
  }
  s32 orient[3] = {s->vel[2], 0, 0};
  obj_set_matrix(s->pos, orient, kUnitScale);
  bool draw = true;
  u16 tex_tpage = 0, tex_clut = 0;
  if (s->point[2] == 2) {
    const u32 f = s->power != 0 ? (g.flags & 0xC) : g.flags;
    bobj_anim_draw(s, f | 0x9800);
    const FrameUv& uv = frame_uv();
    if (uv.v0 == uv.v1) {
      draw = false;
    } else {
      tex_tpage = uv.tpage;
      tex_clut = uv.clut;
      g_ring_uv[2][0] = static_cast<u16>((uv.u0 & 0xFF) | (uv.v0 & 0xFF) << 8);
      g_ring_uv[2][1] = static_cast<u16>((uv.u1 & 0xFF) | (uv.v0 & 0xFF) << 8);
      g_ring_uv[2][2] = static_cast<u16>((uv.u0 & 0xFF) | (uv.v1 & 0xFF) << 8);
      g_ring_uv[2][3] = static_cast<u16>((uv.u1 & 0xFF) | (uv.v1 & 0xFF) << 8);
    }
  }
  RenderContext* pool = s->pool;
  if (draw && arena_has_room(pool, s->p128 * 0x28)) {
    psx::Gte& q = psyq::gte();
    s32 c0 = bcos(static_cast<u32>(s->aux)), s0 = bsin(static_cast<u32>(s->aux));
    u32 colour = 0x808080;
    if (s->p134 < 0x20) colour = scale_colour(0x808080, s->p134 << 2);
    for (int i = 0; i < s->p128; ++i) {
      if (3 < s->point[2]) {
        ++s->point[2];
        if (7 < s->point[2]) s->point[2] = 4;
      }
      const s32 a = ((i + 1) << 12) / s->p128;
      const s32 c1 = bcos(static_cast<u32>(a + s->aux)), s1 = bsin(static_cast<u32>(a + s->aux));
      const s32 r = s->p138, r2 = s->p138 + s->point[0];
      const s16 h = static_cast<s16>(-s->point[1]);
      q.write_data(psx::Gte::VXY0, svec_xy((c0 * r) >> 12, 0));
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(static_cast<s16>((s0 * r) >> 12))));
      q.execute(psx::Gte::kRtps);
      const u32 v0 = q.read_data(psx::Gte::SXY2);
      q.write_data(psx::Gte::VXY0, svec_xy((c1 * r) >> 12, 0));
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(static_cast<s16>((s1 * r) >> 12))));
      q.write_data(psx::Gte::VXY1, svec_xy((c0 * r2) >> 12, h));
      q.write_data(psx::Gte::VZ1, static_cast<u32>(static_cast<s32>(static_cast<s16>((s0 * r2) >> 12))));
      q.write_data(psx::Gte::VXY2, svec_xy((c1 * r2) >> 12, h));
      q.write_data(psx::Gte::VZ2, static_cast<u32>(static_cast<s32>(static_cast<s16>((s1 * r2) >> 12))));
      q.execute(psx::Gte::kRtpt);
      const u32 v1 = q.read_data(psx::Gte::SXY0), v2 = q.read_data(psx::Gte::SXY1), v3 = q.read_data(psx::Gte::SXY2);
      c0 = c1;
      s0 = s1;
      auto x_in = [](u32 v) { return static_cast<u16>(static_cast<u16>(v) + 0x28) < 0x1A4; };
      auto y_in = [](u32 v) { return static_cast<u16>(static_cast<u16>(v >> 16) + 0x3C) < 300; };
      if (!(x_in(v0) || x_in(v1) || x_in(v2) || x_in(v3))) continue;
      q.execute(psx::Gte::kAvsz4);
      if (!(y_in(v0) || y_in(v1) || y_in(v2) || y_in(v3))) continue;
      const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ)) >> 3;
      if (0x5F8 < otz) continue;
      auto* p = screen_alloc_prims<psyq::POLY_FT4>(pool->arena, 1);
      if (p == nullptr) break;
      psyq::setPolyFT4(p);
      const u32 rgbc = colour | 0x2E000000u;
      std::memcpy(&p->r0, &rgbc, 4);
      const int k = s->point[2] & 7;
      if (s->point[2] == 2) {
        p->clut = tex_clut;
        p->tpage = tex_tpage;
      } else {
        p->clut = static_cast<u16>(s->p12c + 0x7D40);
        p->tpage = s->point[2] < 4 ? 0x2F : 0x3F;
      }
      auto put_uv = [](u8* u, u16 uv) { u[0] = static_cast<u8>(uv), u[1] = static_cast<u8>(uv >> 8); };
      put_uv(&p->u0, g_ring_uv[k][2]);
      put_uv(&p->u1, g_ring_uv[k][3]);
      put_uv(&p->u2, g_ring_uv[k][0]);
      put_uv(&p->u3, g_ring_uv[k][1]);
      p->x0 = static_cast<s16>(v0), p->y0 = static_cast<s16>(v0 >> 16);
      p->x1 = static_cast<s16>(v1), p->y1 = static_cast<s16>(v1 >> 16);
      p->x2 = static_cast<s16>(v2), p->y2 = static_cast<s16>(v2 >> 16);
      p->x3 = static_cast<s16>(v3), p->y3 = static_cast<s16>(v3 >> 16);
      psyq::addPrim(&pool->ot_head[0x617 - otz], p);
      if (pool == g.prim_pool_a) pool_a_bytes_used() += 0x28;
    }
  }
  if (s->power == 1 && (g.flags & 0x40) != 0) return;
  if ((g.flags & 0xBF) != 0) return;
  const s32 t = g.frame_ticks;
  s->pos[0] += s->vel[1] * t;
  s->aux = static_cast<s16>((static_cast<u16>(s->aux) + s->vel[0] * t) & 0xFFF);
  if (s->power != 0) s->p138 += (s->p130 * t) / 2;
  else s->p138 += s->p130;
  s->p134 -= t;
  if (s->p134 <= 0 || (s->point[2] == 2 && kStateOver <= g.state && g.end_countdown < 0x18)) shot_kill(s);
}

// Kinds 0x21 / 0x33 (0x8005b530): the handler at +0x138.
void shot_k21_callback(Shot* s) {
  if (s->handler != nullptr) s->handler(s);
}

void unported_kind(Shot* s) {
  runtime::Runtime::get().log_once("battle-shot-" + std::to_string(s->kind),
                                   "battle: Shot kind " + std::to_string(s->kind) + " not ported (removed)");
  shot_kill(s);
}

}  // namespace

void shot_pool_create() {
  g_ring_uv_loaded = false;
  g_pool = std::make_unique<Shot[]>(kShots);
  for (int i = 0; i < kShots; ++i) g_pool[i].kind = 0;
}

void shot_pool_destroy() { g_pool.reset(); }

Shot* shot_at(int index) { return g_pool && index >= 0 && index < kShots ? &g_pool[index] : nullptr; }

void shot_pool_update() {
  if (!g_pool) return;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind != 0) g_pool[i].update();
}

void Shot::update() {
  BattleGlobals& g = bg();
  if (g.effects_suppressed != 0 && kind != 0x21 && command != 0xA5) return;
  switch (kind) {
    case 1: shot_k01_stun_orbit(this); break;
    case 0x11: shot_k11_sprite_ring(this); break;
    case 0x21:
    case 0x33: shot_k21_callback(this); break;
    default: unported_kind(this); break;
  }
}

Shot* shot_spawn(int kind) {
  if (!g_pool) return nullptr;
  for (int i = 0; i < kShots; ++i) {
    Shot* s = &g_pool[i];
    if (s->kind != 0) continue;
    shot_reset(s);
    s->kind = static_cast<u16>(kind);
    return s;
  }
  return nullptr;
}

int shot_count_free() {
  if (!g_pool) return 0;
  int n = 0;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind == 0) ++n;
  return n;
}

void shot_reset(Shot* s) {
  bobj_anim_reset(s);
  s->p128 = s->p12c = s->p130 = s->p134 = s->p138 = 0;
  s->handler = nullptr;
  s->spark_owner = nullptr;
  for (s32& v : s->user) v = 0;
  s->lift = 0;
  s->animset = nullptr;
  s->blend = 0;
  s->clut_offset = 0;
  s->loop_limit = 0;
  s->texcache = nullptr;
  s->colour_mode = 0;
  s->owner = nullptr;
  s->target = nullptr;
  s->scale_y = s->scale_x = 0x1000;
  s->facing[0] = s->facing[1] = s->facing[2] = 0;
  s->vel[0] = s->vel[1] = s->vel[2] = 0;
  s->point[0] = s->point[1] = s->point[2] = 0;
  s->roll = 0;
  s->aux = 0;
  s->command = 0;
  s->pool = bg().prim_pool_a;
}

void shot_kill(Shot* s) {
  BattleGlobals& g = bg();
  if (s == nullptr) return;
  s->kind = 0;
  if (g.camera == nullptr) return;
  camera_set_focus_timer(g.camera, s, 0x14);
  if (g.camera->mode == 0xC && g.camera->target == s) camera_reset_mode(g.camera);
}

void shot_use_anims_f928(BObj* o) { bobj_share_sprite(bg().sprite_set_a.get(), o); }
void shot_use_anims_f920(BObj* o) { bobj_share_sprite(bg().sprite_bank_fx.get(), o); }

Sprite* shot_spawn_sprite(Shot* s, int anim) { return sprite_spawn_from(anim, 0, s, nullptr); }
Sprite* shot_spawn_sprite_at(Shot* s, int anim, int arg) { return sprite_spawn_from(anim, arg, s, nullptr); }

int shot_hit_melee(Shot* s, int power, int vs_party) {
  Unit* units[8] = {};
  Vec4 points[8] = {};
  const int n = collider_attack_overlaps(s, units, points, vs_party);
  if (n == 0) return 0;
  s->hit_landed = 1;
  s32 dir = s->vel[0];
  if (dir == 0) dir = s->owner->facing[0];
  const int r = unit_attack_targets(s->owner, n, units, points, power, dir < 0 ? 0x800 : 0, 0) + 1;
  s->owner->attack_recovery = 0;
  return r;
}

void shot_place_at_unit(Shot* s, const BObj* u, s32 dx, s32 dy, s32 dz) {
  s->pos[0] = u->pos[0] + bobj_facing_fx(u, dx);
  s->pos[1] = u->pos[1] + dy * -0x1000;
  s->pos[2] = u->pos[2] + dz;
  s->facing[0] = u->facing[0];
}

int shot_draw_and_box(Shot* s, int size, u32 shadow_colour) {
  bobj_project(s);
  const int frame = bobj_anim_draw(s, bg().flags);
  s32 r = s->ext_x1;
  if (r < 0) r = -r;
  r *= size;
  bobj_draw_shadow(s, -r, r, -r, r, shadow_colour);
  return frame;
}

int shot_draw_and_box_ground(Shot* s, int size, u32 shadow_colour) {
  bobj_project(s);
  const int frame = bobj_anim_draw(s, bg().flags);
  s32 h = bobj_ground_height(s) - s->pos[1];
  if (h < 0) h += 0xFFFF;
  s32 r = s->ext_x1;
  if (r < 0) r = -r;
  r = r * size - (h >> 16);
  if (r < 0x10) r = 0x10;
  bobj_draw_shadow(s, -r, r, -r, r, shadow_colour);
  return frame;
}

int shot_fade_out(Shot* s) {
  BattleGlobals& g = bg();
  if (kStateOver <= g.state) {
    if (s->p12c == 0) s->p12c = 1;
  }
  if (s->p12c == 0) return 0;
  if ((g.flags & 0xBF) != 0) return 0;
  s->colour_mode = 2;
  s->rgb[0] = static_cast<u8>(s->p12c * -4 - 0x80);
  s->p12c += g.frame_ticks;
  if (0x18 < s->p12c) {
    shot_kill(s);
    return 1;
  }
  return 0;
}

Shot* shot_spawn_callback(ShotFn fn) {
  Shot* s = shot_spawn(0x33);
  if (s != nullptr) s->handler = fn;
  return s;
}

Shot* shot_spawn_callback_always(ShotFn fn) {
  Shot* s = shot_spawn(0x21);
  if (s != nullptr) s->handler = fn;
  return s;
}

int shot_kind_absent(int kind) {
  if (!g_pool) return 1;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind == static_cast<u16>(kind)) return 0;
  return 1;
}

int shot_kind21_absent(ShotFn fn) {
  if (!g_pool) return 1;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind == 0x21 && g_pool[i].handler == fn) return 0;
  return 1;
}

Shot* effect_spawn_sparks_at(const s32 pos[3], s32 count, s32 p118, s32 p11c, s32 p130, s32 p134, s32 p12c,
                             s32 p120, s32 p138, s16 power) {
  Shot* s = shot_spawn(0x11);
  if (s == nullptr) return nullptr;
  s->pos[0] = pos[0], s->pos[1] = pos[1], s->pos[2] = pos[2];
  s->p128 = count;
  s->p12c = p12c;
  s->p130 = p130;
  s->p134 = p134;
  s->p138 = p138;
  s->point[0] = p118;
  s->point[1] = p11c;
  s->point[2] = p120;
  s->power = power;
  return s;
}

Shot* effect_spawn_sparks_on(BObj* owner, s32 count, s32 p118, s32 p11c, s32 p130, s32 p134, s32 p12c, s32 p120,
                             s32 p138, s16 power) {
  Shot* s = shot_spawn(0x11);
  if (s == nullptr) return nullptr;
  // (The owner field holds any BObj here; only its position is read.)
  s->spark_owner = owner;
  s->p128 = count;
  s->p12c = p12c;
  s->p130 = p130;
  s->p134 = p134;
  s->p138 = p138;
  s->point[0] = p118;
  s->point[1] = p11c;
  s->point[2] = p120;
  s->power = power;
  return s;
}

// ---- unit_apply_stun (0x80055588) --------------------------------------------------------

void unit_apply_stun(Unit* u) {
  if (u->status_test(0, 0x1000000) != 0) return;
  if (g_pool) {
    for (int i = 0; i < kShots; ++i) {
      Shot* s = &g_pool[i];
      if (s->kind == 1 && s->target == u) {
        s->p128 = 0x78;
        return;
      }
    }
  }
  Shot* s = shot_spawn(1);
  if (s == nullptr) return;
  s->target = u;
  s->p128 = u->status_test(0, 0x800000) != 0 ? 0x3C : 0x78;
}

}  // namespace ss::game::battle
