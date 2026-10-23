// Spell code module 440856a00a (entries 0x8f8 / 0x8f9): Light Cross (spell 0x1b): a glow on
// the caster and three title sprites, a fade, two wings of light over the screen, then for 280
// ticks spinning crosses of light (the data's model 0) drop on random points of the arena (or
// the combo point), before the hit on the whole other side (element 0x80). Functions
// 0x8008cc00 (the handler) and 0x8008d17c (a falling cross, a kind-0x21 callback); the jump
// table of the handler's steps at 0x8008d46c.

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the rain's ticks (step 4), s24 the cross timer; centre 0x300
// above the target; obj the data's sprite set (anims 1 the glow, 2 / 3 the wings).

// A cross's model (Shot +0x128 holds the pointer on the console).
std::unordered_map<const Shot*, const u8*> g_model;

// 0x8008d17c: a cross (its model, 4 times, lit, spinning +0x130 by 100 a tick, the fx bank's
// CLUT, tpage 0x1a) flying at 0x30 a tick to +0x118 (+0x10c the combo kind: 3 first takes a
// random arena point, 2 just goes); at the point: (kind 0) two spark rings, debris twice,
// sfx 0x50, a shake, and it goes.
void falling_cross(Shot* s) {
  BattleGlobals& g = bg();
  const s32 light_orient[3] = {0, s->p130, 0};
  lights_build_oriented(light_orient);
  s32 orient[3] = {0, s->p130, 0};
  const s32 scale[3] = {0x4000, 0x4000, 0x4000};
  obj_set_matrix(s->pos, orient, scale);
  const auto it = g_model.find(s);
  const u8* model = it != g_model.end() ? it->second : nullptr;
  const BObj* fx = g.sprite_bank_fx.get();
  if (model != nullptr && fx != nullptr && fx->animset != nullptr) model_draw(model, fx->animset->clut[0], 0x1A, 3);
  if ((g.flags & 0xC) != 0) return;
  const Vec4 d = vec_direction(s->pos, s->point);
  s->vel[0] = d.x, s->vel[1] = d.y, s->vel[2] = d.z, s->extra34 = d.w;
  const s32 dist = vec_distance_fx(s->pos, s->point);
  s->vel[0] *= 0x30;
  s->vel[1] *= 0x30;
  s->vel[2] *= 0x30;
  bobj_move(s, s->vel);
  const s32 near = g.frame_ticks * 0x30000;
  s->p130 = (s->p130 + g.frame_ticks * 100) & 0xFFF;
  if (!(dist < near)) return;
  if (s->aux == 3) {
    s->p12c = 0;
    s->aux = 0;
    s32 p[4];
    stage_random_point(p, 1);
    s->point[0] = p[0], s->point[1] = p[1], s->point[2] = p[2];
    return;
  }
  if (s->aux != 2) {
    s32 at[3] = {s->pos[0], 0, s->pos[2]};
    at[1] = bobj_ground_height(s) + -0x10000;
    if (s->aux == 0) {
      effect_spawn_sparks_at(at, 0xC, 0x80, 0x180, 0x20, 0x5A, 1, 1, 8, 0);
      effect_spawn_sparks_at(at, 0x10, 0x1E0, 0x30, 0x30, 0x50, 0xB, 1, 0x10, 0);
    }
    for (int i = 0; i < 2; ++i) effect_debris_burst(at, kZeroVector, 2, 2, 1, 24000, 4);
    bsfx_play_at_pos(0x50, s->pos);
    screen_shake2(4, 2, 2, 2, 6);
  }
  g_model.erase(s);
  shot_kill(s);
}

void light_cross(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + -0x300000;
  sc->centre[2] = t->pos[2];
  sc->eff_power = c->st()->intel[2] / 4 + sc->power / 4 + 0xFA;
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) camera_request_mode(g.camera, 7, c);
      spellcast_wait(sc, 0x1E);
      return;
    case 1: {
      // A glow (anim 1) on the caster's head, three fx-bank sprites (0x25-0x27) at the
      // screen's left, sfx 0x28 / 0x29, the camera on the caster.
      if (Sprite* sp = sprite_spawn_from(1, 0, &sc->obj, nullptr)) {
        const s32 at[3] = {c->pos[0], c->pos[1] + c->size[1] * -0x4000, c->pos[2]};
        sprite_set_pos_v(sp, at);
        sprite_set_fade(sp, 0x80, -2, 0x28);
        sp->roll_speed = 0x16;
      }
      s32 y = 0x20;
      for (int i = 0; i < 3; ++i) {
        if (Sprite* sp = sprite_spawn_from(i + 0x25, 0, g.sprite_bank_fx.get(), nullptr)) {
          sprite_set_fade(sp, 0x80, -2, 0x1E);
          sprite_set_screen_pos(sp, 0x10, static_cast<s16>(y));
          sp->vel[0] = i;
          sp->depth = 10;
        }
        y += 0x10;
      }
      bsfx_play_ui(0x28);
      bsfx_play_ui(0x29);
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        const s32 yaw = rng_range(2) != 0 ? 0xCE3 : 0xB1C;
        camera_move_to(g.camera, c->pos, yaw, 0x1C7, 0x1000, 0x3C, 0x80, 5);
      }
      spellcast_wait(sc, 0x1E);
      return;
    }
    case 2:
      screen_fade_create(0, 0x20, -8, 0x14);  // (0x8001fd94 allocates its 0x1c bytes)
      spellcast_wait(sc, 0x1E);
      return;
    case 3:
      // An overview of the other side; the wings (anims 2 / 3) at the screen's edges.
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0xD55 : 0xAAA;
        camera_move_to(g.camera, kZeroVector, yaw, 0x18E, 5000, 0, 0x80, 5);
        camera_setup_overview(g.camera, 0x168, c->is_enemy ^ 1);
      }
      if (Sprite* sp = sprite_spawn_from(2, 0, &sc->obj, nullptr)) {
        sprite_set_screen_pos(sp, 0, 0x78);
        sprite_set_fade(sp, 0x80, -2, 0xF0);
        sp->depth = 9;
      }
      if (Sprite* sp = sprite_spawn_from(3, 0, &sc->obj, nullptr)) {
        sprite_set_screen_pos(sp, 0x140, 0x78);
        sprite_set_fade(sp, 0x80, -2, 0xF0);
        sp->depth = 9;
      }
      ++sc->step;
      return;
    case 4: {
      // For 280 ticks a cross whenever the timer passes 0x8000 (power * 2 + 10000 a frame),
      // from 0x2bc above a random arena point to another (or the combo point).
      sc->s20 += g.frame_ticks;
      if (0x118 < sc->s20) ++sc->step;
      sc->s24 += sc->power * 2 + 10000;
      if (!(0x7FFF < sc->s24)) return;
      sc->s24 += -0x8000;
      Shot* s = shot_spawn_callback_always(falling_cross);
      if (s == nullptr) return;
      s32 point[4];
      const int r = spellcast_combo_point(sc, point);
      if (r == 0) stage_random_point(point, 1);
      s->point[0] = point[0], s->point[1] = point[1], s->point[2] = point[2];
      g_model[s] = spellcast_model(sc, 0);
      s32 from[4];
      stage_random_point(from, 1);
      s->pos[0] = from[0], s->pos[1] = from[1], s->pos[2] = from[2], s->pos_w = from[3];
      s->pos[1] = static_cast<s32>(0xFFD44000);
      s->aux = static_cast<s16>(r);
      s->power = static_cast<s16>(sc->power);
      return;
    }
    case 5:
      if (shot_kind21_absent(falling_cross) == 0) return;
      ++sc->step;
      return;
    case 6:
      spell_hit_all(sc, 0x80, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_light_cross(SpellCast* sc) { light_cross(sc); }

}  // namespace ss::game::battle
