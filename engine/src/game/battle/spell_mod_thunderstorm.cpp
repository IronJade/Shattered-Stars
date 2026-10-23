// Spell code module 9f010f460d (entries 0x8f6 / 0x8f7): Thunderstorm (spell 0xe): for 180
// ticks lightning flashes over the field at random (power-scaled odds; the units turn
// semi-transparent while the flash lasts), then every standing unit of the other side in
// the arena takes a spark burst before the hit on all of them (element 0x10). One function
// at 0x8008cc00 (block 0, 2008 bytes; its step jump table at 0x8008d3c4).

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/spells.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the storm's ticks (step 1), s24 the flash's ticks left, s28 the
// thunder played for this flash; obj the data's sprite set (anims 5 / 6 the bolts, 1-4 the
// flares).

void thunderstorm(SpellCast* sc) {
  BattleGlobals& g = bg();
  UnitGlobals& ug = unit_globals();
  const u8 flags = g.flags;
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1];
  sc->centre[2] = t->pos[2];
  sc->eff_power = sc->caster->st()->intel[2] + sc->power / 2 + 700;
  if ((flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      battle_flash_set(sc, 0, 0, 0);
      if (spellcast_leads_camera(sc)) camera_setup_overview(g.camera, 0x1E, sc->caster->is_enemy ^ 1);
      spellcast_wait(sc, 0x1E);
      return;
    case 1: {
      // 180 ticks of storm: when no flash is on, one at odds (power * 10 + 0x1000) / 0x8000 a
      // frame (and one at the end): a shake, the stage lit to 0x180, 6 bolts round the look-at
      // point and 12 flares on screen. The slot-0 cast makes the units semi-transparent
      // while a flash lasts (20 ticks; thunder 10 ticks before its end).
      if (sc->data != nullptr) return;
      const s32 odds = sc->power * 10 + 0x1000;
      sc->s20 += g.frame_ticks;
      if ((sc->s24 == 0 && static_cast<u32>(rng_next() & 0x7FFF) < static_cast<u32>(odds)) || 0xB4 < sc->s20) {
        sc->s24 = 0x14;
        screen_shake_vertical(6, 2, 10);
        ViewState& v = view();
        v.stage_colour[2] = 0x180;
        v.stage_colour[1] = 0x180;
        v.stage_colour[0] = 0x180;
        g.flash_active = 1;
        g.flash_delay = 2;
        g.flash_speed = 0x10;
        for (int i = 0; i < 6; ++i) {
          Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(2)) + 5);
          if (sp == nullptr) continue;
          const s32 x = v.view_target[0] + rng_between_fixed(-600, 600);
          const s32 y = rng_between_fixed(-600, 0);
          const s32 z = v.view_target[2] + rng_between_fixed(-400, 400);
          sprite_set_pos(sp, x, y, z);
          sprite_set_fade(sp, 0x80, -2, 0xC);
          const s32 scale = rng_between(12000, 0x4000);
          sp->scale_y = scale;
          sp->scale_x = scale;
        }
        for (int i = 0; i < 0xC; ++i) {
          Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(4)) + 1);
          if (sp == nullptr) continue;
          const s32 x = rng_between(0x8C, 0xB4);
          const s32 y = rng_between(100, 0x7C);
          sprite_set_screen_pos(sp, static_cast<s16>(x), static_cast<s16>(y));
          sp->roll = static_cast<s16>(rng_range(0x1000));
          sp->depth = 10;
          sprite_set_fade(sp, 0x80, -2, 10);
          const s32 grow = rng_between(0x80, 0x200);
          sp->scale_vel[1] = grow;
          sp->scale_vel[0] = grow;
          const s32 scale = rng_between(0x1000, 0x1800);
          sp->scale_y = scale;
          sp->scale_x = scale;
          sp->roll_speed = static_cast<s16>(rng_between(10, 0x28));
        }
        sc->s28 = 0;
      }
      if (sc->s24 == 0) {
        if (sc->slot == 0) {
          if (ug.blend_mode == 2) ug.blend_mode = 0;
          if (ug.blend_mode == 1) ug.blend_mode = 2;
        }
      } else {
        sc->s24 -= g.frame_ticks;
        if (sc->s24 < 10 && sc->s28 == 0) {
          sc->s28 = 1;
          bsfx_play_ui(0x52);
        }
        if (sc->s24 < 0) sc->s24 = 0;
        if (sc->slot == 0) {
          ug.blend_mode = 1;
          ug.blend_alpha = 0;
        }
      }
      if (sc->s20 < 0xB5) return;
      ++sc->step;
      return;
    }
    case 2: {
      // A spark burst on every standing unit of the other side in the arena (none on combos
      // 1 / 2), through a temporary Shot (the console's heap Shot, the fx bank's sprite).
      if (1 < static_cast<u8>(sc->combo - 1)) {
        const int side = sc->combo == 3 ? sc->caster->is_enemy ^ 1 : sc->caster->is_enemy;
        Unit* list[8] = {};
        const int n = units_in_rect(sc, stage().bounds, list, side);
        Shot tmp;
        for (int i = 0; i < n; ++i) {
          Unit* u = list[i];
          const s32 feet[3] = {u->pos[0], bobj_ground_height(u), u->pos[2]};
          effect_spawn_sparks_at(feet, 0x10, 0x60, 0xC0, 0x14, 0x5A, 0, 1, 0x10, 0);
          const s32 chest[3] = {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2]};
          effect_spawn_sparks_at(chest, 0x18, 0x140, 0, 0x3C, 0x3C, 0, 3, 0, 0);
          tmp.target = u;
          tmp.power = static_cast<s16>(sc->power);
          bobj_share_sprite(g.sprite_bank_fx.get(), &tmp);
          shot_hit_burst(&tmp, 1);
        }
        screen_shake(4, 2, 2, 2, 0xC);
        bsfx_play_ui(0x4C);
      }
      ++sc->step;
      return;
    }
    case 3:
      if (sc->slot == 0) {
        if (ug.blend_mode == 2) ug.blend_mode = 0;
        if (ug.blend_mode == 1) ug.blend_mode = 2;
      }
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 4:
      if (sc->slot == 0 && ug.blend_mode == 2) ug.blend_mode = 0;
      spell_hit_all(sc, 0x10, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_thunderstorm(SpellCast* sc) { thunderstorm(sc); }

}  // namespace ss::game::battle
