// Spell code module 67a7a8d842 (entries 0x928 / 0x929): Black Saber (spell 0x1f): dark
// shards swirl round the caster and gather in, then a blade (the data's sprite) flies at the
// target along a track of two glowing strips and cuts whatever it meets. Functions 0x8008cc00
// (the handler), 0x8008d9c0 (a swirling shard, a kind-0x21 callback) and 0x8008dcd4 (the
// blade, kind 0x21).

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
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the gathering's ticks (step 1), s24 the camera moved, s28 the
// track's brightness, s3c set once the blade spawned (its Shot in g_blade); obj the data's
// sprite set (anim 2 the track, the blade shares it).
std::unordered_map<const SpellCast*, Shot*> g_blade;

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

// 0x8008d9c0: a dark shard: a gouraud triangle from the Shot's position to two points of a
// sphere of radius +0x11c (growing 0x20 a tick to +0x130) at angles +0x128 / +0x12c (turning
// +0x118 / +0x120 a tick), colour +0x134; +0x10c the life, fading below 0, gone at -0x40.
void swirling_shard(Shot* s) {
  BattleGlobals& g = bg();
  s32 orient[3] = {0, 0, 0};
  obj_set_matrix(kZeroVector, orient, kUnitScale);
  s32 r = s->point[1];
  psyq::SVECTOR v[3] = {};
  for (int k = 0; k < 2; ++k) {
    const s32 a = s->p128 + k * 0x38, b = s->p12c + k * 0x38;
    const s32 x = ((bcos(static_cast<u32>(a)) * bcos(static_cast<u32>(b))) >> 12) * r + s->pos[0];
    const s32 y = s->pos[1] - bsin(static_cast<u32>(a)) * r;
    const s32 z = ((bcos(static_cast<u32>(a)) * bsin(static_cast<u32>(b))) >> 12) * r + s->pos[2];
    v[k].vx = static_cast<s16>(x >> 12);
    v[k].vy = static_cast<s16>(y >> 12);
    v[k].vz = static_cast<s16>(z >> 12);
  }
  v[2].vx = static_cast<s16>(s->pos[0] >> 12);
  v[2].vy = static_cast<s16>(s->pos[1] >> 12);
  v[2].vz = static_cast<s16>(s->pos[2] >> 12);
  s32 lit = 0x100, lit_centre = 0x80;
  if (s->aux < 0) {
    lit = s->aux * 4 + 0x100;
    lit_centre = s->aux * 2 + 0x80;
  }
  u32 c[3];
  c[0] = scale_colour(static_cast<u32>(s->p134), lit) | 0x32000000u;
  c[1] = c[0];
  c[2] = scale_colour(static_cast<u32>(s->p134), lit_centre);
  draw_tri_g3(v, c, 1);
  if ((g.flags & 0xC) != 0) return;
  r += g.frame_ticks * 0x20;
  s->p128 = (s->p128 + s->point[0] * g.frame_ticks) & 0xFFF;
  s->aux = static_cast<s16>(s->aux - g.frame_ticks);
  s->p12c = (s->p12c + s->point[2] * g.frame_ticks) & 0xFFF;
  if (s->p130 < r) r = s->p130;
  s->point[1] = r;
  if (s->aux < -0x40) shot_kill(s);
}

// 0x8008dcd4: the blade: anim 0 of the cast's sprite as a 2 x 2 patch lying along its
// velocity, flying past +0x118 (+0x10c the combo kind: 3 turns back to the caster, other
// than 0 / 3 goes); then +0x134 counts 32 ticks of fading out. While it flies (kinds 0 and 3)
// its 200-unit box cuts every unit of side +0x130 once (power +0x128, sfx 0x4b, element
// 0x100, knockback 0x10): a shake, and splinters (anim 2) and sparks (anims 4-17) on the
// first unit cut.
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
    code = grey | grey << 8 | grey << 16 | 0x2C000000u;
    s->p128 += g.frame_ticks * 4;
  }
  const s32 y = s->pos[1];
  s32 a = ratan2(s->vel[2], s->vel[0]);
  const s32 du = uv[2] - uv[0], dv = uv[3] - uv[1];
  const s32 ax = (du * bsin(static_cast<u32>(a)) + dv * bcos(static_cast<u32>(a))) * 2;
  const s32 az = (-du * bcos(static_cast<u32>(a)) + dv * bsin(static_cast<u32>(a))) * 2;
  a += 0x400;
  const s32 bx = (du * bsin(static_cast<u32>(a)) + dv * bcos(static_cast<u32>(a))) * 2;
  const s32 bz = (-du * bcos(static_cast<u32>(a)) + dv * bsin(static_cast<u32>(a))) * 2;
  const s32 corners[4][4] = {{s->pos[0] + bx, y, s->pos[2] + bz, 0},
                             {s->pos[0] + ax, y, s->pos[2] + az, 0},
                             {s->pos[0] - ax, y, s->pos[2] - az, 0},
                             {s->pos[0] - bx, y, s->pos[2] - bz, 0}};
  draw_textured_patch(uv, corners, f.clut, static_cast<u16>(f.tpage | 0x20), 0, code, 2);
  if ((g.flags & 0xC) != 0) return;
  const s32 before = vec_distance_fx(s->pos, s->point);
  bobj_move(s, s->vel);
  const s32 after = vec_distance_fx(s->pos, s->point);
  bool cut = false;
  if (s->p134 == 0) {
    if (before < after) {
      if (s->aux == 3) {
        s->p12c = 0;
        s->aux = 0;
        const Vec4 d = vec_direction(s->pos, s->owner->pos);
        s->vel[0] = d.x, s->vel[1] = d.y, s->vel[2] = d.z, s->extra34 = d.w;
        // ORIGINAL BUG? (to review): only x is scaled to the blade's speed (0x28); y and z
        // keep the unit vector's length, so the blade flies back almost along x.
        s->vel[0] *= 0x28;
        return;
      }
      if (s->aux != 0) {
        shot_kill(s);
        return;
      }
      s->p134 = 1;
    }
    cut = s->p134 == 0;
  }
  if (!cut) {
    s->p134 += g.frame_ticks;
    if (0x20 < s->p134) {
      shot_kill(s);
      return;
    }
    if (s->p134 != 0) return;
  }
  if (s->aux != 0 && s->aux != 3) return;
  // The hit list: the first four entries hold what the box met, the filtered list is
  // written from entry 4 on (the console's two adjacent 4-entry arrays).
  Unit* hit[12] = {};
  Vec4 points[8] = {};
  collider_set_attack_box(s, 200, 200, 200);
  s->size[2] = 0x100;
  const int met = collider_attack_overlaps(s, hit, points, s->p130);
  if (met == 0) return;
  // ORIGINAL BUG? (to review): the console's lists hold 4 units and 4 points: a fifth unit
  // met lands in the filtered list's slots, and points beyond the fourth run over the
  // callback's saved registers (6+ units met). The points passed on are the unfiltered ones.
  Unit** list = hit + 4;
  int n = 0;
  for (int i = 0; i < met; ++i) {
    Unit* u = hit[i];
    if (((s->hit_landed >> (u->slot & 0x1F)) & 1) == 0) list[n++] = u;
    s->hit_landed = static_cast<u8>(s->hit_landed | (1 << (hit[i]->slot & 0x1F)));
  }
  if (n == 0) return;
  shot_apply_damage(s, n, list, points, s->p128, 0x4B, 0x100, 0, 0x10);
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
    sprite_set_pos(sp, x, list[0]->pos[1] - dy, list[0]->pos[2] - 0x4000);
    sp->scale_y = 0x2000;
    sp->scale_x = 0x2000;
    sp->layer = 2;
  }
  for (int i = 0; i < bits * 2 + 8; ++i) {
    Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(0xE)) + 4);
    if (sp == nullptr) continue;
    const s32 dx = rng_between_fixed(list[0]->size[0] * -0xC, list[0]->size[0] * 0xC);
    const s32 x = list[0]->pos[0] + dx;
    const s32 dy = rng_between_fixed(list[0]->size[1] * -10, list[0]->size[1]);
    const s32 y2 = list[0]->pos[1] + dy;
    const s32 dz = rng_between_fixed(list[0]->size[2] * -0xC, list[0]->size[2] * 0xC);
    sprite_set_pos(sp, x, y2, list[0]->pos[2] + dz);
    sprite_set_fade(sp, 0x80, -2, 0x10);
    sp->size[2] = static_cast<s16>(rng_range(0x1E));
    const s32 scale = rng_between(0x2000, 0x4000);
    sp->scale_y = scale;
    sp->scale_x = scale;
    sp->layer = 3;
  }
}

// Steps 2 on: the track, two strips of anim 2's cel (8 x 8 quads) 0x50 above the centres,
// from the caster to the target and on to as far beyond it, offset 250..700 units to the
// side of the line, drawn on both sides.
void draw_track(SpellCast* sc) {
  BObj* o = &sc->obj;
  o->colour_mode = 2;
  o->anim = 2;
  o->rgb[0] = static_cast<u8>(sc->s28);
  bobj_anim_draw(o, 0x9800);
  const Unit* c = sc->caster;
  const Unit* t = sc->target;
  const s32 from[3] = {c->pos[0], c->pos[1] + c->size[1] * -0x2000 - 0x50000, c->pos[2]};
  const s32 at[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000 - 0x50000, t->pos[2]};
  const s32 far[3] = {(at[0] - from[0]) * 2 + from[0], (at[1] - from[1]) * 2 + from[1],
                      (at[2] - from[2]) * 2 + from[2]};
  const s32 a = ratan2(at[2] - from[2], at[0] - from[0]) + 0x400;
  const s32 cs = bcos(static_cast<u32>(a)), sn = bsin(static_cast<u32>(a));
  s32 nx = cs * 250, nz = sn * 250, fx = cs * 700, fz = sn * 700;
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  for (int side = 0; side < 2; ++side) {
    const s32 near_strip[4][4] = {{at[0] + nx, at[1], at[2] + nz, 0},
                                  {at[0] + fx, at[1], at[2] + fz, 0},
                                  {from[0] + nx, from[1], from[2] + nz, 0},
                                  {from[0] + fx, from[1], from[2] + fz, 0}};
    u32 code = bobj_poly_tag(o, tpage);
    draw_textured_patch(uv, near_strip, clut, static_cast<u16>(tpage | 0x20), 0, code, 8);
    const s32 far_strip[4][4] = {{far[0] + nx, far[1], far[2] + nz, 0},
                                 {far[0] + fx, far[1], far[2] + fz, 0},
                                 {at[0] + nx, at[1], at[2] + nz, 0},
                                 {at[0] + fx, at[1], at[2] + fz, 0}};
    code = bobj_poly_tag(o, tpage);
    nx = -nx, nz = -nz, fx = -fx, fz = -fz;
    draw_textured_patch(uv, far_strip, clut, static_cast<u16>(tpage | 0x20), 0, code, 8);
  }
}

void black_saber(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;
  sc->eff_power = c->st()->intel[2] / 2 + sc->power / 4 + 500;
  if (1 < sc->step) draw_track(sc);
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
        Shot* s = shot_spawn_callback_always(swirling_shard);
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
      // 120 ticks of streaks homing in on the caster; a ring when they end.
      sc->s20 += g.frame_ticks;
      if (0x78 < sc->s20) {
        ++sc->step;
        const s32 at[4] = {c->pos[0], c->pos[1] + c->size[1] * -0x4000, c->pos[2], 0};
        particle_spawn_ring(at, 0x10, 0x40, 0x40, 0x1E, 0x402080, 0x8040FF, 0, 0);
      }
      if (0x1E < sc->s20 && sc->s24 == 0) {
        sc->s24 = 1;
        const s32 yaw = ratan2(c->pos[2] - t->pos[2], c->pos[0] - t->pos[0]);
        vec_distance_fx(c->pos, t->pos);
        if (spellcast_leads_camera(sc)) camera_move_to(g.camera, c->pos, yaw, 0x155, 0x800, 0x1E, 0x80, 5);
      }
      const s32 to[4] = {c->pos[0], c->pos[1] + c->size[1] * -0x4000, c->pos[2], 0};
      const Vec4 from = vec_random_around(to, 800);
      particle_spawn_homing_line(from.data(), to, 0x406060, 0xC0FFFF, 0x60, 0x6000, 0x1000);
      return;
    }
    case 2: {
      Shot* s = shot_spawn_callback_always(blade);
      if (s != nullptr) {
        sc->s3c = 1;
        g_blade[sc] = s;
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
        s->vel[0] = d.x * 0x28, s->vel[1] = d.y * 0x28, s->vel[2] = d.z * 0x28, s->extra34 = d.w;
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
      // The camera follows the blade until it starts fading.
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

void spell_mod_black_saber(SpellCast* sc) { black_saber(sc); }

}  // namespace ss::game::battle
