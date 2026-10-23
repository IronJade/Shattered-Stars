// Spell code module d43a9b4c0f (entries 0x961-0x963): Neutral (spell 0x3f): three rings of
// light turn on the ground round the target, motes spiral up round it for 60 ticks and
// sparkles rise; then its status bits are cleared (a unit freed of bits 0x48 recovers).
// Functions 0x8008cc00 (the handler; its step jump table at 0x8008d3f4) and 0x8008d298 (a
// rising mote, a kind-0x21 callback).

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the sparkle timer (step 3), s24 the ticks of
// step 2, s28 the ticks of step 3; obj the data's sprite set (anim 0 the motes, 2 / 3
// sparkles, 3 the rings).

// 0x8008d298: a mote (anim 0 at scale 2) circling its unit (+0x114) at 6 times its width,
// turning 20 a tick (+0x128) and rising 4 a tick; after 80 ticks (+0x12c) a flash of the
// next anim and it goes.
void rising_mote(Shot* s) {
  BattleGlobals& g = bg();
  s->scale_y = 0x2000;
  s->scale_x = 0x2000;
  s->anim = 0;
  const Unit* t = s->target;
  const s32 r = t->size[0] * 6;
  s->pos[0] = t->pos[0] + bcos(static_cast<u32>(s->p128)) * r;
  s->pos[2] = t->pos[2] + bsin(static_cast<u32>(s->p128)) * r;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  s->pos[1] += g.frame_ticks * -0x4000;
  s->p128 = (s->p128 + g.frame_ticks * 0x14) & 0xFFF;
  s->p12c += g.frame_ticks;
  if (s->p12c < 0x51) return;
  if (Sprite* sp = shot_spawn_sprite(s, s->anim + 1)) {
    sprite_set_pos_v(sp, s->pos);
    sp->layer = 2;
    sprite_set_fade(sp, 0x80, -4, 0);
    sp->blend = 0x82;
  }
  shot_kill(s);
}

// 0x8008cc00: the handler (no power, no dim). 0: flash 0x30, the camera on the target, wait
// 30. 1 (data loaded): the camera, sounds, three spark rings (anim 3) turning on the
// ground round the target. 2: a mote a frame for 60 ticks. 3: three sparkles every 8
// ticks; after 60 ticks sounds, the target's status bits cleared, 32 sparks burst from it;
// still standing and freed of bits 0x48 it recovers ([21]); flash back. 4: the effects end,
// done.
void neutral(SpellCast* sc) {
  BattleGlobals& g = bg();
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  Unit* t = sc->target;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x30, 0x30, 0x30);
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0xB1C : 0xCE3;
        camera_move_to(g.camera, t->pos, yaw, 0x155, 0xA00, 0x14, 0x80, 5);
      }
      spellcast_wait(sc, 0x1E);
      return;
    case 1: {
      if (sc->data != nullptr) return;
      ++sc->step;
      camera_move_to(g.camera, t->pos, 0xC00, 0xAA, 0x800, 0x78, 0x80, 5);
      bsfx_play_at(0x21, t);
      bsfx_play_at(0x34, t);
      const u32 h = bsfx_play_at(0x3B, t);
      sound_handle_fade_out(h, 0x3C, 0x50);
      sound_handle_pitch_up(h, 0xB4);
      struct Ring {
        s32 p11c, p130, turn;
      };
      const Ring rings[3] = {{0xC0, 6, 0x22}, {0xE0, 3, -0xB}, {0x100, 2, 0x16}};
      for (int i = 0; i < 3; ++i) {
        const s32 at[3] = {t->pos[0], bobj_ground_height(t), t->pos[2]};
        const s32 p118 = i == 2 ? 0x40 : 0x30;
        Shot* r = effect_spawn_sparks_at(at, 10, p118, rings[i].p11c, rings[i].p130, 0x96, 1, 2, t->size[0] * 6, 0);
        if (r == nullptr) continue;
        r->vel[0] = rings[i].turn;
        r->anim = 3;
        bobj_share_sprite(&sc->obj, r);
      }
      return;
    }
    case 2:
      if (Shot* s = shot_spawn_callback_always(rising_mote)) {
        s->target = t;
        s->pos[0] = t->pos[0];
        s->pos[1] = t->pos[1];
        s->pos[2] = t->pos[2];
        s->p128 = static_cast<s32>(rng_range(0x1000));
        bobj_share_sprite(&sc->obj, s);
      }
      sc->s24 += g.frame_ticks;
      if (sc->s24 < 0x3D) return;
      ++sc->step;
      return;
    case 3:
      sc->s20 += g.frame_ticks;
      if (7 < sc->s20) {
        sc->s20 -= 8;
        for (int i = 0; i < 3; ++i) {
          Sprite* sp = spellcast_sprite_spawn(sc, 2);
          if (sp == nullptr) continue;
          const s32 dx = rng_between_fixed(t->size[0] * -6, t->size[0] * 6);
          const s32 dz = rng_between_fixed(t->size[2] * -4, t->size[2] << 2);
          sprite_set_pos(sp, t->pos[0] + dx, t->pos[1], t->pos[2] + dz);
          sprite_set_fade(sp, 0x80, -2, 0x10);
          sp->layer = 2;
          sp->scale_y = 0x1000;
          sp->scale_x = 0x1000;
        }
      }
      sc->s28 += g.frame_ticks;
      if (sc->s28 < 0x3D) return;
      {
        bsfx_play_at(0x22, t);
        bsfx_play_at(0x28, t);
        bsfx_play_at(0x30, t);
        bsfx_play_ui(10);
        ++sc->step;
        const u32 had = t->status_bits;
        t->status_bits = 0;
        for (int i = 0; i < 0x20; ++i) {
          Sprite* sp = spellcast_sprite_spawn(sc, 3);
          if (sp == nullptr) continue;
          const s32 at[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]};
          sprite_set_pos_v(sp, at);
          sp->layer = 2;
          sprite_set_fade(sp, 0x80, -4, 8);
          const Vec4 v = vec_random_around(kZeroVector, 8);
          sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
          sp->scale_y = 0x2000;
          sp->scale_x = 0x2000;
        }
        if (!unit_is_down(t) && (had & 0x48) != 0) t->status_recovered();  // [21]
        battle_flash_set(sc, 0x80, 0x80, 0x80);
      }
      return;
    case 4:
      if (spellcast_effects_gone(sc) == 0) return;
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_neutral(SpellCast* sc) { neutral(sc); }

}  // namespace ss::game::battle
