// Spell code module 4d978c241f (entries 0x90e-0x910): Fairy Light (spell 0x33): lights swirl
// round the caster, then columns of light rise over every standing unit of the caster's
// side while fairies fly in; rings and rising sparkles follow and each is healed INT * 2 +
// power * 4 + 2200. Functions 0x8008cc00 (the handler), 0x8008d930 (a swirling light, a
// kind-0x21 callback), 0x8008dac0 (a fairy flying in, kind 0x21) and 0x8008dc00 (a rising
// sparkle, kind 0x21).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the lights spawned (step 2), then the sparkles' ticks (step
// 7); s24 the columns' length, s28 their radius, s2c their fade; s3c the chant sound's
// handle; obj the data's sprite set (anim 3 the swirling lights, 1 the fairies, 5 the
// sparkles).

// A fairy's afterimage (Shot +0x134 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

// The caster's side: the party (4) or the monsters (8).
Unit* side_unit(const SpellCast* sc, int i) {
  const BattleGlobals& g = bg();
  return sc->caster->is_enemy == 0 ? static_cast<Unit*>(g.party_units[static_cast<size_t>(i)])
                                   : static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)]);
}

// 0x8008d930: a light (anim 3) on a sphere of radius +0x130 (shrinking 2 a tick) round
// +0x118 at angles +0x128 / +0x12c (turning 30 / -20 a tick); below 40 it dims (anim 4),
// below 5 it goes.
void swirling_light(Shot* s) {
  BattleGlobals& g = bg();
  s->pos[0] = s->point[0] + ((bcos(static_cast<u32>(s->p128)) * bcos(static_cast<u32>(s->p12c))) >> 12) * s->p130;
  s->pos[1] = s->point[1] + bsin(static_cast<u32>(s->p12c)) * s->p130;
  s->pos[2] = s->point[2] + ((bsin(static_cast<u32>(s->p128)) * bcos(static_cast<u32>(s->p12c))) >> 12) * s->p130;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  s->p128 = (s->p128 + g.frame_ticks * 0x1E) & 0xFFF;
  s->p12c = (s->p12c + g.frame_ticks * -0x14) & 0xFFF;
  s->p130 += g.frame_ticks * -2;
  if (s->p130 < 0x28) {
    s->colour_mode = 2;
    s->anim = 4;
    s->rgb[0] = static_cast<u8>(s->p130 * 0x14);
  }
  if (s->p130 < 5) shot_kill(s);
}

// 0x8008dac0: a fairy (anim 1, 1.25) flying in on its velocity under gravity 0x600 a tick
// with an afterimage; after 64 ticks it leaves a fading puff (anim 2) and goes.
void fairy(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  s->layer = 2;
  s->scale_y = 0x1400;
  s->scale_x = 0x1400;
  shot_draw_with_shadow(s, 3, 0xFFFFFFFF);
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr) trail = afterimage_create(s, 4, 0x4060, 1);
  if ((g.flags & 0xC) != 0) return;
  bobj_move(s, s->vel);
  s->p128 += g.frame_ticks;
  s->vel[1] += g.frame_ticks * -0x600;
  if (!(0x40 < s->p128)) return;
  if (trail != nullptr) trail->ending = 1;
  g_trail.erase(s);
  if (Sprite* sp = shot_spawn_sprite(s, s->anim + 1)) {
    sprite_set_pos_v(sp, s->pos);
    sprite_set_fade(sp, 0x60, -4, 0);
    sp->blend = 2;
  }
  shot_kill(s);
}

// 0x8008dc00: a sparkle (anim 5) circling +0x118 at radius +0x130 (growing 3 a tick) while
// it rises 1 unit a tick; after 90 ticks it fades, after 150 it goes.
void rising_sparkle(Shot* s) {
  BattleGlobals& g = bg();
  const s32 c = bcos(static_cast<u32>(s->p128));
  s->pos[1] = s->point[1];
  s->pos[0] = s->point[0] + c * s->p130;
  s->pos[2] = s->point[2] + bsin(static_cast<u32>(s->p128)) * s->p130;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  s->point[1] -= g.frame_ticks << 12;
  s->p128 = (s->p128 + g.frame_ticks * 0x1E) & 0xFFF;
  s->p134 += g.frame_ticks;
  s->p130 += g.frame_ticks * 3;
  if (0x5A < s->p134) {
    s->colour_mode = 2;
    s->rgb[0] = static_cast<u8>(-0x80 - (s->p134 - 0x5A) * 2);
  }
  if (0x96 < s->p134) shot_kill(s);
}

void fairy_light(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  const int count = c->is_enemy != 0 ? 8 : 4;
  if (static_cast<u32>(sc->step - 3) < 6) {
    // Steps 3-8: a column of light over each standing unit of the side.
    for (int i = 0; i < count; ++i) {
      Unit* u = side_unit(sc, i);
      if (unit_is_down(u)) continue;
      s32 r = u->size[0] << 2;
      if (sc->s28 <= r) r = sc->s28;
      const s32 up[3] = {0, -0x10000, 0};
      const u32 a = scale_colour(0xC02000, 0x100 - sc->s2c);
      const u32 b = scale_colour(0xF0B060, 0x100 - sc->s2c);
      draw_beam(u->pos, up, a, b, sc->s24, r, r, 8);
    }
  }
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      spellcast_dim_begin(sc, c->is_enemy ^ 1);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, c->pos, 0xC00, 0x93, 0x800, 0x1E, 0x80, 5);
      ++sc->step;
      return;
    case 1:
      if (sc->data != nullptr) return;
      ++sc->step;
      sc->s3c = static_cast<s32>(bsfx_play_at(0x3B, c));
      sound_handle_pitch_up(static_cast<u32>(sc->s3c), 0x78);
      return;
    case 2: {
      // Two lights a tick round the caster up to power / 30 + 32 (max 48); then the camera
      // turns to another standing unit of the side.
      s32 n = sc->power / 0x1E + 0x20;
      if (0x30 < n) n = 0x30;
      if (n < sc->s20) {
        int standing = 0;
        for (int i = 0; i < (c->is_enemy != 0 ? 8 : 4); ++i)
          if (!unit_is_down(side_unit(sc, i))) ++standing;
        if (1 < standing) {
          Unit* u = nullptr;
          for (int tries = 0;;) {
            u = c->is_enemy == 0 ? static_cast<Unit*>(g.party_units[rng_range(4)])
                                 : static_cast<Unit*>(g.enemy_units[rng_range(8)]);
            if (u != c && !unit_is_down(u)) break;
            if (++tries >= 0x50) break;  // (80 picks at most)
          }
          if (!unit_is_down(u) && spellcast_leads_camera(sc)) {
            camera_reset(g.camera);
            const s32 yaw = rng_range(2) != 0 ? 0xE38 : 0x9C7;
            camera_move_to(g.camera, u->pos, yaw, 0xE3, 0xC00, 0xF0, 0x80, 5);
          }
        }
        spellcast_wait(sc, 0x1E);
      } else {
        for (int i = 0; i < g.frame_ticks << 1; ++i) {
          if (Shot* s = shot_spawn_callback_always(swirling_light)) {
            shot_place_at_unit(s, c, 0x80, 0x60, 0);
            s->point[0] = s->pos[0], s->point[1] = s->pos[1], s->point[2] = s->pos[2];
            s->p128 = static_cast<s32>(rng_range(0x1000));
            s->p12c = static_cast<s32>(rng_range(0x1000));
            s->p130 = 300;
            s->anim = 3;
            bobj_share_sprite(&sc->obj, s);
          }
          ++sc->s20;
        }
      }
      sc->s28 = 0x30;
      return;
    }
    case 3:
      // The columns grow; the chant fades.
      sc->s24 += g.frame_ticks * 0xC;
      sc->s28 += g.frame_ticks * 4;
      if (!(0x400 < sc->s24)) return;
      ++sc->step;
      sound_handle_fade_out(static_cast<u32>(sc->s3c), 0x1E, 0);
      return;
    case 4:
      // A fairy toward each standing unit of the side.
      sc->s24 += g.frame_ticks * 0xC;
      if (0x600 < sc->s24) sc->s24 = 0x600;
      bsfx_play_at(0x26, c);
      bsfx_play_at(0x22, c);
      for (int i = 0; i < count; ++i) {
        Unit* u = side_unit(sc, i);
        if (unit_is_down(u)) continue;
        Shot* s = shot_spawn_callback_always(fairy);
        if (s == nullptr) continue;
        g_trail.erase(s);
        s->pos[0] = u->pos[0] + 0x258000;
        s->pos[1] = u->pos[1] + u->size[1] * -0x4000 - 0x78000;
        s->pos[2] = u->pos[2];
        bobj_share_sprite(&sc->obj, s);
        s->vel[0] = static_cast<s32>(0xFFFEC000);
        s->vel[1] = 0xC000;
      }
      ++sc->step;
      return;
    case 5:
      sc->s24 += g.frame_ticks * 0xC;
      if (!(0x600 < sc->s24)) return;
      sc->s24 = 0x600;
      ++sc->step;
      bsfx_play_at(0x27, c);
      bsfx_play_at(0x34, c);
      screen_fade_create(0xC080FF, 0x20, 4, 4);
      return;
    case 6:
      // A ring of power / 30 + 2 (max 8) orbiting lights (anim 1) round each unit.
      for (int i = 0; i < count; ++i) {
        Unit* u = side_unit(sc, i);
        if (unit_is_down(u)) continue;
        s32 n = sc->power / 0x1E + 2;
        if (8 < n) n = 8;
        bsfx_play_at(0x28, u);
        bsfx_play_at(0x33, u);
        for (int k = 0, deg = 0; k < n; ++k, deg += 0x168) {
          Sprite* sp = spellcast_sprite_spawn(sc, 1);
          if (sp == nullptr) continue;
          sprite_set_pos_v(sp, u->pos);
          sprite_set_orbit(sp, ((deg / n) * 0x1000) / 0x168, 0, u->size[0] << 2, 0, u->size[0] << 2, 0x22, 0, 1);
          sprite_set_fade(sp, 0x80, -1, 0x3C);
        }
      }
      ++sc->step;
      return;
    case 7:
      // For 60 ticks power / 36 + 2 (max 4) sparkles a frame rise round each unit, with
      // floating motes (anim 7).
      for (int i = 0; i < count; ++i) {
        Unit* u = side_unit(sc, i);
        if (unit_is_down(u)) continue;
        s32 n = sc->power / 0x24 + 2;
        if (4 < n) n = 4;
        for (int k = 0; k < n; ++k) {
          if (Shot* s = shot_spawn_callback_always(rising_sparkle)) {
            s->pos[0] = u->pos[0], s->pos[1] = u->pos[1], s->pos[2] = u->pos[2];
            s->point[0] = s->pos[0], s->point[1] = s->pos[1], s->point[2] = s->pos[2];
            s->p128 = static_cast<s32>(rng_range(0x1000));
            s->p130 = rng_between(u->size[0] << 1, u->size[0] * 5);
            const s32 scale = rng_between(0x1000, 0x2000);
            s->scale_y = scale;
            s->scale_x = scale;
            s->anim = 5;
            bobj_share_sprite(&sc->obj, s);
          }
          if (Sprite* sp = spellcast_sprite_spawn(sc, 7)) {
            const s32 dx = rng_between_fixed(u->size[0] * -5, u->size[0] * 5);
            const s32 x = u->pos[0] + dx;
            const s32 dz = rng_between_fixed(u->size[2] * -3, u->size[2] * 3);
            sprite_set_pos(sp, x, u->pos[1], u->pos[2] + dz);
            sprite_set_fade(sp, 0x80, -1, 0x3C);
            sp->layer = 2;
            sp->vel[0] = rng_between_fixed(-2, 2);
            sp->accel[1] = -0xA0;
            const s32 scale = rng_between(0x1000, 0x2000);
            sp->scale_y = scale;
            sp->scale_x = scale;
          }
        }
      }
      sc->s20 += g.frame_ticks;
      if (!(0x3C < sc->s20)) return;
      ++sc->step;
      if (spellcast_leads_camera(sc)) camera_setup_overview(g.camera, 0x78, c->is_enemy);
      return;
    case 8:
      // The columns fade.
      sc->s2c += g.frame_ticks * 4;
      if (sc->s2c < 0x101) return;
      ++sc->step;
      return;
    case 9:
      // The heal.
      bsfx_play_at(0x15, c);
      bsfx_play_at(0x16, c);
      bsfx_play_at(0x17, c);
      for (int i = 0; i < count; ++i) {
        Unit* u = side_unit(sc, i);
        if (unit_is_down(u) || u->st()->hp == 0) continue;
        const s32 heal = -(c->st()->intel[2] * 2 + sc->power * 4 + 0x898);
        damage_popup_spawn(u, heal, static_cast<s32>(0xFFFFE000), 0);
        unit_damage_hp(u, heal);
      }
      ++sc->step;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      return;
    case 10:
      if (spellcast_effects_gone(sc) == 0 || shot_kind21_absent(rising_sparkle) == 0) return;
      ++sc->step;
      return;
    case 11:
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_fairy_light(SpellCast* sc) { fairy_light(sc); }

}  // namespace ss::game::battle
