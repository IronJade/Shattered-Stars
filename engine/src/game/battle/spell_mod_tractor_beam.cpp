// Spell code module 3d76980103 (entries 0x903-0x905): Tractor Beam (spell 0x16): a turning
// column of coloured light (and a disc on the ground) rises round the target and widens;
// every standing, walking unit of the target's side inside it is lifted, turned upside down
// and dropped: it bounces where it lands (a ring, dust, a puff) and all of them are hit at
// once (element 0x20). Functions 0x8008cc00 (the handler; its step jump table at
// 0x8008df6c) and 0x8008dce4 (the landing ring, a kind-0x21 callback); the lifted units'
// list at 0x8008df8c-0x8008dff4. Resident 0x8007cc5c draw_disc (#1984.0) is not in the
// port's shared code and is ported here (as spell_mod_eruption.cpp ports it).

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/particle.h"
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

// The SpellCast's fields this module uses: s20 the column's radius (units), s24 its height
// (0..0x1000; 0x80 a unit of 0x1000), s28 its turn, s2c its colour phase, s30 the lift's
// ticks (step 3); obj the data's sprite set (anim 1 the landing ring, 1 the dust).

// The module's own data (one copy for both cast slots, as on the console): the lifted units.
struct Lifted {
  s32 count = 0;        // 0x8008dff4
  Unit* units[8] = {};  // 0x8008df8c
  u8 active[8] = {};    // 0x8008dfac: still falling / bouncing
  s32 home_y[8] = {};   // 0x8008dfb4: the y it was lifted from
  s32 vel_y[8] = {};    // 0x8008dfd4
};
Lifted g_lift;

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

s32 mul_lo(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }
u32 svec_xy(s32 x, s32 y) { return static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16; }

// 0x8007cc5c draw_disc (#1984.0): a flat fan of 'segments' gouraud triangles (POLY_G3) in
// the x-z plane round the current GTE origin, 'radius' units: rgb_centre at the centre (its
// code 0x30, 0x32 when blend is set), rgb_edge on the rim; blend != 0 adds a DR_MODE (mode
// blend - 1) after each. A triangle with no vertex inside x 0..319 / y -64..279, or with
// otz / 8 >= 0x5fa - ot, is skipped; the others go to OT entry 0x616 + ot - otz / 8 of pool
// A. (A copy of spell_mod_eruption.cpp's port.)
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

// A phase 0..0x1fff folded to a triangle 0..0xfff.
u32 fold(u32 t) { return 0xFFF < t ? 0x1FFF - t : t; }

// 0x8008dce4: the landing ring (anim 1 as one 3 x 3 patch, +0x128 wide, flat on the ground
// a little above it, bobbing with the view pitch) growing 4 a tick; after 40 ticks it
// fades (+0x130) and goes.
void landing_ring(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  bobj_anim_draw(s, 0x9800);
  const FrameUv& f = frame_uv();
  const u16 tpage = f.tpage, clut = f.clut;
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u32 code = bobj_poly_tag(s, tpage);
  const s32 r = s->p128;
  const s32 x0 = s->pos[0] + r * -0x1000, x1 = s->pos[0] + r * 0x1000;
  const s32 z0 = s->pos[2] + r * -0x1000, z1 = s->pos[2] + r * 0x1000;
  s32 lift = bsin(static_cast<u32>(static_cast<s32>(view().pitch))) << 4;
  if (lift < 0) lift = 0;
  const s32 y = s->pos[1] - (lift + 0x10000);
  const s32 c[4][4] = {{x0, y, z0, 0}, {x1, y, z0, 0}, {x0, y, z1, 0}, {x1, y, z1, 0}};
  draw_textured_patch(uv, c, clut, tpage, 8, code, 3);
  if ((g.flags & 0xC) != 0) return;
  s->p12c += g.frame_ticks;
  s->p128 += g.frame_ticks * 4;
  if (s->p12c < 0x29) return;
  s->colour_mode = 2;
  s->rgb[0] = static_cast<u8>(-0x80 - s->p130);
  s->p130 += g.frame_ticks * 4;
  if (0x7F < s->p130) shot_kill(s);
}

// Steps 1-4: the column round u, 6 rings x 16 gouraud quads (additive) of radius s20, the
// rings s24 / 32 units high from the ground, turning by s28 (16 a tick); the colours run
// red-green-blue round it and fade toward red up it with the phase s2c (-0x60 a tick). Then
// the disc on the ground (radius s20 + 8, brightness s24 / 16).
void draw_column(SpellCast* sc, const Unit* u) {
  BattleGlobals& g = bg();
  if ((g.flags & 0xC) == 0) {
    sc->s28 += (g.frame_ticks << 4) & 0xFFF;
    sc->s2c -= (g.frame_ticks * 0x60) & 0xFFF;
  }
  const s32 ground = bobj_ground_height(u);
  for (int i = 0; i < 6; ++i) {
    const u32 fade0 = fold(static_cast<u32>(((i << 12) / 6 + sc->s2c)) & 0x1FFF);
    const u32 fade1 = fold(static_cast<u32>((((i + 1) << 12) / 6 + sc->s2c)) & 0x1FFF);
    const s32 h = sc->s24 * 0x80;
    const s32 y0 = ground + ((i * h) >> 12) * -0x1000;
    const s32 y1 = ground + (((i + 1) * h) >> 12) * -0x1000;
    for (int j = 0; j < 16; ++j) {
      const u32 a0 = static_cast<u32>(j * 0x100 + sc->s28) & 0xFFF;
      const u32 a1 = static_cast<u32>((j + 1) * 0x100 + sc->s28) & 0xFFF;
      const s32 c0 = bcos(a0) * sc->s20, s0 = bsin(a0) * sc->s20;
      const s32 c1 = bcos(a1) * sc->s20, s1 = bsin(a1) * sc->s20;
      const auto vx = [u](s32 d) { return static_cast<s16>((u->pos[0] + d) >> 12); };
      const auto vz = [u](s32 d) { return static_cast<s16>((u->pos[2] + d) >> 12); };
      const s16 ya = static_cast<s16>(y0 >> 12), yb = static_cast<s16>(y1 >> 12);
      const s16 pts[4][4] = {{vx(c0), ya, vz(s0), 0}, {vx(c1), ya, vz(s1), 0},
                             {vx(c0), yb, vz(s0), 0}, {vx(c1), yb, vz(s1), 0}};
      const int k = j + i;
      u32 col[4];
      col[2] = colour_lerp(0xFF, 0xFF00, static_cast<s32>(fold(static_cast<u32>((k + 1) * 0x200) & 0x1FFF)));
      col[3] = colour_lerp(0xFF, 0xFF00, static_cast<s32>(fold(static_cast<u32>((k + 2) * 0x200) & 0x1FFF)));
      col[2] = colour_lerp(col[2], 0xFF0000, static_cast<s32>(fade1));
      col[3] = colour_lerp(col[3], 0xFF0000, static_cast<s32>(fade1));
      col[0] = colour_lerp(0xFF, 0xFF00, static_cast<s32>(fold(static_cast<u32>(k * 0x200) & 0x1FFF)));
      col[1] = colour_lerp(0xFF, 0xFF00, static_cast<s32>(fold(static_cast<u32>((k + 1) * 0x200) & 0x1FFF)));
      col[0] = colour_lerp(col[0], 0xFF0000, static_cast<s32>(fade0)) | 0x3A000000;
      col[1] = colour_lerp(col[1], 0xFF0000, static_cast<s32>(fade0));
      gte_set_view_matrix();
      draw_quad_g4(pts, col, 0);
    }
  }
  const s32 at[3] = {u->pos[0], bobj_ground_height(u), u->pos[2]};
  s32 orient[3] = {0, sc->s28, 0};
  obj_set_matrix(at, orient, kUnitScale);
  s32 level = sc->s24;
  if (level < 0) level += 0xF;
  const u32 inner = scale_colour(0xA02070, level >> 4);
  level = sc->s24;
  if (level < 0) level += 0xF;
  const u32 rim = scale_colour(0x10C000, level >> 4);
  draw_disc(0x10, sc->s20 + 8, inner, rim, 2, 0xB);
}

// 0x8008cc00: the handler, round the target (the caster in a combo of kind 3). Power =
// INT / 2 + power / 2 + 600; the centre 0xc8 up over the unit; the column grows to
// power / 2 + 0x280. Steps 1-3 also draw streaks up from within it. 0: dim, flash 0x50, the
// camera on the unit (sfx 0x28 with it), wait 20. 1: the column widens 12 a tick and grows
// 0x80; at its full width (once the data is loaded) the camera and a rising sound. 2: the
// units of the unit's side within it (standing, not flying, not airborne) are taken. 3:
// they rise 4 a tick; after 120 ticks (or none) they turn upside down, the camera, sounds.
// 4: the column closes; they fall and bounce, the first landing turned back with a shake,
// a ring, dust, a puff and sounds; once none moves (and the column is closed, or nobody
// was taken). 5: the hit on them (element 0x20, knockback 0x10), flash back, undim. 6: the
// effects end. 7: done.
void tractor_beam(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* u = sc->combo == 3 ? sc->caster : sc->target;
  sc->centre[0] = u->pos[0];
  sc->centre[1] = u->pos[1] + u->size[1] * -0x2000;
  sc->centre[2] = u->pos[2];
  sc->centre[3] = 0;  // (an uninitialised stack word on the console)
  sc->centre[1] = static_cast<s32>(0xFFF38000);
  sc->eff_power = sc->caster->st()->intel[2] / 2 + sc->power / 2 + 600;
  if (static_cast<u32>(sc->step - 1) < 4) draw_column(sc, u);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  if (static_cast<u32>(sc->step - 1) < 3) {
    for (int i = 0; i < g.frame_ticks << 1; ++i) {
      const Vec4 p = vec_random_in_disc_xz(u->pos, sc->s20);
      const s32 from[4] = {p.x, 0, p.z, p.w};
      const s32 to[4] = {p.x, static_cast<s32>(0xFFD00000), p.z, 0};
      const u32 head = rng_range(2) != 0 ? 0x80u : 0x800000u;
      const u32 tail = rng_range(2) != 0 ? 0xFF00u : 0xFF00FFu;
      const s32 length = rng_between(0x20, 0x80);
      particle_spawn_homing_line(from, to, head, tail, length, 0x2000, 0x1000);
    }
  }
  const s32 full = sc->power / 2 + 0x280;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      battle_flash_set(sc, 0x50, 0x50, 0x50);
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, u->pos, 0xC00, 0xE3, 0x800, 0x14, 0x80, 5);
        bsfx_play_ui(0x28);
      }
      spellcast_wait(sc, 0x14);
      return;
    case 1:
      sc->s20 += g.frame_ticks * 0xC;
      if (full < sc->s20) {
        sc->s20 = full;
        if (sc->data == nullptr) {
          ++sc->step;
          const s32 at[3] = {u->pos[0], u->pos[1] - 0x140000, u->pos[2]};
          const s32 yaw = rng_range(2) != 0 ? 0xD55 : 0xAAA;
          camera_move_to(g.camera, at, yaw, 0x155, 0xC00, 0x78, 0x80, 5);
          const u32 h = bsfx_play_ui(0x46);
          sound_handle_fade_out(h, 0x3C, 0xB4);
          sound_handle_pitch_up(h, 0xB4);
        }
      }
      sc->s24 += g.frame_ticks * 0x80;
      if (0x1000 < sc->s24) sc->s24 = 0x1000;
      return;
    case 2: {
      const int n = u->is_enemy != 0 ? 8 : 4;
      g_lift.count = 0;
      if (1 < static_cast<u8>(sc->combo - 1)) {
        for (int i = 0; i < n; ++i) {
          Unit* v = u->is_enemy == 0 ? static_cast<Unit*>(g.party_units[static_cast<size_t>(i)])
                                     : static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)]);
          if (unit_is_down(v) || v->flying != 0 || v->airborne == 1) continue;
          if (!(vec_distance_xz_fx(v->pos, sc->centre) < sc->s20 << 12)) continue;
          const int k = g_lift.count;
          g_lift.active[k] = 1;
          g_lift.units[k] = v;
          g_lift.vel_y[k] = 0;
          ++g_lift.count;
          g_lift.home_y[k] = v->pos[1];
        }
      }
      ++sc->step;
      return;
    }
    case 3:
      sc->s30 += g.frame_ticks;
      if (0x78 < sc->s30 || g_lift.count == 0) {
        for (int i = 0; i < g_lift.count; ++i) g_lift.units[i]->flags |= 0x20;
        ++sc->step;
        const s32 at[3] = {u->pos[0], bobj_ground_height(u), u->pos[2]};
        camera_move_to(g.camera, at, 0xC00, 0x88, 0x800, 0x1E, 0x80, 5);
        bsfx_play_ui(0x38);
        bsfx_play_ui(0x39);
      }
      for (int i = 0; i < g_lift.count; ++i) g_lift.units[i]->pos[1] -= g.frame_ticks << 14;
      return;
    case 4: {
      sc->s20 -= g.frame_ticks * 0x10;
      if (sc->s20 < 0) sc->s20 = 0;
      sc->s24 -= g.frame_ticks * 0x40;
      if (sc->s24 < 0) sc->s24 = 0;
      int moving = 0;
      for (int i = 0; i < g_lift.count; ++i) {
        if (g_lift.active[i] == 0) continue;
        Unit* v = g_lift.units[i];
        v->pos[1] += g_lift.vel_y[i];
        g_lift.vel_y[i] += g.frame_ticks * 0x1000;
        ++moving;
        if (v->pos[1] < g_lift.home_y[i] + v->size[1] * -0x2000) continue;
        if ((v->flags & 0x20) != 0) {
          screen_shake(6, 4, 0x10);
          if (Shot* s = shot_spawn_callback_always(landing_ring)) {
            s->pos[0] = v->pos[0];
            s->pos[1] = bobj_ground_height(v);
            s->pos[2] = v->pos[2];
            s->pos_w = 0;  // (an uninitialised stack word on the console)
            s->p128 = 0x80;
            bobj_share_sprite(&sc->obj, s);
          }
          s32 n = sc->power / 0x1E + 4;
          if (8 < n) n = 8;
          for (int k = 0; k < n; ++k) {
            Sprite* sp = spellcast_sprite_spawn(sc, 1);
            if (sp == nullptr) continue;
            const s32 dx = rng_between_fixed(v->size[0] * -6, v->size[0] * 6);
            const s32 dz = rng_between_fixed(v->size[2] * -3, v->size[2] * 3);
            sprite_set_pos(sp, v->pos[0] + dx, v->pos[1], v->pos[2] + dz);
            sprite_set_fade(sp, 0x80, -2, 0xC);
            sp->layer = 2;
            const s32 scale = rng_between(0x1000, 12000);
            sp->scale_y = scale;
            sp->scale_x = scale;
          }
          const s32 at[4] = {v->pos[0], bobj_ground_height(v) - 0x40000, v->pos[2], 0};
          particle_spawn_ring(at, 0x10, 0x80, 0x28, 0x78, 0x401000, 0x80C0, 0x80, 0);
          bsfx_play_at(0x23, v);
          bsfx_play_at(0x21, v);
          v->flags &= 0xDF;
        }
        v->pos[1] = g_lift.home_y[i];
        s32 vy = -g_lift.vel_y[i];
        if (vy < 0) vy += 3;
        g_lift.vel_y[i] = vy >> 2;
        if (-0x3001 < g_lift.vel_y[i]) g_lift.active[i] = 0;
      }
      if (moving != 0) return;
      if (sc->s20 != 0 && g_lift.count != 0) return;
      ++sc->step;
      return;
    }
    case 5:
      if (g_lift.count != 0) {
        spellcast_damage_units(sc, g_lift.count, g_lift.units, sc->eff_power, 0x20, 0x10, u->is_enemy);
      }
      ++sc->step;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      return;
    case 6:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 7:
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_tractor_beam(SpellCast* sc) { tractor_beam(sc); }

}  // namespace ss::game::battle
