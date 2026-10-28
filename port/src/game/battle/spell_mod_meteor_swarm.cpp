// Spell code module ac59fc1380 (entries 0x967-0x96a): Meteor Swarm (spell 0x13): the view
// leaves for space (the stage hidden): a planet (the data's model 0), a sun and a lens flare,
// and a swarm of meteors falling on the planet with the camera riding the first; then back
// on the field the meteors rain on random points of the arena, each a crater of sparks, beams
// and a shake, before the hit on the whole other side. Functions 0x8008cc00 (the handler),
// 0x8008d4bc (a meteor in space, a kind-0x21 callback) and 0x8008d844 (a meteor on the field,
// kind 0x21); the jump table of the handler's steps at 0x8008de74; the count of meteors still
// falling in space at 0x8008de98.

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/fx_draw.h"
#include "game/battle/killer.h"
#include "game/battle/particle.h"
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

// The SpellCast's fields: s28 the white-out begun (step 4), s38 / s3c the two rushing
// sounds' handles; obj the data's sprite set (anim 7 the sun, 6 a far star; the meteors
// share it).

// 0x8008de98: the meteors still falling in space. A module variable: each slot's copy of the
// module (slot 1's is linked for 0x8008e600) has its own, and so its own callbacks; the
// callbacks here are per slot for that reason.
s32 g_in_space[2] = {};

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

// A field meteor's afterimage (Shot +0x134 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

// 0x8008d4bc: a meteor in space (anim 0 the lead, 1-4 the others) flying at the planet
// (+0x118) and dying after +0x134 ticks. The lead moves the camera: (+0x128 0) the look-at
// follows it for 400 ticks, (1) the camera sits behind it for 220, (2) two random swings
// round the planet. Near the planet (0x200 units) a ring once (+0x10c) and the count drops;
// then it fades (+0x130).
template <int kSlot>
void space_meteor(Shot* s) {
  BattleGlobals& g = bg();
  bobj_project(s);
  bobj_anim_draw(s, 0);
  if ((g.flags & 0xC) != 0) return;
  if (s->anim == 0) {
    s->p12c += g.frame_ticks;
    bool next = false;
    if (s->p128 == 1) {
      camera_move_to(g.camera, s->pos, 0xF1C, -0x238, 0x1800, 0, 0x80, 5);
      if (0xDC < s->p12c) {
        s->p12c = 0;
        next = true;
      }
    } else if (s->p128 == 0) {
      ViewState& v = view();
      v.cam_lookat[0] = s->pos[0];
      v.cam_lookat[1] = s->pos[1];
      v.cam_lookat[2] = s->pos[2];
      s->scale_y = 0x2000;
      s->scale_x = 0x2000;
      if (0x190 < s->p12c) {
        s->p12c = 0;
        next = true;
      }
    } else if (s->p128 == 2) {
      s->scale_y = 0x800;
      s->scale_x = 0x800;
      s32 at[3];
      at[0] = rng_range(2) != 0 ? 0x3E8000 : static_cast<s32>(0xFFC18000);
      at[1] = 0x1388000;
      at[2] = 0;
      s32 yaw = rng_range(2) != 0 ? 0xCE3 : 0x955;
      s32 pitch = rng_range(2) != 0 ? 0x2AA : 0x1C7;
      camera_move_to(g.camera, at, yaw, pitch, 12000, 0, 0x80, 5);
      at[0] = rng_range(2) != 0 ? 0xC8000 : static_cast<s32>(0xFFF38000);
      at[1] = 0x1388000;
      at[2] = 0;
      yaw = rng_range(2) != 0 ? 0xCE3 : 0x955;
      pitch = rng_range(2) != 0 ? 0x2AA : 0x1C7;
      camera_move_to(g.camera, at, yaw, pitch, 10000, 300, 0x80, 5);
      next = true;
    }
    if (next) ++s->p128;
  }
  bobj_move(s, s->vel);
  if (vec_distance_fx(s->pos, s->point) < 0x200000) {
    if (s->aux == 0) {
      s->aux = 1;
      const s32 at[4] = {s->pos[0], s->pos[1], s->pos[2], s->pos_w};
      particle_spawn_ring(at, 0x16, 0x20, 0x18, 0x3C, 0x201010, 0xC0A0A0, 0x78, 0);
      --g_in_space[kSlot];
    }
    s->colour_mode = 2;
    s->rgb[0] = static_cast<u8>(-0x80 - s->p130);
    s->p130 += g.frame_ticks * 2;
  }
  s->p134 -= g.frame_ticks;
  if (s->p134 < 1) shot_kill(s);
}

// 0x8008d844: a meteor on the field (anim 0 the lead, 1-4 the others), double size, a fiery
// vortex trailing behind it (the lead's bigger and blue) and a 5-copy afterimage, flying at
// +0x118; the lead's look-at follows it for 400 ticks (+0x128 the ticks). Below the ground:
// a shake (the lead's twice as strong), 3 debris sprites, sfx 0x50 / 0x22, five spark rings,
// 8 beams, and it goes.
template <int kSlot>
void field_meteor(Shot* s) {
  BattleGlobals& g = bg();
  s->scale_y = 0x2000;
  s->scale_x = 0x2000;
  bobj_project(s);
  bobj_anim_draw(s, 0);
  VortexParams p;
  s32 dir[3];
  p.radius = 0x4000;
  if (s->anim == 0) {
    p.base = -0x200;
    p.width = 0x600;
    p.step = 0xC0;
    p.rings = 8;
    p.segments = 8;
    p.colour_a = 0x4080FF;
    p.colour_b = 0x1020;
  } else {
    p.base = -0x100;
    p.width = 0x400;
    p.step = 0x80;
    p.rings = 6;
    p.segments = 6;
    p.colour_a = 0xFF8020;
    p.colour_b = 0x402010;
  }
  p.phase_span = 2;
  p.phase = 0x1FFF - ((s->p128 << 7) & 0x1FFF);
  p.twist_freq = 1;
  p.twist_amp = 0x1000;
  p.blend = 2;
  p.ot_bias = 5;
  // (x and y of the velocity are negated, z is not.)
  dir[0] = -s->vel[0];
  dir[1] = -s->vel[1];
  dir[2] = s->vel[2];
  s32 angles[3];
  vec_to_angles(angles, dir);
  obj_set_matrix(s->pos, angles, kUnitScale);
  draw_vortex(p);
  if ((g.flags & 0xC) != 0) return;
  s->p128 += g.frame_ticks;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr) trail = afterimage_create(s, 5, 0x4060, 2);
  if (trail != nullptr) trail->next_kind = 1;
  bobj_move(s, s->vel);
  if (bobj_ground_height(s) < s->pos[1]) {
    s->pos[1] = static_cast<s32>(0xFFF38000);
    screen_shake(s->anim == 0 ? 0x10 : 8, 6, 4, 6, 0x14);
    for (int i = 0; i < 3; ++i) {
      Sprite* sp = shot_spawn_sprite(s, 6);
      if (sp == nullptr) continue;
      const s32 r = static_cast<s32>(rng_range(0x100));
      const Vec4 at = vec_random_around(s->pos, r);
      sprite_set_pos_v(sp, at.data());
      const s32 scale = rng_between(0x2000, 20000);
      sp->scale_y = scale;
      sp->scale_x = scale;
      sp->size[2] = static_cast<s16>(rng_range(0xC));
      sprite_set_fade(sp, 0x80, -2, 0x14);
      sp->layer = 6;
    }
    bsfx_play_at_pos(0x50, s->pos);
    bsfx_play_at_pos(0x22, s->pos);
    s32 at[3] = {s->pos[0], 0, s->pos[2]};
    at[1] = bobj_ground_height(s);
    if (Shot* r = effect_spawn_sparks_at(at, 0x10, 0xF0, 0x140, 0xB, 0xA0, 0, 2, 0x1E0, 0)) {
      r->anim = 8;
      bobj_share_sprite(s, r);
    }
    effect_spawn_sparks_at(at, 0x14, 0x168, 0xC0, 0x10, 0xF0, 5, 1, 0x230, 0);
    effect_spawn_sparks_at(at, 0x14, 0x1E0, 0x80, 0x30, 0xF0, 0, 0, 0x280, 0);
    at[1] += -0xB4000;
    effect_spawn_sparks_at(at, 0x14, 0x1E0, 0, 0x5A, 0xDC, 4, 3, 0x140, 0);
    if (Shot* r = effect_spawn_sparks_at(at, 0x14, 0x1E0, 0, 0x5A, 0xDC, 0xB, 3, 0x140, 0)) r->vel[2] = 0x400;
    for (int i = 0; i < 8; ++i) {
      const Vec4 end = vec_random_around(s->pos, 0x960);
      effect_spawn_beam_at(s->pos, end.data(), 0xB, 0x280, 0x30, 0xC000, 0x800, 0);
    }
    if (trail != nullptr) trail->ending = 1;
    g_trail.erase(s);
    shot_kill(s);
  } else if (s->anim == 0 && s->p128 < 400) {
    ViewState& v = view();
    v.cam_lookat[0] = s->pos[0];
    v.cam_lookat[1] = s->pos[1];
    v.cam_lookat[2] = s->pos[2];
  }
}

// Step 4: the planet (model 0, 3.5 times, stage grey 0xa0), the sun (anim 7, double size)
// above it, a far star (anim 6, 4 times) and the lens flare from it.
void draw_space(SpellCast* sc) {
  stage_colour_set_grey(0xA0);
  const s32 scale[3] = {0x3800, 0x3800, 0x3800};
  s32 orient[3] = {0, 0, 0};
  lights_build_oriented(kZeroVector);
  obj_set_matrix(kZeroVector, orient, scale);
  obj_set_matrix(kZeroVector, orient, scale);
  BObj* o = &sc->obj;
  const u8* planet = spellcast_model(sc, 0);
  if (planet != nullptr && o->animset != nullptr) model_draw(planet, o->animset->clut[1], o->animset->tpage[1], 4);
  stage_colour_restore();
  o->pos[1] = 0x1388000;
  o->anim = 7;
  o->pos[0] = 0;
  o->pos[2] = 0;
  o->scale_y = 0x2000;
  o->scale_x = 0x2000;
  bobj_project(o);
  bobj_anim_draw(o, 0);
  o->pos[0] = static_cast<s32>(0xFD8F0000);
  o->anim = 6;
  o->pos[1] = static_cast<s32>(0xFFC18000);
  o->pos[2] = 0;
  o->scale_y = 0x4000;
  o->scale_x = 0x4000;
  bobj_project(o);
  bobj_anim_draw(o, 0);
  stage_colour_set_grey(0x100);
  lens_flare_draw(o->pos, 0x800, 0x708088, 0x20, 0x2D);
  stage_colour_restore();
}

template <int kSlot>
void meteor_swarm(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;
  sc->eff_power = sc->caster->st()->intel[2] * 4 + sc->power * 4 + 0xDAC;
  if (sc->step == 4) draw_space(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  s32 n = sc->power / 0x14 + 0x10;
  if (0x20 < n) n = 0x20;
  switch (sc->step) {
    case 0: {
      // Dim, the camera pulls far back over the field; the rush sound fades in 80 ticks.
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      if (spellcast_leads_camera(sc)) {
        camera_reset(g.camera);
        camera_move_to(g.camera, kZeroVector, 0x800, 0x400, 0x4000, 0x78, 0x80, 5);
      }
      sound_handle_fade_out(bsfx_play_ui(0x2E), 0x3C, 0x50);
      spellcast_wait(sc, 0x32);
      return;
    }
    case 1:
      // A fade to black.
      bsfx_play_ui(0x21);
      bsfx_play_ui(0x22);
      screen_fade_create(0, 0x20, -4, 0x14);  // (0x8001fd94 allocates its 0x1c bytes)
      ++sc->step;
      return;
    case 2:
      if (sc->data != nullptr) return;
      spellcast_wait(sc, 0x3C);
      g_in_space[kSlot] = 0;
      return;
    case 3: {
      // Space: the stage hidden, n meteors far above a random point, falling on the planet.
      battle_flash_set(sc, 0, 0, 0);
      const s32 x = rng_between_fixed(-20000, 20000);
      const s32 z = rng_between_fixed(-15000, 15000);
      const s32 eye[3] = {x, static_cast<s32>(0xF92A0000), z};
      camera_move_to(g.camera, eye, 0xB8E, -0x38E, 0x1400, 0, 0x80, 5);
      for (int i = 0; i < n; ++i) {
        Shot* s = shot_spawn_callback_always(space_meteor<kSlot>);
        if (s == nullptr) continue;
        if (i == 0) {
          sc->s3c = static_cast<s32>(bsfx_play_at(0x2D, s));
          sc->s38 = static_cast<s32>(bsfx_play_at(0x44, s));
        }
        ++g_in_space[kSlot];
        s->command = 0xA5;
        s->pos[0] = x + rng_between_fixed(-0x400, 0x400);
        s->pos[1] = rng_between_fixed(-30000, -26000);
        s->pos[2] = z + rng_between_fixed(-0x400, 0x400);
        s->anim = i == 0 ? 0 : static_cast<u16>(rng_between(1, 5));
        s->point[0] = 0;
        s->point[1] = 0x1388000;
        s->point[2] = 0;
        s->p134 = (vec_distance_fx(s->pos, s->point) / 0x28) >> 12;
        const Vec4 v = vec_velocity_toward(s->pos, s->point, 0x28);
        s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
        bobj_share_sprite(&sc->obj, s);
      }
      g.stage_hidden = 1;
      g.effects_suppressed = 1;
      ++sc->step;
      return;
    }
    case 4:
      if (g_in_space[kSlot] < 2 && sc->s28 == 0) {
        // The last meteors reach the planet: a white-out, the rush stops.
        screen_fade_create(0xFFFFFF, 0x20, 4, 0x24);
        sound_handle_fade_out(static_cast<u32>(sc->s3c), 5, 0);
        sound_handle_fade_out(static_cast<u32>(sc->s38), 5, 0);
        sc->s28 = 1;
        bsfx_play_ui(0x28);
        bsfx_play_ui(0x29);
        battle_flash_set(sc, 0, 0, 0);
      } else if (g_in_space[kSlot] == 0) {
        // Back on the field: n meteors on random points of the arena.
        g.stage_hidden = 0;
        g.effects_suppressed = 0;
        spellcast_wait(sc, 0x1A4);
        const s32 eye[3] = {0, static_cast<s32>(0xFD120000), 0};
        camera_move_to(g.camera, eye, 0xC00, -0x38E, 0x800, 0, 0x80, 5);
        for (int i = 0; i < n; ++i) {
          Shot* s = shot_spawn_callback_always(field_meteor<kSlot>);
          if (s == nullptr) continue;
          g_trail.erase(s);
          s32 p[4];
          stage_random_point(p, 1);
          s->pos[0] = p[0], s->pos[1] = p[1], s->pos[2] = p[2], s->pos_w = p[3];
          s->pos[1] = rng_between_fixed(-23500, -15000);
          if (i == 0) s->pos[1] = static_cast<s32>(0xFA240000);
          stage_random_point(p, 1);
          s->point[0] = p[0], s->point[1] = p[1], s->point[2] = p[2];
          s->anim = i == 0 ? 0 : static_cast<u16>(rng_between(1, 5));
          const Vec4 v = vec_velocity_toward(s->pos, s->point, 0x28);
          s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
          bobj_share_sprite(&sc->obj, s);
          if (i == 0) {
            sc->s3c = static_cast<s32>(bsfx_play_at(0x2D, s));
            sc->s38 = static_cast<s32>(bsfx_play_at(0x44, s));
          }
        }
      }
      return;
    case 5: {
      // Two random camera swings over the field, wait 180.
      const s32 yaw = rng_range(2) != 0 ? 0x955 : 0xF1C;
      camera_move_to(g.camera, kZeroVector, yaw, 0x1C7, 0x2000, 0, 0x80, 5);
      const u32 side = rng_range(2);
      const s32 at[3] = {side != 0 ? static_cast<s32>(0xFFB50000) : 0x4B0000, 0, static_cast<s32>(0xFFC18000)};
      const s32 pitch = rng_range(2) != 0 ? 0x22 : 0x155;
      camera_move_to(g.camera, at, side != 0 ? 0xA38 : 0xE38, pitch, 0x1000, 0xF0, 0x80, 5);
      spellcast_wait(sc, 0xB4);
      return;
    }
    case 6:
      battle_flash_set(sc, 0x60, 0x20, 0x20);
      ++sc->step;
      return;
    case 7:
      if (shot_kind21_absent(field_meteor<kSlot>) != 0 && spellcast_effects_gone(sc) != 0) spellcast_wait(sc, 0x3C);
      return;
    case 8:
      sound_handle_fade_out(static_cast<u32>(sc->s3c), 0x3C, 0);
      sound_handle_fade_out(static_cast<u32>(sc->s38), 0x3C, 0);
      spell_hit_all(sc, 0x20, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_meteor_swarm(SpellCast* sc) {
  if (sc->slot == 0) {
    meteor_swarm<0>(sc);
  } else {
    meteor_swarm<1>(sc);
  }
}

}  // namespace ss::game::battle
