// Spell code module f8c3b94995 (entries 0x939-0x93d): Fairy Heal (spell 0x31): a column of
// light rises over the target, fairies spiral down round it leaving sparkles, light streaks
// rise from the ground, and the target is healed INT * 2 + power * 4 + 2800 (at most 9999).
// Functions 0x8008cc00 (the handler) and 0x8008d4f4 (a fairy, a kind-0x21 callback); the
// jump table of the handler's steps at 0x8008d738.

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
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

// The SpellCast's fields: s20 the spawn timer (steps 3 / 4), s24 the column's length (to
// 0x500), s28 its radius (capped at 5 times the target's width), s2c its fade (step 6),
// s30 / s34 the ticks of steps 3 / 4; obj the data's sprite set (anim 3 the rings and
// sparkles; the fairies share it).

// A fairy's afterimage (Shot +0x134 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

// 0x8008d4f4: a fairy (its anim set at spawn) circling its unit (+0x114) at 6 times its
// width (angle +0x128 turning 0x14 a tick), facing its motion, sinking 6 a tick from 0x258
// above it with an 8-copy afterimage and a sparkle (anim 2) every 4 ticks (+0x130); at the
// unit's chest a fading sprite (its anim + 1) and it goes.
void fairy(Shot* s) {
  BattleGlobals& g = bg();
  const Unit* u = s->target;
  const s32 r = u->size[0] * 6;
  s->facing[0] = s->pos[0];
  s->facing[2] = s->pos[2];
  s->pos[0] = u->pos[0] + bcos(static_cast<u32>(s->p128)) * r;
  s->pos[2] = u->pos[2] + bsin(static_cast<u32>(s->p128)) * r;
  s->facing[0] = s->pos[0] - s->facing[0];
  s->facing[2] = s->pos[2] - s->facing[2];
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr) trail = afterimage_create(s, 8, 0x2050, 2);
  if (trail != nullptr) trail->next_kind = 1;
  s->p130 += g.frame_ticks;
  if (3 < s->p130) {
    s->p130 += -4;
    if (Sprite* sp = shot_spawn_sprite_at(s, 2, 1)) {
      sprite_set_pos_v(sp, s->pos);
      sp->vel[1] = 0x1000;
    }
  }
  s->pos[1] += g.frame_ticks * 0x6000;
  s->p128 = (s->p128 + g.frame_ticks * 0x14) & 0xFFF;
  if (u->pos[1] + u->size[1] * -0x2000 <= s->pos[1]) {
    if (Sprite* sp = shot_spawn_sprite(s, s->anim + 1)) {
      sprite_set_pos_v(sp, s->pos);
      sp->layer = 2;
      sprite_set_fade(sp, 0x80, -4, 0);
      sp->blend = 0x82;
    }
    if (trail != nullptr) trail->ending = 1;
    g_trail.erase(s);
    shot_kill(s);
  }
}

void fairy_heal(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* t = sc->target;
  if (static_cast<u32>(sc->step - 3) < 4) {
    // The column: an 8-sided beam from 0x500 above the target downwards.
    s32 r = t->size[0] * 5;
    if (!(r < sc->s28)) r = sc->s28;
    const s32 from[3] = {t->pos[0], t->pos[1] + -0x500000, t->pos[2]};
    const s32 dir[3] = {0, 0x10000, 0};
    const u32 a = scale_colour(0x80A0C0, 0x100 - sc->s2c);
    const u32 b = scale_colour(0x204050, 0x100 - sc->s2c);
    draw_beam(from, dir, a, b, sc->s24, r, r, 8);
  }
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      spellcast_dim_begin(sc, sc->caster->is_enemy ^ 1);
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0xAAA : 0xD55;
        camera_move_to(g.camera, t->pos, yaw, 0x18E, 0xA00, 0x1E, 0x80, 5);
      }
      ++sc->step;
      return;
    case 1: {
      // Two rings of anim 3 spreading on the ground under the target.
      if (sc->data != nullptr) return;
      s32 at[3] = {t->pos[0], bobj_ground_height(t), t->pos[2]};
      if (Shot* r = effect_spawn_sparks_at(at, 10, 0x30, 0xC0, 0, 0xB4, 1, 2, t->size[0] * 6, 0)) {
        r->vel[0] = 0x22;
        r->anim = 3;
        bobj_share_sprite(&sc->obj, r);
      }
      at[1] = bobj_ground_height(t);
      if (Shot* r = effect_spawn_sparks_at(at, 10, 0x20, 0x80, 0, 0xB4, 1, 2, t->size[0] * 7, 0)) {
        r->vel[0] = 0x22;
        r->anim = 3;
        bobj_share_sprite(&sc->obj, r);
      }
      bsfx_play_at(0x34, t);
      bsfx_play_at(0x21, t);
      ++sc->step;
      return;
    }
    case 2: {
      // The column grows to 0x280; a falling hum.
      sc->s24 += g.frame_ticks * 0x10;
      sc->s28 += g.frame_ticks * 4;
      if (sc->s24 < 0x281) return;
      ++sc->step;
      const u32 h = bsfx_play_at(0x2E, t);
      sound_handle_pitch_down(h, 0x78);
      sound_handle_fade_out(h, 0xB4, 0);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, t->pos, 0xC00, 0x93, 0x800, 0xF0, 0x80, 5);
      return;
    }
    case 3: {
      // For 90 ticks a fairy every 12 - power / 30 (at least 6) ticks from 0x258 above.
      sc->s24 += g.frame_ticks * 0x10;
      sc->s28 += g.frame_ticks * 4;
      if (0x500 < sc->s24) sc->s24 = 0x500;
      s32 every = 0xC - sc->power / 0x1E;
      if (every < 6) every = 6;
      sc->s20 += g.frame_ticks;
      if (every <= sc->s20) {
        sc->s20 -= every;
        if (Shot* s = shot_spawn_callback_always(fairy)) {
          g_trail.erase(s);
          s->target = t;
          s->pos[0] = t->pos[0];
          s->pos[1] = t->pos[1] + -0x258000;
          s->pos[2] = t->pos[2];
          s->p128 = static_cast<s32>(rng_range(0x1000));
          s->anim = 0;
          bsfx_play_at(0x30, s);
          bobj_share_sprite(&sc->obj, s);
        }
      }
      sc->s30 += g.frame_ticks;
      if (sc->s30 < 0x5B) return;
      ++sc->step;
      return;
    }
    case 4: {
      // For 90 ticks (then wait 60): three streaks a frame rising from the ground round the
      // target, and every 6 - power / 30 (at least 2) ticks sfx 0x3a and three sparkles.
      sc->s34 += g.frame_ticks;
      if (0x5A < sc->s34) spellcast_wait(sc, 0x3C);
      for (int i = 0; i < 3; ++i) {
        const s32 at[3] = {t->pos[0], bobj_ground_height(t), t->pos[2]};
        const Vec4 p = vec_random_in_disc_xz(at, t->size[0] << 2);
        const s32 top[4] = {p.x, static_cast<s32>(0xFFC40000), p.z, p.w};
        particle_spawn_homing_line(p.data(), top, 0x804020, 0xFF8040, 0xB4, 0x4000, 0x1000);
      }
      s32 every = 6 - sc->power / 0x1E;
      if (every < 2) every = 2;
      sc->s20 += g.frame_ticks;
      if (sc->s20 < every) return;
      sc->s20 -= every;
      bsfx_play_at(0x3A, t);
      for (int i = 0; i < 3; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, 3);
        if (sp == nullptr) continue;
        const s32 dx = rng_between_fixed(t->size[0] * -6, t->size[0] * 6);
        const s32 dz = rng_between_fixed(t->size[2] * -4, t->size[2] << 2);
        sprite_set_pos(sp, t->pos[0] + dx, t->pos[1], t->pos[2] + dz);
        sprite_set_fade(sp, 0x80, -2, 0x32);
        sp->layer = 2;
        const s32 scale = rng_between(0x1000, 0x2000);
        sp->scale_y = scale;
        sp->scale_x = scale;
        sp->accel[1] = -0xE0;
      }
      return;
    }
    case 5:
      // The heal.
      bsfx_play_at(0x13, t);
      bsfx_play_at(0x16, t);
      if (t->st()->hp != 0) {
        s32 heal = -(sc->caster->st()->intel[2] * 2 + sc->power * 4 + 0xAF0);
        if (heal < -9999) heal = -9999;
        damage_popup_spawn(t, heal, static_cast<s32>(0xFFFFE000), 0);
        unit_damage_hp(t, heal);
      }
      ++sc->step;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      return;
    case 6:
      // The column fades; on when the fairies and every effect are gone.
      sc->s2c += g.frame_ticks * 4;
      if (0x100 < sc->s2c) sc->s2c = 0x100;
      if (spellcast_effects_gone(sc) == 0 || shot_kind21_absent(fairy) == 0) return;
      ++sc->step;
      return;
    case 7:
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_fairy_heal(SpellCast* sc) { fairy_heal(sc); }

}  // namespace ss::game::battle
