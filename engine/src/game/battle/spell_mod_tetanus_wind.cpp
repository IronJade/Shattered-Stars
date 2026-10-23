// Spell code module 03874d77f8 (entries 0x901 / 0x902): Tetanus Wind (spell 0xc): gusts (the
// data's sprite) appear round the target one a frame (power / 20 + 16, at most 48) and
// swirl into it, each bursting on arrival; then the target is hit (element 0x100).
// Functions 0x8008cc00 (the handler) and 0x8008d15c (a gust, a kind-0x21 callback).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the gusts spawned (step 2), s24 the camera's side (240 or
// 300 degrees), s28 a level rising in steps 2-3 and falling after (nothing reads it); obj
// the data's sprite set (anim 0 the gusts, 2 the bursts).

// A gust's afterimage (Shot +0x130 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

// One velocity axis steering toward the point: 0x1000 a tick, at most 0xc000.
void steer(s32& vel, s32 pos, s32 point, s32 ticks) {
  if (point < pos) {
    vel += ticks * -0x1000;
    if (vel < -0xC000) vel = -0xC000;
  } else {
    vel += ticks * 0x1000;
    if (0xC000 < vel) vel = 0xC000;
  }
}

// 0x8008d15c: a gust (anim 0, animated even in pauses) rolled along its velocity with an
// afterimage, steering at +0x118; within 2 units a tick of it (or after 900 ticks): +0x10c
// 3 turns back at its owner's centre; else (0: a shake, sfx 0x4b, a burst of anim 2) the
// afterimage ends and it goes.
void gust(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 0;
  s->roll = static_cast<s16>(ratan2(s->vel[1], -s->vel[0]));
  bobj_project(s);
  bobj_anim_draw(s, 0);
  if ((g.flags & 0xC) != 0) return;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr) trail = afterimage_create(s, 6, 0x2060, 0);
  steer(s->vel[0], s->pos[0], s->point[0], g.frame_ticks);
  steer(s->vel[1], s->pos[1], s->point[1], g.frame_ticks);
  steer(s->vel[2], s->pos[2], s->point[2], g.frame_ticks);
  bobj_move(s, s->vel);
  s->p134 += g.frame_ticks;
  if (!(vec_distance_fx(s->pos, s->point) < g.frame_ticks << 17) && s->p134 < 900) return;
  if (s->aux == 3) {
    const Unit* o = s->owner;
    s->p12c = 0;
    s->aux = 0;
    s->point[0] = o->pos[0];
    s->point[1] = o->pos[1] + o->size[1] * -0x2000;
    s->point[2] = o->pos[2];
    return;
  }
  if (s->aux == 0) {
    screen_shake(3, 4, 4, 4, 0xC);
    bsfx_play_at(0x4B, s);
    if (Sprite* sp = shot_spawn_sprite(s, 2)) {
      const s32 scale = rng_between(0x4000, 24000);
      sp->scale_y = scale;
      sp->scale_x = scale;
      sprite_set_pos_v(sp, s->pos);
      sprite_set_fade(sp, 0x80, -4, 8);
      sp->layer = 5;
    }
  }
  if (trail != nullptr) trail->ending = 1;
  g_trail.erase(s);
  shot_kill(s);
}

void tetanus_wind(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;
  s32 at[4] = {sc->centre[0], sc->centre[1], sc->centre[2], sc->centre[3]};
  if (sc->combo == 3) {
    at[0] = c->pos[0];
    at[1] = c->pos[1] + c->size[1] * -0x2000;
    at[2] = c->pos[2];
  }
  s32 i4 = c->st()->intel[2];
  if (i4 < 0) i4 += 3;
  s32 p4 = sc->power;
  if (p4 < 0) p4 += 3;
  sc->eff_power = (i4 >> 2) + (p4 >> 2) + 0x118;
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
    case 0:
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x60, 0x60, 0x60);
      sc->s24 = rng_range(2) != 0 ? 300 : 0xF0;
      if (spellcast_leads_camera(sc)) {
        camera_reset(g.camera);
        camera_move_to(g.camera, t->pos, (sc->s24 * 0x1000) / 0x168, 0x111, 0x1000, 0x1E, 0x80, 5);
      }
      spellcast_wait(sc, 0x1E);
      return;
    case 1:
      if (sc->data != nullptr) return;
      ++sc->step;
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = 0x10E < sc->s24 ? 0xAAA : 0xD55;
        camera_move_to(g.camera, at, yaw, 0x71, 0x800, 300, 0x80, 5);
      }
      [[fallthrough]];
    case 2: {
      // A gust a frame round the centre, aimed at the target (or the combo point).
      s32 n = sc->power / 0x14 + 0x10;
      if (0x30 < n) n = 0x30;
      ++sc->s20;
      if (n < sc->s20) ++sc->step;
      Shot* s = shot_spawn_callback_always(gust);
      if (s == nullptr) return;
      g_trail.erase(s);
      s32 point[4];
      const int r = spellcast_combo_point(sc, point);
      if (r == 0) {
        point[0] = t->pos[0];
        point[1] = t->pos[1] + t->size[1] * -0x2000;
        point[2] = t->pos[2];
      }
      s->point[0] = point[0], s->point[1] = point[1], s->point[2] = point[2];
      s->aux = static_cast<s16>(r);
      const Vec4 p = vec_random_around(sc->centre, 0x500);
      s->pos[0] = p.x, s->pos[1] = p.y, s->pos[2] = p.z, s->pos_w = p.w;
      const Vec4 v = vec_random_around(kZeroVector, 8);
      s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
      s->owner = c;
      s->target = sc->combo == 3 ? c : t;
      if (static_cast<u32>(rng_next() & 0x7FFF) < 0x2000) bsfx_play_at(0x34, s);
      bobj_share_sprite(&sc->obj, s);
      return;
    }
    case 3:
      if (spellcast_effects_gone(sc) != 0) ++sc->step;
      return;
    case 4:
      spellcast_dim_end(sc);
      spell_hit_target(sc, 0x100, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_tetanus_wind(SpellCast* sc) { tetanus_wind(sc); }

}  // namespace ss::game::battle
