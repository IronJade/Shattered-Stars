// Spell code module 95b9fdccfa (entries 0x933-0x936): Cure All (spell 0x32): a light comes
// down over the field and every standing unit of the caster's side is healed INT + power +
// 1200. Functions 0x8008cc00 (the handler) and 0x8008d294 (the descending light, a kind-0x21
// callback).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/killer.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The light's afterimage (Shot +0x130 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

// 0x8008d294: the light (the cast's sprite, anim 0, grey +0x128) sinking 0x800 a tick
// while it grows to 1.5; it brightens (+0x12c 0), holds 120 ticks with an afterimage (1),
// then fades (2) and goes; a sparkle every 8 ticks round it.
void descending_light(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 0;
  s->colour_mode = 2;
  s->rgb[0] = static_cast<u8>(s->p128);
  bobj_project(s);
  bobj_anim_draw(s, 0);
  if ((g.flags & 0xC) != 0) return;
  s->scale_x += g.frame_ticks * 0x100;
  if (0x1800 < s->scale_x) s->scale_x = 0x1800;
  s->vel[1] = static_cast<s32>(0xFFFFF800);
  s->scale_y = s->scale_x;
  bobj_move(s, s->vel);
  Afterimage*& trail = g_trail[s];
  if (s->p12c == 1 && trail == nullptr) trail = afterimage_create(s, 4, 0x802000, 0);
  if (trail != nullptr) trail->next_kind = 1;
  s->p134 += g.frame_ticks;
  if (7 < s->p134) {
    s->p134 -= 8;
    if (Sprite* sp = shot_spawn_sprite(s, 3)) {
      const s32 dx = rng_between_fixed(-0x168, 0x168);
      const s32 dy = rng_between_fixed(-0x180, 0x280);
      const s32 dz = rng_between_fixed(-0x100, 0x100);
      sprite_set_pos(sp, s->pos[0] + dx, s->pos[1] + dy, s->pos[2] + dz);
      sprite_set_fade(sp, 0x80, -2, 0x14);
      sp->layer = 3;
    }
  }
  if (s->p12c == 1) {
    s->aux = static_cast<s16>(s->aux + g.frame_ticks);
    if (0x78 < s->aux) s->p12c = 2;
  } else if (s->p12c == 0) {
    s->p128 += g.frame_ticks * 4;
    if (0x7F < s->p128) {
      s->p128 = 0x80;
      s->p12c = 1;
    }
  } else if (s->p12c == 2) {
    s->p128 += g.frame_ticks * -2;
    if (s->p128 < 1) {
      if (trail != nullptr) trail->ending = 1;
      g_trail.erase(s);
      shot_kill(s);
    }
  }
}

void cure_all(SpellCast* sc) {
  BattleGlobals& g = bg();
  if ((g.flags & 4) != 0 || spellcast_timer_tick(sc) != 0) return;
  Unit* c = sc->caster;
  const int count = c->is_enemy != 0 ? 8 : 4;
  auto unit = [&](int i) -> Unit* {
    return c->is_enemy == 0 ? static_cast<Unit*>(g.party_units[static_cast<size_t>(i)])
                            : static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)]);
  };
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      spellcast_dim_begin(sc, c->is_enemy ^ 1);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, c->pos, 0xC00, 0xE3, 0x800, 0xC, 0x80, 5);
      spellcast_wait(sc, 0xF);
      return;
    case 1:
      if (sc->data != nullptr) return;
      bsfx_play_at(0x21, c);
      if (spellcast_leads_camera(sc)) camera_setup_overview(g.camera, 0x78, c->is_enemy);
      effect_spawn_sphere_at_default(c->pos, 10, 10, 0x18, 0x3C, 0xFF6020, 100, 0);
      ++sc->step;
      return;
    case 2:
      for (int i = 0; i < count; ++i) {
        Unit* u = unit(i);
        if (unit_is_down(u)) continue;
        s32 n = sc->power / 0x1E + 2;
        if (6 < n) n = 6;
        for (int k = 0; k < n; ++k) {
          Sprite* sp = spellcast_sprite_spawn(sc, 2);
          if (sp == nullptr) continue;
          const s32 dx = rng_between_fixed(u->size[0] * -2, u->size[0] << 1);
          const s32 dy = rng_between_fixed(u->size[1] * -3, u->size[1]);
          const s32 dz = rng_between_fixed(-u->size[2], u->size[2]);
          sprite_set_pos(sp, u->pos[0] + dx, u->pos[1] + dy, u->pos[2] + dz);
          sprite_set_fade(sp, 0x80, -1, 0x78);
          const s32 scale = rng_between(0x1000, 0x2000);
          sp->scale_y = scale;
          sp->scale_x = scale;
          sp->layer = 3;
        }
      }
      spellcast_wait(sc, 10);
      return;
    case 3: {
      if (Shot* s = shot_spawn_callback_always(descending_light)) {
        s->pos[0] = g.camera->lookat_dest[0];
        s->pos[1] = static_cast<s32>(0xFFD44000);
        s->pos[2] = g.camera->lookat_dest[2];
        s->scale_y = 0x800;
        s->scale_x = 0x800;
        g_trail.erase(s);
        bobj_share_sprite(&sc->obj, s);
        bsfx_play_at(0x29, s);
      }
      sound_handle_fade_out(bsfx_play_at(0x44, c), 0xF0, 0);
      spellcast_wait(sc, 0xB4);
      return;
    }
    case 4:
      bsfx_play_at(0x13, c);
      bsfx_play_at(0x16, c);
      bsfx_play_at(0x17, c);
      for (int i = 0; i < count; ++i) {
        Unit* u = unit(i);
        if (unit_is_down(u)) continue;
        if (u->st()->hp != 0) {
          const s32 heal = -(c->st()->intel[2] + sc->power + 0x4B0);
          damage_popup_spawn(u, heal, static_cast<s32>(0xFFFFE000), 0);
          unit_damage_hp(u, heal);
        }
        s32 n = sc->power / 0x1E + 0xC;
        if (0x14 < n) n = 0x14;
        for (int k = 0; k < n; ++k) {
          Sprite* sp = sprite_spawn_from(static_cast<int>(rng_range(5)) + 3, 0, g.sprite_bank_fx.get(), nullptr);
          if (sp == nullptr) continue;
          const s32 dx = rng_between_fixed(u->size[0] * -3, u->size[0] * 3);
          const s32 dz = rng_between_fixed(u->size[2] * -3, u->size[2] * 3);
          sprite_set_pos(sp, u->pos[0] + dx, u->pos[1] + u->size[1] * -0x4000, u->pos[2] + dz);
          sprite_set_fade(sp, 0x80, -2, 8);
          sp->scale_y = 0x1800;
          sp->scale_x = 0x1800;
          sp->vel[1] = rng_between_fixed(1, 3);
        }
      }
      ++sc->step;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      return;
    case 5:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 6:
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_cure_all(SpellCast* sc) { cure_all(sc); }

}  // namespace ss::game::battle
