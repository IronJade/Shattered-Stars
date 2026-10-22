// Spell code module 7f6a6e02c6 (entries 0x8fa-0x8fc): Eruption (spell 0x2): the ground under
// the target bursts (a glowing disc, spark rings, a shake), a curtain of heat shimmers over
// the screen and rocks rain on the area for 280 ticks; then everything of the other side
// within the disc's radius is hit (element 4). Functions 0x8008cc00 (the handler) and
// 0x8008d918 (a falling rock, a kind-0x21 callback); the jump table of the handler's steps at
// 0x8008daf8. Resident 0x8007cc5c draw_disc and 0x8007d9e0 draw_wave_grid (#1984.0) are not
// in the port's shared code and are ported here.

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/common/screen.h"
#include "game/title/new_game.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the disc's radius (growing 16 a tick to the area radius, then
// shrinking once s30 is set), s24 the shimmer's wave phase, s28 the rain's ticks (step 3),
// s2c the rock timer, s30 set when the rain ends, s34 the shimmer's brightness (0..0x100),
// s3c the rumble's sound handle; obj the data's sprite set (anims 5 / 6 the shimmer, 2 the
// burst, 1 the dust, 2-4 rocks).

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

s32 mul_lo(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }
u32 svec_xy(s32 x, s32 y) { return static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16; }

// 0x8007cc5c draw_disc (#1984.0): a flat fan of 'segments' gouraud triangles (POLY_G3) in
// the x-z plane round the current GTE origin, 'radius' units: rgb_centre at the centre (its
// code 0x30, 0x32 when blend is set), rgb_edge on the rim; blend != 0 adds a DR_MODE (mode
// blend - 1) after each. A triangle with no vertex inside x 0..319 / y -64..279, or with
// otz / 8 >= 0x5fa - ot, is skipped; the others go to OT entry 0x616 + ot - otz / 8 of pool
// A. (The console checks room for all of them, takes the arena's cursor and commits what it
// used at the end.)
void draw_disc(s32 segments, s32 radius, u32 rgb_centre, u32 rgb_edge, s32 blend, s32 ot) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  const s32 size = blend != 0 ? 0x28 : 0x1C;
  if (pool == nullptr || !arena_has_room(pool, mul_lo(size, segments))) return;
  const u32 code = rgb_centre | (blend != 0 ? 0x32000000u : 0x30000000u);
  psx::Gte& q = psyq::gte();
  s32 used = 0;
  s32 x = radius, z = 0;
  for (s32 i = 0; i < segments; ++i) {
    const s32 ang = ((i + 1) << 12) / segments;
    const s32 px = x, pz = z;
    x = mul_lo(bcos(static_cast<u32>(ang)), radius) >> 12;
    z = mul_lo(bsin(static_cast<u32>(ang)), radius) >> 12;
    q.write_data(psx::Gte::VXY0, svec_xy(0, 0));
    q.write_data(psx::Gte::VZ0, 0);
    q.write_data(psx::Gte::VXY1, svec_xy(px, 0));
    q.write_data(psx::Gte::VZ1, static_cast<u32>(static_cast<s32>(static_cast<s16>(pz))));
    q.write_data(psx::Gte::VXY2, svec_xy(x, 0));
    q.write_data(psx::Gte::VZ2, static_cast<u32>(static_cast<s32>(static_cast<s16>(z))));
    q.execute(psx::Gte::kRtpt);
    const u32 s0 = q.read_data(psx::Gte::SXY0), s1 = q.read_data(psx::Gte::SXY1), s2 = q.read_data(psx::Gte::SXY2);
    q.execute(psx::Gte::kAvsz3);
    auto x_in = [](u32 xy) { return static_cast<u16>(xy) < 0x140; };
    auto y_in = [](u32 xy) { return static_cast<u16>(static_cast<u16>(xy >> 16) + 0x40) < 0x158; };
    if (!(x_in(s0) || x_in(s1) || x_in(s2))) continue;
    if (!(y_in(s0) || y_in(s1) || y_in(s2))) continue;
    const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ)) >> 3;
    if (!(otz < 0x5FA - ot)) continue;
    auto* p = screen_alloc_prims<psyq::POLY_G3>(pool->arena, 1);
    if (p == nullptr) return;
    psyq::setPolyG3(p);
    std::memcpy(&p->r0, &code, 4);
    std::memcpy(&p->r1, &rgb_edge, 4);
    std::memcpy(&p->r2, &rgb_edge, 4);
    p->x0 = static_cast<s16>(s0), p->y0 = static_cast<s16>(s0 >> 16);
    p->x1 = static_cast<s16>(s1), p->y1 = static_cast<s16>(s1 >> 16);
    p->x2 = static_cast<s16>(s2), p->y2 = static_cast<s16>(s2 >> 16);
    psyq::P_TAG* entry = &pool->ot_head[0x616 + ot - otz];
    psyq::addPrim(entry, p);
    used += 0x1C;
    if (blend != 0) {
      auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
      if (dm == nullptr) return;
      psyq::SetDrawMode(dm, 0, 1, (blend - 1) << 5, nullptr);
      psyq::addPrim(entry, dm);
      used += 0xC;
    }
  }
  pool_a_bytes_used() += used < 4 ? 4 : used;
}

// 0x8007d9e0 draw_wave_grid (#1984.0; as spell_mod_star_flare.cpp ports it): an n x n grid
// of screen-space POLY_FT4 over rect {x0, y0, x1, y1} with the UVs of uv {u0, v0, u1, v1}
// spread over it, every vertex row shifted in x by wave_x {amp, phase, freq} (amp * sin(phase
// + freq * row / n) >> 24) and every vertex column in y by wave_y alike; all at OT entry
// 0x610 - ot of pool A, colour word code_rgb.
void draw_wave_grid(const s32 uv[4], const s32 rect[4], const s32 wave_x[3], const s32 wave_y[3], u16 clut,
                    u16 tpage, s32 ot, u32 code_rgb, s32 n) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  // (screen_alloc 0x80069fd4 of n * n * 0x28 bytes at once; none: nothing is drawn.)
  const s32 bytes = mul_lo(n, n * 0x28);
  if (pool == nullptr || !arena_has_room(pool, bytes)) return;
  pool_a_bytes_used() += bytes;
  const s32 u0 = static_cast<s16>(uv[0]);  // (the console keeps uv[0] << 16 and shifts it back)
  const s32 v0 = static_cast<s16>(uv[1]);
  const s32 du = static_cast<s16>(uv[2]) - u0;
  const s32 dv = static_cast<s16>(uv[3]) - v0;
  const s32 w = rect[2] - rect[0];
  const s32 h = rect[3] - rect[1];
  const u32 clut_hi = static_cast<u32>(clut) << 16;
  const u32 tpage_hi = static_cast<u32>(tpage) << 16;
  s32 xd_low = mul_lo(wave_x[0], bsin(static_cast<u32>(wave_x[1]))) >> 24;
  s32 v_low = v0 << 8;
  for (s32 row = 0; row < n; ++row) {
    const s32 v_high = v_low;
    v_low = (mul_lo(dv, row + 1) / n + v0) << 8;
    s32 yd_next = mul_lo(wave_y[0], bsin(static_cast<u32>(wave_y[1]))) >> 24;
    const s32 xd_high = xd_low;
    xd_low = mul_lo(wave_x[0], bsin(static_cast<u32>(mul_lo(wave_x[2], row + 1) / n + wave_x[1]))) >> 24;
    const s32 y_high = mul_lo(h, row) / n + rect[1];
    const s32 y_low = mul_lo(h, row + 1) / n + rect[1];
    s32 u_next = u0;
    for (s32 col = 0; col < n; ++col) {
      const s32 yd = yd_next;
      yd_next = mul_lo(wave_y[0], bsin(static_cast<u32>(mul_lo(wave_y[2], col + 1) / n + wave_y[1]))) >> 24;
      const s32 u = u_next;
      u_next = mul_lo(du, col + 1) / n + u0;
      const s32 x_a = mul_lo(w, col) / n + rect[0];
      const s32 x_b = mul_lo(w, col + 1) / n + rect[0];
      auto* p = screen_alloc_prims<psyq::POLY_FT4>(pool->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyFT4(p);
      std::memcpy(&p->r0, &code_rgb, 4);
      p->x0 = static_cast<s16>(x_a + xd_high), p->y0 = static_cast<s16>(y_high + yd);
      p->x1 = static_cast<s16>(x_b + xd_high), p->y1 = static_cast<s16>(y_high + yd_next);
      p->x2 = static_cast<s16>(x_a + xd_low), p->y2 = static_cast<s16>(y_low + yd);
      p->x3 = static_cast<s16>(x_b + xd_low), p->y3 = static_cast<s16>(y_low + yd_next);
      const u32 uv0 = static_cast<u32>(u | v_high) | clut_hi;
      const u32 uv1 = static_cast<u32>(u_next | v_high) | tpage_hi;
      const u32 uv2 = static_cast<u32>(u | v_low);
      const u32 uv3 = static_cast<u32>(u_next | v_low);
      p->u0 = static_cast<u8>(uv0), p->v0 = static_cast<u8>(uv0 >> 8), p->clut = static_cast<u16>(uv0 >> 16);
      p->u1 = static_cast<u8>(uv1), p->v1 = static_cast<u8>(uv1 >> 8), p->tpage = static_cast<u16>(uv1 >> 16);
      p->u2 = static_cast<u8>(uv2), p->v2 = static_cast<u8>(uv2 >> 8);
      p->u3 = static_cast<u8>(uv3), p->v3 = static_cast<u8>(uv3 >> 8);
      psyq::addPrim(&pool->ot_head[0x610 - ot], p);
    }
  }
}

// 0x8008d918: a rock (anims 2-4 of the cast's set, 0x1a-0x1b of the fx bank) falling at 32 a
// tick toward +0x118 (+0x10c the combo kind: 3 first takes a random point of the area round
// its unit +0x114, radius +0x128; 2 just goes); at the point: debris, sfx 0x23, a shake, and
// it goes.
void falling_rock(Shot* s) {
  BattleGlobals& g = bg();
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  const Vec4 d = vec_direction(s->pos, s->point);
  s->vel[0] = d.x, s->vel[1] = d.y, s->vel[2] = d.z, s->extra34 = d.w;
  const s32 dist = vec_distance_fx(s->pos, s->point);
  s->vel[0] <<= 5;
  s->vel[2] <<= 5;
  s->vel[1] <<= 5;
  bobj_move(s, s->vel);
  if (!(dist < g.frame_ticks * 0x30000)) return;
  if (s->aux == 3) {
    s->p12c = 0;
    s->aux = 0;
    const Vec4 p = vec_random_in_disc_xz(s->target->pos, s->p128);
    s->point[0] = p.x, s->point[1] = p.y, s->point[2] = p.z;
    s->point[1] = stage_ground_height(s->point[0] >> 12, s->point[2] >> 12);
    return;
  }
  if (s->aux != 2) {
    s->pos[1] = bobj_ground_height(s) + -0x10000;
    effect_debris_burst(s->pos, kZeroVector, 1, 0, 1, 0x2000, 0);
    bsfx_play_at_pos(0x23, s->pos);
    screen_shake2(3, 2, 2, 2, 10);
  }
  shot_kill(s);
}

// Steps 2 on: the glowing disc on the ground under the target.
void draw_ground_disc(SpellCast* sc, s32 radius) {
  BattleGlobals& g = bg();
  const Unit* t = sc->target;
  const s32 at[3] = {t->pos[0], bobj_ground_height(t), t->pos[2]};
  s32 orient[3] = {0, 0, 0};
  obj_set_matrix(at, orient, kUnitScale);
  draw_disc(0x20, sc->s20, 0x80FFFF, 0x820, 2, 5);
  if ((g.flags & 4) != 0) return;
  if (sc->s30 == 0) {
    sc->s20 += g.frame_ticks * 0x10;
    if (radius < sc->s20) sc->s20 = radius;
  } else {
    sc->s20 += g.frame_ticks * -0x10;
    if (sc->s20 < 0) sc->s20 = 0;
  }
}

// Steps 3 on: the heat shimmer: anims 5 and 6 of the sprite set, each as two wave-distorted
// 10 x 10 grids side by side at the top of the screen (x 0xa0), brightness s34 (the second
// half again brighter); false when the frame is paused after it.
bool draw_shimmer(SpellCast* sc) {
  BattleGlobals& g = bg();
  u32 code = scale_colour(0x808080, sc->s34) | 0x2E000000;
  s32 wave_x[3] = {0x10000, sc->s24, 0x2000};
  if ((g.flags & 4) == 0) {
    if (sc->s30 == 0) {
      sc->s34 += g.frame_ticks * 6;
      if (0x100 < sc->s34) sc->s34 = 0x100;
    } else {
      sc->s34 += g.frame_ticks * -6;
      if (sc->s34 < 0) sc->s34 = 0;
    }
  }
  if (sc->s34 == 0) return true;
  const s32 wave_y[3] = {0, 0, 0};  // (memset 0x80023f10)
  BObj* o = &sc->obj;
  for (int i = 0; i < 2; ++i) {
    o->anim = static_cast<u16>(i + 5);
    bobj_anim_draw(o, 0x9800);
    const FrameUv& f = frame_uv();
    const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
    for (int j = 0; j < 2; ++j) {
      s32 rect[4];
      rect[0] = (j - 1) * (uv[2] - uv[0]) + 0xA0;
      rect[1] = 0;
      rect[2] = rect[0] + (uv[2] - uv[0]);
      rect[3] = uv[3] - uv[1];
      draw_wave_grid(uv, rect, wave_x, wave_y, o->animset->clut[0], static_cast<u16>(o->animset->tpage[0] | 0x20), 8,
                     code, 10);
    }
    wave_x[1] = sc->s24 + 0x400;
    code = scale_colour(0x808080, sc->s34 / 2 + 0x80) | 0x2E000000;
  }
  if ((g.flags & 4) != 0) return false;
  sc->s24 += g.frame_ticks * 0x28;
  return true;
}

void eruption(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + -0x200000;
  s32 radius = sc->power / 2 + 0x300;
  sc->centre[2] = t->pos[2];
  sc->eff_power = c->st()->intel[2] + sc->power / 2 + 800;
  if (0x800 < radius) radius = 0x800;
  if (1 < sc->step) draw_ground_disc(sc, radius);
  if (2 < sc->step && !draw_shimmer(sc)) return;
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0: {
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x30, 0x30, 0x30);
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, t->pos, 0xC00, 0x71, 0x800, 0, 0x80, 5);
        const s32 yaw = rng_range(2) != 0 ? 0xB1C : 0xCE3;
        camera_move_to(g.camera, t->pos, yaw, 0x155, 0xC00, 0x3C, 0x80, 5);
      }
      spellcast_wait(sc, 0x1E);
      return;
    }
    case 1: {
      // The burst: a sprite and sfx 0x50 at the target's feet, a sphere, three spark rings, a
      // red-out, a long shake.
      sc->s3c = static_cast<s32>(bsfx_play_ui(0x2D));
      if (Sprite* sp = sprite_spawn_from(2, 5, &sc->obj, nullptr)) {
        const s32 at[3] = {t->pos[0], bobj_ground_height(t), t->pos[2]};
        sprite_set_pos_v(sp, at);
        sp->layer = 2;
        bsfx_play_at(0x50, sp);
      }
      s32 at[3] = {t->pos[0], bobj_ground_height(t), t->pos[2]};
      at[1] += -0x20000;
      effect_spawn_sphere_at_default(at, 8, 8, 0x26, 0x78, 0x4080FF, 0, 0);
      if (Shot* r = effect_spawn_sparks_at(at, 0x20, 0x1E0, 0, 0x28, 0x78, 5, 3, 0x10, 0)) r->vel[0] = 0x16;
      effect_spawn_sparks_at(at, 0x20, 0, 0x1E0, 0x30, 0x78, 0, 1, 0x80, 0);
      at[1] += -0xC8000;
      effect_spawn_sparks_at(at, 0x20, 0x1E0, 0, 0x27, 0x78, 2, 3, 0x10, 0);
      screen_fade_create(0, 0x20, -4, 0x1E);  // (0x8001fd94 allocates its 0x1c bytes)
      battle_flash_set(sc, 0x40, 0x18, 0x18);
      screen_shake(4, 2, 0x6E);
      spellcast_wait(sc, 0x3C);
      return;
    }
    case 2:
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0xCE3 : 0xB1C;
        camera_move_to(g.camera, t->pos, yaw, 0x155, 0xC00, 0, 0x80, 5);
        const s32 yaw2 = rng_range(2) != 0 ? 0xDC7 : 0xA38;
        camera_move_to(g.camera, t->pos, yaw2, 0x71, 0x800, 400, 0x80, 5);
      }
      ++sc->step;
      return;
    case 3: {
      // 280 ticks of rain: dust and a rock whenever the timer passes 0x8000 (power-scaled).
      sc->s28 += g.frame_ticks;
      if (0x118 < sc->s28) ++sc->step;
      sc->s2c += g.frame_ticks * 3000 + sc->power * 2;
      if (!(0x7FFF < sc->s2c)) return;
      sc->s2c += -0x8000;
      if (Sprite* sp = sprite_spawn_from(1, 0, &sc->obj, nullptr)) {
        Vec4 p = vec_random_in_disc_xz(t->pos, radius);
        p.y = stage_ground_height(p.x >> 12, p.z >> 12);
        sprite_set_pos_v(sp, p.data());
        sprite_set_fade(sp, 0x80, -4, 0x1E);
        sp->scale_vel[1] = -0x20;
      }
      Shot* s = shot_spawn_callback_always(falling_rock);
      if (s == nullptr) return;
      const Vec4 from = vec_random_in_disc_xz(t->pos, radius);
      s->pos[0] = from.x, s->pos[1] = from.y, s->pos[2] = from.z, s->pos_w = from.w;
      s->pos[1] = static_cast<s32>(0xFFD00000);
      s32 point[4];
      const int r = spellcast_combo_point(sc, point);
      if (r == 0) {
        const Vec4 p = vec_random_in_disc_xz(t->pos, radius);
        point[0] = p.x, point[1] = p.y, point[2] = p.z;
        point[1] = stage_ground_height(point[0] >> 12, point[2] >> 12);
      }
      s->point[0] = point[0], s->point[1] = point[1], s->point[2] = point[2];
      const s32 k = static_cast<s32>(rng_range(5));
      const BObj* src = g.sprite_bank_fx.get();
      if (k < 3) {
        s->anim = static_cast<u16>(k + 2);
        src = &sc->obj;
      } else {
        s->anim = static_cast<u16>(k + 0x17);
      }
      bobj_share_sprite(src, s);
      s->p128 = radius;
      s->aux = static_cast<s16>(r);
      s->target = t;
      return;
    }
    case 4:
      if (sc->s30 == 0) {
        sc->s30 = 1;
        battle_flash_set(sc, 0x80, 0x80, 0x80);
        spellcast_dim_end(sc);
      }
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 5: {
      // The hit: the standing units of the other side within the radius (none on combos 1 / 2).
      if (1 < static_cast<u8>(sc->combo - 1)) {
        const int side = sc->combo == 3 ? c->is_enemy ^ 1 : c->is_enemy;
        if (sc->combo == 4) sc->eff_power += spellcast_slot(sc->slot ^ 1)->eff_power;
        const int count = side != 0 ? 4 : 8;
        Unit* list[8] = {};
        int n = 0;
        for (int i = 0; i < count; ++i) {
          Unit* u = side == 0 ? static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)])
                              : static_cast<Unit*>(g.party_units[static_cast<size_t>(i)]);
          if (unit_is_down(u)) continue;
          if (vec_distance_xz_fx(u->pos, sc->centre) < radius << 12) list[n++] = u;
        }
        if (n != 0) spellcast_damage_units(sc, n, list, sc->eff_power, 4, 0x10, side);
      }
      sound_handle_fade_out(static_cast<u32>(sc->s3c), 0x3C, 0);
      sc->finished = 1;
      ++sc->step;
      return;
    }
    default: return;
  }
}

}  // namespace

void spell_mod_eruption(SpellCast* sc) { eruption(sc); }

}  // namespace ss::game::battle
