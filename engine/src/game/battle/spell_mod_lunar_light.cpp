// Spell code module e794431858 (entries 0x926 / 0x927): Lunar Light (spell 0x1d): a moon
// rises over the field (three sprites of the data's set) while beams of light fall in a
// ring from high above and gather at the centre; the light bursts there (spheres, spark
// rings, a white flash, a long shake) and everything of the other side is hit (element
// 0x80). Functions 0x8008cc00 (the handler; its step jump table at 0x8008d824) and
// 0x8008d68c (a falling beam, a kind-0x21 callback with a ribbon trail).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
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

// The SpellCast's fields this module uses: s20 the moon's rise (step 2), then the ticks of
// step 3, s24 the ticks of step 2, s28 the ticks to the burst (from 0x138), s38 / s3c the
// two hum sounds' handles; obj the data's sprite set (anim 3 the moon, 0 and 1 the
// clouds), placed and drawn three times a frame.

// A beam's ribbon trail (Shot +0x130 holds the pointer on the console).
std::unordered_map<const Shot*, RibbonTrail*> g_trail;

// 0x8008d68c: a beam (box 0x20) falling 0x20 a tick from high above with a 24-long ribbon
// trail (0x40c0ff), steering toward x / z 0 (0x800 a tick, at most 0x18 a tick); at the
// ground it stops and its trail ends.
void falling_beam(Shot* s) {
  BattleGlobals& g = bg();
  s->size[0] = 0x20;
  s->size[1] = 0x20;
  s->size[2] = 0x20;
  if ((g.flags & 0xC) != 0) return;
  RibbonTrail*& trail = g_trail[s];
  if (trail == nullptr) trail = ribbon_trail_create(s, 0x18, 0x40C0FF, 0x40C0FF);
  s->vel[1] = 0x20000;
  for (int k = 0; k < 3; k += 2) {
    if (0 < s->pos[k]) {
      s->vel[k] += g.frame_ticks * -0x800;
      if (s->vel[k] < -0x18000) s->vel[k] = -0x18000;
    } else if (s->pos[k] < 0) {
      s->vel[k] += g.frame_ticks * 0x800;
      if (0x18000 < s->vel[k]) s->vel[k] = 0x18000;
    }
  }
  bobj_move(s, s->vel);
  if (0 < s->pos[1]) {
    s->pos[1] = 0;
    if (trail != nullptr) trail->stop = 1;
    g_trail.erase(s);
    shot_kill(s);
  }
}

// Steps 2-3: the moon (anim 3 at scale 1.25, rising with s20 from (-800, -0x1004) at z
// 0x5dc) and two clouds (anims 0 and 1) in front of it, all animated even in the pauses.
void draw_sky(SpellCast* sc) {
  BObj* o = &sc->obj;
  o->pos[0] = (sc->s20 - 800) * 0x1000;
  o->pos[1] = (sc->s20 - 0x1004) * 0x1000;
  o->pos[2] = 0x5DC000;
  o->pos_w = 0;  // (an uninitialised stack word on the console, as below)
  o->anim = 3;
  bobj_project(o);
  o->scale_y = 0x1400;
  o->scale_x = 0x1400;
  bobj_anim_draw(o, 0);
  o->pos[0] = static_cast<s32>(0xFFD44000);
  o->pos[1] = static_cast<s32>(0xFEFFC000);
  o->pos[2] = 0xDAC000;
  o->anim = 0;
  o->scale_y = 0x1000;
  o->scale_x = 0x1000;
  bobj_project(o);
  bobj_anim_draw(o, 0);
  o->pos[0] = 0x320000;
  o->pos[1] = static_cast<s32>(0xFEED0000);
  o->pos[2] = 0xDAC000;
  o->anim = 1;
  bobj_project(o);
  bobj_anim_draw(o, 0);
}

// 0x8008cc00: the handler. Power = INT * 2 + power + 1600; the centre 0x140 above the
// target. 0: dim, flash 0x20. 1 (data loaded): the camera high above (zoomed in), sounds,
// power / 50 + 10 (at most 16) beams in a ring of 6000 round the centre, 0x2710 up (the
// first carries the hums). 2: for 140 ticks the moon rises; then the camera. 3: 60 ticks;
// then flash 0x40, the camera. 4: at the burst (s28 run out): the camera, sounds, a white
// flash, four spheres, five spark rings, three particle rings, a shake, wait 280. 5: a
// small shake, the hums fade, wait 30. 6: the hit on the whole field (element 0x80,
// knockback 0x10), done.
void lunar_light(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] - 0x140000;
  sc->centre[2] = t->pos[2];
  sc->eff_power = sc->caster->st()->intel[2] * 2 + sc->power + 0x640;
  if (static_cast<u32>(sc->step - 2) < 2) draw_sky(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      battle_flash_set(sc, 0x20, 0x20, 0x20);
      ++sc->step;
      return;
    case 1: {
      if (sc->data != nullptr) return;
      if (spellcast_leads_camera(sc)) {
        const s32 at[3] = {0, static_cast<s32>(0xFF400000), 0};
        camera_move_to(g.camera, at, 0xC00, -0x38, 0x1800, 0x1E, 0x80, 5);
        g.camera->zoom_target = 0x3CB0;
        g.camera->zoom_speed = 0x180;
      }
      s32 n = sc->power / 0x32 + 10;
      if (0x10 < n) n = 0x10;
      bsfx_play_ui(0x21);
      bsfx_play_ui(0x22);
      bsfx_play_ui(0x29);
      s32 turn = 0;
      for (int i = 0; i < n; ++i) {
        if (Shot* s = shot_spawn_callback_always(falling_beam)) {
          g_trail.erase(s);
          const s32 a = ((turn / n) * 0x1000) / 360;
          s->pos[0] = bcos(static_cast<u32>(a)) * 6000;
          s->pos[1] = static_cast<s32>(0xFD8F0000);
          s->pos[2] = bsin(static_cast<u32>(a)) * 6000;
          const Vec4 v = vec_random_in_disc_xz(kZeroVector, 0x20);
          s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
          if (i == 0) {
            sc->s3c = static_cast<s32>(bsfx_play_at(0x2D, s));
            sc->s38 = static_cast<s32>(bsfx_play_at(0x44, s));
          }
        }
        turn += 0x168;
      }
      sc->s28 = 0x138;
      ++sc->step;
      return;
    }
    case 2:
      sc->s28 -= g.frame_ticks;
      sc->s24 += g.frame_ticks;
      sc->s20 += g.frame_ticks;
      if (0x8C < sc->s24) {
        sc->s24 = 0;
        ++sc->step;
        if (spellcast_leads_camera(sc)) {
          g.camera->zoom_target = 0x1000;
          g.camera->zoom_speed = 0xC0;
          const s32 at[3] = {0, static_cast<s32>(0xFF800000), 0};
          camera_move_to(g.camera, at, 0xD55, 0x18E, 0x2000, 0x1E, 0x80, 5);
        }
        sc->s20 = 0;
      }
      return;
    case 3:
      sc->s20 += g.frame_ticks;
      sc->s28 -= g.frame_ticks;
      if (0x3C < sc->s20) {
        battle_flash_set(sc, 0x40, 0x40, 0x40);
        ++sc->step;
        if (spellcast_leads_camera(sc)) camera_move_to(g.camera, kZeroVector, 0x9C7, 0x155, 0x1800, 200, 0x80, 5);
      }
      return;
    case 4: {
      sc->s28 -= g.frame_ticks;
      if (0 < sc->s28) return;
      if (spellcast_leads_camera(sc)) {
        const u32 r = rng_range(2);
        const s32 at[3] = {r != 0 ? -0x12C000 : 0x12C000, 0, 0};
        const s32 yaw = r != 0 ? 0xB1C : 0xDC7;
        const s32 pitch = r != 0 ? 0xAA : 0x71;
        const s32 dist = r != 0 ? 0x1800 : 0x1BD0;
        camera_move_to(g.camera, at, yaw, pitch, dist, 300, 0x80, 5);
      }
      battle_flash_set(sc, 0x10, 0x10, 0x10);
      bsfx_play_ui(0x50);
      bsfx_play_ui(0x22);
      sound_handle_fade_out(bsfx_play_ui(0x2D), 0xF0, 0);
      // (the console allocates the fade's 0x1c-byte task with 0x8001fd94 and builds it with
      // 0x8003c3b0; the port's screen_fade_create does both)
      screen_fade_create(0xFFFFFF, 0x20, 4, 10);
      effect_spawn_sphere_at_default(kZeroVector, 10, 10, 0x10, 0x140, 0x3070C0, 600, 0);
      effect_spawn_sphere_at_default(kZeroVector, 8, 8, 0x10, 0x140, 0x4080FF, 0x21C, 0);
      effect_spawn_sphere_at(kZeroVector, 6, 8, 0xE, 0x140, 0xC0FF, 0x168, 0, 0x800);
      effect_spawn_sphere_at(kZeroVector, 8, 7, 0xE, 0x140, 0xFF0080, 0x8C, 0, 0x1800);
      Shot* r = effect_spawn_sparks_at(kZeroVector, 0x18, 0x168, 800, 0x10, 300, 0, 0, 600, 0);
      if (r != nullptr) r->vel[0] = 0xB;
      // ORIGINAL BUG (original_bugs.md S8): the second ring's Shot is dropped and its turn
      // (-11) goes to the first ring again, so the first turns -11 and the second not at all:
      //   effect_spawn_sparks_at(kZeroVector, 0x18, 0x280, 0x280, 0x10, 300, 5, 0, 0x2A8, 0);
      //   if (r != nullptr) r->vel[0] = -0xB;
      if (Shot* r2 = effect_spawn_sparks_at(kZeroVector, 0x18, 0x280, 0x280, 0x10, 300, 5, 0, 0x2A8, 0))
        r2->vel[0] = -0xB;
      effect_spawn_sparks_at(kZeroVector, 0x14, 0xF0, 0x1E0, 0x18, 0xB4, 0xE, 1, 0x2EE, 0);
      effect_spawn_sparks_at(kZeroVector, 0x14, 0x140, 0, 0x50, 0x78, 5, 3, 0x154, 0);
      effect_spawn_sparks_at(kZeroVector, 0x14, 0x1E0, 0, 0x40, 0x78, 4, 4, 0x154, 0);
      particle_spawn_ring(kZeroVector, 0x10, 600, 0x30, 300, 0x303030, 0x3070C0, 0, 0);
      particle_spawn_ring(kZeroVector, 0x14, 0x40, 0x30, 0x78, 0, 0x40FF, 600, 0);
      const s32 low[4] = {0, static_cast<s32>(0xFFED4000), 0, 0};
      particle_spawn_ring(low, 0x14, 0x40, 0x2C, 0x78, 0, 0x40FF, 500, 0);
      screen_shake2(6, 8, 6, 4, 300);
      spellcast_wait(sc, 0x118);
      return;
    }
    case 5:
      screen_shake2(2, 2, 4, 4, 0x3C);
      sound_handle_fade_out(static_cast<u32>(sc->s38), 0x3C, 0);
      sound_handle_fade_out(static_cast<u32>(sc->s3c), 0x3C, 0);
      spellcast_wait(sc, 0x1E);
      return;
    case 6:
    case 7:
      spell_hit_all(sc, 0x80, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_lunar_light(SpellCast* sc) { lunar_light(sc); }

}  // namespace ss::game::battle
