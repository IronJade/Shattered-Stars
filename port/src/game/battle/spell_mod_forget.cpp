// Spell code module 68317e194a (entries 0x8e5 / 0x8e6): Forget (spell 0x11): a ring of the
// data's sprites and bursts of debris round the target's head; if it fails its resistance
// it gets status bit 2 and wisps rise off it for 60 ticks, else a rising icon. One function
// at 0x8008cc00 (block 0, 1160 bytes; its step jump table at 0x8008d070).

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the ticks of step 3, s24 the hum's handle;
// obj the data's sprite set (anim 1 the ring, 2-10 the wisps).

// 0x8008cc00: the handler (power: the roll's chance, power / 10 + 180; no dim, and no
// camera lead test). 0: flash, the camera on the target, wait 30. 1: power / 50 + 5 (at
// most 12) sprites turned round the target's chest and as many debris bursts near it, the
// camera, a hum, wait 40. 2: status bit 2 (+0x11c) unless it has it or resists: sfx 0x3a
// and on to 3; else a rising icon, sfx 11 and on to 4; the hum fades. 3: a wisp a frame
// for 60 ticks. 4: the effects end. 5: flash back, done.
void forget(SpellCast* sc) {
  BattleGlobals& g = bg();
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  Unit* t = sc->target;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x20, 0x20, 0x40);
      camera_move_to(g.camera, t->pos, 0xC00, 0x155, 0x800, 0x14, 0x80, 5);
      spellcast_wait(sc, 0x1E);
      return;
    case 1: {
      // (No test that the data is loaded: the sprites come from whatever the set has.)
      s32 n = sc->power / 0x32 + 5;
      if (0xC < n) n = 0xC;
      const s32 chest[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]};
      for (int i = 0; i < n; ++i) {
        Sprite* sp = sprite_spawn_from(1, 0, &sc->obj, nullptr);
        if (sp == nullptr) continue;
        sprite_set_pos_v(sp, chest);
        sprite_set_fade(sp, 0x80, -2, 0x1E);
        sp->layer = 8;
        sp->roll = static_cast<s16>((i << 12) / n);
      }
      for (int i = 0; i < n; ++i) {
        const Vec4 at = vec_random_around(chest, 300);
        const s32 spread[3] = {0x20000, 0x10000, 0x20000};
        effect_debris_burst(at.data(), spread, n * 2 + 2, 2, 0, 0x1800, 6);
      }
      camera_move_to(g.camera, t->pos, 0xC00, 0xAA, 0x800, 0x78, 0x80, 5);
      sc->s24 = static_cast<s32>(bsfx_play_ui(0x45));
      spellcast_wait(sc, 0x28);
      return;
    }
    case 2: {
      int sfx;
      if ((t->status_bits & 2) == 0 && t->resist_roll(1, sc->power / 10 + 0xB4) != 0) {  // [13]
        sfx = 0x3A;
        ++sc->step;
        t->status_bits |= 2;
      } else {
        sc->step += 2;
        effect_rise_icon(t, 0);  // 0x800308a0
        sfx = 0xB;
      }
      bsfx_play_ui(sfx);
      sound_handle_fade_out(static_cast<u32>(sc->s24), 0x3C, 0);
      return;
    }
    case 3: {
      if (0x3C < sc->s20) ++sc->step;
      sc->s20 += g.frame_ticks;
      Sprite* sp = sprite_spawn_from(rng_between(2, 0xB), 0, &sc->obj, nullptr);
      if (sp == nullptr) return;
      const s32 dx = rng_spread_fixed(t->size[0] * 5);
      const s32 dy = static_cast<s32>(rng_range(static_cast<u32>(t->size[1] << 3)));
      sprite_set_pos(sp, t->pos[0] + dx, t->pos[1] + dy * -0x1000, t->pos[2]);
      sprite_set_fade(sp, 0x80, -4, 4);
      sp->scale_vel[1] = 0x80;
      sp->scale_vel[0] = 0x80;
      sp->roll_speed = static_cast<s16>(rng_between(-0x1E, 0x1E));
      sp->layer = 2;
      return;
    }
    case 4:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 5:
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_forget(SpellCast* sc) { forget(sc); }

}  // namespace ss::game::battle
