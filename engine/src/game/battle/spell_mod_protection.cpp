// Spell code module 2acd939273 (entries 0x949-0x94b): Protection (spell 0x3a): a shield (the
// data's model, scale 8) descends spinning onto the target, slows and bursts in a spray of
// light; the target gets the status bit 0x400 (a miss marker if it already has it). One
// function at 0x8008cc00.

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s24 the shield's turn, s2c its spin speed (step 3); obj the data's
// sprite set (anims 1 / 2 the burst) and the shield's position.

// One of the fx bank's sparkles: 0x2a, or 0x30 / 0x31.
Sprite* fx_sparkle() {
  const int anim = rng_range(2) != 0 ? 0x2A : static_cast<int>(rng_range(2)) + 0x30;
  return sprite_spawn_from(anim, 0, bg().sprite_bank_fx.get(), nullptr);
}

// Steps 2-3: the shield, the data's model 0 (scale 8, unlit grey 0x100) at obj's position
// turned by s24.
void draw_shield(SpellCast* sc) {
  stage_colour_set_grey(0x100);
  s32 orient[3] = {0, sc->s24, 0};
  const s32 scale[3] = {0x8000, 0x8000, 0x8000};
  lights_build_oriented(orient);
  obj_set_matrix(sc->obj.pos, orient, scale);
  obj_set_matrix(sc->obj.pos, orient, scale);
  if (const u8* m = spellcast_model(sc, 0)) model_draw(m, sc->obj.animset->clut[0], sc->obj.animset->tpage[0], 6);
  stage_colour_restore();
}

void protection(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* t = sc->target;
  if (static_cast<u32>(sc->step - 2) < 2) draw_shield(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) {
        const s32 above[3] = {t->pos[0], t->pos[1] - 0x2BC000, t->pos[2]};
        camera_move_to(g.camera, above, 0xC00, 0x71, 0xC00, 10, 0x80, 5);
      }
      ++sc->step;
      return;
    case 1: {
      // The shield 0x514 above the target; the camera; its hum.
      if (sc->data != nullptr) return;
      ++sc->step;
      sc->obj.pos[0] = t->pos[0];
      sc->obj.pos[1] = t->pos[1] - 0x514000;
      sc->obj.pos[2] = t->pos[2];
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, t->pos, 0xC00, 0xE3, 0x898, 0x6E, 0x80, 5);
      }
      u32 h = bsfx_play_at(0x44, &sc->obj);
      sound_handle_fade_out(h, 0x78, 0);
      sound_handle_pitch_down(h, 0x78);
      h = bsfx_play_at(0x2E, &sc->obj);
      sound_handle_fade_out(h, 0x1E, 0);
      return;
    }
    case 2:
      // It sinks 10 units a tick turning 30, sparkles round it, to 0x1c2 above the target.
      sc->s24 = (sc->s24 + g.frame_ticks * 0x1E) & 0xFFF;
      sc->obj.pos[1] += g.frame_ticks * 0xA000;
      if (t->pos[1] - 0x1C2000 < sc->obj.pos[1]) ++sc->step;
      for (int i = 0; i < g.frame_ticks << 1; ++i) {
        Sprite* sp = fx_sparkle();
        if (sp == nullptr) continue;
        const Vec4 p = vec_random_around(sc->obj.pos, rng_between(0x32, 300));
        sprite_set_pos_v(sp, p.data());
        sprite_set_fade(sp, 0x80, -2, 8);
        sp->vel[1] = 0x2000;
      }
      sc->s2c = 0x3C;
      return;
    case 3: {
      // It spins s2c / 2 a tick, slowing past 0xce4; after a full turn the burst: sfx, a
      // flash sprite (anim 1) on the target, 48 streaks and 64 sparks from a point near it.
      sc->s24 += (sc->s2c * g.frame_ticks) / 2;
      if (0xCE4 < sc->s24) sc->s2c -= g.frame_ticks;
      if (sc->s24 < 0x1000) return;
      sc->s24 = 0;
      bsfx_play_at(0x28, t);
      bsfx_play_at(0x21, t);
      bsfx_play_at(0x26, t);
      spellcast_wait(sc, 0x1E);
      if (Sprite* sp = spellcast_sprite_spawn(sc, 1)) {
        const s32 head[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x4000, t->pos[2]};
        sprite_set_pos_v(sp, head);
        sprite_set_fade(sp, 0x80, -2, 0x1E);
        sp->roll_speed = 0x71;
      }
      const Vec4 from = vec_random_around(sc->obj.pos, rng_between(0x32, 300));
      for (int i = 0; i < 0x30; ++i) {
        const Vec4 to = vec_random_around(t->pos, 1000);
        particle_spawn_homing_line(from.data(), to.data(), 0x301000, 0xFF8000, 0xA0, 0x14000, 0);
      }
      for (int i = 0; i < 0x40; ++i) {
        Sprite* sp = rng_range(2) == 0 ? spellcast_sprite_spawn(sc, 2) : fx_sparkle();
        if (sp == nullptr) continue;
        sprite_set_pos_v(sp, from.data());
        sprite_set_fade(sp, 0x80, -2, 8);
        const Vec4 v = vec_random_around(kZeroVector, rng_between(0x10, 0x28));
        sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
        const s32 scale = rng_between(0x1000, 0x2000);
        sp->scale_y = scale;
        sp->scale_x = scale;
      }
      return;
    }
    case 4:
      // The status (no sound for the miss).
      if ((t->status_bits & 0x400) == 0) {
        effect_rise_icon(t, 3);
        t->status_bits |= 0x400;
        bsfx_play_ui(10);
      } else {
        effect_rise_icon(t, 0);
      }
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      ++sc->step;
      [[fallthrough]];
    case 5:
      if (spellcast_effects_gone(sc) == 0) return;
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_protection(SpellCast* sc) { protection(sc); }

}  // namespace ss::game::battle
