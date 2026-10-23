// Spell code module 8e03dba0da (entries 0x951-0x953): Delay (spell 0x38): a clock (the data's
// models 0-2: the dial and two hands) appears above the target, spins, its hands run back
// while sparks circle the target, then it shatters and the target is slowed (Unit +0x11c
// bit 0x20) unless it resists (kind 1, power / 16 + 160). Functions 0x8008cc00 (the handler)
// and 0x8008d334 (a circling spark, a kind-0x21 callback); the jump table of the handler's
// steps at 0x8008d4e8. Resident 0x80084d48 item_fx_sparkle: items.h; 0x8005f30c model_shatter:
// shatter.h.

#include <unordered_map>

#include "game/battle/items.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/shatter.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the clock's scale (to 0x1800), s24 its spin (yaw), s28 / s2c
// the hands' angles, s30 the ticks of step 2, s34 the spin speed (step 3) then the ticks of
// step 4, s38 the hands' speed (step 4); obj the data's sprite set, its position the clock's
// (0x1c2 above the target).

// A spark's afterimage (Shot +0x134 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

// 0x8008d334: a spark (anim 2) circling its unit (+0x114) at 6 times its width (angle +0x128
// turning 0x28 a tick) and rising 5 a tick with a 6-copy afterimage; after 60 ticks (+0x12c)
// a fading sprite (anim 3) and it goes.
void circling_spark(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 2;
  const Unit* u = s->target;
  s->pos[0] = u->pos[0] + bcos(static_cast<u32>(s->p128)) * u->size[0] * 6;
  s->pos[2] = u->pos[2] + bsin(static_cast<u32>(s->p128)) * u->size[0] * 6;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr) trail = afterimage_create(s, 6, 0x5030, 2);
  s->p12c += g.frame_ticks;
  s->pos[1] += g.frame_ticks * -0x5000;
  if (s->p12c < 0x3D) {
    s->p128 = (s->p128 + g.frame_ticks * 0x28) & 0xFFF;
    return;
  }
  if (Sprite* sp = shot_spawn_sprite(s, s->anim + 1)) {
    sprite_set_pos_v(sp, s->pos);
    sprite_set_fade(sp, 0x80, -4, 0);
  }
  if (trail != nullptr) trail->ending = 1;
  g_trail.erase(s);
  shot_kill(s);
}

// Steps 2-4: the clock at obj's position, lit and spun by s24: the dial (model 0, OT bias 4)
// and the hands (models 1 / 2, turned s28 / s2c, biases 8 / 6), back-face culled.
void draw_clock(SpellCast* sc) {
  stage_colour_set_grey(0x100);
  s32 orient[3] = {0, -sc->s24, 0};
  const s32 scale[3] = {sc->s20, sc->s20, sc->s20};
  const AnimSet* set = sc->obj.animset;
  const u16 clut = set != nullptr ? set->clut[1] : 0;
  const u16 tpage = set != nullptr ? set->tpage[1] : 0;
  const s32 bias[3] = {4, 8, 6};
  for (int k = 0; k < 3; ++k) {
    if (k == 1) orient[0] = sc->s28;
    if (k == 2) orient[0] = sc->s2c;
    lights_build_oriented(orient);
    obj_set_matrix(sc->obj.pos, orient, scale);
    obj_set_matrix(sc->obj.pos, orient, scale);
    if (const u8* m = spellcast_model(sc, k)) model_draw_culled(m, clut, tpage, bias[k]);
  }
  stage_colour_restore();
}

void delay(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* t = sc->target;
  if (static_cast<u32>(sc->step - 2) < 3) draw_clock(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  const s32 ticks = g.frame_ticks;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x50, 0x30, 0x60);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, t->pos, 0xC00, 0x71, 0x800, 0x1E, 0x80, 5);
      ++sc->step;
      return;
    case 1:
      // The clock 0x1c2 above the target; sfx 0x32 / 0x53 / 0x26.
      if (sc->data != nullptr) return;
      ++sc->step;
      sc->obj.pos[0] = t->pos[0];
      sc->obj.pos[1] = t->pos[1] + -0x1C2000;
      sc->obj.pos[2] = t->pos[2];
      bsfx_play_at(0x32, &sc->obj);
      bsfx_play_at(0x53, &sc->obj);
      bsfx_play_at(0x26, &sc->obj);
      return;
    case 2:
      // It grows (0x100 a tick to 0x1800) and spins for 60 ticks.
      sc->s20 += g.frame_ticks * 0x100;
      if (0x1800 < sc->s20) sc->s20 = 0x1800;
      sc->s24 = (sc->s24 + g.frame_ticks * 0x3C) & 0xFFF;
      sc->s30 += g.frame_ticks;
      if (sc->s30 < 0x3D) return;
      sc->s34 = 0x3C;
      ++sc->step;
      return;
    case 3:
      // The spin slows past 0x9c4 and stops at a full turn: the data's clip, camera.
      sc->s24 += sc->s34 * g.frame_ticks;
      if (0x9C4 < sc->s24) sc->s34 -= ticks;
      if (!(0xFFF < sc->s24)) return;
      sc->s34 = 0;
      sc->s38 = 0x100;
      ++sc->step;
      if (!sc->raw.empty()) bsfx_play(0, sc->raw.data(), &sc->obj, 1);
      camera_move_to(g.camera, t->pos, 0xC00, 0x155, 0x800, 0x3C, 0x80, 5);
      return;
    case 4:
      // The hands run back (ever slower) with a circling spark a frame; after 90 ticks the
      // clock shatters into 40 sprites (anim 4).
      sc->s28 -= sc->s38 * g.frame_ticks;
      sc->s38 += ticks * -2;
      sc->s2c = sc->s28 / 5;
      if (Shot* s = shot_spawn_callback_always(circling_spark)) {
        g_trail.erase(s);
        s->target = t;
        s->pos[1] = t->pos[1];
        s->p128 = static_cast<s32>(rng_range(0x1000));
        const s32 scale = rng_between(0x1000, 0x2000);
        s->scale_y = scale;
        s->scale_x = scale;
        bobj_share_sprite(&sc->obj, s);
      }
      sc->s34 += g.frame_ticks;
      if (!(0x5A < sc->s34)) return;
      bsfx_play_at(0x21, &sc->obj);
      bsfx_play_at(0x30, &sc->obj);
      ++sc->step;
      stage_colour_set_grey(0x100);
      {
        const s32 scale[3] = {0x1800, 0x1800, 0x1800};
        const AnimSet* set = sc->obj.animset;
        model_shatter(spellcast_model(sc, 0), sc->obj.pos, kZeroVector, scale, set != nullptr ? set->clut[1] : 0,
                      set != nullptr ? set->tpage[1] : 0, 2, 0, 4, 0, 0x10);
      }
      stage_colour_restore();
      for (int i = 0; i < 0x28; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, 4);
        if (sp == nullptr) continue;
        const s32 dx = rng_between_fixed(t->size[0] * -7, t->size[0] * 7);
        const s32 dy = rng_between_fixed(t->size[1] * -7, t->size[1] * -4);
        // ORIGINAL BUG (original_bugs.md S16): both bounds are -4 times the depth, so the z
        // spread is fixed (and rng_between makes no draw):
        //   const s32 dz = rng_between_fixed(t->size[2] * -4, t->size[2] * -4);
        // Fixed (user, 2026-10-22): -4 .. 4 times the depth from the visual-only random
        // source, so the game's RNG sequence stays the original's.
        const s32 dz = visual_rng_between_fixed(t->size[2] * -4, t->size[2] * 4);
        sprite_set_pos(sp, t->pos[0] + dx, t->pos[1] + dy, t->pos[2] + dz);
        sprite_set_fade(sp, 0x80, -2, 8);
        sp->vel[1] = rng_between_fixed(0, 2);
        const s32 scale = rng_between(0x1000, 0x2800);
        sp->scale_y = scale;
        sp->scale_x = scale;
      }
      return;
    case 5: {
      // The status (not when it is already slowed or the roll fails: the plain icon, sfx 11).
      bool slowed = false;
      if ((t->status_bits & 0x20) == 0 && t->resist_roll(1, sc->power / 16 + 0xA0) != 0) {
        item_fx_sparkle(t);
        t->status_bits |= 0x20;
        slowed = true;
      }
      if (!slowed) effect_rise_icon(t, 0);
      bsfx_play_ui(slowed ? 10 : 0xB);
      ++sc->step;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      [[fallthrough]];
    }
    case 6:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 7:
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_delay(SpellCast* sc) { delay(sc); }

}  // namespace ss::game::battle
