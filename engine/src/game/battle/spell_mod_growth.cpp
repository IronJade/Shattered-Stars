// Spell code module 44645f977c (entries 0x943-0x945): Growth (spell 0x39): a sphere of light
// (the data's anim 8) swells round the target while sparks spiral up it and the target
// grows; the sphere bursts and fades, the target shrinks back, and it gets the status bit
// 0x1000 (a miss marker if it already has it). One function at 0x8008cc00.

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

// The SpellCast's fields: s20 the sphere's radius (units), s24 its fade (step 3), s34 /
// s38 the target's scale x / y before; obj the data's sprite set (anim 8 the sphere, 7 the
// sparks' quads, 1-6 the motes).

// Steps 2 on: the sphere, anim 8's cel on 10 x 10 one-quad patches of a sphere of radius r
// round the target's upper body.
void draw_sphere_shell(SpellCast* sc) {
  BObj* o = &sc->obj;
  const s32 r = sc->s20;
  o->anim = 8;
  bobj_anim_draw(o, (bg().flags & 0xC) | 0x9800);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(o, tpage);
  const Unit* t = sc->target;
  const s32 centre[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x3000, t->pos[2]};
  s32 lat_next = 0x800;
  for (int i = 0; i < 10; ++i, lat_next += 0x800) {
    for (int j = 0; j < 10;) {
      const s32 lat0 = (i << 11) / 10 - 0x400;
      const s32 c0 = bcos(static_cast<u32>(lat0)), s0 = bsin(static_cast<u32>(lat0));
      const s32 lon0 = (j << 12) / 10;
      const s32 cl0 = bcos(static_cast<u32>(lon0)), sl0 = bsin(static_cast<u32>(lon0));
      const s32 lat1 = lat_next / 10 - 0x400;
      const s32 c1 = bcos(static_cast<u32>(lat1)), s1 = bsin(static_cast<u32>(lat1));
      ++j;
      const s32 lon1 = (j << 12) / 10;
      const s32 cl1 = bcos(static_cast<u32>(lon1)), sl1 = bsin(static_cast<u32>(lon1));
      const s32 y0 = -s0 * r + centre[1], y1 = -s1 * r + centre[1];
      const s32 c[4][4] = {{((c0 * cl0) >> 12) * r + centre[0], y0, ((c0 * sl0) >> 12) * r + centre[2], 0},
                           {((c0 * cl1) >> 12) * r + centre[0], y0, ((c0 * sl1) >> 12) * r + centre[2], 0},
                           {((c1 * cl0) >> 12) * r + centre[0], y1, ((c1 * sl0) >> 12) * r + centre[2], 0},
                           {((c1 * cl1) >> 12) * r + centre[0], y1, ((c1 * sl1) >> 12) * r + centre[2], 0}};
      draw_textured_patch(uv, c, clut, tpage, 8, code, 1);
    }
  }
}

// Steps 3-4: the target's scale shrinks 0x80 a tick back to what it was.
void shrink_back(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* t = sc->target;
  t->scale_x += g.frame_ticks * -0x80;
  if (t->scale_x < sc->s34) t->scale_x = sc->s34;
  t->scale_y += g.frame_ticks * -0x80;
  if (t->scale_y < sc->s38) t->scale_y = sc->s38;
}

void growth(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* t = sc->target;
  if (1 < sc->step) draw_sphere_shell(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, t->pos, 0xC00, 0x11C, 0x800, 10, 0x80, 5);
      ++sc->step;
      return;
    case 1: {
      if (sc->data != nullptr) return;
      ++sc->step;
      sc->s34 = t->scale_x;
      sc->s38 = t->scale_y;
      u32 h = bsfx_play_at(0x45, t);
      sound_handle_fade_out(h, 0x3C, 0x96);
      sound_handle_pitch_up(h, 0x96);
      h = bsfx_play_at(0x46, t);
      sound_handle_fade_out(h, 0x3C, 0x96);
      sound_handle_pitch_up(h, 0xB4);
      camera_reset_mode(g.camera);
      camera_move_to(g.camera, t->pos, 0xC00, 0xE3, 0xA00, 0x78, 0x80, 5);
      return;
    }
    case 2: {
      // A spark ring rising round the target at a random height, motes orbiting it; the
      // sphere grows 4 a tick and the target 0x40; past 600 a burst of motes.
      const s32 rise = static_cast<s32>(rng_range(static_cast<u32>(t->size[1] << 14)));
      const s32 p[3] = {t->pos[0], t->pos[1] - rise, t->pos[2]};
      const s32 spin = rng_between(4, 0xC);
      if (Shot* r = effect_spawn_sparks_at(p, 8, 0x78, 0, spin, 0x2D, 1, 2, t->size[0] * 4 + sc->s20 / 2, 0)) {
        r->anim = 7;
        r->vel[0] = -0x2D;
        bobj_share_sprite(&sc->obj, r);
      }
      for (int i = 0; i < g.frame_ticks; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(6)) + 1);
        if (sp == nullptr) continue;
        sprite_set_pos_v(sp, p);
        sprite_set_fade(sp, 0x80, -2, 8);
        const s32 rad = rng_between(t->size[0] << 2, t->size[0] * 6) + sc->s20 / 2;
        const s32 yaw = static_cast<s32>(rng_range(0x1000));
        const s32 grow = rng_between(2, 6);
        sprite_set_orbit(sp, yaw, 0, rad, 0, rad, 0x2D, 0, grow);
        const s32 scale = rng_between(0x1000, 0x2000);
        sp->scale_y = scale;
        sp->scale_x = scale;
        sp->layer = 2;
      }
      sc->s20 += g.frame_ticks * 4;
      t->scale_x += g.frame_ticks * 0x40;
      t->scale_y += g.frame_ticks * 0x40;
      if (!(600 < sc->s20)) return;
      ++sc->step;
      bsfx_play_at(0x28, t);
      bsfx_play_at(0x29, t);
      bsfx_play_at(0x22, t);
      for (int i = 0; i < 0x18; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(6)) + 1);
        if (sp == nullptr) continue;
        const s32 mid[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]};
        sprite_set_pos_v(sp, mid);
        sprite_set_fade(sp, 0x80, -4, 8);
        const Vec4 v = vec_random_around(kZeroVector, 0x18);
        sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
        sp->layer = 3;
        const s32 scale = rng_between(0x1000, 0x2000);
        sp->scale_y = scale;
        sp->scale_x = scale;
      }
      return;
    }
    case 3:
      // The sphere swells 0x20 a tick and fades over 64; then the status.
      shrink_back(sc);
      sc->obj.colour_mode = 2;
      sc->s20 += g.frame_ticks * 0x20;
      sc->obj.rgb[0] = static_cast<u8>(-0x80 - sc->s24);
      sc->s24 += g.frame_ticks * 2;
      if (sc->s24 < 0x80) return;
      camera_move_to(g.camera, t->pos, 0xC00, 0x93, 0x800, 0x3C, 0x80, 5);
      ++sc->step;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      if ((t->status_bits & 0x1000) == 0) {
        effect_rise_icon(t, 1);
        t->status_bits |= 0x1000;
        bsfx_play_ui(10);
      } else {
        effect_rise_icon(t, 0);
        bsfx_play_ui(0xB);
      }
      return;
    case 4:
      shrink_back(sc);
      if (spellcast_effects_gone(sc) == 0) return;
      if (t->scale_x != sc->s34 || t->scale_y != sc->s38) return;
      ++sc->step;
      return;
    case 5:
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_growth(SpellCast* sc) { growth(sc); }

}  // namespace ss::game::battle
