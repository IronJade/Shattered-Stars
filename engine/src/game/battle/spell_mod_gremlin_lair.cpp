// Spell code module fc13393fe3 (entries 0x90c / 0x90d): Gremlin Lair (spell 0x23). Light
// gathers in front of the caster and bursts; a gremlin (the data's sprite) drops out of it,
// throws a stream of imps that whirl out from it (hitting what they meet), then visits each
// standing unit of the other side in turn and strikes it up to four times; then it goes
// back and vanishes. In a combo the imps fly to the combo point instead and the gremlin
// strikes no one. Functions 0x8008cc00 (the handler; its step jump table at 0x8008e54c),
// 0x8008d648 (the gremlin, a kind-0x21 callback; its jump table at 0x8008e574) and
// 0x8008e0c0 (an imp, kind 0x21).

#include <cstring>
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
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the light is drawn (from step 2), s24 the
// gathering ticks (step 1), s28 the gathering sound's handle, s2c the gremlin (a Shot*:
// g_gremlin here), s30 the gremlin's signal (1 it moves to a unit, 2 it is back, 3 it is
// gone); obj the data's sprite set (anim 2 the light, 10 at the end), drawn at the centre.
std::unordered_map<const SpellCast*, Shot*> g_gremlin;

// The gremlin's cast: its +0x130 points at the cast's s30 (the signal) and, in a combo, its
// +0x120 holds the cast (for the combo point).
std::unordered_map<const Shot*, SpellCast*> g_cast;

// An imp's afterimage (Shot +0x114 holds the pointer on the console; its +0x130, the unit
// it last hit, is Shot::last_hit).
std::unordered_map<const Shot*, Afterimage*> g_trail;

SpellCast* cast_of(const Shot* s) {
  const auto it = g_cast.find(s);
  if (it == g_cast.end()) return nullptr;
  // (The console writes through the pointer whatever became of the cast; the cast outlives
  // the gremlin unless the battle ends.)
  return it->second == spellcast_slot(0) || it->second == spellcast_slot(1) ? it->second : nullptr;
}

void signal(const Shot* s, s32 value) {
  if (SpellCast* sc = cast_of(s)) sc->s30 = value;
}

// The unit the gremlin is at (+0x128 the index into the list of its side, +0x134: 0 the
// monsters, else the party).
Unit* gremlin_unit(const Shot* s) {
  BattleGlobals& g = bg();
  return s->p134 == 0 ? static_cast<Unit*>(g.enemy_units[static_cast<size_t>(s->p128)])
                      : static_cast<Unit*>(g.party_units[static_cast<size_t>(s->p128)]);
}

// A heap Shot (0x8001fd94(0x13c), bobj_create, the Collider and Shot vtables) of the
// owner, x far on the owner's side, through shot_apply_damage (no sfx, element 0x200,
// knockback 4), then freed: spellcast_damage_units with the Shot's owner.
void damage_unit(Unit* owner, Unit* u, int power) {
  Shot tmp;
  tmp.owner = owner;
  tmp.pos[0] = owner->is_enemy != 0 ? static_cast<s32>(0xFD8F0000) : 0x2710000;
  tmp.pos[1] = 0;
  tmp.pos[2] = 0;
  Unit* list[1] = {u};
  shot_apply_damage(&tmp, 1, list, nullptr, power, 0, 0x200, 0, 4);
}

// 0x8008e0c0: an imp (anim 7, a 4-copy afterimage) whirling out round +0x118 (angle +0x128
// turning 40 a tick, radius +0x12c growing 12, spinning and growing) at its rising
// velocity, gone at radius 0x961; it hits (+0x10c the power) the first unit of side +0x134
// its box meets, once per unit in a row. Side 2 (a combo): it flies to +0x118 and goes.
void imp(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 7;
  if (s->p134 != 2) {
    s->pos[0] = bcos(static_cast<u32>(s->p128)) * s->p12c + s->point[0];
    s->pos[2] = bsin(static_cast<u32>(s->p128)) * s->p12c + s->point[2];
  }
  s->size[2] = 0x100;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr) trail = afterimage_create(s, 4, 0x802040, 0);
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  bobj_move(s, s->vel);
  if (s->p134 == 2) {
    const Vec4 d = vec_direction(s->pos, s->point);
    s->vel[0] = d.x << 4, s->vel[1] = d.y << 4, s->vel[2] = d.z << 4, s->extra34 = d.w;
    if (0x3FFFF < vec_distance_fx(s->pos, s->point)) return;
  } else {
    const s32 t = g.frame_ticks;
    s->p12c += t * 0xC;
    s->p128 = (s->p128 + t * 0x28) & 0xFFF;
    s->roll = static_cast<s16>((static_cast<u16>(s->roll) + t * 0x80) & 0xFFF);
    s->scale_x += t * 0x10;
    s->scale_y += t * 0x10;
    if (s->p12c < 0x961) {
      Unit* units[8] = {};
      Vec4 points[8] = {};
      if (collider_attack_overlaps(s, units, points, s->p134) == 0) {
        s->last_hit = nullptr;
        return;
      }
      if (s->last_hit == units[0]) return;
      Unit* u = units[0];
      s->last_hit = u;
      screen_shake(2, 5, 2, 2, 0xC);
      u->shake_frames = 0x14;
      u->shake_amp = 0x10;
      bsfx_play_at(0x4A, u);
      const s32 spread[3] = {0x100000, 0x40000, 0x100000};
      effect_debris_burst(u->pos, spread, 4, 1, 1, 0x4000, 0xB);
      damage_unit(s->owner, u, s->aux);
      return;
    }
  }
  if (trail != nullptr) trail->ending = 1;
  g_trail.erase(s);
  shot_kill(s);
}

// 0x8008d648: the gremlin (+0x10c its state; +0x128 / +0x12c timers, +0x128 also the unit
// index; +0x134 the side it attacks; +0x118 its power, +0x11c set in a combo). 0 grows in;
// 1 drops to the ground; 2 waits 30; 3 throws an imp every 11 ticks (power / 24 + 16 of
// them, at most 32; a combo then skips to 11); 4 waits 30; 5; 6 the next standing unit of
// its side (none left: 11); 7 signals 1 and leaps beside it; 8 lands; 9 strikes it on
// frame 3 of each of 4 loops; 10 the next unit; 11 waits 20; 12 signals 2 (the cast puts
// it back at the centre), sfx 0x32; 13 anim 9 for 60 ticks, sfx 0x2c; 14 shrinks away and
// signals 3. Drawn with a shadow at layer 6.
void gremlin(Shot* s) {
  BattleGlobals& g = bg();
  const u8 flags = g.flags;
  const s32 ticks = g.frame_ticks;
  switch (s->aux) {
    case 0:
      s->anim = 3;
      s->scale_y = s->p128;
      s->scale_x = s->p128;
      if ((flags & 0xC) == 0) {
        s->p128 += g.frame_ticks * 0x100;
        if (0xFFF < s->p128) {
          s->scale_y = 0x1000;
          s->scale_x = 0x1000;
          s->p128 = 0;
          ++s->aux;
        }
      }
      break;
    case 1: {
      if ((g.flags & 0xC) != 0) break;
      s->vel[1] += g.frame_ticks * 0x400;
      bobj_move(s, s->vel);
      const s32 ground = bobj_ground_height(s);
      if (s->pos[1] <= ground) break;
      s->pos[1] = ground;
      ++s->aux;
      break;
    }
    case 2:
    case 4:
      s->p128 += g.frame_ticks;
      if (s->p128 < 0x1F) break;
      s->p128 = 0;
      ++s->aux;
      break;
    case 3: {
      s->anim = 0;
      s->p128 += ticks;
      if (s->p128 < 0x33) break;
      s->p128 = 0x28;
      if (Shot* m = shot_spawn_callback_always(imp)) {
        g_trail.erase(m);
        bsfx_play_at(0x36, m);
        m->point[0] = s->pos[0] - 0x20000;
        m->point[1] = s->pos[1] - 0x64000;
        m->point[2] = s->pos[2];
        m->aux = static_cast<s16>(s->point[0]);
        m->vel[1] = rng_between(-0x6000, 0);
        m->owner = s->owner;
        m->p134 = s->p134;
        if (s->point[1] != 0) {
          m->p134 = 2;
          m->pos[0] = m->point[0];
          m->pos[1] = m->point[1];
          m->pos[2] = m->point[2];
          m->pos_w = 0;  // (+0x124, the Shot's ribbon word: 0)
          if (SpellCast* sc = cast_of(s)) {
            s32 p[4] = {m->point[0], m->point[1], m->point[2], 0};
            spellcast_combo_point(sc, p);
            m->point[0] = p[0], m->point[1] = p[1], m->point[2] = p[2];
          }
        }
        m->p128 = static_cast<s32>(rng_range(0xFFF));
        m->roll = static_cast<s16>(rng_range(0xFFF));
        m->scale_y = 0x2000;
        m->scale_x = 0x2000;
        bobj_share_sprite(s, m);
      }
      s32 n = static_cast<s16>(s->power / 0x18) + 0x10;
      if (0x20 < n) n = 0x20;
      if (++s->p12c <= n) break;
      s->anim = 3;
      s->p128 = 0;
      s->p12c = 0;
      ++s->aux;
      if (s->point[1] != 0) s->aux = 0xB;
      break;
    }
    case 5:
      s->p128 = 0;
      ++s->aux;
      break;
    case 6: {
      const bool in_list = s->p134 == 0 ? s->p128 < 8 : s->p128 < 4;
      if (!in_list) {
        s->aux = 0xB;
        break;
      }
      if (unit_is_down(gremlin_unit(s))) {
        s->aux = 10;
        break;
      }
      s->anim = 4;
      s->p12c += g.frame_ticks;
      if (s->p12c < 0x15) break;
      s->p12c = 0;
      ++s->aux;
      break;
    }
    case 7:
      s->anim = 5;
      s->p12c += ticks;
      if (0x1E < s->p12c) {
        signal(s, 1);
        s->p12c = 0;
        ++s->aux;
        if (const Unit* u = gremlin_unit(s)) {
          s->pos[0] = u->pos[0] + u->size[0] * 4 + 0xC0000;
          s->pos[1] = u->pos[1] + u->size[1] * -4 + 0x50000;
          s->pos[2] = u->pos[2];
        }
      }
      break;
    case 8:
      s->anim = 6;
      s->p12c += ticks;
      if (0x14 < s->p12c) {
        s->p12c = 0;
        ++s->aux;
        bobj_anim_reset(s);
      }
      break;
    case 9: {
      s->anim = 1;
      if (s->frame != 3) {
        s->p12c = 0;
      } else if (s->p12c == 0) {
        s->p12c = 1;
        Unit* u = gremlin_unit(s);
        if (unit_is_down(u)) {
          s->p12c = 0;
          ++s->aux;
          break;
        }
        bsfx_play_at(0x4C, s);
        bsfx_play_at(0x48, s);
        const s32 dx = rng_between_fixed(0xC0, 0xE0);
        const s32 dy = rng_between_fixed(300, 0x15E);
        const s32 at[4] = {s->pos[0] - dx, s->pos[1] - dy, s->pos[2], 0};
        if (Sprite* sp = sprite_spawn_from(9, 8, s, nullptr)) {
          sprite_set_pos_v(sp, at);
          sp->layer = 2;
        }
        particle_spawn_ring(at, 0x10, 0x30, 0x30, 0x3C, 0x201040, 0xC02060, 0, 0);
        screen_shake(4, 2, 2, 2, 0x10);
        for (int k = 0; k < 2; ++k) {
          const s32 spread[3] = {0x100000, 0x40000, 0x100000};
          effect_debris_burst(at, spread, 4, 1, k ^ 1, k == 0 ? 12000 : 8000, static_cast<u8>(k == 0 ? 0xB : 10));
        }
        for (int k = 0; k < 0x20; ++k) {
          const Vec4 v = vec_random_around(kZeroVector, 0xE);
          particle_spawn_spark_line(at, v.data(), 0xC0, 0x4040FF, 0x18, 0x5A);
        }
        u->shake_frames = 0x14;
        u->shake_amp = 0x10;
        damage_unit(s->owner, u, s->point[0]);
      }
      if (s->loops < 4) break;
      s->p12c = 0;
      ++s->aux;
      break;
    }
    case 10:
      s->aux = 6;
      ++s->p128;
      break;
    case 11:
      s->anim = 5;
      s->p12c += g.frame_ticks;
      if (s->p12c < 0x15) break;
      s->p12c = 0;
      ++s->aux;
      break;
    case 12:
      signal(s, 2);
      ++s->aux;
      bsfx_play_at(0x32, s);
      break;
    case 13:
      s->anim = 9;
      s->p12c += ticks;
      if (s->p12c < 0x3D) break;
      s->p12c = 0;
      ++s->aux;
      bsfx_play_at(0x2C, s);
      break;
    case 14: {
      const s32 scale = 0x1000 - s->p128;
      s->scale_y = scale;
      s->scale_x = scale;
      if ((g.flags & 0xC) != 0) break;
      s->p128 += g.frame_ticks * 0x80;
      if (s->p128 < 0x1000) break;
      s->scale_y = 0;
      s->scale_x = 0;
      signal(s, 3);
      g_cast.erase(s);
      shot_kill(s);
      return;
    }
    default: break;
  }
  s->layer = 6;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  s32 h = s->pos[1] - bobj_ground_height(s);
  if (h < 0) h += 0xF;
  s32 ext = s->ext_x1;
  if (ext < 0) ext = -ext;
  s32 r = (ext * 5 + (h >> 16)) * s->scale_x;
  if (r < 0) r += 0xFFF;
  r >>= 12;
  if (r < 0x10) r = 0x10;
  bobj_draw_shadow(s, -r, r, -r, r, 0xFFFFFFFF);
}

// 0x8008cc00: the handler. Power = (INT + power / 2 + 950) / 4; the centre 0x180 in front
// of the caster, 0x100 up. 0: dim, flash 0x60, camera on the caster, the gathering sound.
// 1: light streaks drawn in for 90 ticks; then sounds, the camera, five rings, a sphere, a
// flash, a shake, 64 streaks out. 2: the light (anim 2) appears, wait 40. 3: the gremlin
// (none: step 8), wait 60. 4: the camera, wait 60. 5: follows the gremlin's signals (1 /
// 2 the camera orbits it, 2 also puts it back at the centre; 3 step 6). 6: anim 10,
// sounds, 72 streaks out, wait 16. 7: the effects end. 8: flash back, undim, done.
void gremlin_lair(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  sc->centre[0] = bobj_front_x(c, 0x180);
  sc->centre[1] = c->pos[1] - 0x100000;
  sc->centre[2] = c->pos[2];
  s32 p = c->st()->intel[2] + sc->power / 2 + 0x3B6;
  if (p < 0) p += 3;
  sc->eff_power = p >> 2;
  if (sc->s20 != 0) {
    sc->obj.pos[0] = sc->centre[0];
    sc->obj.pos[1] = sc->centre[1];
    sc->obj.pos[2] = sc->centre[2];
    bobj_project(&sc->obj);
    bobj_anim_draw(&sc->obj, g.flags & 0xC);
  }
  if ((g.flags & 4) != 0) return;
  // ORIGINAL BUG (original_bugs.md S17, held): the handler never calls spellcast_timer_tick,
  // so the waits of steps 2, 3, 4 and 6 hold nothing (each step runs on the next frame). The
  // fix (the timer ticked here, step 6 waiting into step 7) lengthens the cast, which the
  // 60-second battles' time limit counts: held; SS_HELD_FIXES=S17 turns it on to measure.
  if (held_fix_enabled("S17") && spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0: {
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x60, 0x60, 0x60);
      if (spellcast_leads_camera(sc)) {
        camera_move_to(g.camera, c->pos, 0xC00, 0x1C7, 0x800, 0, 0x80, 5);
        camera_move_to(g.camera, c->pos, 0xC00, 0x1C7, 0xC00, 0x5A, 0x80, 5);
      }
      const u32 h = bsfx_play_ui(0x45);
      sc->s28 = static_cast<s32>(h);
      sound_handle_pitch_up(h, 300);
      sc->step = 1;
      return;
    }
    case 1: {
      for (int i = 0; i < (g.frame_ticks + 1) / 2; ++i) {
        s32 from[4];
        from[0] = sc->centre[0] + rng_between_fixed(-0x400, 0x400);
        from[1] = sc->centre[1] + rng_between_fixed(-0xC0, 0xC0);
        from[2] = sc->centre[2] + rng_between_fixed(-0x400, 0x400);
        from[3] = 0;  // (an uninitialised stack word on the console)
        particle_spawn_homing_line(from, sc->centre, 0x820, 0x40FF, 0xA0, 0x14000, 0);
      }
      sc->s24 += g.frame_ticks;
      if (sc->s24 < 0x5B) return;
      s32 at[4] = {sc->centre[0], sc->centre[1], sc->centre[2], 0};
      sound_handle_fade_out(static_cast<u32>(sc->s28), 0x1E, 0);
      bsfx_play_ui(0x33);
      bsfx_play_ui(0x2A);
      at[1] = stage_ground_height(at[0] >> 12, at[2] >> 12);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, at, 0xC00, 0xAA, 0x800, 0xC, 0x80, 5);
      particle_spawn_ring(sc->centre, 0x10, 0x60, 0x80, 0x2D, 0x60, 0xFF, 0, 0);
      particle_spawn_ring(sc->centre, 0x10, 0x80, 0x50, 0x3C, 0x300050, 0x9000FF, 0, 0);
      particle_spawn_ring(sc->centre, 0x10, 0x60, 0x80, 0x2D, 0x60, 0xFF, 0, 0x400);
      particle_spawn_ring(sc->centre, 0x10, 0x60, 0x80, 0x2D, 0x60, 0xFF, 0, 0x200);
      particle_spawn_ring(sc->centre, 0x10, 0x60, 0x80, 0x2D, 0x60, 0xFF, 0, 0x600);
      effect_spawn_sphere_at_default(at, 8, 8, 0x32, 0x3C, 0x8040C0, 0, 0);
      // (the console allocates the fade's 0x1c-byte task with 0x8001fd94 and builds it with
      // 0x8003c3b0; the port's screen_fade_create does both)
      screen_fade_create(0xFF18FF, 0x20, 4, 10);
      screen_shake_vertical(3, 2, 0x10);
      for (int i = 0; i < 0x40; ++i) {
        const Vec4 to = vec_random_around(sc->centre, 0x640);
        particle_spawn_homing_line(sc->centre, to.data(), 0x402008, 0x80FF40, 0xA0, 0x14000, -0x100);
      }
      sc->step = 2;
      return;
    }
    case 2:
      bsfx_play_at_pos(0x32, sc->centre);
      sc->obj.anim = 2;
      sc->s20 = 1;
      spellcast_wait(sc, 0x28);
      return;
    case 3: {
      battle_flash_set(sc, 0x60, 0x40, 0x40);
      Shot* s = shot_spawn_callback_always(gremlin);
      if (s == nullptr) {
        sc->step = 8;
        return;
      }
      spellcast_wait(sc, 0x3C);
      s->owner = c;
      s->power = static_cast<s16>(sc->power);
      g_gremlin[sc] = s;
      g_cast[s] = sc;  // (+0x130 = &sc->s30)
      bobj_set_pos(s, sc->centre);
      bobj_share_sprite(&sc->obj, s);
      s->p134 = sc->combo == 3 ? c->is_enemy ^ 1 : c->is_enemy;
      s->point[1] = 0;
      s->point[0] = sc->eff_power;
      if (static_cast<u8>(sc->combo - 1) < 2) s->point[1] = 1;  // (+0x120 = sc: g_cast)
      if (sc->combo == 4) s->point[0] = sc->eff_power + spellcast_slot(sc->slot ^ 1)->eff_power;
      return;
    }
    case 4: {
      s32 at[3] = {sc->centre[0], sc->centre[1], sc->centre[2]};
      at[1] = stage_ground_height(at[0] >> 12, at[2] >> 12);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, at, 0xA71, 0x155, 0x1800, 600, 0x80, 5);
      spellcast_wait(sc, 0x3C);
      return;
    }
    case 5: {
      const auto it = g_gremlin.find(sc);
      Shot* s = it != g_gremlin.end() ? it->second : nullptr;
      if (sc->s30 == 1) {
        if (spellcast_leads_camera(sc) && s != nullptr) {
          camera_orbit_unit(g.camera, s, 0xC00, 0x155, 0x800, 0x1E, 0x80, 5);
        }
        sc->s30 = 0;
      }
      if (sc->s30 == 2) {
        if (s != nullptr) {
          s->pos[0] = sc->centre[0];
          s->pos[1] = sc->centre[1];
          s->pos[2] = sc->centre[2];
        }
        if (spellcast_leads_camera(sc) && s != nullptr) {
          camera_orbit_unit(g.camera, s, 0xC00, 0x155, 0x800, 0x1E, 0x80, 5);
        }
        sc->s30 = 0;
      }
      if (sc->s30 == 3) sc->step = 6;
      return;
    }
    case 6:
      sc->obj.anim = 10;
      // ORIGINAL BUG (original_bugs.md S17, held): step 7 is meant to come next, but
      // spellcast_wait below advances the step once more, so the wait for the effects (step 7)
      // is skipped.
      sc->step = held_fix_enabled("S17") ? 6 : 7;
      bsfx_play_ui(0x28);
      bsfx_play_ui(0x29);
      bsfx_play_ui(0x2A);
      for (int i = 0; i < 0x18; ++i) {
        for (int k = 0; k < 3; ++k) {
          u32 head = 0, tail = 0;
          if (k == 0) {
            head = 0x402008, tail = 0xFF8040;
          } else if (k == 1) {
            head = 0x204008, tail = 0x80FF40;
          } else {
            head = 0x200840, tail = 0x804080;
          }
          const Vec4 to = vec_random_around(sc->centre, 0x640);
          const s32 speed = rng_between(0x14000, 0x18000);
          particle_spawn_homing_line(sc->centre, to.data(), head, tail, 0xA0, speed, -0x100);
        }
      }
      spellcast_wait(sc, 0x10);
      return;
    case 7:
      if (spellcast_effects_gone(sc) != 0) sc->step = 8;
      return;
    case 8:
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      sc->finished = 1;
      sc->step = 9;
      g_gremlin.erase(sc);
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_gremlin_lair(SpellCast* sc) { gremlin_lair(sc); }

}  // namespace ss::game::battle
