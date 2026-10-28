// Spell code module e37e03cd9f (entries 0x94c-0x950): Dispel (spell 0x36): a glowing seal
// (the data's anim 3) spreads under the target while light gathers into it; then the
// target's statuses are cleared (status bits 0x2-0x8 of its record and every status flag)
// with a burst of light. One function at 0x8008cc00.

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/enemy_support.h"
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

// The SpellCast's fields: s20 the gathering's ticks (step 3), s30 the seal's fade (step 4);
// obj the data's sprite set (anim 3 the seal, grey +0x64 its brightness).

// Steps 2-4: the seal, anim 3's cel as an 8 x 8 patch (size x * 4 + 0x200) round the
// target, lifted 1 unit (and more as the camera looks down) above it; the target's shadow
// is hidden meanwhile.
void draw_seal(SpellCast* sc) {
  BObj* o = &sc->obj;
  o->anim = 3;
  bobj_anim_draw(o, 0x9800);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(o, tpage);
  Unit* t = sc->target;
  const s32 half = (t->size[0] * 4 + 0x200) * 0x1000;
  const s32 x0 = t->pos[0] - half, x1 = t->pos[0] + half;
  const s32 z0 = t->pos[2] - half, z1 = t->pos[2] + half;
  s32 lift = bsin(static_cast<u32>(view().pitch)) << 4;
  if (lift < 0) lift = 0;
  const s32 y = t->pos[1] - (lift + 0x10000);
  const s32 c[4][4] = {{x0, y, z0, 0}, {x1, y, z0, 0}, {x0, y, z1, 0}, {x1, y, z1, 0}};
  draw_textured_patch(uv, c, clut, tpage, 4, code, 8);
  t->shadow_scale = 0;
}

void dispel(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* t = sc->target;
  if (static_cast<u32>(sc->step - 2) < 3) draw_seal(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x30, 0x30, 0x30);
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = (t->facing[0] >> 31) & 0x800;
        camera_move_to(g.camera, t->pos, yaw, 0x11C, 0x800, 0x1E, 0x80, 5);
      }
      ++sc->step;
      return;
    case 1: {
      if (sc->data != nullptr) return;
      sc->obj.colour_mode = 2;
      sc->obj.rgb[0] = 0;
      ++sc->step;
      const u32 h = bsfx_play_at(0x40, t);
      sound_handle_fade_out(h, 0x3C, 0x5A);
      sound_handle_pitch_up(h, 0xB4);
      return;
    }
    case 2: {
      // The seal brightens 2 a tick; light (anim 2) flows into the target from a dome of
      // 0x200 round it.
      const u32 lit = static_cast<u32>(sc->obj.rgb[0]) + static_cast<u32>(g.frame_ticks * 2);
      sc->obj.rgb[0] = static_cast<u8>(lit);
      if (0x80 < (lit & 0xFF)) {
        sc->obj.rgb[0] = 0x80;
        ++sc->step;
        bsfx_play_at(0x34, t);
        if (spellcast_leads_camera(sc)) camera_move_to(g.camera, t->pos, 0xC00, 0xAA, 0x800, 0x5A, 0x80, 5);
      }
      for (int i = 0; i < g.frame_ticks; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, 2);
        if (sp == nullptr) continue;
        const u32 a = rng_range(0x1000);
        const u32 b = static_cast<u32>(rng_between(0, 0x400));
        const s32 x = ((bcos(a) * bcos(b)) >> 12) * 0x200 + t->pos[0];
        const s32 y = t->pos[1] + t->size[1] * -0x2000 + bsin(a) * -0x200;
        const s32 z = ((bcos(a) * bsin(b)) >> 12) * 0x200 + t->pos[2];
        sprite_set_pos(sp, x, y, z);
        const s32 mid[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]};
        const Vec4 v = vec_velocity_toward(sp->pos, mid, 0xC);
        sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
        sprite_set_fade(sp, 0x80, -6, 10);
        sp->layer = 3;
      }
      return;
    }
    case 3: {
      // Half the frames a mote (anim 1) on the ground and one rising (anim 3) round the
      // target; after 45 ticks the burst and the cure.
      if (static_cast<u32>(rng_next() & 0x7FFF) < 0x4000) {
        if (Sprite* sp = spellcast_sprite_spawn(sc, 1)) {
          const s32 dx = rng_between_fixed(t->size[0] * -4, t->size[0] << 2);
          const s32 dz = rng_between_fixed(t->size[2] * -4, t->size[2] << 2);
          sprite_set_pos(sp, t->pos[0] + dx, t->pos[1], t->pos[2] + dz);
          sprite_set_fade(sp, 0x80, -4, 0xC);
          sp->layer = 3;
        }
      }
      if (static_cast<u32>(rng_next() & 0x7FFF) < 0x4000) {
        if (Sprite* sp = spellcast_sprite_spawn(sc, 3)) {
          const u32 a = rng_range(0x1000);
          const s32 r = t->size[0] * 6;
          const s32 dz = rng_between_fixed(t->size[2] * -6, t->size[2] * 6);
          sprite_set_pos(sp, t->pos[0] + bcos(a) * r, t->pos[1] + bsin(a) * r, t->pos[2] + dz);
          sprite_set_fade(sp, 0x80, -4, 8);
          sp->vel[1] = rng_between_fixed(0, 0xC);
          sp->layer = 3;
          const s32 scale = rng_between(0x1000, 0x2000);
          sp->scale_y = scale;
          sp->scale_x = scale;
        }
      }
      sc->s20 += g.frame_ticks * 2;
      if (!(0x5A < sc->s20)) return;
      for (int i = 0; i < 0x18; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, 3);
        if (sp == nullptr) continue;
        const s32 mid[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]};
        sprite_set_pos_v(sp, mid);
        sprite_set_fade(sp, 0x80, -2, 8);
        const Vec4 v = vec_random_around(kZeroVector, 0x18);
        sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
        sp->layer = 3;
        const s32 scale = rng_between(0x1000, 0x2000);
        sp->scale_y = scale;
        sp->scale_x = scale;
      }
      const s32 ground[3] = {t->pos[0], bobj_ground_height(t), t->pos[2]};
      effect_spawn_sparks_at(ground, 0xC, 0, 0xF0, 0x10, 0x3C, 4, 0, t->size[0] << 2, 0);
      sound_handle_pitch_up(bsfx_play_at(0x28, t), 0x3C);
      bsfx_play_at(0x16, t);
      // The cure: every status flag, and the record's status bits 0x2 / 0x4 / 0x8; a unit
      // that was down and no longer is recovers and starts over (slots 21 and 12), as one
      // that had flags 0x48.
      const u32 had = t->status_bits;
      t->status_bits = 0;
      const bool was_down = unit_is_down(t);
      t->st()->status &= 0xF1;
      if (was_down && !unit_is_down(t)) {
        t->status_recovered();
        t->action_kind = 0;
        t->begin_action();
      }
      if (!unit_is_down(t) && (had & 0x48) != 0) t->status_recovered();
      ++sc->step;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      return;
    }
    case 4:
      // The seal fades over 64 ticks; the shadow is back.
      sc->obj.rgb[0] = static_cast<u8>(-0x80 - sc->s30);
      sc->s30 += g.frame_ticks * 2;
      if (0x7F < sc->s30) {
        ++sc->step;
        t->shadow_scale = 0x100;
      }
      return;
    case 5:
      if (spellcast_effects_gone(sc) == 0) return;
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_dispel(SpellCast* sc) { dispel(sc); }

}  // namespace ss::game::battle
