// Spell code module 02571c0499 (entries 0x97c / 0x97d): Extinction (spell 0x25): three
// turning funnels of colour hang over the field while meteors (the data's sprite) rain on
// random points for 300 ticks; then a burst of rings and streaks at the centre, 360 ticks
// of exploding sparks over the whole field, and everything of the other side is hit
// (element 0x200). Functions 0x8008cc00 (the handler; its step jump table at 0x8008d728)
// and 0x8008d470 (a meteor, a kind-0x21 callback with a ribbon trail).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
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

// The SpellCast's fields this module uses: s20 the ticks since the start (the funnels'
// colour phase; the rain's end at 300), s24 the ticks of step 4; obj the data's sprite set
// (anim 5 the meteor, 4 the dust, 1-2 debris, 7-10 the sparks).

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

// A meteor's ribbon trail (Shot +0x134 holds the pointer on the console).
std::unordered_map<const Shot*, RibbonTrail*> g_trail;

// 0x8008d470: a meteor (anim 5 at scale 2, box 0x40, an 8-long ribbon trail 0x400030)
// flying at its velocity; below the ground it is set 0xc8 up and bursts: a shake, three
// dust puffs, three debris sprites, two sounds, a ring; its trail ends and it goes.
void meteor(Shot* s) {
  BattleGlobals& g = bg();
  s->scale_y = 0x2000;
  s->scale_x = 0x2000;
  s->anim = 5;
  bobj_project(s);
  bobj_anim_draw(s, 0);
  if ((g.flags & 0xC) != 0) return;
  s->size[0] = 0x40;
  s->size[1] = 0x40;
  s->size[2] = 0x40;
  s->p128 += g.frame_ticks;
  RibbonTrail*& trail = g_trail[s];
  if (trail == nullptr) trail = ribbon_trail_create(s, 8, 0x400030, 0x400030);
  bobj_move(s, s->vel);
  if (!(bobj_ground_height(s) < s->pos[1])) return;
  s->pos[1] = static_cast<s32>(0xFFF38000);
  screen_shake(4, 6, 4, 6, 0x14);
  for (int i = 0; i < 3; ++i) {
    Sprite* sp = shot_spawn_sprite(s, 4);
    if (sp == nullptr) continue;
    const s32 dx = rng_between_fixed(-0x60, 0x60);
    const s32 y = bobj_ground_height(s);
    const s32 dz = rng_between_fixed(-0x60, 0x60);
    sprite_set_pos(sp, s->pos[0] + dx, y, s->pos[2] + dz);
    sp->scale_x = 0x2000;
    sp->scale_y = 0;
    sp->scale_vel[1] = 0x100;
    sprite_set_fade(sp, 0x80, -2, 8);
    sp->layer = 4;
  }
  for (int i = 0; i < 3; ++i) {
    Sprite* sp = shot_spawn_sprite_at(s, static_cast<int>(rng_range(2)) + 1, 1);
    if (sp == nullptr) continue;
    const s32 r = static_cast<s32>(rng_range(0x100));
    const Vec4 at = vec_random_around(s->pos, r);
    sprite_set_pos_v(sp, at.data());
    const s32 scale = rng_between(0x2000, 0x4000);
    sp->scale_y = scale;
    sp->scale_x = scale;
    sp->size[2] = static_cast<s16>(rng_range(8));
    sp->layer = 8;
  }
  const s32 at[4] = {s->pos[0], s->pos[1], s->pos[2], s->pos_w};
  bsfx_play_at_pos(0x50, at);
  bsfx_play_at_pos(0x22, at);
  particle_spawn_ring(at, 0xC, 0x80, 0x80, 0x3C, 0x1030, 0x20F0A0, 0x78, 0);
  if (trail != nullptr) trail->stop = 1;
  g_trail.erase(s);
  shot_kill(s);
}

// Steps 2-3: three funnels (draw_vortex: 8 rings x 8 segments, additive) along z from -0x514
// (0x64 apart) at y -0x1f4, axis toward -z; colours 0x2020c0 / 0x40c020 / 0xc04010 to
// 0x1020, their phase running with s20.
void draw_funnels(SpellCast* sc) {
  s32 z = -0x514000;
  for (int i = 0; i < 3; ++i) {
    VortexParams p;
    p.radius = 0x8000;
    p.base = -0x200;
    p.width = 0x800;
    p.step = (i + 1) * 0x40;
    p.rings = 8;
    p.segments = 8;
    p.colour_a = i == 0 ? 0x2020C0u : i == 1 ? 0x40C020u : 0xC04010u;
    p.colour_b = 0x1020;
    p.phase_span = 2;
    p.phase = 0x1FFF - ((sc->s20 * 0x80 + i * 0x100) & 0x1FFF);
    p.twist_freq = 1;
    p.twist_amp = 0x1000;
    p.blend = 2;
    p.ot_bias = 5;
    const s32 axis[3] = {0, 0, static_cast<s32>(0xFFF9C000)};
    s32 orient[4] = {};
    vec_to_angles(orient, axis);
    const s32 at[3] = {0, static_cast<s32>(0xFFE0C000), z};
    obj_set_matrix(at, orient, kUnitScale);
    draw_vortex(p);
    z += 0x64000;
  }
}

// 0x8008cc00: the handler. Power = INT * 4 + power * 2 + 3000. s20 counts every frame
// that is not held. 0: dim, flash 0x40, the camera, wait 30. 1: the data loaded. 2: a
// meteor a frame from high over a random point to another (sfx 0x2c now and then); after
// 300 ticks a fade, wait 50, sounds. 3: a sound, four rings and 96 streaks at the centre,
// the camera. 4: for 360 ticks two sparks a tick all over (sfx 0x50 and a shake now and
// then). 5: the effects end. 6: the hit on the whole field (element 0x200, knockback
// 0x10), done.
void extinction(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;  // (an uninitialised stack word on the console)
  sc->eff_power = sc->caster->st()->intel[2] * 4 + sc->power * 2 + 3000;
  const s32 mid[4] = {0, static_cast<s32>(0xFFE0C000), 0, 0};
  if (static_cast<u32>(sc->step - 2) < 2) draw_funnels(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  sc->s20 += g.frame_ticks;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) {
        camera_reset(g.camera);
        const s32 yaw = rng_range(2) != 0 ? 0xAAA : 0xD55;
        camera_move_to(g.camera, kZeroVector, yaw, 0x1C7, 0x1800, 0, 0x80, 5);
        camera_move_to(g.camera, kZeroVector, 0xC00, 0x71, 0x1000, 300, 0x80, 5);
      }
      spellcast_wait(sc, 0x1E);
      return;
    case 1:
      if (sc->data != nullptr) return;
      ++sc->step;
      return;
    case 2:
      if (Shot* s = shot_spawn_callback_always(meteor)) {
        g_trail.erase(s);
        s32 p[4];
        stage_random_point(p, 1);
        s->pos[0] = p[0], s->pos[1] = p[1], s->pos[2] = p[2], s->pos_w = p[3];
        s->pos[1] = static_cast<s32>(0xFFC18000);
        stage_random_point(p, 1);
        s->point[0] = p[0], s->point[1] = p[1], s->point[2] = p[2];  // (p[3] lands on +0x124)
        const Vec4 v = vec_velocity_toward(s->pos, s->point, 0x40);
        s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
        bobj_share_sprite(&sc->obj, s);
        if (static_cast<u32>(rng_next() & 0x7FFF) < 0x1800) bsfx_play_at(0x2C, s);
      }
      if (300 < sc->s20) {
        // (the console allocates the fade's 0x1c-byte task with 0x8001fd94 and builds it
        // with 0x8003c3b0; the port's screen_fade_create does both)
        screen_fade_create(0, 0x20, -4, 0xC);
        spellcast_wait(sc, 0x32);
        bsfx_play_ui(0x28);
        bsfx_play_ui(0x29);
      }
      return;
    case 3:
      sound_handle_fade_out(bsfx_play_ui(0x2D), 0x1E, 0xB4);
      ++sc->step;
      particle_spawn_ring(mid, 0x10, 0x100, 0x50, 0x5A, 0x1010, 0x2040FF, 0, 0);
      particle_spawn_ring(mid, 0x10, 0x100, 0x50, 0x5A, 0x1010, 0x2040FF, 0, 0x400);
      particle_spawn_ring(mid, 0x10, 0x100, 0x20, 0xB4, 0x1010, 0xFF4000, 0, 0x200);
      particle_spawn_ring(mid, 0x10, 0x100, 0x20, 0xB4, 0x1010, 0xFF4000, 0, 0x600);
      for (int i = 0; i < 0x60; ++i) {
        const Vec4 to = vec_random_around(mid, 0x640);
        const u32 head = rng_range(2) != 0 ? 0x400020u : 0x105010u;
        const u32 tail = rng_range(2) != 0 ? 0x20FFu : 0xFF8020u;
        particle_spawn_homing_line(mid, to.data(), head, tail, 0x50, 8, 0x800);
      }
      if (spellcast_leads_camera(sc)) {
        camera_reset(g.camera);
        const u32 r = rng_range(2);
        const s32 at[3] = {r != 0 ? static_cast<s32>(0xFFE0C000) : 0x1F4000, 0, 0x3E8000};
        const s32 yaw = r != 0 ? 0x6AA : 0x155;
        camera_move_to(g.camera, at, yaw, 0x105, 0x1800, 0x168, 0x80, 5);
      }
      return;
    case 4:
      sc->s24 += g.frame_ticks;
      if (0x168 < sc->s24) ++sc->step;
      for (int i = 0; i < g.frame_ticks << 1; ++i) {
        s32 at[3];
        at[0] = rng_between_fixed(-0x9C4, 0x9C4);
        at[1] = rng_between_fixed(-0x514, 0);
        at[2] = rng_between_fixed(-0x7D0, 2000);
        Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(4)) + 7);
        if (sp == nullptr) continue;
        sprite_set_pos_v(sp, at);
        const s32 scale = rng_between(0x4000, 40000);
        sp->scale_y = scale;
        sp->scale_x = scale;
        sprite_set_fade(sp, 0x80, -4, 8);
        sp->layer = 0x10;
        const Vec4 v = vec_random_in_disc_xz(kZeroVector, 4);
        sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
        if (static_cast<u32>(rng_next() & 0x7FFF) < 0x800) {
          bsfx_play_at(0x50, sp);
          screen_shake(8, 4, 6, 6, 0xC);
        }
      }
      return;
    case 5:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 6:
      spell_hit_all(sc, 0x200, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_extinction(SpellCast* sc) { extinction(sc); }

}  // namespace ss::game::battle
