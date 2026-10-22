// Spell code module c325548547 (entry 0x97f): Spicule (spell 0x44), Decus's boss spell. The
// caster rises out of sight inside a spinning cone of the data's sprite while a ring of his
// own sprites turns high above; the screen goes black, he falls back through an inverted
// cone into a double ring of the data's sprites, lands with a white flash, a shake, spark
// rings, a sphere and debris; the rings fade and every standing party member takes a
// physical hit (power 0x400, element 4). The module draws the caster itself in steps 1-5.
// One function at 0x8008cc00 (block 0, 5136 bytes; its step jump table at 0x8008dfe8).

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the cone (0 off, 1 apex up, 2 apex down);
// s24 the rings (0 off, 1 one ring 5000 up, 2 a ground ring and a bowl); s28 the rings'
// inner radius (0xc00 down to 0); s2c their turn; s30 the fades' once flag, then the rings'
// fade (step 7); s34 the rise speed (steps 2-3), then ticks (step 6); s38 the bowl's
// height (steps 6-7); obj the data's sprite set (anim 0, the cone); obj2 the rings (the
// caster's set until step 4, anims 0xc / 8; then the data's, anims 1 / 2).

// Steps 1-5: the caster drawn by the module (projected, its cel, a shadow as wide as its
// last frame's extents).
void draw_caster(Unit* u) {
  BattleGlobals& g = bg();
  bobj_project(u);
  bobj_anim_draw(u, g.flags & 0xC);
  s32 a = u->ext_x1, b = u->ext_x0;
  if (a < 0) a = -a;
  if (b < 0) b = -b;
  if (a < b) a = b;
  const s32 r = a * 4 + 0x20;
  bobj_draw_shadow(u, -r, r, -r, r, 0xFFFFFFFF);
}

// s20 != 0: a cone of obj's anim 0 round the caster's centre: 8 bands of 8 one-quad
// patches, band i from radius sqrt(i * 1024) * 400 / 128 at height i * 0x40 - 0x200 (the
// apex 0x200 above; s20 2 mirrors it below), each band a slice of the cel's height.
void draw_cone(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* c = sc->caster;
  BObj* o = &sc->obj;
  o->anim = 0;
  bobj_anim_draw(o, (g.flags & 0xC) | 0x9800u);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(o, tpage);
  s32 k[4][4] = {};
  for (int i = 0; i < 8; ++i) {
    const s32 r0 = square_root0((((i << 12) / 8) << 13) >> 12);
    const s32 r1 = square_root0(((((i + 1) << 12) / 8) << 13) >> 12);
    s32 y0 = i * 0x40 - 0x200, y1 = (i + 1) * 0x40 - 0x200;
    if (sc->s20 == 2) {
      y0 = -y0;
      y1 = -y1;
    }
    k[0][0] = ((r0 * 0x190000) >> 7) + c->pos[0];
    k[0][1] = y0 * 0x1000 + c->pos[1] - c->size[1] * 0x2000;
    k[1][1] = k[0][1];
    k[0][2] = c->pos[2];
    k[2][0] = ((r1 * 0x190000) >> 7) + c->pos[0];
    k[2][1] = y1 * 0x1000 + c->pos[1] - c->size[1] * 0x2000;
    k[3][1] = k[2][1];
    k[2][2] = c->pos[2];
    const s32 dv = uv[3] - uv[1];
    const s32 band[4] = {uv[0], (dv * i) / 8 + uv[1], uv[2], (dv * (i + 1)) / 8 + uv[1]};
    for (int j = 1; j <= 8; ++j) {
      k[1][0] = k[0][0];
      k[1][2] = k[0][2];
      k[3][0] = k[2][0];
      k[3][2] = k[2][2];
      const s32 ang = (j << 12) / 8;
      const s32 cs = bcos(static_cast<u32>(ang));
      const s32 sn = bsin(static_cast<u32>(ang));
      k[0][0] = ((cs * r0 * 400) >> 7) + c->pos[0];
      k[0][2] = ((sn * r0 * 400) >> 7) + c->pos[2];
      k[2][0] = ((cs * r1 * 400) >> 7) + c->pos[0];
      k[2][2] = ((sn * r1 * 400) >> 7) + c->pos[2];
      draw_textured_patch(band, k, clut, tpage, 0x10, code, 1);
    }
  }
}

// s24 != 0: s24 layers of obj2 round the caster's x / z, each 6 rings (radius from s28 to
// 0xc00) of 12 one-quad patches turned by s2c (the second layer the other way). Layer 0 is
// flat at y -0x64 (s24 1: -0x1388, anim 0xc for the inner ring, 8 for the others); layer 1
// (anim 2, the animation held) a bowl rising with s38: ring i at -0x64 - ((36 - i^2) * s38 / 2
// + 0x64). OT bias 8, then 12.
void draw_rings(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* c = sc->caster;
  BObj* o = &sc->obj2;
  const s32 y_base = sc->s24 == 1 ? -0x1388000 : -0x64000;
  const s32 cx = c->pos[0], cz = c->pos[2];
  s32 uv[4] = {};
  u16 tpage = 0, clut = 0;
  u32 code = 0;
  s32 bias = 8;
  s32 k[4][4] = {};
  for (s32 layer = 0; layer < sc->s24; ++layer, bias += 4) {
    for (int i = 0; i < 6; ++i) {
      s32 anim;
      if (sc->s24 == 1) {
        anim = i == 0 ? 0xC : 8;
      } else {
        anim = layer + 1;
      }
      o->anim = static_cast<u16>(anim);
      if (i == 0 || (i == 1 && sc->s24 == 1)) {
        bobj_anim_draw(o, layer == 0 ? ((g.flags & 0xC) | 0x9800u) : 0x9801u);
        const FrameUv& f = frame_uv();
        uv[0] = f.u0;
        uv[1] = f.v0;
        uv[2] = f.u1;
        uv[3] = f.v1;
        tpage = f.tpage;
        clut = f.clut;
        code = bobj_poly_tag(o, tpage);
      }
      const s32 r = sc->s28;
      const s32 r0 = r + (i * (0xC00 - r)) / 6;
      const s32 r1 = r + ((i + 1) * (0xC00 - r)) / 6;
      s32 y0 = y_base, y1 = y_base;
      if (layer != 0) {
        y0 = y_base - ((((0x24 - i * i) * sc->s38) / 2) * 0x1000 + 0x64000);
        y1 = y_base - ((((0x24 - (i + 1) * (i + 1)) * sc->s38) / 2) * 0x1000 + 0x64000);
      }
      for (int j = 0; j < 12; ++j) {
        const s32 turn = layer == 0 ? sc->s2c : -sc->s2c;
        const s32 a0 = (j << 12) / 12 + turn;
        const s32 a1 = ((j + 1) << 12) / 12 + turn;
        const s32 cos0 = bcos(static_cast<u32>(a0));
        const s32 sin0 = bsin(static_cast<u32>(a0));
        const s32 cos1 = bcos(static_cast<u32>(a1));
        const s32 sin1 = bsin(static_cast<u32>(a1));
        k[0][0] = cos0 * r0 + cx;
        k[0][1] = y0;
        k[0][2] = sin0 * r0 + cz;
        k[1][0] = cos1 * r0 + cx;
        k[1][1] = y0;
        k[1][2] = sin1 * r0 + cz;
        k[2][0] = cos0 * r1 + cx;
        k[2][1] = y1;
        k[2][2] = sin0 * r1 + cz;
        k[3][0] = cos1 * r1 + cx;
        k[3][1] = y1;
        k[3][2] = sin1 * r1 + cz;
        draw_textured_patch(uv, k, clut, tpage, bias, code, 1);
      }
    }
  }
}

// 0x8008cc00: the handler.
void spicule(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  if (static_cast<u32>(sc->step - 1) < 5) draw_caster(c);
  if (sc->s20 != 0) draw_cone(sc);
  if (sc->s24 != 0) draw_rings(sc);
  if ((g.flags & 0xC) != 0) return;
  // (looks like an original bug: the module never runs spellcast_timer_tick, so its three
  // spellcast_wait calls only advance the step; the waits never happen)
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x40, 0x30, 0x30);
      camera_move_to(g.camera, c->pos, 0xC00, 0x155, 0xC00, 0xF, 0x100, 5);
      spellcast_wait(sc, 0x14);
      return;
    case 1:
      if (sc->data != nullptr) return;
      c->anim = 6;
      spellcast_wait(sc, 8);
      bobj_share_sprite(c, &sc->obj2);
      sc->obj2.pool = g.prim_pool_a;
      return;
    case 2:
      sc->s20 = 1;
      c->anim = 7;
      sc->s34 = 0x20;
      sc->s24 = 1;
      bsfx_play_at(0x34, c);
      bsfx_play_at(0x21, c);
      sound_handle_fade_out(bsfx_play_at(0x44, c), 0xB4, 0x3C);
      sc->s28 = 0xC00;
      ++sc->step;
      if (Sprite* sp = unit_play_anim(c, 0x11)) {
        sprite_set_pos(sp, c->pos[0], -0x1388000, c->pos[2]);
        sprite_set_fade(sp, 0x80, -4, 0xF0);
        sp->layer = 5;
        sp->scale_vel[1] = 0x40;
        sp->scale_vel[0] = 0x40;
      }
      return;
    case 3: {
      // The rise: s34 units a tick; until he is 0xfa0 up (y >= -0xfa0) sparkles round him and
      // the camera looks at him; higher, a black fade and a voice once, then s34 runs down to
      // the next step (he slows to a stop).
      sc->s28 -= g.frame_ticks * 0x14;
      if (sc->s28 < 0) sc->s28 = 0;
      const s32 rise = g.frame_ticks * sc->s34;
      sc->s2c = (sc->s2c + g.frame_ticks * 10) & 0xFFF;
      c->pos[1] -= rise * 0x1000;
      if (-0xFA0000 <= c->pos[1]) {
        if (Sprite* sp = unit_play_anim_ex(c, 0x12, 1)) {
          const s32 centre[3] = {c->pos[0], c->pos[1] + c->size[1] * -0x2000, c->pos[2]};
          const Vec4 at = vec_random_around(centre, 0x140);
          sprite_set_pos_v(sp, at.data());
          sp->vel[1] = -0x1C000;
          sp->layer = 3;
        }
        if (Sprite* sp = unit_play_anim_ex(c, 0xE, 1)) {
          const s32 centre[3] = {c->pos[0], c->pos[1] + c->size[1] * -0x2000, c->pos[2]};
          const Vec4 at = vec_random_around(centre, 0x140);
          sprite_set_pos_v(sp, at.data());
          sp->layer = 3;
        }
        ViewState& v = view();
        v.cam_lookat[0] = c->pos[0];
        v.cam_lookat[1] = c->pos[1];
        v.cam_lookat[2] = c->pos[2];
        return;
      }
      if (sc->s30 == 0) {
        // (the console allocates the fade's 0x1c-byte task with 0x8001fd94 and builds it with
        // 0x8003c3b0; the port's screen_fade_create does both)
        screen_fade_create(0, 0x20, -8, 0x10);
        sc->s30 = 1;
        int voice = 0x4D2;
        if (rng_range(2) == 0) voice = static_cast<int>(rng_range(3)) + 0x4C7;
        voice_play_entry(voice, c);  // (third argument -1)
      }
      sc->s34 -= g.frame_ticks;
      if (-1 < sc->s34) return;
      sc->s30 = 0;
      ++sc->step;
      return;
    }
    case 4: {
      bsfx_play_at(0x38, c);
      sound_handle_fade_out(bsfx_play_at(0x44, c), 0x78, 0x3C);
      bobj_share_sprite(&sc->obj, &sc->obj2);
      sc->s20 = 2;
      sc->s24 = 2;
      sc->s28 = 0xC00;
      sc->s30 = 0;
      c->anim = 0x12;
      const s32 yaw = rng_range(2) != 0 ? 0x800 : 0;
      camera_move_to(g.camera, c->pos, yaw, 0, 0x800, 0, 0x80, 5);
      const s32 ground[3] = {c->pos[0], bobj_ground_height(c), c->pos[2]};
      camera_move_to(g.camera, ground, 0xC00, 0xE3, 0x1000, 0x78, 0x80, 5);
      spellcast_wait(sc, 0x18);
      return;
    }
    case 5: {
      // The fall: 0x1c units a tick; within 0x12c of the ground a white fade (once), within
      // 0x96 the landing.
      sc->s28 -= g.frame_ticks * 0x14;
      if (sc->s28 < 0) sc->s28 = 0;
      sc->s2c = (sc->s2c + g.frame_ticks * 0x12) & 0xFFF;
      c->pos[1] += g.frame_ticks * 0x1C000;
      const s32 ground = bobj_ground_height(c);
      if (ground - 0x12C000 < c->pos[1] && sc->s30 == 0) {
        screen_fade_create(0xFFFFFF, 0x20, 8, 8);  // (allocated as in step 3)
        sc->s30 = 1;
      }
      if (!(ground - 0x96000 < c->pos[1])) return;
      screen_shake2(6, 4, 3, 6, 0xB4);
      const s32 at[3] = {c->pos[0], -0x12C000, c->pos[2]};
      c->pos[1] = ground;
      ++sc->step;
      bsfx_play_ui(0x22);
      bsfx_play_ui(0x50);
      sound_handle_fade_out(bsfx_play_ui(0x2D), 0x1E, 0xF0);
      sound_handle_fade_out(bsfx_play_at(0x2D, c), 300, 0x1E);
      sc->s20 = 0;
      const s32 g1[3] = {c->pos[0], bobj_ground_height(c), c->pos[2]};
      const s32 yaw = rng_range(2) != 0 ? 0xA38 : 0xE38;
      camera_move_to(g.camera, g1, yaw, 0x1C7, 0x800, 0, 0x80, 5);
      const s32 g2[3] = {c->pos[0], bobj_ground_height(c), c->pos[2]};
      camera_move_to(g.camera, g2, 0xC00, 0x155, 0x2000, 0xD2, 0x80, 5);
      effect_spawn_sparks_at(at, 0x18, 0xF0, 0, 0x80, 0x5A, 0, 3, 0x80, 0);
      effect_spawn_sparks_at(at, 0x18, 0x168, 0, 0x18, 0xF0, 5, 3, 0x80, 0);
      effect_spawn_sphere_at_default(at, 8, 8, 0x1E, 0x50, 0x40FF, 0x80, 0);
      sc->s30 = 0;
      if (Sprite* sp = unit_play_anim_ex(c, 0xF, 1)) {
        sprite_set_pos(sp, c->pos[0], -0xC8000, c->pos[2]);
        sp->layer = 0xC;
      }
      for (int i = 0; i < 0x30; ++i) {
        Sprite* sp = spellcast_sprite_spawn2(sc, static_cast<int>(rng_range(2)) + 4, 1);
        if (sp == nullptr) continue;
        const Vec4 p = vec_random_in_disc_xz(c->pos, 0xC00);
        sprite_set_pos(sp, p.x, -0x12C000, p.z);
        sp->size[2] = static_cast<s16>(rng_range(0x10));
        sprite_set_fade(sp, 0x80, -4, 0xB4);
        const s32 s = rng_between(0x2000, 0x4000);
        sp->scale_y = s;
        sp->scale_x = s;
        sp->layer = 8;
      }
      return;
    }
    case 6:
      sc->s2c = (sc->s2c + g.frame_ticks * 10) & 0xFFF;
      sc->s34 += g.frame_ticks;
      sc->s38 += g.frame_ticks;
      if (sc->s34 < 0xB5) return;
      sc->obj2.colour_mode = 2;
      sc->s30 = 0;
      sc->obj2.rgb[0] = 0x80;
      ++sc->step;
      return;
    case 7:
      sc->s2c = (sc->s2c + g.frame_ticks * 10) & 0xFFF;
      sc->obj2.rgb[0] = static_cast<u8>(-0x80 - sc->s30);
      sc->s30 += g.frame_ticks * 4;
      sc->s38 += g.frame_ticks;
      if (sc->s30 < 0x80) return;
      sc->s24 = 0;
      ++sc->step;
      return;
    case 8:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 9: {
      // The hit: every standing party member, at its centre.
      Unit* list[8] = {};
      Vec4 points[8] = {};
      int n = 0;
      for (int i = 0; i < 4; ++i) {
        PartyUnit* u = g.party_units[static_cast<size_t>(i)];
        if (unit_is_down(u)) continue;
        list[n] = u;
        // (the point's w is stack garbage on the console)
        points[n] = Vec4{u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0};
        ++n;
      }
      if (n != 0) {
        unit_attack_targets(c, n, list, points, 0x400, 9999, 4);
        c->attack_recovery = 0;
      }
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      sc->finished = 1;
      ++sc->step;
      return;
    }
    default: return;
  }
}

}  // namespace

void spell_mod_spicule(SpellCast* sc) { spicule(sc); }

}  // namespace ss::game::battle
