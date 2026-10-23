// Spell code module fb43fdecea (entries 0x91e / 0x91f): Earth Grave (spell 0x18): for 160
// ticks rock spikes (the data's two models) burst out of the ground under random units of
// the other side, throwing up debris; then every unit there is hit (element 1). Functions
// 0x8008cc00 (the handler) and 0x8008d388 (a spike, a kind-0x21 callback).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the spike timer, s24 the ticks of step 2; obj the data's
// sprite set (anim 1 the dust).

// A spike's model (Shot +0x128 holds the pointer on the console).
std::unordered_map<const Shot*, const u8*> g_model;

const s32 kSpikeScale[3] = {12000, 12000, 12000};

// A random unit of the side (0 the monsters, else the party).
Unit* random_unit(int side) {
  const BattleGlobals& g = bg();
  return side == 0 ? static_cast<Unit*>(g.enemy_units[rng_range(8)]) : static_cast<Unit*>(g.party_units[rng_range(4)]);
}

// 0x8008d388: a spike (its model, scale 12000 / 0x1000, CLUT 0x7dc5, tpage 0x1a) rising 30
// units a tick from 400 below; at the ground (+0x12c 1) it shakes the screen and its unit
// (+0x114), holds 16 ticks, then (2) sinks back and goes.
void spike(Shot* s) {
  BattleGlobals& g = bg();
  s32 orient[3] = {0, 0, 0};
  lights_build_oriented(kZeroVector);
  obj_set_matrix(s->pos, orient, kSpikeScale);
  const auto it = g_model.find(s);
  if (it != g_model.end() && it->second != nullptr) model_draw(it->second, 0x7DC5, 0x1A, 3);
  if ((g.flags & 0xC) != 0) return;
  const s32 ground = bobj_ground_height(s);
  if (s->p12c == 0) {
    s->vel[1] = static_cast<s32>(0xFFFE2000);
    bobj_move(s, s->vel);
    if (s->pos[1] < ground) {
      s->pos[1] = ground;
      s->p12c = 1;
      s->target->shake_amp = 10;
      screen_shake(4, 3, 2, 2, 8);
      bsfx_play_at(0x4E, s);
    }
  } else if (s->p12c == 1) {
    s->p130 += g.frame_ticks;
    if (0xF < s->p130) s->p12c = 2;
  } else if (s->p12c == 2) {
    s->vel[1] = 0x1E000;
    bobj_move(s, s->vel);
    if (0x190000 < s->pos[1]) {
      g_model.erase(s);
      shot_kill(s);
    }
  }
}

void earth_grave(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;
  const Unit* at_unit = sc->combo == 3 ? c : t;
  const s32 at[3] = {at_unit->pos[0], bobj_ground_height(at_unit), at_unit->pos[2]};
  s32 i4 = c->st()->intel[2];
  if (i4 < 0) i4 += 3;
  s32 p4 = sc->power;
  if (p4 < 0) p4 += 3;
  sc->eff_power = (i4 >> 2) + (p4 >> 2) + 0x15E;
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x50, 0x50, 0x50);
      if (spellcast_leads_camera(sc)) {
        camera_reset(g.camera);
        const s32 yaw = rng_range(2) != 0 ? 0xCE3 : 0xB1C;
        camera_move_to(g.camera, at, yaw, 0x18E, 0x800, 0x1E, 0x80, 5);
      }
      spellcast_wait(sc, 0x1E);
      return;
    case 1: {
      // The data loaded: the camera to another standing unit of the side hit.
      if (sc->data != nullptr) return;
      ++sc->step;
      int side = c->is_enemy;
      const Unit* skip = t;
      if (sc->combo == 3) {
        side ^= 1;
        skip = c;
      }
      int standing = 0;
      for (int i = 0; i < (side == 0 ? 8 : 4); ++i) {
        const Unit* u = side == 0 ? static_cast<const Unit*>(g.enemy_units[static_cast<size_t>(i)])
                                  : static_cast<const Unit*>(g.party_units[static_cast<size_t>(i)]);
        if (!unit_is_down(u)) ++standing;
      }
      if (1 < standing) {
        Unit* u = nullptr;
        for (int tries = 0;;) {
          u = random_unit(side);
          if (u != skip && !unit_is_down(u)) break;
          if (++tries >= 0x32) break;  // (50 picks at most)
        }
        if (!unit_is_down(u) && spellcast_leads_camera(sc)) {
          const s32 yaw = rng_range(2) != 0 ? 0xCE3 : 0xB1C;
          camera_move_to(g.camera, u->pos, yaw, 0xE3, 0xC00, 0xA0, 0x80, 5);
        }
      }
      return;
    }
    case 2: {
      // Every 12 - power / 30 ticks (at least 6) a spike under a random standing unit of the
      // side hit, with debris (fx anims 0x1b / 0x1c) and dust (anim 1); 160 ticks.
      s32 every = 0xC - sc->power / 0x1E;
      if (every < 6) every = 6;
      sc->s20 += g.frame_ticks;
      while (every <= sc->s20) {
        sc->s20 -= every;
        int side = c->is_enemy;
        if (sc->combo == 3) side ^= 1;
        Unit* u = nullptr;
        for (int tries = 0;;) {
          u = random_unit(side);
          if (!unit_is_down(u) || ++tries >= 0x14) break;  // (20 picks at most)
        }
        if (unit_is_down(u)) continue;
        if (Shot* s = shot_spawn_callback_always(spike)) {
          g_model[s] = spellcast_model(sc, static_cast<int>(rng_range(2)));
          s->target = u;
          const s32 dx = rng_between_fixed(u->size[0] * -2 - 200, u->size[0] * 2 + 200);
          s->pos[1] = 0x190000;
          s->pos[0] = u->pos[0] + dx;
          const s32 dz = rng_between_fixed(-300, 300);
          s->pos[2] = u->pos[2] + dz;
          bsfx_play_at(0x34, s);
        }
        for (int i = 0; i < 4; ++i) {
          Sprite* sp = sprite_spawn_from(static_cast<int>(rng_range(2)) + 0x1B, 0x10, g.sprite_bank_fx.get(), nullptr);
          if (sp == nullptr) continue;
          const s32 p[3] = {u->pos[0], bobj_ground_height(u), u->pos[2]};
          sprite_set_pos_v(sp, p);
          sp->vel[0] = rng_between(-0x10000, 0x10000);
          sp->vel[1] = -0xC000 - static_cast<s32>(rng_range(0x4000));
          sp->accel[1] = 0x1000;
          sp->roll_speed = 0x100;
          sp->layer = 3;
        }
        for (int i = 0; i < 6; ++i) {
          Sprite* sp = spellcast_sprite_spawn(sc, 1);
          if (sp == nullptr) continue;
          const s32 p[3] = {u->pos[0], bobj_ground_height(u), u->pos[2]};
          sprite_set_pos_v(sp, p);
          const Vec4 v = vec_random_in_disc_xz(kZeroVector, 0x14);
          sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
          sprite_set_fade(sp, 0x80, -2, 8);
          const s32 scale = rng_between(0x2000, 0x4000);
          sp->scale_y = scale;
          sp->scale_x = scale;
          sp->layer = 3;
        }
      }
      sc->s24 += g.frame_ticks;
      if (sc->s24 < 0xA1) return;
      ++sc->step;
      return;
    }
    case 3:
      if (spellcast_effects_gone(sc) == 0 || shot_kind21_absent(spike) == 0) return;
      ++sc->step;
      return;
    case 4:
      spell_hit_all(sc, 1, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_earth_grave(SpellCast* sc) { earth_grave(sc); }

}  // namespace ss::game::battle
