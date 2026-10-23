// Spell code module 00b1337668 (entries 0x930-0x932): Energy Arrow (spell 0x2c): arrows of
// light appear round the target one a frame (power / 20 + 16, at most 48) and home in on it,
// each bursting on arrival; then the target is hit (element 0x40). Functions 0x8008cc00 (the
// handler) and 0x8008d074 (an arrow, a kind-0x21 callback).

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

// The SpellCast's fields: s20 the arrows spawned (step 2); obj the data's sprite set (anims
// 0-2 the arrows, 4-9 the sparks).

// An arrow's afterimage (Shot +0x130 holds the pointer on the console).
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

// 0x8008d074: an arrow rolled along its velocity with an afterimage, steering at +0x118;
// within 3 units a tick of it (or after 900 ticks): +0x10c 3 turns back at its owner's
// centre; else (0: the full impact on +0x114: a shake, sfx 0x4b, a spark ring, 8 beams and
// sparks; other kinds nothing) the afterimage ends and it goes.
void arrow(Shot* s) {
  BattleGlobals& g = bg();
  s->roll = static_cast<s16>(ratan2(s->vel[1], -s->vel[0]));
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr) trail = afterimage_create(s, 6, 0x500070, 0);
  steer(s->vel[0], s->pos[0], s->point[0], g.frame_ticks);
  steer(s->vel[1], s->pos[1], s->point[1], g.frame_ticks);
  steer(s->vel[2], s->pos[2], s->point[2], g.frame_ticks);
  bobj_move(s, s->vel);
  s->p134 += g.frame_ticks;
  if (!(vec_distance_fx(s->pos, s->point) < g.frame_ticks * 0x30000) && s->p134 < 900) return;
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
    screen_shake2(3, 4, 4, 4, 0xC);
    bsfx_play_at(0x4B, s);
    const Unit* t = s->target;
    const s32 ground[3] = {t->pos[0], bobj_ground_height(t), t->pos[2]};
    effect_spawn_sparks_at(ground, 0xE, 0xA0, 0x32, 0x28, 0x3C, 7, 0, 0x40, 0);
    for (int i = 0; i < 8; ++i) {
      const s32 mid[3] = {s->target->pos[0], s->target->pos[1] + s->target->size[1] * -0x2000, s->target->pos[2]};
      const Vec4 end = vec_random_around(mid, 0x578);
      const s32 from[3] = {s->target->pos[0], s->target->pos[1] + s->target->size[1] * -0x2000, s->target->pos[2]};
      effect_spawn_beam_at(from, end.data(), 7, 0xA0, 0x20, 0x18000, 0x1000, 0);
    }
    for (int k = 0; k < 2; ++k) {
      // 4 sparks of anim 4, then 8 of anims 5-9 shrinking.
      for (int i = 0; i < (k == 0 ? 4 : 8); ++i) {
        const int anim = k == 0 ? 4 : static_cast<int>(rng_range(5)) + 5;
        Sprite* sp = shot_spawn_sprite(s, anim);
        if (sp == nullptr) continue;
        const s32 x = s->target->pos[0] + rng_between_fixed(-0x100, 0x100);
        const s32 y = s->target->pos[1] + rng_between_fixed(-0x180, 0x80);
        const s32 dz = rng_between_fixed(-0x100, 0x100);
        sprite_set_pos(sp, x, y, s->target->pos[2] + dz);
        sprite_set_fade(sp, 0x80, -4, 8);
        sp->size[2] = static_cast<s16>(rng_range(8));
        const s32 scale = rng_between(0x1000, 0x2000);
        sp->scale_y = scale;
        sp->scale_x = scale;
        if (k == 1) {
          sp->scale_vel[1] = -0x80;
          sp->scale_vel[0] = -0x80;
        }
        sp->layer = 3;
      }
    }
  }
  if (trail != nullptr) trail->ending = 1;
  g_trail.erase(s);
  shot_kill(s);
}

void energy_arrow(SpellCast* sc) {
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
  sc->eff_power = c->st()->intel[2] / 2 + sc->power / 4 + 0x1B8;
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, t->pos, 0xC00, 0xAA, 0x1000, 0xF, 0x80, 5);
      spellcast_wait(sc, 0x1E);
      return;
    case 1:
      if (sc->data != nullptr) return;
      ++sc->step;
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0xAAA : 0xD55;
        camera_move_to(g.camera, at, yaw, 0x71, 0x800, 0xF0, 0x80, 5);
      }
      [[fallthrough]];
    case 2: {
      // An arrow a frame round the centre, aimed at the target (or the combo point).
      s32 n = sc->power / 0x14 + 0x10;
      if (0x30 < n) n = 0x30;
      ++sc->s20;
      if (n < sc->s20) ++sc->step;
      Shot* s = shot_spawn_callback_always(arrow);
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
      s->owner = c;
      s->target = sc->combo == 3 ? c : t;
      s->anim = static_cast<u16>(rng_range(3));
      if (static_cast<u32>(rng_next() & 0x7FFF) < 0x2000) bsfx_play_at(0x23, s);
      bobj_share_sprite(&sc->obj, s);
      return;
    }
    case 3:
      if (spellcast_effects_gone(sc) != 0) ++sc->step;
      return;
    case 4:
      spellcast_dim_end(sc);
      spell_hit_target(sc, 0x40, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_energy_arrow(SpellCast* sc) { energy_arrow(sc); }

}  // namespace ss::game::battle
