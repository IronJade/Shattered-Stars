// Spell code module 2ecc4bc085 (entries 0x8f3-0x8f5): Silence (spell 0x3c): a sign (the
// cast's sprite) rises at the caster and bursts into notes; the music fades down and every
// unit of the other side that fails a resist roll is silenced (status bit 4), a model
// rising from it. Functions 0x8008cc00 (the handler) and 0x8008d0a8 (the rising model, a
// kind-0x21 callback).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"
#include "game/boot/sound.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the sign's ticks (step 1); obj the data's sprite set and the
// sign (anim 1 rising, 0 bursting; drawn from step 1 on, layer -1).

// A rising model's model (Shot +0x128 holds the pointer on the console).
std::unordered_map<const Shot*, const u8*> g_model;

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

// 0x8008d0a8: the data's model 0 over a silenced unit, rising 4 units a tick and turning
// 0xa000 / 360 a tick; after 40 ticks (+0x130) it fades out (shot_fade_out).
void rising_model(Shot* s) {
  BattleGlobals& g = bg();
  s32 orient[3] = {0, s->p134, 0};
  shot_fade_out(s);
  stage_colour_set_grey(s->colour_mode != 0 ? s->rgb[0] : 0x80);
  lights_build_oriented(orient);
  obj_set_matrix(s->pos, orient, kUnitScale);
  const auto it = g_model.find(s);
  if (it != g_model.end() && it->second != nullptr) model_draw(it->second, g.sprite_bank_fx->animset->clut[0], 0x1A, 3);
  stage_colour_restore();
  if ((g.flags & 4) != 0) return;
  s->vel[1] = -0x4000;
  bobj_move(s, s->vel);
  s->p130 += g.frame_ticks;
  s->p134 += (g.frame_ticks * 0xA000) / 0x168;
  if (0x28 < s->p130 && s->p12c == 0) s->p12c = 1;
}

void silence(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  if (0 < sc->step) {
    sc->obj.layer = -1;
    bobj_project(&sc->obj);
    bobj_anim_draw(&sc->obj, 0);
  }
  switch (sc->step) {
    case 0: {
      // The camera on the caster facing the target; the sign at the caster.
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      camera_set_mode(g.camera, 7, c);
      camera_set_focus_unit(g.camera, c, sc->target);
      spellcast_wait(sc, 0x1E);
      sc->obj.pos[0] = c->pos[0];
      // Original quirk (original_bugs.md S14, to decide): y - (y << 14) + 3 units: the caster's y where its height
      // (size y, +0x54) seems meant (y - height * 4 units + 3); units on the ground have y 0.
      const u32 y = static_cast<u32>(c->pos[1]);
      sc->obj.pos[1] = static_cast<s32>(y - (y << 14) + 0x30000u);
      sc->obj.anim = 1;
      sc->obj.pos[2] = c->pos[2];
      bsfx_play_at(0x32, c);
      return;
    }
    case 1:
      // It rises 1 unit a tick for 32 ticks.
      sc->s20 += g.frame_ticks;
      sc->obj.pos[1] += g.frame_ticks * -0x1000;
      if (sc->s20 < 0x21) return;
      ++sc->step;
      return;
    case 2: {
      // The burst: power / 16 + 48 (max 96) notes (anims 3-22) flying out of the caster's
      // head; the music fades down.
      sc->obj.anim = 0;
      s32 n = sc->power;
      if (n < 0) n += 0xF;
      n = (n >> 4) + 0x30;
      if (0x60 < n) n = 0x60;
      for (int i = 0; i < n; ++i) {
        Sprite* sp = sprite_spawn_from(rng_between(3, 0x17), 0, &sc->obj, nullptr);
        if (sp == nullptr) continue;
        sprite_set_pos(sp, c->pos[0], c->pos[1] + c->size[1] * -0x4000, c->pos[2]);
        const u32 a = rng_range(0x1000);
        sp->vel[0] = bcos(a) * 0xC;
        sp->vel[1] = rng_between(-0xC000, 0x8000);
        sp->vel[2] = bsin(a) * 0xC;
        sprite_set_fade(sp, 0x80, -2, 0x1E);
      }
      bsfx_play_ui(0x28);
      sound_pause(g.sound, 0x40, 0x1E);
      ++sc->step;
      return;
    }
    case 3: {
      // The effects gone: the overview; each standing unit of the other side not yet silent
      // that fails resist_roll(1, power / 14 + 130) is silenced with a rising model; the
      // others get a miss marker.
      if (spellcast_effects_gone(sc) == 0) return;
      camera_setup_overview(g.camera, 0x1E, c->is_enemy ^ 1);
      bool any = false;
      const int count = c->is_enemy != 0 ? 4 : 8;
      const s32 chance = sc->power / 0xE + 0x82;
      for (int i = 0; i < count; ++i) {
        Unit* u = c->is_enemy == 0 ? static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)])
                                   : static_cast<Unit*>(g.party_units[static_cast<size_t>(i)]);
        if (unit_is_down(u)) continue;
        if ((u->status_bits & 4) == 0 && u->resist_roll(1, chance) != 0) {
          u->status_bits |= 4;
          Shot* s = shot_spawn_callback_always(rising_model);
          any = true;
          if (s != nullptr) {
            s->pos[0] = u->pos[0];
            s->pos[1] = u->pos[1] + u->size[1] * -0x4000;
            s->pos[2] = u->pos[2];
            g_model[s] = spellcast_model(sc, 0);
          }
        } else {
          effect_rise_icon(u, 0);
        }
      }
      bsfx_play_ui(any ? 10 : 0xB);
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      ++sc->step;
      return;
    }
    case 4:
      if (shot_kind21_absent(rising_model) == 0) return;
      sound_resume(g.sound, 0x1E);
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_silence(SpellCast* sc) { silence(sc); }

}  // namespace ss::game::battle
