// Spell code module 2a4716a80a (entries 0x95e-0x960): Bless (spell 0x3e): light gathers at
// the target's feet, rings of light swell out of it and sparkles rise; then the target gets
// status bit 0x4000 (with a sparkle), or a rising icon if it already has it. Functions
// 0x8008cc00 (the handler; its step jump table at 0x8008d1d8) and 0x8008d100 (a swelling
// ring, a kind-0x21 callback).

#include "game/battle/items.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/shot.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the ticks of step 2, s24 the ticks of step
// 3, s28 the ring timer; obj the data's sprite set (anim 1 the ground light, 4 the rings,
// 3 the sparkles).

// 0x8008d100: a ring of light (anim 4, layer 3, drawn with the global sprite offset at
// 0x20 / 0x20) growing 0x38 a tick; after 80 ticks (+0x128) it goes.
void swelling_ring(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 4;
  s->layer = 3;
  sprite_offset_x() = 0x20;
  sprite_offset_y() = 0x20;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  sprite_offset_x() = 0;
  sprite_offset_y() = 0;
  if ((g.flags & 0xC) != 0) return;
  const s32 grow = g.frame_ticks * 0x38;
  s->scale_x += grow;
  s->p128 += g.frame_ticks;
  s->scale_y += grow;
  if (0x50 < s->p128) shot_kill(s);
}

// 0x8008cc00: the handler (no power, no dim). 0: flash, the camera on the target, wait 30.
// 1 (data loaded): the camera (no lead test), a rising hum. 2: a light at the target's feet
// every frame; from 30 ticks a ring every 12 ticks (sfx 0x31); at 90 sounds. 3: sparkles
// rise round the target for 60 ticks. 4: the status (bit 0x4000 of +0x11c) and a sparkle
// with sfx 10, or (had it) a rising icon with sfx 11; flash back; on into 5: the effects
// end. 6: done.
void bless(SpellCast* sc) {
  BattleGlobals& g = bg();
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  Unit* t = sc->target;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x38, 0x40, 0x48);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, t->pos, 0xC00, 0x18E, 0x960, 0x14, 0x80, 5);
      spellcast_wait(sc, 0x1E);
      return;
    case 1: {
      if (sc->data != nullptr) return;
      ++sc->step;
      camera_move_to(g.camera, t->pos, 0xC00, 0x93, 0x800, 0xB4, 0x80, 5);
      const u32 h = bsfx_play_at(0x2E, t);
      sound_handle_fade_out(h, 0x5A, 0xB4);
      sound_handle_pitch_up(h, 0x78);
      return;
    }
    case 2:
      if (Sprite* sp = spellcast_sprite_spawn(sc, 1)) {
        sp->facing[0] = rng_between_fixed(-4, 4);
        const s32 dx = rng_between_fixed(t->size[0] * -5, t->size[0] * 5);
        const s32 y = bobj_ground_height(t);
        sprite_set_pos(sp, t->pos[0] + dx, y, t->pos[2]);
        sprite_set_fade(sp, 0x80, -2, 0xC);
        sp->layer = 3;
      }
      sc->s20 += g.frame_ticks;
      if (0x1E < sc->s20) {
        sc->s28 += g.frame_ticks;
        if (0xB < sc->s28) {
          sc->s28 -= 0xC;
          bsfx_play_at(0x31, t);
          if (Shot* s = shot_spawn_callback_always(swelling_ring)) {
            s->pos[0] = t->pos[0];
            s->pos[1] = t->pos[1];
            s->pos[2] = t->pos[2];
            s->pos_w = t->pos_w;
            bobj_share_sprite(&sc->obj, s);
            s->scale_y = 0x200;
            s->scale_x = 0x200;
          }
        }
      }
      if (0x5A < sc->s20) {
        ++sc->step;
        bsfx_play_at(0x34, t);
        bsfx_play_at(0x21, t);
        bsfx_play_at(0x22, t);
      }
      return;
    case 3:
      for (int i = 0; i < g.frame_ticks; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, 3);
        if (sp == nullptr) continue;
        const s32 dx = rng_between_fixed(t->size[0] * -8, t->size[0] << 3);
        const s32 dy = rng_between_fixed(t->size[1] << 2, t->size[1] * 7);
        const s32 dz = rng_between_fixed(t->size[2] * -2, t->size[2] << 1);
        sprite_set_pos(sp, t->pos[0] + dx, t->pos[1] - dy, t->pos[2] + dz);
        sprite_set_fade(sp, 0x80, -2, 0xC);
        sp->vel[1] = rng_between_fixed(0, 4);
        const s32 scale = rng_between(0x1000, 0x2000);
        sp->scale_y = scale;
        sp->scale_x = scale;
        sp->layer = 3;
      }
      sc->s24 += g.frame_ticks;
      if (sc->s24 < 0x3D) return;
      ++sc->step;
      return;
    case 4: {
      int sfx;
      if ((t->status_bits & 0x4000) == 0) {
        item_fx_sparkle(t);
        sfx = 10;
        t->status_bits |= 0x4000;
      } else {
        effect_rise_icon(t, 0);  // 0x800308a0
        sfx = 0xB;
      }
      bsfx_play_ui(sfx);
      ++sc->step;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      [[fallthrough]];
    }
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

void spell_mod_bless(SpellCast* sc) { bless(sc); }

}  // namespace ss::game::battle
