// Spell code module dfd43383ca (entries 0x96b-0x970): Southern Cross (spell 0x14): the view
// leaves for space (the stage hidden): over a planet (the data's model 0) five stars appear
// one by one in a cross, a second planet (model 1) brightens and shatters, the stars fall on
// it; then back on the field power-scaled stars rain on random points of the arena, each a
// crater of sparks and debris, before the hit on the whole other side. Functions 0x8008cc00
// (the handler), 0x8008d5a8 (a star in space, a kind-0x21 callback) and 0x8008d6f0 (a star
// on the field, kind 0x21); the jump table of the handler's steps at 0x8008dbac; the star
// table 0x8008dbd0 (x) / 0x8008dbdc (y) (5 s16), the field stars' vortex colours 0x8008dbe8 /
// 0x8008dbfc (5 u32 each); the module's variables at 0x8008dc10 (u8 the phase: 0 the stars
// wait, 1 they fall, 2 the space part is over) and 0x8008dc12 (s16 the stars in space).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/fx_draw.h"
#include "game/battle/killer.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shatter.h"
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

// The SpellCast's fields: s20 the star timer (step 4), s24 the next star (0..4), s28 the
// second planet's brightness (step 5), s2c the white-out begun (step 6), s38 / s3c the two
// rushing sounds' handles; obj the data's sprite set (anim 3 the sun, 2 a far star; the
// stars share it).

// 0x8008dc10 / 0x8008dc12: module variables. Each slot's copy of the module (slot 1's is
// linked for 0x8008e600) has its own, and so its own callbacks; the callbacks here are per
// slot for that reason.
u8 g_phase[2] = {};
s16 g_in_space[2] = {};

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

// A field star's afterimage (Shot +0x134 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

// 0x8008d5a8: a star in space (anim 0, 4 times), still until the phase (0x8008dc10) is 1,
// then flying at the planet (+0x118) for +0x134 ticks, fading within 0x200 units of it
// (+0x130); it dies at the end of its time or at phase 2 (the count 0x8008dc12 drops). The
// centre star (+0x12c 4) holds the camera's look-at.
template <int kSlot>
void space_star(Shot* s) {
  BattleGlobals& g = bg();
  s->scale_y = 0x4000;
  s->scale_x = 0x4000;
  s->anim = 0;
  bobj_project(s);
  bobj_anim_draw(s, 0);
  if ((g.flags & 0xC) != 0 || g_phase[kSlot] == 0) return;
  bobj_move(s, s->vel);
  if (vec_distance_fx(s->pos, s->point) < 0x200000) {
    s->colour_mode = 2;
    s->rgb[0] = static_cast<u8>(-0x80 - s->p130);
    s->p130 += g.frame_ticks * 2;
  }
  s->p134 -= g.frame_ticks;
  if (s->p134 < 1 || g_phase[kSlot] == 2) {
    --g_in_space[kSlot];
    shot_kill(s);
  }
  if (s->p12c == 4) {
    ViewState& v = view();
    v.cam_lookat[0] = s->pos[0];
    v.cam_lookat[1] = s->pos[1];
    v.cam_lookat[2] = s->pos[2];
  }
}

// 0x8008d6f0: a star on the field (anim 0 at 12000 / 0x1000), a vortex tail in the colours
// of its index (+0x12c mod 5) and an 8-copy afterimage, flying at +0x118 (+0x128 its ticks).
// Below the ground: a shake, 8 sprites (anim 2), debris, sfx 0x50 / 0x52 / 0x22, two spark
// rings, a sphere and a particle ring, and it goes.
template <int kSlot>
void field_star(Shot* s) {
  BattleGlobals& g = bg();
  s->scale_y = 12000;
  s->scale_x = 12000;
  bobj_project(s);
  bobj_anim_draw(s, 0);
  const SpellCast* sc = spellcast_slot(kSlot);
  const u32 k = static_cast<u32>(s->p12c % 5);
  VortexParams p;
  p.radius = 0x4000;
  p.base = -0x100;
  p.width = 0x400;
  p.step = 0x100;
  p.rings = 6;
  p.segments = 6;
  // (Port guard: the tables are read from the cast's module bytes; the cast outlives the stars.)
  p.colour_a = sc != nullptr ? static_cast<u32>(spell_module_s32(sc, 0x8008DBE8 + k * 4)) : 0;
  p.colour_b = sc != nullptr ? static_cast<u32>(spell_module_s32(sc, 0x8008DBFC + k * 4)) : 0;
  p.phase_span = 4;
  p.twist_freq = 1;
  p.twist_amp = 0x1000;
  p.ot_bias = 5;
  p.blend = 2;
  p.phase = 0x1FFF - ((s->p128 << 7) & 0x1FFF);
  // (x and y of the velocity are negated, z is not.)
  const s32 dir[3] = {-s->vel[0], -s->vel[1], s->vel[2]};
  s32 angles[3];
  vec_to_angles(angles, dir);
  obj_set_matrix(s->pos, angles, kUnitScale);
  draw_vortex(p);
  if ((g.flags & 0xC) != 0) return;
  s->p128 += g.frame_ticks;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr) trail = afterimage_create(s, 8, 0x400000, 2);
  if (trail != nullptr) trail->next_kind = 3;
  bobj_move(s, s->vel);
  if (!(bobj_ground_height(s) < s->pos[1])) return;
  s->pos[1] = static_cast<s32>(0xFFF38000);
  screen_shake2(0xC, 6, 4, 6, 0x14);
  for (int i = 0; i < 8; ++i) {
    Sprite* sp = shot_spawn_sprite(s, 2);
    if (sp == nullptr) continue;
    const s32 r = static_cast<s32>(rng_range(0x180));
    const Vec4 at = vec_random_around(s->pos, r);
    sprite_set_pos_v(sp, at.data());
    const s32 scale = rng_between(20000, 30000);
    sp->scale_y = scale;
    sp->scale_x = scale;
    sp->size[2] = static_cast<s16>(rng_range(0xC));
    sprite_set_fade(sp, 0x80, -2, 0x14);
    sp->layer = 6;
  }
  const s32 spread[3] = {0x280, 0x100, 0x280};
  effect_debris_burst(s->pos, spread, 6, 2, 2, 0x8000, 4);
  bsfx_play_at_pos(0x50, s->pos);
  bsfx_play_at_pos(0x52, s->pos);
  bsfx_play_at_pos(0x22, s->pos);
  s32 at[4] = {s->pos[0], 0, s->pos[2], 0};
  at[1] = bobj_ground_height(s);
  effect_spawn_sparks_at(at, 0x14, 0x230, 0x140, 0x12, 0xA0, 7, 1, 0xF0, 0);
  effect_spawn_sparks_at(at, 0x14, 0x1B8, 0x78, 0x60, 100, 0xD, 1, 0x140, 0);
  effect_spawn_sphere_at_default(at, 7, 8, 0x10, 0xA0, 0xFF2000, 0x1E0, 0);
  at[1] += -0x64000;
  particle_spawn_ring(at, 0x14, 0x100, 0x60, 0x3C, 0x401010, 0xC040A0, 300, 0);
  if (trail != nullptr) trail->ending = 1;
  g_trail.erase(s);
  shot_kill(s);
}

// Steps 4-6: the planet (model 0, 3.5 times, grey 0xa0), the second planet (model 1, double,
// grey s28, with the fx bank's CLUT) until it shatters, the sun (anim 3) and a far star (anim
// 2) with the lens flare.
void draw_space(SpellCast* sc) {
  BattleGlobals& g = bg();
  stage_colour_set_grey(0xA0);
  s32 scale[3] = {0x3800, 0x3800, 0x3800};
  s32 orient[3] = {0, 0, 0};
  lights_build_oriented(kZeroVector);
  obj_set_matrix(kZeroVector, orient, scale);
  obj_set_matrix(kZeroVector, orient, scale);
  BObj* o = &sc->obj;
  if (const u8* m = spellcast_model(sc, 0); m != nullptr && o->animset != nullptr)
    model_draw(m, o->animset->clut[1], o->animset->tpage[1], 4);
  stage_colour_restore();
  if (sc->s28 != 0 && sc->step < 6) {
    stage_colour_set_grey(sc->s28);
    scale[0] = scale[1] = scale[2] = 0x2000;
    lights_build_oriented(kZeroVector);
    obj_set_matrix(kZeroVector, orient, scale);
    obj_set_matrix(kZeroVector, orient, scale);
    const AnimSet* fx = g.sprite_bank_fx != nullptr ? g.sprite_bank_fx->animset : nullptr;
    if (const u8* m = spellcast_model(sc, 1); m != nullptr && fx != nullptr) model_draw(m, fx->clut[0], fx->tpage[0], 4);
    stage_colour_restore();
  }
  o->pos[1] = 0x1388000;
  o->anim = 3;
  o->pos[0] = 0;
  o->pos[2] = 0;
  o->scale_y = 0x2000;
  o->scale_x = 0x2000;
  bobj_project(o);
  bobj_anim_draw(o, 0);
  o->pos[0] = static_cast<s32>(0xFD8F0000);
  o->anim = 2;
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
void southern_cross(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;
  sc->eff_power = sc->caster->st()->intel[2] * 2 + sc->power * 2 + 0x708;
  if (static_cast<u32>(sc->step - 4) < 3) draw_space(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      // Dim, the camera pulls far back; the rush sound fades in 80 ticks.
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, kZeroVector, 0x800, 0x400, 0x4000, 0x78, 0x80, 5);
      }
      g_phase[kSlot] = 0;
      g_in_space[kSlot] = 0;
      sound_handle_fade_out(bsfx_play_ui(0x2E), 0x3C, 0x50);
      spellcast_wait(sc, 0x32);
      return;
    case 1:
      // A fade to black.
      bsfx_play_ui(0x21);
      bsfx_play_ui(0x22);
      screen_fade_create(0, 0x20, -4, 0x14);  // (0x8001fd94 allocates its 0x1c bytes)
      ++sc->step;
      return;
    case 2:
      if (sc->loaded != 0) spellcast_wait(sc, 0x3C);
      return;
    case 3: {
      // Space: the stage hidden, the camera at a random angle over the planet.
      battle_flash_set(sc, 0, 0, 0);
      g.stage_hidden = 1;
      g.effects_suppressed = 1;
      ++sc->step;
      const s32 deg = rng_between(0xB4, 0x168);
      camera_move_to(g.camera, kZeroVector, (deg * 0x1000) / 0x168, 0x1C7, 0x2400, 0, 0x80, 5);
      return;
    }
    case 4:
      // A star of the cross every 30 ticks (table 0x8008dbd0 / 0x8008dbdc), aimed at the planet.
      sc->s20 += g.frame_ticks;
      if (!(0x1E < sc->s20)) return;
      sc->s20 += -0x1E;
      if (Shot* s = shot_spawn_callback_always(space_star<kSlot>)) {
        bsfx_play_ui(0x28);
        bsfx_play_ui(0x29);
        const u32 i = static_cast<u32>(sc->s24);
        s->pos[0] = spell_module_s16(sc, 0x8008DBD0 + i * 2) << 13;
        s->pos[2] = 0;
        s->pos[1] = spell_module_s16(sc, 0x8008DBDC + i * 2) << 13;
        bobj_share_sprite(&sc->obj, s);
        s->point[0] = 0;
        s->point[1] = 0x1388000;
        s->point[2] = 0;
        s->p134 = (vec_distance_fx(s->pos, s->point) / 32) >> 12;
        const Vec4 v = vec_velocity_toward(s->pos, s->point, 0x20);
        s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
        ++g_in_space[kSlot];
        s->p12c = sc->s24;
      }
      ++sc->s24;
      if (4 < sc->s24) {
        ++sc->step;
        bsfx_play_ui(0x26);
        bsfx_play_ui(0x34);
      }
      return;
    case 5:
      // The second planet brightens, then shatters; the stars fall.
      sc->s28 += g.frame_ticks * 4;
      if (!(0xFF < sc->s28)) return;
      sc->s28 = 0x100;
      ++sc->step;
      stage_colour_set_grey(0x100);
      {
        const s32 scale[3] = {0x2000, 0x2000, 0x2000};
        const AnimSet* fx = g.sprite_bank_fx != nullptr ? g.sprite_bank_fx->animset : nullptr;
        model_shatter(spellcast_model(sc, 1), kZeroVector, kZeroVector, scale, fx != nullptr ? fx->clut[0] : 0,
                      fx != nullptr ? fx->tpage[0] : 0, 2, 0, 4, 0, 0x20);
      }
      stage_colour_restore();
      g_phase[kSlot] = 1;
      bsfx_play_ui(0x2C);
      bsfx_play_ui(0x38);
      return;
    case 6:
      if (g_in_space[kSlot] < 2 && sc->s2c == 0) {
        // The last stars reach the planet: a white-out.
        screen_fade_create(0xFFFFFF, 0x20, 4, 0x24);
        sc->s2c = 1;
        bsfx_play_ui(0x28);
        bsfx_play_ui(0x29);
      } else if (g_in_space[kSlot] == 0) {
        // Back on the field: power / 30 + 5 (max 10) stars on random points of the arena.
        g_phase[kSlot] = 2;
        g.stage_hidden = 0;
        g.effects_suppressed = 0;
        spellcast_wait(sc, 0x1A4);
        battle_flash_set(sc, 0x40, 0x20, 0x20);
        const s32 eye[3] = {0, static_cast<s32>(0xFE0C0000), 0};
        camera_move_to(g.camera, eye, rng_range(2) != 0 ? 0xA38 : 0xE38, 0x38E, 0x2800, 0, 0x80, 5);
        const u32 side = rng_range(2);
        const s32 at[3] = {side != 0 ? static_cast<s32>(0xFFB50000) : 0x4B0000, 0, static_cast<s32>(0xFFF9C000)};
        camera_move_to(g.camera, at, side != 0 ? 0x871 : 0xF8E, 0x105, 0x1400, 0x280, 0x80, 5);
        s32 n = sc->power / 0x1E + 5;
        if (10 < n) n = 10;
        for (int i = 0; i < n; ++i) {
          Shot* s = shot_spawn_callback_always(field_star<kSlot>);
          if (s == nullptr) continue;
          g_trail.erase(s);
          s32 p[4];
          stage_random_point(p, 1);
          s->pos[0] = p[0], s->pos[1] = p[1], s->pos[2] = p[2], s->pos_w = p[3];
          s->pos[1] = rng_between_fixed(-14000, -11000);
          stage_random_point(p, 1);
          s->point[0] = p[0], s->point[1] = p[1], s->point[2] = p[2];
          s->anim = 0;
          s->p12c = i;
          const Vec4 v = vec_velocity_toward(s->pos, s->point, 0x28);
          s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
          bobj_share_sprite(&sc->obj, s);
          if (i == 0) {
            sc->s3c = static_cast<s32>(bsfx_play_at(0x2D, s));
            sc->s38 = static_cast<s32>(bsfx_play_at(0x2E, s));
          }
        }
      }
      return;
    case 7:
      if (shot_kind21_absent(field_star<kSlot>) != 0 && spellcast_effects_gone(sc) != 0) ++sc->step;
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

void spell_mod_southern_cross(SpellCast* sc) {
  if (sc->slot == 0) {
    southern_cross<0>(sc);
  } else {
    southern_cross<1>(sc);
  }
}

}  // namespace ss::game::battle
