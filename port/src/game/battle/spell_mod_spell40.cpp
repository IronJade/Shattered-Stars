// Spell code module e75959284f (entry 0x948): unnamed (spell 0x40): a double of the caster
// (the data's sprite set, its own animation, with an afterimage when the power is not 0)
// swings three times in front of it; then the ground erupts round the double (power-scaled
// debris sprites with shakes) for 140 ticks and every standing monster takes a physical hit
// (unit_attack_targets, power * 0x80 + 0x180). One function at 0x8008cc00 (block 0, 1880 bytes;
// its step jump table at 0x8008d344).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the double's attack box was on last frame, s24 its swings,
// s28 the slash sprite spawned, s2c the eruption begun, s34 the eruption's ticks, s38 the fade
// begun, s3c the double's afterimage (set: the pointer is kept in g_trail); obj the double.
std::unordered_map<const SpellCast*, Afterimage*> g_trail;

Afterimage* trail_of(const SpellCast* sc) {
  if (sc->s3c == 0) return nullptr;
  const auto it = g_trail.find(sc);
  return it != g_trail.end() ? it->second : nullptr;
}

void spell40(SpellCast* sc) {
  BattleGlobals& g = bg();
  if (sc->data != nullptr) return;
  Unit* c = sc->caster;
  BObj* o = &sc->obj;
  if (static_cast<u32>(sc->step - 2) < 2) {
    // The double and its shadow (the cel's widest half width * 4 + 32).
    bobj_project(o);
    bobj_anim_draw(o, g.flags & 0xC);
    s32 a = o->ext_x1, b = o->ext_x0;
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    if (a < b) a = b;
    const s32 w = a * 4 + 0x20;
    bobj_draw_shadow(o, -w, w, -w, w, 0xFFFFFFFF);
  }
  if (Afterimage* trail = trail_of(sc)) trail->next_kind = 1;
  if ((g.flags & 4) != 0 || spellcast_timer_tick(sc) != 0) return;
  // The point 0x80 in front of the caster at chest height.
  s32 front[4] = {};
  front[0] = bobj_front_x(c, 0x80);
  front[1] = c->pos[1] + c->size[1] * -0x2000;
  front[2] = c->pos[2];
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      camera_move_to(g.camera, c->pos, 0xC00, 0xE3, 0x800, 0x19, 0x100, 5);
      spellcast_wait(sc, 0x12);
      return;
    case 1:
      // The double takes the caster's place and facing (anim 0).
      o->anim = 0;
      ++sc->step;
      o->pos[0] = c->pos[0];
      o->pos[1] = c->pos[1];
      o->pos[2] = c->pos[2];
      o->pos_w = c->pos_w;
      o->facing[0] = c->facing[0];
      o->facing[1] = c->facing[1];
      o->facing[2] = c->facing[2];
      if (sc->power != 0) {
        g_trail[sc] = afterimage_create(o, 8, 0x507020, 1);
        sc->s3c = 1;
      }
      return;
    case 2: {
      // Each time the double's attack box (box_mask & 0xc) comes on: a shout and sfx 0x34;
      // past frame 5 a slash sprite (anim 4) in front; after the third swing the eruption.
      if (sc->s20 == 0) {
        if ((o->box_mask & 0xC) != 0) {
          sc->s20 = 1;
          // (A monster caster would run slot 43 of its own vtable here; the port only draws
          // the shout's random numbers for it.)
          if (PartyUnit* p = dynamic_cast<PartyUnit*>(c)) {
            pc_voice_attack_shout(p);
          } else if ((rng_next() & 0x7FFF) >= 0x4000 && (rng_next() & 0x7FFF) >= 24000) {
            rng_next();
          }
          bsfx_play_at(0x34, c);
          ++sc->s24;
        }
      } else if ((o->box_mask & 0xC) == 0) {
        sc->s20 = 0;
      }
      if (5 < o->frame && sc->s28 == 0) {
        sc->s28 = 1;
        if (Sprite* sp = sprite_spawn_from(4, 1, o, nullptr)) {
          sprite_set_pos_v(sp, front);
          sp->layer = 2;
        }
      }
      if (!(2 < sc->s24)) return;
      if (sc->s2c == 0) {
        camera_move_to(g.camera, c->pos, 0xC00, 0x1C7, 6000, 300, 0x100, 5);
        sc->s2c = 1;
        for (int i = 0; i < 0x14; ++i) {
          Sprite* sp = sprite_spawn_from(2, 0, o, nullptr);
          if (sp == nullptr) continue;
          sprite_set_pos_v(sp, front);
          sprite_set_fade(sp, 0x80, -1, 0x3C);
          sp->roll = static_cast<s16>(rng_range(0x1000));
          sp->roll_speed = static_cast<s16>(rng_between(-0x1E, 0x1E));
          sp->layer = 8;
          sp->scale_y = 0x2000;
          sp->scale_x = 0x2000;
        }
        bsfx_play_at(0x28, c);
        bsfx_play_at(0x29, c);
      }
      // power * 4 + 4 debris sprites a frame round the double, each with a shake.
      for (int i = 0; i < sc->power * 4 + 4; ++i) {
        Sprite* sp = sprite_spawn_from(static_cast<int>(rng_range(2)) * 2 + 3, 1, o, nullptr);
        if (sp == nullptr) continue;
        const s32 dx = rng_between_fixed(-0x708, 0x708);
        const s32 dy = rng_between_fixed(-300, 0x4B0);
        const s32 dz = rng_between_fixed(-0x4B0, 0x4B0);
        const s32 at[3] = {o->pos[0] + dx, o->pos[1] - dy, o->pos[2] + dz};
        sprite_set_pos_v(sp, at);
        const s32 scale = rng_between(0x2000, 32000);
        sp->scale_y = scale;
        sp->scale_x = scale;
        sp->facing[0] = rng_range(2) != 0 ? -0x1000 : 0x1000;
        if (static_cast<u32>(rng_next() & 0x7FFF) < 3000) bsfx_play_at_pos(0x50, at);
        screen_shake(6, 6, 4, 4, 10);
      }
      sc->s34 += g.frame_ticks;
      if (0x5A < sc->s34 && sc->s38 == 0) {
        sc->s38 = 1;
        screen_fade_create(0, 0x20, -4, 0x1E);  // (0x8001fd94 allocates its 0x1c bytes)
      }
      if (0x8C < sc->s34) {
        ++sc->step;
        battle_flash_set(sc, 0x80, 0x80, 0x80);
      }
      return;
    }
    case 3:
      if (spellcast_effects_gone(sc) != 0) ++sc->step;
      if (Afterimage* trail = trail_of(sc)) {
        trail->ending = 1;
        g_trail.erase(sc);
        sc->s3c = 0;
      }
      return;
    case 4: {
      // The hit: every standing monster (at its chest), knocked along x away from the caster.
      Unit* list[8] = {};
      Vec4 points[8] = {};
      int n = 0;
      for (int i = 0; i < 8; ++i) {
        Unit* u = g.enemy_units[static_cast<size_t>(i)];
        if (unit_is_down(u)) continue;
        list[n] = u;
        points[n].x = u->pos[0];
        points[n].y = u->pos[1] + u->size[1] * -0x2000;
        points[n].z = u->pos[2];
        ++n;
      }
      if (n != 0) {
        unit_attack_targets(c, n, list, points, sc->power * 0x80 + 0x180, 9999, 0);
        c->attack_recovery = 0;
      }
      sc->finished = 1;
      ++sc->step;
      g_trail.erase(sc);
      return;
    }
    default: return;
  }
}

}  // namespace

void spell_mod_spell40(SpellCast* sc) { spell40(sc); }

}  // namespace ss::game::battle
