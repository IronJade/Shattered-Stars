// Spell code module 6673c9f485 (entries 0x8fd / 0x8fe): Deep Freeze (spell 5). Snow falls
// over the field; on every unit of the other side within a radius of the spell's centre
// five waves of ice shards drop in a row (one wave every 0x8000 / 2000 ticks), then the
// shards shatter in a burst of ice and the units are hit (element 2), each perhaps frozen.
// Functions 0x8008cc00 (the handler) and 0x8008d984 (an ice shard, a kind-0x21 callback);
// the jump table of the handler's steps at 0x8008dbe4. Cast by Zand.

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
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The cast's scratch the original allocates on its first frame (0xc0 bytes at SpellCast
// +0x5c, never freed by the module): per target slot the shards spawned and the shards
// themselves, five waves each.
struct FreezeState {
  s32 spawned[8] = {};
  Shot* shards[8][5] = {};
};
std::unordered_map<const SpellCast*, FreezeState> g_state;

// A shard's afterimage (Shot +0x130 holds the pointer on the console; p130 is the flag).
std::unordered_map<const Shot*, Afterimage*> g_trail;

void end_trail(Shot* s) {
  const auto it = g_trail.find(s);
  if (it == g_trail.end()) return;
  if (it->second != nullptr) it->second->ending = 1;
  g_trail.erase(it);
}

// 0x8008d984: an ice shard (Shot +0x114 its unit, +0x118 the point it falls to, +0x10c
// the combo kind: 0 a unit, 2 the combo point, 3 the combo point and then its unit). It
// falls at 0x18 a tick with a 6-copy afterimage; at the point: (unless kind 2) it snaps
// there with a shake and a sound; kind 0 then stays and brightens (+0x134 1), the others
// are removed; kind 3 instead takes its unit as the next point.
void ice_shard(Shot* s) {
  BattleGlobals& g = bg();
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  if (s->p130 == 0) {
    g_trail[s] = afterimage_create(s, 6, 0x808080, 2);
    s->p130 = 1;
  }
  if (s->p134 != 0) {
    s->colour_mode = 2;
    s32 v = s->rgb[0] + g.frame_ticks * 4;
    if (0xFF < v) v = 0xFF;
    s->rgb[0] = static_cast<u8>(v);
    return;
  }
  const Vec4 d = vec_direction(s->pos, s->point);
  s->vel[0] = d.x, s->vel[1] = d.y, s->vel[2] = d.z, s->extra34 = d.w;
  const s32 dist = vec_distance_fx(s->pos, s->point);
  s->vel[0] *= 0x18;
  s->vel[1] *= 0x18;
  s->vel[2] *= 0x18;
  bobj_move(s, s->vel);
  if (!(dist < g.frame_ticks * 0x30000)) return;
  if (s->aux == 3) {
    const Unit* u = s->target;
    s->p12c = 0;
    s->aux = 0;
    s->point[0] = u->pos[0];
    s->point[1] = u->pos[1];
    s->point[2] = u->pos[2];
    return;
  }
  if (s->aux != 2) {
    s->pos[0] = s->point[0];
    s->pos[1] = s->point[1];
    s->pos[2] = s->point[2];
    screen_shake2(3, 1, 2, 2, 10);
    bsfx_play_at(0x28, s);
  }
  s->rgb[0] = 0x80;
  if (s->aux == 0) {
    s->p134 = 1;
    return;
  }
  end_trail(s);
  shot_kill(s);
}

// 0x8008cc00: the handler. Power = INT / 4 + power / 4 + 320; the shards' radius power / 2
// + 0x300 (at most 0x800).
void deep_freeze(SpellCast* sc) {
  BattleGlobals& g = bg();
  FreezeState& st = g_state[sc];
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;  // (the original copies an uninitialised stack word)
  s32 i4 = sc->caster->st()->intel[2];
  if (i4 < 0) i4 += 3;
  s32 p4 = sc->power;
  if (p4 < 0) p4 += 3;
  sc->eff_power = (i4 >> 2) + (p4 >> 2) + 0x140;
  s32 radius = sc->power / 2 + 0x300;
  if (0x800 < radius) radius = 0x800;
  if ((g.flags & 4) != 0) return;

  // Snow over the field while the data has loaded and the shards still fall.
  if (sc->data == nullptr && sc->step < 4) {
    sc->s20 += g.frame_ticks * 20000 + sc->power;
    while (0x7FFF < sc->s20) {
      sc->s20 -= 0x8000;
      Sprite* sp = sprite_spawn_from(rng_between(8, 0x16), 0, &sc->obj, nullptr);
      if (sp == nullptr) continue;
      s32 at[4] = {0, 0, 0, 0};
      stage_random_point(at, 1);
      at[1] = rng_between(static_cast<s32>(0xFFD00000), 0x20000);
      sprite_set_pos_v(sp, at);
      sprite_set_fade(sp, 0x80, -2, 0x3C);
      sp->vel[0] = rng_between(static_cast<s32>(0xFFFF0000), 0x10000);
      sp->vel[1] = rng_between(static_cast<s32>(0xFFFFC000), 0x4000);
      sp->vel[2] = rng_between(static_cast<s32>(0xFFFF0000), 0x10000);
      sp->roll_speed = static_cast<s16>(rng_between(-0x71, 0x71));
    }
  }

  // The units of the other side within the radius of the centre (of the caster's centre
  // when the clash turns the spell on its own side); none for combo kinds 1 and 2.
  Unit* units[8] = {};
  int n = 0;
  s32 point[4] = {0, 0, 0, 0};
  int side = 0;
  if (1 < static_cast<u8>(sc->combo - 1)) {
    side = sc->combo == 3 ? sc->caster->is_enemy ^ 1 : sc->caster->is_enemy;
    const int count = side != 0 ? 4 : 8;
    point[0] = sc->centre[0], point[1] = sc->centre[1], point[2] = sc->centre[2], point[3] = sc->centre[3];
    if (sc->combo == 3) {
      const Unit* c = sc->caster;
      point[0] = c->pos[0];
      point[1] = c->pos[1] + c->size[1] * -0x2000;
      point[2] = c->pos[2];
    }
    for (int i = 0; i < count; ++i) {
      Unit* u = side == 0 ? static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)])
                          : static_cast<Unit*>(g.party_units[static_cast<size_t>(i)]);
      if (unit_is_down(u)) continue;
      if (vec_distance_xz_fx(u->pos, point) < radius << 12) units[n++] = u;
    }
  }

  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      battle_flash_set(sc, 0x30, 0x30, 0x90);
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, point, 0xC00, 0x71, 0x800, 0x1E, 0x80, 5);
      }
      spellcast_wait(sc, 0x1E);
      st = FreezeState{};  // (the original clears the counts; the shards are zero from the allocation)
      sc->s38 = static_cast<s32>(bsfx_play_ui(0x2D));
      return;
    case 1: {
      // A wave of shards (+0x28 the wave: 0-3 in a row 0x100 apart from x - 0x200, 4 on
      // the unit), 0x200 above the ground over each unit.
      sc->s24 += g.frame_ticks * 2000;
      if (sc->s24 < 0x8000) return;
      sc->s24 = 0;
      const int wave = sc->s28;
      for (int i = 0; i < n; ++i) {
        Shot* s = shot_spawn_callback_always(ice_shard);
        st.shards[i][wave] = s;
        if (s == nullptr) continue;
        ++st.spawned[i];
        const Unit* u = units[i];
        s->pos[0] = u->pos[0] + wave * 0x100000 - 0x200000;
        s->pos[1] = static_cast<s32>(0xFFE00000);
        s->layer = 2;
        s->pos[2] = u->pos[2];
        if (wave == 4) {
          s->layer = 3;
          s->pos[0] = u->pos[0];
        }
        s32 combo_at[4] = {0, 0, 0, 0};
        const int combo = spellcast_combo_point(sc, combo_at);
        if (combo == 0) {
          s->point[0] = u->pos[0], s->point[1] = u->pos[1], s->point[2] = u->pos[2];
        } else {
          s->point[0] = combo_at[0], s->point[1] = combo_at[1], s->point[2] = combo_at[2];
        }
        s->anim = static_cast<u16>(wave + 2);
        bobj_share_sprite(&sc->obj, s);
        s->p128 = radius;
        s->aux = static_cast<s16>(combo);
        s->target = units[i];
      }
      ++sc->s28;
      if (sc->s28 < 5) return;
      ++sc->step;
      return;
    }
    case 2: {
      // On once every shard spawned for each unit still stands (+0x134) or still falls.
      sc->s30 += g.frame_ticks;
      int i = 0;
      for (; i < 8; ++i) {
        int there = 0;
        for (int k = 0; k < 5; ++k) {
          const Shot* s = st.shards[i][k];
          if (s != nullptr && (s->p134 != 0 || (s->kind == 0x21 && s->handler == ice_shard))) ++there;
        }
        if (there < st.spawned[i]) break;
      }
      if (i != 8) return;
      s32 combo_at[4] = {0, 0, 0, 0};
      const int combo = spellcast_combo_point(sc, combo_at);
      sc->step += (combo == 0 || combo == 3) ? 1 : 2;
      return;
    }
    case 3: {
      // The shards shatter: two flashes and power / 30 + 8 (at most 16) ice bits on each unit.
      for (int i = 0; i < 8; ++i) {
        for (int k = 0; k < 5; ++k) {
          Shot* s = st.shards[i][k];
          if (s == nullptr) continue;
          end_trail(s);
          shot_kill(s);
        }
        if (units[i] == nullptr) continue;
        s32 at[4] = {0, 0, 0, 0};
        const int combo = spellcast_combo_point(sc, at);
        if (combo == 0 || combo == 3) {
          const Unit* u = units[i];
          at[0] = u->pos[0], at[1] = u->pos[1], at[2] = u->pos[2], at[3] = u->pos_w;
        }
        if (Sprite* sp = sprite_spawn_from(1, 0, &sc->obj, nullptr)) {
          sprite_set_pos_v(sp, at);
          sp->layer = 2;
          sprite_set_fade(sp, 0xFF, -4, 0x3C);
          sp->scale_vel[0] = -8;
          sp->scale_vel[1] = -8;
        }
        if (Sprite* sp = sprite_spawn_from(2, 0, &sc->obj, nullptr)) {
          sprite_set_pos_v(sp, at);
          sp->layer = -1;
          sprite_set_fade(sp, 0xFF, -4, 0x3C);
          sp->scale_vel[0] = -8;
          sp->scale_vel[1] = -8;
        }
        s32 bits = sc->power / 0x1E + 8;
        if (0x10 < bits) bits = 0x10;
        for (int k = 0; k < bits; ++k) {
          int anim = static_cast<int>(rng_range(3));
          if (anim != 2) anim += 8;
          Sprite* sp = sprite_spawn_from(anim + 1, 0, g.sprite_bank_fx.get(), nullptr);
          if (sp == nullptr) continue;
          sprite_set_pos_v(sp, at);
          sprite_set_fade(sp, 0x80, -2, 0x1E);
          sp->vel[0] = rng_between(static_cast<s32>(0xFFFF8000), 0x8000);
          sp->vel[1] = rng_between(static_cast<s32>(0xFFFF8000), 0x1000);
          sp->layer = 1;
        }
      }
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, point, 0xC00, 0x155, 0x1000, 0x5A, 0x80, 5);
      }
      spellcast_wait(sc, 0x3C);
      return;
    }
    case 4:
      sound_handle_release(static_cast<u32>(sc->s38));
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      ++sc->step;
      return;
    case 5:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 6:
      // The hit: each unit may freeze (resist roll 1 at power / 50 + 25: status bit 3), then
      // damage through a temporary Shot of the caster's (element 2, knockback 0x10).
      if (1 < static_cast<u8>(sc->combo - 1)) {
        if (sc->combo == 4) {
          if (const SpellCast* other = spellcast_slot(sc->slot ^ 1)) sc->eff_power += other->eff_power;
        }
        if (n != 0) {
          const s32 power = sc->power;
          s32 at[4] = {0, 0, 0, 0};
          const int combo = spellcast_combo_point(sc, at);
          if (combo == 0 || combo == 3) {
            for (int i = 0; i < n; ++i)
              if (units[i]->resist_roll(1, power / 0x32 + 0x19) != 0) units[i]->status_bits |= 8;
          }
          spellcast_damage_units(sc, n, units, sc->eff_power, 2, 0x10, side);
        }
      }
      sc->finished = 1;
      ++sc->step;
      g_state.erase(sc);
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_deep_freeze(SpellCast* sc) { deep_freeze(sc); }

}  // namespace ss::game::battle
