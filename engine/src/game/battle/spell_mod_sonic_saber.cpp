// Spell code module b82e4cf842 (entries 0x8ff / 0x900): Sonic Saber (spell 0x9): sparks
// whirl round the caster and gather for 120 ticks, then a blade of light (the data's sprite,
// anim 0) flies from the caster through the target, hitting what it meets, over a glowing
// path (anim 2) drawn from the caster past the target. Functions 0x8008cc00 (the handler),
// 0x8008d9c4 (a whirling spark, a kind-0x21 callback) and 0x8008dcd8 (the blade, kind 0x21);
// the jump table of the handler's steps at 0x8008e3dc.

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the gathering's ticks (step 1), s24 the camera turned to the
// caster, s28 the path's brightness (0..0x80), s3c the blade Shot (set: the pointer is kept
// in g_blade); obj the data's sprite set (anim 2 the path's cel).
std::unordered_map<const SpellCast*, Shot*> g_blade;

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

// 0x8008d9c4: a whirling spark: a gouraud triangle from the Shot's position (the caster's
// chest) out to radius +0x11c (growing 0x20 a tick to +0x130) at yaw +0x128 / pitch +0x12c
// (turning +0x118 / +0x120 a tick), colour +0x134; +0x10c counts down from 0x78 and the spark
// darkens below 0 and goes at -0x40.
void whirling_spark(Shot* s) {
  BattleGlobals& g = bg();
  s32 zero[3] = {0, 0, 0};
  obj_set_matrix(kZeroVector, zero, kUnitScale);
  s32 r = s->point[1];
  psyq::SVECTOR v[3] = {};
  for (int k = 0; k < 2; ++k) {
    const u32 yaw = static_cast<u32>(s->p128 + k * 0x38);
    const u32 pitch = static_cast<u32>(s->p12c + k * 0x38);
    v[k].vx = static_cast<s16>((((bcos(yaw) * bcos(pitch)) >> 12) * r + s->pos[0]) >> 12);
    v[k].vy = static_cast<s16>((s->pos[1] - bsin(yaw) * r) >> 12);
    v[k].vz = static_cast<s16>((((bcos(yaw) * bsin(pitch)) >> 12) * r + s->pos[2]) >> 12);
  }
  v[2].vx = static_cast<s16>(s->pos[0] >> 12);
  v[2].vy = static_cast<s16>(s->pos[1] >> 12);
  v[2].vz = static_cast<s16>(s->pos[2] >> 12);
  s32 level_a = 0x100, level_b = 0x80;
  if (s->aux < 0) {
    level_a = s->aux * 4 + 0x100;
    level_b = s->aux * 2 + 0x80;
  }
  u32 colours[3];
  colours[0] = scale_colour(static_cast<u32>(s->p134), level_a) | 0x32000000;
  colours[1] = colours[0];
  colours[2] = scale_colour(static_cast<u32>(s->p134), level_b);
  draw_tri_g3(v, colours, 1);
  if ((g.flags & 0xC) != 0) return;
  r += g.frame_ticks * 0x20;
  s->p128 = (s->p128 + s->point[0] * g.frame_ticks) & 0xFFF;
  s->aux = static_cast<s16>(s->aux - g.frame_ticks);
  s->p12c = (s->p12c + s->point[2] * g.frame_ticks) & 0xFFF;
  if (s->p130 < r) r = s->p130;
  s->point[1] = r;
  if (s->aux < -0x40) shot_kill(s);
}

// 0x8008dcd8: the blade: anim 0's cel as one flat quad turned to its flight (twice the cel's
// size), flying at +0x118 (+0x10c the combo kind: 0 the target, 2 the combo point only, 3 the
// combo point and then back at the caster). While it flies (+0x134 0) it hits what its box
// (200 units) meets on side +0x130 once per unit (power +0x128); past the point it fades
// over 32 ticks (+0x134 counting up) and goes.
void blade(Shot* s) {
  BattleGlobals& g = bg();
  if (s->p134 != 0) {
    s->colour_mode = 2;
    s->rgb[0] = static_cast<u8>(-0x80 - s->p134 * 2);
  }
  s->anim = 0;
  bobj_anim_draw(s, (g.flags & 0xC) | 0x9800);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  u32 code;
  if (s->colour_mode == 0) {
    code = 0x2C808080;
  } else {
    const u32 grey = s->rgb[0];
    code = grey | grey << 8 | grey << 16 | 0x2C000000;
    s->p128 += g.frame_ticks * 4;
  }
  const s32 y = s->pos[1];
  const s32 ang = ratan2(s->vel[2], s->vel[0]);
  const s32 du = uv[2] - uv[0], dv = uv[3] - uv[1];
  const s32 ax = (du * bsin(static_cast<u32>(ang)) + dv * bcos(static_cast<u32>(ang))) * 2;
  const s32 az = (-du * bcos(static_cast<u32>(ang)) + dv * bsin(static_cast<u32>(ang))) * 2;
  const s32 ang2 = ang + 0x400;
  const s32 bx = (du * bsin(static_cast<u32>(ang2)) + dv * bcos(static_cast<u32>(ang2))) * 2;
  const s32 bz = (-du * bcos(static_cast<u32>(ang2)) + dv * bsin(static_cast<u32>(ang2))) * 2;
  const s32 c[4][4] = {{s->pos[0] + bx, y, s->pos[2] + bz, 0},
                       {s->pos[0] + ax, y, s->pos[2] + az, 0},
                       {s->pos[0] - ax, y, s->pos[2] - az, 0},
                       {s->pos[0] - bx, y, s->pos[2] - bz, 0}};
  draw_textured_patch(uv, c, f.clut, static_cast<u16>(f.tpage | 0x20), 0, code, 2);
  if ((g.flags & 0xC) != 0) return;
  const s32 before = vec_distance_fx(s->pos, s->point);
  bobj_move(s, s->vel);
  const s32 after = vec_distance_fx(s->pos, s->point);
  if (s->p134 == 0) {
    if (before < after) {
      if (s->aux == 3) {
        s->p12c = 0;
        s->aux = 0;
        const Vec4 d = vec_direction(s->pos, s->owner->pos);
        s->vel[0] = d.x, s->vel[1] = d.y, s->vel[2] = d.z, s->extra34 = d.w;
        // ORIGINAL BUG (original_bugs.md S5): only x is scaled to the blade's speed (0x28); y
        // and z keep the unit vector, so the blade turning back flies almost flat along x:
        //   s->vel[0] *= 0x28;
        s->vel[0] *= 0x28;
        s->vel[1] *= 0x28;
        s->vel[2] *= 0x28;
        return;
      }
      if (s->aux != 0) {
        shot_kill(s);
        return;
      }
      s->p134 = 1;
    }
  }
  if (s->p134 != 0) {
    s->p134 += g.frame_ticks;
    if (0x20 < s->p134) {
      shot_kill(s);
      return;
    }
    if (s->p134 != 0) return;
  }
  if (s->aux != 0 && s->aux != 3) return;
  // The hit.
  // ORIGINAL BUG (original_bugs.md S4): the console's frame keeps 4 units, 4 hit units and
  // 4 points in a row (the hit list starts at the 5th unit): with more than 4 units in the
  // box the list overwrites units 5-8 before they are read, points 5-8 overrun the stack
  // frame (the saved registers), and the points passed on are the unfiltered ones:
  //   Unit* buf[12]; Unit** units = buf; Unit** list = buf + 4; ... (s, k, list, points, ...)
  Unit* units[8] = {};
  Vec4 points[8] = {};
  collider_set_attack_box(s, 200, 200, 200);
  s->size[2] = 0x100;
  const int n = collider_attack_overlaps(s, units, points, s->p130);
  if (n == 0) return;
  Unit* list[8] = {};
  Vec4 list_points[8] = {};
  int k = 0;
  for (int i = 0; i < n; ++i) {
    Unit* u = units[i];
    if (((s->hit_landed >> (u->slot & 0x1F)) & 1) == 0) {
      list_points[k] = points[i];
      list[k++] = u;
    }
    s->hit_landed = static_cast<u8>(s->hit_landed | (1 << (units[i]->slot & 0x1F)));
  }
  if (k == 0) return;
  shot_apply_damage(s, k, list, list_points, s->p128, 0x4B, 8, 0, 0x10);
  screen_shake2(3, 4, 4, 4, 0xC);
  s32 bits = static_cast<s16>(s->power / 0x1E) + 4;
  if (8 < bits) bits = 8;
  for (int i = 0; i < bits; ++i) {
    Sprite* sp = shot_spawn_sprite_at(s, 2, 1);
    if (sp == nullptr) continue;
    const Unit* u = list[0];
    const s32 dx = rng_between(u->size[0] * -2, u->size[0] << 1);
    const s32 x = u->pos[0] + dx * 0x1000;
    const s32 dy = rng_between(0x30000, 0xA0000);
    sprite_set_pos(sp, x, u->pos[1] - dy, u->pos[2] - 0x4000);
    sp->scale_y = 0x2000;
    sp->scale_x = 0x2000;
    sp->layer = 2;
  }
}

// Steps 2-5: the path: two strips of 8 x 8 patches of anim 2's cel (grey s28), 250..700
// units either side of the line from the caster's chest through the target's (0x50 above
// both) to as far again beyond it.
void draw_path(SpellCast* sc) {
  BObj* o = &sc->obj;
  o->colour_mode = 2;
  o->anim = 2;
  o->rgb[0] = static_cast<u8>(sc->s28);
  bobj_anim_draw(o, 0x9800);
  const Unit* c = sc->caster;
  const Unit* t = sc->target;
  const s32 from[3] = {c->pos[0], c->pos[1] + c->size[1] * -0x2000 + -0x50000, c->pos[2]};
  const s32 mid[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000 + -0x50000, t->pos[2]};
  const s32 far[3] = {mid[0] * 2 - from[0], mid[1] * 2 - from[1], mid[2] * 2 - from[2]};
  const s32 ang = ratan2(mid[2] - from[2], mid[0] - from[0]) + 0x400;
  const s32 cs = bcos(static_cast<u32>(ang)), sn = bsin(static_cast<u32>(ang));
  s32 nx = cs * 0xFA, nz = sn * 0xFA, fx = cs * 700, fz = sn * 700;
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  for (int side = 0; side < 2; ++side) {
    const s32 a[4][4] = {{mid[0] + nx, mid[1], mid[2] + nz, 0},
                         {mid[0] + fx, mid[1], mid[2] + fz, 0},
                         {from[0] + nx, from[1], from[2] + nz, 0},
                         {from[0] + fx, from[1], from[2] + fz, 0}};
    draw_textured_patch(uv, a, clut, static_cast<u16>(tpage | 0x20), 0, bobj_poly_tag(o, tpage), 8);
    const s32 b[4][4] = {{far[0] + nx, far[1], far[2] + nz, 0},
                         {far[0] + fx, far[1], far[2] + fz, 0},
                         {mid[0] + nx, mid[1], mid[2] + nz, 0},
                         {mid[0] + fx, mid[1], mid[2] + fz, 0}};
    const u32 code = bobj_poly_tag(o, tpage);
    nx = -nx, nz = -nz, fx = -fx, fz = -fz;
    draw_textured_patch(uv, b, clut, static_cast<u16>(tpage | 0x20), 0, code, 8);
  }
}

void sonic_saber(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;
  sc->eff_power = c->st()->intel[2] / 4 + sc->power / 4 + 0x15E;
  if (1 < sc->step) draw_path(sc);
  if ((g.flags & 4) != 0) return;
  if (static_cast<u32>(sc->step - 2) < 2) {
    sc->s28 += g.frame_ticks * 2;
    if (0x80 < sc->s28) sc->s28 = 0x80;
  } else if (3 < sc->step) {
    sc->s28 += g.frame_ticks * -2;
    if (sc->s28 < 0) sc->s28 = 0;
  }
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0: {
      // Dim, camera behind the caster, the whirling sparks.
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x30, 0x30, 0x30);
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        const s32 yaw = ratan2(c->pos[2] - t->pos[2], c->pos[0] - t->pos[0]);
        const s32 dist = vec_distance_fx(c->pos, t->pos);
        camera_move_to(g.camera, t->pos, yaw + 0x800, 0x71, (dist >> 12) + 0x800, 0, 0x80, 5);
        camera_move_to(g.camera, t->pos, yaw + 0x400, 0xE3, 0x400, 0x1E, 0x80, 5);
      }
      s32 n = sc->power / 0x14 + 8;
      if (0x10 < n) n = 0x10;
      for (int i = 0; i < n; ++i) {
        Shot* s = shot_spawn_callback_always(whirling_spark);
        if (s == nullptr) continue;
        s->pos[0] = c->pos[0];
        s->pos[1] = c->pos[1] + c->size[1] * -0x4000;
        s->pos[2] = c->pos[2];
        s->p128 = static_cast<s32>(rng_range(0x1000));
        s->p12c = rng_between(-0x400, 0x400);
        s->p130 = rng_between(300, 400);
        s->point[0] = 0xB;
        s->point[2] = -0xB;
        s->p134 = 0xC0FFFF;
        s->aux = 0x78;
      }
      ++sc->step;
      bsfx_play_ui(0x26);
      return;
    }
    case 1: {
      // 120 ticks of streaks gathering on the caster; a ring at the end, the camera turns
      // to the caster after 30.
      sc->s20 += g.frame_ticks;
      if (0x78 < sc->s20) {
        ++sc->step;
        const s32 at[4] = {c->pos[0], c->pos[1] + c->size[1] * -0x4000, c->pos[2], 0};
        particle_spawn_ring(at, 0x10, 0x40, 0x40, 0x1E, 0x402080, 0x8040FF, 0, 0);
      }
      if (0x1E < sc->s20 && sc->s24 == 0) {
        sc->s24 = 1;
        const s32 yaw = ratan2(c->pos[2] - t->pos[2], c->pos[0] - t->pos[0]);
        // (The original also takes vec_distance_fx(caster, target) here and drops it.)
        if (spellcast_leads_camera(sc)) camera_move_to(g.camera, c->pos, yaw, 0x155, 0x800, 0x1E, 0x80, 5);
      }
      const s32 chest[4] = {c->pos[0], c->pos[1] + c->size[1] * -0x4000, c->pos[2], 0};
      const Vec4 from = vec_random_around(chest, 800);
      particle_spawn_homing_line(from.data(), chest, 0x406060, 0xC0FFFF, 0x60, 0x6000, 0x1000);
      return;
    }
    case 2: {
      // The blade, from the caster's chest at the target (or the combo point).
      Shot* s = shot_spawn_callback_always(blade);
      if (s != nullptr) {
        g_blade[sc] = s;
        sc->s3c = 1;
        s->pos[0] = c->pos[0];
        s->pos[1] = c->pos[1] + c->size[1] * -0x2000;
        s->pos[2] = c->pos[2];
        s32 point[4];
        const int r = spellcast_combo_point(sc, point);
        if (r == 0) {
          point[0] = t->pos[0];
          point[1] = t->pos[1] + t->size[1] * -0x2000;
          point[2] = t->pos[2];
        }
        s->point[0] = point[0], s->point[1] = point[1], s->point[2] = point[2];
        s->p130 = c->is_enemy;
        if (sc->combo == 3) s->p130 = c->is_enemy ^ 1;
        if (sc->combo == 4) sc->eff_power += spellcast_slot(sc->slot ^ 1)->eff_power;
        bobj_share_sprite(&sc->obj, s);
        const Vec4 d = vec_direction(c->pos, s->point);
        s->vel[0] = d.x, s->vel[1] = d.y, s->vel[2] = d.z, s->extra34 = d.w;
        s->vel[0] *= 0x28;
        s->vel[1] *= 0x28;
        s->vel[2] *= 0x28;
        s->aux = static_cast<s16>(r);
        s->p128 = sc->eff_power;
        s->power = static_cast<s16>(sc->power);
        s->owner = c;
        s->target = t;
      }
      bsfx_play_at(0x34, s);
      ++sc->step;
      [[fallthrough]];
    }
    case 3: {
      // The camera rides behind the blade while it flies; on when every effect is gone.
      Shot* s = sc->s3c != 0 ? g_blade[sc] : nullptr;
      if (s != nullptr && spellcast_leads_camera(sc) && s->p134 == 0) {
        const s32 yaw = ratan2(-s->vel[2], -s->vel[0]);
        camera_move_to(g.camera, s->pos, yaw, 0x155, 0x800, 10, 0x80, 5);
      }
      if (spellcast_effects_gone(sc) != 0) ++sc->step;
      return;
    }
    case 4:
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      spellcast_wait(sc, 0x1E);
      return;
    case 5:
      if (spellcast_leads_camera(sc)) camera_end_mode(g.camera, g.camera->mode, 0, nullptr);
      sc->finished = 1;
      ++sc->step;
      g_blade.erase(sc);
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_sonic_saber(SpellCast* sc) { sonic_saber(sc); }

}  // namespace ss::game::battle
